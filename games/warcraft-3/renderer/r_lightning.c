#include "r_lightning.h"
#include "common/stb_slk.h"
#include "games/warcraft-3/common/weather.h"

typedef struct {
    uint32_t id;
    cstring_t dir;
    cstring_t file;
    float avg_seg_len;
    float width;
    uint32_t r, g, b, a;
    float noise_scale;
    float texcoord_scale;
    float duration;
    uint32_t version;
    LPCTEXTURE texture;
} W3LIGHTNINGART;
typedef W3LIGHTNINGART *LPW3LIGHTNINGART;
typedef W3LIGHTNINGART const *LPCW3LIGHTNINGART;

static slkField_t const lightning_schema[] = {
    { "", offsetof(W3LIGHTNINGART, id), STB_SLK_FOURCC },
    { "Dir", offsetof(W3LIGHTNINGART, dir), STB_SLK_STR },
    { "file", offsetof(W3LIGHTNINGART, file), STB_SLK_STR },
    { "AvgSegLen", offsetof(W3LIGHTNINGART, avg_seg_len), STB_SLK_FLOAT },
    { "Width", offsetof(W3LIGHTNINGART, width), STB_SLK_FLOAT },
    { "R", offsetof(W3LIGHTNINGART, r), STB_SLK_INT },
    { "G", offsetof(W3LIGHTNINGART, g), STB_SLK_INT },
    { "B", offsetof(W3LIGHTNINGART, b), STB_SLK_INT },
    { "A", offsetof(W3LIGHTNINGART, a), STB_SLK_INT },
    { "NoiseScale", offsetof(W3LIGHTNINGART, noise_scale), STB_SLK_FLOAT },
    { "TexCoordScale", offsetof(W3LIGHTNINGART, texcoord_scale), STB_SLK_FLOAT },
    { "Duration", offsetof(W3LIGHTNINGART, duration), STB_SLK_FLOAT },
    { "version", offsetof(W3LIGHTNINGART, version), STB_SLK_INT },
    { NULL, 0, 0 },
};

static W3LIGHTNINGART *lightning_rows;
static uint32_t lightning_count;
static slkIndex_t lightning_index;
static struct {
    ARRAY(uint32_t, ids);
    uint32_t capacity;
} lightning_missing;

/* Remember every unresolved row for the current map so alternating effects do not warn per draw. */
static bool R_LightningMissing(uint32_t id) {
    uint32_t *ids;
    uint32_t capacity;

    FOR_LOOP(i, ARRAY_COUNT(lightning_missing.ids))
        if (lightning_missing.ids[i] == id) return true;
    if (ARRAY_COUNT(lightning_missing.ids) == lightning_missing.capacity) {
        capacity = lightning_missing.capacity ? lightning_missing.capacity * 2 : 16;
        ids = ri.MemAlloc(sizeof(*ids) * capacity);
        if (!ids) {
            fprintf(stderr, "WC3 Lightning: unable to cache missing row %08x\n", (unsigned)id);
            return false;
        }
        if (lightning_missing.ids) {
            memcpy(ids, lightning_missing.ids, sizeof(*ids) * ARRAY_COUNT(lightning_missing.ids));
            ri.MemFree(lightning_missing.ids);
        }
        lightning_missing.ids = ids;
        lightning_missing.capacity = capacity;
    }
    lightning_missing.ids[ARRAY_COUNT(lightning_missing.ids)++] = id;
    return false;
}

/* Release the per-map missing-row cache before the next asset scope is loaded. */
static void R_LightningClearMissing(void) {
    if (lightning_missing.ids) ri.MemFree(lightning_missing.ids);
    lightning_missing.ids = NULL;
    ARRAY_COUNT(lightning_missing.ids) = lightning_missing.capacity = 0;
}

/* Resolve a map-scoped LightningData table before falling back to the base archive. */
static uint32_t R_LightningLoadSlk(cstring_t filename, void **dest) {
    PATHSTR scoped;
    uint32_t count = 0;
    if (R_MapAssetCandidate(filename, scoped, sizeof(scoped)))
        count = ri.LoadSlk(scoped, lightning_schema, dest, sizeof(W3LIGHTNINGART));
    if (!count) count = ri.LoadSlk(filename, lightning_schema, dest, sizeof(W3LIGHTNINGART));
    return count;
}

