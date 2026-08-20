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
    Wow_LoadMinimapTranslations();
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

typedef struct {
    DWORD considered, chunks, vertices, wmo_groups, wmo_batches, wmo_instances, wmo_models, wmo_textures, wmo_batched;
    LPCVOID model_hash[128], texture_hash[1024];
    BOOL collect;
} WOWDRAWSTATS;

/* Pointer hash counts visible model/material diversity without quadratic profiling overhead. */
static BOOL Wow_StatPointer(LPCVOID ptr, LPCVOID *table, DWORD size) {
    DWORD slot = ((uintptr_t)ptr >> 4) & (size - 1);
    FOR_LOOP(i, size) {
        DWORD at = (slot + i) & (size - 1);
        if (table[at] == ptr) return false;
        if (!table[at]) { table[at] = ptr; return true; }
    }
    return false;
}

/* This 1.5 archive predates Light*.dbc, so expose and log the outdoor fallback instead of hiding guessed data. */
static void Wow_SetupFog(void) {
    static BOOL logged;
    float start = atof(ri.CvarString("r_fog_start", WOW_WORLD_FOG_START_STRING));
    float end = atof(ri.CvarString("r_fog_end", WOW_WORLD_FOG_END_STRING));
    if (start < 0.0f) start = 0.0f;
    if (end <= start) end = start + 1.0f;
    tr.viewDef.fogEnable = R_CvarEnabled("r_fog", "1");
    tr.viewDef.fogStart = start; tr.viewDef.fogEnd = MIN(end, WOW_WORLD_FAR_CLIP);
    tr.viewDef.fogColor = (VECTOR3){ WOW_WORLD_FOG_RED, WOW_WORLD_FOG_GREEN, WOW_WORLD_FOG_BLUE };
    if (!logged) {
        logged = true;
        fprintf(stderr, "WoW fog: Light*.dbc absent; fallback start=%.0f end=%.0f hard_clip=%.0f\n",
                (double)tr.viewDef.fogStart, (double)tr.viewDef.fogEnd, (double)WOW_WORLD_FAR_CLIP);
    }
}

