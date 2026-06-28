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
static MATRIX4 bone_matrices[MDX_MATRIX_PALETTE];

mdxSequence_t const *R_FindSequenceAtTime(mdxModel_t const *model, DWORD time) {
    FOR_LOOP(seqIndex, model->num_sequences) {
        mdxSequence_t const *seq = &model->sequences[seqIndex];
        if (seq->interval[0] <= time && seq->interval[1] > time) {
            return seq;
        }
    }
    return NULL;
}

void MDLX_GetModelKeytrackValue(mdxModel_t const *model, mdxKeyTrack_t const *keytrack, DWORD time, HANDLE output) {
    DWORD const keyframeSize = GetModelKeyFrameSize(keytrack->datatype, keytrack->linetype);
    LPCSTR keyFrames = (LPCSTR)keytrack->values;
    mdxKeyFrame_t *prevKeyFrame = NULL;
    DWORD interval[2] = { 0, 0 };
    if (keytrack->globalSeqId != -1) {
        interval[0] = 0;
        interval[1] = model->globalSequences[keytrack->globalSeqId].value;
        /* Reference (WarsmashModEngine / MdxComplexInstance.updateAnimations):
         * global sequences use a monotonically increasing counter that does NOT
         * reset when the per-sequence frame loops.  Our entity->frame cycles
         * within the sequence interval (e.g. 11267..12700 for Stand), which
         * would lock the global sequence to a tiny window of its keyframes.
         * Use the wall clock instead so all global sequence keyframes play. */
        DWORD gs_len = model->globalSequences[keytrack->globalSeqId].value + 1;
        time = gs_len > 0 ? (SDL_GetTicks() % gs_len) : 0;
    } else {
        mdxSequence_t const *seq = R_FindSequenceAtTime(model, time);
        if (!seq)
            return;
        interval[0] = seq->interval[0];
        interval[1] = seq->interval[1];
    }
    {
        /* Find the first and last keyframe within the sequence interval.
         * Used by both the main evaluation loop and the wrap-at-end path. */
        mdxKeyFrame_t *firstKF = NULL, *lastKF = NULL;

        FOR_LOOP(i, keytrack->keyframeCount) {
            mdxKeyFrame_t *kf = (HANDLE)(keyFrames + keyframeSize * i);
            if (kf->time < interval[0] || kf->time > interval[1]) continue;
            if (!firstKF || kf->time < firstKF->time) firstKF = kf;
            if (!lastKF  || kf->time > lastKF->time)  lastKF  = kf;
        }

        if (lastKF && firstKF && time >= lastKF->time && time <= interval[1]) {
            /* Reference (WarsmashModEngine / SdSequence.getValue): when frame
             * exceeds the last keyframe within the interval, wrap back toward
             * the first keyframe over the remaining interval span. */
            if (firstKF != lastKF) {
                FLOAT end_to_start = (FLOAT)((interval[1] - lastKF->time) + (firstKF->time - interval[0]));
                FLOAT wrap_t = (FLOAT)(time - lastKF->time) / end_to_start;
                if (wrap_t < 1.0f) {
                    R_EvalKeyframeValue(lastKF->data, firstKF->data,
                        wrap_t, keytrack->datatype, keytrack->linetype, output);
                    return;
                }
            }
            memcpy(output, lastKF->data, GetModelKeyTrackDataTypeSize(keytrack->datatype));
            return;
        }

        FOR_LOOP(keyframeIndex, keytrack->keyframeCount) {
            mdxKeyFrame_t *keyFrame = (HANDLE)(keyFrames + keyframeSize * keyframeIndex);
            if (keyFrame->time < interval[0])
                continue;
            if (keyFrame->time > interval[1]) {
                if (prevKeyFrame) {
                    memcpy(output, prevKeyFrame->data, GetModelKeyTrackDataTypeSize(keytrack->datatype));
                }
                return;
            }
            if (keyFrame->time == time || (keyFrame->time > time && !prevKeyFrame)) {
                memcpy(output, keyFrame->data, GetModelKeyTrackDataTypeSize(keytrack->datatype));
                return;
            }
            if (keyFrame->time > time) {
                R_GetKeyframeValue(prevKeyFrame, keyFrame, keytrack, time, output);
                return;
            }
            prevKeyFrame = keyFrame;
        }
        if (prevKeyFrame) {
            memcpy(output, prevKeyFrame->data, GetModelKeyTrackDataTypeSize(keytrack->datatype));
        }
    }
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

static void MDLX_BindBoneMatrices(mdxModel_t const *model, LPCMATRIX4 model_matrix, DWORD frame1, DWORD frame0) {
    LPSHADER shader = mdlx.shader;
    memset(node_matrices, 0, sizeof(node_matrices));

    FOR_LOOP(node_id, MDX_MAX_NODES) {
        mdxNode_t *node = model->nodes[node_id];
        if (!node)
            continue;
        R_CalculateNodeMatrix(model, node, frame1, frame0, &local_matrices[node->node_id]);
    }
    FOR_LOOP(node_id, MDX_MAX_NODES) {
        mdxNode_t *node = model->nodes[node_id];
        if (!node)
            continue;
        R_GetNodeGlobalMatrix(model, model_matrix, node);
    }

    FOR_LOOP(i, MDX_MATRIX_PALETTE) {
        Matrix4_identity(&bone_matrices[i]);
        if (i < MDX_MAX_NODES && model->nodes[i]) {
            bone_matrices[i] = node_matrices[i];
        }
    }
    
#if 0
    if (model->knight) {
        static VECTOR3 pos[1024];
        FOR_EACH_LIST(mdxGeoset_t, geoset, model->geosets) {
            FOR_LOOP(i, geoset->num_vertices) {
                VECTOR3 org = geoset->vertices[i];
                memset(pos+i, 0, sizeof(VECTOR3));
                FOR_LOOP(j, MAX_SKIN_BONES) {
                    AddSkin(pos+i, global_matrices+geoset->skinning[i].skin[j], &org, geoset->skinning[i].boneWeight[j]/255.f);
            }
            R_Call(glBindBuffer, GL_ARRAY_BUFFER, geoset->buffer[1]);
            R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(VECTOR3) * geoset->vertices, pos, GL_STATIC_DRAW);
            R_Call(glVertexAttribPointer, attrib_position, 3, GL_FLOAT, GL_FALSE, 0, 0);
            R_Call(glEnableVertexAttribArray, attrib_position);
        }
    }
#endif
    R_Call(glUseProgram, shader->progid);
    R_Call(glUniformMatrix4fv, shader->uBones, MDX_MATRIX_PALETTE, GL_FALSE, bone_matrices->v);
}
