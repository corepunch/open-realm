#include "r_wowmap.h"

model_t *Wow_LoadDoodadModel(cstring_t path) {
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
    entry->can_instance = R_ModelCanStaticInstance(entry->model);
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

void Wow_AddDoodadInstance(cstring_t model_path, wowDoodadDef_t const *def) {
    wowDoodadInstance_t *instance;
    model_t *model;

    if (!model_path || !*model_path || !def) {
        wow_world.num_missing_doodad_models++;
        return;
    }
    if (def->flags & 0x40) {
        wow_world.num_filedata_doodads++;
        return;
    }
    /* Deduplicate by authoritative MDDF unique_id: same ID across ADT tiles = same placed doodad. */
    if (def->unique_id) {
        FOR_LOOP(i, wow_world.num_placed_dood_ids)
            if (wow_world.placed_dood_ids[i] == def->unique_id) return;
        if (wow_world.num_placed_dood_ids == wow_world.cap_placed_dood_ids) {
            uint32_t cap = wow_world.cap_placed_dood_ids ? wow_world.cap_placed_dood_ids * 2 : 256;
            uint32_t *arr = ri.MemAlloc(cap * sizeof(*arr));
            if (arr) {
                if (wow_world.placed_dood_ids)
                    memcpy(arr, wow_world.placed_dood_ids, wow_world.num_placed_dood_ids * sizeof(*arr));
                SAFE_DELETE(wow_world.placed_dood_ids, ri.MemFree);
                wow_world.placed_dood_ids = arr;
                wow_world.cap_placed_dood_ids = cap;
            }
        }
        if (wow_world.num_placed_dood_ids < wow_world.cap_placed_dood_ids)
            wow_world.placed_dood_ids[wow_world.num_placed_dood_ids++] = def->unique_id;
    }
    model = Wow_LoadDoodadModel(model_path);
    if (!model) {
        return;
    }

    instance = ri.MemAlloc(sizeof(*instance));
    memset(instance, 0, sizeof(*instance));
    instance->entity.origin = Wow_ObjectPoint(def->position);
    instance->entity.rotation = (vec3_t){ def->rotation.x, def->rotation.y, def->rotation.z };
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
void Wow_AddGroundEffectInstance(cstring_t model_path, vec3_t origin, float angle) {
    wowDoodadInstance_t *instance;
    model_t *model;

    model = Wow_LoadDoodadModel(model_path);
    if (!model) {
        return;
    }
    instance = ri.MemAlloc(sizeof(*instance));
    memset(instance, 0, sizeof(*instance));
    instance->entity.origin = origin;
    instance->entity.angle = angle;
    instance->entity.scale = 1.0f;
    instance->entity.model = model;
    instance->entity.radius = WOW_DOODAD_BUCKET_SIZE * 0.25f;
    instance->entity.flags = RF_NO_SHADOW | RF_GROUND_EFFECT;
    instance->next = wow_world.ground_effects;
    wow_world.ground_effects = instance;
    wow_world.num_ground_effects++;
}

void Wow_AddMarker(vertex_t *vertices, uint32_t *index, vec3_t p, float size, color32_t color) {
    vec3_t a = { p.x - size, p.y - size, p.z };
    vec3_t b = { p.x + size, p.y - size, p.z };
    vec3_t c = { p.x + size, p.y + size, p.z };
    vec3_t d = { p.x - size, p.y + size, p.z };
    vec3_t top = { p.x, p.y, p.z + size * 3.0f };
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

vertex_t *Wow_AppendMarkers(vertex_t *old_vertices,
                                 uint32_t *old_count,
                                 uint8_t const *chunk,
                                 uint32_t size,
                                 uint8_t const *name_blob,
                                 uint32_t name_blob_size,
                                 uint32_t const *name_offsets,
                                 uint32_t name_offset_count,
                                 bool wmo) {
    uint32_t record_size = wmo ? sizeof(wowMapObjDef_t) : sizeof(wowDoodadDef_t);
    uint32_t count = size / record_size;
    uint32_t new_count = *old_count + count * 12;
    vertex_t *vertices = ri.MemAlloc(sizeof(vertex_t) * MAX(new_count, 1));

    if (*old_count && old_vertices) {
        memcpy(vertices, old_vertices, sizeof(vertex_t) * *old_count);
        ri.MemFree(old_vertices);
    }

    FOR_LOOP(i, count) {
        vec3_t p;
        if (wmo) {
            wowMapObjDef_t const *def = (wowMapObjDef_t const *)(chunk + i * record_size);
            p = Wow_ObjectPoint(def->position);
            Wow_AddMarker(vertices, old_count, p, 18.0f, Wow_Color(90, 130, 255, 255));
            wow_world.num_wmos++;
        } else {
            wowDoodadDef_t const *def = (wowDoodadDef_t const *)(chunk + i * record_size);
            cstring_t model_path = NULL;
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

vertex_t *Wow_AppendDoodadErrorMarkers(vertex_t *old_vertices,
                                            uint32_t *old_count,
                                            uint8_t const *chunk,
                                            uint32_t size) {
    uint32_t count = size / sizeof(wowDoodadDef_t);
    uint32_t new_count = *old_count + count * 12;
    vertex_t *vertices = ri.MemAlloc(sizeof(vertex_t) * MAX(new_count, 1));

    if (*old_count && old_vertices) {
        memcpy(vertices, old_vertices, sizeof(vertex_t) * *old_count);
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
