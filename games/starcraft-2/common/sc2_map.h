#ifndef SC2_MAP_H
#define SC2_MAP_H

#include "common/common.h"
#include <stdio.h>
#include <math.h>

static inline FLOAT SC2_LerpDegrees(FLOAT a, FLOAT b, FLOAT k) {
    FLOAT delta = fmodf(b - a + 540.0f, 360.0f) - 180.0f;
    return a + delta * k;
}

/* Map/Galaxy pitch is degrees down from horizontal (old lookAt). Orbit identity looks down -Z, so
 * playerState.viewangles.x is pitch-90. Gameplay 56 becomes -34, matching WC3's 326 Euler tilt. */
static inline VECTOR3 SC2_EulerFromCamera(FLOAT pitch, FLOAT yaw) {
    return (VECTOR3){ pitch - 90.0f, 0.0f, yaw };
}
static inline VECTOR3 SC2_CameraFromEuler(LPCVECTOR3 euler, FLOAT height) {
    return (VECTOR3){ euler->x + 90.0f, euler->z, height };
}

#define SC2_MAX_MAP_OBJECTS 4096 // objects; accommodates object-heavy campaign maps such as TRaynor01
#define SC2_CELL_SIZE          1.0f
#define SC2_BROAD_HEIGHT_RADIUS  8.0f // world units; half-width of the air/camera terrain filter footprint
#define SC2_BROAD_HEIGHT_SAMPLES 5    // taps per axis; suppresses narrow depressions without filtering over time
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
    VECTOR3         target;
    FLOAT           distance;
    FLOAT           pitch;
    FLOAT           yaw;
    FLOAT           fov;
    FLOAT           znear;
    FLOAT           zfar;
    FLOAT           height_offset;
} sc2MapCamera_t;

typedef struct {
    sc2ObjectType_t type;
    DWORD           id;
    char            name[64];
    char            model[256];
    char            footprint[64];
    char            mover[64];
    char            type_name[64];
    char            anim_props[64];
    char            sound[256];
    char            attach_id[64];
    char            object_type[64];
    VECTOR3         position;
    FLOAT           angle;
    FLOAT           scale;
    FLOAT           radius;
    FLOAT           footprint_width;
    FLOAT           footprint_height;
    FLOAT           footprint_radius;
    FLOAT           move_height;
    FLOAT           pathing_soft_radius;
    FLOAT           pathing_hard_radius;
    DWORD           variation;
    DWORD           player;
    DWORD           section;
    DWORD           resources;
    DWORD           object_id;
    DWORD           flags;
    DWORD           unit_flags;
    COLOR32         color;
    COLOR32         tint_color;
    sc2MapCamera_t  camera;
} sc2MapObject_t;

static inline FLOAT sc2_unit_world_height(FLOAT terrain, FLOAT height, BOOL flying) {
    return terrain + (flying ? height : 0.0f);
}

typedef struct {
    BOOL            enabled;
    VECTOR3         color;
    FLOAT           color_multiplier;
    FLOAT           spec_color_multiplier;
    VECTOR3         direction;
} sc2DirectionalLight_t;

typedef struct {
    BOOL            enabled;
    DWORD           colorize;
    char            id[64];
    VECTOR3         ambient_color;
    FLOAT           colorization_blend;
    sc2DirectionalLight_t directional[SC2_MAX_DIRECTIONAL_LIGHTS];
} sc2MapLighting_t;

/* Colorized SC2 lights use the authored blend as ambient strength; ordinary lights use their ambient directly. */
static VECTOR3 sc2_light_ambient(sc2MapLighting_t const *light) {
    FLOAT scale = light && light->colorize ? light->colorization_blend : 1.0f;
    return light ? Vector3_scale(&light->ambient_color, scale) : (VECTOR3){ 0.35f, 0.35f, 0.40f };
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
    DWORD          index;
    DWORD          flags;
    DWORD          cliff_set;
    DWORD          variant;
} sc2CliffCell_t;

typedef struct {
    char           tile_set[64];
    DWORD          num_terrain_textures;
    sc2TerrainTexture_t terrain_textures[SC2_MAX_TERRAIN_TEXTURES];
    DWORD          num_cliff_sets;
    sc2CliffSet_t cliff_sets[SC2_MAX_CLIFF_SETS];
    DWORD          num_cliff_cells;
    sc2CliffCell_t cliff_cells[SC2_MAX_CLIFF_CELLS];
    FLOAT          height_quantize_bias;
    FLOAT          height_quantize_scale;
    FLOAT          standard_height;
    BOOL           fog_enabled;
    FLOAT          fog_density;
    FLOAT          fog_falloff;
    FLOAT          fog_start_height;
    COLOR32        fog_color;
} sc2MapTerrain_t;

