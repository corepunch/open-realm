#include "g_sc2_local.h"
#include <ctype.h>
#include <stdlib.h>

static uint32_t fnv1a32(cstring_t str) {
    uint32_t prime = 16777619;
    uint32_t hash  = 2166136261;
    while (*str) {
        hash = (hash ^ *str++) * prime;
    }
    return hash;
}

static void ConvertMD34AnimationName(animation_t *seq) {
    char buffer[80];
    memset(buffer, 0, sizeof(buffer));
    strncpy(buffer, seq->name, sizeof(buffer) - 1);
    for (uint32_t i = 0; buffer[i]; i++)
        buffer[i] = (char)tolower(buffer[i]);
    for (uint32_t i = (uint32_t)strlen(buffer) - 1; i > 0 && isspace(buffer[i]); i--)
        buffer[i] = '\0';
    seq->syncpoint = fnv1a32(buffer);
}

/* ---- MD34 (StarCraft II M3) ---- */

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
            ConvertMD34AnimationName(animations + j);
        }
        break;
    }
    *out_count = num;
    return animations;
}

/* ---- model cache ---- */

#define G_MAX_MODELS MAX_MODELS

typedef struct {
    animation_t *animations;
    uint32_t        num_animations;
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
        string_t ext;

        if (len == 0 || len >= sizeof(path))
            return NULL;
        memcpy(path, filename, len + 1);
        ext = strstr(path, ".m3");
        if (ext && ext[3] == '\0' && (size_t)(ext - path) + 5 <= sizeof(path)) {
            memcpy(ext, ".m3x", 5);
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
    if (fileheader == ID_43DM)
        model->animations = LoadModelMD34(data, data_size, &model->num_animations);

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
