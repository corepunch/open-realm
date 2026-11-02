#include "s_skills.h"

/* Spirit/beast summons vanish in a magical burst when killed or expired.
 * Keep ordinary summoned units on their existing death path. */
static bool summon_magical_death(uint32_t code) {
    return code == MAKEFOURCC('A','O','s','f') || /* Feral Spirit */
           code == MAKEFOURCC('A','C','s','7') || /* Akama Feral Spirit */
           code == MAKEFOURCC('A','N','s','g') || /* Summon Grizzly */
           code == MAKEFOURCC('A','N','s','q') || /* Summon Quilbeast */
           code == MAKEFOURCC('A','N','s','w') || /* Summon Hawk */
           code == MAKEFOURCC('A','r','s','g') || /* Campaign Misha */
           code == MAKEFOURCC('A','r','s','q');   /* Campaign Quilbeast */
}

static edict_t *summon_unit(edict_t *caster, uint32_t unit_id, uint32_t index, uint32_t count, float duration) {
    vec2_t loc;
    float angle;
    edict_t *summon;

    if (!caster || !unit_id)
        return NULL;

    angle = count > 0 ? (2.0f * (float)M_PI * (float)index) / (float)count : 0.0f;
    loc = caster->s.origin2;
    loc.x += cosf(angle) * MAX(64.0f, caster->collision + 32.0f);
    loc.y += sinf(angle) * MAX(64.0f, caster->collision + 32.0f);
    SP_FindEmptySpaceAround(caster, unit_id, &loc, &angle);

    summon = SP_SpawnAtLocation(unit_id, caster->s.player, &loc);
    if (!summon)
        return NULL;
    summon->owner = caster;
    G_ActivateUnitFood(summon);
    if (summon->stand)
        summon->stand(summon);
    if (duration > 0)
        S_SpellApplyTimedLife(summon, 1, duration);
    G_PublishSummonEvents(caster, summon);
    return summon;
}

static edict_t *summon_ability_unit(edict_t *caster, uint32_t code, uint32_t unit_id, uint32_t index,
                                    uint32_t count, float duration) {
    edict_t *summon = summon_unit(caster, unit_id, index, count, duration);
    if (summon) {
        summon->summon_ability = code;
        if (code == MAKEFOURCC('A','E','s','v') &&
            unit_id == MAKEFOURCC('e','s','p','v'))
            (void)G_SetUnitAutocast(summon, MAKEFOURCC('A','v','n','g'), true);
        if (summon_magical_death(code)) {
            summon->aiflags |= AI_SUMMON_EXPLODES_ON_DEATH;
            G_SpawnSummonMagicalEffect(summon);
        }
        if (WC3_MISHA_DEBUG_ENABLED() && code == MAKEFOURCC('A','r','s','g'))
            fprintf(stderr, "WC3_MISHA summon-created caster=%u summon=%u id=%.4s owner=%u ability=%.4s index=%u/%u\n",
                    (unsigned)caster->s.number, (unsigned)summon->s.number,
                    (cstring_t)&summon->class_id, (unsigned)summon->s.player,
                    (cstring_t)&code, index + 1, count);
    } else if (WC3_MISHA_DEBUG_ENABLED() && code == MAKEFOURCC('A','r','s','g')) {
        fprintf(stderr, "WC3_MISHA summon-failed caster=%u unit=%.4s index=%u/%u\n",
                (unsigned)caster->s.number, (cstring_t)&unit_id, index + 1, count);
    }
    return summon;
}

void S_SummonUnits(edict_t *caster, uint32_t unit_id, uint32_t count, float duration) {
    if (!count) count = 1;
    FOR_LOOP(i, count) (void)summon_unit(caster, unit_id, i, count, duration);
}

edict_t *S_SummonAt(edict_t *caster, uint32_t unit_id, vec2_t const *loc, float duration) {
    edict_t *summon;
    if (!caster || !unit_id || !loc) return NULL;
    summon = SP_SpawnAtLocation(unit_id, caster->s.player, loc);
    if (!summon) return NULL;
    summon->owner = caster; G_ActivateUnitFood(summon);
    if (summon->stand) summon->stand(summon);
    if (duration > 0.0f) S_SpellApplyTimedLife(summon, 1, duration);
    G_PublishSummonEvents(caster, summon);
    return summon;
}

