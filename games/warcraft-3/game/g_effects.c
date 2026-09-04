#include "g_local.h"

/* Warcraft III ability presentation art is selected by an effect-type enum.
 * Keep the renderer/content boundary on the game side: gameplay chooses an
 * ability/buff rawcode and effect slot, this module resolves the model path,
 * and the shared engine only sees a registered model index on an ordinary
 * game edict. */

static umove_t wc3_effect_temp_birth;
static umove_t wc3_effect_temp_stand;
static umove_t wc3_effect_birth;
static umove_t wc3_effect_stand;
static umove_t wc3_effect_death;

static void G_EffectEnterStand(LPEDICT effect);
static void G_EffectLoopStand(LPEDICT effect);

static umove_t wc3_effect_temp_birth = { "birth", NULL, G_FreeEdict };
static umove_t wc3_effect_temp_stand = { "stand", NULL, G_FreeEdict };
static umove_t wc3_effect_birth = { "birth", NULL, G_EffectEnterStand };
static umove_t wc3_effect_stand = { "stand", NULL, G_EffectLoopStand };
static umove_t wc3_effect_death = { "death", NULL, G_FreeEdict };

static LPCSTR G_EffectFieldName(wc3EffectType_t type, BOOL alternate) {
    switch (type) {
        case WC3_EFFECT_EFFECT:      return alternate ? "Effectart"     : "EffectArt";
        case WC3_EFFECT_TARGET:      return alternate ? "Targetart"     : "TargetArt";
        case WC3_EFFECT_CASTER:      return alternate ? "Casterart"     : "CasterArt";
        case WC3_EFFECT_SPECIAL:     return alternate ? "Specialart"    : "SpecialArt";
        case WC3_EFFECT_AREA_EFFECT: return alternate ? "Areaeffectart" : "AreaEffectArt";
        case WC3_EFFECT_MISSILE:     return alternate ? "Missileart"    : "MissileArt";
        default:                     return NULL;
    }
}


static LPCSTR G_BuffEffectValue(DWORD buff_id, wc3EffectType_t type) {
    AbilityBuffData_t const *row = G_AbilityBuffData(buff_id);
    LPCSTR value = NULL;

    if (row->id != buff_id) return NULL;
    switch (type) {
        case WC3_EFFECT_EFFECT:  value = row->effectArt; break;
        case WC3_EFFECT_TARGET:  value = row->targetArt; break;
        case WC3_EFFECT_SPECIAL: value = row->specialArt; break;
        case WC3_EFFECT_MISSILE: value = row->missileArt; break;
        default: break;
    }
    if (value && *value && strcmp(value, "-") && strcmp(value, "_")) return value;

    if (row->code && row->code != buff_id) {
        AbilityBuffData_t const *base = G_AbilityBuffData(row->code);
        if (base->id == row->code) {
            switch (type) {
                case WC3_EFFECT_EFFECT:  return base->effectArt;
                case WC3_EFFECT_TARGET:  return base->targetArt;
                case WC3_EFFECT_SPECIAL: return base->specialArt;
                case WC3_EFFECT_MISSILE: return base->missileArt;
                default: break;
            }
        }
    }
    return NULL;
}

static LPCSTR G_EffectConfigValue(DWORD ability_id, wc3EffectType_t type) {
    char classname[5];
    LPCSTR field;
    LPCSTR value;
    AbilityData_t const *row;

    memcpy(classname, &ability_id, 4);
    classname[4] = '\0';
    field = G_EffectFieldName(type, false);
    value = field ? FindConfigValue(classname, field) : NULL;
    if (!value) {
        field = G_EffectFieldName(type, true);
        value = field ? FindConfigValue(classname, field) : NULL;
    }
    if (value) return value;

    /* Custom aliases may inherit presentation from their base code. The SLK
     * resolver already exposes that base rawcode, so use it only as a fallback
     * after the alias's own Func entry has been checked. */
    row = G_AbilityData(ability_id);
    if (row->code && row->code != ability_id) {
        memcpy(classname, &row->code, 4);
        classname[4] = '\0';
        field = G_EffectFieldName(type, false);
        value = field ? FindConfigValue(classname, field) : NULL;
        if (!value) {
            field = G_EffectFieldName(type, true);
            value = field ? FindConfigValue(classname, field) : NULL;
        }
        if (value) return value;
    }
    return G_BuffEffectValue(ability_id, type);
}

