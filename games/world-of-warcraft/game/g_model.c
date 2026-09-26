#include "g_wow_local.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>

enum {
    ID_SEQS = MAKEFOURCC('S','E','Q','S'),
};

static uint32_t fnv1a32(cstring_t str) {
    uint32_t prime = 16777619;
    uint32_t hash  = 2166136261;
    while (*str) {
        hash = (hash ^ *str++) * prime;
    }
    return hash;
}

static void ConvertMDLXAnimationName(animation_t *seq) {
    char buffer[80];
    char *last_char = buffer;
    memset(buffer, 0, sizeof(buffer));
    strlcpy(buffer, seq->name, sizeof(buffer));
    for (char *ch = buffer; *ch; ch++) {
        if (isdigit(*ch) || *ch == '-') {
            while (*(++last_char)) {
                *last_char = '\0';
            }
            seq->syncpoint = fnv1a32(buffer);
            return;
        } else if (isalpha(*ch)) {
            *ch = tolower(*ch);
            last_char = ch;
        }
    }
    for (uint32_t i = (uint32_t)strlen(buffer) - 1; i > 0 && isspace(buffer[i]); i--) {
        buffer[i] = '\0';
    }
    seq->syncpoint = fnv1a32(buffer);
}

/* ---- MDLX (Warcraft III) ---- */

static animation_t *LoadModelMDLX(uint8_t const *data, uint32_t data_size, uint32_t *out_count) {
    uint32_t payloadSize = data_size > 4 ? data_size - 4 : 0;
    animation_t *animations = NULL;
    uint32_t num = 0;
    uint8_t const *ptr = data + 4;
    uint8_t const *end = ptr + payloadSize;

    while (ptr && ptr + 8 <= end) {
        uint32_t header, size;
        memcpy(&header, ptr, sizeof(uint32_t));
        memcpy(&size,   ptr + 4, sizeof(uint32_t));
        ptr += 8;
        if (ptr + size > end) {
            size = (uint32_t)(end - ptr);
        }
        if (header == ID_SEQS) {
            enum { SEQ_RECORD_SIZE = 132 }; /* on-disk mdxSequence_t, may differ from animation_t */
            num = size / SEQ_RECORD_SIZE;
            animations = gi.MemAlloc(sizeof(animation_t) * num);
            memset(animations, 0, sizeof(animation_t) * num);
            FOR_LOOP(i, num) {
                memcpy(animations + i, ptr + i * SEQ_RECORD_SIZE, SEQ_RECORD_SIZE);
                ConvertMDLXAnimationName(animations + i);
            }
        }
        ptr += size;
    }
    *out_count = num;
    return animations;
}

/* ---- MD34 (StarCraft II / SC2 M3) ---- */

typedef struct {
    uint32_t nEntries;
    uint32_t offset;
    uint32_t flags;
} md34Reference_t;

struct md33Header {
    uint32_t ofsRefs;
    uint32_t nRefs;
    md34Reference_t MODL;
};

struct md34ReferenceEntry {
    uint32_t id;
    uint32_t offset;
    uint32_t nEntries;
    uint32_t version;
};

struct md34NameRef {
    uint32_t nEntries;
    uint32_t ref;
    uint32_t flags;
};

struct md34Sequence {
    uint32_t unknown[2];
    struct md34NameRef name;
    uint32_t interval[2];
    float movementSpeed;
    uint32_t flags;
    uint32_t frequency;
    int32_t unk[3];
    int32_t unk2;
    struct { vector3_t min; vector3_t max; float radius; } boundingSphere;
    int32_t d5[3];
};

static uint8_t const *ModelDataAt(uint8_t const *data, uint32_t data_size, uint32_t offset, uint32_t size) {
    if (!data || offset > data_size || size > data_size - offset)
        return NULL;
    return data + offset;
}

static int compare_animation_name(void const *a, void const *b) {
    return strcmp(((animation_t const *)a)->name, ((animation_t const *)b)->name);
}

