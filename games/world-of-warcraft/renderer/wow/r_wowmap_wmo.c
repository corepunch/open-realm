#include "r_wowmap.h"

typedef struct {
    VERTEX *vertices;
    LPTEXTURE texture;
    DWORD count, capacity;
} WOWWMOBUILD;

typedef struct {
    LPTEXTURE const *materials;
    BYTE const *mat_blend_modes;  /* blend_modes[material_id], 0-4, size=material_count */
    WOWWMOBUILD *builds;
    DWORD material_count, slot_count, build_count;
} WOWWMOLOAD;

/* Parsed MOGP subchunk payloads: each field is a pointer into the resident group
   file image paired with its element count, filled by Wow_ParseGroupSubchunk. */
typedef struct {
    ARRAY(wowWmoPoly_t const, mopy);
    ARRAY(WORD const, indices);
    ARRAY(wowVec3_t const, vertices);
    ARRAY(wowVec3_t const, normals);
    ARRAY(BYTE const, colors);
    ARRAY(wowVec2_t const, uvs);
    ARRAY(wowWmoBatchDef_t const, batches);
} wowWmoGroupChunks_t;

/* Reversed MOGP subchunk tag -> (ptr field, count field, element size) it fills. */
static const struct { DWORD tag; size_t ptr_off, count_off, elem_size; } kGroupSubchunks[] = {
    { ID_YPOM, offsetof(wowWmoGroupChunks_t, mopy),     offsetof(wowWmoGroupChunks_t, mopy_count),     sizeof(wowWmoPoly_t) },
    { ID_IVOM, offsetof(wowWmoGroupChunks_t, indices),   offsetof(wowWmoGroupChunks_t, indices_count),  sizeof(WORD) },
    { ID_TVOM, offsetof(wowWmoGroupChunks_t, vertices),  offsetof(wowWmoGroupChunks_t, vertices_count), sizeof(wowVec3_t) },
    { ID_RNOM, offsetof(wowWmoGroupChunks_t, normals),   offsetof(wowWmoGroupChunks_t, normals_count),  sizeof(wowVec3_t) },
    { ID_VTOM, offsetof(wowWmoGroupChunks_t, uvs),       offsetof(wowWmoGroupChunks_t, uvs_count),      sizeof(wowVec2_t) },
    { ID_ABOM, offsetof(wowWmoGroupChunks_t, batches),   offsetof(wowWmoGroupChunks_t, batches_count),  sizeof(wowWmoBatchDef_t) },
    { ID_VCOM, offsetof(wowWmoGroupChunks_t, colors),    offsetof(wowWmoGroupChunks_t, colors_count),   sizeof(COLOR32) },
};

/* Fill a { pointer, count } pair at the given struct offsets from a raw subchunk.
   Used by both the group-subchunk and root-chunk schema tables. */
static void Wow_FillRef(void *base, BYTE const *chunk, DWORD chunk_size,
                        size_t ptr_off, size_t count_off, size_t elem_size) {
    *(BYTE const **)((BYTE *)base + ptr_off) = chunk;
    *(DWORD *)((BYTE *)base + count_off) = (DWORD)(chunk_size / elem_size);
}

static void Wow_ParseGroupSubchunk(wowWmoGroupChunks_t *chunks, BYTE const *subtag, BYTE const *subchunk, DWORD sub_size) {
    DWORD tag = *(DWORD const *)subtag;
    FOR_LOOP(i, sizeof(kGroupSubchunks) / sizeof(*kGroupSubchunks))
        if (tag == kGroupSubchunks[i].tag) {
            Wow_FillRef(chunks, subchunk, sub_size, kGroupSubchunks[i].ptr_off, kGroupSubchunks[i].count_off, kGroupSubchunks[i].elem_size);
            return;
        }
}

