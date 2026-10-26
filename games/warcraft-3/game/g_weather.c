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

/* Build the presentation tint for this recipient. Authoritative vertex colour
 * remains unit state. Hero dissipation fades for every viewer; Shadow Meld and
 * invisibility additionally multiply only owner/shared-vision presentation so
 * hostile detector viewers retain the normal authored tint. */
static bool G_ClientVertexColor(edict_t *client_ent, edict_t const *unit, color32_t *out) {
    uint32_t player;
    color32_t color;
    float presentation_alpha = 1.0f;

    if (!unit || !unit->inuse || !out) return false;
    color = G_UnitHasFlag(unit, UNIT_FLAG_VERTEX_COLOR_SET) ? unit->vertex_color : COLOR32_WHITE;
    presentation_alpha = G_UnitDissipatePresentationAlpha(unit);

    /* Most entities have no authored tint or presentation fade. An ordinary
     * entity cannot acquire viewer-relative alpha without Shadow Meld,
     * permanent/ghost invisibility, or an invisibility-backed RF_HIDDEN state.
     * Reject those before the potentially expensive FoW/detector queries.
     * This is only a candidate check: the original visibility and fade logic
     * below remains authoritative for all possible tinted entities. */
    if (!G_UnitHasFlag(unit, UNIT_FLAG_VERTEX_COLOR_SET) && presentation_alpha >= 1.0f) {
        if (!client_ent || !client_ent->client) return false;
        player = client_ent->client->ps.number;
        if (unit->s.player != player && !G_FowPlayersShareVision(player, unit->s.player)) return false;
        if (!G_GetShadowMeld(unit) && !(unit->s.renderfx & RF_HIDDEN) &&
            !(unit->runtime.flags & (UNIT_BALANCE_PERMANENT_INVISIBLE | UNIT_BALANCE_GHOST_INVISIBLE)))
            return false;
    }

    if (client_ent && client_ent->client) {
        player = client_ent->client->ps.number;
        if (unit->s.player != player && !G_FowPlayerCanSeeEntity(player, unit)) return false;
        if (unit->s.player == player || G_FowPlayersShareVision(player, unit->s.player)) {
            float ghost_alpha = S_ShadowMeldPresentationAlpha(unit);
            if (S_UnitHasInvisibilityState(unit)) ghost_alpha = MIN(ghost_alpha, 0.35f);
            presentation_alpha *= ghost_alpha;
        }
    }

    if (!G_UnitHasFlag(unit, UNIT_FLAG_VERTEX_COLOR_SET) && presentation_alpha >= 1.0f) return false;
    color.a = (uint8_t)MIN(255, MAX(0, (int)((float)color.a * presentation_alpha + 0.5f)));
    *out = color;
    return true;
}

/* Serialize authoritative weather and vertex-colour state so dropped frames converge without widening entityState_t. */
uint32_t G_WriteClientDatagram(edict_t *ent, uint8_t *data, uint32_t size) {
    uint32_t weather_count = 0, lightning_count = 0, tint_count = 0, terrain_mask_size = 0;
    uint32_t const tint_wire_size = sizeof(uint16_t) + sizeof(color32_t);
    uint32_t const lightning_header_size = sizeof(uint16_t);
    uint32_t base, lightning_need, mask_min, tint_need, tint_offset, tint_capacity;
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
    base = sizeof(wire_count) + weather_count * sizeof(wc3WeatherEffect_t);
    lightning_need = lightning_header_size + lightning_count * sizeof(lightningEffect_t);
    mask_min = 0;
    if (G_BlightDatagramPending(ent) && level.blight.width)
        mask_min = sizeof(terrainMaskChunk_t) + (level.blight.width + 7) / 8;
    /* Wire order is weather, lightning, tints, terrain mask. The lightning
     * and terrain decisions do not depend on the tint count. */
    emit_lightning = lightning_count && base + lightning_need <= size;
    emit_terrain_mask = mask_min && base + (emit_lightning ? lightning_need : 0) + mask_min <= size;

    /* Stage tint entries at their final wire offset in the caller's buffer.
     * Reserving the terrain minimum preserves the old all-or-nothing fit rule
     * without allocating a 16K-entity scratch array or running FoW twice.
     * If they do not all fit, omit the tint section; the terrain writer may
     * overwrite these staged bytes as it did before. */
    tint_offset = base + (emit_lightning ? lightning_need : 0);
    tint_capacity = 0;
    if (tint_offset <= size &&
        sizeof(uint16_t) + (emit_terrain_mask ? mask_min : 0) <= size - tint_offset) {
        tint_capacity = (size - tint_offset - sizeof(uint16_t) -
                         (emit_terrain_mask ? mask_min : 0)) / tint_wire_size;
    }
    FOR_LOOP(i, globals.num_edicts) {
        edict_t const *unit = &g_edicts[i];
        color32_t color;
        if (!G_ClientVertexColor(ent, unit, &color)) continue;
        if (tint_count < tint_capacity) {
            uint16_t number = (uint16_t)unit->s.number;
            uint32_t offset = tint_offset + sizeof(uint16_t) + tint_count * tint_wire_size;
            memcpy(data + offset, &number, sizeof(number));
            memcpy(data + offset + sizeof(number), &color, sizeof(color));
        }
        tint_count++;
    }
    tint_need = sizeof(uint16_t) + tint_count * tint_wire_size;
    emit_tints = tint_offset <= size &&
        tint_need + (emit_terrain_mask ? mask_min : 0) <= size - tint_offset;
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
        out += sizeof(wire_tint_count) + tint_count * tint_wire_size;
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
