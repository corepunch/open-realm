#ifndef __r_wowmap_h__
#define __r_wowmap_h__

#include "renderer/r_local.h"
#include "common/ui_constants.h"
#include "common/wow_chunks.h"
#include <strings.h>
#include <stdlib.h>
#include <float.h>
#include <math.h>

#define WOW_WDT_TILES 64
#define WOW_MCVT_COUNT (9 * 9 + 8 * 8)
#define WOW_ADT_RADIUS 1
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
#define WOW_GRASS_WIND_DIRECTION_Y 0.51f        // sway XY direction, Y component; used as uGrassPhase.zw in the instanced shader
#define WOW_GRASS_ROAD_COVERAGE_MIN 24          // alpha [0-255]; cells with a road-layer alpha above this suppress grass
#define WOW_GRASS_CELL_STEP 1                   // stride over the 8×8 cell grid; 1 = every cell, 2 = every other (halves density)
#define WOW_GRASS_CELLS_PER_AXIS 8              // cells per MCNK axis; matches WoW's fixed 8×8 sub-cell layout
#define WOW_GRASS_MAX_PLACEMENTS_PER_SAMPLE 12  // max M2 instances per sampled cell; hard ceiling on the clumps formula
#define WOW_GRASS_COVERAGE_MIN 32               // alpha [0-255]; minimum coverage to spawn any grass in a cell at all
#define WOW_GRASS_ALPHA_AXIS 8                  // sample points per axis when mapping a cell coordinate to an alpha texel index
#define WOW_GRASS_ALPHA_MAX 63                  // max alpha texel index (8×8 = 64 entries, 0-based)
#define WOW_GRASS_ALPHA_TEXEL_MAX 255.0f        // float denominator to normalize a raw alpha byte to [0,1]
#define WOW_GRASS_DBC_DENSITY_MAX 24            // cap on GroundEffectTexture.dbc density field; prevents over-spawning on high-density records
#define WOW_GRASS_DBC_FIELD_COUNT 11            // total DWORD fields per GroundEffectTexture.dbc record
#define WOW_GRASS_DOODAD_FIELD_COUNT 3          // total DWORD fields per GroundEffectDoodad.dbc record (id, legacy_field, model_path)
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
    BOOL present;
} wowWdtTile_t;

typedef struct wowTextureCache_s {
    PATHSTR path;
    LPTEXTURE texture;
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
    LPMODEL model;
    MATRIX4 *matrices;
    INSTANCEBUFFER instances;
    DWORD count, capacity;
    /* WMO doodads are static — built once per ADT load into a persistent GPU buffer */
    MATRIX4 *wmo_matrices;
    INSTANCEBUFFER wmo_instances;
    DWORD wmo_count, wmo_capacity;
    BOOL can_instance;
    struct wowDoodadModel_s *next;
} wowDoodadModel_t;

typedef struct wowDoodadInstance_s {
    renderEntity_t entity;
    wowDoodadModel_t *group;
    struct wowDoodadInstance_s *next;
    struct wowDoodadInstance_s *bucket_next;
} wowDoodadInstance_t;

typedef struct {
    BYTE      type;        /* 0=OMNI 1=SPOT 2=DIRECT 3=AMBIENT */
    BYTE      use_atten;
    BYTE      pad[2];
    COLOR32   color;       /* BGRA in file */
    wowVec3_t position;    /* WMO local space */
    float     intensity;
    float     atten_start;
    float     atten_end;
    float     unk[4];
} wowWmoLight_t;  /* 48 bytes */

typedef struct wowWmoBatch_s {
    LPBUFFER buffer;
    LPTEXTURE texture;
    DWORD num_vertices;
    BOOL indoor;
    BYTE blend_mode;    /* MOMT blendMode: 0=Opaque 1=AlphaKey 2=Alpha 3=NoAlphaAdd 4=Add */
    BOOL transparent;   /* true when blend_mode >= 2 (requires GL_BLEND pass) */
    struct wowWmoBatch_s *next;
} wowWmoBatch_t;

