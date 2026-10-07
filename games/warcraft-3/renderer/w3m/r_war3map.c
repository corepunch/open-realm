#include "r_war3map.h"
#include "../mdx/r_mdx.h"
#include "renderer/r_shader.h"
#include <float.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define WC3_CAMERA_HEIGHT_RADIUS 4 // terrain cells; half-width of the client camera blur footprint
mapsegment_t *g_mapSegments = NULL;
maplayer_t *g_groundLayers = NULL;
static cameraHeightMap_t w3_camera_height;

#define WC3_TERRAIN_DEFORM_MAX 32
#define WC3_TERRAIN_DEFORM_UPDATE_MS 33
typedef struct {
    terrainDeform_t params;
    uint32_t start_time, stop_time, stop_fade_ms;
    float stop_base_fade;
    bool active, stopping;
} wc3TerrainDeformation_t;
static wc3TerrainDeformation_t w3_deformations[WC3_TERRAIN_DEFORM_MAX];
static float *w3_terrain_offsets;
static uint8_t *w3_terrain_dirty_segments;
static uint32_t w3_terrain_offset_count, w3_deform_last_update, w3_terrain_segment_count;
static bool w3_terrain_rebuild_pending;
static int w3_deform_x0, w3_deform_y0, w3_deform_x1, w3_deform_y1;
static bool w3_deform_bounds_valid;
static bool w3_warn_deform_unavailable, w3_warn_deform_type, w3_warn_deform_pool;
static float r_w3_camera_grid_height(void const *data, uint32_t x, uint32_t y);
static void R_W3RebuildCameraHeightMap(void);
static void R_W3ResetTerrainDeformations(void);
static mapsegment_t *R_BuildMapSegment(war3map_t const *map, uint32_t sx, uint32_t sy);
static vec3_t R_GetMapVertexPoint(war3map_t const *map, uint32_t x, uint32_t y);
static void R_LoadMapSegments(war3map_t const *map);
static bool R_W3GetGridBounds(war3map_t const *map, wc3TerrainDeformation_t const *deform,
                              int *x0, int *y0, int *x1, int *y1);

static void R_W3RebuildCameraHeightMap(void) {
    if (!tr.world) return;
    R_BuildCameraHeightMap(&(cameraHeightBuild_t){ .map = &w3_camera_height, .data = tr.world,
        .width = tr.world->width, .height_count = tr.world->height, .radius = WC3_CAMERA_HEIGHT_RADIUS,
        .samples = BZ_BROAD_HEIGHT_SAMPLES, .origin = tr.world->center, .cell_size = TILE_SIZE,
        .get_height = r_w3_camera_grid_height });
}

