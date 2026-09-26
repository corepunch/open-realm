#ifndef __r_wowmap_h__
#define __r_wowmap_h__

#include "renderer/r_local.h"
#include "common/wow_coords.h"
#include "common/ui_constants.h"
#include "common/wow_chunks.h"
#include <strings.h>
#include <stdlib.h>
#include <float.h>
#include <math.h>

#define WOW_WDT_TILES 64
#define WOW_MCVT_COUNT (9 * 9 + 8 * 8)
#define WOW_ADT_RADIUS 1
#define WOW_TEXTURE_KEEP_GENERATIONS 2 // window rebuilds; keep streaming textures this many slides before reclaim to avoid reload thrash in the overlapping tiles
#define WOW_ADT_CHUNK_SIZE (WOW_ADT_SIZE / 16.0f)
#define WOW_ADT_UNIT_SIZE (WOW_ADT_CHUNK_SIZE / 8.0f)
#define WOW_ALPHA_TEXELS (64 * 64)
#define WOW_ALPHA_CHUNK_SIZE 64
#define WOW_ALPHA_ATLAS_CHUNKS ((WOW_ADT_RADIUS * 2 + 1) * 16)
#define WOW_ALPHA_ATLAS_SIZE (WOW_ALPHA_CHUNK_SIZE * WOW_ALPHA_ATLAS_CHUNKS)
#define WOW_IGNORE_TERRAIN_HOLES 1
#define WOW_DEBUG_OBJECT_MARKERS 0
#define WOW_DEBUG_DOODAD_ERROR_MESHES 0
#define WOW_DOODAD_DRAW_DISTANCE 450.0f
#define WOW_TERRAIN_DRAW_DISTANCE WOW_WORLD_FAR_CLIP
#define WOW_MINIMAP_WORLD_RADIUS 160.0f
#define WOW_MINIMAP_HASH_LENGTH 32
#define WOW_WMO_MODEL_BATCH_DIVISOR 2
#define WOW_DOODAD_BUCKET_SIZE 128.0f
#define WOW_DOODAD_BUCKETS 272
#define WOW_WORLD_COORD_OFFSET (32.0f * WOW_ADT_SIZE)
#define WOW_SPLAT_MAX_SUBDIVISIONS 16
#define WOW_SPLAT_MIN_SUBDIVISIONS 4
#define WOW_SPLAT_BATCHES 8
#define WOW_SPLAT_BATCH_VERTICES 4096
#define WOW_SPLAT_Z_BIAS 0.05f
#define WOW_SPLAT_MAX_HEIGHT_DELTA 3.0f
#define WOW_GRASS_DRAW_DISTANCE 220.0f          // world units; instances beyond this are discarded
#define WOW_GRASS_CULL_RADIUS   487.0f          // build-time filter: draw_dist(220) + half-ADT(267); safe to discard beyond this
#define WOW_GRASS_FADE_START_DISTANCE 160.0f    // world units; alpha fade begins here and reaches 0 at DRAW_DISTANCE
#define WOW_GRASS_WIND_SPEED 1.7f               // rad/s; angular frequency of the sway sin wave
#define WOW_GRASS_WIND_AMPLITUDE 0.12f          // fraction of blade height; peak lateral displacement
#define WOW_GRASS_WIND_ROOT_FRACTION 0.15f      // [0,1]; sway weight is 0 below this normalized blade height (root anchor)
#define WOW_GRASS_WIND_PHASE_X 0.917f           // rad/world-unit in X; large enough that adjacent blades (~2.5 u apart) differ by ~131°
#define WOW_GRASS_WIND_PHASE_Y 1.481f           // rad/world-unit in Y; ratio to PHASE_X ≈ φ² to prevent grid-aligned periodicity
#define WOW_GRASS_WIND_DIRECTION_X 0.86f        // sway XY direction, X component; together with Y ≈ 30° off axis, length ≈ 1
#define WOW_GRASS_WIND_DIRECTION_Y 0.51f        // sway XY direction, Y component; used as uGrassParams[2].zw in the instanced shader
#define WOW_GRASS_ROAD_COVERAGE_MIN 24          // alpha [0-255]; cells with a road-layer alpha above this suppress grass
#define WOW_GRASS_CELL_STEP 1                   // stride over the 8×8 cell grid; 1 = every cell, 2 = every other (halves density)
#define WOW_GRASS_CELLS_PER_AXIS 8              // cells per MCNK axis; matches WoW's fixed 8×8 sub-cell layout
#define WOW_GRASS_MAX_PLACEMENTS_PER_SAMPLE 12  // max M2 instances per sampled cell; hard ceiling on the clumps formula
#define WOW_GRASS_COVERAGE_MIN 32               // alpha [0-255]; minimum coverage to spawn any grass in a cell at all
#define WOW_GRASS_ALPHA_AXIS 8                  // sample points per axis when mapping a cell coordinate to an alpha texel index
#define WOW_GRASS_ALPHA_MAX 63                  // max alpha texel index (8×8 = 64 entries, 0-based)
#define WOW_GRASS_ALPHA_TEXEL_MAX 255.0f        // float denominator to normalize a raw alpha byte to [0,1]
#define WOW_GRASS_DBC_DENSITY_MAX 24            // cap on GroundEffectTexture.dbc density field; prevents over-spawning on high-density records
#define WOW_GRASS_DBC_FIELD_COUNT 11            // total uint32_t fields per GroundEffectTexture.dbc record
#define WOW_GRASS_DOODAD_FIELD_COUNT 3          // total uint32_t fields per GroundEffectDoodad.dbc record (id, legacy_field, model_path)
#define WOW_GRASS_TEXTURE_LEGACY_DOODAD_FIELD 5 // field index of first doodad_id in the pre-TBC GroundEffectTexture layout
#define WOW_GRASS_TEXTURE_MODERN_DOODAD_FIELD 1 // field index of first doodad_id in the TBC+ GroundEffectTexture layout
#define WOW_GRASS_TEXTURE_WEIGHT_FIELD 5        // field index of first doodad weight in GroundEffectTexture (4 consecutive DWORDs)
#define WOW_GRASS_TEXTURE_DENSITY_FIELD 9       // field index of the density value in a GroundEffectTexture.dbc record
#define WOW_GRASS_DOODAD_MODEL_FIELD 2          // field index of the model path string in a GroundEffectDoodad.dbc record
#define WOW_GRASS_DOODAD_LOGGED_IDS 65536       // size of the one-shot missing-ID log bitfield; covers the full 16-bit DBC id space
#define WOW_GRASS_DOODAD_SLOTS 4                // weighted doodad variants per GroundEffectTexture record
#define WOW_GRASS_INVALID_DOODAD 0xFFFFFFFFU    // sentinel value for an empty doodad slot in a GroundEffectTexture record
#define WOW_GRASS_VERTICES_PER_CLUMP 12         // triangle-list cross blade: 2 quads × 6 verts (used by camera-mesh path)
#define WOW_GRASS_CELL_OFFSET 0.20f             // [0,1] cell fraction; minimum inset from edge before jitter is applied
#define WOW_GRASS_CELL_MARGIN 0.40f             // [0,1] cell fraction; caps the random jitter range to stay inside the cell
#define WOW_GRASS_CLUMP_JITTER 0.45f            // cell units; scatter radius applied to each instance within a clump
#define WOW_GRASS_COORD_EPSILON 0.001f          // safety margin when clamping local row/col to [0, CELLS_PER_AXIS − ε]
#define WOW_GRASS_Z_BIAS 0.02f                  // world units; upward offset on all placements to avoid z-fighting with terrain
#define WOW_GRASS_FULL_CIRCLE 6.2831853f        // 2π rad; full yaw rotation range for random blade orientation
#define WOW_GRASS_BLADE_HEIGHT_MIN 0.55f        // world units; shortest possible blade before the random height variation is added
#define WOW_GRASS_BLADE_HEIGHT_VARIATION 0.45f  // world units; random value in [0,1] × this is added to BLADE_HEIGHT_MIN
#define WOW_GRASS_BLADE_WIDTH_MIN 0.30f         // world units; narrowest possible blade half-width
#define WOW_GRASS_BLADE_WIDTH_VARIATION 0.20f   // world units; random value in [0,1] × this is added to BLADE_WIDTH_MIN
#define WOW_GRASS_CROSS_ANGLE 1.5707963f        // π/2 rad; rotation between the two quads of a cross-blade mesh
#define WOW_GRASS_CROSS_WIDTH_SCALE 0.85f       // scale factor applied to the second quad's width for visual variety
#define WOW_GRASS_CROSS_HEIGHT_SCALE 0.90f      // scale factor applied to the second quad's height for visual variety
#define WOW_GRASS_NORMAL_Z 0.10f               // Z component of the fake upward normal baked into blade vertices