/* Ability-owned summons carry the concrete ability alias separately from the
 * generic owner pointer. Limit/recast/ward/dispel policy can then identify the
 * creating ability without every caller repeating this assignment. */
edict_t *S_SummonAbilityAt(edict_t *caster, uint32_t code, uint32_t unit_id, vec2_t const *loc, float duration) {
    edict_t *summon = S_SummonAt(caster, unit_id, loc, duration);
    if (summon) {
        summon->summon_ability = code;
        if (summon_magical_death(code)) {
            summon->aiflags |= AI_SUMMON_EXPLODES_ON_DEATH;
            G_SpawnSummonMagicalEffect(summon);
        }
    }
    return summon;
}

/* Common Object Editor summon contract used by simple campaign/requested
 * abilities: UnitID selects the unit, DataA is count (minimum one), and Dur is
 * timed life. Every result records the concrete ability alias; point-target
 * variants spawn at the authored target point and other variants use the normal
 * collision-safe ring around the caster. */
void S_SummonAbilityUnits(edict_t *caster, uint32_t code, spellTarget_t const *target) {
    uint32_t level, unit_id, count;
    float duration;

    if (!caster || !code) return;
    level = S_SpellLevel(caster, code);
    unit_id = S_SpellUnitId(code, level);
#if BZ_COMPAT_WORKAROUNDS
    /* RoC 1.0 AEst describes the Owl Scout in its tooltip but leaves UnitID
     * empty. The stock summoned unit is nowl; later data authors UnitID. */
    if (!unit_id && code == MAKEFOURCC('A','E','s','t'))
        unit_id = MAKEFOURCC('n','o','w','l');
#endif
    count = (uint32_t)MAX(1.0f, S_SpellData(code, level, 1));
    duration = S_SpellDuration(code, level, false);
    if (WC3_MISHA_DEBUG_ENABLED() && code == MAKEFOURCC('A','r','s','g'))
        fprintf(stderr, "WC3_MISHA summon-request caster=%u ability=Arsg level=%u unit=%.4s count=%u duration=%.2f target_type=%u origin=(%.1f,%.1f)\n",
                (unsigned)caster->s.number, level, unit_id ? (cstring_t)&unit_id : "----",
                count, duration, target ? (unsigned)target->type : 0u,
                caster->s.origin2.x, caster->s.origin2.y);
    if (!unit_id) return;
    if (target && target->type == SPELL_TARGET_POINT) {
        FOR_LOOP(i, count) (void)S_SummonAbilityAt(caster, code, unit_id, &target->point, duration);
        return;
    }
    FOR_LOOP(i, count) (void)summon_ability_unit(caster, code, unit_id, i, count, duration);
}

/* Some Warcraft summon abilities cap one authored unit type rather than all
 * results of the cast.  Keep the eviction primitive generic: callers supply
 * the Object Editor limit-check type and the retail cap, while summoned-unit
 * identity comes from the shared summon_ability marker. */
uint32_t S_EnforceSummonedUnitTypeLimit(edict_t *caster, uint32_t unit_id, uint32_t max_count) {
    uint32_t removed = 0;

    if (!caster || !unit_id || !max_count) return 0;
    for (;;) {
        edict_t *oldest = NULL;
        uint32_t count = 0;

        FILTER_EDICTS(unit, unit->inuse && !M_IsDead(unit) && unit->s.player == caster->s.player &&
                      unit->class_id == unit_id && unit->summon_ability) {
            count++;
            if (!oldest || unit->spawn_time < oldest->spawn_time ||
                (unit->spawn_time == oldest->spawn_time && unit->s.number < oldest->s.number))
                oldest = unit;
        }
        if (count <= max_count || !oldest) break;
        if (oldest->die) oldest->die(oldest, caster);
        else unit_die(oldest, caster);
        removed++;
    }
    return removed;
}

