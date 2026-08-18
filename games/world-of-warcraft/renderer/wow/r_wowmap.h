#ifndef __r_wowmap_h__
#define __r_wowmap_h__

#include "renderer/r_local.h"
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
#define WOW_TERRAIN_DRAW_DISTANCE 700.0f
#define WOW_MINIMAP_WORLD_RADIUS 160.0f
#define WOW_MINIMAP_CAMERA_HEIGHT 4000.0f
#define WOW_DOODAD_BUCKET_SIZE 128.0f
#define WOW_DOODAD_BUCKETS 272
#define WOW_WORLD_COORD_OFFSET (32.0f * WOW_ADT_SIZE)
#define WOW_SPLAT_MAX_SUBDIVISIONS 16
#define WOW_SPLAT_MIN_SUBDIVISIONS 4
#define WOW_SPLAT_BATCHES 8
#define WOW_SPLAT_BATCH_VERTICES 4096
#define WOW_SPLAT_Z_BIAS 0.05f
#define WOW_SPLAT_MAX_HEIGHT_DELTA 3.0f
#define WOW_GRASS_DRAW_DISTANCE 220.0f
#define WOW_GRASS_FADE_START_DISTANCE 160.0f
#define WOW_GRASS_ROAD_COVERAGE_MIN 24
#define WOW_GRASS_CELL_STEP 1
#define WOW_GRASS_CELLS_PER_AXIS 8
#define WOW_GRASS_MAX_PLACEMENTS_PER_SAMPLE 8
#define WOW_GRASS_COVERAGE_MIN 32
#define WOW_GRASS_ALPHA_AXIS 8
#define WOW_GRASS_ALPHA_MAX 63
#define WOW_GRASS_ALPHA_TEXEL_MAX 255.0f
#define WOW_GRASS_DBC_DENSITY_MAX 16
#define WOW_GRASS_DBC_FIELD_COUNT 11
#define WOW_GRASS_DOODAD_FIELD_COUNT 3
#define WOW_GRASS_TEXTURE_LEGACY_DOODAD_FIELD 5
#define WOW_GRASS_TEXTURE_MODERN_DOODAD_FIELD 1
#define WOW_GRASS_TEXTURE_WEIGHT_FIELD 5
#define WOW_GRASS_TEXTURE_DENSITY_FIELD 9
#define WOW_GRASS_DOODAD_MODEL_FIELD 2
#define WOW_GRASS_DOODAD_LOGGED_IDS 65536
#define WOW_GRASS_DOODAD_SLOTS 4
#define WOW_GRASS_INVALID_DOODAD 0xFFFFFFFFU
#define WOW_GRASS_VERTICES_PER_CLUMP 12
#define WOW_GRASS_CELL_OFFSET 0.20f
#define WOW_GRASS_CELL_MARGIN 0.40f
#define WOW_GRASS_CLUMP_JITTER 0.45f
#define WOW_GRASS_COORD_EPSILON 0.001f
#define WOW_GRASS_Z_BIAS 0.02f
#define WOW_GRASS_FULL_CIRCLE 6.2831853f
#define WOW_GRASS_BLADE_HEIGHT_MIN 0.55f
#define WOW_GRASS_BLADE_HEIGHT_VARIATION 0.45f
#define WOW_GRASS_BLADE_WIDTH_MIN 0.30f
#define WOW_GRASS_BLADE_WIDTH_VARIATION 0.20f
#define WOW_GRASS_CROSS_ANGLE 1.5707963f
#define WOW_GRASS_CROSS_WIDTH_SCALE 0.85f
#define WOW_GRASS_CROSS_HEIGHT_SCALE 0.90f
#define WOW_GRASS_NORMAL_Z 0.10f

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

/* Camera-following static grass tile VBO */
#define WOW_GRASS_TILE_SIZE      480.0f
#define WOW_GRASS_BLADE_SLOTS    32768
#define WOW_GRASS_VERTS_PER_BLADE 12  /* triangle-list cross: 2 quads x 6 verts */

/* Phase 3 enable flag: camera-following mesh replaces per-instance draw.
   Set to 0 to fall back to the old Wow_AddGroundEffectInstance path. */
#define WOW_GRASS_CAMERA_MESH 1

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
    struct wowDoodadModel_s *next;
} wowDoodadModel_t;

typedef struct wowDoodadInstance_s {
    renderEntity_t entity;
    struct wowDoodadInstance_s *next;
    struct wowDoodadInstance_s *bucket_next;
} wowDoodadInstance_t;

typedef struct wowWmoBatch_s {
    LPBUFFER buffer;
    LPTEXTURE texture;
    DWORD num_vertices;
    struct wowWmoBatch_s *next;
} wowWmoBatch_t;

