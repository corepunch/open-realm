#include "s_skills.h"
#include "../g_entity_set.h"

#define ID_BRILLIANCE MAKEFOURCC('A', 'H', 'a', 'b')
#define ID_DEVOTION_AURA MAKEFOURCC('A', 'H', 'a', 'd')
#define ID_CRITICAL_STRIKE MAKEFOURCC('A', 'O', 'c', 'r')
#define ID_CREEP_CRITICAL_STRIKE MAKEFOURCC('A', 'C', 'c', 't')
#define ID_SPIKED_CARAPACE MAKEFOURCC('A', 'U', 't', 's')
#define ID_SPIKED_BARRICADES MAKEFOURCC('A', 's', 'p', 'i')
#define ID_PULVERIZE MAKEFOURCC('A', 'w', 'a', 'r')
#define ID_UNHOLY_AURA MAKEFOURCC('A', 'U', 'a', 'u')
#define ID_EVASION MAKEFOURCC('A', 'E', 'e', 'v')
#define ID_VAMPIRIC_AURA MAKEFOURCC('A', 'U', 'a', 'v')
#define ID_THORNS_AURA MAKEFOURCC('A', 'E', 'a', 'h')
#define ID_MANA_SHIELD MAKEFOURCC('A', 'N', 'm', 's')
#define ID_DRUNKEN_BRAWLER MAKEFOURCC('A', 'N', 'd', 'b')
#define ID_SEARING_ARROWS MAKEFOURCC('A', 'H', 'f', 'a')
#define ID_POISON_ARROWS MAKEFOURCC('A', 'E', 'p', 'a')
#define ID_TRUESHOT_AURA MAKEFOURCC('A', 'E', 'a', 'r')
#define ID_SLOW_AURA MAKEFOURCC('A', 'a', 's', 'l')
#define ID_COMMAND_AURA MAKEFOURCC('A', 'C', 'a', 'c')
#define ID_COMMAND_AURA_NEUTRAL MAKEFOURCC('A', 'O', 'a', 'c')
#define ID_WAR_DRUMS MAKEFOURCC('A', 'a', 'k', 'b')

#define ID_REGEN_LIFE_ORC MAKEFOURCC('A', 'o', 'a', 'r')
#define ID_REGEN_LIFE_BLIGHT MAKEFOURCC('A', 'a', 'b', 'r')
#define ID_REGEN_MANA MAKEFOURCC('A', 'a', 'r', 'm')

typedef struct {
    uint32_t alias;
    uint32_t level;
} auraAbilityRef_t;

typedef struct {
    uint32_t code, data;
} aura_cache_key_t;

enum { HERO_AURA_CACHE_KEYS = 10 };

static aura_cache_key_t const aura_cache_keys[HERO_AURA_CACHE_KEYS] = {
    { ID_BRILLIANCE, 1 },
    { ID_DEVOTION_AURA, 1 },
    { ID_UNHOLY_AURA, 1 },
    { ID_UNHOLY_AURA, 2 },
    { ID_VAMPIRIC_AURA, 1 },
    { ID_TRUESHOT_AURA, 1 },
    { ID_THORNS_AURA, 1 }
    ,{ ID_COMMAND_AURA, 1 }
    ,{ ID_COMMAND_AURA_NEUTRAL, 1 }
    ,{ ID_WAR_DRUMS, 1 }
};

typedef struct {
    edict_t *source;
    uint32_t spawn;
    uint32_t combat_mask;
    auraAbilityRef_t life_orc;
    auraAbilityRef_t life_blight;
    auraAbilityRef_t mana;
    auraAbilityRef_t devotion;
    auraAbilityRef_t unholy;
    auraAbilityRef_t combat[HERO_AURA_CACHE_KEYS];
    abilityLevel_t const *combat_rows[HERO_AURA_CACHE_KEYS];
} regenAuraSource_t;

typedef enum {
    REGEN_FAMILY_LIFE_ORC,
    REGEN_FAMILY_LIFE_BLIGHT,
    REGEN_FAMILY_MANA,
    REGEN_FAMILY_COUNT
} regenFamily_t;

typedef enum {
    REGEN_VALUE_NORMAL,
    REGEN_VALUE_MAXIMUM,
    REGEN_VALUE_COUNT
} regenValue_t;

static regenAuraSource_t regen_sources[MAX_ENTITIES];
static uint32_t regen_source_count;
static entitySet_t regen_members, combat_members, regen_changed, slow_members, slow_changed;
typedef struct { edict_t *unit; uint32_t spawn; auraAbilityRef_t ability; } slowAuraSource_t;
static slowAuraSource_t slow_sources[MAX_ENTITIES];
static uint32_t slow_generation;
static uint32_t overlay_cache_frame = UINT_MAX;
static bool slow_dirty=true, slow_pending;
static uint32_t regen_cache_frame = UINT_MAX;
static uint32_t regen_cache_generation = UINT_MAX;
static bool regen_sources_dirty=true;
static edict_t *regen_overlays[MAX_ENTITIES][REGEN_FAMILY_COUNT];
static edict_t *devotion_overlays[MAX_ENTITIES];
static edict_t *unholy_overlays[MAX_ENTITIES];
static uint32_t devotion_recipient_buff[MAX_ENTITIES];
static uint32_t unholy_recipient_buff[MAX_ENTITIES];
static uint32_t regen_value_next_update[MAX_ENTITIES];
static uint32_t regen_visual_next_update[MAX_ENTITIES];
static void const *regen_value_ability_data[MAX_ENTITIES];

static float aura_cache[MAX_ENTITIES][sizeof(aura_cache_keys) / sizeof(*aura_cache_keys)];
static uint32_t aura_cache_next_update[MAX_ENTITIES];
static uint32_t aura_cache_generation[MAX_ENTITIES];
static uint32_t aura_cache_last_time = UINT_MAX;
#ifdef BZ_TESTS
static uint32_t test_hero_aura_alias_resolves,test_slow_aura_visits;
static uint32_t test_aura_discovery_visits, test_aura_overlay_visits, test_slow_discovery_visits;
static uint32_t test_aura_membership_resolves;
static uint32_t test_combat_aura_visits;
static uint32_t test_combat_aura_eligibility_checks;
static uint32_t test_authored_aura_classifications;
uint32_t S_TestSlowAuraVisits(bool reset) {
    uint32_t result=test_slow_aura_visits;
    if(reset)test_slow_aura_visits=0;
    return result;
}
void S_TestResetHeroAuraAliasResolves(void) { test_hero_aura_alias_resolves = 0; }
uint32_t S_TestHeroAuraAliasResolves(void) { return test_hero_aura_alias_resolves; }
#endif

void S_InvalidateAuraSources(void) {
    regen_cache_frame=regen_cache_generation=overlay_cache_frame=UINT_MAX;
    regen_sources_dirty=true;
    slow_dirty=true;
    S_InvalidateEnduranceSources();
}
/* A local ownership mutation must not rediscover every other unit. Non-world
 * fixtures have no stable slot, so their notification uses complete invalidation. */
void S_MarkAuraSource(edict_t const *unit) {
    uintptr_t offset = (uintptr_t)unit - (uintptr_t)g_edicts;
    if (!unit || !g_edicts || offset >= sizeof(*unit) * MAX_ENTITIES || offset % sizeof(*unit)) {
        S_InvalidateAuraSources();
        return;
    }
    uint32_t index = offset / sizeof(*unit);
    entity_set_put(&regen_changed, index, true);
    entity_set_put(&slow_changed, index, true);
    slow_pending = true;
    regen_cache_frame = UINT_MAX;
    S_MarkEnduranceSource(unit);
}

static regenFamily_t regen_family(uint32_t base_code);

/* Invalidate deadlines when a new map or test resets the simulation clock. */
static void aura_cache_update_time(void) {
    if (level.time < aura_cache_last_time) {
        memset(regen_value_next_update, 0, sizeof(regen_value_next_update));
        memset(regen_visual_next_update, 0, sizeof(regen_visual_next_update));
        memset(regen_value_ability_data, 0, sizeof(regen_value_ability_data));
        memset(aura_cache_next_update, 0, sizeof(aura_cache_next_update));
        memset(aura_cache_generation, 0, sizeof(aura_cache_generation));
        memset(devotion_recipient_buff, 0, sizeof(devotion_recipient_buff));
        memset(unholy_recipient_buff, 0, sizeof(unholy_recipient_buff));
    }
    aura_cache_last_time = level.time;
}

