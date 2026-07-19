#include "g_sc2_local.h"
#include <ctype.h>
#include <stdlib.h>

static DWORD fnv1a32(LPCSTR str) {
    DWORD prime = 16777619;
    DWORD hash  = 2166136261;
    while (*str) {
        hash = (hash ^ *str++) * prime;
    }
    return hash;
}

static void ConvertMD34AnimationName(LPANIMATION seq) {
    char buffer[80];
    memset(buffer, 0, sizeof(buffer));
    strncpy(buffer, seq->name, sizeof(buffer) - 1);
    for (DWORD i = 0; buffer[i]; i++)
        buffer[i] = (char)tolower(buffer[i]);
    for (DWORD i = (DWORD)strlen(buffer) - 1; i > 0 && isspace(buffer[i]); i--)
        buffer[i] = '\0';
    seq->syncpoint = fnv1a32(buffer);
}

/* ---- MD34 (StarCraft II M3) ---- */

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
        LPSTR ext;

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
    if (fileheader == ID_43DM)
        model->animations = LoadModelMD34(data, data_size, &model->num_animations);

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