typedef struct {
    WORD  start_vertex; /* first vertex index in model->portal_vertices */
    WORD  count;        /* number of vertices in this portal polygon */
    float plane[4];     /* (nx, ny, nz, d) in WMO local space */
} wowWmoPortal_t;  /* 20 bytes */

typedef struct {
    WORD  portal_index; /* index into model->portals */
    WORD  group_index;  /* group this portal connects to */
    int16_t side;       /* -1 or +1: which side the group is on */
    WORD  pad;
} wowWmoPortalRef_t;  /* 8 bytes */

typedef struct wowWmoGroup_s {
    wowWmoBatch_t *batches;
    BOX3 bounds;
    BOOL has_bounds;
    WORD portal_start;     /* MOGP +0x24: first entry in model->portal_refs */
    WORD portal_count;     /* MOGP +0x26: number of portal_refs for this group */
    COLOR32 group_amb;       /* MOGP replacement_for_header_color (BGRA→RGB) */
    BOOL    has_group_amb;   /* true when replacement_for_header_color was non-zero */
} wowWmoGroup_t;

typedef struct {
    char  name[20];  /* doodad set name, null-padded */
    DWORD start;     /* first MODD index in this set */
    DWORD count;     /* number of MODD entries */
    DWORD pad;
} wowWmoDoodadSet_t;  /* 32 bytes */

typedef struct {
    DWORD     name_flags;  /* bits 0-23 = byte offset into MODN blob; bits 24-31 = instance flags */
    wowVec3_t position;    /* WMO local space */
    float     quat[4];     /* (x, y, z, w) orientation in WMO local space */
    float     scale;
    COLOR32   color;       /* BGRA; color.a = MOLT index when flags bit 2 set */
} wowWmoDoodadDef_t;  /* 40 bytes */

typedef struct wowWmoModel_s {
    PATHSTR path;
    wowWmoGroup_t *groups;
    wowWmoBatch_t *batches;
    DWORD num_groups;
    DWORD num_batches;
    BOOL loaded;
    COLOR32 amb_color;   /* MOHD.ambColor: .r=R .g=G .b=B after BGRA swap */
    DWORD   mohd_flags;  /* bit 0x02=lighten_interiors, 0x04=skip_base_color */
    DWORD   n_lights;    /* MOHD.nLights, for MOLT */
    wowWmoDoodadSet_t *doodad_sets;
    DWORD              num_doodad_sets;
    wowWmoDoodadDef_t *doodad_defs;
    DWORD              num_doodad_defs;
    char              *doodad_name_blob;   /* raw MODN chunk bytes, null-terminated */
    DWORD              doodad_name_blob_size;
    wowWmoLight_t     *lights;             /* MOLT light array */
    DWORD              num_lights_parsed;  /* actual parsed count (n_lights = from MOHD header) */
    wowWmoPortal_t    *portals;            /* MOPT portal plane definitions */
    DWORD              num_portals;
    wowVec3_t         *portal_vertices;   /* MOPV portal polygon vertices */
    DWORD              num_portal_vertices;
    wowWmoPortalRef_t *portal_refs;       /* MOPR per-group portal references */
    DWORD              num_portal_refs;
    /* Per-doodad-def group pointer cache — filled once on first Wow_QueueWmoDoodads call
     * to avoid O(n) strcasecmp lookup on every render frame. num_doodad_defs entries. */
    wowDoodadModel_t **def_groups;
    /* Model-space bounding sphere from MOHD; used for whole-WMO early-out in precompute. */
    VECTOR3 bounds_center;
    float   bounds_radius;
    BOOL    has_bounds;
    struct wowWmoModel_s *next;
} wowWmoModel_t;

typedef struct wowWmoInstance_s {
    wowWmoModel_t *model;
    MATRIX4 matrix;
    WORD doodad_set;  /* MODF.doodadSet index into model->doodad_sets */
    struct wowWmoInstance_s *next;
} wowWmoInstance_t;

