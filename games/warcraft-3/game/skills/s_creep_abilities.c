#include "s_skills.h"

#define ID_MIND_ROT MAKEFOURCC('A','N','m','r')
#define ID_LIQUID_FIRE MAKEFOURCC('A','l','i','q')
#define ID_CORROSIVE_BREATH MAKEFOURCC('A','c','o','r')
#define BUFF_LIQUID_FIRE MAKEFOURCC('B','l','i','q')
#define BUFF_CORROSIVE_BREATH MAKEFOURCC('B','c','o','r')
#define ID_DEATH_DAMAGE_AOE MAKEFOURCC('A','d','d','a')
#define ID_FEEDBACK MAKEFOURCC('A','f','b','k')
#define ID_FEEDBACK_TOWER MAKEFOURCC('A','f','b','t')
#define ID_HARDENED_SKIN MAKEFOURCC('A','s','s','k')
#define ID_HARDENED_SKIN_NAGA MAKEFOURCC('A','n','s','k')
#define ID_MANA_REGEN_AURA MAKEFOURCC('A','a','r','m')
#define ID_ORB_ANNIHILATION MAKEFOURCC('A','N','a','k')


static BOOL unit_has_proc_ability(LPCEDICT ent, abilityProc_t proc) {
    char name[5] = {0};
    if (!ent) return false;
    if (ent->data.UnitAbilities && ent->data.UnitAbilities->abilList) {
        PARSE_LIST(ent->data.UnitAbilities->abilList, token, parse_segment) {
            abilityitem_t item;
            if (strlen(token) != 4 || !G_ActorHasSkill((LPEDICT)ent, token)) continue;
            item = S_AbilityItem(FS_SLKKey(token));
            if (item.ability && item.ability->proc == proc) return true;
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added)) {
        DWORD alias = ent->abilities.added[i];
        abilityitem_t item;
        if (!alias) continue;
        memcpy(name, &alias, 4); item = S_AbilityItem(alias);
        if (G_ActorHasSkill((LPEDICT)ent, name) && item.ability && item.ability->proc == proc) return true;
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        abilityitem_t item;
        if (!ent->heroabilities[i].code) continue;
        item = S_AbilityItem(ent->heroabilities[i].code);
        if (item.ability && item.ability->proc == proc) return true;
    }
    return false;
}

void S_CreepAttackOnHit(LPEDICT attacker, LPEDICT target) {
    DWORD level, code;
    if (!attacker || !target || !S_SpellIsEnemy(attacker, target) || M_IsDead(target)) return;
    code = G_UnitAbilityLevel(attacker, ID_MIND_ROT) ? ID_MIND_ROT : 0;
    if (code) {
        level = MAX(1u, G_UnitAbilityLevel(attacker, code));
        target->mana.value = MAX(0.0f, target->mana.value - S_SpellData(code, level, 1));
    }
    code = G_UnitAbilityLevel(attacker, ID_LIQUID_FIRE) ? ID_LIQUID_FIRE : 0;
    if (code) {
        level = MAX(1u, G_UnitAbilityLevel(attacker, code));
        unit_addtimedstatus(target, "Bliq", level, S_SpellDuration(code, level, G_UnitIsHero(target)));
    }
    code = G_UnitAbilityLevel(attacker, ID_CORROSIVE_BREATH) ? ID_CORROSIVE_BREATH : 0;
    if (code) {
        level = MAX(1u, G_UnitAbilityLevel(attacker, code));
        unit_addtimedstatus(target, "Bcor", level, S_SpellDuration(code, level, G_UnitIsHero(target)));
    }
}

FLOAT S_CreepAttackSpeedReduction(LPCEDICT unit) {
    DWORD level = unit ? G_UnitStatusLevel(unit, BUFF_LIQUID_FIRE) : 0;
    return level ? S_SpellData(ID_LIQUID_FIRE, level, 3) : 0.0f;
}