/* Shared owned-alias resolver: actual rawcode plus actual rank for base_code,
 * across native abilList, runtime-added abilities (honoring removals) and
 * ranked hero slots, via the authored code mapping. */
abilityAliasRef_t S_ResolveAbilityAlias(edict_t *ent, uint32_t base_code) {
#ifdef BZ_TESTS
    test_aura_membership_resolves++;
#endif
    abilityAliasRef_t result = {0};
    char alias_name[5] = {0};
    if (!ent || !base_code) return result;
    uint32_t authored_count;
    unitAbilityToken_t const *authored = G_UnitAbilityTokens(ent->data.UnitAbilities, &authored_count);
    FOR_LOOP(i, authored_count) {
        uint32_t alias = authored[i].code;
        if (authored[i].length != 4 || (alias != base_code && authored[i].base != base_code)) continue;
        if (!G_ActorHasAbilityCode(ent, alias)) continue;
        result.alias = alias;
        result.level = 1;
        return result;
    }
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added)) {
        uint32_t const alias = ent->abilities.added[i];
        if (!alias) continue;
        memcpy(alias_name, &alias, 4);
        if ((alias == base_code || G_AbilityCode(alias) == base_code) && G_ActorHasSkill(ent, alias_name)) {
            result.alias = alias;
            result.level = 1;
            return result;
        }
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        heroability_t const *hero = ent->heroabilities + i;
        if (hero->level && (hero->code == base_code || G_AbilityCode(hero->code) == base_code)) {
            result.alias = hero->code;
            result.level = hero->level;
            return result;
        }
    }
    return result;
}

/* Binding ordinary combat units changes their abilities, but cannot alter
 * aura-provider membership. Keep that common spawn path local to its owner. */
static bool aura_source_base(uint32_t code, uint32_t base) {
    static uint32_t const extra[]={ID_REGEN_LIFE_ORC,ID_REGEN_LIFE_BLIGHT,ID_REGEN_MANA,
        ID_SLOW_AURA,MAKEFOURCC('A','O','a','e')};
    FOR_LOOP(i,HERO_AURA_CACHE_KEYS)
        if(code == aura_cache_keys[i].code || base == aura_cache_keys[i].code)return true;
    FOR_LOOP(i,sizeof(extra)/sizeof(*extra))
        if(code == extra[i] || base == extra[i])return true;
    return false;
}

static bool aura_source_code(uint32_t code) { return aura_source_base(code,G_AbilityCode(code)); }

typedef struct {
    UnitAbilities_t const *row;
    cstring_t list;
    uint32_t unit_generation, ability_generation;
    bool valid, candidate;
} authoredAuraType_t;
static authoredAuraType_t authored_aura_types[512];

/* Cache the type predicate, never the unit's mutable ownership. Most combat
 * types have no authored aura; additions and learned ranks remain live. */
static bool authored_aura_candidate(UnitAbilities_t const *row) {
    if (!row) return false;
    uintptr_t key = (uintptr_t)row >> 4;
    key ^= key >> 16;
    authoredAuraType_t *entry = authored_aura_types + key % (sizeof(authored_aura_types) / sizeof(*authored_aura_types));
    uint32_t unit_generation = G_UnitDataGeneration(), ability_generation = G_AbilityDataGeneration();
    if (entry->valid && entry->row == row && entry->list == row->abilList &&
        entry->unit_generation == unit_generation && entry->ability_generation == ability_generation)
        return entry->candidate;
    bool candidate = false;
    uint32_t count;
    unitAbilityToken_t const *tokens = G_UnitAbilityTokens(row, &count);
    FOR_LOOP(i, count) {
#ifdef BZ_TESTS
        test_authored_aura_classifications++;
#endif
        if (tokens[i].length == 4 && aura_source_base(tokens[i].code, tokens[i].base)) {
            candidate = true; break;
        }
    }
    *entry = (authoredAuraType_t){ .row = row, .list = row->abilList, .unit_generation = unit_generation,
        .ability_generation = ability_generation, .valid = true, .candidate = candidate };
    return candidate;
}

bool S_UnitHasAuraSource(edict_t *unit) {
    if(!unit || (unit->svflags&SVF_STATIC_SCENERY))return false;
    /* Membership is a union of families: inspect each owned rawcode once.
     * Hero slots retain the resolver's independent learned-rank contract. */
    if (authored_aura_candidate(unit->data.UnitAbilities)) {
        uint32_t authored_count;
        unitAbilityToken_t const *authored = G_UnitAbilityTokens(unit->data.UnitAbilities, &authored_count);
        FOR_LOOP(i, authored_count)
            if (authored[i].length==4 && aura_source_base(authored[i].code,authored[i].base) &&
                G_ActorHasAbilityCode(unit,authored[i].code)) return true;
    }
    FOR_LOOP(i, ARRAY_COUNT(unit->abilities.added)) {
        uint32_t code = unit->abilities.added[i];
        char name[5] = {0};
        memcpy(name, &code, sizeof(code));
        if(code && aura_source_code(code) && G_ActorHasSkill(unit, name))return true;
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES)
        if(unit->heroabilities[i].level && aura_source_code(unit->heroabilities[i].code))return true;
    return false;
}

static auraAbilityRef_t actor_aura_ability(edict_t *ent, uint32_t base_code) {
#ifdef BZ_TESTS
    test_hero_aura_alias_resolves++;
#endif
    abilityAliasRef_t resolved = S_ResolveAbilityAlias(ent, base_code);
    auraAbilityRef_t result = { resolved.alias, resolved.level };
    return result;
}

static auraAbilityRef_t unit_ability_with_proc(edict_t *ent, abilityProc_t proc) {
    auraAbilityRef_t result = {0};
    char alias_name[5] = {0};
    if (!ent || !proc) return result;
    uint32_t count;
    uint32_t const *codes=G_UnitAbilityCodes(ent->data.UnitAbilities,&count);
    FOR_LOOP(i,count) {
        uint32_t alias=codes[i];
        if(!G_ActorHasAbilityCode(ent,alias))continue;
        abilityitem_t item=S_AbilityItem(alias);
        if(item.ability && item.ability->proc==proc) {
            result.alias=alias;result.level=1;return result;
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added)) {
        uint32_t const alias = ent->abilities.added[i];
        abilityitem_t item;
        if (!alias) continue;
        memcpy(alias_name, &alias, 4);
        item = S_AbilityItem(alias);
        if (G_ActorHasSkill(ent, alias_name) && item.ability && item.ability->proc == proc) {
            result.alias = alias; result.level = 1; return result;
        }
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        heroability_t const *hero = ent->heroabilities + i;
        abilityitem_t item;
        if (!hero->level) continue;
        item = S_AbilityItem(hero->code);
        if (item.ability && item.ability->proc == proc) {
            result.alias = hero->code; result.level = hero->level; return result;
        }
    }
    return result;
}

static auraAbilityRef_t mana_shield_ability(edict_t *ent) {
    auraAbilityRef_t ref = actor_aura_ability(ent, ID_MANA_SHIELD);
    return ref.alias ? ref : unit_ability_with_proc(ent, CAbilityManaShield);
}

/* Static scenery, hidden, and gameplay-invisible actors do not participate in auras.
 * RF_HIDDEN covers ShowUnit-style hidden state and temporary invisibility such as
 * Invisibility/Wind Walk; Permanent Invisibility and Shadow Meld are tracked
 * independently. Fog visibility and detector state are deliberately irrelevant:
 * detection reveals an invisible actor to a viewer, but does not reactivate auras. */
bool S_AuraUnitActive(edict_t const *unit) {
    return unit && unit->inuse && !M_IsDead(unit) &&
           !(unit->svflags & SVF_STATIC_SCENERY) &&
           !(unit->s.renderfx & RF_HIDDEN) &&
           !S_PermanentInvisibilityActive(unit) && !S_GhostActive(unit) && !S_ShadowMeldActive(unit);
}

