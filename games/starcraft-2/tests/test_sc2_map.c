/*
 * test_sc2_map.c - StarCraft II map fixture coverage.
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "common.h"
#include "games/starcraft-2/common/sc2_map.h"
#include "test.h"
#include "games/starcraft-2/renderer/sc2/r_sc2map.h"
#include "games/starcraft-2/renderer/sc2/sc2_shadow.h"
#include "games/starcraft-2/renderer/sc2/r_sc2_ramps.h"
#include "games/starcraft-2/renderer/m3/r_m3.h"
#include "games/warcraft-3/renderer/w3m/r_terrain_layers.h"

#ifndef TEST_SC2_MPQ
#define TEST_SC2_MPQ "build/tests/test-sc2.SC2Maps"
#endif

#define TEST_SC2_SRC_DIR "games/starcraft-2/tests/resources-src"
#define TEST_SC2_TINY_DIR TEST_SC2_SRC_DIR "/Maps/Test/Tiny.SC2Map"
#define TEST_SC2_SHORT_TERRAIN_DIMENSIONS 0
#define TEST_SC2_ZERO_TERRAIN_DIMENSIONS  1
#define TEST_SC2_HUGE_TERRAIN_DIMENSIONS  2

static bool sc2_tests_initialized;
static uint32_t short_terrain_dimensions;

static float test_grid_height(void const * data, uint32_t x, uint32_t y) {
    float const * heights = data;

    return heights[x + y * 3];
}

/* Grid derivatives stay smooth regardless of render-cell triangulation or omitted cliff cells. */
TEST(sc2_map, shared_grid_normals) {
    float flat[9] = {0};
    float slope[9] = {0,1,2, 0,1,2, 0,1,2};
    terrainNormals_t grid = { flat, test_grid_height, 3, 3, 1.0f };
    vector3_t normal = R_TerrainGridNormal(&grid, 1, 1);

    T_FEQ(normal.x, 0.0f, 0.0001f); T_FEQ(normal.y, 0.0f, 0.0001f); T_FEQ(normal.z, 1.0f, 0.0001f);
    grid.data = slope; normal = R_TerrainGridNormal(&grid, 1, 1);
    T_FEQ(normal.x, -0.707107f, 0.0001f); T_FEQ(normal.y, 0.0f, 0.0001f); T_FEQ(normal.z, 0.707107f, 0.0001f);
}

/* Camera-only spatial filtering suppresses narrow depressions without changing exact terrain queries. */
TEST(sc2_map, camera_height_blurs_narrow_depressions) {
    uint32_t const side = 17, count = side * side;
    sc2MapHeightMap_t *layer = MemAlloc(sizeof(*layer) + count * sizeof(*layer->data));
    sc2Map_t map = { .MapInfo = { .width = 16, .height = 16 }, .cell_size = 1.0f, .t3HeightMap = layer };
    uint32_t i;

    memset(layer, 0, sizeof(*layer) + count * sizeof(*layer->data));
    layer->width = side; layer->height = side;
    for (i = 0; i < count; i++) layer->data[i].height = 11;
    T_FEQ(sc2_map_broad_height_at_point(&map, 8.0f, 8.0f), 10.0f, 0.001f);
    layer->data[8 + 8 * side].height = 1;
    T_FEQ(sc2_map_height_at_point(&map, 8.0f, 8.0f), 0.0f, 0.001f);
    T_FEQ(sc2_map_broad_height_at_point(&map, 8.0f, 8.0f), 10.0f, 0.001f);
    MemFree(layer);
}

TEST(sc2_map, flying_unit_height_is_terrain_relative) {
    T_FEQ(sc2_unit_world_height(0.4f, 3.75f, true), 4.15f, 0.001f);
    T_FEQ(sc2_unit_world_height(0.4f, 3.75f, false), 0.4f, 0.001f);
}

TEST(sc2_map, ramp_join_matches_ground_triangles) {
    float height[] = { 0, 0, 0, 4 };
    /* A diagonal cliff lip at the cell center lies on the emitted 00--11 edge. */
    T_FEQ(r_sc2_ground_triangle_height(height, (vector2_t){0.5f,0.5f}), 2, 0.0001f);
    T_FEQ(r_sc2_ground_triangle_height(height, (vector2_t){0.75f,0.5f}), 2, 0.0001f);
    T_FEQ(r_sc2_ground_triangle_height(height, (vector2_t){0.5f,0.75f}), 2, 0.0001f);
}

TEST(sc2_map, ramp_footprints_select_authored_straight_and_diagonal_meshes) {
    /* TRaynor01's actual ramp boxes, with only their four outside tier samples needed here. */
    sc2MapSyncCliffLevel_t *grid = calloc(1, sizeof(*grid) + 136*160*sizeof(uint16_t));
    grid->width = 136; grid->height = 160;
    FOR_LOOP(i, 136*160) grid->data[i] = 64;
    sc2Map_t map = { .t3SyncCliffLevel = grid };
    sc2Ramp_t ramp = { .lo = 1, .hi = 2,
        .mid = { .up = {0,1}, .right = {1,0}, .center = {102,34}, .width = 2, .height = 2 },
        .edge = { { .up = {0,1}, .right = {1,0}, .center = {99,34}, .width = 1, .height = 2 },
            { .up = {0,1}, .right = {1,0}, .center = {105,34}, .width = 1, .height = 2 } } };
    grid->data[98+36*136] = grid->data[106+36*136] = 128;
    sc2RampPiece_t left = r_sc2_ramp_piece(&map, &ramp, 0), right = r_sc2_ramp_piece(&map, &ramp, 1);
    T_STREQ(left.config, "BQRC"); T_EQ(left.rotation, 0);
    T_STREQ(right.config, "BCRQ"); T_EQ(right.rotation, 1);
    T_FEQ(left.bounds.max.y-left.bounds.min.y, 4, 0.001f);
    map.t3Terrain.ramps = &ramp; ARRAY_COUNT(map.t3Terrain.ramps) = 1;
    T_ASSERT(r_sc2_ramp_covers_ground(&map, (vector2_t){99,34}));
    T_ASSERT(!r_sc2_ramp_covers_ground(&map, (vector2_t){101,34}));
    ramp.mid = (sc2RampBox_t){ .up = {-0.7071068f,-0.7071068f}, .right = {-0.7071068f,0.7071068f}, .center = {50,90}, .width = 2.828427f, .height = 2.828427f };
    ramp.edge[0] = (sc2RampBox_t){ .up = {0,-1}, .right = {-1,0}, .center = {52,88}, .width = 2, .height = 2 };
    ramp.edge[1] = (sc2RampBox_t){ .up = {0,-1}, .right = {-1,0}, .center = {48,92}, .width = 2, .height = 2 };
    left = r_sc2_ramp_piece(&map, &ramp, 0); right = r_sc2_ramp_piece(&map, &ramp, 1);
    T_STREQ(left.config, "BQQR"); T_EQ(left.rotation, 1);
    /* This cyclic configuration resolves to the archive's QBRQ orientation, not lexicographic BRQQ. */
    T_STREQ(right.config, "BRQQ"); T_EQ(right.rotation, 3);
    /* BQQR is L-shaped: its NE 2x2 block is absent. After rotation, keep only the NW block. */
    T_ASSERT(r_sc2_ramp_covers_ground(&map, (vector2_t){50.5f,87.5f}));
    T_ASSERT(r_sc2_ramp_covers_ground(&map, (vector2_t){52.5f,89.5f}));
    T_ASSERT(!r_sc2_ramp_covers_ground(&map, (vector2_t){51.5f,88.5f}));
    T_ASSERT(r_sc2_ramp_covers_ground(&map, (vector2_t){53,87}));
    T_ASSERT(!r_sc2_ramp_covers_ground(&map, (vector2_t){51,89}));
    free(grid);
}