/* Height atlas: 17x9 texel tiles packed into a GL_R32F atlas */
#define WOW_HEIGHT_ATLAS_TILE_W  17
#define WOW_HEIGHT_ATLAS_TILE_H  9
#define WOW_HEIGHT_ATLAS_CHUNKS  WOW_ALPHA_ATLAS_CHUNKS
#define WOW_HEIGHT_ATLAS_W       (WOW_HEIGHT_ATLAS_TILE_W * WOW_HEIGHT_ATLAS_CHUNKS)
#define WOW_HEIGHT_ATLAS_H       (WOW_HEIGHT_ATLAS_TILE_H * WOW_HEIGHT_ATLAS_CHUNKS)

/* Grass control texture: one RGBA8 texel per 8x8-grid cell (suppression, density, effect) */
#define WOW_GRASS_CTRL_CELLS     8
#define WOW_GRASS_CTRL_CHUNKS    WOW_ALPHA_ATLAS_CHUNKS
#define WOW_GRASS_CTRL_SIZE      (WOW_GRASS_CTRL_CELLS * WOW_GRASS_CTRL_CHUNKS)

/* Camera-following world-cell grid: an odd side keeps one slot centered on the camera cell. */
#define WOW_GRASS_GRID_SIDE      181
#define WOW_GRASS_GRID_HALF      90
#define WOW_GRASS_SLOT_SPACING   2.5f
#define WOW_GRASS_BLADE_SLOTS    (WOW_GRASS_GRID_SIDE * WOW_GRASS_GRID_SIDE)
#define WOW_GRASS_VERTS_PER_BLADE 12  /* triangle-list cross: 2 quads x 6 verts */

