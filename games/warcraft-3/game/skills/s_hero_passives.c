#include "s_skills.h"

#define ID_BRILLIANCE MAKEFOURCC('A', 'H', 'a', 'b')
#define ID_CRITICAL_STRIKE MAKEFOURCC('A', 'O', 'c', 'r')
#define ID_SPIKED_CARAPACE MAKEFOURCC('A', 'U', 't', 's')
#define ID_UNHOLY_AURA MAKEFOURCC('A', 'U', 'a', 'u')
#define ID_EVASION MAKEFOURCC('A', 'E', 'e', 'v')
#define ID_VAMPIRIC_AURA MAKEFOURCC('A', 'U', 'a', 'v')
#define ID_THORNS_AURA MAKEFOURCC('A', 'E', 'a', 'h')
#define ID_MANA_SHIELD MAKEFOURCC('A', 'N', 'm', 's')
#define ID_DRUNKEN_BRAWLER MAKEFOURCC('A', 'N', 'd', 'b')
#define ID_SEARING_ARROWS MAKEFOURCC('A', 'H', 'f', 'a')
#define ID_POISON_ARROWS MAKEFOURCC('A', 'E', 'p', 'a')
#define ID_TRUESHOT_AURA MAKEFOURCC('A', 'E', 'a', 'r')

#define ID_REGEN_LIFE_ORC MAKEFOURCC('A', 'o', 'a', 'r')
#define ID_REGEN_LIFE_BLIGHT MAKEFOURCC('A', 'a', 'b', 'r')
#define ID_REGEN_MANA MAKEFOURCC('A', 'a', 'r', 'm')

typedef struct {
    DWORD alias;
    DWORD level;
} auraAbilityRef_t;

