/*
 * ui_layout.h — Server-authored layout system header.
 *
 * This header provides only what the layout draw system needs:
 * layout frames (uiFrame_t const *), renderer, player state, and constants.
 * It does NOT include FDF types (FRAMEDEF, uiFrameDef_s, etc.).
 */
#ifndef ui_layout_h
#define ui_layout_h

#include "common/shared.h"
#include "client/client.h"
#include "client/menu.h"
#include "client/model_matrix.h"

/* Layout frame draw function pointer */
typedef void (*layoutDrawFunc_t)(uiFrame_t const *frame, rect_t const *screen);

/* Layout system functions (implemented in cl_unit_layout.c) */
void SCR_SetLayoutLayer(uint32_t layer, handle_t data);
void SCR_ClearLayoutLayer(uint32_t layer);
void SCR_SetLayoutRoot(rect_t const *root);
rect_t SCR_LayoutSceneRect(void);
float SCR_UICanvasWidth(void);
vector2_t SCR_ScreenToUI(int x, int y);
bool SCR_LayoutFrameHasClickCommand(uiFrame_t const *frame);
void SCR_LayoutSendFrameCommand(uiFrame_t const *frame);
void SCR_LayoutSetPointer(handle_t layout, uint32_t number, bool down);
void SCR_LayoutPrepare(handle_t layout, rect_t const *root);
void SCR_WindowPrepare(handle_t layout, rect_t const *root);
bool SCR_WindowLayoutIsCurrent(handle_t layout);
void SCR_LayoutDrawOverlay(handle_t layout);
bool SCR_LayoutHitTest(int x, int y);
bool SCR_LayoutModalActive(void);
void SCR_LayoutClampSelectionRect(rect_t *rect);
void SCR_DrawLayout(void);
void SCR_DrawLoadingLayout(void);
bool SCR_LayoutMouseEvent(menuMouseEvent_t event, int x, int y, int32_t param);
bool SCR_LayoutScrollTextAreaAt(handle_t layout, vector2_t const *point, int wheel_y);
float SCR_LayoutTextAreaMaxScroll(uiFrame_t const *frame);
bool SCR_LayoutKeyEvent(int key);

#endif /* ui_layout_h */