/* Per-triangle MOPY material id for the batch-less MOVI path (index_pos is a MOVI index). */
static DWORD Wow_WmoTriMaterialId(DWORD index_pos, wowWmoGroupChunks_t const *chunks) {
    DWORD poly_index = index_pos / 3;
    return (chunks->mopy && poly_index < ARRAY_COUNT(chunks->mopy)) ? chunks->mopy[poly_index].material_id : 0;
}

/* Parsed root-WMO chunk payloads: MOTX/MOMT are pointers into the resident file. */
typedef struct {
    LPCSTR texture_blob;        DWORD texture_blob_size;
    BYTE const *materials_blob; DWORD material_count;
} wowWmoRootChunks_t;

/* Reversed root chunk tag -> (ptr field, count field, element size); elem_size 1 = raw bytes. */
static const struct { DWORD tag; size_t ptr_off, count_off, elem_size; } kRootRefs[] = {
    { ID_XTOM, offsetof(wowWmoRootChunks_t, texture_blob),   offsetof(wowWmoRootChunks_t, texture_blob_size), 1 },
    { ID_TMOM, offsetof(wowWmoRootChunks_t, materials_blob), offsetof(wowWmoRootChunks_t, material_count),    64 },
};

/* Reversed root chunk tag -> (count field, array field, element size) memcpy'd into a fresh model array. */
static const struct { DWORD tag; size_t count_off, ptr_off, elem_size; } kRootArrays[] = {
    { ID_SDOM, offsetof(wowWmoModel_t, num_doodad_sets),     offsetof(wowWmoModel_t, doodad_sets),     sizeof(wowWmoDoodadSet_t) },
    { ID_DDOM, offsetof(wowWmoModel_t, num_doodad_defs),     offsetof(wowWmoModel_t, doodad_defs),     sizeof(wowWmoDoodadDef_t) },
    { ID_TLOM, offsetof(wowWmoModel_t, num_lights_parsed),   offsetof(wowWmoModel_t, lights),          sizeof(wowWmoLight_t) },
    { ID_TPOM, offsetof(wowWmoModel_t, num_portals),         offsetof(wowWmoModel_t, portals),         sizeof(wowWmoPortal_t) },
    { ID_VPOM, offsetof(wowWmoModel_t, num_portal_vertices), offsetof(wowWmoModel_t, portal_vertices), sizeof(wowVec3_t) },
    { ID_RPOM, offsetof(wowWmoModel_t, num_portal_refs),     offsetof(wowWmoModel_t, portal_refs),     sizeof(wowWmoPortalRef_t) },
};

static void Wow_AllocWmoArray(wowWmoModel_t *model, BYTE const *chunk, DWORD chunk_size,
                              size_t count_off, size_t ptr_off, size_t elem_size) {
    DWORD *count = (DWORD *)((BYTE *)model + count_off);
    void **ptr = (void **)((BYTE *)model + ptr_off);
    *count = (DWORD)(chunk_size / elem_size);
    *ptr = ri.MemAlloc(*count * elem_size);
    if (*ptr) memcpy(*ptr, chunk, *count * elem_size);
}

/* Root chunks with bespoke logic (scalar headers / owned blobs) map to these. */
static void Wow_LoadMohd(wowWmoModel_t *model, BYTE const *chunk, DWORD chunk_size) {
    if (chunk_size < 8) return;
    model->num_groups = Wow_Read32(chunk + 4);
    if (chunk_size >= 0x10) model->n_lights = Wow_Read32(chunk + 0x0C);
    if (chunk_size >= 0x20) {
        /* MOHD ambColor is BGRA in file; store with .r=R .g=G .b=B */
        model->amb_color.r = chunk[0x1E];
        model->amb_color.g = chunk[0x1D];
        model->amb_color.b = chunk[0x1C];
        model->amb_color.a = chunk[0x1F];
    }
    /* MOHD layout: nTex(4)+nGrp(4)+nPrt(4)+nLt(4)+nDN(4)+nDD(4)+nDS(4)+ambColor(4)+wmoID(4)
     * = 0x24; bounding_box.min(12)+max(12) = 0x24..0x3B; flags(2) = 0x3C */
    if (chunk_size >= 0x3C) {
        float bmin[3], bmax[3];
        memcpy(bmin, chunk + 0x24, sizeof(bmin));
        memcpy(bmax, chunk + 0x30, sizeof(bmax));
        model->bounds_center.x = (bmin[0] + bmax[0]) * 0.5f;
        model->bounds_center.y = (bmin[1] + bmax[1]) * 0.5f;
        model->bounds_center.z = (bmin[2] + bmax[2]) * 0.5f;
        VECTOR3 half = { bmax[0] - model->bounds_center.x,
                         bmax[1] - model->bounds_center.y,
                         bmax[2] - model->bounds_center.z };
        model->bounds_radius = Vector3_len(&half);
        model->has_bounds = true;
    }
    if (chunk_size >= 0x3E) model->mohd_flags = Wow_Read16(chunk + 0x3C);
}

