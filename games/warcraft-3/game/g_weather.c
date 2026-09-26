#include "g_local.h"
#include "games/warcraft-3/common/weather.h"

static bool G_WeatherValid(gweather_t const *effect) {
    return effect && effect >= level.weather_effects &&
           effect < level.weather_effects + MAX_WEATHER_EFFECTS && effect->inuse;
}

gweather_t *G_WeatherAdd(box2_t const *bounds, uint32_t effect_id, bool enabled) {
    gweather_t *effect = NULL;

    if (!bounds || !effect_id) return NULL;
    FOR_LOOP(i, MAX_WEATHER_EFFECTS) {
        if (!level.weather_effects[i].inuse) {
            effect = level.weather_effects + i;
            break;
        }
    }
    if (!effect) return NULL;
    memset(effect, 0, sizeof(*effect));
    effect->inuse = true;
    effect->enabled = enabled;
    effect->effect_id = effect_id;
    effect->bounds = *bounds;
    if (++level.next_weather_id == 0) level.next_weather_id = 1;
    effect->handle_id = level.next_weather_id;
    return effect;
}

void G_WeatherEnable(gweather_t *effect, bool enabled) {
    if (!G_WeatherValid(effect)) return;
    enabled = !!enabled;
    if (effect->enabled == enabled) return;
    effect->enabled = enabled;
}

void G_WeatherRemove(gweather_t *effect) {
    if (!G_WeatherValid(effect)) return;
    memset(effect, 0, sizeof(*effect));
}

void G_WeatherInitMap(void) {
    mapInfo_t const *mapinfo = level.mapinfo;

    if (!mapinfo) return;
    if (mapinfo->weatherID) {
        box2_t bounds = CM_GetWorldBounds();
        G_WeatherAdd(&bounds, mapinfo->weatherID, true);
    }
    FOR_LOOP(i, mapinfo->num_weatherRegions) {
        mapWeatherRegion_t const *region = mapinfo->weatherRegions + i;
        if (region->weatherID) G_WeatherAdd(&region->bounds, region->weatherID, true);
    }
}

/* Filter vertex colours by unit visibility while allowing unresolved presentation models. */
static bool G_ClientReceivesVertexColor(edict_t *client_ent, edict_t const *unit) {
    uint32_t player;
    /* Vertex colour is authoritative unit state; publish it before model resolution too,
     * because the client may receive the tint before the unit's presentation model. */
    if (!unit->inuse || !unit->vertex_color_set) return false;
    if (!client_ent || !client_ent->client) return true;
    player = client_ent->client->ps.number;
    return unit->s.player == player || G_FowPlayerCanSeeEntity(player, unit);
}

