#include "r_wowmap.h"

typedef struct {
    VERTEX *vertices;
    LPTEXTURE texture;
    DWORD count, capacity;
} WOWWMOBUILD;

typedef struct {
    LPTEXTURE const *materials;
    WOWWMOBUILD *builds;
    DWORD material_count, slot_count, build_count;
} WOWWMOLOAD;

static DWORD Wow_WmoMaterialSlot(DWORD material_id, LPTEXTURE const *materials, DWORD count) {
    LPTEXTURE texture = material_id < count ? materials[material_id] : tr.texture[TEX_WHITE];
    FOR_LOOP(i, count)
        if (materials[i] == texture) return i;
    return count;
}

static BOOL Wow_WmoBuildAppend(WOWWMOBUILD *build, VERTEX vertex) {
    if (build->count == build->capacity) {
        DWORD capacity = build->capacity ? build->capacity * 2 : 256;
        VERTEX *vertices = ri.MemAlloc(capacity * sizeof(*vertices));
        if (!vertices) return false;
        if (build->vertices) {
            memcpy(vertices, build->vertices, build->count * sizeof(*vertices));
            ri.MemFree(build->vertices);
        }
        build->vertices = vertices; build->capacity = capacity;
    }
    build->vertices[build->count++] = vertex;
    return true;
}

static void Wow_WmoBuildFree(WOWWMOBUILD *builds, DWORD count) {
    if (!builds) return;
    FOR_LOOP(i, count)
        if (builds[i].vertices) ri.MemFree(builds[i].vertices);
    ri.MemFree(builds);
}

VECTOR3 Wow_ObjectPoint(wowVec3_t p) {
    return CM_WowObjectPoint(p.x, p.y, p.z);
}

void Wow_InstanceMatrix(wowMapObjDef_t const *def, LPMATRIX4 matrix) {
    MATRIX4 basis;
    MATRIX4 tmp;
    VECTOR3 origin;

    Matrix4_identity(matrix);
    origin = Wow_ObjectPoint(def->position);
    Matrix4_translate(matrix, &origin);

    Matrix4_identity(&basis);
    basis.v[0] = 0.0f;
    basis.v[1] = 1.0f;
    basis.v[2] = 0.0f;
    basis.v[4] = 0.0f;
    basis.v[5] = 0.0f;
    basis.v[6] = 1.0f;
    basis.v[8] = 1.0f;
    basis.v[9] = 0.0f;
    basis.v[10] = 0.0f;
    Matrix4_multiply(matrix, &basis, &tmp);
    *matrix = tmp;

    Matrix4_rotate(matrix, &(VECTOR3){ 0.0f, def->rotation.y - 270.0f, 0.0f }, ROTATE_XYZ);
    Matrix4_rotate(matrix, &(VECTOR3){ 0.0f, 0.0f, -def->rotation.x }, ROTATE_XYZ);
    Matrix4_rotate(matrix, &(VECTOR3){ def->rotation.z - 90.0f, 0.0f, 0.0f }, ROTATE_XYZ);
    if (def->unk) {
        float scale = def->unk / 1024.0f;
        Matrix4_scale(matrix, &(VECTOR3){ scale, scale, scale });
    }
}

void Wow_GroupPath(LPCSTR root_path, DWORD group_index, LPSTR out, DWORD out_size) {
    size_t len = strlen(root_path);
    if (len > 4 && Wow_PathHasExtension(root_path, ".wmo")) {
        snprintf(out, out_size, "%.*s_%03u.wmo", (int)(len - 4), root_path, (unsigned)group_index);
    } else {
        snprintf(out, out_size, "%s_%03u.wmo", root_path, (unsigned)group_index);
    }
}

LPCSTR Wow_StringAt(LPCSTR blob, DWORD blob_size, DWORD offset) {
    if (!blob || offset >= blob_size) {
        return NULL;
    }
    if (!memchr(blob + offset, '\0', blob_size - offset)) {
        return NULL;
    }
    return blob + offset;
}

/* MOCV fixup: pre-subtract ambient and bake interior/exterior flag into alpha.
   Algorithm from WebWowViewerCpp (deamon87) and Noggit3 (wowdev). */
