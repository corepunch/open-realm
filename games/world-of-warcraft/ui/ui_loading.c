/*
 * ui_loading.c — Loading screen rendering and map-background management.
 *
 * Handles the full-screen loading screen shown while a map is loading:
 * background texture selection, progress-bar smoothing, and the complete
 * draw pass (background, fill bar, glow/glass/border layers, title, status).
 */
#include "ui_local.h"

static LPCSTR UIWow_DefaultLoadingBackground(void) {
    return "Interface\\Glues\\LoadingScreens\\LoadScreenEnviroment.blp";
}

void UIWow_LoadStaticAssets(void) {
    UIWow_EnsureRenderer();
    if (!wow_ui.renderer) {
        return;
    }
    if (!wow_ui.bar_background) {
        wow_ui.bar_background = wow_ui.renderer->LoadTexture("Interface\\Glues\\LoadingBar\\Loading-BarBackground.blp");
    }
    if (!wow_ui.bar_border) {
        wow_ui.bar_border = wow_ui.renderer->LoadTexture("Interface\\Glues\\LoadingBar\\Loading-BarBorder.blp");
    }
    if (!wow_ui.bar_fill) {
        wow_ui.bar_fill = wow_ui.renderer->LoadTexture("Interface\\Glues\\LoadingBar\\Loading-BarFill.blp");
    }
    if (!wow_ui.bar_glass) {
        wow_ui.bar_glass = wow_ui.renderer->LoadTexture("Interface\\Glues\\LoadingBar\\Loading-BarGlass.blp");
    }
    if (!wow_ui.bar_glow) {
        wow_ui.bar_glow = wow_ui.renderer->LoadTexture("Interface\\Glues\\LoadingBar\\Loading-BarGlow.blp");
    }
    if (!wow_ui.font_title) {
        wow_ui.font_title = wow_ui.renderer->LoadFont("Fonts\\FRIZQT__.TTF", 22);
    }
    if (!wow_ui.font_status) {
        wow_ui.font_status = wow_ui.renderer->LoadFont("Fonts\\FRIZQT__.TTF", 16);
    }
}

void UIWow_UpdateMapBackground(LPCPLAYER ps) {
    LPCSTR map_path;
    LPCSTR screen_path;

    UIWow_EnsureRenderer();
    if (!wow_ui.renderer) {
        return;
    }

    map_path = uiimport.GetLoadingMap ? uiimport.GetLoadingMap() : "";
    if (!map_path) {
        map_path = "";
    }
    screen_path = (ps && ps->texts[PLAYERTEXT_MAP_PREVIEW] && *ps->texts[PLAYERTEXT_MAP_PREVIEW])
        ? ps->texts[PLAYERTEXT_MAP_PREVIEW]
        : UIWow_DefaultLoadingBackground();

    if (!strcmp(wow_ui.active_map, map_path)) {
        return;
    }

    snprintf(wow_ui.active_map, sizeof(wow_ui.active_map), "%s", map_path);

    SAFE_DELETE(wow_ui.background, wow_ui.renderer->ReleaseTexture);
    wow_ui.background = wow_ui.renderer->LoadTexture(screen_path);
    if (!wow_ui.background) {
        wow_ui.background = wow_ui.renderer->LoadTexture(UIWow_DefaultLoadingBackground());
    }

    UIWow_Printf("UIWow: loading screen map=%s background=%s\n",
                 wow_ui.active_map[0] ? wow_ui.active_map : "<none>",
                 screen_path);
}

static FLOAT UIWow_GetLoadingProgress(void) {
    FLOAT target = uiimport.GetLoadingProgress ? uiimport.GetLoadingProgress() : 0.0f;

    if (target < 0.0f) {
        target = 0.0f;
    } else if (target > 1.0f) {
        target = 1.0f;
    }

    if (target < wow_ui.displayed_progress) {
        wow_ui.displayed_progress = target;
    } else {
        wow_ui.displayed_progress = wow_ui.displayed_progress * 0.82f + target * 0.18f;
        if (target - wow_ui.displayed_progress < 0.002f) {
            wow_ui.displayed_progress = target;
        }
    }
    return wow_ui.displayed_progress;
}

