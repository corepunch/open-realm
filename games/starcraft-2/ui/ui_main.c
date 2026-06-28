#include "client/ui.h"

uiImport_t uiimport;

static void UI_InitLocal(void) {}
static void UI_ShutdownLocal(void) {}
static void UI_RefreshLocal(DWORD time) { (void)time; }
static void UI_KeyEventLocal(int key, BOOL down, DWORD time) { (void)key; (void)down; (void)time; }
static void UI_TextInputLocal(LPCSTR text) { (void)text; }
static BOOL UI_MouseEventLocal(uiMouseEvent_t event, int x, int y, int32_t param) { (void)event; (void)x; (void)y; (void)param; return false; }
static void UI_UpdateUnitUILocal(DWORD num_units, uiUnitData_t *units) { (void)num_units; (void)units; }
static void UI_UpdateLobbySetupLocal(lobbyState_t const *state) { (void)state; }

uiExport_t UI_GetAPI(uiImport_t import) {
    uiimport = import;
    return (uiExport_t) {
        .Init             = UI_InitLocal,
        .Shutdown         = UI_ShutdownLocal,
        .Refresh          = UI_RefreshLocal,
        .KeyEvent         = UI_KeyEventLocal,
        .TextInput        = UI_TextInputLocal,
        .MouseEvent       = UI_MouseEventLocal,
        .UpdateUnitUI     = UI_UpdateUnitUILocal,
        .UpdateLobbySetup = UI_UpdateLobbySetupLocal,
    };
}
