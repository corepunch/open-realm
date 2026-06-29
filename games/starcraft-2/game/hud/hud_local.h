#ifndef SC2_HUD_LOCAL_H
#define SC2_HUD_LOCAL_H

#include "../g_sc2_local.h"
#include "games/starcraft-2/common/stb_sc2layout.h"

/* HUD font sizes */
#define HUD_FONT_SIZE 10

/* Frame-write primitives (hud.c) */
extern DWORD ui_next_frame_number;

void SC2_WriteStart(DWORD layer);
void SC2_WriteEnd(LPEDICT ent);
void SC2_WriteFrame(LPSC2FRAMEDEF frame);
void SC2_WriteFrameWithChildren(LPSC2FRAMEDEF frame);
void SC2_WriteLayout(LPEDICT ent, LPSC2FRAMEDEF root, DWORD layer);

/* Panel modules */
void SC2_WriteResourcePanel(LPEDICT ent);
void SC2_WriteMinimapFrame(LPEDICT ent);

#endif /* SC2_HUD_LOCAL_H */
