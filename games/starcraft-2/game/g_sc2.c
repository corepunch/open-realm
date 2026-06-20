/*
 * g_sc2.c — StarCraft II game module: unit selection, movement, and collision.
 *
 * Movement uses the shared flow-field pathfinding from common/routing.c.
 * Multi-unit move orders assign non-overlapping destination slots via a
 * ring search (ported from WC3 s_move.c).  Collision resolution splits
 * separation proportionally by remaining distance to goal and includes
 * transitive bumping for stopped units sharing a destination.
 */
#include "g_sc2_local.h"
#include "games/starcraft-2/common/sc2_map.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

struct game_import gi;
struct game_export globals;

static edict_t sc2_edicts[SC2_MAX_EDICTS];
static struct client_s sc2_clients[SC2_MAX_CLIENTS];
static DWORD current_waypoint;

/* ── waypoint pool ─────────────────────────────────────────────────── */

LPEDICT Waypoint_add(LPCVECTOR2 spot) {
    static edict_t waypoints[SC2_MAX_WAYPOINTS];
    LPEDICT wp = &waypoints[current_waypoint++ % SC2_MAX_WAYPOINTS];
    wp->s.origin.x = spot->x;
    wp->s.origin.y = spot->y;
    wp->s.origin.z = SC2_MapHeightAtPoint(spot->x, spot->y);
    wp->heatmap2 = 0;
    wp->secondarygoal = NULL;
    wp->collision = 0;
    return wp;
}

/* ── helpers ───────────────────────────────────────────────────────── */

static BOOL SC2_ObjectIsMobile(sc2MapObject_t const *object) {
    if (!object || object->type != SC2_OBJECT_UNIT)
        return false;
    if (strstr(object->name, "CommandCenter") || strstr(object->model, "CommandCenter"))
        return false;
    return true;
}