/* Inferno blast hits living ground/structure enemies; air is out of authored targs. */
static bool inferno_hits(edict_t *caster, edict_t *target, float radius, vec2_t const *origin) {
    if (!S_SpellIsAliveTarget(target) || !S_SpellIsEnemy(caster, target)) return false;
    if (Vector2_distance(&target->s.origin2, origin) > radius) return false;
    if (G_UnitTargetType(target) == TARG_AIR) return false;
    return G_UnitTargetType(target) == TARG_GROUND || G_UnitTargetType(target) == TARG_STRUCTURE;
}

/* DataA damage + Bstu Dur/HeroDur, then UnitID with DataB timed life. */
static void inferno_impact(edict_t *caster, uint32_t code, uint32_t level, vec2_t const *point) {
    float area = S_SpellNumber(code, ABILITY_NUMBER_AREA, level);
    int damage = (int)S_SpellData(code, level, 1);
    float life = S_SpellData(code, level, 2);
    uint32_t unit_id = S_SpellUnitId(code, level);

    FILTER_EDICTS(target, inferno_hits(caster, target, area, point)) {
        S_SpellDamage(target, caster, damage);
        if (!M_IsDead(target))
            S_SpellApplyStun(target, S_SpellResistantDuration(code, level, target));
    }
    if (!unit_id) {
        fprintf(stderr, "WC3 Inferno: missing UnitID for %.4s\n", (cstring_t)&code);
        return;
    }
    S_SummonAbilityAt(caster, code, unit_id, point, life);
}

void inferno_think(edict_t *ent) {
    uint32_t now = G_Time();
    if (ent->freetime && now < ent->freetime) return;
    if (!ent->owner || !ent->owner->inuse) { G_FreeEdict(ent); return; }
    inferno_impact(ent->owner, ent->class_id, (uint32_t)ent->wait, &ent->s.origin2);
    G_FreeEdict(ent);
}

/* DataC<=0 impacts immediately; otherwise a thinker owns the meteor delay. */
void S_InfernoLand(edict_t *caster, uint32_t code, uint32_t level, vec2_t const *point) {
    float delay;
    edict_t *thinker;
    if (!caster || !point) return;
    delay = S_SpellData(code, level, 3);
    if (delay <= 0.0f) { inferno_impact(caster, code, level, point); return; }
    thinker = G_Spawn();
    thinker->owner = caster; thinker->class_id = code; thinker->wait = (float)level;
    thinker->s.origin2 = *point; thinker->s.origin.x = point->x; thinker->s.origin.y = point->y;
    thinker->freetime = G_Time() + (uint32_t)(delay * 1000.0f);
    thinker->think = inferno_think;
}

/* Name=Inferno
 * Ubertip: area damage + stun on land, then summon Infernal for DataB seconds after DataC delay.
 */
BZ_SIMPLE_SPELL_PROC(AbilityInferno) {
    S_InfernoLand(caster, spell->code, S_SpellLevel(caster, spell->code), &st.point);
}

/* Rain of Chaos resolves each landing through the Inferno ability linked by DataA. */
void rain_of_chaos_think(edict_t *ent) {
    uint32_t now = G_Time(), level = (uint32_t)ent->wait, inferno = ent->damage, code = ent->class_id;
    float angle, radius;
    vec2_t loc = ent->s.origin2;
    if (!ent->owner || !ent->owner->inuse || !ent->resources) { G_FreeEdict(ent); return; }
    if (ent->freetime && now < ent->freetime) return;
    angle = ((float)rand() / (float)RAND_MAX) * 2.0f * (float)M_PI;
    radius = sqrtf((float)rand() / (float)RAND_MAX) * ent->collision;
    loc.x += cosf(angle) * radius; loc.y += sinf(angle) * radius;
    S_InfernoLand(ent->owner, inferno, level, &loc);
    if (!--ent->resources) { G_FreeEdict(ent); return; }
    /* Zero Dur cannot schedule the next landing; stop rather than spin every frame. */
    if (ent->velocity <= 0.0f) {
        fprintf(stderr, "WC3 Rain of Chaos: landing interval became zero for %.4s\n", (cstring_t)&code);
        G_FreeEdict(ent); return;
    }
    ent->freetime = now + (uint32_t)(ent->velocity * 1000.0f);
}

