#ifndef SC2_MAP_H
#define SC2_MAP_H

#include "common/common.h"
#include "sc2_coords.h"
#include <stdio.h>
#include <math.h>

static inline float SC2_LerpDegrees(float a, float b, float k) {
    float delta = fmodf(b - a + 540.0f, 360.0f) - 180.0f;
    return a + delta * k;
}

#define SC2_MAX_MAP_OBJECTS 4096 // objects; accommodates object-heavy campaign maps such as TRaynor01
#define SC2_CELL_SIZE          1.0f
#define SC2_BROAD_HEIGHT_RADIUS  8.0f // world units; half-width of the air/camera terrain filter footprint
#define SC2_MAX_TERRAIN_TEXTURES 16
#define SC2_MAX_CLIFF_SETS     8
#define SC2_MAX_CLIFF_CELLS    16384
#define SC2_MAX_HARD_TILES     16384 // placements; bounds corrupt HRDT counts before allocation
#define SC2_MAPINFO_DATA_SIZE  512
#define SC2_OBJECT_HEIGHT_ABSOLUTE 0x00000001
#define SC2_OBJECT_HEIGHT_OFFSET   0x00000002
#define SC2_OBJECT_FORCE_PLACEMENT 0x00000004
#define SC2_UNIT_FLAG_MOVABLE      0x00000001
#define SC2_UNIT_FLAG_WORKER       0x00000002
#define SC2_UNIT_FLAG_RESOURCE     0x00000004
#define SC2_UNIT_FLAG_STRUCTURE    0x00000008
#define SC2_LIGHT_KEY              0
#define SC2_LIGHT_FILL             1
#define SC2_LIGHT_BACK             2
#define SC2_MAX_DIRECTIONAL_LIGHTS 3
#define SC2_DIFFUSE_LIGHTS         1 // lights; the shadow-casting key alone drives coherent Lambert diffuse

typedef enum {
    SC2_OBJECT_UNIT,
    SC2_OBJECT_DOODAD,
    SC2_OBJECT_POINT,
    SC2_OBJECT_CAMERA,
} sc2ObjectType_t;

typedef struct {
    vector3_t         target;
    float           distance;
    float           pitch;
    float           yaw;
    float           fov;
    float           znear;
    float           zfar;
    float           height_offset;
} sc2MapCamera_t;

typedef struct {
    sc2ObjectType_t type;
    uint32_t           id;
    char            name[64];
    char            model[256];
    char            footprint[64];
    char            mover[64];
    char            type_name[64];
    char            anim_props[64];
    char            sound[256];
    char            attach_id[64];
    char            object_type[64];
    vector3_t         position;
    float           angle;
    float           scale;
    float           radius;
    float           footprint_width;
    float           footprint_height;
    float           footprint_radius;
    float           move_height;
    float           pathing_soft_radius;
    float           pathing_hard_radius;
    uint32_t           variation;
    uint32_t           player;
    uint32_t           section;
    uint32_t           resources;
    uint32_t           object_id;
    uint32_t           flags;
    uint32_t           unit_flags;
    color32_t         color;
    color32_t         tint_color;
    sc2MapCamera_t  camera;
} sc2MapObject_t;

static inline float sc2_unit_world_height(float terrain, float height, bool flying) {
    return terrain + (flying ? height : 0.0f);
}

typedef struct {
    bool            enabled;
    vector3_t         color;
    float           color_multiplier;
    float           spec_color_multiplier;
    vector3_t         direction;
} sc2DirectionalLight_t;

typedef struct {
    bool            enabled;
    uint32_t           colorize;
    char            id[64];
    vector3_t         ambient_color;
    float           colorization_blend;
    sc2DirectionalLight_t directional[SC2_MAX_DIRECTIONAL_LIGHTS];
} sc2MapLighting_t;

/* Colorized SC2 lights use the authored blend as ambient strength; ordinary lights use their ambient directly. */
static vector3_t sc2_light_ambient(sc2MapLighting_t const *light) {
    float scale = light && light->colorize ? light->colorization_blend : 1.0f;
    return light ? Vector3_scale(&light->ambient_color, scale) : (vector3_t){ 0.35f, 0.35f, 0.40f };
}

