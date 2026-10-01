#ifndef G_SC2_LOCAL_H
#define G_SC2_LOCAL_H

#include "common/common.h"
#include "server/game.h"
#include "server/routing.h"
#include "games/starcraft-2/common/sc2_map.h"
#include "games/warcraft-3/jass/jass_api.h"
#include "games/starcraft-2/game/galaxy/galaxy_host.h"

#define SC2_MAX_CLIENTS 1
/* TRaynor01 alone places 2657 map objects; leave headroom above that for
 * cinematic/galaxy-spawned units (dropship, cargo, later Init0xUnits triggers)
 * so SC2_GalaxyCreateUnit never silently starves out of edicts. */
#define SC2_MAX_EDICTS  4096

/* Movement is simulation state, owned by g_sc2.c like WC3's edict movement fields. */
typedef struct {
    bool moving;
    bool mobile;
    bool flying;
    vec2_t target;
    routePath_t path;
    float speed, height;
    animation_t const *anim;
    uint32_t animtime;
} sc2MoveState_t;

struct client_s {
    player_t ps;
    uint64_t hud_hash;
    uint32_t pending_order;
    bool minimap_signal;
};

/* Like WC3 g_local.h, the game owns the full edict and the server only sees the leading fields.
 * Per-unit simulation state lives here instead of in arrays indexed by edict number. */
struct edict_s {
    entityState_t s;
    gameClient_t *client;
    pathTex_t *pathtex;
    float collision;
    box2_t bounds;
    uint32_t svflags;
    uint32_t selected;
    uint32_t areanum;
    link_t area;
    bool inuse;
    box2_t areabounds;

    // keep above in sync with server.h
    sc2UnitState_t unit; /* vitals, flags, catalog links; Galaxy natives read it through sc2_galaxy_unit_state */
    sc2MoveState_t move;
    struct { uint32_t kind, target, next_attack; vec2_t origin, destination; bool returning; } order;
};

extern struct game_import gi;
extern struct game_export globals;

/* Level-local state for the Galaxy VM and cinematic system. */
typedef struct {
    vec2_t origin;
    vec3_t angles;
    float distance, fov;
} sc2Camera_t;



typedef struct {
    jass_t *vm;
    bool   scriptsStarted;
    uint32_t selection_dirty; /* player bits whose selection a Galaxy script changed this frame */
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
animation_t const *G_GetAnimation(uint32_t modelindex, cstring_t animname);
void         G_FreeModels(void);

bool SC2_CommandSupported(cstring_t ability, cstring_t command);
void SC2_CommandButton(edict_t *client, cstring_t abilcmd);
bool SC2_CommandPoint(edict_t *client, vec2_t const *point);
bool SC2_CommandTarget(edict_t *client, uint32_t target);
void SC2_RunOrders(edict_t *unit);
void SC2_CancelCommand(edict_t *client);
void SC2_SetUnitAnimation(edict_t *unit, cstring_t name);
void SC2_OrderMove(edict_t *unit, vec2_t const *target);
void SC2_StopUnit(edict_t *unit);
void SC2_UpdateUnit(edict_t *unit);
edict_t *SC2_SelectedUnit(edict_t const *client);

/* HUD declarations are in hud/hud.h; include that separately in .c files. */
#endif
