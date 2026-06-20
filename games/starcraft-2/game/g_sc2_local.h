#ifndef G_SC2_LOCAL_H
#define G_SC2_LOCAL_H

#include "common/common.h"
#include "server/game.h"

#define SC2_MAX_CLIENTS      1
#define SC2_MAX_EDICTS       2048
#define SC2_MAX_WAYPOINTS    256
#define SC2_MAX_COLLIDERS    256
#define SC2_MAX_SELECTED     64

#define SC2_MOVE_SPEED       6.0f
#define SC2_ARRIVE_TOLERANCE 4.0f
#define SC2_BLOCKED_FRAMES   8
#define SC2_SETTLE_FRAMES    4
#define SC2_SLOT_MARGIN      8.0f
#define SC2_MIN_SLOT_SPACING 16.0f
#define SC2_NAVI_THRESHOLD   128.0f

#define SAFE_CALL(FUNC, ...) if (FUNC) FUNC(__VA_ARGS__)

extern struct game_import gi;
extern struct game_export globals;

struct client_s {
    PLAYER ps;
    int ping;
};

struct edict_s {
    entityState_t s;
    LPGAMECLIENT client;
    pathTex_t *pathtex;
    FLOAT collision;
    BOX2 bounds;
    DWORD svflags;
    DWORD selected;
    DWORD areanum;
    LINK area;
    BOOL inuse;
    BOX2 areabounds;
    void (*idle)(LPEDICT);
    void (*move)(LPEDICT);
    void (*run)(LPEDICT);
    void (*attack)(LPEDICT);
    void (*pain)(LPEDICT);

    DWORD class_id;
    DWORD heatmap2;
    BOOL mobile;
    LPEDICT goalentity;
    LPEDICT secondarygoal;
    FLOAT move_speed;
    VECTOR2 move_last_origin;
    FLOAT move_last_distance;
    DWORD move_blocked_frames;
};

LPCANIMATION G_GetAnimation(DWORD modelindex, LPCSTR animname);
void         G_FreeModels(void);
LPEDICT       Waypoint_add(LPCVECTOR2 spot);
FLOAT         SC2_DistanceToGoal(LPEDICT ent);
FLOAT         SC2_UnitMoveDistance(LPEDICT ent);
void          SC2_UnitChangeAngle(LPEDICT self);
void          SC2_UnitMoveInDirection(LPEDICT self);
void          SC2_PushEntity(LPEDICT ent, FLOAT distance, LPCVECTOR2 direction);

#endif