typedef struct {
    char           diffuse[256];
    char           normal[256];
} sc2TerrainTexture_t;

typedef struct {
    char           name[64];
    char           mesh[64];
} sc2CliffSet_t;

typedef struct {
    uint32_t          index;
    uint32_t          flags;
    uint32_t          cliff_set;
    uint32_t          variant;
} sc2CliffCell_t;

/* t3Terrain rampList boxes use orthonormal up/right axes and half extents in height-grid units. */
typedef struct sc2RampBox_s {
    vector2_t up, right, center;
    float width, height;
} sc2RampBox_t;

typedef struct sc2Ramp_s {
    uint32_t dir, hi, lo, cid;
    sc2RampBox_t edge[4], base, mid;
    uint32_t variant[4];
} sc2Ramp_t;

typedef struct {
    ARRAY(sc2Ramp_t, ramps);
    char           tile_set[64];
    uint32_t          num_terrain_textures;
    sc2TerrainTexture_t terrain_textures[SC2_MAX_TERRAIN_TEXTURES];
    uint32_t          num_cliff_sets;
    sc2CliffSet_t cliff_sets[SC2_MAX_CLIFF_SETS];
    uint32_t          num_cliff_cells;
    sc2CliffCell_t cliff_cells[SC2_MAX_CLIFF_CELLS];
    float          height_quantize_bias;
    float          height_quantize_scale;
    float          standard_height;
    bool           fog_enabled;
    float          fog_density;
    float          fog_falloff;
    float          fog_start_height;
    color32_t        fog_color;
} sc2MapTerrain_t;

typedef struct {
    uint16_t         adjustment;
    uint16_t         height;
    uint16_t         extra;
} sc2MapHeightSample_t;

typedef struct {
    uint32_t          fourcc;
    uint32_t          version;
    uint32_t          width;
    uint32_t          height;
    uint8_t           padding[16];
    sc2MapHeightSample_t data[];
} sc2MapHeightMap_t;

typedef struct {
    int16_t          height;
    uint16_t         mask;
} sc2MapSyncHeightSample_t;

typedef struct {
    uint32_t          fourcc;
    uint32_t          version;
    uint32_t          width;
    uint32_t          height;
    uint8_t           padding[48];
    sc2MapSyncHeightSample_t data[];
} sc2MapSyncHeightMap_t;

typedef struct {
    uint32_t          fourcc;
    uint32_t          version;
    uint32_t          zero[4];
    uint32_t          width;
    uint32_t          height;
    uint8_t           data[];
} sc2MapCellFlags_t;

typedef struct {
    uint32_t          fourcc;
    uint32_t          version;
    uint32_t          width;
    uint32_t          height;
    uint32_t          zero[4];
    uint16_t         data[];
} sc2MapSyncCliffLevel_t;

typedef struct {
    uint32_t          fourcc;
    uint32_t          version;
    uint32_t          unknown;
    uint32_t          width;
    uint32_t          height;
    uint32_t          zero[11];
    uint8_t           data[];
} sc2MapTextureMasks_t;

typedef struct {
    char            tile[64];
    char            model[256];
    vector3_t         position;
    vector3_t         normal;
    vector3_t         start;
    vector3_t         end;
    vector2_t         scale;
    uint16_t          flags;
} sc2MapHardTile_t;

typedef struct {
    uint32_t          fourcc;
    uint32_t          version;
    uint32_t          unknown0;
    uint32_t          unknown1;
    uint32_t          width;
    uint32_t          height;
    uint8_t           data[SC2_MAPINFO_DATA_SIZE];
} sc2MapInfo_t;

typedef struct {
    uint32_t          units;
    uint32_t          actors;
    uint32_t          models;
    uint32_t          footprints;
    uint32_t          unresolved_models;
} sc2CatalogStats_t;

