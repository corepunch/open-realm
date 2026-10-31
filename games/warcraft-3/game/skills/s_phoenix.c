#include "s_skills.h"

/* Stock Phoenix morphing uses unit objects, not a timed summon. In particular
 * neither Dispel Magic nor the generic summoned-unit limit may destroy hphx. */
#define PHOENIX_UNIT MAKEFOURCC('h','p','h','x') // rawcode; stock Phoenix unit restored when its egg hatches
#define PHOENIX_EGG MAKEFOURCC('h','p','x','e') // rawcode; stock temporary egg form during rebirth
#define PHOENIX_MORPH MAKEFOURCC('A','p','h','x') // rawcode; stock Phoenix Egg rebirth ability
#define PHOENIX_FIRE MAKEFOURCC('A','p','x','f') // rawcode; Phoenix Fire passive attack effect
#define PHOENIX_HATCH_MS 10000u // milliseconds; authored Phoenix Egg lifetime before rebirth completes
#define PHOENIX_FIRE_TICK_MS 1000u // milliseconds; authored cadence for Phoenix Fire self-damage ticks

static void phoenix_fire_impact(edict_t *missile);
static umove_t phoenix_fire_missile_move = { "stand", NULL, phoenix_fire_impact, CAbilityPhoenixFire };

static float phoenix_fire_missile_speed(uint32_t code) {
    cstring_t value = S_SpellString(code, "Missilespeed", 0);
    float speed;
    if (!value && G_AbilityCode(code) != code)
        value = S_SpellString(G_AbilityCode(code), "Missilespeed", 0);
    speed = value ? atof(value) : 0.0f;
    return speed > 0.0f ? speed : 900.0f;
}

/* The Blood Mage replaces an existing Phoenix with a normal unit, without
 * timed life, summon identity or summon-damage bonuses. Use the spell's UnitID
 * so custom maps can replace the summoned unit; only stock hphx/hpxe use the
 * special automatic rebirth contract. */
void S_SummonPhoenix(edict_t *caster, uint32_t code) {
    uint32_t unit_id;
    vec2_t loc;
    float angle;
    edict_t *created;

    if (!caster || !code) return;
    unit_id = S_SpellUnitId(code, S_SpellLevel(caster, code));
    if (!unit_id) return;
    /* Recast replaces only this Blood Mage's previous Phoenix. The owner
     * pointer alone is insufficient: its edict slot may have been recycled. */
    FILTER_EDICTS(old, old->inuse && !M_IsDead(old) && old->s.player == caster->s.player &&
                  old->owner == caster &&
                  old->phoenix_owner_spawn_time == caster->spawn_time &&
                  (old->class_id == unit_id ||
                   (unit_id == PHOENIX_UNIT && old->class_id == PHOENIX_EGG && old->phoenix_hatch_ms))) {
        /* Recast replacement is terminal; unlike combat death it must not
         * leave a second, independently hatching Phoenix Egg. */
        if (old->class_id == PHOENIX_UNIT) old->aiflags |= AI_SCRIPT_EXPLODED;
        if (old->die) old->die(old, caster);
        else unit_die(old, caster);
    }
    loc = caster->s.origin2;
    angle = caster->s.angle;
    loc.x += cosf(angle) * MAX(64.0f, caster->collision + 32.0f);
    loc.y += sinf(angle) * MAX(64.0f, caster->collision + 32.0f);
    SP_FindEmptySpaceAround(caster, unit_id, &loc, &angle);
    created = SP_SpawnAtLocation(unit_id, caster->s.player, &loc);
    if (!created) return;
    created->owner = caster;
    created->phoenix_owner_spawn_time = caster->spawn_time;
    G_ActivateUnitFood(created);
    if (created->stand) created->stand(created);
}

BZ_ABILITY_PROC(CAbilityPhoenixMorphing) {
    if (!ent) return false;
    if (msg == A_DEATH && ent->class_id == PHOENIX_UNIT &&
        !(ent->aiflags & AI_SCRIPT_EXPLODED) &&
        G_UnitAbilityLevel(ent, PHOENIX_MORPH)) {
        edict_t *egg = SP_SpawnAtLocationNoBirth(PHOENIX_EGG, ent->s.player, &ent->s.origin2);
        if (!egg) return false;
        egg->owner = ent->owner;
        egg->phoenix_owner_spawn_time = ent->phoenix_owner_spawn_time;
        egg->phoenix_hatch_ms = G_Time() + PHOENIX_HATCH_MS;
        G_ActivateUnitFood(egg);
        if (egg->stand) egg->stand(egg);
        /* The Phoenix dies normally for triggers, scores and experience, but
         * its old visual must not overlap the newly created egg or yield a corpse. */
        ent->aiflags |= AI_CORPSE_UNRAISABLE | AI_CORPSE_NO_DECAY;
        ent->s.renderfx |= RF_HIDDEN;
        ent->wait = FRAMETIME / 1000.0f;
        return true;
    }
    if (msg == A_UPDATE && ent->class_id == PHOENIX_EGG && ent->phoenix_hatch_ms &&
        !M_IsDead(ent) && (int32_t)(G_Time() - ent->phoenix_hatch_ms) >= 0) {
        if (!G_TransformUnitType(ent, PHOENIX_UNIT)) return false;
        ent->phoenix_hatch_ms = 0;
        ent->phoenix_fire_next_ms = 0;
        G_SetHealth(ent, ent->health.max_value);
        if (ent->stand) ent->stand(ent);
        return true;
    }
    return CAbilityPassive(ent, msg, call);
}

