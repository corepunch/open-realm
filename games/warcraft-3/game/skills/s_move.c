/*
 * s_move.c — Move ability: ground movement orders for units.
 *
 * When a player right-clicks on empty ground, move_selectlocation() is called
 * on the server.  It creates a waypoint entity at the target position and
 * calls order_move() for each selected unit.
 *
 * order_move() sets the unit's goalentity to the waypoint and switches to the
 * movement state.  Each game frame, ai_move_walk() checks the remaining distance: if
 * the unit has arrived it switches to the stand (idle) state; otherwise it
 * rotates toward the goal and advances by one frame's worth of movement.
 *
 * Collision separation between walking units is handled in g_phys.c
 * (G_SolveCollisions) after all units have moved.
 */
#include "s_skills.h"

void move_walk(LPEDICT ent);

/* With move-time collision (block-and-slide), "blocked" now means the unit
 * could not take a step this frame because it was boxed in — common and
 * transient while a group slides around obstacles.  These thresholds are
 * raised from the old free-move-plus-push values so units keep trying to
 * thread through instead of giving up the instant they are briefly packed. */
#define MOVE_BLOCKED_FRAMES 24
#define MOVE_SETTLE_FRAMES 8
#define MOVE_SLOT_MARGIN 8.0f
#define MOVE_MIN_SLOT_SPACING 16.0f
#define MOVE_ARRIVE_TOLERANCE 4.0f

typedef struct {
    VECTOR2 point;
    FLOAT radius;
} moveSlot_t;

static FLOAT move_slot_spacing(LPEDICT const *units, DWORD count) {
    FLOAT max_radius = 0;
    FOR_LOOP(i, count) {
        max_radius = MAX(max_radius, units[i]->collision);
    }
    return MAX(MOVE_MIN_SLOT_SPACING, max_radius * 2 + MOVE_SLOT_MARGIN);
}

static BOOL move_slot_overlaps(LPCVECTOR2 point,
                               FLOAT radius,
                               moveSlot_t const *reserved,
                               DWORD num_reserved) {
    FOR_LOOP(i, num_reserved) {
        FLOAT min_distance = radius + reserved[i].radius + MOVE_SLOT_MARGIN;
        if (Vector2_distance(point, &reserved[i].point) < min_distance) {
            return true;
        }
    }
    return false;
}

static BOOL move_try_slot(LPCVECTOR2 point,
                          FLOAT radius,
                          moveSlot_t const *reserved,
                          DWORD num_reserved,
                          LPVECTOR2 out) {
    VECTOR2 pathable = *point;
    if (!CM_ClosestPathablePointForRadius(point, radius, &pathable)) {
        return false;
    }
    if (move_slot_overlaps(&pathable, radius, reserved, num_reserved)) {
        return false;
    }
    *out = pathable;
    return true;
}

