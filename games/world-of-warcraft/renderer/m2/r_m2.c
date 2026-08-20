#include "renderer/r_local.h"
#include "renderer/r_emit.h"
#include "r_dbc.h"
#include "r_m2_utils.h"
#include "../wow/r_wowmap.h"
#include <stdlib.h>
#include <strings.h>

#define M2_MAX_BONES_PER_BATCH 128
#define M2_MAX_BONES 1024
#define M2_CHARACTER_TEXTURE_NONE 0xff

typedef struct m2KnownTexture_s {
    PATHSTR path;
    BOOL exists;
    struct m2KnownTexture_s *next;
} m2KnownTexture_t;

static m2KnownTexture_t *m2_known_textures;

typedef struct m2ModelBatch_s {
	LPBUFFER buffer;
	LPTEXTURE texture;
	DWORD num_vertices;
	DWORD texture_type;
	WORD bone_count;
	WORD bone_combo_index;
	WORD section_id;
	WORD geoset_index;
	BYTE alphamode;
	BYTE character_texture_slot;
	struct m2ModelBatch_s *next;
} m2ModelBatch_t;

struct m2Model_s {
    m2File_t *file;
    BYTE *file_image;
    DWORD file_image_size;
    DWORD base_offset;
    DWORD file_size;
    m2FormatDef_t const *format;
    PATHSTR filename;
    BOX3 bounds;
    BOX3 geometry_bounds;
    DWORD flags;
    /* Renderer-owned state that has no representation in the M2 file. */
    m2ModelBatch_t *batches;
    DWORD num_batches;
};

#define M2_MODEL_CHARACTER 0x00000001u

typedef struct {
    m2Array_t vertices;
    m2Array_t textures;
    m2Array_t texture_lookup_table;
    m2Box_t bounding_box;
} m2GeometryInfo_t;

static LPSHADER m2_shader;
static MATRIX4 m2_bone_matrices[M2_MAX_BONES];

static LPSHADER M2_Shader(void) {
    if (!m2_shader) {
        m2_shader = R_ModelShader();
    }
    return m2_shader ? m2_shader : tr.shader[SHADER_DEFAULT];
}

static void M2_LogFallback(LPCSTR modelFilename, LPCSTR reason) {
    static DWORD fallback_count;

    fallback_count++;
    if (fallback_count <= 64 || (fallback_count % 100) == 0) {
        fprintf(stderr, "M2 fallback: %s model=%s count=%u\n", reason ? reason : "unknown", modelFilename ? modelFilename : "<null>", fallback_count);
    }
}

static m2Model_t *M2_CreateFallbackModel(LPCSTR modelFilename, LPCSTR reason) {
    static VERTEX vertices[12];
    static BOOL initialized;
    m2Model_t *model;
    m2ModelBatch_t *batch;
    COLOR32 color = { 90, 230, 130, 255 };

    M2_LogFallback(modelFilename, reason);

    if (!initialized) {
        VECTOR3 base[4] = {
            { -14.0f, -14.0f, 0.0f },
            {  14.0f, -14.0f, 0.0f },
            {  14.0f,  14.0f, 0.0f },
            { -14.0f,  14.0f, 0.0f },
        };
        VECTOR3 top = { 0.0f, 0.0f, 42.0f };
        int tri[12] = { 0, 1, 2, 0, 2, 3, 0, 1, 4, 2, 3, 4 };
        VECTOR3 points[5];
        memcpy(points, base, sizeof(base));
        points[4] = top;
        FOR_LOOP(i, 12) {
            memset(&vertices[i], 0, sizeof(vertices[i]));
            vertices[i].position = points[tri[i]];
            vertices[i].normal = (VECTOR3){ 0.0f, 0.0f, 1.0f };
            vertices[i].texcoord = (VECTOR2){ 0.0f, 0.0f };
            vertices[i].color = color;
        }
        initialized = true;
    }

    model = ri.MemAlloc(sizeof(*model));
    memset(model, 0, sizeof(*model));
    model->bounds = (BOX3){
        .min = { -14.0f, -14.0f, 0.0f },
        .max = { 14.0f, 14.0f, 42.0f },
    };
    model->geometry_bounds = model->bounds;
    batch = ri.MemAlloc(sizeof(*batch));
    memset(batch, 0, sizeof(*batch));
    batch->buffer = R_MakeVertexArrayObject(vertices, 12);
    batch->texture = tr.texture[TEX_WHITE];
    batch->num_vertices = 12;
    model->batches = batch;
    model->num_batches = 1;
    return model;
}

/* Modified character atlases are rendered into this shared scratch target. */
#define M2_CHARACTER_COMPOSITE_RESOLUTION 256
static LPRENDERTARGET m2_character_composite_target;
static TEXTURE m2_character_composite_texture;
static BYTE M2_CharacterTextureSlotForSection(WORD section_id) {
    switch (section_id) {
        case 401:
        case 402:
        case 403:
        case 404:
            return M2_CHAR_TEX_HAND;
        case 501:
        case 502:
        case 503:
        case 504:
            return M2_CHAR_TEX_FOOT;
        case 802:
        case 803:
            return M2_CHAR_TEX_LOWER_ARM;
        case 902:
        case 903:
            return M2_CHAR_TEX_LOWER_LEG;
        case 1002:
            return M2_CHAR_TEX_UPPER_TORSO;
        case 1102:
        case 1202:
            return M2_CHAR_TEX_LOWER_TORSO;
        case 1302:
            return M2_CHAR_TEX_UPPER_LEG;
        default:
            return M2_CHARACTER_TEXTURE_NONE;
    }
}

static BOOL M2_TextureExists(LPCSTR path) {
    m2KnownTexture_t *known;
    LPBYTE data = NULL;
    int size;

    if (!path || !*path) {
        return false;
    }
    for (known = m2_known_textures; known; known = known->next)
        if (!strcasecmp(known->path, path)) return known->exists;
    size = ri.FS_ReadFile(path, (void **)&data);
    known = ri.MemAlloc(sizeof(*known));
    memset(known, 0, sizeof(*known));
    snprintf(known->path, sizeof(known->path), "%s", path);
    known->exists = size > 0 && data != NULL;
    known->next = m2_known_textures;
    m2_known_textures = known;
    SAFE_DELETE(data, ri.FS_FreeFile);
    return known->exists;
}

static BOOL M2_CharacterComponentTexturePath(LPCSTR stem,
                                             BYTE slot,
                                             LPCSTR model_path,
                                             LPSTR out,
                                             DWORD out_size) {
    static LPCSTR const folders[M2_CHAR_TEX_COMPONENT_COUNT] = {
        "ArmUpperTexture",
        "ArmLowerTexture",
        "HandTexture",
        "TorsoUpperTexture",
        "TorsoLowerTexture",
        "LegUpperTexture",
        "LegLowerTexture",
        "FootTexture"
    };
    DWORD race_id;
    DWORD gender_id;
    LPCSTR gender_suffix;
    PATHSTR candidate;

    if (!stem || !*stem || slot >= M2_CHAR_TEX_COMPONENT_COUNT || !out || out_size == 0) {
        return false;
    }
    gender_suffix = M2_DbcCharacterRaceGender(model_path, &race_id, &gender_id) && gender_id ? "F" : "M";

    snprintf(candidate, sizeof(candidate), "Item\\TextureComponents\\%s\\%s_%s.blp", folders[slot], stem, gender_suffix);
    if (M2_TextureExists(candidate)) {
        snprintf(out, out_size, "%s", candidate);
        return true;
    }
    snprintf(candidate, sizeof(candidate), "Item\\TextureComponents\\%s\\%s_U.blp", folders[slot], stem);
    if (M2_TextureExists(candidate)) {
        snprintf(out, out_size, "%s", candidate);
        return true;
    }
    snprintf(candidate, sizeof(candidate), "Item\\TextureComponents\\%s\\%s.blp", folders[slot], stem);
    if (M2_TextureExists(candidate)) {
        snprintf(out, out_size, "%s", candidate);
        return true;
    }
    return false;
}

static BOOL M2_DefaultObjectComponentTexturePath(LPCSTR model_path,
                                                 DWORD texture_type,
                                                 LPSTR out,
                                                 DWORD out_size) {
    LPCSTR filename;
    size_t stem_len;

    if (!model_path || !out || out_size == 0 || texture_type != 2) {
        return false;
    }
    if (!strcasestr(model_path, "Item\\ObjectComponents\\Weapon\\") &&
        !strcasestr(model_path, "Item/ObjectComponents/Weapon/")) {
        return false;
    }

    filename = strrchr(model_path, '\\');
    if (!filename) {
        filename = strrchr(model_path, '/');
    }
    filename = filename ? filename + 1 : model_path;
    stem_len = strcspn(filename, ".");

    if (stem_len == strlen("Axe_1H_Horde_A_01") &&
        !strncasecmp(filename, "Axe_1H_Horde_A_01", stem_len)) {
        snprintf(out, out_size, "Item\\ObjectComponents\\Weapon\\Axe_1H_Horde_A_01Gray.blp");
        return true;
    }
    if (stem_len == 0 || stem_len + strlen("Item\\ObjectComponents\\Weapon\\.blp") + 1 > out_size) {
        return false;
    }

    snprintf(out, out_size, "Item\\ObjectComponents\\Weapon\\%.*s.blp", (int)stem_len, filename);
    return true;
}

static BOOL M2_DefaultCreatureTexturePath(LPCSTR model_path,
                                          DWORD texture_type,
                                          LPSTR out,
                                          DWORD out_size) {
    typedef struct {
        LPCSTR model;
        DWORD texture_type;
        LPCSTR texture;
    } defaultCreatureTexture_t;
    static defaultCreatureTexture_t const defaults[] = {
        { "Creature\\Wolf\\Wolf.mdx",     11, "Creature\\Wolf\\WolfSkinCoyote.blp" },
        { "Creature\\Wolf\\Wolf.m2",      11, "Creature\\Wolf\\WolfSkinCoyote.blp" },
        { "Creature\\Wolf\\Wolf.mdx",     12, "Creature\\Wolf\\WolfSkinCoyoteAlpha.blp" },
        { "Creature\\Wolf\\Wolf.m2",      12, "Creature\\Wolf\\WolfSkinCoyoteAlpha.blp" },
        { "Creature\\Boar\\Boar.mdx",     11, "Creature\\Boar\\BoarSkinIvory.blp" },
        { "Creature\\Boar\\Boar.m2",      11, "Creature\\Boar\\BoarSkinIvory.blp" },
        { "Creature\\Kobold\\Kobold.mdx", 11, "Creature\\Kobold\\koboldskinAlbino.blp" },
        { "Creature\\Kobold\\Kobold.m2",  11, "Creature\\Kobold\\koboldskinAlbino.blp" },
        { "Creature\\Murloc\\Murloc.mdx", 11, "Creature\\Murloc\\SahauginskinBlue.blp" },
        { "Creature\\Murloc\\Murloc.m2",  11, "Creature\\Murloc\\SahauginskinBlue.blp" },
    };

    if (!model_path || !out || out_size == 0) {
        return false;
    }

    FOR_LOOP(i, sizeof(defaults) / sizeof(defaults[0])) {
        if (texture_type == defaults[i].texture_type &&
            !strcasecmp(model_path, defaults[i].model)) {
            snprintf(out, out_size, "%s", defaults[i].texture);
            return true;
        }
    }
    return false;
}

static void *M2_ModelArrayPtr(m2Model_t const *model, m2Array_t array, DWORD elem_size) {
    if (!model || !model->file) {
        return NULL;
    }
    return m2_array_ptr((BYTE const *)model->file, model->file_size, array, elem_size);
}

/* Classic and modern headers are both file images; access their arrays without copying schema into runtime state. */
#define BZ_M2_ARRAY_ACCESSOR(name, field) \
static m2Array_t name(m2Model_t const *model) { \
    if (!model || !model->file) return (m2Array_t){ 0 }; \
    return model->format->format == M2_FORMAT_CLASSIC ? \
        model->file->classic.field : model->file->modern.field; \
}
BZ_M2_ARRAY_ACCESSOR(M2_GlobalLoopsArray, global_loops)
BZ_M2_ARRAY_ACCESSOR(M2_SequencesArray, sequences)
BZ_M2_ARRAY_ACCESSOR(M2_BonesArray, bones)
BZ_M2_ARRAY_ACCESSOR(M2_BoneLookupArray, bone_lookup_table)
BZ_M2_ARRAY_ACCESSOR(M2_AttachmentsArray, attachments)
BZ_M2_ARRAY_ACCESSOR(M2_AttachmentLookupArray, attachment_lookup)
BZ_M2_ARRAY_ACCESSOR(M2_CamerasArray, cameras)
BZ_M2_ARRAY_ACCESSOR(M2_TexturesArray, textures)
BZ_M2_ARRAY_ACCESSOR(M2_TextureLookupArray, texture_lookup_table)
BZ_M2_ARRAY_ACCESSOR(M2_RibbonsArray, ribbons)
BZ_M2_ARRAY_ACCESSOR(M2_ParticlesArray, particles)
#undef BZ_M2_ARRAY_ACCESSOR

static BYTE const *M2_Sequences(m2Model_t const *model) {
    return M2_ModelArrayPtr(model, M2_SequencesArray(model), model->format->sequence_stride);
}

static DWORD M2_SequenceCount(m2Model_t const *model) { return model ? (DWORD)M2_SequencesArray(model).size : 0; }

static BYTE const *M2_Sequence(m2Model_t const *model, DWORD index) {
    BYTE const *sequences = model ? M2_Sequences(model) : NULL;
    return sequences && index < M2_SequenceCount(model) ? sequences + index * model->format->sequence_stride : NULL;
}

static BYTE const *M2_Bones(m2Model_t const *model) {
    return M2_ModelArrayPtr(model, M2_BonesArray(model), model->format->bone_stride);
}

static WORD const *M2_BoneLookup(m2Model_t const *model) {
    return M2_ModelArrayPtr(model, M2_BoneLookupArray(model), sizeof(WORD));
}

