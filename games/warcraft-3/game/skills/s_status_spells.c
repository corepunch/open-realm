#include "s_skills.h"

static cstring_t status_buff_fallback(uint32_t code) {
    if (code == MAKEFOURCC('A', 'c', 'r', 'i') || code == MAKEFOURCC('A', 'C', 'c', 'r')) return "Bcri";
    if (code == MAKEFOURCC('A', 'N', 's', 'o')) return "BNso";
    return NULL;
}

static cstring_t status_buff(abilityitem_t const *spell, uint32_t level) {
    cstring_t buff = G_AbilityLevel(spell->code, level)->buffID;
    return buff && strlen(buff) >= 4 ? buff : status_buff_fallback(spell->code);
}

static void status_execute(edict_t * caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    cstring_t buff = status_buff(spell, level);
    if (!st.entity || !buff) return;
    unit_addtimedstatus(st.entity, buff, level, S_SpellDuration(spell->code, level, S_UnitIsResistant(st.entity)));
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, st.entity, NULL, true);
}

/* ---- Cripple (Acri) -------------------------------------------------------
 * Name=Cripple
 * Ubertip="Reduces movement speed by <Acri,DataA1,%>%, attack rate by <Acri,DataB1,%>%, and damage by <Acri,DataC1,%>% of a target enemy unit. |nLasts <Acri,Dur1> seconds."
 */
static bool cripple_validate(edict_t * caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)spell;
    return st.entity && S_SpellIsAliveTarget(st.entity) && S_SpellIsEnemy(caster, st.entity);
}

BZ_VALIDATED_SPELL_PROC(AbilityCripple, cripple_validate, status_execute)

/* DataA = movement speed reduction fraction; DataB = attack rate reduction; DataC = damage reduction. */
float S_CrippleMoveReduction(edict_t const * unit) {
    uint32_t level = G_UnitStatusLevel(unit, MAKEFOURCC('B', 'c', 'r', 'i'));
    return level ? S_SpellData(MAKEFOURCC('A', 'c', 'r', 'i'), level, 1) : 0.0f;
}

float S_CrippleAttackReduction(edict_t const * unit) {
    uint32_t level = G_UnitStatusLevel(unit, MAKEFOURCC('B', 'c', 'r', 'i'));
    return level ? S_SpellData(MAKEFOURCC('A', 'c', 'r', 'i'), level, 2) : 0.0f;
}

float S_CrippleDamageReduction(edict_t const * unit) {
    uint32_t level = G_UnitStatusLevel(unit, MAKEFOURCC('B', 'c', 'r', 'i'));
    return level ? S_SpellData(MAKEFOURCC('A', 'c', 'r', 'i'), level, 3) : 0.0f;
}

/* ---- Soul Burn (ANso) -----------------------------------------------------
 * Name=Soul Burn
 * Ubertip="Wreaths an enemy unit in magical flames which cause <ANso,DataA1> damage per second, prevent the casting of spells, and reduce attack damage by <ANso,DataC1,%>%.|nLasts <ANso,Dur1> seconds."
 */
#define BZ_SILENCE_BUFF MAKEFOURCC('B', 'N', 's', 'i') // rawcode; Silence (ANsi) cast lock
#define BZ_SOUL_BURN_BUFF MAKEFOURCC('B', 'N', 's', 'o') // rawcode; Soul Burn cast lock + drain

/* BNsi (Silence) and BNso (Soul Burn) both reject spell casts with "Silenced." */
bool S_UnitIsSilenced(edict_t const * unit) {
    return unit && (S_UnitHasStatus(unit, BZ_SILENCE_BUFF) || S_UnitHasStatus(unit, BZ_SOUL_BURN_BUFF));
}

static bool soul_burn_validate(edict_t * caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)spell;
    return st.entity && S_SpellIsAliveTarget(st.entity) && S_SpellIsEnemy(caster, st.entity);
}

BZ_VALIDATED_SPELL_PROC(AbilitySoulBurn, soul_burn_validate, status_execute)

/* DataA = damage per second; DataC = attack damage reduction fraction. */
float S_SoulBurnDamageRate(edict_t const * unit) {
    uint32_t level = G_UnitStatusLevel(unit, BZ_SOUL_BURN_BUFF);
    return level ? S_SpellData(MAKEFOURCC('A', 'N', 's', 'o'), level, 1) : 0.0f;
}

float S_SoulBurnDamageReduction(edict_t const * unit) {
    uint32_t level = G_UnitStatusLevel(unit, BZ_SOUL_BURN_BUFF);
    return level ? S_SpellData(MAKEFOURCC('A', 'N', 's', 'o'), level, 3) : 0.0f;
}

/* ---- Taunt (Atau) ----------------------------------------------------------
 * Name=Taunt
 * Ubertip="Forces nearby enemy units to attack the caster. Lasts <Atau,Dur1> seconds."
 * One-shot AOE that re-targets all enemies in Area to attack the caster.
 */
BZ_SIMPLE_SPELL_PROC(AbilityTaunt) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
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

static uint32_t const poison_codes[] = { ID_VENOM_SPEARS, ID_POISON_ATTACK, ID_POISON_ORB };

static cstring_t poison_buffs(uint32_t code) {
    static struct { uint32_t code; cstring_t buffs; } const fallback[] = {
        { ID_VENOM_SPEARS, "Bpoi,Bpsd" },
        { ID_POISON_ATTACK, "Bpoi,Bpsd" },
        { ID_POISON_ORB, "BIpb,BIpd" },
    };
    cstring_t buffs = G_AbilityLevel(code, 1)->buffID;
    if (buffs && strlen(buffs) >= 4) return buffs;
    FOR_LOOP(i, sizeof(fallback) / sizeof(fallback[0]))
        if (fallback[i].code == code) return fallback[i].buffs;
    return NULL;
}

static void poison_apply(edict_t * attacker, edict_t * target, uint32_t code, uint32_t *seen, uint32_t *count) {
    cstring_t buffs;
    uint32_t level;
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
void S_PoisonOnHit(edict_t * attacker, edict_t * target) {
    uint32_t seen[sizeof(poison_codes) / sizeof(poison_codes[0])];
    uint32_t count = 0;
    if (!attacker || !target || !S_SpellIsEnemy(attacker, target)) return;
    FOR_LOOP(o, sizeof(poison_codes) / sizeof(poison_codes[0]))
        if (G_UnitAbilityLevel(attacker, poison_codes[o])) poison_apply(attacker, target, poison_codes[o], seen, &count);
    if (!G_InventoryCanUseItems(attacker)) return;
    FOR_LOOP(i, MAX_INVENTORY) {
        edict_t * item = attacker->inventory[i];
        cstring_t abilities;
        if (!item) continue;
        abilities = G_ItemAbilityList(item);
        if (!abilities) continue;
        PARSE_LIST(abilities, name, parse_segment) {
            uint32_t code = strlen(name) == 4 ? FS_SLKKey(name) : 0;
            FOR_LOOP(o, sizeof(poison_codes) / sizeof(poison_codes[0]))
                if (code && code == poison_codes[o]) poison_apply(attacker, target, code, seen, &count);
        }
    }
}