/* The camera grid cannot preserve GroundEffectDoodad M2 geometry/material identity yet. */
#define WOW_GRASS_CAMERA_MESH 0

typedef struct wowWdtTile_s {
    bool present;
} wowWdtTile_t;

typedef struct wowTextureCache_s {
    PATHSTR path;
    texture_t *texture;
    struct wowTextureCache_s *next;
} wowTextureCache_t;

typedef struct wowM2BoundsCache_s {
    PATHSTR path;
    float radius;
    struct wowM2BoundsCache_s *next;
} wowM2BoundsCache_t;

typedef struct wowM2Array_s {
    int32_t count;
    int32_t offset;
} wowM2Array_t;

typedef struct {
    float x, y, z;
} wowVec3_t;

typedef struct wowDoodadModel_s {
    PATHSTR path;
    model_t *model;
    mat4_t *matrices;
    instanceBuffer_t instances;
    uint32_t count, capacity;
    /* Retain buffer capacity while rebuilding only the visible MODR subset each frame. */
    mat4_t *wmo_matrices;
    instanceBuffer_t wmo_instances;
    uint32_t wmo_count, wmo_capacity;
    bool can_instance;
    struct wowDoodadModel_s *next;
} wowDoodadModel_t;

typedef struct wowDoodadInstance_s {
    renderEntity_t entity;
    wowDoodadModel_t *group;
    struct wowDoodadInstance_s *next;
    struct wowDoodadInstance_s *bucket_next;
} wowDoodadInstance_t;

typedef struct {
    uint8_t      type;        /* 0=OMNI 1=SPOT 2=DIRECT 3=AMBIENT */
    uint8_t      use_atten;
    uint8_t      pad[2];
    color32_t   color;       /* BGRA in file */
    wowVec3_t position;    /* WMO local space */
    float     intensity;
    float     atten_start;
    float     atten_end;
    float     unk[4];
} wowWmoLight_t;  /* 48 bytes */

typedef struct wowWmoBatch_s {
    buffer_t *buffer;
    texture_t *texture;
    uint32_t num_vertices;
    bool indoor;
    uint8_t blend_mode;    /* MOMT blendMode: 0=Opaque 1=AlphaKey 2=Alpha 3=NoAlphaAdd 4=Add */
    bool transparent;   /* true when blend_mode >= 2 (requires GL_BLEND pass) */
    struct wowWmoBatch_s *next;
} wowWmoBatch_t;

