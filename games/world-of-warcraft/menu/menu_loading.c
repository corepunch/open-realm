/*
 * menu_loading.c - Loading screen drawing and map background management.
 *
 * Draws the dynamic map background and runtime title; classic WoW ships no loading-screen FrameXML.
 */
#include "menu_local.h"

/* -------------------------------------------------------------------------
 * Map background texture
 * ---------------------------------------------------------------------- */

void UIWow_UpdateMapBackground(LPCPLAYER ps) {
    static LPCSTR default_bg = "Interface\\Glues\\LoadingScreens\\LoadScreenEnviroment.blp";
    LPCSTR screen_path;

    LPCSTR map_path = ps && ps->texts[PLAYERTEXT_MAP_PREVIEW] && *ps->texts[PLAYERTEXT_MAP_PREVIEW]
        ? ps->texts[PLAYERTEXT_MAP_PREVIEW]
        : default_bg;

    if (!strcmp(wow_ui.active_map, map_path)) {
        return;
    }
    snprintf(wow_ui.active_map, sizeof(wow_ui.active_map), "%s", map_path);

    screen_path = map_path;

    if (!wow_ui.renderer) {
        UIWow_WarnOnce(WOW_UI_WARN_NO_RENDERER, "UIWow: loading background update skipped because renderer is unavailable\n");
        return;
    }

    SAFE_DELETE(wow_ui.textures[WOW_UI_TEX_BACKGROUND], wow_ui.renderer->ReleaseTexture);
    wow_ui.textures[WOW_UI_TEX_BACKGROUND] = wow_ui.renderer->LoadTexture(screen_path);
    if (!wow_ui.textures[WOW_UI_TEX_BACKGROUND]) {
        UIWow_Printf("UIWow: failed loading map background '%s', trying default\n", screen_path);
        wow_ui.textures[WOW_UI_TEX_BACKGROUND] = wow_ui.renderer->LoadTexture(default_bg);
        if (!wow_ui.textures[WOW_UI_TEX_BACKGROUND]) {
            UIWow_WarnOnce(WOW_UI_WARN_NO_LOAD_BACKGROUND, "UIWow: failed loading default loading background '%s'\n", default_bg);
        }
    }

    UIWow_Printf("UIWow: loading screen map=%s background=%s\n", wow_ui.active_map[0] ? wow_ui.active_map : "<none>", screen_path);
}

/* Classic WoW creates loading presentation outside FrameXML, so keep this renderer-owned runtime path. */

void UIWow_DrawLoadingScreenC(LPCSTR map, LPCSTR status, FLOAT progress) {
    RECT full = MAKE(RECT, 0, 0, 1, 1);
    RECT uv = MAKE(RECT, 0, 0, 1, 1);
    LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;
    LPCSTR map_title = ps ? ps->texts[PLAYERTEXT_MAP_TITLE] : NULL;

    (void)map;
    (void)status;
    (void)progress;

    UIWow_EnsureRenderer();
    if (!wow_ui.renderer) {
        return;
    }

    if (wow_ui.textures[WOW_UI_TEX_BACKGROUND]) {
        wow_ui.renderer->DrawImage(wow_ui.textures[WOW_UI_TEX_BACKGROUND], &full, &uv, COLOR32_WHITE);
    }

    if (!map_title || !*map_title) {
        map_title = "";
    }

    if (*map_title) {
        RECT title = MAKE(RECT, 0.16f, 0.77f, 0.68f, 0.05f);
        wow_ui.renderer->DrawText(&MAKE(drawText_t, .font = UIWow_LoadFont(22), .text = map_title, .rect = title,
            .color = MAKE(COLOR32,235,210,160,255), .textWidth = title.w, .lineHeight = title.h,
            .halign = FONT_JUSTIFYCENTER, .valign = FONT_JUSTIFYMIDDLE));
    }
}
