#include "r_wowmap.h"

typedef struct {
    DWORD magic;
    DWORD records;
    DWORD fields;
    DWORD record_size;
    DWORD string_size;
} wowDbcHeader_t;

// GroundEffectTexture.dbc cache: maps effect_id to doodad information
static wowGroundEffectTexture_t *wow_ground_effect_textures = NULL;
static DWORD wow_ground_effect_texture_count = 0;
static BOOL wow_ground_effect_textures_loaded = false;
static BOOL wow_ground_effect_textures_attempted = false;

// GroundEffectDoodad.dbc cache: maps doodad_id to model path info
#define WOW_MAX_GROUND_EFFECT_DOODADS 512
static wowGroundEffectDoodad_t wow_ground_effect_doodads[WOW_MAX_GROUND_EFFECT_DOODADS];
static DWORD wow_ground_effect_doodad_count = 0;
static BOOL wow_ground_effect_doodads_loaded = false;

static float Wow_GrassClamp(float value, float min_value, float max_value) {
    return MAX(min_value, MIN(value, max_value));
}

static DWORD Wow_GrassHash(DWORD value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

static float Wow_GrassRandom(LPDWORD seed) {
    *seed = Wow_GrassHash(*seed + 0x9e3779b9U);
    return (float)(*seed & 0xffff) / 65535.0f;
}

void Wow_LoadGroundEffectDBCs(void) {
    LPBYTE data;
    DWORD size = 0, records, record_size;
    DWORD records_to_copy = 0;
    BYTE const *records_base;

    if (wow_ground_effect_textures_attempted) {
        return;
    }
    wow_ground_effect_textures_attempted = true;

    fprintf(stderr, "[GRASS] Wow_LoadGroundEffectDBCs: Starting load\n");
    fflush(stderr);

    // Load GroundEffectTexture.dbc
    fprintf(stderr, "[GRASS] Loading GroundEffectTexture.dbc...\n");
    fflush(stderr);
    size = ri.FS_ReadFile("DBFilesClient\\GroundEffectTexture.dbc", (void **)&data);
    fprintf(stderr, "[GRASS] FS_ReadFile returned size=%u\n", (unsigned)size);
    fflush(stderr);

    if (data && size >= sizeof(wowDbcHeader_t)) {
        wowDbcHeader_t const *header = (wowDbcHeader_t const *)data;

        fprintf(stderr, "[GRASS] Validating WDBC header...\n");
        if (header->magic == MAKEFOURCC('W', 'D', 'B', 'C')) {
            records = header->records;
            record_size = header->record_size;

            fprintf(stderr, "[GRASS] WDBC: records=%u record_size=%u\n",
                    (unsigned)records, (unsigned)record_size);

            if (records > 0 && record_size == sizeof(*wow_ground_effect_textures) &&
                sizeof(*header) + (uint64_t)records * (uint64_t)record_size + header->string_size <= (uint64_t)size) {
                records_base = data + sizeof(*header);
                records_to_copy = records;
                wow_ground_effect_textures = ri.MemAlloc(sizeof(*wow_ground_effect_textures) * records_to_copy);
                if (!wow_ground_effect_textures) {
                    fprintf(stderr, "[GRASS] allocation failed for %u GroundEffectTexture rows\n",
                            (unsigned)records_to_copy);
                    ri.FS_FreeFile(data);
                    wow_ground_effect_doodads_loaded = true;
                    return;
                }

                fprintf(stderr, "[GRASS] Copying %u records...\n", (unsigned)records_to_copy);
                memcpy(wow_ground_effect_textures,
                       records_base,
                       sizeof(*wow_ground_effect_textures) * records_to_copy);

                wow_ground_effect_texture_count = records_to_copy;
                wow_ground_effect_textures_loaded = true;
                fprintf(stderr, "[GRASS] Successfully loaded %u GroundEffectTexture records\n",
                        (unsigned)records);
            } else {
                fprintf(stderr, "[GRASS] WDBC validation failed: record_size=%u records=%u size=%u\n",
                        (unsigned)record_size, (unsigned)records, (unsigned)size);
            }
        } else {
            fprintf(stderr, "[GRASS] WDBC magic check failed\n");
        }
        ri.FS_FreeFile(data);
    } else {
        fprintf(stderr, "[GRASS] FS_ReadFile failed or invalid size\n");
    }

    // Load GroundEffectDoodad.dbc (if needed)
    // Note: GroundEffectDoodad.dbc contains ID, name_id, model_id
    // For now, we just track that we would load it
    // The actual doodad model paths come from the MMDX/MMID chunks in ADTs
    wow_ground_effect_doodads_loaded = true;

    fprintf(stderr, "[GRASS] Wow_LoadGroundEffectDBCs: Complete\n");
    fflush(stderr);
}

static wowGroundEffectTexture_t *Wow_GetGroundEffectTexture(DWORD effect_id) {
    if (!wow_ground_effect_textures_attempted) {
        Wow_LoadGroundEffectDBCs();
    }

    if (!wow_ground_effect_textures_loaded || effect_id == 0 || effect_id == 0xFFFFFFFFU) {
        return NULL;
    }

    FOR_LOOP(i, wow_ground_effect_texture_count) {
        if (wow_ground_effect_textures[i].id == effect_id) {
            return &wow_ground_effect_textures[i];
        }
    }

    return NULL;
}

static DWORD Wow_SelectDoodadFromWeights(DWORD const weights[4], LPDWORD seed) {
    DWORD total_weight = weights[0] + weights[1] + weights[2] + weights[3];
    DWORD roll;
    
    if (total_weight == 0) {
        return 0; // No valid doodads
    }
    
    roll = (DWORD)(Wow_GrassRandom(seed) * total_weight);
    
    if (roll < weights[0]) return 0;
    if (roll < weights[0] + weights[1]) return 1;
    if (roll < weights[0] + weights[1] + weights[2]) return 2;
    return 3;
}

static DWORD Wow_GrassLayerSlot(wowLayer_t const *layers, DWORD layer_count, DWORD wanted_layer) {
    DWORD unique_texture_ids[4] = { 0, 0, 0, 0 };
    DWORD unique_count = 0;

    FOR_LOOP(layer_index, MIN(layer_count, 4)) {
        DWORD slot = Wow_AlphaSlotForTexture(unique_texture_ids, &unique_count, layers[layer_index].texture_id);
        if (layer_index == wanted_layer) {
            return slot;
        }
    }
    return 0;
}

static BYTE Wow_GrassLayerCoverage(BYTE const alpha[4][WOW_ALPHA_TEXELS],
                                   wowLayer_t const *layers,
                                   DWORD layer_count,
                                   DWORD layer_index,
                                   DWORD alpha_index) {
    DWORD slot;

    if (!layers || layer_index >= layer_count || alpha_index >= WOW_ALPHA_TEXELS) {
        return 0;
    }

    slot = Wow_GrassLayerSlot(layers, layer_count, layer_index);
    if (layer_index == 0) {
        int coverage = 255;
        FOR_LOOP(i, 3) {
            coverage -= alpha[i + 1][alpha_index];
        }
        return (BYTE)MAX(0, MIN(coverage, 255));
    }
    return alpha[slot][alpha_index];
}

static BYTE Wow_GrassEffectCoverage(BYTE const alpha[4][WOW_ALPHA_TEXELS],
                                    wowLayer_t const *layers,
                                    DWORD layer_count,
                                    DWORD alpha_index) {
    BYTE best = 0;

    if (!layers) {
        return 0;
    }

    FOR_LOOP(layer_index, MIN(layer_count, 4)) {
        BYTE coverage;
        if (!layers[layer_index].effect_id || layers[layer_index].effect_id == 0xFFFFFFFFU) {
            continue;
        }
        coverage = Wow_GrassLayerCoverage(alpha, layers, layer_count, layer_index, alpha_index);
        if (coverage > best) {
            best = coverage;
        }
    }
    return best;
}

// Get the effect_id of the layer with the highest coverage
static DWORD Wow_GrassEffectIdForCoverage(BYTE const alpha[4][WOW_ALPHA_TEXELS],
                                         wowLayer_t const *layers,
                                         DWORD layer_count,
                                         DWORD alpha_index) {
    BYTE best = 0;
    DWORD best_effect_id = 0;

    if (!layers) {
        return 0;
    }

    FOR_LOOP(layer_index, MIN(layer_count, 4)) {
        BYTE coverage;
        if (!layers[layer_index].effect_id || layers[layer_index].effect_id == 0xFFFFFFFFU) {
            continue;
        }
        coverage = Wow_GrassLayerCoverage(alpha, layers, layer_count, layer_index, alpha_index);
        if (coverage > best) {
            best = coverage;
            best_effect_id = layers[layer_index].effect_id;
        }
    }
    return best_effect_id;
}

void Wow_BuildGrassForChunk(wowAdtChunk_t *chunk,
                            BYTE const alpha[4][WOW_ALPHA_TEXELS],
                            wowLayer_t const *layers,
                            DWORD layer_count) {
    enum {
        WOW_GRASS_SAMPLES_PER_AXIS = 8 / WOW_GRASS_CELL_STEP,
        WOW_GRASS_MAX_CLUMPS_PER_SAMPLE = 4,
        WOW_GRASS_MAX_VERTICES = WOW_GRASS_SAMPLES_PER_AXIS * WOW_GRASS_SAMPLES_PER_AXIS *
                                 WOW_GRASS_MAX_CLUMPS_PER_SAMPLE * WOW_GRASS_VERTICES_PER_CLUMP,
    };
    VERTEX *vertices;
    DWORD num_vertices = 0;
    float density = Wow_GrassClamp(WOW_GRASS_DENSITY, 0.0f, 2.0f);

    if (!chunk || !alpha || !layers || layer_count == 0 || density <= 0.0f) {
        return;
    }

    vertices = ri.MemAlloc(sizeof(VERTEX) * WOW_GRASS_MAX_VERTICES);
    if (!vertices) {
        return;
    }

    chunk->grass_bounds = Wow_EmptyBounds();
    for (int row = 0; row < 8; row += WOW_GRASS_CELL_STEP) {
        for (int col = 0; col < 8; col += WOW_GRASS_CELL_STEP) {
            DWORD seed = (chunk->alpha_index_x * 73856093U) ^
                         (chunk->alpha_index_y * 19349663U) ^
                         ((DWORD)row * 83492791U) ^
                         ((DWORD)col * 2654435761U);
            float local_row = row + 0.20f + Wow_GrassRandom(&seed) * (WOW_GRASS_CELL_STEP - 0.40f);
            float local_col = col + 0.20f + Wow_GrassRandom(&seed) * (WOW_GRASS_CELL_STEP - 0.40f);
            int cell_row = (int)floorf(MIN(local_row, 7.999f));
            int cell_col = (int)floorf(MIN(local_col, 7.999f));
            int alpha_x = MAX(0, MIN((int)(local_col * 8.0f), 63));
            int alpha_y = MAX(0, MIN((int)(local_row * 8.0f), 63));
            DWORD effect_id = Wow_GrassEffectIdForCoverage(alpha, layers, layer_count, alpha_y * 64 + alpha_x);
            BYTE coverage = Wow_GrassEffectCoverage(alpha, layers, layer_count, alpha_y * 64 + alpha_x);
            int clumps;
            wowGroundEffectTexture_t *ground_effect = NULL;

            if (coverage < 32 || !effect_id) {
                continue;
            }

            // Look up the ground effect for this layer
            ground_effect = Wow_GetGroundEffectTexture(effect_id);

            clumps = MAX(1, (int)ceilf(((float)coverage / 255.0f) * density * 4.0f));
            clumps = MIN(clumps, WOW_GRASS_MAX_CLUMPS_PER_SAMPLE);
            FOR_LOOP(clump, clumps) {
                float row_jitter = local_row + (Wow_GrassRandom(&seed) - 0.5f) * 0.45f;
                float col_jitter = local_col + (Wow_GrassRandom(&seed) - 0.5f) * 0.45f;
                float height;
                VECTOR3 origin;
                float angle;
                float blade_height;
                float blade_width;
                COLOR32 color;

                row_jitter = Wow_GrassClamp(row_jitter, 0.001f, 7.999f);
                col_jitter = Wow_GrassClamp(col_jitter, 0.001f, 7.999f);
                cell_row = (int)floorf(row_jitter);
                cell_col = (int)floorf(col_jitter);
                if (!Wow_HeightInCell(chunk->heights,
                                      cell_row,
                                      cell_col,
                                      row_jitter - cell_row,
                                      col_jitter - cell_col,
                                      &height)) {
                    continue;
                }

                origin = (VECTOR3){
                    chunk->position.x - row_jitter * WOW_ADT_UNIT_SIZE,
                    chunk->position.y - col_jitter * WOW_ADT_UNIT_SIZE,
                    chunk->position.z + height + 0.02f,
                };
                angle = Wow_GrassRandom(&seed) * 6.2831853f;
                blade_height = 0.20f + Wow_GrassRandom(&seed) * 0.22f;
                blade_width = 0.12f + Wow_GrassRandom(&seed) * 0.12f;
                
                // Use proper WoW grass colors based on GroundEffect data
                // If we have ground effect info, vary colors realistically
                // Otherwise fall back to default green grass
                if (ground_effect) {
                    BYTE green = (BYTE)(105 + (Wow_GrassRandom(&seed) * 55.0f));
                    BYTE red = (BYTE)(42 + (Wow_GrassRandom(&seed) * 32.0f));
                    BYTE blue = (BYTE)(24 + (Wow_GrassRandom(&seed) * 24.0f));
                    color = (COLOR32){ red, green, blue, 210 };
                } else {
                    BYTE green = (BYTE)(100 + Wow_GrassRandom(&seed) * 62.0f);
                    color = (COLOR32){
                        (BYTE)(40 + Wow_GrassRandom(&seed) * 26.0f),
                        green,
                        (BYTE)(26 + Wow_GrassRandom(&seed) * 20.0f),
                        210,
                    };
                }

                if (num_vertices + WOW_GRASS_VERTICES_PER_CLUMP > WOW_GRASS_MAX_VERTICES) {
                    break;
                }
                
                // Emit two perpendicular quad cards (billboard-style grass clump).
                VECTOR3 right = { cosf(angle) * blade_width, sinf(angle) * blade_width, 0.0f };
                VECTOR3 normal = { -right.y, right.x, 0.10f };
                VECTOR3 base_left = { origin.x - right.x, origin.y - right.y, origin.z };
                VECTOR3 base_right = { origin.x + right.x, origin.y + right.y, origin.z };
                VECTOR3 top_left = { base_left.x, base_left.y, origin.z + blade_height };
                VECTOR3 top_right = { base_right.x, base_right.y, origin.z + blade_height };

                if (Vector3_lengthsq(&normal) > 0.000001f) {
                    Vector3_normalize(&normal);
                } else {
                    normal = (VECTOR3){ 0.0f, 0.0f, 1.0f };
                }

                // First quad
                VERTEX *v = &vertices[num_vertices++];
                *v = Wow_Vertex(base_left.x, base_left.y, base_left.z, 0.0f, 0.0f, color);
                v->normal = normal;

                v = &vertices[num_vertices++];
                *v = Wow_Vertex(base_right.x, base_right.y, base_right.z, 1.0f, 0.0f, color);
                v->normal = normal;

                v = &vertices[num_vertices++];
                *v = Wow_Vertex(top_right.x, top_right.y, top_right.z, 1.0f, 1.0f, color);
                v->normal = normal;

                v = &vertices[num_vertices++];
                *v = Wow_Vertex(base_left.x, base_left.y, base_left.z, 0.0f, 0.0f, color);
                v->normal = normal;

                v = &vertices[num_vertices++];
                *v = Wow_Vertex(top_right.x, top_right.y, top_right.z, 1.0f, 1.0f, color);
                v->normal = normal;

                v = &vertices[num_vertices++];
                *v = Wow_Vertex(top_left.x, top_left.y, top_left.z, 0.0f, 1.0f, color);
                v->normal = normal;

                // Second quad (perpendicular, slightly shorter)
                right = (VECTOR3){ cosf(angle + 1.5707963f) * blade_width * 0.85f, 
                                   sinf(angle + 1.5707963f) * blade_width * 0.85f, 0.0f };
                normal = (VECTOR3){ -right.y, right.x, 0.10f };
                base_left = (VECTOR3){ origin.x - right.x, origin.y - right.y, origin.z };
                base_right = (VECTOR3){ origin.x + right.x, origin.y + right.y, origin.z };
                top_left = (VECTOR3){ base_left.x, base_left.y, origin.z + blade_height * 0.90f };
                top_right = (VECTOR3){ base_right.x, base_right.y, origin.z + blade_height * 0.90f };

                if (Vector3_lengthsq(&normal) > 0.000001f) {
                    Vector3_normalize(&normal);
                } else {
                    normal = (VECTOR3){ 0.0f, 0.0f, 1.0f };
                }

                v = &vertices[num_vertices++];
                *v = Wow_Vertex(base_left.x, base_left.y, base_left.z, 0.0f, 0.0f, color);
                v->normal = normal;

                v = &vertices[num_vertices++];
                *v = Wow_Vertex(base_right.x, base_right.y, base_right.z, 1.0f, 0.0f, color);
                v->normal = normal;

                v = &vertices[num_vertices++];
                *v = Wow_Vertex(top_right.x, top_right.y, top_right.z, 1.0f, 1.0f, color);
                v->normal = normal;

                v = &vertices[num_vertices++];
                *v = Wow_Vertex(base_left.x, base_left.y, base_left.z, 0.0f, 0.0f, color);
                v->normal = normal;

                v = &vertices[num_vertices++];
                *v = Wow_Vertex(top_right.x, top_right.y, top_right.z, 1.0f, 1.0f, color);
                v->normal = normal;

                v = &vertices[num_vertices++];
                *v = Wow_Vertex(top_left.x, top_left.y, top_left.z, 0.0f, 1.0f, color);
                v->normal = normal;

                Wow_AddBoundsPoint(&chunk->grass_bounds, &origin);
                origin.z += blade_height;
                Wow_AddBoundsPoint(&chunk->grass_bounds, &origin);
            }
        }
    }

    if (num_vertices) {
        chunk->grass_buffer = R_MakeVertexArrayObject(vertices, num_vertices);
        chunk->num_grass_vertices = num_vertices;
        wow_world.num_grass_chunks++;
        wow_world.num_grass_vertices += num_vertices;
    }
    ri.MemFree(vertices);
}

BOOL Wow_GrassChunkInRange(wowAdtChunk_t const *chunk) {
    VECTOR3 camera_origin;
    VECTOR3 center;
    float dx = 0.0f;
    float dy = 0.0f;
    float draw_distance = WOW_GRASS_DRAW_DISTANCE;

    if (!chunk || !chunk->grass_buffer || !chunk->num_grass_vertices || draw_distance <= 0.0f) {
        return false;
    }

    camera_origin = tr.viewDef.camerastate[0].origin;
    if (camera_origin.x < chunk->grass_bounds.min.x) {
        dx = chunk->grass_bounds.min.x - camera_origin.x;
    } else if (camera_origin.x > chunk->grass_bounds.max.x) {
        dx = camera_origin.x - chunk->grass_bounds.max.x;
    }
    if (camera_origin.y < chunk->grass_bounds.min.y) {
        dy = chunk->grass_bounds.min.y - camera_origin.y;
    } else if (camera_origin.y > chunk->grass_bounds.max.y) {
        dy = camera_origin.y - chunk->grass_bounds.max.y;
    }
    if (dx * dx + dy * dy > draw_distance * draw_distance) {
        return false;
    }

    center = (VECTOR3){
        (chunk->grass_bounds.min.x + chunk->grass_bounds.max.x) * 0.5f,
        (chunk->grass_bounds.min.y + chunk->grass_bounds.max.y) * 0.5f,
        (chunk->grass_bounds.min.z + chunk->grass_bounds.max.z) * 0.5f,
    };
    return Frustum_ContainsSphere(&tr.viewDef.frustum, &(SPHERE3){
        .center = center,
        .radius = WOW_ADT_CHUNK_SIZE,
    });
}

void Wow_DrawGrass(void) {
    wowAdtChunk_t *chunk;
    VECTOR3 camera_origin;
    DWORD drawn_chunks = 0;
    DWORD drawn_vertices = 0;
    float draw_distance;

    if (!wow_world.num_grass_vertices) {
        return;
    }

    Wow_InitGrassShader();
    if (!wow_grass_shader) {
        return;
    }

    camera_origin = tr.viewDef.camerastate[0].origin;
    draw_distance = WOW_GRASS_DRAW_DISTANCE;

    R_Call(glUseProgram, wow_grass_shader->progid);
    R_Call(glUniformMatrix4fv, wow_grass_shader->uViewProjectionMatrix, 1, GL_FALSE, tr.viewDef.viewProjectionMatrix.v);
    R_Call(glUniformMatrix4fv, wow_grass_shader->uLightMatrix, 1, GL_FALSE, tr.viewDef.lightMatrix.v);
    R_Call(glUniform3f, wow_grass_shader->uLightDir,
           tr.viewDef.lightDir.x, tr.viewDef.lightDir.y, tr.viewDef.lightDir.z);
    R_Call(glUniform1f, wow_uGrassTime, (GLfloat)tr.viewDef.time * 0.001f);
    R_Call(glUniform3f, wow_uGrassCameraOrigin, camera_origin.x, camera_origin.y, camera_origin.z);
    R_Call(glUniform1f, wow_uGrassDrawDistance, draw_distance);
    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glDepthFunc, GL_LEQUAL);
    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_Call(glDepthMask, GL_FALSE);

    for (chunk = wow_world.chunks; chunk; chunk = chunk->next) {
        if (!Wow_GrassChunkInRange(chunk)) {
            continue;
        }
        R_DrawBuffer(chunk->grass_buffer, chunk->num_grass_vertices);
        drawn_chunks++;
        drawn_vertices += chunk->num_grass_vertices;
    }

    R_Call(glDepthMask, GL_TRUE);
    R_Call(glDisable, GL_BLEND);

    {
        static int logged_x = -1;
        static int logged_y = -1;
        if (logged_x != wow_world.adt_center_x || logged_y != wow_world.adt_center_y) {
            logged_x = wow_world.adt_center_x;
            logged_y = wow_world.adt_center_y;
            fprintf(stderr,
                    "R_DrawWorld: grass chunks=%u/%u vertices=%u/%u draw_distance=%.0f\n",
                    (unsigned)drawn_chunks,
                    (unsigned)wow_world.num_grass_chunks,
                    (unsigned)drawn_vertices,
                    (unsigned)wow_world.num_grass_vertices,
                    (double)draw_distance);
        }
    }
}