static bool aura_target_has_token(cstring_t targets, cstring_t full, cstring_t short_name) {
    char token[32];
    cstring_t cursor = targets;

    while (cursor && *cursor) {
        size_t len = 0;
        while (*cursor == ',' || isspace((unsigned char)*cursor)) cursor++;
        while (*cursor && *cursor != ',' && len + 1 < sizeof(token)) token[len++] = *cursor++;
        while (len && isspace((unsigned char)token[len - 1])) len--;
        token[len] = '\0';
        if (!strcasecmp(token, full) || (short_name && !strcasecmp(token, short_name))) return true;
        while (*cursor && *cursor != ',') cursor++;
    }
    return false;
}

static bool aura_allows_target(edict_t *source, edict_t *target, cstring_t targets) {
    bool is_self, is_friend, is_enemy, is_neutral;
    bool const wants_vulnerability = aura_target_has_token(targets, "vulnerable", "vuln") ||
        aura_target_has_token(targets, "invulnerable", "invu");

    if (!S_AuraUnitActive(source) || !S_AuraUnitActive(target)) return false;
    is_self = source == target;
    is_friend = S_SpellIsFriend(source, target);
    is_enemy = S_SpellIsEnemy(source, target);
    is_neutral = target->s.player < MAX_PLAYERS && level.mapinfo &&
        level.mapinfo->players[target->s.player].playerType == kPlayerTypeNeutral;
    bool const wants_relation = aura_target_has_token(targets, "friend", "frie") ||
        aura_target_has_token(targets, "allies", "alli") ||
        aura_target_has_token(targets, "enemy", "enem") ||
        aura_target_has_token(targets, "enemies", NULL) ||
        aura_target_has_token(targets, "neutral", "neut") ||
        aura_target_has_token(targets, "self", NULL);

    if (!targets || !*targets) return true;
    if (aura_target_has_token(targets, "dead", NULL)) return false;
    if (aura_target_has_token(targets, "notself", "nots") && is_self) return false;
    if (aura_target_has_token(targets, "hero", NULL) && !G_UnitIsHero(target)) return false;
    if (aura_target_has_token(targets, "nonhero", "nonh") && G_UnitIsHero(target)) return false;
    if (aura_target_has_token(targets, "mechanical", "mech") && target->targtype != TARG_MECHANICAL) return false;
    if (aura_target_has_token(targets, "organic", "orga") && target->targtype == TARG_MECHANICAL) return false;
    if (aura_target_has_token(targets, "structure", "stru") && !G_UnitIsStructure(target)) return false;
    /* WC3 target lists may name both vulnerability classes; that means either
     * class is accepted, not that both conditions must hold. */
    if (wants_vulnerability &&
        !((aura_target_has_token(targets, "vulnerable", "vuln") && !target->invulnerable) ||
          (aura_target_has_token(targets, "invulnerable", "invu") && target->invulnerable))) return false;
    if ((aura_target_has_token(targets, "air", NULL) || aura_target_has_token(targets, "ground", "grou")) &&
        !(aura_target_has_token(targets, "air", NULL) && G_UnitTargetType(target) == TARG_AIR) &&
        !(aura_target_has_token(targets, "ground", "grou") && G_UnitTargetType(target) == TARG_GROUND)) return false;
    if (wants_relation &&
        !(aura_target_has_token(targets, "self", NULL) && is_self) &&
        !((aura_target_has_token(targets, "friend", "frie") || aura_target_has_token(targets, "allies", "alli")) && is_friend) &&
        !((aura_target_has_token(targets, "enemy", "enem") || aura_target_has_token(targets, "enemies", NULL)) && is_enemy) &&
        !(aura_target_has_token(targets, "neutral", "neut") && is_neutral)) return false;
    return true;
}

typedef struct {
    float amount;
    uint32_t alias;
    uint32_t buff;
} regenerationAuraInfo_t;

static regenerationAuraInfo_t regen_value_cache[MAX_ENTITIES][REGEN_FAMILY_COUNT][REGEN_VALUE_COUNT];

void G_ResetHeroPassiveCaches(void) {
    S_InvalidateAuraSources();
    memset(authored_aura_types, 0, sizeof(authored_aura_types));
    memset(regen_sources, 0, sizeof(regen_sources));
    regen_source_count = 0;
    memset(&regen_members, 0, sizeof(regen_members));
    memset(&combat_members, 0, sizeof(combat_members));
    regen_cache_frame = UINT_MAX;
    regen_cache_generation = UINT_MAX;
    memset(regen_overlays, 0, sizeof(regen_overlays));
    memset(devotion_overlays, 0, sizeof(devotion_overlays));
    memset(unholy_overlays, 0, sizeof(unholy_overlays));
    memset(devotion_recipient_buff, 0, sizeof(devotion_recipient_buff));
    memset(unholy_recipient_buff, 0, sizeof(unholy_recipient_buff));
    memset(regen_value_cache, 0, sizeof(regen_value_cache));
    memset(regen_value_next_update, 0, sizeof(regen_value_next_update));
    memset(regen_visual_next_update, 0, sizeof(regen_visual_next_update));
    memset(regen_value_ability_data, 0, sizeof(regen_value_ability_data));
    memset(aura_cache, 0, sizeof(aura_cache));
    memset(aura_cache_next_update, 0, sizeof(aura_cache_next_update));
    memset(aura_cache_generation, 0, sizeof(aura_cache_generation));
    aura_cache_last_time = UINT_MAX;
#ifdef BZ_TESTS
    S_TestResetHeroAuraAliasResolves();
#endif
}

static uint32_t aura_buff_code(cstring_t buff_id) {
    uint32_t code = 0;
    if (buff_id && strlen(buff_id) >= 4) memcpy(&code, buff_id, 4);
    return code;
}

/* Records are addressed by edict identity; the membership sets retain original
 * encounter order without shifting the provider array after each insertion. */
static void regen_aura_discover(uint32_t index) {
    edict_t *unit = g_edicts + index;
    regenAuraSource_t *entry = regen_sources + index;
    bool was = regen_members.bits[index / 64] & (UINT64_C(1) << (index % 64));
    bool present = false;
#ifdef BZ_TESTS
    test_aura_discovery_visits++;
#endif
    if (unit->inuse && !(unit->svflags & SVF_STATIC_SCENERY)) {
        entry->source = unit;
        entry->spawn = unit->spawn_time;
        entry->combat_mask = 0;
        entry->life_orc = actor_aura_ability(unit, ID_REGEN_LIFE_ORC);
        entry->life_blight = actor_aura_ability(unit, ID_REGEN_LIFE_BLIGHT);
        entry->mana = actor_aura_ability(unit, ID_REGEN_MANA);
        entry->devotion = actor_aura_ability(unit, ID_DEVOTION_AURA);
        entry->unholy = actor_aura_ability(unit, ID_UNHOLY_AURA);
        FOR_LOOP(j, HERO_AURA_CACHE_KEYS) {
            entry->combat[j] = actor_aura_ability(unit, aura_cache_keys[j].code);
            if (entry->combat[j].alias) {
                entry->combat_mask |= 1u << j;
                entry->combat_rows[j] = G_AbilityLevel(entry->combat[j].alias, entry->combat[j].level);
            }
        }
        present = entry->life_orc.alias || entry->life_blight.alias || entry->mana.alias ||
            entry->devotion.alias || entry->unholy.alias || entry->combat_mask;
    }
    regen_source_count += (uint32_t)present - (uint32_t)was;
    entity_set_put(&regen_members, index, present);
    entity_set_put(&combat_members, index, present && entry->combat_mask);
}

/* Ownership discovery and overlay recovery have different invalidators. Local
 * provider changes update only their records; overlay owners maintain pointers
 * synchronously, with ordered recovery once per frame and after global resets. */