static void Wow_FixMocvAlpha(BYTE *colors, DWORD color_count,
                              wowWmoBatchDef_t const *batches, DWORD batch_count,
                              DWORD trans_batch_count,
                              COLOR32 amb, DWORD mohd_flags,
                              BOOL exterior) {
    BOOL skip_base = (mohd_flags & 0x04) != 0;
    BOOL lighten   = (mohd_flags & 0x02) != 0;
    BYTE ambR = skip_base ? 0 : amb.r;
    BYTE ambG = skip_base ? 0 : amb.g;
    BYTE ambB = skip_base ? 0 : amb.b;
    int begin_second = 0;
    DWORD i;

    if (trans_batch_count > 0 && batch_count > 0) {
        DWORD last_a = trans_batch_count - 1 < batch_count ? trans_batch_count - 1 : batch_count - 1;
        begin_second = (int)batches[last_a].last_vertex + 1;
    }

    if (lighten) {
        for (i = (DWORD)begin_second; i < color_count; i++)
            colors[i * 4 + 3] = exterior ? 0xFF : 0x00;
        return;
    }

    /* Batch-A (transparent) vertices: pre-multiply rgb by (1 - alpha), subtract ambient */
    for (i = 0; i < (DWORD)begin_second && i < color_count; i++) {
        float a = colors[i * 4 + 3] / 255.f;
        int r = (int)colors[i * 4 + 2]; /* BGRA: +2 = R */
        int g = (int)colors[i * 4 + 1];
        int b = (int)colors[i * 4 + 0];
        colors[i * 4 + 2] = (BYTE)MAX(0, (int)((r - ambR) * (1.f - a) / 2.f));
        colors[i * 4 + 1] = (BYTE)MAX(0, (int)((g - ambG) * (1.f - a) / 2.f));
        colors[i * 4 + 0] = (BYTE)MAX(0, (int)((b - ambB) * (1.f - a) / 2.f));
        /* alpha left as authored for batch-A */
    }

    /* Batch-B/C vertices: additive ambient fixup + bake interior/exterior into alpha.
       Shader multiplies by 2 to cancel the /2 here. */
    for (i = (DWORD)begin_second; i < color_count; i++) {
        float a = colors[i * 4 + 3] / 255.f;
        int r = (int)colors[i * 4 + 2];
        int g = (int)colors[i * 4 + 1];
        int b = (int)colors[i * 4 + 0];
        colors[i * 4 + 2] = (BYTE)MIN(255, MAX(0, (int)((r * a / 64.f + r - ambR) / 2.f)));
        colors[i * 4 + 1] = (BYTE)MIN(255, MAX(0, (int)((g * a / 64.f + g - ambG) / 2.f)));
        colors[i * 4 + 0] = (BYTE)MIN(255, MAX(0, (int)((b * a / 64.f + b - ambB) / 2.f)));
        colors[i * 4 + 3] = exterior ? 0xFF : 0x00;
    }
}

