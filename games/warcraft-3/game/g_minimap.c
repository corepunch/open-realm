#include "g_local.h"
#include "games/warcraft-3/common/minimap.h"

#define WC3_DEFAULT_MINIMAP_INDICATOR "UI\\Minimap\\Minimap-Ping.mdl"
#define WC3_DEFAULT_ALERT_PING_DURATION 1.0f

/* Serialize transient minimap presentation for one connected client. */
void G_SendMinimapPing(gameClient_t *client, vec2_t const *position, float duration, color32_t color, uint32_t flags) {
    edict_t *clent;
    cstring_t model;

    if (!client || !position || duration <= 0.0f || !client->connected || !gi.MinimapPing) return;
    clent = G_GetPlayerEntityByNumber(client->ps.number);
    if (!clent || !clent->client) return;

    model = Theme_PlayerString(client, "MinimapIndicator", WC3_DEFAULT_MINIMAP_INDICATOR);
    gi.configstring(CS_MINIMAP, model && model[0] ? model : WC3_DEFAULT_MINIMAP_INDICATOR);
    gi.MinimapPing(clent, position, duration, color.a ? color : COLOR32_WHITE, flags);
}

/* Derive owner alerts from the completed entity so no alert state enters save/load. */
void G_SendOwnerMinimapAlert(edict_t *ent) {
    gameClient_t *client;

    if (!ent || ent->s.player >= MAX_PLAYERS) return;
    client = G_GetPlayerClientByNumber(ent->s.player);
    if (!client || client->ps.number != ent->s.player) return;
    G_SendMinimapPing(client, &ent->s.origin2, WC3_DEFAULT_ALERT_PING_DURATION,
                      COLOR32_WHITE, MINIMAP_PING_REMEMBER);
}

/* Cursor color follows the skin's team-color image, not a hardcoded RGB palette. */
uint32_t G_SignalColorImage(gameClient_t *client) {
    cstring_t prefix = Theme_PlayerString(client, "TeamColor", NULL);
    cstring_t count_text = Theme_PlayerString(client, "TeamColors", NULL);
    uint32_t count = count_text ? strtoul(count_text, NULL, 10) : 0;
    uint32_t color = client->ps.color;
    PATHSTR path;
    if (!prefix || !*prefix || !count) {
        fprintf(stderr, "WC3 signal: missing or invalid TeamColor/TeamColors skin fields\n");
        return 0;
    }
    color %= count;
    if (client->ps.stats[WC3_PLAYERSTAT_MINIMAP_ALLY_COLOR] == WC3_MINIMAP_ALLY_COLOR_WORLD) {
        cstring_t index = Stb_IniCacheFind(&game.config.misc, "TeamColorFilter", "ColorIndexPlayer");
        if (!index || !*index) {
            fprintf(stderr, "WC3 signal: missing TeamColorFilter.ColorIndexPlayer\n");
            return 0;
        }
        color = strtoul(index, NULL, 10);
        if (color >= count) {
            fprintf(stderr, "WC3 signal: ColorIndexPlayer %u exceeds TeamColors %u\n", color, count);
            return 0;
        }
    }
    snprintf(path, sizeof(path), "%s%02u.blp", prefix, color);
    return gi.ImageIndex(path);
}

/* Signal is an overlay, not a replacement target callback. Right click/Esc
 * pops it; a left click emits an ally ping and restores the underlying command. */
bool G_SignalCommand(edict_t *ent, uint32_t argc, cstring_t argv[]) {
    gameClient_t *client = ent ? ent->client : NULL;
    vec2_t position;
    if (!client || !argc) return false;
    if (!strcmp(argv[0], "signal")) {
        client->cursor_signal = true;
        return true;
    }
    if (!client->cursor_signal) return false;
    if (!strcmp(argv[0], "cancel") || !strcmp(argv[0], "smart") || !strcmp(argv[0], "smartpoint")) {
        client->cursor_signal = false;
        return true;
    }
    if (!strcmp(argv[0], "point") && argc >= 3) {
        position = (vec2_t){atof(argv[1]), atof(argv[2])};
        if (!isfinite(position.x) || !isfinite(position.y)) return true;
    } else if (!strcmp(argv[0], "select") && argc >= 2) {
        char *end;
        unsigned long number = strtoul(argv[1], &end, 10);
        if (*end || !number || number >= globals.num_edicts || !g_edicts[number].inuse) return true;
        position = g_edicts[number].s.origin2;
    } else return false;
    client->cursor_signal = false;
    FOR_LOOP(i, game.max_clients) {
        gameClient_t *recipient = game.clients + i;
        if (recipient == client || G_GetPlayerAlliance(&client->ps, &recipient->ps, ALLIANCE_PASSIVE))
            G_SendMinimapPing(recipient, &position, WC3_DEFAULT_ALERT_PING_DURATION,
                             COLOR32_WHITE, MINIMAP_PING_REMEMBER);
    }
    return true;
}
