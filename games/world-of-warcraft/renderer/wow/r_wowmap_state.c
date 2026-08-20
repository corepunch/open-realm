#include "r_wowmap.h"
#include <ctype.h>

wowMap_t wow_world;

BOOL Wow_PathHasExtension(LPCSTR path, LPCSTR extension) {
    size_t path_len;
    size_t ext_len;

    if (!path || !extension) {
        return false;
    }
    path_len = strlen(path);
    ext_len = strlen(extension);
    if (path_len < ext_len) {
        return false;
    }
    return strcasecmp(path + path_len - ext_len, extension) == 0;
}

void Wow_NormalizeMapPath(LPCSTR mapFileName, LPSTR out, DWORD out_size) {
    if (!mapFileName || !*mapFileName) {
        snprintf(out, out_size, "World/Maps/Azeroth/Azeroth.wdt");
    } else if (Wow_PathHasExtension(mapFileName, ".wdt")) {
        snprintf(out, out_size, "%s", mapFileName);
    } else {
        snprintf(out, out_size, "World/Maps/%s/%s.wdt", mapFileName, mapFileName);
    }
}

void Wow_SetMapNames(LPCSTR path) {
    char const *slash = strrchr(path, '/');
    char const *backslash = strrchr(path, '\\');
    char const *base = slash > backslash ? slash : backslash;
    size_t dir_len;
    size_t name_len;

    base = base ? base + 1 : path;
    dir_len = (size_t)(base - path);
    if (dir_len > 0 && dir_len < sizeof(wow_world.map_dir)) {
        memcpy(wow_world.map_dir, path, dir_len);
        wow_world.map_dir[dir_len] = '\0';
        if (wow_world.map_dir[dir_len - 1] == '/' || wow_world.map_dir[dir_len - 1] == '\\') {
            wow_world.map_dir[dir_len - 1] = '\0';
        }
    }

    name_len = strlen(base);
    if (name_len > 4 && Wow_PathHasExtension(base, ".wdt")) {
        name_len -= 4;
    }
    name_len = MIN(name_len, sizeof(wow_world.map_name) - 1);
    memcpy(wow_world.map_name, base, name_len);
    wow_world.map_name[name_len] = '\0';
}

/* Resolve Blizzard's logical mapXX_YY names once; runtime tile draws then avoid filesystem work. */
BOOL Wow_LoadMinimapTranslations(void) {
    LPBYTE data = NULL;
    int size = ri.FS_ReadFile("Textures/Minimap/md5translate.trs", (void **)&data);
    DWORD found = 0;

    if (size <= 0 || !data) {
        fprintf(stderr, "Wow_LoadMinimapTranslations: missing Textures/Minimap/md5translate.trs\n");
        return false;
    }
    for (int offset = 0; offset < size;) {
        PATHSTR line; char dir[128], hash[WOW_MINIMAP_HASH_LENGTH + 1];
        int start = offset, x, y;
        while (offset < size && data[offset] != '\r' && data[offset] != '\n') offset++;
        int len = MIN(offset - start, (int)sizeof(line) - 1);
        memcpy(line, data + start, len); line[len] = '\0';
        while (offset < size && (data[offset] == '\r' || data[offset] == '\n')) offset++;
        if (sscanf(line, "%127[^\\]\\map%d_%d.blp\t%32s", dir, &x, &y, hash) != 4) continue;
        if (strcasecmp(dir, wow_world.map_name) || x < 0 || x >= WOW_WDT_TILES || y < 0 || y >= WOW_WDT_TILES) continue;
        if (strlen(hash) != WOW_MINIMAP_HASH_LENGTH) continue;
        BOOL valid = true;
        FOR_LOOP(i, WOW_MINIMAP_HASH_LENGTH) valid = valid && isxdigit((unsigned char)hash[i]);
        if (!valid) continue;
        memcpy(wow_world.minimap_hash[x][y], hash, sizeof(hash)); found++;
    }
    ri.FS_FreeFile(data);
    if (!found) fprintf(stderr, "Wow_LoadMinimapTranslations: no tiles for map %s\n", wow_world.map_name);
    else fprintf(stderr, "Wow_LoadMinimapTranslations: map=%s tiles=%u\n", wow_world.map_name, (unsigned)found);
    return found != 0;
}

DWORD Wow_Read32(BYTE const *p) {
    DWORD v;
    memcpy(&v, p, sizeof(v));
    return v;
}

