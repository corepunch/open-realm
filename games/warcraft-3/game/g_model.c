#include "g_local.h"
#include <ctype.h>
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
    for (DWORD i = (DWORD)strlen(buffer) - 1; i > 0 && isspace(buffer[i]); i--) {
        buffer[i] = '\0';
    }
    seq->syncpoint = fnv1a32(buffer);
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

struct md34BoundingSphere {
    VECTOR3 min;
    VECTOR3 max;
    float radius;
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
    struct md34BoundingSphere boundingSphere;
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

/* ---- model cache ---- */

#define G_MAX_MODELS MAX_MODELS

typedef struct {
    animation_t *animations;
    DWORD        num_animations;
    char         filename[MAX_PATHLEN];
    BOOL         loaded;   /* load attempted (success or failure) — avoids
                              re-reading/parsing the model from the MPQ every
                              frame for models that fail or have 0 animations. */
} g_cmodel_t;

static g_cmodel_t g_models[G_MAX_MODELS];

void G_NormalizeModelFilename(LPCSTR authored, LPSTR out, size_t out_size) {
    LPCSTR slash_back;
    LPCSTR slash_forward;
    LPCSTR slash;
    LPCSTR dot;

    if (!out || !out_size) return;
    out[0] = '\0';
    if (!authored || !*authored) return;

    slash_back = strrchr(authored, '\\');
    slash_forward = strrchr(authored, '/');
    slash = slash_back;
    if (!slash || (slash_forward && slash_forward > slash)) slash = slash_forward;
    dot = strrchr(authored, '.');
    if (dot && (!slash || dot > slash))
        strlcpy(out, authored, out_size);
    else
        snprintf(out, out_size, "%s.mdx", authored);
}

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
        memcpy(path, filename, len + 1);
        path[len - 1] = 'x';
        data = gi.ReadFile(path, out_size);
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
    if (!entry->loaded && entry->filename[0]) {
        /* Attempt the load exactly once. Mark loaded up-front so a failed or
         * empty parse is not retried (re-reading the MPQ) on every frame. */
        entry->loaded = true;
        g_cmodel_t *m = LoadModel(entry->filename);
        if (m) {
            entry->animations     = m->animations;
            entry->num_animations = m->num_animations;
            gi.MemFree(m);
        }
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


#define WC3_ANIMATION_MAX_TAGS 24
#define WC3_ANIMATION_TAG_SIZE 32

typedef struct {
    char value[WC3_ANIMATION_MAX_TAGS][WC3_ANIMATION_TAG_SIZE];
    DWORD count;
} animationTagSet_t;

static BOOL AnimationTokenIsNumeric(LPCSTR token) {
    if (!token || !*token) return false;
    for (; *token; token++) if (!isdigit((unsigned char)*token)) return false;
    return true;
}

static BOOL AnimationTagSetContains(animationTagSet_t const *set, LPCSTR token) {
    FOR_LOOP(i, set->count) if (!strcasecmp(set->value[i], token)) return true;
    return false;
}

static void AnimationTagSetAdd(animationTagSet_t *set, LPCSTR token) {
    if (!token || !*token || AnimationTokenIsNumeric(token) ||
        AnimationTagSetContains(set, token) || set->count >= WC3_ANIMATION_MAX_TAGS)
        return;
    strlcpy(set->value[set->count++], token, WC3_ANIMATION_TAG_SIZE);
}

static void AnimationParseWords(LPCSTR text, animationTagSet_t *set) {
    char token[WC3_ANIMATION_TAG_SIZE];
    DWORD length = 0;

    if (!text) return;
    for (;;) {
        unsigned char ch = (unsigned char)*text++;
        if (isalnum(ch) || ch == '_') {
            if (length + 1 < sizeof(token)) token[length++] = (char)tolower(ch);
            continue;
        }
        if (length) {
            token[length] = '\0';
            AnimationTagSetAdd(set, token);
            length = 0;
        }
        if (!ch) break;
    }
}

static void AnimationParseRequest(LPCSTR text, char primary[WC3_ANIMATION_TAG_SIZE],
                                  animationTagSet_t *secondary) {
    animationTagSet_t words = {0};

    primary[0] = '\0';
    AnimationParseWords(text, &words);
    if (!words.count) return;
    strlcpy(primary, words.value[0], WC3_ANIMATION_TAG_SIZE);
    for (DWORD i = 1; i < words.count; i++) AnimationTagSetAdd(secondary, words.value[i]);
}

static DWORD AnimationTagSetMatchCount(animationTagSet_t const *required,
                                       animationTagSet_t const *candidate) {
    DWORD matches = 0;
    FOR_LOOP(i, required->count) if (AnimationTagSetContains(candidate, required->value[i])) matches++;
    return matches;
}

static BOOL AnimationTagSetContainsAll(animationTagSet_t const *candidate,
                                       animationTagSet_t const *required) {
    return AnimationTagSetMatchCount(required, candidate) == required->count;
}

static void AnimationTagSetReplace(animationTagSet_t const *source, LPCSTR from, LPCSTR to,
                                   animationTagSet_t *dest) {
    memset(dest, 0, sizeof(*dest));
    FOR_LOOP(i, source->count) {
        AnimationTagSetAdd(dest, !strcasecmp(source->value[i], from) ? to : source->value[i]);
    }
}

static LPCANIMATION AnimationFindContainingSet(LPCANIMATION animations, DWORD count, LPCSTR primary,
                                               animationTagSet_t const *required) {
    LPCANIMATION contains = NULL;
    DWORD contains_extras = UINT32_MAX;

    FOR_LOOP(i, count) {
        animationTagSet_t sequence_tags = {0};
        char sequence_primary[WC3_ANIMATION_TAG_SIZE];
        DWORD matches, extras;

        AnimationParseRequest(animations[i].name, sequence_primary, &sequence_tags);
        if (strcasecmp(primary, sequence_primary)) continue;
        matches = AnimationTagSetMatchCount(required, &sequence_tags);
        extras = sequence_tags.count > matches ? sequence_tags.count - matches : 0;
        if (!AnimationTagSetContainsAll(&sequence_tags, required)) continue;
        if (extras == 0) return animations + i;
        if (!contains || extras < contains_extras) {
            contains = animations + i;
            contains_extras = extras;
        }
    }
    return contains;
}

/* Select by Warcraft's primary animation family plus Required Animation Names.
 * Sequence number suffixes are intentionally ignored. Exact secondary-tag sets
 * win; if a model has no exact set, prefer a sequence containing all requested
 * tags with the fewest extras, then the best-overlap sequence. Warcraft's stock
 * object data also uses `alternateex` for some alternate-form units whose models
 * only expose `Alternate` sequences (notably Medivh raven form). Preserve a real
 * AlternateEx sequence when present, but retry Alternate before dropping to an
 * unrelated/untagged fallback. */
LPCANIMATION G_SelectAnimationForProperties(LPCANIMATION animations, DWORD count,
                                            LPCSTR animname, LPCSTR properties) {
    animationTagSet_t required = {0};
    char primary[WC3_ANIMATION_TAG_SIZE];
    LPCANIMATION contains = NULL;
    LPCANIMATION overlap = NULL;
    DWORD contains_extras = UINT32_MAX;
    DWORD overlap_matches = 0;
    DWORD overlap_extras = UINT32_MAX;

    if (!animations || !count || !animname || !*animname) return NULL;
    AnimationParseRequest(animname, primary, &required);
    AnimationParseWords(properties, &required);
    if (!primary[0]) return NULL;

    FOR_LOOP(i, count) {
        animationTagSet_t sequence_tags = {0};
        char sequence_primary[WC3_ANIMATION_TAG_SIZE];
        DWORD matches, extras;

        AnimationParseRequest(animations[i].name, sequence_primary, &sequence_tags);
        if (strcasecmp(primary, sequence_primary)) continue;
        matches = AnimationTagSetMatchCount(&required, &sequence_tags);
        extras = sequence_tags.count > matches ? sequence_tags.count - matches : 0;

        if (AnimationTagSetContainsAll(&sequence_tags, &required)) {
            if (extras == 0) return animations + i;
            if (!contains || extras < contains_extras) {
                contains = animations + i;
                contains_extras = extras;
            }
        }
        if (matches && (!overlap || matches > overlap_matches ||
                        (matches == overlap_matches && extras < overlap_extras))) {
            overlap = animations + i;
            overlap_matches = matches;
            overlap_extras = extras;
        }
    }

    if (contains) return contains;

    if (AnimationTagSetContains(&required, "alternateex") &&
        !AnimationTagSetContains(&required, "alternate")) {
        animationTagSet_t alternate_fallback = {0};
        LPCANIMATION alternate;

        AnimationTagSetReplace(&required, "alternateex", "alternate", &alternate_fallback);
        alternate = AnimationFindContainingSet(animations, count, primary, &alternate_fallback);
        if (alternate) return alternate;
    }

    if (overlap) return overlap;
    return NULL;
}

LPCANIMATION G_GetAnimationForProperties(DWORD modelindex, LPCSTR animname, LPCSTR properties) {
    g_cmodel_t *model = GetModel(modelindex);
    LPCANIMATION selected;

    if (!model) return NULL;
    selected = G_SelectAnimationForProperties(model->animations, model->num_animations, animname, properties);
    if (selected) return selected;
    /* Preserve the old exact-name behavior when no tagged candidate exists. */
    return G_GetAnimation(modelindex, animname);
}

BOOL G_AnimationHasPrimary(LPCANIMATION animation, LPCSTR primary) {
    size_t len;
    unsigned char next;

    if (!animation || !primary || !*primary) return false;
    len = strlen(primary);
    if (strncasecmp(animation->name, primary, len)) return false;
    next = (unsigned char)animation->name[len];
    return next == '\0' || !isalnum(next);
}

static void AnimationTagSetWrite(animationTagSet_t const *set, LPSTR out, size_t out_size) {
    if (!out || !out_size) return;
    out[0] = '\0';
    FOR_LOOP(i, set->count) {
        if (i) strlcat(out, ",", out_size);
        strlcat(out, set->value[i], out_size);
    }
}

void G_ResetUnitAnimationProperties(LPEDICT unit) {
    animationTagSet_t properties = {0};
    LPCSTR authored;

    if (!unit) return;
    authored = unit->data.UnitProfile ? unit->data.UnitProfile->animProps : NULL;
    AnimationParseWords(authored, &properties);
    AnimationTagSetWrite(&properties, unit->animation_props, sizeof(unit->animation_props));
    unit->animation_request[0] = '\0';
}

LPCANIMATION G_GetUnitAnimation(LPEDICT unit, LPCSTR animname) {
    return unit ? G_GetAnimationForProperties(unit->s.model, animname, unit->animation_props) : NULL;
}

void G_SetUnitAnimation(LPEDICT unit, LPCSTR animname) {
    char request[WC3_ANIMATION_REQUEST_SIZE];

    if (!unit || !animname) return;
    strlcpy(request, animname, sizeof(request));
    strlcpy(unit->animation_request, request, sizeof(unit->animation_request));
    unit->animation = G_GetUnitAnimation(unit, request);
}

void G_AddUnitAnimationProperties(LPEDICT unit, LPCSTR properties, BOOL add) {
    animationTagSet_t current = {0};
    animationTagSet_t changed = {0};
    animationTagSet_t result = {0};
    char request[WC3_ANIMATION_REQUEST_SIZE];
    BOOL mutated = false;

    if (!unit || !properties || !*properties) return;
    AnimationParseWords(unit->animation_props, &current);
    AnimationParseWords(properties, &changed);
    result = current;

    if (add) {
        FOR_LOOP(i, changed.count) {
            if (!AnimationTagSetContains(&result, changed.value[i])) {
                AnimationTagSetAdd(&result, changed.value[i]);
                mutated = true;
            }
        }
    } else {
        animationTagSet_t kept = {0};
        FOR_LOOP(i, result.count) {
            if (AnimationTagSetContains(&changed, result.value[i])) mutated = true;
            else AnimationTagSetAdd(&kept, result.value[i]);
        }
        result = kept;
    }
    if (!mutated) return;

    AnimationTagSetWrite(&result, unit->animation_props, sizeof(unit->animation_props));
    strlcpy(request, unit->animation_request, sizeof(request));
    if (!request[0] && unit->currentmove && unit->currentmove->animation)
        strlcpy(request, unit->currentmove->animation, sizeof(request));
    if (!request[0]) strlcpy(request, "stand", sizeof(request));
    G_SetUnitAnimation(unit, request);
}

void G_FreeModels(void) {
    FOR_LOOP(i, G_MAX_MODELS) {
        if (g_models[i].animations) {
            gi.MemFree(g_models[i].animations);
        }
        memset(&g_models[i], 0, sizeof(g_models[i]));
    }
}
