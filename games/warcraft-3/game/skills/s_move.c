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

    FOR_CONTROLLABLE_SELECTED_UNITS(client, ent) {
        if (count >= max_units) {
            break;
        }
        if ((ent->aiflags & AI_IMMOBILE) || ent->data.UnitBalance->speed <= 0) {
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
    self->movement.last_origin = self->s.origin2;
    self->movement.last_distance = -1;
    self->movement.blocked_frames = 0;
    self->movement.flow_generation = 0;
    self->movement.flow_goal_reached = false;
    self->movement.flow_unreachable = false;
    self->movement.flow_direct = false;
    self->movement.worker_avoid_origin = self->s.origin2;
    self->movement.worker_avoid_heading = self->s.angle;
    self->movement.worker_avoid_blocked_frames = 0;
    self->movement.worker_avoid_active = false;
    self->movement.group_speed = 0;  /* single-unit/default: travel at own speed */
}

/* Effective current move speed of a unit (runtime override, else data table). */
static FLOAT unit_effective_speed(LPEDICT ent) {
    return ent->unitinfo.MoveSpeed > 0 ? ent->unitinfo.MoveSpeed : ent->data.UnitBalance->speed;
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
    if (ent->movement.last_distance >= 0) {
        /* move_last_distance is the *closest* the unit has come to its goal (a
         * watermark), not just the previous frame's distance.  With move-time
         * block-and-slide a unit boxed in near its goal orbits it: distance
         * oscillates but never beats the watermark.  Measuring progress against
         * the best-so-far (instead of frame-to-frame) lets the stuck counter
         * accumulate through the orbit so the unit settles, instead of the
         * lateral motion resetting it every frame and walking forever. */
        FLOAT const improvement = ent->movement.last_distance - distance;
        FLOAT const moved = Vector2_distance(&ent->s.origin2, &ent->movement.last_origin);
        FLOAT const min_progress = MAX(1.0f, move_distance * 0.05f);
        FLOAT const min_moved = MAX(1.0f, move_distance * 0.25f);

        /* "Near goal" is judged by the watermark (the closest the unit has
         * ever come), not the current position: once a unit has reached its
         * best distance and can no longer improve on it, it is stuck even if
         * its orbit around the blocked goal momentarily flings it back out
         * past settle_distance. */
        if (improvement >= min_progress) {
            ent->movement.blocked_frames = 0;
            ent->movement.last_distance = distance;     /* advance the watermark */
        } else if (ent->movement.last_distance <= settle_distance || moved < min_moved) {
            ent->movement.blocked_frames++;             /* near goal, or barely moving */
        } else {
            ent->movement.blocked_frames = 0;           /* far away but still making way */
        }
    } else {
        ent->movement.last_distance = distance;
    }

    ent->movement.last_origin = ent->s.origin2;
    return ent->movement.last_distance <= settle_distance
        ? ent->movement.blocked_frames >= MOVE_SETTLE_FRAMES
        : ent->movement.blocked_frames >= MOVE_BLOCKED_FRAMES;
}

/* Interaction walkers sometimes stop just outside a blocked building because
 * another worker occupies the final approach lane.  Reuse Move's established
 * near-goal settle window instead of duplicating its margin/frame constants in
 * each behavior.  This only reports true when the unit has both stopped making
 * progress and reached the same near-goal band where an ordinary Move would
 * settle; a wall or disconnected route farther away is not an arrival. */
BOOL move_is_settled_near_goal(LPEDICT ent, FLOAT distance, FLOAT move_distance) {
    FLOAT const settle_distance = move_distance + ent->collision + MOVE_SLOT_MARGIN;
    BOOL const blocked = move_is_blocked(ent, distance, move_distance);
    return blocked && ent->movement.last_distance <= settle_distance;
}

/* Unit-target Move/Smart is a persistent follow order rather than a snapshot
 * point move. Keep the target entity authoritative so a moving ally can be
 * tracked and the retained goal can be resumed after opportunistic combat. */
static BOOL follow_target_is_valid(LPCEDICT self, LPCEDICT target) {
    DWORD owner;

    if (!self || !target || !target->inuse || !(target->svflags & SVF_MONSTER) || M_IsDead((LPEDICT)target)) {
        return false;
    }
    if (self->s.player >= MAX_PLAYERS || target->s.player >= MAX_PLAYERS) {
        return false;
    }
    owner = target->s.player;
    if (owner == self->s.player) {
        return true;
    }
    if (owner == PLAYER_NEUTRAL_AGGRESSIVE || owner == PLAYER_NEUTRAL_PASSIVE) {
        return false;
    }
    if (level.mapinfo && level.mapinfo->players[owner].playerType == kPlayerTypeNone) {
        return false;
    }
    return (level.alliances[self->s.player][owner] & (1 << ALLIANCE_PASSIVE)) != 0;
}

static BOOL follow_can_auto_attack(LPCEDICT self) {
    if (!self || self->attack1.cooldown <= 0.0f ||
        (self->attack1.damageBase <= 0 && self->attack1.numberOfDice <= 0)) {
        return false;
    }
    return !level.mapinfo || level.mapinfo->players[self->s.player].playerType != kPlayerTypeNeutral;
}

static void ai_follow_walk(LPEDICT ent) {
    LPEDICT target = ent->movement.follow_target;
    FLOAT distance;
    FLOAT follow_range;
    BOOL standing;

    if (!follow_target_is_valid(ent, target)) {
        ent->movement.follow_target = NULL;
        if (ent->goalentity == target) ent->goalentity = NULL;
        unit_stand(ent);
        return;
    }

    ent->goalentity = target;
    if (follow_can_auto_attack(ent) && G_ShouldAcquireThisFrame(ent)) {
        LPEDICT enemy = G_FindNearestEnemy(ent, G_AcquisitionRange(ent));
        if (enemy) {
            order_attack(ent, enemy);
            return;
        }
    }

    distance = M_DistanceToGoal(ent);
    follow_range = MAX(G_AcquisitionRange(ent), ent->collision + target->collision);
    standing = ent->animation && !strcmp(ent->animation->name, "stand");
    if (distance <= follow_range) {
        if (!standing) {
            move_reset_progress(ent);
            unit_setanimation(ent, "stand");
        }
        return;
    }

    if (standing) move_reset_progress(ent);
    unit_setanimation(ent, "walk");
    unit_changeangle(ent);
    if (ent->movement.flow_unreachable) {
        unit_setanimation(ent, "stand");
        return;
    }
    unit_moveindirection(ent);
}

static umove_t follow_move_walk = { "walk", ai_follow_walk, NULL, &a_move };

void order_follow_resume(LPEDICT self) {
    LPEDICT target;

    if (!self || S_GoldMineWorkerIsInside(self) || (self->aiflags & AI_IMMOBILE)) {
        return;
    }
    target = self->movement.follow_target;
    if (!follow_target_is_valid(self, target)) {
        self->movement.follow_target = NULL;
        if (self->goalentity == target) self->goalentity = NULL;
        unit_stand(self);
        return;
    }
    self->goalentity = target;
    self->movement.holding_position = false;
    move_reset_progress(self);
    unit_setmove(self, &follow_move_walk);
}

void order_follow(LPEDICT self, LPEDICT target) {
    if (!self || (self->aiflags & AI_IMMOBILE) || S_GoldMineWorkerIsInside(self) ||
        !follow_target_is_valid(self, target)) {
        return;
    }
    self->movement.attackmove_waypoint = NULL;
    self->movement.patrol_a = NULL;
    self->movement.patrol_b = NULL;
    self->movement.patrol_target = NULL;
    self->movement.follow_target = target;
    self->movement.holding_position = false;
    order_follow_resume(self);
}

static umove_t move_move_hold = { "stand", NULL, NULL, &a_move };

BOOL move_is_terminal_hold(LPCEDICT ent) {
    return ent && ent->currentmove == &move_move_hold;
}

static void move_hold(LPEDICT ent) {
    /* A terminal blocked/unreachable Move is complete for queue purposes.
     * Continue a Shift chain instead of stranding pending commands behind the
     * legacy hold pose. */
    if (G_UnitStartNextQueuedOrder(ent)) return;
    ent->build = NULL;
    ent->s.renderfx &= ~RF_NO_UBERSPLAT;
    ent->s.ability = 0;
    ent->movement.last_distance = 0;
    ent->movement.blocked_frames = 0;
    unit_setmove(ent, &move_move_hold);
}

static void ai_move_walk(LPEDICT ent) {
    FLOAT distance = M_DistanceToGoal(ent);
    FLOAT move_distance = unit_movedistance(ent);
    FLOAT const settle_distance = move_distance + ent->collision + MOVE_SLOT_MARGIN;
    BOOL blocked;

    if (move_should_arrive(ent, move_distance)) {
        /* Snap exactly onto the goal only if that spot is actually free; if the
         * goal is occupied (e.g. ordered onto another unit, or an attack target)
         * stop where we are rather than overlapping it. */
        if (M_MoveIsValid(ent, &ent->goalentity->s.origin2)) {
            ent->s.origin2 = ent->goalentity->s.origin2;
            gi.LinkEntity(ent);
        }
        ent->stand(ent);
    } else {
        blocked = move_is_blocked(ent, distance, move_distance);

        /* Plain Move owns a private destination, so location-aware steering
         * uses the mover footprint and retargets a disconnected click before
         * this behavior treats an unresolved route as terminal. */
        unit_changeangle(ent);

        if (ent->movement.flow_unreachable) {
            move_hold(ent); /* static topology says this goal cannot be reached */
            return;
        }
        if (!ent->movement.flow_direct && !ent->movement.path_valid && !ent->movement.flow_generation) {
            return; /* resumable route field is still being built */
        }
        if (ent->movement.flow_goal_reached) {
            return;
        }

        /* Restore the walk pose only after steering resolves a heading;
         * previously it advertised the stale facing throughout the pause. */
        unit_setanimation(ent, "walk");

        /* Retail move orders keep trying when another unit temporarily blocks
         * the path.  Preserve the old near-goal settle behavior so an occupied
         * final slot does not orbit forever, but do not cancel a distant move
         * merely because local avoidance failed for a short period. */
        if (blocked && ent->movement.last_distance <= settle_distance) {
            move_hold(ent);
            return;
        }
        if (blocked)
            ent->movement.blocked_frames = 0;
        unit_moveindirection(ent);
    }
}

static umove_t move_move_walk = { "walk", ai_move_walk, NULL, &a_move };

/* Set the unit's move target and begin walking.
 * goalentity must be a waypoint or any entity whose origin is the destination. */
void order_move(LPEDICT self, LPEDICT target) {
    if (S_GoldMineWorkerIsInside(self))
        return;
    if (self->aiflags & AI_IMMOBILE)
        return;
    self->goalentity = target;
    self->movement.attackmove_waypoint = NULL;
    self->movement.patrol_a = NULL;
    self->movement.patrol_b = NULL;
    self->movement.patrol_target = NULL;
    self->movement.follow_target = NULL;
    self->movement.holding_position = false;
    move_reset_progress(self);
    unit_setmove(self, &move_move_walk);
    /* No route heading exists at submission time. Hold the stand pose instead
     * of showing a walking unit facing its previous, often opposite, heading. */
    unit_setanimation(self, "stand");
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
    BOOL issued = false;
    DWORD num_units = move_collect_selected(clent->client, units, MAX_SELECTED_ENTITIES, &center);
    FLOAT spacing = move_slot_spacing(units, num_units);
    LPEDICT route_waypoint;

    if (num_units == 0) {
        return false;
    }
    /* A multi-unit move travels at the slowest member's speed so the group
     * stays together (WC3).  A lone unit keeps its own speed (cap 0). */
    FLOAT const group_speed = num_units > 1 ? move_group_speed(units, num_units) : 0;
    route_waypoint = clent->client->menu.order_queued ? NULL : Waypoint_add(location);

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
        if (!have_confirmation) {
            confirmation = target;
            have_confirmation = true;
        }
        if (clent->client->menu.order_queued) {
            /* Queued units may reach this leg at different times, so retain the
             * resolved per-unit slot and speed in the unit's own FIFO. */
            if (G_IssueUnitPointOrder(ent, "move", &target, true,
                                      clent->client->ps.number, group_speed)) {
                issued = true;
            }
        } else {
            LPEDICT waypoint = Waypoint_add(&target);
            waypoint->secondarygoal = route_waypoint;
            G_ClearUnitOrderQueue(ent);
            ent->movement.holding_position = false;
            order_move(ent, waypoint);
            ent->movement.group_speed = group_speed;  /* after order_move, which resets it */
            issued = true;
        }
    }
    if (issued) G_SendPointConfirmation(clent, &confirmation, false);
    return issued;
}

void move_command(LPEDICT ent) {
    UI_AddCancelButton(ent);
    ent->client->menu.on_location_selected = move_selectlocation;
    ent->client->menu.supports_order_queue = true;
}

ability_t a_move = {
    .cmd = move_command,
};
