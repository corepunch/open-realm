#include "s_skills.h"

#define ID_TIMED_LIFE "BTLF"
#define ID_STUN_BUFF "Bstu"

static LPEDICT summon_unit(LPEDICT caster, DWORD unit_id, DWORD index, DWORD count, FLOAT duration) {
    VECTOR2 loc;
    FLOAT angle;
    LPEDICT summon;

    if (!caster || !unit_id)
        return NULL;

    angle = count > 0 ? (2.0f * (FLOAT)M_PI * (FLOAT)index) / (FLOAT)count : 0.0f;
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
        unit_addtimedstatus(summon, ID_TIMED_LIFE, 1, duration);
    G_PublishSummonEvents(caster, summon);
    return summon;
}

void S_SummonUnits(LPEDICT caster, DWORD unit_id, DWORD count, FLOAT duration) {
    if (!count) count = 1;
    FOR_LOOP(i, count) (void)summon_unit(caster, unit_id, i, count, duration);
}

LPEDICT S_SummonAt(LPEDICT caster, DWORD unit_id, LPCVECTOR2 loc, FLOAT duration) {
    LPEDICT summon;
    if (!caster || !unit_id || !loc) return NULL;
    summon = SP_SpawnAtLocation(unit_id, caster->s.player, loc);
    if (!summon) return NULL;
    summon->owner = caster; G_ActivateUnitFood(summon);
    if (summon->stand) summon->stand(summon);
    if (duration > 0.0f) unit_addtimedstatus(summon, ID_TIMED_LIFE, 1, duration);
    G_PublishSummonEvents(caster, summon);
    return summon;
}

/* Some Warcraft summon abilities cap one authored unit type rather than all
 * results of the cast.  Keep the eviction primitive generic: callers supply
 * the Object Editor limit-check type and the retail cap, while summoned-unit
 * identity comes from the shared summon_ability marker. */
DWORD S_EnforceSummonedUnitTypeLimit(LPEDICT caster, DWORD unit_id, DWORD max_count) {
    DWORD removed = 0;

    if (!caster || !unit_id || !max_count) return 0;
    for (;;) {
        LPEDICT oldest = NULL;
        DWORD count = 0;

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
static BOOL inferno_hits(LPEDICT caster, LPEDICT target, FLOAT radius, LPCVECTOR2 origin) {
    if (!S_SpellIsAliveTarget(target) || !S_SpellIsEnemy(caster, target)) return false;
    if (Vector2_distance(&target->s.origin2, origin) > radius) return false;
    if (target->targtype == TARG_AIR) return false;
    return target->targtype == TARG_GROUND || target->targtype == TARG_STRUCTURE ||
           G_UnitIsBuilding(target->class_id);
}

/* DataA damage + Bstu Dur/HeroDur, then UnitID with DataB timed life. */
static void inferno_impact(LPEDICT caster, DWORD code, DWORD level, LPCVECTOR2 point) {
    FLOAT area = S_SpellNumber(code, ABILITY_NUMBER_AREA, level);
    int damage = (int)S_SpellData(code, level, 1);
    FLOAT life = S_SpellData(code, level, 2);
    DWORD unit_id = S_SpellUnitId(code, level);

    FILTER_EDICTS(target, inferno_hits(caster, target, area, point)) {
        S_SpellDamage(target, caster, damage);
        if (!M_IsDead(target))
            unit_addtimedstatus(target, ID_STUN_BUFF, 1, S_SpellDuration(code, level, S_UnitIsResistant(target)));
    }
    if (!unit_id) {
        fprintf(stderr, "WC3 Inferno: missing UnitID for %.4s\n", (LPCSTR)&code);
        return;
    }
    S_SummonAt(caster, unit_id, point, life);
}

void inferno_think(LPEDICT ent) {
    DWORD now = G_Time();
    if (ent->freetime && now < ent->freetime) return;
    if (!ent->owner || !ent->owner->inuse) { G_FreeEdict(ent); return; }
    inferno_impact(ent->owner, ent->class_id, (DWORD)ent->wait, &ent->s.origin2);
    G_FreeEdict(ent);
}

/* DataC<=0 impacts immediately; otherwise a thinker owns the meteor delay. */
void S_InfernoLand(LPEDICT caster, DWORD code, DWORD level, LPCVECTOR2 point) {
    FLOAT delay;
    LPEDICT thinker;
    if (!caster || !point) return;
    delay = S_SpellData(code, level, 3);
    if (delay <= 0.0f) { inferno_impact(caster, code, level, point); return; }
    thinker = G_Spawn();
    thinker->owner = caster; thinker->class_id = code; thinker->wait = (FLOAT)level;
    thinker->s.origin2 = *point; thinker->s.origin.x = point->x; thinker->s.origin.y = point->y;
    thinker->freetime = G_Time() + (DWORD)(delay * 1000.0f);
    thinker->think = inferno_think;
}

/* Name=Inferno
 * Ubertip: area damage + stun on land, then summon Infernal for DataB seconds after DataC delay.
 */
BZ_SIMPLE_SPELL_PROC(AbilityInferno) {
    S_InfernoLand(caster, spell->code, S_SpellLevel(caster, spell->code), &st.point);
}

/* Rain of Chaos resolves each landing through the Inferno ability linked by DataA. */
void rain_of_chaos_think(LPEDICT ent) {
    DWORD now = G_Time(), level = (DWORD)ent->wait, inferno = ent->damage, code = ent->class_id;
    FLOAT angle, radius;
    VECTOR2 loc = ent->s.origin2;
    if (!ent->owner || !ent->owner->inuse || !ent->resources) { G_FreeEdict(ent); return; }
    if (ent->freetime && now < ent->freetime) return;
    angle = ((FLOAT)rand() / (FLOAT)RAND_MAX) * 2.0f * (FLOAT)M_PI;
    radius = sqrtf((FLOAT)rand() / (FLOAT)RAND_MAX) * ent->collision;
    loc.x += cosf(angle) * radius; loc.y += sinf(angle) * radius;
    S_InfernoLand(ent->owner, inferno, level, &loc);
    if (!--ent->resources) { G_FreeEdict(ent); return; }
    /* Zero Dur cannot schedule the next landing; stop rather than spin every frame. */
    if (ent->velocity <= 0.0f) {
        fprintf(stderr, "WC3 Rain of Chaos: landing interval became zero for %.4s\n", (LPCSTR)&code);
        G_FreeEdict(ent); return;
    }
    ent->freetime = now + (DWORD)(ent->velocity * 1000.0f);
}

/* Unlike Rain of Fire, Rain of Chaos is not channeled: its effect owns the remaining landings after cast. */
BZ_SIMPLE_SPELL_PROC(AbilityRainOfChaos) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPEDICT thinker = G_Spawn();
    thinker->owner = caster; thinker->class_id = spell->code; thinker->s.origin2 = st.point;
    thinker->collision = MAX(0.0f, S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level));
    thinker->damage = S_SpellDataId(spell->code, level, 1);
    thinker->resources = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 2));
    thinker->velocity = MAX(0.0f, S_SpellDuration(spell->code, level, false));
    thinker->wait = (FLOAT)level; thinker->think = rain_of_chaos_think;
    rain_of_chaos_think(thinker);
}

