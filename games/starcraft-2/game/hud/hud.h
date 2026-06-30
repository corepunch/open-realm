/*
 * hud.h — SC2 server-authored HUD: sc2BaseFrame_t → uiFrame_t bridge.
 *
 * Mirrors the WC3 hud_local.h pattern.  The server reads parsed .SC2Layout
 * data (sc2BaseFrame_t arrays from sc2_layout.h), overlays dynamic state
 * (stat bindings, text, visibility), converts to uiFrame_t, and sends via
 * svc_layout.  The client (cl_unit_layout.c) renders generically.
 *
 * Pipeline:
 *   SC2_LayoutBuildGameUI() → sc2BaseFrame_t[] → SC2_HUD_WriteLayout()
 *   → uiFrame_t per frame → gi.Write(PF_UIFRAME) → gi.unicast(ent)
 */
#ifndef SC2_HUD_H
#define SC2_HUD_H

#include "../g_sc2_local.h"
#include "games/starcraft-2/ui/sc2_layout.h"

/* Build sc2BaseFrame_t → uiFrame_t and queue via gi.Write(PF_UIFRAME).
 * Returns false if frame is NULL or the uiFrame_t buffer overflows. */
BOOL SC2_HUD_BuildFrameForWrite(LPCSC2BASEFRAME frame, uiFrame_t *out);

/* Write one frame (calls SC2_HUD_BuildFrameForWrite + gi.Write). */
void SC2_HUD_WriteFrame(LPCSC2BASEFRAME frame);

/* Write frame tree rooted at 'frame' recursively (depth-first, skip hidden). */
void SC2_HUD_WriteFrameWithChildren(LPCSC2BASEFRAME frames, DWORD count,
                                    LPCSC2BASEFRAME frame);

/* Open a layout layer message (svc_layout + layer byte). */
void SC2_HUD_WriteStart(DWORD layer);

/* Close the message and unicast to ent. */
void SC2_HUD_WriteEnd(LPEDICT ent);

/* Write a complete layout layer: start → tree → end. */
void SC2_HUD_WriteLayout(LPEDICT ent, LPCSC2BASEFRAME frames, DWORD count,
                         LPCSC2BASEFRAME root, DWORD layer);

/* Wire gi file I/O into the layout parser — call once from SC2_Init. */
void SC2_HUD_InitLayoutHost(void);

/* Per-frame HUD writers called from G_RunFrame */
void SC2_HUD_WriteResourcePanel(LPEDICT ent);
void SC2_HUD_WriteConsolePanel(LPEDICT ent);
void SC2_HUD_WriteCommandPanel(LPEDICT ent);
void SC2_HUD_WriteInfoPanel(LPEDICT ent);

#endif /* SC2_HUD_H */
