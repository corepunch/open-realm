#include "g_local.h"
#include "games/warcraft-3/common/weather.h"

static BOOL G_WeatherValid(LPCGWEATHER effect) {
    return effect && effect >= level.weather_effects &&
           effect < level.weather_effects + MAX_WEATHER_EFFECTS && effect->inuse;
}

LPGWEATHER G_WeatherAdd(LPCBOX2 bounds, DWORD effect_id, BOOL enabled) {
    LPGWEATHER effect = NULL;

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

void G_WeatherEnable(LPGWEATHER effect, BOOL enabled) {
    if (!G_WeatherValid(effect)) return;
    enabled = !!enabled;
    if (effect->enabled == enabled) return;
    effect->enabled = enabled;
}

void G_WeatherRemove(LPGWEATHER effect) {
    if (!G_WeatherValid(effect)) return;
    memset(effect, 0, sizeof(*effect));
}

void G_WeatherInitMap(void) {
    LPCMAPINFO mapinfo = level.mapinfo;

    if (!mapinfo) return;
    if (mapinfo->weatherID) {
        BOX2 bounds = CM_GetWorldBounds();
        G_WeatherAdd(&bounds, mapinfo->weatherID, true);
    }
    FOR_LOOP(i, mapinfo->num_weatherRegions) {
        mapWeatherRegion_t const *region = mapinfo->weatherRegions + i;
        if (region->weatherID) G_WeatherAdd(&region->bounds, region->weatherID, true);
    }
}

/* Serialize the authoritative weather set into the per-frame game datagram so
 * reconnects and dropped packets converge without renderer commands. */
DWORD G_WriteClientDatagram(LPEDICT ent, LPBYTE data, DWORD size) {
    DWORD count = 0;
    BYTE *out = data;
    USHORT wire_count;

    (void)ent;
    if (!data || size < sizeof(wire_count)) return 0;
    FOR_LOOP(i, MAX_WEATHER_EFFECTS) if (level.weather_effects[i].inuse) count++;
    wire_count = (USHORT)count;
    memcpy(out, &wire_count, sizeof(wire_count));
    out += sizeof(wire_count);
    FOR_LOOP(i, MAX_WEATHER_EFFECTS) {
        LPCGWEATHER effect = level.weather_effects + i;
        wc3WeatherEffect_t state;
        if (!effect->inuse) continue;
        state = (wc3WeatherEffect_t){ .handle = effect->handle_id, .effect_id = effect->effect_id,
            .bounds = effect->bounds, .enabled = effect->enabled };
        if ((DWORD)(out - data) + sizeof(state) > size) return 0;
        memcpy(out, &state, sizeof(state));
        out += sizeof(state);
    }
    return (DWORD)(out - data);
}
