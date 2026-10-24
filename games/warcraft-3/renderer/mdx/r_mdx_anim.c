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

/* node_matrices is a single scratch pose, shared by all MDX models. Its
 * generation changes on every bind, so child rendering cannot make an older
 * parent pose look reusable. This is a one-draw hint, not a frame cache. */
static struct {
    uint64_t generation;
    mdxModel_t const *model;
    mat4_t transform;
    uint32_t frame, oldframe;
} bound_bone_pose;

uint64_t MDLX_BoundBonePoseGeneration(void) {
    return bound_bone_pose.generation;
}

#ifdef BZ_TESTS
static uint32_t test_mdx_sequence_searches;
void MDLX_TestResetSequenceSearches(void) { test_mdx_sequence_searches = 0; }
uint32_t MDLX_TestSequenceSearches(void) { return test_mdx_sequence_searches; }
#endif

mdxSequence_t const *R_FindSequenceAtTime(mdxModel_t const *model, uint32_t time) {
#ifdef BZ_TESTS
    test_mdx_sequence_searches++;
#endif
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

typedef struct {
    uint32_t generation;
    uint32_t last_time;
    uint32_t pause_start;
    uint32_t paused_total;
    bool valid;
    bool paused;
} mdxGlobalSequencePauseState_t;

static mdxGlobalSequencePauseState_t global_sequence_pause[MAX_GAME_ENTITIES];
static bool entity_global_time_valid;
static uint32_t entity_global_time;

/* Per-view, bounded cross-pass pose snapshot. The normal renderer has one
 * global scratch pose: saving only the active node IDs lets an alpha draw
 * restore its solid-pass result after arbitrary intervening models. No heap,
 * no cross-frame/model-resource references, and no change to key evaluation. */
#define MDLX_VIEW_POSE_ENTRIES 96
#define MDLX_VIEW_POSE_NODES 4096

typedef struct {
    renderEntity_t const *entity_ptr;
    renderEntity_t entity_snapshot;
    mdxModel_t const *model;
    mat4_t transform;
    mat4_t view_projection;
    uint32_t view_time, frame, oldframe, global_time;
    float lerpfrac;
    uint16_t start, count;
} mdxViewPoseEntry_t;

typedef struct {
    uint16_t node_id;
    mat4_t matrix;
} mdxViewPoseNode_t;

/* Index by scene-array position, NOT by entity->number (which can be shared
 * by synthetic/instanced draw entities). A stamp avoids clearing 16,000 map
 * slots on every view; a wrapped stamp is the only full index reset. The
 * uint8_t slot stores entry index + 1 (96 maximum, zero means missing). */
static struct {
    bool active;
    uint16_t epoch;
    uint16_t scene_epoch[MAX_GAME_ENTITIES];
    uint8_t scene_entry[MAX_GAME_ENTITIES];
    uint32_t count, nodes;
    mdxViewPoseEntry_t entries[MDLX_VIEW_POSE_ENTRIES];
    mdxViewPoseNode_t snapshots[MDLX_VIEW_POSE_NODES];
} view_pose_cache;

void MDLX_ViewPoseBegin(void) {
    if (++view_pose_cache.epoch == 0) {
        memset(view_pose_cache.scene_epoch, 0, sizeof(view_pose_cache.scene_epoch));
        view_pose_cache.epoch = 1;
    }
    view_pose_cache.count = view_pose_cache.nodes = 0;
    view_pose_cache.active = true;
}

void MDLX_ViewPoseEnd(void) {
    view_pose_cache.active = false;
    view_pose_cache.count = view_pose_cache.nodes = 0;
}

/* Never admit temporary child/sprite entities. Use pointer subtraction only
 * after checking the scene-array range: a draw-local copy is not a cache key.
 * The explicit cap keeps out-of-range views on the uncached render path. */
static uint32_t MDLX_ViewPoseSceneIndex(renderEntity_t const *entity) {
    uintptr_t base, addr, distance;
    uintptr_t index;
    if (!entity || !tr.viewDef.entities || !tr.viewDef.num_entities) return UINT32_MAX;
    base = (uintptr_t)tr.viewDef.entities;
    addr = (uintptr_t)entity;
    if (addr < base) return UINT32_MAX;
    distance = addr - base;
    if (distance % sizeof(*entity)) return UINT32_MAX;
    index = distance / sizeof(*entity);
    if (index >= tr.viewDef.num_entities || index >= MAX_GAME_ENTITIES) return UINT32_MAX;
    return (uint32_t)index;
}

#ifdef BZ_TESTS
static uint32_t test_mdx_pose_lookup_candidates;
void MDLX_TestResetPoseLookupCandidates(void) { test_mdx_pose_lookup_candidates = 0; }
uint32_t MDLX_TestPoseLookupCandidates(void) { return test_mdx_pose_lookup_candidates; }
#endif

static mdxViewPoseEntry_t const *MDLX_ViewPoseFind(renderEntity_t const *entity) {
    uint32_t index, slot;
    mdxViewPoseEntry_t const *entry;
    if (!view_pose_cache.active) return NULL;
    index = MDLX_ViewPoseSceneIndex(entity);
    if (index == UINT32_MAX || view_pose_cache.scene_epoch[index] != view_pose_cache.epoch)
        return NULL;
    slot = view_pose_cache.scene_entry[index];
    if (!slot || slot > view_pose_cache.count) return NULL;
    entry = &view_pose_cache.entries[slot - 1];
#ifdef BZ_TESTS
    test_mdx_pose_lookup_candidates++;
#endif
    if (entry->entity_ptr == entity && entry->view_time == tr.viewDef.time &&
        entry->lerpfrac == tr.viewDef.lerpfrac &&
        !memcmp(&entry->entity_snapshot, entity, sizeof(*entity)) &&
        !memcmp(&entry->view_projection, &tr.viewDef.viewProjectionMatrix,
                sizeof(entry->view_projection)))
        return entry;
    return NULL;
}

/* Called before alpha-pass transform construction. This includes the model
 * selected for solid rendering, but excludes terrain-fitted replacements:
 * those are map-owned mutable resources and must be re-resolved. */
bool MDLX_ViewPoseCachedDraw(renderEntity_t const *entity, mdxModel_t const **model,
                             mat4_t *transform) {
    mdxViewPoseEntry_t const *entry = MDLX_ViewPoseFind(entity);
    if (!entry || !entity->model || entry->model != entity->model->mdx ||
        !model || !transform) return false;
    *model = entry->model;
    *transform = entry->transform;
    return true;
}

void MDLX_ViewPoseStore(renderEntity_t const *entity, mdxModel_t const *model,
                        mat4_t const *transform, uint32_t frame, uint32_t oldframe) {
    mdxViewPoseEntry_t *entry;
    uint32_t scene_index;
    if (tr.render_phase == RENDER_PHASE_SOLID && !view_pose_cache.active)
        MDLX_ViewPoseBegin(); /* isolated portrait/entity-camera view */
    scene_index = MDLX_ViewPoseSceneIndex(entity);
    if (!view_pose_cache.active || tr.render_phase != RENDER_PHASE_SOLID ||
        scene_index == UINT32_MAX || !entity->model || !model || !transform ||
        model != entity->model->mdx || model->num_nodes < 0 ||
        model->num_nodes > MDX_MAX_NODES ||
        view_pose_cache.count >= MDLX_VIEW_POSE_ENTRIES ||
        view_pose_cache.nodes + (uint32_t)model->num_nodes > MDLX_VIEW_POSE_NODES)
        return;
    entry = &view_pose_cache.entries[view_pose_cache.count++];
    /* Latest store wins, matching the former reverse scan if the same scene
     * entity is drawn more than once during the solid pass. */
    view_pose_cache.scene_epoch[scene_index] = view_pose_cache.epoch;
    view_pose_cache.scene_entry[scene_index] = (uint8_t)view_pose_cache.count;
    *entry = (mdxViewPoseEntry_t){
        .entity_ptr = entity, .entity_snapshot = *entity,
        .model = model, .transform = *transform,
        .view_projection = tr.viewDef.viewProjectionMatrix,
        .view_time = tr.viewDef.time, .frame = frame, .oldframe = oldframe,
        .global_time = entity_global_time, .lerpfrac = tr.viewDef.lerpfrac,
        .start = (uint16_t)view_pose_cache.nodes,
        .count = (uint16_t)model->num_nodes,
    };
    FOR_LOOP(i, model->num_nodes) {
        uint32_t const id = model->node_list[i]->node_id;
        mdxViewPoseNode_t *snapshot = &view_pose_cache.snapshots[view_pose_cache.nodes++];
        snapshot->node_id = (uint16_t)id;
        snapshot->matrix = node_matrices[id];
    }
}

bool MDLX_ViewPoseRestore(renderEntity_t const *entity, mdxModel_t const *model,
                          mat4_t const *transform, uint32_t frame, uint32_t oldframe) {
    mdxViewPoseEntry_t const *entry = MDLX_ViewPoseFind(entity);
    if (tr.render_phase != RENDER_PHASE_ALPHA || !entry || entry->model != model ||
        entry->frame != frame || entry->oldframe != oldframe ||
        entry->global_time != entity_global_time ||
        memcmp(&entry->transform, transform, sizeof(*transform)) ||
        entry->count != (uint32_t)model->num_nodes)
        return false;
    FOR_LOOP(i, entry->count) {
        mdxViewPoseNode_t const *snapshot = &view_pose_cache.snapshots[entry->start + i];
        if (!model->node_list[i] || model->node_list[i]->node_id != snapshot->node_id)
            return false;
    }
    FOR_LOOP(i, entry->count) {
        mdxViewPoseNode_t const *snapshot = &view_pose_cache.snapshots[entry->start + i];
        node_matrices[snapshot->node_id] = snapshot->matrix;
    }
    /* Other callers rely on a unique generation for the currently bound
     * global scratch pose, including attachments after a child bind. */
    bound_bone_pose.generation++;
    if (!bound_bone_pose.generation) bound_bone_pose.generation++;
    bound_bone_pose.model = model;
    bound_bone_pose.transform = *transform;
    bound_bone_pose.frame = frame;
    bound_bone_pose.oldframe = oldframe;
    return true;
}

/* MDX global sequences usually follow view time, but held construction must
 * keep per-entity global tracks (for example a Farm's globally animated saw)
 * at their current phase. Subtract accumulated paused time on resume so the
 * track continues from that phase instead of jumping forward. */
void MDLX_UpdateEntityAnimationPause(renderEntity_t const *entity) {
    uint32_t now = tr.viewDef.time ? tr.viewDef.time : SDL_GetTicks();
    if (entity && entity->number < MAX_GAME_ENTITIES) {
        mdxGlobalSequencePauseState_t *state = &global_sequence_pause[entity->number];
        bool const paused = (entity->flags & RF_MDX_ANIMATION_PAUSED) != 0;
        if (!state->valid || state->generation != entity->generation) {
            *state = (mdxGlobalSequencePauseState_t){ .generation = entity->generation,
                .last_time = now, .pause_start = now, .valid = true, .paused = paused };
        } else {
            if (now < state->last_time) {
                state->paused_total = 0;
                state->pause_start = now;
            }
            state->last_time = now;
            if (paused && !state->paused) {
                state->pause_start = now;
                state->paused = true;
            } else if (!paused && state->paused) {
                state->paused_total += now - state->pause_start;
                state->paused = false;
            }
        }
    }
}

void MDLX_PushEntityAnimationTime(renderEntity_t const *entity, mdxAnimationTimeScope_t *scope) {
    uint32_t now = tr.viewDef.time ? tr.viewDef.time : SDL_GetTicks();
    uint32_t effective = now;
    if (!scope) return;
    scope->previous_valid = entity_global_time_valid;
    scope->previous_time = entity_global_time;
    MDLX_UpdateEntityAnimationPause(entity);
    if (entity && entity->number < MAX_GAME_ENTITIES) {
        mdxGlobalSequencePauseState_t const *state = &global_sequence_pause[entity->number];
        effective = state->paused ? state->pause_start - state->paused_total : now - state->paused_total;
    }
    entity_global_time = effective;
    entity_global_time_valid = true;
}

void MDLX_PopEntityAnimationTime(mdxAnimationTimeScope_t const *scope) {
    if (!scope) return;
    entity_global_time = scope->previous_time;
    entity_global_time_valid = scope->previous_valid;
}

/* MDX key times are authored in ascending order. Restrict binary searches
 * to the current sequence's key span instead of searching the whole track. */
static uint32_t R_KeyFrameBound(mdxKeyTrack_t const *track, uint32_t stride, uint32_t time,
                                bool upper, uint32_t lo, uint32_t hi) {
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int32_t keytime = R_KeyFrameAt(track, stride, mid)->time;
        if (keytime < (int32_t)time || (upper && keytime == (int32_t)time)) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

/* Callers computing several tracks of the same pose already know which
 * authored sequence contains each sampled time. Keep the range and global
 * sequence logic here, but avoid repeating the sequence list walk for every
 * translation/rotation/scale track and for every bone. */
static void MDLX_GetModelKeytrackValueResolved(mdxModel_t const *model, mdxKeyTrack_t const *keytrack,
                                                uint32_t time, mdxSequence_t const *sequence, handle_t output) {
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
            uint32_t global_time = entity_global_time_valid ? entity_global_time :
                                   (tr.viewDef.time ? tr.viewDef.time : SDL_GetTicks());
            time = gs_len > 0 ? (global_time % gs_len) : 0;
        }
    } else {
        if (!sequence) return;
        interval[0] = sequence->interval[0];
        interval[1] = sequence->interval[1];
    }
    /* Constant tracks are common (bind poses and visibility). Their sole key
     * still belongs to a specific sequence; do not return it in a different
     * interval or skip the global-sequence validation above. */
    if (keytrack->keyframeCount == 1) {
        mdxKeyFrame_t const *key = R_KeyFrameAt(keytrack, stride, 0);
        int32_t const key_time = key->time;
        if (key_time >= (int32_t)interval[0] && key_time <= (int32_t)interval[1])
            memcpy(output, key->data, GetModelKeyTrackDataTypeSize(keytrack->datatype));
        return;
    }

    /* The first/last authored keys frequently bracket an entire animation
     * interval. In that case no sequence-bound search is needed at all. MDX
     * key times are signed; some assets use negative lead-in keys. */
    uint32_t const count = keytrack->keyframeCount;
    mdxKeyFrame_t const *track_first = R_KeyFrameAt(keytrack, stride, 0);
    mdxKeyFrame_t const *track_last = R_KeyFrameAt(keytrack, stride, count - 1);
    uint32_t const first_index = track_first->time >= (int32_t)interval[0] ? 0 :
        R_KeyFrameBound(keytrack, stride, interval[0], false, 0, count);
    uint32_t const end_index = track_last->time <= (int32_t)interval[1] ? count :
        R_KeyFrameBound(keytrack, stride, interval[1], true, first_index, count);
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
    /* A time before the first in-sequence key clamps to that key. With two
     * keys the right bracket is known without searching. Larger tracks search
     * only the sequence's occupied subrange. */
    if ((int32_t)time <= first->time) {
        memcpy(output, first->data, GetModelKeyTrackDataTypeSize(keytrack->datatype));
        return;
    }
    uint32_t const right_index = end_index - first_index == 2 ? first_index + 1 :
        R_KeyFrameBound(keytrack, stride, time, false, first_index + 1, end_index);
    mdxKeyFrame_t *right = R_KeyFrameAt(keytrack, stride, right_index);
    if (right->time == time)
        memcpy(output, right->data, GetModelKeyTrackDataTypeSize(keytrack->datatype));
    else
        R_GetKeyframeValue(R_KeyFrameAt(keytrack, stride, right_index - 1), right, keytrack, time, output);
}