/* Draw the loaded terrain chunks + WMO instances using the current view/frustum. */
static void Wow_DrawTerrainAndWmos(WOWDRAWSTATS *stats) {
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
    int bound_indoor = 0;
    BOOL draw_terrain = R_CvarEnabled("r_terrain", "1") && !(wow_world.wdt_flags & 0x01);
    BOOL draw_wmos = R_CvarEnabled("r_wmos", "1");

    if (!draw_terrain && !draw_wmos) return;

    R_Call(glUseProgram, wow_terrain_shader->progid);
    Matrix3_normal(&normal_matrix, &identity);
    R_Call(glUniformMatrix4fv, wow_terrain_shader->uViewProjectionMatrix, 1, GL_FALSE, tr.viewDef.viewProjectionMatrix.v);
    R_Call(glUniformMatrix4fv, wow_terrain_shader->uModelMatrix, 1, GL_FALSE, identity.v);
    {
        VECTOR3 sun_dir;
        Wow_SunDirection(Wow_DayFraction(), &sun_dir);
        R_Call(glUniform3f, wow_uSunDir, sun_dir.x, sun_dir.y, sun_dir.z);
        R_Call(glUniform3f, wow_uSunAmbient, WOW_LIGHT_AMBIENT_R, WOW_LIGHT_AMBIENT_G, WOW_LIGHT_AMBIENT_B);
        R_Call(glUniform3f, wow_uSunDiffuse, WOW_LIGHT_DIFFUSE_R, WOW_LIGHT_DIFFUSE_G, WOW_LIGHT_DIFFUSE_B);
    }
    R_Call(glUniformMatrix3fv, wow_terrain_shader->uNormalMatrix, 1, GL_TRUE, normal_matrix.v);
    R_Call(glUniform1i, wow_uUseWeightedBlend, wow_world.use_weighted_blend ? 1 : 0);
    R_Call(glUniform1i, wow_uSingleTexture, 0);
    R_Call(glUniform1i, wow_uWmoIndoor, bound_indoor);
    R_Call(glUniform1i, wow_uFogEnable, tr.viewDef.fogEnable);
    R_Call(glUniform3f, wow_uFogColor, tr.viewDef.fogColor.x, tr.viewDef.fogColor.y, tr.viewDef.fogColor.z);
    R_Call(glUniform2f, wow_uFogParams, tr.viewDef.fogStart, tr.viewDef.fogEnd);
    R_Call(glUniform3f, wow_uFogCamera, tr.viewDef.camerastate[0].origin.x, tr.viewDef.camerastate[0].origin.y,
           tr.viewDef.camerastate[0].origin.z);
    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_TRUE);
    R_Call(glDepthFunc, GL_LEQUAL);
    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glDisable, GL_BLEND);

    for (chunk = draw_terrain ? wow_world.chunks : NULL; chunk; chunk = chunk->next) {
        if (!chunk->buffer || !chunk->num_vertices) {
            continue;
        }
        stats->considered++;
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
        stats->chunks++; stats->vertices += chunk->num_vertices;
    }

    /* WMO batches have one texture; two passes: opaque first, alpha-blended second.
     * Pre-compute per-WMO visible_group count + cam_inside once; re-use across both passes
     * so the expensive Matrix3_normal / MOLT / frustum checks don't run twice per WMO. */
    R_Call(glUniform1i, wow_uSingleTexture, 1);
    R_Call(glUniform1i, wow_uWmoBlendMode, 0);
    R_Call(glUniform1i, wow_uUseWeightedBlend, 0);     /* constant: WMOs don't use weighted alpha blend */
    R_Call(glUniform2f, wow_uAlphaOrigin, 0.0f, 0.0f); /* constant: WMOs use full-texture coords */
    {
        /* Per-WMO pre-computed data. WMOs are static so this is stable across passes. */
        enum { WMO_CACHE_MAX = 256 };
        struct { DWORD vis; uint64_t vis_bits; BOOL inside, model_batch, has_trans; MATRIX3 nm; VECTOR3 molt; } wmo_cache[WMO_CACHE_MAX];
        wowWmoInstance_t *wmo_ptrs[WMO_CACHE_MAX];
        int wmo_n = 0;
        VECTOR3 cam = tr.viewDef.camerastate[0].origin;
        for (wowWmoInstance_t *wmo = draw_wmos ? wow_world.wmos : NULL; wmo && wmo_n < WMO_CACHE_MAX; wmo = wmo->next) {
            DWORD vis = 0; uint64_t vis_bits = 0; BOOL mi, has_trans = false;
            if (!wmo->model || !wmo->model->groups) { wmo_ptrs[wmo_n++] = NULL; continue; }
            /* Cheap whole-WMO distance reject before iterating all groups. */
            if (wmo->model->has_bounds) {
                VECTOR3 wc = Matrix4_multiply_vector3(&wmo->matrix, &wmo->model->bounds_center);
                VECTOR3 d = Vector3_sub(&wc, &cam);
                if (Vector3_len(&d) - wmo->model->bounds_radius > tr.viewDef.fogEnd) {
                    wmo_ptrs[wmo_n++] = NULL; continue;
                }
            }
            FOR_LOOP(gi, wmo->model->num_groups) {
                if (Wow_WmoGroupInView(&wmo->model->groups[gi], &wmo->matrix)) {
                    vis++;
                    if (gi < 64) vis_bits |= ((uint64_t)1 << gi);
                }
            }
            if (!vis) { wmo_ptrs[wmo_n++] = NULL; continue; }
            mi = Wow_WmoContainsPoint(wmo->model, &wmo->matrix, cam);
            Matrix3_normal(&wmo_cache[wmo_n].nm, &wmo->matrix);
            Wow_ComputeMoltContribution(wmo->model, &wmo->matrix, cam, &wmo_cache[wmo_n].molt);
            /* Check if this WMO has any transparent batches — skip pass 1 if not. */
            if (wmo->model->batches) {
                for (wowWmoBatch_t *b = wmo->model->batches; b && !has_trans; b = b->next)
                    if (b->transparent) has_trans = true;
            } else {
                FOR_LOOP(gi, wmo->model->num_groups)
                    for (wowWmoBatch_t *b = wmo->model->groups[gi].batches; b && !has_trans; b = b->next)
                        if (b->transparent) has_trans = true;
            }
            wmo_cache[wmo_n].vis = vis; wmo_cache[wmo_n].vis_bits = vis_bits; wmo_cache[wmo_n].inside = mi;
            wmo_cache[wmo_n].has_trans = has_trans;
            wmo_cache[wmo_n].model_batch = wmo->model->batches != NULL && vis * WOW_WMO_MODEL_BATCH_DIVISOR >= wmo->model->num_groups;
            wmo_ptrs[wmo_n++] = wmo;
            stats->wmo_instances++;
            if (stats->collect && Wow_StatPointer(wmo->model, stats->model_hash, sizeof(stats->model_hash) / sizeof(*stats->model_hash))) stats->wmo_models++;
            stats->wmo_groups += vis;
        }
        int bound_blend_mode = 0;
        for (int wmo_pass = 0; wmo_pass < 2; wmo_pass++) {
            if (wmo_pass == 1) {
                R_Call(glEnable, GL_BLEND);
                R_Call(glDepthMask, GL_FALSE);
                R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                bound_blend_mode = 2;
            }
            for (int wi = 0; wi < wmo_n; wi++) {
                wowWmoInstance_t *wmo = wmo_ptrs[wi];
                BOOL model_batch;
                BOOL cam_inside;
                if (!wmo) continue;
                /* Skip pass 1 for WMOs that have no transparent batches — avoids uniform
                 * uploads and batch iteration when there is nothing to draw. */
                if (wmo_pass == 1 && !wmo_cache[wi].has_trans) continue;
                cam_inside = wmo_cache[wi].inside;
                model_batch = wmo_cache[wi].model_batch;
                R_Call(glUniformMatrix4fv, wow_terrain_shader->uModelMatrix, 1, GL_FALSE, wmo->matrix.v);
                R_Call(glUniformMatrix3fv, wow_terrain_shader->uNormalMatrix, 1, GL_TRUE, wmo_cache[wi].nm.v);
                R_Call(glUniform3f, wow_uWmoAmbient, wmo->model->amb_color.r / 255.0f,
                       wmo->model->amb_color.g / 255.0f, wmo->model->amb_color.b / 255.0f);
                R_Call(glUniform3f, wow_uWmoLightAdd, wmo_cache[wi].molt.x, wmo_cache[wi].molt.y, wmo_cache[wi].molt.z);
                if (model_batch) {
                    for (wowWmoBatch_t *batch = wmo->model->batches; batch; batch = batch->next) {
                        if (!batch->buffer || !batch->num_vertices) continue;
                        if ((int)batch->transparent != wmo_pass) continue;
                        if (cam_inside && !batch->indoor) continue; /* portal cull: skip exterior when inside */
                        if (bound_indoor != batch->indoor) { bound_indoor = batch->indoor; R_Call(glUniform1i, wow_uWmoIndoor, bound_indoor); }
                        if (bound_blend_mode != (int)batch->blend_mode) {
                            bound_blend_mode = (int)batch->blend_mode;
                            R_Call(glUniform1i, wow_uWmoBlendMode, bound_blend_mode);
                            if (wmo_pass) {
                                if (bound_blend_mode == 3)      { R_Call(glBlendFunc, GL_ONE, GL_ONE); }
                                else if (bound_blend_mode == 4) { R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE); }
                                else                             { R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
                            }
                        }
                        Wow_BindWorldTexture(batch->texture ? batch->texture : tr.texture[TEX_WHITE], 0, bound_textures, &texture_binds);
                        R_DrawBuffer(batch->buffer, batch->num_vertices); stats->wmo_batches++;
                        if (stats->collect && Wow_StatPointer(batch->texture, stats->texture_hash, sizeof(stats->texture_hash) / sizeof(*stats->texture_hash))) stats->wmo_textures++;
                    }
                    if (!wmo_pass) stats->wmo_batched++;
                } else {
                    FOR_LOOP(gi2, wmo->model->num_groups) {
                        wowWmoGroup_t *group = &wmo->model->groups[gi2];
                        /* Use cached vis_bits for groups 0-63; fall back to live check for the rest. */
                        if (gi2 < 64 ? !(wmo_cache[wi].vis_bits & ((uint64_t)1 << gi2))
                                     : !Wow_WmoGroupInView(group, &wmo->matrix)) continue;
                        for (wowWmoBatch_t *batch = group->batches; batch; batch = batch->next) {
                            if (!batch->buffer || !batch->num_vertices) continue;
                            if ((int)batch->transparent != wmo_pass) continue;
                            if (cam_inside && !batch->indoor) continue; /* portal cull */
                            if (bound_indoor != batch->indoor) { bound_indoor = batch->indoor; R_Call(glUniform1i, wow_uWmoIndoor, bound_indoor); }
                            if (bound_blend_mode != (int)batch->blend_mode) {
                                bound_blend_mode = (int)batch->blend_mode;
                                R_Call(glUniform1i, wow_uWmoBlendMode, bound_blend_mode);
                                if (wmo_pass) {
                                    if (bound_blend_mode == 3)      { R_Call(glBlendFunc, GL_ONE, GL_ONE); }
                                    else if (bound_blend_mode == 4) { R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE); }
                                    else                             { R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
                                }
                            }
                            Wow_BindWorldTexture(batch->texture ? batch->texture : tr.texture[TEX_WHITE], 0, bound_textures, &texture_binds);
                            R_DrawBuffer(batch->buffer, batch->num_vertices); stats->wmo_batches++;
                            if (stats->collect && Wow_StatPointer(batch->texture, stats->texture_hash, sizeof(stats->texture_hash) / sizeof(*stats->texture_hash))) stats->wmo_textures++;
                        }
                    }
                }
            }
            if (wmo_pass == 1) {
                R_Call(glDisable, GL_BLEND);
                R_Call(glDepthMask, GL_TRUE);
            }
        }
    }
    Matrix3_normal(&normal_matrix, &identity);
    R_Call(glUniformMatrix4fv, wow_terrain_shader->uModelMatrix, 1, GL_FALSE, identity.v);
    R_Call(glUniformMatrix3fv, wow_terrain_shader->uNormalMatrix, 1, GL_TRUE, normal_matrix.v);
    R_Call(glUniform1i, wow_uUseWeightedBlend, wow_world.use_weighted_blend ? 1 : 0);
    R_Call(glUniform1i, wow_uSingleTexture, 0);
    R_Call(glUniform1i, wow_uWmoIndoor, 0);
    R_Call(glUniform3f, wow_uWmoAmbient, 0.0f, 0.0f, 0.0f);
    R_Call(glUniform3f, wow_uWmoLightAdd, 0.0f, 0.0f, 0.0f);
    R_Call(glUniform1i, wow_uWmoBlendMode, 0);
}

