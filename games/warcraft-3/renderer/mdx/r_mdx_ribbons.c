#include "r_mdx.h"
#include <string.h>

#define BZ_MDX_RIBBON_MODELS 16 // concurrently loaded ribboned models tracked for orphan detection
#define BZ_MDX_DETACHED_RIBBONS 32 // entity-less fading trails; each drains within one lifespan

static mdxModel_t *ribbon_models[BZ_MDX_RIBBON_MODELS];
static DWORD ribbon_model_count;
static mdxDetachedRibbon_t *detached_ribbons;
static DWORD detached_count, detached_tick;
static BOOL detached_tick_valid;

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
    DWORD n;

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
    FOR_LOOP(i, ribbon_model_count)
        if (ribbon_models[i] == model) return state;
    if (ribbon_model_count < BZ_MDX_RIBBON_MODELS) /* beyond the cap live trails still work, only fading is skipped */
        ribbon_models[ribbon_model_count++] = model;
    return state;
}

/* The entity draws again (or the model unloads): its detached copy must not double-draw. */
static void MDLX_DropDetachedRibbons(mdxModel_t *model, DWORD number) {
    mdxDetachedRibbon_t **link = &detached_ribbons;
    while (*link) {
        mdxDetachedRibbon_t *dead = *link;
        if (dead->model == model && (number == (DWORD)-1 || dead->number == number)) {
            *link = dead->next; ri.MemFree(dead); detached_count--;
        } else link = &dead->next;
    }
}

void MDLX_ForgetRibbonModel(mdxModel_t *model) {
    FOR_LOOP(i, ribbon_model_count)
        if (ribbon_models[i] == model) {
            ribbon_models[i] = ribbon_models[--ribbon_model_count];
            break;
        }
    MDLX_DropDetachedRibbons(model, (DWORD)-1);
}

#ifndef BZ_MDX_RIBBON_HEADLESS
static VERTEX orphan_verts[TRAIL_MAX_EDGES * 6];
static trailVert_t orphan_strip[TRAIL_MAX_EDGES * 6];
#endif

static mdxRibbonEmitter_t *MDLX_RibbonAt(mdxModel_t const *model, DWORD idx) {
    FOR_EACH_LIST(mdxRibbonEmitter_t, ribbon, model->ribbons)
        if (!idx--) return ribbon;
    return NULL;
}

/* Copy live trails the entity left behind into fading orphans. Runs on the
 * previous completed frame (stamp < model tick) so entities drawn later in the
 * current frame are never mistaken for gone. */
static void MDLX_DetachStaleInstances(mdxModel_t *model, DWORD now) {
    FOR_EACH_LIST(mdxRibbonInstance_t, state, model->ribbon_states) {
        if (state->stamp == now) continue;
        FOR_LOOP(e, state->ntrails) {
            mdxRibbonEmitter_t *ribbon = MDLX_RibbonAt(model, e);
            mdxDetachedRibbon_t *orphan, *it;
            if (!ribbon || !state->trails[e].count) continue;
            for (it = detached_ribbons; it; it = it->next)
                if (it->model == model && it->number == state->number && it->emitter == e) break;
            if (it) continue;
            while (detached_count >= BZ_MDX_DETACHED_RIBBONS) {
                mdxDetachedRibbon_t *oldest = detached_ribbons;
                detached_ribbons = oldest->next; ri.MemFree(oldest); detached_count--;
            }
            orphan = ri.MemAlloc(sizeof(*orphan));
            *orphan = (mdxDetachedRibbon_t){
                .model = model, .number = state->number, .emitter = e,
                .materialId = ribbon->materialId, .columns = ribbon->columns, .rows = ribbon->rows,
                .slot = ribbon->textureSlot, .team = state->team,
                .lifespan = ribbon->lifespan, .gravity = ribbon->gravity,
                .trail = state->trails[e], .next = detached_ribbons,
            };
            detached_ribbons = orphan; detached_count++;
        }
    }
    model->ribbon_tick = now;
}

static void MDLX_FadeRibbonVertices(VERTEX *verts, DWORD nverts, trail_t const *trail,
                                    float lifespan)
{
    int alive, i, write;

    if (!verts || !trail || lifespan <= 0.0f) return;
    alive = trail->count;
    write = trail->head;
    for (i = 0; i < alive - 1 && (DWORD)(i * 6 + 5) < nverts; i++) {
        int a = (write - alive + i + TRAIL_MAX_EDGES) % TRAIL_MAX_EDGES;
        int b = (write - alive + i + 1 + TRAIL_MAX_EDGES) % TRAIL_MAX_EDGES;
        BYTE aa = (BYTE)(trail->edges[a].color.a * MAX(0.0f, 1.0f - trail->edges[a].age / lifespan));
        BYTE ab = (BYTE)(trail->edges[b].color.a * MAX(0.0f, 1.0f - trail->edges[b].age / lifespan));
        verts[i * 6 + 0].color.a = aa; verts[i * 6 + 1].color.a = aa;
        verts[i * 6 + 2].color.a = ab; verts[i * 6 + 3].color.a = aa;
        verts[i * 6 + 4].color.a = ab; verts[i * 6 + 5].color.a = ab;
    }
}

static void MDLX_DrawDetachedRibbon(mdxDetachedRibbon_t *orphan) {
#ifdef BZ_MDX_RIBBON_HEADLESS
    (void)orphan;
#else
    mdxMaterial_t *material;
    DWORD nverts;

    if (!orphan || !orphan->model) return;
    nverts = R_TrailStripVerts(&orphan->trail, orphan->lifespan, orphan->columns,
                               orphan->rows, orphan->slot, orphan_strip,
                               sizeof(orphan_strip) / sizeof(*orphan_strip));
    nverts = MDLX_RibbonConvertStrip(orphan_strip, nverts, orphan_verts,
                                     sizeof(orphan_verts) / sizeof(*orphan_verts));
    MDLX_FadeRibbonVertices(orphan_verts, nverts, &orphan->trail, orphan->lifespan);
    material = MDLX_MaterialAt(orphan->model, orphan->materialId);
    if (nverts && material)
        MDLX_DrawRibbonVerts(orphan->model, orphan_verts, nverts, material, orphan->team);
#endif
}

void MDLX_TickDetachedRibbons(void) {
    mdxDetachedRibbon_t **link;
    DWORD now = tr.viewDef.time;

    if (detached_tick_valid && detached_tick == now) return;
    detached_tick = now; detached_tick_valid = true;
    FOR_LOOP(i, ribbon_model_count)
        MDLX_DetachStaleInstances(ribbon_models[i], now);
    link = &detached_ribbons;
    while (*link) {
        mdxDetachedRibbon_t *orphan = *link;
        if (!R_TrailAdvance(&orphan->trail, (VECTOR3){0}, (VECTOR3){0}, (COLOR32){0},
                            orphan->lifespan, 0.0f, orphan->gravity, now, tr.viewDef.deltaTime) &&
            orphan->trail.count == 0) {
            *link = orphan->next; ri.MemFree(orphan); detached_count--; continue;
        }
        MDLX_DrawDetachedRibbon(orphan);
        link = &orphan->next;
    }
}

DWORD MDLX_DetachedRibbonCount(void) {
    return detached_count;
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
    state->team = entity->team;
    MDLX_DropDetachedRibbons(model, entity->number); /* redrawn after absence: live resumes, orphan must not double-draw */
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
