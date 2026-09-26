#include "r_mdx.h"
#include "renderer/r_local.h"
#include <ctype.h>

/* Shared state used across anim, geoset, and render translation units.
   Must live in the first file included by the unity build (alphabetically). */
mdlx_state_t mdlx;

/* Forward declarations for functions defined in r_mdx_interpolation.c */
uint32_t GetModelKeyTrackDataTypeSize(MODELKEYTRACKDATATYPE dataType);
uint32_t GetModelKeyTrackTypeSize(MODELKEYTRACKTYPE keyTrackType);
uint32_t GetModelKeyFrameSize(MODELKEYTRACKDATATYPE dataType, MODELKEYTRACKTYPE keyTrackType);
void R_GetKeyframeValue(mdxKeyFrame_t const *left, mdxKeyFrame_t const *right, mdxKeyTrack_t const *keytrack, uint32_t time, handle_t out);
void R_EvalKeyframeValue(void const *left, void const *right, float t, MODELKEYTRACKDATATYPE datatype, MODELKEYTRACKTYPE linetype, handle_t out);

static mat4_t local_matrices[MDX_MAX_NODES];
mat4_t node_matrices[MDX_MAX_NODES];

mdxSequence_t const *R_FindSequenceAtTime(mdxModel_t const *model, uint32_t time) {
    FOR_LOOP(seqIndex, model->num_sequences) {
        mdxSequence_t const *seq = &model->sequences[seqIndex];
        if (seq->interval[0] <= time && seq->interval[1] > time) {
            return seq;
        }
    }
    return NULL;
}

bool MDLX_EventKeyCrossed(mdxModel_t const *model, mdxEvent_t const *event, uint32_t key,
                          uint32_t previous_frame, uint32_t current_frame,
                          uint32_t previous_time, uint32_t current_time) {
    mdxSequence_t const *previous_seq, *current_seq;

    if (!model || !event) return false;
    if (event->globalSeqId != (uint32_t)-1) {
        uint32_t duration, previous, current;
        if (event->globalSeqId >= (uint32_t)model->num_globalSequences || !model->globalSequences) return false;
        duration = model->globalSequences[event->globalSeqId].value;
        if (!duration) return false;
        if (current_time >= previous_time && current_time - previous_time >= duration) return true;
        previous = previous_time % duration;
        current = current_time % duration;
        return current >= previous ? (key > previous && key <= current)
                                   : (key > previous || key <= current);
    }

    previous_seq = R_FindSequenceAtTime(model, previous_frame);
    current_seq = R_FindSequenceAtTime(model, current_frame);
    if (!current_seq || key < current_seq->interval[0] || key > current_seq->interval[1]) return false;
    if (previous_seq != current_seq) return key >= current_seq->interval[0] && key <= current_frame;
    if (current_frame >= previous_frame) return key > previous_frame && key <= current_frame;
    return key > previous_frame || key <= current_frame;
}

bool MDLX_EventObjectId(mdxEvent_t const *event, cstring_t type, char *out, uint32_t out_size) {
    size_t name_len, offset, len;
    if (!event || !type || !out || out_size < 2 || strlen(type) != 3) return false;
    name_len = strnlen(event->node.name, sizeof(event->node.name));
    if (name_len <= 4 || strncmp(event->node.name, type, 3)) return false;
    offset = 4;
    while (offset < name_len && isspace((unsigned char)event->node.name[offset])) offset++;
    len = name_len - offset;
    while (len && isspace((unsigned char)event->node.name[offset + len - 1])) len--;
    if (!len) return false;
    len = MIN(len, (size_t)out_size - 1);
    memcpy(out, event->node.name + offset, len); out[len] = '\0';
    return true;
}

