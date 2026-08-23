/*
 * ui_windows.c — In-game named XML window manager.
 *
 * Thin adapter over ui_xml.c's elem registry.  The glue-screen Lua runtime
 * is shut down before entering game mode; in-game windows use the same elem
 * registry loaded fresh via UIWow_XMLLoadFile and drawn by UIWow_XMLDraw.
 *
 * Receiving svc_window from the server:
 *   show=1 → load Interface/FrameXML/<id>.xml (if not yet loaded),
 *             then mark the root frame visible.
 *   show=0 → mark the root frame hidden.
 *
 * Button OnClick scripts are raw server command strings (e.g.
 * "window_close WelcomeFrame").  UIWow_WindowMouseDown fires them directly
 * via uiimport.ServerCommand without going through the Lua VM.
 */

#include "ui_local.h"
#include <string.h>
#include <stdio.h>

#define WOW_TIP_WIDTH 230.0f // UI pixels; TutorialFrame.xml root width
#define WOW_TIP_BOTTOM_OFFSET 90.0f // UI pixels; tutorial 42 bottom anchor offset from UIParent center
#define WOW_TIP_BACKDROP_CORNERS 0x1ff // corner flags; TutorialFrame.xml uses all tooltip border pieces

static FLOAT UIWow_TipX(FLOAT px) { return px / 1024.0f; }
static FLOAT UIWow_TipY(FLOAT px) { return px / 768.0f; }

BOOL UIWow_TipsEnabled(void) {
    LPCSTR value = uiimport.Cvar_String(BZ_WOW_CVAR_SHOW_TIPS, "1");
    return value && atoi(value) != 0;
}

/* Copy one localized value while the authoritative GlobalStrings Lua state is active. */
static BOOL UIWow_CopyGlobal(LPCSTR key, LPSTR dst, size_t size) {
    LPCSTR value;
    lua_getglobal(wow_ui.lua, key); value = lua_isstring(wow_ui.lua, -1) ? lua_tostring(wow_ui.lua, -1) : NULL;
    snprintf(dst, size, "%s", value ? value : ""); lua_pop(wow_ui.lua, 1);
    return dst[0] != '\0';
}

/* GlobalStrings.lua is the authoritative localized source for every tutorial panel string. */
static BOOL UIWow_LoadTutorialText(DWORD id) {
    char key[32]; BOOL okay;
    if (!wow_ui.lua || !UIWow_LoadLuaFile("Interface\\FrameXML\\GlobalStrings.lua", true)) return false;
    snprintf(key, sizeof(key), "TUTORIAL_TITLE%u", (unsigned)id); okay = UIWow_CopyGlobal(key, wow_ui.tutorial_title, sizeof(wow_ui.tutorial_title));
    snprintf(key, sizeof(key), "TUTORIAL%u", (unsigned)id); okay &= UIWow_CopyGlobal(key, wow_ui.tutorial_body, sizeof(wow_ui.tutorial_body));
    okay &= UIWow_CopyGlobal("ENABLE_TUTORIAL_TEXT", wow_ui.tutorial_check, sizeof(wow_ui.tutorial_check));
    okay &= UIWow_CopyGlobal("OKAY", wow_ui.tutorial_okay, sizeof(wow_ui.tutorial_okay));
    if (okay) return true;
    UIWow_Printf("UIWow: tutorial %u is missing TUTORIAL_TITLE%u or TUTORIAL%u\n", (unsigned)id, (unsigned)id, (unsigned)id);
    return false;
}

/* Every tutorial alert resolves through the same localized GlobalStrings keys and panel. */
BOOL UIWow_ShowTip(DWORD id) {
    wow_ui.tutorial_open = UIWow_TipsEnabled() && UIWow_LoadTutorialText(id);
    wow_ui.tutorial_id = wow_ui.tutorial_open ? id : 0;
    return wow_ui.tutorial_open;
}

/* Match TutorialFrame_NewTutorial: keep a bounded ID queue and refuse duplicates. */
BOOL UIWow_QueueTip(DWORD id) {
    if (!id || !UIWow_TipsEnabled()) return false;
    FOR_LOOP(i, wow_ui.tutorial_alert_count)
        if (wow_ui.tutorial_alerts[i] == id) return true;
    if (wow_ui.tutorial_alert_count >= WOW_UI_MAX_TUTORIAL_ALERTS) {
        UIWow_Printf("UIWow: tutorial alert queue full; rejected tutorial %u\n", (unsigned)id);
        return false;
    }
    wow_ui.tutorial_alerts[wow_ui.tutorial_alert_count++] = id;
    return true;
}

