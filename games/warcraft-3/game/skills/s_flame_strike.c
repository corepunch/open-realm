#include "s_skills.h"

/* Flame Strike stores its cast rank and start time; Hfs1/Hfs3 are pulse damage,
 * Hfs2/Hfs4 are pulse intervals, Cast is the delay and HeroDur is the full-damage phase. */
void flame_strike_tick(LPEDICT ent) {
    uint32_t now = G_Time(), rank = ent->resources, code = ent->class_id;
    float delay = S_SpellNumber(code, ABILITY_NUMBER_CAST, rank);
    float age = (now - ent->spawn_time) / 1000.0f - delay;
    float duration = S_SpellDuration(code, rank, false);
    float limit = S_SpellData(code, rank, 6);

    if (age > duration) { G_FreeEdict(ent); return; }
    while (ent->freetime <= now) {
        bool full = (ent->freetime - ent->spawn_time) / 1000.0f - delay <= S_SpellDuration(code, rank, true);
        float damage = S_SpellData(code, rank, full ? 1 : 3), total = 0;
        uint32_t step = (uint32_t)(S_SpellData(code, rank, full ? 2 : 4) * 1000.0f);
        if (!step) {
            fprintf(stderr, "WC3 Flame Strike: damage interval became zero for %.4s\n", (cstring_t)&code);
            G_FreeEdict(ent); return;
        }
#define BZ_FLAME_HITS(t) (S_SpellAllowsTarget(code, ent->owner, t) && \
    Vector2_distance(&(t)->s.origin2, &ent->s.origin2) <= ent->collision)
        FILTER_EDICTS(target, BZ_FLAME_HITS(target))
            total += damage * (G_UnitIsBuilding(target->class_id) ? S_SpellData(code, rank, 5) : 1);
        FILTER_EDICTS(target, BZ_FLAME_HITS(target)) {
            float hit = damage * (G_UnitIsBuilding(target->class_id) ? S_SpellData(code, rank, 5) : 1);
            if (limit > 0 && total > limit) hit *= limit / total;
            S_SpellDamage(target, ent->owner, (int)hit);
        }
#undef BZ_FLAME_HITS
        ent->freetime += step;
        if (full && (ent->freetime - ent->spawn_time) / 1000.0f - delay > S_SpellDuration(code, rank, true))
            ent->freetime = ent->spawn_time + (uint32_t)((delay + S_SpellDuration(code, rank, true) + S_SpellData(code, rank, 4)) * 1000.0f);
    }
}

/* Zero damage intervals cannot schedule a fire patch; reject malformed rows rather than substituting guessed values. */
static bool flame_strike_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t rank = S_SpellLevel(caster, spell->code);
    if (S_SpellData(spell->code, rank, 2) >= .001f && S_SpellData(spell->code, rank, 4) >= .001f) return true;
    fprintf(stderr, "WC3 Flame Strike: invalid damage intervals for %.4s rank %u\n", (cstring_t)&spell->code, rank);
    return false;
}

/* The authored delay and intervals schedule persistent ground damage independently of the caster's next order. */
static void flame_strike_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t rank = S_SpellLevel(caster, spell->code);
    LPEDICT ent = G_Spawn();
    ent->owner = caster; ent->class_id = spell->code; ent->resources = rank;
    ent->s.origin2 = st.point; ent->collision = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, rank);
    ent->spawn_time = G_Time();
    ent->freetime = G_Time() + (uint32_t)(S_SpellNumber(spell->code, ABILITY_NUMBER_CAST, rank) * 1000.0f);
    ent->think = flame_strike_tick;
}

BZ_VALIDATED_SPELL_PROC(AbilityFlameStrikeNeutral, flame_strike_validate, flame_strike_execute)
BZ_VALIDATED_SPELL_PROC(AbilityFlameStrike, flame_strike_validate, flame_strike_execute)