static float R_W3SmoothStep(float t) {
    t = MAX(0.0f, MIN(1.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

static uint32_t R_W3DeformHash(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t v = x * 0x9e3779b9u ^ y * 0x85ebca6bu ^ seed * 0xc2b2ae35u;
    v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu; v ^= v >> 16;
    return v;
}

static float R_W3DeformationValue(wc3TerrainDeformation_t const *deform, float x, float y, uint32_t now) {
    terrainDeform_t const *p = &deform->params;
    float elapsed = (float)(now - deform->start_time) * 0.001f;
    float duration = (float)p->duration_ms * 0.001f;
    float fade = 1.0f, dx, dy, radius = 0.0f, shape = 0.0f;

    if (deform->stopping) {
        if (!deform->stop_fade_ms || now - deform->stop_time >= deform->stop_fade_ms) return 0.0f;
        fade *= deform->stop_base_fade *
                (1.0f - R_W3SmoothStep((float)(now - deform->stop_time) / deform->stop_fade_ms));
    } else if (!(p->flags & TERRAIN_DEFORM_PERMANENT)) {
        if (!duration || elapsed >= duration) return 0.0f;
        fade *= sinf((float)M_PI * elapsed / duration);
    }

    dx = x - p->origin.x; dy = y - p->origin.y;
    switch (p->type) {
    case TERRAIN_DEFORM_CRATER:
        radius = p->crater.radius;
        if (radius <= 0.0f) return 0.0f;
        {
            float q = sqrtf(dx * dx + dy * dy) / radius;
            float edge = 1.0f - R_W3SmoothStep(q);
            shape = -fabsf(p->crater.depth) * edge * edge;
        }
        break;
    case TERRAIN_DEFORM_RIPPLE:
        radius = p->ripple.radius;
        if (radius <= 0.0f) return 0.0f;
        {
            float q = sqrtf(dx * dx + dy * dy) / radius;
            float cycles = MAX(1.0f, p->ripple.space_waves);
            float period = MAX(0.05f, p->ripple.time_waves);
            float inner = MAX(0.0f, MIN(0.99f, p->ripple.radius_start));
            if (q > 1.0f || q < inner) return 0.0f;
            shape = p->ripple.depth * sinf((q - elapsed / period) * cycles * 2.0f * (float)M_PI) *
                    R_W3SmoothStep((1.0f - q) / MAX(0.001f, 1.0f - inner));
            if (p->flags & TERRAIN_DEFORM_LIMIT_NEGATIVE) shape = MAX(0.0f, shape);
        }
        break;
    case TERRAIN_DEFORM_WAVE:
        {
            float dir_x = p->wave.dir.x, dir_y = p->wave.dir.y;
            float dir_len = sqrtf(dir_x * dir_x + dir_y * dir_y);
            float along, across, speed = fabsf(p->wave.speed);
            radius = p->wave.radius;
            if (dir_len <= 0.0001f || speed <= 0.0f || radius <= 0.0f) return 0.0f;
            dir_x /= dir_len; dir_y /= dir_len;
            along = dx * dir_x + dy * dir_y;
            across = fabsf(dx * dir_y - dy * dir_x);
            if (along < 0.0f || along > p->wave.distance || across > radius) return 0.0f;
            shape = p->wave.depth * sinf((along - speed * elapsed) * (float)M_PI / speed) *
                    (1.0f - R_W3SmoothStep(across / radius));
            break;
        }
    case TERRAIN_DEFORM_RANDOM:
        radius = p->random.radius;
        if (radius <= 0.0f) return 0.0f;
        {
            float q = sqrtf(dx * dx + dy * dy) / radius;
            uint32_t interval = MAX(1u, p->update_ms);
            uint32_t sample = (now - deform->start_time) / interval;
            float random_value;
            if (q > 1.0f) return 0.0f;
            random_value = (float)(R_W3DeformHash((uint32_t)lroundf(x / TILE_SIZE),
                                                   (uint32_t)lroundf(y / TILE_SIZE),
                                                   p->id ^ sample) & 0xffffu) / 65535.0f;
            shape = LerpNumber(p->random.min_delta, p->random.max_delta, random_value) * (1.0f - R_W3SmoothStep(q));
            break;
        }
    default:
        return 0.0f;
    }
    return shape * fade;
}

static bool R_W3DeformationBounds(wc3TerrainDeformation_t const *deform, float *min_x,
                                  float *min_y, float *max_x, float *max_y) {
    terrainDeform_t const *p = &deform->params;
    float radius;
    FOR_LOOP(i, TERRAIN_DEFORM_FLOATS) if (!isfinite(p->data[i])) return false;
    switch (p->type) {
    case TERRAIN_DEFORM_WAVE: {
        float dir_x = p->wave.dir.x, dir_y = p->wave.dir.y, distance = p->wave.distance, width = p->wave.radius;
        float len = sqrtf(dir_x * dir_x + dir_y * dir_y);
        if (len <= 0.0001f || distance <= 0.0f || width <= 0.0f) return false;
        dir_x /= len; dir_y /= len;
        *min_x = p->origin.x + MIN(0.0f, dir_x * distance) - fabsf(dir_y) * width;
        *max_x = p->origin.x + MAX(0.0f, dir_x * distance) + fabsf(dir_y) * width;
        *min_y = p->origin.y + MIN(0.0f, dir_y * distance) - fabsf(dir_x) * width;
        *max_y = p->origin.y + MAX(0.0f, dir_y * distance) + fabsf(dir_x) * width;
        return true;
    }
    case TERRAIN_DEFORM_CRATER: radius = p->crater.radius; break;
    case TERRAIN_DEFORM_RIPPLE: radius = p->ripple.radius; break;
    case TERRAIN_DEFORM_RANDOM: radius = p->random.radius; break;
    default: return false;
    }
    if (radius <= 0.0f) return false;
    *min_x = p->origin.x - radius; *max_x = p->origin.x + radius;
    *min_y = p->origin.y - radius; *max_y = p->origin.y + radius;
    return true;
}

static void R_W3MarkDeformationBounds(wc3TerrainDeformation_t const *deform) {
    war3map_t const *map = tr.world;
    int x0, y0, x1, y1;
    if (!map || !w3_terrain_dirty_segments || !R_W3GetGridBounds(map, deform, &x0, &y0, &x1, &y1)) return;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++)
        for (int sy = y / SEGMENT_SIZE - (y % SEGMENT_SIZE == 0 && y > 0);
             sy <= y / SEGMENT_SIZE && sy < (int)((map->height - 1) / SEGMENT_SIZE); sy++)
            for (int sx = x / SEGMENT_SIZE - (x % SEGMENT_SIZE == 0 && x > 0);
                 sx <= x / SEGMENT_SIZE && sx < (int)((map->width - 1) / SEGMENT_SIZE); sx++)
                w3_terrain_dirty_segments[sx + sy * ((map->width - 1) / SEGMENT_SIZE)] = 1;
}

static void R_W3MarkMapVertex(war3map_t const *map, uint32_t x, uint32_t y) {
    if (!map || !w3_terrain_dirty_segments) return;
    for (int sy = (int)y / SEGMENT_SIZE - (y % SEGMENT_SIZE == 0 && y > 0);
         sy <= (int)y / SEGMENT_SIZE && sy < (int)((map->height - 1) / SEGMENT_SIZE); sy++)
        for (int sx = (int)x / SEGMENT_SIZE - (x % SEGMENT_SIZE == 0 && x > 0);
             sx <= (int)x / SEGMENT_SIZE && sx < (int)((map->width - 1) / SEGMENT_SIZE); sx++)
            w3_terrain_dirty_segments[sx + sy * ((map->width - 1) / SEGMENT_SIZE)] = 1;
}

static bool R_W3GetGridBounds(war3map_t const *map, wc3TerrainDeformation_t const *deform,
                              int *x0, int *y0, int *x1, int *y1) {
    float min_x, min_y, max_x, max_y;
    if (!map || !map->width || !map->height ||
        !R_W3DeformationBounds(deform, &min_x, &min_y, &max_x, &max_y) ||
        !isfinite(min_x) || !isfinite(min_y) || !isfinite(max_x) || !isfinite(max_y) ||
        min_x > map->center.x + (map->width - 1) * TILE_SIZE || max_x < map->center.x ||
        min_y > map->center.y + (map->height - 1) * TILE_SIZE || max_y < map->center.y) return false;
    *x0 = (int)MAX(0.0f, MIN((float)map->width - 1.0f,
                             floorf((min_x - map->center.x) / TILE_SIZE)));
    *y0 = (int)MAX(0.0f, MIN((float)map->height - 1.0f,
                             floorf((min_y - map->center.y) / TILE_SIZE)));
    *x1 = (int)MAX(0.0f, MIN((float)map->width - 1.0f,
                             ceilf((max_x - map->center.x) / TILE_SIZE)));
    *y1 = (int)MAX(0.0f, MIN((float)map->height - 1.0f,
                             ceilf((max_y - map->center.y) / TILE_SIZE)));
    return true;
}

void R_W3StartTerrainDeformation(terrainDeform_t const *deformation) {
    wc3TerrainDeformation_t *slot = NULL;
    if (!deformation || !deformation->id || !tr.world) return;
    if (!w3_terrain_offsets) {
        if (!w3_warn_deform_unavailable) {
            fprintf(stderr, "WC3 renderer: terrain deformation unavailable for this map\n");
            w3_warn_deform_unavailable = true;
        }
        return;
    }
    if (deformation->type > TERRAIN_DEFORM_RANDOM) {
        if (!w3_warn_deform_type) {
            fprintf(stderr, "WC3 renderer: unsupported terrain deformation type %u\n", (unsigned)deformation->type);
            w3_warn_deform_type = true;
        }
        return;
    }
    FOR_LOOP(i, WC3_TERRAIN_DEFORM_MAX) {
        if (w3_deformations[i].active && w3_deformations[i].params.id == deformation->id) {
            slot = &w3_deformations[i]; break;
        }
        if (!slot && !w3_deformations[i].active) slot = &w3_deformations[i];
    }
    if (!slot) {
        if (!w3_warn_deform_pool) {
            fprintf(stderr, "WC3 renderer: terrain deformation pool full; replacing oldest entry\n");
            w3_warn_deform_pool = true;
        }
        slot = &w3_deformations[0];
        FOR_LOOP(i, WC3_TERRAIN_DEFORM_MAX)
            if (w3_deformations[i].start_time < slot->start_time) slot = &w3_deformations[i];
    }
    if (slot->active)
        R_W3MarkDeformationBounds(slot);
    *slot = (wc3TerrainDeformation_t){ .params = *deformation,
        .start_time = tr.viewDef.time, .active = true };
    R_W3MarkDeformationBounds(slot);
    w3_terrain_rebuild_pending = true;
    w3_deform_last_update = 0;
}

void R_W3StopTerrainDeformation(uint32_t id, uint32_t fade_ms) {
    if (!id) return;
    FOR_LOOP(i, WC3_TERRAIN_DEFORM_MAX) {
        wc3TerrainDeformation_t *deform = &w3_deformations[i];
        if (!deform->active || deform->params.id != id) continue;
        if (!fade_ms) {
            deform->active = false;
            R_W3MarkDeformationBounds(deform);
            w3_terrain_rebuild_pending = true;
        }
        else {
            deform->stopping = true;
            deform->stop_time = tr.viewDef.time;
            deform->stop_fade_ms = fade_ms;
            if (deform->params.flags & TERRAIN_DEFORM_PERMANENT) deform->stop_base_fade = 1.0f;
            else {
                uint32_t elapsed = tr.viewDef.time - deform->start_time;
                deform->stop_base_fade = deform->params.duration_ms
                    ? sinf((float)M_PI * MIN(1.0f, (float)elapsed / deform->params.duration_ms)) : 0.0f;
            }
            w3_terrain_rebuild_pending = true;
        }
        w3_deform_last_update = 0;
        return;
    }
}

void R_W3StopAllTerrainDeformations(void) {
    R_W3ResetTerrainDeformations();
}

float R_W3TerrainOffsetAtPoint(float x, float y) {
    war3map_t const *map = tr.world;
    float gx, gy, fx, fy, a, b, c, d, ab, cd;
    uint32_t x0, y0, x1, y1;
    if (!map || !w3_terrain_offsets ||
        w3_terrain_offset_count != (size_t)map->width * map->height) return 0.0f;
    gx = MAX(0.0f, MIN((float)map->width - 1.0f, (x - map->center.x) / TILE_SIZE));
    gy = MAX(0.0f, MIN((float)map->height - 1.0f, (y - map->center.y) / TILE_SIZE));
    fx = floorf(gx); fy = floorf(gy); x0 = (uint32_t)fx; y0 = (uint32_t)fy;
    x1 = MIN(map->width - 1, x0 + 1); y1 = MIN(map->height - 1, y0 + 1);
    a = w3_terrain_offsets[x0 + y0 * map->width]; b = w3_terrain_offsets[x1 + y0 * map->width];
    c = w3_terrain_offsets[x0 + y1 * map->width]; d = w3_terrain_offsets[x1 + y1 * map->width];
    ab = LerpNumber(a, b, gx - fx); cd = LerpNumber(c, d, gx - fx);
    return LerpNumber(ab, cd, gy - fy);
}

static float r_w3_camera_grid_height(void const *data, uint32_t x, uint32_t y) {
    war3map_t const *map = data;
    return GetWar3MapVertexHeight(GetWar3MapVertex(map, x, y));
}

static void R_FreeMapLayers(maplayer_t * *layers) {
    while (*layers) {
        maplayer_t *layer = *layers;
        *layers = layer->next;
        if (layer->buffer) R_ReleaseVertexArrayObject((buffer_t *)layer->buffer);
        ri.MemFree(layer);
    }
}

static void R_FreeMapSegments(void) {
    while (g_mapSegments) {
        mapsegment_t *segment = g_mapSegments;
        g_mapSegments = segment->next;
        R_FreeMapLayers(&segment->layers);
        ri.MemFree(segment);
    }
}

static void R_FreeWar3Map(war3map_t *map) {
    if (!map) return;
    SAFE_DELETE(map->grounds, ri.MemFree);
    SAFE_DELETE(map->cliffs, ri.MemFree);
    SAFE_DELETE(map->vertices, ri.MemFree);
    ri.MemFree(map);
}

static void R_W3ClearTerrainDeformations(void) {
    SAFE_DELETE(w3_terrain_offsets, ri.MemFree);
    SAFE_DELETE(w3_terrain_dirty_segments, ri.MemFree);
    w3_terrain_offset_count = 0;
    w3_terrain_segment_count = 0;
    w3_deform_last_update = 0;
    w3_terrain_rebuild_pending = false;
    w3_deform_bounds_valid = false;
    memset(w3_deformations, 0, sizeof(w3_deformations));
}

static void R_W3ResetTerrainDeformations(void) {
    memset(w3_deformations, 0, sizeof(w3_deformations));
    if (w3_terrain_offsets) memset(w3_terrain_offsets, 0, (size_t)w3_terrain_offset_count * sizeof(float));
    if (w3_terrain_dirty_segments) memset(w3_terrain_dirty_segments, 1, w3_terrain_segment_count);
    w3_deform_last_update = 0;
    w3_deform_bounds_valid = false;
    w3_terrain_rebuild_pending = true;
}

static void R_W3SetMapTerrainOffsets(war3map_t *map) {
    size_t count;
    R_W3ClearTerrainDeformations();
    if (!map || !map->width || !map->height) return;
    if (map->width - 1 < SEGMENT_SIZE || map->height - 1 < SEGMENT_SIZE ||
        (map->width - 1) % SEGMENT_SIZE || (map->height - 1) % SEGMENT_SIZE) {
        fprintf(stderr, "WC3 renderer: terrain deformation requires complete terrain segments\n");
        return;
    }
    count = (size_t)map->width * map->height;
    if (count > UINT32_MAX || count > SIZE_MAX / sizeof(float)) {
        fprintf(stderr, "WC3 renderer: terrain deformation grid dimensions overflow\n");
        return;
    }
    w3_terrain_segment_count = ((map->width - 1) / SEGMENT_SIZE) * ((map->height - 1) / SEGMENT_SIZE);
    w3_terrain_offsets = ri.MemAlloc((long)(count * sizeof(float)));
    w3_terrain_dirty_segments = ri.MemAlloc((long)w3_terrain_segment_count);
    if (!w3_terrain_offsets || !w3_terrain_dirty_segments) {
        fprintf(stderr, "WC3 renderer: failed to allocate terrain deformation grid\n");
        R_W3ClearTerrainDeformations();
        return;
    }
    memset(w3_terrain_offsets, 0, count * sizeof(float));
    memset(w3_terrain_dirty_segments, 0, w3_terrain_segment_count);
    w3_terrain_offset_count = (uint32_t)count;
}

/* A deformed segment rebuilds its own water and cliff layers and overwrites its slice of each whole-map
 * ground buffer, so the ground stays one draw call per layer while a deformation is running. */
static void R_W3RebuildSegmentGeometry(mapsegment_t *segment) {
    mapsegment_t *fresh;
    if (!tr.world || !segment) return;
    R_UpdateGroundSegment(tr.world, segment->sx, segment->sy);
    fresh = R_BuildMapSegment(tr.world, segment->sx, segment->sy);
    R_FreeMapLayers(&segment->layers);
    segment->layers = fresh->layers;
    fresh->layers = NULL;
    segment->bbox = (box3_t){ .min = MAKE(vec3_t, FLT_MAX, FLT_MAX, FLT_MAX),
                              .max = MAKE(vec3_t, -FLT_MAX, -FLT_MAX, -FLT_MAX) };
    FOR_LOOP(sx, SEGMENT_SIZE + 1) FOR_LOOP(sy, SEGMENT_SIZE + 1) {
        uint32_t x = segment->sx * SEGMENT_SIZE + sx, y = segment->sy * SEGMENT_SIZE + sy;
        vec3_t v = R_GetMapVertexPoint(tr.world, x, y);
        segment->bbox.min.x = MIN(segment->bbox.min.x, v.x);
        segment->bbox.min.y = MIN(segment->bbox.min.y, v.y);
        segment->bbox.min.z = MIN(segment->bbox.min.z, v.z);
        segment->bbox.max.x = MAX(segment->bbox.max.x, v.x);
        segment->bbox.max.y = MAX(segment->bbox.max.y, v.y);
        segment->bbox.max.z = MAX(segment->bbox.max.z, v.z);
    }
    ri.MemFree(fresh);
}

void R_W3UpdateTerrainDeformations(void) {
    war3map_t const *map = tr.world;
    bool active = false, changed = false, bounds_valid = false;
    uint32_t now = tr.viewDef.time;
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
    int current_x0 = 0, current_y0 = 0, current_x1 = -1, current_y1 = -1;
    bool current_bounds_valid;
    int deform_x0[WC3_TERRAIN_DEFORM_MAX], deform_y0[WC3_TERRAIN_DEFORM_MAX];
    int deform_x1[WC3_TERRAIN_DEFORM_MAX], deform_y1[WC3_TERRAIN_DEFORM_MAX];
    if (!map || !w3_terrain_offsets ||
        w3_terrain_offset_count != (size_t)map->width * map->height) return;
    if (!w3_terrain_dirty_segments) return;
    FOR_LOOP(i, WC3_TERRAIN_DEFORM_MAX) active |= w3_deformations[i].active;
    if (!active && !w3_deform_last_update && !w3_terrain_rebuild_pending) return;
    if (w3_deform_last_update && now - w3_deform_last_update < WC3_TERRAIN_DEFORM_UPDATE_MS &&
        !w3_terrain_rebuild_pending) return;
    w3_deform_last_update = now ? now : 1;
    FOR_LOOP(i, WC3_TERRAIN_DEFORM_MAX) {
        wc3TerrainDeformation_t *deform = &w3_deformations[i];
        deform_x0[i] = 0; deform_y0[i] = 0; deform_x1[i] = -1; deform_y1[i] = -1;
        if (!deform->active) continue;
        if (deform->stopping && (!deform->stop_fade_ms || now - deform->stop_time >= deform->stop_fade_ms)) {
            deform->active = false;
            w3_terrain_rebuild_pending = true;
            continue;
        }
        if (!deform->stopping && !(deform->params.flags & TERRAIN_DEFORM_PERMANENT) &&
            (!deform->params.duration_ms || now - deform->start_time >= deform->params.duration_ms)) {
            deform->active = false;
            w3_terrain_rebuild_pending = true;
            continue;
        }
        if (!R_W3GetGridBounds(map, deform, &deform_x0[i], &deform_y0[i], &deform_x1[i], &deform_y1[i])) {
            deform->active = false;
            w3_terrain_rebuild_pending = true;
            continue;
        }
        x0 = bounds_valid ? MIN(x0, deform_x0[i]) : deform_x0[i];
        y0 = bounds_valid ? MIN(y0, deform_y0[i]) : deform_y0[i];
        x1 = bounds_valid ? MAX(x1, deform_x1[i]) : deform_x1[i];
        y1 = bounds_valid ? MAX(y1, deform_y1[i]) : deform_y1[i];
        bounds_valid = true;
    }
    current_bounds_valid = bounds_valid;
    current_x0 = x0; current_y0 = y0; current_x1 = x1; current_y1 = y1;
    if (w3_deform_bounds_valid) {
        x0 = bounds_valid ? MIN(x0, w3_deform_x0) : w3_deform_x0;
        y0 = bounds_valid ? MIN(y0, w3_deform_y0) : w3_deform_y0;
        x1 = bounds_valid ? MAX(x1, w3_deform_x1) : w3_deform_x1;
        y1 = bounds_valid ? MAX(y1, w3_deform_y1) : w3_deform_y1;
        bounds_valid = true;
    }
    if (bounds_valid) {
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            float wx = map->center.x + x * TILE_SIZE, wy = map->center.y + y * TILE_SIZE;
            float offset = 0.0f;
            uint32_t index = x + y * map->width;
            FOR_LOOP(i, WC3_TERRAIN_DEFORM_MAX) {
                wc3TerrainDeformation_t const *deform = &w3_deformations[i];
                if (!deform->active || x < deform_x0[i] || x > deform_x1[i] ||
                    y < deform_y0[i] || y > deform_y1[i]) continue;
                offset += R_W3DeformationValue(deform, wx, wy, now);
            }
            if (fabsf(w3_terrain_offsets[index] - offset) < 0.001f) continue;
            w3_terrain_offsets[index] = offset;
            changed = true;
            R_W3MarkMapVertex(map, (uint32_t)x, (uint32_t)y);
        }
    }
    active = false;
    FOR_LOOP(i, WC3_TERRAIN_DEFORM_MAX) active |= w3_deformations[i].active;
    if (active) {
        w3_deform_bounds_valid = current_bounds_valid;
        w3_deform_x0 = current_x0; w3_deform_y0 = current_y0;
        w3_deform_x1 = current_x1; w3_deform_y1 = current_y1;
    } else w3_deform_bounds_valid = false;
    if (changed) w3_terrain_rebuild_pending = true;
    if (!active) w3_deform_last_update = 0;
}

static bool R_W3SegmentBordersDirty(uint32_t sx, uint32_t sy, uint32_t columns, uint32_t rows) {
    for (int y = (int)sy - 1; y <= (int)sy + 1; y++) for (int x = (int)sx - 1; x <= (int)sx + 1; x++)
        if (x >= 0 && y >= 0 && x < (int)columns && y < (int)rows && w3_terrain_dirty_segments[x + y * columns])
            return true;
    return false;
}

static void R_W3EmitChangedTerrain(void) {
    war3map_t const *map = tr.world;
    uint32_t columns, rows;
    bool rebuilt = false;
    if (!map || !w3_terrain_dirty_segments) return;
    columns = (map->width - 1) / SEGMENT_SIZE; rows = (map->height - 1) / SEGMENT_SIZE;
    R_BeginTerrainNormalCache(map);
    FOR_EACH_LIST(mapsegment_t, segment, g_mapSegments) {
        if (!w3_terrain_dirty_segments[segment->sx + segment->sy * columns]) continue;
        R_W3RebuildSegmentGeometry(segment);
        rebuilt = true;
    }
    /* Clean neighbours join the cliff bake as weld context only, so a rebuilt segment's
     * border normals weld across the seam as they did in the load-time whole-map bake. */
    if (rebuilt) FOR_LOOP(sy, rows) FOR_LOOP(sx, columns) {
        if (w3_terrain_dirty_segments[sx + sy * columns] || !R_W3SegmentBordersDirty(sx, sy, columns, rows))
            continue;
        FOR_LOOP(cliff, map->num_cliffs) {
            R_BakeMapSegmentCliffsForWeld(map, sx, sy, cliff);
        }
    }
    if (rebuilt) {
        R_FinishCliffs();
        R_InvalidateBlightLayer();
    }
    R_EndTerrainNormalCache();
    memset(w3_terrain_dirty_segments, 0, w3_terrain_segment_count);
    w3_terrain_rebuild_pending = false;
}

/* Retail layers the nested "<tileset>.mpq" from War3(x).mpq between map imports and base data while a map is
 * loaded. It supplies the tileset's ReplaceableTextures (Cliff0/Cliff1, uber splats, water frames) and a few
 * model skins; Outland's abyss cliffs exist only there. Kept open so membership checks are hash lookups. */
static struct {
    handle_t archive;
    void *data;
    char name[8];
} w3_tileset_archive;

static void R_W3CloseTilesetArchive(void) {
    if (w3_tileset_archive.archive) SFileCloseArchive(w3_tileset_archive.archive);
    if (w3_tileset_archive.data) ri.FS_FreeFile(w3_tileset_archive.data);
    memset(&w3_tileset_archive, 0, sizeof(w3_tileset_archive));
}

void R_W3OpenTilesetArchive(uint8_t tileset) {
    int size;

    R_W3CloseTilesetArchive();
    snprintf(w3_tileset_archive.name, sizeof(w3_tileset_archive.name), "%c.mpq", tileset);
    size = ri.FS_ReadFile(w3_tileset_archive.name, &w3_tileset_archive.data);
    if (size <= 0 || !w3_tileset_archive.data) {
        fprintf(stderr, "WC3 renderer: no tileset archive %s; tileset textures resolve from base data\n",
                w3_tileset_archive.name);
        R_W3CloseTilesetArchive();
        return;
    }
    if (!SFileOpenArchiveFromMemory(w3_tileset_archive.data, (uint32_t)size, 0, &w3_tileset_archive.archive)) {
        fprintf(stderr, "WC3 renderer: failed to open tileset archive %s\n", w3_tileset_archive.name);
        R_W3CloseTilesetArchive();
    }
}

bool R_GameAssetCandidate(cstring_t asset, string_t candidate, uint32_t candidate_size) {
    handle_t file;
    int written;

    if (!w3_tileset_archive.archive || !asset || !*asset || !candidate || !candidate_size) return false;
    if (!SFileOpenFileEx(w3_tileset_archive.archive, asset, SFILE_OPEN_FROM_MPQ, &file)) return false;
    SFileCloseFile(file);
    written = snprintf(candidate, candidate_size, "%s\\%s", w3_tileset_archive.name, asset);
    return written > 0 && (uint32_t)written < candidate_size;
}

void _W3M_ClearMap(void) {
    texture_t *shadow = tr.texture[TEX_TERRAIN_SHADOW];

    R_FreeMapSegments();
    R_FreeMapLayers(&g_groundLayers);
    R_W3ClearTerrainDeformations();
    R_ResetTerrainNormalCache();
    R_ResetGroundTextures();
    R_ResetCliffCache();
    R_ResetBlightCache();
    R_FreeCameraHeightMap(&w3_camera_height);
    R_W3CloseTilesetArchive();
    R_ShutdownFogOfWar();
    SAFE_DELETE(tr.minimap, R_ReleaseTexture);
    tr.texture[TEX_TERRAIN_SHADOW] = NULL;
    if (shadow) R_ReleaseTexture(shadow);
    if (tr.world) {
        R_FreeWar3Map((war3map_t *)tr.world);
        tr.world = NULL;
    }
}

static void R_FileReadShadowMap(handle_t hMpq, war3map_t *pWorld) {
    handle_t file;
    if (!SFileOpenFileEx(hMpq, "war3map.shd", SFILE_OPEN_FROM_MPQ, &file)) {
        return;
    }
    int const w = (pWorld->width - 1) * 4;
    int const h = (pWorld->height - 1) * 4;
    string_t shadows = ri.MemAlloc(w * h);
    if (!SFileReadFile(file, shadows, w * h, NULL, NULL)) {
        ri.MemFree(shadows);
        SFileCloseFile(file);
        return;
    }
    texture_t *pShadowmap = R_AllocateTexture(w, h);
    color32_t *pixels = ri.MemAlloc(w * h * sizeof(struct color32));
    FOR_LOOP(i, w * h) {
        uint8_t shadow = (uint8_t)shadows[i];
        pixels[i].r = 0;
        pixels[i].g = 0;
        pixels[i].b = 0;
        pixels[i].a = shadow / 2;
    }
    R_LoadTextureMipLevel(pShadowmap, &(texMip_t){ pixels, w, h, 0, PIXEL_RGBA });
    SFileCloseFile(file);
    ri.MemFree(shadows);
    ri.MemFree(pixels);

    tr.texture[TEX_TERRAIN_SHADOW] = pShadowmap;
}

static mapsegment_t *R_BuildMapSegment(war3map_t const *map, uint32_t sx, uint32_t sy) {
    mapsegment_t *mapSegment = ri.MemAlloc(sizeof(mapsegment_t));
    maplayer_t *mapLayer;
    mapSegment->sx = sx;
    mapSegment->sy = sy;
    mapLayer = R_BuildMapSegmentWater(map, sx, sy);
    R_AddMapSegmentLayer(mapSegment, mapLayer);
    FOR_LOOP(cliff, map->num_cliffs) {
        if ((mapLayer = R_BuildMapSegmentCliffs(map, sx, sy, cliff))) {
            R_AddMapSegmentLayer(mapSegment, mapLayer);
        }
    }
    mapSegment->bbox.min = MAKE(vec3_t, FLT_MAX, FLT_MAX, FLT_MAX);
    mapSegment->bbox.max = MAKE(vec3_t, -FLT_MAX, -FLT_MAX, -FLT_MAX);
    return mapSegment;
}

static void R_BuildGroundLayers(war3map_t const *map) {
    for (uint32_t layer = map->num_grounds; layer > 0; layer--) {
        maplayer_t *mapLayer = R_BuildGroundLayerGlobal(map, layer - 1);
        if (mapLayer) {
            ADD_TO_LIST(mapLayer, g_groundLayers);
        }
    }
}

static vec3_t R_GetMapVertexPoint(war3map_t const *map, uint32_t x, uint32_t y) {
    war3mapVertex_t const *mapVertex = GetWar3MapVertex(map, x, y);
    float height = GetWar3MapVertexHeight(mapVertex);
    if (map == tr.world) height += R_W3TerrainOffsetAtPoint(map->center.x + x * TILE_SIZE,
                                                            map->center.y + y * TILE_SIZE);
    return (vec3_t) {
        .x = map->center.x + x * TILE_SIZE,
        .y = map->center.y + y * TILE_SIZE,
        .z = height,
    };
}

static void R_LoadMapSegments(war3map_t const *map) {
    FOR_LOOP(fx, (map->width - 1) / SEGMENT_SIZE) {
        FOR_LOOP(fy, (map->height - 1) / SEGMENT_SIZE) {
            mapsegment_t *segment = R_BuildMapSegment(map, fx, fy);
            ADD_TO_LIST(segment, g_mapSegments);
            FOR_LOOP(sx, SEGMENT_SIZE+1) FOR_LOOP(sy, SEGMENT_SIZE+1) {
                uint32_t x = fx * SEGMENT_SIZE + sx, y = fy * SEGMENT_SIZE + sy;
                vec3_t v = R_GetMapVertexPoint(map, x, y);
                segment->bbox.min.x = MIN(segment->bbox.min.x, v.x);
                segment->bbox.min.y = MIN(segment->bbox.min.y, v.y);
                segment->bbox.min.z = MIN(segment->bbox.min.z, v.z);
                segment->bbox.max.x = MAX(segment->bbox.max.x, v.x);
                segment->bbox.max.y = MAX(segment->bbox.max.y, v.y);
                segment->bbox.max.z = MAX(segment->bbox.max.z, v.z);
            }
        }
    }
}

void R_AllocateFogOfWar(war3map_t *map) {
    R_InitFogOfWar((map->width - 1) * 4, (map->height - 1) * 4);
}

static void R_LoadMapMinimap(handle_t hMpq, cstring_t mapFilename) {
    static cstring_t const candidates[] = {
        "war3mapMap.blp",
        "war3mapMap.tga",
        NULL,
    };

    SAFE_DELETE(tr.minimap, R_ReleaseTexture);

    FOR_LOOP(i, sizeof(candidates) / sizeof(candidates[0])) {
        handle_t file;
        PATHSTR path;

        if (!candidates[i]) {
            break;
        }
        if (!SFileOpenFileEx(hMpq, candidates[i], SFILE_OPEN_FROM_MPQ, &file)) {
            continue;
        }
        SFileCloseFile(file);
        snprintf(path, sizeof(path), "%s\\%s", mapFilename, candidates[i]);
        tr.minimap = R_LoadTexture(path);
        return;
    }
}

static bool R_ReadWar3MapVertex(handle_t file, war3mapVertex_t *vert) {
    uint16_t water_and_edge;
    uint8_t flags;
    uint8_t variation;
    uint8_t cliff_and_layer;

    if (!file || !vert) {
        return false;
    }
    memset(vert, 0, sizeof(*vert));
    if (!SFileReadFile(file, &vert->accurate_height, sizeof(vert->accurate_height), NULL, NULL)) {
        return false;
    }
    if (!SFileReadFile(file, &water_and_edge, sizeof(water_and_edge), NULL, NULL)) {
        return false;
    }
    if (!SFileReadFile(file, &flags, sizeof(flags), NULL, NULL)) {
        return false;
    }
    if (!SFileReadFile(file, &variation, sizeof(variation), NULL, NULL)) {
        return false;
    }
    if (!SFileReadFile(file, &cliff_and_layer, sizeof(cliff_and_layer), NULL, NULL)) {
        return false;
    }

    vert->waterlevel = water_and_edge & 0x3FFF;
    vert->mapedge = (water_and_edge & 0x4000) != 0;
    vert->ground = flags & 0x0F;
    vert->ramp = (flags & 0x10) != 0;
    vert->blight = (flags & 0x20) != 0;
    vert->water = (flags & 0x40) != 0;
    vert->boundary = (flags & 0x80) != 0;
    vert->cliffVariation = (variation >> 5) & 0x07;
    vert->groundVariation = variation & 0x1F;
    vert->cliff = (cliff_and_layer >> 4) & 0x0F;
    vert->level = cliff_and_layer & 0x0F;
    return true;
}

war3map_t *FileReadWar3Map(handle_t archive) {
    war3map_t *map = ri.MemAlloc(sizeof(war3map_t));
    handle_t file;
    SFileOpenFileEx(archive, "war3map.w3e", SFILE_OPEN_FROM_MPQ, &file);
    SFileReadFile(file, &map->header, 4, NULL, NULL);
    SFileReadFile(file, &map->version, 4, NULL, NULL);
    SFileReadFile(file, &map->tileset, 1, NULL, NULL);
    SFileReadFile(file, &map->custom, 4, NULL, NULL);
    SFileReadArray(file, map, grounds, 4, ri.MemAlloc);
    SFileReadArray(file, map, cliffs, 4, ri.MemAlloc);
    SFileReadFile(file, &map->width, 4, NULL, NULL);
    SFileReadFile(file, &map->height, 4, NULL, NULL);
    SFileReadFile(file, &map->center, 8, NULL, NULL);
    uint32_t const num_vertices = map->width * map->height;
    int const vertexblocksize = sizeof(war3mapVertex_t) * num_vertices;
    map->vertices = ri.MemAlloc(vertexblocksize);
    R_AllocateFogOfWar(map);
    FOR_LOOP(i, num_vertices) {
        if (!R_ReadWar3MapVertex(file, (war3mapVertex_t *)map->vertices + i)) {
            break;
        }
    }
    SFileCloseFile(file);
    FOR_LOOP(y, map->height) {
//        printf("%04x  ", y);
        FOR_LOOP(x, map->width) {
            war3mapVertex_t *vert = (war3mapVertex_t *)GetWar3MapVertex(map, x, y);
            if (!vert->ramp)
                continue;
            vert->cliffVariation = 0; // used also to mark mid-ramp
            war3mapVertex_t const *l = GetWar3MapVertex(map, x-1, y);
            war3mapVertex_t const *r = GetWar3MapVertex(map, x+1, y);
            war3mapVertex_t const *t = GetWar3MapVertex(map, x, y-1);
            war3mapVertex_t const *b = GetWar3MapVertex(map, x, y+1);
            if (l && r && l->ramp && r->ramp && l->level != r->level) {
                vert->cliffVariation = 1;
            } else if (t && b && t->ramp && b->ramp && t->level != b->level) {
                vert->cliffVariation = 1;
            }
//            printf("%x", GetWar3MapVertex(map, x, y)->level);
        }
//        printf("\n");
    }
    return map;
}

void _W3M_RegisterMap(char const *mapFilename) {
    handle_t hMpq;
    uint8_t *mapData;
    int mapSize;
    war3map_t *map;

    /* A map registration replaces the whole WC3 world.  Free GPU buffers,
     * map-owned models, fog targets and source terrain before creating the
     * next level so repeated campaign transitions do not accumulate state. */
    _W3M_ClearMap();

    /* Load .w3m file (which is itself an MPQ archive) */
    mapSize = ri.FS_ReadFile(mapFilename, (void **)&mapData);
    if (mapSize < 0 || !mapData) {
        ri.error("R_RegisterMap: failed to open map %s\n", mapFilename);
        return;
    }
    
    /* Open the .w3m as a nested MPQ archive to read internal files */
    if (!SFileOpenArchiveFromMemory(mapData, (uint32_t)mapSize, 0, &hMpq)) {
        ri.FS_FreeFile(mapData);
        ri.error("R_RegisterMap: failed to open map archive %s\n", mapFilename);
        return;
    }
    map = FileReadWar3Map(hMpq);
    R_FileReadShadowMap(hMpq, map);
    R_LoadMapMinimap(hMpq, mapFilename);
    SFileCloseArchive(hMpq);
    ri.FS_FreeFile(mapData);
    tr.world = map;
    R_W3OpenTilesetArchive(map->tileset);
    R_LoadBlightTexture(map->tileset);
    R_LoadWaterStyle(map->tileset);
    R_W3SetMapTerrainOffsets(map);
    R_W3RebuildCameraHeightMap();

    R_LoadMapSegments(map);
    R_FinishCliffs();
    R_BuildGroundLayers(map);
    w3_terrain_rebuild_pending = false;
}

float R_W3CameraHeightAtPoint(float x, float y) {
    R_W3UpdateTerrainDeformations();
    return R_SampleCameraHeightMap(&w3_camera_height, x, y) + R_W3TerrainOffsetAtPoint(x, y);
}

/* Sample the exact world terrain source used to build the client camera height map. */
float R_W3TerrainHeightAtPoint(float x, float y) {
    R_W3UpdateTerrainDeformations();
    float gx, gy, tx, ty, h0, h1;
    uint32_t x0, y0, x1, y1;

    if (!tr.world || !tr.world->vertices || !tr.world->width || !tr.world->height) return 0.0f;
    gx = (x - tr.world->center.x) / TILE_SIZE;
    gy = (y - tr.world->center.y) / TILE_SIZE;
    gx = MAX(0.0f, MIN((float)tr.world->width - 1.0f, gx));
    gy = MAX(0.0f, MIN((float)tr.world->height - 1.0f, gy));
    x0 = (uint32_t)floorf(gx); y0 = (uint32_t)floorf(gy);
    x1 = MIN(tr.world->width - 1, x0 + 1); y1 = MIN(tr.world->height - 1, y0 + 1);
    tx = gx - x0; ty = gy - y0;
    h0 = LerpNumber(GetWar3MapVertexHeight(GetWar3MapVertex(tr.world, x0, y0)),
                    GetWar3MapVertexHeight(GetWar3MapVertex(tr.world, x1, y0)), tx);
    h1 = LerpNumber(GetWar3MapVertexHeight(GetWar3MapVertex(tr.world, x0, y1)),
                    GetWar3MapVertexHeight(GetWar3MapVertex(tr.world, x1, y1)), tx);
    return LerpNumber(h0, h1, ty) + R_W3TerrainOffsetAtPoint(x, y);
}

void _W3M_DrawTerrainShadows(void) {
    if (!tr.world || !tr.texture[TEX_TERRAIN_SHADOW] || (tr.viewDef.rdflags & RDF_NOWORLDMODEL)) {
        return;
    }

    vec2_t size = GetWar3MapSize(tr.world);
    vec2_t mins = tr.world->center;
    vec2_t maxs = {
        .x = tr.world->center.x + size.x,
        .y = tr.world->center.y + size.y,
    };
    color32_t shadowColor = {0, 0, 0, 255};

    R_RenderRectSplat(&mins, &maxs, tr.texture[TEX_TERRAIN_SHADOW], R_SPLAT_SHADER(&tr.shader_shadowSplat), shadowColor);
}

/* Copy the server-authored scene fog into the terrain shader before each pass. */
static void _W3M_SetSceneFog(void) {
    tr.shader_default.state.fogEnable = tr.viewDef.fogEnable;
    tr.shader_default.state.fogColor = tr.viewDef.fogColor;
    tr.shader_default.state.fogParams = (vec2_t){ tr.viewDef.fogStart, tr.viewDef.fogEnd };
}

void _W3M_DrawWorld(void) {
    R_W3UpdateTerrainDeformations();
    if (w3_terrain_rebuild_pending) {
        R_W3EmitChangedTerrain();
        w3_terrain_rebuild_pending = false;
    }
    if (tr.viewDef.rdflags & RDF_NOWORLDMODEL)
        return;

    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_TRUE);
    R_Call(glDepthFunc, GL_LEQUAL);
    _W3M_SetSceneFog();

    {
        modelLighting_t lighting;
        R_SetDefaultLighting(&tr.shader_default,
                             R_LightingFromEnviron(&tr.viewDef.terrainLight, &lighting)
                                 ? &lighting : NULL);
    }

    FOR_EACH_LIST(maplayer_t, layer, g_groundLayers) {
        if (layer == g_groundLayers) {
            R_Call(glDisable, GL_BLEND);
        } else {
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        R_BindTexture(layer->texture, 0);
        R_ApplyShader(&tr.shader_default);
        R_DrawBuffer(layer->buffer, layer->num_vertices);
    }

    R_UpdateBlightLayer();
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_DrawBlightLayer();

    FOR_EACH_LIST(mapsegment_t, segment, g_mapSegments) {
        R_DrawTerrainSegment(segment, (1 << MAPLAYERTYPE_CLIFF));
    }
}

void _W3M_DrawAlphaSurfaces(void) {
    if (tr.viewDef.rdflags & RDF_NOWORLDMODEL)
        return;

    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_Call(glDepthMask, GL_FALSE);
    _W3M_SetSceneFog();

    texture_t const *water_frame = R_WaterFrame(R_WaterStyle(), tr.viewDef.time);
    FOR_EACH_LIST(mapsegment_t, segment, g_mapSegments) {
        FOR_EACH_LIST(maplayer_t, layer, segment->layers)
            if (layer->type == MAPLAYERTYPE_WATER) layer->texture = water_frame;
        R_DrawTerrainSegment(segment, (1 << MAPLAYERTYPE_WATER));
    }
    R_Call(glDepthMask, GL_TRUE);
}