static BOOL move_find_reserved_slot(LPCVECTOR2 location,
                                    LPCVECTOR2 preferred,
                                    FLOAT radius,
                                    FLOAT spacing,
                                    DWORD unit_count,
                                    moveSlot_t const *reserved,
                                    DWORD num_reserved,
                                    LPVECTOR2 out) {
    FLOAT best_distance = 0;
    BOOL found = false;
    VECTOR2 best = *location;
    int max_ring = (int)ceilf(sqrtf(MAX(1, unit_count))) + 8;

    if (move_try_slot(preferred, radius, reserved, num_reserved, out)) {
        return true;
    }

    for (int ring = 0; ring <= max_ring; ring++) {
        int min = -ring;
        int max = ring;
        for (int y = min; y <= max; y++) {
            for (int x = min; x <= max; x++) {
                if (ring > 0 && x != min && x != max && y != min && y != max) {
                    continue;
                }
                VECTOR2 candidate = {
                    location->x + x * spacing,
                    location->y + y * spacing,
                };
                VECTOR2 pathable;
                FLOAT distance;

                if (!move_try_slot(&candidate, radius, reserved, num_reserved, &pathable)) {
                    continue;
                }

                distance = Vector2_distance(&pathable, preferred);
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

static VECTOR2 move_preferred_slot(LPEDICT ent,
                                   LPCVECTOR2 group_center,
                                   LPCVECTOR2 location,
                                   FLOAT spacing,
                                   DWORD unit_count) {
    VECTOR2 offset = Vector2_sub(&ent->s.origin2, group_center);
    FLOAT max_offset = spacing * (sqrtf(MAX(1, unit_count)) + 1);
    FLOAT len = Vector2_len(&offset);
    if (len > max_offset && len > 0.001f) {
        offset = Vector2_scale(&offset, max_offset / len);
    }
    return Vector2_add(location, &offset);
}

static DWORD move_collect_selected(LPGAMECLIENT client,
                                   LPEDICT *units,
                                   DWORD max_units,
                                   LPVECTOR2 center) {
    DWORD count = 0;
    *center = MAKE(VECTOR2, 0, 0);

    FOR_SELECTED_UNITS(client, ent) {
        if (count >= max_units) {
            break;
        }
        if (UNIT_IS_BUILDING(ent->class_id) || UNIT_SPEED(ent->class_id) <= 0) {
            continue;
        }
        units[count++] = ent;
        center->x += ent->s.origin2.x;
        center->y += ent->s.origin2.y;
    }

    if (count > 0) {
        center->x /= count;
        center->y /= count;
    }
    return count;
}

void move_reset_progress(LPEDICT self) {
    self->move_last_origin = self->s.origin2;
    self->move_last_distance = -1;
    self->move_blocked_frames = 0;
    self->move_group_speed = 0;  /* single-unit/default: travel at own speed */
}

/* Effective current move speed of a unit (runtime override, else data table). */
static FLOAT unit_effective_speed(LPEDICT ent) {
    return ent->unitinfo.MoveSpeed > 0 ? ent->unitinfo.MoveSpeed : UNIT_SPEED(ent->class_id);
}

/* Slowest move speed across a group, so the whole group travels at it. */
static FLOAT move_group_speed(LPEDICT const *units, DWORD count) {
    FLOAT slowest = 0;
    FOR_LOOP(i, count) {
        FLOAT const s = unit_effective_speed(units[i]);
        if (s > 0 && (slowest == 0 || s < slowest)) {
            slowest = s;
        }
    }
    return slowest;
}

BOOL move_should_arrive(LPEDICT ent, FLOAT move_distance) {
    VECTOR2 to_goal = Vector2_sub(&ent->goalentity->s.origin2, &ent->s.origin2);
    FLOAT distance = Vector2_len(&to_goal);

    if (distance <= move_distance) {
        return true;
    }

    /*
     * If the goal lies within this frame's movement corridor, snap to it
     * rather than letting the unit wobble around the destination.  This keeps
     * short path segments and near-goal collision nudges from producing a
     * visible back-and-forth at the endpoint.
     */
    VECTOR2 direction = { cosf(ent->s.angle), sinf(ent->s.angle) };
    FLOAT projected = Vector2_dot(&to_goal, &direction);
    if (projected < 0 || projected > move_distance + MOVE_ARRIVE_TOLERANCE) {
        return false;
    }

    FLOAT lateral = fabsf(to_goal.x * direction.y - to_goal.y * direction.x);
    return lateral <= MAX(MOVE_ARRIVE_TOLERANCE, ent->collision + MOVE_SLOT_MARGIN);
}

BOOL move_is_blocked(LPEDICT ent, FLOAT distance, FLOAT move_distance) {
    FLOAT const settle_distance = move_distance + ent->collision + MOVE_SLOT_MARGIN;
    if (ent->move_last_distance >= 0) {
        /* move_last_distance is the *closest* the unit has come to its goal (a
         * watermark), not just the previous frame's distance.  With move-time
         * block-and-slide a unit boxed in near its goal orbits it: distance
         * oscillates but never beats the watermark.  Measuring progress against
         * the best-so-far (instead of frame-to-frame) lets the stuck counter
         * accumulate through the orbit so the unit settles, instead of the
         * lateral motion resetting it every frame and walking forever. */
        FLOAT const improvement = ent->move_last_distance - distance;
        FLOAT const moved = Vector2_distance(&ent->s.origin2, &ent->move_last_origin);
        FLOAT const min_progress = MAX(1.0f, move_distance * 0.05f);
        FLOAT const min_moved = MAX(1.0f, move_distance * 0.25f);

        /* "Near goal" is judged by the watermark (the closest the unit has
         * ever come), not the current position: once a unit has reached its
         * best distance and can no longer improve on it, it is stuck even if
         * its orbit around the blocked goal momentarily flings it back out
         * past settle_distance. */
        if (improvement >= min_progress) {
            ent->move_blocked_frames = 0;
            ent->move_last_distance = distance;     /* advance the watermark */
        } else if (ent->move_last_distance <= settle_distance || moved < min_moved) {
            ent->move_blocked_frames++;             /* near goal, or barely moving */
        } else {
            ent->move_blocked_frames = 0;           /* far away but still making way */
        }
    } else {
        ent->move_last_distance = distance;
    }

    ent->move_last_origin = ent->s.origin2;
    return ent->move_last_distance <= settle_distance
        ? ent->move_blocked_frames >= MOVE_SETTLE_FRAMES
        : ent->move_blocked_frames >= MOVE_BLOCKED_FRAMES;
}

static void ai_move_walk(LPEDICT ent) {
    FLOAT distance = M_DistanceToGoal(ent);
    FLOAT move_distance = unit_movedistance(ent);

    if (move_should_arrive(ent, move_distance)) {
        /* Snap exactly onto the goal only if that spot is actually free; if the
         * goal is occupied (e.g. ordered onto another unit, or an attack target)
         * stop where we are rather than overlapping it. */
        if (M_MoveIsValid(ent, &ent->goalentity->s.origin2)) {
            ent->s.origin2 = ent->goalentity->s.origin2;
            gi.LinkEntity(ent);
        }
        ent->stand(ent);
    } else if (move_is_blocked(ent, distance, move_distance)) {
        ent->stand(ent);
    } else {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    }
}

static umove_t move_move_walk = { "walk", ai_move_walk, NULL, &a_move };

/* Set the unit's move target and begin walking.
 * goalentity must be a waypoint or any entity whose origin is the destination. */
void order_move(LPEDICT self, LPEDICT target) {
    self->goalentity = target;
    self->attackmove_waypoint = NULL;
    self->holding_position = false;
    self->patrol_a = NULL;
    move_reset_progress(self);
    unit_setmove(self, &move_move_walk);
}

/* Handle a right-click move command from the client.
 * Creates a shared waypoint at the clicked map position, issues move orders
 * to all currently selected units, and sends a move-confirmation effect back
 * to the commanding client (svc_temp_entity / TE_MOVE_CONFIRMATION). */
BOOL move_selectlocation(LPEDICT clent, LPCVECTOR2 location) {
    LPEDICT units[MAX_SELECTED_ENTITIES];
    moveSlot_t reserved[MAX_SELECTED_ENTITIES];
    VECTOR2 center;
    VECTOR2 confirmation = *location;
    BOOL have_confirmation = false;
    DWORD num_units = move_collect_selected(clent->client, units, MAX_SELECTED_ENTITIES, &center);
    FLOAT spacing = move_slot_spacing(units, num_units);
    LPEDICT route_waypoint;

    if (num_units == 0) {
        return false;
    }
    /* A multi-unit move travels at the slowest member's speed so the group
     * stays together (WC3).  A lone unit keeps its own speed (cap 0). */
    FLOAT const group_speed = num_units > 1 ? move_group_speed(units, num_units) : 0;
    route_waypoint = Waypoint_add(location);

    FOR_LOOP(i, num_units) {
        LPEDICT ent = units[i];
        VECTOR2 preferred = move_preferred_slot(ent, &center, location, spacing, num_units);
        VECTOR2 target;

        if (!move_find_reserved_slot(location,
                                     &preferred,
                                     ent->collision,
                                     spacing,
                                     num_units,
                                     reserved,
                                     i,
                                     &target)) {
            target = *location;
            CM_ClosestPathablePointForRadius(location, ent->collision, &target);
        }
        reserved[i] = (moveSlot_t){ target, ent->collision };
        LPEDICT waypoint = Waypoint_add(&target);
        waypoint->secondarygoal = route_waypoint;
        if (!have_confirmation) {
            confirmation = target;
            have_confirmation = true;
        }
        order_move(ent, waypoint);
        ent->move_group_speed = group_speed;  /* after order_move, which resets it */
    }
    gi.Write(PF_BYTE, &(LONG){svc_temp_entity});
    gi.Write(PF_BYTE, &(LONG){TE_MOVE_CONFIRMATION});
    gi.Write(PF_POSITION, &(VECTOR3){confirmation.x, confirmation.y, 0});
    gi.unicast(clent);
    return true;
}

void move_command(LPEDICT ent) {
    UI_AddCancelButton(ent);
    ent->client->menu.on_location_selected = move_selectlocation;
}

ability_t a_move = {
    .cmd = move_command,
};