WORD Wow_Read16(BYTE const *p) {
    WORD v;
    memcpy(&v, p, sizeof(v));
    return v;
}

void Wow_FreeChunks(void) {
    wowAdtChunk_t *chunk = wow_world.chunks;
    while (chunk) {
        wowAdtChunk_t *next = chunk->next;
        R_ReleaseVertexArrayObject(chunk->buffer);
        R_ReleaseVertexArrayObject(chunk->grass_buffer);
        if (chunk->alpha_texture && chunk->alpha_texture != wow_world.alpha_atlas_texture) {
            R_ReleaseTexture(chunk->alpha_texture);
        }
        ri.MemFree(chunk);
        chunk = next;
    }
    wow_world.chunks = NULL;
    wow_world.num_grass_chunks = 0;
    wow_world.num_grass_vertices = 0;
    if (wow_world.alpha_atlas_texture) {
        R_ReleaseTexture(wow_world.alpha_atlas_texture);
        wow_world.alpha_atlas_texture = NULL;
    }
    SAFE_DELETE(wow_world.height_atlas, R_ReleaseTexture);
    SAFE_DELETE(wow_world.grass_ctrl, R_ReleaseTexture);
    wow_world.has_atlas_origin = false;
    wow_world.atlas_world_x = wow_world.atlas_world_y = 0.0f;
    if (wow_world.object_buffer) {
        R_ReleaseVertexArrayObject(wow_world.object_buffer);
        wow_world.object_buffer = NULL;
    }
    wow_world.num_object_vertices = 0;
}

void Wow_FreeWmoModels(void) {
    wowWmoModel_t *model = wow_world.wmo_models;
    while (model) {
        wowWmoModel_t *next_model = model->next;
        wowWmoBatch_t *model_batch = model->batches;
        while (model_batch) {
            wowWmoBatch_t *next_batch = model_batch->next;
            R_ReleaseVertexArrayObject(model_batch->buffer); ri.MemFree(model_batch); model_batch = next_batch;
        }
        if (model->groups) {
            FOR_LOOP(i, model->num_groups) {
                wowWmoBatch_t *batch = model->groups[i].batches;
                while (batch) {
                    wowWmoBatch_t *next_batch = batch->next;
                    R_ReleaseVertexArrayObject(batch->buffer);
                    ri.MemFree(batch);
                    batch = next_batch;
                }
            }
            ri.MemFree(model->groups);
        }
        SAFE_DELETE(model->doodad_sets, ri.MemFree);
        SAFE_DELETE(model->doodad_defs, ri.MemFree);
        SAFE_DELETE(model->doodad_name_blob, ri.MemFree);
        SAFE_DELETE(model->def_groups, ri.MemFree);
        SAFE_DELETE(model->lights, ri.MemFree);
        SAFE_DELETE(model->portals, ri.MemFree);
        SAFE_DELETE(model->portal_vertices, ri.MemFree);
        SAFE_DELETE(model->portal_refs, ri.MemFree);
        ri.MemFree(model);
        model = next_model;
    }
    wow_world.wmo_models = NULL;
}

void Wow_FreeWmoInstances(void) {
    wowWmoInstance_t *instance = wow_world.wmos;
    while (instance) {
        wowWmoInstance_t *next = instance->next;
        ri.MemFree(instance);
        instance = next;
    }
    wow_world.wmos = NULL;
}

void Wow_FreeDoodadInstances(void) {
    wowDoodadInstance_t *doodad = wow_world.doodads;
    while (doodad) {
        wowDoodadInstance_t *next = doodad->next;
        ri.MemFree(doodad);
        doodad = next;
    }
    wow_world.doodads = NULL;
    doodad = wow_world.ground_effects;
    while (doodad) {
        wowDoodadInstance_t *next = doodad->next;
        ri.MemFree(doodad);
        doodad = next;
    }
    wow_world.ground_effects = NULL;
    memset(wow_world.doodad_buckets, 0, sizeof(wow_world.doodad_buckets));
}

