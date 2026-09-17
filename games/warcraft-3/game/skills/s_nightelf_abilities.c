#include "s_skills.h"

/* Night Elf passive attack abilities: Moon Glaive (Amgl/Amgr) and Slow Poison (Aspo). */

#define ID_MOON_GLAIVE  MAKEFOURCC('A','m','g','l')
#define ID_MOON_GLAIVER MAKEFOURCC('A','m','g','r')
#define ID_SLOW_POISON  MAKEFOURCC('A','s','p','o')
#define BUFF_SLOW_POI   MAKEFOURCC('B','s','p','o')

/* ---- Moon Glaive (Amgl / Amgr): passive attack bounce -------------------- */

BZ_ABILITY_PROC(CAbilityMoonGlaive) { return CAbilityPassive(ent, msg, call); }

/* Called from S_ResolveAttackHit after the primary hit lands.  Bounces the
 * attack to DataA-1 additional enemies within Area of the current bounce
 * target.  Damage is fixed at the same value as the primary hit (no falloff). */
void S_MoonGlaiveAttack(LPEDICT attacker, LPEDICT primary, int damage) {
    DWORD level = G_UnitAbilityLevel(attacker, ID_MOON_GLAIVE);
    if (!level) level = G_UnitAbilityLevel(attacker, ID_MOON_GLAIVER);
    if (!level || !primary) return;
    DWORD total = (DWORD)MAX(1.0f, S_SpellData(ID_MOON_GLAIVE, level, 1));
    FLOAT range = S_SpellNumber(ID_MOON_GLAIVE, ABILITY_NUMBER_AREA, level);
    LPEDICT visited[8];
    DWORD nvisited = 0;
    if (nvisited < 8) visited[nvisited++] = primary;
    LPEDICT current = primary;
    for (DWORD i = 1; i < total; i++) {
        LPEDICT next = NULL;
        FILTER_EDICTS(other, other != attacker && S_SpellIsAliveTarget(other) &&
                      S_SpellIsEnemy(attacker, other) &&
                      Vector2_distance(&other->s.origin2, &current->s.origin2) <= range) {
            BOOL seen = false;
            FOR_LOOP(j, nvisited) seen |= other == visited[j];
            if (!seen) { next = other; break; }
        }
        if (!next) break;
        if (nvisited < 8) visited[nvisited++] = next;
        T_Damage(next, attacker, G_AttackDamage(attacker, next, damage));
        current = next;
    }
}

/* ---- Slow Poison (Aspo): passive poison on hit --------------------------- */

BZ_ABILITY_PROC(CAbilitySlowPoison) { return CAbilityPassive(ent, msg, call); }

/* Called from S_ResolveAttackHit after a hit lands on an enemy.  Applies the
 * Bspo buff which the movement and attack-speed hooks read each frame. */
void S_SlowPoisonOnHit(LPEDICT attacker, LPEDICT target) {
    DWORD level = G_UnitAbilityLevel(attacker, ID_SLOW_POISON);
    if (!level || !target || !S_SpellIsEnemy(attacker, target)) return;
    unit_addtimedstatus(target, "Bspo", level, S_SpellDuration(ID_SLOW_POISON, level, false));
}

/* Movement-speed reduction fraction while Bspo is active on unit. */
FLOAT S_SlowPoisonMoveReduction(LPCEDICT unit) {
    DWORD level = G_UnitStatusLevel(unit, BUFF_SLOW_POI);
    if (!level) return 0.0f;
    return S_SpellData(ID_SLOW_POISON, level, 2) * 0.01f;
}

/* Attack-speed reduction fraction while Bspo is active on unit. */
FLOAT S_SlowPoisonAttackReduction(LPCEDICT unit) {
    DWORD level = G_UnitStatusLevel(unit, BUFF_SLOW_POI);
    if (!level) return 0.0f;
    return S_SpellData(ID_SLOW_POISON, level, 3) * 0.01f;
}
