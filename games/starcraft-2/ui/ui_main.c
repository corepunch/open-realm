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
    BOOL game_mode;
    DWORD time;
    PATHSTR loading_map;
    PATHSTR loading_status;
    FLOAT loading_progress;
} sc2UIState_t;

static sc2UIState_t sc2_ui_state;

/* Text storage for unit info labels (pointed to by uiBaseFrame_t::text) */
static char sc2_unit_name[128];
static char sc2_mineral_text[32];

static void SC2_UI_EnterGameMode(void) {
    sc2_ui_state.game_mode = true;
}

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

    /* Enter game mode immediately if a map is already loaded */
    LPCSTR map = uiimport.Cvar_String ? uiimport.Cvar_String("map", "") : "";
    if (map && *map) {
        SC2_UI_EnterGameMode();
    }

    fprintf(stderr, "SC2_UI: initialized successfully\n");
}

static void SC2_UI_ShutdownLocal(void) {
    SC2_LayoutShutdown();
    memset(&sc2_ui_state, 0, sizeof(sc2_ui_state));
}

static void SC2_UI_UpdateResourceDisplay(void) {
    if (!uiimport.GetPlayerState) return;
    LPCPLAYER ps = uiimport.GetPlayerState();
    if (!ps) return;

    uiBaseFrame_t *res = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_RESOURCE_PANEL);
    if (!res) return;

    DWORD frame_count = 0;
    uiBaseFrame_t *frames = SC2_LayoutGetFrames(&frame_count);
    if (!frames) return;

    /* Find the first FT_TEXT child of the resource panel and set mineral count */
    for (DWORD i = 0; i < frame_count; i++) {
        uiBaseFrame_t *f = &frames[i];
        if (f->parent_index == res->number && f->type == FT_TEXT) {
            snprintf(sc2_mineral_text, sizeof(sc2_mineral_text),
                     "%u", (unsigned)ps->stats[PLAYERSTATE_RESOURCE_GOLD]);
            f->text = sc2_mineral_text;
            break;
        }
    }
}

static void SC2_UI_RefreshLocal(DWORD msec) {
    sc2_ui_state.time += msec;
    if (sc2_ui_state.game_mode) {
        SC2_UI_UpdateResourceDisplay();
    }
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

static void SC2_UI_MouseEvent(uiMouseEvent_t event, int x, int y, int32_t param) {
    /* TODO: SC2 frame hit testing and event dispatch */
    (void)event; (void)x; (void)y; (void)param;
}

/* ---------- Frame data access ---------- */

static DWORD SC2_UI_GetNumFrames(void) {
    DWORD count = 0;
    SC2_LayoutGetFrames(&count);
    return count;
}

static void SC2_UI_DrawFrame(void) {
    if (!sc2_ui_state.game_mode) return;

    LPRENDERER renderer = uiimport.GetRenderer ? uiimport.GetRenderer() : NULL;
    if (!renderer) return;

    /* Draw minimap in its panel rect */
    uiBaseFrame_t *minimap = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_MINIMAP_PANEL);
    if (minimap && renderer->DrawMinimap) {
        renderer->DrawMinimap(&minimap->screen_rect);
    }
}

static void SC2_UI_SetLoadingState(LPCSTR map, LPCSTR status, FLOAT progress) {
    if (map) { strncpy(sc2_ui_state.loading_map, map, MAX_PATHLEN - 1); sc2_ui_state.loading_map[MAX_PATHLEN - 1] = '\0'; }
    if (status) { strncpy(sc2_ui_state.loading_status, status, MAX_PATHLEN - 1); sc2_ui_state.loading_status[MAX_PATHLEN - 1] = '\0'; }
    sc2_ui_state.loading_progress = progress;
}

static void SC2_UI_UpdateUnitUI(DWORD num_units, uiUnitData_t *units) {
    uiBaseFrame_t *info   = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_INFO_PANEL);
    uiBaseFrame_t *cmd    = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_COMMAND_PANEL);
    uiBaseFrame_t *res    = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_RESOURCE_PANEL);
    DWORD frame_count = 0;
    uiBaseFrame_t *frames = SC2_LayoutGetFrames(&frame_count);

    /* Clear unit name and command buttons when nothing is selected */
    if (!num_units || !units) {
        sc2_unit_name[0] = '\0';
        if (info) {
            /* Hide info panel when nothing selected */
            UI_BaseSetHidden(info, true);
        }
        /* Clear all command buttons under the command panel */
        if (cmd && frames) {
            for (DWORD i = 0; i < frame_count; i++) {
                uiBaseFrame_t *f = &frames[i];
                if (f->parent_index == cmd->number && f->type == FT_BUTTON) {
                    f->image = 0;
                    f->text = NULL;
                    UI_BaseSetHidden(f, true);
                }
            }
        }
        (void)res;
        return;
    }

    uiUnitData_t const *unit = &units[0];

    /* Info panel: unit name */
    if (info) {
        UI_BaseSetHidden(info, false);
        /* Find first FT_TEXT child of the info panel and set unit name */
        if (frames) {
            for (DWORD i = 0; i < frame_count; i++) {
                uiBaseFrame_t *f = &frames[i];
                if (f->parent_index == info->number && f->type == FT_TEXT) {
                    snprintf(sc2_unit_name, sizeof(sc2_unit_name), "%s", unit->name);
                    f->text = sc2_unit_name;
                    break;
                }
            }
        }
    }

    /* Command panel: populate buttons from unit->buttons[], hide extras */
    if (cmd && frames) {
        BYTE btn_idx = 0;
        for (DWORD i = 0; i < frame_count; i++) {
            uiBaseFrame_t *f = &frames[i];
            if (f->parent_index != cmd->number || f->type != FT_BUTTON) continue;
            if (btn_idx < unit->num_buttons) {
                uiCommandButton_t const *btn = &unit->buttons[btn_idx];
                f->image = btn->art[0] ? uiimport.ImageIndex(btn->art) : 0;
                f->text = NULL;
                UI_BaseSetHidden(f, false);
                btn_idx++;
            } else {
                f->image = 0;
                UI_BaseSetHidden(f, true);
            }
        }
    }
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
    DWORD count = 0;
    uiBaseFrame_t *frames = SC2_LayoutGetFrames(&count);
    if (!frames || count == 0) return false;

    /* Convert pixel coords to normalized FDF space */
    /* TODO: use proper scene rect conversion from client */
    FLOAT nx = (FLOAT)x / 1024.0f;
    FLOAT ny = (FLOAT)y / 768.0f;

    /* Iterate back-to-front (last drawn = topmost) */
    for (int i = (int)count - 1; i >= 0; i--) {
        uiBaseFrame_t *f = &frames[i];
        if (f->ui_flags & UIFLAG_HIDDEN) continue;
        LPCRECT r = &f->screen_rect;
        if (nx >= r->x && nx < r->x + r->w && ny >= r->y && ny < r->y + r->h)
            return true;
    }
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
    exp.DrawFrame = SC2_UI_DrawFrame;
    exp.DrawLoadingScreen = SC2_UI_SetLoadingState;
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
