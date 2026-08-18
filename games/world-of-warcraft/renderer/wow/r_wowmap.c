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

    fprintf(stderr, "R_RegisterMap: WoW map %s loaded chunks=%u doodads=%u rendered_doodads=%u doodad_models=%u missing_doodad_models=%u wmos=%u wmo_models=%u wmo_batches=%u missing_wmos=%u weighted_blend=%d doodad_error_meshes=%d\n", path, (unsigned)wow_world.num_chunks, (unsigned)wow_world.num_doodads, (unsigned)wow_world.num_doodad_instances, (unsigned)wow_world.num_doodad_models, (unsigned)wow_world.num_missing_doodad_models, (unsigned)wow_world.num_wmos, (unsigned)wow_world.num_wmo_models, (unsigned)wow_world.num_wmo_batches, (unsigned)wow_world.num_missing_wmos, wow_world.use_weighted_blend ? 1 : 0, WOW_DEBUG_DOODAD_ERROR_MESHES ? 1 : 0);
    ri.FS_FreeFile(data);
}

/* Draw the loaded terrain chunks + WMO instances using the current view/frustum.
   Shared by the main scene and the top-down minimap pass; callers set up the
   viewProjectionMatrix, frustum, viewport and scissor first. */
static void Wow_DrawTerrainAndWmos(DWORD *considered, DWORD *drawn_chunks, DWORD *vertices, DWORD *wmo_groups, DWORD *wmo_batches) {
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
    BOOL draw_terrain = R_CvarEnabled("r_terrain", "1");
    BOOL draw_wmos = R_CvarEnabled("r_wmos", "1");

    if (!draw_terrain && !draw_wmos) return;

    R_Call(glUseProgram, wow_terrain_shader->progid);
    Matrix3_normal(&normal_matrix, &identity);
    R_Call(glUniformMatrix4fv, wow_terrain_shader->uViewProjectionMatrix, 1, GL_FALSE, tr.viewDef.viewProjectionMatrix.v);
    R_Call(glUniformMatrix4fv, wow_terrain_shader->uModelMatrix, 1, GL_FALSE, identity.v);
    R_Call(glUniformMatrix4fv, wow_terrain_shader->uLightMatrix, 1, GL_FALSE, tr.viewDef.lightMatrix.v);
    R_Call(glUniformMatrix3fv, wow_terrain_shader->uNormalMatrix, 1, GL_TRUE, normal_matrix.v);
    R_Call(glUniform1i, wow_uUseWeightedBlend, wow_world.use_weighted_blend ? 1 : 0);
    R_Call(glUniform1i, wow_uSingleTexture, 0);
    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_TRUE);
    R_Call(glDepthFunc, GL_LEQUAL);
    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glDisable, GL_BLEND);

    for (chunk = draw_terrain ? wow_world.chunks : NULL; chunk; chunk = chunk->next) {
        if (!chunk->buffer || !chunk->num_vertices) {
            continue;
        }
        if (considered) (*considered)++;
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
        if (drawn_chunks) (*drawn_chunks)++;
        if (vertices) (*vertices) += chunk->num_vertices;
    }

    /* WMO batches have one texture; do not churn four terrain samplers per material. */
    R_Call(glUniform1i, wow_uSingleTexture, 1);
    for (wowWmoInstance_t *wmo = draw_wmos ? wow_world.wmos : NULL; wmo; wmo = wmo->next) {
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
            if (wmo_groups) (*wmo_groups)++;
            for (wowWmoBatch_t *batch = group->batches; batch; batch = batch->next) {
                if (!batch->buffer || !batch->num_vertices) {
                    continue;
                }
                Wow_BindWorldTexture(batch->texture ? batch->texture : tr.texture[TEX_WHITE], 0, bound_textures, &texture_binds);
                R_DrawBuffer(batch->buffer, batch->num_vertices);
                if (wmo_batches) (*wmo_batches)++;
            }
        }
    }
    Matrix3_normal(&normal_matrix, &identity);
    R_Call(glUniformMatrix4fv, wow_terrain_shader->uModelMatrix, 1, GL_FALSE, identity.v);
    R_Call(glUniformMatrix3fv, wow_terrain_shader->uNormalMatrix, 1, GL_TRUE, normal_matrix.v);
    R_Call(glUniform1i, wow_uUseWeightedBlend, wow_world.use_weighted_blend ? 1 : 0);
    R_Call(glUniform1i, wow_uSingleTexture, 0);
}

/* Live top-down terrain minimap: the classic client renders the minimap from the
   world each frame (WoW has no pre-baked minimap image like WC3's war3mapMap). */
