#include "r_war3map.h"
#include "games/warcraft-3/renderer/mdx/r_mdx.h"
#include "renderer/r_game.h"

#define W3I_FLAGS_AFTER_STRINGS (8 * 4 + 4 * 4 + 2 * 4) // bytes; camera bounds, margins, playable size
#define W3I_WAVES_ON_CLIFF_SHORES 0x0800
#define W3I_WAVES_ON_ROLLING_SHORES 0x1000
#define SHORE_WAVES_DEPTH (25.0f / 128) // tiles; the Game.dll "WavesDepth" threshold, as used by Warsmash
#define SHORE_WAVE_CULL_RADIUS (TILE_SIZE * 2) // world units; covers the emitters' spread around the tile
#define SHORE_WAVE_PHASE_STEP 733 // ms per tile; keeps neighbouring emitters out of step

typedef struct {
    model_t const *model;
    vec3_t origin;
    float angle;
    uint32_t phase;
} w3ShoreWave_t;

static w3ShoreWave_t *shore_waves;
static uint32_t shore_wave_count;

bool R_W3ParseMapInfoFlags(uint8_t const *data, size_t size, uint32_t *flags) {
    size_t offset = 12; /* format, save count and editor version */
    FOR_LOOP(i, 4) { /* name, author, description, recommended players */
        uint8_t const *end = offset < size ? memchr(data + offset, 0, size - offset) : NULL;
        if (!end) return false;
        offset = (size_t)(end - data) + 1;
    }
    offset += W3I_FLAGS_AFTER_STRINGS;
    if (offset > size || size - offset < sizeof(*flags)) return false;
    memcpy(flags, data + offset, sizeof(*flags));
    return true;
}

/* Corners run counter-clockwise from the tile's minimum corner. One deep corner is a bay (inside corner),
 * three are a headland (outside corner), and two adjacent ones a straight shore. Diagonal pairs and tiles
 * entirely deep or shallow have no model. Rotations are the ones retail's models are authored for. */
bool R_W3ShoreWavePlacement(float const depth[4], w3ShoreKind_t *kind, float *angle) {
    bool const a = depth[0] > SHORE_WAVES_DEPTH, b = depth[1] > SHORE_WAVES_DEPTH;
    bool const c = depth[2] > SHORE_WAVES_DEPTH, d = depth[3] > SHORE_WAVES_DEPTH;
    switch (a + b + c + d) {
        case 1:
            *kind = W3_SHORE_INSIDE_CORNER;
            *angle = (a ? -3 * M_PI / 4 : b ? -M_PI / 4 : c ? M_PI / 4 : 3 * M_PI / 4) - 3 * M_PI / 4;
            return true;
        case 2:
            *kind = W3_SHORE_LINE;
            if (a && b) *angle = -M_PI / 2;
            else if (b && c) *angle = 0;
            else if (c && d) *angle = M_PI / 2;
            else if (a && d) *angle = M_PI;
            else return false;
            return true;
        case 3:
            *kind = W3_SHORE_OUTSIDE_CORNER;
            *angle = (!a ? -3 * M_PI / 4 : !b ? -M_PI / 4 : !c ? M_PI / 4 : 3 * M_PI / 4) + 5 * M_PI / 4;
            return true;
        default:
            return false;
    }
}

void R_W3ClearShoreWaves(void) {
    if (shore_waves) ri.MemFree(shore_waves);
    shore_waves = NULL;
    shore_wave_count = 0;
}

static bool R_W3ShoreTileShowsWaves(war3mapVertex_t const *tile, uint32_t map_flags) {
    return map_flags & (IsTileCliff(tile) ? W3I_WAVES_ON_CLIFF_SHORES : W3I_WAVES_ON_ROLLING_SHORES);
}

