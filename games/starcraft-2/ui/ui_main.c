/*
 * ui_main.c — SC2 UI library entry point and lifecycle management.
 *
 * Implements UI_GetAPI() for StarCraft II, providing the uiExport_t
 * function table that the client uses to interact with the UI library.
 * Parses .SC2Layout XML files and builds frame arrays for client rendering.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "sc2_layout.h"
#include "client/ui.h"

/* Global import table filled by UI_GetAPI */
uiImport_t uiimport;

/* SC2 UI state */
typedef struct {
    BOOL initialized;
    DWORD time;
    PATHSTR loading_map;
    PATHSTR loading_status;
    FLOAT loading_progress;
} sc2UIState_t;

static sc2UIState_t sc2_ui_state;

/* ---------- Command handlers ---------- */

static void SC2_UI_Menu_f(void) {
    fprintf(stderr, "SC2_UI: menu command received\n");
    /* TODO: navigate to SC2 glue screens */
}

/* ---------- Initialization ---------- */

static void SC2_UI_InitLocal(void) {
    if (sc2_ui_state.initialized) return;

    fprintf(stderr, "SC2_UI: initializing SC2 layout system\n");

    /* Register SC2 UI commands */
    uiimport.Cmd_AddCommand("sc2_menu", SC2_UI_Menu_f);

    /* Parse SC2 layout files */
    if (!SC2_LayoutBuildGameUI()) {
        fprintf(stderr, "SC2_UI: failed to build game UI from layout files\n");
    }

    sc2_ui_state.initialized = true;
    fprintf(stderr, "SC2_UI: initialized successfully\n");
}

static void SC2_UI_ShutdownLocal(void) {
    SC2_LayoutShutdown();
    memset(&sc2_ui_state, 0, sizeof(sc2_ui_state));
}

static void SC2_UI_RefreshLocal(DWORD msec) {
    sc2_ui_state.time += msec;
    /* TODO: update frame states, animations, data bindings */
}

/* ---------- Input handling ---------- */

static void SC2_UI_KeyEvent(int key, BOOL down, DWORD time) {
    /* TODO: SC2 glue screen key handling */
    (void)key; (void)down; (void)time;
}

static void SC2_UI_TextInput(LPCSTR text) {
    /* TODO: SC2 edit box text input */
    (void)text;
}

static void SC2_UI_MouseEvent(int x, int y, int button, BOOL down) {
    /* TODO: SC2 frame hit testing and event dispatch */
    (void)x; (void)y; (void)button; (void)down;
}

/* ---------- Frame data access ---------- */

static DWORD SC2_UI_GetNumFrames(void) {
    DWORD count = 0;
    SC2_LayoutGetFrames(&count);
    return count;
}

static void SC2_UI_SetLoadingState(LPCSTR map, LPCSTR status, FLOAT progress) {
    if (map) { strncpy(sc2_ui_state.loading_map, map, MAX_PATHLEN - 1); sc2_ui_state.loading_map[MAX_PATHLEN - 1] = '\0'; }
    if (status) { strncpy(sc2_ui_state.loading_status, status, MAX_PATHLEN - 1); sc2_ui_state.loading_status[MAX_PATHLEN - 1] = '\0'; }
    sc2_ui_state.loading_progress = progress;
}

static void SC2_UI_UpdateUnitUI(DWORD num_units, uiUnitData_t *units) {
    /* TODO: update command panel, info panel with unit data */
    (void)num_units; (void)units;
}

static void SC2_UI_UpdateLobbySetup(lobbyState_t const *state) {
    /* TODO: SC2 lobby UI */
    (void)state;
}

static void SC2_UI_SetLayoutLayer(DWORD layer, HANDLE data) {
    (void)layer; (void)data;
}

static void SC2_UI_ClearLayoutLayer(DWORD layer) {
    (void)layer;
}

static BOOL SC2_UI_HitTestLayout(int x, int y) {
    (void)x; (void)y;
    return false;
}

/* ---------- Entry point ---------- */

uiExport_t UI_GetAPI(uiImport_t import) {
    uiExport_t exp;
    memset(&exp, 0, sizeof(exp));

    uiimport = import;

    exp.Init = SC2_UI_InitLocal;
    exp.Shutdown = SC2_UI_ShutdownLocal;
    exp.Refresh = SC2_UI_RefreshLocal;
    exp.SetLoadingState = SC2_UI_SetLoadingState;
    exp.KeyEvent = SC2_UI_KeyEvent;
    exp.TextInput = SC2_UI_TextInput;
    exp.MouseEvent = SC2_UI_MouseEvent;
    exp.UpdateUnitUI = SC2_UI_UpdateUnitUI;
    exp.UpdateLobbySetup = SC2_UI_UpdateLobbySetup;
    exp.SetLayoutLayer = SC2_UI_SetLayoutLayer;
    exp.ClearLayoutLayer = SC2_UI_ClearLayoutLayer;
    exp.HitTestLayout = SC2_UI_HitTestLayout;

    /* Frame data — client iterates by stride, game-specific struct extends uiBaseFrame_t */
    exp.frame_size = sizeof(uiBaseFrame_t);
    exp.frames = SC2_LayoutGetFrames(NULL);
    exp.GetNumFrames = SC2_UI_GetNumFrames;

    return exp;
}