static void Wow_LoadModn(wowWmoModel_t *model, BYTE const *chunk, DWORD chunk_size) {
    if (!chunk_size) return;
    /* MODN: null-terminated doodad model filename blob */
    model->doodad_name_blob = ri.MemAlloc(chunk_size + 1);
    if (model->doodad_name_blob) {
        memcpy(model->doodad_name_blob, chunk, chunk_size);
        model->doodad_name_blob[chunk_size] = '\0';
        model->doodad_name_blob_size = chunk_size;
    }
}

static const struct { DWORD tag; void (*load)(wowWmoModel_t *, BYTE const *, DWORD); } kRootLoaders[] = {
    { ID_DHOM, Wow_LoadMohd },
    { ID_NDOM, Wow_LoadModn },
};

/* Dispatch a root chunk: bespoke loaders first, then the pointer+count and array tables. */
static void Wow_ParseRootChunk(wowWmoModel_t *model, wowWmoRootChunks_t *chunks,
                               DWORD tag, BYTE const *chunk, DWORD chunk_size) {
    FOR_LOOP(i, sizeof(kRootLoaders) / sizeof(*kRootLoaders))
        if (tag == kRootLoaders[i].tag) { kRootLoaders[i].load(model, chunk, chunk_size); return; }
    FOR_LOOP(i, sizeof(kRootRefs) / sizeof(*kRootRefs))
        if (tag == kRootRefs[i].tag) { Wow_FillRef(chunks, chunk, chunk_size, kRootRefs[i].ptr_off, kRootRefs[i].count_off, kRootRefs[i].elem_size); return; }
    FOR_LOOP(i, sizeof(kRootArrays) / sizeof(*kRootArrays))
        if (tag == kRootArrays[i].tag) { Wow_AllocWmoArray(model, chunk, chunk_size, kRootArrays[i].count_off, kRootArrays[i].ptr_off, kRootArrays[i].elem_size); return; }
}