typedef struct wowAdtChunk_s {
    LPBUFFER buffer;
    LPBUFFER grass_buffer;
    LPTEXTURE textures[4];
    LPTEXTURE alpha_texture;
    DWORD alpha_index_x;
    DWORD alpha_index_y;
    DWORD num_vertices;
    DWORD num_grass_vertices;
    DWORD layer_count;
    wowVec3_t position;
    float heights[WOW_MCVT_COUNT];
    BOOL has_heights;
    BYTE mcsh[512];
    BOOL has_mcsh;
    BOX3 bounds;
    BOX3 grass_bounds;
    struct wowAdtChunk_s *next;
} wowAdtChunk_t;

typedef struct wowMap_s {
    wowWdtTile_t tiles[WOW_WDT_TILES][WOW_WDT_TILES];
    wowAdtChunk_t *chunks;
    wowTextureCache_t *textures;
    wowM2BoundsCache_t *m2_bounds;
    wowDoodadModel_t *doodad_models;
    wowDoodadInstance_t *doodads;
    wowDoodadInstance_t *doodad_buckets[WOW_DOODAD_BUCKETS][WOW_DOODAD_BUCKETS];
    wowDoodadInstance_t *ground_effects;
    wowWmoModel_t *wmo_models;
    wowWmoInstance_t *wmos;
    LPTEXTURE alpha_atlas_texture;
    LPTEXTURE height_atlas;      /* R32F 17x9-per-chunk height values */
    LPTEXTURE grass_ctrl;        /* RGBA8 per-cell suppression/density/effect */
    LPBUFFER  grass_tile_vbo;    /* immutable camera-following blade mesh */
    DWORD     grass_tile_nverts;
    float atlas_world_x;         /* world pos.x of atlas tile (iy=0) chunk */
    float atlas_world_y;         /* world pos.y of atlas tile (ix=0) chunk */
    BOOL  has_atlas_origin;
    LPBUFFER object_buffer;
    DWORD num_object_vertices;
    DWORD num_adts;
    DWORD num_chunks;
    DWORD num_grass_chunks;
    DWORD num_grass_vertices;
    DWORD num_doodads;
    DWORD num_doodad_instances;
    DWORD num_ground_effects;
    DWORD num_doodad_models;
    DWORD num_missing_doodad_models;
    DWORD num_filedata_doodads;
    DWORD num_wmos;
    DWORD num_wmo_models;
    DWORD num_wmo_batches;
    DWORD num_missing_wmos;
    DWORD wdt_flags;
    BOOL use_weighted_blend;
    BOOL has_adt_window;
    int adt_center_x;
    int adt_center_y;
    DWORD layer_histogram[5];
    int alpha_origin_x;
    int alpha_origin_y;
    PATHSTR map_dir;
    char map_name[128];
    char minimap_hash[WOW_WDT_TILES][WOW_WDT_TILES][WOW_MINIMAP_HASH_LENGTH + 1];
    LPTEXTURE minimap_tiles[WOW_WDT_TILES][WOW_WDT_TILES];
    BYTE minimap_warned[WOW_WDT_TILES][WOW_WDT_TILES];
    BOOL wmo_doodads_built; /* WMO doodad persistent instance buffers are ready */
} wowMap_t;

typedef struct {
    DWORD flags;
    DWORD async_id;
} wowWdtMainEntry_t;

typedef struct {
    DWORD texture_id;
    DWORD flags;
    DWORD offset_in_mcal;
    DWORD effect_id;
} wowLayer_t;

typedef struct {
    DWORD name_id;
    DWORD unique_id;
    wowVec3_t position;
    wowVec3_t rotation;
    WORD scale;
    WORD flags;
} wowDoodadDef_t;

typedef struct {
    wowVec3_t min;
    wowVec3_t max;
} wowBox_t;

typedef struct {
    DWORD name_id;
    DWORD unique_id;
    wowVec3_t position;
    wowVec3_t rotation;
    wowBox_t extents;
    WORD flags;
    WORD doodad_set;
    WORD name_set;
    WORD scale;
} wowMapObjDef_t;

typedef struct {
    BYTE flags;
    BYTE material_id;
} wowWmoPoly_t;

typedef struct {
    SHORT box_min[3];
    SHORT box_max[3];
    DWORD first_index;
    WORD num_indices;
    WORD first_vertex;
    WORD last_vertex;
    BYTE flags;
    BYTE material_id;
} wowWmoBatchDef_t;

