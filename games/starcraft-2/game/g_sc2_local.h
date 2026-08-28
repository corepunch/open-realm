#ifndef G_SC2_LOCAL_H
#define G_SC2_LOCAL_H

#include "common/common.h"
#include "server/server.h"
#include "games/starcraft-2/common/sc2_map.h"
#include "games/warcraft-3/jass/jass_api.h"
#include "games/starcraft-2/game/galaxy/galaxy_host.h"

#define SC2_MAX_CLIENTS 1
#define SC2_MAX_EDICTS  2048

extern struct game_import gi;
extern struct game_export globals;

/* Level-local state for the Galaxy VM and cinematic system. */
typedef struct {
    VECTOR2 origin;
    VECTOR3 angles;
    FLOAT distance, fov;
} SC2CAMERA;
typedef SC2CAMERA *LPSC2CAMERA;
typedef SC2CAMERA const *LPCSC2CAMERA;

typedef struct {
    LPJASS vm;
    BOOL   scriptsStarted;
    FLOAT  cinefade;       /* 0=clear … 1=fully black (written to client ps.cinefade) */
    BOOL   cinematic;      /* true while cinematic bars/overlay is active */
    struct {
        SC2CAMERA old, state;
        DWORD start_time, end_time;
        BYTE log_stage;
    } camera;
} sc2Level_t;

extern sc2Level_t sc2_level;

LPCANIMATION G_GetAnimation(DWORD modelindex, LPCSTR animname);
void         G_FreeModels(void);

/* HUD declarations are in hud/hud.h; include that separately in .c files. */
#endif