static void regen_aura_cache_update(void) {
    uint32_t const generation = G_AbilityDataGeneration();
    if (regen_cache_frame == level.framenum && regen_cache_generation == generation) return;
    bool full = regen_sources_dirty || regen_cache_generation != generation;
    if (full) {
        memset(&regen_members, 0, sizeof(regen_members));
        memset(&combat_members, 0, sizeof(combat_members));
        regen_source_count = 0;
        FOR_LOOP(i, globals.num_edicts) regen_aura_discover(i);
        memset(&regen_changed, 0, sizeof(regen_changed));
    } else {
        for (uint32_t i = entity_set_next(&regen_changed, 0); i < MAX_ENTITIES;
             i = entity_set_next(&regen_changed, i + 1)) {
            entity_set_put(&regen_changed, i, false);
            regen_aura_discover(i);
        }
    }
    regen_sources_dirty = false;
    regen_cache_frame = level.framenum;
    regen_cache_generation = generation;
    if (!full && overlay_cache_frame == level.framenum) return;
    overlay_cache_frame = level.framenum;
    memset(regen_overlays, 0, sizeof(regen_overlays));
    memset(devotion_overlays, 0, sizeof(devotion_overlays));
    memset(unholy_overlays, 0, sizeof(unholy_overlays));
    FOR_LOOP(i, globals.num_edicts) {
#ifdef BZ_TESTS
        test_aura_overlay_visits++;
#endif
        edict_t *effect = g_edicts + i;
        regenFamily_t family;
        if (!effect->inuse || !effect->owner || effect->owner->s.number >= MAX_ENTITIES ||
            effect->goalentity != effect->owner) continue;
        if (effect->summon_ability == ID_DEVOTION_AURA) {
            if (!devotion_overlays[effect->owner->s.number])
                devotion_overlays[effect->owner->s.number] = effect;
            continue;
        }
        if (effect->summon_ability == ID_UNHOLY_AURA) {
            if (!unholy_overlays[effect->owner->s.number])
                unholy_overlays[effect->owner->s.number] = effect;
            continue;
        }
        if (effect->summon_ability != ID_REGEN_LIFE_ORC &&
            effect->summon_ability != ID_REGEN_LIFE_BLIGHT && effect->summon_ability != ID_REGEN_MANA) continue;
        family = regen_family(effect->summon_ability);
        if (!regen_overlays[effect->owner->s.number][family])
            regen_overlays[effect->owner->s.number][family] = effect;
    }
}

static auraAbilityRef_t regen_aura_ref(regenAuraSource_t const *entry, uint32_t base_code) {
    if (base_code == ID_REGEN_LIFE_ORC) return entry->life_orc;
    if (base_code == ID_REGEN_LIFE_BLIGHT) return entry->life_blight;
    if (base_code == ID_REGEN_MANA) return entry->mana;
    if (base_code == ID_DEVOTION_AURA) return entry->devotion;
    if (base_code == ID_UNHOLY_AURA) return entry->unholy;
    return (auraAbilityRef_t){0};
}

static bool regen_aura_source_active(regenAuraSource_t const *entry) {
    return entry->source->spawn_time==entry->spawn && S_AuraUnitActive(entry->source);
}

static regenFamily_t regen_family(uint32_t base_code) {
    if (base_code == ID_REGEN_LIFE_ORC) return REGEN_FAMILY_LIFE_ORC;
    if (base_code == ID_REGEN_LIFE_BLIGHT) return REGEN_FAMILY_LIFE_BLIGHT;
    return REGEN_FAMILY_MANA;
}

/* Resolve one regeneration aura directly from live providers for a cache refresh. */
static regenerationAuraInfo_t regen_aura_info_uncached(edict_t *unit, uint32_t base_code, bool use_maximum) {
    regenerationAuraInfo_t result = {0};

    regen_aura_cache_update();
    for (uint32_t i = entity_set_next(&regen_members, 0); i < MAX_ENTITIES;
         i = entity_set_next(&regen_members, i + 1)) {
        edict_t *source = regen_sources[i].source;
        auraAbilityRef_t const ability = regen_aura_ref(regen_sources + i, base_code);
        abilityLevel_t const *row;
        float amount;

        if (!ability.alias || !regen_aura_source_active(regen_sources+i) || !S_SpellIsAliveTarget(source)) continue;
        row = G_AbilityLevel(ability.alias, ability.level);
        float const distance = Vector2_distance(&source->s.origin2, &unit->s.origin2);
        if (distance > row->area) continue;
        if (!aura_allows_target(source, unit, row->targs)) {
            continue;
        }
        amount = row->data[0].number;
        if (row->data[1].number != 0.0f && use_maximum)
            amount *= base_code == ID_REGEN_MANA ? unit->mana.max_value : unit->health.max_value;
        if (amount > result.amount) {
            cstring_t buff_id = row->buffID;
            if ((!buff_id || !*buff_id || !strcmp(buff_id, "-") || !strcmp(buff_id, "_")) &&
                ability.alias != base_code)
                buff_id = G_AbilityLevel(base_code, ability.level)->buffID;
            result.amount = amount;
            result.alias = ability.alias;
            result.buff = aura_buff_code(buff_id);
        }
    }
    return result;
}

/* Return a recipient's cached regeneration aura value until the retail refresh deadline. */
static float regen_aura_bonus(edict_t *unit, uint32_t base_code, bool use_maximum) {
    regenFamily_t family;
    regenValue_t value;
    void const *ability_data;

    if (!unit || unit->s.number >= MAX_ENTITIES) return 0.0f;
    aura_cache_update_time();
    family = regen_family(base_code);
    value = use_maximum ? REGEN_VALUE_MAXIMUM : REGEN_VALUE_NORMAL;
    ability_data = G_AbilityData(ID_REGEN_LIFE_ORC);
    if (level.time >= regen_value_next_update[unit->s.number] ||
        regen_value_ability_data[unit->s.number] != ability_data) {
        memset(regen_value_cache[unit->s.number], 0, sizeof(regen_value_cache[unit->s.number]));
        regen_value_cache[unit->s.number][REGEN_FAMILY_LIFE_ORC][REGEN_VALUE_NORMAL] =
            regen_aura_info_uncached(unit, ID_REGEN_LIFE_ORC, false);
        regen_value_cache[unit->s.number][REGEN_FAMILY_LIFE_ORC][REGEN_VALUE_MAXIMUM] =
            regen_aura_info_uncached(unit, ID_REGEN_LIFE_ORC, true);
        regen_value_cache[unit->s.number][REGEN_FAMILY_LIFE_BLIGHT][REGEN_VALUE_NORMAL] =
            regen_aura_info_uncached(unit, ID_REGEN_LIFE_BLIGHT, false);
        regen_value_cache[unit->s.number][REGEN_FAMILY_LIFE_BLIGHT][REGEN_VALUE_MAXIMUM] =
            regen_aura_info_uncached(unit, ID_REGEN_LIFE_BLIGHT, true);
        regen_value_cache[unit->s.number][REGEN_FAMILY_MANA][REGEN_VALUE_NORMAL] =
            regen_aura_info_uncached(unit, ID_REGEN_MANA, false);
        regen_value_cache[unit->s.number][REGEN_FAMILY_MANA][REGEN_VALUE_MAXIMUM] =
            regen_aura_info_uncached(unit, ID_REGEN_MANA, true);
        regen_value_next_update[unit->s.number] = level.time + AURA_UPDATE_MS;
        regen_value_ability_data[unit->s.number] = ability_data;
    }
    return regen_value_cache[unit->s.number][family][value].amount;
}

static bool is_regen_aura_overlay(edict_t const *effect, edict_t const *unit, uint32_t base_code) {
    return effect && effect->inuse && effect->owner == unit && effect->goalentity == unit &&
           effect->summon_ability == base_code;
}

static void sync_regen_aura_overlay(edict_t *unit, uint32_t base_code, regenerationAuraInfo_t const *info) {
    bool const needs_resource = base_code == ID_REGEN_MANA
        ? unit->mana.max_value > 0.0f && unit->mana.value < unit->mana.max_value
        : unit->health.max_value > 0.0f && unit->health.value > 0.0f && unit->health.value < unit->health.max_value;
    uint32_t effect_code = needs_resource && info ? info->buff : 0;
    cstring_t art = effect_code ? G_AbilityEffectArt(effect_code, WC3_EFFECT_TARGET, 0) : NULL;

    /* Buff rows may carry only the icon while the alias owns TargetArt. Keep
     * the authored buff presentation when present, then fall back to the
     * ability alias so a valid aura cannot become visually silent. */
    if ((!art || !*art) && needs_resource && info) {
        effect_code = info->alias;
        art = effect_code ? G_AbilityEffectArt(effect_code, WC3_EFFECT_TARGET, 0) : NULL;
    }
    uint32_t desired_model = art && *art ? G_RegisterModel(art) : 0;
    regenFamily_t const family = regen_family(base_code);
    edict_t *keep = unit->s.number < MAX_ENTITIES ? regen_overlays[unit->s.number][family] : NULL;

    if (keep && !is_regen_aura_overlay(keep, unit, base_code)) keep = NULL;
    if (keep && (!desired_model || keep->s.model != desired_model)) {
        G_DestroyEffect(keep);
        regen_overlays[unit->s.number][family] = NULL;
        keep = NULL;
    }

    if (!keep && desired_model) {
        edict_t *effect = G_SpawnAbilityEffectTarget(effect_code, WC3_EFFECT_TARGET, 0,
                                                    unit, NULL, false);
        if (effect) {
            /* Effect edicts are not summoned units; this otherwise-unused rawcode
             * field is a stable lifecycle tag that survives save/load and lets
             * each regeneration family own exactly one recipient overlay. */
            effect->owner = unit;
            effect->summon_ability = base_code;
            regen_overlays[unit->s.number][family] = effect;
        }
    }
}