/* General consumers remain stateless and reselect their sequence every call.
 * The pose-only shortcut below never changes this public API's semantics. */
void MDLX_GetModelKeytrackValue(mdxModel_t const *model, mdxKeyTrack_t const *keytrack,
                                uint32_t time, handle_t output) {
    mdxSequence_t const *sequence = NULL;
    if (!model || !keytrack || !output || !keytrack->keyframeCount) return;
    if (keytrack->globalSeqId == UINT32_MAX) sequence = R_FindSequenceAtTime(model, time);
    MDLX_GetModelKeytrackValueResolved(model, keytrack, time, sequence, output);
}

/* This cache is a stack local owned by one MDLX_BindBoneMatrices invocation.
 * It holds exactly the two sampled sequence identities for that pose, never
 * storing key pointers or data across binds, reloads or frame transitions.
 * Resolve lazily: a model with only global-sequence tracks needs no search. */
typedef struct {
    mdxSequence_t const *sequence[2];
    bool resolved[2];
} mdxPoseSequencePair_t;

static void MDLX_GetPoseKeytrackValue(mdxModel_t const *model, mdxKeyTrack_t const *track,
                                      uint32_t time, uint32_t sample, mdxPoseSequencePair_t *pair,
                                      handle_t output) {
    mdxSequence_t const *sequence = NULL;
    if (!track || !track->keyframeCount) return;
    if (track->globalSeqId == UINT32_MAX) {
        if (!pair->resolved[sample]) {
            pair->sequence[sample] = R_FindSequenceAtTime(model, time);
            pair->resolved[sample] = true;
        }
        sequence = pair->sequence[sample];
    }
    MDLX_GetModelKeytrackValueResolved(model, track, time, sequence, output);
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

/* Static GEOA colors are already RGB. Warsmash swaps them into its BGR shader input, then swaps back in
 * MdxShaders; copying only its first swap here turned the Orc campaign's orange sky blue. */
void MDLX_GetGeosetAnimationStaticColor(mdxGeosetAnim_t const *geosetAnim, vec3_t *output) {
    if (!geosetAnim || !output) return;
    *output = geosetAnim->staticColor;
}

static void R_CalculateNodeMatrix(mdxModel_t const *model, mdxNode_t *node, uint32_t frame1,
                                   uint32_t frame0, mdxPoseSequencePair_t *sequences, mat4_t *matrix) {
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
            MDLX_GetPoseKeytrackValue(model, node->translation, frame0, 0, sequences, &t0);
            MDLX_GetPoseKeytrackValue(model, node->translation, frame1, 1, sequences, &t1);
            vTranslation = Vector3_lerp(&t0, &t1, tr.viewDef.lerpfrac);
        }
        if (node->rotation) {
            quaternion_t r0 = vRotation, r1 = vRotation;
            MDLX_GetPoseKeytrackValue(model, node->rotation, frame0, 0, sequences, &r0);
            MDLX_GetPoseKeytrackValue(model, node->rotation, frame1, 1, sequences, &r1);
            vRotation = Quaternion_slerp(&r0, &r1, tr.viewDef.lerpfrac);
        }
        if (node->scale) {
            vec3_t s0 = vScale, s1 = vScale;
            MDLX_GetPoseKeytrackValue(model, node->scale, frame0, 0, sequences, &s0);
            MDLX_GetPoseKeytrackValue(model, node->scale, frame1, 1, sequences, &s1);
            vScale = Vector3_lerp(&s0, &s1, tr.viewDef.lerpfrac);
        }
    } else {
        if (node->translation) {
            MDLX_GetPoseKeytrackValue(model, node->translation, frame1, 1, sequences, &vTranslation);
        }
        if (node->rotation) {
            MDLX_GetPoseKeytrackValue(model, node->rotation, frame1, 1, sequences, &vRotation);
        }
        if (node->scale) {
            MDLX_GetPoseKeytrackValue(model, node->scale, frame1, 1, sequences, &vScale);
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
            /* An unanimated node has the identity local TRS, so it inherits
             * its parent's already-computed transform directly. Animated
             * TRS and all billboard matrices are affine; avoid the general
             * homogeneous 4x4 multiply on this hot bone-hierarchy path. */
            if (!node->translation && !node->rotation && !node->scale)
                *global_matrix = *parent_matrix;
            else
                Matrix4_multiply_affine(parent_matrix, local_matrix, global_matrix);
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
                /* Parent animation may include breathing scale. Extract rotation from unit axes;
                 * scaled columns skewed billboard cancellation even with the correct camera. */
                mat4_t rotation = *parent_matrix;
                FOR_LOOP(i, 3) Vector3_normalize((vec3_t *)rotation.m[i]);
                quaternion_t tmprot = Quaternion_fromMatrix(&rotation);
                tmprot = Quaternion_unm(&tmprot);
                Matrix4_from_rotation_origin(&tmp1, &tmprot, &tmppvt);
                Matrix4_multiply_affine(&tmp1, global_matrix, &tmp2);
                *global_matrix = tmp2;
            }
            
            /* Projection rows retain camera right/up even for authored cameras and camera roll.
             * MDX billboard quads lie in YZ, with their normal along +X. */
            mat4_t const *vp = &tr.viewDef.viewProjectionMatrix;
            vec3_t right = {vp->v[0], vp->v[4], vp->v[8]};
            vec3_t up = {vp->v[1], vp->v[5], vp->v[9]};
            Vector3_normalize(&right);
            Vector3_normalize(&up);
            vec3_t front = Vector3_cross(&right, &up);
            Matrix4_identity(&tmp1);
            FOR_LOOP(i, 3) {
                tmp1.m[i][0] = ((float *)&front)[i];
                tmp1.m[i][1] = ((float *)&right)[i];
                tmp1.m[i][2] = ((float *)&up)[i];
            }
            Matrix4_multiply(&tmp1, model_matrix, &tmp2);
            FOR_LOOP(i, 3) Vector3_normalize((vec3_t *)tmp2.m[i]);

            quaternion_t viewrot = Quaternion_fromMatrix(&tmp2);
            viewrot = Quaternion_unm(&viewrot);
            Matrix4_from_rotation_origin(&tmp1, &viewrot, &tmppvt);
            Matrix4_multiply_affine(&tmp1, global_matrix, &tmp2);
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
    mdxPoseSequencePair_t sequences = { 0 };
    /* Only the nodes this model actually has need their global matrices
     * recomputed.  The old path memset the full 64KB node_matrices array and
     * scanned all MDX_MAX_NODES slots twice; models have tens of nodes, so the
     * compact node_list built at load time is orders of magnitude smaller. */
    FOR_LOOP(i, model->num_nodes) {
        mdxNode_t *node = model->node_list[i];
        memset(&node_matrices[node->node_id], 0, sizeof(mat4_t)); /* reset the "computed" flag (v[15]==0) */
        R_CalculateNodeMatrix(model, node, frame1, frame0, &sequences, &local_matrices[node->node_id]);
    }
    FOR_LOOP(i, model->num_nodes)
        R_GetNodeGlobalMatrix(model, model_matrix, model->node_list[i]);
    bound_bone_pose.generation++;
    /* Generation zero is reserved for an untrusted caller. */
    if (!bound_bone_pose.generation) bound_bone_pose.generation++;
    bound_bone_pose.model = model;
    bound_bone_pose.transform = *model_matrix;
    bound_bone_pose.frame = frame1;
    bound_bone_pose.oldframe = frame0;
}

