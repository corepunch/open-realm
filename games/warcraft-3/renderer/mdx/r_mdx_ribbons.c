#include "r_mdx.h"
#include <string.h>

static DWORD MDLX_CountRibbons(mdxModel_t const *model) {
    DWORD n = 0;
    FOR_EACH_LIST(mdxRibbonEmitter_t, ribbon, model->ribbons) n++;
    return n;
}

/* Age live edges, drop expired ones, and push a new edge when the emission accumulator crosses 1. */
int MDLX_UpdateRibbonTrail(mdxRibbonTrail_t *trail, VECTOR3 above, VECTOR3 below,
                           float lifespan, float rate, float gravity, float dt)
{
    int write, alive, e;

    if (!trail || lifespan <= 0.0f) {
        if (trail) { trail->count = 0; trail->acc = 0.0f; }
        return 0;
    }
    write = trail->head;
    alive = trail->count;
    for (e = 0; e < alive; e++) {
        int idx = (write - alive + e + BZ_MDX_RIBBON_EDGES) % BZ_MDX_RIBBON_EDGES;
        trail->edges[idx].age += dt;
        trail->edges[idx].above.z -= gravity * dt;
        trail->edges[idx].below.z -= gravity * dt;
    }
    while (alive > 0) {
        int oldest = (write - alive + BZ_MDX_RIBBON_EDGES) % BZ_MDX_RIBBON_EDGES;
        if (trail->edges[oldest].age < lifespan) break;
        alive--;
    }
    if (rate > 0.0f && dt > 0.0f) {
        trail->acc += rate * dt;
        while (trail->acc >= 1.0f) {
            mdxRibbonEdge_t *edge;
            trail->acc -= 1.0f;
            if (alive >= BZ_MDX_RIBBON_EDGES) alive--;
            edge = &trail->edges[write];
            edge->above = above;
            edge->below = below;
            edge->age = 0.0f;
            write = (write + 1) % BZ_MDX_RIBBON_EDGES;
            alive++;
        }
        if (trail->acc > 2.0f) trail->acc = 0.0f;
    }
    trail->head = write;
    trail->count = alive;
    return alive;
}

static void MDLX_RibbonQuad(VERTEX *out, VECTOR3 a, VECTOR3 b, VECTOR3 c, VECTOR3 d,
                            VECTOR2 uv_a, VECTOR2 uv_b, VECTOR2 uv_c, VECTOR2 uv_d, COLOR32 color)
{
    VERTEX v[6];
    memset(v, 0, sizeof(v));
    v[0].position = a; v[1].position = b; v[2].position = c;
    v[3].position = a; v[4].position = c; v[5].position = d;
    v[0].texcoord = uv_a; v[1].texcoord = uv_b; v[2].texcoord = uv_c;
    v[3].texcoord = uv_a; v[4].texcoord = uv_c; v[5].texcoord = uv_d;
    FOR_LOOP(i, 6) {
        v[i].color = color;
        v[i].normal = (VECTOR3){ 0, 0, 1 };
        v[i].boneWeight[0] = 255;
    }
    memcpy(out, v, sizeof(v));
}

/* One quad per consecutive edge pair. U unwraps along the chain so Ghost2-style textures decorate the strip. */
DWORD MDLX_RibbonStripVertices(mdxRibbonTrail_t const *trail, DWORD columns, DWORD rows, DWORD slot,
                               COLOR32 color, VERTEX *out, DWORD max)
{
    int alive, i, write;
    float chain, cols, rows_f, cell_u, cell_v;
    DWORD used = 0;

    if (!trail || !out || trail->count < 2) return 0;
    alive = trail->count;
    write = trail->head;
    cols = (float)MAX(1, columns);
    rows_f = (float)MAX(1, rows);
    cell_u = (float)(slot % MAX(1, columns)) / cols;
    cell_v = (float)(slot / MAX(1, columns)) / rows_f;
    chain = 1.0f / (float)(alive - 1);
    for (i = 0; i < alive - 1 && used + 6 <= max; i++) {
        int a = (write - alive + i + BZ_MDX_RIBBON_EDGES) % BZ_MDX_RIBBON_EDGES;
        int b = (write - alive + i + 1 + BZ_MDX_RIBBON_EDGES) % BZ_MDX_RIBBON_EDGES;
        float u0 = cell_u + (1.0f - (float)i * chain - chain) / cols;
        float u1 = u0 + chain / cols;
        VECTOR2 uv_above0 = { u1, cell_v };
        VECTOR2 uv_below0 = { u1, cell_v + 1.0f / rows_f };
        VECTOR2 uv_below1 = { u0, cell_v + 1.0f / rows_f };
        VECTOR2 uv_above1 = { u0, cell_v };
        MDLX_RibbonQuad(out + used, trail->edges[a].above, trail->edges[a].below,
                        trail->edges[b].below, trail->edges[b].above,
                        uv_above0, uv_below0, uv_below1, uv_above1, color);
        used += 6;
    }
    return used;
}

