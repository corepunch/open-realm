/*
 * test_sc2_map.c - StarCraft II map fixture coverage.
 */

#include <stdio.h>
#include <string.h>

#include "common.h"
#include "games/starcraft-2/common/sc2_map.h"
#include "test_framework.h"

#ifndef TEST_SC2_MPQ
#define TEST_SC2_MPQ "build/tests/test-sc2.SC2Maps"
#endif

static BOOL sc2_tests_initialized;
static DWORD listed_count;
static PATHSTR listed_map;

void Key_Init(void) {
}

void Key_WriteBindings(FILE *file) {
    (void)file;
}

void Cmd_ForwardToServer(LPCSTR text) {
    (void)text;
}

void CL_SetGameplayBindings(void) {
}

void CL_BeginLoadingMap(LPCSTR mapName) {
    (void)mapName;
}

void CL_Shutdown(void) {
}

void SV_Map(LPCSTR pFilename) {
    (void)pFilename;
}

void SV_Shutdown(void) {
}

void Sys_Quit(void) {
}

void PF_Sleep(DWORD msec) {
    (void)msec;
}

static void setup_sc2_tests(void) {
    if (sc2_tests_initialized) {
        return;
    }

    LPCSTR argv[] = { "test_sc2", "-config", "" };
    Com_Init(3, argv);
    ASSERT(FS_AddArchive(TEST_SC2_MPQ) != NULL);
    SC2_MapSetHost(&(sc2MapHost_t){
        .read_file = FS_ReadFile,
        .free_file = FS_FreeFile,
        .mem_alloc = MemAlloc,
        .mem_free = MemFree,
    });
    sc2_tests_initialized = true;
}

static void collect_map(LPCSTR path, void *userData) {
    (void)userData;
    listed_count++;
    if (path && !strcmp(path, "Maps\\Test\\Tiny.SC2Map")) {
        snprintf(listed_map, sizeof(listed_map), "%s", path ? path : "");
    }
}

static void test_sc2_fixture_archive_lists_map_root(void) {
    setup_sc2_tests();
    listed_count = 0;
    listed_map[0] = '\0';

    ASSERT(FS_ListMaps(collect_map, NULL) >= 1);
    ASSERT(listed_count >= 1);
    ASSERT_STR_EQ(listed_map, "Maps\\Test\\Tiny.SC2Map");
}

static void test_sc2_fixture_short_name_resolves(void) {
    PATHSTR path;

    setup_sc2_tests();
    ASSERT_EQ_INT(FS_ResolveMapPath("Tiny", path, sizeof(path)), FS_MAP_RESOLVE_OK);
    ASSERT_STR_EQ(path, "Maps\\Test\\Tiny.SC2Map");
}

