#include "r_weather.h"
#include "common/stb_slk.h"
#include "games/warcraft-3/common/weather.h"

#define MAX_RENDER_WEATHER_EFFECTS 256
#define WEATHER_SCALE_QUANT 16.0f
#define WEATHER_MIN_EMIT_RADIUS 1024.0f
#define WEATHER_DEG2RAD 0.01745329251994329577f

typedef struct {
    uint32_t id;
    cstring_t name;
    cstring_t texDir;
    cstring_t texFile;
    uint32_t alphaMode;
    bool useFog;
    float height;
    float angleX;
    float angleY;
    float emissionRate;
    float lifespan;
    uint32_t particles;
    float velocity;
    float acceleration;
    float variation;
    uint32_t rows;
    uint32_t columns;
    bool head;
    bool tail;
    float tailLength;
    float latitude;
    float longitude;
    float midTime;
    uint32_t redStart, greenStart, blueStart;
    uint32_t redMid, greenMid, blueMid;
    uint32_t redEnd, greenEnd, blueEnd;
    uint32_t alphaStart, alphaMid, alphaEnd;
    float scaleStart, scaleMid, scaleEnd;
    uint32_t headUVStart, headUVMid, headUVEnd;
    uint32_t tailUVStart, tailUVMid, tailUVEnd;
    cstring_t ambientSound;
    uint32_t version;
} w3WeatherArt_t;

typedef struct {
    bool inuse;
    bool enabled;
    uint32_t handle;
    uint32_t effect_id;
    box2_t bounds;
    w3WeatherArt_t const *art;
    texture_t const *texture;
    float emission_accum;
    uint32_t seen;
} renderWeatherEffect_t;