static DWORD M2_SequenceStart(m2Model_t const *model, DWORD seq) {
    BYTE const *sequence = M2_Sequence(model, seq);
    if (!sequence) return 0;
    if (model->format->format == M2_FORMAT_CLASSIC) {
        return ((m2SequenceClassic_t const *)sequence)->start_timestamp;
    }
    return 0;
}

static DWORD M2_SequenceDuration(m2Model_t const *model, DWORD seq) {
    BYTE const *sequence = M2_Sequence(model, seq);
    if (!sequence) return 0;
    if (model->format->format == M2_FORMAT_CLASSIC) {
        m2SequenceClassic_t const *classic = (m2SequenceClassic_t const *)sequence;
        return classic->end_timestamp > classic->start_timestamp
            ? classic->end_timestamp - classic->start_timestamp
            : 0;
    } else {
        return ((m2SequenceModern_t const *)sequence)->duration;
    }
}

static DWORD M2_SequenceFlags(m2Model_t const *model, DWORD seq) {
    BYTE const *sequence = M2_Sequence(model, seq);
    if (!sequence) return 0;
    return model->format->format == M2_FORMAT_CLASSIC ? ((m2SequenceClassic_t const *)sequence)->flags
                                                       : ((m2SequenceModern_t const *)sequence)->flags;
}

static WORD M2_SequenceAnimId(m2Model_t const *model, DWORD seq) {
    BYTE const *sequence;

    sequence = M2_Sequence(model, seq);
    if (!sequence) return 0;
    if (model->format->format == M2_FORMAT_CLASSIC)
        return ((m2SequenceClassic_t const *)sequence)->animation_id;
    return ((m2SequenceModern_t const *)sequence)->animation_id;
}

/* Lua SetSequence passes Blizzard animation IDs, not raw M2 sequence row indices. */
static BOOL M2_FindSequenceByAnimId(m2Model_t const *model, DWORD anim_id, LPDWORD seq) {
    if (!model || !seq)
        return false;
    FOR_LOOP(i, M2_SequenceCount(model)) {
        if (M2_SequenceAnimId(model, i) == anim_id) {
            *seq = i;
            return true;
        }
    }
    return false;
}

#define M2_FRAME_SEQUENCE_FLAG  0x80000000u
#define M2_FRAME_SEQUENCE_SHIFT 21
#define M2_FRAME_SEQUENCE_MASK  0x3ffu
#define M2_FRAME_TIME_MASK      0x1fffffu

BOOL M2_SetEntitySequenceFrame(m2Model_t const *model, LPCSTR anim, renderEntity_t *entity) {
    char *end = NULL;
    DWORD anim_id;
    DWORD seq;

    if (!model || !entity)
        return false;
    anim_id = anim && *anim ? (DWORD)strtoul(anim, &end, 10) : 0;
    if (anim && *anim && (!end || *end))
        return false;
    if (!M2_FindSequenceByAnimId(model, anim_id, &seq))
        seq = anim_id;
    if (M2_SequenceCount(model) && seq >= M2_SequenceCount(model))
        return false;
    entity->frame = M2_FRAME_SEQUENCE_FLAG |
                    ((anim_id & M2_FRAME_SEQUENCE_MASK) << M2_FRAME_SEQUENCE_SHIFT) |
                    (entity->frame & M2_FRAME_TIME_MASK);
    entity->oldframe = M2_FRAME_SEQUENCE_FLAG |
                       ((anim_id & M2_FRAME_SEQUENCE_MASK) << M2_FRAME_SEQUENCE_SHIFT) |
                       (entity->oldframe & M2_FRAME_TIME_MASK);
    return true;
}

typedef struct {
    DWORD seq;
    DWORD tim;
} m2PoseTime_t;

static BOOL M2_FrameToPoseTime(m2Model_t const *model, DWORD frame, m2PoseTime_t *pose) {
    DWORD range_start = 0;

    if (pose) {
        pose->seq = 0;
        pose->tim = frame;
    }
    if (!model || !M2_SequenceCount(model) || !pose) {
        return false;
    }

    if (frame & M2_FRAME_SEQUENCE_FLAG) {
        DWORD anim_id = (frame >> M2_FRAME_SEQUENCE_SHIFT) & M2_FRAME_SEQUENCE_MASK;
        DWORD sequence;
        DWORD local_time = frame & M2_FRAME_TIME_MASK;
        DWORD duration;

        if (!M2_FindSequenceByAnimId(model, anim_id, &sequence))
            sequence = anim_id;
        if (sequence >= M2_SequenceCount(model))
            sequence = 0;
        duration = M2_SequenceDuration(model, sequence);
        pose->seq = sequence;
        pose->tim = duration
            ? M2_SequenceStart(model, sequence) + (local_time % duration)
            : M2_SequenceStart(model, sequence);
        return true;
    }

    FOR_LOOP(i, M2_SequenceCount(model)) {
        DWORD duration = M2_SequenceDuration(model, i);
        DWORD range_length = MAX(duration, 1);

        if (frame >= range_start && frame < range_start + range_length) {
            DWORD local_time = frame - range_start;

            pose->seq = i;
            pose->tim = duration
                ? M2_SequenceStart(model, i) + (local_time % duration)
                : M2_SequenceStart(model, i);
            return true;
        }
        range_start += range_length;
    }

    if (M2_SequenceCount(model)) {
        DWORD duration = M2_SequenceDuration(model, 0);

        pose->seq = 0;
        pose->tim = duration
            ? M2_SequenceStart(model, 0) + (frame % duration)
            : M2_SequenceStart(model, 0);
        return true;
    }
    return false;
}

static DWORD M2_AnimationTime(m2Model_t const *model, renderEntity_t const *entity, DWORD *seq) {
    DWORD frame = entity ? entity->frame : tr.viewDef.time;
    m2PoseTime_t pose, old_pose;

    if (seq) {
        *seq = 0;
    }
    if (!M2_FrameToPoseTime(model, frame, &pose)) {
        return frame;
    }

    if (entity &&
        M2_FrameToPoseTime(model, entity->oldframe, &old_pose) &&
        old_pose.seq == pose.seq) {
        DWORD duration = M2_SequenceDuration(model, pose.seq);
        DWORD start_time = M2_SequenceStart(model, pose.seq);
        DWORD old_time = old_pose.tim - start_time;
        DWORD local_time = pose.tim - start_time;
        FLOAT end_time = (FLOAT)local_time;
        FLOAT lerped;

        if (duration > 0 && old_time > local_time && !(M2_SequenceFlags(model, pose.seq) & 0x1)) {
            end_time += (FLOAT)duration;
        }
        lerped = LerpNumber((FLOAT)old_time, end_time, tr.viewDef.lerpfrac);
        if (duration > 0 && lerped >= (FLOAT)duration) {
            lerped -= (FLOAT)duration * floorf(lerped / (FLOAT)duration);
        }
        pose.tim = start_time + (DWORD)MAX(0.0f, lerped);
    }

    if (seq) {
        *seq = pose.seq;
    }
    return pose.tim;
}

static DWORD M2_TrackTime(m2Model_t const *model,
                          m2TrackView_t const *track,
                          DWORD seq,
                          DWORD tim) {
    DWORD const *loops;
    m2Array_t global_loops;

    if (!model || !track || !model->file || track->loop_index == 0xFFFF) {
        return tim;
    }

    global_loops = M2_GlobalLoopsArray(model);
    loops = M2_ModelArrayPtr(model, global_loops, sizeof(DWORD));
    if (!loops || track->loop_index >= (WORD)global_loops.size || loops[track->loop_index] == 0) {
        return tim;
    }

    (void)seq;
    return tr.viewDef.time % loops[track->loop_index];
}

static BOOL M2_FindTrackKeys(m2Model_t const *model,
                             m2TrackView_t const *track,
                             DWORD seq,
                             DWORD tim,
                             DWORD elem_size,
                             void const **left,
                             void const **right,
                             float *ratio) {
    DWORD const *times;
    BYTE const *keys;
    DWORD count;

    if (!model || !track || !left || !right || !ratio) {
        return false;
    }

    if (track->classic) {
        m2Range_t const *ranges = M2_ModelArrayPtr(model, track->ranges, sizeof(*ranges));
        m2Range_t range;

        /* TBC-era classic particles store tracks without per-animation ranges (ranges.size==0).
           Fall back to reading times and keys as flat global arrays in that case. */
        if (!ranges || track->ranges.size == 0) {
            times = M2_ModelArrayPtr(model, track->sequence_times, sizeof(DWORD));
            keys = M2_ModelArrayPtr(model, track->sequence_keys, elem_size);
            if (!times || !keys) return false;
            count = MIN((DWORD)track->sequence_times.size, (DWORD)track->sequence_keys.size);
            if (count == 0) return false;
        } else {
        if (seq >= (DWORD)track->ranges.size) {
            seq = 0;
        }

        range = ranges[seq];
        if (range.end < range.start) {
            return false;
        }

        times = M2_ModelArrayPtr(model, track->sequence_times, sizeof(DWORD));
        keys = M2_ModelArrayPtr(model, track->sequence_keys, elem_size);
        if (!times || !keys ||
            range.start >= (DWORD)track->sequence_times.size ||
            range.start >= (DWORD)track->sequence_keys.size) {
            return false;
        }

        count = range.end - range.start + 1;
        count = MIN(count, (DWORD)track->sequence_times.size - range.start);
        count = MIN(count, (DWORD)track->sequence_keys.size - range.start);
        times += range.start;
        keys += range.start * elem_size;
        if (count == 0) {
            return false;
        }
        }
    } else {
        m2SequenceTimes_t const *stms = M2_ModelArrayPtr(model, track->sequence_times, sizeof(*stms));
        m2SequenceKeys_t const *skeys = M2_ModelArrayPtr(model, track->sequence_keys, sizeof(*skeys));
        if (!stms || !skeys || track->sequence_times.size == 0 || track->sequence_keys.size == 0) {
            return false;
        }

        if (seq >= (DWORD)track->sequence_times.size || seq >= (DWORD)track->sequence_keys.size) {
            seq = 0;
        }

        times = M2_ModelArrayPtr(model, stms[seq].times, sizeof(DWORD));
        keys = M2_ModelArrayPtr(model, skeys[seq].keys, elem_size);
        count = MIN((DWORD)stms[seq].times.size, (DWORD)skeys[seq].keys.size);
        if (!times || !keys || count == 0) {
            return false;
        }
    }

    if (count == 1 || track->track_type == TRACK_NO_INTERP || tim <= times[0]) {
        *left = keys;
        *right = keys;
        *ratio = 0.0f;
        return true;
    }

    for (DWORD i = 1; i < count; i++) {
        if (tim <= times[i]) {
            DWORD left_time = times[i - 1];
            DWORD right_time = times[i];
            *left = keys + ((i - 1) * elem_size);
            *right = keys + (i * elem_size);
            *ratio = right_time > left_time
                ? (float)(tim - left_time) / (float)(right_time - left_time)
                : 0.0f;
            return true;
        }
    }

    *left = keys + ((count - 1) * elem_size);
    *right = *left;
    *ratio = 0.0f;
    return true;
}

static VECTOR3 M2_EvaluateVectorTrack(m2Model_t const *model,
                                      m2TrackView_t const *track,
                                      DWORD seq,
                                      DWORD tim,
                                      VECTOR3 default_value) {
    void const *left;
    void const *right;
    float ratio;
    DWORD track_time = M2_TrackTime(model, track, seq, tim);

    if (!M2_FindTrackKeys(model, track, seq, track_time, sizeof(VECTOR3), &left, &right, &ratio)) {
        return default_value;
    }
    if (left == right) {
        return *(VECTOR3 const *)left;
    }
    return Vector3_lerp((LPCVECTOR3)left, (LPCVECTOR3)right, ratio);
}

static FLOAT M2_EvaluateFloatTrack(m2Model_t const *model,
                                   m2TrackView_t const *track,
                                   DWORD seq,
                                   DWORD tim,
                                   FLOAT default_value) {
    void const *left;
    void const *right;
    float ratio;
    DWORD track_time = M2_TrackTime(model, track, seq, tim);

    if (!M2_FindTrackKeys(model, track, seq, track_time, sizeof(FLOAT), &left, &right, &ratio)) {
        return default_value;
    }
    if (left == right) {
        return *(FLOAT const *)left;
    }
    return LerpNumber(*(FLOAT const *)left, *(FLOAT const *)right, ratio);
}

BOOL M2_CameraView(m2Model_t const *model,
                   DWORD camera_index,
                   LPVECTOR3 eye,
                   LPVECTOR3 target,
                   LPFLOAT fov_degrees,
                   LPFLOAT znear,
                   LPFLOAT zfar) {
    m2PoseTime_t pose;
    m2TrackView_t position_track;
    m2TrackView_t target_track;
    VECTOR3 position_value;
    VECTOR3 target_value;
    VECTOR3 position_pivot;
    VECTOR3 target_pivot;
    FLOAT fov;
    FLOAT far_clip;
    FLOAT near_clip;
    m2Array_t cameras;
    BYTE const *camera_data;

    if (!model || !eye || !target) return false;
    cameras = M2_CamerasArray(model);
    camera_data = M2_ModelArrayPtr(model, cameras, model->format->camera_stride);
    if (!camera_data || camera_index >= (DWORD)cameras.size) return false;

    if (model->format->format == M2_FORMAT_CLASSIC) {
        BYTE const *record = camera_data + camera_index * model->format->camera_stride;
        m2CameraClassic_t const *camera = (m2CameraClassic_t const *)record;

        position_track = m2_classic_track(&camera->position_track);
        target_track = m2_classic_track(&camera->target_track);
        position_pivot = camera->position_pivot;
        target_pivot = camera->target_pivot;
        fov = camera->fov; near_clip = camera->near_clip; far_clip = camera->far_clip;
    } else {
        BYTE const *record = camera_data + camera_index * model->format->camera_stride;
        m2CameraModern_t const *camera = (m2CameraModern_t const *)record;

        position_track = m2_modern_track(&camera->position_track);
        target_track = m2_modern_track(&camera->target_track);
        position_pivot = camera->position_pivot;
        target_pivot = camera->target_pivot;
        fov = camera->fov; near_clip = camera->near_clip; far_clip = camera->far_clip;
    }
    M2_FrameToPoseTime(model, tr.viewDef.time, &pose);
    position_value = M2_EvaluateVectorTrack(model, &position_track, pose.seq, pose.tim, (VECTOR3){ 0.0f, 0.0f, 0.0f });
    target_value = M2_EvaluateVectorTrack(model, &target_track, pose.seq, pose.tim, (VECTOR3){ 0.0f, 0.0f, 0.0f });
    *eye = Vector3_add(&position_pivot, &position_value);
    *target = Vector3_add(&target_pivot, &target_value);
    if (fov_degrees)
        *fov_degrees = fov > 0.0f ? fov * 0.6f * 180.0f / (FLOAT)M_PI : 35.0f;
    if (znear)
        *znear = near_clip > 0.0f ? near_clip : 1.0f;
    if (zfar)
        *zfar = far_clip > near_clip ? far_clip : 4000.0f;
    return true;
}

