/*
 * ui_layout.h — Server-authored layout system header.
 *
 * This header provides only what the layout draw system needs:
 * layout frames (LPCUIFRAME), renderer, player state, and constants.
 * It does NOT include FDF types (FRAMEDEF, uiFrameDef_s, etc.).
 */
#ifndef ui_layout_h
#define ui_layout_h

#include "common/shared.h"
#include "client/client.h"
#include "client/ui.h"

/* Layout frame draw function pointer */
typedef void (*layoutDrawFunc_t)(LPCUIFRAME frame, LPCRECT screen);

/* Layout system functions (implemented in cl_unit_layout.c) */
void SCR_SetLayoutLayer(DWORD layer, HANDLE data);
void SCR_ClearLayoutLayer(DWORD layer);
BOOL SCR_LayoutHitTest(int x, int y);
void SCR_DrawLayout(void);
void SCR_LayoutMouseEvent(uiMouseEvent_t event, int x, int y, int32_t param);
BOOL SCR_LayoutKeyEvent(int key);

#endif /* ui_layout_h */