static slkField_t const weather_schema[] = {
    { "", offsetof(w3WeatherArt_t, id), STB_SLK_FOURCC },
    { "name", offsetof(w3WeatherArt_t, name), STB_SLK_STR },
    { "texDir", offsetof(w3WeatherArt_t, texDir), STB_SLK_STR },
    { "texFile", offsetof(w3WeatherArt_t, texFile), STB_SLK_STR },
    { "alphaMode", offsetof(w3WeatherArt_t, alphaMode), STB_SLK_INT },
    { "useFog", offsetof(w3WeatherArt_t, useFog), STB_SLK_BOOL },
    { "height", offsetof(w3WeatherArt_t, height), STB_SLK_FLOAT },
    { "angx", offsetof(w3WeatherArt_t, angleX), STB_SLK_FLOAT },
    { "angy", offsetof(w3WeatherArt_t, angleY), STB_SLK_FLOAT },
    { "emrate", offsetof(w3WeatherArt_t, emissionRate), STB_SLK_FLOAT },
    { "lifespan", offsetof(w3WeatherArt_t, lifespan), STB_SLK_FLOAT },
    { "particles", offsetof(w3WeatherArt_t, particles), STB_SLK_INT },
    { "veloc", offsetof(w3WeatherArt_t, velocity), STB_SLK_FLOAT },
    { "accel", offsetof(w3WeatherArt_t, acceleration), STB_SLK_FLOAT },
    { "var", offsetof(w3WeatherArt_t, variation), STB_SLK_FLOAT },
    { "texr", offsetof(w3WeatherArt_t, rows), STB_SLK_INT },
    { "texc", offsetof(w3WeatherArt_t, columns), STB_SLK_INT },
    { "head", offsetof(w3WeatherArt_t, head), STB_SLK_BOOL },
    { "tail", offsetof(w3WeatherArt_t, tail), STB_SLK_BOOL },
    { "taillen", offsetof(w3WeatherArt_t, tailLength), STB_SLK_FLOAT },
    { "lati", offsetof(w3WeatherArt_t, latitude), STB_SLK_FLOAT },
    { "long", offsetof(w3WeatherArt_t, longitude), STB_SLK_FLOAT },
    { "midTime", offsetof(w3WeatherArt_t, midTime), STB_SLK_FLOAT },
    { "redStart", offsetof(w3WeatherArt_t, redStart), STB_SLK_INT },
    { "greenStart", offsetof(w3WeatherArt_t, greenStart), STB_SLK_INT },
    { "blueStart", offsetof(w3WeatherArt_t, blueStart), STB_SLK_INT },
    { "redMid", offsetof(w3WeatherArt_t, redMid), STB_SLK_INT },
    { "greenMid", offsetof(w3WeatherArt_t, greenMid), STB_SLK_INT },
    { "blueMid", offsetof(w3WeatherArt_t, blueMid), STB_SLK_INT },
    { "redEnd", offsetof(w3WeatherArt_t, redEnd), STB_SLK_INT },
    { "greenEnd", offsetof(w3WeatherArt_t, greenEnd), STB_SLK_INT },
    { "blueEnd", offsetof(w3WeatherArt_t, blueEnd), STB_SLK_INT },
    { "alphaStart", offsetof(w3WeatherArt_t, alphaStart), STB_SLK_INT },
    { "alphaMid", offsetof(w3WeatherArt_t, alphaMid), STB_SLK_INT },
    { "alphaEnd", offsetof(w3WeatherArt_t, alphaEnd), STB_SLK_INT },
    { "scaleStart", offsetof(w3WeatherArt_t, scaleStart), STB_SLK_FLOAT },
    { "scaleMid", offsetof(w3WeatherArt_t, scaleMid), STB_SLK_FLOAT },
    { "scaleEnd", offsetof(w3WeatherArt_t, scaleEnd), STB_SLK_FLOAT },
    { "hUVStart", offsetof(w3WeatherArt_t, headUVStart), STB_SLK_INT },
    { "hUVMid", offsetof(w3WeatherArt_t, headUVMid), STB_SLK_INT },
    { "hUVEnd", offsetof(w3WeatherArt_t, headUVEnd), STB_SLK_INT },
    { "tUVStart", offsetof(w3WeatherArt_t, tailUVStart), STB_SLK_INT },
    { "tUVMid", offsetof(w3WeatherArt_t, tailUVMid), STB_SLK_INT },
    { "tUVEnd", offsetof(w3WeatherArt_t, tailUVEnd), STB_SLK_INT },
    { "AmbientSound", offsetof(w3WeatherArt_t, ambientSound), STB_SLK_STR },
    { "version", offsetof(w3WeatherArt_t, version), STB_SLK_INT },
    { NULL, 0, 0 },
};

static w3WeatherArt_t *weather_rows;
static uint32_t weather_count;
static slkIndex_t weather_index;
static renderWeatherEffect_t weather_effects[MAX_RENDER_WEATHER_EFFECTS];
static uint32_t weather_rng = 0x7f4a7c15u;
static uint32_t weather_sync;

static float R_WeatherRandom01(void) {
    weather_rng ^= weather_rng << 13;
    weather_rng ^= weather_rng >> 17;
    weather_rng ^= weather_rng << 5;
    return (float)(weather_rng & 0x00ffffffu) / 16777216.0f;
}

static renderWeatherEffect_t *R_WeatherFind(uint32_t handle) {
    FOR_LOOP(i, MAX_RENDER_WEATHER_EFFECTS)
        if (weather_effects[i].inuse && weather_effects[i].handle == handle) return weather_effects + i;
    return NULL;
}

static texture_t const *R_WeatherTexture(w3WeatherArt_t const *art) {
    PATHSTR path;

    if (!art || !art->texFile || !*art->texFile) return NULL;
    if (art->texDir && *art->texDir)
        snprintf(path, sizeof(path), "%s\\%s.blp", art->texDir, art->texFile);
    else
        snprintf(path, sizeof(path), "%s.blp", art->texFile);
    return R_LoadTexture(path);
}

static void R_WeatherResolve(renderWeatherEffect_t *effect) {
    if (!effect || !effect->inuse) return;
    effect->art = FS_SLKLookup(&weather_index, effect->effect_id);
    if (!effect->art) {
        effect->texture = NULL;
        return;
    }
    effect->texture = R_WeatherTexture(effect->art);
}