/* Unlike Rain of Fire, Rain of Chaos is not channeled: its effect owns the remaining landings after cast. */
BZ_SIMPLE_SPELL_PROC(AbilityRainOfChaos) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    edict_t *thinker = G_Spawn();
    thinker->owner = caster; thinker->class_id = spell->code; thinker->s.origin2 = st.point;
    thinker->collision = MAX(0.0f, S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level));
    thinker->damage = S_SpellDataId(spell->code, level, 1);
    thinker->resources = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 2));
    thinker->velocity = MAX(0.0f, S_SpellDuration(spell->code, level, false));
    thinker->wait = (float)level; thinker->think = rain_of_chaos_think;
    rain_of_chaos_think(thinker);
}

static void summon_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    uint32_t unit_id = S_SpellUnitId(spell->code, level);
    uint32_t count = (uint32_t)S_SpellData(spell->code, level, 1);
    float duration = S_SpellDuration(spell->code, level, false);

    if (!caster || !unit_id || !count) return;
    FOR_LOOP(i, count) {
        edict_t *summon = summon_ability_unit(caster, spell->code, unit_id, i, count, duration);
        if (!summon) continue;
        if (!summon_magical_death(spell->code))
            G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, summon, NULL, true);
    }
}

/* Name=Summon Water Elemental
 * Ubertip="Summons a Water Elemental to fight for the caster."
 */
BZ_SIMPLE_SPELL_PROC(AbilityWaterElemental) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    uint32_t unit_id = S_SpellUnitId(spell->code, level);
    uint32_t count = (uint32_t)S_SpellData(spell->code, level, 1);
    float duration = S_SpellDuration(spell->code, level, false);
    float distance = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    cstring_t buff = S_SpellBuffId(spell->code, level);
    vec2_t loc = caster->s.origin2;

#if BZ_COMPAT_WORKAROUNDS
    /* RoC 1.0 stores the summon unit rawcode in DataA and leaves UnitID
     * empty. Its DataA number is therefore zero; later data uses DataA as
     * the summon count and puts the rawcode in UnitID. */
    if (spell->code == MAKEFOURCC('A','H','w','e') && !unit_id) {
        uint32_t const legacy_unit_id = S_SpellDataId(spell->code, level, 1);
        if (legacy_unit_id && G_UnitData(legacy_unit_id)->id == legacy_unit_id) {
            unit_id = legacy_unit_id;
            count = 1;
        }
    }
#endif
    if (!caster || !unit_id || !count) return;
    loc.x += cosf(caster->s.angle) * distance;
    loc.y += sinf(caster->s.angle) * distance;
    FOR_LOOP(i, count) {
        float const angle = caster->s.angle + 2.0f * (float)M_PI * (float)i / (float)count;
        vec2_t spawn = { loc.x + cosf(angle) * MAX(32.0f, caster->collision),
                          loc.y + sinf(angle) * MAX(32.0f, caster->collision) };
        edict_t *summon = S_SummonAbilityAt(caster, spell->code, unit_id, &spawn, duration);
        if (!summon) continue;
        if (G_FindUnitUnstuckPosition(summon, &spawn, &summon->s.origin2)) {
            summon->s.origin.x = summon->s.origin2.x;
            summon->s.origin.y = summon->s.origin2.y;
        }
        /* Warsmash creates the Elemental using the caster's facing. Relink
         * after collision-safe displacement so the server broad phase follows
         * the authoritative position rather than the original spawn point. */
        summon->s.angle = caster->s.angle;
        gi.LinkEntity(summon);
        /* Warsmash's CBuffTimedLife uses the ability's authored BuffID.
         * OpenRealm keeps BTLF as the authoritative timed-life clock (needed
         * by UnitPauseTimedLife/the timed-life bar), while this persistent
         * authored status supplies the correct buff identity/presentation for
         * Water Elemental without creating a second expiry clock. */
        if (buff && strlen(buff) >= 4) unit_addstatus(summon, buff, level);
        G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, summon, NULL, true);
    }
}