LPCSTR G_AbilityEffectArt(DWORD ability_id, wc3EffectType_t type, DWORD index) {
    static char selected[4][MAX_PATHLEN];
    static DWORD cursor;
    char *out = selected[cursor++ & 3];
    LPCSTR list = G_EffectConfigValue(ability_id, type);
    DWORD count = 0;

    out[0] = '\0';
    if (!list || !*list || type == WC3_EFFECT_LIGHTNING) return NULL;

    PARSE_LIST(list, art, parse_segment) {
        if (!art || !*art || !strcmp(art, "-") || !strcmp(art, "_")) continue;
        strlcpy(out, art, MAX_PATHLEN);
        if (count++ == index) return out;
    }

    /* Warsmash's AbilityUI selection falls back to the last configured art
     * entry when an index exceeds the list. Preserve that useful data-driven
     * behavior rather than turning an otherwise valid spell invisible. */
    return count ? out : NULL;
}

void G_EffectValidateTarget(LPEDICT effect) {
    if (!effect->goalentity || !effect->goalentity->inuse ||
        effect->goalentity->spawn_time != effect->damage) {
        effect->goalentity = NULL;
        effect->movetype = MOVETYPE_NONE;
        effect->think = G_FreeEdict;
    }
}

void G_EffectThink(LPEDICT effect) {
    if (effect->goalentity && effect->wait != 0.0f) {
        effect->s.origin.z += effect->wait;
    }
    M_MoveFrame(effect);
}

static void G_EffectLoopStand(LPEDICT effect) {
    unit_setmove(effect, &wc3_effect_stand);
}

static void G_EffectEnterStand(LPEDICT effect) {
    unit_setmove(effect, &wc3_effect_stand);
    if (!effect->animation) {
        effect->think = NULL;
        effect->currentmove = NULL;
    }
}

static void G_EffectStartAnimation(LPEDICT effect, BOOL temporary) {
    unit_setmove(effect, temporary ? &wc3_effect_temp_birth : &wc3_effect_birth);
    if (effect->animation) return;

    unit_setmove(effect, temporary ? &wc3_effect_temp_stand : &wc3_effect_stand);
    if (!effect->animation) {
        if (temporary) {
            /* No usable animation sequence means there is no deterministic
             * lifetime to drive. A one-shot art with no animation should not
             * leak an edict indefinitely. */
            G_FreeEdict(effect);
        } else {
            effect->think = NULL;
            effect->currentmove = NULL;
        }
    }
}

LPEDICT G_SpawnModelEffect(LPCSTR model, LPCVECTOR2 point, LPEDICT target,
                           LPCSTR attach_point, BOOL temporary) {
    LPEDICT effect;

    if (!model || !*model || (!point && !target)) return NULL;
    effect = G_Spawn();
    effect->s.model = G_RegisterModel(model);
    if (!effect->s.model) {
        G_FreeEdict(effect);
        return NULL;
    }

    if (target) {
        effect->s.origin = target->s.origin;
        effect->s.origin2 = target->s.origin2;
        effect->s.angle = target->s.angle;
        effect->goalentity = target;
        effect->damage = target->spawn_time; /* target generation guard */
        effect->movetype = MOVETYPE_LINK;
        effect->prethink = G_EffectValidateTarget;
        if (attach_point && !strcasecmp(attach_point, "overhead")) {
            effect->wait = target->s.radius * 2.5f;
            effect->s.origin.z += effect->wait;
        }
    } else {
        effect->s.origin2 = *point;
        effect->s.origin.x = point->x;
        effect->s.origin.y = point->y;
        effect->s.origin.z = CM_GetHeightAtPoint(point->x, point->y);
        effect->movetype = MOVETYPE_NONE;
    }

    effect->think = G_EffectThink;
    G_EffectStartAnimation(effect, temporary);
    return effect->inuse ? effect : NULL;
}

LPEDICT G_SpawnAbilityEffectAtPoint(DWORD ability_id, wc3EffectType_t type, DWORD index,
                                    LPCVECTOR2 point, BOOL temporary) {
    return G_SpawnModelEffect(G_AbilityEffectArt(ability_id, type, index), point, NULL, NULL, temporary);
}

LPEDICT G_SpawnAbilityEffectTarget(DWORD ability_id, wc3EffectType_t type, DWORD index,
                                   LPEDICT target, LPCSTR attach_point, BOOL temporary) {
    return G_SpawnModelEffect(G_AbilityEffectArt(ability_id, type, index), NULL, target, attach_point, temporary);
}

void G_DestroyEffect(LPEDICT effect) {
    if (!effect || !effect->inuse) return;
    effect->prethink = NULL;
    effect->goalentity = NULL;
    effect->movetype = MOVETYPE_NONE;
    effect->wait = 0.0f;
    effect->think = G_EffectThink;
    unit_setmove(effect, &wc3_effect_death);
    if (!effect->animation) G_FreeEdict(effect);
}
