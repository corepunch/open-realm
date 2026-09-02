/* hud_allies.c — Server-authored Alliance dialog with client-owned dismissal. */

#include "hud_local.h"
#include "../generated/alliance_dialog.h"

static AllianceDialog_t alliance;
static BOOL alliance_loaded;

static BOOL AlliesEnsureLoaded(void) {
    if (alliance_loaded) return alliance.AllianceDialog != NULL;
    alliance_loaded = true;
    if (!AllianceDialog_Load(&alliance)) return false;
    UI_CenterFrame(alliance.AllianceDialog);
    UI_SetOnClick(alliance.AllianceAcceptButton, UI_WINDOW_CLOSE_NOTIFY_ACTION);
    UI_SetOnClick(alliance.AllianceCancelButton, UI_WINDOW_CLOSE_NOTIFY_ACTION);
    return true;
}

void UI_ShowAllies(LPEDICT ent) {
    if (!ent || !ent->client || !ent->client->connected || !AlliesEnsureLoaded()) return;
    UI_WriteWindow(ent, alliance.AllianceDialog, &MAKE(uiWindowDef_t,
        .id = BZ_WC3_WINDOW_ALLIES, .class_id = BZ_WC3_WINDOW_ALLIES,
        .flags = UI_WINDOW_MOVABLE | UI_WINDOW_MODAL | UI_WINDOW_UNIQUE));
    G_SetClientModal(ent, WC3_MODAL_ALLIES, true);
}
