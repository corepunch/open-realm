#include "client/ui.h"

uiImport_t uiimport;

static void M_Init(void) {}
static void M_Shutdown(void) {}
static void M_Refresh(DWORD time) { (void)time; }
static void M_KeyEvent(int key, BOOL down, DWORD time) { (void)key; (void)down; (void)time; }
static void M_TextInput(LPCSTR text) { (void)text; }
static BOOL M_MouseEvent(uiMouseEvent_t event, int x, int y, int32_t param) { (void)event; (void)x; (void)y; (void)param; return false; }
static void UI_UpdateUnitUILocal(DWORD num_units, uiUnitData_t *units) { (void)num_units; (void)units; }
static void UI_UpdateLobbySetupLocal(lobbyState_t const *state) { (void)state; }
static LPCSTR UI_ResolveImagePathLocal(LPCSTR key) { return key; }

uiExport_t UI_GetAPI(uiImport_t import) {
    uiimport = import;
    return (uiExport_t) {
        .Init             = M_Init,
        .Shutdown         = M_Shutdown,
        .Refresh          = M_Refresh,
        .KeyEvent         = M_KeyEvent,
        .TextInput        = M_TextInput,
        .MouseEvent       = M_MouseEvent,
        .UpdateUnitUI     = UI_UpdateUnitUILocal,
        .UpdateLobbySetup = UI_UpdateLobbySetupLocal,
        .ResolveImagePath  = UI_ResolveImagePathLocal,
    };
}