float S_RegenerationHealthAura(edict_t *unit) {
    return regen_aura_bonus(unit, ID_REGEN_LIFE_ORC, true) +
           regen_aura_bonus(unit, ID_REGEN_LIFE_BLIGHT, true);
}

float S_RegenerationManaAura(edict_t *unit) {
    return regen_aura_bonus(unit, ID_REGEN_MANA, true);
}

void S_UpdateRegenerationAuraEffects(edict_t *unit) {
    /* Populate the shared two-second aura snapshot before reading its art metadata. */
    regen_aura_bonus(unit, ID_REGEN_LIFE_ORC, true);
    regenerationAuraInfo_t const health = regen_value_cache[unit->s.number][REGEN_FAMILY_LIFE_ORC][REGEN_VALUE_MAXIMUM];
    regenerationAuraInfo_t const blight = regen_value_cache[unit->s.number][REGEN_FAMILY_LIFE_BLIGHT]
        [REGEN_VALUE_MAXIMUM];
    regenerationAuraInfo_t const mana = regen_value_cache[unit->s.number][REGEN_FAMILY_MANA][REGEN_VALUE_MAXIMUM];

    sync_regen_aura_overlay(unit, ID_REGEN_LIFE_ORC, &health);
    sync_regen_aura_overlay(unit, ID_REGEN_LIFE_BLIGHT, &blight);
    sync_regen_aura_overlay(unit, ID_REGEN_MANA, &mana);
}

/* Gate presentation reconciliation independently from value refreshes. */
bool S_RegenerationAuraUpdateDue(edict_t *unit) {
    aura_cache_update_time();
    if (!unit || unit->s.number >= MAX_ENTITIES ||
        level.time < regen_visual_next_update[unit->s.number]) return false;
    regen_visual_next_update[unit->s.number] = level.time + AURA_UPDATE_MS;
    return true;
}

/* Aura presentation is an ability-owned periodic update, reached through the
 * shared ability dispatcher rather than the physics implementation. */
void S_UpdateUnitPassiveEffects(edict_t *unit) {
    if (!unit || !unit->inuse || (unit->svflags & SVF_STATIC_SCENERY) ||
        !unit->data.UnitBalance || !S_RegenerationAuraUpdateDue(unit)) return;
    S_UpdateRegenerationAuraEffects(unit);
    S_UpdateHeroAuraEffects(unit);
}

/* Refresh all combat aura families together so one recipient scan serves every consumer. */
static float hero_aura_bonus(edict_t *unit, uint32_t code, uint32_t data) {
    uint32_t slot = sizeof(aura_cache_keys) / sizeof(*aura_cache_keys);
    uint32_t ability_generation;

    FOR_LOOP(i, sizeof(aura_cache_keys) / sizeof(*aura_cache_keys))
        if (aura_cache_keys[i].code == code && aura_cache_keys[i].data == data) { slot = i; break; }
    if (slot == sizeof(aura_cache_keys) / sizeof(*aura_cache_keys) || !unit || unit->s.number >= MAX_ENTITIES)
        return 0.0f;
    ability_generation = G_AbilityDataGeneration();
    aura_cache_update_time();
    if (level.time >= aura_cache_next_update[unit->s.number] ||
        aura_cache_generation[unit->s.number] != ability_generation) {
        memset(aura_cache[unit->s.number], 0, sizeof(aura_cache[unit->s.number]));
        regen_aura_cache_update();
        for (uint32_t i = entity_set_next(&combat_members, 0); i < MAX_ENTITIES;
             i = entity_set_next(&combat_members, i + 1)) {
#ifdef BZ_TESTS
            test_combat_aura_visits++;
#endif
            regenAuraSource_t const *source = regen_sources + i;
            edict_t *aura = source->source;
            /* Distance and decoded rows are shared by every family from this
             * provider. Reject distant providers before status/ownership work.
             * Row pointers refresh with AbilityData; their areas remain live. */
            float distance = Vector2_distance(&aura->s.origin2, &unit->s.origin2);
            uint32_t nearby = 0;
            for (uint32_t slots = source->combat_mask; slots; slots &= slots - 1) {
                uint32_t j = __builtin_ctz(slots);
                if (!(distance > source->combat_rows[j]->area)) nearby |= 1u << j;
            }
            if (!nearby) continue;
#ifdef BZ_TESTS
            test_combat_aura_eligibility_checks++;
#endif
            if (!regen_aura_source_active(source) || !S_SpellIsFriend(aura, unit)) continue;
            for (uint32_t slots = nearby; slots; slots &= slots - 1) {
                uint32_t const j = __builtin_ctz(slots);
                abilityLevel_t const *row = source->combat_rows[j];
                if (!aura_allows_target(aura, unit, row->targs)) continue;
                {
                    float amount = row->data[aura_cache_keys[j].data - 1].number;
                    /* ABILITY_BLF_PERCENT_BONUS_UAU3: when enabled, Unholy
                     * Aura's DataB is max-life regeneration per second. DataA
                     * remains the ordinary movement-speed fraction. */
                    if (aura_cache_keys[j].code == ID_UNHOLY_AURA &&
                        aura_cache_keys[j].data == 2 && row->data[2].number != 0.0f)
                        amount *= unit->health.max_value;
                    if (aura_cache_keys[j].code == ID_DEVOTION_AURA &&
                        aura_cache_keys[j].data == 1 && row->data[1].number != 0.0f) {
                        UnitBalance_t const *balance = unit->data.UnitBalance;
                        if (!balance) balance = G_UnitBalance(unit->class_id);
                        /* Had2 percent mode uses the authored `def` Defense Base,
                         * not realdef, agility, upgrades, or current runtime armor. */
                        amount *= balance ? (float)balance->baseArmor : 0.0f;
                    }
                    aura_cache[unit->s.number][j] = MAX(aura_cache[unit->s.number][j], amount);
                }
            }
        }
        aura_cache_next_update[unit->s.number] = level.time + AURA_UPDATE_MS;
        aura_cache_generation[unit->s.number] = ability_generation;
    }
    return aura_cache[unit->s.number][slot];
}

typedef struct {
    float amount;
    uint32_t alias;
    uint32_t level;
    uint32_t buff;
} heroAuraPresentation_t;

static heroAuraPresentation_t hero_aura_presentation(edict_t *unit, uint32_t base_code) {
    heroAuraPresentation_t result = {0};

    regen_aura_cache_update();
    for (uint32_t i = entity_set_next(&regen_members, 0); i < MAX_ENTITIES;
         i = entity_set_next(&regen_members, i + 1)) {
        edict_t *source = regen_sources[i].source;
        auraAbilityRef_t const ability = regen_aura_ref(regen_sources + i, base_code);
        abilityLevel_t const *row;
        float amount;
        cstring_t buff_id;

        if (!regen_aura_source_active(regen_sources+i) || !S_SpellIsFriend(source, unit)) continue;
        if (!ability.alias) continue;
        row = G_AbilityLevel(ability.alias, ability.level);
        if (Vector2_distance(&source->s.origin2, &unit->s.origin2) > row->area ||
            !aura_allows_target(source, unit, row->targs)) continue;
        /* Presentation follows the primary authored aura value. Stock Devotion
         * and Unholy Aura levels increase monotonically; custom aliases retain
         * stable source order for equal values. Mechanical consumers still
         * resolve each numeric contribution independently. */
        amount = row->data[0].number;
        if (result.alias && amount <= result.amount) continue;
        buff_id = row->buffID;
        if ((!buff_id || !*buff_id || !strcmp(buff_id, "-") || !strcmp(buff_id, "_")) &&
            ability.alias != base_code)
            buff_id = G_AbilityLevel(base_code, ability.level)->buffID;
        result.amount = amount;
        result.alias = ability.alias;
        result.level = ability.level;
        result.buff = aura_buff_code(buff_id);
    }
    return result;
}

