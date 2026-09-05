#include "r_mdx.h"
#include "renderer/r_local.h"

BOOL MDLX_EvaluateLight(mdxModel_t const *model,
                        mdxLight_t const *light,
                        LPCMATRIX4 modelMatrix,
                        DWORD frame,
                        BOOL useVisibility,
                        LPRMODELLIGHT output)
{
    float visibility = 1.0f;
    VECTOR3 color, ambc;
    float intensity, ambIntensity, astart;
    VECTOR3 pivot = { 0, 0, 0 };
    VECTOR3 localPos, localDirTarget, worldPos, worldDirTarget, worldDir;

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

    if (light->node.node_id < (DWORD)model->num_pivots)
        pivot = model->pivots[light->node.node_id];
    localPos = pivot;
    localDirTarget = (VECTOR3){ pivot.x, pivot.y, pivot.z - 1.0f };
    if (light->node.node_id < MDX_MAX_NODES && model->nodes[light->node.node_id]) {
        localPos = Matrix4_multiply_vector3(&node_matrices[light->node.node_id], &pivot);
        localDirTarget = Matrix4_multiply_vector3(&node_matrices[light->node.node_id], &localDirTarget);
    }

    worldPos = Matrix4_multiply_vector3(modelMatrix, &localPos);
    worldDirTarget = Matrix4_multiply_vector3(modelMatrix, &localDirTarget);
    worldDir = Vector3_sub(&worldDirTarget, &worldPos);
    if (Vector3_lengthsq(&worldDir) < EPSILON)
        worldDir = (VECTOR3){ 0, 0, -1 };
    else
        Vector3_normalize(&worldDir);

    *output = (RMODELLIGHT){
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
BOOL MDLX_SampleFirstLight(LPCMODEL model, FLOAT ratio, LPRMODELLIGHT output) {
    mdxModel_t const *mdx;
    mdxSequence_t const *seq;
    MATRIX4 identity;
    DWORD length, offset, frame;

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
    offset = (DWORD)floorf(ratio * (FLOAT)length);
    if (offset >= length) offset = length - 1;
    frame = seq->interval[0] + offset;

    /* A unit DNC is shared by every unit in the view. Avoid rebinding the same
     * tiny MDX hierarchy once per entity while keeping the cache render-frame
     * local so resource reloads cannot leave a stale cross-frame result. */
    {
        typedef struct {
            LPCMODEL model;
            DWORD frame;
            DWORD viewTime;
            RMODELLIGHT light;
        } DNC_SAMPLE_CACHE;
        static DNC_SAMPLE_CACHE cache[2];
        static DWORD nextCache;

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

        cache[nextCache] = (DNC_SAMPLE_CACHE){
            .model = model,
            .frame = frame,
            .viewTime = tr.viewDef.time,
            .light = *output,
        };
        nextCache = (nextCache + 1) % 2;
        return true;
    }
}