static animation_t *LoadModelMD34(uint8_t const *data, uint32_t data_size, uint32_t *out_count) {
    struct md33Header const *hdr = (struct md33Header const *)ModelDataAt(data, data_size, 4, sizeof(*hdr));
    struct md34ReferenceEntry const *ent;

    animation_t *animations = NULL;
    uint32_t num = 0;

    if (!hdr) {
        *out_count = 0;
        return NULL;
    }
    ent = (struct md34ReferenceEntry const *)ModelDataAt(data, data_size, hdr->ofsRefs, sizeof(struct md34ReferenceEntry) * hdr->nRefs);
    if (!ent) {
        *out_count = 0;
        return NULL;
    }

    FOR_LOOP(i, hdr->nRefs) {
        struct md34ReferenceEntry const *re = ent + i;
        if (re->id != MAKEFOURCC('S','Q','E','S'))
            continue;
        struct md34Sequence const *seq = (struct md34Sequence const *)ModelDataAt(data, data_size, re->offset, re->nEntries * sizeof(struct md34Sequence));
        if (!seq)
            continue;
        animations = gi.MemAlloc(sizeof(animation_t) * re->nEntries);
        memset(animations, 0, sizeof(animation_t) * re->nEntries);
        num = re->nEntries;
        uint32_t startanim = 0;
        FOR_LOOP(j, re->nEntries) {
            struct md34Sequence const *src = seq + j;
            char const *name = src->name.ref < hdr->nRefs
                ? (char const *)ModelDataAt(data, data_size, ent[src->name.ref].offset, src->name.nEntries)
                : NULL;
            animation_t *dest = animations + j;
            if (name) {
                uint32_t name_len = MIN(src->name.nEntries, sizeof(dest->name) - 1);
                memcpy(dest->name, name, name_len);
            }
            dest->interval[0] = startanim + src->interval[0];
            dest->interval[1] = startanim + src->interval[1];
            startanim += src->interval[1];
        }
        qsort(animations, num, sizeof(animation_t), compare_animation_name);
        FOR_LOOP(j, num) {
            ConvertMDLXAnimationName(animations + j);
        }
        break;
    }
    *out_count = num;
    return animations;
}

/* ---- M2 (World of Warcraft) ---- */

typedef struct {
    int32_t size;
    int32_t offset;
} svM2Array_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    svM2Array_t name;
    uint32_t flags;
    svM2Array_t global_loops;
    svM2Array_t sequences;
} svM2Header_t;

typedef struct {
    uint16_t  animation_id;
    uint16_t  sub_animation_id;
    uint32_t start_timestamp;
    uint32_t end_timestamp;
    float movement_speed;
    uint32_t flags;
    int16_t probability;
    uint16_t  padding;
    uint32_t minimum_repetitions;
    uint32_t maximum_repetitions;
    uint32_t blend_time;
    vector3_t min;
    vector3_t max;
    float radius;
    int16_t next_animation;
    uint16_t  alias_next;
} svM2SequenceClassic_t;

typedef struct {
    uint16_t  animation_id;
    uint16_t  sub_animation_id;
    uint32_t length;
    float movement_speed;
    uint32_t flags;
    uint32_t frequency;
    uint32_t minimum_repetitions;
    uint32_t maximum_repetitions;
    uint32_t blend_time;
    vector3_t min;
    vector3_t max;
    float radius;
    int16_t next_animation;
    uint16_t  alias_next;
} svM2SequenceModern_t;

/* M2 event FourCC identifiers for weapon hits (little-endian packed uint32) */
#define M2_EVENT_SWH  (*(uint32_t const *)"$SWH")   /* swing weapon hit (melee right) */
#define M2_EVENT_SHD  (*(uint32_t const *)"$SHD")   /* shield / off-hand hit */

/* M2TrackBase layout matches all track types (bone, event, etc.) — same struct in file.
 * Event tracks have the keys/values M2Arrays present but always zero-size.
 * Modern (Wrath+, version >= 264): sequence_times -> per-sequence M2Array<uint32_t>.
 * Classic (pre-Wrath): ranges -> M2Array<M2Range>; times -> flat M2Array<uint32_t>. */
typedef struct {
    uint16_t track_type;
    uint16_t loop_index;
    svM2Array_t sequence_times;
    svM2Array_t sequence_keys;
} svM2EventTrack_t;

typedef struct {
    uint16_t track_type;
    uint16_t loop_index;
    svM2Array_t ranges;
    svM2Array_t times;
} svM2EventTrackClassic_t;

typedef struct {
    svM2Array_t times;
} svM2SequenceTimes_t;