static uint32_t R_WeatherLoadSlk(cstring_t filename, void **dest) {
    PATHSTR scoped;
    uint32_t count = 0;

    if (R_MapAssetCandidate(filename, scoped, sizeof(scoped)))
        count = ri.LoadSlk(scoped, weather_schema, dest, sizeof(w3WeatherArt_t));
    if (!count) count = ri.LoadSlk(filename, weather_schema, dest, sizeof(w3WeatherArt_t));
    return count;
}

void R_WeatherInit(void) {
    memset(weather_effects, 0, sizeof(weather_effects));
    weather_sync = 0;
}

void R_WeatherShutdown(void) {
    memset(weather_effects, 0, sizeof(weather_effects));
    weather_sync = 0;
    FS_SLKFreeIndex(&weather_index);
    FS_SLKFreeRows(weather_schema, weather_rows, weather_count, sizeof(w3WeatherArt_t));
    weather_rows = NULL;
    weather_count = 0;
}

void R_WeatherRegisterMap(void) {
    FS_SLKFreeIndex(&weather_index);
    FS_SLKFreeRows(weather_schema, weather_rows, weather_count, sizeof(w3WeatherArt_t));
    weather_rows = NULL;
    weather_count = R_WeatherLoadSlk("TerrainArt\\Weather.slk", (void **)&weather_rows);
    FS_SLKBuildIndex(&weather_index, weather_rows, weather_count, sizeof(w3WeatherArt_t));
    memset(weather_effects, 0, sizeof(weather_effects));
    weather_rng = 0x7f4a7c15u;
    weather_sync = 0;
}

/* Reconcile renderer-owned particle accumulators with the latest client view. */
static void R_WeatherSync(void) {
    uint32_t sync = ++weather_sync;

    FOR_LOOP(i, tr.viewDef.num_weather_effects) {
        wc3WeatherEffect_t const *state = tr.viewDef.weather_effects + i;
        renderWeatherEffect_t *effect = R_WeatherFind(state->handle);
        if (!effect) FOR_LOOP(j, MAX_RENDER_WEATHER_EFFECTS) if (!weather_effects[j].inuse) {
            effect = weather_effects + j;
            memset(effect, 0, sizeof(*effect));
            effect->inuse = true;
            break;
        }
        if (!effect) continue;
        effect->seen = sync;
        if (effect->effect_id != state->effect_id) {
            effect->effect_id = state->effect_id;
            R_WeatherResolve(effect);
        }
        effect->enabled = !!state->enabled;
        effect->handle = state->handle;
        effect->bounds = state->bounds;
    }
    FOR_LOOP(i, MAX_RENDER_WEATHER_EFFECTS)
        if (weather_effects[i].inuse && weather_effects[i].seen != sync) memset(weather_effects + i, 0, sizeof(*weather_effects));
}

static box2_t R_WeatherEmissionBounds(void) {
    float radius = MAX(WEATHER_MIN_EMIT_RADIUS, tr.viewDef.camerastate[0].distance * 1.25f);
    vec3_t center = tr.viewDef.camerastate[0].origin;
    return (box2_t){
        .min = { center.x - radius, center.y - radius },
        .max = { center.x + radius, center.y + radius },
    };
}

static bool R_WeatherIntersect(box2_t const *a, box2_t const *b, box2_t *out) {
    if (!a || !b || !out) return false;
    out->min.x = MAX(a->min.x, b->min.x);
    out->min.y = MAX(a->min.y, b->min.y);
    out->max.x = MIN(a->max.x, b->max.x);
    out->max.y = MIN(a->max.y, b->max.y);
    return out->min.x < out->max.x && out->min.y < out->max.y;
}

static uint8_t R_WeatherByte(uint32_t value) {
    return (uint8_t)MIN(value, 255u);
}

static uint8_t R_WeatherScale(float value) {
    int32_t encoded = (int32_t)lroundf(MAX(value, 0.0f) * WEATHER_SCALE_QUANT);
    return (uint8_t)MIN(MAX(encoded, 0), 255);
}

