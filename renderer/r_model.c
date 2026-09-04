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
static PATHSTR r_map_asset_scope;

/* A game renderer may install an archive/directory scope for map-local assets.
 * Shared model/texture caches use the resolved scoped path as their identity. */
BOOL R_MapAssetCandidate(LPCSTR asset, LPSTR candidate, DWORD candidate_size) {
    size_t scope_len;
    int written;

    if (!asset || !*asset || !candidate || candidate_size == 0 || !r_map_asset_scope[0]) return false;
    scope_len = strlen(r_map_asset_scope);
    if (strlen(asset) > scope_len && !strncasecmp(asset, r_map_asset_scope, scope_len) &&
        (asset[scope_len] == '\\' || asset[scope_len] == '/')) return false;
    if (asset[0] == '/' || asset[0] == '\\' || (asset[0] && asset[1] == ':')) return false;
    written = snprintf(candidate, candidate_size, "%s\\%s", r_map_asset_scope, asset);
    return written > 0 && (DWORD)written < candidate_size;
}

void R_SetMapAssetScope(LPCSTR scope) {
    r_map_asset_scope[0] = '\0';
    if (scope && *scope) snprintf(r_map_asset_scope, sizeof(r_map_asset_scope), "%s", scope);
}

/* Missing files remain valid cached handles, matching Quake II registration semantics. */
static LPMODEL R_LoadEmptyModel(LPCSTR modelFilename, LPCSTR reason) {
    LPMODEL model;
    fprintf(stderr, "R_LoadModel: %s: %s, using empty model\n", reason, modelFilename);
    model = ri.MemAlloc(sizeof(*model));
    memset(model, 0, sizeof(*model));
    return model;
}

static LPMODEL R_LoadRegisteredModelPath(LPCSTR modelFilename, BOOL cache_missing) {
    KNOWNMODEL *entry = NULL;
    LPMODEL model;

    FOR_LOOP(i, MAX_MOD_KNOWN) {
        if (mod_known[i].model && !strcasecmp(mod_known[i].name, modelFilename)) {
            mod_known[i].references++;
            mod_known[i].registration_sequence = mod_registration_sequence;
            return mod_known[i].model;
        }
        if (!entry && !mod_known[i].model) entry = &mod_known[i];
    }
    if (!entry) {
        ri.error("R_LoadModel: MAX_MOD_KNOWN (%u) reached", MAX_MOD_KNOWN);
        return cache_missing ? R_LoadEmptyModel(modelFilename, "model registry exhausted") : NULL;
    }
    model = R_LoadModel(modelFilename);
    if (!model && !cache_missing) return NULL;
    if (!model) model = R_LoadEmptyModel(modelFilename, "not found");
    snprintf(entry->name, sizeof(entry->name), "%s", modelFilename);
    entry->model = model;
    entry->references = 1;
    entry->registration_sequence = mod_registration_sequence;
    return model;
}

/* Quake II keeps one renderer model entry per resolved filename and marks it during registration. */
LPMODEL R_LoadRegisteredModel(LPCSTR modelFilename) {
    PATHSTR scoped;
    LPMODEL model;

    if (!modelFilename || !*modelFilename) return R_LoadEmptyModel("<empty>", "empty filename");
    if (R_MapAssetCandidate(modelFilename, scoped, sizeof(scoped))) {
        model = R_LoadRegisteredModelPath(scoped, false);
        if (model) return model;
    }
    return R_LoadRegisteredModelPath(modelFilename, true);
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
    if (!mapFileName || !*mapFileName) R_SetMapAssetScope(NULL);
    mod_registration_sequence++;
    if (!mod_registration_sequence) mod_registration_sequence = 1;
    if (mapFileName && *mapFileName) R_RegisterMap(mapFileName);
    R_FreeUnusedModels(false);
}

void R_ShutdownModels(void) {
    R_SetMapAssetScope(NULL);
    R_FreeUnusedModels(true);
}
