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
void Wow_FixMocvAlpha(BYTE *colors, DWORD color_count,
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
                if (*(DWORD const *)subtag == ID_YPOM) {
                    mopy = subchunk;
                    mopy_count = sub_size / sizeof(wowWmoPoly_t);
                } else if (*(DWORD const *)subtag == ID_IVOM) {
                    indices = (WORD const *)subchunk;
                    index_count = sub_size / sizeof(WORD);
                } else if (*(DWORD const *)subtag == ID_TVOM) {
                    vertices = (wowVec3_t const *)subchunk;
                    vertex_count = sub_size / sizeof(wowVec3_t);
                } else if (*(DWORD const *)subtag == ID_RNOM) {
                    normals = (wowVec3_t const *)subchunk;
                    normal_count = sub_size / sizeof(wowVec3_t);
                } else if (*(DWORD const *)subtag == ID_VTOM) {
                    uvs = (wowVec2_t const *)subchunk;
                    uv_count = sub_size / sizeof(wowVec2_t);
                } else if (*(DWORD const *)subtag == ID_ABOM) {
                    batches = (wowWmoBatchDef_t const *)subchunk;
                    batch_count = sub_size / sizeof(wowWmoBatchDef_t);
                } else if (*(DWORD const *)subtag == ID_VCOM) {
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
            DWORD slot = Wow_WmoMaterialSlot(material_id, load->materials, load->mat_blend_modes, load->material_count) + (indoor ? load->slot_count : 0);
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
            DWORD slot = Wow_WmoMaterialSlot(material_id, load->materials, load->mat_blend_modes, load->material_count) + (indoor ? load->slot_count : 0);
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
    BYTE *mat_blend_modes = NULL;
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
        if (*(DWORD const *)tag == ID_DHOM && chunk_size >= 8) {
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
        } else if (*(DWORD const *)tag == ID_XTOM) {
            texture_blob = (LPCSTR)chunk;
            texture_blob_size = chunk_size;
        } else if (*(DWORD const *)tag == ID_TMOM) {
            materials_blob = chunk;
            material_count = chunk_size / 64;
        } else if (*(DWORD const *)tag == ID_NDOM && chunk_size > 0) {
            /* MODN: null-terminated doodad model filename blob */
            model->doodad_name_blob = ri.MemAlloc(chunk_size + 1);
            if (model->doodad_name_blob) {
                memcpy(model->doodad_name_blob, chunk, chunk_size);
                model->doodad_name_blob[chunk_size] = '\0';
                model->doodad_name_blob_size = chunk_size;
            }
        } else if (*(DWORD const *)tag == ID_SDOM && chunk_size >= sizeof(wowWmoDoodadSet_t)) {
            /* MODS: doodad sets array */
            model->num_doodad_sets = chunk_size / sizeof(wowWmoDoodadSet_t);
            model->doodad_sets = ri.MemAlloc(model->num_doodad_sets * sizeof(wowWmoDoodadSet_t));
            if (model->doodad_sets)
                memcpy(model->doodad_sets, chunk, model->num_doodad_sets * sizeof(wowWmoDoodadSet_t));
        } else if (*(DWORD const *)tag == ID_DDOM && chunk_size >= sizeof(wowWmoDoodadDef_t)) {
            /* MODD: doodad definitions array */
            model->num_doodad_defs = chunk_size / sizeof(wowWmoDoodadDef_t);
            model->doodad_defs = ri.MemAlloc(model->num_doodad_defs * sizeof(wowWmoDoodadDef_t));
            if (model->doodad_defs)
                memcpy(model->doodad_defs, chunk, model->num_doodad_defs * sizeof(wowWmoDoodadDef_t));
        } else if (*(DWORD const *)tag == ID_TLOM && chunk_size >= sizeof(wowWmoLight_t)) {
            /* MOLT: light array (used for doodad directional lighting) */
            model->num_lights_parsed = chunk_size / sizeof(wowWmoLight_t);
            model->lights = ri.MemAlloc(model->num_lights_parsed * sizeof(wowWmoLight_t));
            if (model->lights)
                memcpy(model->lights, chunk, model->num_lights_parsed * sizeof(wowWmoLight_t));
        } else if (*(DWORD const *)tag == ID_TPOM && chunk_size >= sizeof(wowWmoPortal_t)) {
            /* MOPT: portal plane definitions */
            model->num_portals = chunk_size / sizeof(wowWmoPortal_t);
            model->portals = ri.MemAlloc(model->num_portals * sizeof(wowWmoPortal_t));
            if (model->portals)
                memcpy(model->portals, chunk, model->num_portals * sizeof(wowWmoPortal_t));
        } else if (*(DWORD const *)tag == ID_VPOM && chunk_size >= sizeof(wowVec3_t)) {
            /* MOPV: portal polygon vertices */
            model->num_portal_vertices = chunk_size / sizeof(wowVec3_t);
            model->portal_vertices = ri.MemAlloc(model->num_portal_vertices * sizeof(wowVec3_t));
            if (model->portal_vertices)
                memcpy(model->portal_vertices, chunk, model->num_portal_vertices * sizeof(wowVec3_t));
        } else if (*(DWORD const *)tag == ID_RPOM && chunk_size >= sizeof(wowWmoPortalRef_t)) {
            /* MOPR: per-group portal references */
            model->num_portal_refs = chunk_size / sizeof(wowWmoPortalRef_t);
            model->portal_refs = ri.MemAlloc(model->num_portal_refs * sizeof(wowWmoPortalRef_t));
            if (model->portal_refs)
                memcpy(model->portal_refs, chunk, model->num_portal_refs * sizeof(wowWmoPortalRef_t));
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
        mat_blend_modes = ri.MemAlloc(material_count);
        memset(materials, 0, sizeof(*materials) * material_count);
        memset(mat_blend_modes, 0, material_count);
        FOR_LOOP(i, material_count) {
            DWORD texture_offset = Wow_Read32(materials_blob + i * 64 + 0x0c);
            WORD blend = Wow_Read16(materials_blob + i * 64 + 0x02);
            LPCSTR texture_path = Wow_StringAt(texture_blob, texture_blob_size, texture_offset);
            materials[i] = texture_path ? Wow_LoadTexture(texture_path) : tr.texture[TEX_WHITE];
            mat_blend_modes[i] = (BYTE)(blend > 4 ? 0 : blend);
        }
    }

    load.materials = materials; load.mat_blend_modes = mat_blend_modes;
    load.material_count = material_count; load.slot_count = material_count + 1; load.build_count = load.slot_count * 2;
    load.builds = ri.MemAlloc(load.build_count * sizeof(*load.builds));
    if (!load.builds) { if (materials) ri.MemFree(materials); if (mat_blend_modes) ri.MemFree(mat_blend_modes); ri.FS_FreeFile(data); return false; }
    memset(load.builds, 0, load.build_count * sizeof(*load.builds));
    FOR_LOOP(i, load.build_count) load.builds[i].texture = i % load.slot_count < material_count ? materials[i % load.slot_count] : tr.texture[TEX_WHITE];

    model->groups = ri.MemAlloc(sizeof(*model->groups) * group_count);
    memset(model->groups, 0, sizeof(*model->groups) * group_count);
    model->num_groups = group_count;
    FOR_LOOP(i, group_count) {
        if (!Wow_LoadWmoGroup(model, i, &load)) {
            Wow_WmoBuildFree(load.builds, load.build_count);
            if (materials)       ri.MemFree(materials);
            if (mat_blend_modes) ri.MemFree(mat_blend_modes);
            ri.FS_FreeFile(data);
            return false;
        }
    }
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
    Wow_WmoBuildFree(load.builds, load.build_count);

    if (materials)       ri.MemFree(materials);
    if (mat_blend_modes) ri.MemFree(mat_blend_modes);
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
   Matrices are pre-composed in world/renderer space: wmo->matrix * doodad_local. */
void Wow_QueueWmoDoodads(wowWmoInstance_t const *wmo) {
    wowWmoModel_t const *model;
    wowWmoDoodadSet_t const *ds;
    DWORD i;

    if (!wmo || !wmo->model) return;
    model = wmo->model;
    if (!model->doodad_sets || wmo->doodad_set >= model->num_doodad_sets) return;
    ds = &model->doodad_sets[wmo->doodad_set];

    for (i = 0; i < ds->count; i++) {
        DWORD idx = ds->start + i;
        wowWmoDoodadDef_t const *def;
        DWORD name_offset;
        LPCSTR model_path;
        LPMODEL m;
        wowDoodadModel_t *group;
        MATRIX4 local, world;

        if (idx >= model->num_doodad_defs) continue;
        def = &model->doodad_defs[idx];
        /* Phase 3.2: skip doodads that need a MOLT per-instance directional light.
           Instanced rendering cannot vary the light direction per matrix.
           These will be rendered correctly once per-entity MOLT lighting is added. */
        {
            BYTE inst_flags = (BYTE)(def->name_flags >> 24);
            if ((inst_flags & 0x04) && def->color.a < model->num_lights_parsed)
                continue;
        }
        name_offset = def->name_flags & 0x00FFFFFF;
        model_path = Wow_StringAt(model->doodad_name_blob, model->doodad_name_blob_size, name_offset);
        if (!model_path || !*model_path) continue;

        m = Wow_LoadDoodadModel(model_path);
        if (!m) continue;

        for (group = wow_world.doodad_models; group; group = group->next)
            if (group->model == m) break;
        if (!group || !group->can_instance) continue;

        Wow_WmoDoodadLocalMatrix(def, &local);
        Matrix4_multiply(&wmo->matrix, &local, &world);

        if (group->count == group->capacity) {
            DWORD capacity = group->capacity ? group->capacity * 2 : 16;
            MATRIX4 *matrices = ri.MemAlloc(capacity * sizeof(*matrices));
            if (!matrices) continue;
            if (group->matrices) {
                memcpy(matrices, group->matrices, group->count * sizeof(*matrices));
                ri.MemFree(group->matrices);
            }
            group->matrices = matrices;
            group->capacity = capacity;
        }
        group->matrices[group->count++] = world;
    }
}
