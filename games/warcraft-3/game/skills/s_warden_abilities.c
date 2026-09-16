#include "s_skills.h"

/* Night Elf Warden (Maiev) hero abilities: Blink, Fan of Knives, Shadow Strike. */

#define ID_BLINK        MAKEFOURCC('A', 'E', 'b', 'l')

/* ---- Blink (AEbl): instant teleport to a target point within range -------- */

static BOOL blink_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT maxrange = S_SpellData(spell->code, level, 1);
    FLOAT minrange = S_SpellData(spell->code, level, 2);
    FLOAT dist = Vector2_distance(&caster->s.origin2, &st.point);

    if (maxrange > 0 && dist > maxrange) return false;
    if (minrange > 0 && dist < minrange) return false;
    return true;
}

static void blink_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_SPECIAL, 0, caster, NULL, true);
    VECTOR2 dest = st.point;
    CM_ClosestPathablePointForRadiusFlags(&st.point, caster->collision, M_UnitStaticPathingFlags(caster), &dest);
    caster->s.origin2 = dest;
    caster->s.origin.x = dest.x;
    caster->s.origin.y = dest.y;
    gi.LinkEntity(caster);
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_AREA_EFFECT, 0, caster, NULL, true);
}

/* ---- Registration -------------------------------------------------------- */

BZ_VALIDATED_SPELL_PROC(AbilityBlink, blink_validate, blink_execute)

/* ---- Fan of Knives (AEfk): instant area damage centred on the caster ------ */

BZ_SIMPLE_SPELL_PROC(AbilityFanOfKnives) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT radius = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    FLOAT damage = MAX(1.0f, S_SpellData(spell->code, level, 1));
    FLOAT maxtotal = S_SpellData(spell->code, level, 2);
    DWORD ntargets = 0;

    if (!caster) return;
    if (radius <= 0.0f) radius = 400.0f;

#define FOK_HITS(t) ((t)->inuse && (t) != caster && S_SpellIsEnemy(caster, t) && \
                     S_SpellAllowsTarget(spell->code, caster, t) &&               \
                     Vector2_distance(&(t)->s.origin2, &caster->s.origin2) <= radius)

    FILTER_EDICTS(target, FOK_HITS(target))
        ntargets++;
    if (maxtotal > 0.0f && ntargets > 0 && damage * (FLOAT)ntargets > maxtotal)
        damage = MAX(1.0f, maxtotal / (FLOAT)ntargets);
    FILTER_EDICTS(target, FOK_HITS(target))
        S_SpellDamage(target, caster, (DWORD)damage);
#undef FOK_HITS
}

/* ---- Shadow Strike (AEsh): single-target nuke ----------------------------- */

BZ_SIMPLE_SPELL_PROC(AbilityShadowStrike) {
    LPEDICT target = st.entity;
    DWORD level = S_SpellLevel(caster, spell->code);
    DWORD damage = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 5)); /* DataE = Initial Damage */

    S_SpellDamage(target, caster, damage);
    /* TODO(1:1): Shadow Strike also applies a movement slow and a decaying
     * poison DoT via the BEsh buff.  The status system needs a movement-speed
     * modifier and a periodic-damage tick first. */
}