static DWORD Wow_WmoMaterialSlot(DWORD material_id, LPTEXTURE const *materials,
                                   BYTE const *blend_modes, DWORD count) {
    LPTEXTURE texture = material_id < count ? materials[material_id] : tr.texture[TEX_WHITE];
    BYTE blend = (blend_modes && material_id < count) ? blend_modes[material_id] : 0;
    FOR_LOOP(i, count)
        if (materials[i] == texture && (!blend_modes || blend_modes[i] == blend)) return i;
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
    if (def->scale) {
        float scale = def->scale / 1024.0f;
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
void Wow_FixMocvAlpha(BYTE *colors, DWORD color_count,
                              wowWmoBatchDef_t const *batches, DWORD batch_count,
                              DWORD trans_batch_count,
                              COLOR32 amb, DWORD mohd_flags,
                              BOOL exterior) {
    BOOL skip_base = (mohd_flags & 0x04) != 0;
    BOOL lighten   = (mohd_flags & 0x02) != 0;
    /* MOCV is BGRA; keep ambient in that same B,G,R channel order for the loop. */
    BYTE amb_c[3] = { skip_base ? 0 : amb.b, skip_base ? 0 : amb.g, skip_base ? 0 : amb.r };
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
        FOR_LOOP(c, 3) colors[i * 4 + c] = BZ_CLAMP_U8((colors[i * 4 + c] - amb_c[c]) * (1.f - a) / 2.f);
        /* alpha left as authored for batch-A */
    }

    /* Batch-B/C vertices: additive ambient fixup + bake interior/exterior into alpha.
       Shader multiplies by 2 to cancel the /2 here. */
    for (i = (DWORD)begin_second; i < color_count; i++) {
        float a = colors[i * 4 + 3] / 255.f;
        FOR_LOOP(c, 3) {
            int v = colors[i * 4 + c];
            colors[i * 4 + c] = BZ_CLAMP_U8((v * a / 64.f + v - amb_c[c]) / 2.f);
        }
        colors[i * 4 + 3] = exterior ? 0xFF : 0x00;
    }
}

static BOOL Wow_LoadWmoGroup(wowWmoModel_t *model, DWORD group_index, WOWWMOLOAD *load) {
    PATHSTR group_path;
    LPBYTE data = NULL;
    int size;
    DWORD offset = 0;
    wowWmoGroupChunks_t chunks = { 0 };
    BYTE *colors_copy = NULL;
    DWORD *material_ids = NULL;
    WORD trans_batch_count = 0;
    BOX3 group_bounds = Wow_EmptyBounds();
    BOOL group_has_bounds = false;
    BOOL indoor = false;
    WOWWMOBUILD *builds = NULL;
    DWORD build_count = load->build_count;
    BOOL ok = false;

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

        if (*(DWORD const *)tag == ID_PGOM) {
            DWORD sub = 0x44;
            if (chunk_size < sub) {
                break;
            }
            indoor = (Wow_Read32(chunk + 8) & 0x2000) != 0;
            trans_batch_count = Wow_Read16(chunk + 0x30);
            model->groups[group_index].portal_start = Wow_Read16(chunk + 0x24);
            model->groups[group_index].portal_count = Wow_Read16(chunk + 0x26);
            /* replacement_for_header_color at +0x38: overrides MOHD ambient for this group */
            if (chunk_size >= 0x3C && Wow_Read32(chunk + 0x38)) {
                model->groups[group_index].group_amb.b = chunk[0x38];
                model->groups[group_index].group_amb.g = chunk[0x39];
                model->groups[group_index].group_amb.r = chunk[0x3A];
                model->groups[group_index].group_amb.a = chunk[0x3B];
                model->groups[group_index].has_group_amb = true;
            }
            while (sub + 8 <= chunk_size) {
                BYTE const *subtag = chunk + sub;
                DWORD sub_size = Wow_Read32(chunk + sub + 4);
                BYTE const *subchunk = chunk + sub + 8;
                sub += 8;
                if (sub + sub_size > chunk_size) {
                    break;
                }
                Wow_ParseGroupSubchunk(&chunks, subtag, subchunk, sub_size);
                sub += sub_size;
            }
        }
        offset += chunk_size;
    }

    if (IS_ARRAY_EMPTY(chunks.vertices) || IS_ARRAY_EMPTY(chunks.indices)) {
        fprintf(stderr, "WoW WMO: group %s has no drawable geometry\n", group_path);
        goto cleanup;
    }

    if (!IS_ARRAY_EMPTY(chunks.colors)) {
        colors_copy = ri.MemAlloc(ARRAY_COUNT(chunks.colors) * 4);
        if (colors_copy) {
            memcpy(colors_copy, chunks.colors, ARRAY_COUNT(chunks.colors) * 4);
            Wow_FixMocvAlpha(colors_copy, ARRAY_COUNT(chunks.colors), chunks.batches, ARRAY_COUNT(chunks.batches),
                             trans_batch_count, model->amb_color, model->mohd_flags, !indoor);
        }
    }

    /* Expand MOBA batches into a per-MOVI-index material id table so the batch and
       batch-less paths share one vertex-emit loop. Uncovered indices stay 0xFFFFFFFF
       and are skipped, matching the original batch-range-only iteration. */
    if (ARRAY_COUNT(chunks.batches)) {
        material_ids = ri.MemAlloc(ARRAY_COUNT(chunks.indices) * sizeof(*material_ids));
        if (!material_ids) {
            fprintf(stderr, "WoW WMO: failed to allocate material ids for %s\n", group_path);
            goto cleanup;
        }
        memset(material_ids, 0xFF, ARRAY_COUNT(chunks.indices) * sizeof(*material_ids));
        FOR_EACH_ARRAY(wowWmoBatchDef_t const, batch, chunks.batches) {
            if (batch->first_index >= ARRAY_COUNT(chunks.indices) || batch->first_index + batch->num_indices > ARRAY_COUNT(chunks.indices) || !batch->num_indices)
                continue;
            FOR_LOOP(j, batch->num_indices) material_ids[batch->first_index + j] = batch->material_id;
        }
    }

    builds = ri.MemAlloc(build_count * sizeof(*builds));
    if (!builds) {
        fprintf(stderr, "WoW WMO: failed to allocate material builders for %s\n", group_path);
        goto cleanup;
    }
    memset(builds, 0, build_count * sizeof(*builds));
    FOR_LOOP(i, build_count) builds[i].texture = i % load->slot_count < load->material_count ? load->materials[i % load->slot_count] : tr.texture[TEX_WHITE];

    FOR_LOOP(i, ARRAY_COUNT(chunks.indices)) {
        WORD vertex_index = chunks.indices[i];
        DWORD material_id;
        DWORD slot;
        WOWWMOBUILD *build;
        wowVec3_t p;
        wowVec2_t uv = { 0.0f, 0.0f };
        if (vertex_index >= ARRAY_COUNT(chunks.vertices)) continue;
        if (ARRAY_COUNT(chunks.batches)) {
            if (material_ids[i] == 0xFFFFFFFF) continue;
            material_id = material_ids[i];
        } else {
            material_id = Wow_WmoTriMaterialId(i, &chunks);
        }
        slot = Wow_WmoMaterialSlot(material_id, load->materials, load->mat_blend_modes, load->material_count) + (indoor ? load->slot_count : 0);
        build = &builds[slot];
        p = chunks.vertices[vertex_index];
        if (chunks.uvs && vertex_index < ARRAY_COUNT(chunks.uvs)) uv = chunks.uvs[vertex_index];
        COLOR32 color = colors_copy && vertex_index < ARRAY_COUNT(chunks.colors)
            ? Wow_Color(colors_copy[vertex_index * 4], colors_copy[vertex_index * 4 + 1], colors_copy[vertex_index * 4 + 2], colors_copy[vertex_index * 4 + 3])
            : Wow_Color(127, 127, 127, 0xFF);
        VERTEX vertex = Wow_Vertex(p.x, p.y, p.z, uv.u, uv.v, color);
        if (chunks.normals && vertex_index < ARRAY_COUNT(chunks.normals)) vertex.normal = *(VECTOR3 const *)(chunks.normals + vertex_index);
        if (!Wow_WmoBuildAppend(build, vertex) || !Wow_WmoBuildAppend(&load->builds[slot], vertex)) {
            fprintf(stderr, "WoW WMO: failed to grow material geometry for %s\n", group_path);
            goto cleanup;
        }
        Wow_AddBoundsPoint(&group_bounds, &vertex.position);
        group_has_bounds = true;
    }

    /* One VBO per material slot; blend mode comes from the slot's material. */
    FOR_LOOP(i, build_count) {
        WOWWMOBUILD *build = &builds[i];
        if (build->count) {
            DWORD slot_index = (DWORD)i % load->slot_count;
            BYTE blend_mode = (load->mat_blend_modes && slot_index < load->material_count)
                              ? load->mat_blend_modes[slot_index] : 0;
            wowWmoBatch_t *out_batch = ri.MemAlloc(sizeof(*out_batch));
            memset(out_batch, 0, sizeof(*out_batch));
            out_batch->buffer = R_MakeVertexArrayObject(build->vertices, build->count);
            out_batch->num_vertices = build->count;
            out_batch->texture = build->texture;
            out_batch->indoor = indoor;
            out_batch->blend_mode = blend_mode;
            out_batch->transparent = (blend_mode >= 2);
            out_batch->next = model->groups[group_index].batches;
            model->groups[group_index].batches = out_batch;
            wow_world.num_wmo_batches++;
        }
    }

    model->groups[group_index].bounds = group_bounds;
    model->groups[group_index].has_bounds = group_has_bounds;
    ok = true;

cleanup:
    SAFE_DELETE(material_ids, ri.MemFree);
    SAFE_DELETE(colors_copy, ri.MemFree);
    if (builds) Wow_WmoBuildFree(builds, build_count);
    ri.FS_FreeFile(data);
    return ok;
}