static void R_WeatherSpawn(renderWeatherEffect_t *effect, box2_t const *area) {
    w3WeatherArt_t const *art = effect->art;
    cparticle_t *p;
    float ax, ay, speed;
    vec3_t direction;

    if (!art || !area || art->lifespan <= 0.0f) return;
    p = R_SpawnParticle();
    if (!p) return;
    memset(p->color, 0, sizeof(p->color));
    p->texture = effect->texture;
    p->org.x = area->min.x + (area->max.x - area->min.x) * R_WeatherRandom01();
    p->org.y = area->min.y + (area->max.y - area->min.y) * R_WeatherRandom01();
    p->org.z = R_GetHeightAtPoint(p->org.x, p->org.y) + art->height;

    ax = art->angleX * WEATHER_DEG2RAD;
    ay = art->angleY * WEATHER_DEG2RAD;
    direction = (vec3_t){ sinf(ay) * cosf(ax), -sinf(ax), cosf(ay) * cosf(ax) };
    speed = art->velocity;
    p->vel = Vector3_scale(&direction, speed);
    p->accel = Vector3_scale(&direction, art->acceleration);
    p->tail = art->tail ? Vector3_scale(&p->vel, art->tailLength) : (vec3_t){0};

    p->color[0] = (color32_t){ R_WeatherByte(art->redStart), R_WeatherByte(art->greenStart), R_WeatherByte(art->blueStart), R_WeatherByte(art->alphaStart) };
    p->color[1] = (color32_t){ R_WeatherByte(art->redMid), R_WeatherByte(art->greenMid), R_WeatherByte(art->blueMid), R_WeatherByte(art->alphaMid) };
    p->color[2] = (color32_t){ R_WeatherByte(art->redEnd), R_WeatherByte(art->greenEnd), R_WeatherByte(art->blueEnd), R_WeatherByte(art->alphaEnd) };
    p->size[0] = R_WeatherScale(art->scaleStart);
    p->size[1] = R_WeatherScale(art->scaleMid);
    p->size[2] = R_WeatherScale(art->scaleEnd);
    p->size_value_scale = 1.0f / WEATHER_SCALE_QUANT;
    p->size_time_scale = 1.0f / art->lifespan;
    p->midtime = (uint8_t)MIN(MAX((int32_t)lroundf(art->midTime * 255.0f), 1), 254);
    p->rows = (uint8_t)MIN(MAX(art->rows, 1u), 255u);
    p->columns = (uint8_t)MIN(MAX(art->columns, 1u), 255u);
    /* Weather alphaMode is not the shared particle enum; until every legacy
     * numeric mode is verified, regular alpha blending is the safe baseline. */
    p->blend_mode = BLEND_MODE_BLEND;
    p->time = 0.0f;
    p->lifespan = art->lifespan;
}

void R_WeatherEmit(void) {
    box2_t visible;
    uint32_t delta_ms;

    if (tr.viewDef.rdflags & RDF_NOWORLDMODEL) return;
    R_WeatherSync();
    delta_ms = tr.viewDef.deltaTime;
    if (!delta_ms) return;
    visible = R_WeatherEmissionBounds();

    FOR_LOOP(i, MAX_RENDER_WEATHER_EFFECTS) {
        renderWeatherEffect_t *effect = weather_effects + i;
        w3WeatherArt_t const *art = effect->art;
        box2_t area;
        uint32_t emit_count;

        if (!effect->inuse || !effect->enabled || !art || !effect->texture ||
            art->emissionRate <= 0.0f || art->lifespan <= 0.0f ||
            !R_WeatherIntersect(&visible, &effect->bounds, &area)) continue;
        effect->emission_accum += art->emissionRate * (float)delta_ms / 1000.0f;
        emit_count = (uint32_t)effect->emission_accum;
        effect->emission_accum -= (float)emit_count;
        while (emit_count--) R_WeatherSpawn(effect, &area);
    }
}