void Wow_ClearLoadedAdts(void) {
    Wow_FreeGrassScratch();
    Wow_FreeChunks();
    Wow_FreeWmoInstances();
    Wow_FreeWmoModels();
    Wow_FreeDoodadInstances();
    /* WMO doodad persistent buffers must be rebuilt when new WMOs load */
    wow_world.wmo_doodads_built = false;
    for (wowDoodadModel_t *g = wow_world.doodad_models; g; g = g->next) {
        SAFE_DELETE(g->wmo_matrices, ri.MemFree);
        R_ReleaseInstanceBuffer(&g->wmo_instances);
        g->wmo_count = 0;
        g->wmo_capacity = 0;
    }
    wow_world.num_adts = 0;
    wow_world.num_chunks = 0;
    wow_world.num_doodads = 0;
    wow_world.num_doodad_instances = 0;
    wow_world.num_ground_effects = 0;
    wow_world.num_wmos = 0;
    wow_world.num_wmo_models = 0;
    wow_world.num_wmo_batches = 0;
    wow_world.num_missing_wmos = 0;
}

void Wow_FreeWorld(void) {
    wowM2BoundsCache_t *bounds;
    wowDoodadModel_t *doodad_model;

    Wow_FreeChunks();
    Wow_FreeWmoInstances();
    Wow_FreeWmoModels();
    wowTextureCache_t *texture = wow_world.textures;
    while (texture) {
        wowTextureCache_t *next = texture->next;
        R_ReleaseTexture(texture->texture);
        ri.MemFree(texture);
        texture = next;
    }
    bounds = wow_world.m2_bounds;
    while (bounds) {
        wowM2BoundsCache_t *next = bounds->next;
        ri.MemFree(bounds);
        bounds = next;
    }
    doodad_model = wow_world.doodad_models;
    while (doodad_model) {
        wowDoodadModel_t *next = doodad_model->next;
        R_ReleaseInstanceBuffer(&doodad_model->instances);
        R_ReleaseInstanceBuffer(&doodad_model->wmo_instances);
        SAFE_DELETE(doodad_model->matrices, ri.MemFree);
        SAFE_DELETE(doodad_model->wmo_matrices, ri.MemFree);
        SAFE_DELETE(doodad_model->model, R_ReleaseModel);
        ri.MemFree(doodad_model);
        doodad_model = next;
    }
    Wow_FreeDoodadInstances();
    Wow_FreeGrassScratch();
    Wow_FreeCameraGrassMesh();
    memset(&wow_world, 0, sizeof(wow_world));
}

void Wow_ShutdownWorldShaders(void) {
    SAFE_DELETE(wow_terrain_shader, R_ReleaseShader);
    SAFE_DELETE(wow_grass_shader, R_ReleaseShader);
    wow_uTexture0 = -1;
    wow_uTexture1 = -1;
    wow_uTexture2 = -1;
    wow_uTexture3 = -1;
    wow_uAlphaTexture = -1;
    wow_uUseWeightedBlend = -1;
    wow_uSingleTexture = -1;
    wow_uWmoIndoor = -1;
    wow_uWmoAmbient = -1;
    wow_uWmoBlendMode = -1;
    wow_uAlphaOrigin = -1;
    wow_uAlphaAtlasChunks = -1;
    wow_uGrassTime = -1;
    wow_uGrassCameraOrigin = -1;
    wow_uGrassDrawDistance = -1;
    wow_uGrassFadeStartDistance = -1;
    wow_uHeightAtlas = -1;
    wow_uAtlasOriginWorld = -1;
    wow_uAtlasChunkSize = -1;
    wow_uAtlasUnitSize = -1;
    wow_uGrassCtrl = -1;
    wow_uCtrlOriginWorld = -1;
    wow_uCtrlCellSize = -1;
    wow_uCameraXZ = -1;
    wow_uGrassSlotSpacing = -1;
}

LPTEXTURE Wow_LoadTexture(LPCSTR path) {
    wowTextureCache_t *entry;

    for (entry = wow_world.textures; entry; entry = entry->next) {
        if (!strcasecmp(entry->path, path)) {
            return entry->texture;
        }
    }

    entry = ri.MemAlloc(sizeof(*entry));
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->path, sizeof(entry->path), "%s", path);
    entry->texture = R_LoadTexture(path);
    if (entry->texture) {
        R_SetTextureWrap(entry->texture, true, true);
        if (entry->texture != tr.texture[TEX_WHITE]) {
            /* Keep cache hit path silent in normal runs. */
        }
    } else {
        entry->texture = tr.texture[TEX_WHITE];
    }
    entry->next = wow_world.textures;
    wow_world.textures = entry;
    return entry->texture;
}

