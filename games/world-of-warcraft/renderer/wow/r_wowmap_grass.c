#include "r_wowmap.h"
#include "renderer/r_game.h"
#include "common/stb_dbc.h"

#define WOW_GRASS_MAX_MODELS 64

typedef struct {
    LPCMODEL model;
    MATRIX4 *matrices;  /* slice of wow_grass_scratch, not owned */
    DWORD count;
} wowGrassGroup_t;

static MATRIX4 *wow_grass_scratch;
static DWORD wow_grass_scratch_cap;

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

static BOOL Wow_GrassRoadTexture(LPCSTR path) {
    return path && (strcasestr(path, "road") || strcasestr(path, "cobble") || strcasestr(path, "path") ||
                    strcasestr(path, "street") || strcasestr(path, "pavement") || strcasestr(path, "brick"));
}

static float Wow_GrassRandom(LPDWORD seed) {
    *seed = Wow_GrassHash(*seed + 0x9e3779b9U);
    return (float)(*seed & 0xffff) / 65535.0f;
}

static wowGroundEffectDoodad_t *Wow_GetGroundEffectDoodad(DWORD doodad_id) {
    FOR_LOOP(i, wow_ground_effect_doodad_count) {
        if (wow_ground_effect_doodads[i].id == doodad_id) {
            return &wow_ground_effect_doodads[i];
        }
    }
    return NULL;
}

static void Wow_GroundEffectWeights(wowGroundEffectTexture_t const *effect, DWORD weights[WOW_GRASS_DOODAD_SLOTS]) {
    memcpy(weights, effect->weight, sizeof(effect->weight));
}

static BOOL Wow_GroundEffectModelPath(DWORD const *record, BYTE const *strings, DWORD string_size, LPSTR out, DWORD out_size) {
    DWORD fields[2] = { WOW_GRASS_DOODAD_MODEL_FIELD, 1 };
    FOR_LOOP(i, 2) {
        LPCSTR name = Wow_StringAt((LPCSTR)strings, string_size, record[fields[i]]);
        LPBYTE data = NULL;
        int size;
        if (!name || !*name) continue;
        snprintf(out, out_size, "World\\NoDXT\\Detail\\%s", name);
        if (Wow_PathHasExtension(out, ".mdl")) {
            char *ext = strrchr(out, '.');
            snprintf(ext, 5, ".m2");
        }
        size = ri.FS_ReadFile(out, (void **)&data);
        if (size >= 0 && data) {
            ri.FS_FreeFile(data);
            return true;
        }
    }
    out[0] = '\0';
    return false;
}

static void Wow_LoadGroundEffectDoodads(void) {
    stbDbc_t h;
    LPBYTE data = NULL;
    int size = ri.FS_ReadFile("DBFilesClient\\GroundEffectDoodad.dbc", (void **)&data);
    if (Stb_DbcValid(data, (DWORD)size, &h) && h.record_size >= WOW_GRASS_DOODAD_FIELD_COUNT * sizeof(DWORD)) {
        BYTE const *records = Stb_DbcRecords(data);
        BYTE const *strings = Stb_DbcStrings(data, &h);
        DWORD count = MIN(h.records, WOW_MAX_GROUND_EFFECT_DOODADS);
        FOR_LOOP(i, count) {
            DWORD const *record = (DWORD const *)(records + i * h.record_size);
            wowGroundEffectDoodad_t *doodad = &wow_ground_effect_doodads[wow_ground_effect_doodad_count];
            if (!Wow_GroundEffectModelPath(record, strings, h.string_size, doodad->model_path, sizeof(doodad->model_path))) continue;
            doodad->id = record[0];
            wow_ground_effect_doodad_count++;
        }
        wow_ground_effect_doodads_loaded = true;
        fprintf(stderr, "[GRASS] Loaded %u GroundEffectDoodad records\n", (unsigned)wow_ground_effect_doodad_count);
    }
    if (data) ri.FS_FreeFile(data);
}