/* Resolve authored attachment pivots from the same interpolated node pose used
 * for geometry. Callers can filter by a name prefix (for example "Sprite ")
 * without depending on list order in the MDX file. */
uint32_t MDLX_CollectAttachmentPositionsWithPose(mdxModel_t const *model, mat4_t const *model_matrix,
                                      uint32_t frame, uint32_t oldframe, cstring_t prefix,
                                      mdxAttachmentPosition_t *positions, uint32_t max_positions,
                                      uint64_t pose_generation) {
    uint32_t count = 0;
    size_t const prefix_len = prefix ? strlen(prefix) : 0;

    if (!model || !model_matrix || !positions || !max_positions || !model->attachments) {
        return 0;
    }

    /* Skip the bind when no attachment can match the requested prefix. In
     * particular, attachmentless units should never recalculate their bones. */
    if (prefix_len) {
        bool matching = false;
        FOR_EACH_LIST(mdxAttachment_t, candidate, model->attachments) {
            if (!strncasecmp(candidate->node.name, prefix, prefix_len)) {
                matching = true;
                break;
            }
        }
        if (!matching) return 0;
    }

    /* Only the caller holding the generation obtained from its own model
     * rendering may reuse the shared matrices. Another model's bind changes
     * that generation, including when drawing a nested attachment. */
    if (!pose_generation || bound_bone_pose.generation != pose_generation ||
        bound_bone_pose.model != model || bound_bone_pose.frame != frame ||
        bound_bone_pose.oldframe != oldframe ||
        memcmp(&bound_bone_pose.transform, model_matrix, sizeof(*model_matrix))) {
        MDLX_BindBoneMatrices(model, model_matrix, frame, oldframe);
    }
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

/* Most Warcraft models contain only point references (Origin, Overhead,
 * Sprite, Weapon, etc.) without a child-model path. The regular collection
 * API must still expose them to overhead and building-effect consumers, but
 * the child-model renderer will discard all such entries. Avoid rebuilding
 * an entire pose after a culled draw for those no-op attachment lists.
 *
 * Do not filter empty paths out of a mixed list: the original collector's
 * encounter order, visibility tests and max_positions cap must remain intact. */
uint32_t MDLX_CollectRenderAttachmentPositionsWithPose(mdxModel_t const *model,
                                      mat4_t const *model_matrix, uint32_t frame,
                                      uint32_t oldframe, mdxAttachmentPosition_t *positions,
                                      uint32_t max_positions, uint64_t pose_generation) {
    if (!model || !model_matrix || !positions || !max_positions) return 0;
    FOR_EACH_LIST(mdxAttachment_t, attachment, model->attachments) {
        if (attachment->path[0]) {
            return MDLX_CollectAttachmentPositionsWithPose(model, model_matrix,
                frame, oldframe, NULL, positions, max_positions, pose_generation);
        }
    }
    return 0;
}

uint32_t MDLX_CollectAttachmentPositions(mdxModel_t const *model, mat4_t const *model_matrix,
                                      uint32_t frame, uint32_t oldframe, cstring_t prefix,
                                      mdxAttachmentPosition_t *positions, uint32_t max_positions) {
    return MDLX_CollectAttachmentPositionsWithPose(model, model_matrix, frame, oldframe,
                                                     prefix, positions, max_positions, 0);
}
