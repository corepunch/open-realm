#include "r_wowmap.h"
#include "renderer/r_game.h"
#include "common/stb_dbc.h"

#define WOW_GRASS_MAX_MODELS 64

typedef struct {
    LPCMODEL model;
    MATRIX4 *matrices;  /* slice of wow_grass_scratch, not owned */
    DWORD count;
    INSTANCEBUFFER instances;
} wowGrassGroup_t;

static MATRIX4 *wow_grass_scratch;
static DWORD wow_grass_scratch_cap;
static wowGrassGroup_t wow_grass_groups[WOW_GRASS_MAX_MODELS];
static DWORD wow_grass_group_count, wow_grass_draw_count;
static BOOL wow_grass_cache_failed;

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
    SAFE_DELETE(data, ri.FS_FreeFile);
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
    FOR_LOOP(i, wow_grass_group_count) R_ReleaseInstanceBuffer(&wow_grass_groups[i].instances);
    ri.MemFree(wow_grass_scratch);
    wow_grass_scratch = NULL;
    wow_grass_scratch_cap = 0;
    wow_grass_group_count = wow_grass_draw_count = 0;
    wow_grass_cache_failed = false;
    memset(wow_grass_groups, 0, sizeof(wow_grass_groups));
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
        SAFE_DELETE(data, ri.FS_FreeFile);
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
                            DWORD num_textures,
                            uint64_t no_effect_mask) {
    if (!chunk || !alpha || !layers || layer_count == 0) return;

#if WOW_GRASS_CAMERA_MESH
    /* Camera-following mesh path: encode per-cell suppression/density into
       the GPU grass-control texture (R=suppressed, G=density 0..255). */
    {
        BYTE pixels[WOW_GRASS_CTRL_CELLS * WOW_GRASS_CTRL_CELLS * 4];
        int cell = 0;
        for (int row = 0; row < WOW_GRASS_CTRL_CELLS; row++) {
            for (int col = 0; col < WOW_GRASS_CTRL_CELLS; col++, cell++) {
                BOOL suppressed = (no_effect_mask >> (row * 8 + col)) & 1U;
                int ax = MIN(col * 8, 63), ay = MIN(row * 8, 63);
                DWORD aidx = (DWORD)(ay * 64 + ax);
                BOOL is_road = (!suppressed) &&
                    Wow_GrassRoadAt(alpha, layers, layer_count, textures, num_textures, aidx);
                BYTE density = 0;
                if (!suppressed && !is_road) {
                    FOR_LOOP(li, MIN(layer_count, 4)) {
                        DWORD eid = layers[li].effect_id;
                        if (!eid || eid == WOW_GRASS_INVALID_DOODAD) continue;
                        BYTE cov = Wow_GrassLayerCoverage(alpha, layers, layer_count, li, aidx);
                        wowGroundEffectTexture_t *fx = Wow_GetGroundEffectTexture(eid);
                        if (fx && cov >= WOW_GRASS_COVERAGE_MIN) {
                            BYTE d = (BYTE)((float)cov / 255.0f *
                                (float)MIN(fx->density, WOW_GRASS_DBC_DENSITY_MAX) /
                                (float)WOW_GRASS_DBC_DENSITY_MAX * 255.0f);
                            if (d > density) density = d;
                        }
                    }
                }
                pixels[cell * 4 + 0] = (suppressed || is_road) ? 255 : 0;  /* R: suppress */
                pixels[cell * 4 + 1] = density;                             /* G: density  */
                pixels[cell * 4 + 2] = is_road ? 255 : 0;                  /* B: road     */
                pixels[cell * 4 + 3] = 0;
            }
        }
        Wow_EnsureGrassCtrlTexture();
        R_Call(glBindTexture, GL_TEXTURE_2D, wow_world.grass_ctrl->texid);
        R_Call(glTexSubImage2D, GL_TEXTURE_2D, 0,
               (GLint)(chunk->alpha_index_x * WOW_GRASS_CTRL_CELLS),
               (GLint)(chunk->alpha_index_y * WOW_GRASS_CTRL_CELLS),
               WOW_GRASS_CTRL_CELLS, WOW_GRASS_CTRL_CELLS,
               GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
#else
    /* Old instanced-draw path: spawn one renderEntity per clump. */
    for (int row = 0; row < WOW_GRASS_CELLS_PER_AXIS; row += WOW_GRASS_CELL_STEP) {
        for (int col = 0; col < WOW_GRASS_CELLS_PER_AXIS; col += WOW_GRASS_CELL_STEP) {
            DWORD seed = (chunk->alpha_index_x * 73856093U) ^ (chunk->alpha_index_y * 19349663U) ^
                         ((DWORD)row * 83492791U) ^ ((DWORD)col * 2654435761U);
            /* suppress via no_effect_mask first */
            if ((no_effect_mask >> (row * 8 + col)) & 1U) continue;

            float local_row = row + WOW_GRASS_CELL_OFFSET + Wow_GrassRandom(&seed) * (WOW_GRASS_CELL_STEP - WOW_GRASS_CELL_MARGIN);
            float local_col = col + WOW_GRASS_CELL_OFFSET + Wow_GrassRandom(&seed) * (WOW_GRASS_CELL_STEP - WOW_GRASS_CELL_MARGIN);
            int cell_row = (int)floorf(MIN(local_row, WOW_GRASS_CELLS_PER_AXIS - WOW_GRASS_COORD_EPSILON));
            int cell_col = (int)floorf(MIN(local_col, WOW_GRASS_CELLS_PER_AXIS - WOW_GRASS_COORD_EPSILON));
            int alpha_x = MAX(0, MIN((int)(local_col * WOW_GRASS_ALPHA_AXIS), WOW_GRASS_ALPHA_MAX));
            int alpha_y = MAX(0, MIN((int)(local_row * WOW_GRASS_ALPHA_AXIS), WOW_GRASS_ALPHA_MAX));
            DWORD effect_id = Wow_GrassEffectIdForCoverage(alpha, layers, layer_count, alpha_y * 64 + alpha_x);
            BYTE coverage = Wow_GrassEffectCoverage(alpha, layers, layer_count, alpha_y * 64 + alpha_x);
            wowGroundEffectTexture_t *ground_effect;
            int clumps;

            if (Wow_GrassRoadAt(alpha, layers, layer_count, textures, num_textures, alpha_y * 64 + alpha_x) ||
                coverage < WOW_GRASS_COVERAGE_MIN || !effect_id) continue;
            ground_effect = Wow_GetGroundEffectTexture(effect_id);
            if (!ground_effect || !ground_effect->density || !wow_ground_effect_doodads_loaded) continue;

            clumps = MIN(MAX(1, (int)ceilf((float)coverage / WOW_GRASS_ALPHA_TEXEL_MAX *
                MIN(ground_effect->density, WOW_GRASS_DBC_DENSITY_MAX))), WOW_GRASS_MAX_PLACEMENTS_PER_SAMPLE);
            FOR_LOOP(clump, clumps) {
                float row_j = Wow_GrassClamp(local_row + (Wow_GrassRandom(&seed) - 0.5f) * WOW_GRASS_CLUMP_JITTER, 0.001f, 7.999f);
                float col_j = Wow_GrassClamp(local_col + (Wow_GrassRandom(&seed) - 0.5f) * WOW_GRASS_CLUMP_JITTER, 0.001f, 7.999f);
                float height;
                cell_row = (int)floorf(row_j);
                cell_col = (int)floorf(col_j);
                if (!Wow_HeightInCell(chunk->heights, cell_row, cell_col, row_j - cell_row, col_j - cell_col, &height)) continue;
                {
                    DWORD weights[WOW_GRASS_DOODAD_SLOTS];
                    static BYTE missing_logged[WOW_GRASS_DOODAD_LOGGED_IDS];
                    VECTOR3 origin = { chunk->position.x - row_j * WOW_ADT_UNIT_SIZE,
                                       chunk->position.y - col_j * WOW_ADT_UNIT_SIZE,
                                       chunk->position.z + height + WOW_GRASS_Z_BIAS };
                    Wow_GroundEffectWeights(ground_effect, weights);
                    if (!weights[0] && !weights[1] && !weights[2] && !weights[3]) continue;
                    DWORD di = Wow_SelectDoodadFromWeights(weights, &seed);
                    wowGroundEffectDoodad_t *doodad = Wow_GetGroundEffectDoodad(ground_effect->doodad_id[di]);
                    if (!doodad) {
                        DWORD did = ground_effect->doodad_id[di];
                        if (did < WOW_GRASS_DOODAD_LOGGED_IDS && !missing_logged[did]) {
                            fprintf(stderr, "[GRASS] missing GroundEffectDoodad id=%u for effect=%u\n", (unsigned)did, (unsigned)effect_id);
                            missing_logged[did] = true;
                        }
                        continue;
                    }
                    Wow_AddGroundEffectInstance(doodad->model_path, origin, Wow_GrassRandom(&seed) * WOW_GRASS_FULL_CIRCLE);
                }
            }
        }
    }
#endif
}

/* Build one immutable cross; gl_InstanceID expands it over the camera-following world-cell grid. */
void Wow_EnsureCameraGrassMesh(void) {
    float H, HW;
    VERTEX *verts;
    DWORD total;

    if (wow_world.grass_tile_vbo) return;

    total = WOW_GRASS_VERTS_PER_BLADE;
    verts = ri.MemAlloc(sizeof(VERTEX) * total);
    if (!verts) return;

    H  = WOW_GRASS_BLADE_HEIGHT_MIN + WOW_GRASS_BLADE_HEIGHT_VARIATION * 0.5f;
    HW = (WOW_GRASS_BLADE_WIDTH_MIN  + WOW_GRASS_BLADE_WIDTH_VARIATION  * 0.5f) * 0.5f;
    {
        int q;

        /* Blade A: spans X axis in XY plane (6 verts) */
        static const float A[6][5] = {
            {-1, 0, 0, 0, 0}, { 1, 0, 0, 1, 0}, { 1, 1, 0, 1, 1},
            {-1, 0, 0, 0, 0}, { 1, 1, 0, 1, 1}, {-1, 1, 0, 0, 1},
        };
        /* Blade B: spans Z axis in ZY plane (6 verts) */
        static const float B[6][5] = {
            { 0, 0,-1, 0, 0}, { 0, 0, 1, 1, 0}, { 0, 1, 1, 1, 1},
            { 0, 0,-1, 0, 0}, { 0, 1, 1, 1, 1}, { 0, 1,-1, 0, 1},
        };

        for (q = 0; q < 12; q++) {
            const float *bd = (q < 6) ? A[q] : B[q - 6];
            VERTEX *v = &verts[q];
            memset(v, 0, sizeof(*v));
            v->position = (VECTOR3){ bd[0] * HW, bd[1] * H, bd[2] * HW };
            v->texcoord = (VECTOR2){ bd[3], bd[4] };
            v->normal   = (VECTOR3){ 0, 0, 1 };
            v->color    = COLOR32_WHITE;
        }
    }

    wow_world.grass_tile_vbo    = R_MakeVertexArrayObject(verts, total);
    wow_world.grass_tile_nverts = total;
    ri.MemFree(verts);
    fprintf(stderr, "[GRASS] Camera grass mesh built: %u GPU slots, %u shared verts\n",
            (unsigned)WOW_GRASS_BLADE_SLOTS, (unsigned)total);
}

void Wow_FreeCameraGrassMesh(void) {
    if (!wow_world.grass_tile_vbo) return;
    R_ReleaseVertexArrayObject(wow_world.grass_tile_vbo);
    wow_world.grass_tile_vbo    = NULL;
    wow_world.grass_tile_nverts = 0;
}

void Wow_DrawGrass(void) {
#if WOW_GRASS_CAMERA_MESH
    VECTOR3 cam;

    /* Nothing to draw until at least one chunk has loaded. */
    if (!wow_world.has_atlas_origin || !wow_world.grass_ctrl || !wow_world.height_atlas) return;

    Wow_InitGrassShader();
    if (!wow_grass_shader) return;

    Wow_EnsureCameraGrassMesh();
    if (!wow_world.grass_tile_vbo) return;

    cam = tr.viewDef.camerastate[0].origin;
    R_Call(glUseProgram, wow_grass_shader->progid);
    R_Call(glUniformMatrix4fv, wow_grass_shader->uViewProjectionMatrix, 1, GL_FALSE, tr.viewDef.viewProjectionMatrix.v);
    {
        VECTOR3 sun_dir;
        Wow_SunDirection(Wow_DayFraction(), &sun_dir);
        R_Call(glUniform3f, wow_uGrassSunDir, sun_dir.x, sun_dir.y, sun_dir.z);
        R_Call(glUniform3f, wow_uGrassSunAmbient, WOW_LIGHT_AMBIENT_R, WOW_LIGHT_AMBIENT_G, WOW_LIGHT_AMBIENT_B);
        R_Call(glUniform3f, wow_uGrassSunDiffuse, WOW_LIGHT_DIFFUSE_R, WOW_LIGHT_DIFFUSE_G, WOW_LIGHT_DIFFUSE_B);
    }
    R_Call(glUniform1f,  wow_uGrassTime,             (GLfloat)(tr.viewDef.time / 1000.0f));
    R_Call(glUniform3f,  wow_uGrassCameraOrigin,     (GLfloat)cam.x, (GLfloat)cam.y, (GLfloat)cam.z);
    R_Call(glUniform1f,  wow_uGrassDrawDistance,     (GLfloat)WOW_GRASS_DRAW_DISTANCE);
    R_Call(glUniform1f,  wow_uGrassFadeStartDistance,(GLfloat)WOW_GRASS_FADE_START_DISTANCE);
    R_Call(glUniform2f,  wow_uCameraXZ,              (GLfloat)cam.x, (GLfloat)cam.y);
    R_Call(glUniform1f,  wow_uGrassSlotSpacing,      (GLfloat)WOW_GRASS_SLOT_SPACING);
    R_Call(glUniform2f,  wow_uAtlasOriginWorld,      (GLfloat)wow_world.atlas_world_x, (GLfloat)wow_world.atlas_world_y);
    R_Call(glUniform1f,  wow_uAtlasChunkSize,        (GLfloat)WOW_ADT_CHUNK_SIZE);
    R_Call(glUniform1f,  wow_uAtlasUnitSize,         (GLfloat)WOW_ADT_UNIT_SIZE);
    R_Call(glUniform2f,  wow_uCtrlOriginWorld,       (GLfloat)wow_world.atlas_world_x, (GLfloat)wow_world.atlas_world_y);
    R_Call(glUniform1f,  wow_uCtrlCellSize,          (GLfloat)WOW_ADT_UNIT_SIZE);

    /* Height atlas on texture unit 5 */
    R_Call(glActiveTexture, GL_TEXTURE5);
    R_Call(glBindTexture, GL_TEXTURE_2D, wow_world.height_atlas->texid);
    /* Grass control texture on unit 6 */
    R_Call(glActiveTexture, GL_TEXTURE6);
    R_Call(glBindTexture, GL_TEXTURE_2D, wow_world.grass_ctrl->texid);
    R_Call(glActiveTexture, GL_TEXTURE0);
    R_Call(glUniform1i, wow_uHeightAtlas, 5);
    R_Call(glUniform1i, wow_uGrassCtrl,   6);

    R_Call(glEnable,    GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_TRUE);
    R_Call(glDepthFunc, GL_LEQUAL);
    R_Call(glDisable,   GL_CULL_FACE);
    R_SetAlphaKeyState(true);

    R_DrawBufferCopies(wow_world.grass_tile_vbo, wow_world.grass_tile_nverts, WOW_GRASS_BLADE_SLOTS);

    R_SetAlphaKeyState(false);

    {
        static int logged_x = -1, logged_y = -1;
        if (logged_x != wow_world.adt_center_x || logged_y != wow_world.adt_center_y) {
            logged_x = wow_world.adt_center_x; logged_y = wow_world.adt_center_y;
            fprintf(stderr, "[GRASS] camera-mesh draw: blades=%u grid=%ux%u spacing=%.1fm atlas_origin=(%.1f,%.1f)\n",
                    (unsigned)WOW_GRASS_BLADE_SLOTS, WOW_GRASS_GRID_SIDE, WOW_GRASS_GRID_SIDE,
                    (double)WOW_GRASS_SLOT_SPACING,
                    (double)wow_world.atlas_world_x, (double)wow_world.atlas_world_y);
        }
    }
#else
    /* Authoritative M2 batches retain MPQ geometry/materials; build their transforms once per ADT window. */
    wowDoodadInstance_t *inst;

    if ((!wow_world.ground_effects && !wow_grass_group_count) || wow_grass_cache_failed) return;
    if (!wow_grass_group_count) {
        /* Filter instances to within WOW_GRASS_CULL_RADIUS of the current camera position.
         * Any instance farther than draw_dist + half_ADT can never be visible before an ADT reload. */
        float cam_x = tr.viewDef.camerastate[0].origin.x;
        float cam_y = tr.viewDef.camerastate[0].origin.y;
        float cull_sq = WOW_GRASS_CULL_RADIUS * WOW_GRASS_CULL_RADIUS;
        DWORD culled = 0;
        memset(wow_grass_groups, 0, sizeof(wow_grass_groups));
        for (inst = wow_world.ground_effects; inst; inst = inst->next) {
            float dx = inst->entity.origin.x - cam_x;
            float dy = inst->entity.origin.y - cam_y;
            if (dx*dx + dy*dy > cull_sq) { culled++; continue; }
            wowGrassGroup_t *group = NULL;
            FOR_LOOP(i, wow_grass_group_count)
                if (wow_grass_groups[i].model == inst->entity.model) { group = &wow_grass_groups[i]; break; }
            if (!group) {
                if (wow_grass_group_count >= WOW_GRASS_MAX_MODELS) continue;
                group = &wow_grass_groups[wow_grass_group_count++]; group->model = inst->entity.model;
            }
            group->count++; wow_grass_draw_count++;
        }
        if (culled) fprintf(stderr, "[GRASS] culled %u/%u instances beyond %.0f yards\n",
                            (unsigned)culled, (unsigned)(wow_grass_draw_count + culled), (double)WOW_GRASS_CULL_RADIUS);
        if (wow_grass_draw_count) {
            wow_grass_scratch_cap = wow_grass_draw_count;
            wow_grass_scratch = ri.MemAlloc(wow_grass_scratch_cap * sizeof(MATRIX4));
        }
        if (wow_grass_draw_count && !wow_grass_scratch) {
            fprintf(stderr, "[GRASS] failed to allocate %u cached instance matrices\n", (unsigned)wow_grass_draw_count);
            wow_grass_cache_failed = true;
            return;
        }
        if (wow_grass_scratch) {
            DWORD offset = 0;
            FOR_LOOP(i, wow_grass_group_count) {
                wow_grass_groups[i].matrices = wow_grass_scratch + offset;
                offset += wow_grass_groups[i].count; wow_grass_groups[i].count = 0;
            }
            for (inst = wow_world.ground_effects; inst; inst = inst->next) {
                float dx = inst->entity.origin.x - cam_x;
                float dy = inst->entity.origin.y - cam_y;
                if (dx*dx + dy*dy > cull_sq) continue;
                MATRIX4 matrix;
                wowGrassGroup_t *group = NULL;
                FOR_LOOP(i, wow_grass_group_count)
                    if (wow_grass_groups[i].model == inst->entity.model) { group = &wow_grass_groups[i]; break; }
                if (!group) continue;
                R_GetEntityMatrix(&inst->entity, &matrix);
                group->matrices[group->count++] = matrix;
            }
            FOR_LOOP(i, wow_grass_group_count) {
                wowGrassGroup_t *group = &wow_grass_groups[i];
                if (R_MakeInstanceBuffer(&group->instances, group->matrices, group->count)) continue;
                fprintf(stderr, "[GRASS] failed to upload %u persistent instance matrices for model group %u\n",
                        (unsigned)group->count, (unsigned)i);
                wow_grass_cache_failed = true;
                return;
            }
            /* The immutable GPU streams replace the 29.8 MiB per-frame matrix upload and CPU backing store. */
            ri.MemFree(wow_grass_scratch); wow_grass_scratch = NULL; wow_grass_scratch_cap = 0;
            FOR_LOOP(i, wow_grass_group_count) wow_grass_groups[i].matrices = NULL;
        }
        inst = wow_world.ground_effects;
        while (inst) {
            wowDoodadInstance_t *next = inst->next;
            ri.MemFree(inst); inst = next;
        }
        wow_world.ground_effects = NULL;
        fprintf(stderr, "[GRASS] cached authoritative M2 geometry/materials: instances=%u groups=%u\n",
                (unsigned)wow_grass_draw_count, (unsigned)wow_grass_group_count);
    }
    FOR_LOOP(i, wow_grass_group_count)
        R_GameRenderModelInstanced(wow_grass_groups[i].model, &wow_grass_groups[i].instances, RF_GROUND_EFFECT);
#endif
}