typedef struct {
    USHORT         adjustment;
    USHORT         height;
    USHORT         extra;
} sc2MapHeightSample_t;

typedef struct {
    DWORD          fourcc;
    DWORD          version;
    DWORD          width;
    DWORD          height;
    BYTE           padding[16];
    sc2MapHeightSample_t data[];
} sc2MapHeightMap_t;

typedef struct {
    SHORT          height;
    USHORT         mask;
} sc2MapSyncHeightSample_t;

typedef struct {
    DWORD          fourcc;
    DWORD          version;
    DWORD          width;
    DWORD          height;
    BYTE           padding[48];
    sc2MapSyncHeightSample_t data[];
} sc2MapSyncHeightMap_t;

typedef struct {
    DWORD          fourcc;
    DWORD          version;
    DWORD          zero[4];
    DWORD          width;
    DWORD          height;
    BYTE           data[];
} sc2MapCellFlags_t;

typedef struct {
    DWORD          fourcc;
    DWORD          version;
    DWORD          width;
    DWORD          height;
    DWORD          zero[4];
    USHORT         data[];
} sc2MapSyncCliffLevel_t;

typedef struct {
    DWORD          fourcc;
    DWORD          version;
    DWORD          unknown;
    DWORD          width;
    DWORD          height;
    DWORD          zero[11];
    BYTE           data[];
} sc2MapTextureMasks_t;

typedef struct {
    char            tile[64];
    char            model[256];
    VECTOR3         position;
    VECTOR3         normal;
    VECTOR3         start;
    VECTOR3         end;
    VECTOR2         scale;
    USHORT          flags;
} sc2MapHardTile_t;

typedef struct {
    DWORD          fourcc;
    DWORD          version;
    DWORD          unknown0;
    DWORD          unknown1;
    DWORD          width;
    DWORD          height;
    BYTE           data[SC2_MAPINFO_DATA_SIZE];
} sc2MapInfo_t;

typedef struct {
    DWORD          units;
    DWORD          actors;
    DWORD          models;
    DWORD          footprints;
    DWORD          unresolved_models;
} sc2CatalogStats_t;

typedef struct {
    char           map_name[128];
    VECTOR2        origin;
    FLOAT          cell_size;
    DWORD          num_objects;
    sc2MapObject_t objects[SC2_MAX_MAP_OBJECTS];
    sc2MapTerrain_t t3Terrain;
    sc2MapTextureMasks_t *t3TextureMasks;
    DWORD          t3TextureMasksSize;
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
    DWORD          x0;
    DWORD          y0;
    DWORD          x1;
    DWORD          y1;
    FLOAT          tx;
    FLOAT          ty;
} sc2MapHeightPoint_t;

static inline DWORD sc2_map_cell_width(sc2Map_t const *map) {
    return map ? map->MapInfo.width : 0;
}

static inline DWORD sc2_map_cell_height(sc2Map_t const *map) {
    return map ? map->MapInfo.height : 0;
}

static inline FLOAT sc2_map_height_scale(sc2Map_t const *map) {
    return map && map->t3Terrain.height_quantize_scale ? map->t3Terrain.height_quantize_scale : 1.0f;
}

static inline FLOAT sc2_map_height_offset(sc2Map_t const *map) {
    return map ? map->t3Terrain.height_quantize_bias + map->t3Terrain.standard_height + 1.0f : 1.0f;
}

static inline FLOAT sc2_map_height_at_grid(sc2Map_t const *map, DWORD x, DWORD y) {
    sc2MapHeightSample_t const *sample;

    if (!map || !map->t3HeightMap || !map->t3HeightMap->width || !map->t3HeightMap->height)
        return 0.0f;
    x = MIN(map->t3HeightMap->width - 1, x);
    y = MIN(map->t3HeightMap->height - 1, y);
    sample = &map->t3HeightMap->data[x + y * map->t3HeightMap->width];
    return ((FLOAT)sample->height + (FLOAT)sample->adjustment) * sc2_map_height_scale(map) - sc2_map_height_offset(map);
}

static inline FLOAT sc2_map_height_adjust_at_grid(sc2Map_t const *map, DWORD x, DWORD y) {
    sc2MapHeightSample_t const *sample;

    if (!map || !map->t3HeightMap || !map->t3HeightMap->width || !map->t3HeightMap->height)
        return 0.0f;
    x = MIN(map->t3HeightMap->width - 1, x);
    y = MIN(map->t3HeightMap->height - 1, y);
    sample = &map->t3HeightMap->data[x + y * map->t3HeightMap->width];
    return (FLOAT)sample->adjustment * sc2_map_height_scale(map);
}