BOOL Wow_LoadWmoModel(wowWmoModel_t *model) {
    LPBYTE data = NULL;
    int size;
    DWORD offset = 0;
    wowWmoRootChunks_t chunks = { 0 };
    LPTEXTURE *materials = NULL;
    BYTE *mat_blend_modes = NULL;
    WOWWMOLOAD load = { 0 };
    BOOL ok = false;

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
        if (offset + chunk_size > (DWORD)size) break;
        Wow_ParseRootChunk(model, &chunks, *(DWORD const *)tag, chunk, chunk_size);
        offset += chunk_size;
    }

    if (!model->num_groups) {
        fprintf(stderr, "WoW WMO: %s has no groups\n", model->path);
        goto cleanup;
    }

    if (chunks.material_count) {
        materials = ri.MemAlloc(sizeof(*materials) * chunks.material_count);
        mat_blend_modes = ri.MemAlloc(chunks.material_count);
        memset(materials, 0, sizeof(*materials) * chunks.material_count);
        memset(mat_blend_modes, 0, chunks.material_count);
        FOR_LOOP(i, chunks.material_count) {
            DWORD texture_offset = Wow_Read32(chunks.materials_blob + i * 64 + 0x0c);
            WORD blend = Wow_Read16(chunks.materials_blob + i * 64 + 0x02);
            LPCSTR texture_path = Wow_StringAt(chunks.texture_blob, chunks.texture_blob_size, texture_offset);
            materials[i] = texture_path ? Wow_LoadTexture(texture_path) : tr.texture[TEX_WHITE];
            mat_blend_modes[i] = (BYTE)(blend > 4 ? 0 : blend);
        }
    }

    load.materials = materials; load.mat_blend_modes = mat_blend_modes;
    load.material_count = chunks.material_count; load.slot_count = chunks.material_count + 1; load.build_count = load.slot_count * 2;
    load.builds = ri.MemAlloc(load.build_count * sizeof(*load.builds));
    if (!load.builds) {
        fprintf(stderr, "WoW WMO: failed to allocate group builders for %s\n", model->path);
        goto cleanup;
    }
    memset(load.builds, 0, load.build_count * sizeof(*load.builds));
    FOR_LOOP(i, load.build_count) load.builds[i].texture = i % load.slot_count < chunks.material_count ? materials[i % load.slot_count] : tr.texture[TEX_WHITE];

    model->groups = ri.MemAlloc(sizeof(*model->groups) * model->num_groups);
    memset(model->groups, 0, sizeof(*model->groups) * model->num_groups);
    FOR_LOOP(i, model->num_groups)
        if (!Wow_LoadWmoGroup(model, i, &load)) goto cleanup;

    /* Duplicate group geometry once on the GPU so dense views bind each material once per WMO instance. */
    FOR_LOOP(i, load.build_count) {
        WOWWMOBUILD *build = &load.builds[i];
        if (build->count) {
            DWORD slot_index = (DWORD)i % load.slot_count;
            BYTE blend_mode = (load.mat_blend_modes && slot_index < load.material_count)
                              ? load.mat_blend_modes[slot_index] : 0;
            wowWmoBatch_t *batch = ri.MemAlloc(sizeof(*batch));
            memset(batch, 0, sizeof(*batch));
            batch->buffer = R_MakeVertexArrayObject(build->vertices, build->count);
            batch->num_vertices = build->count; batch->texture = build->texture;
            batch->indoor = i >= load.slot_count;
            batch->blend_mode = blend_mode;
            batch->transparent = (blend_mode >= 2);
            batch->next = model->batches; model->batches = batch; model->num_batches++;
        }
    }

    model->loaded = true;
    ok = true;