typedef struct {
    uint32_t start;
    uint32_t end;
} svM2Range_t;

typedef struct {
    uint32_t event_id;
    uint32_t data;
    uint16_t bone_index;
    uint16_t padding;
    vector3_t position;
    svM2EventTrack_t track;
} svM2EventModern_t;

typedef struct {
    uint32_t event_id;
    uint32_t data;
    uint16_t bone_index;
    uint16_t padding;
    vector3_t position;
    svM2EventTrackClassic_t track;
} svM2EventClassic_t;

static bool M2ArrayRange(svM2Array_t array, uint32_t elem_size, uint32_t file_size,
                         uint32_t *offset, uint32_t *bytes) {
    if (array.size <= 0 || array.offset < 0 || elem_size == 0)
        return false;
    if ((uint32_t)array.size > ((uint32_t)~0u) / elem_size)
        return false;
    *offset = (uint32_t)array.offset;
    *bytes  = (uint32_t)array.size * elem_size;
    return *offset <= file_size && *bytes <= file_size - *offset;
}

static void const *M2ArrayAt(uint8_t const *data, uint32_t file_size, svM2Array_t array, uint32_t elem_size) {
    uint32_t offset, bytes;
    if (!M2ArrayRange(array, elem_size, file_size, &offset, &bytes))
        return NULL;
    return data + offset;
}

/* Read the first timestamp for a given sequence from an event track.
 * Returns 0 if no timestamp is found. */
static uint32_t M2EventTrackTime(uint8_t const *data, uint32_t file_size,
                               uint8_t const *track_ptr, bool classic,
                               uint32_t sequence_index) {
    if (classic) {
        svM2EventTrackClassic_t const *track = (svM2EventTrackClassic_t const *)track_ptr;
        svM2Range_t const *ranges = (svM2Range_t const *)M2ArrayAt(data, file_size, track->ranges, sizeof(svM2Range_t));
        uint32_t const *times = (uint32_t const *)M2ArrayAt(data, file_size, track->times, sizeof(uint32_t));
        if (!ranges || !times || sequence_index >= (uint32_t)track->ranges.size)
            return 0;
        svM2Range_t range = ranges[sequence_index];
        if (range.start >= (uint32_t)track->times.size)
            return 0;
        return times[range.start];
    } else {
        svM2EventTrack_t const *track = (svM2EventTrack_t const *)track_ptr;
        svM2SequenceTimes_t const *seq_times = (svM2SequenceTimes_t const *)M2ArrayAt(data, file_size, track->sequence_times, sizeof(svM2SequenceTimes_t));
        if (!seq_times || sequence_index >= (uint32_t)track->sequence_times.size)
            return 0;
        uint32_t const *times = (uint32_t const *)M2ArrayAt(data, file_size, seq_times[sequence_index].times, sizeof(uint32_t));
        if (!times || seq_times[sequence_index].times.size == 0)
            return 0;
        return times[0];
    }
}

/* Read the events m2Array_t from the correct header offset.
 * Classic M2 (version <= 263) has playable_animation_lookup between
 * sequence_lookups and bones, shifting events to a later offset. */
static svM2Array_t M2ReadEventsArray(uint8_t const *payload, uint32_t payload_size) {
    svM2Array_t empty = { 0, 0 };
    svM2Header_t const *hdr = (svM2Header_t const *)payload;
    bool classic = hdr->version <= 263;
    /* events offset from start of M2 header:
     *   modern: 264 bytes (no playable_animation_lookup, no texture_flipbooks, views is uint32)
     *   classic: 284 bytes (has playable_animation_lookup + texture_flipbooks, views is m2Array) */
    uint32_t events_offset = classic ? 284 : 264;
    if (events_offset + sizeof(svM2Array_t) > payload_size)
        return empty;
    return *(svM2Array_t const *)(payload + events_offset);
}

