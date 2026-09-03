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
void SCR_SetLayoutRoot(LPCRECT root);
FLOAT SCR_UICanvasWidth(void);
VECTOR2 SCR_ScreenToUI(int x, int y);
BOOL SCR_LayoutFrameHasClickCommand(LPCUIFRAME frame);
void SCR_LayoutSendFrameCommand(LPCUIFRAME frame);
void SCR_LayoutSetPointer(HANDLE layout, DWORD number, BOOL down);
void SCR_LayoutPrepare(HANDLE layout, LPCRECT root);
void SCR_WindowPrepare(HANDLE layout, LPCRECT root);
void SCR_LayoutDrawOverlay(HANDLE layout);
BOOL SCR_LayoutHitTest(int x, int y);
BOOL SCR_LayoutModalActive(void);
void SCR_LayoutClampSelectionRect(LPRECT rect);
void SCR_DrawLayout(void);
BOOL SCR_LayoutMouseEvent(uiMouseEvent_t event, int x, int y, int32_t param);
BOOL SCR_LayoutScrollTextAreaAt(HANDLE layout, LPCVECTOR2 point, int wheel_y);
FLOAT SCR_LayoutTextAreaMaxScroll(LPCUIFRAME frame);
BOOL SCR_LayoutKeyEvent(int key);

#endif /* ui_layout_h */