/* Group the small visible set by static M2 so repeated trees/props share material draws. */
static BOOL Wow_QueueDoodadInstance(wowDoodadInstance_t *doodad) {
    wowDoodadModel_t *group = doodad ? doodad->group : NULL;
    MATRIX4 *matrices;
    DWORD capacity;

    if (!group || !group->can_instance) return false;
    if (group->count == group->capacity) {
        capacity = group->capacity ? group->capacity * 2 : 16;
        matrices = ri.MemAlloc(capacity * sizeof(*matrices));
        if (!matrices) return false;
        if (group->matrices) {
            memcpy(matrices, group->matrices, group->count * sizeof(*matrices));
            ri.MemFree(group->matrices);
        }
        group->matrices = matrices; group->capacity = capacity;
    }
    R_GetEntityMatrix(&doodad->entity, &group->matrices[group->count++]);
    return true;
}

/* Draw one cropped Blizzard minimap tile, rotated into the renderer's +X-right/+Y-up world view. */
static void Wow_DrawMinimapTile(LPCRECT dst, LPCTEXTURE tex, float u0, float u1, float v0, float v1) {
    VERTEX v[6] = { 0 };
    VECTOR2 uv[6] = { {u0,v1}, {u0,v0}, {u1,v0}, {u0,v1}, {u1,v0}, {u1,v1} };
    VECTOR2 pos[6] = {
        {dst->x,dst->y}, {dst->x+dst->w,dst->y}, {dst->x+dst->w,dst->y+dst->h},
        {dst->x,dst->y}, {dst->x+dst->w,dst->y+dst->h}, {dst->x,dst->y+dst->h},
    };
    FOR_LOOP(i, 6) { v[i].position = (VECTOR3){pos[i].x,pos[i].y,0}; v[i].texcoord = uv[i]; v[i].color = COLOR32_WHITE; }
    R_DrawImageBatch(tex, SHADER_UI, BLEND_MODE_BLEND, 0, false, NULL, v, 6, false);
}