static bool M2FindPayload(uint8_t const *data, uint32_t size,
                          uint8_t const **payload, uint32_t *payload_size) {
    if (!data || size < sizeof(uint32_t))
        return false;
    if (*(uint32_t const *)data == ID_MD20) {
        *payload      = data;
        *payload_size = size;
        return true;
    }
    if (*(uint32_t const *)data != ID_MD21 && *(uint32_t const *)data != ID_12DM)
        return false;

    uint8_t const *ptr = data;
    uint8_t const *end = data + size;
    while (ptr + 8 <= end) {
        uint32_t tag, chunk_size;
        memcpy(&tag,        ptr,     sizeof(tag));
        memcpy(&chunk_size, ptr + 4, sizeof(chunk_size));
        ptr += 8;
        if (chunk_size > (uint32_t)(end - ptr))
            return false;
        if (tag == ID_MD20 ||
            ((tag == ID_MD21 || tag == ID_12DM) &&
             chunk_size >= sizeof(uint32_t) && *(uint32_t const *)ptr == ID_MD20)) {
            *payload      = ptr;
            *payload_size = chunk_size;
            return true;
        }
        ptr += chunk_size;
    }
    return false;
}

static void M2AnimationName(uint16_t id, string_t out, uint32_t out_size) {
    cstring_t name = NULL;
    switch (id) {
        case  0: name = "Stand";          break;
        case  1: name = "Death";          break;
        case  2: name = "Spell";          break;
        case  3: name = "Stop";           break;
        case  4: name = "Walk";           break;
        case  5: name = "Run";            break;
        case  6: name = "Dead";           break;
        case  7: name = "Rise";           break;
        case  8: name = "StandWound";     break;
        case  9: name = "CombatWound";    break;
        case 10: name = "CombatCritical"; break;
        case 11: name = "ShuffleLeft";    break;
        case 12: name = "ShuffleRight";   break;
        case 13: name = "WalkBackwards";  break;
        case 14: name = "Stun";           break;
        case 15: name = "HandsClosed";    break;
        case 16: name = "AttackUnarmed";  break;
        case 17: name = "Attack1H";       break;
        case 18: name = "Attack2H";       break;
        case 19: name = "Attack2HL";      break;
        case 20: name = "ParryUnarmed";   break;
        case 21: name = "Parry1H";        break;
        case 22: name = "Parry2H";        break;
        case 23: name = "Parry2HL";       break;
        case 24: name = "ShieldBlock";    break;
        case 25: name = "ReadyUnarmed";   break;
        case 26: name = "Ready1H";        break;
        case 27: name = "Ready2H";        break;
        case 28: name = "Ready2HL";       break;
        case 29: name = "ReadyBow";       break;
        case 30: name = "Dodge";          break;
        case 37: name = "JumpStart";      break;
        case 38: name = "Jump";           break;
        case 39: name = "JumpEnd";        break;
        case 40: name = "Fall";           break;
        case 41: name = "SwimIdle";       break;
        case 42: name = "Swim";           break;
        case 53: name = "ReadySpellDirected"; break;
        case 54: name = "ReadySpellOmni";     break;
        case 55: name = "SpellCastDirected";  break;
        case 56: name = "SpellCastOmni";      break;
        default: break;
    }
    if (name)
        snprintf(out, out_size, "%s", name);
    else
        snprintf(out, out_size, "Animation%u", (unsigned)id);
}

static uint16_t M2SequenceAnimId(uint8_t const *seq, bool classic) {
    return classic ? ((svM2SequenceClassic_t const *)seq)->animation_id
                   : ((svM2SequenceModern_t  const *)seq)->animation_id;
}
static uint32_t M2SequenceLength(uint8_t const *seq, bool classic) {
    if (classic) {
        svM2SequenceClassic_t const *s = (svM2SequenceClassic_t const *)seq;
        return s->end_timestamp > s->start_timestamp
             ? s->end_timestamp - s->start_timestamp : 0;
    }
    return ((svM2SequenceModern_t const *)seq)->length;
}
static float M2SequenceMoveSpeed(uint8_t const *seq, bool classic) {
    return classic ? ((svM2SequenceClassic_t const *)seq)->movement_speed
                   : ((svM2SequenceModern_t  const *)seq)->movement_speed;
}
static uint32_t M2SequenceFlags(uint8_t const *seq, bool classic) {
    return classic ? ((svM2SequenceClassic_t const *)seq)->flags
                   : ((svM2SequenceModern_t  const *)seq)->flags;
}
static int16_t M2SequenceRarity(uint8_t const *seq, bool classic) {
    return classic ? ((svM2SequenceClassic_t const *)seq)->probability
                   : (int16_t)((svM2SequenceModern_t const *)seq)->frequency;
}
static vector3_t M2SequenceMin(uint8_t const *seq, bool classic) {
    return classic ? ((svM2SequenceClassic_t const *)seq)->min
                   : ((svM2SequenceModern_t  const *)seq)->min;
}
static vector3_t M2SequenceMax(uint8_t const *seq, bool classic) {
    return classic ? ((svM2SequenceClassic_t const *)seq)->max
                   : ((svM2SequenceModern_t  const *)seq)->max;
}
static float M2SequenceRadius(uint8_t const *seq, bool classic) {
    return classic ? ((svM2SequenceClassic_t const *)seq)->radius
                   : ((svM2SequenceModern_t  const *)seq)->radius;
}