static QUATERNION M2_DecodeCompQuat(m2CompQuat_t const *source) {
    return (QUATERNION) {
        .x = (float)(source->auCompQ[0] & 0xFFFF) * 0.000030518044f - 1.0f,
        .y = (float)(source->auCompQ[0] >> 16)    * 0.000030518044f - 1.0f,
        .z = (float)(source->auCompQ[1] & 0xFFFF) * 0.000030518044f - 1.0f,
        .w = (float)(source->auCompQ[1] >> 16)    * 0.000030518044f - 1.0f,
    };
}

static QUATERNION M2_QuaternionNlerp(LPCQUATERNION q1, LPCQUATERNION q2, float ratio) {
    QUATERNION out = {
        .x = (q2->x - q1->x) * ratio + q1->x,
        .y = (q2->y - q1->y) * ratio + q1->y,
        .z = (q2->z - q1->z) * ratio + q1->z,
        .w = (q2->w - q1->w) * ratio + q1->w,
    };
    float m = out.x * out.x + out.y * out.y + out.z * out.z + out.w * out.w;
    float v = ((m - 0.95906597f) * -0.532516f) + 1.021435f;

    if (m <= 0.91521198f) {
        v *= (((v * v * m) - 0.95906597f) * -0.532516f) + 1.021435f;
        if (m <= 0.6521197f) {
            v *= (((v * v * m) - 0.95906597f) * -0.532516f) + 1.021435f;
        }
    }

    out.x *= v;
    out.y *= v;
    out.z *= v;
    out.w *= v;
    return out;
}

static QUATERNION M2_EvaluateRotationTrack(m2Model_t const *model,
                                           m2TrackView_t const *track,
                                           DWORD seq,
                                           DWORD tim,
                                           QUATERNION default_value) {
    void const *left;
    void const *right;
    float ratio;
    DWORD track_time = M2_TrackTime(model, track, seq, tim);
    DWORD elem_size = track && track->classic ? sizeof(QUATERNION) : sizeof(m2CompQuat_t);

    if (!M2_FindTrackKeys(model, track, seq, track_time, elem_size, &left, &right, &ratio)) {
        return default_value;
    }
    if (track->classic) {
        if (left == right) {
            return *(QUATERNION const *)left;
        }

        QUATERNION q1 = *(QUATERNION const *)left;
        QUATERNION q2 = *(QUATERNION const *)right;
        return M2_QuaternionNlerp(&q1, &q2, ratio);
    }
    if (left == right) {
        return M2_DecodeCompQuat((m2CompQuat_t const *)left);
    }

    QUATERNION q1 = M2_DecodeCompQuat((m2CompQuat_t const *)left);
    QUATERNION q2 = M2_DecodeCompQuat((m2CompQuat_t const *)right);
    return M2_QuaternionNlerp(&q1, &q2, ratio);
}

static BOOL M2_TrackHasKeys(m2TrackView_t const *track) {
    if (!track) {
        return false;
    }
    if (track->classic) {
        return track->ranges.size > 0 && track->sequence_times.size > 0 && track->sequence_keys.size > 0;
    }
    return track->sequence_times.size > 0 && track->sequence_keys.size > 0;
}

static void const *M2_BonePtr(m2Model_t const *model, DWORD bone_index) {
    BYTE const *bones = model ? M2_Bones(model) : NULL;
    if (!bones || bone_index >= (DWORD)M2_BonesArray(model).size || !model->format->bone_stride) return NULL;
    return bones + bone_index * model->format->bone_stride;
}

static DWORD M2_BoneFlags(m2Model_t const *model, DWORD bone_index) {
    void const *bone = M2_BonePtr(model, bone_index);

    if (!bone) {
        return 0;
    }
    if (model->format->format == M2_FORMAT_CLASSIC) {
        return ((m2CompBoneClassic_t const *)bone)->flags;
    }
    return ((m2CompBoneModern_t const *)bone)->flags;
}

static WORD M2_BoneParentIndex(m2Model_t const *model, DWORD bone_index) {
    void const *bone = M2_BonePtr(model, bone_index);

    if (!bone) {
        return 0xFFFF;
    }
    if (model->format->format == M2_FORMAT_CLASSIC) {
        return ((m2CompBoneClassic_t const *)bone)->parent_index;
    }
    return ((m2CompBoneModern_t const *)bone)->parent_index;
}

static VECTOR3 M2_BonePivot(m2Model_t const *model, DWORD bone_index) {
    void const *bone = M2_BonePtr(model, bone_index);

    if (!bone) {
        return (VECTOR3){ 0.0f, 0.0f, 0.0f };
    }
    if (model->format->format == M2_FORMAT_CLASSIC) {
        return ((m2CompBoneClassic_t const *)bone)->pivot;
    }
    return ((m2CompBoneModern_t const *)bone)->pivot;
}

static m2TrackView_t M2_BoneTranslationTrack(m2Model_t const *model, DWORD bone_index) {
    void const *bone = M2_BonePtr(model, bone_index);

    if (!bone) {
        return m2_modern_track(NULL);
    }
    if (model->format->format == M2_FORMAT_CLASSIC) {
        return m2_classic_track(&((m2CompBoneClassic_t const *)bone)->translation_track);
    }
    return m2_modern_track(&((m2CompBoneModern_t const *)bone)->translation_track);
}

static m2TrackView_t M2_BoneRotationTrack(m2Model_t const *model, DWORD bone_index) {
    void const *bone = M2_BonePtr(model, bone_index);

    if (!bone) {
        return m2_modern_track(NULL);
    }
    if (model->format->format == M2_FORMAT_CLASSIC) {
        return m2_classic_track(&((m2CompBoneClassic_t const *)bone)->rotation_track);
    }
    return m2_modern_track(&((m2CompBoneModern_t const *)bone)->rotation_track);
}

static m2TrackView_t M2_BoneScaleTrack(m2Model_t const *model, DWORD bone_index) {
    void const *bone = M2_BonePtr(model, bone_index);

    if (!bone) {
        return m2_modern_track(NULL);
    }
    if (model->format->format == M2_FORMAT_CLASSIC) {
        return m2_classic_track(&((m2CompBoneClassic_t const *)bone)->scale_track);
    }
    return m2_modern_track(&((m2CompBoneModern_t const *)bone)->scale_track);
}

/* Identity-palette instancing is exact only for static M2s without emitter side effects. */
BOOL M2_CanStaticInstance(m2Model_t const *model) {
    if (!model || !model->file || model->flags & M2_MODEL_CHARACTER || M2_ParticlesArray(model).size || M2_RibbonsArray(model).size)
        return false;
    FOR_LOOP(i, (DWORD)M2_BonesArray(model).size) {
        m2TrackView_t pos = M2_BoneTranslationTrack(model, i);
        m2TrackView_t rot = M2_BoneRotationTrack(model, i);
        m2TrackView_t scl = M2_BoneScaleTrack(model, i);
        if (M2_TrackHasKeys(&pos) || M2_TrackHasKeys(&rot) || M2_TrackHasKeys(&scl)) return false;
    }
    return true;
}

static float m2_fixed16_to_float(SHORT v) { return (float)v / 32767.0f; }

static BOOL m2_is_visible(m2Model_t const *model, m2TrackView_t *track,
                          DWORD seq_idx, DWORD seq_time) {
	if (!M2_TrackHasKeys(track)) return true;
	void const *left, *right; float ratio;
	DWORD t = M2_TrackTime(model, track, seq_idx, seq_time);
	if (!M2_FindTrackKeys(model, track, seq_idx, t, sizeof(BYTE), &left, &right, &ratio))
		return true;
	BYTE b = *(BYTE const *)left;
	if (left != right) b = (BYTE)LerpNumber((FLOAT)*(BYTE const *)left, (FLOAT)*(BYTE const *)right, ratio);
	return b != 0;
}

static void m2_sample_part_track(m2Model_t const *model, m2PartTrack_t const *track,
                                 float progress, DWORD elem_size, void *out) {
	SHORT const *times = M2_ModelArrayPtr(model, track->times, sizeof(SHORT));
	BYTE const *vals = M2_ModelArrayPtr(model, track->values, elem_size);
	DWORD count = (DWORD)track->times.size;
	if (!times || !vals || count == 0 || count != (DWORD)track->values.size) {
		memset(out, 0, elem_size); return;
	}
	progress = MAX(0.0f, MIN(1.0f, progress));
	if (count == 1 || progress <= m2_fixed16_to_float(times[0])) {
		memcpy(out, vals, elem_size); return;
	}
	FOR_LOOP(i, count) {
		if (i == 0) continue;
		float fi = m2_fixed16_to_float(times[i]), fi1 = m2_fixed16_to_float(times[i - 1]);
		if (progress <= fi || i == count - 1) {
			float ratio = fi > fi1 ? (progress - fi1) / (fi - fi1) : 0.0f;
			BYTE const *a = vals + (i - 1) * elem_size, *b = vals + i * elem_size;
			if (elem_size == sizeof(SHORT))
				*(SHORT *)out = (SHORT)LerpNumber((FLOAT)*(SHORT *)a, (FLOAT)*(SHORT *)b, ratio);
			else if (elem_size == sizeof(VECTOR2))
				*(VECTOR2 *)out = Vector2_lerp((LPCVECTOR2)a, (LPCVECTOR2)b, ratio);
			else if (elem_size == sizeof(VECTOR3))
				*(VECTOR3 *)out = Vector3_lerp((LPCVECTOR3)a, (LPCVECTOR3)b, ratio);
			else memcpy(out, a, elem_size);
			return;
		}
	}
	memcpy(out, vals + (count - 1) * elem_size, elem_size);
}

static LPTEXTURE m2_particle_texture(m2Model_t const *model, m2Particle_t const *p) {
	m2TextureDisk_t const *tex; LPCSTR path; m2Array_t textures;
	if (!model || !p) return tr.texture[TEX_WHITE];
	textures = M2_TexturesArray(model);
	if (!textures.size || p->texture_index >= (WORD)textures.size)
		return tr.texture[TEX_WHITE];
	tex = M2_ModelArrayPtr(model, textures, sizeof(*tex));
	if (!tex || p->texture_index >= (DWORD)textures.size) return tr.texture[TEX_WHITE];
	path = m2_string_ptr((BYTE const *)model->file, model->file_size, tex[p->texture_index].filename);
	return path && *path ? R_LoadTexture(path) : tr.texture[TEX_WHITE];
}

static LPTEXTURE m2_ribbon_texture(m2Model_t const *model, m2Ribbon_t const *r, DWORD slot) {
	WORD const *indices; m2TextureDisk_t const *tex; DWORD idx; LPCSTR path; m2Array_t textures;
	if (!model || !r) return tr.texture[TEX_WHITE];
	textures = M2_TexturesArray(model);
	if (!textures.size) return tr.texture[TEX_WHITE];
	indices = M2_ModelArrayPtr(model, r->texture_indices, sizeof(WORD));
	tex = M2_ModelArrayPtr(model, textures, sizeof(*tex));
	if (!indices || !tex || slot >= (DWORD)r->texture_indices.size) return tr.texture[TEX_WHITE];
	idx = indices[slot];
	if (idx >= (DWORD)textures.size) return tr.texture[TEX_WHITE];
	path = m2_string_ptr((BYTE const *)model->file, model->file_size, tex[idx].filename);
	return path && *path ? R_LoadTexture(path) : tr.texture[TEX_WHITE];
}

#define M2_C32(v,a) (COLOR32){ (BYTE)((v).x < 0 ? 0 : (v).x > 1 ? 255 : (BYTE)((v).x*255+.5f)), \
                               (BYTE)((v).y < 0 ? 0 : (v).y > 1 ? 255 : (BYTE)((v).y*255+.5f)), \
                               (BYTE)((v).z < 0 ? 0 : (v).z > 1 ? 255 : (BYTE)((v).z*255+.5f)), \
                               (BYTE)((a) < 0 ? 0 : (a) > 1 ? 255 : (BYTE)((a)*255+.5f)) }

typedef struct {
	FLOAT speed, varia, lat, lon, grav, life, life_var, zsource, midpoint;
	FLOAT alpha[3]; VECTOR2 scale[3]; VECTOR3 color[3];
	LPTEXTURE texture; WORD bone_index;
	m2Model_t const *model; m2Particle_t const *p; LPCMATRIX4 model_matrix;
} m2_pctx_t;

/* M2 emitter positions are local to their bone, not the model origin. */
static void M2_EmitterMatrix(m2_pctx_t const *ctx, LPMATRIX4 out) {
    if (ctx->model && ctx->bone_index < (DWORD)M2_BonesArray(ctx->model).size) {
        Matrix4_multiply(ctx->model_matrix, &m2_bone_matrices[ctx->bone_index], out);
        return;
    }
    *out = *ctx->model_matrix;
}