static FLOAT SC2_ObjectSpawnZ(sc2MapObject_t const *object) {
    if (!object)
        return 0.0f;
    if (object->flags & SC2_OBJECT_HEIGHT_ABSOLUTE)
        return object->position.z;
    return SC2_MapHeightAtPoint(object->position.x, object->position.y) + object->position.z;
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

static void SC2_LinkAtGround(LPEDICT ent) {
    ent->s.origin.x = ent->s.origin2.x;
    ent->s.origin.y = ent->s.origin2.y;
    ent->s.origin.z = SC2_MapHeightAtPoint(ent->s.origin2.x, ent->s.origin2.y);
    gi.LinkEntity(ent);
}

static DWORD SC2_EdictNumber(LPCEDICT ent) {
    if (!ent || ent < sc2_edicts || ent >= sc2_edicts + SC2_MAX_EDICTS)
        return SC2_MAX_EDICTS;
    return (DWORD)(ent - sc2_edicts);
}

static DWORD SC2_ClientPlayer(LPCEDICT ent) {
    return ent && ent->client ? ent->client->ps.number : 1;
}

static BOOL SC2_IsSelectable(LPCEDICT ent, DWORD player) {
    return SC2_EdictNumber(ent) < SC2_MAX_EDICTS &&
        ent->inuse && ent->s.model &&
        ent->s.player == player && ent->mobile;
}

FLOAT SC2_DistanceToGoal(LPEDICT ent) {
    return ent->goalentity
        ? Vector2_distance(&ent->goalentity->s.origin2, &ent->s.origin2)
        : 0;
}

FLOAT SC2_UnitMoveDistance(LPEDICT ent) {
    FLOAT speed = ent->move_speed > 0 ? ent->move_speed : SC2_MOVE_SPEED;
    return 10 * speed / FRAMETIME;
}

/* ── movement angle + advance ──────────────────────────────────────── */

void SC2_UnitChangeAngle(LPEDICT self) {
    VECTOR2 to_goal = Vector2_sub(&self->goalentity->s.origin2, &self->s.origin2);
    FLOAT dist = Vector2_len(&to_goal);
    VECTOR2 dir;

    if (dist <= SC2_NAVI_THRESHOLD) {
        dir = to_goal;
    } else {
        DWORD heatmap = CM_BuildHeatmap(self->goalentity);
        dir = get_flow_direction(heatmap, self->s.origin.x, self->s.origin.y);
        if (Vector2_len(&dir) <= 0.001f)
            dir = to_goal;
    }
    self->s.angle = atan2f(dir.y, dir.x);
}

void SC2_UnitMoveInDirection(LPEDICT self) {
    VECTOR2 dir = { cosf(self->s.angle), sinf(self->s.angle) };
    SC2_PushEntity(self, SC2_UnitMoveDistance(self), &dir);
}

void SC2_PushEntity(LPEDICT ent, FLOAT distance, LPCVECTOR2 direction) {
    ent->s.origin2 = Vector2_mad(&ent->s.origin2, distance, direction);
    SC2_LinkAtGround(ent);
}

/* ── arrival + stuck detection ─────────────────────────────────────── */

#define SC2_ARRIVE_TOLERANCE 4.0f

static BOOL SC2_ShouldArrive(LPEDICT ent, FLOAT move_distance) {
    VECTOR2 to_goal = Vector2_sub(&ent->goalentity->s.origin2, &ent->s.origin2);
    FLOAT distance = Vector2_len(&to_goal);

    if (distance <= move_distance)
        return true;

    VECTOR2 direction = { cosf(ent->s.angle), sinf(ent->s.angle) };
    FLOAT projected = Vector2_dot(&to_goal, &direction);
    if (projected < 0 || projected > move_distance + SC2_ARRIVE_TOLERANCE)
        return false;

    FLOAT lateral = fabsf(to_goal.x * direction.y - to_goal.y * direction.x);
    return lateral <= MAX(SC2_ARRIVE_TOLERANCE, ent->collision + SC2_SLOT_MARGIN);
}

static BOOL SC2_IsBlocked(LPEDICT ent, FLOAT distance, FLOAT move_distance) {
    if (ent->move_last_distance >= 0) {
        FLOAT progress = ent->move_last_distance - distance;
        FLOAT moved = Vector2_distance(&ent->s.origin2, &ent->move_last_origin);
        FLOAT min_progress = MAX(1.0f, move_distance * 0.05f);
        FLOAT min_moved = MAX(1.0f, move_distance * 0.25f);
        FLOAT settle_distance = move_distance + ent->collision + SC2_SLOT_MARGIN;

        if (distance <= settle_distance && progress < min_progress)
            ent->move_blocked_frames++;
        else if (progress < min_progress && moved < min_moved)
            ent->move_blocked_frames++;
        else
            ent->move_blocked_frames = 0;
    }
    ent->move_last_distance = distance;
    ent->move_last_origin = ent->s.origin2;
    return distance <= move_distance + ent->collision + SC2_SLOT_MARGIN
        ? ent->move_blocked_frames >= SC2_SETTLE_FRAMES
        : ent->move_blocked_frames >= SC2_BLOCKED_FRAMES;
}

/* ── selection ─────────────────────────────────────────────────────── */

static void SC2_Select(LPEDICT clent, DWORD argc, LPCSTR argv[]) {
    DWORD player = SC2_ClientPlayer(clent);
    BOOL cleared = false;

    for (DWORD i = 1; i < argc; i++) {
        DWORD number = (DWORD)atoi(argv[i]);
        if (number >= (DWORD)globals.num_edicts || !SC2_IsSelectable(&sc2_edicts[number], player))
            continue;
        if (!cleared) {
            FOR_LOOP(j, globals.num_edicts)
                sc2_edicts[j].selected &= ~(1 << player);
            cleared = true;
        }
        sc2_edicts[number].selected |= 1 << player;
    }
}

/* ── slot assignment (ring search) ─────────────────────────────────── */

typedef struct {
    VECTOR2 point;
    FLOAT radius;
} sc2Slot_t;

static FLOAT sc2_slot_spacing(LPEDICT const *units, DWORD count) {
    FLOAT max_radius = 0;
    FOR_LOOP(i, count)
        max_radius = MAX(max_radius, units[i]->collision);
    return MAX(SC2_MIN_SLOT_SPACING, max_radius * 2 + SC2_SLOT_MARGIN);
}

static BOOL sc2_slot_overlaps(LPCVECTOR2 point, FLOAT radius,
                              sc2Slot_t const *reserved, DWORD num_reserved) {
    FOR_LOOP(i, num_reserved) {
        FLOAT min_distance = radius + reserved[i].radius + SC2_SLOT_MARGIN;
        if (Vector2_distance(point, &reserved[i].point) < min_distance)
            return true;
    }
    return false;
}

static BOOL sc2_try_slot(LPCVECTOR2 point, FLOAT radius,
                         sc2Slot_t const *reserved, DWORD num_reserved,
                         LPVECTOR2 out) {
    VECTOR2 pathable = *point;
    if (!CM_ClosestPathablePointForRadius(point, radius, &pathable))
        return false;
    if (sc2_slot_overlaps(&pathable, radius, reserved, num_reserved))
        return false;
    *out = pathable;
    return true;
}

static BOOL sc2_find_reserved_slot(LPCVECTOR2 location, LPCVECTOR2 preferred,
                                   FLOAT radius, FLOAT spacing, DWORD unit_count,
                                   sc2Slot_t const *reserved, DWORD num_reserved,
                                   LPVECTOR2 out) {
    FLOAT best_distance = 0;
    BOOL found = false;
    VECTOR2 best = *location;
    int max_ring = (int)ceilf(sqrtf(MAX(1, unit_count))) + 8;

    if (sc2_try_slot(preferred, radius, reserved, num_reserved, out))
        return true;

    for (int ring = 0; ring <= max_ring; ring++) {
        for (int y = -ring; y <= ring; y++) {
            for (int x = -ring; x <= ring; x++) {
                if (ring > 0 && x != -ring && x != ring && y != -ring && y != ring)
                    continue;
                VECTOR2 candidate = { location->x + x * spacing, location->y + y * spacing };
                VECTOR2 pathable;
                if (!sc2_try_slot(&candidate, radius, reserved, num_reserved, &pathable))
                    continue;
                FLOAT distance = Vector2_distance(&pathable, preferred);
                if (!found || distance < best_distance) {
                    best_distance = distance;
                    best = pathable;
                    found = true;
                }
            }
        }
        if (found) {
            *out = best;
            return true;
        }
    }
    return false;
}

static VECTOR2 sc2_preferred_slot(LPEDICT ent, LPCVECTOR2 group_center,
                                  LPCVECTOR2 location, FLOAT spacing, DWORD unit_count) {
    VECTOR2 offset = Vector2_sub(&ent->s.origin2, group_center);
    FLOAT max_offset = spacing * (sqrtf(MAX(1, unit_count)) + 1);
    FLOAT len = Vector2_len(&offset);
    if (len > max_offset && len > 0.001f)
        offset = Vector2_scale(&offset, max_offset / len);
    return Vector2_add(location, &offset);
}

/* ── move order ────────────────────────────────────────────────────── */

static void SC2_StopUnit(LPEDICT ent) {
    ent->goalentity = NULL;
    ent->move_blocked_frames = 0;
    ent->move_last_distance = -1;
    ent->s.frame = gi.GetTime();
    ent->s.ability = 0;
}

static void SC2_OrderMoveUnit(LPEDICT ent, LPEDICT waypoint) {
    ent->goalentity = waypoint;
    ent->move_blocked_frames = 0;
    ent->move_last_distance = -1;
    ent->move_last_origin = ent->s.origin2;
    ent->s.frame = gi.GetTime();
    ent->s.ability = 1;
    CM_InvalidatePathCache();
}

static BOOL SC2_MoveSelected(LPEDICT clent, LPCVECTOR2 target) {
    DWORD player = SC2_ClientPlayer(clent);
    LPEDICT units[SC2_MAX_SELECTED];
    sc2Slot_t reserved[SC2_MAX_SELECTED];
    VECTOR2 center = { 0, 0 };
    DWORD num_units = 0;
    VECTOR2 confirmation = *target;
    BOOL have_confirmation = false;

    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &sc2_edicts[i];
        if (num_units >= SC2_MAX_SELECTED)
            break;
        if (!(ent->selected & (1 << player)) || !SC2_IsSelectable(ent, player))
            continue;
        units[num_units++] = ent;
        center.x += ent->s.origin2.x;
        center.y += ent->s.origin2.y;
    }
    if (num_units == 0)
        return false;
    center.x /= num_units;
    center.y /= num_units;

    FLOAT spacing = sc2_slot_spacing(units, num_units);
    LPEDICT route_waypoint = Waypoint_add(target);

    FOR_LOOP(i, num_units) {
        LPEDICT ent = units[i];
        VECTOR2 preferred = sc2_preferred_slot(ent, &center, target, spacing, num_units);
        VECTOR2 slot;

        if (!sc2_find_reserved_slot(target, &preferred, ent->collision, spacing,
                                    num_units, reserved, i, &slot)) {
            slot = *target;
            CM_ClosestPathablePointForRadius(target, ent->collision, &slot);
        }
        reserved[i] = (sc2Slot_t){ slot, ent->collision };
        LPEDICT waypoint = Waypoint_add(&slot);
        waypoint->secondarygoal = route_waypoint;
        if (!have_confirmation) {
            confirmation = slot;
            have_confirmation = true;
        }
        SC2_OrderMoveUnit(ent, waypoint);
    }
    gi.Write(PF_BYTE, &(LONG){svc_temp_entity});
    gi.Write(PF_BYTE, &(LONG){TE_MOVE_CONFIRMATION});
    gi.Write(PF_POSITION, &(VECTOR3){confirmation.x, confirmation.y, 0});
    gi.unicast(clent);
    return true;
}