typedef struct {
    char           map_name[128];
    vector2_t        origin;
    float          cell_size;
    uint32_t          num_objects;
    sc2MapObject_t objects[SC2_MAX_MAP_OBJECTS];
    sc2MapTerrain_t t3Terrain;
    sc2MapTextureMasks_t *t3TextureMasks;
    uint32_t          t3TextureMasksSize;
    ARRAY(sc2MapHardTile_t, hard_tiles);
    sc2MapCellFlags_t *t3CellFlags;
    sc2MapSyncCliffLevel_t *t3SyncCliffLevel;
    sc2MapInfo_t   MapInfo;
    sc2MapHeightMap_t *t3HeightMap;
    sc2MapSyncHeightMap_t *t3SyncHeightMap;
    sc2MapLighting_t lighting;
    sc2CatalogStats_t catalog;
} sc2Map_t;

typedef struct {
    uint32_t          x0;
    uint32_t          y0;
    uint32_t          x1;
    uint32_t          y1;
    float          tx;
    float          ty;
} sc2MapHeightPoint_t;

static inline uint32_t sc2_map_cell_width(sc2Map_t const *map) {
    return map ? map->MapInfo.width : 0;
}

static inline uint32_t sc2_map_cell_height(sc2Map_t const *map) {
    return map ? map->MapInfo.height : 0;
}

static inline float sc2_map_height_scale(sc2Map_t const *map) {
    return map && map->t3Terrain.height_quantize_scale ? map->t3Terrain.height_quantize_scale : 1.0f;
}

static inline float sc2_map_height_offset(sc2Map_t const *map) {
    return map ? map->t3Terrain.height_quantize_bias + map->t3Terrain.standard_height + 1.0f : 1.0f;
}

static inline float sc2_map_height_at_grid(sc2Map_t const *map, uint32_t x, uint32_t y) {
    sc2MapHeightSample_t const *sample;

    if (!map || !map->t3HeightMap || !map->t3HeightMap->width || !map->t3HeightMap->height)
        return 0.0f;
    x = MIN(map->t3HeightMap->width - 1, x);
    y = MIN(map->t3HeightMap->height - 1, y);
    sample = &map->t3HeightMap->data[x + y * map->t3HeightMap->width];
    return ((float)sample->height + (float)sample->adjustment) * sc2_map_height_scale(map) - sc2_map_height_offset(map);
}

static inline float sc2_map_height_adjust_at_grid(sc2Map_t const *map, uint32_t x, uint32_t y) {
    sc2MapHeightSample_t const *sample;

    if (!map || !map->t3HeightMap || !map->t3HeightMap->width || !map->t3HeightMap->height)
        return 0.0f;
    x = MIN(map->t3HeightMap->width - 1, x);
    y = MIN(map->t3HeightMap->height - 1, y);
    sample = &map->t3HeightMap->data[x + y * map->t3HeightMap->width];
    return (float)sample->adjustment * sc2_map_height_scale(map);
}

static inline bool sc2_map_height_point(sc2Map_t const *map, float x, float y, sc2MapHeightPoint_t *point) {
    float fx, fy;

    if (!point || !map || !map->t3HeightMap || !map->t3HeightMap->width || !map->t3HeightMap->height)
        return false;
    memset(point, 0, sizeof(*point));
    fx = (x - map->origin.x) / (map->cell_size ? map->cell_size : 1.0f);
    fy = (y - map->origin.y) / (map->cell_size ? map->cell_size : 1.0f);
    fx = MIN(MAX(fx, 0.0f), (float)(sc2_map_cell_width(map) ? sc2_map_cell_width(map) : map->t3HeightMap->width - 1));
    fy = MIN(MAX(fy, 0.0f), (float)(sc2_map_cell_height(map) ? sc2_map_cell_height(map) : map->t3HeightMap->height - 1));
    point->x0 = (uint32_t)floorf(fx);
    point->y0 = (uint32_t)floorf(fy);
    point->x1 = point->x0 + 1;
    point->y1 = point->y0 + 1;
    point->tx = fx - (float)point->x0;
    point->ty = fy - (float)point->y0;
    return true;
}