void UIWow_DrawLoadingScreen(void) {
    RECT full       = MAKE(RECT, 0,      0,      1,     1);
    RECT uv         = MAKE(RECT, 0,      0,      1,     1);
    RECT bar        = MAKE(RECT, 0.305f, 0.865f, 0.39f, 0.032f);
    RECT bar_border = MAKE(RECT, 0.285f, 0.848f, 0.43f, 0.067f);
    RECT bar_fill   = bar;
    RECT title_rect = MAKE(RECT, 0.16f,  0.77f,  0.68f, 0.05f);
    RECT status_rect= MAKE(RECT, 0.16f,  0.818f, 0.68f, 0.04f);
    FLOAT progress  = UIWow_GetLoadingProgress();
    LPCPLAYER ps    = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;
    LPCSTR map_path = uiimport.GetLoadingMap  ? uiimport.GetLoadingMap()  : "";
    LPCSTR map_title= ps ? ps->texts[PLAYERTEXT_MAP_TITLE] : NULL;
    LPCSTR status   = uiimport.GetLoadingStatus ? uiimport.GetLoadingStatus() : "";

    UIWow_EnsureRenderer();
    if (!wow_ui.renderer) {
        return;
    }

    if (wow_ui.background) {
        wow_ui.renderer->DrawImage(wow_ui.background, &full, &uv, COLOR32_WHITE);
    }

    if (wow_ui.bar_background) {
        wow_ui.renderer->DrawImage(wow_ui.bar_background, &bar, &uv, COLOR32_WHITE);
    }
    if (wow_ui.bar_fill) {
        bar_fill.w *= progress;
        if (bar_fill.w > 0.0f) {
            RECT fill_uv = uv;
            fill_uv.w = progress;
            wow_ui.renderer->DrawImage(wow_ui.bar_fill, &bar_fill, &fill_uv, COLOR32_WHITE);
        }
    }
    if (wow_ui.bar_glow) {
        wow_ui.renderer->DrawImageEx(&MAKE(drawImage_t,
                                           .texture   = wow_ui.bar_glow,
                                           .screen    = bar_border,
                                           .uv        = uv,
                                           .color     = MAKE(COLOR32, 255, 255, 255, 200),
                                           .shader    = SHADER_UI,
                                           .alphamode = BLEND_MODE_ADD));
    }
    if (wow_ui.bar_glass) {
        wow_ui.renderer->DrawImage(wow_ui.bar_glass, &bar_border, &uv, COLOR32_WHITE);
    }
    if (wow_ui.bar_border) {
        wow_ui.renderer->DrawImage(wow_ui.bar_border, &bar_border, &uv, COLOR32_WHITE);
    }

    if (!map_title || !*map_title) {
        map_title = map_path;
    }
    if (wow_ui.font_title && map_title && *map_title) {
        wow_ui.renderer->DrawText(&MAKE(drawText_t,
                                        .font       = wow_ui.font_title,
                                        .text       = map_title,
                                        .rect       = title_rect,
                                        .color      = MAKE(COLOR32, 235, 210, 160, 255),
                                        .textWidth  = title_rect.w,
                                        .lineHeight = title_rect.h,
                                        .wordWrap   = false,
                                        .halign     = FONT_JUSTIFYCENTER,
                                        .valign     = FONT_JUSTIFYMIDDLE));
    }
    if (wow_ui.font_status && status && *status) {
        wow_ui.renderer->DrawText(&MAKE(drawText_t,
                                        .font       = wow_ui.font_status,
                                        .text       = status,
                                        .rect       = status_rect,
                                        .color      = MAKE(COLOR32, 220, 220, 220, 255),
                                        .textWidth  = status_rect.w,
                                        .lineHeight = status_rect.h,
                                        .wordWrap   = false,
                                        .halign     = FONT_JUSTIFYCENTER,
                                        .valign     = FONT_JUSTIFYMIDDLE));
    }
}