TEST(sc2_map, cliff_weld_requires_matching_height_and_normal_hemisphere) {
    vertex_t base = {.position={1,2,3},.normal={1,0,0}};
    vertex_t seam = {.position={1,2,3},.normal={0.5f,0.5f,0}};
    vertex_t stacked = {.position={1,2,4},.normal={1,0,0}};
    vertex_t opposed = {.position={1,2,3},.normal={-1,0,0}};
    vertex_t zero = {.position={1,2,3},.normal={0,0,0}};

    T_ASSERT(R_CliffWeldCompatible(&base, 1, &seam, 2, 0.001f));
    T_ASSERT(!R_CliffWeldCompatible(&base, 1, &seam, 1, 0.001f));
    T_ASSERT(!R_CliffWeldCompatible(&base, 1, &stacked, 2, 0.001f));
    T_ASSERT(!R_CliffWeldCompatible(&base, 1, &opposed, 2, 0.001f));
    T_ASSERT(!R_CliffWeldCompatible(&base, 1, &zero, 2, 0.001f));
}

TEST(sc2_map, m3_division_faces_pack_into_model_ranges) {
    uint16_t a[] = {0,1,2}, b[] = {2,3,0}, indices[6];
    m3Divisions_t divisions[2] = {{.facesNum=3,.faces=a}, {.facesNum=3,.faces=b}};
    T_EQ((int)m3_pack_division_faces(divisions, 2, indices), 6);
    T_EQ((int)divisions[0].indexofs, 0); T_EQ((int)divisions[1].indexofs, 6);
    T_EQ(indices[0], 0); T_EQ(indices[3], 2); T_EQ(indices[5], 0);
}

TEST(sc2_map, hard_tile_matrix_maps_prism_to_authored_surface) {
    sc2MapHardTile_t tile = {
        .position = { 10, 20, 3 },
        .normal = { 0,0,1 },
        .start = { -2,0,0 },
        .end = { 2,0,0 },
        .scale = { 1.5f, 1 },
    };
    matrix4_t matrix;
    vector3_t start_left, end_right, long_end, width_edge, top;

    r_sc2_hard_tile_matrix(&tile, &matrix);
    start_left = Matrix4_multiply_vector3(&matrix, &(vector3_t){-.5f,-.5f,1});
    end_right = Matrix4_multiply_vector3(&matrix, &(vector3_t){.5f,.5f,1});
    long_end = Matrix4_multiply_vector3(&matrix, &(vector3_t){0,.5f,1});
    width_edge = Matrix4_multiply_vector3(&matrix, &(vector3_t){.5f,0,1});
    top = Matrix4_multiply_vector3(&matrix, &(vector3_t){0,0,1});
    T_FEQ(start_left.x, 8, .0001f); T_FEQ(start_left.y, 21.5f, .0001f); T_FEQ(start_left.z, 3, .0001f);
    T_FEQ(end_right.x, 12, .0001f); T_FEQ(end_right.y, 18.5f, .0001f); T_FEQ(end_right.z, 3, .0001f);
    T_FEQ(long_end.x, 12, .0001f); T_FEQ(long_end.y, 20, .0001f);
    T_FEQ(width_edge.x, 10, .0001f); T_FEQ(width_edge.y, 18.5f, .0001f);
    T_FEQ(top.x, 10, .0001f); T_FEQ(top.y, 20, .0001f); T_FEQ(top.z, 3, .0001f);
}

/* The old road max(authored Z, terrain+.05) hid rings at terrain+.02. */
TEST(sc2_map, hard_tile_surface_matches_terrain_below_selection) {
    vertex_t ground[] = {
        {.position={0,0,2.5f}, .normal={0,0,1}},
        {.position={1,0,2.5f}, .normal={0,0,1}},
        {.position={1,1,2.5f}, .normal={0,0,1}} }, road[3], out[18];
    memcpy(road, ground, sizeof(road));
    FOR_LOOP(i, 3) road[i].position.z = 3;
    uint32_t n = r_sc2_clip_road(road, ground, out);
    T_EQ(n, 3);
    FOR_LOOP(i, n) T_FEQ(out[i].position.z, 2.5f, .0001f);
    FOR_LOOP(i, 3) ground[i].position.z = 3.08f;
    n = r_sc2_clip_road(road, ground, out);
    T_EQ(n, 3);
    FOR_LOOP(i, n) T_FEQ(out[i].position.z, 3.08f, .0001f);
}

/* A wide road crosses the diagonal of a non-planar cell; UV and coverage survive the cut. */
TEST(sc2_map, hard_tile_clips_at_terrain_diagonal) {
    vertex_t road[] = {
        {.position={-1,-1,9}, .texcoord={-1,-1}},
        {.position={3,-1,9}, .texcoord={3,-1}},
        {.position={-1,3,9}, .texcoord={-1,3}} };
    vertex_t corners[] = {
        {.position={0,0,0}, .normal={0,0,1}}, {.position={1,0,0}, .normal={0,0,1}},
        {.position={1,1,4}, .normal={0,0,1}}, {.position={0,1,0}, .normal={0,0,1}} };
    float area = 0;
    FOR_LOOP(tri, 2) {
        vertex_t ground[] = { corners[0], corners[tri+1], corners[tri+2] }, out[18];
        uint32_t n = r_sc2_clip_road(road, ground, out);
        T_EQ(n, r_sc2_clip_road(road, ground, NULL)); T_ASSERT(n >= 3);
        for (uint32_t i = 0; i < n; i += 3) {
            vector3_t center = {0};
            area += fabsf(r_sc2_road_side(out[i].position, out[i+1].position, out[i+2].position)) * .5f;
            FOR_LOOP(j, 3) {
                vertex_t v = out[i+j];
                T_FEQ(v.texcoord.x, v.position.x, .0001f); T_FEQ(v.texcoord.y, v.position.y, .0001f);
                T_FEQ(v.position.z, 4*MIN(v.position.x,v.position.y), .0001f);
                center = Vector3_add(&center, &v.position);
            }
            center = Vector3_scale(&center, 1.0f/3);
            T_FEQ(center.z, 4*MIN(center.x,center.y), .0001f);
        }
    }
    T_FEQ(area, 1, .0001f);
    FOR_LOOP(i, 3) road[i].position.x += 10;
    T_EQ(r_sc2_clip_road(road, corners, NULL), 0);
    FOR_LOOP(i, 3) road[i] = corners[0];
    T_EQ(r_sc2_clip_road(road, corners, NULL), 0);
}

/* Cliff M3s are two-sided and can use the opposite winding from grid terrain. */
TEST(sc2_map, road_covers_cliff_top_at_bridge_approach) {
    vertex_t road[] = {
        {.position={0,0,8}}, {.position={2,0,8}}, {.position={2,2,8}} };
    vertex_t cliff[] = {
        {.position={2,2,7.8f}, .normal={0,0,1}},
        {.position={2,0,8}, .normal={0,0,1}},
        {.position={0,0,8}, .normal={0,0,1}} }, out[18];
    uint32_t n = r_sc2_clip_road(road, cliff, out);
    T_EQ(n, 3);
    float area = 0;
    for (uint32_t i = 0; i < n; i += 3)
        area += fabsf(r_sc2_road_side(out[i].position, out[i+1].position, out[i+2].position)) * .5f;
    T_FEQ(area, 2, .0001f);
}

