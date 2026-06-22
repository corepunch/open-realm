#include "r_wowmap.h"

void R_RegisterMap(LPCSTR mapFileName) {
    PATHSTR path;
    LPBYTE data = NULL;
    int size;

    fprintf(stderr, "[MAP_REGISTER] Starting: %s\n", mapFileName);
    fflush(stderr);
    
    Wow_FreeWorld();
    Wow_NormalizeMapPath(mapFileName, path, sizeof(path));
    Wow_SetMapNames(path);
    fprintf(stderr, "[MAP_REGISTER] Calling Wow_LoadMapDbcFlags\n");
    fflush(stderr);
    Wow_LoadMapDbcFlags();
    fprintf(stderr, "[MAP_REGISTER] Calling Wow_LoadGroundEffectDBCs\n");
    fflush(stderr);
    Wow_LoadGroundEffectDBCs();
    fprintf(stderr, "[MAP_REGISTER] Wow_LoadGroundEffectDBCs returned\n");
    fflush(stderr);

    size = ri.FS_ReadFile(path, (void **)&data);
    if (size <= 0 || !data) {
        fprintf(stderr, "R_RegisterMap: failed to read WoW WDT %s\n", path);
        return;
    }

    if (!Wow_LoadWdtTiles(data, (DWORD)size)) {
        fprintf(stderr, "R_RegisterMap: failed to parse WoW WDT tiles %s\n", path);
        ri.FS_FreeFile(data);
        return;
    }

    fprintf(stderr,
            "R_RegisterMap: WoW map %s loaded chunks=%u doodads=%u rendered_doodads=%u doodad_models=%u missing_doodad_models=%u wmos=%u wmo_models=%u wmo_batches=%u missing_wmos=%u weighted_blend=%d doodad_error_meshes=%d\n",
            path,
            (unsigned)wow_world.num_chunks,
            (unsigned)wow_world.num_doodads,
            (unsigned)wow_world.num_doodad_instances,
            (unsigned)wow_world.num_doodad_models,
            (unsigned)wow_world.num_missing_doodad_models,
            (unsigned)wow_world.num_wmos,
            (unsigned)wow_world.num_wmo_models,
            (unsigned)wow_world.num_wmo_batches,
            (unsigned)wow_world.num_missing_wmos,
            wow_world.use_weighted_blend ? 1 : 0,
            WOW_DEBUG_DOODAD_ERROR_MESHES ? 1 : 0);
    ri.FS_FreeFile(data);
}

