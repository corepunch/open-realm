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
 * Steering, collision-aware steps, route goals, and support heights are owned here.
 */
#include "s_skills.h"

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
#define BZ_MOVE_FALLBACK_RETRY_MS 500 // milliseconds; bounds repeated unreachable floods; used as the retry interval

typedef struct {
    VECTOR2 point;
    FLOAT radius;
} moveSlot_t;

#define MOVE_SLIDE_STEP BZ_ROUTE_SLIDE_STEP
#define MOVE_SLIDE_RINGS BZ_ROUTE_SLIDE_RINGS
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
    MOVE_AVOID_STATIC_ONLY,
} moveAvoidPolicy_t;

typedef enum {
    MOVE_COLLIDE_UNITS,
    MOVE_IGNORE_UNITS,
} moveCollisionPolicy_t;

static LPEDICT trymove_self = NULL;
static LPEDICT trymove_blocker = NULL;  /* unit that rejected the last candidate (NULL = clear or terrain) */
static LPEDICT trymove_colliders[MAX_MOVE_COLLIDERS];

/* Keep a failed exceptional route search from monopolizing the frame while
 * the same goal remains unreachable; order changes clear this state below. */
static BOOL move_fallback_throttled(LPEDICT self, LPCVECTOR2 target, FLOAT radius) {
    if (self->movement.flow_fallback_state == MOVE_FALLBACK_APPLIED &&
        self->movement.flow_fallback_goal == self->goalentity) {
        self->movement.flow_unreachable = true;
        return true;
    }
    if (self->movement.flow_fallback_state != MOVE_FALLBACK_RETRY)
        return false;
    if (self->movement.flow_fallback_goal != self->goalentity ||
        fabsf(self->movement.flow_fallback_target.x - target->x) >= 0.01f ||
        fabsf(self->movement.flow_fallback_target.y - target->y) >= 0.01f ||
        fabsf(self->movement.flow_fallback_radius - radius) >= 0.01f ||
        (DWORD)(level.time - self->movement.flow_fallback_time) >= BZ_MOVE_FALLBACK_RETRY_MS) {
        self->movement.flow_fallback_state = MOVE_FALLBACK_NONE;
        return false;
    }
    self->movement.flow_unreachable = true;
    return true;
}

/* Wrap an angle delta into [-PI, PI]. */
static FLOAT angle_wrap(FLOAT a) {
    while (a > (FLOAT)M_PI)  a -= 2.0f * (FLOAT)M_PI;
    while (a < -(FLOAT)M_PI) a += 2.0f * (FLOAT)M_PI;
    return a;
}

/* unit_changeangle is defined lower down — it needs the move-validity test and
 * the give-way helpers, which are declared below. */

/* A unit is actively executing a ground move order (right-click move). */
BOOL unit_is_walking(LPCEDICT ent) {
    return ent->currentmove && ent->currentmove->proc == CAbilityMove;
}

/* Location orders own a legal ground endpoint; interaction orders route to an
 * entity centre and let their behavior-specific range decide arrival. */
static BOOL unit_routes_to_location(LPCEDICT ent) {
    if (!ent->currentmove)
        return false;
    if (ent->currentmove->proc == CAbilityMove || ent->currentmove->proc == CAbilityPatrol)
        return true;
    return ent->currentmove->proc == CAbilityAttack && ent->goalentity == ent->movement.attackmove_waypoint;
}

/* Unit's effective current move speed.  Group moves travel at the slowest
 * member's speed so the selection stays a cohesive formation instead of
 * stringing out (WC3); the cap is gated on the move state so it never leaks
 * into a later attack/harvest order that reuses this.  Using the *capped*
 * speed means members of one group compare equal (no give-way within a group). */
