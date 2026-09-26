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


static bool unit_has_proc_ability(edict_t const *ent, abilityProc_t proc) {
    char name[5] = {0};
    if (!ent) return false;
    if (ent->data.UnitAbilities && ent->data.UnitAbilities->abilList) {
        PARSE_LIST(ent->data.UnitAbilities->abilList, token, parse_segment) {
            abilityitem_t item;
            if (strlen(token) != 4 || !G_ActorHasSkill((edict_t *)ent, token)) continue;
            item = S_AbilityItem(FS_SLKKey(token));
            if (item.ability && item.ability->proc == proc) return true;
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added)) {
        uint32_t alias = ent->abilities.added[i];
        abilityitem_t item;
        if (!alias) continue;
        memcpy(name, &alias, 4); item = S_AbilityItem(alias);
        if (G_ActorHasSkill((edict_t *)ent, name) && item.ability && item.ability->proc == proc) return true;
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        abilityitem_t item;
        if (!ent->heroabilities[i].code) continue;
        item = S_AbilityItem(ent->heroabilities[i].code);
        if (item.ability && item.ability->proc == proc) return true;
    }
    return false;
}

void S_CreepAttackOnHit(edict_t *attacker, edict_t *target) {
    uint32_t level, code;
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

float S_CreepAttackSpeedReduction(edict_t const *unit) {
    uint32_t level = unit ? G_UnitStatusLevel(unit, BUFF_LIQUID_FIRE) : 0;
    return level ? S_SpellData(ID_LIQUID_FIRE, level, 3) : 0.0f;
}

/* Death Damage AOE is physical damage: authored target classes/alliance apply,
 * while spell immunity/invisibility do not suppress an explosion. */
static bool death_damage_aoe_allows(uint32_t code, uint32_t ability_level, edict_t * source, edict_t * target) {
    abilityLevel_t const *row;
    cstring_t targets;
    bool structure;

    if (!source || !S_SpellIsAliveTarget(target) || target == source || S_UnitIsCycloned(target)) return false;
    row = G_AbilityLevel(code, ability_level);
    targets = row ? row->targs : NULL;
    if (!targets || !*targets) return S_SpellIsEnemy(source, target);

    structure = target->targtype == TARG_STRUCTURE || G_UnitIsBuilding(target->class_id);
    if ((strstr(targets, "air") || strstr(targets, "ground") || strstr(targets, "structure")) &&
        !(strstr(targets, "air") && target->targtype == TARG_AIR) &&
        !(strstr(targets, "ground") && target->targtype == TARG_GROUND) &&
        !(strstr(targets, "structure") && structure)) return false;
    if (strstr(targets, "organic") && target->targtype == TARG_MECHANICAL) return false;
    if (strstr(targets, "mechanical") && target->targtype != TARG_MECHANICAL) return false;

    if (strstr(targets, "player") && target->s.player == source->s.player) return true;
    if (strstr(targets, "friend") && S_SpellIsFriend(source, target)) return true;
    if (strstr(targets, "enemy") && S_SpellIsEnemy(source, target)) return true;
    if (strstr(targets, "neutral") && target->s.player < MAX_PLAYERS && level.mapinfo &&
        level.mapinfo->players[target->s.player].playerType == kPlayerTypeNeutral) return true;
    return !strstr(targets, "player") && !strstr(targets, "friend") &&
        !strstr(targets, "enemy") && !strstr(targets, "neutral");
}

/* AIdm was the old tree/wall helper; retail Amnx/Adda now uses its own
 * Targets Allowed list for destructibles. Destructibles have no player
 * alliance, so only their authored target class participates here. */
static bool death_damage_aoe_allows_destructable(uint32_t code, uint32_t ability_level, edict_t const * target) {
    abilityLevel_t const *row;
    cstring_t targets;

    if (!target || !G_IsDestructable(target) || target->destructable.dead || target->invulnerable) return false;
    row = G_AbilityLevel(code, ability_level);
    targets = row ? row->targs : NULL;
    if (!targets || !*targets) return false;
    switch (target->targtype) {
    case TARG_TREE:       return strstr(targets, "tree") != NULL;
    case TARG_WALL:       return strstr(targets, "wall") != NULL;
    case TARG_DEBRIS:     return strstr(targets, "debris") != NULL;
    case TARG_BRIDGE:     return strstr(targets, "bridge") != NULL;
    case TARG_DECORATION: return strstr(targets, "decoration") != NULL;
    default:              return false;
    }
}

static void death_damage_aoe_apply(edict_t * source, uint32_t code, uint32_t level, vec2_t const * origin) {
    float full_r = S_SpellData(code, level, 1), full_d = S_SpellData(code, level, 2);
    float part_r = S_SpellData(code, level, 3), part_d = S_SpellData(code, level, 4);
    if (!source || !origin || (full_d <= 0.0f && part_d <= 0.0f)) return;
    if (part_r < full_r) part_r = full_r;
    FILTER_EDICTS(target, death_damage_aoe_allows(code, level, source, target)) {
        float dist = Vector2_distance(&target->s.origin2, origin);
        float damage;
        if (dist > part_r) continue;
        damage = dist <= full_r ? full_d : part_d;
        if (damage > 0.0f) T_Damage(target, source, (int)damage);
    }
    FILTER_EDICTS(target, death_damage_aoe_allows_destructable(code, level, target)) {
        float dist = Vector2_distance(&target->s.origin2, origin);
        float damage;
        if (dist > part_r) continue;
        damage = dist <= full_r ? full_d : part_d;
        if (damage > 0.0f) G_DestructableApplyDamage(target, source, damage);
    }
}

/* Delayed Amnx/Adda damage snapshots only the death position. Victims are
 * enumerated when Duration expires, preserving retail's fast-unit escape. */
void death_damage_aoe_think(edict_t * thinker) {
    edict_t * source;
    uint32_t code, level;

    if (!thinker || !thinker->inuse) return;
    if (G_Time() < thinker->freetime) return;
    source = thinker->owner;
    if (!source || !source->inuse || source->spawn_time != thinker->channel.owner_spawn_time) source = thinker;
    code = thinker->class_id;
    level = MAX(1u, (uint32_t)thinker->wait);
    death_damage_aoe_apply(source, code, level, &thinker->s.origin2);
    G_FreeEdict(thinker);
}

static void death_damage_aoe(edict_t * ent, uint32_t code) {
    uint32_t level;
    float delay;
    edict_t * thinker;

    if (!ent || !code) return;
    level = MAX(1u, G_UnitAbilityLevel(ent, code));
    delay = S_SpellDuration(code, level, false);
    if (delay <= 0.0f) {
        death_damage_aoe_apply(ent, code, level, &ent->s.origin2);
        return;
    }

    thinker = G_Spawn();
    if (!thinker) {
        /* HACK: edict exhaustion leaves no timer entity; resolve damage now rather than lose the death effect. */
        fprintf(stderr, "WC3 death AOE: no thinker for ability %c%c%c%c on unit %u; resolving immediately\n",
                (char)(code & 255), (char)((code >> 8) & 255), (char)((code >> 16) & 255), (char)(code >> 24), ent->s.number);
        death_damage_aoe_apply(ent, code, level, &ent->s.origin2);
        return;
    }
    thinker->owner = ent;
    thinker->channel.owner_spawn_time = ent->spawn_time;
    thinker->class_id = code;
    thinker->wait = (float)level;
    thinker->s.origin2 = ent->s.origin2;
    thinker->s.player = ent->s.player;
    thinker->freetime = G_Time() + (uint32_t)(delay * 1000.0f);
    thinker->think = death_damage_aoe_think;
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
BZ_ABILITY_PROC(CAbilityGrabTree) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityDetector) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityMagicSentry) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityNeutralSpell) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityDrunkenBrawler) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilitySellItem) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilitySellUnit) { return CAbilityPassive(ent, msg, call); }