typedef struct {
    float u, v;
} wowVec2_t;

typedef struct {
    DWORD id;
    DWORD date_stamp;
    DWORD continent_id;
    DWORD zone_id;
    DWORD texture_id;
    DWORD doodad_id[WOW_GRASS_DOODAD_SLOTS];
    DWORD weight[WOW_GRASS_DOODAD_SLOTS];
    DWORD density;
    DWORD sound;
} wowGroundEffectTexture_t;

typedef struct {
    DWORD id;
    DWORD legacy_field;
    PATHSTR model_path;
} wowGroundEffectDoodad_t;

extern wowMap_t wow_world;
extern LPSHADER wow_terrain_shader;
extern LPSHADER wow_grass_shader;
extern GLint wow_uTexture0;
extern GLint wow_uTexture1;
extern GLint wow_uTexture2;
extern GLint wow_uTexture3;
extern GLint wow_uAlphaTexture;
extern GLint wow_uUseWeightedBlend;
extern GLint wow_uSingleTexture;
extern GLint wow_uWmoIndoor;
extern GLint wow_uWmoAmbient;
extern GLint wow_uWmoLightAdd;
extern GLint wow_uWmoBlendMode;
extern GLint wow_uAlphaOrigin;
extern GLint wow_uAlphaAtlasChunks;
extern GLint wow_uFogEnable;
extern GLint wow_uFogColor;
extern GLint wow_uFogParams;
extern GLint wow_uFogCamera;
extern GLint wow_uSunDir;
extern GLint wow_uSunAmbient;
extern GLint wow_uSunDiffuse;
extern GLint wow_uGrassTime;
extern GLint wow_uGrassCameraOrigin;
extern GLint wow_uGrassDrawDistance;
extern GLint wow_uGrassFadeStartDistance;
/* Height atlas uniforms (terrain + grass) */
extern GLint wow_uHeightAtlas;
extern GLint wow_uAtlasOriginWorld;
extern GLint wow_uAtlasChunkSize;
extern GLint wow_uAtlasUnitSize;
/* Grass control texture uniforms */
extern GLint wow_uGrassCtrl;
extern GLint wow_uCtrlOriginWorld;
extern GLint wow_uCtrlCellSize;
/* Camera-following grass tile uniforms */
extern GLint wow_uCameraXZ;
extern GLint wow_uGrassSlotSpacing;