static bool M2AnimationNameExists(animation_t const *anims, uint32_t count, cstring_t name) {
    FOR_LOOP(i, count) {
        if (!strcasecmp(anims[i].name, name))
            return true;
    }
    return false;
}

static uint32_t M2AnimationSyncPoint(cstring_t name) {
    char buffer[80];
    memset(buffer, 0, sizeof(buffer));
    strncpy(buffer, name, sizeof(buffer) - 1);
    for (uint32_t i = 0; buffer[i]; i++)
        buffer[i] = (char)tolower(buffer[i]);
    return fnv1a32(buffer);
}

static animation_t *LoadModelM2(uint8_t const *data, uint32_t read_size, uint32_t *out_count) {
    uint8_t const *payload      = NULL;
    uint32_t payload_size = 0;
    animation_t *animations  = NULL;
    uint32_t num = 0;

    if (read_size < sizeof(uint32_t)) {
        *out_count = 0;
        return NULL;
    }

    if (read_size < sizeof(svM2Header_t) ||
        !M2FindPayload(data, read_size, &payload, &payload_size) ||
        payload_size < sizeof(svM2Header_t)) {
        *out_count = 0;
        return NULL;
    }

    svM2Header_t const *header = (svM2Header_t const *)payload;
    bool classic = header->version <= 263;
    uint32_t stride = classic ? sizeof(svM2SequenceClassic_t) : sizeof(svM2SequenceModern_t);
    uint32_t sequences_offset, sequences_bytes;
    if (!M2ArrayRange(header->sequences, stride, payload_size, &sequences_offset, &sequences_bytes)) {
        *out_count = 0;
        return NULL;
    }

    uint8_t const *sequences   = payload + sequences_offset;
    uint32_t sequence_count    = sequences_bytes / stride;

    /* Parse M2 events to extract weapon-hit timestamps per sequence.
     * Events reference sequences by raw index; we store the damage_point
     * for each sequence, then apply it when building the animation array. */
    uint32_t *seq_damage_points = NULL;
    svM2Array_t events_array = M2ReadEventsArray(payload, payload_size);
    if (sequence_count > 0) {
        seq_damage_points = gi.MemAlloc(sizeof(uint32_t) * sequence_count);
        memset(seq_damage_points, 0, sizeof(uint32_t) * sequence_count);

        uint32_t event_stride = classic ? sizeof(svM2EventClassic_t) : sizeof(svM2EventModern_t);
        uint32_t event_offset, event_bytes;
        if (M2ArrayRange(events_array, event_stride, payload_size, &event_offset, &event_bytes)) {
            uint8_t const *events = payload + event_offset;
            uint32_t event_count = event_bytes / event_stride;
            FOR_LOOP(e, event_count) {
                uint8_t const *ev = events + e * event_stride;
                uint32_t event_id = *(uint32_t const *)ev;
                if (event_id != M2_EVENT_SWH && event_id != M2_EVENT_SHD)
                    continue;
                /* event track starts after: event_id(4) + data(4) + bone(2) + padding(2) + position(12) = 24 */
                uint8_t const *track_ptr = ev + 24;
                /* Try all sequences — weapon-hit events typically fire in attack animations */
                FOR_LOOP(s, sequence_count) {
                    uint32_t ts = M2EventTrackTime(payload, payload_size, track_ptr, classic, s);
                    if (ts > 0 && seq_damage_points[s] == 0)
                        seq_damage_points[s] = ts;
                }
            }
        }
    }

    animations = gi.MemAlloc(sizeof(animation_t) * sequence_count);
    memset(animations, 0, sizeof(animation_t) * sequence_count);

    uint32_t frame_base = 0;
    FOR_LOOP(i, sequence_count) {
        uint8_t const *src   = sequences + i * stride;
        char name[80];
        uint32_t length = M2SequenceLength(src, classic);
        if (length == 0) length = 1;

        M2AnimationName(M2SequenceAnimId(src, classic), name, sizeof(name));
        if (!M2AnimationNameExists(animations, num, name)) {
            animation_t *dest = animations + num++;
            strncpy(dest->name, name, sizeof(dest->name) - 1);
            dest->interval[0] = frame_base;
            dest->interval[1] = frame_base + length;
            dest->movespeed   = M2SequenceMoveSpeed(src, classic);
            dest->flags       = M2SequenceFlags(src, classic);
            dest->rarity      = (float)M2SequenceRarity(src, classic);
            dest->syncpoint   = M2AnimationSyncPoint(dest->name);
            dest->radius      = M2SequenceRadius(src, classic);
            dest->min         = M2SequenceMin(src, classic);
            dest->max         = M2SequenceMax(src, classic);
            /* Apply damage_point from M2 events (clamped to sequence length) */
            if (seq_damage_points && seq_damage_points[i] > 0)
                dest->damage_point = MIN(seq_damage_points[i], length);
        }
        frame_base += length;
    }

    if (seq_damage_points)
        gi.MemFree(seq_damage_points);

    if (num > 1)
        qsort(animations, num, sizeof(animation_t), compare_animation_name);

    *out_count = num;
    return animations;
}

