#include "s_skills.h"

/* ANto is a channeled summon, not an area-damage spell on the Sea Witch.
 * ntor owns Atdg, Atsp, Atwa and Aasl in UnitAbilities.slk. Simulation and
 * animation are separate: the spawned unit supplies its model, while this
 * channel thinker owns expiry and periodic actions. */
#define TORNADO MAKEFOURCC('A','N','t','o')
#define TORNADO_DAMAGE MAKEFOURCC('A','t','d','g')
#define TORNADO_SPIN MAKEFOURCC('A','t','s','p')
#define TORNADO_MARK MAKEFOURCC('T','s','p','M')
#define TORNADO_DAMAGE_INTERVAL_MS 1000u
#define TORNADO_SPIN_LIFT_DURATION_SECONDS 10.0f
#define TORNADO_SPIN_LIFT_HEIGHT 200.0f

static uint32_t tornado_spin_interval_ms(edict_t *tornado) {
    abilityAliasRef_t ability = S_ResolveAbilityAlias(tornado, TORNADO_SPIN);
    float const seconds = ability.alias ? S_SpellData(ability.alias, ability.level, 2) : 0.0f;
    return seconds > 0.0f ? (uint32_t)(seconds * 1000.0f) : 0;
}

static void tornado_set_fly_height(edict_t *unit, float height) {
    if (!unit) return;
    unit->unitinfo.FlyHeight = MAX(0.0f, height);
    M_CheckGround(unit);
    if (unit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    gi.LinkEntity(unit);
}

static bool tornado_valid_unit(edict_t *unit) {
    return unit && unit->inuse && !M_IsDead(unit) &&
           unit->summon_ability && G_AbilityCode(unit->summon_ability) == TORNADO;
}

/* Each toss cooldown marker holds two generation-checked pointers. Markers
 * are private (no snapshot) and are freed when their parent thinker ends. */
static bool tornado_recent_target(edict_t *thinker, edict_t *target) {
    FILTER_EDICTS(mark, mark->class_id == TORNADO_MARK &&
                  S_SpellChannelOwner(mark) == thinker &&
                  S_SpellChannelTarget(mark) == target &&
                  (int32_t)(G_Time() - mark->resources) < 0)
        return true;
    return false;
}

static void tornado_mark_target(edict_t *thinker, edict_t *target, uint32_t repeat_ms) {
    if (!repeat_ms) return;
    edict_t *mark = S_SpellIdentityThinker(thinker, TORNADO_MARK, target);
    if (!mark) return;
    mark->svflags |= SVF_NOCLIENT;
    mark->resources = G_Time() + repeat_ms;
}

static void tornado_release_markers(edict_t *thinker, bool all) {
    FILTER_EDICTS(mark, mark->class_id == TORNADO_MARK &&
                  S_SpellChannelOwner(mark) == thinker &&
                  (all || (int32_t)(G_Time() - mark->resources) >= 0))
        G_FreeEdict(mark);
}

/* Atdg: Area is the outer radius; DataA is its DPS. DataB/C give the
 * medium-radius/DPS and DataD/E the close-radius/DPS. A building belongs to
 * exactly one band. Read all values from its actual owned ability row. */
static void tornado_damage_buildings(edict_t *tornado) {
    abilityAliasRef_t ability = S_ResolveAbilityAlias(tornado, TORNADO_DAMAGE);
    abilityLevel_t const *row;
    float outer, medium, inner, outer_dps, medium_dps, inner_dps;
    if (!ability.alias || !(row = G_AbilityLevel(ability.alias, ability.level))) return;
    outer = MAX(0.0f, row->area);
    outer_dps = MAX(0.0f, S_SpellData(ability.alias, ability.level, 1));
    medium = MAX(0.0f, S_SpellData(ability.alias, ability.level, 2));
    medium_dps = MAX(0.0f, S_SpellData(ability.alias, ability.level, 3));
    inner = MAX(0.0f, S_SpellData(ability.alias, ability.level, 4));
    inner_dps = MAX(0.0f, S_SpellData(ability.alias, ability.level, 5));
    if (!outer) return;
    FILTER_EDICTS(target, target != tornado && G_UnitIsStructure(target) &&
                  S_SpellIsAliveTarget(target) && S_SpellIsEnemy(tornado, target)) {
        float const dist = Vector2_distance(&target->s.origin2, &tornado->s.origin2);
        float dps;
        if (dist > outer || !S_SpellAllowsAreaTarget(ability.alias, tornado, target)) continue;
        dps = inner > 0.0f && dist <= inner ? inner_dps :
              medium > 0.0f && dist <= medium ? medium_dps : outer_dps;
        if (dps > 0.0f) S_SpellDamage(target, tornado, (int)(dps + 0.5f));
    }
}

/* Atsp temporarily makes a nearby ground unit airborne and uncommandable
 * through the shared Cyclone status. The Bcyc buff owns its persistent art.
 * The chooser is a persisted per-thinker PRNG, not process rand(). */
static void tornado_spin_one(edict_t *tornado, edict_t *thinker) {
    abilityAliasRef_t ability = S_ResolveAbilityAlias(tornado, TORNADO_SPIN);
    abilityLevel_t const *row;
    edict_t *choice = NULL;
    uint32_t seen = 0;
    if (!ability.alias || !(row = G_AbilityLevel(ability.alias, ability.level)) || row->area <= 0.0f) return;
    FILTER_EDICTS(target, target != tornado && S_SpellIsAliveTarget(target) &&
                  S_SpellIsEnemy(tornado, target) && G_UnitTargetType(target) == TARG_GROUND) {
        float const distance = Vector2_distance(&target->s.origin2, &tornado->s.origin2);
        if (distance > row->area || tornado_recent_target(thinker, target) ||
            !S_SpellAllowsAreaTarget(ability.alias, tornado, target) ||
            S_UnitIsCycloned(target)) continue;
        thinker->variation = thinker->variation * 1664525u + 1013904223u;
        if (thinker->variation % ++seen == 0) choice = target;
    }
    if (choice) {
        /* Tornado lifts last ten seconds per victim, independent of the
         * remaining summon life and Atsp's authored Dur/HeroDur values. */
        if (thinker->resources > G_Time()) {
            heroabilitystatus_t *slot = S_SpellApplyTimedStatusFromAbility(
                choice, ability.alias, ability.level, "Bcyc",
                TORNADO_SPIN_LIFT_DURATION_SECONDS);
            if (slot) {
                /* Ground Atsp targets start at zero height. Keep the value in
                 * next_tick, which this non-periodic Cyclone status does not
                 * otherwise use, so expiry/dispel can restore it exactly. */
                union { float height; uint32_t bits; } original_height = { .height = choice->unitinfo.FlyHeight };
                slot->next_tick = original_height.bits;
                slot->buff_flags |= WC3_STATUS_BUFF_TARGET_ART;
                unit_status_enabletargetart(choice, slot, "origin");
                tornado_set_fly_height(choice, TORNADO_SPIN_LIFT_HEIGHT);
                choice->goalentity = NULL;
                choice->currentmove = &holdpos_move_stand;
                float const repeat_seconds = S_SpellData(ability.alias, ability.level, 1);
                tornado_mark_target(thinker, choice,
                                    repeat_seconds > 0.0f ? (uint32_t)(repeat_seconds * 1000.0f) : 0);
            }
        }
    }
}

static void tornado_finish(edict_t *thinker, edict_t *tornado) {
    tornado_release_markers(thinker, true);
    if (tornado && tornado->inuse) {
        /* Do not explode the unit: its own Death sequence plays once and the
         * ordinary corpse/decay lifecycle then retires the rendered model. */
        if (!M_IsDead(tornado)) {
            if (tornado->die) tornado->die(tornado, thinker->owner);
            else unit_die(tornado, thinker->owner);
        }
    }
    S_SpellEndChannel(thinker);
}

void tornado_think(edict_t *thinker) {
    edict_t *tornado = S_SpellChannelTarget(thinker);
    uint32_t now = G_Time();
    uint32_t interval_ms;
    if (!tornado_valid_unit(tornado) || !S_SpellChannelActive(thinker) ||
        (int32_t)(now - thinker->resources) >= 0) {
        tornado_finish(thinker, tornado);
        return;
    }
    tornado_release_markers(thinker, false);
    /* Catch up using fixed, authored one-second damage pulses, even when a
     * server frame was skipped; never apply damage at initial spawn time. */
    while ((int32_t)(now - thinker->freetime) >= 0 &&
           (int32_t)(thinker->resources - thinker->freetime) > 0) {
        tornado_damage_buildings(tornado);
        thinker->freetime += TORNADO_DAMAGE_INTERVAL_MS;
    }
    interval_ms = tornado_spin_interval_ms(tornado);
    if (interval_ms && (int32_t)(now - thinker->damage) >= 0) {
        tornado_spin_one(tornado, thinker);
        thinker->damage = now + MAX(FRAMETIME, interval_ms);
    }
}

BZ_ABILITY_PROC(CAbilityTornado) {
    edict_t *summon, *thinker;
    uint32_t unit_id, level;
    vec2_t location, clear_location;
    float duration;
    animation_t const *birth;
    if (msg != A_EXECUTE) return CAbilitySimpleSpell(ent, msg, call);
    if (!ent || !call || !call->item || !call->target ||
        call->target->type != SPELL_TARGET_POINT) return false;
    level = S_SpellLevel(ent, call->item->code);
    unit_id = S_SpellUnitId(call->item->code, level);
    duration = S_SpellDuration(call->item->code, level, false);
    if (!unit_id || duration <= 0.0f) return false;
    location = call->target->point;
    summon = S_SummonAbilityAt(ent, call->item->code, unit_id, &location, duration);
    if (!summon) return false;
    /* Spawn at the chosen point, nudging only when pathing or another unit
     * blocks it. Unlike SP_FindEmptySpaceAround, which searches around its
     * unit argument, this keeps the requested target as the placement origin. */
    (void)G_FindUnitUnstuckPosition(summon, &location, &clear_location);
    if (clear_location.x != summon->s.origin2.x || clear_location.y != summon->s.origin2.y) {
        vec2_t const old_location = summon->s.origin2;
        summon->s.origin2 = clear_location;
        summon->s.origin.x = clear_location.x;
        summon->s.origin.y = clear_location.y;
        M_CheckGround(summon);
        if (summon->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
        gi.LinkEntity(summon);
        G_UnitPositionChanged(summon, &old_location);
    }
    /* BNto is the authored summon buff, independent of the BTLF lifetime
     * marker. Respect custom BuffID aliases instead of hardcoding icon art. */
    {
        cstring_t buff = S_SpellBuffId(call->item->code, level);
        if (buff && strlen(buff) >= 4)
            S_SpellApplyTimedStatusFromAbility(summon, call->item->code, level,
                                               buff, duration);
    }
    /* The generic summoned-unit path calls stand immediately; ntor must first
     * play its actual Birth animation, then loop its combined Stand Walk. */
    birth = G_GetUnitAnimation(summon, "birth");
    if (birth) {
        unit_birth(summon);
        summon->wait = MAX(FRAMETIME / 1000.0f,
                           (float)(birth->interval[1] - birth->interval[0]) / 1000.0f);
    }
    thinker = S_SpellChannelThinker(ent, call->item->code);
    if (!thinker) {
        if (summon->die) summon->die(summon, ent);
        else unit_die(summon, ent);
        return false;
    }
    thinker->goalentity = summon;
    G_GetChannel(thinker)->target_spawn_time = summon->spawn_time;
    thinker->resources = G_Time() + (uint32_t)(duration * 1000.0f);
    thinker->freetime = G_Time() + TORNADO_DAMAGE_INTERVAL_MS;
    thinker->damage = G_Time() + tornado_spin_interval_ms(summon);
    thinker->variation = (summon->s.number + 1u) * 2654435761u ^ summon->spawn_time;
    thinker->svflags |= SVF_NOCLIENT;
    thinker->think = tornado_think;
    return true;
}

#ifdef BZ_TESTS
/* Test the actual damage-band evaluator with synthetic non-stock SLK values. */
void S_TestTornadoBuildingPulse(edict_t *tornado) { tornado_damage_buildings(tornado); }
#endif

BZ_ABILITY_PROC(CAbilityTornadoDamage) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityTornadoSpin) {
    if (msg == A_STATUS_REMOVE && call && call->status.slot &&
        S_CycloneRestoreLiftHeight(ent, call->status.slot)) return true;
    return CAbilityPassive(ent, msg, call);
}
