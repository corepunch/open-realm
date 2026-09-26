#include "r_mdx.h"
#include "renderer/r_local.h"

bool MDLX_EvaluateLight(mdxModel_t const *model,
                        mdxLight_t const *light,
                        matrix4_t const * modelMatrix,
                        uint32_t frame,
                        bool useVisibility,
                        rModelLight_t * output)
{
    float visibility = 1.0f;
    vector3_t color, ambc;
    float intensity, ambIntensity, astart;
    vector3_t pivot = { 0, 0, 0 };
    vector3_t localPos, localDirTarget, worldPos, worldDirTarget, worldDir;

    if (!model || !light || !modelMatrix || !output) return false;

    color = light->Color;
    ambc = light->AmbColor;
    intensity = light->Intensity;
    ambIntensity = light->AmbIntensity;
    astart = light->AttenuationStart;

    if (useVisibility && light->keytracks.Visibility)
        MDLX_GetModelKeytrackValue(model, light->keytracks.Visibility, frame, &visibility);
    if (useVisibility && visibility < EPSILON)
        return false;
    if (light->keytracks.Color)
        MDLX_GetAnimatedColorTrackValue(model, light->keytracks.Color, frame, &color);
    if (light->keytracks.Intensity)
        MDLX_GetModelKeytrackValue(model, light->keytracks.Intensity, frame, &intensity);
    if (light->keytracks.AmbColor)
        MDLX_GetAnimatedColorTrackValue(model, light->keytracks.AmbColor, frame, &ambc);
    if (light->keytracks.AmbIntensity)
        MDLX_GetModelKeytrackValue(model, light->keytracks.AmbIntensity, frame, &ambIntensity);
    if (light->keytracks.AttenuationStart)
        MDLX_GetModelKeytrackValue(model, light->keytracks.AttenuationStart, frame, &astart);

    if (light->node.node_id < (uint32_t)model->num_pivots)
        pivot = model->pivots[light->node.node_id];
    localPos = pivot;
    localDirTarget = (vector3_t){ pivot.x, pivot.y, pivot.z - 1.0f };
    if (light->node.node_id < MDX_MAX_NODES && model->nodes[light->node.node_id]) {
        localPos = Matrix4_multiply_vector3(&node_matrices[light->node.node_id], &pivot);
        localDirTarget = Matrix4_multiply_vector3(&node_matrices[light->node.node_id], &localDirTarget);
    }

    worldPos = Matrix4_multiply_vector3(modelMatrix, &localPos);
    worldDirTarget = Matrix4_multiply_vector3(modelMatrix, &localDirTarget);
    worldDir = Vector3_sub(&worldDirTarget, &worldPos);
    if (Vector3_lengthsq(&worldDir) < EPSILON)
        worldDir = (vector3_t){ 0, 0, -1 };
    else
        Vector3_normalize(&worldDir);

    *output = (rModelLight_t){
        .pos = worldPos,
        .dir = Vector3_unm(&worldDir),
        .color = color,
        .ambient = ambc,
        .atten_start = astart,
        .intensity = intensity * visibility,
        .ambient_intensity = ambIntensity * visibility,
        .type = (RMODELLIGHTTYPE)light->type,
    };
    return true;
}

/* Warcraft DNC instances are held on sequence 0 and scrubbed by a normalized
 * game-time ratio. Warsmash consumes the first light from each DNC instance
 * directly; its DNC world-light manager does not filter that light through the
 * normal scene-light visibility list. */
bool MDLX_SampleFirstLight(model_t const * model, float ratio, rModelLight_t * output) {
    mdxModel_t const *mdx;
    mdxSequence_t const *seq;
    matrix4_t identity;
    uint32_t length, offset, frame;

    if (!model || model->modeltype != ID_MDLX || !model->mdx || !output)
        return false;
    mdx = model->mdx;
    if (!mdx->lights || !mdx->sequences || mdx->num_sequences < 1)
        return false;

    if (!isfinite(ratio)) ratio = 0.0f;
    ratio = MAX(0.0f, MIN(ratio, 1.0f));
    seq = &mdx->sequences[0];
    length = seq->interval[1] - seq->interval[0];
    if (length == 0) length = 1;
    offset = (uint32_t)floorf(ratio * (float)length);
    if (offset >= length) offset = length - 1;
    frame = seq->interval[0] + offset;

    /* A unit DNC is shared by every unit in the view. Avoid rebinding the same
     * tiny MDX hierarchy once per entity while keeping the cache render-frame
     * local so resource reloads cannot leave a stale cross-frame result. */
    {
        typedef struct {
            model_t const * model;
            uint32_t frame;
            uint32_t viewTime;
            rModelLight_t light;
        } dncSampleCache_t;
        static dncSampleCache_t cache[2];
        static uint32_t nextCache;

        FOR_LOOP(i, 2) {
            if (cache[i].model == model && cache[i].frame == frame &&
                cache[i].viewTime == tr.viewDef.time) {
                *output = cache[i].light;
                return true;
            }
        }

        Matrix4_identity(&identity);
        MDLX_BindBoneMatrices(mdx, &identity, frame, frame);
        if (!MDLX_EvaluateLight(mdx, mdx->lights, &identity, frame, false, output))
            return false;

        cache[nextCache] = (dncSampleCache_t){
            .model = model,
            .frame = frame,
            .viewTime = tr.viewDef.time,
            .light = *output,
        };
        nextCache = (nextCache + 1) % 2;
        return true;
    }
}