bool S_UnitIsResistant(edict_t const *unit) { return G_UnitIsHero(unit) || unit_has_proc_ability(unit, CAbilityResistantSkin); }

int S_OrbAnnihilationDamage(edict_t *attacker, int damage) {
    uint32_t level = attacker ? G_UnitAbilityLevel(attacker, ID_ORB_ANNIHILATION) : 0;
    return level ? damage + (int)S_SpellData(ID_ORB_ANNIHILATION, level, 1) : damage;
}
BZ_ABILITY_PROC(CAbilitySlowAura) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityCommandAura) { return CAbilityPassive(ent, msg, call); }
BZ_ABILITY_PROC(CAbilityWarDrums) { return CAbilityPassive(ent, msg, call); }

int S_FeedbackDamage(edict_t *attacker, edict_t *target, int damage) {
    static uint32_t const codes[] = { ID_FEEDBACK, ID_FEEDBACK_TOWER };
    uint32_t code = 0, level, slot;
    size_t i;
    if (!attacker || !target) return damage;
    for (i = 0; i < sizeof(codes) / sizeof(*codes); i++)
        if ((level = G_UnitAbilityLevel(attacker, codes[i]))) { code = codes[i]; break; }
    if (!code || target->mana.value <= 0.0f) return damage;
    slot = G_UnitIsHero(target) ? 3 : 1;
    { float drained = MIN(target->mana.value, S_SpellData(code, level, slot));
      target->mana.value -= drained;
      return damage + (int)(drained * S_SpellData(code, level, slot + 1));
    }
}

int S_HardenedSkinDamage(edict_t *target, int damage) {
    static uint32_t const codes[] = { ID_HARDENED_SKIN, ID_HARDENED_SKIN_NAGA };
    uint32_t code = 0, level;
    size_t i;
    if (!target || damage <= 0) return damage;
    for (i = 0; i < sizeof(codes) / sizeof(*codes); i++)
        if ((level = G_UnitAbilityLevel(target, codes[i]))) { code = codes[i]; break; }
    if (!code || (float)(rand() % 100) >= S_SpellData(code, level, 1)) return damage;
    return MAX((int)S_SpellData(code, level, 2), damage - (int)S_SpellData(code, level, 3));
}