static void summon_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    DWORD unit_id = S_SpellUnitId(spell->code, level);
    DWORD count = (DWORD)S_SpellData(spell->code, level, 1);
    FLOAT duration = S_SpellDuration(spell->code, level, false);

    if (!caster || !unit_id || !count) return;
    FOR_LOOP(i, count) {
        LPEDICT summon = summon_unit(caster, unit_id, i, count, duration);
        if (!summon) continue;
        summon->summon_ability = spell->code;
        G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, summon, NULL, true);
    }
}

/* Name=Summon Water Elemental
 * Ubertip="Summons a Water Elemental to fight for the caster."
 */
BZ_SIMPLE_SPELL_PROC(AbilityWaterElemental) {
    DWORD level = S_SpellLevel(caster, spell->code);
    DWORD unit_id = S_SpellUnitId(spell->code, level);
    DWORD count = (DWORD)S_SpellData(spell->code, level, 1);
    FLOAT duration = S_SpellDuration(spell->code, level, false);
    FLOAT distance = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    LPCSTR buff = G_AbilityLevel(spell->code, level)->buffID;
    VECTOR2 loc = caster->s.origin2;

    if (!caster || !unit_id || !count) return;
    loc.x += cosf(caster->s.angle) * distance;
    loc.y += sinf(caster->s.angle) * distance;
    FOR_LOOP(i, count) {
        FLOAT const angle = caster->s.angle + 2.0f * (FLOAT)M_PI * (FLOAT)i / (FLOAT)count;
        VECTOR2 spawn = { loc.x + cosf(angle) * MAX(32.0f, caster->collision),
                          loc.y + sinf(angle) * MAX(32.0f, caster->collision) };
        LPEDICT summon = S_SummonAt(caster, unit_id, &spawn, duration);
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
        summon->summon_ability = spell->code;
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
    DWORD level, unit_id, count;
    FLOAT duration, distance;
    VECTOR2 loc;

    if (!caster) return;
    level = S_SpellLevel(caster, spell->code);
    unit_id = S_SpellUnitId(spell->code, level);
    count = (DWORD)S_SpellData(spell->code, level, 2);
    duration = S_SpellDuration(spell->code, level, false);
    distance = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
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
        LPEDICT summon = S_SummonAt(caster, unit_id, &loc, duration);
        if (!summon) continue;
        summon->summon_ability = spell->code;
        G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_SPECIAL, 0, summon, NULL, true);
    }
}

/* Name=Force of Nature
 * Ubertip="Summons treants from a target area to fight for the caster."
 */
BZ_SIMPLE_SPELL_PROC(AbilityForceOfNature) { summon_execute(caster, st, spell); }

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
