#include "../../../client/ui_layout.h"

void UI_LayoutSetLayer(DWORD layer, HANDLE data) {
    (void)layer;
    (void)data;
}

void UI_LayoutClearLayer(DWORD layer) {
    (void)layer;
}

BOOL UI_LayoutHitTest(int x, int y) {
    (void)x;
    (void)y;
    return false;
}

void UI_LayoutDrawOverlays(void) {
}

void UI_LayoutMouseEvent(uiMouseEvent_t event, int x, int y, int32_t param) {
    (void)event;
    (void)x;
    (void)y;
    (void)param;
}