/* Blizzard ships an authoritative 64x64 tile atlas; crop its local 256px tiles around the camera. */
void Wow_DrawMinimap(LPCRECT screen) {
    VECTOR3 cam = tr.viewDef.camerastate[0].origin;
    float r = WOW_MINIMAP_WORLD_RADIUS, x0 = cam.x - r, x1 = cam.x + r, y0 = cam.y - r, y1 = cam.y + r;
    int center_x = Wow_AdtIndexForWorldCoord(cam.y), center_y = Wow_AdtIndexForWorldCoord(cam.x);

    if (!R_CvarEnabled("r_minimap", "1") || !screen || (tr.viewDef.rdflags & RDF_NOWORLDMODEL)) return;
    for (int tx = center_x - 1; tx <= center_x + 1; tx++) {
        for (int ty = center_y - 1; ty <= center_y + 1; ty++) {
            float wx0 = (31 - ty) * WOW_ADT_SIZE, wx1 = (32 - ty) * WOW_ADT_SIZE;
            float wy0 = (31 - tx) * WOW_ADT_SIZE, wy1 = (32 - tx) * WOW_ADT_SIZE;
            float ix0 = MAX(x0, wx0), ix1 = MIN(x1, wx1), iy0 = MAX(y0, wy0), iy1 = MIN(y1, wy1);
            PATHSTR path; LPCSTR hash; RECT dst;
            if (tx < 0 || tx >= WOW_WDT_TILES || ty < 0 || ty >= WOW_WDT_TILES || ix0 >= ix1 || iy0 >= iy1) continue;
            hash = wow_world.minimap_hash[tx][ty];
            if (!*hash) {
                if (!wow_world.minimap_warned[tx][ty]) {
                    fprintf(stderr, "Wow_DrawMinimap: unresolved %s map%02d_%02d.blp\n", wow_world.map_name, tx, ty);
                    wow_world.minimap_warned[tx][ty] = true;
                }
                continue;
            }
            if (!wow_world.minimap_tiles[tx][ty]) {
                snprintf(path, sizeof(path), "Textures/Minimap/%s.blp", hash);
                wow_world.minimap_tiles[tx][ty] = Wow_LoadTexture(path);
            }
            dst = (RECT){ screen->x + (ix0-x0)/(2*r)*screen->w, screen->y + (y1-iy1)/(2*r)*screen->h,
                          (ix1-ix0)/(2*r)*screen->w, (iy1-iy0)/(2*r)*screen->h };
            Wow_DrawMinimapTile(&dst, wow_world.minimap_tiles[tx][ty], (wy1-iy1)/WOW_ADT_SIZE, (wy1-iy0)/WOW_ADT_SIZE,
                                (wx1-ix1)/WOW_ADT_SIZE, (wx1-ix0)/WOW_ADT_SIZE);
        }
    }
}