void R_W3BuildShoreWaves(war3map_t const *map, uint32_t map_flags) {
    wc3WaterStyle_t const *style = R_WaterStyle();
    model_t const *models[W3_SHORE_COUNT] = { NULL };
    uint32_t const capacity = (map->width - 1) * (map->height - 1);

    R_W3ClearShoreWaves();
    if (!(map_flags & (W3I_WAVES_ON_CLIFF_SHORES | W3I_WAVES_ON_ROLLING_SHORES))) return;
    FOR_LOOP(kind, W3_SHORE_COUNT) {
        if (style->shore_models[kind][0]) models[kind] = R_LoadRegisteredModel(style->shore_models[kind]);
    }
    shore_waves = ri.MemAlloc(capacity * sizeof(*shore_waves));
    FOR_LOOP(y, map->height - 1) FOR_LOOP(x, map->width - 1) {
        struct War3MapVertex tile[4];
        w3ShoreKind_t kind;
        float angle;
        GetTileVertices(x, y, map, tile);
        if (!IsTileWater(tile) || !R_W3ShoreTileShowsWaves(tile, map_flags)) continue;
        /* GetTileVertices orders (x+1,y+1), (x,y+1), (x+1,y), (x,y). */
        war3mapVertex_t const *const corner[4] = { &tile[3], &tile[2], &tile[0], &tile[1] };
        uint32_t const corner_x[4] = { x, x + 1, x + 1, x }, corner_y[4] = { y, y, y + 1, y + 1 };
        float depth[4], surface = 0;
        FOR_LOOP(i, 4) {
            float const level = GetWar3MapVertexWaterLevel(corner[i]);
            depth[i] = corner[i]->water ?
                (level - R_GetVertexPosition(map, corner_x[i], corner_y[i], true).z) / TILE_SIZE : 0;
            surface += level / 4;
        }
        if (!R_W3ShoreWavePlacement(depth, &kind, &angle)) continue;
        if (!models[kind] || models[kind]->modeltype != ID_MDLX || !models[kind]->mdx ||
            models[kind]->mdx->num_sequences < 1) continue;
        shore_waves[shore_wave_count++] = (w3ShoreWave_t){
            .model = models[kind],
            .origin = { map->center.x + (x + 0.5f) * TILE_SIZE, map->center.y + (y + 0.5f) * TILE_SIZE, surface + 1 },
            .angle = angle,
            .phase = (x + y * map->width) * SHORE_WAVE_PHASE_STEP,
        };
    }
}

uint32_t R_W3ShoreWaveCount(void) {
    return shore_wave_count;
}

/* Retail cycles a doodad's stands. The shoreline emitter is hidden for most of them and pulses visible inside
 * "Stand 3", so playing one fixed sequence can show no wave at all. */
static uint32_t R_W3ShoreWaveFrame(mdxModel_t const *model, uint32_t time) {
    uint32_t total = 0;
    FOR_LOOP(i, model->num_sequences) total += model->sequences[i].interval[1] - model->sequences[i].interval[0];
    if (!total) return model->sequences[0].interval[0];
    time %= total;
    FOR_LOOP(i, model->num_sequences) {
        mdxSequence_t const *sequence = &model->sequences[i];
        uint32_t const duration = sequence->interval[1] - sequence->interval[0];
        if (time < duration) return sequence->interval[0] + time;
        time -= duration;
    }
    return model->sequences[0].interval[0];
}

/* The models are particle emitters; MDLX_RenderParticleEmitters only enqueues outside the alpha phase. */
void R_W3DrawShoreWaves(void) {
    if (tr.render_phase != RENDER_PHASE_SOLID) return;
    FOR_LOOP(i, shore_wave_count) {
        w3ShoreWave_t const *wave = &shore_waves[i];
        if (!Frustum_ContainsSphere(&tr.viewDef.frustum,
                                    &(sphere3_t){ .center = wave->origin, .radius = SHORE_WAVE_CULL_RADIUS }))
            continue;
        renderEntity_t entity = {
            .origin = wave->origin,
            .model = wave->model,
            .number = MAX_GAME_ENTITIES, /* no pause clock or snapshot identity */
            .angle = wave->angle,
            .scale = 1.0f,
            .flags = RF_NO_SHADOW,
        };
        entity.frame = R_W3ShoreWaveFrame(wave->model->mdx, tr.viewDef.time + wave->phase);
        entity.oldframe = entity.frame;
        R_RenderModel(&entity);
    }
}