static DWORD Wow_GroundEffectLayout(BYTE const *records, DWORD count, DWORD record_size) {
    DWORD score[2] = { 0, 0 }, rows[2] = { 0, 0 };
    FOR_LOOP(i, count) {
        DWORD const *record = (DWORD const *)(records + i * record_size);
        DWORD valid[2] = { 0, 0 };
        FOR_LOOP(slot, WOW_GRASS_DOODAD_SLOTS) {
            if (Wow_GetGroundEffectDoodad(record[WOW_GRASS_TEXTURE_LEGACY_DOODAD_FIELD + slot])) valid[0]++;
            if (Wow_GetGroundEffectDoodad(record[WOW_GRASS_TEXTURE_MODERN_DOODAD_FIELD + slot])) valid[1]++;
        }
        FOR_LOOP(layout, 2) if (valid[layout] >= 2) rows[layout]++;
        score[0] += valid[0]; score[1] += valid[1];
    }
    fprintf(stderr, "[GRASS] GroundEffectTexture layout scores: legacy=%u/%u modern=%u/%u\n",
            (unsigned)rows[0], (unsigned)score[0], (unsigned)rows[1], (unsigned)score[1]);
    return rows[1] > rows[0] ? WOW_GRASS_TEXTURE_MODERN_DOODAD_FIELD : WOW_GRASS_TEXTURE_LEGACY_DOODAD_FIELD;
}

void Wow_FreeGrassScratch(void) {
    ri.MemFree(wow_grass_scratch);
    wow_grass_scratch = NULL;
    wow_grass_scratch_cap = 0;
}

void Wow_LoadGroundEffectDBCs(void) {
    stbDbc_t h = { 0 };
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

    Wow_LoadGroundEffectDoodads();

    // Load GroundEffectTexture.dbc
    fprintf(stderr, "[GRASS] Loading GroundEffectTexture.dbc...\n");
    fflush(stderr);
    size = ri.FS_ReadFile("DBFilesClient\\GroundEffectTexture.dbc", (void **)&data);
    fprintf(stderr, "[GRASS] FS_ReadFile returned size=%u\n", (unsigned)size);
    fflush(stderr);

    if (Stb_DbcValid(data, size, &h) && h.records > 0 && h.record_size == WOW_GRASS_DBC_FIELD_COUNT * sizeof(DWORD)) {
        records = h.records;
        record_size = h.record_size;
        records_base = Stb_DbcRecords(data);
        records_to_copy = records;
        wow_ground_effect_textures = ri.MemAlloc(sizeof(*wow_ground_effect_textures) * records_to_copy);
        if (!wow_ground_effect_textures) {
            fprintf(stderr, "[GRASS] allocation failed for %u GroundEffectTexture rows\n", (unsigned)records_to_copy);
            ri.FS_FreeFile(data);
            return;
        }

        fprintf(stderr, "[GRASS] Copying %u records...\n", (unsigned)records_to_copy);
        DWORD doodad_field = Wow_GroundEffectLayout(records_base, records_to_copy, record_size);
        FOR_LOOP(i, records_to_copy) {
            DWORD const *record = (DWORD const *)(records_base + i * record_size);
            wowGroundEffectTexture_t *effect = &wow_ground_effect_textures[i];
            memset(effect, 0, sizeof(*effect));
            effect->id = record[0];
            effect->density = record[WOW_GRASS_TEXTURE_DENSITY_FIELD];
            FOR_LOOP(slot, WOW_GRASS_DOODAD_SLOTS) {
                effect->doodad_id[slot] = record[doodad_field + slot];
                effect->weight[slot] = doodad_field == WOW_GRASS_TEXTURE_MODERN_DOODAD_FIELD ?
                    record[WOW_GRASS_TEXTURE_WEIGHT_FIELD + slot] :
                    (effect->doodad_id[slot] == WOW_GRASS_INVALID_DOODAD ? 0 : 1);
            }
        }

        wow_ground_effect_texture_count = records_to_copy;
        wow_ground_effect_textures_loaded = true;
        fprintf(stderr, "[GRASS] Successfully loaded %u GroundEffectTexture records\n", (unsigned)records);
        ri.FS_FreeFile(data);
    } else {
        if (data) ri.FS_FreeFile(data);
        fprintf(stderr, "[GRASS] WDBC validation failed: records=%u record_size=%u size=%u\n", (unsigned)h.records, (unsigned)h.record_size, (unsigned)size);
    }

    fprintf(stderr, "[GRASS] Wow_LoadGroundEffectDBCs: Complete\n");
    fflush(stderr);
}