void UIWow_ShowWindow(const char *window_id, int show) {
    char path[256];

    if (!window_id || !window_id[0]) return;
    if (!strcmp(window_id, "TutorialFrame")) {
        if (show) UIWow_ShowTip(42);
        else { wow_ui.tutorial_open = false; wow_ui.tutorial_id = 0; }
        return;
    }

    if (!show) {
        UIWow_XMLSetFrameVisible(window_id, false);
        return;
    }

    /* Load if not yet in the registry. */
    if (UIWow_XmlFindByNamePub(window_id) < 0) {
        snprintf(path, sizeof(path), "Interface\\FrameXML\\%s.xml", window_id);
        if (!UIWow_XMLLoadFile(path)) {
            UIWow_Printf("UIWow: failed to load window '%s'\n", window_id);
            return;
        }
    }
    UIWow_XMLSetFrameVisible(window_id, true);
}

void UIWow_DrawWindows(void) {
    drawBackdrop_t backdrop = {0}; drawText_t text = {0}; drawImage_t image = {0};
    RECT frame, body, check, okay; VECTOR2 size; FLOAT h;
    UIWow_XMLDraw();
    if (!wow_ui.tutorial_open) return;
    body = MAKE(RECT, 0, 0, UIWow_TipX(210), 1);
    text = MAKE(drawText_t, .font = UIWow_LoadFont(14), .text = wow_ui.tutorial_body, .rect = body,
        .color = COLOR32_WHITE, .textWidth = body.w, .lineHeight = 1.15f, .flags = DRAW_WORD_WRAP,
        .halign = FONT_JUSTIFYLEFT, .valign = FONT_JUSTIFYTOP);
    size = wow_ui.renderer->GetTextSize(&text);
    h = MAX(UIWow_TipY(128), size.y + UIWow_TipY(62));
    frame = MAKE(RECT, 0.5f-UIWow_TipX(WOW_TIP_WIDTH)*0.5f, 0.5f+UIWow_TipY(WOW_TIP_BOTTOM_OFFSET)-h,
                 UIWow_TipX(WOW_TIP_WIDTH), h);
    backdrop.screen = frame;
    backdrop.bg.texture = UIWow_LoadTexture("Interface\\TutorialFrame\\TutorialFrameBackground");
    backdrop.bg.color = backdrop.edge.color = COLOR32_WHITE;
    backdrop.edge.texture = UIWow_LoadTexture("Interface\\Tooltips\\UI-Tooltip-Border");
    backdrop.corner.flags = WOW_TIP_BACKDROP_CORNERS; backdrop.corner.size = UIWow_TipY(16);
    backdrop.insets.right = UIWow_TipX(5); backdrop.insets.top = UIWow_TipY(3);
    backdrop.insets.bottom = UIWow_TipY(5); backdrop.insets.left = UIWow_TipX(3);
    backdrop.flags = DRAW_TILE;
    wow_ui.renderer->DrawBackdrop(&backdrop);
    text = MAKE(drawText_t, .font = UIWow_LoadFont(14), .text = wow_ui.tutorial_title,
        .rect = MAKE(RECT, frame.x+UIWow_TipX(10), frame.y+UIWow_TipY(9), UIWow_TipX(210), UIWow_TipY(18)),
        .color = MAKE(COLOR32,255,209,0,255), .textWidth = UIWow_TipX(210), .lineHeight = UIWow_TipY(18),
        .halign = FONT_JUSTIFYLEFT, .valign = FONT_JUSTIFYTOP);
    wow_ui.renderer->DrawText(&text);
    text.text = wow_ui.tutorial_body; text.rect = MAKE(RECT, frame.x+UIWow_TipX(10), frame.y+UIWow_TipY(29), UIWow_TipX(210), size.y);
    text.color = COLOR32_WHITE; text.textWidth = UIWow_TipX(210); text.lineHeight = 1.15f; text.flags = DRAW_WORD_WRAP;
    wow_ui.renderer->DrawText(&text);
    check = MAKE(RECT, frame.x+UIWow_TipX(5), frame.y+frame.h-UIWow_TipY(29), UIWow_TipX(24), UIWow_TipY(24));
    image = MAKE(drawImage_t, .texture = UIWow_LoadTexture("Interface\\Buttons\\UI-CheckBox-Up"), .shader = SHADER_UI,
        .alphamode = BLEND_MODE_BLEND, .screen = check, .uv = MAKE(RECT,0,0,1,1), .color = COLOR32_WHITE);
    wow_ui.renderer->DrawImageEx(&image);
    if (UIWow_TipsEnabled()) { image.texture = UIWow_LoadTexture("Interface\\Buttons\\UI-CheckBox-Check"); wow_ui.renderer->DrawImageEx(&image); }
    text = MAKE(drawText_t, .font = UIWow_LoadFont(12), .text = wow_ui.tutorial_check,
        .rect = MAKE(RECT, check.x+check.w, check.y, UIWow_TipX(95), check.h), .color = MAKE(COLOR32,255,209,0,255),
        .textWidth = UIWow_TipX(95), .lineHeight = check.h, .halign = FONT_JUSTIFYLEFT, .valign = FONT_JUSTIFYMIDDLE);
    wow_ui.renderer->DrawText(&text);
    okay = MAKE(RECT, frame.x+frame.w-UIWow_TipX(83), frame.y+frame.h-UIWow_TipY(28), UIWow_TipX(76), UIWow_TipY(21));
    image.texture = UIWow_LoadTexture("Interface\\Buttons\\UI-Panel-Button-Up"); image.screen = okay;
    image.uv = MAKE(RECT,0,0,0.625f,0.6875f); wow_ui.renderer->DrawImageEx(&image);
    text = MAKE(drawText_t, .font = UIWow_LoadFont(12), .text = wow_ui.tutorial_okay, .rect = okay,
        .color = MAKE(COLOR32,255,209,0,255), .textWidth = okay.w, .lineHeight = okay.h,
        .halign = FONT_JUSTIFYCENTER, .valign = FONT_JUSTIFYMIDDLE);
    wow_ui.renderer->DrawText(&text);
}

