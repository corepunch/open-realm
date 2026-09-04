/*
 * menu_windows.c — In-game named XML window manager.
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

#include "menu_local.h"
#include "wow_assets.h"
#include <string.h>
#include <stdio.h>

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

/* TutorialFrame inherits Blizzard fonts, button art, and UICheckButtonTemplate from earlier FrameXML files. */
static BOOL UIWow_LoadTutorialLayout(void) {
    static LPCSTR const files[] = { "Interface\\FrameXML\\Fonts.xml", "Interface\\FrameXML\\BasicControls.xml",
        "Interface\\FrameXML\\UIPanelTemplates.xml", WOW_TIP_LAYOUT };
    static LPCSTR const names[] = { "TutorialFrame", "TutorialFrameTitle", "TutorialFrameText",
        "TutorialFrameCheckButton", "TutorialFrameCheckboxText", "TutorialFrameOkayButton" };
    if (UIWow_XmlFindByNamePub(names[0]) < 0) {
        FOR_LOOP(i, sizeof(files) / sizeof(files[0])) {
            if (UIWow_XMLLoadFile(files[i])) continue;
            UIWow_Printf("UIWow: required native FrameXML %s is missing\n", files[i]);
            return false;
        }
    }
    FOR_LOOP(i, sizeof(names) / sizeof(names[0])) {
        if (UIWow_XmlFindByNamePub(names[i]) >= 0) continue;
        UIWow_Printf("UIWow: required tutorial FrameXML element %s is missing\n", names[i]);
        return false;
    }
    return true;
}

/* Bind localized state, then apply TutorialFrame.lua's SetHeight(body:GetHeight() + 62) contract. */
static BOOL UIWow_BindTutorial(void) {
    struct { LPCSTR name, text; } const values[] = {
        { "TutorialFrameTitle", wow_ui.tutorial_title }, { "TutorialFrameText", wow_ui.tutorial_body },
        { "TutorialFrameCheckboxText", wow_ui.tutorial_check }, { "TutorialFrameOkayButton", wow_ui.tutorial_okay },
    };
    FOR_LOOP(i, sizeof(values) / sizeof(values[0]))
        if (!UIWow_XMLSetFrameText(values[i].name, values[i].text)) return false;
    if (!UIWow_XMLSizeFrameToText("TutorialFrame", "TutorialFrameText", 62.0f)) return false;
    /* Native Lua bottom-anchors intro 42, which shifts a dynamically taller translation upward; center its resized bounds instead. */
    if (!UIWow_XMLSetFramePoint("TutorialFrame", &(WOWXMLPOINT){ wow_ui.tutorial_id == 42 ? "CENTER" : "BOTTOM", "UIParent", wow_ui.tutorial_id == 42 ? "CENTER" : "BOTTOM", 0, wow_ui.tutorial_id == 42 ? 0 : 100 })) return false;
    UIWow_XMLSetButtonChecked("TutorialFrameCheckButton", UIWow_TipsEnabled());
    UIWow_XMLSetFrameVisible("TutorialFrame", true);
    return true;
}

/* Every tutorial alert resolves through the same localized GlobalStrings keys and XML panel. */
BOOL UIWow_ShowTip(DWORD id) {
    wow_ui.tutorial_id = id;
    wow_ui.tutorial_open = UIWow_TipsEnabled() && UIWow_LoadTutorialText(id) && UIWow_LoadTutorialLayout() && UIWow_BindTutorial();
    if (!wow_ui.tutorial_open) wow_ui.tutorial_id = 0;
    if (!wow_ui.tutorial_open) {
        wow_ui.tutorial_okay_pressed = false;
        UIWow_XMLSetFrameVisible("TutorialFrame", false);
    }
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
        else {
            UIWow_XMLSetFrameVisible("TutorialFrame", false);
            wow_ui.tutorial_open = false; wow_ui.tutorial_id = 0; wow_ui.tutorial_okay_pressed = false;
        }
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

/* Input follows the authored frame rectangle instead of maintaining parallel hit geometry. */
static BOOL UIWow_TutorialContains(LPCSTR name, FLOAT x, FLOAT y) {
    int idx = UIWow_XmlFindByNamePub(name); RECT r;
    if (idx < 0) return false;
    UIWow_XmlComputeRectPub(idx, &r.x, &r.y, &r.w, &r.h);
    return Rect_contains(&r, &MAKE(VECTOR2, x, y));
}

/* Expose the XML-authored Okay rectangle for input tests. */
void UIWow_TutorialOkayRect(RECT *out) {
    int idx;
    if (!out) return;
    *out = MAKE(RECT, 0, 0, 0, 0); idx = UIWow_XmlFindByNamePub("TutorialFrameOkayButton");
    if (idx >= 0) UIWow_XmlComputeRectPub(idx, &out->x, &out->y, &out->w, &out->h);
}

void UIWow_DrawWindows(void) {
    UIWow_XMLDraw();
}

BOOL UIWow_WindowMouseDown(float nx, float ny) {
    if (wow_ui.tutorial_open) {
        if (UIWow_TutorialContains("TutorialFrameCheckButton", nx, ny)) {
            uiimport.Cvar_Set(BZ_WOW_CVAR_SHOW_TIPS, UIWow_TipsEnabled() ? "0" : "1");
            UIWow_XMLSetButtonChecked("TutorialFrameCheckButton", UIWow_TipsEnabled());
            if (!UIWow_TipsEnabled()) wow_ui.tutorial_alert_count = 0;
            return true;
        }
        if (UIWow_TutorialContains("TutorialFrameOkayButton", nx, ny)) {
            wow_ui.tutorial_okay_pressed = UIWow_XMLSetButtonPressed("TutorialFrameOkayButton", true);
            return wow_ui.tutorial_okay_pressed;
        }
    }
    LPCSTR onclick = UIWow_XMLHitButton(nx, ny);
    if (!onclick) return false;
    if (uiimport.ServerCommand) uiimport.ServerCommand(onclick);
    return true;
}

/* The Okay button closes on release, not press: mouse down only arms the
 * pushed visual.  Matches the XML button handler's press/release contract. */
BOOL UIWow_WindowMouseUp(float nx, float ny) {
    if (!wow_ui.tutorial_okay_pressed) return false;
    UIWow_XMLSetButtonPressed("TutorialFrameOkayButton", false); wow_ui.tutorial_okay_pressed = false;
    if (wow_ui.tutorial_open && UIWow_TutorialContains("TutorialFrameOkayButton", nx, ny)) {
        UIWow_XMLSetFrameVisible("TutorialFrame", false);
        wow_ui.tutorial_open = false;
    }
    return true;
}

void UIWow_ShutdownWindows(void) {
    /* Registry is managed by ui_xml.c; UIWow_XMLClearFrames is called on game-mode entry. */
}