/* Keep the animated node basis and place the child at its transformed pivot. */
bool MDLX_EventWorldTransform(mdxModel_t const *model, mdxEvent_t const *event,
                              renderEntity_t const *entity, mat4_t const *model_transform,
                              mat4_t *out) {
    vec3_t pivot = {0}, local, world;
    if (!model || !event || !entity || !model_transform || !out) return false;
    MDLX_BindBoneMatrices(model, model_transform, entity->frame, entity->oldframe);
    if (event->node.node_id < (uint32_t)model->num_pivots) pivot = model->pivots[event->node.node_id];
    local = pivot; *out = *model_transform;
    if (event->node.node_id < MDX_MAX_NODES && model->nodes[event->node.node_id]) {
        local = Matrix4_multiply_vector3(&node_matrices[event->node.node_id], &pivot);
        Matrix4_multiply(model_transform, &node_matrices[event->node.node_id], out);
    }
    world = Matrix4_multiply_vector3(model_transform, &local);
    out->v[12] = world.x; out->v[13] = world.y; out->v[14] = world.z;
    return true;
}

static mdxKeyFrame_t *R_KeyFrameAt(mdxKeyTrack_t const *track, uint32_t stride, uint32_t index) {
    return (mdxKeyFrame_t *)((string_t)track->values + stride * index);
}

