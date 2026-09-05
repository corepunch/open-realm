#include "r_mdx.h"
#include "renderer/r_local.h"

/* Shared state used across anim, geoset, and render translation units.
   Must live in the first file included by the unity build (alphabetically). */
mdlx_state_t mdlx;

/* Forward declarations for functions defined in r_mdx_interpolation.c */
DWORD GetModelKeyTrackDataTypeSize(MODELKEYTRACKDATATYPE dataType);
DWORD GetModelKeyTrackTypeSize(MODELKEYTRACKTYPE keyTrackType);
DWORD GetModelKeyFrameSize(MODELKEYTRACKDATATYPE dataType, MODELKEYTRACKTYPE keyTrackType);
void R_GetKeyframeValue(mdxKeyFrame_t const *left, mdxKeyFrame_t const *right, mdxKeyTrack_t const *keytrack, DWORD time, HANDLE out);
void R_EvalKeyframeValue(void const *left, void const *right, float t, MODELKEYTRACKDATATYPE datatype, MODELKEYTRACKTYPE linetype, HANDLE out);

static MATRIX4 local_matrices[MDX_MAX_NODES];
MATRIX4 node_matrices[MDX_MAX_NODES];

mdxSequence_t const *R_FindSequenceAtTime(mdxModel_t const *model, DWORD time) {
    FOR_LOOP(seqIndex, model->num_sequences) {
        mdxSequence_t const *seq = &model->sequences[seqIndex];
        if (seq->interval[0] <= time && seq->interval[1] > time) {
            return seq;
        }
    }
    return NULL;
}

static mdxKeyFrame_t *R_KeyFrameAt(mdxKeyTrack_t const *track, DWORD index) {
    DWORD stride = GetModelKeyFrameSize(track->datatype, track->linetype);
    return (mdxKeyFrame_t *)((LPSTR)track->values + stride * index);
}