/* Each source owns one independent burn on each victim. Repeated hits refresh
 * its duration without moving the next 1-second tick; the shared timed-status
 * dispatcher and source-incarnation guard handle save/load and slot reuse. */
static void phoenix_fire_burn(edict_t *target, heroabilitystatus_t *slot) {
    edict_t *source = S_SpellStatusSource(slot);
    /* The flame keeps burning until its authored duration expires, even if
     * the Phoenix has died or its edict slot has been recycled. In that case
     * apply damage without attributing it to a different unit incarnation. */
    while (unit_status_take_due_tick(slot, PHOENIX_FIRE_TICK_MS, G_Time())) {
        int const damage = (int)MAX(0.0f, S_SpellData(slot->data, slot->rank, 2));
        if (damage > 0) S_SpellDamage(target, source, damage);
        if (M_IsDead(target)) break;
    }
}

static void phoenix_fire_impact(edict_t *missile) {
    edict_t *target = S_SpellProjectileTarget(missile);
    edict_t *source = S_SpellProjectileOwner(missile);
    if (source && S_SpellIsAliveTarget(target) &&
        S_SpellIsEnemy(source, target) && S_SpellAllowsTarget(missile->class_id, source, target)) {
        uint32_t const level = MAX(1u, S_SpellLevel(source, missile->class_id));
        float const duration = S_SpellHeroDuration(missile->class_id, level, target);
        if (S_SpellDamage(target, source, (int)missile->damage) && !M_IsDead(target) && duration > 0.0f) {
            cstring_t buff = S_SpellBuffId(missile->class_id, level);
            status_application_t app;
            heroabilitystatus_t *slot;
            if (!buff) buff = "Bpxf"; /* ROC data can omit BuffID. */
            slot = unit_findstatussource(target, FS_SLKKey(buff), source);
            if (slot && slot->stack_policy == WC3_STATUS_STACK_INDEPENDENT &&
                slot->source_ability == missile->class_id) {
                slot->timestamp = G_Time() + (uint32_t)(duration * 1000.0f);
                slot->duration_ms = (uint32_t)(duration * 1000.0f);
                unit_status_invalidate_update_mask(target);
            } else {
                app = (status_application_t){
                    .buff = buff, .level = level, .duration = duration,
                    .source_ability = missile->class_id, .data = missile->class_id,
                    .source = source, .rank = level,
                    .stack_policy = WC3_STATUS_STACK_INDEPENDENT,
                    .buff_flags = WC3_STATUS_BUFF_NEGATIVE | WC3_STATUS_BUFF_MAGICAL
                };
                slot = unit_applystatus(target, &app);
                if (slot) slot->next_tick = G_Time() + PHOENIX_FIRE_TICK_MS;
            }
            G_SpawnAbilityEffectTarget(FS_SLKKey(buff), WC3_EFFECT_TARGET, 0, target, NULL, true);
        }
    }
    G_FreeEdict(missile);
}

BZ_ABILITY_PROC(CAbilityPhoenixFire) {
    abilityAliasRef_t ability;
    abilityLevel_t const *row;
    edict_t *best = NULL;
    float best_dist = 0.0f;
    uint32_t now, interval;
    if (msg == A_STATUS_TICK && call && call->status.slot) {
        phoenix_fire_burn(ent, call->status.slot);
        return true;
    }
    if (msg == A_STATUS_DEATH && call && call->status.slot) {
        unit_expirestatus(ent, call->status.slot);
        return true;
    }
    if (msg != A_UPDATE || !S_AuraUnitActive(ent) ||
        G_UnitHasFlag(ent, UNIT_FLAG_PAUSED) ||
        unit_hasstatusstate(ent, WC3_STATUS_STATE_STUNNED))
        return CAbilityPassive(ent, msg, call);
    ability = S_ResolveAbilityAlias(ent, PHOENIX_FIRE);
    if (!ability.alias) return false;
    row = G_AbilityLevel(ability.alias, ability.level);
    if (!row || row->area <= 0.0f) return false;
    now = G_Time();
    if (ent->phoenix_fire_next_ms && (int32_t)(now - ent->phoenix_fire_next_ms) < 0) return true;
    FILTER_EDICTS(target, target != ent && S_SpellIsAliveTarget(target) &&
                  S_SpellIsEnemy(ent, target)) {
        float dx = target->s.origin2.x - ent->s.origin2.x;
        float dy = target->s.origin2.y - ent->s.origin2.y;
        float dist;
        if (fabsf(dx) > row->area || fabsf(dy) > row->area) continue;
        dist = dx * dx + dy * dy;
        if (dist > row->area * row->area ||
            !S_SpellAllowsTarget(ability.alias, ent, target)) continue;
        if (!best || dist < best_dist) { best = target; best_dist = dist; }
    }
    if (!best) return true;
    interval = (uint32_t)(MAX(0.05f, row->cool) * 1000.0f);
    ent->phoenix_fire_next_ms = now + interval;
    /* Apxf has a separate feather missile rather than consuming the Phoenix's
     * ordinary attack. The effect/caster and its travelling art are game-owned. */
    {
        edict_t *missile = S_SpawnUnitTargetSpellMissile(ent, ability.alias, best,
                                                          phoenix_fire_missile_speed(ability.alias),
                                                          &phoenix_fire_missile_move);
        if (missile) missile->damage = (uint32_t)MAX(0.0f, S_SpellData(ability.alias, ability.level, 1));
    }
    return true;
}