static void hero_aura_sync_overlay(edict_t *unit, uint32_t base_code, edict_t * *overlays,
                                   heroAuraPresentation_t const *info) {
    uint32_t effect_code = info ? info->buff : 0;
    cstring_t art = effect_code ? G_AbilityEffectArt(effect_code, WC3_EFFECT_TARGET, 0) : NULL;
    uint32_t desired_model;
    edict_t *keep = unit->s.number < MAX_ENTITIES ? overlays[unit->s.number] : NULL;

    if ((!art || !*art) && info && info->alias) {
        effect_code = info->alias;
        art = G_AbilityEffectArt(effect_code, WC3_EFFECT_TARGET, 0);
    }
    desired_model = art && *art ? G_RegisterModel(art) : 0;
    if (keep && (!keep->inuse || keep->owner != unit || keep->goalentity != unit ||
                 keep->summon_ability != base_code)) keep = NULL;
    if (keep && (!desired_model || keep->s.model != desired_model)) {
        G_DestroyEffect(keep);
        overlays[unit->s.number] = NULL;
        keep = NULL;
    }
    if (!keep && desired_model) {
        edict_t *effect = G_SpawnAbilityEffectTarget(effect_code, WC3_EFFECT_TARGET, 0, unit, NULL, false);
        if (effect) {
            effect->owner = unit;
            effect->summon_ability = base_code;
            overlays[unit->s.number] = effect;
        }
    }
}

void S_UpdateHeroAuraEffects(edict_t *unit) {
    heroAuraPresentation_t devotion, unholy;

    if (!unit || !unit->inuse || unit->s.number >= MAX_ENTITIES) return;
    devotion = hero_aura_presentation(unit, ID_DEVOTION_AURA);
    devotion_recipient_buff[unit->s.number] = devotion.alias ? devotion.buff : 0;
    hero_aura_sync_overlay(unit, ID_DEVOTION_AURA, devotion_overlays, devotion.alias ? &devotion : NULL);

    unholy = hero_aura_presentation(unit, ID_UNHOLY_AURA);
    unholy_recipient_buff[unit->s.number] = unholy.alias ? unholy.buff : 0;
    hero_aura_sync_overlay(unit, ID_UNHOLY_AURA, unholy_overlays, unholy.alias ? &unholy : NULL);
}

uint32_t S_DevotionAuraBuff(edict_t *unit) {
    if (!unit || unit->s.number >= MAX_ENTITIES) return 0;
    return devotion_recipient_buff[unit->s.number];
}

uint32_t S_UnholyAuraBuff(edict_t *unit) {
    if (!unit || unit->s.number >= MAX_ENTITIES) return 0;
    return unholy_recipient_buff[unit->s.number];
}

float S_BrillianceManaRegen(edict_t *unit) { return hero_aura_bonus(unit, ID_BRILLIANCE, 1); }
float S_DevotionArmorBonus(edict_t *unit) { return hero_aura_bonus(unit, ID_DEVOTION_AURA, 1); }
float S_UnholyHealthRegen(edict_t *unit) { return hero_aura_bonus(unit, ID_UNHOLY_AURA, 2); }
float S_UnholyMoveBonus(edict_t *unit) { return hero_aura_bonus(unit, ID_UNHOLY_AURA, 1); }
float S_VampiricLifeSteal(edict_t *unit) { return hero_aura_bonus(unit, ID_VAMPIRIC_AURA, 1); }

/* Discovery changes with authored/runtime ownership, not recipient queries or
 * frame time. Eligibility is deliberately deferred so hiding, death, alliance
 * and position changes do not require rebuilding the ownership registry. */
static void slow_aura_discover(uint32_t index) {
    edict_t *unit = g_edicts + index;
    auraAbilityRef_t ability = {0};
#ifdef BZ_TESTS
    test_slow_discovery_visits++;
#endif
    if (unit->inuse && !(unit->svflags & SVF_STATIC_SCENERY)) ability = actor_aura_ability(unit, ID_SLOW_AURA);
    entity_set_put(&slow_members, index, ability.alias != 0);
    if (ability.alias) slow_sources[index] = (slowAuraSource_t){unit, unit->spawn_time, ability};
}

static void slow_aura_prepare(void) {
    uint32_t generation = G_AbilityDataGeneration();
    if (!slow_dirty && !slow_pending && slow_generation == generation) return;
    if (slow_dirty || slow_generation != generation) {
        memset(&slow_members, 0, sizeof(slow_members));
        FOR_LOOP(i, globals.num_edicts) slow_aura_discover(i);
        memset(&slow_changed, 0, sizeof(slow_changed));
    } else {
        for (uint32_t i = entity_set_next(&slow_changed, 0); i < MAX_ENTITIES;
             i = entity_set_next(&slow_changed, i + 1)) {
            entity_set_put(&slow_changed, i, false);
            slow_aura_discover(i);
        }
    }
    slow_generation = generation;
    slow_dirty = slow_pending = false;
}

/* Speed queries must follow providers, not every scenery/nonprovider actor.
 * Ownership discovery is shared; position, alliance, visibility and lifetime
 * remain live even when several decisions occur in one simulation frame. */
static float slow_aura_bonus(edict_t const *unit, uint32_t data) {
    float result=0;
    if(!unit)return 0;
    slow_aura_prepare();
    for (uint32_t i = entity_set_next(&slow_members, 0); i < MAX_ENTITIES;
         i = entity_set_next(&slow_members, i + 1)) {
        slowAuraSource_t const *entry=slow_sources+i;
#ifdef BZ_TESTS
        test_slow_aura_visits++;
#endif
        edict_t *source=entry->unit;
        if(source->spawn_time!=entry->spawn || !S_AuraUnitActive(source) ||
            !S_SpellIsAliveTarget(source) || !S_SpellIsEnemy(source,(edict_t *)unit))continue;
        abilityLevel_t const *row=G_AbilityLevel(entry->ability.alias,entry->ability.level);
        if(Vector2_distance(&source->s.origin2,&unit->s.origin2)>row->area ||
            !aura_allows_target(source,(edict_t *)unit,row->targs))continue;
        result=MAX(result,row->data[data-1].number);
    }
    return MAX(0.0f,MIN(0.9f,result));
}

float S_SlowAuraMoveReduction(edict_t const *unit) { return slow_aura_bonus(unit, 1); }
float S_SlowAuraAttackReduction(edict_t const *unit) { return slow_aura_bonus(unit, 2); }
float S_CommandAuraAttackBonus(edict_t *unit) {
    return MAX(hero_aura_bonus(unit, ID_COMMAND_AURA, 1), hero_aura_bonus(unit, ID_COMMAND_AURA_NEUTRAL, 1));
}
float S_WarDrumsAttackBonus(edict_t *unit) { return hero_aura_bonus(unit, ID_WAR_DRUMS, 1); }

float S_TrueshotAttackBonus(edict_t *unit) {
    return S_AttackProfileRead(unit, 0)->type == ATK_PIERCE ? hero_aura_bonus(unit, ID_TRUESHOT_AURA, 1) : 0.0f;
}

int S_SearingArrowDamage(edict_t *attacker, int damage) {
    uint32_t code = ID_SEARING_ARROWS, level = 0;
    if (!attacker) return damage;
    FOR_LOOP(i, G_UnitStatusSlotCount(attacker)) {
        heroabilitystatus_t const *st = attacker->abilstatus + i;
        if (!st->level || (st->timestamp && st->timestamp <= G_Time())) continue;
        if (st->code == ID_SEARING_ARROWS || G_AbilityCode(st->code) == ID_SEARING_ARROWS) {
            code = st->code; level = st->level; break;
        }
    }
    if (!level) { level = G_UnitStatusLevel(attacker, ID_POISON_ARROWS); code = ID_POISON_ARROWS; }
    return level && S_AttackProfileRead(attacker, 0)->weapon == WPN_MISSILE ? damage + (int)S_SpellData(code, level, 1) : damage;
}