static void death_damage_aoe(LPEDICT ent, DWORD code) {
    DWORD level = MAX(1u, G_UnitAbilityLevel(ent, code));
    FLOAT full_r = S_SpellData(code, level, 1), full_d = S_SpellData(code, level, 2);
    FLOAT part_r = S_SpellData(code, level, 3), part_d = S_SpellData(code, level, 4);
    VECTOR2 origin = ent->s.origin2;
    if (part_r < full_r) part_r = full_r;
    FILTER_EDICTS(target, target != ent && S_SpellIsAliveTarget(target) && S_SpellIsEnemy(ent, target)) {
        FLOAT dist = Vector2_distance(&target->s.origin2, &origin);
        if (dist <= part_r) T_Damage(target, ent, (int)(dist <= full_r ? full_d : part_d));
    }
}

BZ_ABILITY_PROC(CAbilityDeathDamageAoe) {
    if (msg == A_DEATH) { death_damage_aoe(ent, call && call->item ? call->item->code : ID_DEATH_DAMAGE_AOE); return true; }
    return CAbilityPassive(ent, msg, call);
}
BZ_ABILITY_PROC(CAbilityMindRot) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityLiquidFire) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityCorrosiveBreath) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityLightningAttack) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityBash) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityFeedback) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityCleavingAttack) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityPulverize) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilitySpiked) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityHardenedSkin) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityAuraRegenMana) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityResistantSkin) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityCreepAura) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityReincarnation) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityOrbAnnihilation) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityTrueSight) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityAbsorb) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityChaos) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilitySpiderAttack) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityWander) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityMagicImmunity) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityEngineeringUpgrade) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityDemolish) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityFactory) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityTornadoDamage) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityRevenge) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityGhost) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityGhostVisible) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityEthereal) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityScout) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityBallsOfFire) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilitySalvage) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityTreeOfLife) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityWarp) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityGrabTree) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityDetector) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityMagicSentry) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityNeutralSpell) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityDrunkenBrawler) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilitySellItem) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilitySellUnit) { return CAbilityPassive(ent, msg, call); }

BOOL S_UnitIsResistant(LPCEDICT unit) { return G_UnitIsHero(unit) || unit_has_proc_ability(unit, CAbilityResistantSkin); }

int S_OrbAnnihilationDamage(LPEDICT attacker, int damage) {
    DWORD level = attacker ? G_UnitAbilityLevel(attacker, ID_ORB_ANNIHILATION) : 0;
    return level ? damage + (int)S_SpellData(ID_ORB_ANNIHILATION, level, 1) : damage;
}
BZ_ABILITY_PROC(CAbilitySlowAura) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityCommandAura) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityWarDrums) { return CAbilityPassive(ent, msg, call); }

int S_FeedbackDamage(LPEDICT attacker, LPEDICT target, int damage) {
    static DWORD const codes[] = { ID_FEEDBACK, ID_FEEDBACK_TOWER };
    DWORD code = 0, level, slot;
    size_t i;
    if (!attacker || !target) return damage;
    for (i = 0; i < sizeof(codes) / sizeof(*codes); i++)
        if ((level = G_UnitAbilityLevel(attacker, codes[i]))) { code = codes[i]; break; }
    if (!code || target->mana.value <= 0.0f) return damage;
    slot = G_UnitIsHero(target) ? 3 : 1;
    { FLOAT drained = MIN(target->mana.value, S_SpellData(code, level, slot));
      target->mana.value -= drained;
      return damage + (int)(drained * S_SpellData(code, level, slot + 1));
    }
}

int S_HardenedSkinDamage(LPEDICT target, int damage) {
    static DWORD const codes[] = { ID_HARDENED_SKIN, ID_HARDENED_SKIN_NAGA };
    DWORD code = 0, level;
    size_t i;
    if (!target || damage <= 0) return damage;
    for (i = 0; i < sizeof(codes) / sizeof(*codes); i++)
        if ((level = G_UnitAbilityLevel(target, codes[i]))) { code = codes[i]; break; }
    if (!code || (FLOAT)(rand() % 100) >= S_SpellData(code, level, 1)) return damage;
    return MAX((int)S_SpellData(code, level, 2), damage - (int)S_SpellData(code, level, 3));
}