/* Cache the authored ribbon texture; malformed rows are logged once and skipped by the caller. */
static LPCTEXTURE R_LightningTexture(W3LIGHTNINGART *art) {
    PATHSTR path;
    if (!art) return NULL;
    if (art->texture) return art->texture;
    if (!art->file || !*art->file) {
        if (!R_LightningMissing(art->id))
            fprintf(stderr, "WC3 Lightning: row %08x has no texture file\n", (unsigned)art->id);
        return NULL;
    }
    if (art->dir && *art->dir) snprintf(path, sizeof(path), "%s\\%s", art->dir, art->file);
    else strlcpy(path, art->file, sizeof(path));
    art->texture = R_LoadTexture(path);
    R_SetTextureWrap(art->texture, true, false);
    return art->texture;
}

/* Reset map-independent renderer state before the first map is registered. */
void R_LightningInit(void) {
    lightning_rows = NULL;
    lightning_count = 0;
    memset(&lightning_index, 0, sizeof(lightning_index));
    R_LightningClearMissing();
}

/* Release the table and texture references owned by the current map. */
void R_LightningShutdown(void) {
    FS_SLKFreeIndex(&lightning_index);
    FS_SLKFreeRows(lightning_schema, lightning_rows, lightning_count, sizeof(W3LIGHTNINGART));
    lightning_rows = NULL;
    lightning_count = 0;
    R_LightningClearMissing();
}

/* Rebuild the authored LightningData lookup whenever the map asset scope changes. */
void R_LightningRegisterMap(void) {
    FS_SLKFreeIndex(&lightning_index);
    FS_SLKFreeRows(lightning_schema, lightning_rows, lightning_count, sizeof(W3LIGHTNINGART));
    lightning_rows = NULL;
    R_LightningClearMissing();
    lightning_count = R_LightningLoadSlk("Splats\\LightningData.slk", (void **)&lightning_rows);
    if (!lightning_count) fprintf(stderr, "WC3 Lightning: Splats\\LightningData.slk has no rows\n");
    FS_SLKBuildIndex(&lightning_index, lightning_rows, lightning_count, sizeof(W3LIGHTNINGART));
}

/* Apply the authored channel multiplier without overflowing the byte product. */
static uint8_t R_LightningMulByte(uint32_t authored, uint8_t tint) {
    uint32_t value = MIN(authored, 255u) * (uint32_t)tint;
    return (uint8_t)((value + 127u) / 255u);
}

#define WC3_LIGHTNING_MAX_SEGMENTS 64 // points; bounds procedural crackle work for one lightning bolt

/* Produce a deterministic integer hash for stable crackle phase selection. */
static uint32_t R_LightningHash(uint32_t value) {
    value ^= value >> 16; value *= 0x7feb352d; value ^= value >> 15;
    value *= 0x846ca68b; return value ^ (value >> 16);
}

/* Convert a hash to a symmetric unit interval for lateral and longitudinal offsets. */
static float R_LightningHashSigned(uint32_t seed) {
    uint32_t hash = R_LightningHash(seed);
    return ((float)(hash & 0x00ffffffu) / 8388607.5f) - 1.0f;
}

/* Build the camera-independent polyline that the shared ribbon renderer expands. */
static uint32_t R_LightningBuildPoints(W3LIGHTNINGART const *art,
                                    LPCLIGHTNINGEFFECT state,
                                    VECTOR3 *points, uint32_t point_capacity) {
    VECTOR3 delta, direction, reference, side;
    float distance, average, noise_ratio, lateral_scale;
    uint32_t segments, crackle_frame;

    if (!art || !state || !points || point_capacity < 2) return 0;
    delta = Vector3_sub(&state->target, &state->source);
    distance = Vector3_len(&delta);
    if (distance <= 0.001f) return 0;
    direction = Vector3_scale(&delta, 1.0f / distance);
    reference = fabsf(direction.z) < 0.9f ? (VECTOR3){0, 0, 1} : (VECTOR3){0, 1, 0};
    side = Vector3_cross(&direction, &reference); Vector3_normalize(&side);
    average = art->avg_seg_len > 0.0f ? art->avg_seg_len : distance;
    segments = (uint32_t)ceilf(distance / average);
    segments = MAX(1u, MIN(segments, MIN(point_capacity - 1, WC3_LIGHTNING_MAX_SEGMENTS)));
    /* Stock Chain Lightning uses NoiseScale 0.05. Keep that authored value as
     * the baseline and scale custom LightningData rows proportionally. */
    noise_ratio = MAX(0.0f, art->noise_scale) / 0.05f;
    lateral_scale = MAX(0.0f, art->width) * 1.05f * noise_ratio;
    /* The browser reference and observed WC3 presentation crackle between
     * discrete shapes instead of continuously waving like a rope. */
    crackle_frame = tr.viewDef.time / 55u;
    FOR_LOOP(i, segments + 1) {
        float fraction = (float)i / (float)segments;
        points[i] = Vector3_lerp(&state->source, &state->target, fraction);
        if (i && i < segments && lateral_scale > 0.0f) {
            uint32_t seed = state->handle ^ state->effect_id ^ (i * 0x9e3779b9u) ^
                (crackle_frame * 0x85ebca6bu);
            float edge = sinf(3.14159265359f * fraction);
            float lateral = R_LightningHashSigned(seed) * lateral_scale * edge;
            float longitudinal = R_LightningHashSigned(seed ^ 0x68bc21ebu) * average * 0.12f;
            points[i] = Vector3_mad(&points[i], lateral, &side);
            points[i] = Vector3_mad(&points[i], longitudinal, &direction);
        }
    }
    return segments + 1;
}