cleanup:
    if (load.builds) Wow_WmoBuildFree(load.builds, load.build_count);
    SAFE_DELETE(materials, ri.MemFree);
    SAFE_DELETE(mat_blend_modes, ri.MemFree);
    ri.FS_FreeFile(data);
    return ok;
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
    instance->doodad_set = def->doodad_set;
    Wow_InstanceMatrix(def, &instance->matrix);
    instance->next = wow_world.wmos;
    wow_world.wmos = instance;
}

/* Sum the MOLT point/ambient light contributions at ref_pos (world space).
   Each OMNI/SPOT light's position is transformed from WMO local to world space via the
   instance matrix. Linear attenuation from atten_start to atten_end is applied when
   use_atten is set; AMBIENT lights (type 3) contribute fully regardless of distance.
   The result is clamped to [0,1] per channel to prevent over-brightening. */
void Wow_ComputeMoltContribution(wowWmoModel_t const *model, LPCMATRIX4 matrix,
                                  VECTOR3 ref_pos, VECTOR3 *out) {
    DWORD i;
    out->x = out->y = out->z = 0.0f;
    if (!model->lights || !model->num_lights_parsed) return;
    for (i = 0; i < model->num_lights_parsed; i++) {
        wowWmoLight_t const *lt = &model->lights[i];
        float atten = 0.0f;
        float contrib;
        if (lt->type == 3) { /* AMBIENT: global contribution, no position needed */
            atten = 1.0f;
        } else if (lt->type == 0 || lt->type == 1) { /* OMNI / SPOT: distance falloff */
            VECTOR3 local_pos = { lt->position.x, lt->position.y, lt->position.z };
            VECTOR3 world_pos = Matrix4_multiply_vector3(matrix, &local_pos);
            VECTOR3 delta = Vector3_sub(&world_pos, &ref_pos);
            float dist = Vector3_len(&delta);
            if (lt->use_atten) {
                if (dist <= lt->atten_start) {
                    atten = 1.0f;
                } else if (lt->atten_end > lt->atten_start && dist < lt->atten_end) {
                    atten = 1.0f - (dist - lt->atten_start) / (lt->atten_end - lt->atten_start);
                }
            } else {
                atten = 1.0f;
            }
        }
        contrib = atten * lt->intensity;
        out->x += contrib * lt->color.r / 255.0f;
        out->y += contrib * lt->color.g / 255.0f;
        out->z += contrib * lt->color.b / 255.0f;
    }
    out->x = MIN(1.0f, out->x);
    out->y = MIN(1.0f, out->y);
    out->z = MIN(1.0f, out->z);
}

