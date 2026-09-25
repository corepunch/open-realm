#include "s_skills.h"

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
    DWORD alias;
    DWORD level;
} auraAbilityRef_t;

typedef struct {
    DWORD code, data;
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
    LPEDICT source;
    auraAbilityRef_t life_orc;
    auraAbilityRef_t life_blight;
    auraAbilityRef_t mana;
    auraAbilityRef_t devotion;
    auraAbilityRef_t unholy;
    auraAbilityRef_t combat[HERO_AURA_CACHE_KEYS];
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
static DWORD regen_source_count;
static DWORD regen_cache_frame = UINT_MAX;
static DWORD regen_cache_generation = UINT_MAX;
static LPEDICT regen_overlays[MAX_ENTITIES][REGEN_FAMILY_COUNT];
static LPEDICT devotion_overlays[MAX_ENTITIES];
static LPEDICT unholy_overlays[MAX_ENTITIES];
static DWORD devotion_recipient_buff[MAX_ENTITIES];
static DWORD unholy_recipient_buff[MAX_ENTITIES];
static DWORD regen_value_next_update[MAX_ENTITIES];
static DWORD regen_visual_next_update[MAX_ENTITIES];
static LPCVOID regen_value_ability_data[MAX_ENTITIES];

static FLOAT aura_cache[MAX_ENTITIES][sizeof(aura_cache_keys) / sizeof(*aura_cache_keys)];
static DWORD aura_cache_next_update[MAX_ENTITIES];
static DWORD aura_cache_generation[MAX_ENTITIES];
static DWORD aura_cache_last_time = UINT_MAX;
#ifdef BZ_TESTS
static DWORD test_hero_aura_alias_resolves;
void S_TestResetHeroAuraAliasResolves(void) { test_hero_aura_alias_resolves = 0; }
DWORD S_TestHeroAuraAliasResolves(void) { return test_hero_aura_alias_resolves; }
#endif

static regenFamily_t regen_family(DWORD base_code);

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
abilityAliasRef_t S_ResolveAbilityAlias(LPEDICT ent, DWORD base_code) {
    abilityAliasRef_t result = {0};
    char alias_name[5] = {0};
    if (!ent || !base_code) return result;
    if (ent->data.UnitAbilities && ent->data.UnitAbilities->abilList) {
        PARSE_LIST(ent->data.UnitAbilities->abilList, token, parse_segment) {
            DWORD alias = 0;
            if (strlen(token) != 4 || !G_ActorHasSkill(ent, token)) continue;
            memcpy(&alias, token, sizeof(alias));
            if (alias == base_code || G_AbilityCode(alias) == base_code) {
                result.alias = alias;
                result.level = 1;
                return result;
            }
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added)) {
        DWORD const alias = ent->abilities.added[i];
        if (!alias) continue;
        memcpy(alias_name, &alias, 4);
        if (G_ActorHasSkill(ent, alias_name) && (alias == base_code || G_AbilityCode(alias) == base_code)) {
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

static auraAbilityRef_t actor_aura_ability(LPEDICT ent, DWORD base_code) {
#ifdef BZ_TESTS
    test_hero_aura_alias_resolves++;
#endif
    abilityAliasRef_t resolved = S_ResolveAbilityAlias(ent, base_code);
    auraAbilityRef_t result = { resolved.alias, resolved.level };
    return result;
}

static auraAbilityRef_t unit_ability_with_proc(LPEDICT ent, abilityProc_t proc) {
    auraAbilityRef_t result = {0};
    char alias_name[5] = {0};
    if (!ent || !proc) return result;
    if (ent->data.UnitAbilities && ent->data.UnitAbilities->abilList) {
        PARSE_LIST(ent->data.UnitAbilities->abilList, token, parse_segment) {
            DWORD alias = 0;
            abilityitem_t item;
            if (strlen(token) != 4 || !G_ActorHasSkill(ent, token)) continue;
            memcpy(&alias, token, sizeof(alias));
            item = S_AbilityItem(alias);
            if (item.ability && item.ability->proc == proc) {
                result.alias = alias; result.level = 1; return result;
            }
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added)) {
        DWORD const alias = ent->abilities.added[i];
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

static auraAbilityRef_t mana_shield_ability(LPEDICT ent) {
    auraAbilityRef_t ref = actor_aura_ability(ent, ID_MANA_SHIELD);
    return ref.alias ? ref : unit_ability_with_proc(ent, CAbilityManaShield);
}

/* Static scenery, hidden, and gameplay-invisible actors do not participate in auras.
 * RF_HIDDEN covers ShowUnit-style hidden state and temporary invisibility such as
 * Invisibility/Wind Walk; Permanent Invisibility is tracked independently. Fog
 * visibility and detector state are deliberately irrelevant here. */
BOOL S_AuraUnitActive(LPCEDICT unit) {
    return unit && unit->inuse && !M_IsDead(unit) &&
           !(unit->svflags & SVF_STATIC_SCENERY) &&
           !(unit->s.renderfx & RF_HIDDEN) && !S_PermanentInvisibilityActive(unit);
}

static BOOL aura_target_has_token(LPCSTR targets, LPCSTR full, LPCSTR short_name) {
    char token[32];
    LPCSTR cursor = targets;

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

static BOOL aura_allows_target(LPEDICT source, LPEDICT target, LPCSTR targets) {
    BOOL is_self, is_friend, is_enemy, is_neutral;
    BOOL const wants_vulnerability = aura_target_has_token(targets, "vulnerable", "vuln") ||
        aura_target_has_token(targets, "invulnerable", "invu");

    if (!S_AuraUnitActive(source) || !S_AuraUnitActive(target)) return false;
    is_self = source == target;
    is_friend = S_SpellIsFriend(source, target);
    is_enemy = S_SpellIsEnemy(source, target);
    is_neutral = target->s.player < MAX_PLAYERS && level.mapinfo &&
        level.mapinfo->players[target->s.player].playerType == kPlayerTypeNeutral;
    BOOL const wants_relation = aura_target_has_token(targets, "friend", "frie") ||
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
    if (aura_target_has_token(targets, "structure", "stru") && target->targtype != TARG_STRUCTURE) return false;
    /* WC3 target lists may name both vulnerability classes; that means either
     * class is accepted, not that both conditions must hold. */
    if (wants_vulnerability &&
        !((aura_target_has_token(targets, "vulnerable", "vuln") && !target->invulnerable) ||
          (aura_target_has_token(targets, "invulnerable", "invu") && target->invulnerable))) return false;
    if ((aura_target_has_token(targets, "air", NULL) || aura_target_has_token(targets, "ground", "grou")) &&
        !(aura_target_has_token(targets, "air", NULL) && target->targtype == TARG_AIR) &&
        !(aura_target_has_token(targets, "ground", "grou") && target->targtype == TARG_GROUND)) return false;
    if (wants_relation &&
        !(aura_target_has_token(targets, "self", NULL) && is_self) &&
        !((aura_target_has_token(targets, "friend", "frie") || aura_target_has_token(targets, "allies", "alli")) && is_friend) &&
        !((aura_target_has_token(targets, "enemy", "enem") || aura_target_has_token(targets, "enemies", NULL)) && is_enemy) &&
        !(aura_target_has_token(targets, "neutral", "neut") && is_neutral)) return false;
    return true;
}

typedef struct {
    FLOAT amount;
    DWORD alias;
    DWORD buff;
} regenerationAuraInfo_t;

static regenerationAuraInfo_t regen_value_cache[MAX_ENTITIES][REGEN_FAMILY_COUNT][REGEN_VALUE_COUNT];

void G_ResetHeroPassiveCaches(void) {
    memset(regen_sources, 0, sizeof(regen_sources));
    regen_source_count = 0;
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

static DWORD aura_buff_code(LPCSTR buff_id) {
    DWORD code = 0;
    if (buff_id && strlen(buff_id) >= 4) memcpy(&code, buff_id, 4);
    return code;
}

/* Discover aura providers once per simulation frame; target checks still run
 * per unit because range, alliances, and invulnerability are live. */
static void regen_aura_cache_update(void) {
    DWORD const ability_generation = G_AbilityDataGeneration();
    if (regen_cache_frame == level.framenum && regen_cache_generation == ability_generation)
        return;
    regen_source_count = 0;
    FOR_LOOP(i, globals.num_edicts) {
        regenAuraSource_t *entry = regen_sources + regen_source_count;
        BOOL has_combat_aura = false;

        entry->source = g_edicts + i;
        entry->life_orc = actor_aura_ability(entry->source, ID_REGEN_LIFE_ORC);
        entry->life_blight = actor_aura_ability(entry->source, ID_REGEN_LIFE_BLIGHT);
        entry->mana = actor_aura_ability(entry->source, ID_REGEN_MANA);
        entry->devotion = actor_aura_ability(entry->source, ID_DEVOTION_AURA);
        entry->unholy = actor_aura_ability(entry->source, ID_UNHOLY_AURA);
        FOR_LOOP(j, sizeof(aura_cache_keys) / sizeof(*aura_cache_keys)) {
            entry->combat[j] = actor_aura_ability(entry->source, aura_cache_keys[j].code);
            if (entry->combat[j].alias) has_combat_aura = true;
        }
        if (entry->life_orc.alias || entry->life_blight.alias || entry->mana.alias ||
            entry->devotion.alias || entry->unholy.alias || has_combat_aura) regen_source_count++;
    }
    regen_cache_frame = level.framenum;
    regen_cache_generation = ability_generation;
    memset(regen_overlays, 0, sizeof(regen_overlays));
    memset(devotion_overlays, 0, sizeof(devotion_overlays));
    memset(unholy_overlays, 0, sizeof(unholy_overlays));
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT effect = g_edicts + i;
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

static auraAbilityRef_t regen_aura_ref(regenAuraSource_t const *entry, DWORD base_code) {
    if (base_code == ID_REGEN_LIFE_ORC) return entry->life_orc;
    if (base_code == ID_REGEN_LIFE_BLIGHT) return entry->life_blight;
    if (base_code == ID_REGEN_MANA) return entry->mana;
    if (base_code == ID_DEVOTION_AURA) return entry->devotion;
    if (base_code == ID_UNHOLY_AURA) return entry->unholy;
    return (auraAbilityRef_t){0};
}

static regenFamily_t regen_family(DWORD base_code) {
    if (base_code == ID_REGEN_LIFE_ORC) return REGEN_FAMILY_LIFE_ORC;
    if (base_code == ID_REGEN_LIFE_BLIGHT) return REGEN_FAMILY_LIFE_BLIGHT;
    return REGEN_FAMILY_MANA;
}

/* Resolve one regeneration aura directly from live providers for a cache refresh. */
static regenerationAuraInfo_t regen_aura_info_uncached(LPEDICT unit, DWORD base_code, BOOL use_maximum) {
    regenerationAuraInfo_t result = {0};

    regen_aura_cache_update();
    FOR_LOOP(i, regen_source_count) {
        LPEDICT source = regen_sources[i].source;
        auraAbilityRef_t const ability = regen_aura_ref(regen_sources + i, base_code);
        abilityLevel_t const *row;
        FLOAT amount;

        if (!ability.alias || !S_AuraUnitActive(source) || !S_SpellIsAliveTarget(source)) continue;
        row = G_AbilityLevel(ability.alias, ability.level);
        FLOAT const distance = Vector2_distance(&source->s.origin2, &unit->s.origin2);
        if (distance > row->area) continue;
        if (!aura_allows_target(source, unit, row->targs)) {
            continue;
        }
        amount = row->data[0].number;
        if (row->data[1].number != 0.0f && use_maximum)
            amount *= base_code == ID_REGEN_MANA ? unit->mana.max_value : unit->health.max_value;
        if (amount > result.amount) {
            LPCSTR buff_id = row->buffID;
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
static FLOAT regen_aura_bonus(LPEDICT unit, DWORD base_code, BOOL use_maximum) {
    regenFamily_t family;
    regenValue_t value;
    LPCVOID ability_data;

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

static BOOL is_regen_aura_overlay(LPCEDICT effect, LPCEDICT unit, DWORD base_code) {
    return effect && effect->inuse && effect->owner == unit && effect->goalentity == unit &&
           effect->summon_ability == base_code;
}

static void sync_regen_aura_overlay(LPEDICT unit, DWORD base_code, regenerationAuraInfo_t const *info) {
    BOOL const needs_resource = base_code == ID_REGEN_MANA
        ? unit->mana.max_value > 0.0f && unit->mana.value < unit->mana.max_value
        : unit->health.max_value > 0.0f && unit->health.value > 0.0f && unit->health.value < unit->health.max_value;
    DWORD effect_code = needs_resource && info ? info->buff : 0;
    LPCSTR art = effect_code ? G_AbilityEffectArt(effect_code, WC3_EFFECT_TARGET, 0) : NULL;

    /* Buff rows may carry only the icon while the alias owns TargetArt. Keep
     * the authored buff presentation when present, then fall back to the
     * ability alias so a valid aura cannot become visually silent. */
    if ((!art || !*art) && needs_resource && info) {
        effect_code = info->alias;
        art = effect_code ? G_AbilityEffectArt(effect_code, WC3_EFFECT_TARGET, 0) : NULL;
    }
    DWORD desired_model = art && *art ? G_RegisterModel(art) : 0;
    regenFamily_t const family = regen_family(base_code);
    LPEDICT keep = unit->s.number < MAX_ENTITIES ? regen_overlays[unit->s.number][family] : NULL;

    if (keep && !is_regen_aura_overlay(keep, unit, base_code)) keep = NULL;
    if (keep && (!desired_model || keep->s.model != desired_model)) {
        G_DestroyEffect(keep);
        regen_overlays[unit->s.number][family] = NULL;
        keep = NULL;
    }

    if (!keep && desired_model) {
        LPEDICT effect = G_SpawnAbilityEffectTarget(effect_code, WC3_EFFECT_TARGET, 0,
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

FLOAT S_RegenerationHealthAura(LPEDICT unit) {
    return regen_aura_bonus(unit, ID_REGEN_LIFE_ORC, true) +
           regen_aura_bonus(unit, ID_REGEN_LIFE_BLIGHT, true);
}

FLOAT S_RegenerationManaAura(LPEDICT unit) {
    return regen_aura_bonus(unit, ID_REGEN_MANA, true);
}

void S_UpdateRegenerationAuraEffects(LPEDICT unit) {
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
BOOL S_RegenerationAuraUpdateDue(LPEDICT unit) {
    aura_cache_update_time();
    if (!unit || unit->s.number >= MAX_ENTITIES ||
        level.time < regen_visual_next_update[unit->s.number]) return false;
    regen_visual_next_update[unit->s.number] = level.time + AURA_UPDATE_MS;
    return true;
}

/* Aura presentation is an ability-owned periodic update, reached through the
 * shared ability dispatcher rather than the physics implementation. */
void S_UpdateUnitPassiveEffects(LPEDICT unit) {
    if (!unit || !unit->inuse || (unit->svflags & SVF_STATIC_SCENERY) ||
        !unit->data.UnitBalance || !S_RegenerationAuraUpdateDue(unit)) return;
    S_UpdateRegenerationAuraEffects(unit);
    S_UpdateHeroAuraEffects(unit);
}

/* Refresh all combat aura families together so one recipient scan serves every consumer. */
static FLOAT hero_aura_bonus(LPEDICT unit, DWORD code, DWORD data) {
    DWORD slot = sizeof(aura_cache_keys) / sizeof(*aura_cache_keys);
    DWORD ability_generation;

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
        FOR_LOOP(i, regen_source_count) {
            regenAuraSource_t const *source = regen_sources + i;
            LPEDICT aura = source->source;
            if (!S_AuraUnitActive(aura) || !S_SpellIsFriend(aura, unit)) continue;
            FOR_LOOP(j, sizeof(aura_cache_keys) / sizeof(*aura_cache_keys)) {
                auraAbilityRef_t const ability = source->combat[j];
                abilityLevel_t const *row;
                if (!ability.alias) continue;
                row = G_AbilityLevel(ability.alias, ability.level);
                if (Vector2_distance(&aura->s.origin2, &unit->s.origin2) > row->area ||
                    !aura_allows_target(aura, unit, row->targs)) continue;
                {
                    FLOAT amount = row->data[aura_cache_keys[j].data - 1].number;
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
                        amount *= balance ? (FLOAT)balance->baseArmor : 0.0f;
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
    FLOAT amount;
    DWORD alias;
    DWORD level;
    DWORD buff;
} heroAuraPresentation_t;

static heroAuraPresentation_t hero_aura_presentation(LPEDICT unit, DWORD base_code) {
    heroAuraPresentation_t result = {0};

    regen_aura_cache_update();
    FOR_LOOP(i, regen_source_count) {
        LPEDICT source = regen_sources[i].source;
        auraAbilityRef_t const ability = regen_aura_ref(regen_sources + i, base_code);
        abilityLevel_t const *row;
        FLOAT amount;
        LPCSTR buff_id;

        if (!S_AuraUnitActive(source) || !S_SpellIsFriend(source, unit)) continue;
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

static void hero_aura_sync_overlay(LPEDICT unit, DWORD base_code, LPEDICT *overlays,
                                   heroAuraPresentation_t const *info) {
    DWORD effect_code = info ? info->buff : 0;
    LPCSTR art = effect_code ? G_AbilityEffectArt(effect_code, WC3_EFFECT_TARGET, 0) : NULL;
    DWORD desired_model;
    LPEDICT keep = unit->s.number < MAX_ENTITIES ? overlays[unit->s.number] : NULL;

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
        LPEDICT effect = G_SpawnAbilityEffectTarget(effect_code, WC3_EFFECT_TARGET, 0, unit, NULL, false);
        if (effect) {
            effect->owner = unit;
            effect->summon_ability = base_code;
            overlays[unit->s.number] = effect;
        }
    }
}

void S_UpdateHeroAuraEffects(LPEDICT unit) {
    heroAuraPresentation_t devotion, unholy;

    if (!unit || !unit->inuse || unit->s.number >= MAX_ENTITIES) return;
    devotion = hero_aura_presentation(unit, ID_DEVOTION_AURA);
    devotion_recipient_buff[unit->s.number] = devotion.alias ? devotion.buff : 0;
    hero_aura_sync_overlay(unit, ID_DEVOTION_AURA, devotion_overlays, devotion.alias ? &devotion : NULL);

    unholy = hero_aura_presentation(unit, ID_UNHOLY_AURA);
    unholy_recipient_buff[unit->s.number] = unholy.alias ? unholy.buff : 0;
    hero_aura_sync_overlay(unit, ID_UNHOLY_AURA, unholy_overlays, unholy.alias ? &unholy : NULL);
}

DWORD S_DevotionAuraBuff(LPEDICT unit) {
    if (!unit || unit->s.number >= MAX_ENTITIES) return 0;
    return devotion_recipient_buff[unit->s.number];
}

DWORD S_UnholyAuraBuff(LPEDICT unit) {
    if (!unit || unit->s.number >= MAX_ENTITIES) return 0;
    return unholy_recipient_buff[unit->s.number];
}

FLOAT S_BrillianceManaRegen(LPEDICT unit) { return hero_aura_bonus(unit, ID_BRILLIANCE, 1); }
FLOAT S_DevotionArmorBonus(LPEDICT unit) { return hero_aura_bonus(unit, ID_DEVOTION_AURA, 1); }
FLOAT S_UnholyHealthRegen(LPEDICT unit) { return hero_aura_bonus(unit, ID_UNHOLY_AURA, 2); }
FLOAT S_UnholyMoveBonus(LPEDICT unit) { return hero_aura_bonus(unit, ID_UNHOLY_AURA, 1); }
FLOAT S_VampiricLifeSteal(LPEDICT unit) { return hero_aura_bonus(unit, ID_VAMPIRIC_AURA, 1); }

static FLOAT slow_aura_bonus(LPCEDICT unit, DWORD data) {
    FLOAT result = 0.0f;
    if (!unit) return 0.0f;
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT source = g_edicts + i;
        auraAbilityRef_t ability;
        abilityLevel_t const *row;
        if (!S_AuraUnitActive(source) || !S_SpellIsAliveTarget(source) || !S_SpellIsEnemy(source, (LPEDICT)unit)) continue;
        ability = actor_aura_ability(source, ID_SLOW_AURA);
        if (!ability.alias) continue;
        row = G_AbilityLevel(ability.alias, ability.level);
        if (Vector2_distance(&source->s.origin2, &unit->s.origin2) > row->area ||
            !aura_allows_target(source, (LPEDICT)unit, row->targs)) continue;
        result = MAX(result, row->data[data - 1].number);
    }
    return MAX(0.0f, MIN(0.9f, result));
}

FLOAT S_SlowAuraMoveReduction(LPCEDICT unit) { return slow_aura_bonus(unit, 1); }
FLOAT S_SlowAuraAttackReduction(LPCEDICT unit) { return slow_aura_bonus(unit, 2); }
FLOAT S_CommandAuraAttackBonus(LPEDICT unit) {
    return MAX(hero_aura_bonus(unit, ID_COMMAND_AURA, 1), hero_aura_bonus(unit, ID_COMMAND_AURA_NEUTRAL, 1));
}
FLOAT S_WarDrumsAttackBonus(LPEDICT unit) { return hero_aura_bonus(unit, ID_WAR_DRUMS, 1); }

FLOAT S_TrueshotAttackBonus(LPEDICT unit) {
    return unit->attack1.type == ATK_PIERCE ? hero_aura_bonus(unit, ID_TRUESHOT_AURA, 1) : 0.0f;
}

int S_SearingArrowDamage(LPEDICT attacker, int damage) {
    DWORD code = ID_SEARING_ARROWS, level = 0;
    if (!attacker) return damage;
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t const *st = attacker->abilstatus + i;
        if (!st->level || (st->timestamp && st->timestamp <= G_Time())) continue;
        if (st->code == ID_SEARING_ARROWS || G_AbilityCode(st->code) == ID_SEARING_ARROWS) {
            code = st->code; level = st->level; break;
        }
    }
    if (!level) { level = G_UnitStatusLevel(attacker, ID_POISON_ARROWS); code = ID_POISON_ARROWS; }
    return level && attacker->attack1.weapon == WPN_MISSILE ? damage + (int)S_SpellData(code, level, 1) : damage;
}

static DWORD mana_shield_buff(DWORD code, DWORD level) {
    LPCSTR buff = G_AbilityLevel(code, level)->buffID;
    return buff && strlen(buff) >= 4 ? FS_SLKKey(buff) : 0;
}

static void mana_shield_remove(LPEDICT unit, DWORD buff) {
    FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (unit->abilstatus[i].level && unit->abilstatus[i].code == buff)
            memset(unit->abilstatus + i, 0, sizeof(unit->abilstatus[i]));
    G_InvalidateUnitInfoPanel(unit);
}

/* Mana Shield owns its authored buff so learned-but-inactive abilities never intercept damage. */
BZ_ABILITY_PROC(CAbilityManaShield) {
    DWORD code = call && call->item && call->item->code ? call->item->code : 0;
    auraAbilityRef_t ref;
    DWORD level, buff;
    BOOL active;
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
int S_ManaShieldDamage(LPEDICT target, int damage) {
    auraAbilityRef_t ref = mana_shield_ability(target);
    DWORD code = ref.alias ? ref.alias : ID_MANA_SHIELD;
    DWORD level = ref.level ? ref.level : G_UnitAbilityLevel(target, ID_MANA_SHIELD);
    DWORD buff = level ? mana_shield_buff(code, level) : 0;
    FLOAT ratio, fraction, absorbed;
    if (!buff || !G_UnitStatusLevel(target, buff) || damage <= 0 || target->mana.value <= 0.0f) return damage;
    ratio = S_SpellData(code, level, 1);
    fraction = MIN(1.0f, MAX(0.0f, S_SpellData(code, level, 2)));
    if (ratio <= 0.0f || fraction <= 0.0f) return damage;
    absorbed = MIN((FLOAT)damage * fraction, target->mana.value * ratio);
    target->mana.value = MAX(0.0f, target->mana.value - absorbed / ratio);
    if (target->mana.value <= 0.0f) mana_shield_remove(target, buff);
    return damage - (int)absorbed;
}

FLOAT S_ThornsDamageReturn(LPCEDICT target, LPCEDICT attacker, FLOAT damage) {
    if (!target || !attacker || (attacker->attack1.weapon != WPN_NORMAL && attacker->attack1.weapon != WPN_INSTANT))
        return 0.0f;
    return damage * hero_aura_bonus((LPEDICT)target, ID_THORNS_AURA, 1);
}

BOOL S_EvasionRoll(LPEDICT target) {
    abilityAliasRef_t ev = S_ResolveAbilityAlias(target, ID_EVASION);
    DWORD level;
    if (ev.alias && ev.level && (FLOAT)(rand() % 10000) / 10000.0f < S_SpellData(ev.alias, ev.level, 1)) return true;
    level = G_UnitAbilityLevel(target, ID_DRUNKEN_BRAWLER);
    return level && (FLOAT)(rand() % 10000) / 10000.0f < S_SpellData(ID_DRUNKEN_BRAWLER, level, 4);
}

int S_CriticalStrikeDamage(LPEDICT attacker, int damage) {
    DWORD level = G_UnitAbilityLevel(attacker, ID_CRITICAL_STRIKE);
    DWORD code = ID_CRITICAL_STRIKE;
    if (!level) { level = G_UnitAbilityLevel(attacker, ID_CREEP_CRITICAL_STRIKE); code = ID_CREEP_CRITICAL_STRIKE; }
    if (!level) { level = G_UnitAbilityLevel(attacker, ID_DRUNKEN_BRAWLER); code = ID_DRUNKEN_BRAWLER; }
    if (!level || (FLOAT)(rand() % 100) >= S_SpellData(code, level, 1)) return damage;
    return (int)((FLOAT)damage * MAX(1.0f, S_SpellData(code, level, 2)));
}

FLOAT S_SpikedArmorBonus(LPCEDICT unit) {
    DWORD level = G_UnitAbilityLevel(unit, ID_SPIKED_CARAPACE);
    return level ? S_SpellData(ID_SPIKED_CARAPACE, level, 3) : 0.0f;
}

FLOAT S_SpikedDamageReturn(LPCEDICT unit, FLOAT damage) {
    DWORD code = ID_SPIKED_CARAPACE, level = G_UnitAbilityLevel(unit, code);
    if (!level) { code = ID_SPIKED_BARRICADES; level = G_UnitAbilityLevel(unit, code); }
    if (!level) return 0.0f;
    return MAX(S_SpellData(code, level, 2), damage * S_SpellData(code, level, 1));
}

/* Pulverize is a passive attack proc. DataA is percent chance, DataB damage,
 * DataC/D full/half damage radii; the authored Area cell is unused.
 * Its damage is an authored physical-spell event, so secondary victims do not
 * recursively trigger attack listeners. */
void S_PulverizeAttack(LPEDICT attacker, LPCEDICT primary) {
    abilityAliasRef_t ability = S_ResolveAbilityAlias(attacker, ID_PULVERIZE);
    DWORD code = ability.alias, level = ability.level;
    FLOAT full_radius, partial_radius, chance, full_damage, partial_damage;
    if (!level || !primary) return;
    chance = S_SpellData(code, level, 1) * 0.01f;
    if ((FLOAT)(rand() % 10000) / 10000.0f >= chance) return;
    full_damage = S_SpellData(code, level, 2);
    partial_damage = full_damage * 0.5f;
    full_radius = S_SpellData(code, level, 3);
    partial_radius = S_SpellData(code, level, 4);
    FILTER_EDICTS(target, target != attacker && target != primary &&
                  S_SpellIsAliveTarget(target) && S_SpellIsEnemy(attacker, target) &&
                  target->targtype == TARG_GROUND) {
        FLOAT distance = Vector2_distance(&target->s.origin2, &primary->s.origin2);
        FLOAT amount = distance <= full_radius ? full_damage :
                       distance <= partial_radius ? partial_damage : 0.0f;
        if (amount > 0.0f) S_SpellDamage(target, attacker, (int)amount);
    }
}
