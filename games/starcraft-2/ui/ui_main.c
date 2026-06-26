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

typedef enum {
    SC2_UI_MODE_NONE,
    SC2_UI_MODE_MAIN_MENU,
    SC2_UI_MODE_GAME,
} sc2UIMode_t;

/* SC2 UI state */
typedef struct {
    BOOL initialized;
    sc2UIMode_t mode;
    DWORD time;
    PATHSTR loading_map;
    PATHSTR loading_status;
    FLOAT loading_progress;
} sc2UIState_t;

static sc2UIState_t sc2_ui_state;

/* Text storage for unit info labels (pointed to by uiBaseFrame_t::text) */
static char sc2_unit_name[128];
static char sc2_mineral_text[32];

static void SC2_UI_ShowMainMenu(void) {
    fprintf(stderr, "SC2_UI: loading main menu layout\n");
    if (!SC2_LayoutBuildMainMenu()) {
        fprintf(stderr, "SC2_UI: failed to build main menu layout\n");
        return;
    }
    sc2_ui_state.mode = SC2_UI_MODE_MAIN_MENU;
}

static void SC2_UI_EnterGameMode(void) {
    fprintf(stderr, "SC2_UI: loading game HUD layout\n");
    if (!SC2_LayoutBuildGameUI()) {
        fprintf(stderr, "SC2_UI: failed to build game HUD layout\n");
    }
    sc2_ui_state.mode = SC2_UI_MODE_GAME;
}

/* ---------- Command handlers ---------- */

static void SC2_UI_MenuLogin_f(void)  { SC2_UI_ShowMainMenu(); }
static void SC2_UI_MenuMain_f(void)   { SC2_UI_ShowMainMenu(); }

/* ---------- Initialization ---------- */

static void SC2_UI_InitLocal(void) {
    if (sc2_ui_state.initialized) return;

    fprintf(stderr, "SC2_UI: initializing\n");

    uiimport.Cmd_AddCommand("menu_login", SC2_UI_MenuLogin_f);
    uiimport.Cmd_AddCommand("menu_main",  SC2_UI_MenuMain_f);

    sc2_ui_state.initialized = true;

    /* If a map is already loaded jump straight into game mode, otherwise show menu */
    LPCSTR map = uiimport.Cvar_String ? uiimport.Cvar_String("map", "") : "";
    if (map && *map) {
        SC2_UI_EnterGameMode();
    } else {
        SC2_UI_ShowMainMenu();
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

static void SC2_UI_DrawLayoutFrames(void) {
    if (sc2_ui_state.mode == SC2_UI_MODE_NONE) return;

    LPRENDERER renderer = uiimport.GetRenderer ? uiimport.GetRenderer() : NULL;
    if (!renderer) return;

    DWORD count = 0;
    uiBaseFrame_t *frames = SC2_LayoutGetFrames(&count);
    if (!frames || !count) return;

    if (sc2_ui_state.mode == SC2_UI_MODE_GAME) {
        uiBaseFrame_t *minimap = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_MINIMAP_PANEL);
        if (minimap && renderer->DrawMinimap) {
            renderer->DrawMinimap(&minimap->screen_rect);
        }
    }

    for (DWORD i = 0; i < count; i++) {
        uiBaseFrame_t *f = &frames[i];
        if (f->ui_flags & UIFLAG_HIDDEN) continue;

        if (f->on_draw) {
            f->on_draw(f, &f->screen_rect);
            continue;
        }

        /* Draw image if present */
        if (f->image && renderer->DrawImageEx) {
            LPCTEXTURE *textures = uiimport.GetTextures ? uiimport.GetTextures() : NULL;
            LPCTEXTURE tex = textures ? textures[f->image] : NULL;
            if (tex) {
                renderer->DrawImageEx(&MAKE(drawImage_t,
                    .texture = tex,
                    .screen = f->screen_rect,
                    .uv = MAKE(RECT, 0, 0, 1, 1),
                    .color = f->color.a ? f->color : COLOR32_WHITE,
                    .alphamode = BLEND_MODE_BLEND,
                ));
            }
        }

        /* Draw text label */
        if (f->text && renderer->DrawText) {
            LPCFONT font = renderer->LoadFont ? renderer->LoadFont("fonts/default.ttf", 14) : NULL;
            if (font) {
                renderer->DrawText(&MAKE(drawText_t,
                    .font = font,
                    .text = f->text,
                    .rect = f->screen_rect,
                    .color = f->text_color.a ? f->text_color : COLOR32_WHITE,
                    .textWidth = f->screen_rect.w,
                ));
            }
        }
    }
}

static void SC2_UI_RefreshLocal(DWORD msec) {
    sc2_ui_state.time += msec;
    if (sc2_ui_state.mode == SC2_UI_MODE_GAME) {
        SC2_UI_UpdateResourceDisplay();
    }
    SC2_UI_DrawLayoutFrames();
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

static BOOL SC2_UI_MouseEvent(uiMouseEvent_t event, int x, int y, int32_t param) {
    /* TODO: SC2 frame hit testing and event dispatch */
    (void)event; (void)x; (void)y; (void)param;
    return false;
}

static void SC2_UI_SetLoadingState(LPCSTR map, LPCSTR status, FLOAT progress) {
    if (map) { strncpy(sc2_ui_state.loading_map, map, MAX_PATHLEN - 1); sc2_ui_state.loading_map[MAX_PATHLEN - 1] = '\0'; }
    if (status) { strncpy(sc2_ui_state.loading_status, status, MAX_PATHLEN - 1); sc2_ui_state.loading_status[MAX_PATHLEN - 1] = '\0'; }
    sc2_ui_state.loading_progress = progress;
}

static void SC2_UI_UpdateUnitUI(DWORD num_units, uiUnitData_t *units) {
    if (sc2_ui_state.mode != SC2_UI_MODE_GAME) return;
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
    (void)state;
}

/* ---------- Entry point ---------- */

uiExport_t UI_GetAPI(uiImport_t import) {
    uiExport_t exp;
    memset(&exp, 0, sizeof(exp));

    uiimport = import;

    exp.Init             = SC2_UI_InitLocal;
    exp.Shutdown         = SC2_UI_ShutdownLocal;
    exp.Refresh          = SC2_UI_RefreshLocal;
    exp.KeyEvent         = SC2_UI_KeyEvent;
    exp.TextInput        = SC2_UI_TextInput;
    exp.MouseEvent       = SC2_UI_MouseEvent;
    exp.UpdateUnitUI     = SC2_UI_UpdateUnitUI;
    exp.UpdateLobbySetup = SC2_UI_UpdateLobbySetup;
    exp.DrawLoadingScreen = SC2_UI_SetLoadingState;

    return exp;
}
