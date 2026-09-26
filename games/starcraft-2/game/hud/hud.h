/*
 * hud.h — SC2 server-authored HUD: sc2BaseFrame_t → uiFrame_t bridge.
 *
 * Mirrors the WC3 hud_local.h pattern.  The server reads parsed .SC2Layout
 * data (sc2BaseFrame_t arrays from menu_layout.h), overlays dynamic state
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
#include "games/starcraft-2/menu/menu_layout.h"

/* Build sc2BaseFrame_t → uiFrame_t and queue via gi.Write(PF_UIFRAME).
 * Returns false if frame is NULL or the uiFrame_t buffer overflows. */
bool SC2_HUD_BuildFrameForWrite(sc2BaseFrame_t const * frame, uiFrame_t *out);

/* Write one frame (calls SC2_HUD_BuildFrameForWrite + gi.Write). */
void SC2_HUD_WriteFrame(sc2BaseFrame_t const * frame);

/* Write the parent chain of 'frame' up to the root, root first, skipping
 * already-assigned frames.  Ensures all ancestor wire numbers exist before
 * children reference them. */
void SC2_HUD_WriteAncestors(sc2BaseFrame_t const * frames, uint32_t count,
                             sc2BaseFrame_t const * frame);

/* Write frame tree rooted at 'frame' recursively (depth-first, skip hidden). */
void SC2_HUD_WriteFrameWithChildren(sc2BaseFrame_t const * frames, uint32_t count,
                                    sc2BaseFrame_t const * frame);

/* Open a layout layer message (svc_layout + layer byte). */
void SC2_HUD_WriteStart(uint32_t layer);

/* Close the message and unicast to ent. */
void SC2_HUD_WriteEnd(edict_t * ent);

/* Write a complete layout layer: start → tree → end. */
void SC2_HUD_WriteLayout(edict_t * ent, sc2BaseFrame_t const * frames, uint32_t count,
                         sc2BaseFrame_t const * root, uint32_t layer);

/* Wire gi file I/O into the layout parser — call once from SC2_Init. */
void SC2_HUD_InitLayoutHost(void);

/* Load the game UI layout exactly once (idempotent after first call).
 * Returns the flat frame array and sets *count.  Both return NULL/0 if
 * loading failed.  All panel modules must call this instead of invoking
 * SC2_LayoutBuildGameUI() directly — there is one shared sc2_layout
 * global, so each independent call would wipe the previous load. */
sc2BaseFrame_t *SC2_HUD_EnsureLayout(uint32_t *count);

/* Per-frame HUD writers called from G_RunFrame */
void SC2_HUD_WriteResourcePanel(edict_t * ent);
void SC2_HUD_WriteConsolePanel(edict_t * ent);
void SC2_HUD_PrepareCommandPanel(sc2BaseFrame_t *frames, uint32_t count, sc2BaseFrame_t *root);

/* Set the model index used by FT_PORTRAIT frames on the next console write.
 * Call before SC2_HUD_WriteConsolePanel when a unit is selected. */
void SC2_HUD_SetPortraitModel(RESOURCE model);

#endif /* SC2_HUD_H */