static uint32_t mana_shield_buff(uint32_t code, uint32_t level) {
    cstring_t buff = G_AbilityLevel(code, level)->buffID;
    return buff && strlen(buff) >= 4 ? FS_SLKKey(buff) : 0;
}

static void mana_shield_remove(edict_t *unit, uint32_t buff) {
    FOR_LOOP(i, G_UnitStatusSlotCount(unit))
        if (unit->abilstatus[i].level && unit->abilstatus[i].code == buff)
            memset(unit->abilstatus + i, 0, sizeof(unit->abilstatus[i]));
    G_InvalidateUnitInfoPanel(unit);
}

/* Mana Shield owns its authored buff so learned-but-inactive abilities never intercept damage. */
BZ_ABILITY_PROC(CAbilityManaShield) {
    uint32_t code = call && call->item && call->item->code ? call->item->code : 0;
    auraAbilityRef_t ref;
    uint32_t level, buff;
    bool active;
    if (!code) {
        ref = mana_shield_ability(ent);
        code = ref.alias ? ref.alias : ID_MANA_SHIELD;
    }
    level = S_SpellLevel(ent, code); buff = mana_shield_buff(code, level);
    active = buff && G_UnitStatusLevel(ent, buff);
    switch (msg) {
    case A_TOGGLE_ON: return active;
    case A_EXECUTE:
        if (active) mana_shield_remove(ent, buff);
        else if (buff && ent->mana.value > 0.0f) unit_addstatus(ent, GetClassName(buff), level);
        return true;
    case A_ORDER:
        if (!call || !call->order) return false;
        ref = mana_shield_ability(ent);
        if (!ref.alias) return false;
        code = ref.alias; level = ref.level; buff = mana_shield_buff(code, level);
        active = buff && G_UnitStatusLevel(ent, buff);
        if (!strcmp(call->order, "manashieldon")) {
            if (!active && buff && ent->mana.value > 0.0f) unit_addstatus(ent, GetClassName(buff), level);
            return true;
        }
        if (!strcmp(call->order, "manashieldoff")) {
            if (active) mana_shield_remove(ent, buff);
            return true;
        }
        return false;
    case A_DISABLE:
    case A_UNIT_REMOVE:
        if (active) mana_shield_remove(ent, buff);
        return true;
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

/* Retail DataA is damage absorbed per mana and DataB is the fraction of each hit absorbed. */
int S_ManaShieldDamage(edict_t *target, int damage) {
    auraAbilityRef_t ref = mana_shield_ability(target);
    uint32_t code = ref.alias ? ref.alias : ID_MANA_SHIELD;
    uint32_t level = ref.level ? ref.level : G_UnitAbilityLevel(target, ID_MANA_SHIELD);
    uint32_t buff = level ? mana_shield_buff(code, level) : 0;
    float ratio, fraction, absorbed;
    if (!buff || !G_UnitStatusLevel(target, buff) || damage <= 0 || target->mana.value <= 0.0f) return damage;
    ratio = S_SpellData(code, level, 1);
    fraction = MIN(1.0f, MAX(0.0f, S_SpellData(code, level, 2)));
    if (ratio <= 0.0f || fraction <= 0.0f) return damage;
    absorbed = MIN((float)damage * fraction, target->mana.value * ratio);
    target->mana.value = MAX(0.0f, target->mana.value - absorbed / ratio);
    if (target->mana.value <= 0.0f) mana_shield_remove(target, buff);
    return damage - (int)absorbed;
}

float S_ThornsDamageReturn(edict_t const *target, edict_t const *attacker, float damage) {
    if (!target || !attacker || (S_AttackProfileRead(attacker, 0)->weapon != WPN_NORMAL && S_AttackProfileRead(attacker, 0)->weapon != WPN_INSTANT))
        return 0.0f;
    return damage * hero_aura_bonus((edict_t *)target, ID_THORNS_AURA, 1);
}

bool S_EvasionRoll(edict_t *target) {
    abilityAliasRef_t ev = S_ResolveAbilityAlias(target, ID_EVASION);
    uint32_t level;
    if (ev.alias && ev.level && (float)(rand() % 10000) / 10000.0f < S_SpellData(ev.alias, ev.level, 1)) return true;
    level = G_UnitAbilityLevel(target, ID_DRUNKEN_BRAWLER);
    return level && (float)(rand() % 10000) / 10000.0f < S_SpellData(ID_DRUNKEN_BRAWLER, level, 4);
}

int S_CriticalStrikeDamage(edict_t *attacker, int damage) {
    uint32_t level = G_UnitAbilityLevel(attacker, ID_CRITICAL_STRIKE);
    uint32_t code = ID_CRITICAL_STRIKE;
    if (!level) { level = G_UnitAbilityLevel(attacker, ID_CREEP_CRITICAL_STRIKE); code = ID_CREEP_CRITICAL_STRIKE; }
    if (!level) { level = G_UnitAbilityLevel(attacker, ID_DRUNKEN_BRAWLER); code = ID_DRUNKEN_BRAWLER; }
    if (!level || (float)(rand() % 100) >= S_SpellData(code, level, 1)) return damage;
    return (int)((float)damage * MAX(1.0f, S_SpellData(code, level, 2)));
}

float S_SpikedArmorBonus(edict_t const *unit) {
    uint32_t level = G_UnitAbilityLevel(unit, ID_SPIKED_CARAPACE);
    return level ? S_SpellData(ID_SPIKED_CARAPACE, level, 3) : 0.0f;
}

float S_SpikedDamageReturn(edict_t const *unit, float damage) {
    uint32_t code = ID_SPIKED_CARAPACE, level = G_UnitAbilityLevel(unit, code);
    if (!level) { code = ID_SPIKED_BARRICADES; level = G_UnitAbilityLevel(unit, code); }
    if (!level) return 0.0f;
    return MAX(S_SpellData(code, level, 2), damage * S_SpellData(code, level, 1));
}

/* Pulverize is a passive attack proc. DataA is percent chance, DataB damage,
 * DataC/D full/half damage radii; the authored Area cell is unused.
 * Its damage is an authored physical-spell event, so secondary victims do not
 * recursively trigger attack listeners. */
void S_PulverizeAttack(edict_t *attacker, edict_t const *primary) {
    abilityAliasRef_t ability = S_ResolveAbilityAlias(attacker, ID_PULVERIZE);
    uint32_t code = ability.alias, level = ability.level;
    float full_radius, partial_radius, chance, full_damage, partial_damage;
    if (!level || !primary) return;
    chance = S_SpellData(code, level, 1) * 0.01f;
    if ((float)(rand() % 10000) / 10000.0f >= chance) return;
    full_damage = S_SpellData(code, level, 2);
    partial_damage = full_damage * 0.5f;
    full_radius = S_SpellData(code, level, 3);
    partial_radius = S_SpellData(code, level, 4);
    FILTER_EDICTS(target, target != attacker && target != primary &&
                  S_SpellIsAliveTarget(target) && S_SpellIsEnemy(attacker, target) &&
                  G_UnitTargetType(target) == TARG_GROUND) {
        float distance = Vector2_distance(&target->s.origin2, &primary->s.origin2);
        float amount = distance <= full_radius ? full_damage :
                       distance <= partial_radius ? partial_damage : 0.0f;
        if (amount > 0.0f) S_SpellDamage(target, attacker, (int)amount);
    }
}

#ifdef BZ_TESTS
#include "shared/test.h"
TEST(wc3_ability_dispatch, aura_membership_matches_alias_resolver_without_family_scans) {
    static cstring_t const lists[] = { "Ashm,Amgr,AInv", "AHad,AInv", "Aoar,Aarm", "Aas l,", "", "AHab,AUau,AOae" };
    static uint32_t const extra[] = { ID_REGEN_LIFE_ORC, ID_REGEN_LIFE_BLIGHT, ID_REGEN_MANA,
        ID_SLOW_AURA, MAKEFOURCC('A','O','a','e') };
    FOR_LOOP(i, sizeof(lists) / sizeof(*lists)) FOR_LOOP(state, 5) {
        UnitAbilities_t row = { .abilList = lists[i] };
        edict_t unit = { .data.UnitAbilities = &row };
        if (state == 1) unit.abilities.added[unit.abilities.added_count++] = ID_SLOW_AURA;
        if (state == 2) unit.abilities.removed[unit.abilities.removed_count++] = ID_DEVOTION_AURA;
        if (state == 3) {
            unit.heroabilities[0] = (heroability_t){ .code = ID_BRILLIANCE, .level = 2 };
            unit.abilities.removed[unit.abilities.removed_count++] = ID_BRILLIANCE;
        }
        if (state == 4) unit.svflags = SVF_STATIC_SCENERY;
        bool expected = false;
        if (!(unit.svflags & SVF_STATIC_SCENERY)) {
            FOR_LOOP(k, HERO_AURA_CACHE_KEYS) expected |= S_ResolveAbilityAlias(&unit, aura_cache_keys[k].code).alias != 0;
            FOR_LOOP(k, sizeof(extra) / sizeof(*extra)) expected |= S_ResolveAbilityAlias(&unit, extra[k]).alias != 0;
        }
        test_aura_membership_resolves = 0;
        T_EQ(S_UnitHasAuraSource(&unit), expected);
        T_EQ(test_aura_membership_resolves, 0);
    }
}
void reset_entities(void);
void setup_test_world(void);
edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
TEST(wc3_ability_dispatch, combat_aura_queries_skip_regeneration_only_providers) {
    reset_entities(); setup_test_world();
    edict_t *source = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    source->abilities.added[source->abilities.added_count++] = ID_REGEN_LIFE_BLIGHT;
    S_InvalidateAuraSources();
    test_combat_aura_visits = 0;
    T_FEQ(S_UnholyMoveBonus(target), 0, 0);
    T_ASSERT(regen_source_count > 0);
    T_EQ(test_combat_aura_visits, 0);
    source->abilities.added[source->abilities.added_count++] = ID_UNHOLY_AURA;
    S_InvalidateAuraSources();
    level.time += AURA_UPDATE_MS;
    S_UnholyMoveBonus(target);
    T_EQ(test_combat_aura_visits, 1);
    reset_entities(); setup_test_world();
}
TEST(wc3_ability_dispatch, combat_aura_range_prunes_eligibility_and_keeps_live_rows) {
    reset_entities(); setup_test_world();
    uint32_t alias = MAKEFOURCC('X','U','a','u');
    AbilityData_t ability = {.id = alias, .code = ID_UNHOLY_AURA};
    ability.level[0].area = 17.5f; ability.level[0].targs = "ground,friend";
    ability.level[0].data[0].number = .25f; ability.level[0].data[1].number = .75f;
    slkTestData_t rows = {.rows = &ability, .count = 1};
    slkTestData_t *old = G_SetSLKRows("AbilityData", &rows);
    edict_t *source = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128, 0);
    source->abilities.added[source->abilities.added_count++] = alias;
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    S_InvalidateAuraSources(); test_combat_aura_eligibility_checks = 0;
    FOR_LOOP(i, 128) {
        level.time += AURA_UPDATE_MS;
        T_FEQ(S_UnholyMoveBonus(target), 0, 0);
    }
    T_EQ(test_combat_aura_eligibility_checks, 0);
    target->s.origin2.x = 17.5f; level.time += AURA_UPDATE_MS;
    T_FEQ(S_UnholyMoveBonus(target), .25f, 0);
    T_FEQ(S_UnholyHealthRegen(target), .75f, 0);
    source->s.renderfx |= RF_HIDDEN; level.time += AURA_UPDATE_MS;
    T_FEQ(S_UnholyMoveBonus(target), 0, 0);
    source->s.renderfx &= ~RF_HIDDEN;
    ability.level[0].area = 17.25f; level.time += AURA_UPDATE_MS;
    T_FEQ(S_UnholyMoveBonus(target), 0, 0);
    AbilityData_t replacement = ability;
    replacement.level[0].area = 32; replacement.level[0].data[0].number = .5f;
    slkTestData_t updated = {.rows = &replacement, .count = 1};
    slkTestData_t *previous = G_SetSLKRows("AbilityData", &updated);
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_UnholyMoveBonus(target), .5f, 0);
    G_SetSLKRows("AbilityData", old); free(previous); free(old);
    reset_entities(); setup_test_world();
}
TEST(wc3_ability_dispatch, aura_free_types_skip_reclassification_but_keep_runtime_ownership_live) {
    UnitAbilities_t row = { .abilList = "Ashm,Amgr,AInv" };
    edict_t unit = { .data.UnitAbilities = &row };
    T_ASSERT(!S_UnitHasAuraSource(&unit));
    test_authored_aura_classifications = 0;
    FOR_LOOP(i, 128) T_ASSERT(!S_UnitHasAuraSource(&unit));
    T_EQ(test_authored_aura_classifications, 0);
    unit.abilities.added[unit.abilities.added_count++] = ID_BRILLIANCE;
    T_ASSERT(S_UnitHasAuraSource(&unit));
    unit.abilities.removed[unit.abilities.removed_count++] = ID_BRILLIANCE;
    T_ASSERT(!S_UnitHasAuraSource(&unit));
    unit.heroabilities[0] = (heroability_t){ .code = ID_BRILLIANCE, .level = 2 };
    T_ASSERT(S_UnitHasAuraSource(&unit));
    unit.heroabilities[0].level = 0;
    row.abilList = "AHad,AInv";
    T_ASSERT(S_UnitHasAuraSource(&unit));
    unit.abilities.removed[unit.abilities.removed_count++] = ID_DEVOTION_AURA;
    T_ASSERT(!S_UnitHasAuraSource(&unit));
}
/* A provider edit must perform constant discovery work even in a large world;
 * values still use live eligibility and the ordinary recipient refresh clock. */
