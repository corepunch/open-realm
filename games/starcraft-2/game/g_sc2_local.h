#ifndef G_SC2_LOCAL_H
#define G_SC2_LOCAL_H

#include "common/common.h"
#include "server/server.h"
#include "games/starcraft-2/common/sc2_map.h"
#include "games/warcraft-3/jass/jass_api.h"
#include "games/starcraft-2/game/galaxy/galaxy_host.h"

#define SC2_MAX_CLIENTS 1
/* TRaynor01 alone places 2657 map objects; leave headroom above that for
 * cinematic/galaxy-spawned units (dropship, cargo, later Init0xUnits triggers)
 * so SC2_GalaxyCreateUnit never silently starves out of edicts. */
#define SC2_MAX_EDICTS  4096

extern struct game_import gi;
extern struct game_export globals;

/* Level-local state for the Galaxy VM and cinematic system. */
typedef struct {
    vector2_t origin;
    vector3_t angles;
    float distance, fov;
} sc2Camera_t;



typedef struct {
    jass_t * vm;
    bool   scriptsStarted;
    float  cinefade;       /* 0=clear … 1=fully black (written to client ps.cinefade) */
    bool   cinematic;      /* true while cinematic bars/overlay is active */
    struct {
        sc2Camera_t old, state;
        uint32_t start_time, end_time;
        uint8_t log_stage;
    } camera;
} sc2Level_t;

extern sc2Level_t sc2_level;

int          G_RegisterModel(cstring_t filename);
animation_t const * G_GetAnimation(uint32_t modelindex, cstring_t animname);
void         G_FreeModels(void);

/* HUD declarations are in hud/hud.h; include that separately in .c files. */
#endif
