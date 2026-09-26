/*
 * menu_loading.c - Loading screen drawing and map background management.
 *
 * Draws the dynamic map background and runtime title; classic WoW ships no loading-screen FrameXML.
 */
#include "menu_local.h"

/* Classic WoW creates loading presentation outside FrameXML, so keep this renderer-owned runtime path. */

void UIWow_DrawLoadingScreenC(cstring_t map, cstring_t status, float progress) {
    rect_t full = MAKE(rect_t, 0, 0, 1, 1);
    rect_t uv = MAKE(rect_t, 0, 0, 1, 1);
    cstring_t info = mi.GetConfigString(WOW_CS_MAPINFO);
    cstring_t map_title = Wow_InfoValueForKey(info, "title", "");

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

    if (map_title && *map_title) {
        rect_t title = MAKE(rect_t, 0.16f, 0.77f, 0.68f, 0.05f);
        wow_ui.renderer->DrawText(&MAKE(drawText_t, .font = UIWow_LoadFont(22), .text = map_title, .rect = title,
            .color = MAKE(COLOR32,235,210,160,255), .textWidth = title.w, .lineHeight = title.h,
            .halign = FONT_JUSTIFYCENTER, .valign = FONT_JUSTIFYMIDDLE));
    }

}