TEST(wc3_ability_dispatch, aura_local_edits_do_not_replay_world_or_overlays) {
    reset_entities(); setup_test_world();
    uint32_t alias = MAKEFOURCC('X','U','a','u');
    AbilityData_t ability = {.id = alias, .code = ID_UNHOLY_AURA};
    ability.level[0].area = 500; ability.level[0].targs = "ground,friend";
    ability.level[0].data[0].number = .375f;
    ability.level[1] = ability.level[0]; ability.level[1].data[0].number = .625f;
    slkTestData_t rows = {.rows = &ability, .count = 1};
    slkTestData_t *old = G_SetSLKRows("AbilityData", &rows);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    target->s.player = 0; target->targtype = TARG_GROUND;
    FOR_LOOP(i, 4096) alloc_test_unit(MAKEFOURCC('h','f','o','o'), 2000, 2000);
    S_UnholyMoveBonus(target); S_SlowAuraMoveReduction(target);
    test_aura_discovery_visits = test_slow_discovery_visits = test_aura_overlay_visits = 0;
    edict_t *source = NULL;
    FOR_LOOP(i, 128) {
        source = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
        source->s.player = 0; source->targtype = TARG_GROUND;
        unit_learnability(source, alias);
        T_EQ(G_UnitAbilityLevel(source, alias), 1);
        level.time += AURA_UPDATE_MS;
        T_FEQ(S_UnholyMoveBonus(target), .375f, 0);
        T_FEQ(S_SlowAuraMoveReduction(target), 0, 0);
    }
    T_EQ(test_aura_discovery_visits, 128);
    T_EQ(test_slow_discovery_visits, 128);
    T_EQ(test_aura_overlay_visits, 0);
    T_EQ(G_UnitSetAbilityLevel(source, alias, 2), 2);
    level.time += AURA_UPDATE_MS;
    float ranked = S_UnholyMoveBonus(target);
    T_FEQ(ranked, .625f, 0);
    S_InvalidateAuraSources(); level.time += AURA_UPDATE_MS;
    T_FEQ(S_UnholyMoveBonus(target), ranked, 0);
    G_FreeEdict(source); level.time += AURA_UPDATE_MS;
    T_FEQ(S_UnholyMoveBonus(target), .375f, 0);
    S_InvalidateAuraSources(); level.time += AURA_UPDATE_MS;
    T_FEQ(S_UnholyMoveBonus(target), .375f, 0);
    G_SetSLKRows("AbilityData", old); free(old);
    reset_entities(); setup_test_world();
}
#endif