void R_DrawWorld(void) {
    WOWDRAWSTATS stats = { .collect = R_CvarEnabled("r_stats", "0") };
    DWORD doodad_bucket_count = 0;
    DWORD doodad_candidates = 0;
    DWORD drawn_doodads = 0;
    DWORD doodad_draws = 0, instanced_doodads = 0, fallback_doodads = 0, instanced_models = 0;

    if (tr.viewDef.rdflags & RDF_NOWORLDMODEL) {
        return;
    }

    Wow_SetupFog();
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

    Wow_DrawTerrainAndWmos(&stats);

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

        DWORD draw_start = R_GetFrameDrawCalls();
        for (wowDoodadModel_t *group = wow_world.doodad_models; group; group = group->next) group->count = 0;
        for (int bucket_y = min_y; bucket_y <= max_y; bucket_y++) {
            for (int bucket_x = min_x; bucket_x <= max_x; bucket_x++) {
                doodad_bucket_count++;
                for (wowDoodadInstance_t *doodad = wow_world.doodad_buckets[bucket_y][bucket_x];
                     doodad;
                     doodad = doodad->bucket_next) {
                    doodad_candidates++;
                    if (Wow_EntityInView(&doodad->entity)) {
                        if (Wow_QueueDoodadInstance(doodad)) instanced_doodads++;
                        else { R_RenderModel(&doodad->entity); fallback_doodads++; }
                        drawn_doodads++;
                    }
                }
            }
        }
        if (R_CvarEnabled("r_wmos", "1")) {
            if (!wow_world.wmo_doodads_built) {
                /* First time after ADT load: queue all WMO doodads into wmo_matrices scratch */
                for (wowWmoInstance_t *wmo = wow_world.wmos; wmo; wmo = wmo->next)
                    Wow_QueueWmoDoodads(wmo);
                /* Upload persistent GPU buffers; free CPU scratch */
                for (wowDoodadModel_t *group = wow_world.doodad_models; group; group = group->next) {
                    if (!group->wmo_count) continue;
                    if (R_MakeInstanceBuffer(&group->wmo_instances, group->wmo_matrices, group->wmo_count)) {
                        SAFE_DELETE(group->wmo_matrices, ri.MemFree);
                        group->wmo_capacity = 0;
                    }
                }
                wow_world.wmo_doodads_built = true;
            }
        }
        for (wowDoodadModel_t *group = wow_world.doodad_models; group; group = group->next) {
            if (!group->count) continue;
            if (!R_UpdateInstanceBuffer(&group->instances, group->matrices, group->count)) {
                /* GPU allocation failure must preserve visible authored objects through the exact path. */
                fprintf(stderr, "WoW doodads: instance upload failed for %s; drawing exact instances\n", group->path);
                for (wowDoodadInstance_t *doodad = wow_world.doodads; doodad; doodad = doodad->next)
                    if (doodad->group == group && Wow_EntityInView(&doodad->entity)) R_RenderModel(&doodad->entity);
                continue;
            }
            R_GameRenderModelInstanced(group->model, &group->instances, 0); instanced_models++;
        }
        if (R_CvarEnabled("r_wmos", "1") && wow_world.wmo_doodads_built) {
            for (wowDoodadModel_t *group = wow_world.doodad_models; group; group = group->next) {
                if (!group->wmo_count) continue;
                R_GameRenderModelInstanced(group->model, &group->wmo_instances, 0); instanced_models++;
            }
        }
        doodad_draws = R_GetFrameDrawCalls() - draw_start;
    }

    {
        static int logged_x = -1;
        static int logged_y = -1;
        if (logged_x != wow_world.adt_center_x || logged_y != wow_world.adt_center_y) {
            logged_x = wow_world.adt_center_x;
            logged_y = wow_world.adt_center_y;
            fprintf(stderr, "R_DrawWorld: terrain chunks=%u/%u considered=%u vertices=%u\n", (unsigned)stats.chunks, (unsigned)wow_world.num_chunks, (unsigned)stats.considered, (unsigned)stats.vertices);
            fprintf(stderr, "R_DrawWorld: doodads buckets=%u candidates=%u visible=%u/%u draw_distance=%.0f\n", (unsigned)doodad_bucket_count, (unsigned)doodad_candidates, (unsigned)drawn_doodads, (unsigned)wow_world.num_doodad_instances, (double)WOW_DOODAD_DRAW_DISTANCE);
            fprintf(stderr, "R_DrawWorld: visible WMO groups=%u batches=%u of total batches=%u\n", (unsigned)stats.wmo_groups, (unsigned)stats.wmo_batches, (unsigned)wow_world.num_wmo_batches);
        }
    }
    if (R_CvarEnabled("r_stats", "0")) {
        static DWORD last_stats;
        if (tr.viewDef.time - last_stats >= 1000) {
            last_stats = tr.viewDef.time;
            fprintf(stderr, "[WOW_STATS] terrain=%u/%u vertices=%u wmo=%u instances/%u models groups=%u draws=%u textures=%u model_batched=%u doodads=%u/%u draws=%u instanced=%u/%u fallback=%u\n",
                    (unsigned)stats.chunks, (unsigned)stats.considered, (unsigned)stats.vertices,
                    (unsigned)stats.wmo_instances, (unsigned)stats.wmo_models, (unsigned)stats.wmo_groups,
                    (unsigned)stats.wmo_batches, (unsigned)stats.wmo_textures, (unsigned)stats.wmo_batched,
                    (unsigned)drawn_doodads, (unsigned)doodad_candidates, (unsigned)doodad_draws,
                    (unsigned)instanced_doodads, (unsigned)instanced_models, (unsigned)fallback_doodads);
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
