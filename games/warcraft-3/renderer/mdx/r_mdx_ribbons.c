#include "r_mdx.h"
#include <string.h>

static DWORD MDLX_CountRibbons(mdxModel_t const *model) {
    DWORD n = 0;
    FOR_EACH_LIST(mdxRibbonEmitter_t, ribbon, model->ribbons) n++;
    return n;
}

/* Engine strip verts carry position/uv/color; the shared model shader also
 * needs an identity bone bind (skin 0, weight 255) plus a face normal. */
static trailVert_t strip_buf[TRAIL_MAX_EDGES * 6];

static DWORD MDLX_RibbonConvertStrip(trailVert_t const *strip, DWORD nverts, VERTEX *out, DWORD max)
{
    DWORD n, i;

    if (!strip || !out) return 0;
    n = MIN(nverts, max);
    memset(out, 0, n * sizeof(*out));
    FOR_LOOP(i, n) {
        out[i].position = strip[i].position;
        out[i].texcoord = strip[i].uv;
        out[i].color = strip[i].color;
        out[i].normal = (VECTOR3){ 0, 0, 1 };
        out[i].boneWeight[0] = 255;
    }
    return n;
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
        memset(oldest->trails, 0, sizeof(trail_t) * oldest->ntrails);
        oldest->number = number;
        oldest->stamp = 0;
        return oldest;
    }
    state = ri.MemAlloc(sizeof(*state));
    *state = (mdxRibbonInstance_t){ .number = number, .ntrails = ntrails, .next = model->ribbon_states };
    state->trails = ri.MemAlloc(sizeof(trail_t) * ntrails);
    memset(state->trails, 0, sizeof(trail_t) * ntrails);
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
    trail_t *trail;
    DWORD idx, slot, frame, nverts;
    float visibility = 1.0f, heightAbove, heightBelow, alpha, rate;
    VECTOR3 color, above, below;
    COLOR32 rgba;

    if (!model || !entity || !model_matrix || !ribbon || !out) return 0;
    idx = MDLX_RibbonIndex(model, ribbon);
    state = MDLX_RibbonInstance(model, entity->number);
    if (!state || idx >= state->ntrails) return 0;
    trail = &state->trails[idx];
    frame = entity->frame;
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
    rgba = (COLOR32){
        (BYTE)MIN(255, MAX(0, color.x * 255.0f + 0.5f)),
        (BYTE)MIN(255, MAX(0, color.y * 255.0f + 0.5f)),
        (BYTE)MIN(255, MAX(0, color.z * 255.0f + 0.5f)),
        (BYTE)MIN(255, MAX(0, alpha * 255.0f + 0.5f)),
    };
    /* Engine trail owns stamp/stale/clamp/U; per-edge color keeps animated tracks historic. */
    R_TrailAdvance(trail, above, below, rgba, ribbon->lifespan, rate, ribbon->gravity,
                   tr.viewDef.time, tr.viewDef.deltaTime);
    state->stamp = trail->stamp;
    nverts = R_TrailStripVerts(trail, ribbon->lifespan, ribbon->columns, ribbon->rows, slot,
                               strip_buf, sizeof(strip_buf) / sizeof(*strip_buf));
    return MDLX_RibbonConvertStrip(strip_buf, nverts, out, max);
}
