/*
 * cl_layout_stubs.c — Stub implementations of layout functions for standalone tools.
 *
 * When the WC3 UI dylib is loaded by the client, the real implementations in
 * cl_unit_layout.c take precedence (client symbols are in the global scope).
 * These stubs allow standalone tools (blpgen, mdxtool, etc.) to link against
 * the UI dylib without pulling in the full client.
 */
#include "client/ui.h"
#include "client/ui_layout.h"

extern uiImport_t uiimport;

__attribute__((weak)) void UI_LayoutSetLayer(DWORD layer, HANDLE data) {
    (void)layer;
    (void)data;
}

__attribute__((weak)) void UI_LayoutClearLayer(DWORD layer) {
    (void)layer;
}

__attribute__((weak)) BOOL UI_LayoutHitTest(int x, int y) {
    (void)x;
    (void)y;
    return false;
}

__attribute__((weak)) void UI_LayoutDrawOverlays(void) {
}

__attribute__((weak)) void UI_LayoutMouseEvent(uiMouseEvent_t event, int x, int y, int32_t param) {
    (void)event;
    (void)x;
    (void)y;
    (void)param;
}

__attribute__((weak)) BOOL UI_LayoutEditKey(int key) {
    (void)key;
    return false;
}

__attribute__((weak)) void UI_LayoutTextInput(LPCSTR text) {
    (void)text;
}