static BOOL Wow_LoadWmoGroup(wowWmoModel_t *model, DWORD group_index, WOWWMOLOAD *load) {
    PATHSTR group_path;
    LPBYTE data = NULL;
    int size;
    DWORD offset = 0;
    BYTE const *mopy = NULL;
    DWORD mopy_count = 0;
    WORD const *indices = NULL;
    DWORD index_count = 0;
    wowVec3_t const *vertices = NULL;
    DWORD vertex_count = 0;
    wowVec3_t const *normals = NULL;
    DWORD normal_count = 0;
    BYTE const *colors = NULL;
    DWORD color_count = 0;
    BYTE *colors_copy = NULL;
    wowVec2_t const *uvs = NULL;
    DWORD uv_count = 0;
    wowWmoBatchDef_t const *batches = NULL;
    DWORD batch_count = 0;
    WORD trans_batch_count = 0;
    BOX3 group_bounds = Wow_EmptyBounds();
    BOOL group_has_bounds = false;
    BOOL indoor = false;
    WOWWMOBUILD *builds;
    DWORD build_count = load->build_count;

    Wow_GroupPath(model->path, group_index, group_path, sizeof(group_path));
    size = ri.FS_ReadFile(group_path, (void **)&data);
    if (size <= 0 || !data) {
        fprintf(stderr, "WoW WMO: missing group %s\n", group_path);
        return false;
    }

    while (offset + 8 <= (DWORD)size) {
        BYTE const *tag = data + offset;
        DWORD chunk_size = Wow_Read32(data + offset + 4);
        BYTE const *chunk = data + offset + 8;
        offset += 8;
        if (offset + chunk_size > (DWORD)size) {
            break;
        }

        if (Wow_TagEquals(tag, "PGOM")) {
            DWORD sub = 0x44;
            if (chunk_size < sub) {
                break;
            }
            indoor = (Wow_Read32(chunk + 8) & 0x2000) != 0;
            trans_batch_count = Wow_Read16(chunk + 0x30);
            while (sub + 8 <= chunk_size) {
                BYTE const *subtag = chunk + sub;
                DWORD sub_size = Wow_Read32(chunk + sub + 4);
                BYTE const *subchunk = chunk + sub + 8;
                sub += 8;
                if (sub + sub_size > chunk_size) {
                    break;
                }
                if (Wow_TagEquals(subtag, "YPOM")) {
                    mopy = subchunk;
                    mopy_count = sub_size / sizeof(wowWmoPoly_t);
                } else if (Wow_TagEquals(subtag, "IVOM")) {
                    indices = (WORD const *)subchunk;
                    index_count = sub_size / sizeof(WORD);
                } else if (Wow_TagEquals(subtag, "TVOM")) {
                    vertices = (wowVec3_t const *)subchunk;
                    vertex_count = sub_size / sizeof(wowVec3_t);
                } else if (Wow_TagEquals(subtag, "RNOM")) {
                    normals = (wowVec3_t const *)subchunk;
                    normal_count = sub_size / sizeof(wowVec3_t);
                } else if (Wow_TagEquals(subtag, "VTOM")) {
                    uvs = (wowVec2_t const *)subchunk;
                    uv_count = sub_size / sizeof(wowVec2_t);
                } else if (Wow_TagEquals(subtag, "ABOM")) {
                    batches = (wowWmoBatchDef_t const *)subchunk;
                    batch_count = sub_size / sizeof(wowWmoBatchDef_t);
                } else if (Wow_TagEquals(subtag, "VCOM")) {
                    colors = subchunk;
                    color_count = sub_size / sizeof(COLOR32);
                }
                sub += sub_size;
            }
        }
        offset += chunk_size;
    }

    if (!vertices || !indices || !vertex_count || !index_count) {
        fprintf(stderr, "WoW WMO: group %s has no drawable geometry\n", group_path);
        ri.FS_FreeFile(data);
        return false;
    }

    if (colors && color_count) {
        colors_copy = ri.MemAlloc(color_count * 4);
        if (colors_copy) {
            memcpy(colors_copy, colors, color_count * 4);
            Wow_FixMocvAlpha(colors_copy, color_count, batches, batch_count,
                             trans_batch_count, model->amb_color, model->mohd_flags, !indoor);
        }
    }

    builds = ri.MemAlloc(build_count * sizeof(*builds));
    if (!builds) {
        fprintf(stderr, "WoW WMO: failed to allocate material builders for %s\n", group_path);
        if (colors_copy) ri.MemFree(colors_copy);
        ri.FS_FreeFile(data);
        return false;
    }
    memset(builds, 0, build_count * sizeof(*builds));
    FOR_LOOP(i, build_count) builds[i].texture = i % load->slot_count < load->material_count ? load->materials[i % load->slot_count] : tr.texture[TEX_WHITE];

    if (batch_count) {
        FOR_LOOP(batch_index, batch_count) {
            wowWmoBatchDef_t const *batch = batches + batch_index;
            DWORD first_index = batch->first_index;
            DWORD num_indices = batch->num_indices;
            DWORD material_id = batch->material_id;
            DWORD slot = Wow_WmoMaterialSlot(material_id, load->materials, load->material_count) + (indoor ? load->slot_count : 0);
            WOWWMOBUILD *build = &builds[slot];

            if (first_index >= index_count || first_index + num_indices > index_count || !num_indices) {
                continue;
            }
            FOR_LOOP(i, num_indices) {
                WORD vertex_index = indices[first_index + i];
                wowVec3_t p;
                wowVec2_t uv = { 0.0f, 0.0f };
                if (vertex_index >= vertex_count) {
                    continue;
                }
                p = vertices[vertex_index];
                if (uvs && vertex_index < uv_count) {
                    uv = uvs[vertex_index];
                }
                COLOR32 color = colors_copy && vertex_index < color_count
                    ? Wow_Color(colors_copy[vertex_index * 4], colors_copy[vertex_index * 4 + 1], colors_copy[vertex_index * 4 + 2], colors_copy[vertex_index * 4 + 3])
                    : Wow_Color(127, 127, 127, 0xFF);
                VERTEX vertex = Wow_Vertex(p.x, p.y, p.z, uv.u, uv.v, color);
                if (normals && vertex_index < normal_count) vertex.normal = *(VECTOR3 const *)(normals + vertex_index);
                if (!Wow_WmoBuildAppend(build, vertex) || !Wow_WmoBuildAppend(&load->builds[slot], vertex)) {
                    fprintf(stderr, "WoW WMO: failed to grow material geometry for %s\n", group_path);
                    Wow_WmoBuildFree(builds, build_count); if (colors_copy) ri.MemFree(colors_copy); ri.FS_FreeFile(data); return false;
                }
                Wow_AddBoundsPoint(&group_bounds, &vertex.position);
                group_has_bounds = true;
            }
        }
    } else {
        FOR_LOOP(i, index_count) {
            WORD vertex_index = indices[i];
            DWORD poly_index = i / 3;
            DWORD material_id = 0;
            WOWWMOBUILD *build;
            wowVec3_t p;
            wowVec2_t uv = { 0.0f, 0.0f };
            if (vertex_index >= vertex_count) {
                continue;
            }
            if (mopy && poly_index < mopy_count) {
                material_id = ((wowWmoPoly_t const *)mopy)[poly_index].material_id;
            }
            DWORD slot = Wow_WmoMaterialSlot(material_id, load->materials, load->material_count) + (indoor ? load->slot_count : 0);
            build = &builds[slot];
            p = vertices[vertex_index];
            if (uvs && vertex_index < uv_count) {
                uv = uvs[vertex_index];
            }
            COLOR32 color = colors_copy && vertex_index < color_count
                ? Wow_Color(colors_copy[vertex_index * 4], colors_copy[vertex_index * 4 + 1], colors_copy[vertex_index * 4 + 2], colors_copy[vertex_index * 4 + 3])
                : Wow_Color(127, 127, 127, 0xFF);
            VERTEX vertex = Wow_Vertex(p.x, p.y, p.z, uv.u, uv.v, color);
            if (normals && vertex_index < normal_count) vertex.normal = *(VECTOR3 const *)(normals + vertex_index);
            if (!Wow_WmoBuildAppend(build, vertex) || !Wow_WmoBuildAppend(&load->builds[slot], vertex)) {
                fprintf(stderr, "WoW WMO: failed to grow material geometry for %s\n", group_path);
                Wow_WmoBuildFree(builds, build_count); if (colors_copy) ri.MemFree(colors_copy); ri.FS_FreeFile(data); return false;
            }
            Wow_AddBoundsPoint(&group_bounds, &vertex.position);
            group_has_bounds = true;
        }
    }

    /* Same-texture triangles are opaque under the current WMO material contract, so one VBO preserves output. */
    FOR_LOOP(i, build_count) {
        WOWWMOBUILD *build = &builds[i];
        if (build->count) {
            wowWmoBatch_t *out_batch = ri.MemAlloc(sizeof(*out_batch));
            memset(out_batch, 0, sizeof(*out_batch));
            out_batch->buffer = R_MakeVertexArrayObject(build->vertices, build->count);
            out_batch->num_vertices = build->count;
            out_batch->texture = build->texture;
            out_batch->indoor = indoor;
            out_batch->next = model->groups[group_index].batches;
            model->groups[group_index].batches = out_batch;
            wow_world.num_wmo_batches++;
        }
    }
    Wow_WmoBuildFree(builds, build_count);
    if (colors_copy) ri.MemFree(colors_copy);

    model->groups[group_index].bounds = group_bounds;
    model->groups[group_index].has_bounds = group_has_bounds;
    ri.FS_FreeFile(data);
    return true;
}