BOOL Wow_PathHasExtension(LPCSTR path, LPCSTR extension);
void Wow_NormalizeMapPath(LPCSTR mapFileName, LPSTR out, DWORD out_size);
void Wow_SetMapNames(LPCSTR path);
BOOL Wow_LoadMinimapTranslations(void);
DWORD Wow_Read32(BYTE const *p);
WORD Wow_Read16(BYTE const *p);
void Wow_FreeChunks(void);
void Wow_FreeWmoModels(void);
void Wow_FreeWmoInstances(void);
void Wow_FreeDoodadInstances(void);
void Wow_ClearLoadedAdts(void);
void Wow_FreeWorld(void);
void Wow_ShutdownWorldShaders(void);
LPTEXTURE Wow_LoadTexture(LPCSTR path);
BOOL Wow_ReadM2RadiusFromPath(LPCSTR path, float *radius);
BOOL Wow_CopyModelPathFallback(LPCSTR path, LPSTR out, DWORD out_size);
float Wow_LoadM2BoundsRadius(LPCSTR path);
LPTEXTURE Wow_CreateAlphaTexture(BYTE const alpha[4][WOW_ALPHA_TEXELS]);
void Wow_EnsureAlphaAtlasTexture(void);
void Wow_UploadAlphaAtlasChunk(DWORD index_x, DWORD index_y, BYTE const alpha[4][WOW_ALPHA_TEXELS]);
void Wow_InitTerrainShader(void);
COLOR32 Wow_Color(BYTE r, BYTE g, BYTE b, BYTE a);
VERTEX Wow_Vertex(float x, float y, float z, float u, float v, COLOR32 color);
void Wow_AddBoundsPoint(LPBOX3 bounds, LPCVECTOR3 p);
BOX3 Wow_EmptyBounds(void);
VECTOR3 Wow_WorldPoint(float x, float y, float z);
VECTOR2 Wow_McvtCoords(int index);
VECTOR3 Wow_McvtPoint(wowVec3_t pos, float const *heights, int index);
VECTOR3 Wow_TerrainFaceNormal(LPCVECTOR3 a, LPCVECTOR3 b, LPCVECTOR3 c);
void Wow_AccumulateTerrainCellNormals(VECTOR3 normals[WOW_MCVT_COUNT], wowVec3_t pos, float const *heights, int x, int y);
void Wow_NormalizeTerrainNormals(VECTOR3 normals[WOW_MCVT_COUNT]);
void Wow_PushTerrainVertex(VERTEX *vertices, LPDWORD index, wowVec3_t pos, float const *heights, LPCVECTOR3 normal, int height_index, COLOR32 color);
BOOL Wow_IsHole(WORD holes, int x, int y);
void Wow_AddTerrainCell(VERTEX *vertices, LPDWORD index, wowVec3_t pos, float const *heights, VECTOR3 const normals[WOW_MCVT_COUNT], int x, int y, COLOR32 const *mccv);
BOOL Wow_BarycentricHeight(float px, float py, float ax, float ay, float ah, float bx, float by, float bh, float cx, float cy, float ch, float *height);
BOOL Wow_HeightInCell(float const *heights, int row, int col, float fx, float fy, float *height);
BOOL Wow_TerrainHeightAtPoint(float sx, float sy, float *height);
void Wow_FlushSplats(void);
DWORD Wow_PredictedLayer(WORD const pred_tex[8], DWORD layer_count, int x, int y);
DWORD Wow_AlphaSlotForTexture(DWORD unique_texture_ids[4], DWORD *unique_count, DWORD texture_id);
DWORD Wow_BuildUniqueTextureSlots(wowLayer_t const *layers, DWORD layer_count, DWORD slot_texture_ids[4]);
void Wow_DecodeAlphaLayer(BYTE const *src, BYTE const *src_end, DWORD flags, DWORD mcnk_flags, BOOL big_alpha, BYTE out[WOW_ALPHA_TEXELS]);
void Wow_DecodeAlphaMaps(BYTE const *mcal, DWORD mcal_size, wowLayer_t const *layers, DWORD layer_count, DWORD mcnk_flags, BYTE alpha[4][WOW_ALPHA_TEXELS]);
void Wow_AddAdtChunk(wowVec3_t pos, DWORD alpha_index_x, DWORD alpha_index_y, WORD holes, uint64_t no_effect_mask, BYTE const alpha[4][WOW_ALPHA_TEXELS], wowLayer_t const *layers, DWORD layer_count, char **textures, DWORD num_textures, float const *heights, BYTE const *normals, COLOR32 const *mccv, BYTE const *mcsh);
void Wow_FreeStringList(char **strings, DWORD count);
char **Wow_ParseStringBlock(BYTE const *data, DWORD size, LPDWORD out_count);
LPCSTR Wow_StringRefFromOffsets(BYTE const *blob, DWORD blob_size, DWORD const *offsets, DWORD offset_count, DWORD id);
VECTOR3 Wow_ObjectPoint(wowVec3_t p);
void Wow_InstanceMatrix(wowMapObjDef_t const *def, LPMATRIX4 matrix);
void Wow_GroupPath(LPCSTR root_path, DWORD group_index, LPSTR out, DWORD out_size);
LPCSTR Wow_StringAt(LPCSTR blob, DWORD blob_size, DWORD offset);
BOOL Wow_LoadWmoModel(wowWmoModel_t *model);
wowWmoModel_t *Wow_GetWmoModel(LPCSTR path);
void Wow_AddWmoInstance(LPCSTR path, wowMapObjDef_t const *def);
LPMODEL Wow_LoadDoodadModel(LPCSTR path);
int Wow_DoodadBucketIndex(float coord);
void Wow_BucketDoodadInstance(wowDoodadInstance_t *instance);
void Wow_AddDoodadInstance(LPCSTR model_path, wowDoodadDef_t const *def);
void Wow_AddGroundEffectInstance(LPCSTR model_path, VECTOR3 origin, float angle);
void Wow_AddMarker(VERTEX *vertices, LPDWORD index, VECTOR3 p, float size, COLOR32 color);
VERTEX *Wow_AppendMarkers(VERTEX *old_vertices, LPDWORD old_count, BYTE const *chunk, DWORD size, BYTE const *name_blob, DWORD name_blob_size, DWORD const *name_offsets, DWORD name_offset_count, BOOL wmo);
VERTEX *Wow_AppendDoodadErrorMarkers(VERTEX *old_vertices, LPDWORD old_count, BYTE const *chunk, DWORD size);
void Wow_LoadAdt(BYTE const *data, DWORD size, DWORD tile_x, DWORD tile_y);
void Wow_LoadAdtFile(DWORD tile_x, DWORD tile_y);
BYTE const *Wow_FindMainChunk(BYTE const *data, DWORD size, LPDWORD main_size);
void Wow_LoadWdtFlags(BYTE const *data, DWORD size);
BOOL Wow_LoadWdtTiles(BYTE const *data, DWORD size);
int Wow_AdtIndexForWorldCoord(float coord);
void Wow_LoadMapDbcFlags(void);
void Wow_LoadGroundEffectDBCs(void);
void Wow_FreeGrassScratch(void);
void Wow_LoadNearbyAdts(int center_x, int center_y);
void Wow_LoadCameraAdts(void);
void Wow_InitGrassShader(void);
void Wow_BuildGrassForChunk(wowAdtChunk_t *chunk, BYTE const alpha[4][WOW_ALPHA_TEXELS], wowLayer_t const *layers, DWORD layer_count, char **textures, DWORD num_textures, uint64_t no_effect_mask);
void Wow_DrawGrass(void);
void Wow_EnsureHeightAtlas(void);
void Wow_UploadHeightAtlasChunk(DWORD ix, DWORD iy, float base_z, float const heights[WOW_MCVT_COUNT]);
void Wow_EnsureGrassCtrlTexture(void);
void Wow_UpdateGrassCtrlForChunk(DWORD ix, DWORD iy, uint64_t no_effect_mask, BYTE const alpha[4][WOW_ALPHA_TEXELS], wowLayer_t const *layers, DWORD layer_count, char **textures, DWORD num_textures);
void Wow_EnsureCameraGrassMesh(void);
void Wow_FreeCameraGrassMesh(void);
void Wow_FixMocvAlpha(BYTE *colors, DWORD color_count,
                      wowWmoBatchDef_t const *batches, DWORD batch_count,
                      DWORD trans_batch_count,
                      COLOR32 amb, DWORD mohd_flags, BOOL exterior);
