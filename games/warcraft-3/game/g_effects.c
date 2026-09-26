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

static void G_EffectEnterStand(edict_t *effect);
static void G_EffectLoopStand(edict_t *effect);

static umove_t wc3_effect_temp_birth = { "birth", NULL, G_FreeEdict };
static umove_t wc3_effect_temp_stand = { "stand", NULL, G_FreeEdict };
static umove_t wc3_effect_birth = { "birth", NULL, G_EffectEnterStand };
static umove_t wc3_effect_stand = { "stand", NULL, G_EffectLoopStand };
static umove_t wc3_effect_death = { "death", NULL, G_FreeEdict };

static cstring_t G_EffectFieldName(wc3EffectType_t type, bool alternate) {
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


static cstring_t G_BuffEffectValue(uint32_t buff_id, wc3EffectType_t type) {
    AbilityBuffData_t const *row = G_AbilityBuffData(buff_id);
    cstring_t value = NULL;

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

static cstring_t G_EffectConfigValue(uint32_t ability_id, wc3EffectType_t type) {
    char classname[5];
    cstring_t field;
    cstring_t value;
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


/* Resolve an optional presentation override before reading the authored ability row. */
static cstring_t G_AbilityPresentationValue(uint32_t ability_id, cstring_t field) {
    char classname[5];
    cstring_t value;
    AbilityData_t const *row;

    if (!field) return NULL;
    memcpy(classname, &ability_id, 4);
    classname[4] = '\0';
    value = FindConfigValue(classname, field);
    if (value && *value && strcmp(value, "-") && strcmp(value, "_")) return value;

    row = G_AbilityData(ability_id);
    if (row->code && row->code != ability_id) {
        memcpy(classname, &row->code, 4);
        classname[4] = '\0';
        value = FindConfigValue(classname, field);
        if (value && *value && strcmp(value, "-") && strcmp(value, "_")) return value;
    }
    return NULL;
}

/* Select the indexed LightningEffect rawcode while preserving the last authored fallback. */
uint32_t G_AbilityLightningId(uint32_t ability_id, uint32_t index) {
    cstring_t list = G_AbilityPresentationValue(ability_id, "LightningEffect");
    uint32_t selected = 0, count = 0;

    if (!list || !*list) return 0;
    PARSE_LIST(list, lightning, parse_segment) {
        if (!lightning || strlen(lightning) < 4 || !strcmp(lightning, "-") || !strcmp(lightning, "_")) continue;
        selected = MAKEFOURCC(lightning[0], lightning[1], lightning[2], lightning[3]);
        if (count++ == index) return selected;
    }
    return count ? selected : 0;
}

/* Reject pointers outside the stable level registry before any native mutates a handle. */
bool G_LightningValid(gLightning_t const *effect) {
    uintptr_t pointer = (uintptr_t)effect, base = (uintptr_t)level.lightning_effects;
    return effect && pointer >= base && pointer < base + sizeof(level.lightning_effects) &&
        (pointer - base) % sizeof(*effect) == 0 && effect->inuse;
}

/* Allocate one save-stable lightning slot and initialize its presentation state. */
gLightning_t *G_LightningAdd(lightningAddParams_t const *params) {
    gLightning_t *effect = NULL;
    uint32_t now;

    if (!params || !params->effect_id || !params->source || !params->target) return NULL;
    FOR_LOOP(i, MAX_LIGHTNING_EFFECTS) {
        if (!level.lightning_effects[i].inuse) {
            effect = level.lightning_effects + i;
            break;
        }
    }
    if (!effect) {
        fprintf(stderr, "WC3 Lightning: presentation registry is full (%u slots)\n", (unsigned)MAX_LIGHTNING_EFFECTS);
        return NULL;
    }
    memset(effect, 0, sizeof(*effect));
    effect->inuse = true;
    if (++level.next_lightning_id == 0) level.next_lightning_id = 1;
    now = G_Time();
    effect->state.handle = level.next_lightning_id;
    effect->state.effect_id = params->effect_id;
    effect->state.source = *params->source;
    effect->state.target = *params->target;
    effect->state.color = params->color;
    effect->script_color[0] = BYTE2FLOAT(params->color.r); effect->script_color[1] = BYTE2FLOAT(params->color.g);
    effect->script_color[2] = BYTE2FLOAT(params->color.b); effect->script_color[3] = BYTE2FLOAT(params->color.a);
    effect->state.start_time = now;
    effect->state.end_time = params->duration_ms ? now + params->duration_ms : 0;
    return effect;
}

/* Replace explicit coordinates without changing an attached endpoint contract. */
void G_LightningMove(gLightning_t *effect, vector3_t const *source, vector3_t const *target) {
    if (!G_LightningValid(effect)) return;
    if (source) effect->state.source = *source;
    if (target) effect->state.target = *target;
}

/* Validate an endpoint's spawn generation before copying its current origin. */
static bool G_LightningEntityValid(edict_t *entity, uint32_t spawn_time) {
    return entity && entity->inuse && entity->spawn_time == spawn_time;
}

/* Refresh one endpoint or clear it when its edict was freed or reused. */
static void G_LightningEndpoint(edict_t * *entity, uint32_t *spawn_time, vector3_t *position) {
    if (!G_LightningEntityValid(*entity, *spawn_time)) {
        *entity = NULL; *spawn_time = 0;
        return;
    }
    *position = (*entity)->s.origin;
    position->z += (*entity)->s.radius * 0.5f;
}

/* Attach a spell bolt to the units that own its two moving endpoints. */
void G_LightningAttach(gLightning_t *effect, edict_t const *source, edict_t const *target) {
    if (!G_LightningValid(effect)) return;
    effect->source_entity = (edict_t *)source;
    effect->source_spawn_time = source ? source->spawn_time : 0;
    effect->target_entity = (edict_t *)target;
    effect->target_spawn_time = target ? target->spawn_time : 0;
    G_LightningUpdateAttached(effect);
}

/* Refresh both attached endpoints before the next client datagram is emitted. */
void G_LightningUpdateAttached(gLightning_t *effect) {
    if (!G_LightningValid(effect)) return;
    G_LightningEndpoint(&effect->source_entity, &effect->source_spawn_time, &effect->state.source);
    G_LightningEndpoint(&effect->target_entity, &effect->target_spawn_time, &effect->state.target);
}

/* Update the byte colour used by the renderer and the precise JASS colour cache. */
void G_LightningColor(gLightning_t *effect, color32_t color) {
    if (!G_LightningValid(effect)) return;
    effect->state.color = color;
    effect->script_color[0] = BYTE2FLOAT(color.r); effect->script_color[1] = BYTE2FLOAT(color.g);
    effect->script_color[2] = BYTE2FLOAT(color.b); effect->script_color[3] = BYTE2FLOAT(color.a);
}

/* Store JASS's unclamped colour values alongside the wire-compatible bytes. */
void G_LightningScriptColor(gLightning_t *effect, color32_t color, float const *precise) {
    if (!G_LightningValid(effect)) return;
    effect->state.color = color;
    if (precise) memcpy(effect->script_color, precise, sizeof(effect->script_color));
}

/* Release a lightning slot so its stable registry ordinal can be reused. */
void G_LightningRemove(gLightning_t *effect) {
    if (!G_LightningValid(effect)) return;
    memset(effect, 0, sizeof(*effect));
}

/* Resolve and attach one ability-selected bolt to its caster and target. */
gLightning_t *G_SpawnAbilityLightning(abilityLightningParams_t const *params) {
    vector3_t from, to;
    uint32_t effect_id;

    if (!params || !params->source || !params->target) return NULL;
    effect_id = G_AbilityLightningId(params->ability_id, params->index);
    if (!effect_id) return NULL;
    from = params->source->s.origin;
    to = params->target->s.origin;
    /* Ability lightning connects unit bodies, not terrain.  A half-radius lift
     * keeps the generic ribbon near the model centre without renderer-side WC3
     * attachment knowledge. */
    from.z += params->source->s.radius * 0.5f;
    to.z += params->target->s.radius * 0.5f;
    {
        gLightning_t *effect = G_LightningAdd(&(lightningAddParams_t){
            .effect_id = effect_id, .source = &from, .target = &to,
            .color = COLOR32_WHITE, .duration_ms = params->duration_ms,
        });
        G_LightningAttach(effect, params->source, params->target);
        return effect;
    }
}

cstring_t G_AbilityEffectArt(uint32_t ability_id, wc3EffectType_t type, uint32_t index) {
    static char selected[4][MAX_PATHLEN];
    static uint32_t cursor;
    char *out = selected[cursor++ & 3];
    cstring_t list = G_EffectConfigValue(ability_id, type);
    uint32_t count = 0;

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

void G_EffectValidateTarget(edict_t *effect) {
    if (!effect->goalentity || !effect->goalentity->inuse ||
        effect->goalentity->spawn_time != effect->damage) {
        effect->goalentity = NULL;
        effect->movetype = MOVETYPE_NONE;
        effect->think = G_FreeEdict;
    }
}

void G_EffectThink(edict_t *effect) {
    if (effect->goalentity && effect->wait != 0.0f) {
        effect->s.origin.z += effect->wait;
    }
    M_MoveFrame(effect);
}

static void G_EffectLoopStand(edict_t *effect) {
    unit_setmove(effect, &wc3_effect_stand);
}

static void G_EffectEnterStand(edict_t *effect) {
    unit_setmove(effect, &wc3_effect_stand);
    if (!effect->animation) {
        effect->think = G_FreeEdict;
        effect->currentmove = NULL;
    }
}

static void G_EffectStartAnimation(edict_t *effect, bool temporary) {
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
            /* A persistent effect with no animation sequences cannot drive its
             * own lifetime.  Schedule an immediate free so the edict does not
             * leak when the JASS caller omits DestroyEffect. */
            effect->think = G_FreeEdict;
            effect->currentmove = NULL;
        }
    }
}

edict_t *G_SpawnModelEffect(cstring_t model, vector2_t const *point, edict_t *target,
                           cstring_t attach_point, bool temporary) {
    edict_t *effect;

    if (!model || !*model || (!point && !target)) return NULL;
    effect = G_Spawn();
    if (!effect) return NULL;
    /* JASS effect extends agent, not widget.  Special/spell effect art is
     * presentation only and must never win world selection or right-click
     * picking over the terrain/real widget beneath it.  The client maps this
     * snapshot bit to RF_NOT_SELECTABLE, so the model still renders normally
     * while TraceEntity/rectangle selection pass through it. */
    effect->s.flags |= EF_NOT_SELECTABLE;
    effect->s.model = G_RegisterModel(model);
    if (!effect->s.model) {
        G_FreeEdict(effect);
        return NULL;
    }

    if (target) {
        if (target->svflags & SVF_MONSTER) G_InheritUnitTeamColor(effect, target);
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

edict_t *G_SpawnAbilityEffectAtPoint(uint32_t ability_id, wc3EffectType_t type, uint32_t index,
                                    vector2_t const *point, bool temporary) {
    return G_SpawnModelEffect(G_AbilityEffectArt(ability_id, type, index), point, NULL, NULL, temporary);
}

edict_t *G_SpawnAbilityEffectTarget(uint32_t ability_id, wc3EffectType_t type, uint32_t index,
                                   edict_t *target, cstring_t attach_point, bool temporary) {
    return G_SpawnModelEffect(G_AbilityEffectArt(ability_id, type, index), NULL, target, attach_point, temporary);
}


edict_t *G_SpawnOwnedAbilityEffectAtPoint(edict_t *owner, uint32_t ability_id,
                                         wc3EffectType_t type, uint32_t index,
                                         vector2_t const *point) {
    edict_t *effect = G_SpawnAbilityEffectAtPoint(ability_id, type, index, point, false);
    if (effect) effect->owner = owner;
    return effect;
}

void G_DestroyOwnedEffects(edict_t *owner) {
    edict_t *owned[32];
    uint32_t count = 0;
    if (!owner) return;
    FILTER_EDICTS(effect, effect->owner == owner && (effect->s.flags & EF_NOT_SELECTABLE) &&
                  (effect->s.model || effect->s.sound)) {
        if (count < sizeof(owned) / sizeof(owned[0])) owned[count++] = effect;
    }
    FOR_LOOP(i, count) {
        owned[i]->s.sound = 0;
        if (owned[i]->s.model) G_DestroyEffect(owned[i]);
        else G_FreeEdict(owned[i]);
    }
}

void G_DestroyEffect(edict_t *effect) {
    if (!effect || !effect->inuse) return;
    effect->prethink = NULL;
    effect->s.sound = 0;
    effect->goalentity = NULL;
    effect->movetype = MOVETYPE_NONE;
    effect->wait = 0.0f;
    effect->think = G_EffectThink;
    unit_setmove(effect, &wc3_effect_death);
    if (!effect->animation) G_FreeEdict(effect);
}
