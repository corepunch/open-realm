#ifndef SC2_HUD_LOCAL_H
#define SC2_HUD_LOCAL_H

#include "../g_sc2_local.h"
#include "games/starcraft-2/common/stb_sc2layout.h"

/* Frame-write primitives (hud.c) */
extern DWORD ui_next_frame_number;

void SC2_WriteStart(DWORD layer);
void SC2_WriteEnd(LPEDICT ent);
void SC2_WriteFrame(LPSC2FRAMEDEF frame);
void SC2_WriteFrameWithChildren(LPSC2FRAMEDEF frame);

/* Panel modules: write into an already-started svc_layout message */
void SC2_WriteResourcePanelFrames(void);
void SC2_WriteMinimapFrame(void);

/* Combined console layout (hud.c) */
void SC2_WriteConsoleLayout(LPEDICT ent);

#endif /* SC2_HUD_LOCAL_H */
