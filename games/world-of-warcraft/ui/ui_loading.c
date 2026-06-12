/*
 * ui_loading.c — Map background texture management for the loading screen.
 *
 * The loading screen layout, bar, and text are drawn by LoadingScreen.lua
 * via ow3.draw_image / ow3.get_loading_progress / ow3.get_loading_title.
 * This file only manages the per-map background texture that Lua draws
 * as the first layer via ow3.draw_image with the cached index.
 */
#include "ui_local.h"

void UIWow_UpdateMapBackground(LPCPLAYER ps) {
    static LPCSTR default_bg = "Interface\\Glues\\LoadingScreens\\LoadScreenEnviroment.blp";
    LPCSTR map_path   = uiimport.GetLoadingMap ? uiimport.GetLoadingMap() : "";
    LPCSTR screen_path;

    if (!map_path) {
        map_path = "";
    }
    if (!strcmp(wow_ui.active_map, map_path)) {
        return;
    }
    snprintf(wow_ui.active_map, sizeof(wow_ui.active_map), "%s", map_path);

    screen_path = (ps && ps->texts[PLAYERTEXT_MAP_PREVIEW] && *ps->texts[PLAYERTEXT_MAP_PREVIEW])
        ? ps->texts[PLAYERTEXT_MAP_PREVIEW]
        : default_bg;

    SAFE_DELETE(wow_ui.background, wow_ui.renderer->ReleaseTexture);
    wow_ui.background = wow_ui.renderer->LoadTexture(screen_path);
    if (!wow_ui.background) {
        wow_ui.background = wow_ui.renderer->LoadTexture(default_bg);
    }

    UIWow_Printf("UIWow: loading screen map=%s background=%s\n",
                 wow_ui.active_map[0] ? wow_ui.active_map : "<none>",
                 screen_path);
}