/* MDX key times are authored in ascending order; binary bounds avoid rescanning every track for every model instance. */
static uint32_t R_KeyFrameBound(mdxKeyTrack_t const *track, uint32_t stride, uint32_t time, bool upper) {
    uint32_t lo = 0, hi = track->keyframeCount;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t keytime = R_KeyFrameAt(track, stride, mid)->time;
        if (keytime < time || (upper && keytime == time)) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

void MDLX_GetModelKeytrackValue(mdxModel_t const *model, mdxKeyTrack_t const *keytrack, uint32_t time, handle_t output) {
    uint32_t interval[2] = { 0, 0 };
    uint32_t stride;

    if (!model || !keytrack || !output || !keytrack->keyframeCount)
        return;
    stride = GetModelKeyFrameSize(keytrack->datatype, keytrack->linetype);
    if (keytrack->globalSeqId != (uint32_t)-1) {
        mdxKeyFrame_t *first_authored;

        if (keytrack->globalSeqId >= (uint32_t)model->num_globalSequences || !model->globalSequences)
            return;
        interval[0] = 0;
        interval[1] = model->globalSequences[keytrack->globalSeqId].value;

        /* Warsmash treats a global sequence whose first authored key lies
         * beyond the declared duration as a constant equal to that first key.
         * Stock DNC models use this odd contract for their light-node rotation:
         * a zero-duration global sequence points at a later quaternion key. */
        first_authored = R_KeyFrameAt(keytrack, stride, 0);
        if (first_authored->time >= 0 && (uint32_t)first_authored->time > interval[1]) {
            memcpy(output, first_authored->data, GetModelKeyTrackDataTypeSize(keytrack->datatype));
            return;
        }

        /* Global sequences follow the render clock instead of entity->frame,
           which loops within the selected sequence. This preserves their full
           range while keeping fixed-time renderer captures deterministic. */
        {
            uint32_t gs_len = interval[1] + 1;
            uint32_t global_time = tr.viewDef.time ? tr.viewDef.time : SDL_GetTicks();
            time = gs_len > 0 ? (global_time % gs_len) : 0;
        }
    } else {
        mdxSequence_t const *seq = R_FindSequenceAtTime(model, time);
        if (!seq)
            return;
        interval[0] = seq->interval[0];
        interval[1] = seq->interval[1];
    }
    uint32_t first_index = R_KeyFrameBound(keytrack, stride, interval[0], false);
    uint32_t end_index = R_KeyFrameBound(keytrack, stride, interval[1], true);
    if (first_index >= end_index)
        return;
    mdxKeyFrame_t *first = R_KeyFrameAt(keytrack, stride, first_index);
    mdxKeyFrame_t *last = R_KeyFrameAt(keytrack, stride, end_index - 1);
    if (time >= last->time) {
        /* The interval tail blends back to its first key; keys outside this sequence never participate. */
        uint32_t span = (interval[1] - last->time) + (first->time - interval[0]);
        if (first != last && span && time < interval[1]) {
            float t = (float)(time - last->time) / (float)span;
            R_EvalKeyframeValue(last->data, first->data, t, keytrack->datatype, keytrack->linetype, output);
        } else {
            memcpy(output, last->data, GetModelKeyTrackDataTypeSize(keytrack->datatype));
        }
        return;
    }
    uint32_t right_index = R_KeyFrameBound(keytrack, stride, time, false);
    mdxKeyFrame_t *right = R_KeyFrameAt(keytrack, stride, right_index);
    if (right_index == first_index || right->time == time)
        memcpy(output, right->data, GetModelKeyTrackDataTypeSize(keytrack->datatype));
    else
        R_GetKeyframeValue(R_KeyFrameAt(keytrack, stride, right_index - 1), right, keytrack, time, output);
}

/* Warcraft animated MDX color tracks use BGR component semantics in the file.
 * Convert the float vector to renderer RGB after interpolation. This is not a
 * texture byte-order conversion, so it is identical on GL, GLES, and hosts with
 * or without native BGRA texture-upload support. */
void MDLX_GetAnimatedColorTrackValue(mdxModel_t const *model,
                                     mdxKeyTrack_t const *keytrack,
                                     uint32_t time,
                                     vec3_t *output)
{
    float red;

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
                                        vec3_t *output)
{
    if (!geosetAnim || !output) return;
    output->x = geosetAnim->staticColor.z;
    output->y = geosetAnim->staticColor.y;
    output->z = geosetAnim->staticColor.x;
}

static void R_CalculateNodeMatrix(mdxModel_t const *model, mdxNode_t *node, uint32_t frame1, uint32_t frame0, mat4_t *matrix) {
    vec3_t vTranslation = { 0, 0, 0 };
    quaternion_t vRotation = { 0, 0, 0, 1 };
    vec3_t vScale = { 1, 1, 1 };
    vec3_t zero_pivot = { 0, 0, 0 };
    vec3_t const *pivot = &zero_pivot;
    if (node->node_id < (uint32_t)model->num_pivots) {
        pivot = (vec3_t const *)&model->pivots[node->node_id];
    }
    if (frame0 != frame1) {
        if (node->translation) {
            vec3_t t0 = vTranslation, t1 = vTranslation;
            MDLX_GetModelKeytrackValue(model, node->translation, frame0, &t0);
            MDLX_GetModelKeytrackValue(model, node->translation, frame1, &t1);
            vTranslation = Vector3_lerp(&t0, &t1, tr.viewDef.lerpfrac);
        }
        if (node->rotation) {
            quaternion_t r0 = vRotation, r1 = vRotation;
            MDLX_GetModelKeytrackValue(model, node->rotation, frame0, &r0);
            MDLX_GetModelKeytrackValue(model, node->rotation, frame1, &r1);
            vRotation = Quaternion_slerp(&r0, &r1, tr.viewDef.lerpfrac);
        }
        if (node->scale) {
            vec3_t s0 = vScale, s1 = vScale;
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

mat4_t const *R_GetNodeGlobalMatrix(mdxModel_t const *model, mat4_t const *model_matrix, mdxNode_t const *node) {
    if (!node || node->node_id >= MDX_MAX_NODES) {
        return NULL;
    }
    mat4_t *global_matrix = node_matrices + node->node_id;
    mat4_t *local_matrix = local_matrices+node->node_id;
    if (global_matrix->v[15] == 0) {
        if (node->parent_id != -1 && node->parent_id < MDX_MAX_NODES && model->nodes[node->parent_id]) {
            mat4_t const *parent_matrix = R_GetNodeGlobalMatrix(model, model_matrix, model->nodes[node->parent_id]);
            if (!parent_matrix) {
                return NULL;
            }
            Matrix4_multiply(parent_matrix, local_matrix, global_matrix);
        } else {
            *global_matrix = *local_matrix;
        }
        if (node->flags & MDLXNODE_Billboarded) {
            mat4_t tmp1, tmp2;
            vec3_t pivot = { 0, 0, 0 };
            if (node->node_id < (uint32_t)model->num_pivots) {
                pivot = *(vec3_t const *)(&model->pivots[node->node_id]);
            }
            vec3_t tmppvt = Matrix4_multiply_vector3(global_matrix, &pivot);
            if (node->parent_id != -1 && node->parent_id < MDX_MAX_NODES && model->nodes[node->parent_id]) {
                mat4_t const *parent_matrix = R_GetNodeGlobalMatrix(model, model_matrix, model->nodes[node->parent_id]);
                if (!parent_matrix) {
                    return NULL;
                }
                quaternion_t tmprot = Quaternion_fromMatrix(parent_matrix);
                tmprot = Quaternion_unm(&tmprot);
                Matrix4_from_rotation_origin(&tmp1, &tmprot, &tmppvt);
                Matrix4_multiply(&tmp1, global_matrix, &tmp2);
                *global_matrix = tmp2;
            }
            
            Matrix4_identity(&tmp1);
            Matrix4_rotate(&tmp1, &(vec3_t){30,0,90}, ROTATE_XYZ);
            Matrix4_multiply(&tmp1, model_matrix, &tmp2);

            quaternion_t viewrot = Quaternion_fromMatrix(&tmp2);
            viewrot = Quaternion_unm(&viewrot);
            Matrix4_from_rotation_origin(&tmp1, &viewrot, &tmppvt);
            Matrix4_multiply(&tmp1, global_matrix, &tmp2);
            *global_matrix = tmp2;
        }
    }
    return global_matrix;
}

void AddSkin(vec3_t *pos, mat4_t const *mat, vec3_t const *org, float weight) {
    if (weight == 0) return;
    vec3_t val = Matrix4_multiply_vector3(mat, org);
    val = Vector3_scale(&val, weight);
    *pos = Vector3_add(pos, &val);
}

void MDLX_BindBoneMatrices(mdxModel_t const *model, mat4_t const *model_matrix, uint32_t frame1, uint32_t frame0) {
    /* Only the nodes this model actually has need their global matrices
     * recomputed.  The old path memset the full 64KB node_matrices array and
     * scanned all MDX_MAX_NODES slots twice; models have tens of nodes, so the
     * compact node_list built at load time is orders of magnitude smaller. */
    FOR_LOOP(i, model->num_nodes) {
        mdxNode_t *node = model->node_list[i];
        memset(&node_matrices[node->node_id], 0, sizeof(mat4_t)); /* reset the "computed" flag (v[15]==0) */
        R_CalculateNodeMatrix(model, node, frame1, frame0, &local_matrices[node->node_id]);
    }
    FOR_LOOP(i, model->num_nodes)
        R_GetNodeGlobalMatrix(model, model_matrix, model->node_list[i]);
}

/* Resolve authored attachment pivots from the same interpolated node pose used
 * for geometry. Callers can filter by a name prefix (for example "Sprite ")
 * without depending on list order in the MDX file. */
uint32_t MDLX_CollectAttachmentPositions(mdxModel_t const *model, mat4_t const *model_matrix,
                                      uint32_t frame, uint32_t oldframe, cstring_t prefix,
                                      mdxAttachmentPosition_t *positions, uint32_t max_positions) {
    uint32_t count = 0;
    size_t const prefix_len = prefix ? strlen(prefix) : 0;

    if (!model || !model_matrix || !positions || !max_positions) {
        return 0;
    }

    MDLX_BindBoneMatrices(model, model_matrix, frame, oldframe);
    FOR_EACH_LIST(mdxAttachment_t, attachment, model->attachments) {
        mdxNode_t const *node = &attachment->node;
        vec3_t pivot = { 0, 0, 0 };
        vec3_t local;
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
        if (node->node_id < (uint32_t)model->num_pivots) {
            pivot = model->pivots[node->node_id];
        }
        local = pivot;
        if (node->node_id < MDX_MAX_NODES && model->nodes[node->node_id]) {
            local = Matrix4_multiply_vector3(&node_matrices[node->node_id], &pivot);
        }
        positions[count].name = node->name;
        positions[count].path = attachment->path;
        positions[count].origin = Matrix4_multiply_vector3(model_matrix, &local);
        Matrix4_multiply(model_matrix, &node_matrices[node->node_id], &positions[count].transform);
        count++;
        if (count == max_positions) {
            break;
        }
    }
    return count;
}