/* Serialize authoritative weather and vertex-colour state so dropped frames converge without widening entityState_t. */
uint32_t G_WriteClientDatagram(edict_t *ent, uint8_t *data, uint32_t size) {
    uint32_t weather_count = 0, lightning_count = 0, tint_count = 0, terrain_mask_size = 0;
    uint32_t const tint_wire_size = sizeof(uint16_t) + sizeof(color32_t);
    uint32_t const lightning_header_size = sizeof(uint16_t);
    uint32_t base, lightning_need, mask_min, tint_need;
    uint8_t *out = data;
    uint16_t wire_count;
    bool emit_lightning, emit_tints, emit_terrain_mask;
    uint32_t now = G_Time();

    if (!data || size < sizeof(wire_count)) return 0;
    FOR_LOOP(i, MAX_WEATHER_EFFECTS) if (level.weather_effects[i].inuse) weather_count++;
    FOR_LOOP(i, MAX_LIGHTNING_EFFECTS) {
        gLightning_t *effect = level.lightning_effects + i;
        if (!effect->inuse) continue;
        G_LightningUpdateAttached(effect);
        if (effect->state.end_time && now >= effect->state.end_time) {
            G_LightningRemove(effect);
            continue;
        }
        lightning_count++;
    }
    FOR_LOOP(i, globals.num_edicts) if (G_ClientReceivesVertexColor(ent, &g_edicts[i])) tint_count++;
    base = sizeof(wire_count) + weather_count * sizeof(wc3WeatherEffect_t);
    lightning_need = lightning_header_size + lightning_count * sizeof(lightningEffect_t);
    mask_min = 0;
    if (G_BlightDatagramPending(ent) && level.blight.width)
        mask_min = sizeof(terrainMaskChunk_t) + (level.blight.width + 7) / 8;
    tint_need = sizeof(uint16_t) + tint_count * tint_wire_size;
    /* Wire order is weather, lightning, tints, terrain mask; gate each section on the cumulative fit. */
    emit_lightning = lightning_count && base + lightning_need <= size;
    emit_terrain_mask = mask_min && base + (emit_lightning ? lightning_need : 0) + mask_min <= size;
    emit_tints = base + (emit_lightning ? lightning_need : 0) + (emit_terrain_mask ? mask_min : 0) + tint_need <= size;
    if (!emit_tints && tint_count) {
        fprintf(stderr, "G_WriteClientDatagram: tint snapshot needs %u bytes, buffer has %u; omitting tints\n",
            (unsigned)(base + (emit_lightning ? lightning_need : 0) + tint_need), (unsigned)size);
    }
    wire_count = (uint16_t)weather_count |
        (emit_lightning ? BZ_GAME_DATAGRAM_LIGHTNING : 0) |
        (emit_tints ? BZ_GAME_DATAGRAM_ENTITY_TINTS : 0) |
        (emit_terrain_mask ? BZ_GAME_DATAGRAM_TERRAIN_MASK : 0);
    memcpy(out, &wire_count, sizeof(wire_count));
    out += sizeof(wire_count);
    FOR_LOOP(i, MAX_WEATHER_EFFECTS) {
        gweather_t const *effect = level.weather_effects + i;
        wc3WeatherEffect_t state;
        if (!effect->inuse) continue;
        state = (wc3WeatherEffect_t){ .handle = effect->handle_id, .effect_id = effect->effect_id,
            .bounds = effect->bounds, .enabled = effect->enabled };
        if ((uint32_t)(out - data) + sizeof(state) > size) return 0;
        memcpy(out, &state, sizeof(state));
        out += sizeof(state);
    }
    if (emit_lightning) {
        uint16_t wire_lightning_count = (uint16_t)lightning_count;
        memcpy(out, &wire_lightning_count, sizeof(wire_lightning_count));
        out += sizeof(wire_lightning_count);
        FOR_LOOP(i, MAX_LIGHTNING_EFFECTS) {
            gLightning_t const *effect = level.lightning_effects + i;
            if (!effect->inuse) continue;
            memcpy(out, &effect->state, sizeof(effect->state));
            out += sizeof(effect->state);
        }
    }
    if (emit_tints) {
        uint16_t wire_tint_count = (uint16_t)tint_count;
        memcpy(out, &wire_tint_count, sizeof(wire_tint_count));
        out += sizeof(wire_tint_count);
        FOR_LOOP(i, globals.num_edicts) {
            edict_t *unit = &g_edicts[i];
            uint16_t number;
            if (!G_ClientReceivesVertexColor(ent, unit)) continue;
            number = (uint16_t)unit->s.number;
            memcpy(out, &number, sizeof(number)); out += sizeof(number);
            memcpy(out, &unit->vertex_color, sizeof(unit->vertex_color)); out += sizeof(unit->vertex_color);
        }
    }
    if (emit_terrain_mask) {
        terrain_mask_size = G_BlightWriteDatagram(ent, out, size - (uint32_t)(out - data));
        if (!terrain_mask_size) {
            emit_terrain_mask = false;
            wire_count &= ~BZ_GAME_DATAGRAM_TERRAIN_MASK;
            memcpy(data, &wire_count, sizeof(wire_count));
        } else {
            out += terrain_mask_size;
        }
    }
    return (uint32_t)(out - data);
}
