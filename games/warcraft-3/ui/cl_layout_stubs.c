/*
 * cl_layout_stubs.c — Stub implementations of edit hooks for standalone tools.
 */
#include "client/ui.h"

__attribute__((weak)) BOOL UI_LayoutEditKey(int key) {
    (void)key;
    return false;
}

__attribute__((weak)) void UI_LayoutTextInput(LPCSTR text) {
    (void)text;
}
