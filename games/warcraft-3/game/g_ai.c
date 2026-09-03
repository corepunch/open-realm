#include "g_local.h"

#define NAVI_THRESHOLD 128.0f

/* Wrap an angle delta into [-PI, PI]. */
static FLOAT angle_wrap(FLOAT a) {
    while (a > (FLOAT)M_PI)  a -= 2.0f * (FLOAT)M_PI;
    while (a < -(FLOAT)M_PI) a += 2.0f * (FLOAT)M_PI;
    return a;
}

/* unit_changeangle is defined lower down — it needs the move-validity test and
 * the give-way helpers, which are declared below. */

extern ability_t a_move, a_attack, a_patrol;

/* A unit is actively executing a ground move order (right-click move). */
BOOL unit_is_walking(LPCEDICT ent) {
    return ent->currentmove && ent->currentmove->ability == &a_move;
}

/* Location orders own a legal ground endpoint; interaction orders route to an
 * entity centre and let their behavior-specific range decide arrival. */
static BOOL unit_routes_to_location(LPCEDICT ent) {
    if (!ent->currentmove)
        return false;
    if (ent->currentmove->ability == &a_move || ent->currentmove->ability == &a_patrol)
        return true;
    return ent->currentmove->ability == &a_attack && ent->goalentity == ent->movement.attackmove_waypoint;
}

/* Unit's effective current move speed.  Group moves travel at the slowest
 * member's speed so the selection stays a cohesive formation instead of
 * stringing out (WC3); the cap is gated on the move state so it never leaks
 * into a later attack/harvest order that reuses this.  Using the *capped*
 * speed means members of one group compare equal (no give-way within a group). */
static FLOAT unit_current_speed(LPCEDICT self) {
    FLOAT speed = self->unitinfo.MoveSpeed > 0
        ? self->unitinfo.MoveSpeed
        : self->UnitBalance->speed;
    if (self->movement.group_speed > 0 && self->movement.group_speed < speed && unit_is_walking(self)) {
        speed = self->movement.group_speed;
    }
    return speed;
}

FLOAT unit_movedistance(LPEDICT self) {
    return 10 * unit_current_speed(self) / FRAMETIME;
}

/* --- Collision-aware movement (block-and-slide) ---------------------------
 *
 * A unit only commits a step into a position that is free of walkable terrain
 * and of other units' collision circles.  When the steered heading is blocked
 * it tries progressively larger left/right deflections ("sliding"), so units
 * flow around obstacles instead of plowing through them.  Idle units are hard,
 * immovable obstacles: walking into one never displaces it (the WC3 invariant
 * that the old post-move push solver violated). */

#define MOVE_SLIDE_STEP        (15.0f * (FLOAT)M_PI / 180.0f)  /* deflection step */
#define MOVE_SLIDE_RINGS       6                               /* up to +/- 90 deg */
#define MOVE_SLIDE_RINGS_YIELD 2                               /* +/- 30 deg: faster unit holds its line */
#define MOVE_WORKER_QUEUE_TICKS 4                              /* same-stream blocker: queue before passing */
#define MOVE_WORKER_ESCAPE_TICKS 8                             /* widen bounded escape corridor after this */
#define MOVE_WORKER_CORRIDOR_RESET (30.0f * (FLOAT)M_PI / 180.0f)
#define MOVE_WORKER_MAX_DEVIATION 5.0f                         /* collision radii */
#define MOVE_WORKER_ESCAPE_DEVIATION 6.0f                      /* collision radii */
#define MAX_MOVE_COLLIDERS     256

typedef enum {
    MOVE_AVOID_GENERIC,
    MOVE_AVOID_RESOURCE_WORKER,
} moveAvoidPolicy_t;

static LPEDICT trymove_self = NULL;
static LPEDICT trymove_blocker = NULL;  /* unit that rejected the last candidate (NULL = clear or terrain) */
static LPEDICT trymove_colliders[MAX_MOVE_COLLIDERS];

static BOOL unit_is_flying(LPCEDICT ent) {
    return (ent->aiflags & AI_FLYING) != 0;
}

/* BoxEdicts predicate: solid units/buildings sharing this mover's collision
 * layer.  Excludes self, hollow entities, zero-collision entities (waypoints,
 * effects, missiles), and the opposite air/ground layer (flyers and ground
 * units pass through each other). */
