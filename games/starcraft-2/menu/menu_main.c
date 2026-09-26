#include "client/menu.h"

menuImport_t mi;

static void M_Init(void) {}
static void M_Shutdown(void) {}
static void M_Refresh(uint32_t time) { (void)time; }
static void M_KeyEvent(int key, bool down, uint32_t time) { (void)key; (void)down; (void)time; }
static void M_TextInput(cstring_t text) { (void)text; }
static bool M_MouseEvent(menuMouseEvent_t event, int x, int y, int32_t param) { (void)event; (void)x; (void)y; (void)param; return false; }
static void M_UpdateLobbySetup(lobbyState_t const *state) { (void)state; }

menuExport_t M_GetAPI(menuImport_t import) {
    mi = import;
    return (menuExport_t) {
        .Init             = M_Init,
        .Shutdown         = M_Shutdown,
        .Refresh          = M_Refresh,
        .KeyEvent         = M_KeyEvent,
        .TextInput        = M_TextInput,
        .MouseEvent       = M_MouseEvent,
        .UpdateLobbySetup = M_UpdateLobbySetup,
    };
}
