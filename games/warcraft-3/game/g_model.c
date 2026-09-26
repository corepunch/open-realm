#include "g_local.h"
#include <ctype.h>
#include <stdlib.h>
#ifdef BZ_TESTS
#include "shared/test.h"
#endif

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
    for (size_t len = strlen(buffer); len && isspace((unsigned char)buffer[len - 1]);)
        buffer[--len] = '\0';
    seq->syncpoint = fnv1a32(buffer);
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

struct md34BoundingSphere {
    vector3_t min;
    vector3_t max;
    float radius;
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
    struct md34BoundingSphere boundingSphere;
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

#ifdef BZ_TESTS
TEST(wc3_model, empty_mdlx_sequence_name_does_not_break_animation_loading) {
    enum { SEQ_SIZE = 132, HEADER_SIZE = 12 };
    uint8_t data[HEADER_SIZE + 2 * SEQ_SIZE] = {0};
    uint32_t chunk = ID_SEQS, chunk_size = 2 * SEQ_SIZE, count = 0;
    animation_t *animations;

    memcpy(data, "MDLX", 4);
    memcpy(data + 4, &chunk, sizeof(chunk));
    memcpy(data + 8, &chunk_size, sizeof(chunk_size));
    memcpy(data + HEADER_SIZE + SEQ_SIZE, "Stand", 5);
    animations = LoadModelMDLX(data, sizeof(data), &count);
    T_NOT_NULL(animations);
    T_EQ(count, 2);
    if (animations && count == 2) {
        T_STREQ(animations[0].name, "");
        T_EQ(animations[0].syncpoint, fnv1a32(""));
        T_STREQ(animations[1].name, "Stand");
    }
    if (animations) gi.MemFree(animations);
}
#endif

/* ---- model cache ---- */

#define G_MAX_MODELS MAX_MODELS

typedef struct {
    animation_t *animations;
    uint32_t        num_animations;
    char         filename[MAX_PATHLEN];
    bool         loaded;   /* load attempted (success or failure) — avoids
                              re-reading/parsing the model from the MPQ every
                              frame for models that fail or have 0 animations. */
} g_cmodel_t;

static g_cmodel_t g_models[G_MAX_MODELS];

void G_NormalizeModelFilename(cstring_t authored, string_t out, size_t out_size) {
    cstring_t slash_back;
    cstring_t slash_forward;
    cstring_t slash;
    cstring_t dot;

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


#define WC3_ANIMATION_MAX_TAGS 24
#define WC3_ANIMATION_TAG_SIZE 32

typedef struct {
    char value[WC3_ANIMATION_MAX_TAGS][WC3_ANIMATION_TAG_SIZE];
    uint32_t count;
} animationTagSet_t;

static bool AnimationTokenIsNumeric(cstring_t token) {
    if (!token || !*token) return false;
    for (; *token; token++) if (!isdigit((unsigned char)*token)) return false;
    return true;
}

static bool AnimationTagSetContains(animationTagSet_t const *set, cstring_t token) {
    FOR_LOOP(i, set->count) if (!strcasecmp(set->value[i], token)) return true;
    return false;
}

static void AnimationTagSetAdd(animationTagSet_t *set, cstring_t token) {
    if (!token || !*token || AnimationTokenIsNumeric(token) ||
        AnimationTagSetContains(set, token) || set->count >= WC3_ANIMATION_MAX_TAGS)
        return;
    strlcpy(set->value[set->count++], token, WC3_ANIMATION_TAG_SIZE);
}

static void AnimationParseWords(cstring_t text, animationTagSet_t *set) {
    char token[WC3_ANIMATION_TAG_SIZE];
    uint32_t length = 0;

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

static void AnimationParseRequest(cstring_t text, char primary[WC3_ANIMATION_TAG_SIZE],
                                  animationTagSet_t *secondary) {
    animationTagSet_t words = {0};

    primary[0] = '\0';
    AnimationParseWords(text, &words);
    if (!words.count) return;
    strlcpy(primary, words.value[0], WC3_ANIMATION_TAG_SIZE);
    for (uint32_t i = 1; i < words.count; i++) AnimationTagSetAdd(secondary, words.value[i]);
}

static uint32_t AnimationTagSetMatchCount(animationTagSet_t const *required,
                                       animationTagSet_t const *candidate) {
    uint32_t matches = 0;
    FOR_LOOP(i, required->count) if (AnimationTagSetContains(candidate, required->value[i])) matches++;
    return matches;
}

static bool AnimationTagSetsEqual(animationTagSet_t const *a, animationTagSet_t const *b) {
    return a->count == b->count && AnimationTagSetMatchCount(a, b) == a->count;
}

static bool AnimationTagSetContainsAll(animationTagSet_t const *candidate,
                                       animationTagSet_t const *required) {
    return AnimationTagSetMatchCount(required, candidate) == required->count;
}

static void AnimationTagSetReplace(animationTagSet_t const *source, cstring_t from, cstring_t to,
                                   animationTagSet_t *dest) {
    memset(dest, 0, sizeof(*dest));
    FOR_LOOP(i, source->count) {
        AnimationTagSetAdd(dest, !strcasecmp(source->value[i], from) ? to : source->value[i]);
    }
}

static animation_t const *AnimationFindContainingSet(animation_t const *animations, uint32_t count, cstring_t primary,
                                               animationTagSet_t const *required) {
    animation_t const *contains = NULL;
    uint32_t contains_extras = UINT32_MAX;

    FOR_LOOP(i, count) {
        animationTagSet_t sequence_tags = {0};
        char sequence_primary[WC3_ANIMATION_TAG_SIZE];
        uint32_t matches, extras;

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
animation_t const *G_SelectAnimationForProperties(animation_t const *animations, uint32_t count,
                                            cstring_t animname, cstring_t properties) {
    animationTagSet_t required = {0};
    char primary[WC3_ANIMATION_TAG_SIZE];
    animation_t const *contains = NULL;
    animation_t const *overlap = NULL;
    animation_t const *primary_fallback = NULL;
    uint32_t contains_extras = UINT32_MAX;
    uint32_t overlap_matches = 0;
    uint32_t overlap_extras = UINT32_MAX;

    if (!animations || !count || !animname || !*animname) return NULL;
    AnimationParseRequest(animname, primary, &required);
    AnimationParseWords(properties, &required);
    if (!primary[0]) return NULL;

    FOR_LOOP(i, count) {
        animationTagSet_t sequence_tags = {0};
        char sequence_primary[WC3_ANIMATION_TAG_SIZE];
        uint32_t matches, extras;

        AnimationParseRequest(animations[i].name, sequence_primary, &sequence_tags);
        if (strcasecmp(primary, sequence_primary)) continue;
        if (!primary_fallback) primary_fallback = animations + i;
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
        animation_t const *alternate;

        AnimationTagSetReplace(&required, "alternateex", "alternate", &alternate_fallback);
        alternate = AnimationFindContainingSet(animations, count, primary, &alternate_fallback);
        if (alternate) return alternate;
    }

    if (overlap) return overlap;
    /* Warsmash/Warcraft fall back within the requested primary family when a
     * model lacks the requested secondary tag (for example a model with only
     * one generic Decay sequence serving both Decay Flesh and Decay Bone). */
    if (primary_fallback) return primary_fallback;
    return NULL;
}

animation_t const *G_GetAnimationForProperties(uint32_t modelindex, cstring_t animname, cstring_t properties) {
    g_cmodel_t *model = GetModel(modelindex);
    animation_t const *selected;

    if (!model) return NULL;
    selected = G_SelectAnimationForProperties(model->animations, model->num_animations, animname, properties);
    if (selected) return selected;
    /* Preserve the old exact-name behavior when no tagged candidate exists. */
    return G_GetAnimation(modelindex, animname);
}

/* Select numbered variants without crossing the selected sequence's tag set. */
animation_t const *G_SelectAnimationVariantForProperties(animation_t const *animations, uint32_t count,
                                                    cstring_t animname, cstring_t properties, bool randomize) {
    animation_t const *selected;
    animation_t const *choice = NULL;
    animationTagSet_t selected_tags = {0};
    char primary[WC3_ANIMATION_TAG_SIZE];
    char candidate_primary[WC3_ANIMATION_TAG_SIZE];
    uint32_t matches = 0;

    selected = G_SelectAnimationForProperties(animations, count, animname, properties);
    if (!selected || !randomize) return selected;
    AnimationParseRequest(selected->name, primary, &selected_tags);
    FOR_LOOP(i, count) {
        animation_t const *candidate = animations + i;
        animationTagSet_t candidate_tags = {0};
        AnimationParseRequest(candidate->name, candidate_primary, &candidate_tags);
        if (candidate->syncpoint != selected->syncpoint) continue;
        /* A shared sync point does not make Stand a variant of Walk. */
        if (strcasecmp(primary, candidate_primary)) continue;
        if (!AnimationTagSetsEqual(&selected_tags, &candidate_tags)) continue;
        matches++;
        if ((uint32_t)(rand() % matches) == 0) choice = candidate;
    }
    return choice ? choice : selected;
}

/* Select an authored animation variant while preserving the ordinary selector's fallback. */
animation_t const *G_GetAnimationVariant(uint32_t modelindex, cstring_t animname, bool randomize) {
    g_cmodel_t *model = GetModel(modelindex);
    if (!model) return NULL;
    return G_SelectAnimationVariantForProperties(model->animations, model->num_animations,
                                                  animname, NULL, randomize);
}

static animation_t const *AnimationVariantForProperties(g_cmodel_t *model, cstring_t animname, cstring_t properties) {
    if (!model) return NULL;
    return G_SelectAnimationVariantForProperties(model->animations, model->num_animations,
                                                  animname, properties, true);
}

bool G_AnimationHasPrimary(animation_t const *animation, cstring_t primary) {
    size_t len;
    unsigned char next;

    if (!animation || !primary || !*primary) return false;
    len = strlen(primary);
    if (strncasecmp(animation->name, primary, len)) return false;
    next = (unsigned char)animation->name[len];
    return next == '\0' || !isalnum(next);
}

static void AnimationTagSetWrite(animationTagSet_t const *set, string_t out, size_t out_size) {
    if (!out || !out_size) return;
    out[0] = '\0';
    FOR_LOOP(i, set->count) {
        if (i) strlcat(out, ",", out_size);
        strlcat(out, set->value[i], out_size);
    }
}

void G_ResetUnitAnimationProperties(edict_t *unit) {
    animationTagSet_t properties = {0};
    cstring_t authored;

    if (!unit) return;
    authored = unit->data.UnitProfile ? unit->data.UnitProfile->animProps : NULL;
    AnimationParseWords(authored, &properties);
    AnimationTagSetWrite(&properties, unit->animation_props, sizeof(unit->animation_props));
    unit->animation_request[0] = '\0';
}

animation_t const *G_GetUnitAnimation(edict_t *unit, cstring_t animname) {
    return unit ? G_GetAnimationForProperties(unit->s.model, animname, unit->animation_props) : NULL;
}

void G_SetUnitAnimation(edict_t *unit, cstring_t animname) {
    char request[WC3_ANIMATION_REQUEST_SIZE];
    char primary[WC3_ANIMATION_TAG_SIZE];
    animationTagSet_t request_tags = {0};

    if (!unit || !animname) return;
    strlcpy(request, animname, sizeof(request));
    strlcpy(unit->animation_request, request, sizeof(unit->animation_request));
    AnimationParseRequest(request, primary, &request_tags);
    unit->animation = !strcasecmp(primary, "walk") ?
        AnimationVariantForProperties(GetModel(unit->s.model), request, unit->animation_props) : NULL;
    if (!unit->animation) unit->animation = G_GetUnitAnimation(unit, request);
}

void G_AddUnitAnimationProperties(edict_t *unit, cstring_t properties, bool add) {
    animationTagSet_t current = {0};
    animationTagSet_t changed = {0};
    animationTagSet_t result = {0};
    char request[WC3_ANIMATION_REQUEST_SIZE];
    bool mutated = false;

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