TEST(sc2_map, road_cliff_depth_clips_canyon_wall) {
    sc2RoadTri_t road = { .verts = {
        {.position={0,0,8}}, {.position={2,0,8}}, {.position={2,2,8}} }, .depth = .6f };
    vertex_t cliff[] = {
        {.position={0,0,8}}, {.position={2,0,8}}, {.position={2,2,6}} }, out[54];
    uint32_t n = r_sc2_clip_road_cliff(&road, cliff, out);
    T_ASSERT(n >= 3); T_EQ(n, r_sc2_clip_road_cliff(&road, cliff, NULL));
    float area = 0;
    for (uint32_t i = 0; i < n; i += 3)
        area += fabsf(r_sc2_road_side(out[i].position, out[i+1].position, out[i+2].position)) * .5f;
    /* Only the top 0.6 of the two-unit slope receives the road, with no whole-triangle holes. */
    T_FEQ(area, 1.02f, .0001f);
    FOR_LOOP(i, n) {
        T_ASSERT(out[i].position.z >= 7.4f-.0001f);
        T_FEQ(out[i].position.z, 8-out[i].position.y, .0001f);
    }
    FOR_LOOP(i, 3) cliff[i].position.z = 2;
    T_EQ(r_sc2_clip_road_cliff(&road, cliff, out), 0);
    FOR_LOOP(i, 3) cliff[i].position.z = 10;
    T_EQ(r_sc2_clip_road_cliff(&road, cliff, out), 0);
    road.verts[1] = road.verts[0];
    T_EQ(r_sc2_clip_road_cliff(&road, cliff, out), 0);
}

static uint32_t listed_count;
static PATHSTR listed_map;

void Key_Init(void) {
}

void Key_WriteBindings(FILE *file) {
    (void)file;
}

void Cmd_ForwardToServer(cstring_t text) {
    (void)text;
}

void CL_SetGameplayBindings(void) {
}

void CL_Connect(cstring_t host, unsigned short port) { (void)host; (void)port; }

void CL_BeginLoadingMap(cstring_t mapName) {
    (void)mapName;
}

void CL_Shutdown(void) {
}

void SV_Map(cstring_t pFilename) {
    (void)pFilename;
}

bool SV_GetSaveMap(cstring_t name, string_t map, uint32_t map_size) {
    (void)name; (void)map; (void)map_size;
    return false;
}

bool SV_LoadGame(cstring_t name, cstring_t map) {
    (void)name; (void)map;
    return false;
}

void SV_Shutdown(void) {
}

void Sys_Quit(void) {
}

void PF_Sleep(uint32_t msec) {
    (void)msec;
}

static void setup_sc2_tests(void) {
    if (sc2_tests_initialized) {
        return;
    }

    cstring_t argv[] = { "test_sc2", "-config", "" };
    Com_Init(3, argv);
    T_ASSERT(FS_AddArchive(TEST_SC2_MPQ) != NULL);
    sc2_tests_initialized = true;
}

static void use_sc2_fs_host(void) {
    SC2_MapSetHost(&(sc2MapHost_t){
        .read_file = FS_ReadFile,
        .free_file = FS_FreeFile,
        .mem_alloc = MemAlloc,
        .mem_free = MemFree,
    });
}

