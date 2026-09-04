#include "g_sc2_local.h"
#include "games/starcraft-2/common/sc2_map.h"
#include "games/starcraft-2/game/hud/hud.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

sc2Level_t sc2_level;

#define SC2_MOVE_SPEED  6.0f
#define SC2_MOVE_CLOSE  4.0f
#define SC2_MOVE_EPS    0.25f
#define SC2_MAX_COLLIDERS 256

struct game_import gi;
struct game_export globals;

static edict_t sc2_edicts[SC2_MAX_EDICTS];
static struct client_s sc2_clients[SC2_MAX_CLIENTS];
static edict_t sc2_waypoints[SC2_MAX_EDICTS];

typedef struct {
    BOOL moving;
    BOOL mobile;
    BOOL flying;
    BOOL suppress_next_point;
    VECTOR2 target;
    FLOAT speed, height;
} sc2MoveState_t;

static sc2MoveState_t sc2_move[SC2_MAX_EDICTS];

static BOOL SC2_ObjectIsMobile(sc2MapObject_t const *object) {
    if (!object || object->type != SC2_OBJECT_UNIT) {
        return false;
    }
    if (object->mover[0]) {
        return strcasecmp(object->mover, "None") &&
               strcasecmp(object->mover, "Stationary");
    }
    if (object->unit_flags & SC2_UNIT_FLAG_MOVABLE) {
        return true;
    }
    if (strstr(object->name, "CommandCenter") || strstr(object->model, "CommandCenter")) {
        return false;
    }
    return true;
}

static FLOAT SC2_ObjectSpawnZ(sc2MapObject_t const *object, BOOL flying) {
    FLOAT terrain;

    if (!object) {
        return 0.0f;
    }
    if (object->flags & SC2_OBJECT_HEIGHT_ABSOLUTE) {
        return object->position.z + (flying ? object->move_height : 0.0f);
    }
    terrain = flying ? SC2_MapAirHeightAtPoint(object->position.x, object->position.y) :
                       SC2_MapHeightAtPoint(object->position.x, object->position.y);
    return terrain + object->position.z + (flying ? object->move_height : 0.0f);
}

static FLOAT SC2_ObjectRadius(sc2MapObject_t const *object) {
    if (!object) {
        return 0.0f;
    }
    if (object->radius > 0.0f) {
        return object->radius;
    }
    return object->type == SC2_OBJECT_UNIT ? 1.0f : 0.5f;
}

static FLOAT SC2_ObjectCollisionRadius(sc2MapObject_t const *object, FLOAT radius) {
    if (!object) {
        return radius;
    }
    if (!SC2_ObjectIsMobile(object) && object->footprint_radius > 0.0f) {
        return object->footprint_radius;
    }
    return radius;
}

static DWORD SC2_EdictNumber(LPCEDICT ent) {
    if (!ent || ent < sc2_edicts || ent >= sc2_edicts + SC2_MAX_EDICTS) {
        return SC2_MAX_EDICTS;
    }
    return (DWORD)(ent - sc2_edicts);
}

/* Flying movers retain their catalog-authored clearance while crossing terrain tiers. */
static void SC2_LinkUnit(LPEDICT ent) {
    DWORD number = SC2_EdictNumber(ent);
    FLOAT terrain = sc2_move[number].flying ? SC2_MapAirHeightAtPoint(ent->s.origin2.x, ent->s.origin2.y) :
                                              SC2_MapHeightAtPoint(ent->s.origin2.x, ent->s.origin2.y);

    ent->s.origin.x = ent->s.origin2.x;
    ent->s.origin.y = ent->s.origin2.y;
    ent->s.origin.z = sc2_unit_world_height(terrain, sc2_move[number].height, sc2_move[number].flying);
    gi.LinkEntity(ent);
}

static DWORD SC2_ClientPlayer(LPCEDICT ent) {
    return ent && ent->client ? ent->client->ps.number : 1;
}

static BOOL SC2_IsSelectable(LPCEDICT ent, DWORD player) {
    DWORD number = SC2_EdictNumber(ent);

    return number < SC2_MAX_EDICTS &&
        ent->inuse &&
        ent->s.model &&
        ent->s.player == player &&
        sc2_move[number].mobile;
}

static void SC2_Select(LPEDICT clent, DWORD argc, LPCSTR argv[]) {
    DWORD player = SC2_ClientPlayer(clent);
    DWORD client_number = SC2_EdictNumber(clent);
    BOOL cleared = false;

    if (argc == 2 && client_number < SC2_MAX_EDICTS) {
        sc2_move[client_number].suppress_next_point = true;
    }
    for (DWORD i = 1; i < argc; i++) {
        DWORD number = (DWORD)atoi(argv[i]);
        if (number >= (DWORD)globals.num_edicts || !SC2_IsSelectable(&sc2_edicts[number], player)) {
            continue;
        }
        if (!cleared) {
            FOR_LOOP(j, globals.num_edicts) {
                sc2_edicts[j].selected &= ~(1 << player);
            }
            cleared = true;
        }
        sc2_edicts[number].selected |= 1 << player;
    }
}