static void test_sc2_map_loads_xml_objects_and_terrain(void) {
    sc2Map_t *map;

    setup_sc2_tests();
    ASSERT(SC2_MapLoad("Maps\\Test\\Tiny.SC2Map"));
    map = SC2_MapCurrent();

    ASSERT_STR_EQ(map->map_name, "SC2 Tiny Fixture");
    ASSERT_EQ_INT(map->MapInfo.fourcc, MAKEFOURCC('I','p','a','M'));
    ASSERT_EQ_INT(map->MapInfo.width, 8);
    ASSERT_EQ_INT(map->MapInfo.height, 6);
    ASSERT_STR_EQ((char const *)map->MapInfo.data, "SC2 Tiny Fixture");
    ASSERT_EQ_INT(map->num_objects, 5);

    ASSERT_STR_EQ(map->objects[0].name, "StartGame02");
    ASSERT_EQ_INT(map->objects[0].id, 10);
    ASSERT_EQ_INT(map->objects[0].type, SC2_OBJECT_CAMERA);
    ASSERT_EQ_FLOAT(map->objects[0].position.x, 10.0f, 0.001f);
    ASSERT_EQ_FLOAT(map->objects[0].position.y, 10.0f, 0.001f);
    ASSERT_EQ_FLOAT(map->objects[0].camera.target.x, 10.0f, 0.001f);
    ASSERT_EQ_FLOAT(map->objects[0].camera.target.y, 10.0f, 0.001f);
    ASSERT_EQ_FLOAT(map->objects[0].camera.distance, 34.0f, 0.001f);
    ASSERT_EQ_FLOAT(map->objects[0].camera.pitch, 56.0f, 0.001f);
    ASSERT_EQ_FLOAT(map->objects[0].camera.yaw, 179.9584f, 0.001f);
    ASSERT_EQ_FLOAT(map->objects[0].camera.fov, 27.7998f, 0.001f);
    ASSERT_EQ_FLOAT(map->objects[0].camera.znear, 0.0998f, 0.001f);
    ASSERT_EQ_FLOAT(map->objects[0].camera.zfar, 400.0f, 0.001f);

    ASSERT_STR_EQ(map->objects[1].name, "Marine");
    ASSERT_EQ_INT(map->objects[1].id, 1);
    ASSERT_STR_EQ(map->objects[1].model, "Assets\\Units\\Terran\\Marine\\Marine.m3");
    ASSERT_EQ_INT(map->objects[1].type, SC2_OBJECT_UNIT);
    ASSERT_EQ_FLOAT(map->objects[1].position.x, 3.5f, 0.001f);
    ASSERT_EQ_FLOAT(map->objects[1].position.y, 3.5f, 0.001f);
    ASSERT_EQ_FLOAT(map->objects[1].position.z, 0.25f, 0.001f);
    ASSERT_EQ_FLOAT(map->objects[1].angle, 0.75f, 0.001f);
    ASSERT_EQ_INT(map->objects[1].player, 2);

    ASSERT_STR_EQ(map->objects[3].name, "BillboardTall");
    ASSERT_EQ_INT(map->objects[3].id, 3);
    ASSERT_EQ_INT(map->objects[3].type, SC2_OBJECT_DOODAD);
    ASSERT_STR_EQ(map->objects[3].model, "");
    ASSERT_EQ_FLOAT(map->objects[3].position.z, 8.0f, 0.001f);
    ASSERT_EQ_INT(map->objects[3].flags, SC2_OBJECT_HEIGHT_ABSOLUTE | SC2_OBJECT_FORCE_PLACEMENT);

    ASSERT_STR_EQ(map->objects[4].name, "MineralField");
    ASSERT_EQ_INT(map->objects[4].type, SC2_OBJECT_DOODAD);
    ASSERT_STR_EQ(map->objects[4].model, "Assets\\Doodads\\Terran\\MineralField\\MineralField.m3");
    ASSERT_EQ_FLOAT(map->objects[4].position.x, 4.0f, 0.001f);
    ASSERT_EQ_FLOAT(map->objects[4].position.y, 3.0f, 0.001f);
    ASSERT_EQ_FLOAT(map->objects[4].position.z, 0.0f, 0.001f);

    ASSERT_STR_EQ(map->t3Terrain.tile_set, "Fixture");
    ASSERT_EQ_FLOAT(map->t3Terrain.height_quantize_bias, 0.0f, 0.001f);
    ASSERT_EQ_FLOAT(map->t3Terrain.height_quantize_scale, 1.0f, 0.001f);
    ASSERT_EQ_FLOAT(map->t3Terrain.standard_height, 0.0f, 0.001f);
    ASSERT_EQ_INT(map->t3Terrain.fog_enabled, true);
    ASSERT_EQ_FLOAT(map->t3Terrain.fog_density, 0.25f, 0.001f);
    ASSERT_EQ_FLOAT(map->t3Terrain.fog_falloff, 0.5f, 0.001f);
    ASSERT_EQ_FLOAT(map->t3Terrain.fog_start_height, -1.5f, 0.001f);
    ASSERT_EQ_INT(map->t3Terrain.fog_color.a, 255);
    ASSERT_EQ_INT(map->t3Terrain.fog_color.r, 10);
    ASSERT_EQ_INT(map->t3Terrain.fog_color.g, 20);
    ASSERT_EQ_INT(map->t3Terrain.fog_color.b, 30);
    ASSERT_EQ_INT(map->t3Terrain.num_terrain_textures, 2);
    ASSERT_STR_EQ(map->t3Terrain.terrain_textures[0].diffuse, "Assets\\Textures\\Terrain\\FixtureGrass_Diffuse.dds");
    ASSERT_STR_EQ(map->t3Terrain.terrain_textures[0].normal, "Assets\\Textures\\Terrain\\FixtureGrass_Diffuse_normal.dds");
    ASSERT_STR_EQ(map->t3Terrain.terrain_textures[1].diffuse, "Assets\\Textures\\Terrain\\FixtureDirt_Diffuse.dds");

    ASSERT_EQ_INT(map->t3Terrain.num_cliff_sets, 1);
    ASSERT_STR_EQ(map->t3Terrain.cliff_sets[0].name, "FixtureCliff0");
    ASSERT_STR_EQ(map->t3Terrain.cliff_sets[0].mesh, "CliffNatural0");
    ASSERT_EQ_INT(map->t3Terrain.num_cliff_cells, 2);
    ASSERT_EQ_INT(map->t3Terrain.cliff_cells[0].index, 0);
    ASSERT_EQ_INT(map->t3Terrain.cliff_cells[0].flags, 1);
    ASSERT_EQ_INT(map->t3Terrain.cliff_cells[0].cliff_set, 0);
    ASSERT_EQ_INT(map->t3Terrain.cliff_cells[0].variant, 2);
    ASSERT_EQ_INT(map->t3Terrain.cliff_cells[1].index, 1);
    ASSERT_EQ_INT(map->t3Terrain.cliff_cells[1].flags, 3);

    ASSERT_EQ_INT(map->lighting.enabled, true);
    ASSERT_STR_EQ(map->lighting.id, "Fixture");
    ASSERT_EQ_FLOAT(map->lighting.ambient_color.x, 0.1f, 0.001f);
    ASSERT_EQ_FLOAT(map->lighting.ambient_color.y, 0.2f, 0.001f);
    ASSERT_EQ_FLOAT(map->lighting.ambient_color.z, 0.3f, 0.001f);
    ASSERT_EQ_INT(map->lighting.directional[SC2_LIGHT_KEY].enabled, true);
    ASSERT_EQ_FLOAT(map->lighting.directional[SC2_LIGHT_KEY].color.x, 0.4f, 0.001f);
    ASSERT_EQ_FLOAT(map->lighting.directional[SC2_LIGHT_KEY].color.y, 0.5f, 0.001f);
    ASSERT_EQ_FLOAT(map->lighting.directional[SC2_LIGHT_KEY].color.z, 0.6f, 0.001f);
    ASSERT_EQ_FLOAT(map->lighting.directional[SC2_LIGHT_KEY].color_multiplier, 2.0f, 0.001f);
    ASSERT_EQ_FLOAT(map->lighting.directional[SC2_LIGHT_KEY].spec_color_multiplier, 3.0f, 0.001f);
    ASSERT_EQ_FLOAT(map->lighting.directional[SC2_LIGHT_KEY].direction.z, -1.0f, 0.001f);
    ASSERT_EQ_INT(map->lighting.directional[SC2_LIGHT_FILL].enabled, true);
    ASSERT_EQ_FLOAT(map->lighting.directional[SC2_LIGHT_FILL].color_multiplier, 4.0f, 0.001f);
    ASSERT_EQ_FLOAT(map->lighting.directional[SC2_LIGHT_FILL].direction.x, 1.0f, 0.001f);
    ASSERT_EQ_INT(map->lighting.directional[SC2_LIGHT_BACK].enabled, true);
    ASSERT_EQ_FLOAT(map->lighting.directional[SC2_LIGHT_BACK].color_multiplier, 5.0f, 0.001f);
    ASSERT_EQ_FLOAT(map->lighting.directional[SC2_LIGHT_BACK].direction.y, 1.0f, 0.001f);
}

