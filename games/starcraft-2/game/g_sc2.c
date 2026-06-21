#include "g_sc2_local.h"
#include "games/starcraft-2/common/sc2_map.h"
#include <stdio.h>
#include <string.h>

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
    BOOL suppress_next_point;
    VECTOR2 target;
    FLOAT speed;
} sc2MoveState_t;

static sc2MoveState_t sc2_move[SC2_MAX_EDICTS];

static BOOL SC2_ObjectIsMobile(sc2MapObject_t const *object) {
    if (!object || object->type != SC2_OBJECT_UNIT) {
        return false;
    }
    if (strstr(object->name, "CommandCenter") || strstr(object->model, "CommandCenter")) {
        return false;
    }
    return true;
}

static FLOAT SC2_ObjectSpawnZ(sc2MapObject_t const *object) {
    if (!object) {
        return 0.0f;
    }
    if (object->flags & SC2_OBJECT_HEIGHT_ABSOLUTE) {
        return object->position.z;
    }
    return SC2_MapHeightAtPoint(object->position.x, object->position.y) + object->position.z;
}

static void SC2_LinkAtGround(LPEDICT ent) {
    ent->s.origin.x = ent->s.origin2.x;
    ent->s.origin.y = ent->s.origin2.y;
    ent->s.origin.z = SC2_MapHeightAtPoint(ent->s.origin2.x, ent->s.origin2.y);
    gi.LinkEntity(ent);
}

static DWORD SC2_EdictNumber(LPCEDICT ent) {
    if (!ent || ent < sc2_edicts || ent >= sc2_edicts + SC2_MAX_EDICTS) {
        return SC2_MAX_EDICTS;
    }
    return (DWORD)(ent - sc2_edicts);
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
        SC2_LinkAtGround(ent);
        SC2_StopUnit(ent);
        return;
    }
    if (dist <= SC2_MOVE_CLOSE) {
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
    SC2_LinkAtGround(ent);
}

static BOOL sc2_collision_filter(LPCEDICT ent) {
    return ent && ent->inuse && ent->s.model && ent->collision > 0;
}

static void SC2_PushEntity(LPEDICT ent, FLOAT distance, LPCVECTOR2 dir) {
    ent->s.origin2 = Vector2_mad(&ent->s.origin2, distance, dir);
    SC2_LinkAtGround(ent);
}

static void SC2_SolveCollisions(void) {
    LPEDICT colliders[SC2_MAX_COLLIDERS];

    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT a = &sc2_edicts[i];
        if (!sc2_move[i].mobile || !a->inuse || a->collision <= 0) {
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

static void SC2_InitClients(void) {
    sc2MapCamera_t camera;

    SC2_MapDefaultCamera(&camera);
    FOR_LOOP(i, SC2_MAX_CLIENTS) {
        LPEDICT ent = &sc2_edicts[i];
        ent->inuse = true;
        ent->s.number = i;
        ent->client = &sc2_clients[i];
        ent->client->ps.number = i + 1;
        ent->client->ps.client_ui_state = CLIENT_UI_GAME;
        ent->client->ps.origin = (VECTOR2){ camera.target.x, camera.target.y };
        ent->client->ps.fov = (DWORD)camera.fov;
        ent->client->ps.distance = camera.distance;
        ent->client->ps.rdflags = RDF_NOFOG | RDF_NOFOGMASK;
        ent->client->ps.viewangles = (VECTOR3){ camera.pitch, camera.yaw, 0.0f };
        ent->client->ps.viewquat = Quaternion_fromEuler(&ent->client->ps.viewangles, ROTATE_ZYX);
    }
}

static void SC2_Init(void) {
    memset(sc2_edicts,  0, sizeof(sc2_edicts));
    memset(sc2_clients, 0, sizeof(sc2_clients));
    memset(sc2_move,    0, sizeof(sc2_move));
    memset(sc2_waypoints, 0, sizeof(sc2_waypoints));

    globals.edicts     = sc2_edicts;
    globals.num_edicts = SC2_MAX_CLIENTS;
    globals.max_edicts = SC2_MAX_EDICTS;
    globals.max_clients = SC2_MAX_CLIENTS;
    SC2_InitClients();
}

static void SC2_Shutdown(void) {
    G_FreeModels();
    SC2_MapShutdown();
}

static void SC2_SpawnEntities(void);

static bool SC2_LoadMap(LPCSTR mapFilename) {
    if (!CM_LoadMap(mapFilename)) {
        return false;
    }
    if (gi.ApplyLobbySettings) {
        gi.ApplyLobbySettings((LPMAPINFO)CM_GetMapInfo());
    }
    if (gi.ClearWorld) {
        gi.ClearWorld();
    }
    SC2_SpawnEntities();
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
            fprintf(stderr, "SC2_SpawnEntities: hit max edicts at %u objects\n", (unsigned)i);
            break;
        }
        ent = &sc2_edicts[globals.num_edicts++];
        memset(ent, 0, sizeof(*ent));
        ent->inuse = true;
        ent->s.number = (DWORD)(ent - sc2_edicts);
        ent->s.class_id = SC2_MapObjectClassId(object);
        ent->s.origin = object->position;
        ent->s.origin.z = SC2_ObjectSpawnZ(object);
        ent->s.angle = object->angle;
        ent->s.scale = object->scale > 0.0f ? object->scale : 1.0f;
        ent->s.radius = object->type == SC2_OBJECT_UNIT ? 1.0f : 0.5f;
        ent->s.player = object->player;
        ent->s.model = G_RegisterModel(object->model);
        ent->collision = ent->s.radius;
        if (SC2_ObjectIsMobile(object)) {
            ent->svflags |= SVF_MONSTER;
        }
        number = (DWORD)(ent - sc2_edicts);
        sc2_move[number].mobile = (ent->svflags & SVF_MONSTER) != 0;
        sc2_move[number].speed = SC2_MOVE_SPEED;
        gi.LinkEntity(ent);
    }
    CM_BakeStaticObstacles();
}

static void SC2_RunFrame(void) {
    FOR_LOOP(i, globals.num_edicts) {
        if (sc2_edicts[i].inuse) {
            SC2_RunUnit(&sc2_edicts[i]);
        }
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
    ent->client->ps.origin = *position;
}

static BOOL SC2_CanSeeEntity(DWORD player, LPCEDICT ent) {
    (void)player;
    (void)ent;
    return true;
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
    globals.GetThemeValue         = SC2_GetThemeValue;
    globals.LoadMap               = SC2_LoadMap;
    globals.GetWorldBounds        = CM_GetWorldBounds;
    globals.edict_size            = sizeof(edict_t);
    globals.max_clients           = SC2_MAX_CLIENTS;
    globals.max_edicts            = SC2_MAX_EDICTS;

    return &globals;
}
