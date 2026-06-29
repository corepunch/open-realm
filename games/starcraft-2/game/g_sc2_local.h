#ifndef G_SC2_LOCAL_H
#define G_SC2_LOCAL_H

#include "common/common.h"
#include "server/server.h"
#include "games/starcraft-2/common/sc2_map.h"

#define SC2_MAX_CLIENTS 1
#define SC2_MAX_EDICTS  2048

extern struct game_import gi;
extern struct game_export globals;

LPCANIMATION G_GetAnimation(DWORD modelindex, LPCSTR animname);
void         G_FreeModels(void);

/* HUD (hud/hud.c) */
void SC2_HudInit(void);
void SC2_HudShutdown(void);
void SC2_WriteResourcePanel(LPEDICT ent);
void SC2_WriteMinimapFrame(LPEDICT ent);
#endif
