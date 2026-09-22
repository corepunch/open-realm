#include "s_skills.h"

static LPCSTR status_buff_fallback(DWORD code) {
    if (code == MAKEFOURCC('A', 'c', 'r', 'i') || code == MAKEFOURCC('A', 'C', 'c', 'r')) return "Bcri";
    if (code == MAKEFOURCC('A', 'N', 's', 'o')) return "BNso";
    return NULL;
}

static LPCSTR status_buff(abilityitem_t const *spell, DWORD level) {
    LPCSTR buff = G_AbilityLevel(spell->code, level)->buffID;
    return buff && strlen(buff) >= 4 ? buff : status_buff_fallback(spell->code);
}

static void status_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPCSTR buff = status_buff(spell, level);
    if (!st.entity || !buff) return;
    unit_addtimedstatus(st.entity, buff, level, S_SpellDuration(spell->code, level, S_UnitIsResistant(st.entity)));
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, st.entity, NULL, true);
}

/* ---- Cripple (Acri) -------------------------------------------------------
 * Name=Cripple
 * Ubertip="Reduces movement speed by <Acri,DataA1,%>%, attack rate by <Acri,DataB1,%>%, and damage by <Acri,DataC1,%>% of a target enemy unit. |nLasts <Acri,Dur1> seconds."
 */
static BOOL cripple_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)spell;
    return st.entity && S_SpellIsAliveTarget(st.entity) && S_SpellIsEnemy(caster, st.entity);
}

BZ_VALIDATED_SPELL_PROC(AbilityCripple, cripple_validate, status_execute)

/* DataA = movement speed reduction fraction; DataB = attack rate reduction; DataC = damage reduction. */
FLOAT S_CrippleMoveReduction(LPCEDICT unit) {
    DWORD level = G_UnitStatusLevel(unit, MAKEFOURCC('B', 'c', 'r', 'i'));
    return level ? S_SpellData(MAKEFOURCC('A', 'c', 'r', 'i'), level, 1) : 0.0f;
}

FLOAT S_CrippleAttackReduction(LPCEDICT unit) {
    DWORD level = G_UnitStatusLevel(unit, MAKEFOURCC('B', 'c', 'r', 'i'));
    return level ? S_SpellData(MAKEFOURCC('A', 'c', 'r', 'i'), level, 2) : 0.0f;
}

FLOAT S_CrippleDamageReduction(LPCEDICT unit) {
    DWORD level = G_UnitStatusLevel(unit, MAKEFOURCC('B', 'c', 'r', 'i'));
    return level ? S_SpellData(MAKEFOURCC('A', 'c', 'r', 'i'), level, 3) : 0.0f;
}

/* ---- Soul Burn (ANso) -----------------------------------------------------
 * Name=Soul Burn
 * Ubertip="Wreaths an enemy unit in magical flames which cause <ANso,DataA1> damage per second, prevent the casting of spells, and reduce attack damage by <ANso,DataC1,%>%.|nLasts <ANso,Dur1> seconds."
 */
#define BZ_SILENCE_BUFF MAKEFOURCC('B', 'N', 's', 'i') // rawcode; Silence (ANsi) cast lock
#define BZ_SOUL_BURN_BUFF MAKEFOURCC('B', 'N', 's', 'o') // rawcode; Soul Burn cast lock + drain

/* BNsi (Silence) and BNso (Soul Burn) both reject spell casts with "Silenced." */
BOOL S_UnitIsSilenced(LPCEDICT unit) {
    return unit && (S_UnitHasStatus(unit, BZ_SILENCE_BUFF) || S_UnitHasStatus(unit, BZ_SOUL_BURN_BUFF));
}

static BOOL soul_burn_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)spell;
    return st.entity && S_SpellIsAliveTarget(st.entity) && S_SpellIsEnemy(caster, st.entity);
}

BZ_VALIDATED_SPELL_PROC(AbilitySoulBurn, soul_burn_validate, status_execute)

/* DataA = damage per second; DataC = attack damage reduction fraction. */
FLOAT S_SoulBurnDamageRate(LPCEDICT unit) {
    DWORD level = G_UnitStatusLevel(unit, BZ_SOUL_BURN_BUFF);
    return level ? S_SpellData(MAKEFOURCC('A', 'N', 's', 'o'), level, 1) : 0.0f;
}

FLOAT S_SoulBurnDamageReduction(LPCEDICT unit) {
    DWORD level = G_UnitStatusLevel(unit, BZ_SOUL_BURN_BUFF);
    return level ? S_SpellData(MAKEFOURCC('A', 'N', 's', 'o'), level, 3) : 0.0f;
}

