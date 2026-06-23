#include "g_wow_local.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>

enum {
    ID_SEQS = MAKEFOURCC('S','E','Q','S'),
};

static DWORD fnv1a32(LPCSTR str) {
    DWORD prime = 16777619;
    DWORD hash  = 2166136261;
    while (*str) {
        hash = (hash ^ *str++) * prime;
    }
    return hash;
}

static void ConvertMDLXAnimationName(LPANIMATION seq) {
    char buffer[80];
    char *last_char = buffer;
    memset(buffer, 0, sizeof(buffer));
    strcpy(buffer, seq->name);
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
    for (DWORD i = (DWORD)strlen(buffer) - 1; i > 0 && isspace(buffer[i]); i--) {
        buffer[i] = '\0';
    }
    seq->syncpoint = fnv1a32(buffer);
}

/* ---- MDLX (Warcraft III) ---- */

static animation_t *LoadModelMDLX(BYTE const *data, DWORD data_size, DWORD *out_count) {
    DWORD payloadSize = data_size > 4 ? data_size - 4 : 0;
    animation_t *animations = NULL;
    DWORD num = 0;
    BYTE const *ptr = data + 4;
    BYTE const *end = ptr + payloadSize;

    while (ptr && ptr + 8 <= end) {
        DWORD header, size;
        memcpy(&header, ptr, sizeof(DWORD));
        memcpy(&size,   ptr + 4, sizeof(DWORD));
        ptr += 8;
        if (ptr + size > end) {
            size = (DWORD)(end - ptr);
        }
        if (header == ID_SEQS) {
            animations = gi.MemAlloc(size);
            num = size / sizeof(animation_t);
            memcpy(animations, ptr, size);
            FOR_LOOP(i, num) {
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
    DWORD nEntries;
    DWORD offset;
    DWORD flags;
} md34Reference_t;

struct md33Header {
    DWORD ofsRefs;
    DWORD nRefs;
    md34Reference_t MODL;
};

struct md34ReferenceEntry {
    DWORD id;
    DWORD offset;
    DWORD nEntries;
    DWORD version;
};

struct md34NameRef {
    DWORD nEntries;
    DWORD ref;
    DWORD flags;
};

struct md34Sequence {
    DWORD unknown[2];
    struct md34NameRef name;
    DWORD interval[2];
    float movementSpeed;
    DWORD flags;
    DWORD frequency;
    LONG unk[3];
    LONG unk2;
    struct { VECTOR3 min; VECTOR3 max; float radius; } boundingSphere;
    LONG d5[3];
};

static BYTE const *ModelDataAt(BYTE const *data, DWORD data_size, DWORD offset, DWORD size) {
    if (!data || offset > data_size || size > data_size - offset)
        return NULL;
    return data + offset;
}

static int compare_animation_name(const void *a, const void *b) {
    return strcmp(((LPCANIMATION)a)->name, ((LPCANIMATION)b)->name);
}

static animation_t *LoadModelMD34(BYTE const *data, DWORD data_size, DWORD *out_count) {
    struct md33Header const *hdr = (struct md33Header const *)ModelDataAt(data, data_size, 4, sizeof(*hdr));
    struct md34ReferenceEntry const *ent;

    animation_t *animations = NULL;
    DWORD num = 0;

    if (!hdr) {
        *out_count = 0;
        return NULL;
    }
    ent = (struct md34ReferenceEntry const *)ModelDataAt(data, data_size, hdr->ofsRefs,
        sizeof(struct md34ReferenceEntry) * hdr->nRefs);
    if (!ent) {
        *out_count = 0;
        return NULL;
    }

    FOR_LOOP(i, hdr->nRefs) {
        struct md34ReferenceEntry const *re = ent + i;
        if (re->id != MAKEFOURCC('S','Q','E','S'))
            continue;
        struct md34Sequence const *seq = (struct md34Sequence const *)ModelDataAt(data, data_size, re->offset,
            re->nEntries * sizeof(struct md34Sequence));
        if (!seq)
            continue;
        animations = gi.MemAlloc(sizeof(animation_t) * re->nEntries);
        memset(animations, 0, sizeof(animation_t) * re->nEntries);
        num = re->nEntries;
        DWORD startanim = 0;
        FOR_LOOP(j, re->nEntries) {
            struct md34Sequence const *src = seq + j;
            char const *name = src->name.ref < hdr->nRefs
                ? (char const *)ModelDataAt(data, data_size, ent[src->name.ref].offset, src->name.nEntries)
                : NULL;
            LPANIMATION dest = animations + j;
            if (name) {
                DWORD name_len = MIN(src->name.nEntries, sizeof(dest->name) - 1);
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
    DWORD magic;
    DWORD version;
    svM2Array_t name;
    DWORD flags;
    svM2Array_t global_loops;
    svM2Array_t sequences;
} svM2Header_t;

typedef struct {
    WORD  animation_id;
    WORD  sub_animation_id;
    DWORD start_timestamp;
    DWORD end_timestamp;
    FLOAT movement_speed;
    DWORD flags;
    SHORT probability;
    WORD  padding;
    DWORD minimum_repetitions;
    DWORD maximum_repetitions;
    DWORD blend_time;
    VECTOR3 min;
    VECTOR3 max;
    FLOAT radius;
    SHORT next_animation;
    WORD  alias_next;
} svM2SequenceClassic_t;

typedef struct {
    WORD  animation_id;
    WORD  sub_animation_id;
    DWORD length;
    FLOAT movement_speed;
    DWORD flags;
    DWORD frequency;
    DWORD minimum_repetitions;
    DWORD maximum_repetitions;
    DWORD blend_time;
    VECTOR3 min;
    VECTOR3 max;
    FLOAT radius;
    SHORT next_animation;
    WORD  alias_next;
} svM2SequenceModern_t;

/* M2 event IDs for weapon impacts (from wowdev.wiki/M2/eventID) */
#define M2_EVENT_WEAPON_LEFT  4
#define M2_EVENT_WEAPON_RIGHT 5

typedef struct {
    uint16_t track_type;
    uint16_t loop_index;
    svM2Array_t sequence_times;
    svM2Array_t sequence_keys;
} svM2Track_t;

typedef struct {
    uint16_t track_type;
    uint16_t loop_index;
    svM2Array_t ranges;
    svM2Array_t times;
    svM2Array_t keys;
} svM2TrackClassic_t;

typedef struct {
    svM2Array_t times;
} svM2SequenceTimes_t;

typedef struct {
    DWORD start;
    DWORD end;
} svM2Range_t;

typedef struct {
    DWORD event_id;
    DWORD data;
    WORD bone_index;
    WORD padding;
    VECTOR3 position;
    svM2Track_t track;
} svM2EventModern_t;

typedef struct {
    DWORD event_id;
    DWORD data;
    WORD bone_index;
    WORD padding;
    VECTOR3 position;
    svM2TrackClassic_t track;
} svM2EventClassic_t;

/* Read the first timestamp for a given sequence from an event track.
 * Returns 0 if no timestamp is found. */
static DWORD M2EventTrackTime(BYTE const *data, DWORD file_size,
                               BYTE const *track_ptr, BOOL classic,
                               DWORD sequence_index) {
    if (classic) {
        svM2TrackClassic_t const *track = (svM2TrackClassic_t const *)track_ptr;
        svM2Range_t const *ranges = (svM2Range_t const *)M2ArrayAt(data, file_size, track->ranges, sizeof(svM2Range_t));
        DWORD const *times = (DWORD const *)M2ArrayAt(data, file_size, track->times, sizeof(DWORD));
        if (!ranges || !times || sequence_index >= (DWORD)track->ranges.count)
            return 0;
        svM2Range_t range = ranges[sequence_index];
        if (range.start >= (DWORD)track->times.count)
            return 0;
        return times[range.start];
    } else {
        svM2Track_t const *track = (svM2Track_t const *)track_ptr;
        svM2SequenceTimes_t const *seq_times = (svM2SequenceTimes_t const *)M2ArrayAt(data, file_size, track->sequence_times, sizeof(svM2SequenceTimes_t));
        if (!seq_times || sequence_index >= (DWORD)track->sequence_times.count)
            return 0;
        DWORD const *times = (DWORD const *)M2ArrayAt(data, file_size, seq_times[sequence_index].times, sizeof(DWORD));
        if (!times || seq_times[sequence_index].times.count == 0)
            return 0;
        return times[0];
    }
}

static BOOL M2ArrayRange(svM2Array_t array, DWORD elem_size, DWORD file_size,
                         DWORD *offset, DWORD *bytes) {
    if (array.size <= 0 || array.offset < 0 || elem_size == 0)
        return false;
    if ((DWORD)array.size > ((DWORD)~0u) / elem_size)
        return false;
    *offset = (DWORD)array.offset;
    *bytes  = (DWORD)array.size * elem_size;
    return *offset <= file_size && *bytes <= file_size - *offset;
}

static void const *M2ArrayAt(BYTE const *data, DWORD file_size, svM2Array_t array, DWORD elem_size) {
    DWORD offset, bytes;
    if (!M2ArrayRange(array, elem_size, file_size, &offset, &bytes))
        return NULL;
    return data + offset;
}

/* Read the events m2Array_t from the correct header offset.
 * Classic M2 (version <= 263) has playable_animation_lookup between
 * sequence_lookups and bones, shifting events to a later offset. */
static svM2Array_t M2ReadEventsArray(BYTE const *payload, DWORD payload_size) {
    svM2Array_t empty = { 0, 0 };
    svM2Header_t const *hdr = (svM2Header_t const *)payload;
    BOOL classic = hdr->version <= 263;
    /* events offset from start of M2 header:
     *   modern: 264 bytes (no playable_animation_lookup, no texture_flipbooks, views is uint32)
     *   classic: 284 bytes (has playable_animation_lookup + texture_flipbooks, views is m2Array) */
    DWORD events_offset = classic ? 284 : 264;
    if (events_offset + sizeof(svM2Array_t) > payload_size)
        return empty;
    return *(svM2Array_t const *)(payload + events_offset);
}

static BOOL M2FindPayload(BYTE const *data, DWORD size,
                          BYTE const **payload, DWORD *payload_size) {
    if (!data || size < sizeof(DWORD))
        return false;
    if (*(DWORD const *)data == ID_MD20) {
        *payload      = data;
        *payload_size = size;
        return true;
    }
    if (*(DWORD const *)data != ID_MD21 && *(DWORD const *)data != ID_12DM)
        return false;

    BYTE const *ptr = data;
    BYTE const *end = data + size;
    while (ptr + 8 <= end) {
        DWORD tag, chunk_size;
        memcpy(&tag,        ptr,     sizeof(tag));
        memcpy(&chunk_size, ptr + 4, sizeof(chunk_size));
        ptr += 8;
        if (chunk_size > (DWORD)(end - ptr))
            return false;
        if (tag == ID_MD20 ||
            ((tag == ID_MD21 || tag == ID_12DM) &&
             chunk_size >= sizeof(DWORD) && *(DWORD const *)ptr == ID_MD20)) {
            *payload      = ptr;
            *payload_size = chunk_size;
            return true;
        }
        ptr += chunk_size;
    }
    return false;
}

static void M2AnimationName(WORD id, LPSTR out, DWORD out_size) {
    LPCSTR name = NULL;
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
        default: break;
    }
    if (name)
        snprintf(out, out_size, "%s", name);
    else
        snprintf(out, out_size, "Animation%u", (unsigned)id);
}

static WORD M2SequenceAnimId(BYTE const *seq, BOOL classic) {
    return classic ? ((svM2SequenceClassic_t const *)seq)->animation_id
                   : ((svM2SequenceModern_t  const *)seq)->animation_id;
}
static DWORD M2SequenceLength(BYTE const *seq, BOOL classic) {
    if (classic) {
        svM2SequenceClassic_t const *s = (svM2SequenceClassic_t const *)seq;
        return s->end_timestamp > s->start_timestamp
             ? s->end_timestamp - s->start_timestamp : 0;
    }
    return ((svM2SequenceModern_t const *)seq)->length;
}
static FLOAT M2SequenceMoveSpeed(BYTE const *seq, BOOL classic) {
    return classic ? ((svM2SequenceClassic_t const *)seq)->movement_speed
                   : ((svM2SequenceModern_t  const *)seq)->movement_speed;
}
static DWORD M2SequenceFlags(BYTE const *seq, BOOL classic) {
    return classic ? ((svM2SequenceClassic_t const *)seq)->flags
                   : ((svM2SequenceModern_t  const *)seq)->flags;
}
static SHORT M2SequenceRarity(BYTE const *seq, BOOL classic) {
    return classic ? ((svM2SequenceClassic_t const *)seq)->probability
                   : (SHORT)((svM2SequenceModern_t const *)seq)->frequency;
}
static VECTOR3 M2SequenceMin(BYTE const *seq, BOOL classic) {
    return classic ? ((svM2SequenceClassic_t const *)seq)->min
                   : ((svM2SequenceModern_t  const *)seq)->min;
}
static VECTOR3 M2SequenceMax(BYTE const *seq, BOOL classic) {
    return classic ? ((svM2SequenceClassic_t const *)seq)->max
                   : ((svM2SequenceModern_t  const *)seq)->max;
}
static FLOAT M2SequenceRadius(BYTE const *seq, BOOL classic) {
    return classic ? ((svM2SequenceClassic_t const *)seq)->radius
                   : ((svM2SequenceModern_t  const *)seq)->radius;
}

static BOOL M2AnimationNameExists(animation_t const *anims, DWORD count, LPCSTR name) {
    FOR_LOOP(i, count) {
        if (!strcasecmp(anims[i].name, name))
            return true;
    }
    return false;
}

static DWORD M2AnimationSyncPoint(LPCSTR name) {
    char buffer[80];
    memset(buffer, 0, sizeof(buffer));
    strncpy(buffer, name, sizeof(buffer) - 1);
    for (DWORD i = 0; buffer[i]; i++)
        buffer[i] = (char)tolower(buffer[i]);
    return fnv1a32(buffer);
}

static animation_t *LoadModelM2(BYTE const *data, DWORD read_size, DWORD *out_count) {
    BYTE const *payload      = NULL;
    DWORD payload_size = 0;
    animation_t *animations  = NULL;
    DWORD num = 0;

    if (read_size < sizeof(DWORD)) {
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
    BOOL classic = header->version <= 263;
    DWORD stride = classic ? sizeof(svM2SequenceClassic_t) : sizeof(svM2SequenceModern_t);
    DWORD sequences_offset, sequences_bytes;
    if (!M2ArrayRange(header->sequences, stride, payload_size,
                      &sequences_offset, &sequences_bytes)) {
        *out_count = 0;
        return NULL;
    }

    BYTE const *sequences   = payload + sequences_offset;
    DWORD sequence_count    = sequences_bytes / stride;

    /* Parse M2 events to extract weapon-hit timestamps per sequence.
     * Events reference sequences by raw index; we store the damage_point
     * for each sequence, then apply it when building the animation array. */
    DWORD *seq_damage_points = NULL;
    svM2Array_t events_array = M2ReadEventsArray(payload, payload_size);
    if (sequence_count > 0) {
        seq_damage_points = gi.MemAlloc(sizeof(DWORD) * sequence_count);
        memset(seq_damage_points, 0, sizeof(DWORD) * sequence_count);

        DWORD event_stride = classic ? sizeof(svM2EventClassic_t) : sizeof(svM2EventModern_t);
        DWORD event_offset, event_bytes;
        if (M2ArrayRange(events_array, event_stride, payload_size,
                         &event_offset, &event_bytes)) {
            BYTE const *events = payload + event_offset;
            DWORD event_count = event_bytes / event_stride;
            FOR_LOOP(e, event_count) {
                BYTE const *ev = events + e * event_stride;
                DWORD event_id = *(DWORD const *)ev;
                if (event_id != M2_EVENT_WEAPON_LEFT && event_id != M2_EVENT_WEAPON_RIGHT)
                    continue;
                /* event track starts after: event_id(4) + data(4) + bone(2) + padding(2) + position(12) = 24 */
                BYTE const *track_ptr = ev + 24;
                /* Try all sequences — weapon-hit events typically fire in attack animations */
                FOR_LOOP(s, sequence_count) {
                    DWORD ts = M2EventTrackTime(payload, payload_size, track_ptr, classic, s);
                    if (ts > 0 && seq_damage_points[s] == 0)
                        seq_damage_points[s] = ts;
                }
            }
        }
    }

    animations = gi.MemAlloc(sizeof(animation_t) * sequence_count);
    memset(animations, 0, sizeof(animation_t) * sequence_count);

    DWORD frame_base = 0;
    FOR_LOOP(i, sequence_count) {
        BYTE const *src   = sequences + i * stride;
        char name[80];
        DWORD length = M2SequenceLength(src, classic);
        if (length == 0) length = 1;

        M2AnimationName(M2SequenceAnimId(src, classic), name, sizeof(name));
        if (!M2AnimationNameExists(animations, num, name)) {
            LPANIMATION dest = animations + num++;
            strncpy(dest->name, name, sizeof(dest->name) - 1);
            dest->interval[0] = frame_base;
            dest->interval[1] = frame_base + length;
            dest->movespeed   = M2SequenceMoveSpeed(src, classic);
            dest->flags       = M2SequenceFlags(src, classic);
            dest->rarity      = (FLOAT)M2SequenceRarity(src, classic);
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
    DWORD        num_animations;
    char         filename[MAX_PATHLEN];
} g_cmodel_t;

static g_cmodel_t g_models[G_MAX_MODELS];

int G_RegisterModel(LPCSTR filename) {
    int index = gi.ModelIndex(filename);
    if (index > 0 && index < G_MAX_MODELS && !g_models[index].filename[0])
        strncpy(g_models[index].filename, filename, MAX_PATHLEN - 1);
    return index;
}

static BYTE *ReadModelFile(LPCSTR filename, DWORD *out_size) {
    BYTE *data;

    if (!filename || !*filename)
        return NULL;
    data = gi.ReadFile(filename, out_size);
    if (!data) {
        PATHSTR path;
        size_t len = strlen(filename);
        if (len == 0 || len >= sizeof(path))
            return NULL;
        strcpy(path, filename);
        path[len - 1] = 'x';
        data = gi.ReadFile(path, out_size);
        if (!data && strstr(filename, ".mdx")) {
            LPSTR ext;
            strcpy(path, filename);
            ext = strstr(path, ".mdx");
            strcpy(ext, ".m2");
            data = gi.ReadFile(path, out_size);
        }
    }
    return data;
}

static g_cmodel_t *LoadModel(LPCSTR filename) {
    DWORD fileheader;
    DWORD data_size = 0;
    BYTE *data = ReadModelFile(filename, &data_size);
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
            break;
        default:
            break;
    }
    gi.MemFree(data);
    return model;
}

static g_cmodel_t *GetModel(DWORD modelindex) {
    if (modelindex == 0 || modelindex >= G_MAX_MODELS)
        return NULL;
    g_cmodel_t *entry = &g_models[modelindex];
    if (!entry->animations && entry->filename[0]) {
        g_cmodel_t *m = LoadModel(entry->filename);
        if (!m)
            return NULL;
        entry->animations     = m->animations;
        entry->num_animations = m->num_animations;
        gi.MemFree(m);
    }
    return entry->animations ? entry : NULL;
}

LPCANIMATION G_GetAnimation(DWORD modelindex, LPCSTR animname) {
    g_cmodel_t *model = GetModel(modelindex);
    if (!model)
        return NULL;
    DWORD hash = fnv1a32(animname);
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