/* Name=Feral Spirit
 * Ubertip="Summons Spirit Wolf companions."
 */
/* Replace only the caster's prior Feral Spirit summons before spawning the new cast. */
BZ_SIMPLE_SPELL_PROC(AbilitySpiritWolf) {
    uint32_t level, unit_id, count;
    float duration, distance;
    vec2_t loc;

    if (!caster) return;
    level = S_SpellLevel(caster, spell->code);
    unit_id = S_SpellUnitId(spell->code, level);
    count = (uint32_t)S_SpellData(spell->code, level, 2);
    duration = S_SpellDuration(spell->code, level, false);
    distance = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
#if BZ_COMPAT_WORKAROUNDS
    /* RoC 1.0 AOsf has no UnitID or Area: its rank selects osw1-osw3,
     * DataB remains the summon count, and the wolves spawn at the caster. */
    if (spell->code == MAKEFOURCC('A','O','s','f') && !unit_id) {
        static uint32_t const legacy_wolves[] = {
            MAKEFOURCC('o','s','w','1'), MAKEFOURCC('o','s','w','2'), MAKEFOURCC('o','s','w','3')
        };
        uint32_t const rank = MIN(level, 3u) - 1;
        if (G_UnitData(legacy_wolves[rank])->id == legacy_wolves[rank]) unit_id = legacy_wolves[rank];
    }
#endif
    if (!unit_id || !count) return;

    /* Warsmash keeps the previous Feral Spirit cast as ability-owned summons:
     * recasting kills only surviving wolves from that caster's prior cast. */
    FILTER_EDICTS(unit, unit->inuse && unit->owner == caster &&
                  unit->summon_ability == spell->code && !M_IsDead(unit)) {
        if (unit->die) unit->die(unit, caster);
        else unit_die(unit, caster);
    }

    loc = caster->s.origin2;
    loc.x += cosf(caster->s.angle) * distance;
    loc.y += sinf(caster->s.angle) * distance;
    FOR_LOOP(i, count) {
        (void)S_SummonAbilityAt(caster, spell->code, unit_id, &loc, duration);
    }
}

static uint32_t force_of_nature_unit_id(uint32_t code, uint32_t level) {
    uint32_t unit_id = S_SpellUnitId(code, level);
#if BZ_COMPAT_WORKAROUNDS
    /* RoC 1.0 leaves AEfn UnitID empty; the authored result is efon. */
    if (!unit_id && code == MAKEFOURCC('A','E','f','n'))
        unit_id = MAKEFOURCC('e','f','o','n');
#endif
    return unit_id;
}

static bool force_of_nature_tree(edict_t const *tree) {
    destructable_t const *destructable;
    if (!tree || !tree->inuse || tree->targtype != TARG_TREE || !G_IsDestructable(tree)) return false;
    destructable = G_GetDestructable(tree);
    return destructable && !destructable->dead && !G_UnitHasFlag(tree, UNIT_FLAG_INVULNERABLE);
}

static uint32_t force_of_nature_count_trees(vec2_t const *point, float area) {
    uint32_t count = 0;
    FILTER_EDICTS(tree, force_of_nature_tree(tree) &&
                  Vector2_distance(&tree->s.origin2, point) <= area) count++;
    return count;
}

static edict_t *force_of_nature_nearest_tree(vec2_t const *point, float area) {
    edict_t *nearest = NULL;
    float nearest_distance = 1e30f;
    FILTER_EDICTS(tree, force_of_nature_tree(tree) &&
                  Vector2_distance(&tree->s.origin2, point) <= area) {
        float const distance = Vector2_distance(&tree->s.origin2, point);
        if (distance < nearest_distance) {
            nearest = tree;
            nearest_distance = distance;
        }
    }
    return nearest;
}