static void m2_spawn_particle(void *raw) {
	m2_pctx_t *ctx = (m2_pctx_t *)raw;
	cparticle_t *fx = R_SpawnParticle(); if (!fx) return;
	FLOAT r = (FLOAT)rand() / (FLOAT)RAND_MAX;
	MATRIX4 emitter_matrix;
	M2_EmitterMatrix(ctx, &emitter_matrix);
	VECTOR3 local_origin = ctx->p->position;
	local_origin.z += ctx->zsource;
	VECTOR3 org = Matrix4_multiply_vector3(&emitter_matrix, &local_origin);
	VECTOR3 dir = m2_particle_direction(ctx->lat, ctx->lon, (VECTOR2){ 2.0f * (FLOAT)rand() / (FLOAT)RAND_MAX - 1.0f, 2.0f * (FLOAT)rand() / (FLOAT)RAND_MAX - 1.0f });
	VECTOR3 w_dir = Matrix4_multiply_vector3(&emitter_matrix, &dir);
	VECTOR3 w_zero = Matrix4_multiply_vector3(&emitter_matrix, &(VECTOR3){ 0, 0, 0 });
	dir = Vector3_sub(&w_dir, &w_zero);
	Vector3_normalize(&dir);
	fx->texture = ctx->texture;
	fx->blend_mode = m2_particle_blend_mode(ctx->p->blend_mode);
	fx->org = org;
	fx->vel = Vector3_scale(&dir, MAX(0.0f, ctx->speed + (r - 0.5f) * ctx->varia));
	fx->accel = (VECTOR3){ 0, 0, -ctx->grav };
	fx->color[0] = M2_C32(ctx->color[0], ctx->alpha[0]);
	fx->color[1] = M2_C32(ctx->color[1], ctx->alpha[1]);
	fx->color[2] = M2_C32(ctx->color[2], ctx->alpha[2]);
	FLOAT s[3] = { MAX(ctx->scale[0].x, ctx->scale[0].y), MAX(ctx->scale[1].x, ctx->scale[1].y), MAX(ctx->scale[2].x, ctx->scale[2].y) };
	fx->time = 0.0f; fx->lifespan = MAX(0.05f, ctx->life + (r - 0.5f) * ctx->life_var);
	m2_particle_encode_curve(&(M2PARTICLECURVE){ { s[0], s[1], s[2] }, ctx->midpoint, fx->lifespan }, fx);
	fx->columns = MAX(1, ctx->p->cols); fx->rows = MAX(1, ctx->p->rows);
}


/* Vanilla/TBC stores three static BGRA lifecycle colors and three scalar scales. */
static void m2p_sample_classic_data(BYTE const *raw, m2_pctx_t *ctx) {
    m2ParticleClassic_t const *p = (m2ParticleClassic_t const *)raw;
    FLOAT midpoint = p->midpoint;
    BOOL all_alpha_zero = true;
    if (midpoint < 0.0f || midpoint > 1.0f) midpoint = 0.5f;
    FOR_LOOP(i, 3) {
        DWORD bgra = p->colors[i];
        ctx->color[i] = (VECTOR3){ ((bgra >> 16) & 0xff) / 255.0f, ((bgra >> 8) & 0xff) / 255.0f,
                                   (bgra & 0xff) / 255.0f };
        ctx->alpha[i] = ((bgra >> 24) & 0xff) / 255.0f;
        ctx->scale[i] = (VECTOR2){ p->scales[i], 0.0f };
        if (ctx->alpha[i] > 0.01f) all_alpha_zero = false;
    }
    if (all_alpha_zero) { ctx->alpha[0] = 1.0f; ctx->alpha[1] = 1.0f; ctx->alpha[2] = 0.0f; }
    ctx->midpoint = midpoint;
}

static void M2_DrawParticles(m2Model_t const *model, renderEntity_t const *entity, LPCMATRIX4 model_matrix) {
	m2Array_t particles;
	if (!model || !entity) return;
	particles = M2_ParticlesArray(model);
	if (!particles.size) return;
	BYTE const *base = M2_ModelArrayPtr(model, particles, model->format->particle_stride);
	if (!base) return;
	DWORD seq_idx, seq_time = M2_AnimationTime(model, entity, &seq_idx);
	FOR_LOOP(i, (DWORD)particles.size) {
		BYTE const *raw = base + i * model->format->particle_stride;
		m2Particle_t const *p = (m2Particle_t const *)raw;
		m2TrackView_t vis = m2_particle_track(model->format, raw, M2_PARTICLE_VISIBILITY);
		if (!m2_is_visible(model, &vis, seq_idx, seq_time)) continue;
		m2TrackView_t rate_t = m2_particle_track(model->format, raw, M2_PARTICLE_EMISSION_RATE);
		FLOAT rate = M2_EvaluateFloatTrack(model, &rate_t, seq_idx, seq_time, 0.0f);
		if (rate <= 0.0f) continue;
		m2TrackView_t life_t = m2_particle_track(model->format, raw, M2_PARTICLE_LIFE);
		FLOAT life = MAX(0.05f, M2_EvaluateFloatTrack(model, &life_t, seq_idx, seq_time, 0.5f));
		m2_pctx_t ctx = { .midpoint = 0.5f, .model = model, .p = p, .model_matrix = model_matrix };
		{ m2TrackView_t t = m2_particle_track(model->format, raw, M2_PARTICLE_SPEED);
		  ctx.speed = M2_EvaluateFloatTrack(model, &t, seq_idx, seq_time, 0.0f); }
		{ m2TrackView_t t = m2_particle_track(model->format, raw, M2_PARTICLE_VARIATION);
		  ctx.varia = M2_EvaluateFloatTrack(model, &t, seq_idx, seq_time, 0.0f); }
		{ m2TrackView_t t = m2_particle_track(model->format, raw, M2_PARTICLE_VERTICAL_RANGE);
		  ctx.lat = M2_EvaluateFloatTrack(model, &t, seq_idx, seq_time, 0.0f); }
		{ m2TrackView_t t = m2_particle_track(model->format, raw, M2_PARTICLE_HORIZONTAL_RANGE);
		  ctx.lon = M2_EvaluateFloatTrack(model, &t, seq_idx, seq_time, 0.0f); }
		{ m2TrackView_t t = m2_particle_track(model->format, raw, M2_PARTICLE_GRAVITY);
		  ctx.grav = M2_EvaluateFloatTrack(model, &t, seq_idx, seq_time, 0.0f); }
		ctx.life     = life;
		ctx.life_var = model->format->format == M2_FORMAT_CLASSIC ? 0.0f : p->life_variation;
		{ m2TrackView_t t = m2_particle_track(model->format, raw, M2_PARTICLE_ZSOURCE);
		  ctx.zsource = M2_EvaluateFloatTrack(model, &t, seq_idx, seq_time, 0.0f); }
		if (model->format->format == M2_FORMAT_CLASSIC) m2p_sample_classic_data(raw, &ctx);
		else {
			m2PartTrack_t const *alpha_pt = m2_particle_part_track(model->format, raw, 1);
			m2PartTrack_t const *scale_pt = m2_particle_part_track(model->format, raw, 2);
			m2PartTrack_t const *color_pt = m2_particle_part_track(model->format, raw, 0);
			{ SHORT raw2;
			  m2_sample_part_track(model, alpha_pt, 0.0f, sizeof(raw2), &raw2);
			  ctx.alpha[0] = m2_fixed16_to_float(raw2);
			  m2_sample_part_track(model, alpha_pt, 0.5f, sizeof(raw2), &raw2);
			  ctx.alpha[1] = m2_fixed16_to_float(raw2);
			  m2_sample_part_track(model, alpha_pt, 1.0f, sizeof(raw2), &raw2);
			  ctx.alpha[2] = m2_fixed16_to_float(raw2); }
			m2_sample_part_track(model, scale_pt, 0.0f, sizeof(VECTOR2), &ctx.scale[0]);
			m2_sample_part_track(model, scale_pt, 0.5f, sizeof(VECTOR2), &ctx.scale[1]);
			m2_sample_part_track(model, scale_pt, 1.0f, sizeof(VECTOR2), &ctx.scale[2]);
			m2_sample_part_track(model, color_pt, 0.0f, sizeof(VECTOR3), &ctx.color[0]);
			m2_sample_part_track(model, color_pt, 0.5f, sizeof(VECTOR3), &ctx.color[1]);
			m2_sample_part_track(model, color_pt, 1.0f, sizeof(VECTOR3), &ctx.color[2]);
		}
		ctx.texture = m2_particle_texture(model, p);
		ctx.bone_index = p->bone_index;
		R_EmitParticlesAtTime(rate, tr.viewDef.time, tr.viewDef.deltaTime, m2_spawn_particle, &ctx);
	}
}


static void M2_DrawRibbons(m2Model_t const *model, renderEntity_t const *entity, LPCMATRIX4 model_matrix) {
	m2Array_t ribbons;
	if (!model || !entity) return;
	ribbons = M2_RibbonsArray(model);
	if (!ribbons.size) return;
	BYTE const *base = M2_ModelArrayPtr(model, ribbons, model->format->ribbon_stride);
	if (!base) return;
	DWORD seq_idx, seq_time = M2_AnimationTime(model, entity, &seq_idx);
	FOR_LOOP(i, (DWORD)ribbons.size) {
		BYTE const *raw = base + i * model->format->ribbon_stride;
		m2Ribbon_t const *r = (m2Ribbon_t const *)raw;
		m2TrackView_t vis = m2_ribbon_track(model->format, raw, M2_RIBBON_VISIBILITY);
		if (!m2_is_visible(model, &vis, seq_idx, seq_time)) continue;
		FLOAT eps = MAX(0.0f, m2_ribbon_edges_per_second(model->format, raw));
		if (eps <= 0.0f) continue;
		FLOAT edge_life = m2_ribbon_edge_lifetime(model->format, raw);
		m2TrackView_t color_t = m2_ribbon_track(model->format, raw, M2_RIBBON_COLOR);
		VECTOR3 col = M2_EvaluateVectorTrack(model, &color_t, seq_idx, seq_time, (VECTOR3){ 1, 1, 1 });
		m2TrackView_t alpha_t = m2_ribbon_track(model->format, raw, M2_RIBBON_ALPHA);
		void const *la, *ra; float rta;
		DWORD tt = M2_TrackTime(model, &alpha_t, seq_idx, seq_time);
		FLOAT a = 1.0f;
		if (M2_FindTrackKeys(model, &alpha_t, seq_idx, tt, sizeof(SHORT), &la, &ra, &rta)) {
			FLOAT al = m2_fixed16_to_float(*(SHORT const *)la);
			FLOAT ar = la == ra ? al : m2_fixed16_to_float(*(SHORT const *)ra);
			a = LerpNumber(al, ar, rta);
		}
		m2TrackView_t above_t = m2_ribbon_track(model->format, raw, M2_RIBBON_HEIGHT_ABOVE);
		m2TrackView_t below_t = m2_ribbon_track(model->format, raw, M2_RIBBON_HEIGHT_BELOW);
		FLOAT h_above = MAX(0.0f, M2_EvaluateFloatTrack(model, &above_t, seq_idx, seq_time, 0.0f));
		FLOAT h_below = MAX(0.0f, M2_EvaluateFloatTrack(model, &below_t, seq_idx, seq_time, 0.0f));
		FLOAT w = MAX(1.0f, h_above + h_below) / 2.0f; /* half-width for billboard scale */
		m2TrackView_t slot_t = m2_ribbon_track(model->format, raw, M2_RIBBON_TEXTURE_SLOT);
		WORD slot = 0;
		void const *ls, *rs_; float rts;
		DWORD tts = M2_TrackTime(model, &slot_t, seq_idx, seq_time);
		if (M2_FindTrackKeys(model, &slot_t, seq_idx, tts, sizeof(WORD), &ls, &rs_, &rts))
			slot = (WORD)LerpNumber((FLOAT)*(WORD const *)ls, (FLOAT)*(WORD const *)rs_, rts);
		COLOR32 rgba = M2_C32(col, a);
		BYTE size_b = (BYTE)MIN(255, (int)(w + 0.5f));
		DWORD cols = MAX(1, m2_ribbon_cols(model->format, raw));
		DWORD rows = MAX(1, m2_ribbon_rows(model->format, raw));
		LPTEXTURE tex = m2_ribbon_texture(model, r, slot);
		FLOAT grav = m2_ribbon_gravity(model->format, raw);
		MATRIX4 emitter_matrix = *model_matrix;
		if (r->bone_index < (DWORD)M2_BonesArray(model).size)
			Matrix4_multiply(model_matrix, &m2_bone_matrices[r->bone_index], &emitter_matrix);
		VECTOR3 spine = Matrix4_multiply_vector3(&emitter_matrix, &r->position);
		DWORD last_ms = tr.viewDef.time - tr.viewDef.deltaTime;
		DWORD start_ms = last_ms - last_ms % 1000;
		for (FLOAT t = (FLOAT)start_ms; t < (FLOAT)tr.viewDef.time; t += 1000.0f / eps) {
			cparticle_t *fx;
			if (t < (FLOAT)last_ms) continue;
			fx = R_SpawnParticle();
			if (!fx) break;
			fx->texture = tex; fx->org = spine;
			fx->vel = (VECTOR3){ 0, 0, 0 };
			fx->accel = (VECTOR3){ 0, 0, -MAX(0.0f, grav) };
			fx->color[0] = fx->color[1] = fx->color[2] = rgba;
			fx->size[0] = fx->size[1] = fx->size[2] = size_b;
			fx->midtime = 0x80; fx->columns = cols; fx->rows = rows;
			fx->time = 0.0f; fx->lifespan = MAX(0.05f, edge_life);
		}
	}
}