typedef struct {
    uint16_t  start_vertex; /* first vertex index in model->portal_vertices */
    uint16_t  count;        /* number of vertices in this portal polygon */
    float plane[4];     /* (nx, ny, nz, d) in WMO local space */
} wowWmoPortal_t;  /* 20 bytes */

typedef struct {
    uint16_t  portal_index; /* index into model->portals */
    uint16_t  group_index;  /* group this portal connects to */
    int16_t side;       /* -1 or +1: which side the group is on */
    uint16_t  pad;
} wowWmoPortalRef_t;  /* 8 bytes */

typedef struct wowWmoGroup_s {
    wowWmoBatch_t *batches;
    ARRAY(uint16_t, doodad_refs);
    box3_t bounds;
    bool has_bounds;
    bool indoor;           /* MOGP flags bit 0x2000: group is interior */
    uint16_t portal_start;     /* MOGP +0x24: first entry in model->portal_refs */
    uint16_t portal_count;     /* MOGP +0x26: number of portal_refs for this group */
    color32_t group_amb;       /* MOGP replacement_for_header_color (BGRA→RGB) */
    bool    has_group_amb;   /* true when replacement_for_header_color was non-zero */
} wowWmoGroup_t;

typedef struct {
    char  name[20];  /* doodad set name, null-padded */
    uint32_t start;     /* first MODD index in this set */
    uint32_t count;     /* number of MODD entries */
    uint32_t pad;
} wowWmoDoodadSet_t;  /* 32 bytes */

typedef struct {
    uint32_t     name_flags;  /* bits 0-23 = byte offset into MODN blob; bits 24-31 = instance flags */
    wowVec3_t position;    /* WMO local space */
    float     quat[4];     /* (x, y, z, w) orientation in WMO local space */
    float     scale;
    color32_t   color;       /* BGRA; color.a = MOLT index when flags bit 2 set */
} wowWmoDoodadDef_t;  /* 40 bytes */

typedef struct wowWmoModel_s {
    PATHSTR path;
    wowWmoGroup_t *groups;
    wowWmoBatch_t *batches;
    uint32_t num_groups;
    uint32_t num_batches;
    bool loaded;
    color32_t amb_color;   /* MOHD.ambColor: .r=R .g=G .b=B after BGRA swap */
    uint32_t   mohd_flags;  /* bit 0x02=lighten_interiors, 0x04=skip_base_color */
    uint32_t   n_lights;    /* MOHD.nLights, for MOLT */
    wowWmoDoodadSet_t *doodad_sets;
    uint32_t              num_doodad_sets;
    wowWmoDoodadDef_t *doodad_defs;
    uint32_t              num_doodad_defs;
    uint8_t              *doodad_referenced; /* MODR ownership bit per MODD */
    char              *doodad_name_blob;   /* raw MODN chunk bytes, null-terminated */
    uint32_t              doodad_name_blob_size;
    wowWmoLight_t     *lights;             /* MOLT light array */
    uint32_t              num_lights_parsed;  /* actual parsed count (n_lights = from MOHD header) */
    wowWmoPortal_t    *portals;            /* MOPT portal plane definitions */
    uint32_t              num_portals;
    wowVec3_t         *portal_vertices;   /* MOPV portal polygon vertices */
    uint32_t              num_portal_vertices;
    wowWmoPortalRef_t *portal_refs;       /* MOPR per-group portal references */
    uint32_t              num_portal_refs;
    /* Per-doodad-def group pointer cache — filled once on first Wow_QueueWmoDoodads call
     * to avoid O(n) strcasecmp lookup on every render frame. num_doodad_defs entries. */
    wowDoodadModel_t **def_groups;
    /* Model-space bounding sphere from MOHD; used for whole-WMO early-out in precompute. */
    vec3_t bounds_center;
    float   bounds_radius;
    bool    has_bounds;
    struct wowWmoModel_s *next;
} wowWmoModel_t;

typedef struct wowWmoInstance_s {
    wowWmoModel_t *model;
    mat4_t matrix;
    uint16_t doodad_set;  /* MODF.doodadSet index into model->doodad_sets */
    uint8_t *visible_groups; /* current view's authoritative MOGP visibility */
    uint8_t *doodad_seen;    /* per-instance MODR duplicate guard */
    bool visible;
    struct wowWmoInstance_s *next;
} wowWmoInstance_t;