BOOL Wow_ReadM2RadiusFromPath(LPCSTR path, float *radius) {
    LPBYTE data = NULL;
    int size;
    DWORD version;
    DWORD radius_offset;

    size = ri.FS_ReadFile(path, (void **)&data);
    if (size < (int)(sizeof(DWORD) * 2) || !data) {
        SAFE_DELETE(data, ri.FS_FreeFile);
        return false;
    }

    if (*(DWORD *)data != MAKEFOURCC('M', 'D', '2', '0')) {
        ri.FS_FreeFile(data);
        return false;
    }

    memcpy(&version, data + sizeof(DWORD), sizeof(version));
    radius_offset = 8;
    radius_offset += sizeof(wowM2Array_t);
    radius_offset += sizeof(DWORD);
    radius_offset += sizeof(wowM2Array_t) * 3;
    if (version <= 263) {
        radius_offset += sizeof(wowM2Array_t);
    }
    radius_offset += sizeof(wowM2Array_t) * 3;
    radius_offset += version <= 263 ? sizeof(wowM2Array_t) : sizeof(DWORD);
    radius_offset += sizeof(wowM2Array_t) * 3;
    if (version <= 263) {
        radius_offset += sizeof(wowM2Array_t);
    }
    radius_offset += sizeof(wowM2Array_t) * 8;
    radius_offset += sizeof(float) * 6;
    if ((DWORD)size < radius_offset + sizeof(*radius)) {
        ri.FS_FreeFile(data);
        return false;
    }

    memcpy(radius, data + radius_offset, sizeof(*radius));
    ri.FS_FreeFile(data);
    return true;
}

BOOL Wow_CopyModelPathFallback(LPCSTR path, LPSTR out, DWORD out_size) {
    size_t len;

    if (!path || !out || out_size == 0 || !Wow_PathHasExtension(path, ".mdx")) {
        return false;
    }

    len = strlen(path);
    if (len + 1 > out_size) {
        return false;
    }

    snprintf(out, out_size, "%s", path);
    out[len - 3] = 'm';
    out[len - 2] = '2';
    out[len - 1] = '\0';
    return true;
}

float Wow_LoadM2BoundsRadius(LPCSTR path) {
    wowM2BoundsCache_t *entry;
    PATHSTR fallback_path;
    float radius = 0.0f;

    if (!path || !*path) {
        return 0.0f;
    }

    for (entry = wow_world.m2_bounds; entry; entry = entry->next) {
        if (!strcasecmp(entry->path, path)) {
            return entry->radius;
        }
    }

    if (!Wow_ReadM2RadiusFromPath(path, &radius) &&
        Wow_CopyModelPathFallback(path, fallback_path, sizeof(fallback_path))) {
        (void)Wow_ReadM2RadiusFromPath(fallback_path, &radius);
    }

    entry = ri.MemAlloc(sizeof(*entry));
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->path, sizeof(entry->path), "%s", path);
    entry->radius = radius;
    entry->next = wow_world.m2_bounds;
    wow_world.m2_bounds = entry;
    if (radius > 0.0f) {
        wow_world.num_doodad_models++;
    }
    return radius;
}


void Wow_FreeStringList(char **strings, DWORD count) {
    if (!strings) {
        return;
    }
    FOR_LOOP(i, count) {
        SAFE_DELETE(strings[i], ri.MemFree);
    }
    ri.MemFree(strings);
}

char **Wow_ParseStringBlock(BYTE const *data, DWORD size, LPDWORD out_count) {
    DWORD count = 0;
    DWORD offset = 0;
    char **strings;

    while (offset < size) {
        size_t len = strlen((LPCSTR)(data + offset));
        count++;
        offset += (DWORD)len + 1;
    }

    strings = ri.MemAlloc(sizeof(char *) * MAX(count, 1));
    memset(strings, 0, sizeof(char *) * MAX(count, 1));
    offset = 0;
    count = 0;
    while (offset < size) {
        LPCSTR value = (LPCSTR)(data + offset);
        size_t len = strlen(value);
        strings[count] = ri.MemAlloc(len + 1);
        memcpy(strings[count], value, len + 1);
        count++;
        offset += (DWORD)len + 1;
    }

    *out_count = count;
    return strings;
}

LPCSTR Wow_StringRefFromOffsets(BYTE const *blob, DWORD blob_size, DWORD const *offsets, DWORD offset_count, DWORD id) {
    DWORD offset;

    if (!blob || !offsets || id >= offset_count) {
        return NULL;
    }
    offset = offsets[id];
    if (offset >= blob_size || memchr(blob + offset, '\0', blob_size - offset) == NULL) {
        return NULL;
    }
    return (LPCSTR)(blob + offset);
}
