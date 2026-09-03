/* hud_allies.c — Server-authored Alliance dialog with client-owned dismissal. */

#include "hud_local.h"

void UI_LoadHudAllies(void) {
    if (hud.allies.AllianceDialog) return;
    if (!AllianceDialog_Load(&hud.allies)) return;
    UI_CenterFrame(hud.allies.AllianceDialog);
    UI_SetOnClick(hud.allies.AllianceAcceptButton, UI_WINDOW_CLOSE_NOTIFY_ACTION);
    UI_SetOnClick(hud.allies.AllianceCancelButton, UI_WINDOW_CLOSE_NOTIFY_ACTION);
}

void UI_ShowAllies(LPEDICT ent) {
    if (!ent || !ent->client || !ent->client->connected) return;
    /* Decorated dialog art must enter the shared FDF cache under the
     * recipient's race skin, matching subsequent window serialization. */
    UI_SetCurrentClient(ent->client);
    if (!hud.allies.AllianceDialog) { UI_SetCurrentClient(NULL); return; }
    UI_WriteWindow(ent, hud.allies.AllianceDialog, &MAKE(uiWindowDef_t,
        .id = BZ_WC3_WINDOW_ALLIES, .class_id = BZ_WC3_WINDOW_ALLIES,
        .flags = UI_WINDOW_MOVABLE | UI_WINDOW_MODAL | UI_WINDOW_UNIQUE));
    UI_SetCurrentClient(NULL);
}