typedef struct wowAdtChunk_s {
    buffer_t *buffer;
    buffer_t *grass_buffer;
    texture_t *textures[4];
    texture_t *alpha_texture;
    uint32_t alpha_index_x;
    uint32_t alpha_index_y;
    uint32_t num_vertices;
    uint32_t num_grass_vertices;
    uint32_t layer_count;
    wowVec3_t position;
    float heights[WOW_MCVT_COUNT];
    bool has_heights;
    uint8_t mcsh[512];
    bool has_mcsh;
    box3_t bounds;
    box3_t grass_bounds;
    struct wowAdtChunk_s *next;
} wowAdtChunk_t;

typedef struct wowMap_s {
    wowWdtTile_t tiles[WOW_WDT_TILES][WOW_WDT_TILES];
    wowAdtChunk_t *chunks;
    wowAdtChunk_t *height_chunks[WOW_HEIGHT_ATLAS_CHUNKS][WOW_HEIGHT_ATLAS_CHUNKS];
    wowTextureCache_t *textures;
    wowM2BoundsCache_t *m2_bounds;
    wowDoodadModel_t *doodad_models;
    wowDoodadInstance_t *doodads;
    wowDoodadInstance_t *doodad_buckets[WOW_DOODAD_BUCKETS][WOW_DOODAD_BUCKETS];
    wowDoodadInstance_t *ground_effects;
    wowWmoModel_t *wmo_models;
    wowWmoInstance_t *wmos;
    texture_t *alpha_atlas_texture;
    texture_t *height_atlas;      /* R32F 17x9-per-chunk height values */
    texture_t *grass_ctrl;        /* RGBA8 per-cell suppression/density/effect */
    buffer_t *grass_tile_vbo;    /* immutable camera-following blade mesh */
    uint32_t     grass_tile_nverts;
    float atlas_world_x;         /* world pos.x of atlas tile (iy=0) chunk */
    float atlas_world_y;         /* world pos.y of atlas tile (ix=0) chunk */
    bool  has_atlas_origin;
    buffer_t *object_buffer;
    uint32_t num_object_vertices;
    uint32_t num_adts;
    uint32_t num_chunks;
    uint32_t num_grass_chunks;
    uint32_t num_grass_vertices;
    uint32_t num_doodads;
    uint32_t num_doodad_instances;
    uint32_t num_ground_effects;
    uint32_t num_doodad_models;
    uint32_t num_missing_doodad_models;
    uint32_t num_filedata_doodads;
    uint32_t num_wmos;
    uint32_t num_wmo_models;
    uint32_t num_wmo_batches;
    uint32_t num_missing_wmos;
    uint32_t *placed_wmo_ids;     /* non-zero MODF unique_ids accepted this ADT window; dedup guard */
    uint32_t num_placed_wmo_ids, cap_placed_wmo_ids;
    uint32_t *placed_dood_ids;    /* non-zero MDDF unique_ids accepted this ADT window; dedup guard */
    uint32_t num_placed_dood_ids, cap_placed_dood_ids;
    uint32_t wdt_flags;
    bool use_weighted_blend;
    bool has_adt_window;
    int adt_center_x;
    int adt_center_y;
    uint32_t layer_histogram[5];
    int alpha_origin_x;
    int alpha_origin_y;
    PATHSTR map_dir;
    char map_name[128];
    char minimap_hash[WOW_WDT_TILES][WOW_WDT_TILES][WOW_MINIMAP_HASH_LENGTH + 1];
    texture_t *minimap_tiles[WOW_WDT_TILES][WOW_WDT_TILES];
    uint8_t minimap_warned[WOW_WDT_TILES][WOW_WDT_TILES];
} wowMap_t;

typedef struct {
    uint32_t flags;
    uint32_t async_id;
} wowWdtMainEntry_t;

typedef struct {
    uint32_t texture_id;
    uint32_t flags;
    uint32_t offset_in_mcal;
    uint32_t effect_id;
} wowLayer_t;

typedef struct {
    uint32_t name_id;
    uint32_t unique_id;
    wowVec3_t position;
    wowVec3_t rotation;
    uint16_t scale;
    uint16_t flags;
} wowDoodadDef_t;

typedef struct {
    wowVec3_t min;
    wowVec3_t max;
} wowBox_t;