/* ── movement per frame ────────────────────────────────────────────── */

static void SC2_RunUnit(LPEDICT ent) {
    if (!ent->goalentity)
        return;
    FLOAT distance = SC2_DistanceToGoal(ent);
    FLOAT move_distance = SC2_UnitMoveDistance(ent);

    if (SC2_ShouldArrive(ent, move_distance)) {
        ent->s.origin2 = ent->goalentity->s.origin2;
        SC2_LinkAtGround(ent);
        SC2_StopUnit(ent);
    } else if (SC2_IsBlocked(ent, distance, move_distance)) {
        SC2_StopUnit(ent);
    } else {
        SC2_UnitChangeAngle(ent);
        SC2_UnitMoveInDirection(ent);
    }
}

/* ── collision resolution ──────────────────────────────────────────── */

static BOOL sc2_collision_filter(LPCEDICT ent) {
    return ent && ent->inuse && ent->s.model && ent->collision > 0;
}

static void SC2_SolveCollisions(void) {
    LPEDICT colliders[SC2_MAX_COLLIDERS];

    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT a = &sc2_edicts[i];
        if (!a->mobile || !a->inuse || a->collision <= 0)
            continue;
        DWORD num = gi.BoxEdicts(&a->bounds, colliders, SC2_MAX_COLLIDERS, sc2_collision_filter);
        FOR_LOOP(j, num) {
            LPEDICT b = colliders[j];
            if (b == a)
                continue;
            DWORD bnum = SC2_EdictNumber(b);
            if (bnum < SC2_MAX_EDICTS && bnum >= i)
                continue;
            FLOAT radius = a->collision + b->collision;
            FLOAT distance = Vector2_distance(&a->s.origin2, &b->s.origin2);
            if (distance >= radius || distance <= 0.001f)
                continue;
            VECTOR2 d = Vector2_sub(&a->s.origin2, &b->s.origin2);
            Vector2_normalize(&d);
            FLOAT diff = distance - radius;
            BOOL b_mobile = bnum < SC2_MAX_EDICTS && b->mobile;
            if (!b_mobile) {
                SC2_PushEntity(a, -diff, &d);
            } else if (a->goalentity && b->goalentity) {
                FLOAT ad = SC2_DistanceToGoal(a);
                FLOAT bd = SC2_DistanceToGoal(b);
                FLOAT sum = ad + bd;
                if (sum > 0.001f) {
                    SC2_PushEntity(a, -diff * ad / sum, &d);
                    SC2_PushEntity(b, diff * bd / sum, &d);
                } else {
                    SC2_PushEntity(a, -diff * 0.5f, &d);
                    SC2_PushEntity(b, diff * 0.5f, &d);
                }
            } else {
                SC2_PushEntity(a, -diff * 0.5f, &d);
                SC2_PushEntity(b, diff * 0.5f, &d);
            }
        }
    }
}