void R_DrawWorld(void) {
    static MATRIX4 const identity = {
        .v = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        },
    };
    MATRIX3 normal_matrix;
    wowAdtChunk_t *chunk;
    LPCTEXTURE bound_textures[5] = { NULL, NULL, NULL, NULL, NULL };
    DWORD texture_binds = 0;
    DWORD terrain_considered = 0;
    DWORD drawn_chunks = 0;
    DWORD terrain_vertices = 0;
    DWORD doodad_bucket_count = 0;
    DWORD doodad_candidates = 0;
    DWORD drawn_doodads = 0;
    DWORD drawn_wmo_groups = 0;
    DWORD drawn_wmo_batches = 0;

    if (tr.viewDef.rdflags & RDF_NOWORLDMODEL) {
        return;
    }

    Wow_LoadCameraAdts();

    if (!wow_world.chunks) {
        static BOOL logged_no_chunks = false;
        if (!logged_no_chunks) {
            fprintf(stderr, "R_DrawWorld: WoW world has no loaded terrain chunks\n");
            logged_no_chunks = true;
        }
        return;
    }

    Wow_InitTerrainShader();
    if (!wow_terrain_shader) {
        static BOOL logged_no_shader = false;
        if (!logged_no_shader) {
            fprintf(stderr, "R_DrawWorld: WoW terrain shader failed to initialize\n");
            logged_no_shader = true;
        }
        return;
    }

    R_Call(glUseProgram, wow_terrain_shader->progid);
    Matrix3_normal(&normal_matrix, &identity);
    R_Call(glUniformMatrix4fv, wow_terrain_shader->uViewProjectionMatrix, 1, GL_FALSE, tr.viewDef.viewProjectionMatrix.v);
    R_Call(glUniformMatrix4fv, wow_terrain_shader->uModelMatrix, 1, GL_FALSE, identity.v);
    R_Call(glUniformMatrix4fv, wow_terrain_shader->uLightMatrix, 1, GL_FALSE, tr.viewDef.lightMatrix.v);
    R_Call(glUniform3f, wow_terrain_shader->uLightDir,
           tr.viewDef.lightDir.x, tr.viewDef.lightDir.y, tr.viewDef.lightDir.z);
    R_Call(glUniformMatrix3fv, wow_terrain_shader->uNormalMatrix, 1, GL_TRUE, normal_matrix.v);
    R_Call(glUniform1i, wow_uUseWeightedBlend, wow_world.use_weighted_blend ? 1 : 0);
    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_TRUE);
    R_Call(glDepthFunc, GL_LEQUAL);
    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glDisable, GL_BLEND);

    for (chunk = wow_world.chunks; chunk; chunk = chunk->next) {
        if (!chunk->buffer || !chunk->num_vertices) {
            continue;
        }
        terrain_considered++;
        if (!Wow_TerrainChunkInRange(chunk)) {
            continue;
        }
        Wow_BindWorldTexture(chunk->textures[0] ? chunk->textures[0] : tr.texture[TEX_WHITE], 0, bound_textures, &texture_binds);
        Wow_BindWorldTexture(chunk->textures[1] ? chunk->textures[1] : chunk->textures[0], 1, bound_textures, &texture_binds);
        Wow_BindWorldTexture(chunk->textures[2] ? chunk->textures[2] : chunk->textures[0], 2, bound_textures, &texture_binds);
        Wow_BindWorldTexture(chunk->textures[3] ? chunk->textures[3] : chunk->textures[0], 3, bound_textures, &texture_binds);
        Wow_BindWorldTexture(chunk->alpha_texture ? chunk->alpha_texture : tr.texture[TEX_WHITE], 4, bound_textures, &texture_binds);
        R_Call(glUniform2f, wow_uAlphaOrigin, (GLfloat)chunk->alpha_index_x, (GLfloat)chunk->alpha_index_y);
        R_DrawBuffer(chunk->buffer, chunk->num_vertices);
        drawn_chunks++;
        terrain_vertices += chunk->num_vertices;
    }

    for (wowWmoInstance_t *wmo = wow_world.wmos; wmo; wmo = wmo->next) {
        if (!wmo->model || !wmo->model->groups) {
            continue;
        }
        Matrix3_normal(&normal_matrix, &wmo->matrix);
        R_Call(glUniformMatrix4fv, wow_terrain_shader->uModelMatrix, 1, GL_FALSE, wmo->matrix.v);
        R_Call(glUniformMatrix3fv, wow_terrain_shader->uNormalMatrix, 1, GL_TRUE, normal_matrix.v);
        R_Call(glUniform1i, wow_uUseWeightedBlend, 0);
        R_Call(glUniform2f, wow_uAlphaOrigin, 0.0f, 0.0f);
        FOR_LOOP(group_index, wmo->model->num_groups) {
            wowWmoGroup_t *group = &wmo->model->groups[group_index];
            if (!Wow_WmoGroupInView(group, &wmo->matrix)) {
                continue;
            }
            drawn_wmo_groups++;
            for (wowWmoBatch_t *batch = group->batches; batch; batch = batch->next) {
                if (!batch->buffer || !batch->num_vertices) {
                    continue;
                }
                Wow_BindWorldTexture(batch->texture ? batch->texture : tr.texture[TEX_WHITE], 0, bound_textures, &texture_binds);
                Wow_BindWorldTexture(batch->texture ? batch->texture : tr.texture[TEX_WHITE], 1, bound_textures, &texture_binds);
                Wow_BindWorldTexture(batch->texture ? batch->texture : tr.texture[TEX_WHITE], 2, bound_textures, &texture_binds);
                Wow_BindWorldTexture(batch->texture ? batch->texture : tr.texture[TEX_WHITE], 3, bound_textures, &texture_binds);
                Wow_BindWorldTexture(tr.texture[TEX_WHITE], 4, bound_textures, &texture_binds);
                R_DrawBuffer(batch->buffer, batch->num_vertices);
                drawn_wmo_batches++;
            }
        }
    }
    Matrix3_normal(&normal_matrix, &identity);
    R_Call(glUniformMatrix4fv, wow_terrain_shader->uModelMatrix, 1, GL_FALSE, identity.v);
    R_Call(glUniformMatrix3fv, wow_terrain_shader->uNormalMatrix, 1, GL_TRUE, normal_matrix.v);
    R_Call(glUniform1i, wow_uUseWeightedBlend, wow_world.use_weighted_blend ? 1 : 0);

    Wow_DrawGrass();

    {
        VECTOR3 camera_origin = tr.viewDef.camerastate[0].origin;
        int center_x = Wow_DoodadBucketIndex(camera_origin.x);
        int center_y = Wow_DoodadBucketIndex(camera_origin.y);
        int radius = (int)ceilf(WOW_DOODAD_DRAW_DISTANCE / WOW_DOODAD_BUCKET_SIZE) + 1;
        int min_x = MAX(0, center_x - radius);
        int max_x = MIN(WOW_DOODAD_BUCKETS - 1, center_x + radius);
        int min_y = MAX(0, center_y - radius);
        int max_y = MIN(WOW_DOODAD_BUCKETS - 1, center_y + radius);

        for (int bucket_y = min_y; bucket_y <= max_y; bucket_y++) {
            for (int bucket_x = min_x; bucket_x <= max_x; bucket_x++) {
                doodad_bucket_count++;
                for (wowDoodadInstance_t *doodad = wow_world.doodad_buckets[bucket_y][bucket_x];
                     doodad;
                     doodad = doodad->bucket_next) {
                    doodad_candidates++;
                    if (Wow_EntityInView(&doodad->entity)) {
                        R_RenderModel(&doodad->entity);
                        drawn_doodads++;
                    }
                }
            }
        }
    }

    {
        static int logged_x = -1;
        static int logged_y = -1;
        if (logged_x != wow_world.adt_center_x || logged_y != wow_world.adt_center_y) {
            logged_x = wow_world.adt_center_x;
            logged_y = wow_world.adt_center_y;
            fprintf(stderr,
                    "R_DrawWorld: terrain chunks=%u/%u considered=%u vertices=%u texture_binds=%u\n",
                    (unsigned)drawn_chunks,
                    (unsigned)wow_world.num_chunks,
                    (unsigned)terrain_considered,
                    (unsigned)terrain_vertices,
                    (unsigned)texture_binds);
            fprintf(stderr,
                    "R_DrawWorld: doodads buckets=%u candidates=%u visible=%u/%u draw_distance=%.0f\n",
                    (unsigned)doodad_bucket_count,
                    (unsigned)doodad_candidates,
                    (unsigned)drawn_doodads,
                    (unsigned)wow_world.num_doodad_instances,
                    (double)WOW_DOODAD_DRAW_DISTANCE);
            fprintf(stderr,
                    "R_DrawWorld: visible WMO groups=%u batches=%u of total batches=%u\n",
                    (unsigned)drawn_wmo_groups,
                    (unsigned)drawn_wmo_batches,
                    (unsigned)wow_world.num_wmo_batches);
        }
    }

    R_Call(glDepthMask, GL_TRUE);
    R_Call(glDisable, GL_BLEND);
    R_Call(glEnable, GL_CULL_FACE);

    if (wow_world.object_buffer && wow_world.num_object_vertices) {
        R_BindTexture(tr.texture[TEX_WHITE], 0);
        R_DrawBuffer(wow_world.object_buffer, wow_world.num_object_vertices);
    }
}

void R_DrawAlphaSurfaces(void) {
}
