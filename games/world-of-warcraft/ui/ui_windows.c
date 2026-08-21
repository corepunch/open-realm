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

void UIWow_ShowWindow(const char *window_id, int show) {
    char path[256];

    if (!window_id || !window_id[0]) return;

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
    UIWow_XMLDraw();
}

BOOL UIWow_WindowMouseDown(float nx, float ny) {
    LPCSTR onclick = UIWow_XMLHitButton(nx, ny);
    if (!onclick) return false;
    if (uiimport.ServerCommand) uiimport.ServerCommand(onclick);
    return true;
}

void UIWow_ShutdownWindows(void) {
    /* Registry is managed by ui_xml.c; UIWow_XMLClearFrames is called on game-mode entry. */
}
