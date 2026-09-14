#include "r_mdx.h"
#include "renderer/r_emit.h"

#define GET_PARTICLE_ANIM_PARAM(MODEL, EMITTER, NAME) \
float NAME = EMITTER->NAME; \
if (EMITTER->keytracks.NAME) { \
    MDLX_GetModelKeytrackValue(MODEL, EMITTER->keytracks.NAME, frame, &NAME); \
}

/* Context for the R_EmitParticles spawn callback — carries the evaluated tracks
   and emitter metadata needed to fill a cparticle_t on each spawn. */
typedef struct {
    mdxModel_t const *model; mdxParticleEmitter_t const *emitter;
    LPCMATRIX4 matrix; DWORD team_id;
    float speed, varia, lat, grav, life, length, width;
} mdx_pctx_t;

static COLOR32 MDLX_GetEmitterColor(mdxParticleEmitter_t const *emitter, DWORD seg) {
    return (COLOR32) {
        emitter->SegmentColor[seg*3+0] * 0xff,
        emitter->SegmentColor[seg*3+1] * 0xff,
        emitter->SegmentColor[seg*3+2] * 0xff,
        emitter->Alpha[seg],
    };
}

static void mdx_spawn_particle(void *raw) {
    mdx_pctx_t *ctx = (mdx_pctx_t *)raw;
    cparticle_t *p = R_SpawnParticle(); if (!p) return;
    float r = (float)rand() / (float)RAND_MAX;
    VECTOR3 origin = {
        (r - 0.5f) * ctx->length,
        ((float)rand() / (float)RAND_MAX - 0.5f) * ctx->width,
        0.0f,
    };
    VECTOR3 pivot = { 0, 0, 0 };
    if (ctx->emitter->node.node_id < (DWORD)ctx->model->num_pivots)
        pivot = ctx->model->pivots[ctx->emitter->node.node_id];
    VECTOR3 pivoted = Vector3_add(&origin, &pivot);
    VECTOR3 dir = FX_GenerateRandomDirection(ctx->lat * (float)M_PI / 180.0f);
    p->org = Matrix4_multiply_vector3(ctx->matrix, &pivoted);
    p->vel = Vector3_scale(&dir, ctx->speed + (r - 0.5f) * ctx->varia);
    p->accel = (VECTOR3){ 0, 0, -ctx->grav };
    p->lifespan = ctx->life; p->time = 0;
    p->midtime = ctx->emitter->Time * 0xff;
    p->texture = MDLX_GetTexture(ctx->model, ctx->team_id, ctx->emitter->TextureID, ctx->emitter->ReplaceableId, NULL);
    p->blend_mode = MDLX_ParticleBlendMode(ctx->emitter->FilterMode);
    p->columns = ctx->emitter->Columns; p->rows = ctx->emitter->Rows;
    p->color[0] = MDLX_GetEmitterColor(ctx->emitter, 0);
    p->color[1] = MDLX_GetEmitterColor(ctx->emitter, 1);
    p->color[2] = MDLX_GetEmitterColor(ctx->emitter, 2);
    R_EncodeParticleSize(p, ctx->emitter->ParticleScaling);
    if (ctx->emitter->FrameFlags == BZ_MDX_PARTICLE_BOTH) {
        cparticle_t *tail = R_SpawnParticle();
        if (tail) { cparticle_t *next = tail->next; *tail = *p; tail->next = next; p = tail; }
        else return;
    }
    if (ctx->emitter->FrameFlags != BZ_MDX_PARTICLE_HEAD)
        p->tail = Vector3_scale(&p->vel, ctx->emitter->TailLength);
}

/* Frame-relative accumulator emission via R_EmitParticles — replaces the old
   whole-second time-anchored loop.  Uses emitter->accumulator to track fractional
   emission across frames (same pattern as WoW's M2_DrawParticles). */
static void MDLX_RenderHeadEmitter(mdxModel_t const *model,
                                   mdxParticleEmitter_t *emitter,
                                   LPCMATRIX4 modelMatrix,
                                   float frame,
                                   DWORD teamID)
{
    GET_PARTICLE_ANIM_PARAM(model, emitter, EmissionRate);
    GET_PARTICLE_ANIM_PARAM(model, emitter, Speed);
    GET_PARTICLE_ANIM_PARAM(model, emitter, Variation);
    GET_PARTICLE_ANIM_PARAM(model, emitter, Latitude);
    GET_PARTICLE_ANIM_PARAM(model, emitter, Gravity);
    GET_PARTICLE_ANIM_PARAM(model, emitter, Width);
    GET_PARTICLE_ANIM_PARAM(model, emitter, Length);
    if (EmissionRate <= 0.0f) return;
    if (emitter->node.node_id >= MDX_MAX_NODES) return;
    MATRIX4 matrix;
    Matrix4_multiply(modelMatrix, &node_matrices[emitter->node.node_id], &matrix);
    mdx_pctx_t ctx = { model, emitter, &matrix, teamID,
        Speed, Variation, Latitude, Gravity, emitter->LifeSpan, Length, Width };
    R_EmitParticles(EmissionRate, &emitter->accumulator, tr.viewDef.deltaTime, mdx_spawn_particle, &ctx);
}

void MDLX_RenderParticleEmitters(const renderEntity_t *entity, const mdxModel_t *model, LPCMATRIX4 model_matrix) {
    /*
     * Dead destructable remains are marked RF_NOT_SELECTABLE.  While their
     * death sequence is advancing oldframe != frame, so the destruction
     * emitters remain active.  tree_decay1() holds the final frame once the
     * death sequence finishes; after the next snapshot oldframe == frame.
     *
     * Do not keep evaluating/emitting particles indefinitely from that held
     * final death frame. Existing particles remain in the particle system and
     * expire normally.
     */
    if ((entity->flags & RF_NOT_SELECTABLE) &&
        entity->oldframe == entity->frame) {
        return;
    }
    float const frame = LerpNumber(entity->oldframe, entity->frame, tr.viewDef.lerpfrac);

    FOR_EACH_LIST(mdxParticleEmitter_t, emitter, model->emitters) {
        float visibility = 1.0f, rate = emitter->EmissionRate;

        if (emitter->keytracks.Visibility) {
            MDLX_GetModelKeytrackValue(model, emitter->keytracks.Visibility, entity->frame, &visibility);
            if (visibility < EPSILON)
                continue;
        }
        if (emitter->keytracks.EmissionRate)
            MDLX_GetModelKeytrackValue(model, emitter->keytracks.EmissionRate, frame, &rate);
        MDLX_RenderHeadEmitter(model, emitter, model_matrix, frame, entity->team&TEAM_MASK);
    }
}