static bool force_of_nature_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level, unit_id, count;
    float area;
    vec2_t point;
    if (!caster || !spell) {
        return false;
    }
    if (st.type == SPELL_TARGET_POINT) point = st.point;
    else if (st.type == SPELL_TARGET_UNIT && st.entity && st.entity->inuse)
        point = st.entity->s.origin2;
    else {
        return false;
    }
    level = S_SpellLevel(caster, spell->code);
    unit_id = force_of_nature_unit_id(spell->code, level);
    count = (uint32_t)MAX(0.0f, S_SpellData(spell->code, level, 1));
    area = MAX(0.0f, S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level));
    {
        uint32_t const available = force_of_nature_count_trees(&point, area);
        bool const valid = unit_id && count && area > 0.0f && available >= count;
        return valid;
    }
}

static void force_of_nature_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    uint32_t unit_id = force_of_nature_unit_id(spell->code, level);
    uint32_t count = (uint32_t)MAX(0.0f, S_SpellData(spell->code, level, 1));
    float const area = MAX(0.0f, S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level));
    float const duration = S_SpellDuration(spell->code, level, false);
    vec2_t point;
    if (!caster || !spell || !force_of_nature_validate(caster, st, spell) || !unit_id || !count) {
        return;
    }
    point = st.type == SPELL_TARGET_POINT ? st.point : st.entity->s.origin2;
    FOR_LOOP(i, count) {
        edict_t *tree = force_of_nature_nearest_tree(&point, area);
        vec2_t spawn;
        edict_t *summon;
        if (!tree) {
            break;
        }
        spawn = tree->s.origin2;
        /* Each consumed tree contributes one Treant. Killing it first clears
         * its baked footprint so the summon can be placed on that spot. */
        if (!G_KillDestructable(tree, caster)) {
            continue;
        }

        summon = S_SummonAbilityAt(caster, spell->code, unit_id, &spawn, duration);
        if (!summon) {
            continue;
        }
        {
            vec2_t clear;
            if (G_FindUnitUnstuckPosition(summon, &spawn, &clear)) {
                summon->s.origin2 = clear;
                summon->s.origin.x = clear.x;
                summon->s.origin.y = clear.y;
                summon->s.origin.z = CM_GetHeightAtPoint(clear.x, clear.y);
                gi.LinkEntity(summon);
            }
        }
        if (!summon_magical_death(spell->code))
            G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, summon, NULL, true);
    }
}

/* Name=Force of Nature
 * Ubertip="Converts trees in a target area into Treants."
 */
BZ_ABILITY_PROC(CAbilityForceOfNature) {
    abilityitem_t const *spell = call ? call->item : NULL;
    if (spell && spell->code == MAKEFOURCC('A','E','f','n')) {
        spellTarget_t target = call && call->target ? *call->target :
            MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
        if (msg == A_SPELL_TARGET_MODE) {
            if (call) ((abilityCall_t *)call)->spell_entity_target_mode = true;
            return true;
        }
        /* spell_approach_range dispatches with a NULL caster and seeds the
         * field from the authored Rng value. Do not pass that query through
         * generic spell handling, which expects a live unit. */
        if (msg == A_SPELL_APPROACH_RANGE) return true;
        if (msg == A_VALIDATE) return force_of_nature_validate(ent, target, spell);
        if (msg == A_EXECUTE) { force_of_nature_execute(ent, target, spell); return true; }
        return CAbilitySimpleSpell(ent, msg, call);
    }
    if (msg == A_EXECUTE) {
        spellTarget_t target = call && call->target ? *call->target :
            MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
        summon_execute(ent, target, spell);
        return true;
    }
    return CAbilitySimpleSpell(ent, msg, call);
}

/* Name=Summon Bear
 * Ubertip="Summons Misha, a powerful bear, to attack your enemies."
 */
BZ_SIMPLE_SPELL_PROC(AbilitySummonGrizzly) { summon_execute(caster, st, spell); }
/* Name=Summon Quilbeast
 * Ubertip="Summons an angry quilbeast to fling spines at your enemies."
 */
BZ_SIMPLE_SPELL_PROC(AbilitySummonQuillbeast) { summon_execute(caster, st, spell); }
/* Name=Summon Hawk
 * Ubertip="Summons a hawk to fight for the caster."
 */
BZ_SIMPLE_SPELL_PROC(AbilitySummonWarEagle) { summon_execute(caster, st, spell); }