/* MDX key times are authored in ascending order; binary bounds avoid rescanning every track for every model instance. */
static DWORD R_KeyFrameBound(mdxKeyTrack_t const *track, DWORD time, BOOL upper) {
    DWORD lo = 0, hi = track->keyframeCount;
    while (lo < hi) {
        DWORD mid = lo + (hi - lo) / 2;
        DWORD keytime = R_KeyFrameAt(track, mid)->time;
        if (keytime < time || (upper && keytime == time)) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

void MDLX_GetModelKeytrackValue(mdxModel_t const *model, mdxKeyTrack_t const *keytrack, DWORD time, HANDLE output) {
    DWORD interval[2] = { 0, 0 };

    if (!model || !keytrack || !output || !keytrack->keyframeCount)
        return;
    if (keytrack->globalSeqId != (DWORD)-1) {
        mdxKeyFrame_t *first_authored;

        if (keytrack->globalSeqId >= (DWORD)model->num_globalSequences || !model->globalSequences)
            return;
        interval[0] = 0;
        interval[1] = model->globalSequences[keytrack->globalSeqId].value;

        /* Warsmash treats a global sequence whose first authored key lies
         * beyond the declared duration as a constant equal to that first key.
         * Stock DNC models use this odd contract for their light-node rotation:
         * a zero-duration global sequence points at a later quaternion key. */
        first_authored = R_KeyFrameAt(keytrack, 0);
        if (first_authored->time >= 0 && (DWORD)first_authored->time > interval[1]) {
            memcpy(output, first_authored->data, GetModelKeyTrackDataTypeSize(keytrack->datatype));
            return;
        }

        /* Global sequences follow the render clock instead of entity->frame,
           which loops within the selected sequence. This preserves their full
           range while keeping fixed-time renderer captures deterministic. */
        {
            DWORD gs_len = interval[1] + 1;
            DWORD global_time = tr.viewDef.time ? tr.viewDef.time : SDL_GetTicks();
            time = gs_len > 0 ? (global_time % gs_len) : 0;
        }
    } else {
        mdxSequence_t const *seq = R_FindSequenceAtTime(model, time);
        if (!seq)
            return;
        interval[0] = seq->interval[0];
        interval[1] = seq->interval[1];
    }
    DWORD first_index = R_KeyFrameBound(keytrack, interval[0], false);
    DWORD end_index = R_KeyFrameBound(keytrack, interval[1], true);
    if (first_index >= end_index)
        return;
    mdxKeyFrame_t *first = R_KeyFrameAt(keytrack, first_index);
    mdxKeyFrame_t *last = R_KeyFrameAt(keytrack, end_index - 1);
    if (time >= last->time) {
        /* The interval tail blends back to its first key; keys outside this sequence never participate. */
        DWORD span = (interval[1] - last->time) + (first->time - interval[0]);
        if (first != last && span && time < interval[1]) {
            FLOAT t = (FLOAT)(time - last->time) / (FLOAT)span;
            R_EvalKeyframeValue(last->data, first->data, t, keytrack->datatype, keytrack->linetype, output);
        } else {
            memcpy(output, last->data, GetModelKeyTrackDataTypeSize(keytrack->datatype));
        }
        return;
    }
    DWORD right_index = R_KeyFrameBound(keytrack, time, false);
    mdxKeyFrame_t *right = R_KeyFrameAt(keytrack, right_index);
    if (right_index == first_index || right->time == time)
        memcpy(output, right->data, GetModelKeyTrackDataTypeSize(keytrack->datatype));
    else
        R_GetKeyframeValue(R_KeyFrameAt(keytrack, right_index - 1), right, keytrack, time, output);
}

/* Warcraft animated MDX color tracks use BGR component semantics in the file.
 * Convert the float vector to renderer RGB after interpolation. This is not a
 * texture byte-order conversion, so it is identical on GL, GLES, and hosts with
 * or without native BGRA texture-upload support. */
void MDLX_GetAnimatedColorTrackValue(mdxModel_t const *model,
                                     mdxKeyTrack_t const *keytrack,
                                     DWORD time,
                                     LPVECTOR3 output)
{
    FLOAT red;

    if (!model || !keytrack || !output) return;
    MDLX_GetModelKeytrackValue(model, keytrack, time, output);
    red = output->x;
    output->x = output->z;
    output->z = red;
}

/* GeosetAnimation is the static-color exception to the usual MDX RGB fields:
 * Warsmash swizzles its base vector just like KGAC. This semantic conversion is
 * independent of host endianness and GL/BGRA upload capabilities. */
void MDLX_GetGeosetAnimationStaticColor(mdxGeosetAnim_t const *geosetAnim,
                                        LPVECTOR3 output)
{
    if (!geosetAnim || !output) return;
    output->x = geosetAnim->staticColor.z;
    output->y = geosetAnim->staticColor.y;
    output->z = geosetAnim->staticColor.x;
}

static void R_CalculateNodeMatrix(mdxModel_t const *model, mdxNode_t *node, DWORD frame1, DWORD frame0, LPMATRIX4 matrix) {
    VECTOR3 vTranslation = { 0, 0, 0 };
    QUATERNION vRotation = { 0, 0, 0, 1 };
    VECTOR3 vScale = { 1, 1, 1 };
    VECTOR3 zero_pivot = { 0, 0, 0 };
    LPCVECTOR3 pivot = &zero_pivot;
    if (node->node_id < (DWORD)model->num_pivots) {
        pivot = (VECTOR3 const *)&model->pivots[node->node_id];
    }
    if (frame0 != frame1) {
        if (node->translation) {
            VECTOR3 t0 = vTranslation, t1 = vTranslation;
            MDLX_GetModelKeytrackValue(model, node->translation, frame0, &t0);
            MDLX_GetModelKeytrackValue(model, node->translation, frame1, &t1);
            vTranslation = Vector3_lerp(&t0, &t1, tr.viewDef.lerpfrac);
        }
        if (node->rotation) {
            QUATERNION r0 = vRotation, r1 = vRotation;
            MDLX_GetModelKeytrackValue(model, node->rotation, frame0, &r0);
            MDLX_GetModelKeytrackValue(model, node->rotation, frame1, &r1);
            vRotation = Quaternion_slerp(&r0, &r1, tr.viewDef.lerpfrac);
        }
        if (node->scale) {
            VECTOR3 s0 = vScale, s1 = vScale;
            MDLX_GetModelKeytrackValue(model, node->scale, frame0, &s0);
            MDLX_GetModelKeytrackValue(model, node->scale, frame1, &s1);
            vScale = Vector3_lerp(&s0, &s1, tr.viewDef.lerpfrac);
        }
    } else {
        if (node->translation) {
            MDLX_GetModelKeytrackValue(model, node->translation, frame1, &vTranslation);
        }
        if (node->rotation) {
            MDLX_GetModelKeytrackValue(model, node->rotation, frame1, &vRotation);
        }
        if (node->scale) {
            MDLX_GetModelKeytrackValue(model, node->scale, frame1, &vScale);
        }
    }
    if (!node->translation && !node->rotation && !node->scale) {
        Matrix4_identity(matrix);
    } else if (node->translation && !node->rotation && !node->scale) {
        Matrix4_from_translation(matrix, &vTranslation);
    } else if (!node->translation && node->rotation && !node->scale) {
        Matrix4_from_rotation_origin(matrix,  &vRotation, pivot);
    } else {
        Matrix4_from_rotation_translation_scale_origin(matrix, &vRotation, &vTranslation, &vScale, pivot);
    }
}

LPCMATRIX4 R_GetNodeGlobalMatrix(mdxModel_t const *model, LPCMATRIX4 model_matrix, mdxNode_t const *node) {
    if (!node || node->node_id >= MDX_MAX_NODES) {
        return NULL;
    }
    LPMATRIX4 global_matrix = node_matrices + node->node_id;
    LPMATRIX4 local_matrix = local_matrices+node->node_id;
    if (global_matrix->v[15] == 0) {
        if (node->parent_id != -1 && node->parent_id < MDX_MAX_NODES && model->nodes[node->parent_id]) {
            LPCMATRIX4 parent_matrix = R_GetNodeGlobalMatrix(model, model_matrix, model->nodes[node->parent_id]);
            if (!parent_matrix) {
                return NULL;
            }
            Matrix4_multiply(parent_matrix, local_matrix, global_matrix);
        } else {
            *global_matrix = *local_matrix;
        }
        if (node->flags & MDLXNODE_Billboarded) {
            MATRIX4 tmp1, tmp2;
            VECTOR3 pivot = { 0, 0, 0 };
            if (node->node_id < (DWORD)model->num_pivots) {
                pivot = *(LPCVECTOR3)(&model->pivots[node->node_id]);
            }
            VECTOR3 tmppvt = Matrix4_multiply_vector3(global_matrix, &pivot);
            if (node->parent_id != -1 && node->parent_id < MDX_MAX_NODES && model->nodes[node->parent_id]) {
                LPCMATRIX4 parent_matrix = R_GetNodeGlobalMatrix(model, model_matrix, model->nodes[node->parent_id]);
                if (!parent_matrix) {
                    return NULL;
                }
                QUATERNION tmprot = Quaternion_fromMatrix(parent_matrix);
                tmprot = Quaternion_unm(&tmprot);
                Matrix4_from_rotation_origin(&tmp1, &tmprot, &tmppvt);
                Matrix4_multiply(&tmp1, global_matrix, &tmp2);
                *global_matrix = tmp2;
            }
            
            Matrix4_identity(&tmp1);
            Matrix4_rotate(&tmp1, &(VECTOR3){30,0,90}, ROTATE_XYZ);
            Matrix4_multiply(&tmp1, model_matrix, &tmp2);

            QUATERNION viewrot = Quaternion_fromMatrix(&tmp2);
            viewrot = Quaternion_unm(&viewrot);
            Matrix4_from_rotation_origin(&tmp1, &viewrot, &tmppvt);
            Matrix4_multiply(&tmp1, global_matrix, &tmp2);
            *global_matrix = tmp2;
        }
    }
    return global_matrix;
}

void AddSkin(LPVECTOR3 pos, LPCMATRIX4 mat, LPCVECTOR3 org, FLOAT weight) {
    if (weight == 0) return;
    VECTOR3 val = Matrix4_multiply_vector3(mat, org);
    val = Vector3_scale(&val, weight);
    *pos = Vector3_add(pos, &val);
}

void MDLX_BindBoneMatrices(mdxModel_t const *model, LPCMATRIX4 model_matrix, DWORD frame1, DWORD frame0) {
    /* Only the nodes this model actually has need their global matrices
     * recomputed.  The old path memset the full 64KB node_matrices array and
     * scanned all MDX_MAX_NODES slots twice; models have tens of nodes, so the
     * compact node_list built at load time is orders of magnitude smaller. */
    FOR_LOOP(i, model->num_nodes) {
        mdxNode_t *node = model->node_list[i];
        memset(&node_matrices[node->node_id], 0, sizeof(MATRIX4)); /* reset the "computed" flag (v[15]==0) */
        R_CalculateNodeMatrix(model, node, frame1, frame0, &local_matrices[node->node_id]);
    }
    FOR_LOOP(i, model->num_nodes)
        R_GetNodeGlobalMatrix(model, model_matrix, model->node_list[i]);
}

/* Resolve authored attachment pivots from the same interpolated node pose used
 * for geometry. Callers can filter by a name prefix (for example "Sprite ")
 * without depending on list order in the MDX file. */
DWORD MDLX_CollectAttachmentPositions(mdxModel_t const *model, LPCMATRIX4 model_matrix,
                                      DWORD frame, DWORD oldframe, LPCSTR prefix,
                                      mdxAttachmentPosition_t *positions, DWORD max_positions) {
    DWORD count = 0;
    size_t const prefix_len = prefix ? strlen(prefix) : 0;

    if (!model || !model_matrix || !positions || !max_positions) {
        return 0;
    }

    MDLX_BindBoneMatrices(model, model_matrix, frame, oldframe);
    FOR_EACH_LIST(mdxAttachment_t, attachment, model->attachments) {
        mdxNode_t const *node = &attachment->node;
        VECTOR3 pivot = { 0, 0, 0 };
        VECTOR3 local;
        float visibility = 1.0f;

        if (prefix_len && strncasecmp(node->name, prefix, prefix_len)) {
            continue;
        }
        if (attachment->Visibility) {
            MDLX_GetModelKeytrackValue(model, attachment->Visibility, frame, &visibility);
            if (visibility < EPSILON) {
                continue;
            }
        }
        if (node->node_id < (DWORD)model->num_pivots) {
            pivot = model->pivots[node->node_id];
        }
        local = pivot;
        if (node->node_id < MDX_MAX_NODES && model->nodes[node->node_id]) {
            local = Matrix4_multiply_vector3(&node_matrices[node->node_id], &pivot);
        }
        positions[count].name = node->name;
        positions[count].origin = Matrix4_multiply_vector3(model_matrix, &local);
        count++;
        if (count == max_positions) {
            break;
        }
    }
    return count;
}