void Wow_DrawMinimap(LPCRECT screen) {
    MATRIX4 proj, view, vp;
    VECTOR3 cam;
    viewDef_t saved;
    RECT scene;
    RECT vp_rect;

    if (!R_CvarEnabled("r_minimap", "1") || !screen || !wow_world.chunks || (tr.viewDef.rdflags & RDF_NOWORLDMODEL)) {
        return;
    }
    Wow_InitTerrainShader();
    if (!wow_terrain_shader) {
        return;
    }

    cam = tr.viewDef.camerastate[0].origin;
    Matrix4_ortho(&proj, -WOW_MINIMAP_WORLD_RADIUS, WOW_MINIMAP_WORLD_RADIUS, -WOW_MINIMAP_WORLD_RADIUS, WOW_MINIMAP_WORLD_RADIUS, 1.0f, WOW_MINIMAP_CAMERA_HEIGHT + 4000.0f);
    Matrix4_lookAt(&view, &(VECTOR3){ cam.x, cam.y, cam.z + WOW_MINIMAP_CAMERA_HEIGHT }, &(VECTOR3){ 0.0f, 0.0f, -1.0f }, &(VECTOR3){ 0.0f, 1.0f, 0.0f });
    Matrix4_multiply(&proj, &view, &vp);

    saved = tr.viewDef;
    tr.viewDef.viewProjectionMatrix = vp;
    Frustum_Calculate(&vp, &tr.viewDef.frustum);

    /* screen is in UI scene coords (top-down Y); viewport/scissor want bottom-up normalized. */
    scene = R_UISceneRect();
    vp_rect = (RECT){
        screen->x / scene.w,
        (scene.h - screen->y - screen->h) / scene.h,
        screen->w / scene.w,
        screen->h / scene.h,
    };
    R_SetupViewport(&vp_rect);
    R_SetupScissor(&vp_rect);
    R_Call(glClear, GL_DEPTH_BUFFER_BIT);

    Wow_DrawTerrainAndWmos(NULL, NULL, NULL, NULL, NULL);

    tr.viewDef = saved;
    R_RevertSettings();
}

void R_DrawWorld(void) {
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

    Wow_DrawTerrainAndWmos(&terrain_considered, &drawn_chunks, &terrain_vertices, &drawn_wmo_groups, &drawn_wmo_batches);

    if (R_CvarEnabled("r_grass", "1")) Wow_DrawGrass();

    if (R_CvarEnabled("r_doodads", "1")) {
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
            fprintf(stderr, "R_DrawWorld: terrain chunks=%u/%u considered=%u vertices=%u\n", (unsigned)drawn_chunks, (unsigned)wow_world.num_chunks, (unsigned)terrain_considered, (unsigned)terrain_vertices);
            fprintf(stderr, "R_DrawWorld: doodads buckets=%u candidates=%u visible=%u/%u draw_distance=%.0f\n", (unsigned)doodad_bucket_count, (unsigned)doodad_candidates, (unsigned)drawn_doodads, (unsigned)wow_world.num_doodad_instances, (double)WOW_DOODAD_DRAW_DISTANCE);
            fprintf(stderr, "R_DrawWorld: visible WMO groups=%u batches=%u of total batches=%u\n", (unsigned)drawn_wmo_groups, (unsigned)drawn_wmo_batches, (unsigned)wow_world.num_wmo_batches);
        }
    }
    if (R_CvarEnabled("r_stats", "0")) {
        static DWORD last_stats;
        if (tr.viewDef.time - last_stats >= 1000) {
            last_stats = tr.viewDef.time;
            fprintf(stderr, "[WOW_STATS] terrain=%u/%u vertices=%u wmo_groups=%u wmo_draws=%u doodads=%u/%u\n",
                    (unsigned)drawn_chunks, (unsigned)terrain_considered, (unsigned)terrain_vertices,
                    (unsigned)drawn_wmo_groups, (unsigned)drawn_wmo_batches,
                    (unsigned)drawn_doodads, (unsigned)doodad_candidates);
        }
    }

    R_Call(glDepthMask, GL_TRUE);
    R_Call(glDisable, GL_BLEND);
    R_Call(glEnable, GL_CULL_FACE);

    if (R_CvarEnabled("r_doodads", "1") && wow_world.object_buffer && wow_world.num_object_vertices) {
        R_BindTexture(tr.texture[TEX_WHITE], 0);
        R_DrawBuffer(wow_world.object_buffer, wow_world.num_object_vertices);
    }
}

void R_DrawAlphaSurfaces(void) {
}