BOOL UIWow_WindowMouseDown(float nx, float ny) {
    FLOAT h; VECTOR2 size; RECT frame, check, okay;
    if (wow_ui.tutorial_open) {
        size = wow_ui.renderer->GetTextSize(&MAKE(drawText_t, .font = UIWow_LoadFont(14), .text = wow_ui.tutorial_body,
            .rect = MAKE(RECT,0,0,UIWow_TipX(210),1), .textWidth = UIWow_TipX(210), .lineHeight = 1.15f,
            .flags = DRAW_WORD_WRAP, .halign = FONT_JUSTIFYLEFT, .valign = FONT_JUSTIFYTOP));
        h = MAX(UIWow_TipY(128), size.y + UIWow_TipY(62));
        frame = MAKE(RECT, 0.5f-UIWow_TipX(WOW_TIP_WIDTH)*0.5f, 0.5f+UIWow_TipY(WOW_TIP_BOTTOM_OFFSET)-h,
                     UIWow_TipX(WOW_TIP_WIDTH), h);
        check = MAKE(RECT, frame.x+UIWow_TipX(5), frame.y+frame.h-UIWow_TipY(29), UIWow_TipX(119), UIWow_TipY(24));
        okay = MAKE(RECT, frame.x+frame.w-UIWow_TipX(83), frame.y+frame.h-UIWow_TipY(28), UIWow_TipX(76), UIWow_TipY(21));
        if (Rect_contains(&check, &MAKE(VECTOR2,nx,ny))) {
            uiimport.Cvar_Set(BZ_WOW_CVAR_SHOW_TIPS, UIWow_TipsEnabled() ? "0" : "1");
            if (!UIWow_TipsEnabled()) wow_ui.tutorial_alert_count = 0;
            return true;
        }
        if (Rect_contains(&okay, &MAKE(VECTOR2,nx,ny))) { wow_ui.tutorial_open = false; return true; }
    }
    LPCSTR onclick = UIWow_XMLHitButton(nx, ny);
    if (!onclick) return false;
    if (uiimport.ServerCommand) uiimport.ServerCommand(onclick);
    return true;
}

void UIWow_ShutdownWindows(void) {
    /* Registry is managed by ui_xml.c; UIWow_XMLClearFrames is called on game-mode entry. */
}