/* Build a column-major 4x4 matrix for a WMO doodad in WMO local space.
   Combines position, quaternion rotation, and uniform scale into T*R*S. */
void Wow_WmoDoodadLocalMatrix(wowWmoDoodadDef_t const *def, LPMATRIX4 m) {
    float qx = def->quat[0], qy = def->quat[1], qz = def->quat[2], qw = def->quat[3];
    float s = def->scale;
    memset(m->v, 0, sizeof(m->v));
    m->v[0]  = s * (1.0f - 2.0f*(qy*qy + qz*qz));
    m->v[1]  = s * 2.0f*(qx*qy + qz*qw);
    m->v[2]  = s * 2.0f*(qx*qz - qy*qw);
    m->v[4]  = s * 2.0f*(qx*qy - qz*qw);
    m->v[5]  = s * (1.0f - 2.0f*(qx*qx + qz*qz));
    m->v[6]  = s * 2.0f*(qy*qz + qx*qw);
    m->v[8]  = s * 2.0f*(qx*qz + qy*qw);
    m->v[9]  = s * 2.0f*(qy*qz - qx*qw);
    m->v[10] = s * (1.0f - 2.0f*(qx*qx + qy*qy));
    m->v[12] = def->position.x;
    m->v[13] = def->position.y;
    m->v[14] = def->position.z;
    m->v[15] = 1.0f;
}