static void M2_CalculateBoneMatrices(m2Model_t const *model, renderEntity_t const *entity) {
    MATRIX4 identity;
    m2PoseTime_t cur, old;
    FLOAT pose_lerp = 1.0f;
    DWORD bone_count;

    if (!model || !M2_Bones(model)) return;
    bone_count = (DWORD)M2_BonesArray(model).size;

    Matrix4_identity(&identity);
    M2_FrameToPoseTime(model, entity ? entity->frame : tr.viewDef.time, &cur);
    old = cur;
    if (entity &&
        entity->oldframe != entity->frame &&
        M2_FrameToPoseTime(model, entity->oldframe, &old) &&
        old.seq != cur.seq) {
        pose_lerp = MAX(0.0f, MIN(1.0f, tr.viewDef.lerpfrac));
    } else {
        cur.tim = M2_AnimationTime(model, entity, &cur.seq);
        old = cur;
    }

    FOR_LOOP(i, bone_count) {
        WORD parent_index = M2_BoneParentIndex(model, i);
        DWORD flags = M2_BoneFlags(model, i);
        VECTOR3 pivot = M2_BonePivot(model, i);
        m2TrackView_t ttrk = M2_BoneTranslationTrack(model, i);
        m2TrackView_t rtrk = M2_BoneRotationTrack(model, i);
        m2TrackView_t strk = M2_BoneScaleTrack(model, i);
        BOOL has_keys = M2_TrackHasKeys(&ttrk) || M2_TrackHasKeys(&rtrk) || M2_TrackHasKeys(&strk);
        LPCMATRIX4 parent = &identity;

        if (parent_index != 0xFFFF && parent_index < i) {
            parent = &m2_bone_matrices[parent_index];
        }

        if ((flags & (0x80 | 0x200)) || has_keys) {
            MATRIX4 local;
            VECTOR3 tran = M2_EvaluateVectorTrack(model, &ttrk, cur.seq, cur.tim, (VECTOR3){ 0.0f, 0.0f, 0.0f });
            QUATERNION rot = M2_EvaluateRotationTrack(model, &rtrk, cur.seq, cur.tim, (QUATERNION){ 0.0f, 0.0f, 0.0f, 1.0f });
            VECTOR3 scl = M2_EvaluateVectorTrack(model, &strk, cur.seq, cur.tim, (VECTOR3){ 1.0f, 1.0f, 1.0f });

            if (pose_lerp < 1.0f) {
                VECTOR3 otrn = M2_EvaluateVectorTrack(model, &ttrk, old.seq, old.tim, (VECTOR3){ 0.0f, 0.0f, 0.0f });
                QUATERNION orot = M2_EvaluateRotationTrack(model, &rtrk, old.seq, old.tim, (QUATERNION){ 0.0f, 0.0f, 0.0f, 1.0f });
                VECTOR3 oscl = M2_EvaluateVectorTrack(model, &strk, old.seq, old.tim, (VECTOR3){ 1.0f, 1.0f, 1.0f });

                tran = Vector3_lerp(&otrn, &tran, pose_lerp);
                rot = Quaternion_slerp(&orot, &rot, pose_lerp);
                scl = Vector3_lerp(&oscl, &scl, pose_lerp);
            }

            Matrix4_from_rotation_translation_scale_origin(&local, &rot, &tran, &scl, &pivot);
            Matrix4_multiply(parent, &local, &m2_bone_matrices[i]);
        } else {
            m2_bone_matrices[i] = *parent;
        }
    }
}

static void M2_UploadBatchBones(m2Model_t const *model, m2ModelBatch_t const *batch, LPSHADER shader) {
    MATRIX4 palette[M2_MAX_BONES_PER_BATCH];
    WORD const *bone_lookup = model ? M2_BoneLookup(model) : NULL;
    DWORD nlook = model ? (DWORD)M2_BoneLookupArray(model).size : 0;
    DWORD nbone = model ? (DWORD)M2_BonesArray(model).size : 0;

    FOR_LOOP(i, M2_MAX_BONES_PER_BATCH) {
        Matrix4_identity(&palette[i]);
    }

    if (model && batch && bone_lookup) {
        DWORD count = MIN((DWORD)batch->bone_count, (DWORD)M2_MAX_BONES_PER_BATCH);
        FOR_LOOP(i, count) {
            DWORD lookup = (DWORD)batch->bone_combo_index + i;
            if (lookup < nlook) {
                WORD bidx = bone_lookup[lookup];
                if (bidx < nbone) {
                    palette[i] = m2_bone_matrices[bidx];
                }
            }
        }
    }

    R_Call(glUniformMatrix4fv, shader->uBones, M2_MAX_BONES_PER_BATCH, GL_FALSE, palette[0].v);
}

static LPTEXTURE M2_TextureForBatch(BYTE const *m2_data,
                                    DWORD m2_size,
                                    m2GeometryInfo_t const *geom,
                                    m2Batch_t const *batch,
                                    LPCSTR modelFilename,
                                    BOOL use_texture_lookup,
                                    DWORD *texture_type_out) {
    SHORT const *lookup;
    m2TextureDisk_t const *tex;
    SHORT idx;
    LPCSTR path;
    PATHSTR repl;

    if (!geom || !batch || batch->texture_count == 0) {
        if (texture_type_out) {
            *texture_type_out = 0;
        }
        return tr.texture[TEX_WHITE];
    }

    if (use_texture_lookup) {
        lookup = m2_array_ptr(m2_data, m2_size, geom->texture_lookup_table, sizeof(SHORT));
        if (!lookup || batch->texture_combo_index >= (WORD)geom->texture_lookup_table.size) {
            return tr.texture[TEX_WHITE];
        }
        idx = lookup[batch->texture_combo_index];
    } else {
        idx = (SHORT)batch->texture_combo_index;
    }
    if (idx < 0 || idx >= geom->textures.size) {
        if (texture_type_out) {
            *texture_type_out = 0;
        }
        return tr.texture[TEX_WHITE];
    }

    tex = m2_array_ptr(m2_data, m2_size, geom->textures, sizeof(*tex));
    if (!tex) {
        if (texture_type_out) {
            *texture_type_out = 0;
        }
        return tr.texture[TEX_WHITE];
    }
    if (texture_type_out) {
        *texture_type_out = tex[idx].type;
    }

    path = m2_string_ptr(m2_data, m2_size, tex[idx].filename);
    if (!path || !*path) {
        if (M2_DbcCharacterTexturePathForType(modelFilename, 0, tex[idx].type, repl, sizeof(repl))) {
            return R_LoadTexture(repl);
        }
        if (M2_DefaultObjectComponentTexturePath(modelFilename, tex[idx].type, repl, sizeof(repl))) {
            return R_LoadTexture(repl);
        }
        if (M2_DefaultCreatureTexturePath(modelFilename, tex[idx].type, repl, sizeof(repl))) {
            return R_LoadTexture(repl);
        }
        return tr.texture[TEX_WHITE];
    }
    return R_LoadTexture(path);
}

static BOOL M2_SkinPath(LPCSTR model_path, LPSTR out, DWORD out_size) {
    if (!m2_copy_with_extension(model_path, "00.skin", out, out_size)) {
        return false;
    }
    return true;
}

static VERTEX M2_MakeVertex(m2VertexDisk_t const *src) {
    VERTEX out;
    memset(&out, 0, sizeof(out));
    out.position = src->pos;
    out.normal = src->normal;
    out.texcoord = src->tex_coords[0];
    out.color = COLOR32_WHITE;
    memcpy(out.skin, src->bone_indices, sizeof(src->bone_indices));
    memcpy(out.boneWeight, src->bone_weights, sizeof(src->bone_weights));
    return out;
}

static BOOL M2_CalculateGeometryBounds(m2VertexDisk_t const *verts, DWORD nverts, BOX3 *bounds) {
    if (!verts || !nverts || !bounds) {
        return false;
    }

    bounds->min = verts[0].pos;
    bounds->max = verts[0].pos;
    for (DWORD i = 1; i < nverts; i++) {
        VECTOR3 p = verts[i].pos;
        bounds->min.x = MIN(bounds->min.x, p.x);
        bounds->min.y = MIN(bounds->min.y, p.y);
        bounds->min.z = MIN(bounds->min.z, p.z);
        bounds->max.x = MAX(bounds->max.x, p.x);
        bounds->max.y = MAX(bounds->max.y, p.y);
        bounds->max.z = MAX(bounds->max.z, p.z);
    }
    return true;
}


static BOOL M2_LoadSkinData(LPCSTR modelFilename,
                            LPBYTE *skin_data,
                            DWORD *skin_size,
                            PATHSTR skin_path) {
    int nread;

    if (!skin_data || !skin_size || !M2_SkinPath(modelFilename, skin_path, sizeof(PATHSTR))) {
        return false;
    }

    nread = ri.FS_ReadFile(skin_path, (void **)skin_data);
    if (nread <= 0 && m2_path_has_extension(modelFilename, ".mdx")) {
        PATHSTR m2_path;
        if (m2_copy_with_extension(modelFilename, ".m2", m2_path, sizeof(m2_path)) &&
            M2_SkinPath(m2_path, skin_path, sizeof(PATHSTR))) {
            nread = ri.FS_ReadFile(skin_path, (void **)skin_data);
        }
    }
    if (nread <= 0 || !*skin_data) {
        *skin_size = 0;
        return false;
    }
    *skin_size = (DWORD)nread;
    return true;
}

static BOOL M2_InitLegacyGeometry(BYTE const *m2_base,
                                  DWORD m2_size,
                                  m2GeometryInfo_t *geom,
                                  m2LegacyView_t const **view) {
    m2HeaderLegacy_t const *legacy;

    if (!m2_base || m2_size < sizeof(*legacy) || !geom || !view) {
        return false;
    }

    legacy = (m2HeaderLegacy_t const *)m2_base;
    *geom = (m2GeometryInfo_t){
        .vertices = legacy->vertices,
        .textures = legacy->textures,
        .texture_lookup_table = legacy->texture_lookup_table,
        .bounding_box = legacy->bounding_box,
    };
    *view = m2_array_ptr(m2_base, m2_size, legacy->views, sizeof(**view));
    return *view != NULL;
}

static BOOL M2_InitModernGeometry(BYTE const *m2_base,
                                  DWORD m2_size,
                                  m2GeometryInfo_t *geom,
                                  m2Header_t const **header) {
    m2Header_t const *modern;

    if (!m2_base || m2_size < sizeof(*modern) || !geom || !header) {
        return false;
    }

    modern = (m2Header_t const *)m2_base;
    *geom = (m2GeometryInfo_t){
        .vertices = modern->vertices,
        .textures = modern->textures,
        .texture_lookup_table = modern->texture_lookup_table,
        .bounding_box = modern->bounding_box,
    };
    *header = modern;
    return true;
}


static BOOL M2_IsCharacterModelPath(LPCSTR model_path) {
    LPCSTR character;
    LPCSTR race_end;
    LPCSTR gender;
    size_t gender_len;

    if (!model_path) {
        return false;
    }

    character = strcasestr(model_path, "Character\\");
    if (!character) {
        character = strcasestr(model_path, "Character/");
    }
    if (!character) {
        return false;
    }

    race_end = strpbrk(character + strlen("Character\\"), "\\/");
    if (!race_end || !race_end[1]) {
        return false;
    }

    gender = race_end + 1;
    gender_len = strcspn(gender, "\\/.");
    return (gender_len == 4 && !strncasecmp(gender, "Male", 4)) ||
           (gender_len == 6 && !strncasecmp(gender, "Female", 6));
}

/* Geoset group → section ID scheme (wowdev Character_Customization):
 *   section_id = group * 100 + variant, variant 0 → DNE (nothing shown).
 *   Groups 5 and 13 need a fallback scan because some items request a
 *   variant absent from certain race/gender models (retail fallback behavior).
 *   All other groups are pure GROUP*100+variant arithmetic. */
static BOOL M2_CharacterGeosetVisible(m2Model_t const *model,
                                       LPCM2CHARACTEROUTFIT outfit,
                                       WORD section_id) {
    DWORD group, geoset, n;
    WORD available[64];
    m2ModelBatch_t const *b;
    if (!model || !(model->flags & M2_MODEL_CHARACTER)) return true;
    if (section_id < 400) {
        /* Section 0 is the base skin; the head/hair (1–99) and facial geosets
         * (100–399) are hidden per the worn helmet's race-resolved
         * HelmetGeosetVisData mask (wowdev geoset groups 0–3). */
        if (section_id > 0 && outfit && (outfit->helm_hide & (1u << (section_id / 100)))) return false;
        return true;
    }
    if (!outfit)
        return section_id == 401 || section_id == 702 || section_id == 1501;

    group  = section_id / 100;
    geoset = (group < M2_NUM_GEOSET_GROUPS) ? outfit->geoset[group] : 0;

    if (group == 5 || group == 13) {
        if (group == 13 && (outfit->flags & M2_CHAR_FLAG_KNEELENGTH)) return false;
        for (b = model->batches, n = 0; b && n < 64; b = b->next) available[n++] = b->section_id;
        if (group == 13)
            return section_id == Wow_CharacterGeosetPick(available, n, 13, (WORD)(1301 + geoset), 1301);
        return section_id == Wow_CharacterGeosetPick(available, n, 5, (WORD)(501 + geoset), 501);
    }
    switch (group) {
        /* Groups 4/7/8/10/11/12/15 use base+1 offset: geoset=0 selects the bare/default
         * variant (forearms, ears, sleeves, eyes, brows, hair, no-cape) which exists in
         * every shipped model. Group 7 keeps its bare-ears ternary until M2_DbcStartOutfit
         * seeds geoset[7]=2 explicitly — TODO.
         * geoset[9] is driven by geosetGroup[1] of the equipped pants item. */
        case 4:  return section_id == (WORD)(401 + geoset);
        case 7:  return outfit->helm_hide & M2_HELM_HIDE_EARS ? false : section_id == (geoset ? (WORD)(700 + geoset) : 702);
        case 8:  return section_id == (WORD)(801 + geoset);
        /* Group 9 (legs/kneepads): 901 is DNE (bare), 902 long, 903 short
         * (wowdev "09**: Legs {1: none, 2: long, 3: short}"; the decompiled
         * GeosRenderPrep uses 901 + geosetGroup[1]). */
        case 9:  return section_id == (WORD)(901 + geoset);
        case 10: return section_id == (WORD)(1001 + geoset);
        case 11: return section_id == (WORD)(1101 + geoset);
        /* Tabard mesh: 1201 is DNE ("no tabard"); 1202 is the worn tabard flap. */
        case 12: return section_id == (WORD)(1200 + geoset);
        case 15: return section_id == (WORD)(1501 + geoset);
        default: return false;
    }
}

static void M2_FreeModelData(m2Model_t *model) {
	if (!model || !model->file_image) return;
	ri.MemFree(model->file_image);
	model->file = NULL; model->file_image = NULL; model->file_image_size = 0;
    model->base_offset = 0; model->file_size = 0;
}

