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

extern ability_t a_move;

/* A unit is actively executing a ground move order (right-click move). */
static BOOL unit_is_walking(LPCEDICT ent) {
    return ent->currentmove && ent->currentmove->ability == &a_move;
}

/* Unit's effective current move speed.  Group moves travel at the slowest
 * member's speed so the selection stays a cohesive formation instead of
 * stringing out (WC3); the cap is gated on the move state so it never leaks
 * into a later attack/harvest order that reuses this.  Using the *capped*
 * speed means members of one group compare equal (no give-way within a group). */
static FLOAT unit_current_speed(LPCEDICT self) {
    FLOAT speed = self->unitinfo.MoveSpeed > 0
        ? self->unitinfo.MoveSpeed
        : UNIT_SPEED(self->class_id);
    if (self->move_group_speed > 0 && self->move_group_speed < speed && unit_is_walking(self)) {
        speed = self->move_group_speed;
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
#define MAX_MOVE_COLLIDERS     256

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
    FLOAT const dist = unit_movedistance(self);
    VECTOR2 const by_facing = Vector2_mad(&self->s.origin2, dist,
                                          &MAKE(VECTOR2, cosf(self->s.angle), sinf(self->s.angle)));
    if (move_is_valid(self, &by_facing)) {
        unit_commit_step(self, &by_facing);
        return;
    }
    VECTOR2 const by_heading = Vector2_mad(&self->s.origin2, dist,
                                           &MAKE(VECTOR2, cosf(self->move_heading), sinf(self->move_heading)));
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
    FLOAT turn = UNIT_TURN_RATE(self->class_id);
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

/* Pick the heading the unit actually wants to move along this tick: the goal
 * heading if a step along it is free, otherwise the nearest free deflection
 * (the same block-and-slide search and speed give-way as unit_trymove).  By
 * resolving avoidance into the *heading* — which the facing then turns toward
 * and the unit moves along — facing and movement stay aligned, so a unit
 * weaving past trees/units turns to follow its path instead of crab-walking
 * sideways while pointing at the goal (the old split that looked like wobble). */
static FLOAT unit_desired_heading(LPEDICT self, FLOAT goal_angle, FLOAT dist) {
    VECTOR2 const straight = Vector2_mad(&self->s.origin2, dist,
                                         &MAKE(VECTOR2, cosf(goal_angle), sinf(goal_angle)));
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

void unit_changeangle(LPEDICT self) {
    VECTOR2 to_goal = Vector2_sub(&self->goalentity->s.origin2, &self->s.origin2);
    VECTOR2 dir;

    self->move_heading = self->s.angle;  /* default if no heading is resolved this tick */

    /* Global routing: steer straight only when the line to the goal is clear of
     * terrain (near OR far); otherwise follow the flow field around terrain.
     * The cheap Bresenham test gates the expensive flow-field bake. */
    if (CM_LineIsWalkable(&self->s.origin2, &self->goalentity->s.origin2)) {
        dir = to_goal;
    } else {
        DWORD heatmap = M_RefreshHeatmap(self->goalentity);
        dir = get_flow_direction(heatmap, self->s.origin.x, self->s.origin.y);
        if (Vector2_len(&dir) <= 0.001f)
            dir = to_goal;
    }

    FLOAT const dirlen = Vector2_len(&dir);
    if (dirlen <= 0.001f)
        return;  /* no meaningful heading this tick: hold current facing */

    /* Local avoidance resolves into ONE heading; the facing turns toward it and
     * the move step (unit_moveindirection) follows it, keeping facing and motion
     * aligned (no second, disagreeing search). */
    FLOAT const goal_angle = atan2f(dir.y, dir.x);
    FLOAT const desired = unit_desired_heading(self, goal_angle, unit_movedistance(self));
    self->move_heading = desired;
    unit_turn_toward(self, desired);
}

void unit_setanimation(LPEDICT self, LPCSTR anim) {
    self->animation = G_GetAnimation(self->s.model, anim);
}

void unit_setmove(LPEDICT self, umove_t *move) {
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
    if (!(ent->svflags & SVF_MONSTER) || ent->s.player == ai_current_entity->s.player)
        return false;
    /* Any non-allied player's units are valid targets — WC3 auto-acquisition
     * applies to the player's units too, not only the AI's. */
    if (level.alliances[ent->s.player][ai_current_entity->s.player] != 0)
        return false;
    if (ent->svflags & SVF_DEADMONSTER)
        return false;
    if (UNIT_IS_BUILDING(ent->class_id))
        return false;
    /* Only auto-engage fights that involve the human player (player vs anyone,
     * and anyone vs player). This preserves the original computer->player
     * aggression while letting the player's units fight back, without spawning
     * map-wide computer-vs-neutral and neutral-vs-neutral brawls. */
    if (level.mapinfo->players[ai_current_entity->s.player].playerType != kPlayerTypeHuman &&
        level.mapinfo->players[ent->s.player].playerType != kPlayerTypeHuman)
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

/* Max distance a unit will autonomously notice and engage an enemy — capped
 * at sight radius, defaulting to half sight when no explicit value exists. */
FLOAT G_AcquisitionRange(LPCEDICT self) {
    FLOAT const sight = self->balance.sight_radius.day;
    FLOAT acquire = UNIT_ACQUISITION_RANGE(self->class_id);
    if (acquire <= 0.0f)
        acquire = sight * 0.5f;
    if (sight > 0.0f && acquire > sight)
        acquire = sight;
    return acquire;
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
    /* Idle units auto-engage the nearest enemy within acquisition range — for
     * the player's own units too. Units with no attack (workers/critters) and
     * units already chasing/attacking stay as they are. */
    if (!unit_has_attack(self))
        return;
    /* Only human- and computer-controlled units auto-acquire. Neutral/creep
     * units do not initiate (avoids map-wide neutral-vs-neutral aggression),
     * matching WC3's default behaviour closely enough for campaign play. */
    {
        DWORD const pt = level.mapinfo->players[self->s.player].playerType;
        if (pt != kPlayerTypeComputer && pt != kPlayerTypeHuman)
            return;
    }
    if (!G_ShouldAcquireThisFrame(self))
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