static wowGroundEffectTexture_t *Wow_GetGroundEffectTexture(DWORD effect_id) {
    if (!wow_ground_effect_textures_attempted) {
        Wow_LoadGroundEffectDBCs();
    }

    if (!wow_ground_effect_textures_loaded || effect_id == 0 || effect_id == WOW_GRASS_INVALID_DOODAD) {
        return NULL;
    }

    FOR_LOOP(i, wow_ground_effect_texture_count) {
        if (wow_ground_effect_textures[i].id == effect_id) {
            return &wow_ground_effect_textures[i];
        }
    }

    return NULL;
}

static DWORD Wow_SelectDoodadFromWeights(DWORD const weights[WOW_GRASS_DOODAD_SLOTS], LPDWORD seed) {
    DWORD total_weight = 0;
    DWORD roll;
    FOR_LOOP(i, WOW_GRASS_DOODAD_SLOTS) total_weight += weights[i];
    if (total_weight == 0) {
        return 0; // No valid doodads
    }
    roll = (DWORD)(Wow_GrassRandom(seed) * total_weight);
    FOR_LOOP(i, WOW_GRASS_DOODAD_SLOTS) {
        if (roll < weights[i]) return i;
        roll -= weights[i];
    }
    return WOW_GRASS_DOODAD_SLOTS - 1;
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
        if (!layers[layer_index].effect_id || layers[layer_index].effect_id == WOW_GRASS_INVALID_DOODAD) {
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
        if (!layers[layer_index].effect_id || layers[layer_index].effect_id == WOW_GRASS_INVALID_DOODAD) {
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

static BOOL Wow_GrassRoadAt(BYTE const alpha[4][WOW_ALPHA_TEXELS], wowLayer_t const *layers, DWORD layer_count,
                            char **textures, DWORD num_textures, DWORD alpha_index) {
    FOR_LOOP(layer_index, MIN(layer_count, 4)) {
        if (layers[layer_index].texture_id < num_textures && Wow_GrassRoadTexture(textures[layers[layer_index].texture_id]) &&
            Wow_GrassLayerCoverage(alpha, layers, layer_count, layer_index, alpha_index) >= WOW_GRASS_ROAD_COVERAGE_MIN)
            return true;
    }
    return false;
}

void Wow_BuildGrassForChunk(wowAdtChunk_t *chunk,
                            BYTE const alpha[4][WOW_ALPHA_TEXELS],
                            wowLayer_t const *layers,
                            DWORD layer_count,
                            char **textures,
                            DWORD num_textures) {
    if (!chunk || !alpha || !layers || layer_count == 0) {
        return;
    }

    for (int row = 0; row < WOW_GRASS_CELLS_PER_AXIS; row += WOW_GRASS_CELL_STEP) {
        for (int col = 0; col < WOW_GRASS_CELLS_PER_AXIS; col += WOW_GRASS_CELL_STEP) {
            DWORD seed = (chunk->alpha_index_x * 73856093U) ^
                         (chunk->alpha_index_y * 19349663U) ^
                         ((DWORD)row * 83492791U) ^
                         ((DWORD)col * 2654435761U);
            float local_row = row + WOW_GRASS_CELL_OFFSET + Wow_GrassRandom(&seed) * (WOW_GRASS_CELL_STEP - WOW_GRASS_CELL_MARGIN);
            float local_col = col + WOW_GRASS_CELL_OFFSET + Wow_GrassRandom(&seed) * (WOW_GRASS_CELL_STEP - WOW_GRASS_CELL_MARGIN);
            int cell_row = (int)floorf(MIN(local_row, WOW_GRASS_CELLS_PER_AXIS - WOW_GRASS_COORD_EPSILON));
            int cell_col = (int)floorf(MIN(local_col, WOW_GRASS_CELLS_PER_AXIS - WOW_GRASS_COORD_EPSILON));
            int alpha_x = MAX(0, MIN((int)(local_col * WOW_GRASS_ALPHA_AXIS), WOW_GRASS_ALPHA_MAX));
            int alpha_y = MAX(0, MIN((int)(local_row * WOW_GRASS_ALPHA_AXIS), WOW_GRASS_ALPHA_MAX));
            DWORD effect_id = Wow_GrassEffectIdForCoverage(alpha, layers, layer_count, alpha_y * 64 + alpha_x);
            BYTE coverage = Wow_GrassEffectCoverage(alpha, layers, layer_count, alpha_y * 64 + alpha_x);
            int clumps;
            wowGroundEffectTexture_t *ground_effect;

            if (Wow_GrassRoadAt(alpha, layers, layer_count, textures, num_textures, alpha_y * 64 + alpha_x) ||
                coverage < WOW_GRASS_COVERAGE_MIN || !effect_id) {
                continue;
            }
            ground_effect = Wow_GetGroundEffectTexture(effect_id);
            if (!ground_effect || !ground_effect->density || !wow_ground_effect_doodads_loaded) {
                continue;
            }

            clumps = MAX(1, (int)ceilf((float)coverage / WOW_GRASS_ALPHA_TEXEL_MAX *
                                       MIN(ground_effect->density, WOW_GRASS_DBC_DENSITY_MAX)));
            clumps = MIN(clumps, WOW_GRASS_MAX_PLACEMENTS_PER_SAMPLE);
            FOR_LOOP(clump, clumps) {
                float row_jitter = local_row + (Wow_GrassRandom(&seed) - 0.5f) * WOW_GRASS_CLUMP_JITTER;
                float col_jitter = local_col + (Wow_GrassRandom(&seed) - 0.5f) * WOW_GRASS_CLUMP_JITTER;
                float height;
                VECTOR3 origin;

                row_jitter = Wow_GrassClamp(row_jitter, 0.001f, 7.999f);
                col_jitter = Wow_GrassClamp(col_jitter, 0.001f, 7.999f);
                cell_row = (int)floorf(row_jitter);
                cell_col = (int)floorf(col_jitter);
                if (!Wow_HeightInCell(chunk->heights, cell_row, cell_col, row_jitter - cell_row, col_jitter - cell_col, &height)) {
                    continue;
                }

                origin = (VECTOR3){
                    chunk->position.x - row_jitter * WOW_ADT_UNIT_SIZE,
                    chunk->position.y - col_jitter * WOW_ADT_UNIT_SIZE,
                    chunk->position.z + height + WOW_GRASS_Z_BIAS,
                };
                {
                    DWORD weights[WOW_GRASS_DOODAD_SLOTS];
                    Wow_GroundEffectWeights(ground_effect, weights);
                    if (!weights[0] && !weights[1] && !weights[2] && !weights[3]) {
                        continue;
                    }
                    DWORD doodad_index = Wow_SelectDoodadFromWeights(weights, &seed);
                    wowGroundEffectDoodad_t *doodad = Wow_GetGroundEffectDoodad(ground_effect->doodad_id[doodad_index]);
                    static BYTE missing_doodad_logged[WOW_GRASS_DOODAD_LOGGED_IDS];

                    if (!doodad) {
                        DWORD doodad_id = ground_effect->doodad_id[doodad_index];
                        if (doodad_id < WOW_GRASS_DOODAD_LOGGED_IDS && !missing_doodad_logged[doodad_id]) {
                            fprintf(stderr, "[GRASS] missing GroundEffectDoodad id=%u for effect=%u\n",
                                    (unsigned)doodad_id, (unsigned)effect_id);
                            missing_doodad_logged[doodad_id] = true;
                        }
                        continue;
                    }
                    Wow_AddGroundEffectInstance(doodad->model_path, origin, Wow_GrassRandom(&seed) * WOW_GRASS_FULL_CIRCLE);
                }
            }
        }
    }
}

void Wow_DrawGrass(void) {
    wowDoodadInstance_t *inst;
    wowGrassGroup_t groups[WOW_GRASS_MAX_MODELS];
    DWORD group_count = 0;
    DWORD drawn_instances = 0;
    VECTOR3 camera_origin;
    float draw_distance_sq;

    if (!wow_world.ground_effects) {
        return;
    }

    memset(groups, 0, sizeof(groups));
    camera_origin = tr.viewDef.camerastate[0].origin;
    draw_distance_sq = WOW_GRASS_DRAW_DISTANCE * WOW_GRASS_DRAW_DISTANCE;

    // Pass 1: count visible instances per model, no allocations
    for (inst = wow_world.ground_effects; inst; inst = inst->next) {
        wowGrassGroup_t *group = NULL;
        float dx = inst->entity.origin.x - camera_origin.x;
        float dy = inst->entity.origin.y - camera_origin.y;

        if (dx * dx + dy * dy > draw_distance_sq) continue;
        if (!Wow_EntityInView(&inst->entity)) continue;

        FOR_LOOP(i, group_count) {
            if (groups[i].model == inst->entity.model) {
                group = &groups[i];
                break;
            }
        }
        if (!group) {
            if (group_count >= WOW_GRASS_MAX_MODELS) continue;
            group = &groups[group_count++];
            group->model = inst->entity.model;
        }
        group->count++;
        drawn_instances++;
    }

    if (drawn_instances > 0) {
        // Grow the persistent scratch buffer if needed; never frees within a frame
        if (drawn_instances > wow_grass_scratch_cap) {
            ri.MemFree(wow_grass_scratch);
            wow_grass_scratch_cap = drawn_instances + 64;
            wow_grass_scratch = ri.MemAlloc(wow_grass_scratch_cap * sizeof(MATRIX4));
        }

        if (wow_grass_scratch) {
            // Assign contiguous scratch slices via prefix sums, reset count for fill pass
            {
                DWORD offset = 0;
                FOR_LOOP(i, group_count) {
                    groups[i].matrices = wow_grass_scratch + offset;
                    offset += groups[i].count;
                    groups[i].count = 0;
                }
            }

            // Pass 2: fill matrices into pre-assigned scratch slices
            for (inst = wow_world.ground_effects; inst; inst = inst->next) {
                wowGrassGroup_t *group = NULL;
                float dx = inst->entity.origin.x - camera_origin.x;
                float dy = inst->entity.origin.y - camera_origin.y;
                MATRIX4 matrix;

                if (dx * dx + dy * dy > draw_distance_sq) continue;
                if (!Wow_EntityInView(&inst->entity)) continue;

                FOR_LOOP(i, group_count) {
                    if (groups[i].model == inst->entity.model) {
                        group = &groups[i];
                        break;
                    }
                }
                if (!group) continue;

                R_GetEntityMatrix(&inst->entity, &matrix);
                group->matrices[group->count++] = matrix;
            }

            FOR_LOOP(i, group_count) {
                R_GameRenderModelInstanced(groups[i].model, groups[i].matrices, groups[i].count);
            }
        }
    }

    {
        static int logged_x = -1;
        static int logged_y = -1;
        if (logged_x != wow_world.adt_center_x || logged_y != wow_world.adt_center_y) {
            logged_x = wow_world.adt_center_x;
            logged_y = wow_world.adt_center_y;
            fprintf(stderr, "R_DrawWorld: grass instances=%u/%u groups=%u draw_distance=%.0f\n",
                    (unsigned)drawn_instances, (unsigned)wow_world.num_ground_effects,
                    (unsigned)group_count, (double)WOW_GRASS_DRAW_DISTANCE);
        }
    }
}