static void SC2_StopUnit(LPEDICT ent) {
    DWORD number = SC2_EdictNumber(ent);
    if (number >= SC2_MAX_EDICTS) {
        return;
    }
    sc2_move[number].moving = false;
    ent->s.frame = gi.GetTime();
    ent->s.ability = 0;
}

static void SC2_OrderMove(LPEDICT ent, LPCVECTOR2 target) {
    DWORD number = SC2_EdictNumber(ent);
    VECTOR2 pathable = *target;

    if (number >= SC2_MAX_EDICTS || !sc2_move[number].mobile) {
        return;
    }
    CM_ClosestPathablePointForRadius(target, ent->collision, &pathable);
    sc2_move[number].target = pathable;
    sc2_move[number].moving = true;
    sc2_move[number].speed = SC2_MOVE_SPEED;
    sc2_waypoints[number].s.origin2 = pathable;
    sc2_waypoints[number].s.origin.z = SC2_MapHeightAtPoint(pathable.x, pathable.y);
    ent->s.frame = gi.GetTime();
    ent->s.ability = 1;
    CM_InvalidatePathCache();
}

static void SC2_MoveSelected(LPEDICT clent, LPCVECTOR2 target) {
    DWORD player = SC2_ClientPlayer(clent);
    BOOL issued = false;

    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &sc2_edicts[i];
        if (!(ent->selected & (1 << player)) || !SC2_IsSelectable(ent, player)) {
            continue;
        }
        SC2_OrderMove(ent, target);
        issued = true;
    }
    if (!issued) {
        return;
    }
    gi.Write(PF_BYTE, &(LONG){svc_temp_entity});
    gi.Write(PF_BYTE, &(LONG){TE_MOVE_CONFIRMATION});
    gi.Write(PF_POSITION, &(VECTOR3){target->x, target->y, 0});
    gi.unicast(clent);
}

static void SC2_MoveToTargetEntity(LPEDICT clent, DWORD target_number) {
    if (target_number >= (DWORD)globals.num_edicts || !sc2_edicts[target_number].inuse) {
        return;
    }
    SC2_MoveSelected(clent, &sc2_edicts[target_number].s.origin2);
}

static void SC2_RunUnit(LPEDICT ent) {
    DWORD number = SC2_EdictNumber(ent);
    VECTOR2 to_goal;
    VECTOR2 dir;
    FLOAT dist;
    FLOAT step;

    if (number >= SC2_MAX_EDICTS || !sc2_move[number].moving) {
        return;
    }
    to_goal = Vector2_sub(&sc2_move[number].target, &ent->s.origin2);
    dist = Vector2_len(&to_goal);
    step = sc2_move[number].speed * (FRAMETIME / 1000.0f);
    if (dist <= step + SC2_MOVE_EPS) {
        ent->s.origin2 = sc2_move[number].target;
        SC2_LinkUnit(ent);
        SC2_StopUnit(ent);
        return;
    }
    if (sc2_move[number].flying || dist <= SC2_MOVE_CLOSE) {
        dir = to_goal;
    } else {
        DWORD heatmap = CM_BuildHeatmap(&sc2_waypoints[number]);
        dir = get_flow_direction(heatmap, ent->s.origin.x, ent->s.origin.y);
        if (Vector2_len(&dir) <= 0.001f) {
            dir = to_goal;
        }
    }
    Vector2_normalize(&dir);
    ent->s.angle = atan2f(dir.y, dir.x);
    ent->s.origin2 = Vector2_mad(&ent->s.origin2, step, &dir);
    SC2_LinkUnit(ent);
}

static BOOL sc2_collision_filter(LPCEDICT ent) {
    return ent && ent->inuse && ent->s.model && ent->collision > 0;
}

static void SC2_PushEntity(LPEDICT ent, FLOAT distance, LPCVECTOR2 dir) {
    ent->s.origin2 = Vector2_mad(&ent->s.origin2, distance, dir);
    SC2_LinkUnit(ent);
}

