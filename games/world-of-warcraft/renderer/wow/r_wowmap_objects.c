#include "r_wowmap.h"

LPMODEL Wow_LoadDoodadModel(LPCSTR path) {
    wowDoodadModel_t *entry, *prev = NULL;

    if (!path || !*path) return NULL;
    /* Move-to-front: grass models are looked up 465K times across 14 unique paths;
     * after the first hit, the hot model is at position 0 → O(1) on next call. */
    for (entry = wow_world.doodad_models; entry; prev = entry, entry = entry->next) {
        if (!strcasecmp(entry->path, path)) {
            if (prev) {                              /* move to front */
                prev->next = entry->next;
                entry->next = wow_world.doodad_models;
                wow_world.doodad_models = entry;
            }
            return entry->model && entry->model->m2 ? entry->model : NULL;
        }
    }

    entry = ri.MemAlloc(sizeof(*entry));
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->path, sizeof(entry->path), "%s", path);
    entry->model = R_LoadModel(path);
    entry->next = wow_world.doodad_models;
    wow_world.doodad_models = entry;
    wow_world.num_doodad_models++;
    if (!entry->model || !entry->model->m2) {
        wow_world.num_missing_doodad_models++;
        return NULL;
    }
    entry->can_instance = R_GameModelCanStaticInstance(entry->model);
    return entry->model;
}

int Wow_DoodadBucketIndex(float coord) {
    int index = (int)floorf((coord + WOW_WORLD_COORD_OFFSET) / WOW_DOODAD_BUCKET_SIZE);
    if (index < 0) {
        return 0;
    }
    if (index >= WOW_DOODAD_BUCKETS) {
        return WOW_DOODAD_BUCKETS - 1;
    }
    return index;
}

void Wow_BucketDoodadInstance(wowDoodadInstance_t *instance) {
    int bucket_x;
    int bucket_y;

    if (!instance) {
        return;
    }

    bucket_x = Wow_DoodadBucketIndex(instance->entity.origin.x);
    bucket_y = Wow_DoodadBucketIndex(instance->entity.origin.y);
    instance->bucket_next = wow_world.doodad_buckets[bucket_y][bucket_x];
    wow_world.doodad_buckets[bucket_y][bucket_x] = instance;
}

void Wow_AddDoodadInstance(LPCSTR model_path, wowDoodadDef_t const *def) {
    wowDoodadInstance_t *instance;
    LPMODEL model;

    if (!model_path || !*model_path || !def) {
        wow_world.num_missing_doodad_models++;
        return;
    }
    if (def->flags & 0x40) {
        wow_world.num_filedata_doodads++;
        return;
    }
    model = Wow_LoadDoodadModel(model_path);
    if (!model) {
        return;
    }

    instance = ri.MemAlloc(sizeof(*instance));
    memset(instance, 0, sizeof(*instance));
    instance->entity.origin = Wow_ObjectPoint(def->position);
    instance->entity.rotation = (VECTOR3){ def->rotation.x, def->rotation.y, def->rotation.z };
    instance->entity.scale = def->scale / 1024.0f;
    instance->entity.model = model;
    instance->entity.radius = 32.0f;
    instance->entity.flags = RF_NO_SHADOW;
    for (instance->group = wow_world.doodad_models; instance->group; instance->group = instance->group->next)
        if (instance->group->model == model) break;
    instance->next = wow_world.doodads;
    wow_world.doodads = instance;
    Wow_BucketDoodadInstance(instance);
    wow_world.num_doodad_instances++;
}

/* Ground-effect M2s already contain the authoritative geometry and material paths from the MPQ. */
void Wow_AddGroundEffectInstance(LPCSTR model_path, VECTOR3 origin, float angle) {
    wowDoodadInstance_t *instance;
    LPMODEL model;

    model = Wow_LoadDoodadModel(model_path);
    if (!model) {
        return;
    }
    instance = ri.MemAlloc(sizeof(*instance));
    memset(instance, 0, sizeof(*instance));
    instance->entity.origin = origin;
    instance->entity.rotation = (VECTOR3){ 0.0f, 0.0f, angle };
    instance->entity.scale = 1.0f;
    instance->entity.model = model;
    instance->entity.radius = WOW_DOODAD_BUCKET_SIZE * 0.25f;
    instance->entity.flags = RF_NO_SHADOW | RF_GROUND_EFFECT;
    instance->next = wow_world.ground_effects;
    wow_world.ground_effects = instance;
    wow_world.num_ground_effects++;
}

