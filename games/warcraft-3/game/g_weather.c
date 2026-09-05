#include "g_local.h"
#include "games/warcraft-3/common/weather.h"

static BOOL G_WeatherValid(LPCGWEATHER effect) {
    return effect && effect >= level.weather_effects &&
           effect < level.weather_effects + MAX_WEATHER_EFFECTS && effect->inuse;
}

static void G_WeatherSend(LPEDICT ent, LPCGWEATHER effect, wc3WeatherCommandType_t type) {
    wc3WeatherCommand_t command;

    if (!ent || !ent->client || !ent->client->connected || !gi.GameCommand) return;
    memset(&command, 0, sizeof(command));
    command.type = type;
    if (effect) {
        command.handle = effect->handle_id;
        command.effect_id = effect->effect_id;
        command.bounds = effect->bounds;
        command.enabled = effect->enabled;
    }
    gi.GameCommand(ent, WC3_WEATHER_GAME_COMMAND, &command, sizeof(command));
}

static void G_WeatherBroadcast(LPCGWEATHER effect, wc3WeatherCommandType_t type) {
    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT client = game.clients + i;
        LPEDICT ent;

        if (!client->connected) continue;
        ent = G_GetPlayerEntityByNumber(client->ps.number);
        if (ent) G_WeatherSend(ent, effect, type);
    }
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
    G_WeatherBroadcast(effect, WC3_WEATHER_CMD_ADD);
    return effect;
}

void G_WeatherEnable(LPGWEATHER effect, BOOL enabled) {
    if (!G_WeatherValid(effect)) return;
    enabled = !!enabled;
    if (effect->enabled == enabled) return;
    effect->enabled = enabled;
    G_WeatherBroadcast(effect, WC3_WEATHER_CMD_ENABLE);
}

void G_WeatherRemove(LPGWEATHER effect) {
    if (!G_WeatherValid(effect)) return;
    G_WeatherBroadcast(effect, WC3_WEATHER_CMD_REMOVE);
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

void G_WeatherSyncClient(LPEDICT ent) {
    wc3WeatherCommand_t clear = { .type = WC3_WEATHER_CMD_CLEAR };

    if (!ent || !ent->client || !ent->client->connected || !gi.GameCommand) return;
    gi.GameCommand(ent, WC3_WEATHER_GAME_COMMAND, &clear, sizeof(clear));
    FOR_LOOP(i, MAX_WEATHER_EFFECTS) {
        LPCGWEATHER effect = level.weather_effects + i;
        if (effect->inuse) G_WeatherSend(ent, effect, WC3_WEATHER_CMD_ADD);
    }
}