/* Keep the validated M2 as a typed view into the owned source image. */
static BOOL M2_LoadFileImage(m2Model_t *model, BYTE *file_image, DWORD file_image_size,
                             DWORD base_offset, DWORD m2_size) {
    m2Array_t bones, particles, ribbons;
    DWORD version;

    BYTE const *m2_base;
    if (!model || !file_image || base_offset > file_image_size || m2_size < sizeof(DWORD) * 2 ||
        m2_size > file_image_size - base_offset) {
        SAFE_DELETE(file_image, ri.FS_FreeFile);
        return false;
    }
    m2_base = file_image + base_offset;
    version = m2_read32(m2_base + sizeof(DWORD));
    model->format = m2_format_def(version);
    if ((model->format->format == M2_FORMAT_CLASSIC && m2_size < sizeof(m2HeaderLegacy_t)) ||
        (model->format->format == M2_FORMAT_MODERN && m2_size < sizeof(m2Header_t))) {
        ri.FS_FreeFile(file_image);
        return false;
    }
    model->file_image = file_image;
    model->file_image_size = file_image_size;
    model->base_offset = base_offset;
    model->file = (m2File_t *)(file_image + base_offset);
    model->file_size = m2_size;
    bones = M2_BonesArray(model);
    particles = M2_ParticlesArray(model);
    ribbons = M2_RibbonsArray(model);
    if ((bones.size && !M2_Bones(model)) ||
        (particles.size && !M2_ModelArrayPtr(model, particles, model->format->particle_stride)) ||
        (ribbons.size && !M2_ModelArrayPtr(model, ribbons, model->format->ribbon_stride))) {
        fprintf(stderr, "M2: file array extends beyond model data\n");
        M2_FreeModelData(model);
        return false;
    }
    if ((DWORD)bones.size > M2_MAX_BONES) {
        fprintf(stderr, "M2: %s has %d bones; renderer limit is %u\n", model->filename, bones.size, M2_MAX_BONES);
        M2_FreeModelData(model);
        return false;
    }
    return true;
}

m2Model_t *R_LoadModelM2(LPCSTR modelFilename, void *buffer, DWORD size, BOOL *buffer_owned) {
    m2GeometryInfo_t geom;
    BYTE const *m2_base = buffer;
    DWORD m2_size = size;
    m2VertexDisk_t const *m2_vertices;
    LPBYTE skin_data = NULL;
    DWORD skin_size = 0;
    PATHSTR skin_path;
    m2SkinHeader_t const *skin;
    m2LegacyView_t const *legacy_view = NULL;
    WORD const *skin_vertices;
    WORD const *skin_indices;
    void const *sections;
    m2Batch_t const *batches;
    m2Ubyte4_t const *skin_bones;
    m2Model_t *model;
    DWORD batch_count;
    DWORD section_count;
    DWORD skin_vertex_count;
    DWORD skin_index_count;
    BOOL using_legacy_view = false;
    WORD const *materials = NULL;
    DWORD material_count = 0;
    DWORD version;
    m2FormatDef_t const *format;
    DWORD base_offset = 0;

    if (buffer_owned) *buffer_owned = false;

    if (!buffer || size < sizeof(DWORD)) {
        return M2_CreateFallbackModel(modelFilename, "missing model data");
    }

    if (*(DWORD *)buffer == ID_MD21) {
        m2_base = m2_find_chunk(buffer, size, ID_MD21, &m2_size);
        if (!m2_base) {
            m2_base = buffer;
            m2_size = size;
        }
    } else if (*(DWORD *)buffer == ID_12DM) {
        m2_base = m2_find_chunk(buffer, size, ID_12DM, &m2_size);
    }
    if (m2_base) base_offset = (DWORD)(m2_base - (BYTE *)buffer);
    if (!m2_base || m2_size < sizeof(DWORD) * 2) {
        return M2_CreateFallbackModel(modelFilename, "truncated header");
    }

    if (*(DWORD *)m2_base != ID_MD20) {
        return M2_CreateFallbackModel(modelFilename, "bad MD20 header");
    }

    version = m2_read32(m2_base + sizeof(DWORD));
    format = m2_format_def(version);
    skin_vertices = NULL; skin_indices = NULL; skin_bones = NULL;
    sections = NULL; batches = NULL;
    batch_count = section_count = skin_vertex_count = skin_index_count = 0;
    if (format->format == M2_FORMAT_CLASSIC) {
        if (!M2_InitLegacyGeometry(m2_base, m2_size, &geom, &legacy_view))
            return M2_CreateFallbackModel(modelFilename, "invalid classic header or embedded view");
        skin_vertices = m2_array_ptr(m2_base, m2_size, legacy_view->vertices, sizeof(*skin_vertices));
        skin_indices = m2_array_ptr(m2_base, m2_size, legacy_view->indices, sizeof(*skin_indices));
        skin_bones = m2_array_ptr(m2_base, m2_size, legacy_view->bones, sizeof(*skin_bones));
        sections = m2_array_ptr(m2_base, m2_size, legacy_view->sections, sizeof(m2SkinSectionLegacy_t));
        batches = m2_array_ptr(m2_base, m2_size, legacy_view->batches, sizeof(*batches));
        batch_count = (DWORD)legacy_view->batches.size; section_count = (DWORD)legacy_view->sections.size;
        skin_vertex_count = (DWORD)legacy_view->vertices.size; skin_index_count = (DWORD)legacy_view->indices.size;
        using_legacy_view = true;
    } else {
        m2Header_t const *modern_header;
        if (!M2_InitModernGeometry(m2_base, m2_size, &geom, &modern_header))
            return M2_CreateFallbackModel(modelFilename, "invalid modern header");
        if (M2_LoadSkinData(modelFilename, &skin_data, &skin_size, skin_path) && skin_size >= sizeof(*skin)) {
            skin = (m2SkinHeader_t const *)skin_data;
            if (skin->magic == MAKEFOURCC('S', 'K', 'I', 'N')) {
                skin_vertices = m2_array_ptr(skin_data, skin_size, skin->vertices, sizeof(*skin_vertices));
                skin_indices = m2_array_ptr(skin_data, skin_size, skin->indices, sizeof(*skin_indices));
                skin_bones = m2_array_ptr(skin_data, skin_size, skin->bones, sizeof(*skin_bones));
                sections = m2_array_ptr(skin_data, skin_size, skin->sections, sizeof(m2SkinSection_t));
                batches = m2_array_ptr(skin_data, skin_size, skin->batches, sizeof(*batches));
                batch_count = (DWORD)skin->batches.size; section_count = (DWORD)skin->sections.size;
                skin_vertex_count = (DWORD)skin->vertices.size; skin_index_count = (DWORD)skin->indices.size;
            } else {
                M2_LogFallback(modelFilename, "bad skin magic");
                ri.FS_FreeFile(skin_data); skin_data = NULL;
            }
        } else if (skin_data) {
            ri.FS_FreeFile(skin_data); skin_data = NULL;
        }
    }

    m2_vertices = m2_array_ptr(m2_base, m2_size, geom.vertices, sizeof(*m2_vertices));
    if (!m2_vertices || !skin_vertices || !skin_indices || !sections || !batches) {
        SAFE_DELETE(skin_data, ri.FS_FreeFile);
        return M2_CreateFallbackModel(modelFilename, using_legacy_view ? "invalid legacy embedded view" : "missing skin profile");
    }

    model = ri.MemAlloc(sizeof(*model));
    memset(model, 0, sizeof(*model));
    snprintf(model->filename, sizeof(model->filename), "%s", modelFilename ? modelFilename : "");
    model->bounds = (BOX3){ geom.bounding_box.min, geom.bounding_box.max };
    if (!M2_CalculateGeometryBounds(m2_vertices, (DWORD)geom.vertices.size, &model->geometry_bounds))
        model->geometry_bounds = model->bounds;
    if (M2_IsCharacterModelPath(modelFilename)) model->flags |= M2_MODEL_CHARACTER;
    if (buffer_owned) *buffer_owned = true;
    if (!M2_LoadFileImage(model, (BYTE *)buffer, size, base_offset, m2_size)) {
        SAFE_DELETE(skin_data, ri.FS_FreeFile);
        M2_FreeModelData(model);
        ri.MemFree(model);
        return M2_CreateFallbackModel(modelFilename, "failed to copy animation data");
    }

	/* Vanilla stores the same material records in render_flags, at a different header offset. */
	{
        m2Array_t material_array = m2_material_array(model->file->modern.materials, model->file->classic.render_flags, model->format->format == M2_FORMAT_CLASSIC);

		if (material_array.size > 0) {
			BYTE const *materials_data = M2_ModelArrayPtr(model, material_array, 4);
			materials = (WORD const *)materials_data;
			material_count = materials_data ? (DWORD)material_array.size : 0;
		}
	}

	FOR_LOOP(i, batch_count) {
		m2Batch_t const *batch = &batches[i];
        DWORD index_start;
        DWORD index_count;
        WORD bone_count;
        WORD bone_combo_index;
        WORD section_id = 0;
        VERTEX *verts;
        m2ModelBatch_t *out;
        BYTE alphamode;
        if (batch->skin_section_index >= section_count) {
            continue;
        }
        if (using_legacy_view) {
            m2SkinSectionLegacy_t const *section = &((m2SkinSectionLegacy_t const *)sections)[batch->skin_section_index];
            section_id = section->skin_section_id;
            index_start = section->index_start;
            index_count = section->index_count;
            bone_count = section->bone_count;
            bone_combo_index = section->bone_combo_index;
        } else {
            m2SkinSection_t const *section = &((m2SkinSection_t const *)sections)[batch->skin_section_index];
            section_id = section->skin_section_id;
            index_start = section->index_start;
            index_count = section->index_count;
            bone_count = section->bone_count;
            bone_combo_index = section->bone_combo_index;
        }
        if (index_count == 0) {
            continue;
        }
        if (!m2_validate_skin_vertex_range(skin_vertices, skin_vertex_count, skin_indices, skin_index_count, (DWORD)geom.vertices.size, index_start, index_count)) {
            fprintf(stderr, "M2: section %u has an invalid skin index/vertex range\n", section_id);
            continue;
        }
        verts = ri.MemAlloc(sizeof(*verts) * index_count);
        if (!verts) {
            continue;
        }
        FOR_LOOP(j, index_count) {
            DWORD sidx = skin_indices[index_start + j];
            DWORD vidx = skin_vertices[sidx];
            verts[j] = M2_MakeVertex(&m2_vertices[vidx]);
            if (skin_bones) {
                memcpy(verts[j].skin, skin_bones[sidx].v, sizeof(skin_bones[sidx].v));
            }
        }
        alphamode = batch->material_index < material_count ? m2_blend_mode(materials[batch->material_index * 2 + 1]) : 0;
        out = ri.MemAlloc(sizeof(*out));
        memset(out, 0, sizeof(*out));
        out->buffer = R_MakeVertexArrayObject(verts, index_count);
        out->texture = M2_TextureForBatch(m2_base, m2_size, &geom, batch, modelFilename, true, &out->texture_type);
        out->num_vertices = index_count;
        out->bone_count = bone_count;
        out->bone_combo_index = bone_combo_index;
        out->section_id = section_id;
        out->geoset_index = batch->geoset_index;
        out->character_texture_slot = M2_CharacterTextureSlotForSection(section_id);
        out->alphamode = alphamode;
        ADD_TO_LIST(out, model->batches);
        model->num_batches++;
        ri.MemFree(verts);
    }

    SAFE_DELETE(skin_data, ri.FS_FreeFile);
    if (!model->batches) {
        M2_FreeModelData(model);
        ri.MemFree(model);
        return M2_CreateFallbackModel(modelFilename, using_legacy_view ? "legacy view produced no batches" : "skin produced no batches");
    }
    return model;
}

static BOOL M2_CharacterTextureModified(LPCM2CHARACTEROUTFIT outfit, DWORD appearance) {
    wowAppearance_t unpacked = Wow_UnpackAppearance(appearance);

    if (unpacked.faceID || unpacked.facialHairStyleID) return true;
    if (!outfit) return false;
    FOR_LOOP(priority, M2_CHAR_TEX_PRIORITIES)
        FOR_LOOP(slot, M2_CHAR_TEX_COMPONENT_COUNT)
            if (outfit->texture[slot][priority] && *outfit->texture[slot][priority]) return true;
    return false;
}

static void M2_DrawCompositeQuad(LPTEXTURE texture, LPCRECT screen, BOOL blend) {
    VERTEX vertices[6];
    MATRIX4 projection;
    MATRIX4 identity;
    RECT uv = { 0, 0, 1, 1 };

    R_AddQuad(vertices, screen, &uv, COLOR32_WHITE, 0);
    Matrix4_identity(&identity);
    Matrix4_ortho(&projection, 0, M2_CHARACTER_COMPOSITE_RESOLUTION, 0, M2_CHARACTER_COMPOSITE_RESOLUTION, 0, 100);
    R_Call(glUseProgram, tr.shader[SHADER_UI]->progid);
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    R_Call(glUniformMatrix4fv, tr.shader[SHADER_UI]->uViewProjectionMatrix, 1, GL_FALSE, projection.v);
    R_Call(glUniformMatrix4fv, tr.shader[SHADER_UI]->uModelMatrix, 1, GL_FALSE, identity.v);
    R_BindTexture(texture, 0);
    if (blend) {
        R_Call(glEnable, GL_BLEND);
        R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        R_Call(glDisable, GL_BLEND);
    }
    R_StatsDraw(GL_TRIANGLES, 6, 1);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, 6);
}

static void M2_DrawCompositeComponent(LPCSTR stem, BYTE slot, LPCSTR model_path,
                                      DWORD x, DWORD y, DWORD w, DWORD h) {
    if (stem && *stem) {
        PATHSTR resolved;
        LPTEXTURE texture;

        if (!M2_CharacterComponentTexturePath(stem, slot, model_path, resolved, sizeof(resolved))) return;
        texture = R_LoadTexture(resolved);
        if (texture) M2_DrawCompositeQuad(texture, &(RECT){ x, y, w, h }, true);
    }
}