static mdxRibbonInstance_t *MDLX_RibbonInstance(mdxModel_t *model, DWORD number) {
    mdxRibbonInstance_t *state, *oldest = NULL, **link = &model->ribbon_states;
    DWORD ntrails = MDLX_CountRibbons(model), n = 0;

    if (!ntrails) return NULL;
    for (; *link; link = &(*link)->next, n++) {
        if ((*link)->number == number) return *link;
        if (!oldest || (*link)->stamp < oldest->stamp) oldest = *link;
    }
    if (n >= BZ_MDX_RIBBON_INSTANCES && oldest) {
        memset(oldest->trails, 0, sizeof(mdxRibbonTrail_t) * oldest->ntrails);
        oldest->number = number;
        oldest->stamp = 0;
        return oldest;
    }
    state = ri.MemAlloc(sizeof(*state));
    *state = (mdxRibbonInstance_t){ .number = number, .ntrails = ntrails, .next = model->ribbon_states };
    state->trails = ri.MemAlloc(sizeof(mdxRibbonTrail_t) * ntrails);
    memset(state->trails, 0, sizeof(mdxRibbonTrail_t) * ntrails);
    model->ribbon_states = state;
    return state;
}

static void MDLX_RibbonWorldEdge(mdxModel_t const *model, mdxRibbonEmitter_t const *ribbon,
                                 LPCMATRIX4 model_matrix, float heightAbove, float heightBelow,
                                 LPVECTOR3 above, LPVECTOR3 below)
{
    MATRIX4 world;
    VECTOR3 local_above = { 0, heightAbove, 0 };
    VECTOR3 local_below = { 0, -heightBelow, 0 };
    DWORD id = ribbon->node.node_id;

    if (id < (DWORD)model->num_pivots) {
        local_above = Vector3_add(&local_above, &model->pivots[id]);
        local_below = Vector3_add(&local_below, &model->pivots[id]);
    }
    if (id < MDX_MAX_NODES)
        Matrix4_multiply(model_matrix, &node_matrices[id], &world);
    else
        world = *model_matrix;
    *above = Matrix4_multiply_vector3(&world, &local_above);
    *below = Matrix4_multiply_vector3(&world, &local_below);
}

static DWORD MDLX_RibbonIndex(mdxModel_t const *model, mdxRibbonEmitter_t const *ribbon) {
    DWORD idx = 0;
    FOR_EACH_LIST(mdxRibbonEmitter_t, it, model->ribbons) {
        if (it == ribbon) return idx;
        idx++;
    }
    return (DWORD)-1;
}

/* Advance one emitter's per-instance trail and write its current triangle strip. */
DWORD MDLX_EmitRibbonVertices(mdxModel_t *model, renderEntity_t const *entity, LPCMATRIX4 model_matrix,
                              mdxRibbonEmitter_t *ribbon, VERTEX *out, DWORD max)
{
    mdxRibbonInstance_t *state;
    DWORD idx, slot, frame;
    float visibility = 1.0f, heightAbove, heightBelow, alpha, rate, dt;
    VECTOR3 color, above, below;
    COLOR32 rgba;

    if (!model || !entity || !model_matrix || !ribbon || !out) return 0;
    idx = MDLX_RibbonIndex(model, ribbon);
    state = MDLX_RibbonInstance(model, entity->number);
    if (!state || idx >= state->ntrails) return 0;
    state->stamp = tr.viewDef.time;
    frame = entity->frame;
    dt = tr.viewDef.deltaTime / 1000.f;
    heightAbove = ribbon->heightAbove;
    heightBelow = ribbon->heightBelow;
    alpha = ribbon->alpha;
    color = ribbon->color;
    slot = ribbon->textureSlot;
    rate = (float)ribbon->emissionRate;
    if (ribbon->keytracks.Visibility)
        MDLX_GetModelKeytrackValue(model, ribbon->keytracks.Visibility, frame, &visibility);
    if (ribbon->keytracks.HeightAbove)
        MDLX_GetModelKeytrackValue(model, ribbon->keytracks.HeightAbove, frame, &heightAbove);
    if (ribbon->keytracks.HeightBelow)
        MDLX_GetModelKeytrackValue(model, ribbon->keytracks.HeightBelow, frame, &heightBelow);
    if (ribbon->keytracks.Alpha)
        MDLX_GetModelKeytrackValue(model, ribbon->keytracks.Alpha, frame, &alpha);
    if (ribbon->keytracks.Color)
        MDLX_GetModelKeytrackValue(model, ribbon->keytracks.Color, frame, &color);
    if (ribbon->keytracks.TextureSlot)
        MDLX_GetModelKeytrackValue(model, ribbon->keytracks.TextureSlot, frame, &slot);
    MDLX_RibbonWorldEdge(model, ribbon, model_matrix, heightAbove, heightBelow, &above, &below);
    if (visibility < EPSILON) rate = 0.0f;
    MDLX_UpdateRibbonTrail(&state->trails[idx], above, below, ribbon->lifespan, rate, ribbon->gravity, dt);
    rgba = (COLOR32){
        (BYTE)MIN(255, MAX(0, color.x * 255.0f + 0.5f)),
        (BYTE)MIN(255, MAX(0, color.y * 255.0f + 0.5f)),
        (BYTE)MIN(255, MAX(0, color.z * 255.0f + 0.5f)),
        (BYTE)MIN(255, MAX(0, alpha * 255.0f + 0.5f)),
    };
    return MDLX_RibbonStripVertices(&state->trails[idx], ribbon->columns, ribbon->rows, slot, rgba, out, max);
}