static BOOL filter_blockers(LPCEDICT ent) {
    if (ent == trymove_self || IS_HOLLOW(ent) || ent->collision <= 0.0f)
        return false;
    /* Trees have collisionSize 0 (they block only via their baked footprint) so
     * they are already excluded above; buildings keep a real collision circle
     * and ARE counted here — relying on the terrain footprint alone let units
     * walk through buildings (coarse 32u cells, runtime-spawned statics not yet
     * baked).  Flyers and ground units are on separate layers. */
    return unit_is_flying(ent) == unit_is_flying(trymove_self);
}

/* Distance from point p to the segment [a,b]. */
static FLOAT point_segment_distance(LPCVECTOR2 a, LPCVECTOR2 b, LPCVECTOR2 p) {
    VECTOR2 const ab = Vector2_sub(b, a);
    VECTOR2 const ap = Vector2_sub(p, a);
    FLOAT const ab2 = ab.x * ab.x + ab.y * ab.y;
    FLOAT t = ab2 > 0.0001f ? (ap.x * ab.x + ap.y * ab.y) / ab2 : 0.0f;
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;
    VECTOR2 const closest = { a->x + t * ab.x, a->y + t * ab.y };
    return Vector2_distance(&closest, p);
}

/* Is the position 'cand' free for 'self' (static world + other units)?  On a
 * unit rejection, records the blocking unit in trymove_blocker (NULL otherwise)
 * so the slide can apply speed-priority give-way. */
static BOOL move_is_valid(LPEDICT self, LPCVECTOR2 cand) {
    trymove_blocker = NULL;
    /* Pathing-disabled units (SetUnitPathing(false), scripted moves) ignore
     * all collision, matching the old unconditional translate. */
    if (self->no_pathing)
        return true;

    /* Static world: terrain + baked building footprints (pathmap.original). */
    if (!CM_PointIsPathableForRadius(cand, self->collision))
        return false;
    /* WC3's pathing grid rejects a swept step that cuts a diagonal corner. Keep
     * the escape case for units spawned inside stale/changed pathing, where the
     * endpoint remains the authoritative legal position. */
    if (CM_PointIsPathableForRadius(&self->s.origin2, self->collision) &&
        !CM_LineIsWalkableForRadius(&self->s.origin2, cand, self->collision))
        return false;

    /* Dynamic units: precise circle test.  The "don't deepen penetration" rule
     * ignores a neighbour the unit already overlaps unless the candidate moves
     * closer to it, so units that start overlapped (spawn / blink / a building
     * dropped on them) can still slide apart instead of dead-locking. */
    /* Broad-phase box must cover the whole swept segment (origin -> cand), not
     * just the endpoint: a fast unit's step spans many units, and a box centred
     * on cand would miss a blocker sitting near the START of the path — letting
     * the unit jump clean over it.  BoxEdicts tests each entity's bounds (which
     * already extend by its own collision radius), so inflating by self's radius
     * is enough to catch any blocker within rr of the corridor. */
    FLOAT const reach = self->collision + 1.0f;
    FLOAT const ox = self->s.origin2.x, oy = self->s.origin2.y;
    BOX2 const box = {
        { (ox < cand->x ? ox : cand->x) - reach, (oy < cand->y ? oy : cand->y) - reach },
        { (ox > cand->x ? ox : cand->x) + reach, (oy > cand->y ? oy : cand->y) + reach },
    };
    trymove_self = self;
    DWORD const num = gi.BoxEdicts(&box, trymove_colliders, MAX_MOVE_COLLIDERS, filter_blockers);
    FOR_LOOP(i, num) {
        LPEDICT const b = trymove_colliders[i];
        FLOAT const rr = self->collision + b->collision;
        /* Swept test: the unit's whole PATH this tick (origin -> cand) must
         * clear b, not just the endpoint — otherwise a fast unit (step ~one
         * cell) jumps clean over a smaller unit between ticks.  Mirrors WC3's
         * swept-circle collision. */
        FLOAT const seg_d = point_segment_distance(&self->s.origin2, cand, &b->s.origin2);
        if (seg_d >= rr)
            continue;  /* the swept path clears b */
        FLOAT const cur_d = Vector2_distance(&self->s.origin2, &b->s.origin2);
        if (cur_d < rr && seg_d >= cur_d - 0.5f)
            continue;  /* already overlapping b: allow only a step whose path does
                        * not go deeper into b — lets it separate, never slide
                        * tangentially or jump THROUGH it. */
        trymove_blocker = b;
        return false;
    }
    return true;
}