static void M2_DrawCompositeHeadVariation(LPCSTR model_path, DWORD section_id,
                                          DWORD variation_index, DWORD color_index) {
    static DWORD const rects[2][4] = { { 0, 160, 128, 32 }, { 0, 192, 128, 64 } };
    static DWORD const texture_indices[2] = { 1, 0 };

    FOR_LOOP(i, 2) {
        PATHSTR path;
        LPTEXTURE texture;

        if (!M2_DbcCharacterVariationTexturePath(model_path, section_id, variation_index, color_index, texture_indices[i], path, sizeof(path))) continue;
        texture = R_LoadTexture(path);
        if (texture) M2_DrawCompositeQuad(texture, &(RECT){ rects[i][0], rects[i][1], rects[i][2], rects[i][3] }, true);
    }
}

static LPTEXTURE M2_PrepareCharacterTexture(m2Model_t const *model,
                                            renderEntity_t const *entity,
                                            LPCM2CHARACTEROUTFIT outfit) {
    static DWORD const rects[M2_CHAR_TEX_COMPONENT_COUNT][4] = {
        { 0, 0, 128, 64 }, { 0, 64, 128, 64 }, { 0, 128, 128, 32 },
        { 128, 0, 128, 64 }, { 128, 64, 128, 32 }, { 128, 96, 128, 64 },
        { 128, 160, 128, 64 }, { 128, 224, 128, 32 }
    };
    PATHSTR base_path;
    LPTEXTURE base;
    GLint old_framebuffer;
    GLint old_viewport[4];
    GLboolean old_depth, old_cull, old_blend;
    GLboolean old_scissor;
    GLint old_scissor_box[4];

    if (!model || !entity || !m2_character_composite_target ||
        !M2_CharacterTextureModified(outfit, entity->appearance)) return NULL;
    if (!M2_DbcCharacterTexturePathForType(model->filename, entity->appearance, 1, base_path, sizeof(base_path)))
        return NULL;
    base = R_LoadTexture(base_path);
    if (!base) return NULL;

    R_Call(glGetIntegerv, GL_DRAW_FRAMEBUFFER_BINDING, &old_framebuffer);
    R_Call(glGetIntegerv, GL_VIEWPORT, old_viewport);
    old_depth = R_Call(glIsEnabled, GL_DEPTH_TEST);
    old_cull = R_Call(glIsEnabled, GL_CULL_FACE);
    old_blend = R_Call(glIsEnabled, GL_BLEND);
    old_scissor = R_Call(glIsEnabled, GL_SCISSOR_TEST);
    R_Call(glGetIntegerv, GL_SCISSOR_BOX, old_scissor_box);
    R_Call(glBindFramebuffer, GL_FRAMEBUFFER, m2_character_composite_target->buffer);
    R_Call(glViewport, 0, 0, M2_CHARACTER_COMPOSITE_RESOLUTION, M2_CHARACTER_COMPOSITE_RESOLUTION);
    /* The view scissor is in window coordinates; it would clip this 256x256 target. */
    R_Call(glDisable, GL_SCISSOR_TEST);
    R_Call(glDisable, GL_DEPTH_TEST);
    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glClearColor, 0, 0, 0, 0);
    R_Call(glClear, GL_COLOR_BUFFER_BIT);
    M2_DrawCompositeQuad(base, &(RECT){ 0, 0, M2_CHARACTER_COMPOSITE_RESOLUTION, M2_CHARACTER_COMPOSITE_RESOLUTION }, false);
    FOR_LOOP(priority, M2_CHAR_TEX_PRIORITIES)
        FOR_LOOP(slot, M2_CHAR_TEX_COMPONENT_COUNT)
            if (outfit && outfit->texture[slot][priority]) {
                M2_DrawCompositeComponent(outfit->texture[slot][priority], (BYTE)slot, model->filename, rects[slot][0], rects[slot][1], rects[slot][2], rects[slot][3]);
            }
    {
        wowAppearance_t unpacked = Wow_UnpackAppearance(entity->appearance);
        M2_DrawCompositeHeadVariation(model->filename, 1, unpacked.faceID, unpacked.skinColorID);
        M2_DrawCompositeHeadVariation(model->filename, 2, unpacked.facialHairStyleID, unpacked.hairColorID);
    }
    R_Call(glBindFramebuffer, GL_FRAMEBUFFER, old_framebuffer);
    R_Call(glViewport, old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);
    if (old_depth) { R_Call(glEnable, GL_DEPTH_TEST); } else { R_Call(glDisable, GL_DEPTH_TEST); }
    if (old_cull) { R_Call(glEnable, GL_CULL_FACE); } else { R_Call(glDisable, GL_CULL_FACE); }
    if (old_blend) { R_Call(glEnable, GL_BLEND); } else { R_Call(glDisable, GL_BLEND); }
    R_Call(glScissor, old_scissor_box[0], old_scissor_box[1], old_scissor_box[2], old_scissor_box[3]);
    if (old_scissor) { R_Call(glEnable, GL_SCISSOR_TEST); } else { R_Call(glDisable, GL_SCISSOR_TEST); }
    m2_character_composite_texture.texid = m2_character_composite_target->texture;
    m2_character_composite_texture.width = M2_CHARACTER_COMPOSITE_RESOLUTION;
    m2_character_composite_texture.height = M2_CHARACTER_COMPOSITE_RESOLUTION;
    return &m2_character_composite_texture;
}

static LPTEXTURE M2_CharacterTextureForBatch(m2Model_t const *model,
                                             renderEntity_t const *entity,
                                             m2ModelBatch_t *batch) {
    PATHSTR texture_path;

    if (!model || !entity || !batch || !(model->flags & M2_MODEL_CHARACTER))
        return batch ? batch->texture : tr.texture[TEX_WHITE];
    if (M2_DbcCharacterTexturePathForType(model->filename, entity->appearance, batch->texture_type, texture_path, sizeof(texture_path)))
        return R_LoadTexture(texture_path);
    return batch->texture;
}

BOOL M2_AttachmentMatrix(m2Model_t const *model, DWORD attachment_id, LPCMATRIX4 model_matrix, LPMATRIX4 out);
void M2_RenderModel(renderEntity_t const *entity, m2Model_t const *model, LPCMATRIX4 transform);

/* Race abbreviation used by Item\ObjectComponents\Head\<name>_<race><gender>.m2. */
static LPCSTR M2_RaceCode(DWORD race_id) {
    switch (race_id) {
        case 1: return "Hu"; case 2: return "Or"; case 3: return "Dw"; case 4: return "Ni";
        case 5: return "Sc"; case 6: return "Ta"; case 7: return "Gn"; case 8: return "Tr";
        default: return NULL;
    }
}

/* Resolve an ItemDisplayInfo model-name stem to its archive path. Helmets are
 * per-race/gender; shoulders are shared across races. */
static BOOL M2_ItemAttachmentPath(LPCSTR character_path, LPCSTR model_name, BOOL helm, LPSTR out, DWORD out_size) {
    DWORD race_id, gender_id;
    LPCSTR code;
    size_t stem;
    if (!out || !out_size || !model_name || !*model_name) return false;
    stem = strlen(model_name);
    if (stem > 4 && (!strcasecmp(model_name + stem - 4, ".mdx") || !strcasecmp(model_name + stem - 4, ".mdl"))) stem -= 4;
    if (helm) {
        if (!M2_DbcCharacterRaceGender(character_path, &race_id, &gender_id) || !(code = M2_RaceCode(race_id))) return false;
        snprintf(out, out_size, "Item\\ObjectComponents\\Head\\%.*s_%s%c.m2", (int)stem, model_name, code, gender_id ? 'F' : 'M');
    } else {
        snprintf(out, out_size, "Item\\ObjectComponents\\Shoulder\\%.*s.m2", (int)stem, model_name);
    }
    return true;
}

/* Item attachment models are loaded once per path and kept resident by their
 * single R_LoadModel reference; the per-frame path would otherwise leak refs. */
static LPCMODEL M2_ItemModel(LPCSTR path) {
    static PATHSTR cached_path[64];
    static LPCMODEL cached_model[64];
    DWORD free_slot = 64;
    if (!path || !*path) return NULL;
    FOR_LOOP(i, 64) {
        if (cached_model[i] && !strcasecmp(cached_path[i], path)) return cached_model[i];
        if (!cached_model[i] && free_slot == 64) free_slot = i;
    }
    if (free_slot == 64) return NULL;
    cached_model[free_slot] = R_LoadModel(path);
    snprintf(cached_path[free_slot], sizeof(cached_path[0]), "%s", path);
    return cached_model[free_slot];
}

/* Resolve an ItemDisplayInfo model-texture stem (field 3/4) to its archive path.
 * Item models use a replaceable object-skin texture (M2 texture type 2) that the
 * client fills in from this stem: Item\ObjectComponents\{Head,Shoulder}\<stem>.blp. */
static BOOL M2_ItemTexturePath(LPCSTR texture_name, BOOL helm, LPSTR out, DWORD out_size) {
    size_t stem;
    if (!out || !out_size || !texture_name || !*texture_name) return false;
    stem = strlen(texture_name);
    if (stem > 4 && (!strcasecmp(texture_name + stem - 4, ".blp") || !strcasecmp(texture_name + stem - 4, ".tga"))) stem -= 4;
    snprintf(out, out_size, "Item\\ObjectComponents\\%s\\%.*s.blp", helm ? "Head" : "Shoulder", (int)stem, texture_name);
    return true;
}

/* Render the helmet and shoulder attachment M2s carried by a resolved character
 * outfit. Attachment matrices are computed before any recursive render so the
 * parent's bone scratch stays intact. The item's model texture (ItemDisplayInfo
 * field 3/4) overrides the attachment's replaceable object skin. */
static void M2_RenderItemAttachments(renderEntity_t const *entity, m2Model_t const *model,
                                     LPCMATRIX4 transform, LPCM2CHARACTEROUTFIT outfit) {
    static DWORD const ids[3] = { 11, 6, 5 }; /* helm, shoulder-left, shoulder-right */
    LPCMODEL models[3] = { NULL, NULL, NULL };
    LPTEXTURE textures[3] = { NULL, NULL, NULL };
    MATRIX4 matrices[3];
    BOOL valid[3] = { false, false, false };
    PATHSTR path;
    if (!entity || !model || !transform || !outfit) return;
    FOR_LOOP(i, 3) {
        LPCSTR name = i == 0 ? outfit->helm_model : outfit->shoulder_model[i - 1];
        LPCSTR texname = i == 0 ? outfit->helm_texture : outfit->shoulder_texture[i - 1];
        if (!name || !*name) continue;
        if (!M2_ItemAttachmentPath(model->filename, name, i == 0, path, sizeof(path))) continue;
        models[i] = M2_ItemModel(path);
        if (!models[i] || models[i]->modeltype != ID_MD20) continue;
        valid[i] = M2_AttachmentMatrix(model, ids[i], transform, &matrices[i]);
        if (texname && *texname && M2_ItemTexturePath(texname, i == 0, path, sizeof(path)))
            textures[i] = R_LoadTexture(path);
    }
    FOR_LOOP(i, 3) {
        renderEntity_t ae;
        if (!valid[i]) continue;
        ae = *entity;
        ae.model = models[i];
        ae.attached_model = NULL;
        ae.skin = textures[i];
        ae.flags &= ~RF_GROUND_ANCHOR;
        M2_RenderModel(&ae, models[i]->m2, &matrices[i]);
    }
}

/* Keep ordinary and instanced M2 batches on the same material-state contract. */
static void M2_SetBlendMode(LPSHADER shader, DWORD mode) {
    R_Call(glUniform1i, shader->uAlphaKey, mode == BLEND_MODE_ALPHAKEY);
    R_SetAlphaKeyState(mode == BLEND_MODE_ALPHAKEY);
    if (mode == BLEND_MODE_NONE) {
        R_Call(glDisable, GL_BLEND); R_Call(glDepthMask, GL_TRUE);
    } else if (mode != BLEND_MODE_ALPHAKEY) {
        R_Call(glEnable, GL_BLEND); R_Call(glDepthMask, GL_FALSE);
        switch (mode) {
        case BLEND_MODE_ADD: R_Call(glBlendFunc, GL_ONE, GL_ONE); break;
        case BLEND_MODE_ADDALPHA: R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE); break;
        default: R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
        }
    }
}