/* ---- Taunt (Atau) ----------------------------------------------------------
 * Name=Taunt
 * Ubertip="Forces nearby enemy units to attack the caster. Lasts <Atau,Dur1> seconds."
 * One-shot AOE that re-targets all enemies in Area to attack the caster.
 */
BZ_SIMPLE_SPELL_PROC(AbilityTaunt) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_CASTER, 0, caster, NULL, true);
    FILTER_EDICTS(target, target != caster && S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
                  Vector2_distance(&target->s.origin2, &caster->s.origin2) <= area)
        order_attack(target, caster);
}

/* ---- Poison attacks (Aven/Apoi/Apo2) ---------------------------------------
 * Name=Envenomed Spears / Poison Sting / Orb of Venom (Poison Attack)
 * Ubertip="Deals <Aven,DataA1> poison damage per second. |nLasts <Aven,Dur1> seconds."
 * Passive on-hit poison like Slow Poison. ROC omits BuffID; TFT authors the
 * "Bpoi,Bpsd" pair (Aven/Apoi) or "BIpb,BIpd" (Apo2 item orb).
 * TODO(1:1): DataA poison DPS needs the status-system periodic-damage tick
 * first, the same gap Shadow Strike documents for BEsh. This slice applies
 * the buff state with authored durations; no consumer reads Bpoi/Bpsd/BIpb/BIpd yet.
 */
#define ID_VENOM_SPEARS MAKEFOURCC('A', 'v', 'e', 'n')
#define ID_POISON_ATTACK MAKEFOURCC('A', 'p', 'o', 'i')
#define ID_POISON_ORB MAKEFOURCC('A', 'p', 'o', '2')

BZ_ABILITY_PROC(CAbilityPoisonAttack) { return CAbilityPassive(ent, msg, call); }

static DWORD const poison_codes[] = { ID_VENOM_SPEARS, ID_POISON_ATTACK, ID_POISON_ORB };

static LPCSTR poison_buffs(DWORD code) {
    static struct { DWORD code; LPCSTR buffs; } const fallback[] = {
        { ID_VENOM_SPEARS, "Bpoi,Bpsd" },
        { ID_POISON_ATTACK, "Bpoi,Bpsd" },
        { ID_POISON_ORB, "BIpb,BIpd" },
    };
    LPCSTR buffs = G_AbilityLevel(code, 1)->buffID;
    if (buffs && strlen(buffs) >= 4) return buffs;
    FOR_LOOP(i, sizeof(fallback) / sizeof(fallback[0]))
        if (fallback[i].code == code) return fallback[i].buffs;
    return NULL;
}

static void poison_apply(LPEDICT attacker, LPEDICT target, DWORD code, DWORD *seen, DWORD *count) {
    LPCSTR buffs;
    DWORD level;
    FOR_LOOP(i, *count)
        if (seen[i] == code) return;
    buffs = poison_buffs(code);
    if (!buffs) return;
    level = MAX(1, G_UnitAbilityLevel(attacker, code));
    seen[(*count)++] = code;
    while (strlen(buffs) >= 4) {
        unit_addtimedstatus(target, buffs, level, S_SpellDuration(code, level, S_UnitIsResistant(target)));
        buffs = strchr(buffs, ',');
        if (!buffs) break;
        buffs++;
    }
}

/* Called from S_ResolveAttackHit after a hit lands on an enemy. Checks native
 * poison ownership and held poison-orb items; the same poison from both applies once. */
void S_PoisonOnHit(LPEDICT attacker, LPEDICT target) {
    DWORD seen[sizeof(poison_codes) / sizeof(poison_codes[0])];
    DWORD count = 0;
    if (!attacker || !target || !S_SpellIsEnemy(attacker, target)) return;
    FOR_LOOP(o, sizeof(poison_codes) / sizeof(poison_codes[0]))
        if (G_UnitAbilityLevel(attacker, poison_codes[o])) poison_apply(attacker, target, poison_codes[o], seen, &count);
    if (!G_InventoryCanUseItems(attacker)) return;
    FOR_LOOP(i, MAX_INVENTORY) {
        LPEDICT item = attacker->inventory[i];
        LPCSTR abilities;
        if (!item) continue;
        abilities = G_ItemAbilityList(item);
        if (!abilities) continue;
        PARSE_LIST(abilities, name, parse_segment) {
            DWORD code = strlen(name) == 4 ? FS_SLKKey(name) : 0;
            FOR_LOOP(o, sizeof(poison_codes) / sizeof(poison_codes[0]))
                if (code && code == poison_codes[o]) poison_apply(attacker, target, code, seen, &count);
        }
    }
}
