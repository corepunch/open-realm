#ifndef r_trail_h
#define r_trail_h

/* Ring-buffer trail emitter — shared by WoW ribbons and WC3 MODEL_EMITTER_TAIL.
   The caller manages a per-emitter trailEmitter_t, advances it each frame with
   R_UpdateTrail, then spawns one cparticle_t per active edge for billboard
   rendering through the engine particle pool. */

#define MAX_TRAIL_EDGES 64

typedef struct {
    VECTOR3 world_pos;
    VECTOR3 color;
    float alpha, age;
} trailEdge_t;

typedef struct {
    trailEdge_t edges[MAX_TRAIL_EDGES];
    int head, count;
    float acc;      /* edge emission accumulator (rate * dt) */
} trailEmitter_t;

/* Advance trail state for one frame.
   - Ages all active edges by `dt`.
   - Drops edges whose age exceeds `max_age`.
   - Accumulates `edge_rate * dt`; spawns new edges at `spine_pos` when acc >= 1.0.
   - Caps accumulator at 2.0 to suppress lag-spike bursts.
   Returns the number of active edges after the update.  The caller then iterates
   edges 0..(count-1) to spawn billboard particles. */
static int R_UpdateTrail(trailEmitter_t *t, VECTOR3 spine_pos,
                         VECTOR3 color, float alpha,
                         float max_age, float edge_rate, float dt) {
    if (!t || edge_rate <= 0.0f || max_age <= 0.0f) { t->count = 0; t->acc = 0.0f; return 0; }
    int write = t->head, alive = t->count;
    for (int e = 0; e < alive; e++) {
        int idx = (write - alive + e + MAX_TRAIL_EDGES) % MAX_TRAIL_EDGES;
        t->edges[idx].age += dt;
    }
    while (alive > 0) {
        int oldest = (write - alive + MAX_TRAIL_EDGES) % MAX_TRAIL_EDGES;
        if (t->edges[oldest].age < max_age) break;
        alive--;
    }
    t->acc += edge_rate * dt;
    while (t->acc >= 1.0f) {
        t->acc -= 1.0f;
        if (alive >= MAX_TRAIL_EDGES) alive--;
        trailEdge_t *e = &t->edges[write];
        e->world_pos = spine_pos; e->color = color; e->alpha = alpha; e->age = 0.0f;
        write = (write + 1) % MAX_TRAIL_EDGES;
        alive++;
    }
    if (t->acc > 2.0f) t->acc = 0.0f;
    t->head = write; t->count = alive;
    return alive;
}

#endif