static void SC2_SolveCollisions(void) {
    LPEDICT colliders[SC2_MAX_COLLIDERS];

    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT a = &sc2_edicts[i];
        if (!sc2_move[i].mobile || sc2_move[i].flying || !a->inuse || a->collision <= 0) {
            continue;
        }
        DWORD num = gi.BoxEdicts(&a->bounds, colliders, SC2_MAX_COLLIDERS, sc2_collision_filter);
        FOR_LOOP(j, num) {
            LPEDICT b = colliders[j];
            DWORD bnum = SC2_EdictNumber(b);
            FLOAT radius;
            FLOAT distance;
            VECTOR2 d;

            if (b == a) {
                continue;
            }
            if (bnum < SC2_MAX_EDICTS && sc2_move[bnum].mobile && bnum >= i) {
                continue;
            }
            radius = a->collision + b->collision;
            distance = Vector2_distance(&a->s.origin2, &b->s.origin2);
            if (distance >= radius || distance <= 0.001f) {
                continue;
            }
            d = Vector2_sub(&a->s.origin2, &b->s.origin2);
            Vector2_normalize(&d);
            if (bnum < SC2_MAX_EDICTS && sc2_move[bnum].mobile) {
                SC2_PushEntity(a, (radius - distance) * 0.5f, &d);
                SC2_PushEntity(b, -(radius - distance) * 0.5f, &d);
            } else {
                SC2_PushEntity(a, radius - distance, &d);
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Galaxy callbacks — bridge between galaxy_host.c natives and SC2 game state.
 * ------------------------------------------------------------------------- */
/* playerState.viewangles is ROTATE_ZYX {pitch, roll, yaw}; SC2CAMERA.angles is {pitch, yaw, height}. */
static VECTOR3 SC2_ViewAngles(LPCVECTOR3 camera_angles) {
    return SC2_EulerFromCamera(camera_angles->x, camera_angles->y);
}

static VECTOR3 SC2_CameraAnglesFromPlayer(LPCPLAYER ps) {
    return SC2_CameraFromEuler(&ps->viewangles, ps->vieworigin.z - SC2_MapCameraHeightAtPoint(ps->vieworigin.x, ps->vieworigin.y));
}

static void SC2_WriteCamera(LPCVECTOR2 origin, LPCVECTOR3 angles, FLOAT distance, FLOAT fov) {
    gameCamera_t defaults;

    CL_GameDefaultCamera(&defaults);
    defaults.fov = fov;
    FOR_LOOP(i, SC2_MAX_CLIENTS) {
        sc2_clients[i].ps.vieworigin = (VECTOR3){
            origin->x, origin->y, SC2_MapCameraHeightAtPoint(origin->x, origin->y) + angles->z
        };
        sc2_clients[i].ps.viewangles = SC2_ViewAngles(angles);
        sc2_clients[i].ps.distance = distance;
        player_set_lens(&sc2_clients[i].ps, &defaults);
    }
}

static FLOAT SC2_CameraEyeZ(LPCSC2CAMERA camera) {
    return CM_GetHeightAtPoint(camera->origin.x, camera->origin.y) + camera->angles.z +
           sinf((FLOAT)DEG2RAD(camera->angles.x)) * camera->distance;
}

/* CameraApplyInfo durations author server snapshots; clients only smooth between those samples. */
static void SC2_UpdateCamera(void) {
    DWORD now = gi.GetTime();
    DWORD duration = sc2_level.camera.end_time - sc2_level.camera.start_time;
    LPSC2CAMERA a = &sc2_level.camera.old, b = &sc2_level.camera.state;
    SC2CAMERA cur;
    FLOAT k;

    if (!duration || now >= sc2_level.camera.end_time) {
        SC2_WriteCamera(&b->origin, &b->angles, b->distance, b->fov);
        if (duration && sc2_level.camera.log_stage < 2) {
#ifdef SC2_DEBUG_CUTSCENE
            FLOAT ground = CM_GetHeightAtPoint(b->origin.x, b->origin.y), eye_z = SC2_CameraEyeZ(b);
            fprintf(stderr, "SC2 camera move: end t=1.00 target=(%.2f %.2f) terrain=%.2f eye_z=%.2f clearance=%.2f pitch=%.2f yaw=%.2f dist=%.2f\n",
                b->origin.x, b->origin.y, ground, eye_z, eye_z - ground, b->angles.x, b->angles.y, b->distance);
#endif
            sc2_level.camera.log_stage = 2;
        }
        return;
    }
    k = (now - sc2_level.camera.start_time) / (FLOAT)duration;
    cur.origin = Vector2_lerp(&a->origin, &b->origin, k);
    cur.angles = (VECTOR3){ LerpNumber(a->angles.x, b->angles.x, k), SC2_LerpDegrees(a->angles.y, b->angles.y, k),
                            LerpNumber(a->angles.z, b->angles.z, k) };
    cur.distance = LerpNumber(a->distance, b->distance, k);
    cur.fov = LerpNumber(a->fov, b->fov, k);
    SC2_WriteCamera(&cur.origin, &cur.angles, cur.distance, cur.fov);
    if (k >= 0.5f && !sc2_level.camera.log_stage) {
#ifdef SC2_DEBUG_CUTSCENE
        FLOAT ground = CM_GetHeightAtPoint(cur.origin.x, cur.origin.y), eye_z = SC2_CameraEyeZ(&cur);
        fprintf(stderr, "SC2 camera move: mid t=%.2f target=(%.2f %.2f) terrain=%.2f eye_z=%.2f clearance=%.2f pitch=%.2f yaw=%.2f dist=%.2f\n",
            k, cur.origin.x, cur.origin.y, ground, eye_z, eye_z - ground, cur.angles.x, cur.angles.y, cur.distance);
#endif
        sc2_level.camera.log_stage = 1;
    }
}

static void SC2_GalaxySetCamera(float target_x, float target_y,
                                float yaw, float pitch,
                                float dist, float fov, float height_offset, float duration) {
    SC2_UpdateCamera();
    sc2_level.camera.old = (SC2CAMERA){
        { sc2_clients[0].ps.vieworigin.x, sc2_clients[0].ps.vieworigin.y },
        SC2_CameraAnglesFromPlayer(&sc2_clients[0].ps),
        sc2_clients[0].ps.distance, sc2_clients[0].ps.fov };
    sc2_level.camera.state = (SC2CAMERA){ { target_x, target_y }, { pitch, yaw, height_offset }, dist, fov };
    sc2_level.camera.start_time = gi.GetTime();
    sc2_level.camera.end_time = sc2_level.camera.start_time + (DWORD)(MAX(0.0f, duration) * 1000.0f);
    sc2_level.camera.log_stage = duration > 0.0f ? 0 : 2;
    SC2_UpdateCamera();
#ifdef SC2_DEBUG_CUTSCENE
    if (duration > 0.0f) {
        FLOAT ground = CM_GetHeightAtPoint(sc2_level.camera.old.origin.x, sc2_level.camera.old.origin.y);
        FLOAT eye_z = SC2_CameraEyeZ(&sc2_level.camera.old);
        fprintf(stderr, "SC2 camera move: start t=0.00 target=(%.2f %.2f) terrain=%.2f eye_z=%.2f clearance=%.2f -> target=(%.2f %.2f) duration=%.2f\n",
                sc2_level.camera.old.origin.x, sc2_level.camera.old.origin.y, ground, eye_z,
            eye_z - ground, target_x, target_y, duration);
        fprintf(stderr, "SC2_GalaxySetCamera: target=(%.1f,%.1f) yaw=%.1f pitch=%.1f dist=%.1f fov=%.1f height=%.2f duration=%.2f\n",
            target_x, target_y, yaw, pitch, dist, fov, height_offset, duration);
    }
#endif
}

static void SC2_GalaxyCinematicMode(BOOL enable, float duration) {
    (void)duration;
    sc2_level.cinematic = enable;
    FOR_LOOP(i, SC2_MAX_CLIENTS)
        sc2_clients[i].ps.client_ui_state = enable ? CLIENT_UI_CINEMATIC : CLIENT_UI_GAME;
}

static void SC2_GalaxyCinematicFade(float alpha, float duration) {
    (void)duration; /* TODO: interpolated fade */
    sc2_level.cinefade = alpha;
    FOR_LOOP(i, SC2_MAX_CLIENTS)
        sc2_clients[i].ps.cinefade = alpha;
}

/* Camera lookup: find SC2_OBJECT_CAMERA in the loaded map by integer ID. */
static BOOL SC2_GalaxyGetCameraById(DWORD map_id,
    float *tx, float *ty, float *tz,
    float *pitch, float *yaw, float *dist, float *fov, float *height_offset) {
    sc2Map_t const *map = SC2_MapCurrent();
    if (!map) return false;
    FOR_LOOP(i, map->num_objects) {
        sc2MapObject_t const *obj = &map->objects[i];
        if (obj->type == SC2_OBJECT_CAMERA && obj->id == map_id) {
            *tx = obj->camera.target.x;  *ty = obj->camera.target.y;  *tz = obj->camera.target.z;
            *pitch = obj->camera.pitch;  *yaw = obj->camera.yaw;
            *dist  = obj->camera.distance;  *fov = obj->camera.fov;
            *height_offset = obj->camera.height_offset;
            fprintf(stderr, "SC2_GalaxyGetCameraById: id=%u target=(%.1f,%.1f,%.1f) pitch=%.1f yaw=%.1f dist=%.1f fov=%.1f\n",
                    map_id, *tx, *ty, *tz, *pitch, *yaw, *dist, *fov);
            return true;
        }
    }
    fprintf(stderr, "SC2_GalaxyGetCameraById: id=%u not found in map\n", map_id);
    return false;
}

/* Point lookup: find SC2_OBJECT_POINT in the loaded map by integer ID. */
static BOOL SC2_GalaxyGetPointById(DWORD map_id, float *x, float *y) {
    sc2Map_t const *map = SC2_MapCurrent();
    if (!map) return false;
    FOR_LOOP(i, map->num_objects) {
        sc2MapObject_t const *obj = &map->objects[i];
        if (obj->type == SC2_OBJECT_POINT && obj->id == map_id) {
            *x = obj->position.x;  *y = obj->position.y;
            return true;
        }
    }
    fprintf(stderr, "SC2_GalaxyGetPointById: id=%u not found\n", map_id);
    return false;
}

/* Unit model lookup: prefer an already-resolved M3 path from an existing map
 * object with a matching type_name, then fall back to the persistent unit
 * catalog for units created purely at runtime (e.g. cinematic UnitCreate). */
static const char *SC2_GalaxyGetUnitModel(LPCSTR unit_type) {
    sc2Map_t const *map = SC2_MapCurrent();
    LPCSTR catalog_model;
    if (!unit_type || !*unit_type) return "";
    if (map) {
        FOR_LOOP(i, map->num_objects) {
            sc2MapObject_t const *obj = &map->objects[i];
            if (obj->type == SC2_OBJECT_UNIT && obj->model[0] &&
                (!strcasecmp(obj->type_name, unit_type) || !strcasecmp(obj->name, unit_type))) {
                fprintf(stderr, "SC2_GalaxyGetUnitModel: %s -> %s\n", unit_type, obj->model);
                return obj->model;
            }
        }
    }
    catalog_model = SC2_MapResolveUnitModel(unit_type);
    if (catalog_model && catalog_model[0]) {
        fprintf(stderr, "SC2_GalaxyGetUnitModel: %s -> %s (catalog)\n", unit_type, catalog_model);
        return catalog_model;
    }
    fprintf(stderr, "SC2_GalaxyGetUnitModel: no model found for '%s'\n", unit_type);
    return "";
}

/* Unit entity operations. */
static void SC2_GalaxyUnitSetPosition(void *ent_ptr, float x, float y, float facing) {
    LPEDICT ent = (LPEDICT)ent_ptr;
    if (!ent || !ent->inuse) return;
    if (x == x) { /* NaN check: NaN != NaN, so x==x is false only for NaN → skip position */
        ent->s.origin2 = (VECTOR2){ x, y };
        SC2_LinkUnit(ent);
    }
    ent->s.angle = facing;
}

static BOOL SC2_GalaxyUnitIsAlive(void *ent_ptr) {
    LPEDICT ent = (LPEDICT)ent_ptr;
    return ent && ent->inuse;
}

static void SC2_GalaxyUnitMove(void *ent_ptr, float x, float y) {
#ifdef SC2_DEBUG_CUTSCENE
    LPEDICT ent = (LPEDICT)ent_ptr;
    fprintf(stderr, "SC2 dropship/order move: unit=%u from=(%.2f,%.2f,%.2f) to=(%.2f,%.2f)\n",
            (unsigned)SC2_EdictNumber(ent), ent ? ent->s.origin.x : 0.0f, ent ? ent->s.origin.y : 0.0f,
            ent ? ent->s.origin.z : 0.0f, x, y);
#endif
    SC2_OrderMove((LPEDICT)ent_ptr, &(VECTOR2){ x, y });
}

static BOOL SC2_GalaxyUnitIsMoving(void *ent_ptr) {
    LPEDICT ent = (LPEDICT)ent_ptr;
    DWORD number = ent ? SC2_EdictNumber(ent) : SC2_MAX_EDICTS;
    return number < SC2_MAX_EDICTS && sc2_move[number].moving;
}

static void SC2_GalaxyPlaySound(LPCSTR sound_id, int asset) {
    LPCSTR path = SC2_MapResolveSound(sound_id, asset);
    if (!path || !*path) return;
    sc2_edicts[0].s.sound = gi.SoundIndex(path);
    sc2_edicts[0].s.event = EV_MOVE;
    fprintf(stderr, "SC2_GalaxyPlaySound: %s -> %s\n", sound_id, path);
}

/* Galaxy-created units use the same catalog-derived render and movement state as map units. */
static void *SC2_GalaxyCreateUnit(LPCSTR unit_type, int player, float x, float y, float angle) {
    sc2MapObject_t object;
    LPCSTR model;
    if (!SC2_MapResolveUnit(unit_type, &object)) {
        fprintf(stderr, "SC2_GalaxyCreateUnit: no catalog unit '%s'\n", unit_type ? unit_type : "(null)");
        return NULL;
    }
    model = object.model;
    if (globals.num_edicts >= globals.max_edicts) {
        fprintf(stderr, "SC2_GalaxyCreateUnit: FATAL: edict pool full (%u/%u) — galaxy unit '%s' "
                "(player=%d at %.1f,%.1f) DROPPED, will never render; raise SC2_MAX_EDICTS in g_sc2_local.h\n",
                (unsigned)globals.num_edicts, (unsigned)globals.max_edicts, unit_type, player, x, y);
        return NULL;
    }
    LPEDICT ent = &sc2_edicts[globals.num_edicts++];
    memset(ent, 0, sizeof(*ent));
    ent->inuse = true;
    ent->s.number = (DWORD)(ent - sc2_edicts);
    ent->s.origin.x = x;
    ent->s.origin.y = y;
    ent->s.origin2 = (VECTOR2){ x, y };
    ent->s.angle = angle;
    ent->s.scale = 1.0f;
    ent->s.player = (DWORD)player;
        ent->s.model = G_RegisterModel(model);
        ent->s.radius = SC2_ObjectRadius(&object);
        ent->collision = SC2_ObjectCollisionRadius(&object, ent->s.radius);
        if (SC2_ObjectIsMobile(&object)) ent->svflags |= SVF_MONSTER;
        sc2_move[ent->s.number].mobile = ent->svflags & SVF_MONSTER;
        sc2_move[ent->s.number].flying = !strcasecmp(object.mover, "Fly");
        sc2_move[ent->s.number].speed = SC2_MOVE_SPEED;
        sc2_move[ent->s.number].height = object.move_height;
    SC2_LinkUnit(ent);
            fprintf(stderr, "SC2_GalaxyCreateUnit: type=%s model=%s mover=%s mobile=%d collision=%.2f player=%d at (%.1f,%.1f)\n",
                    unit_type, model, object.mover, !!sc2_move[ent->s.number].mobile, ent->collision, player, x, y);
    return ent;
}

static void SC2_InitGalaxyHost(void) {
    sc2_galaxy_on_camera          = SC2_GalaxySetCamera;
    sc2_galaxy_on_cinematic       = SC2_GalaxyCinematicMode;
    sc2_galaxy_on_fade            = SC2_GalaxyCinematicFade;
    sc2_galaxy_sound_length       = SC2_MapSoundLength;
    sc2_galaxy_on_sound           = SC2_GalaxyPlaySound;
    sc2_galaxy_on_unit_create     = SC2_GalaxyCreateUnit;
    sc2_galaxy_get_camera_by_id   = SC2_GalaxyGetCameraById;
    sc2_galaxy_get_point_by_id    = SC2_GalaxyGetPointById;
    sc2_galaxy_get_unit_model     = SC2_GalaxyGetUnitModel;
    sc2_galaxy_unit_set_position  = SC2_GalaxyUnitSetPosition;
    sc2_galaxy_unit_move          = SC2_GalaxyUnitMove;
    sc2_galaxy_unit_is_moving     = SC2_GalaxyUnitIsMoving;
    sc2_galaxy_unit_is_alive      = SC2_GalaxyUnitIsAlive;
}

static void SC2_InitClients(void) {
    gameCamera_t camera;

    CL_GameDefaultCamera(&camera);
    FOR_LOOP(i, SC2_MAX_CLIENTS) {
        LPEDICT ent = &sc2_edicts[i];
        ent->inuse = true;
        ent->s.number = i;
        ent->client = &sc2_clients[i];
        ent->client->ps.number = i + 1;
        ent->client->ps.client_ui_state = CLIENT_UI_GAME;
        ent->client->ps.vieworigin = (VECTOR3){
            camera.target.x, camera.target.y,
            SC2_MapCameraHeightAtPoint(camera.target.x, camera.target.y) + camera.height_offset
        };
        ent->client->ps.distance = camera.distance;
        ent->client->ps.rdflags = RDF_NOFOG | RDF_NOFOGMASK;
        ent->client->ps.viewangles = (VECTOR3){ camera.pitch, 0.0f, camera.yaw };
        player_set_lens(&ent->client->ps, &camera);
    }
    sc2_level.camera.old = sc2_level.camera.state = (SC2CAMERA){ { camera.target.x, camera.target.y },
        SC2_CameraFromEuler(&sc2_clients[0].ps.viewangles, camera.height_offset), camera.distance, camera.fov };
    sc2_level.camera.start_time = sc2_level.camera.end_time = gi.GetTime();
    sc2_level.camera.log_stage = 2;
}

static void SC2_Init(void) {
    memset(sc2_edicts,  0, sizeof(sc2_edicts));
    memset(sc2_clients, 0, sizeof(sc2_clients));
    memset(sc2_move,    0, sizeof(sc2_move));
    memset(sc2_waypoints, 0, sizeof(sc2_waypoints));
    memset(&sc2_level, 0, sizeof(sc2_level));

    globals.edicts     = sc2_edicts;
    globals.num_edicts = SC2_MAX_CLIENTS;
    globals.max_edicts = SC2_MAX_EDICTS;
    globals.max_clients = SC2_MAX_CLIENTS;
    SC2_InitClients();
    SC2_HUD_InitLayoutHost();
    SC2_InitGalaxyHost();
}

static void SC2_Shutdown(void) {
    if (sc2_level.vm) { galaxy_close(sc2_level.vm); sc2_level.vm = NULL; }
    G_FreeModels();
    SC2_MapShutdown();
}

static void SC2_SpawnEntities(void);

static bool SC2_LoadMap(LPCSTR mapFilename) {
    if (!CM_LoadMap(mapFilename)) {
        return false;
    }
    gi.ApplyLobbySettings((LPMAPINFO)CM_GetMapInfo());
    gi.ClearWorld();
    SC2_SpawnEntities();
    /* Register HUD configstrings after memset(&sv,...) in SV_Map wipes them. */
    SC2_HUD_EnsureLayout(NULL);

    /* Open Galaxy VM and load map scripts. */
    if (sc2_level.vm) { galaxy_close(sc2_level.vm); sc2_level.vm = NULL; }
    sc2_level.scriptsStarted = false;
    /* SC2Map archives contain MapScript.galaxy below the map directory; use that
     * authoritative path before the development-only extracted-script fallback. */
    galaxy_set_script_dir(mapFilename);
    sc2_level.vm = galaxy_open(gi.ReadFile, gi.GetTime, gi.MemAlloc, gi.MemFree);
    if (sc2_level.vm)
        galaxy_start(sc2_level.vm);  /* registers triggers via InitMap() */
    return true;
}

static void SC2_SpawnEntities(void) {
    sc2Map_t const *map = SC2_MapCurrent();

    memset(sc2_edicts + globals.max_clients,
           0,
           sizeof(sc2_edicts) - sizeof(sc2_edicts[0]) * globals.max_clients);
    memset(sc2_move + globals.max_clients,
           0,
           sizeof(sc2_move) - sizeof(sc2_move[0]) * globals.max_clients);
    memset(sc2_waypoints, 0, sizeof(sc2_waypoints));
    SC2_InitClients();
    globals.num_edicts = globals.max_clients;

    FOR_LOOP(i, map->num_objects) {
        sc2MapObject_t const *object = &map->objects[i];
        LPEDICT ent;
        DWORD number;

        if (!object->model[0]) {
            continue;
        }
        if (globals.num_edicts >= globals.max_edicts) {
            fprintf(stderr, "SC2_SpawnEntities: FATAL: edict pool exhausted at %u/%u map objects "
                    "(SC2_MAX_EDICTS=%u too small) — %u remaining static objects AND every later "
                    "galaxy UnitCreate/UnitCargoCreate (cinematic units!) will be silently invisible; "
                    "raise SC2_MAX_EDICTS in g_sc2_local.h\n",
                    (unsigned)i, (unsigned)map->num_objects, (unsigned)globals.max_edicts,
                    (unsigned)(map->num_objects - i));
            break;
        }
        ent = &sc2_edicts[globals.num_edicts++];
        memset(ent, 0, sizeof(*ent));
        ent->inuse = true;
        ent->s.number = (DWORD)(ent - sc2_edicts);
        ent->s.class_id = SC2_MapObjectClassId(object);
        ent->s.origin = object->position;
        ent->s.angle = object->angle;
        ent->s.scale = object->scale > 0.0f ? object->scale : 1.0f;
        ent->s.radius = SC2_ObjectRadius(object);
        ent->s.player = object->player;
        ent->s.model = G_RegisterModel(object->model);
        ent->collision = SC2_ObjectCollisionRadius(object, ent->s.radius);
        if (SC2_ObjectIsMobile(object)) {
            ent->svflags |= SVF_MONSTER;
        }
        number = (DWORD)(ent - sc2_edicts);
        sc2_move[number].mobile = (ent->svflags & SVF_MONSTER) != 0;
        sc2_move[number].flying = !strcasecmp(object->mover, "Fly");
        sc2_move[number].speed = SC2_MOVE_SPEED;
        sc2_move[number].height = object->move_height;
        ent->s.origin.z = SC2_ObjectSpawnZ(object, sc2_move[number].flying);
        gi.LinkEntity(ent);
    }
    CM_BakeStaticObstacles();
}

static void SC2_RunFrame(void) {
    FOR_LOOP(i, globals.num_edicts) {
        sc2_edicts[i].s.event = EV_NONE;
        sc2_edicts[i].s.sound = 0;
    }
    /* Tick Galaxy VM — runs pending coroutines (cutscene waits, camera pans). */
    if (sc2_level.vm && sc2_level.scriptsStarted)
        galaxy_tick(sc2_level.vm);
    SC2_UpdateCamera();

#ifdef SC2_DEBUG_CUTSCENE
    {
        static DWORD trace_frame;
        LPEDICT dropship = sc2_gunit_n ? (LPEDICT)sc2_gunits[0] : NULL;
        trace_frame++;
        if (trace_frame % 5 == 0) {
            VECTOR3 const cam = sc2_clients[0].ps.viewangles;
            fprintf(stderr, "SC2 cutscene trace: frame=%u camera=(%.2f,%.2f) pitch=%.2f yaw=%.2f dist=%.2f height=%.2f",
                    (unsigned)trace_frame, sc2_clients[0].ps.vieworigin.x, sc2_clients[0].ps.vieworigin.y,
                    cam.x, cam.z, (double)sc2_clients[0].ps.distance,
                    sc2_clients[0].ps.vieworigin.z - SC2_MapCameraHeightAtPoint(sc2_clients[0].ps.vieworigin.x, sc2_clients[0].ps.vieworigin.y));
            if (dropship) {
                DWORD number = SC2_EdictNumber(dropship);
                fprintf(stderr, " dropship=(%.2f,%.2f,%.2f) moving=%d target=(%.2f,%.2f) flying=%d height=%.2f",
                        dropship->s.origin.x, dropship->s.origin.y, dropship->s.origin.z,
                        sc2_move[number].moving, sc2_move[number].target.x, sc2_move[number].target.y,
                        sc2_move[number].flying, sc2_move[number].height);
            }
            fputc('\n', stderr);
        }
    }
#endif

    FOR_LOOP(i, globals.num_edicts) {
        if (sc2_edicts[i].inuse)
            SC2_RunUnit(&sc2_edicts[i]);
    }
    SC2_SolveCollisions();
}

static void SC2_ClientBegin(LPEDICT ent) {
    DWORD number = SC2_EdictNumber(ent);

    if (number >= SC2_MAX_CLIENTS) {
        number = 0;
        ent = &sc2_edicts[0];
    }
    ent->client = &sc2_clients[number];
    ent->client->ps.client_ui_state = CLIENT_UI_GAME;

    /* Use the first selectable unit's model for the portrait panel. */
    /* Resolve map player: SC2 mission maps assign the human player a specific
     * slot index (e.g. player 2 in TRaynor01) that differs from the lobby
     * client index (ps.number = i+1 from SC2_InitClients).  Find the player
     * number that owns the most mobile units — that is the controllable player.
     * Player 0 (neutral) is excluded. */
    DWORD player_counts[16] = {0};
    for (DWORD i = SC2_MAX_CLIENTS; i < (DWORD)globals.num_edicts; i++) {
        LPEDICT u = &sc2_edicts[i];
        if (u->inuse && u->s.model && sc2_move[i].mobile && u->s.player > 0 && u->s.player < 16)
            player_counts[u->s.player]++;
    }
    DWORD map_player = 0, best_count = 0;
    for (DWORD p = 1; p < 16; p++)
        if (player_counts[p] > best_count) { best_count = player_counts[p]; map_player = p; }
    if (map_player > 0 && map_player != (DWORD)ent->client->ps.number) {
        fprintf(stderr, "SC2_ClientBegin: remapping client ps.number %u → %u (most units)\n",
                ent->client->ps.number, map_player);
        ent->client->ps.number = (int)map_player;
    }

    DWORD client_player = SC2_ClientPlayer(ent);

    /* Pre-select the first selectable unit so InfoPanel shows unit info. */
    for (DWORD i = SC2_MAX_CLIENTS; i < (DWORD)globals.num_edicts; i++) {
        LPEDICT u = &sc2_edicts[i];
        if (!SC2_IsSelectable(u, client_player)) continue;
        u->selected |= 1 << client_player;
        SC2_HUD_SetPortraitModel(u->s.model);
        break;
    }

    /* Send initial static HUD. */
    SC2_HUD_WriteResourcePanel(ent);
    SC2_HUD_WriteConsolePanel(ent);
    SC2_HUD_WriteStart(LAYER_WORLD_HOVER);
    SC2_HUD_WriteEnd(ent);

    /* Fire the Galaxy MapInit event on the first client connect. */
    if (sc2_level.vm && !sc2_level.scriptsStarted) {
        sc2_level.scriptsStarted = true;
        galaxy_fire_mapinit(sc2_level.vm);
    }
}

static void SC2_ClientCommand(LPEDICT ent, DWORD argc, LPCSTR argv[]) {
    DWORD client_number = SC2_EdictNumber(ent);
    VECTOR2 loc;

    if (!ent || argc == 0 || !argv || !argv[0]) {
        return;
    }
    if (!strcmp(argv[0], "select")) {
        SC2_Select(ent, argc, argv);
        return;
    }
    if (!strcmp(argv[0], "point") || !strcmp(argv[0], "smartpoint")) {
        if (argc < 3) {
            return;
        }
        if (client_number < SC2_MAX_EDICTS && sc2_move[client_number].suppress_next_point) {
            sc2_move[client_number].suppress_next_point = false;
            return;
        }
        loc = (VECTOR2){ atoi(argv[1]), atoi(argv[2]) };
        SC2_MoveSelected(ent, &loc);
        return;
    }
    if (!strcmp(argv[0], "smart")) {
        if (argc < 2) {
            return;
        }
        SC2_MoveToTargetEntity(ent, (DWORD)atoi(argv[1]));
    }
}

static void SC2_ClientSetCameraPosition(LPEDICT ent, LPCVECTOR2 position) {
    if (!ent || !ent->client || !position) {
        return;
    }
    FLOAT height = ent->client->ps.vieworigin.z - SC2_MapCameraHeightAtPoint(ent->client->ps.vieworigin.x, ent->client->ps.vieworigin.y);
    ent->client->ps.vieworigin = (VECTOR3){
        position->x, position->y,
        SC2_MapCameraHeightAtPoint(position->x, position->y) + height
    };
}

static BOOL SC2_CanSeeEntity(DWORD player, LPCEDICT ent) {
    (void)player;
    (void)ent;
    return true;
}

/* Keep the mandatory snapshot hook inert because SC2 entity state is identical for every recipient. */
static void SC2_CustomizeEntity(DWORD player, LPCEDICT ent, LPENTITYSTATE state) {
    (void)player; (void)ent; (void)state;
}

static LPCSTR SC2_GetThemeValue(LPCSTR filename) {
    return filename ? filename : "";
}

struct game_export *GetGameAPI(struct game_import *import) {
    gi = *import;

    globals.Init                  = SC2_Init;
    globals.Shutdown              = SC2_Shutdown;
    globals.RunFrame              = SC2_RunFrame;
    globals.ClientBegin           = SC2_ClientBegin;
    globals.ClientCommand         = SC2_ClientCommand;
    globals.ClientSetCameraPosition = SC2_ClientSetCameraPosition;
    globals.CanSeeEntity          = SC2_CanSeeEntity;
    globals.CustomizeEntity       = SC2_CustomizeEntity;
    globals.GetThemeValue         = SC2_GetThemeValue;
    globals.LoadMap               = SC2_LoadMap;
    globals.GetWorldBounds        = CM_GetWorldBounds;
    globals.edict_size            = sizeof(edict_t);
    globals.max_clients           = SC2_MAX_CLIENTS;
    globals.max_edicts            = SC2_MAX_EDICTS;

    return &globals;
}