BOOL Wow_LoadWmoModel(wowWmoModel_t *model) {
    LPBYTE data = NULL;
    int size;
    DWORD offset = 0;
    DWORD group_count = 0;
    LPCSTR texture_blob = NULL;
    DWORD texture_blob_size = 0;
    BYTE const *materials_blob = NULL;
    DWORD material_count = 0;
    LPTEXTURE *materials = NULL;
    WOWWMOLOAD load = { 0 };

    size = ri.FS_ReadFile(model->path, (void **)&data);
    if (size <= 0 || !data) {
        fprintf(stderr, "WoW WMO: missing root %s\n", model->path);
        return false;
    }

    while (offset + 8 <= (DWORD)size) {
        BYTE const *tag = data + offset;
        DWORD chunk_size = Wow_Read32(data + offset + 4);
        BYTE const *chunk = data + offset + 8;
        offset += 8;
        if (offset + chunk_size > (DWORD)size) {
            break;
        }
        if (Wow_TagEquals(tag, "DHOM") && chunk_size >= 8) {
            group_count = Wow_Read32(chunk + 4);
            if (chunk_size >= 0x10) model->n_lights = Wow_Read32(chunk + 0x0C);
            if (chunk_size >= 0x20) {
                /* MOHD ambColor is BGRA in file; store with .r=R .g=G .b=B */
                model->amb_color.r = chunk[0x1E];
                model->amb_color.g = chunk[0x1D];
                model->amb_color.b = chunk[0x1C];
                model->amb_color.a = chunk[0x1F];
            }
            if (chunk_size >= 0x32) model->mohd_flags = Wow_Read16(chunk + 0x30);
        } else if (Wow_TagEquals(tag, "XTOM")) {
            texture_blob = (LPCSTR)chunk;
            texture_blob_size = chunk_size;
        } else if (Wow_TagEquals(tag, "TMOM")) {
            materials_blob = chunk;
            material_count = chunk_size / 64;
        }
        offset += chunk_size;
    }

    if (!group_count) {
        fprintf(stderr, "WoW WMO: %s has no groups\n", model->path);
        ri.FS_FreeFile(data);
        return false;
    }

    if (material_count) {
        materials = ri.MemAlloc(sizeof(*materials) * material_count);
        memset(materials, 0, sizeof(*materials) * material_count);
        FOR_LOOP(i, material_count) {
            DWORD texture_offset = Wow_Read32(materials_blob + i * 64 + 0x0c);
            LPCSTR texture_path = Wow_StringAt(texture_blob, texture_blob_size, texture_offset);
            materials[i] = texture_path ? Wow_LoadTexture(texture_path) : tr.texture[TEX_WHITE];
        }
    }

    load.materials = materials; load.material_count = material_count; load.slot_count = material_count + 1; load.build_count = load.slot_count * 2;
    load.builds = ri.MemAlloc(load.build_count * sizeof(*load.builds));
    if (!load.builds) { if (materials) ri.MemFree(materials); ri.FS_FreeFile(data); return false; }
    memset(load.builds, 0, load.build_count * sizeof(*load.builds));
    FOR_LOOP(i, load.build_count) load.builds[i].texture = i % load.slot_count < material_count ? materials[i % load.slot_count] : tr.texture[TEX_WHITE];

    model->groups = ri.MemAlloc(sizeof(*model->groups) * group_count);
    memset(model->groups, 0, sizeof(*model->groups) * group_count);
    model->num_groups = group_count;
    FOR_LOOP(i, group_count) {
        if (!Wow_LoadWmoGroup(model, i, &load)) {
            Wow_WmoBuildFree(load.builds, load.build_count);
            if (materials) ri.MemFree(materials);
            ri.FS_FreeFile(data);
            return false;
        }
    }
    /* Duplicate group geometry once on the GPU so dense views bind each material once per WMO instance. */
    FOR_LOOP(i, load.build_count) {
        WOWWMOBUILD *build = &load.builds[i];
        if (build->count) {
            wowWmoBatch_t *batch = ri.MemAlloc(sizeof(*batch));
            memset(batch, 0, sizeof(*batch));
            batch->buffer = R_MakeVertexArrayObject(build->vertices, build->count);
            batch->num_vertices = build->count; batch->texture = build->texture;
            batch->indoor = i >= load.slot_count;
            batch->next = model->batches; model->batches = batch; model->num_batches++;
        }
    }
    Wow_WmoBuildFree(load.builds, load.build_count);

    if (materials) {
        ri.MemFree(materials);
    }
    ri.FS_FreeFile(data);
    model->loaded = true;
    return true;
}