static handle_t read_test_disk_path(cstring_t filename, uint32_t * size) {
    FILE *file;
    long file_size;
    uint8_t * data;
    struct stat st;

    if (size) *size = 0;
    if (!filename || !*filename)
        return NULL;
    if (stat(filename, &st) != 0 || !S_ISREG(st.st_mode))
        return NULL;
    file = fopen(filename, "rb");
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = MemAlloc(file_size ? file_size : 1);
    if (!data) {
        fclose(file);
        return NULL;
    }
    if (file_size > 0 && fread(data, 1, file_size, file) != (size_t)file_size) {
        MemFree(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    if (size) *size = (uint32_t)file_size;
    return data;
}

static void normalize_disk_path(string_t path) {
    if (!path) return;
    for (string_t p = path; *p; p++) {
        if (*p == '\\') *p = '/';
    }
}

static handle_t read_test_disk_file(cstring_t filename, uint32_t * size) {
    char path[MAX_PATHLEN * 2];
    handle_t data;

    data = read_test_disk_path(filename, size);
    if (data)
        return data;

    snprintf(path, sizeof(path), "%s", filename ? filename : "");
    normalize_disk_path(path);
    data = read_test_disk_path(path, size);
    if (data)
        return data;

    snprintf(path, sizeof(path), "%s/%s", TEST_SC2_SRC_DIR, filename ? filename : "");
    normalize_disk_path(path);
    return read_test_disk_path(path, size);
}

static void use_sc2_disk_host(void) {
    SC2_MapSetHost(&(sc2MapHost_t){
        .read_file = read_test_disk_file,
        .free_file = MemFree,
        .mem_alloc = MemAlloc,
        .mem_free = MemFree,
    });
}

static bool test_path_leaf_is(cstring_t filename, cstring_t leaf) {
    cstring_t base;

    if (!filename || !leaf)
        return false;
    base = filename + strlen(filename);
    while (base > filename && base[-1] != '/' && base[-1] != '\\') {
        base--;
    }
    return !strcmp(base, leaf);
}

static handle_t read_test_no_manifest_file(cstring_t filename, uint32_t * size) {
    if (test_path_leaf_is(filename, "GameData.xml")) {
        if (size) *size = 0;
        return NULL;
    }
    return read_test_disk_file(filename, size);
}

static void use_sc2_no_manifest_disk_host(void) {
    SC2_MapSetHost(&(sc2MapHost_t){
        .read_file = read_test_no_manifest_file,
        .free_file = MemFree,
        .mem_alloc = MemAlloc,
        .mem_free = MemFree,
    });
}

static uint32_t short_terrain_width(uint32_t width) {
    if (short_terrain_dimensions == TEST_SC2_ZERO_TERRAIN_DIMENSIONS)
        return 0;
    if (short_terrain_dimensions == TEST_SC2_HUGE_TERRAIN_DIMENSIONS)
        return 0xffffffffu;
    return width;
}

static uint32_t short_terrain_height(uint32_t height) {
    if (short_terrain_dimensions == TEST_SC2_ZERO_TERRAIN_DIMENSIONS)
        return 0;
    if (short_terrain_dimensions == TEST_SC2_HUGE_TERRAIN_DIMENSIONS)
        return 0xffffffffu;
    return height;
}

static handle_t make_short_height_map(uint32_t * size) {
    sc2MapHeightMap_t *layer = MemAlloc(sizeof(*layer));

    if (!layer)
        return NULL;
    memset(layer, 0, sizeof(*layer));
    layer->fourcc = MAKEFOURCC('H','M','A','P');
    layer->width = short_terrain_width(9);
    layer->height = short_terrain_height(7);
    if (size) *size = sizeof(*layer);
    return layer;
}

static handle_t make_short_sync_height_map(uint32_t * size) {
    sc2MapSyncHeightMap_t *layer = MemAlloc(sizeof(*layer));

    if (!layer)
        return NULL;
    memset(layer, 0, sizeof(*layer));
    layer->fourcc = MAKEFOURCC('S','M','A','P');
    layer->width = short_terrain_width(9);
    layer->height = short_terrain_height(7);
    if (size) *size = sizeof(*layer);
    return layer;
}

static handle_t make_short_cell_flags(uint32_t * size) {
    sc2MapCellFlags_t *layer = MemAlloc(sizeof(*layer));

    if (!layer)
        return NULL;
    memset(layer, 0, sizeof(*layer));
    layer->fourcc = MAKEFOURCC('L','F','C','T');
    layer->width = short_terrain_width(8);
    layer->height = short_terrain_height(6);
    if (size) *size = sizeof(*layer);
    return layer;
}

static handle_t make_short_sync_cliff_level(uint32_t * size) {
    sc2MapSyncCliffLevel_t *layer = MemAlloc(sizeof(*layer));

    if (!layer)
        return NULL;
    memset(layer, 0, sizeof(*layer));
    layer->fourcc = MAKEFOURCC('C','L','I','F');
    layer->width = short_terrain_width(8);
    layer->height = short_terrain_height(6);
    if (size) *size = sizeof(*layer);
    return layer;
}

static handle_t make_short_texture_masks(uint32_t * size) {
    sc2MapTextureMasks_t *layer = MemAlloc(sizeof(*layer));

    if (!layer)
        return NULL;
    memset(layer, 0, sizeof(*layer));
    layer->fourcc = MAKEFOURCC('M','A','S','K');
    layer->width = short_terrain_width(4);
    layer->height = short_terrain_height(4);
    if (size) *size = sizeof(*layer);
    return layer;
}

static handle_t make_short_hard_tiles(uint32_t * size) {
    uint8_t *layer = MemAlloc(32);

    if (!layer) return NULL;
    memset(layer, 0, 32); memcpy(layer, "HRDT", 4);
    layer[4] = 102; layer[24] = 1; layer[28] = 1;
    if (size) *size = 32;
    return layer;
}

static handle_t read_test_short_terrain_file(cstring_t filename, uint32_t * size) {
    if (size) *size = 0;
    if (test_path_leaf_is(filename, "t3HeightMap"))
        return make_short_height_map(size);
    if (test_path_leaf_is(filename, "t3SyncHeightMap"))
        return make_short_sync_height_map(size);
    if (test_path_leaf_is(filename, "t3CellFlags"))
        return make_short_cell_flags(size);
    if (test_path_leaf_is(filename, "t3SyncCliffLevel"))
        return make_short_sync_cliff_level(size);
    if (test_path_leaf_is(filename, "t3TextureMasks"))
        return make_short_texture_masks(size);
    if (test_path_leaf_is(filename, "t3HardTile"))
        return make_short_hard_tiles(size);
    return read_test_disk_file(filename, size);
}

static void use_sc2_short_terrain_host(uint32_t dimensions) {
    short_terrain_dimensions = dimensions;
    SC2_MapSetHost(&(sc2MapHost_t){
        .read_file = read_test_short_terrain_file,
        .free_file = MemFree,
        .mem_alloc = MemAlloc,
        .mem_free = MemFree,
    });
}

static void collect_map(cstring_t path, void *userData) {
    (void)userData;
    listed_count++;
    if (path && !strcmp(path, "Maps\\Test\\Tiny.SC2Map")) {
        snprintf(listed_map, sizeof(listed_map), "%s", path ? path : "");
    }
}

TEST(sc2_map, sc2_fixture_archive_lists_map_root) {
    setup_sc2_tests();
    use_sc2_fs_host();
    listed_count = 0;
    listed_map[0] = '\0';

    T_ASSERT(FS_ListMaps(collect_map, NULL) >= 1);
    T_ASSERT(listed_count >= 1);
    T_STREQ(listed_map, "Maps\\Test\\Tiny.SC2Map");
}

TEST(sc2_map, sc2_fixture_short_name_resolves) {
    PATHSTR path;

    setup_sc2_tests();
    use_sc2_fs_host();
    T_EQ(FS_ResolveMapPath("Tiny", path, sizeof(path)), FS_MAP_RESOLVE_OK);
    T_STREQ(path, "Maps\\Test\\Tiny.SC2Map");
}

static void assert_tiny_map_catalog_overrides(sc2Map_t *map) {
    T_EQ(map->catalog.footprints, 3);
    T_STREQ(map->objects[1].model, "Assets\\Units\\Terran\\MarineManifestModel\\MarineManifestModel.m3");
    T_STREQ(map->objects[1].footprint, "FootprintMarine");
    T_STREQ(map->objects[1].mover, "Ground");
    T_EQ(map->objects[1].unit_flags, SC2_UNIT_FLAG_MOVABLE);
    T_FEQ(map->objects[1].radius, 0.875f, 0.001f);
    T_FEQ(map->objects[1].move_height, 1.25f, 0.001f);
    T_FEQ(map->objects[1].footprint_width, 1.0f, 0.001f);
    T_FEQ(map->objects[1].footprint_height, 1.0f, 0.001f);
    T_FEQ(map->objects[1].footprint_radius, 0.5f, 0.001f);
    T_STREQ(map->objects[3].footprint, "FootprintDoodad1x1");
    T_FEQ(map->objects[3].footprint_width, 1.0f, 0.001f);
    T_FEQ(map->objects[3].footprint_height, 1.0f, 0.001f);
    T_FEQ(map->objects[3].footprint_radius, 0.7072f, 0.001f);
    T_STREQ(map->objects[6].model, "Assets\\Buildings\\Terran\\SupplyDepotCatalogModel\\SupplyDepotCatalogModel.m3");
    T_STREQ(map->objects[6].footprint, "Footprint2x2");
    T_STREQ(map->objects[6].mover, "None");
    T_EQ(map->objects[6].unit_flags, SC2_UNIT_FLAG_STRUCTURE);
    T_FEQ(map->objects[6].footprint_width, 2.0f, 0.001f);
    T_FEQ(map->objects[6].footprint_height, 2.0f, 0.001f);
    T_FEQ(map->objects[6].footprint_radius, 1.4143f, 0.001f);
    T_STREQ(map->t3Terrain.terrain_textures[0].diffuse, "Assets\\Textures\\Terrain\\FixtureGrass_Diffuse.dds");
    T_STREQ(map->t3Terrain.terrain_textures[0].normal, "Assets\\Textures\\Terrain\\FixtureGrass_Diffuse_normal.dds");
    T_STREQ(map->t3Terrain.cliff_sets[0].mesh, "CliffNatural0");
}

static void assert_tiny_map_known_file_catalog_fallback(sc2Map_t *map) {
    T_EQ(map->catalog.footprints, 3);
    T_STREQ(map->objects[1].model, "Assets\\Units\\Terran\\MarineCatalogModel\\MarineCatalogModel.m3");
    T_STREQ(map->objects[1].footprint, "FootprintMarine");
    T_STREQ(map->objects[1].mover, "Ground");
    T_FEQ(map->objects[1].radius, 0.75f, 0.001f);
    T_STREQ(map->objects[6].model, "Assets\\Buildings\\Terran\\SupplyDepotCatalogModel\\SupplyDepotCatalogModel.m3");
    T_FEQ(map->objects[6].footprint_radius, 1.4143f, 0.001f);
    T_STREQ(map->t3Terrain.terrain_textures[0].diffuse, "Assets\\Textures\\Terrain\\FixtureGrass_Diffuse.dds");
    T_STREQ(map->t3Terrain.cliff_sets[0].mesh, "CliffNatural0");
}

TEST(sc2_map, campaign_object_capacity) { T_EQ(SC2_MAX_MAP_OBJECTS, 4096); }

TEST(sc2_map, camera_pitch_converts_to_orbit_euler) {
    vector3_t euler = SC2_EulerFromCamera(56.0f, 180.0f);
    vector3_t native;
    T_FEQ(euler.x, -34.0f, 0.001f);
    T_FEQ(euler.y, 0.0f, 0.001f);
    T_FEQ(euler.z, 0.0f, 0.001f);
    native = SC2_CameraFromEuler(&euler, 2.5f);
    T_FEQ(native.x, 56.0f, 0.001f);
    T_FEQ(native.y, 180.0f, 0.001f);
    T_FEQ(native.z, 2.5f, 0.001f);
    euler = SC2_EulerFromCamera(34.9f, 193.9f);
    T_FEQ(euler.x, -55.1f, 0.001f);
}

/* The retail bridge view is from negative Y at yaw 180; test the actual quaternion/view path. */
TEST(sc2_map, camera_eye_side_and_upright_basis) {
    FOR_LOOP(i, 5) {
        float yaw = i * 90.0f, pitch = 56.0f;
        vector3_t angles = SC2_EulerFromCamera(pitch, yaw);
        quaternion_t quat = Quaternion_fromEuler(&angles, ROTATE_ZYX);
        matrix4_t view, inv;
        Matrix4_identity(&view); Matrix4_translate(&view, &(vector3_t){ 0, 0, -34 });
        Matrix4_rotateQuat(&view, &quat); Matrix4_inverse(&view, &inv);
        T_FEQ(inv.v[12], 34 * sinf(DEG2RAD(yaw)) * cosf(DEG2RAD(pitch)), 0.001f);
        T_FEQ(inv.v[13], 34 * cosf(DEG2RAD(yaw)) * cosf(DEG2RAD(pitch)), 0.001f);
        T_FEQ(inv.v[14], 34 * sinf(DEG2RAD(pitch)), 0.001f);
        T_ASSERT(view.v[9] > 0); /* World up projects toward screen up. */
    }
}

TEST(sc2_map, sc2_map_loads_xml_objects_and_terrain) {
    sc2Map_t *map;
    sc2MapObject_t unit;

    setup_sc2_tests();
    use_sc2_fs_host();
    T_ASSERT(SC2_MapLoad("Maps\\Test\\Tiny.SC2Map"));
    T_STREQ(SC2_MapConversationField("StoryTips|Marine", "Name"), "ConversationState/StoryTips/Marine");
    T_STREQ(SC2_MapConversationField("StoryTips|Marine", "ImagePath"), "Assets/Textures/MarineOverride.dds");
    T_STREQ(SC2_MapConversationField("StoryTips|Marine", "Text:Description"), "Map description");
    T_STREQ(SC2_MapConversationField("StoryTips|Marine", "Text:Loading Screen Restart"), "Restart description");
    T_NULL(SC2_MapConversationField("StoryTips|Missing", "ImagePath"));
    map = SC2_MapCurrent();

    T_ASSERT(SC2_MapResolveUnit("Marine", &unit));
    T_STREQ(unit.model, "Assets\\Units\\Terran\\MarineManifestModel\\MarineManifestModel.m3");
    T_STREQ(unit.mover, "Ground");
    T_FEQ(unit.radius, 0.875f, 0.001f);
    T_FEQ(unit.move_height, 1.25f, 0.001f);

    T_STREQ(map->map_name, "SC2 Tiny Fixture");
    T_EQ(map->MapInfo.fourcc, MAKEFOURCC('I','p','a','M'));
    T_EQ(map->MapInfo.width, 8);
    T_EQ(map->MapInfo.height, 6);
    T_STREQ((char const *)map->MapInfo.data, "SC2 Tiny Fixture");
    T_EQ(map->num_objects, 7);
    assert_tiny_map_catalog_overrides(map);

    T_STREQ(map->objects[0].name, "StartGame02");
    T_EQ(map->objects[0].id, 10);
    T_EQ(map->objects[0].type, SC2_OBJECT_CAMERA);
    T_FEQ(map->objects[0].position.x, 10.0f, 0.001f);
    T_FEQ(map->objects[0].position.y, 10.0f, 0.001f);
    T_FEQ(map->objects[0].camera.target.x, 10.0f, 0.001f);
    T_FEQ(map->objects[0].camera.target.y, 10.0f, 0.001f);
    T_FEQ(map->objects[0].camera.distance, 34.0f, 0.001f);
    T_FEQ(map->objects[0].camera.pitch, 56.0f, 0.001f);
    T_FEQ(map->objects[0].camera.yaw, 179.9584f, 0.001f);
    T_FEQ(map->objects[0].camera.fov, 27.7998f, 0.001f);
    T_FEQ(map->objects[0].camera.znear, 0.0998f, 0.001f);
    T_FEQ(map->objects[0].camera.zfar, 400.0f, 0.001f);
    {
        sc2MapCamera_t cam;
        T_ASSERT(SC2_MapDefaultCamera(&cam));
        T_ASSERT(cam.fov > 0.0f);
        T_ASSERT(cam.znear > 0.0f);
        T_ASSERT(cam.zfar > 0.0f);
        T_FEQ(cam.fov, map->objects[0].camera.fov, 0.001f);
        T_FEQ(cam.znear, map->objects[0].camera.znear, 0.001f);
        T_FEQ(cam.zfar, map->objects[0].camera.zfar, 0.001f);
    }

    T_STREQ(map->objects[1].name, "Marine");
    T_EQ(map->objects[1].id, 1);
    T_EQ(map->objects[1].type, SC2_OBJECT_UNIT);
    T_FEQ(map->objects[1].position.x, 3.5f, 0.001f);
    T_FEQ(map->objects[1].position.y, 3.5f, 0.001f);
    T_FEQ(map->objects[1].position.z, 0.25f, 0.001f);
    T_FEQ(map->objects[1].angle, 0.75f, 0.001f);
    T_EQ(map->objects[1].player, 2);
    T_EQ(map->objects[1].section, 7);
    T_EQ(map->objects[1].resources, 50);

    T_STREQ(map->objects[3].name, "BillboardTall");
    T_EQ(map->objects[3].id, 3);
    T_EQ(map->objects[3].type, SC2_OBJECT_DOODAD);
    T_STREQ(map->objects[3].model, "Assets\\Doodads\\BillboardTall\\BillboardTall_00.m3");
    T_FEQ(map->objects[3].position.z, 8.0f, 0.001f);
    T_EQ(map->objects[3].flags, SC2_OBJECT_HEIGHT_ABSOLUTE | SC2_OBJECT_FORCE_PLACEMENT);
    T_EQ(map->objects[3].tint_color.r, 10);
    T_EQ(map->objects[3].tint_color.g, 20);
    T_EQ(map->objects[3].tint_color.b, 30);
    T_EQ(map->objects[3].tint_color.a, 128);

    T_STREQ(map->objects[4].name, "MineralField");
    T_EQ(map->objects[4].type, SC2_OBJECT_DOODAD);
    T_STREQ(map->objects[4].model, "Assets\\Doodads\\Terran\\MineralField\\MineralField.m3");
    T_FEQ(map->objects[4].position.x, 4.0f, 0.001f);
    T_FEQ(map->objects[4].position.y, 3.0f, 0.001f);
    T_FEQ(map->objects[4].position.z, 0.0f, 0.001f);

    T_STREQ(map->objects[5].name, "StartPoint01");
    T_EQ(map->objects[5].id, 5);
    T_EQ(map->objects[5].type, SC2_OBJECT_POINT);
    T_STREQ(map->objects[5].type_name, "StartLocation");
    T_STREQ(map->objects[5].model, "Assets\\Editor\\StartLocation\\StartLocation.m3");
    T_STREQ(map->objects[5].anim_props, "Stand");
    T_STREQ(map->objects[5].sound, "Assets\\Sounds\\StartLocation.ogg");
    T_STREQ(map->objects[5].attach_id, "StartAttach");
    T_EQ(map->objects[5].object_id, 1);
    T_STREQ(map->objects[5].object_type, "Unit");
    T_FEQ(map->objects[5].pathing_soft_radius, 1.5f, 0.001f);
    T_FEQ(map->objects[5].pathing_hard_radius, 0.75f, 0.001f);
    T_FEQ(map->objects[5].position.x, 2.0f, 0.001f);
    T_FEQ(map->objects[5].angle, 0.5f, 0.001f);
    T_EQ(map->objects[5].section, 9);
    T_EQ(map->objects[5].color.r, 200);
    T_EQ(map->objects[5].color.g, 180);
    T_EQ(map->objects[5].color.b, 160);
    T_EQ(map->objects[5].color.a, 255);

    T_STREQ(map->objects[6].name, "SupplyDepot");
    T_EQ(map->objects[6].id, 6);
    T_EQ(map->objects[6].type, SC2_OBJECT_UNIT);
    T_FEQ(map->objects[6].position.x, 6.0f, 0.001f);
    T_FEQ(map->objects[6].position.y, 1.0f, 0.001f);
    T_EQ(map->objects[6].player, 2);

    T_STREQ(map->t3Terrain.tile_set, "Fixture");
    T_FEQ(map->t3Terrain.height_quantize_bias, 0.0f, 0.001f);
    T_FEQ(map->t3Terrain.height_quantize_scale, 1.0f, 0.001f);
    T_FEQ(map->t3Terrain.standard_height, 0.0f, 0.001f);
    T_EQ(map->t3Terrain.fog_enabled, true);
    T_FEQ(map->t3Terrain.fog_density, 0.25f, 0.001f);
    T_FEQ(map->t3Terrain.fog_falloff, 0.5f, 0.001f);
    T_FEQ(map->t3Terrain.fog_start_height, -1.5f, 0.001f);
    T_EQ(map->t3Terrain.fog_color.a, 255);
    T_EQ(map->t3Terrain.fog_color.r, 10);
    T_EQ(map->t3Terrain.fog_color.g, 20);
    T_EQ(map->t3Terrain.fog_color.b, 30);
    T_EQ(map->t3Terrain.num_terrain_textures, 2);
    T_STREQ(map->t3Terrain.terrain_textures[1].diffuse, "Assets\\Textures\\Terrain\\FixtureDirt_Diffuse.dds");

    T_EQ(map->t3Terrain.num_cliff_sets, 1);
    T_STREQ(map->t3Terrain.cliff_sets[0].name, "FixtureCliff0");
    T_EQ(ARRAY_COUNT(map->t3Terrain.ramps), 1);
    if (ARRAY_COUNT(map->t3Terrain.ramps)) {
        sc2Ramp_t const *ramp = map->t3Terrain.ramps;
        T_EQ(ramp->hi, 2); T_EQ(ramp->lo, 1); T_EQ(ramp->variant[0], 2); T_EQ(ramp->variant[1], ~0u);
        T_FEQ(ramp->edge[0].center.x, 1, 0.001f); T_FEQ(ramp->edge[0].height, 2, 0.001f);
        T_FEQ(ramp->mid.width, 2, 0.001f); T_FEQ(ramp->mid.up.y, 1, 0.001f);
    }
    T_EQ(map->t3Terrain.num_cliff_cells, 2);
    T_EQ(map->t3Terrain.cliff_cells[0].index, 0);
    T_EQ(map->t3Terrain.cliff_cells[0].flags, 1);
    T_EQ(map->t3Terrain.cliff_cells[0].cliff_set, 0);
    T_EQ(map->t3Terrain.cliff_cells[0].variant, 2);
    T_EQ(map->t3Terrain.cliff_cells[1].index, 1);
    T_EQ(map->t3Terrain.cliff_cells[1].flags, 3);

    T_EQ(map->lighting.enabled, true);
    T_STREQ(map->lighting.id, "Fixture");
    T_FEQ(map->lighting.ambient_color.x, 0.1f, 0.001f);
    T_FEQ(map->lighting.ambient_color.y, 0.2f, 0.001f);
    T_FEQ(map->lighting.ambient_color.z, 0.3f, 0.001f);
    T_EQ(map->lighting.colorize, true);
    T_FEQ(map->lighting.colorization_blend, 0.3f, 0.001f);
    T_FEQ(sc2_light_ambient(&map->lighting).z, 0.09f, 0.001f);
    T_EQ(map->lighting.directional[SC2_LIGHT_KEY].enabled, true);
    T_FEQ(map->lighting.directional[SC2_LIGHT_KEY].color.x, 0.4f, 0.001f);
    T_FEQ(map->lighting.directional[SC2_LIGHT_KEY].color.y, 0.5f, 0.001f);
    T_FEQ(map->lighting.directional[SC2_LIGHT_KEY].color.z, 0.6f, 0.001f);
    T_FEQ(map->lighting.directional[SC2_LIGHT_KEY].color_multiplier, 2.0f, 0.001f);
    T_FEQ(map->lighting.directional[SC2_LIGHT_KEY].spec_color_multiplier, 3.0f, 0.001f);
    T_FEQ(map->lighting.directional[SC2_LIGHT_KEY].direction.z, -1.0f, 0.001f);
    T_EQ(map->lighting.directional[SC2_LIGHT_FILL].enabled, true);
    T_FEQ(map->lighting.directional[SC2_LIGHT_FILL].color_multiplier, 4.0f, 0.001f);
    T_FEQ(map->lighting.directional[SC2_LIGHT_FILL].direction.x, 1.0f, 0.001f);
    T_EQ(map->lighting.directional[SC2_LIGHT_BACK].enabled, true);
    T_FEQ(map->lighting.directional[SC2_LIGHT_BACK].color_multiplier, 5.0f, 0.001f);
    T_FEQ(map->lighting.directional[SC2_LIGHT_BACK].direction.y, 1.0f, 0.001f);
}

/* Non-colorized catalogs retain direct ambient while missing lighting uses the renderer fallback. */
TEST(sc2_map, ordinary_and_missing_light_ambient) {
    sc2MapLighting_t light = { .ambient_color = { 0.1f, 0.2f, 0.3f } };
    vector3_t ambient = sc2_light_ambient(&light);
    T_FEQ(ambient.x, 0.1f, 0.001f); T_FEQ(ambient.y, 0.2f, 0.001f); T_FEQ(ambient.z, 0.3f, 0.001f);
    ambient = sc2_light_ambient(NULL);
    T_FEQ(ambient.x, 0.35f, 0.001f); T_FEQ(ambient.y, 0.35f, 0.001f); T_FEQ(ambient.z, 0.4f, 0.001f);
}

TEST(sc2_map, sc2_map_loads_binary_terrain_layers) {
    sc2Map_t *map;

    setup_sc2_tests();
    use_sc2_fs_host();
    T_ASSERT(SC2_MapLoad("Maps\\Test\\Tiny.SC2Map"));
    map = SC2_MapCurrent();

    T_NOT_NULL(map->t3CellFlags);
    if (map->t3CellFlags) {
        T_EQ(map->t3CellFlags->fourcc, MAKEFOURCC('L','F','C','T'));
        T_EQ(map->t3CellFlags->width, 8);
        T_EQ(map->t3CellFlags->height, 6);
        T_EQ(map->t3CellFlags->data[10], 0x1a);
        T_EQ(map->t3CellFlags->data[29], 0x2d);
    }

    T_NOT_NULL(map->t3SyncCliffLevel);
    if (map->t3SyncCliffLevel) {
        T_EQ(map->t3SyncCliffLevel->fourcc, MAKEFOURCC('C','L','I','F'));
        T_EQ(map->t3SyncCliffLevel->width, 8);
        T_EQ(map->t3SyncCliffLevel->height, 6);
        T_EQ(map->t3SyncCliffLevel->data[10], 11);
        T_EQ(map->t3SyncCliffLevel->data[29], 30);
    }

    T_NOT_NULL(map->t3HeightMap);
    if (map->t3HeightMap) {
        T_EQ(map->t3HeightMap->fourcc, MAKEFOURCC('H','M','A','P'));
        T_EQ(map->t3HeightMap->width, 9);
        T_EQ(map->t3HeightMap->height, 7);
        T_EQ(map->t3HeightMap->data[0].adjustment, 0);
        T_EQ(map->t3HeightMap->data[0].height, 1);
        T_EQ(map->t3HeightMap->data[0].extra, 0);
        T_EQ(map->t3HeightMap->data[42].adjustment, 0);
        T_EQ(map->t3HeightMap->data[42].height, 13);
        T_EQ(map->t3HeightMap->data[42].extra, 0);
        T_FEQ(SC2_MapHeightAtPoint(0.0f, 0.0f), 0.0f, 0.001f);
        T_FEQ(SC2_MapHeightAtPoint(6.0f, 4.0f), 12.0f, 0.001f);
        T_FEQ(SC2_MapHeightAtPoint(map->objects[1].position.x,
                                             map->objects[1].position.y),
                        10.0f,
                        0.001f);
        T_FEQ(SC2_MapHeightAtPoint(map->objects[1].position.x,
                                             map->objects[1].position.y) + map->objects[1].position.z,
                        10.25f,
                        0.001f);
    }
    T_NOT_NULL(map->t3SyncHeightMap);
    if (map->t3SyncHeightMap) {
        T_EQ(map->t3SyncHeightMap->fourcc, MAKEFOURCC('S','M','A','P'));
        T_EQ(map->t3SyncHeightMap->width, 9);
        T_EQ(map->t3SyncHeightMap->height, 7);
        T_EQ(map->t3SyncHeightMap->data[42].height, 128);
    }

    T_NOT_NULL(map->t3TextureMasks);
    if (map->t3TextureMasks) {
        T_EQ(map->t3TextureMasks->fourcc, MAKEFOURCC('M','A','S','K'));
        T_EQ(map->t3TextureMasks->width, 4);
        T_EQ(map->t3TextureMasks->height, 4);
        T_EQ(map->t3TextureMasksSize, 80);
        T_EQ(map->t3TextureMasks->data[0], 0x12);
        T_EQ(map->t3TextureMasks->data[8], 0xab);
    }
    T_EQ(ARRAY_COUNT(map->hard_tiles), 2);
    if (ARRAY_COUNT(map->hard_tiles) == 2) {
        T_STREQ(map->hard_tiles[0].tile, "FixtureTile");
        T_STREQ(map->hard_tiles[0].model, "Assets\\HardTiles\\MarSaraRoad\\MarSaraRoad.m3");
        T_FEQ(map->hard_tiles[0].position.x, 2.0f, 0.001f);
        T_FEQ(map->hard_tiles[1].position.y, 5.0f, 0.001f);
        T_FEQ(map->hard_tiles[1].scale.x, 2.0f, 0.001f);
        T_EQ(map->hard_tiles[1].flags, 8);
    }
}

TEST(sc2_map, sc2_map_loads_directory_fixture_without_generated_layers) {
    sc2Map_t *map;

    setup_sc2_tests();
    use_sc2_disk_host();
    T_ASSERT(SC2_MapLoad(TEST_SC2_TINY_DIR));
    map = SC2_MapCurrent();

    T_STREQ(map->map_name, "SC2 Tiny Fixture");
    T_EQ(map->MapInfo.fourcc, MAKEFOURCC('M','a','p','I'));
    T_EQ(map->MapInfo.width, 8);
    T_EQ(map->MapInfo.height, 6);
    T_EQ(map->num_objects, 7);
    assert_tiny_map_catalog_overrides(map);

    T_STREQ(map->objects[0].name, "StartGame02");
    T_EQ(map->objects[0].type, SC2_OBJECT_CAMERA);
    T_FEQ(map->objects[0].camera.distance, 34.0f, 0.001f);
    T_STREQ(map->objects[1].name, "Marine");
    T_EQ(map->objects[1].type, SC2_OBJECT_UNIT);
    T_FEQ(map->objects[1].position.x, 3.5f, 0.001f);
    T_EQ(map->objects[1].player, 2);
    T_STREQ(map->objects[4].name, "MineralField");
    T_EQ(map->objects[4].type, SC2_OBJECT_DOODAD);
    T_STREQ(map->objects[4].model, "Assets\\Doodads\\Terran\\MineralField\\MineralField.m3");
    T_STREQ(map->objects[6].name, "SupplyDepot");
    T_STREQ(map->objects[6].mover, "None");
    T_FEQ(map->objects[6].footprint_radius, 1.4143f, 0.001f);

    T_STREQ(map->t3Terrain.tile_set, "Fixture");
    T_FEQ(map->t3Terrain.height_quantize_scale, 1.0f, 0.001f);
    T_EQ(map->t3Terrain.num_terrain_textures, 2);
    T_EQ(map->t3Terrain.num_cliff_sets, 1);
    T_STREQ(map->t3Terrain.cliff_sets[0].name, "FixtureCliff0");
    T_EQ(ARRAY_COUNT(map->t3Terrain.ramps), 1);
    if (ARRAY_COUNT(map->t3Terrain.ramps)) {
        sc2Ramp_t const *ramp = map->t3Terrain.ramps;
        T_EQ(ramp->hi, 2); T_EQ(ramp->lo, 1); T_EQ(ramp->variant[0], 2); T_EQ(ramp->variant[1], ~0u);
        T_FEQ(ramp->edge[0].center.x, 1, 0.001f); T_FEQ(ramp->edge[0].height, 2, 0.001f);
        T_FEQ(ramp->mid.width, 2, 0.001f); T_FEQ(ramp->mid.up.y, 1, 0.001f);
    }
    T_EQ(map->t3Terrain.num_cliff_cells, 2);
    T_EQ(map->t3Terrain.cliff_cells[0].variant, 2);
    T_EQ(map->lighting.enabled, true);
    T_STREQ(map->lighting.id, "Fixture");
    T_FEQ(map->lighting.directional[SC2_LIGHT_KEY].color_multiplier, 2.0f, 0.001f);

    T_NULL(map->t3CellFlags);
    T_NULL(map->t3SyncCliffLevel);
    T_NULL(map->t3HeightMap);
    T_NULL(map->t3SyncHeightMap);
    T_NULL(map->t3TextureMasks);
    T_EQ(map->t3TextureMasksSize, 0);

    SC2_MapShutdown();
    use_sc2_fs_host();
}

TEST(sc2_map, sc2_map_catalog_known_files_fallback_without_manifest) {
    sc2Map_t *map;

    setup_sc2_tests();
    use_sc2_no_manifest_disk_host();
    T_ASSERT(SC2_MapLoad(TEST_SC2_TINY_DIR));
    map = SC2_MapCurrent();

    T_STREQ(map->map_name, "SC2 Tiny Fixture");
    T_EQ(map->num_objects, 7);
    assert_tiny_map_known_file_catalog_fallback(map);

    SC2_MapShutdown();
    use_sc2_fs_host();
}

static void assert_tiny_map_loaded_without_binary_terrain_layers(sc2Map_t *map) {
    T_STREQ(map->map_name, "SC2 Tiny Fixture");
    T_EQ(map->MapInfo.width, 8);
    T_EQ(map->MapInfo.height, 6);
    T_EQ(map->num_objects, 7);
    T_STREQ(map->objects[1].name, "Marine");
    T_STREQ(map->t3Terrain.tile_set, "Fixture");

    T_NULL(map->t3HeightMap);
    T_NULL(map->t3SyncHeightMap);
    T_NULL(map->t3CellFlags);
    T_NULL(map->t3SyncCliffLevel);
    T_NULL(map->t3TextureMasks);
    T_EQ(map->t3TextureMasksSize, 0);
    T_ASSERT(IS_ARRAY_EMPTY(map->hard_tiles));
}

TEST(sc2_map, sc2_map_rejects_short_binary_terrain_layers) {
    sc2Map_t *map;

    setup_sc2_tests();
    use_sc2_short_terrain_host(TEST_SC2_SHORT_TERRAIN_DIMENSIONS);
    T_ASSERT(SC2_MapLoad(TEST_SC2_TINY_DIR));
    map = SC2_MapCurrent();

    assert_tiny_map_loaded_without_binary_terrain_layers(map);

    SC2_MapShutdown();
    use_sc2_fs_host();
}

TEST(sc2_map, sc2_map_rejects_zero_dimension_binary_terrain_layers) {
    sc2Map_t *map;

    setup_sc2_tests();
    use_sc2_short_terrain_host(TEST_SC2_ZERO_TERRAIN_DIMENSIONS);
    T_ASSERT(SC2_MapLoad(TEST_SC2_TINY_DIR));
    map = SC2_MapCurrent();

    assert_tiny_map_loaded_without_binary_terrain_layers(map);

    SC2_MapShutdown();
    use_sc2_fs_host();
}

TEST(sc2_map, sc2_map_rejects_huge_dimension_binary_terrain_layers) {
    sc2Map_t *map;

    setup_sc2_tests();
    use_sc2_short_terrain_host(TEST_SC2_HUGE_TERRAIN_DIMENSIONS);
    T_ASSERT(SC2_MapLoad(TEST_SC2_TINY_DIR));
    map = SC2_MapCurrent();

    assert_tiny_map_loaded_without_binary_terrain_layers(map);

    SC2_MapShutdown();
    use_sc2_fs_host();
}


/* Native unit-scale shadow coverage fits the visible ground and handles a vertical key light. */
TEST(sc2_map, shadow_camera_ground_footprint) {
    sc2shadowview_t input = { .target = {48,48,0}, .light = {.724693f,-.124265f,-.677775f}, .reach = 34 };
    matrix4_t view, proj, shadow;
    vector3_t dir = {0,1,-1}; Vector3_normalize(&dir);
    vector3_t eye = Vector3_mad(&input.target, -input.reach, &dir);
    Matrix4_lookAt(&view, &eye, &dir, &(vector3_t){0,0,1});
    Matrix4_perspective(&proj, 45, 16.0f/9, 1, 1000); Matrix4_multiply(&proj, &view, &input.camera);
    T_ASSERT(sc2_shadow_matrix(&input, &shadow));
    vector3_t a = Matrix4_multiply_vector3(&shadow, &input.target);
    vector3_t b = Matrix4_multiply_vector3(&shadow, &(vector3_t){49,48,0});
    T_ASSERT(fabsf(a.x) < 1 && fabsf(a.y) < 1 && fabsf(a.z) < 1);
    T_ASSERT(Vector3_distance(&a, &b) > .01f);
    matrix4_t inv; Matrix4_inverse(&input.camera, &inv);
    FOR_LOOP(i, 4) {
        vector3_t near = Matrix4_multiply_vector3(&inv, &(vector3_t){i&1?1:-1,i&2?1:-1,-1});
        vector3_t far = Matrix4_multiply_vector3(&inv, &(vector3_t){i&1?1:-1,i&2?1:-1,1});
        vector3_t ray = Vector3_sub(&far, &near), ground = Vector3_mad(&near, -near.z/ray.z, &ray);
        vector3_t clip = Matrix4_multiply_vector3(&shadow, &ground);
        T_ASSERT(fabsf(clip.x) <= 1 && fabsf(clip.y) <= 1 && fabsf(clip.z) <= 1);
    }
    input.light = (vector3_t){0,0,-1}; T_ASSERT(sc2_shadow_matrix(&input, &shadow));
    a = Matrix4_multiply_vector3(&shadow, &input.target); T_FEQ(a.x, 0, .0001f); T_FEQ(a.y, 0, .0001f);
    input.light = (vector3_t){0}; T_ASSERT(!sc2_shadow_matrix(&input, &shadow));
    input.light.z = -1; input.reach = 0; T_ASSERT(!sc2_shadow_matrix(&input, &shadow));
}

/* Dependency roots contain Base.SC2Data once; numbered model cache keys include the placed variation. */
TEST(sc2_map, catalog_root_lighting_and_model_variations) {
    setup_sc2_tests(); use_sc2_fs_host();
    T_ASSERT(SC2_MapLoad("Maps/Test/Variants.SC2Map"));
    sc2Map_t *map = SC2_MapCurrent();
    T_EQ(map->lighting.enabled, true); T_STREQ(map->lighting.id, "Variants");
    T_FEQ(map->lighting.directional[SC2_LIGHT_KEY].direction.x, .7f, .0001f);
    T_FEQ(map->lighting.ambient_color.z, .4f, .0001f);
    T_EQ(map->num_objects, 7);
    T_STREQ(map->objects[0].model, "Assets\\Doodads\\BillboardTall\\BillboardTall_00.m3");
    T_STREQ(map->objects[1].model, "Assets\\Doodads\\BillboardTall\\BillboardTall_02.m3");
    T_STREQ(map->objects[2].model, map->objects[0].model);
    T_STREQ(map->objects[3].model, "Assets\\Doodads\\AnimalCorpse\\AnimalCorpse_08.m3");
    T_STREQ(map->objects[4].model, "Assets\\Doodads\\VariantChild\\VariantChild_02.m3");
    T_STREQ(map->objects[5].model, "Assets\\Doodads\\VariantZero\\VariantZero.m3");
    T_STREQ(map->objects[6].model, "Assets\\Doodads\\BillboardTall\\BillboardTall_07.m3");
    SC2_MapShutdown();
}

/* Exercise production pass submission with GL calls captured, rather than judging shadowed pixels. */
static struct {
    render_phase_t render_phase;
    bool offset;
    float factor, units;
    uint32_t calls, draws;
} road_pass;
static void road_enable(GLenum cap) { T_EQ(cap, GL_POLYGON_OFFSET_FILL); road_pass.offset = true; road_pass.calls++; }
static void road_disable(GLenum cap) { T_EQ(cap, GL_POLYGON_OFFSET_FILL); road_pass.offset = false; road_pass.calls++; }
static void road_offset(GLfloat factor, GLfloat units) {
    road_pass.factor = factor; road_pass.units = units; road_pass.calls++;
}
static void road_draw(renderEntity_t const *entity, m3Model_t const *model, buffer_t const * buffer, uint32_t vertices, uint32_t indices) {
    T_ASSERT(entity->model->m3 == model); T_NOT_NULL(buffer); T_EQ(vertices, 3); T_EQ(indices, 3);
    T_ASSERT(road_pass.offset); T_ASSERT(road_pass.factor < -1); T_ASSERT(road_pass.units < -1);
    road_pass.draws++;
}
#define tr road_pass
#define glEnable road_enable
#define glDisable road_disable
#define glPolygonOffset road_offset
#define M3_RenderBuffer road_draw
#include "games/starcraft-2/renderer/sc2/r_sc2_road_draw.h"
#undef M3_RenderBuffer
#undef glPolygonOffset
#undef glDisable
#undef glEnable
#undef tr

TEST(sc2_map, road_overlay_preserves_shadow_caster_pass) {
    m3Model_t m3 = {0};
    model_t model = {.m3=&m3};
    renderEntity_t entity = {.model=&model};
    buffer_t buffer = {0};
    maplayer_t layer = {.buffer=&buffer, .num_vertices=3, .num_indices=3};
    road_pass = (__typeof__(road_pass)){.render_phase=RENDER_PHASE_LIGHTS, .offset=true, .factor=2, .units=4};
    r_sc2_draw_road_layers(&layer, &entity);
    /* Coplanar overlays add no caster surface; preserve terrain depth and the bias for later units. */
    T_EQ(road_pass.draws, 0); T_EQ(road_pass.calls, 0);
    T_ASSERT(road_pass.offset); T_FEQ(road_pass.factor, 2, 0); T_FEQ(road_pass.units, 4, 0);
    road_pass = (__typeof__(road_pass)){.render_phase=RENDER_PHASE_SOLID};
    r_sc2_draw_road_layers(&layer, &entity);
    T_EQ(road_pass.draws, 1); T_ASSERT(!road_pass.offset);
    T_FEQ(road_pass.factor, 0, 0); T_FEQ(road_pass.units, 0, 0);
    road_pass.calls = 0;
    r_sc2_draw_road_layers(NULL, &entity);
    T_EQ(road_pass.calls, 0);
}