typedef struct {
    uint32_t name_id;
    uint32_t unique_id;
    wowVec3_t position;
    wowVec3_t rotation;
    wowBox_t extents;
    uint16_t flags;
    uint16_t doodad_set;
    uint16_t name_set;
    uint16_t scale;
} wowMapObjDef_t;

typedef struct {
    uint8_t flags;
    uint8_t material_id;
} wowWmoPoly_t;

typedef struct {
    int16_t box_min[3];
    int16_t box_max[3];
    uint32_t first_index;
    uint16_t num_indices;
    uint16_t first_vertex;
    uint16_t last_vertex;
    uint8_t flags;
    uint8_t material_id;
} wowWmoBatchDef_t;

typedef struct {
    float u, v;
} wowVec2_t;

typedef struct {
    uint32_t id;
    uint32_t date_stamp;
    uint32_t continent_id;
    uint32_t zone_id;
    uint32_t texture_id;
    uint32_t doodad_id[WOW_GRASS_DOODAD_SLOTS];
    uint32_t weight[WOW_GRASS_DOODAD_SLOTS];
    uint32_t density;
    uint32_t sound;
} wowGroundEffectTexture_t;

typedef struct {
    uint32_t id;
    uint32_t legacy_field;
    PATHSTR model_path;
} wowGroundEffectDoodad_t;

extern wowMap_t wow_world;

/* Terrain and grass have separate typed values and private program locations. */
typedef struct wowTerrainState_s {
    mat4_t viewProjection;
    mat4_t model;
    mat3_t normalMatrix;
    vec3_t sunDir;
    vec3_t sunAmbient;
    vec3_t sunDiffuse;
    int texture0;
    int texture1;
    int texture2;
    int texture3;
    int alphaTexture;
    bool useWeightedBlend;
    bool singleTexture;
    bool wmoIndoor;
    vec3_t wmoAmbient;
    vec3_t wmoLightAdd;
    int wmoBlendMode;
    vec2_t alphaOrigin;
    float alphaAtlasChunks;
    bool fogEnable;
    vec3_t fogColor;
    vec2_t fogParams;
    vec3_t fogCamera;
} wowTerrainState_t;


typedef struct wowTerrainProg_s {
    shaderProg_t prog;
    wowTerrainState_t state;
} wowTerrainProg_t;



typedef struct wowGrassState_s {
    mat4_t viewProjection;
    vec3_t sunDir;
    vec3_t sunAmbient;
    vec3_t sunDiffuse;
    float grassTime;
    int grassCtrl;
    vec2_t ctrlOriginWorld;
    float ctrlCellSize;
    vec2_t cameraXZ;
    float grassSlotSpacing;
    int heightAtlas;
    vec2_t atlasOriginWorld;
    float atlasChunkSize;
    float atlasUnitSize;
    vec3_t grassCameraOrigin;
    float grassDrawDistance;
    float grassFadeStartDistance;
} wowGrassState_t;


typedef struct wowGrassProg_s {
    shaderProg_t prog;
    wowGrassState_t state;
} wowGrassProg_t;



extern wowTerrainProg_t wow_terrain_shader;
extern wowGrassProg_t wow_grass_shader;
/* Height atlas uniforms (terrain + grass) */
/* Grass control texture uniforms */
/* Camera-following grass tile uniforms */