static inline BOOL sc2_map_height_point(sc2Map_t const *map, FLOAT x, FLOAT y, sc2MapHeightPoint_t *point) {
    FLOAT fx, fy;

    if (!point || !map || !map->t3HeightMap || !map->t3HeightMap->width || !map->t3HeightMap->height)
        return false;
    memset(point, 0, sizeof(*point));
    fx = (x - map->origin.x) / (map->cell_size ? map->cell_size : 1.0f);
    fy = (y - map->origin.y) / (map->cell_size ? map->cell_size : 1.0f);
    fx = MIN(MAX(fx, 0.0f), (FLOAT)(sc2_map_cell_width(map) ? sc2_map_cell_width(map) : map->t3HeightMap->width - 1));
    fy = MIN(MAX(fy, 0.0f), (FLOAT)(sc2_map_cell_height(map) ? sc2_map_cell_height(map) : map->t3HeightMap->height - 1));
    point->x0 = (DWORD)floorf(fx);
    point->y0 = (DWORD)floorf(fy);
    point->x1 = point->x0 + 1;
    point->y1 = point->y0 + 1;
    point->tx = fx - (FLOAT)point->x0;
    point->ty = fy - (FLOAT)point->y0;
    return true;
}

static inline FLOAT sc2_map_height_lerp(FLOAT h00, FLOAT h10, FLOAT h01, FLOAT h11, FLOAT tx, FLOAT ty) {
    FLOAT h0 = h00 + (h10 - h00) * tx;
    FLOAT h1 = h01 + (h11 - h01) * tx;
    return h0 + (h1 - h0) * ty;
}

static inline FLOAT sc2_map_height_at_point(sc2Map_t const *map, FLOAT x, FLOAT y) {
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
static inline FLOAT sc2_map_broad_height_at_point(sc2Map_t const *map, FLOAT x, FLOAT y) {
    FLOAT sum = 0.0f, step = SC2_BROAD_HEIGHT_RADIUS * 2.0f / (SC2_BROAD_HEIGHT_SAMPLES - 1);
    int ix, iy;

    for (iy = 0; iy < SC2_BROAD_HEIGHT_SAMPLES; iy++)
        for (ix = 0; ix < SC2_BROAD_HEIGHT_SAMPLES; ix++)
            sum += sc2_map_height_at_point(map, x - SC2_BROAD_HEIGHT_RADIUS + ix * step,
                                          y - SC2_BROAD_HEIGHT_RADIUS + iy * step);
    return sum / (SC2_BROAD_HEIGHT_SAMPLES * SC2_BROAD_HEIGHT_SAMPLES);
}

static inline FLOAT sc2_map_height_adjust_at_point(sc2Map_t const *map, FLOAT x, FLOAT y) {
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
    HANDLE (*read_file)(LPCSTR filename, LPDWORD size);
    void   (*free_file)(HANDLE file);
    HANDLE (*mem_alloc)(long size);
    void   (*mem_free)(HANDLE mem);
    LPCSTR (*cvar_string)(LPCSTR name, LPCSTR fallback);
} sc2MapHost_t;

void          SC2_MapSetHost(sc2MapHost_t const *host);
BOOL          SC2_MapLoad(LPCSTR mapFilename);
void          SC2_MapShutdown(void);
sc2Map_t     *SC2_MapCurrent(void);
LPCSTR        SC2_MapResolveUnitModel(LPCSTR unit_type);
BOOL          SC2_MapResolveUnit(LPCSTR unit_type, sc2MapObject_t *object);
LPCSTR        SC2_MapResolveSound(LPCSTR sound_id, int asset);
FLOAT         SC2_MapSoundLength(LPCSTR sound_id, int asset);
FLOAT         SC2_MapHeightAtPoint(FLOAT x, FLOAT y);
FLOAT         SC2_MapAirHeightAtPoint(FLOAT x, FLOAT y);
FLOAT         SC2_MapCameraHeightAtPoint(FLOAT x, FLOAT y);
BOX2          SC2_MapBounds(void);
VECTOR2       SC2_MapNormalizedPosition(FLOAT x, FLOAT y);
VECTOR2       SC2_MapDenormalizedPosition(FLOAT x, FLOAT y);
DWORD         SC2_MapObjectClassId(sc2MapObject_t const *object);
BOOL          SC2_MapDefaultCamera(sc2MapCamera_t *camera);
void          SC2_MapDump(FILE *out, LPCSTR filename);

#endif