void M2_RenderModel(renderEntity_t const *entity, m2Model_t const *model, LPCMATRIX4 transform) {
    renderEntity_t resolved_entity;
    renderEntity_t const *draw_entity = entity;
    M2CREATUREAPPEARANCE creature = { 0 };
    LPCM2CREATUREAPPEARANCE creature_ptr = NULL;
    MATRIX3 normal_matrix;
    M2CHARACTEROUTFIT outfit_data;
    LPCM2CHARACTEROUTFIT outfit = NULL;
    LPTEXTURE character_texture = NULL;
    m2ModelBatch_t *batch;
    LPSHADER shader;
    BOOL ground_effect;
    FLOAT ground_alpha = 1.0f;

    if (!entity || !model || !transform) {
        return;
    }
    if (!Frustum_ContainsBox(&tr.viewDef.frustum, &model->bounds, transform)) return;

    ground_effect = entity->flags & RF_GROUND_EFFECT;
    if (ground_effect) {
        VECTOR3 delta = Vector3_sub(&entity->origin, &tr.viewDef.camerastate[0].origin);
        FLOAT distance = Vector3_len(&delta);
        FLOAT fade_range = WOW_GRASS_DRAW_DISTANCE - WOW_GRASS_FADE_START_DISTANCE;
        ground_alpha = 1.0f - MAX(0.0f, MIN(1.0f, (distance - WOW_GRASS_FADE_START_DISTANCE) / fade_range));
    }

    if ((model->flags & M2_MODEL_CHARACTER) && M2_DbcResolveCreatureAppearance(entity->display_id, &creature)) {
        resolved_entity = *entity;
        resolved_entity.appearance = creature.appearance;
        draw_entity = &resolved_entity;
        creature_ptr = &creature;
    }

    shader = M2_Shader();
    M2_CalculateBoneMatrices(model, draw_entity);
    M2_DrawParticles(model, draw_entity, transform);
    M2_DrawRibbons(model, draw_entity, transform);
    if ((model->flags & M2_MODEL_CHARACTER) &&
        M2_DbcCharacterOutfit(model->filename, draw_entity->appearance, draw_entity->equipment, creature_ptr, &outfit_data))
        outfit = &outfit_data;
    character_texture = M2_PrepareCharacterTexture(model, draw_entity, outfit);
    Matrix3_normal(&normal_matrix, transform);
    R_Call(glUseProgram, shader->progid);
    R_Call(glUniform1i, shader->uLightCount, 0);
    R_Call(glUniformMatrix4fv, shader->uViewProjectionMatrix, 1, GL_FALSE, tr.viewDef.viewProjectionMatrix.v);
    R_Call(glUniformMatrix4fv, shader->uTextureMatrix, 1, GL_FALSE, tr.viewDef.textureMatrix.v);
    R_Call(glUniformMatrix4fv, shader->uModelMatrix, 1, GL_FALSE, transform->v);
    R_Call(glUniformMatrix4fv, shader->uLightMatrix, 1, GL_FALSE, tr.viewDef.lightMatrix.v);
    R_Call(glUniformMatrix3fv, shader->uNormalMatrix, 1, GL_TRUE, normal_matrix.v);
    {
        VECTOR3 light_dir;
        /* uLightDir points from the surface toward the sun; uLightAmbient +
           uLightColor * N.L is the unified shader's diffuse model. Colors are
           the classic no-DBC fallback (see ui_constants.h). */
        Wow_SunDirection(Wow_DayFraction(), &light_dir);
        R_Call(glUniform3f, shader->uLightDir, light_dir.x, light_dir.y, light_dir.z);
        R_Call(glUniform3f, shader->uLightColor, WOW_LIGHT_DIFFUSE_R, WOW_LIGHT_DIFFUSE_G, WOW_LIGHT_DIFFUSE_B);
        R_Call(glUniform3f, shader->uLightAmbient, WOW_LIGHT_AMBIENT_R, WOW_LIGHT_AMBIENT_G, WOW_LIGHT_AMBIENT_B);
    }
    /* The unified model shader transforms UVs through quat_transform using
     * uUvRot (default (0,0) collapses all UVs to 0.5).  Set identity defaults
     * for all UV/color/layer uniforms that M2 does not animate. */
    R_Call(glUniform4f, shader->uGeosetColor, 1.0f, 1.0f, 1.0f, ground_alpha);
    R_Call(glUniform1f, shader->uLayerAlpha, 1.0f);
    R_Call(glUniform2f, shader->uUvTrans, 0.0f, 0.0f);
    R_Call(glUniform2f, shader->uUvRot, 0.0f, 1.0f);  /* identity quaternion */
    R_Call(glUniform2f, shader->uUvScale, 1.0f, 1.0f);
    R_Call(glUniform1i, shader->uAlphaKey, 0);
    R_Call(glUniform1i, shader->uUnshaded, 0);
    R_Call(glUniform1f, shader->uFogEnable, tr.viewDef.fogEnable);
    R_Call(glUniform3f, shader->uFogColor, tr.viewDef.fogColor.x, tr.viewDef.fogColor.y, tr.viewDef.fogColor.z);
    R_Call(glUniform2f, shader->uFogParams, tr.viewDef.fogStart, tr.viewDef.fogEnd);
    R_Call(glUniform1f, shader->uFirstBoneLookupIndex, 0.0f);
    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_TRUE);
    R_Call(glDisable, GL_BLEND);

	for (batch = model->batches; batch; batch = batch->next) {
		LPCTEXTURE texture;

		if (!M2_CharacterGeosetVisible(model, outfit, batch->section_id)) {
			continue;
		}
		/* Alpha-key batches convert their remapped shader alpha into MSAA coverage. */
        M2_SetBlendMode(shader, batch->alphamode);
		texture = batch->texture_type == 1 && character_texture ? character_texture :
                  draw_entity->skin ? draw_entity->skin :
                  M2_CharacterTextureForBatch(model, draw_entity, batch);
		M2_UploadBatchBones(model, batch, shader);
		R_BindTexture(texture ? texture : tr.texture[TEX_WHITE], 0);
#ifdef USE_SHADOWMAPS
		R_BindTexture(tr.texture[TEX_SHADOWMAP], 1);
#endif
		R_BindTexture(tr.texture[TEX_WHITE], 2);
		R_DrawBuffer(batch->buffer, batch->num_vertices);
	}
	R_SetAlphaKeyState(false);
	if (outfit)
		M2_RenderItemAttachments(draw_entity, model, transform, outfit);
}

/* Static-mesh instanced path for ground-effect clutter. Renders `count` copies
   of the model in one draw call per batch. Classic detail M2s have no keyed bone
   tracks, so this path adds root-anchored wind in the vertex shader. */
void M2_RenderInstanced(m2Model_t const *model, LPCINSTANCEBUFFER instances, DWORD flags) {
    m2ModelBatch_t *batch;
    LPSHADER shader;

    if (!model || !instances || !instances->count) return;

    shader = R_ModelShaderInstanced();
    if (!shader) {
        static BOOL logged;
        if (!logged) {
            logged = true;
            fprintf(stderr, "M2_RenderInstanced: instanced shader failed to compile\n");
        }
        return;
    }
    R_Call(glUseProgram, shader->progid);
    R_Call(glUniform1i, shader->uLightCount, 0);
    R_Call(glUniformMatrix4fv, shader->uViewProjectionMatrix, 1, GL_FALSE, tr.viewDef.viewProjectionMatrix.v);
    R_Call(glUniformMatrix4fv, shader->uTextureMatrix, 1, GL_FALSE, tr.viewDef.textureMatrix.v);
    R_Call(glUniformMatrix4fv, shader->uLightMatrix, 1, GL_FALSE, tr.viewDef.lightMatrix.v);
    {
        VECTOR3 light_dir;
        Wow_SunDirection(Wow_DayFraction(), &light_dir);
        R_Call(glUniform3f, shader->uLightDir, light_dir.x, light_dir.y, light_dir.z);
        R_Call(glUniform3f, shader->uLightColor, WOW_LIGHT_DIFFUSE_R, WOW_LIGHT_DIFFUSE_G, WOW_LIGHT_DIFFUSE_B);
        R_Call(glUniform3f, shader->uLightAmbient, WOW_LIGHT_AMBIENT_R, WOW_LIGHT_AMBIENT_G, WOW_LIGHT_AMBIENT_B);
    }
    R_Call(glUniform4f, shader->uGeosetColor, 1.0f, 1.0f, 1.0f, 1.0f);
    R_Call(glUniform1f, shader->uLayerAlpha, 1.0f);
    R_Call(glUniform2f, shader->uUvTrans, 0.0f, 0.0f);
    R_Call(glUniform2f, shader->uUvRot, 0.0f, 1.0f);
    R_Call(glUniform2f, shader->uUvScale, 1.0f, 1.0f);
    R_Call(glUniform1i, shader->uAlphaKey, 0);
    R_Call(glUniform1i, shader->uUnshaded, 0);
    R_Call(glUniform1f, shader->uFogEnable, tr.viewDef.fogEnable);
    R_Call(glUniform3f, shader->uFogColor, tr.viewDef.fogColor.x, tr.viewDef.fogColor.y, tr.viewDef.fogColor.z);
    R_Call(glUniform2f, shader->uFogParams, tr.viewDef.fogStart, tr.viewDef.fogEnd);
    R_Call(glUniform1f, shader->uFirstBoneLookupIndex, 0.0f);
    {
        static GLuint cached_progid;
        static GLint loc_camera = -1, loc_fade = -1, loc_time = -1, loc_wind = -1, loc_phase = -1, loc_height = -1, loc_ground = -1;
        if (shader->progid != cached_progid) {
            cached_progid = shader->progid;
            loc_camera = glGetUniformLocation(shader->progid, "uGrassCameraPos");
            loc_fade = glGetUniformLocation(shader->progid, "uGrassFade");
            loc_time = glGetUniformLocation(shader->progid, "uGrassTime");
            loc_wind = glGetUniformLocation(shader->progid, "uGrassWind");
            loc_phase = glGetUniformLocation(shader->progid, "uGrassPhase");
            loc_height = glGetUniformLocation(shader->progid, "uGrassHeight");
            loc_ground = glGetUniformLocation(shader->progid, "uGroundEffect");
        }
        if (loc_ground >= 0) glUniform1i(loc_ground, flags & RF_GROUND_EFFECT ? 1 : 0);
        if (loc_camera >= 0) {
            VECTOR3 cam = tr.viewDef.camerastate[0].origin;
            glUniform3f(loc_camera, cam.x, cam.y, cam.z);
        }
        if (loc_fade >= 0) {
            glUniform2f(loc_fade, WOW_GRASS_FADE_START_DISTANCE, WOW_GRASS_DRAW_DISTANCE);
        }
        if (loc_time >= 0) glUniform1f(loc_time, tr.viewDef.time / 1000.0f);
        if (loc_wind >= 0) glUniform3f(loc_wind, WOW_GRASS_WIND_SPEED, WOW_GRASS_WIND_AMPLITUDE, WOW_GRASS_WIND_ROOT_FRACTION);
        if (loc_phase >= 0) glUniform4f(loc_phase, WOW_GRASS_WIND_PHASE_X, WOW_GRASS_WIND_PHASE_Y, WOW_GRASS_WIND_DIRECTION_X, WOW_GRASS_WIND_DIRECTION_Y);
        if (loc_height >= 0) glUniform2f(loc_height, model->geometry_bounds.min.z, model->geometry_bounds.max.z);
    }
    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_TRUE);
    R_Call(glDisable, GL_BLEND);

	for (batch = model->batches; batch; batch = batch->next) {
        M2_SetBlendMode(shader, batch->alphamode);
		R_BindTexture(batch->texture ? batch->texture : tr.texture[TEX_WHITE], 0);
#ifdef USE_SHADOWMAPS
		R_BindTexture(tr.texture[TEX_SHADOWMAP], 1);
#endif
		R_BindTexture(tr.texture[TEX_WHITE], 2);
		R_DrawBufferInstanced(batch->buffer, batch->num_vertices, instances);
	}
	R_SetAlphaKeyState(false);
}

BOOL M2_AttachmentMatrix(m2Model_t const *model,
                         DWORD attachment_id,
                         LPCMATRIX4 model_matrix,
                         LPMATRIX4 out) {
    BYTE const *attachments;
    WORD const *lookup;
    DWORD attachment_index = 0xFFFFu;
    WORD bone_index;
    VECTOR3 position;
    MATRIX4 local;
    m2Array_t attachment_array;
    m2Array_t lookup_array;

    if (!model || !model_matrix || !out || !M2_Bones(model)) return false;

    attachment_array = M2_AttachmentsArray(model);
    lookup_array = M2_AttachmentLookupArray(model);
    attachments = M2_ModelArrayPtr(model, attachment_array, model->format->attachment_stride);
    if (!attachments || attachment_array.size <= 0) return false;

    lookup = M2_ModelArrayPtr(model, lookup_array, sizeof(*lookup));
    if (lookup && attachment_id < (DWORD)lookup_array.size) {
        attachment_index = lookup[attachment_id];
    }
    if (attachment_index >= (DWORD)attachment_array.size) {
        FOR_LOOP(i, (DWORD)attachment_array.size) {
            DWORD id = model->format->format == M2_FORMAT_CLASSIC
                ? ((m2AttachmentClassic_t const *)(attachments + i * model->format->attachment_stride))->attachment_id
                : ((m2AttachmentModern_t const *)(attachments + i * model->format->attachment_stride))->attachment_id;
            if (id == attachment_id) {
                attachment_index = i;
                break;
            }
        }
    }
    if (attachment_index >= (DWORD)attachment_array.size) {
        return false;
    }

    if (model->format->format == M2_FORMAT_CLASSIC) {
        m2AttachmentClassic_t const *attachment = (m2AttachmentClassic_t const *)(attachments + attachment_index * model->format->attachment_stride);
        bone_index = attachment->bone_index;
        position = attachment->position;
    } else {
        m2AttachmentModern_t const *attachment = (m2AttachmentModern_t const *)(attachments + attachment_index * model->format->attachment_stride);
        bone_index = attachment->bone_index;
        position = attachment->position;
    }

    if (bone_index >= (DWORD)M2_BonesArray(model).size) {
        return false;
    }

    local = m2_bone_matrices[bone_index];
    Matrix4_translate(&local, &position);
    Matrix4_multiply(model_matrix, &local, out);
    return true;
}

FLOAT M2_GroundOffset(m2Model_t const *model) {
    if (!model || model->geometry_bounds.min.z >= 0.0f) {
        return 0.0f;
    }
    return -model->geometry_bounds.min.z;
}

/* Top of the model's animation-inclusive bounding box; anchors overhead sprites above the head. */
FLOAT M2_HeadHeight(m2Model_t const *model) {
    return model ? model->bounds.max.z : 0.0f;
}

BOOL M2_IsCharacterModel(m2Model_t const *model) { return model && (model->flags & M2_MODEL_CHARACTER); }

void M2_Release(m2Model_t *model) {
    m2ModelBatch_t *batch;

    if (!model) {
        return;
    }
    batch = model->batches;
    while (batch) {
        m2ModelBatch_t *next = batch->next;
        R_ReleaseVertexArrayObject(batch->buffer);
        ri.MemFree(batch);
        batch = next;
    }
    M2_FreeModelData(model);
    ri.MemFree(model);
}

void M2_Init(void) {
    m2_character_composite_target = R_AllocateRenderTexture(M2_CHARACTER_COMPOSITE_RESOLUTION, M2_CHARACTER_COMPOSITE_RESOLUTION, GL_RGBA, GL_UNSIGNED_BYTE, GL_COLOR_ATTACHMENT0);
}

void M2_Shutdown(void) {
    m2KnownTexture_t *known;
    R_ReleaseRenderTexture(m2_character_composite_target);
    m2_character_composite_target = NULL;
    while ((known = m2_known_textures) != NULL) {
        m2_known_textures = known->next;
        ri.MemFree(known);
    }
    M2_DbcShutdown();
}
