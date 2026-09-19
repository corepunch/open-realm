#include "renderer/r_trail.h"
#include <string.h>

int R_TrailAdvance(trail_t *trail, VECTOR3 above, VECTOR3 below, COLOR32 color,
                   float lifespan, float rate, float gravity, DWORD now_ms, DWORD delta_ms)
{
    int write, alive, e;
    DWORD gap;
    float dt;

    if (!trail) return 0;
    if (lifespan <= 0.0f) { trail->count = 0; trail->acc = 0.0f; return 0; }
    gap = now_ms - trail->stamp;
    if (trail->stamp && gap > TRAIL_STALE_MS) memset(trail, 0, sizeof(*trail));
    dt = gap ? (float)delta_ms / 1000.0f : 0.0f;
    trail->stamp = now_ms;
    write = trail->head;
    alive = trail->count;
    for (e = 0; e < alive; e++) {
        int idx = (write - alive + e + TRAIL_MAX_EDGES) % TRAIL_MAX_EDGES;
        trail->edges[idx].age += dt;
        trail->edges[idx].above.z -= gravity * dt;
        trail->edges[idx].below.z -= gravity * dt;
    }
    while (alive > 0) {
        int oldest = (write - alive + TRAIL_MAX_EDGES) % TRAIL_MAX_EDGES;
        if (trail->edges[oldest].age < lifespan) break;
        alive--;
    }
    if (rate > 0.0f && dt > 0.0f) {
        trail->acc = MIN(trail->acc + rate * dt, 2.0f); /* clamp before emitting: a hitch must not stack coincident edges */
        while (trail->acc >= 1.0f) {
            trailEdge_t *edge;
            trail->acc -= 1.0f;
            if (alive >= TRAIL_MAX_EDGES) alive--;
            edge = &trail->edges[write];
            edge->above = above;
            edge->below = below;
            edge->color = color;
            edge->age = 0.0f;
            write = (write + 1) % TRAIL_MAX_EDGES;
            alive++;
        }
    }
    trail->head = write;
    trail->count = alive;
    return alive;
}

static void R_TrailQuad(trailVert_t *out, VECTOR3 a, VECTOR3 b, VECTOR3 c, VECTOR3 d,
                        VECTOR2 uv_a, VECTOR2 uv_b, VECTOR2 uv_c, VECTOR2 uv_d,
                        COLOR32 ca, COLOR32 cb)
{
    out[0] = (trailVert_t){ a, uv_a, ca }; out[1] = (trailVert_t){ b, uv_b, ca };
    out[2] = (trailVert_t){ c, uv_c, cb }; out[3] = (trailVert_t){ a, uv_a, ca };
    out[4] = (trailVert_t){ c, uv_c, cb }; out[5] = (trailVert_t){ d, uv_d, cb };
}

DWORD R_TrailStripVerts(trail_t const *trail, float lifespan, DWORD columns, DWORD rows, DWORD slot,
                        trailVert_t *out, DWORD max)
{
    int alive, i, write;
    float cols, rows_f, cell_u, cell_v;
    DWORD used = 0;

    if (!trail || !out || trail->count < 2 || lifespan <= 0.0f) return 0;
    alive = trail->count;
    write = trail->head;
    cols = (float)MAX(1, columns);
    rows_f = (float)MAX(1, rows);
    cell_u = (float)(slot % MAX(1, columns)) / cols;
    cell_v = (float)(slot / MAX(1, columns)) / rows_f;
    for (i = 0; i < alive - 1 && used + 6 <= max; i++) {
        int a = (write - alive + i + TRAIL_MAX_EDGES) % TRAIL_MAX_EDGES;
        int b = (write - alive + i + 1 + TRAIL_MAX_EDGES) % TRAIL_MAX_EDGES;
        float t_old = MIN(1.0f, trail->edges[a].age / lifespan);
        float t_new = MIN(1.0f, trail->edges[b].age / lifespan);
        float u0 = cell_u + t_new / cols, u1 = cell_u + t_old / cols;
        VECTOR2 uv_above0 = { u1, cell_v };
        VECTOR2 uv_below0 = { u1, cell_v + 1.0f / rows_f };
        VECTOR2 uv_below1 = { u0, cell_v + 1.0f / rows_f };
        VECTOR2 uv_above1 = { u0, cell_v };
        R_TrailQuad(out + used, trail->edges[a].above, trail->edges[a].below,
                    trail->edges[b].below, trail->edges[b].above,
                    uv_above0, uv_below0, uv_below1, uv_above1,
                    trail->edges[a].color, trail->edges[b].color);
        used += 6;
    }
    return used;
}