/* ---- model cache ---- */

#define G_MAX_MODELS MAX_MODELS

typedef struct {
    animation_t *animations;
    uint32_t        num_animations;
    float        attach_hand_z;   /* attachment id=1 (right hand) local Z, 0 if missing */
    float        attach_chest_z;  /* attachment id=20 (chest) local Z, 0 if missing */
    char         filename[MAX_PATHLEN];
} g_cmodel_t;

static g_cmodel_t g_models[G_MAX_MODELS];

int G_RegisterModel(cstring_t filename) {
    int index = gi.ModelIndex(filename);
    if (index > 0 && index < G_MAX_MODELS && !g_models[index].filename[0])
        strncpy(g_models[index].filename, filename, MAX_PATHLEN - 1);
    return index;
}

static uint8_t *ReadModelFile(cstring_t filename, uint32_t *out_size) {
    uint8_t *data;

    if (!filename || !*filename)
        return NULL;
    data = gi.ReadFile(filename, out_size);
    if (!data) {
        PATHSTR path;
        size_t len = strlen(filename);
        if (len == 0 || len >= sizeof(path))
            return NULL;
        memcpy(path, filename, len + 1);
        path[len - 1] = 'x';
        data = gi.ReadFile(path, out_size);
        if (!data && strstr(filename, ".mdx")) {
            string_t ext;
            memcpy(path, filename, len + 1);
            ext = strstr(path, ".mdx");
            memcpy(ext, ".m2", 4);
            data = gi.ReadFile(path, out_size);
        }
    }
    return data;
}