/* Fade finite bolts while leaving persistent JASS lightning fully opaque. */
static float R_LightningOpacity(W3LIGHTNINGART const *art,
                                LPCLIGHTNINGEFFECT state) {
    uint32_t lifetime, authored;
    float elapsed, duration, fade_start;

    if (!state->end_time) return 1.0f; /* JASS AddLightning handles live until DestroyLightning. */
    if (tr.viewDef.time >= state->end_time) return 0.0f;
    lifetime = state->end_time - state->start_time;
    authored = art->duration > 0.0f ? (uint32_t)(art->duration * 1000.0f) : 0;
    if (authored && authored < lifetime) lifetime = authored;
    elapsed = tr.viewDef.time >= state->start_time ?
        (float)(tr.viewDef.time - state->start_time) / 1000.0f : 0.0f;
    duration = (float)lifetime / 1000.0f;
    if (duration <= 0.0f || elapsed >= duration) return 0.0f;
    fade_start = duration * 0.72f;
    return elapsed <= fade_start ? 1.0f : 1.0f - (elapsed - fade_start) / (duration - fade_start);
}

/* Draw the current endpoint snapshot through the shared ribbon particle pass. */
void R_LightningDraw(void) {
    FOR_LOOP(i, tr.viewDef.num_lightning_effects) {
        LPCLIGHTNINGEFFECT state = tr.viewDef.lightning_effects + i;
        W3LIGHTNINGART *art = FS_SLKLookup(&lightning_index, state->effect_id);
        VECTOR3 points[WC3_LIGHTNING_MAX_SEGMENTS + 1];
        COLOR32 color;
        LPCTEXTURE texture;
        float opacity, average, texture_scale, elapsed;
        uint32_t point_count;
        if (!art) {
            if (!R_LightningMissing(state->effect_id))
                fprintf(stderr, "WC3 Lightning: no LightningData row for %08x\n", (unsigned)state->effect_id);
            continue;
        }
        if (!art->file || !*art->file) {
            R_LightningTexture(art);
            continue;
        }
        texture = R_LightningTexture(art);
        opacity = R_LightningOpacity(art, state);
        if (opacity <= 0.0f) continue;
        color = MAKE(COLOR32,
            R_LightningMulByte(art->r, state->color.r),
            R_LightningMulByte(art->g, state->color.g),
            R_LightningMulByte(art->b, state->color.b),
            (uint8_t)(R_LightningMulByte(art->a, state->color.a) * opacity));
        point_count = R_LightningBuildPoints(art, state, points, sizeof(points) / sizeof(points[0]));
        if (point_count < 2) continue;
        average = art->avg_seg_len > 0.0f ? art->avg_seg_len : 1.0f;
        texture_scale = art->texcoord_scale > 0.0f ? art->texcoord_scale / average : 0.0f;
        elapsed = state->start_time && tr.viewDef.time >= state->start_time ?
            (float)(tr.viewDef.time - state->start_time) / 1000.0f : 0.0f;
        R_DrawRibbon(&(ribbonDraw_t){
            .texture = texture,
            .points = points,
            .point_count = point_count,
            .width = art->width,
            .texcoord_scale = texture_scale,
            .texcoord_phase = -elapsed * art->texcoord_scale * 3.2f,
            .color = color,
            .blend_mode = BLEND_MODE_ADD,
            .depth_test = false,
        });
    }
}
