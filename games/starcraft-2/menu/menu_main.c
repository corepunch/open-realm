#include "client/menu.h"

menuImport_t menuimport;

static void M_Init(void) {}
static void M_Shutdown(void) {}
static void M_Refresh(DWORD time) { (void)time; }
static void M_KeyEvent(int key, BOOL down, DWORD time) { (void)key; (void)down; (void)time; }
static void M_TextInput(LPCSTR text) { (void)text; }
static BOOL M_MouseEvent(menuMouseEvent_t event, int x, int y, int32_t param) { (void)event; (void)x; (void)y; (void)param; return false; }
static void M_UpdateUnitUI(DWORD num_units, menuUnitData_t *units) { (void)num_units; (void)units; }
static void M_UpdateLobbySetup(lobbyState_t const *state) { (void)state; }
static void M_ShowTutorial(DWORD id) { (void)id; }
static LPCSTR M_ResolveImagePath(LPCSTR key) { return key; }

menuExport_t M_GetAPI(menuImport_t import) {
    menuimport = import;
    return (menuExport_t) {
        .Init             = M_Init,
        .Shutdown         = M_Shutdown,
        .Refresh          = M_Refresh,
        .KeyEvent         = M_KeyEvent,
        .TextInput        = M_TextInput,
        .MouseEvent       = M_MouseEvent,
        .UpdateUnitUI     = M_UpdateUnitUI,
        .UpdateLobbySetup = M_UpdateLobbySetup,
        .ShowTutorial    = M_ShowTutorial,
        .ResolveImagePath  = M_ResolveImagePath,
    };
}
