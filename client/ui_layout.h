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

/* The UI import table (defined in the UI library, accessible from client) */
extern uiImport_t uiimport;

/* Layout frame draw function pointer */
typedef void (*layoutDrawFunc_t)(LPCUIFRAME frame, LPCRECT screen);

/* Layout system functions (implemented in cl_unit_layout.c) */
void UI_LayoutSetLayer(DWORD layer, HANDLE data);
void UI_LayoutClearLayer(DWORD layer);
BOOL UI_LayoutHitTest(int x, int y);
void UI_LayoutDrawOverlays(void);
void UI_LayoutMouseEvent(uiMouseEvent_t event, int x, int y, int32_t param);

/* Time accessor — use uiimport.GetTime() instead of calling UI_GetTime directly */

#endif /* ui_layout_h */
