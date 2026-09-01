#include "r_local.h"
#include "r_game.h"
#include <strings.h>

#define MAX_MOD_KNOWN (MAX_MODELS * 4)

typedef struct {
    PATHSTR name;
    LPMODEL model;
    DWORD references;
    DWORD registration_sequence;
} KNOWNMODEL;

static KNOWNMODEL mod_known[MAX_MOD_KNOWN];
static DWORD mod_registration_sequence = 1;

/* Missing files remain valid cached handles, matching Quake II registration semantics. */
static LPMODEL R_LoadEmptyModel(LPCSTR modelFilename, LPCSTR reason) {
    LPMODEL model;
    fprintf(stderr, "R_LoadModel: %s: %s, using empty model\n", reason, modelFilename);
    model = ri.MemAlloc(sizeof(*model));
    memset(model, 0, sizeof(*model));
    return model;
}

/* Quake II keeps one renderer model entry per filename and marks it during registration. */
LPMODEL R_LoadRegisteredModel(LPCSTR modelFilename) {
    KNOWNMODEL *entry = NULL;
    LPMODEL model;
    if (!modelFilename || !*modelFilename) return R_LoadEmptyModel("<empty>", "empty filename");
    FOR_LOOP(i, MAX_MOD_KNOWN) {
        if (mod_known[i].model && !strcasecmp(mod_known[i].name, modelFilename)) {
            mod_known[i].references++; mod_known[i].registration_sequence = mod_registration_sequence;
            return mod_known[i].model;
        }
        if (!entry && !mod_known[i].model) entry = &mod_known[i];
    }
    if (!entry) {
        ri.error("R_LoadModel: MAX_MOD_KNOWN (%u) reached", MAX_MOD_KNOWN);
        return R_LoadEmptyModel(modelFilename, "model registry exhausted");
    }
    model = R_LoadModel(modelFilename);
    if (!model) model = R_LoadEmptyModel(modelFilename, "not found");
    snprintf(entry->name, sizeof(entry->name), "%s", modelFilename);
    entry->model = model; entry->references = 1; entry->registration_sequence = mod_registration_sequence;
    return model;
}

/* Release drops caller ownership; stale resident images are reclaimed at the next registration boundary. */
void R_ReleaseRegisteredModel(LPMODEL model) {
    if (!model) return;
    FOR_LOOP(i, MAX_MOD_KNOWN)
        if (mod_known[i].model == model) {
            if (mod_known[i].references) mod_known[i].references--;
            return;
        }
    R_ReleaseModel(model);
}

/* End-of-registration cleanup mirrors Mod_FreeUnused: only unreferenced, unmarked models leave residency. */
static void R_FreeUnusedModels(BOOL shutdown) {
    FOR_LOOP(i, MAX_MOD_KNOWN) {
        KNOWNMODEL *entry = &mod_known[i];
        if (!entry->model || (!shutdown && (entry->references ||
            entry->registration_sequence == mod_registration_sequence))) continue;
        R_ReleaseModel(entry->model);
        memset(entry, 0, sizeof(*entry));
    }
}

void R_RegisterMapAssets(LPCSTR mapFileName) {
    mod_registration_sequence++;
    if (!mod_registration_sequence) mod_registration_sequence = 1;
    R_RegisterMap(mapFileName);
    R_FreeUnusedModels(false);
}

void R_ShutdownModels(void) { R_FreeUnusedModels(true); }