/* Public: would 'pos' be a free standing spot for 'self' (terrain + units)?
 * Used by the move arrival to avoid snapping a unit onto an occupied goal. */
BOOL M_MoveIsValid(LPEDICT self, LPCVECTOR2 pos) {
    return move_is_valid(self, pos);
}

static void unit_commit_step(LPEDICT self, LPCVECTOR2 cand) {
    if (self->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    self->s.origin2 = *cand;
    gi.LinkEntity(self);
}

/* Advance the unit one tick.  Avoidance is decided ONCE per tick in
 * unit_changeangle (which picks a free heading via unit_desired_heading and
 * turns the facing toward it); this function only commits the step.  WC3 moves a
 * unit ALONG ITS FACING, so we try the facing first; if the facing momentarily
 * lags into an obstacle while it is still turning toward the chosen heading, we
 * fall back to that already-validated heading so the unit keeps progressing
 * around the obstacle instead of stalling.
 *
 * We deliberately do NOT run a second deflection search here.  The previous
 * version searched +/- slide rings off the (turn-rate-lagged) facing, which
 * disagreed with the heading unit_changeangle had already chosen and re-decided
 * a different direction every tick — that disagreement is what made units
 * visibly rotate/wobble and crab sideways past each other and trees. */
void unit_moveindirection(LPEDICT self) {
    if (self->aiflags & AI_IMMOBILE)
        return;

    /* unit_changeangle* clears both routing fields before resolving this
     * tick's heading.  A resumable cache miss deliberately leaves both clear;
     * in that state there is no valid movement decision yet.  Never commit a
     * step using the unit's previous facing/heading while the requested route
     * is still being built.  This is the common safety net for Move, Harvest,
     * Patrol, Attack, Build, Repair, and resource-return walkers. */
    if (!self->movement.flow_direct && !self->movement.path_valid && self->movement.flow_generation == 0)
        return;

    FLOAT const dist = unit_movedistance(self);
    VECTOR2 const by_facing = Vector2_mad(&self->s.origin2, dist,
                                          &MAKE(VECTOR2, cosf(self->s.angle), sinf(self->s.angle)));
    if (move_is_valid(self, &by_facing)) {
        unit_commit_step(self, &by_facing);
        return;
    }
    VECTOR2 const by_heading = Vector2_mad(&self->s.origin2, dist,
                                           &MAKE(VECTOR2, cosf(self->movement.heading), sinf(self->movement.heading)));
    if (move_is_valid(self, &by_heading)) {
        unit_commit_step(self, &by_heading);
    }
}

/* Turn the facing vector toward a target heading by at most the unit's turn
 * rate ('umvr', radians/tick; WC3 default 0.5).  Pure 2-D vector math (cross =
 * signed sin of the angle to turn, dot = cos); atan2 only writes the canonical
 * s.angle the renderer/network consume. */
static void unit_turn_toward(LPEDICT self, FLOAT target) {
    VECTOR2 const facing = { cosf(self->s.angle), sinf(self->s.angle) };
    VECTOR2 const goal   = { cosf(target), sinf(target) };
    FLOAT const cross = facing.x * goal.y - facing.y * goal.x;
    FLOAT const dot   = facing.x * goal.x + facing.y * goal.y;
    FLOAT turn = self->UnitData->turnRate;
    if (turn <= 0.0f) turn = 0.5f;

    if (dot >= cosf(turn)) {
        self->s.angle = target;  /* within one tick's turn: snap */
    } else {
        FLOAT const st = cross >= 0.0f ? sinf(turn) : -sinf(turn);
        FLOAT const ct = cosf(turn);
        VECTOR2 const nf = { facing.x * ct - facing.y * st,
                             facing.x * st + facing.y * ct };
        self->s.angle = atan2f(nf.y, nf.x);
    }
}

/* Resource workers need a different local crowd rule from ordinary combat
 * movement.  A same-direction worker is a queue, not an obstacle to weave
 * around; crossing traffic may pass immediately.  This is the minimal policy
 * that stayed close to the direct Human02 resource corridor in the 30-worker
 * simulation while still breaking counterflow deadlocks. */
static BOOL unit_worker_same_stream(LPCEDICT blocker, FLOAT goal_angle) {
    VECTOR2 dir, goal;
    FLOAT len;

    if (!blocker || !blocker->currentmove || !blocker->goalentity ||
        (blocker->aiflags & AI_IMMOBILE))
        return false;
    dir = Vector2_sub(&blocker->goalentity->s.origin2, &blocker->s.origin2);
    len = Vector2_len(&dir);
    if (len <= 0.001f)
        return false;
    goal = MAKE(VECTOR2, cosf(goal_angle), sinf(goal_angle));
    return Vector2_dot(&goal, &dir) / len > 0.25f;
}

static FLOAT unit_worker_lateral_deviation(LPCEDICT self, LPCVECTOR2 point) {
    VECTOR2 const delta = Vector2_sub(point, &self->movement.worker_avoid_origin);
    VECTOR2 const direct = { cosf(self->movement.worker_avoid_heading),
                             sinf(self->movement.worker_avoid_heading) };
    return fabsf(direct.x * delta.y - direct.y * delta.x);
}

static FLOAT unit_worker_desired_heading(LPEDICT self, FLOAT goal_angle, FLOAT dist) {
    VECTOR2 const straight = Vector2_mad(&self->s.origin2, dist,
                                         &MAKE(VECTOR2, cosf(goal_angle), sinf(goal_angle)));
    LPEDICT blocker;
    FLOAT max_deviation;

    if (move_is_valid(self, &straight)) {
        self->movement.worker_avoid_blocked_frames = 0;
        self->movement.worker_avoid_active = false;
        return goal_angle;
    }

    blocker = trymove_blocker;
    if (!self->movement.worker_avoid_active ||
        fabsf(angle_wrap(goal_angle - self->movement.worker_avoid_heading)) >
            MOVE_WORKER_CORRIDOR_RESET) {
        self->movement.worker_avoid_origin = self->s.origin2;
        self->movement.worker_avoid_heading = goal_angle;
        self->movement.worker_avoid_blocked_frames = 0;
        self->movement.worker_avoid_active = true;
    }
    self->movement.worker_avoid_blocked_frames++;

    /* Do not turn a short pause in a resource stream into overtaking.  Four
     * blocked decisions let the queue advance naturally; a genuinely pinned
     * queue then gets the same bounded escape used for crossing traffic. */
    if (unit_worker_same_stream(blocker, goal_angle) &&
        self->movement.worker_avoid_blocked_frames <= MOVE_WORKER_QUEUE_TICKS)
        return goal_angle;

    max_deviation = self->collision *
        (self->movement.worker_avoid_blocked_frames <= MOVE_WORKER_ESCAPE_TICKS
            ? MOVE_WORKER_MAX_DEVIATION : MOVE_WORKER_ESCAPE_DEVIATION);

    /* Deterministic right-hand passing avoids the +/- re-decision that made
     * packed Peasants dance.  Retry the exact direct heading next think; no
     * passing lane is cached. */
    for (int sign = -1; sign <= 1; sign += 2) {
        for (int ring = 1; ring <= MOVE_SLIDE_RINGS; ring++) {
            FLOAT const angle = angle_wrap(goal_angle + sign * ring * MOVE_SLIDE_STEP);
            VECTOR2 const cand = Vector2_mad(&self->s.origin2, dist,
                                             &MAKE(VECTOR2, cosf(angle), sinf(angle)));
            if (unit_worker_lateral_deviation(self, &cand) > max_deviation)
                continue;
            if (move_is_valid(self, &cand)) {
                self->movement.worker_avoid_blocked_frames = 0;
                return angle;
            }
        }
    }
    return goal_angle;
}

/* Pick the heading the unit actually wants to move along this tick.  Generic
 * units retain speed-priority block-and-slide; resource workers use the
 * queue/pass-right policy above. */
static FLOAT unit_desired_heading(LPEDICT self, FLOAT goal_angle, FLOAT dist,
                                  moveAvoidPolicy_t policy) {
    VECTOR2 const straight = Vector2_mad(&self->s.origin2, dist,
                                         &MAKE(VECTOR2, cosf(goal_angle), sinf(goal_angle)));
    if (policy == MOVE_AVOID_RESOURCE_WORKER)
        return unit_worker_desired_heading(self, goal_angle, dist);
    if (move_is_valid(self, &straight))
        return goal_angle;

    int max_rings = MOVE_SLIDE_RINGS;
    LPEDICT const b = trymove_blocker;
    if (b && unit_is_walking(self) && unit_is_walking(b) &&
        unit_current_speed(self) > unit_current_speed(b)) {
        max_rings = MOVE_SLIDE_RINGS_YIELD;
    }
    for (int ring = 1; ring <= max_rings; ring++) {
        for (int sign = 1; sign >= -1; sign -= 2) {
            FLOAT const angle = angle_wrap(goal_angle + sign * ring * MOVE_SLIDE_STEP);
            VECTOR2 const cand = Vector2_mad(&self->s.origin2, dist,
                                             &MAKE(VECTOR2, cosf(angle), sinf(angle)));
            if (move_is_valid(self, &cand))
                return angle;
        }
    }
    return goal_angle;  /* boxed in: aim at the goal, hold (move step will fail) */
}

static void unit_apply_heading(LPEDICT self, LPCVECTOR2 dir, moveAvoidPolicy_t policy) {
    FLOAT const dirlen = Vector2_len(dir);
    if (dirlen <= 0.001f)
        return;  /* no meaningful heading this tick: hold current facing */

    /* Local avoidance resolves into ONE heading; the facing turns toward it and
     * the move step (unit_moveindirection) follows it, keeping facing and motion
     * aligned (no second, disagreeing search). */
    FLOAT const goal_angle = atan2f(dir->y, dir->x);
    FLOAT const desired = unit_desired_heading(self, goal_angle,
                                                unit_movedistance(self), policy);
    self->movement.heading = desired;
    unit_turn_toward(self, desired);
}

static void unit_changeangle_towards_point_policy(LPEDICT self, LPCVECTOR2 point,
                                                   moveAvoidPolicy_t policy) {
    VECTOR2 dir;

    if (!self || !point || (self->aiflags & AI_IMMOBILE))
        return;
    self->movement.heading = self->s.angle;
    self->movement.flow_generation = 0;
    self->movement.flow_goal_reached = false;
    self->movement.flow_unreachable = false;
    self->movement.flow_direct = true;
    dir = Vector2_sub(point, &self->s.origin2);
    unit_apply_heading(self, &dir, policy);
}

/* Keep the bounded point-route turn until it is reached; retail likewise owns
 * route progress on each mover instead of rebuilding from its current point. */
static BOOL unit_accel_direction(LPEDICT self, FLOAT radius, LPVECTOR2 dir) {
    VECTOR2 const target = self->goalentity->s.origin2;
    FLOAT const reached = CM_PathCellWorldSize();

    if (self->movement.path_valid &&
        (Vector2_distance(&self->movement.path_target, &target) >= 1.0f ||
         fabsf(self->movement.path_radius - radius) >= 0.01f ||
         Vector2_distance(&self->s.origin2, &self->movement.path_waypoint) <= reached ||
         !CM_LineIsWalkableForRadius(&self->s.origin2, &self->movement.path_waypoint, radius)))
        self->movement.path_valid = false;
    if (!self->movement.path_valid) {
        pathAccelParams_t params = { &self->s.origin2, &target, radius };
        if (!CM_FindPathWaypoint(&params, &self->movement.path_waypoint)) return false;
        self->movement.path_target = target; self->movement.path_radius = radius; self->movement.path_valid = true;
    }
    *dir = Vector2_sub(&self->movement.path_waypoint, &self->s.origin2);
    return true;
}

void unit_changeangle_towards_point(LPEDICT self, LPCVECTOR2 point) {
    unit_changeangle_towards_point_policy(self, point, MOVE_AVOID_GENERIC);
}

void unit_changeangle_towards_point_worker(LPEDICT self, LPCVECTOR2 point) {
    unit_changeangle_towards_point_policy(self, point, MOVE_AVOID_RESOURCE_WORKER);
}

static void unit_changeangle_policy(LPEDICT self, moveAvoidPolicy_t policy) {
    if (self->aiflags & AI_IMMOBILE)
        return;
    VECTOR2 to_goal = Vector2_sub(&self->goalentity->s.origin2, &self->s.origin2);
    VECTOR2 dir;
    FLOAT const radius = unit_routes_to_location(self) ? self->collision : 0.0f;

    self->movement.heading = self->s.angle;  /* default if no heading is resolved this tick */
    self->movement.flow_generation = 0;
    self->movement.flow_goal_reached = false;
    self->movement.flow_unreachable = false;
    self->movement.flow_direct = false;

    /* Generic interaction movement keeps the original point-route contract.
     * Attack, mine entry, resource return, repair, and other ranged behaviors
     * decide when their interaction boundary has been reached.  Do not stop
     * those orders at a collision-expanded flow goal outside that boundary.
     * Move orders own radius-valid reserved destinations, so their route must
     * use the same footprint as move-time collision; point routing previously
     * sent units into narrow gaps and touching obstacle corners. */
    if (CM_LineIsWalkableForRadius(&self->s.origin2, &self->goalentity->s.origin2, radius)) {
        self->movement.path_valid = false;
        self->movement.flow_direct = true;
        dir = to_goal;
    } else {
        DWORD heatmap = M_RefreshHeatmap(self->goalentity, radius);
        self->movement.flow_generation = heatmap;
        if (!heatmap) {
            if (!unit_accel_direction(self, radius, &dir))
                return; /* long incremental route is still building; keep the order */
            /* path_valid resolves the heading while the shared field builds;
             * this is not a direct line to the requested destination. */
            unit_apply_heading(self, &dir, policy);
            return;
        }
        self->movement.path_valid = false;
        if (CM_FlowReachedGoal(heatmap, self->s.origin.x, self->s.origin.y)) {
            /* Location orders stop at their collision-safe route endpoint in
             * the owning behavior.  Interaction orders use a point field whose
             * raw target may be blocked (mine/building/unit centre); once the
             * adjusted route end is reached they must steer toward the real
             * target so the behavior's footprint/range check can complete. */
            self->movement.flow_goal_reached = true;
            dir = to_goal;
        } else {
            dir = get_flow_direction(heatmap, self->s.origin.x, self->s.origin.y);
            if (Vector2_len(&dir) <= 0.001f) {
                self->movement.flow_unreachable = !CM_FlowCanReach(heatmap, self->s.origin.x, self->s.origin.y);
                /* Location targets are private waypoints.  When the clicked static
                 * component is unreachable, replace the waypoint with the
                 * closest legal point in this mover's component; aiming at the
                 * raw click made local avoidance walk forever along walls. */
                if (radius > 0.0f && self->movement.flow_unreachable) {
                    VECTOR2 closest;
                        if (CM_ClosestReachablePointForRadius(
                            &self->s.origin2, &self->goalentity->s.origin2, radius, &closest)) {
                        self->goalentity->s.origin2 = closest;
                        self->goalentity->secondarygoal = NULL;
                        self->goalentity->heatmap2 = 0;
                        self->goalentity->heatmap2_radius = 0;
                        move_reset_progress(self);
                    }
                    return;
                }
                return;
            }
        }
    }

    unit_apply_heading(self, &dir, policy);
}

void unit_changeangle(LPEDICT self) {
    unit_changeangle_policy(self, MOVE_AVOID_GENERIC);
}

void unit_changeangle_worker(LPEDICT self) {
    unit_changeangle_policy(self, MOVE_AVOID_RESOURCE_WORKER);
}

/* Behaviors that route around authored blocked geometry may request a
 * collision-sized field. Lumber uses its route-end state to retarget an
 * unreachable tree. Build and Repair instead route toward behavior-owned legal
 * approach points, so reaching the adjusted flow goal never changes their
 * gameplay target. Generic point movement continues through unit_changeangle(). */
static void unit_changeangle_for_radius_policy(LPEDICT self, FLOAT radius,
                                               moveAvoidPolicy_t policy) {
    if (self->aiflags & AI_IMMOBILE)
        return;
    VECTOR2 to_goal = Vector2_sub(&self->goalentity->s.origin2, &self->s.origin2);
    VECTOR2 dir;

    self->movement.heading = self->s.angle;
    self->movement.flow_generation = 0;
    self->movement.flow_goal_reached = false;
    self->movement.flow_unreachable = false;
    self->movement.flow_direct = false;

    if (CM_LineIsWalkableForRadius(&self->s.origin2,
                                   &self->goalentity->s.origin2,
                                   radius)) {
        self->movement.path_valid = false;
        self->movement.flow_direct = true;
        dir = to_goal;
    } else {
        DWORD heatmap = M_RefreshHeatmap(self->goalentity, radius);
        self->movement.flow_generation = heatmap;
        if (!heatmap) {
            if (!unit_accel_direction(self, radius, &dir))
                return; /* long incremental route is still building */
            unit_apply_heading(self, &dir, policy);
            return;
        }
        self->movement.path_valid = false;

        if (CM_FlowReachedGoal(heatmap, self->s.origin.x, self->s.origin.y)) {
            self->movement.flow_goal_reached = true;
            return;
        }
        dir = get_flow_direction(heatmap, self->s.origin.x, self->s.origin.y);
        if (Vector2_len(&dir) <= 0.001f) {
            self->movement.flow_unreachable =
                !CM_FlowCanReach(heatmap, self->s.origin.x, self->s.origin.y);
            return;
        }
    }

    unit_apply_heading(self, &dir, policy);
}

void unit_changeangle_for_radius(LPEDICT self, FLOAT radius) {
    unit_changeangle_for_radius_policy(self, radius, MOVE_AVOID_GENERIC);
}

void unit_changeangle_for_radius_worker(LPEDICT self, FLOAT radius) {
    unit_changeangle_for_radius_policy(self, radius, MOVE_AVOID_RESOURCE_WORKER);
}

void unit_setanimation(LPEDICT self, LPCSTR anim) {
    self->animation = G_GetAnimation(self->s.model, anim);
}

static BOOL unit_is_active_repair_move(LPEDICT self) {
    char rawcode[5];
    ability_t const *handler;

    if (!self || !self->currentmove || !self->buildwork.ability) return false;
    memcpy(rawcode, &self->buildwork.ability, 4);
    rawcode[4] = '\0';
    handler = FindAbilityForCommand(rawcode);
    return handler && self->currentmove->ability == handler;
}

void unit_setmove(LPEDICT self, umove_t *move) {
    BOOL was_idle = G_UnitIsIdleWorker(self);

    /* buildwork.ability is staged before Repair switches from the worker's
     * existing stand/move behavior. Only an OLD Repair move means this
     * transition is actually leaving Repair; otherwise cancelling here erases
     * the new target before the Repair walk can begin. */
    if (self->currentmove && self->currentmove->ability != move->ability &&
        unit_is_active_repair_move(self)) {
        S_CancelRepair(self);
    }
    self->currentmove = move;
    self->animation = G_GetAnimation(self->s.model, move->animation);
    if (self->animation) {
        // skip
    } else if (strstr(move->animation, "run")) {
        self->animation = G_GetAnimation(self->s.model, "walk");
    } else if (strstr(move->animation, "stand ")) {
        self->animation = G_GetAnimation(self->s.model, "stand");
    } else if (strstr(move->animation, "attack ")) {
        self->animation = G_GetAnimation(self->s.model, "attack");
    }
    if (was_idle != G_UnitIsIdleWorker(self)) {
        G_InvalidateUnitShortcutsForUnit(self);
    }
}

void unit_runwait(LPEDICT self, void (*callback)(LPEDICT )) {
    if (self->wait <= 0)
        return;
    if (self->wait > FRAMETIME / 1000.f) {
        self->wait -= FRAMETIME / 1000.f;
    } else {
        self->wait = 0;
        callback(self);
    }
}

void ai_idle(LPEDICT self) {
}

void order_attack(LPEDICT self, LPEDICT target);

#define MAX_SIGHT_ENTITIES 256

static LPEDICT ai_current_entity = NULL;
static LPEDICT sight_entities[MAX_SIGHT_ENTITIES];

static BOOL filter_sight(LPCEDICT ent) {
    if (!(ent->svflags & SVF_MONSTER) || !ai_current_entity ||
        ai_current_entity->s.player >= MAX_PLAYERS || ent->s.player >= MAX_PLAYERS ||
        ent->s.player == ai_current_entity->s.player)
        return false;
    /* Friend/enemy is the acquiring player's directional PASSIVE alliance.
     * Shared vision/control/XP alone must never suppress hostile acquisition. */
    if (level.alliances[ai_current_entity->s.player][ent->s.player] & (1 << ALLIANCE_PASSIVE))
        return false;
    if (ent->svflags & SVF_DEADMONSTER)
        return false;
    if (ent->runtime.flags & UNIT_BALANCE_BUILDING)
        return false;
    /* Local acquisition is a unit behavior, not a Human-vs-AI special case.
     * Computer allies must be able to defend against other computer players.
     * Neutral units already do not initiate from ai_stand(); keep Neutral
     * Passive widgets out of ordinary hostile acquisition as well. */
    if (ent->s.player == PLAYER_NEUTRAL_PASSIVE)
        return false;
    return true;
}

/* Does this unit have an attack to acquire targets with? */
static BOOL unit_has_attack(LPCEDICT self) {
    return self->attack1.cooldown > 0.0f &&
           (self->attack1.damageBase > 0 || self->attack1.numberOfDice > 0);
}

/* Throttle target re-acquisition: units scan only a few times per second,
 * staggered by entity index, instead of every sim tick. */
#define AI_ACQUIRE_INTERVAL 300 /* ms */

BOOL G_ShouldAcquireThisFrame(LPCEDICT self) {
    DWORD const stagger = (DWORD)(self - g_edicts) % AI_ACQUIRE_INTERVAL;
    return ((level.time + stagger) % AI_ACQUIRE_INTERVAL) < (DWORD)FRAMETIME;
}

/* Return the spawn-cached range; repeated SLK walks dominated large acquisition scans. */
FLOAT G_AcquisitionRange(LPCEDICT self) {
    return self->runtime.acquisition_range;
}

LPEDICT G_FindNearestEnemy(LPEDICT self, FLOAT radius) {
    ai_current_entity = self;
    BOX2 const sightbox = {
        { self->s.origin2.x - radius, self->s.origin2.y - radius },
        { self->s.origin2.x + radius, self->s.origin2.y + radius },
    };
    DWORD numents = gi.BoxEdicts(&sightbox, sight_entities, MAX_SIGHT_ENTITIES, filter_sight);
    LPEDICT best = NULL;
    FLOAT best_dist = radius;
    FOR_LOOP(i, numents) {
        LPEDICT ent = sight_entities[i];
        FLOAT const d = Vector2_distance(&ent->s.origin2, &self->s.origin2);
        if (d < best_dist) {
            best_dist = d;
            best = ent;
        }
    }
    return best;
}

void ai_stand(LPEDICT self) {
    if (!(self->svflags & SVF_MONSTER))
        return;
    /* Neutral/creep units do not initiate (avoids map-wide neutral-vs-neutral
     * aggression); campaign defenders may be rescuable until script takeover
     * and still use normal unit auto-acquisition while hostile. */
    if (level.mapinfo->players[self->s.player].playerType == kPlayerTypeNeutral)
        return;
    if (!G_ShouldAcquireThisFrame(self))
        return;

    /* Autocast gets the first acquisition opportunity. Its ability owns target
     * policy and emits an ordinary order; only if no autocast action starts do
     * we fall through to the existing automatic attack scan. */
    if (G_TryUnitAutocast(self))
        return;

    /* Idle units auto-engage the nearest enemy within acquisition range — for
     * the player's own units too. Units with no attack (workers/critters) and
     * units already chasing/attacking stay as they are. */
    if (!unit_has_attack(self))
        return;

    LPEDICT best = G_FindNearestEnemy(self, G_AcquisitionRange(self));
    if (best) {
        order_attack(self, best);
    }
}

void ai_birth(LPEDICT self) {
}

void ai_pain(LPEDICT self) {
}