/* Queue all doodads from the WMO's selected doodad set into instanced rendering.
   Matrices are pre-composed in world/renderer space: wmo->matrix * doodad_local.
   def_groups[] is a per-def cache of group pointers filled on the first call to
   eliminate the per-frame Wow_LoadDoodadModel O(n) strcasecmp lookup. */
void Wow_QueueWmoDoodads(wowWmoInstance_t const *wmo) {
    wowWmoModel_t *model;
    wowWmoDoodadSet_t const *ds;
    DWORD i;

    if (!wmo || !wmo->model) return;
    model = wmo->model;
    if (!model->doodad_sets || wmo->doodad_set >= model->num_doodad_sets) return;
    ds = &model->doodad_sets[wmo->doodad_set];

    /* First call: build the def→group cache; subsequent frames skip the model lookup. */
    if (!model->def_groups && model->num_doodad_defs) {
        model->def_groups = ri.MemAlloc(model->num_doodad_defs * sizeof(*model->def_groups));
        if (!model->def_groups) return;
        FOR_LOOP(di, model->num_doodad_defs) {
            wowWmoDoodadDef_t const *d = &model->doodad_defs[di];
            DWORD name_off = d->name_flags & 0x00FFFFFF;
            LPCSTR path = Wow_StringAt(model->doodad_name_blob, model->doodad_name_blob_size, name_off);
            LPMODEL m; wowDoodadModel_t *g;
            if (!path || !*path) { model->def_groups[di] = NULL; continue; }
            m = Wow_LoadDoodadModel(path);
            if (!m) { model->def_groups[di] = NULL; continue; }
            for (g = wow_world.doodad_models; g; g = g->next)
                if (g->model == m) break;
            model->def_groups[di] = (g && g->can_instance) ? g : NULL;
        }
    }
    if (!model->def_groups) return;

    for (i = 0; i < ds->count; i++) {
        DWORD idx = ds->start + i;
        wowWmoDoodadDef_t const *def;
        wowDoodadModel_t *group;
        MATRIX4 local, world;

        if (idx >= model->num_doodad_defs) continue;
        def = &model->doodad_defs[idx];
        /* Phase 3.2: skip doodads that need a MOLT per-instance directional light. */
        {
            BYTE inst_flags = (BYTE)(def->name_flags >> 24);
            if ((inst_flags & 0x04) && def->color.a < model->num_lights_parsed) continue;
        }

        group = model->def_groups[idx];
        if (!group) continue;

        Wow_WmoDoodadLocalMatrix(def, &local);
        Matrix4_multiply(&wmo->matrix, &local, &world);

        if (group->wmo_count == group->wmo_capacity) {
            DWORD capacity = group->wmo_capacity ? group->wmo_capacity * 2 : 16;
            MATRIX4 *matrices = ri.MemAlloc(capacity * sizeof(*matrices));
            if (!matrices) continue;
            if (group->wmo_matrices) {
                memcpy(matrices, group->wmo_matrices, group->wmo_count * sizeof(*matrices));
                ri.MemFree(group->wmo_matrices);
            }
            group->wmo_matrices = matrices;
            group->wmo_capacity = capacity;
        }
        group->wmo_matrices[group->wmo_count++] = world;
    }
}