void Wow_AddMarker(VERTEX *vertices, LPDWORD index, VECTOR3 p, float size, COLOR32 color) {
    VECTOR3 a = { p.x - size, p.y - size, p.z };
    VECTOR3 b = { p.x + size, p.y - size, p.z };
    VECTOR3 c = { p.x + size, p.y + size, p.z };
    VECTOR3 d = { p.x - size, p.y + size, p.z };
    VECTOR3 top = { p.x, p.y, p.z + size * 3.0f };
    vertices[(*index)++] = Wow_Vertex(a.x, a.y, a.z, 0, 0, color);
    vertices[(*index)++] = Wow_Vertex(b.x, b.y, b.z, 1, 0, color);
    vertices[(*index)++] = Wow_Vertex(top.x, top.y, top.z, 0.5f, 1, color);
    vertices[(*index)++] = Wow_Vertex(b.x, b.y, b.z, 0, 0, color);
    vertices[(*index)++] = Wow_Vertex(c.x, c.y, c.z, 1, 0, color);
    vertices[(*index)++] = Wow_Vertex(top.x, top.y, top.z, 0.5f, 1, color);
    vertices[(*index)++] = Wow_Vertex(c.x, c.y, c.z, 0, 0, color);
    vertices[(*index)++] = Wow_Vertex(d.x, d.y, d.z, 1, 0, color);
    vertices[(*index)++] = Wow_Vertex(top.x, top.y, top.z, 0.5f, 1, color);
    vertices[(*index)++] = Wow_Vertex(d.x, d.y, d.z, 0, 0, color);
    vertices[(*index)++] = Wow_Vertex(a.x, a.y, a.z, 1, 0, color);
    vertices[(*index)++] = Wow_Vertex(top.x, top.y, top.z, 0.5f, 1, color);
}

VERTEX *Wow_AppendMarkers(VERTEX *old_vertices,
                                 LPDWORD old_count,
                                 BYTE const *chunk,
                                 DWORD size,
                                 BYTE const *name_blob,
                                 DWORD name_blob_size,
                                 DWORD const *name_offsets,
                                 DWORD name_offset_count,
                                 BOOL wmo) {
    DWORD record_size = wmo ? sizeof(wowMapObjDef_t) : sizeof(wowDoodadDef_t);
    DWORD count = size / record_size;
    DWORD new_count = *old_count + count * 12;
    VERTEX *vertices = ri.MemAlloc(sizeof(VERTEX) * MAX(new_count, 1));

    if (*old_count && old_vertices) {
        memcpy(vertices, old_vertices, sizeof(VERTEX) * *old_count);
        ri.MemFree(old_vertices);
    }

    FOR_LOOP(i, count) {
        VECTOR3 p;
        if (wmo) {
            wowMapObjDef_t const *def = (wowMapObjDef_t const *)(chunk + i * record_size);
            p = Wow_ObjectPoint(def->position);
            Wow_AddMarker(vertices, old_count, p, 18.0f, Wow_Color(90, 130, 255, 255));
            wow_world.num_wmos++;
        } else {
            wowDoodadDef_t const *def = (wowDoodadDef_t const *)(chunk + i * record_size);
            LPCSTR model_path = NULL;
            float model_scale = def->scale / 1024.0f;
            float radius = 0.0f;
            float marker_size;

            if (def->flags & 0x40) {
                wow_world.num_filedata_doodads++;
            } else {
                model_path = Wow_StringRefFromOffsets(name_blob, name_blob_size, name_offsets, name_offset_count, def->name_id);
                if (model_path) {
                    radius = Wow_LoadM2BoundsRadius(model_path);
                }
            }
            marker_size = radius > 0.0f ? radius * model_scale : model_scale * 8.0f;
            marker_size = MAX(5.0f, MIN(marker_size, 80.0f));
            p = Wow_ObjectPoint(def->position);
            Wow_AddMarker(vertices, old_count, p, marker_size, radius > 0.0f ? Wow_Color(90, 230, 130, 255) : Wow_Color(230, 210, 80, 255));
            wow_world.num_doodads++;
        }
    }

    return vertices;
}

VERTEX *Wow_AppendDoodadErrorMarkers(VERTEX *old_vertices,
                                            LPDWORD old_count,
                                            BYTE const *chunk,
                                            DWORD size) {
    DWORD count = size / sizeof(wowDoodadDef_t);
    DWORD new_count = *old_count + count * 12;
    VERTEX *vertices = ri.MemAlloc(sizeof(VERTEX) * MAX(new_count, 1));

    if (*old_count && old_vertices) {
        memcpy(vertices, old_vertices, sizeof(VERTEX) * *old_count);
        ri.MemFree(old_vertices);
    }

    FOR_LOOP(i, count) {
        wowDoodadDef_t const *def = (wowDoodadDef_t const *)(chunk + i * sizeof(*def));
        if (def->flags & 0x40) {
            wow_world.num_filedata_doodads++;
            continue;
        }
        Wow_AddMarker(vertices, old_count, Wow_ObjectPoint(def->position), 6.0f, Wow_Color(255, 255, 255, 255));
        wow_world.num_doodad_instances++;
    }

    return vertices;
}