/* ── init / shutdown / spawn ───────────────────────────────────────── */

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
    current_waypoint = 0;

    globals.edicts      = sc2_edicts;
    globals.num_edicts  = SC2_MAX_CLIENTS;
    globals.max_edicts  = SC2_MAX_EDICTS;
    globals.max_clients = SC2_MAX_CLIENTS;
    SC2_InitClients();
}

static void SC2_Shutdown(void) {
    G_FreeModels();
    SC2_MapShutdown();
}

static void SC2_SpawnEntities(void);

static bool SC2_LoadMap(LPCSTR mapFilename) {
    if (!CM_LoadMap(mapFilename))
        return false;
    if (gi.ApplyLobbySettings)
        gi.ApplyLobbySettings((LPMAPINFO)CM_GetMapInfo());
    if (gi.ClearWorld)
        gi.ClearWorld();
    SC2_SpawnEntities();
    return true;
}

static void SC2_SpawnEntities(void) {
    sc2Map_t const *map = SC2_MapCurrent();

    memset(sc2_edicts + globals.max_clients, 0,
           sizeof(sc2_edicts) - sizeof(sc2_edicts[0]) * globals.max_clients);
    current_waypoint = 0;
    SC2_InitClients();
    globals.num_edicts = globals.max_clients;

    FOR_LOOP(i, map->num_objects) {
        sc2MapObject_t const *object = &map->objects[i];
        LPEDICT ent;

        if (!object->model[0])
            continue;
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
        ent->s.radius = SC2_ObjectRadius(object);
        ent->s.player = object->player;
        ent->s.model = G_RegisterModel(object->model);
        ent->collision = ent->s.radius;
        ent->mobile = SC2_ObjectIsMobile(object);
        ent->move_speed = SC2_MOVE_SPEED;
        ent->move_last_distance = -1;
        if (ent->mobile)
            ent->svflags |= SVF_MONSTER;
        gi.LinkEntity(ent);
    }
    CM_BakeStaticObstacles();
}