bool Wow_PathHasExtension(cstring_t path, cstring_t extension);
void Wow_NormalizeMapPath(cstring_t mapFileName, string_t out, uint32_t out_size);
void Wow_SetMapNames(cstring_t path);
bool Wow_LoadMinimapTranslations(void);
uint32_t Wow_Read32(uint8_t const *p);
uint16_t Wow_Read16(uint8_t const *p);
void Wow_FreeChunks(void);
void Wow_FreeWmoModels(void);
void Wow_FreeWmoInstances(void);
void Wow_FreeDoodadInstances(void);
void Wow_ClearLoadedAdts(void);
void Wow_FreeWorld(void);
void Wow_ShutdownWorldShaders(void);
texture_t *Wow_LoadTexture(cstring_t path, bool streamable);
bool Wow_ReadM2RadiusFromPath(cstring_t path, float *radius);
bool Wow_CopyModelPathFallback(cstring_t path, string_t out, uint32_t out_size);
float Wow_LoadM2BoundsRadius(cstring_t path);
texture_t *Wow_CreateAlphaTexture(uint8_t const alpha[4][WOW_ALPHA_TEXELS]);
void Wow_EnsureAlphaAtlasTexture(void);
void Wow_UploadAlphaAtlasChunk(uint32_t index_x, uint32_t index_y, uint8_t const alpha[4][WOW_ALPHA_TEXELS]);
void Wow_InitTerrainShader(void);
color32_t Wow_Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
vertex_t Wow_Vertex(float x, float y, float z, float u, float v, color32_t color);
void Wow_AddBoundsPoint(box3_t *bounds, vec3_t const *p);
box3_t Wow_EmptyBounds(void);
vec2_t Wow_McvtCoords(int index);
vec3_t Wow_McvtPoint(wowVec3_t pos, float const *heights, int index);
vec3_t Wow_TerrainFaceNormal(vec3_t const *a, vec3_t const *b, vec3_t const *c);
void Wow_AccumulateTerrainCellNormals(vec3_t normals[WOW_MCVT_COUNT], wowVec3_t pos, float const *heights, int x, int y);
void Wow_NormalizeTerrainNormals(vec3_t normals[WOW_MCVT_COUNT]);
void Wow_PushTerrainVertex(vertex_t *vertices, uint32_t *index, wowVec3_t pos, float const *heights, vec3_t const *normal, int height_index, color32_t color);
bool Wow_IsHole(uint16_t holes, int x, int y);
void Wow_AddTerrainCell(vertex_t *vertices, uint32_t *index, wowVec3_t pos, float const *heights, vec3_t const normals[WOW_MCVT_COUNT], int x, int y, color32_t const *mccv);
bool Wow_BarycentricHeight(float px, float py, float ax, float ay, float ah, float bx, float by, float bh, float cx, float cy, float ch, float *height);
bool Wow_HeightInCell(float const *heights, int row, int col, float fx, float fy, float *height);
bool Wow_TerrainHeightAtPoint(float sx, float sy, float *height);
void Wow_FlushSplats(void);
uint32_t Wow_PredictedLayer(uint16_t const pred_tex[8], uint32_t layer_count, int x, int y);
uint32_t Wow_AlphaSlotForTexture(uint32_t unique_texture_ids[4], uint32_t *unique_count, uint32_t texture_id);
uint32_t Wow_BuildUniqueTextureSlots(wowLayer_t const *layers, uint32_t layer_count, uint32_t slot_texture_ids[4]);
void Wow_DecodeAlphaLayer(uint8_t const *src, uint8_t const *src_end, uint32_t flags, uint32_t mcnk_flags, bool big_alpha, uint8_t out[WOW_ALPHA_TEXELS]);
void Wow_DecodeAlphaMaps(uint8_t const *mcal, uint32_t mcal_size, wowLayer_t const *layers, uint32_t layer_count, uint32_t mcnk_flags, uint8_t alpha[4][WOW_ALPHA_TEXELS]);
void Wow_AddAdtChunk(wowVec3_t pos, uint32_t alpha_index_x, uint32_t alpha_index_y, uint16_t holes, uint64_t no_effect_mask, uint8_t const alpha[4][WOW_ALPHA_TEXELS], wowLayer_t const *layers, uint32_t layer_count, char **textures, uint32_t num_textures, float const *heights, uint8_t const *normals, color32_t const *mccv, uint8_t const *mcsh);
void Wow_FreeStringList(char **strings, uint32_t count);
char **Wow_ParseStringBlock(uint8_t const *data, uint32_t size, uint32_t *out_count);
cstring_t Wow_StringRefFromOffsets(uint8_t const *blob, uint32_t blob_size, uint32_t const *offsets, uint32_t offset_count, uint32_t id);
vec3_t Wow_ObjectPoint(wowVec3_t p);
void Wow_InstanceMatrix(wowMapObjDef_t const *def, mat4_t *matrix);
void Wow_GroupPath(cstring_t root_path, uint32_t group_index, string_t out, uint32_t out_size);
cstring_t Wow_StringAt(cstring_t blob, uint32_t blob_size, uint32_t offset);
bool Wow_LoadWmoModel(wowWmoModel_t *model);
wowWmoModel_t *Wow_GetWmoModel(cstring_t path);
void Wow_AddWmoInstance(cstring_t path, wowMapObjDef_t const *def);
model_t *Wow_LoadDoodadModel(cstring_t path);
int Wow_DoodadBucketIndex(float coord);
void Wow_BucketDoodadInstance(wowDoodadInstance_t *instance);
void Wow_AddDoodadInstance(cstring_t model_path, wowDoodadDef_t const *def);
void Wow_AddGroundEffectInstance(cstring_t model_path, vec3_t origin, float angle);
void Wow_AddMarker(vertex_t *vertices, uint32_t *index, vec3_t p, float size, color32_t color);
vertex_t *Wow_AppendMarkers(vertex_t *old_vertices, uint32_t *old_count, uint8_t const *chunk, uint32_t size, uint8_t const *name_blob, uint32_t name_blob_size, uint32_t const *name_offsets, uint32_t name_offset_count, bool wmo);
vertex_t *Wow_AppendDoodadErrorMarkers(vertex_t *old_vertices, uint32_t *old_count, uint8_t const *chunk, uint32_t size);
void Wow_LoadAdt(uint8_t const *data, uint32_t size, uint32_t tile_x, uint32_t tile_y);
void Wow_LoadAdtFile(uint32_t tile_x, uint32_t tile_y);
uint8_t const *Wow_FindMainChunk(uint8_t const *data, uint32_t size, uint32_t *main_size);
void Wow_LoadWdtFlags(uint8_t const *data, uint32_t size);
bool Wow_LoadWdtTiles(uint8_t const *data, uint32_t size);
void Wow_LoadMapDbcFlags(void);
void Wow_LoadGroundEffectDBCs(void);
void Wow_FreeGrassScratch(void);
void Wow_LoadNearbyAdts(int center_x, int center_y);
void Wow_LoadCameraAdts(void);
void Wow_InitGrassShader(void);
void Wow_BuildGrassForChunk(wowAdtChunk_t *chunk, uint8_t const alpha[4][WOW_ALPHA_TEXELS], wowLayer_t const *layers, uint32_t layer_count, char **textures, uint32_t num_textures, uint64_t no_effect_mask);
void Wow_DrawGrass(void);
void Wow_EnsureHeightAtlas(void);
void Wow_UploadHeightAtlasChunk(uint32_t ix, uint32_t iy, float base_z, float const heights[WOW_MCVT_COUNT]);
void Wow_EnsureGrassCtrlTexture(void);
void Wow_UpdateGrassCtrlForChunk(uint32_t ix, uint32_t iy, uint64_t no_effect_mask, uint8_t const alpha[4][WOW_ALPHA_TEXELS], wowLayer_t const *layers, uint32_t layer_count, char **textures, uint32_t num_textures);
void Wow_EnsureCameraGrassMesh(void);
void Wow_FreeCameraGrassMesh(void);
void Wow_FixMocvAlpha(uint8_t *colors, uint32_t color_count,
                      wowWmoBatchDef_t const *batches, uint32_t batch_count,
                      uint32_t trans_batch_count,
                      color32_t amb, uint32_t mohd_flags, bool exterior);