static g_cmodel_t *LoadModel(cstring_t filename) {
    uint32_t fileheader;
    uint32_t data_size = 0;
    uint8_t *data = ReadModelFile(filename, &data_size);
    if (!data || data_size < sizeof(fileheader)) {
        if (data)
            gi.MemFree(data);
        return NULL;
    }

    g_cmodel_t *model = gi.MemAlloc(sizeof(g_cmodel_t));
    memset(model, 0, sizeof(*model));

    memcpy(&fileheader, data, sizeof(fileheader));
    switch (fileheader) {
        case ID_MDLX:
            model->animations = LoadModelMDLX(data, data_size, &model->num_animations);
            break;
        case ID_43DM:
            model->animations = LoadModelMD34(data, data_size, &model->num_animations);
            break;
        case ID_MD20:
        case ID_MD21:
        case ID_12DM:
            model->animations = LoadModelM2(data, data_size, &model->num_animations);
            /* Parse the M2 right-hand attachment for the server's launch-height approximation. */
            {
                uint8_t const *payload = NULL;
                uint32_t payload_size = 0;
                if (M2FindPayload(data, data_size, &payload, &payload_size)
                    && payload_size >= sizeof(svM2Header_t)) {
                    svM2Header_t const *hdr = (svM2Header_t const *)payload;
                    bool classic = hdr->version <= 263;
                    /* Empirically verified: classic attachments at 0x104, modern at 0xF0 */
                    uint32_t attach_offset = classic ? 0x104 : 0x0F0;
                    uint32_t lookup_offset = classic ? 0x10C : 0x0F8;
                    svM2Array_t attach_arr, lookup_arr;
                    if (attach_offset + sizeof(svM2Array_t) <= payload_size
                        && lookup_offset + sizeof(svM2Array_t) <= payload_size) {
                        memcpy(&attach_arr, payload + attach_offset, sizeof(svM2Array_t));
                        memcpy(&lookup_arr, payload + lookup_offset, sizeof(svM2Array_t));
                        /* Each attachment entry: id(4) + bone(2) + unk(2) + pos(12) + track(28) = 48 */
                        static const uint32_t ATTACH_STRIDE = 48;
                        uint32_t off, bytes;
                        if (M2ArrayRange(attach_arr, ATTACH_STRIDE, payload_size, &off, &bytes)) {
                            uint8_t const *attach_base = payload + off;
                            uint32_t attach_count = bytes / ATTACH_STRIDE;
                            uint16_t const *lookup = (uint16_t const *)M2ArrayAt((uint8_t *)payload, payload_size, lookup_arr, sizeof(uint16_t));
                            FOR_LOOP(aid, 2) {
                                uint32_t attachment_id = aid ? 20 : 1;
                                uint16_t idx = (lookup && attachment_id < (uint32_t)lookup_arr.size)
                                    ? lookup[attachment_id] : 0xFFFF;
                                if (idx != 0xFFFF && (uint32_t)idx < attach_count) {
                                    uint8_t const *entry = attach_base + idx * ATTACH_STRIDE;
                                    if (*(uint32_t const *)entry == attachment_id) {
                                        float z; memcpy(&z, entry + 16, sizeof(float));
                                        if (attachment_id == 1) model->attach_hand_z = z;
                                        else model->attach_chest_z = z;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            break;
        default:
            break;
    }
    gi.MemFree(data);
    return model;
}

static g_cmodel_t *GetModel(uint32_t modelindex) {
    if (modelindex == 0 || modelindex >= G_MAX_MODELS)
        return NULL;
    g_cmodel_t *entry = &g_models[modelindex];
    if (!entry->animations && entry->filename[0]) {
        g_cmodel_t *m = LoadModel(entry->filename);
        if (!m)
            return NULL;
        entry->animations     = m->animations;
        entry->num_animations = m->num_animations;
        entry->attach_hand_z  = m->attach_hand_z;
        entry->attach_chest_z = m->attach_chest_z;
        gi.MemFree(m);
    }
    return entry->animations ? entry : NULL;
}

animation_t const *G_GetAnimation(uint32_t modelindex, cstring_t animname) {
    g_cmodel_t *model = GetModel(modelindex);
    if (!model)
        return NULL;
    uint32_t hash = fnv1a32(animname);
    FOR_LOOP(i, model->num_animations) {
        if (model->animations[i].syncpoint == hash)
            return &model->animations[i];
    }
    FOR_LOOP(i, model->num_animations) {
        if (!strcasecmp(model->animations[i].name, animname))
            return &model->animations[i];
    }
    return NULL;
}

void G_FreeModels(void) {
    FOR_LOOP(i, G_MAX_MODELS) {
        if (g_models[i].animations)
            gi.MemFree(g_models[i].animations);
        memset(&g_models[i], 0, sizeof(g_models[i]));
    }
}

/* Return the model-local Z of attachment `aid` for the given model.
 * Returns 0 if the model or attachment is not found.
 * Attachments 1/20 are the right hand/chest; the animated world transform remains renderer-owned. */
float G_GetAttachmentZ(uint32_t modelindex, int aid) {
    g_cmodel_t *model = GetModel(modelindex);
    if (!model) return 0;
    switch (aid) {
        case 1: return model->attach_hand_z;
        case 20: return model->attach_chest_z;
        default: return 0;
    }
}