typedef struct wowWmoGroup_s {
    wowWmoBatch_t *batches;
    BOX3 bounds;
    BOOL has_bounds;
} wowWmoGroup_t;

typedef struct wowWmoModel_s {
    PATHSTR path;
    wowWmoGroup_t *groups;
    DWORD num_groups;
    BOOL loaded;
    struct wowWmoModel_s *next;
} wowWmoModel_t;

typedef struct wowWmoInstance_s {
    wowWmoModel_t *model;
    MATRIX4 matrix;
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
    WORD unk;
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
extern GLint wow_uAlphaOrigin;
extern GLint wow_uAlphaAtlasChunks;
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
extern GLint wow_uGrassTileSize;

BOOL Wow_PathHasExtension(LPCSTR path, LPCSTR extension);
void Wow_NormalizeMapPath(LPCSTR mapFileName, LPSTR out, DWORD out_size);
void Wow_SetMapNames(LPCSTR path);
DWORD Wow_Read32(BYTE const *p);
WORD Wow_Read16(BYTE const *p);
BOOL Wow_TagEquals(BYTE const *tag, LPCSTR reversed);
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
void Wow_AddTerrainCell(VERTEX *vertices, LPDWORD index, wowVec3_t pos, float const *heights, VECTOR3 const normals[WOW_MCVT_COUNT], int x, int y, COLOR32 color);
BOOL Wow_BarycentricHeight(float px, float py, float ax, float ay, float ah, float bx, float by, float bh, float cx, float cy, float ch, float *height);
BOOL Wow_HeightInCell(float const *heights, int row, int col, float fx, float fy, float *height);
BOOL Wow_TerrainHeightAtPoint(float sx, float sy, float *height);
void Wow_FlushSplats(void);
DWORD Wow_PredictedLayer(WORD const pred_tex[8], DWORD layer_count, int x, int y);
DWORD Wow_AlphaSlotForTexture(DWORD unique_texture_ids[4], DWORD *unique_count, DWORD texture_id);
DWORD Wow_BuildUniqueTextureSlots(wowLayer_t const *layers, DWORD layer_count, DWORD slot_texture_ids[4]);
void Wow_DecodeAlphaLayer(BYTE const *src, BYTE const *src_end, DWORD flags, DWORD mcnk_flags, BOOL big_alpha, BYTE out[WOW_ALPHA_TEXELS]);
void Wow_DecodeAlphaMaps(BYTE const *mcal, DWORD mcal_size, wowLayer_t const *layers, DWORD layer_count, DWORD mcnk_flags, BYTE alpha[4][WOW_ALPHA_TEXELS]);
void Wow_AddAdtChunk(wowVec3_t pos, DWORD alpha_index_x, DWORD alpha_index_y, WORD holes, uint64_t no_effect_mask, BYTE const alpha[4][WOW_ALPHA_TEXELS], wowLayer_t const *layers, DWORD layer_count, char **textures, DWORD num_textures, float const *heights, BYTE const *normals);
void Wow_FreeStringList(char **strings, DWORD count);
char **Wow_ParseStringBlock(BYTE const *data, DWORD size, LPDWORD out_count);
LPCSTR Wow_StringRefFromOffsets(BYTE const *blob, DWORD blob_size, DWORD const *offsets, DWORD offset_count, DWORD id);
VECTOR3 Wow_ObjectPoint(wowVec3_t p);
void Wow_InstanceMatrix(wowMapObjDef_t const *def, LPMATRIX4 matrix);
void Wow_GroupPath(LPCSTR root_path, DWORD group_index, LPSTR out, DWORD out_size);
LPCSTR Wow_StringAt(LPCSTR blob, DWORD blob_size, DWORD offset);
BOOL Wow_LoadWmoGroup(wowWmoModel_t *model, DWORD group_index, LPTEXTURE const *materials, DWORD material_count);
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
LPCSTR Wow_DbcString(BYTE const *string_block, DWORD string_size, DWORD offset);
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
BOOL Wow_EntityInView(renderEntity_t const *entity);
BOOL Wow_TerrainChunkInRange(wowAdtChunk_t const *chunk);
BOOL Wow_WmoGroupInView(wowWmoGroup_t const *group, LPCMATRIX4 matrix);
void Wow_BindWorldTexture(LPCTEXTURE texture, DWORD unit, LPCTEXTURE bound[5], LPDWORD binds);
void Wow_DrawMinimap(LPCRECT screen);
BOOL Wow_MakeSplatVertex(float x, float y, LPCVECTOR2 mins, float width, float height, COLOR32 color, LPVERTEX vertex);
void Wow_AddSplatTriangle(LPVERTEX vertices, LPDWORD count, VERTEX a, VERTEX b, VERTEX c, float max_height_delta);

#endif
