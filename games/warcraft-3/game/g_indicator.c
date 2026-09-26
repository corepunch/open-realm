#include "g_local.h"

/* Retail Smart-click acknowledgement uses the same selection-circle palette
 * as unit relationship presentation. UI\MiscData.txt stores these as A,R,G,B. */
color32_t G_SmartTargetIndicatorColor(uint32_t viewer, edict_t const *unit) {
    static cstring_t const keys[] = { "ColorFriend", "ColorNeutral", "ColorEnemy" };
    static color32_t const stock[] = {
        MAKE(color32_t, .r = 0, .g = 255, .b = 0, .a = 255),
        MAKE(color32_t, .r = 255, .g = 255, .b = 0, .a = 255),
        MAKE(color32_t, .r = 255, .g = 0, .b = 0, .a = 255),
    };
    selectionRelation_t relation;
    cstring_t value;
    unsigned a, r, g, b;

    relation = G_SelectionRelation(viewer, unit);
    if ((uint32_t)relation >= sizeof(keys) / sizeof(keys[0])) relation = SELECT_RELATION_ENEMY;
    value = Stb_IniCacheFind(&game.config.misc, "SelectionCircle", keys[relation]);
    if (value && sscanf(value, "%u,%u,%u,%u", &a, &r, &g, &b) == 4 &&
        a <= 255 && r <= 255 && g <= 255 && b <= 255)
        return MAKE(color32_t, r, g, b, a);

    /* HACK: stock WC3 1.29 values cover minimal/test data that lacks the
     * authoritative UI\MiscData.txt section; keep the missing data visible. */
    fprintf(stderr, "WC3: invalid or missing SelectionCircle.%s; using stock color\n", keys[relation]);
    return stock[relation];
}

/* Serialize one indicator to a target client's temporary-entity stream. */
static void G_SendWidgetIndicatorClient(gameClient_t *client, edict_t const *widget, color32_t color) {
    edict_t *clent;
    uint32_t packed;

    if (!client || !widget || !color.a || !client->connected || !gi.Write || !gi.unicast) return;
    clent = G_GetPlayerEntityByNumber(client->ps.number);
    if (!clent || !clent->client) return;

    packed = (uint32_t)color.r | ((uint32_t)color.g << 8) |
             ((uint32_t)color.b << 16) | ((uint32_t)color.a << 24);
    gi.Write(PF_BYTE, &(int32_t){ svc_temp_entity });
    gi.Write(PF_BYTE, &(int32_t){ TE_ENTITY_INDICATOR });
    gi.Write(PF_LONG, &(int32_t){ (int32_t)widget->s.number });
    gi.Write(PF_LONG, &(int32_t){ (int32_t)packed });
    gi.unicast(clent);
}

/* AddIndicator is local presentation, not simulation state. Keep its lifetime
 * on the client and send only the source widget/color for each recipient. */
void G_SendWidgetIndicator(edict_t *widget, color32_t color, player_t *local_player) {
    if (!widget || !color.a) return;
    if (local_player) {
        G_SendWidgetIndicatorClient(PLAYER_CLIENT(local_player), widget, color);
        return;
    }
    FOR_LOOP(i, game.max_clients)
        G_SendWidgetIndicatorClient(&game.clients[i], widget, color);
}