void Wow_ComputeMoltContribution(wowWmoModel_t const *model, LPCMATRIX4 matrix, VECTOR3 ref_pos, VECTOR3 *out);
void Wow_WmoDoodadLocalMatrix(wowWmoDoodadDef_t const *def, LPMATRIX4 out);
void Wow_QueueWmoDoodads(wowWmoInstance_t const *wmo);
BOOL Wow_EntityInView(renderEntity_t const *entity);
BOOL Wow_TerrainChunkInRange(wowAdtChunk_t const *chunk);
BOOL Wow_WmoGroupInView(wowWmoGroup_t const *group, LPCMATRIX4 matrix);
BOOL Wow_WmoContainsPoint(wowWmoModel_t const *model, LPCMATRIX4 matrix, VECTOR3 point);
void Wow_BindWorldTexture(LPCTEXTURE texture, DWORD unit, LPCTEXTURE bound[5], LPDWORD binds);
void Wow_DrawMinimap(LPCRECT screen);
FLOAT Wow_DayFraction(void);
void Wow_SunDirection(FLOAT day_frac, LPVECTOR3 out);
BOOL Wow_MakeSplatVertex(float x, float y, LPCVECTOR2 mins, float width, float height, COLOR32 color, LPVERTEX vertex);
void Wow_AddSplatTriangle(LPVERTEX vertices, LPDWORD count, VERTEX a, VERTEX b, VERTEX c, float max_height_delta);

#endif