static inline float sc2_map_height_lerp(float h00, float h10, float h01, float h11, float tx, float ty) {
    float h0 = h00 + (h10 - h00) * tx;
    float h1 = h01 + (h11 - h01) * tx;
    return h0 + (h1 - h0) * ty;
}

static inline float sc2_map_height_at_point(sc2Map_t const *map, float x, float y) {
    sc2MapHeightPoint_t p;

    if (!sc2_map_height_point(map, x, y, &p))
        return 0.0f;
    return sc2_map_height_lerp(sc2_map_height_at_grid(map, p.x0, p.y0),
                               sc2_map_height_at_grid(map, p.x1, p.y0),
                               sc2_map_height_at_grid(map, p.x0, p.y1),
                               sc2_map_height_at_grid(map, p.x1, p.y1),
                               p.tx,
                               p.ty);
}

/* Air movers and cameras follow broad terrain elevation without dipping into narrow depressions. */
static inline float sc2_map_broad_height_at_point(sc2Map_t const *map, float x, float y) {
    float sum = 0.0f, step = SC2_BROAD_HEIGHT_RADIUS * 2.0f / (BZ_BROAD_HEIGHT_SAMPLES - 1);
    int ix, iy;

    for (iy = 0; iy < BZ_BROAD_HEIGHT_SAMPLES; iy++)
        for (ix = 0; ix < BZ_BROAD_HEIGHT_SAMPLES; ix++)
            sum += sc2_map_height_at_point(map, x - SC2_BROAD_HEIGHT_RADIUS + ix * step,
                                          y - SC2_BROAD_HEIGHT_RADIUS + iy * step);
    return sum / (BZ_BROAD_HEIGHT_SAMPLES * BZ_BROAD_HEIGHT_SAMPLES);
}

static inline float sc2_map_height_adjust_at_point(sc2Map_t const *map, float x, float y) {
    sc2MapHeightPoint_t p;

    if (!sc2_map_height_point(map, x, y, &p))
        return 0.0f;
    return sc2_map_height_lerp(sc2_map_height_adjust_at_grid(map, p.x0, p.y0),
                               sc2_map_height_adjust_at_grid(map, p.x1, p.y0),
                               sc2_map_height_adjust_at_grid(map, p.x0, p.y1),
                               sc2_map_height_adjust_at_grid(map, p.x1, p.y1),
                               p.tx,
                               p.ty);
}

typedef struct {
    handle_t (*read_file)(cstring_t filename, uint32_t * size);
    void   (*free_file)(handle_t file);
    handle_t (*mem_alloc)(long size);
    void   (*mem_free)(handle_t mem);
    cstring_t (*cvar_string)(cstring_t name, cstring_t fallback);
} sc2MapHost_t;

void          SC2_MapSetHost(sc2MapHost_t const *host);
bool          SC2_MapLoad(cstring_t mapFilename);
void          SC2_MapShutdown(void);
sc2Map_t     *SC2_MapCurrent(void);
cstring_t        SC2_MapResolveUnitModel(cstring_t unit_type);
bool          SC2_MapResolveUnit(cstring_t unit_type, sc2MapObject_t *object);
cstring_t        SC2_MapResolveSound(cstring_t sound_id, int asset);
float         SC2_MapSoundLength(cstring_t sound_id, int asset);
cstring_t        SC2_MapConversationField(cstring_t key, cstring_t field);

float         SC2_MapHeightAtPoint(float x, float y);
float         SC2_MapAirHeightAtPoint(float x, float y);
box2_t          SC2_MapBounds(void);
vector2_t       SC2_MapNormalizedPosition(float x, float y);
vector2_t       SC2_MapDenormalizedPosition(float x, float y);
uint32_t         SC2_MapObjectClassId(sc2MapObject_t const *object);
bool          SC2_MapDefaultCamera(sc2MapCamera_t *camera);
void          SC2_MapDump(FILE *out, cstring_t filename);

#endif
