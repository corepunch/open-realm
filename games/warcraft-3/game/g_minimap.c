#include "g_local.h"

#define WC3_DEFAULT_MINIMAP_INDICATOR "UI\\Minimap\\Minimap-Ping.mdl"
#define WC3_DEFAULT_ALERT_PING_DURATION 1.0f

/* Serialize transient minimap presentation for one connected client. */
void G_SendMinimapPing(LPGAMECLIENT client, LPCVECTOR2 position, FLOAT duration, COLOR32 color, DWORD flags) {
    LPEDICT clent;
    LPCSTR model;

    if (!client || !position || duration <= 0.0f || !client->connected || !gi.MinimapPing) return;
    clent = G_GetPlayerEntityByNumber(client->ps.number);
    if (!clent || !clent->client) return;

    model = Theme_PlayerString(client, "MinimapIndicator", WC3_DEFAULT_MINIMAP_INDICATOR);
    gi.configstring(CS_MINIMAP, model && model[0] ? model : WC3_DEFAULT_MINIMAP_INDICATOR);
    gi.MinimapPing(clent, position, duration, color.a ? color : COLOR32_WHITE, flags);
}

/* Derive owner alerts from the completed entity so no alert state enters save/load. */
void G_SendOwnerMinimapAlert(LPEDICT ent) {
    LPGAMECLIENT client;

    if (!ent || ent->s.player >= MAX_PLAYERS) return;
    client = G_GetPlayerClientByNumber(ent->s.player);
    if (!client || client->ps.number != ent->s.player) return;
    G_SendMinimapPing(client, &ent->s.origin2, WC3_DEFAULT_ALERT_PING_DURATION,
                      COLOR32_WHITE, MINIMAP_PING_REMEMBER);
}