wowWmoModel_t *Wow_GetWmoModel(LPCSTR path) {
    wowWmoModel_t *model;

    if (!path || !*path) {
        return NULL;
    }
    for (model = wow_world.wmo_models; model; model = model->next) {
        if (!strcasecmp(model->path, path)) {
            return model->loaded ? model : NULL;
        }
    }

    model = ri.MemAlloc(sizeof(*model));
    memset(model, 0, sizeof(*model));
    snprintf(model->path, sizeof(model->path), "%s", path);
    model->next = wow_world.wmo_models;
    wow_world.wmo_models = model;
    wow_world.num_wmo_models++;
    if (!Wow_LoadWmoModel(model)) {
        wow_world.num_missing_wmos++;
        return NULL;
    }
    return model;
}

void Wow_AddWmoInstance(LPCSTR path, wowMapObjDef_t const *def) {
    wowWmoModel_t *model = Wow_GetWmoModel(path);
    wowWmoInstance_t *instance;

    wow_world.num_wmos++;
    if (!model || !def) {
        return;
    }

    instance = ri.MemAlloc(sizeof(*instance));
    memset(instance, 0, sizeof(*instance));
    instance->model = model;
    Wow_InstanceMatrix(def, &instance->matrix);
    instance->next = wow_world.wmos;
    wow_world.wmos = instance;
}

LPMODEL Wow_LoadDoodadModel(LPCSTR path) {
    wowDoodadModel_t *entry;

    if (!path || !*path) {
        return NULL;
    }
    for (entry = wow_world.doodad_models; entry; entry = entry->next) {
        if (!strcasecmp(entry->path, path)) {
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