static void test_sc2_map_loads_binary_terrain_layers(void) {
    sc2Map_t *map;

    setup_sc2_tests();
    ASSERT(SC2_MapLoad("Maps\\Test\\Tiny.SC2Map"));
    map = SC2_MapCurrent();

    ASSERT_NOT_NULL(map->t3CellFlags);
    if (map->t3CellFlags) {
        ASSERT_EQ_INT(map->t3CellFlags->fourcc, MAKEFOURCC('L','F','C','T'));
        ASSERT_EQ_INT(map->t3CellFlags->width, 8);
        ASSERT_EQ_INT(map->t3CellFlags->height, 6);
        ASSERT_EQ_INT(map->t3CellFlags->data[10], 0x1a);
        ASSERT_EQ_INT(map->t3CellFlags->data[29], 0x2d);
    }

    ASSERT_NOT_NULL(map->t3SyncCliffLevel);
    if (map->t3SyncCliffLevel) {
        ASSERT_EQ_INT(map->t3SyncCliffLevel->fourcc, MAKEFOURCC('C','L','I','F'));
        ASSERT_EQ_INT(map->t3SyncCliffLevel->width, 8);
        ASSERT_EQ_INT(map->t3SyncCliffLevel->height, 6);
        ASSERT_EQ_INT(map->t3SyncCliffLevel->data[10], 11);
        ASSERT_EQ_INT(map->t3SyncCliffLevel->data[29], 30);
    }

    ASSERT_NOT_NULL(map->t3HeightMap);
    if (map->t3HeightMap) {
        ASSERT_EQ_INT(map->t3HeightMap->fourcc, MAKEFOURCC('H','M','A','P'));
        ASSERT_EQ_INT(map->t3HeightMap->width, 9);
        ASSERT_EQ_INT(map->t3HeightMap->height, 7);
        ASSERT_EQ_INT(map->t3HeightMap->data[0].adjustment, 0);
        ASSERT_EQ_INT(map->t3HeightMap->data[0].height, 1);
        ASSERT_EQ_INT(map->t3HeightMap->data[0].extra, 0);
        ASSERT_EQ_INT(map->t3HeightMap->data[42].adjustment, 0);
        ASSERT_EQ_INT(map->t3HeightMap->data[42].height, 13);
        ASSERT_EQ_INT(map->t3HeightMap->data[42].extra, 0);
        ASSERT_EQ_FLOAT(SC2_MapHeightAtPoint(0.0f, 0.0f), 0.0f, 0.001f);
        ASSERT_EQ_FLOAT(SC2_MapHeightAtPoint(6.0f, 4.0f), 12.0f, 0.001f);
        ASSERT_EQ_FLOAT(SC2_MapHeightAtPoint(map->objects[1].position.x,
                                             map->objects[1].position.y),
                        10.0f,
                        0.001f);
        ASSERT_EQ_FLOAT(SC2_MapHeightAtPoint(map->objects[1].position.x,
                                             map->objects[1].position.y) + map->objects[1].position.z,
                        10.25f,
                        0.001f);
    }
    ASSERT_NOT_NULL(map->t3SyncHeightMap);
    if (map->t3SyncHeightMap) {
        ASSERT_EQ_INT(map->t3SyncHeightMap->fourcc, MAKEFOURCC('S','M','A','P'));
        ASSERT_EQ_INT(map->t3SyncHeightMap->width, 9);
        ASSERT_EQ_INT(map->t3SyncHeightMap->height, 7);
        ASSERT_EQ_INT(map->t3SyncHeightMap->data[42].height, 128);
    }

    ASSERT_NOT_NULL(map->t3TextureMasks);
    if (map->t3TextureMasks) {
        ASSERT_EQ_INT(map->t3TextureMasks->fourcc, MAKEFOURCC('M','A','S','K'));
        ASSERT_EQ_INT(map->t3TextureMasks->width, 4);
        ASSERT_EQ_INT(map->t3TextureMasks->height, 4);
        ASSERT_EQ_INT(map->t3TextureMasksSize, 80);
        ASSERT_EQ_INT(map->t3TextureMasks->data[0], 0x12);
        ASSERT_EQ_INT(map->t3TextureMasks->data[8], 0xab);
    }
}

void run_sc2_map_tests(void) {
    RUN_TEST(test_sc2_fixture_archive_lists_map_root);
    RUN_TEST(test_sc2_fixture_short_name_resolves);
    RUN_TEST(test_sc2_map_loads_xml_objects_and_terrain);
    RUN_TEST(test_sc2_map_loads_binary_terrain_layers);
    SC2_MapShutdown();
    FS_Shutdown();
}