static FLOAT unit_current_speed(LPCEDICT self) {
    FLOAT speed = self->unitinfo.MoveSpeed > 0
        ? self->unitinfo.MoveSpeed
        : self->data.UnitBalance->speed;
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

static BOOL unit_is_flying(LPCEDICT ent) {
    return ent && (ent->aiflags & AI_FLYING) != 0;
}

BYTE M_UnitStaticPathingFlags(LPCEDICT ent) {
    return unit_is_flying(ent) ? CM_PATHING_UNFLYABLE : CM_PATHING_UNWALKABLE;
}

/* BoxEdicts predicate: solid units/buildings sharing this mover's collision
 * layer.  Excludes self, hollow entities, zero-collision entities (waypoints,
 * effects, missiles), and the opposite air/ground layer (flyers and ground
 * units pass through each other). */
static BOOL filter_blockers(LPCEDICT ent) {
    if (ent == trymove_self || IS_HOLLOW(ent) || ent->collision <= 0.0f)
        return false;
    /* An alive walkable destructable is a ground surface, not a circle-shaped
     * obstacle. Its authored path texture remains responsible for deck edges. */
    if (G_IsDestructable(ent) && !ent->destructable.dead &&
        ent->destructable.placement_solid && ent->pathtex &&
        ent->data.DestructableData && ent->data.DestructableData->walkable) return false;
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
static BOOL move_is_valid_policy(LPEDICT self, LPCVECTOR2 cand,
                                 moveCollisionPolicy_t collision_policy) {
    BYTE const blocked_flags = M_UnitStaticPathingFlags(self);
    trymove_blocker = NULL;
    /* Pathing-disabled units (SetUnitPathing(false), scripted moves) ignore
     * all collision, matching the old unconditional translate. */
    if (self->no_pathing)
        return true;

    /* Static world: terrain + baked building footprints (pathmap.original). */
    if (!CM_PointIsPathableForRadiusFlags(cand, self->collision, blocked_flags))
        return false;
    /* WC3's pathing grid rejects a swept step that cuts a diagonal corner. Keep
     * the escape case for units spawned inside stale/changed pathing, where the
     * endpoint remains the authoritative legal position. */
    if (CM_PointIsPathableForRadiusFlags(&self->s.origin2, self->collision, blocked_flags) &&
        !CM_LineIsPathableForRadiusFlags(&self->s.origin2, cand, self->collision, blocked_flags))
        return false;

    if (collision_policy == MOVE_IGNORE_UNITS)
        return true;

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

static BOOL move_is_valid(LPEDICT self, LPCVECTOR2 cand) {
    return move_is_valid_policy(self, cand, MOVE_COLLIDE_UNITS);
}

/* Shared steering uses the same static-only policy as resource interaction movement. */
static BOOL move_static_is_valid(LPEDICT self, LPCVECTOR2 cand) { return move_is_valid_policy(self, cand, MOVE_IGNORE_UNITS); }

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
static void unit_moveindirection_policy(LPEDICT self,
                                        moveCollisionPolicy_t collision_policy) {
    if (self->aiflags & AI_IMMOBILE)
        return;

    /* unit_changeangle* clears both routing fields before resolving this
     * tick's heading.  A resumable cache miss deliberately leaves both clear;
     * in that state there is no valid movement decision yet.  Never commit a
     * step using the unit's previous facing/heading while the requested route
     * is still being built.  This is the common safety net for Move, Harvest,
     * Patrol, Attack, Build, Repair, and resource-return walkers. */
    if (!self->movement.flow_direct && !self->movement.path.valid && self->movement.flow_generation == 0)
        return;

    FLOAT const dist = unit_movedistance(self);
    VECTOR2 const by_facing = Vector2_mad(&self->s.origin2, dist,
                                          &MAKE(VECTOR2, cosf(self->s.angle), sinf(self->s.angle)));
    if (move_is_valid_policy(self, &by_facing, collision_policy)) {
        unit_commit_step(self, &by_facing);
        return;
    }
    VECTOR2 const by_heading = Vector2_mad(&self->s.origin2, dist,
                                           &MAKE(VECTOR2, cosf(self->movement.heading), sinf(self->movement.heading)));
    if (move_is_valid_policy(self, &by_heading, collision_policy)) {
        unit_commit_step(self, &by_heading);
    }
}

void unit_moveindirection(LPEDICT self) {
    unit_moveindirection_policy(self, MOVE_COLLIDE_UNITS);
}

void unit_moveindirection_ignore_units(LPEDICT self) {
    unit_moveindirection_policy(self, MOVE_IGNORE_UNITS);
}

/* Interaction routing may finish at a collision-safe staging point rather
 * than at the blocked building centre.  When that endpoint is within this
 * tick's movement budget, land exactly on it instead of stepping past it and
 * selecting it again from the opposite side next think.  This is the same
 * arrival snap used by ordinary Move, but deliberately ignores live units for
 * Warsmash-style Mine/drop-off legs while retaining all static pathing. */
BOOL unit_snap_to_point_ignore_units(LPEDICT self, LPCVECTOR2 point) {
    if (!self || !point || (self->aiflags & AI_IMMOBILE))
        return false;
    if (Vector2_distance(&self->s.origin2, point) > unit_movedistance(self) + 0.001f)
        return false;
    if (!move_is_valid_policy(self, point, MOVE_IGNORE_UNITS))
        return false;
    unit_commit_step(self, point);
    return true;
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
    FLOAT turn = self->data.UnitData->turnRate;
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
    moveCollisionPolicy_t const collision_policy =
        policy == MOVE_AVOID_STATIC_ONLY ? MOVE_IGNORE_UNITS : MOVE_COLLIDE_UNITS;
    VECTOR2 const straight = Vector2_mad(&self->s.origin2, dist,
                                         &MAKE(VECTOR2, cosf(goal_angle), sinf(goal_angle)));
    if (policy == MOVE_AVOID_RESOURCE_WORKER)
        return unit_worker_desired_heading(self, goal_angle, dist);
    if (move_is_valid_policy(self, &straight, collision_policy))
        return goal_angle;

    int max_rings = MOVE_SLIDE_RINGS;
    LPEDICT const b = trymove_blocker;
    if (b && unit_is_walking(self) && unit_is_walking(b) &&
        unit_current_speed(self) > unit_current_speed(b)) {
        max_rings = MOVE_SLIDE_RINGS_YIELD;
    }
    ROUTESLIDE slide = { .ent = self, .angle = goal_angle, .dist = dist, .rings = max_rings,
        .valid = collision_policy == MOVE_IGNORE_UNITS ? move_static_is_valid : move_is_valid };
    return CM_SlideRoute(&slide);
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
static BOOL unit_accel_direction_to_point(LPEDICT self, LPCVECTOR2 target,
                                          FLOAT radius, LPVECTOR2 dir) {
    if (!self || !target || !dir) return false;
    pathAccelParams_t params = { &self->s.origin2, target, radius, M_UnitStaticPathingFlags(self) };
    return CM_AccelerateRoute(&self->movement.path, &params, dir);
}

static BOOL unit_accel_direction(LPEDICT self, FLOAT radius, LPVECTOR2 dir) {
    return unit_accel_direction_to_point(self, &self->goalentity->s.origin2,
                                         radius, dir);
}

void unit_changeangle_towards_point(LPEDICT self, LPCVECTOR2 point) {
    unit_changeangle_towards_point_policy(self, point, MOVE_AVOID_GENERIC);
}

void unit_changeangle_towards_point_worker(LPEDICT self, LPCVECTOR2 point) {
    unit_changeangle_towards_point_policy(self, point, MOVE_AVOID_RESOURCE_WORKER);
}

BOOL unit_changeangle_towards_point_ignore_units(LPEDICT self, LPCVECTOR2 point) {
    VECTOR2 dir;

    if (!self || !point || (self->aiflags & AI_IMMOBILE))
        return false;

    self->movement.heading = self->s.angle;
    self->movement.flow_generation = 0;
    self->movement.flow_goal_reached = false;
    self->movement.flow_unreachable = false;
    self->movement.flow_direct = false;

    /* Resource-return behaviors keep the building as their authoritative goal,
     * but navigation may target a worker-relative footprint edge.  Prefer that
     * exact point when it is directly reachable; otherwise use the same
     * collision-sized mover-owned A* accelerator used while shared fields are
     * pending.  Live units remain ignored by the steering/move policy. */
    if (CM_LineIsPathableForRadiusFlags(&self->s.origin2, point, self->collision, M_UnitStaticPathingFlags(self))) {
        self->movement.path.valid = false;
        self->movement.flow_direct = true;
        dir = Vector2_sub(point, &self->s.origin2);
    } else if (!unit_accel_direction_to_point(self, point, self->collision, &dir)) {
        return false;
    }

    unit_apply_heading(self, &dir, MOVE_AVOID_STATIC_ONLY);
    return true;
}

static void unit_changeangle_policy(LPEDICT self, moveAvoidPolicy_t policy) {
    if (self->aiflags & AI_IMMOBILE)
        return;
    VECTOR2 to_goal = Vector2_sub(&self->goalentity->s.origin2, &self->s.origin2);
    VECTOR2 dir;
    FLOAT const radius = unit_routes_to_location(self) ? self->collision : 0.0f;
    BYTE const blocked_flags = M_UnitStaticPathingFlags(self);

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
    if (CM_LineIsPathableForRadiusFlags(&self->s.origin2, &self->goalentity->s.origin2, radius, blocked_flags)) {
        self->movement.path.valid = false;
        self->movement.flow_direct = true;
        dir = to_goal;
    } else {
        DWORD heatmap = M_RefreshHeatmapForMover(self, self->goalentity, radius);
        self->movement.flow_generation = heatmap;
        if (!heatmap) {
            if (!unit_accel_direction(self, radius, &dir))
                return; /* long incremental route is still building; keep the order */
            /* path_valid resolves the heading while the shared field builds;
             * this is not a direct line to the requested destination. */
            unit_apply_heading(self, &dir, policy);
            return;
        }
        self->movement.path.valid = false;
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
                    LPCVECTOR2 from = &self->s.origin2, target = &self->goalentity->s.origin2;
                    VECTOR2 closest;
                    if (move_fallback_throttled(self, target, radius))
                        return;
                    self->movement.flow_fallback_target = *target;
                    self->movement.flow_fallback_radius = radius;
                    self->movement.flow_fallback_time = level.time;
                    self->movement.flow_fallback_goal = self->goalentity;
                    self->movement.flow_fallback_state = MOVE_FALLBACK_RETRY;
                    if (CM_ClosestReachablePointForRadiusFlags(from, target, radius, blocked_flags, &closest)) {
                        self->goalentity->s.origin2 = closest;
                        self->goalentity->secondarygoal = NULL;
                        self->goalentity->heatmap2 = 0;
                        self->goalentity->heatmap2_radius = 0;
                        move_reset_progress(self);
                        self->movement.flow_fallback_goal = self->goalentity;
                        self->movement.flow_fallback_state = MOVE_FALLBACK_APPLIED;
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
                                               moveAvoidPolicy_t policy,
                                               BOOL continue_to_target) {
    if (self->aiflags & AI_IMMOBILE)
        return;
    VECTOR2 to_goal = Vector2_sub(&self->goalentity->s.origin2, &self->s.origin2);
    VECTOR2 dir;
    BYTE const blocked_flags = M_UnitStaticPathingFlags(self);

    self->movement.heading = self->s.angle;
    self->movement.flow_generation = 0;
    self->movement.flow_goal_reached = false;
    self->movement.flow_unreachable = false;
    self->movement.flow_direct = false;

    if (CM_LineIsPathableForRadiusFlags(&self->s.origin2, &self->goalentity->s.origin2, radius, blocked_flags)) {
        self->movement.path.valid = false;
        self->movement.flow_direct = true;
        dir = to_goal;
    } else {
        DWORD heatmap = M_RefreshHeatmapForMover(self, self->goalentity, radius);
        self->movement.flow_generation = heatmap;
        if (!heatmap) {
            if (!unit_accel_direction(self, radius, &dir))
                return; /* long incremental route is still building */
            unit_apply_heading(self, &dir, policy);
            return;
        }
        self->movement.path.valid = false;

        if (CM_FlowReachedGoal(heatmap, self->s.origin.x, self->s.origin.y)) {
            self->movement.flow_goal_reached = true;
            if (!continue_to_target)
                return;
            /* Ranged interactions target a blocked unit/building centre.  A
             * collision-sized route deliberately ends at the nearest legal
             * cell around that footprint; from there keep steering at the real
             * target and let the behavior's precise range check complete the
             * interaction before a step would enter static pathing. */
            dir = to_goal;
        } else {
            dir = get_flow_direction(heatmap, self->s.origin.x, self->s.origin.y);
        }
        if (!self->movement.flow_goal_reached && Vector2_len(&dir) <= 0.001f) {
            self->movement.flow_unreachable =
                !CM_FlowCanReach(heatmap, self->s.origin.x, self->s.origin.y);
            return;
        }
    }

    unit_apply_heading(self, &dir, policy);
}

void unit_changeangle_for_radius(LPEDICT self, FLOAT radius) {
    unit_changeangle_for_radius_policy(self, radius, MOVE_AVOID_GENERIC, false);
}

void unit_changeangle_for_radius_worker(LPEDICT self, FLOAT radius) {
    unit_changeangle_for_radius_policy(self, radius, MOVE_AVOID_RESOURCE_WORKER, false);
}

void unit_changeangle_interaction_ignore_units(LPEDICT self) {
    /* Warsmash's mover owns a collision-sized path even when the ranged
     * behavior disables unit collision.  Use the worker radius for static
     * routing, but keep mobile-unit collision disabled at steering/move time. */
    unit_changeangle_for_radius_policy(self, self ? self->collision : 0.0f,
                                       MOVE_AVOID_STATIC_ONLY, true);
}

/* Reserve a body-queue-style ring in g_edicts so ordinary F_EDICT relocation owns every waypoint pointer. */
void G_InitWaypoints(void) {
    DWORD base;
    if (level.waypoints.count) return;
    base = level.waypoints.base = globals.num_edicts;
    FOR_LOOP(i, MAX_WAYPOINTS) {
        LPEDICT waypoint = G_Spawn();
        if (waypoint != g_edicts + base + i) gi.error("G_InitWaypoints: waypoint ring is not contiguous\n");
        waypoint->svflags |= SVF_NOCLIENT;
    }
    level.waypoints.count = MAX_WAYPOINTS;
}

/* Recycle one real edict from the fixed ring, matching Quake II's TRAIL/body queue ownership model. */
LPEDICT Waypoint_add(LPCVECTOR2 spot) {
    LPEDICT waypoint;
    G_InitWaypoints();
    waypoint = g_edicts + level.waypoints.base + level.waypoints.cursor;
    level.waypoints.cursor = (level.waypoints.cursor + 1) % MAX_WAYPOINTS;
    waypoint->s.origin.x = spot->x;
    waypoint->s.origin.y = spot->y;
    waypoint->heatmap2 = 0;
    waypoint->heatmap2_radius = 0;
    waypoint->secondarygoal = NULL;
    waypoint->collision = 0;
    M_CheckGround(waypoint);
    return waypoint;
}

static int move_harvest_path_debug_level(void) {
    LPCSTR value;
    value = gi.CvarString("wc3_harvest_path_debug", "0");
    return value ? atoi(value) : 0;
}

DWORD M_RefreshHeatmapForMover(LPCEDICT mover, LPEDICT self, FLOAT radius) {
    LPEDICT route = self && self->secondarygoal ? self->secondarygoal : self;
    BYTE const blocked_flags = M_UnitStaticPathingFlags(mover);
    BOOL radius_matches;
    BOOL cached = false;
    DWORD generation;

    if (!route)
        return 0;

    radius_matches = fabsf(route->heatmap2_radius - radius) < 0.01f;
    if (radius_matches && route->heatmap2)
        cached = CM_ActivateCachedFlowForFlags(route->heatmap2, blocked_flags);

    /* Fixed waypoints never move, so a still-cached field remains valid until
     * static pathing invalidates the routing cache. */
    if (cached && !(route->svflags & SVF_MONSTER))
        return route->heatmap2;

    if (cached && (route->svflags & SVF_MONSTER)) {
        BOOL const moved = Vector2_distance(&route->s.origin2, &route->heatmap2_origin) >= 64.0f;
        BOOL const stale = (DWORD)(level.time - route->heatmap2_time) >= 400;
        if (!moved || !stale)
            return route->heatmap2;
    }

    /* Cache misses are resumable in common/routing.c.  Return the old field for
     * a moving target while its replacement is being built; fixed goals with
     * no field simply wait until a later tick instead of steering straight into
     * the obstacle that caused routing to be needed. */
    generation = CM_RequestHeatmapForRadiusFlags(route, radius, blocked_flags);
    if (!generation)
        return cached ? route->heatmap2 : 0;

    route->heatmap2 = generation;
    route->heatmap2_origin = route->s.origin2;
    route->heatmap2_time = level.time;
    route->heatmap2_radius = radius;

    if (move_harvest_path_debug_level() >= 2 && route->targtype == TARG_TREE) {
        fprintf(stderr,
                "WC3_HARVEST_PATH heatmap target=%d reason=ready generation=%u radius=%.1f\n",
                route->s.number, route->heatmap2, radius);
    }
    return route->heatmap2;
}

DWORD M_RefreshHeatmap(LPEDICT self, FLOAT radius) {
    return M_RefreshHeatmapForMover(NULL, self, radius);
}

static LPCSTR M_UnitMoveTypeName(LPCEDICT self) {
    return self && self->data.UnitData ? self->data.UnitData->moveTypeName : NULL;
}

static BOOL M_UnitUsesWaterSurface(LPCEDICT self, LPCSTR movetp) {
    if (!movetp) return false;
    if (!strcmp(movetp, "fly") || !strcmp(movetp, "hover") || !strcmp(movetp, "float"))
        return true;
    if (!strcmp(movetp, "amph")) {
        return CM_TerrainPointIsSwimmable(&self->s.origin2) &&
               !CM_TerrainPointIsWalkable(&self->s.origin2);
    }
    return false;
}

/* Resolve the visual/support surface, then apply the unit's mutable fly height.
 * FOOT/HORSE stay terrain-based; FLY/HOVER/FLOAT and swimming AMPH units use
 * max(terrain, water).  Walkable destructables can raise every movement type
 * except FLOAT, matching Warsmash's "boats can't go on bridges" rule. */
void M_CheckGround(LPEDICT self) {
    LPCSTR const movetp = M_UnitMoveTypeName(self);
    BOOL const floating = movetp && !strcmp(movetp, "float");
    FLOAT height = CM_GetHeightAtPoint(self->s.origin.x, self->s.origin.y);
    FLOAT const cell = CM_PathCellWorldSize();

    if (M_UnitUsesWaterSurface(self, movetp))
        height = MAX(height, CM_GetWaterHeightAtPoint(self->s.origin.x, self->s.origin.y));

    if (!floating) {
        for (LPEDICT surface = level.ground_surfaces; surface; surface = surface->ground_next) {
            pathTex_t const *pathtex = surface->pathtex;
            pathTexTransform_t const transform = CM_GetPathTexTransform(surface);
            if (!surface->inuse || surface->destructable.dead ||
                !surface->destructable.placement_solid || !pathtex) continue;
            if (fabsf(self->s.origin.x - surface->s.origin.x) > transform.width * cell * 0.5f ||
                fabsf(self->s.origin.y - surface->s.origin.y) > transform.height * cell * 0.5f) continue;
            height = MAX(height, surface->s.origin.z);
        }
    }
    self->s.ground_offset = self->unitinfo.FlyHeight;
    self->s.origin.z = height + self->s.ground_offset;
}

FLOAT M_DistanceToGoal(LPEDICT ent) {
    if (ent->goalentity) {
        return Vector2_distance(&ent->goalentity->s.origin2, &ent->s.origin2);
    } else {
        return 0;
    }
}

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
                          BYTE blocked_flags,
                          moveSlot_t const *reserved,
                          DWORD num_reserved,
                          LPVECTOR2 out) {
    VECTOR2 pathable = *point;
    if (!CM_ClosestPathablePointForRadiusFlags(point, radius, blocked_flags, &pathable)) {
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
                                    BYTE blocked_flags,
                                    FLOAT spacing,
                                    DWORD unit_count,
                                    moveSlot_t const *reserved,
                                    DWORD num_reserved,
                                    LPVECTOR2 out) {
    FLOAT best_distance = 0;
    BOOL found = false;
    VECTOR2 best = *location;
    int max_ring = (int)ceilf(sqrtf(MAX(1, unit_count))) + 8;

    if (move_try_slot(preferred, radius, blocked_flags, reserved, num_reserved, out)) {
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

                if (!move_try_slot(&candidate, radius, blocked_flags, reserved, num_reserved, &pathable)) {
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
    self->movement.flow_fallback_goal = NULL;
    self->movement.flow_fallback_state = MOVE_FALLBACK_NONE;
    self->movement.worker_avoid_origin = self->s.origin2;
    self->movement.worker_avoid_heading = self->s.angle;
    self->movement.worker_avoid_blocked_frames = 0;
    self->movement.worker_avoid_active = false;
    self->movement.group_speed = 0;  /* single-unit/default: travel at own speed */
}

/* Effective current move speed of a unit (runtime override, else data table). */
static FLOAT unit_effective_speed(LPEDICT ent) {
    FLOAT speed = ent->unitinfo.MoveSpeed > 0 ? ent->unitinfo.MoveSpeed : ent->data.UnitBalance->speed;
    DWORD level = G_UnitStatusLevel(ent, MAKEFOURCC('B', 'O', 'w', 'k'));
    if (level) speed *= 1.0f + G_AbilityLevel(MAKEFOURCC('A', 'O', 'w', 'k'), level)->data[0].number * 0.01f;
    speed *= 1.0f + S_UnholyMoveBonus(ent);
    speed *= 1.0f + S_BloodlustMoveBonus(ent);
    speed *= S_HumanMoveFactor(ent);
    speed *= 1.0f - S_CrippleMoveReduction(ent);
    speed *= 1.0f - S_PurgeMoveReduction(ent);
    speed *= 1.0f - S_SlowPoisonMoveReduction(ent);
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT aura = g_edicts + i;
        DWORD aura_level = G_UnitAbilityLevel(aura, MAKEFOURCC('A', 'O', 'a', 'e'));
        if (aura->inuse && aura_level && S_SpellIsFriend(aura, ent) &&
            Vector2_distance(&aura->s.origin2, &ent->s.origin2) <=
            G_AbilityLevel(MAKEFOURCC('A', 'O', 'a', 'e'), aura_level)->area)
            speed *= 1.0f + G_AbilityLevel(MAKEFOURCC('A', 'O', 'a', 'e'), aura_level)->data[0].number * 0.01f;
    }
    return speed;
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
    if (owner < PLAYER_NEUTRAL_AGGRESSIVE && level.mapinfo &&
        level.mapinfo->players[owner].playerType == kPlayerTypeNone) {
        return false;
    }
    return G_PlayerTreatsPlayerAsAlly(self->s.player, owner);
}

static BOOL follow_can_auto_attack(LPCEDICT self) {
    if (!self || !S_CargoAttacksEnabled(self) || self->attack1.cooldown <= 0.0f ||
        (self->attack1.damageBase <= 0 && self->attack1.numberOfDice <= 0)) {
        return false;
    }
    return !level.mapinfo || level.mapinfo->players[self->s.player].playerType != kPlayerTypeNeutral;
}

FLOAT G_FollowStopRange(LPCEDICT follower, LPCEDICT target) {
    FLOAT configured;
    FLOAT collision_range;

    if (!follower || !target) return 0.0f;
    configured = (target->s.flags & EF_BUILDING)
        ? game.constants.structureFollowRange
        : game.constants.followRange;
    /* A pathing-footprint distance already includes the building extent, so
     * only the follower radius remains as its no-overlap lower bound. */
    collision_range = follower->collision;
    if (!(target->s.flags & EF_BUILDING) || !target->pathtex)
        collision_range += target->collision;
    return MAX(configured, collision_range);
}

/* Warsmash's unit canReach() tests a building target against its authored
 * pathing pixels instead of requiring the follower to approach the building
 * centre.  OpenRealm already uses the same footprint distance for Attack,
 * Repair, harvesting, militia, and cargo interactions; follow/rally must use
 * it too or a freshly trained unit can be nudged legally out of its producer
 * and then immediately walk back into the producer's blocked footprint.
 *
 * CM_DistanceToPathingFootprint() measures from the follower centre to the
 * blocked footprint edge.  StructureFollowRange remains the authored follow
 * margin, while the follower radius is the hard no-overlap lower bound.  The
 * target collision radius is intentionally not added when a real footprint is
 * available because that would count the building extent twice. */
static BOOL follow_footprint_distance(LPCEDICT follower, LPCEDICT target,
                                      FLOAT *distance) {
    FLOAT footprint;

    if (!follower || !target || !distance ||
        !(target->s.flags & EF_BUILDING) || !target->pathtex) {
        return false;
    }
    footprint = CM_DistanceToPathingFootprint(target, &follower->s.origin2);
    if (footprint >= FLT_MAX) return false;

    *distance = footprint;
    return true;
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
    follow_range = G_FollowStopRange(ent, target);
    follow_footprint_distance(ent, target, &distance);
    standing = G_AnimationHasPrimary(ent->animation, "stand");
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

static umove_t follow_move_walk = { "walk", ai_follow_walk, NULL, CAbilityMove };

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

static umove_t move_move_hold = { "stand", NULL, NULL, CAbilityMove };

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

    if (G_UnitStatusLevel(ent, MAKEFOURCC('B', 'E', 'e', 'r'))) {
        ent->stand(ent);
        return;
    }

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
        if (!ent->movement.flow_direct && !ent->movement.path.valid && !ent->movement.flow_generation) {
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

static umove_t move_move_walk = { "walk", ai_move_walk, NULL, CAbilityMove };

/* Identify the ordinary walk move so spell approach orders can detect replacement. */
BOOL move_is_active_order_walk(LPCEDICT ent) {
    return ent && ent->currentmove == &move_move_walk;
}

/* Set the unit's move target and begin walking.
 * goalentity must be a waypoint or any entity whose origin is the destination. */
void order_move(LPEDICT self, LPEDICT target) {
    if (S_GoldMineWorkerIsInside(self))
        return;
    if ((self->aiflags & AI_IMMOBILE) || G_UnitStatusLevel(self, MAKEFOURCC('B', 'E', 'e', 'r'))
        || G_UnitStatusLevel(self, MAKEFOURCC('B', 'e', 'n', 's')))
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
                                     M_UnitStaticPathingFlags(ent),
                                     spacing,
                                     num_units,
                                     reserved,
                                     i,
                                     &target)) {
            target = *location;
            CM_ClosestPathablePointForRadiusFlags(location, ent->collision, M_UnitStaticPathingFlags(ent), &target);
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

BZ_COMMAND_PROC(AbilityMove) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_location_selected = move_selectlocation;
    clent->client->menu.supports_order_queue = true;
}