void Wow_ComputeMoltContribution(wowWmoModel_t const *model, mat4_t const *matrix, vec3_t ref_pos, vec3_t *out);
void Wow_WmoDoodadLocalMatrix(wowWmoDoodadDef_t const *def, mat4_t *out);
void Wow_QueueWmoDoodads(wowWmoInstance_t const *wmo);
bool Wow_EntityInView(renderEntity_t const *entity);
bool Wow_TerrainChunkInRange(wowAdtChunk_t const *chunk);
bool Wow_WmoGroupInView(wowWmoGroup_t const *group, mat4_t const *matrix);
bool Wow_WmoContainsPoint(wowWmoModel_t const *model, mat4_t const *matrix, vec3_t point);
void Wow_BindWorldTexture(texture_t const *texture, uint32_t unit, texture_t const *bound[5], uint32_t *binds);
void Wow_DrawMinimap(rect_t const *screen);
float Wow_DayFraction(void);
void Wow_SunDirection(float day_frac, vec3_t *out);
bool Wow_MakeSplatVertex(float x, float y, vec2_t const *mins, float width, float height, color32_t color, vertex_t *vertex);
void Wow_AddSplatTriangle(vertex_t *vertices, uint32_t *count, vertex_t a, vertex_t b, vertex_t c, float max_height_delta);

#endif