typedef struct {
    LPEDICT source;
    auraAbilityRef_t life_orc;
    auraAbilityRef_t life_blight;
    auraAbilityRef_t mana;
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
static LPCVOID regen_cache_ability_data;
static LPEDICT regen_overlays[MAX_ENTITIES][REGEN_FAMILY_COUNT];
static DWORD regen_value_next_update[MAX_ENTITIES];
static DWORD regen_visual_next_update[MAX_ENTITIES];
static LPCVOID regen_value_ability_data[MAX_ENTITIES];

typedef struct {
    DWORD code, data;
} aura_cache_key_t;

static aura_cache_key_t const aura_cache_keys[] = {
    { ID_BRILLIANCE, 1 },
    { ID_UNHOLY_AURA, 1 },
    { ID_UNHOLY_AURA, 2 },
    { ID_VAMPIRIC_AURA, 1 },
    { ID_TRUESHOT_AURA, 1 },
    { ID_THORNS_AURA, 1 }
};
static FLOAT aura_cache[MAX_ENTITIES][sizeof(aura_cache_keys) / sizeof(*aura_cache_keys)];
static DWORD aura_cache_next_update[MAX_ENTITIES];
static LPCVOID aura_cache_ability_data[MAX_ENTITIES];
static DWORD aura_cache_last_time = UINT_MAX;

static regenFamily_t regen_family(DWORD base_code);

/* Invalidate deadlines when a new map or test resets the simulation clock. */
static void aura_cache_update_time(void) {
    if (level.time < aura_cache_last_time) {
        memset(regen_value_next_update, 0, sizeof(regen_value_next_update));
        memset(regen_visual_next_update, 0, sizeof(regen_visual_next_update));
        memset(regen_value_ability_data, 0, sizeof(regen_value_ability_data));
        memset(aura_cache_next_update, 0, sizeof(aura_cache_next_update));
        memset(aura_cache_ability_data, 0, sizeof(aura_cache_ability_data));
    }
    aura_cache_last_time = level.time;
}

static auraAbilityRef_t actor_aura_ability(LPEDICT ent, DWORD base_code) {
    auraAbilityRef_t result = {0};
    char alias_name[5] = {0};

    if (!ent || !base_code) return result;

    if (ent->data.UnitAbilities && ent->data.UnitAbilities->abilList) {
        PARSE_LIST(ent->data.UnitAbilities->abilList, token, parse_segment) {
            DWORD alias = 0;
            if (strlen(token) != 4 || !G_ActorHasSkill(ent, token)) continue;
            memcpy(&alias, token, sizeof(alias));
            if (G_AbilityCode(alias) == base_code) {
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
        if (G_ActorHasSkill(ent, alias_name) && G_AbilityCode(alias) == base_code) {
            result.alias = alias;
            result.level = 1;
            return result;
        }
    }

    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        heroability_t const *hero = ent->heroabilities + i;
        if (hero->level && G_AbilityCode(hero->code) == base_code) {
            result.alias = hero->code;
            result.level = hero->level;
            return result;
        }
    }
    return result;
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

    if (!source || !target || !target->inuse || !S_SpellIsAliveTarget(target)) return false;
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

static DWORD aura_buff_code(LPCSTR buff_id) {
    DWORD code = 0;
    if (buff_id && strlen(buff_id) >= 4) memcpy(&code, buff_id, 4);
    return code;
}

/* Discover regeneration providers once per simulation frame; target checks
 * still run per unit because range, alliances, and invulnerability are live. */
static void regen_aura_cache_update(void) {
    LPCVOID ability_data = G_AbilityData(ID_REGEN_LIFE_ORC);
    if (level.framenum && regen_cache_frame == level.framenum && regen_cache_ability_data == ability_data)
        return;
    regen_source_count = 0;
    FOR_LOOP(i, globals.num_edicts) {
        regenAuraSource_t *entry = regen_sources + regen_source_count;

        entry->source = g_edicts + i;
        entry->life_orc = actor_aura_ability(entry->source, ID_REGEN_LIFE_ORC);
        entry->life_blight = actor_aura_ability(entry->source, ID_REGEN_LIFE_BLIGHT);
        entry->mana = actor_aura_ability(entry->source, ID_REGEN_MANA);
        if (entry->life_orc.alias || entry->life_blight.alias || entry->mana.alias) regen_source_count++;
    }
    regen_cache_frame = level.framenum;
    regen_cache_ability_data = ability_data;
    memset(regen_overlays, 0, sizeof(regen_overlays));
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT effect = g_edicts + i;
        regenFamily_t family;
        if (!effect->inuse || !effect->owner || effect->owner->s.number >= MAX_ENTITIES ||
            effect->goalentity != effect->owner) continue;
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
    return entry->mana;
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

        if (!source->inuse || !ability.alias || !S_SpellIsAliveTarget(source)) continue;
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

/* Refresh all combat aura families together so one recipient scan serves every consumer. */
static FLOAT hero_aura_bonus(LPEDICT unit, DWORD code, DWORD data) {
    DWORD slot = sizeof(aura_cache_keys) / sizeof(*aura_cache_keys);
    LPCVOID ability_data;

    FOR_LOOP(i, sizeof(aura_cache_keys) / sizeof(*aura_cache_keys))
        if (aura_cache_keys[i].code == code && aura_cache_keys[i].data == data) { slot = i; break; }
    if (slot == sizeof(aura_cache_keys) / sizeof(*aura_cache_keys) || !unit || unit->s.number >= MAX_ENTITIES)
        return 0.0f;
    aura_cache_update_time();
    ability_data = G_AbilityData(ID_BRILLIANCE);
    if (level.time >= aura_cache_next_update[unit->s.number] ||
        aura_cache_ability_data[unit->s.number] != ability_data) {
        memset(aura_cache[unit->s.number], 0, sizeof(aura_cache[unit->s.number]));
        FOR_LOOP(i, globals.num_edicts) {
            LPEDICT aura = g_edicts + i;
            if (!aura->inuse || !S_SpellIsAliveTarget(aura) || !S_SpellIsFriend(aura, unit)) continue;
            FOR_LOOP(j, sizeof(aura_cache_keys) / sizeof(*aura_cache_keys)) {
                DWORD const aura_level = G_UnitAbilityLevel(aura, aura_cache_keys[j].code);
                if (!aura_level || Vector2_distance(&aura->s.origin2, &unit->s.origin2) >
                    S_SpellNumber(aura_cache_keys[j].code, ABILITY_NUMBER_AREA, aura_level)) continue;
                aura_cache[unit->s.number][j] = MAX(aura_cache[unit->s.number][j],
                    S_SpellData(aura_cache_keys[j].code, aura_level, aura_cache_keys[j].data));
            }
        }
        aura_cache_next_update[unit->s.number] = level.time + AURA_UPDATE_MS;
        aura_cache_ability_data[unit->s.number] = ability_data;
    }
    return aura_cache[unit->s.number][slot];
}

FLOAT S_BrillianceManaRegen(LPEDICT unit) { return hero_aura_bonus(unit, ID_BRILLIANCE, 1); }
FLOAT S_UnholyHealthRegen(LPEDICT unit) { return hero_aura_bonus(unit, ID_UNHOLY_AURA, 2); }
FLOAT S_UnholyMoveBonus(LPEDICT unit) { return hero_aura_bonus(unit, ID_UNHOLY_AURA, 1); }
FLOAT S_VampiricLifeSteal(LPEDICT unit) { return hero_aura_bonus(unit, ID_VAMPIRIC_AURA, 1); }

FLOAT S_TrueshotAttackBonus(LPEDICT unit) {
    return unit->attack1.type == ATK_PIERCE ? hero_aura_bonus(unit, ID_TRUESHOT_AURA, 1) : 0.0f;
}

int S_SearingArrowDamage(LPEDICT attacker, int damage) {
    DWORD level = G_UnitStatusLevel(attacker, ID_SEARING_ARROWS);
    DWORD code = ID_SEARING_ARROWS;
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
    DWORD code = call && call->item && call->item->code ? call->item->code : ID_MANA_SHIELD;
    DWORD level = S_SpellLevel(ent, code), buff = mana_shield_buff(code, level);
    BOOL active = buff && G_UnitStatusLevel(ent, buff);
    switch (msg) {
    case A_TOGGLE_ON: return active;
    case A_EXECUTE:
        if (active) mana_shield_remove(ent, buff);
        else if (buff && ent->mana.value > 0.0f) unit_addstatus(ent, GetClassName(buff), level);
        return true;
    case A_ORDER:
        if (!call || !call->order) return false;
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
    DWORD level = G_UnitAbilityLevel(target, ID_MANA_SHIELD);
    DWORD buff = level ? mana_shield_buff(ID_MANA_SHIELD, level) : 0;
    FLOAT ratio, fraction, absorbed;
    if (!buff || !G_UnitStatusLevel(target, buff) || damage <= 0 || target->mana.value <= 0.0f) return damage;
    ratio = S_SpellData(ID_MANA_SHIELD, level, 1);
    fraction = MIN(1.0f, MAX(0.0f, S_SpellData(ID_MANA_SHIELD, level, 2)));
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
    DWORD level = G_UnitAbilityLevel(target, ID_EVASION);
    if (level && (FLOAT)(rand() % 10000) / 10000.0f < S_SpellData(ID_EVASION, level, 1)) return true;
    level = G_UnitAbilityLevel(target, ID_DRUNKEN_BRAWLER);
    return level && (FLOAT)(rand() % 10000) / 10000.0f < S_SpellData(ID_DRUNKEN_BRAWLER, level, 4);
}

int S_CriticalStrikeDamage(LPEDICT attacker, int damage) {
    DWORD level = G_UnitAbilityLevel(attacker, ID_CRITICAL_STRIKE);
    DWORD code = ID_CRITICAL_STRIKE;
    if (!level) { level = G_UnitAbilityLevel(attacker, ID_DRUNKEN_BRAWLER); code = ID_DRUNKEN_BRAWLER; }
    if (!level || (FLOAT)(rand() % 100) >= S_SpellData(code, level, 1)) return damage;
    return (int)((FLOAT)damage * MAX(1.0f, S_SpellData(code, level, 2)));
}

FLOAT S_SpikedArmorBonus(LPCEDICT unit) {
    DWORD level = G_UnitAbilityLevel(unit, ID_SPIKED_CARAPACE);
    return level ? S_SpellData(ID_SPIKED_CARAPACE, level, 3) : 0.0f;
}

FLOAT S_SpikedDamageReturn(LPCEDICT unit, FLOAT damage) {
    DWORD level = G_UnitAbilityLevel(unit, ID_SPIKED_CARAPACE);
    if (!level) return 0.0f;
    return MAX(S_SpellData(ID_SPIKED_CARAPACE, level, 2), damage * S_SpellData(ID_SPIKED_CARAPACE, level, 1));
}