/* ── frame ─────────────────────────────────────────────────────────── */

static void SC2_RunFrame(void) {
    FOR_LOOP(i, globals.num_edicts) {
        if (sc2_edicts[i].inuse)
            SC2_RunUnit(&sc2_edicts[i]);
    }
    SC2_SolveCollisions();
}

/* ── client commands ───────────────────────────────────────────────── */

static void SC2_ClientBegin(LPEDICT ent) {
    DWORD number = SC2_EdictNumber(ent);

    if (number >= SC2_MAX_CLIENTS)
        number = 0, ent = &sc2_edicts[0];
    ent->client = &sc2_clients[number];
    ent->client->ps.client_ui_state = CLIENT_UI_GAME;
}

static void SC2_ClientCommand(LPEDICT ent, DWORD argc, LPCSTR argv[]) {
    if (!ent || argc == 0 || !argv || !argv[0])
        return;
    if (!strcmp(argv[0], "select")) {
        SC2_Select(ent, argc, argv);
        return;
    }
    if (!strcmp(argv[0], "point") || !strcmp(argv[0], "smartpoint")) {
        if (argc < 3)
            return;
        VECTOR2 loc = { atoi(argv[1]), atoi(argv[2]) };
        SC2_MoveSelected(ent, &loc);
        return;
    }
    if (!strcmp(argv[0], "smart")) {
        if (argc < 2)
            return;
        DWORD target_number = (DWORD)atoi(argv[1]);
        if (target_number < (DWORD)globals.num_edicts && sc2_edicts[target_number].inuse)
            SC2_MoveSelected(ent, &sc2_edicts[target_number].s.origin2);
    }
}

static void SC2_ClientSetCameraPosition(LPEDICT ent, LPCVECTOR2 position) {
    if (ent && ent->client && position)
        ent->client->ps.origin = *position;
}

static BOOL SC2_CanSeeEntity(DWORD player, LPCEDICT ent) {
    (void)player; (void)ent;
    return true;
}

static LPCSTR SC2_GetThemeValue(LPCSTR filename) {
    return filename ? filename : "";
}

/* ── game export ───────────────────────────────────────────────────── */

struct game_export *GetGameAPI(struct game_import *import) {
    gi = *import;

    globals.Init                     = SC2_Init;
    globals.Shutdown                 = SC2_Shutdown;
    globals.RunFrame                 = SC2_RunFrame;
    globals.ClientBegin              = SC2_ClientBegin;
    globals.ClientCommand            = SC2_ClientCommand;
    globals.ClientSetCameraPosition  = SC2_ClientSetCameraPosition;
    globals.CanSeeEntity             = SC2_CanSeeEntity;
    globals.GetThemeValue            = SC2_GetThemeValue;
    globals.LoadMap                  = SC2_LoadMap;
    globals.GetWorldBounds           = CM_GetWorldBounds;
    globals.edict_size               = sizeof(edict_t);
    globals.max_clients              = SC2_MAX_CLIENTS;
    globals.max_edicts               = SC2_MAX_EDICTS;

    return &globals;
}
