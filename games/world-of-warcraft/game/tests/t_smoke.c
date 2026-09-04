/*
 * t_smoke.c — In-engine smoke tests (see CONTRIBUTING.md).
 *
 * These run inside the REAL, fully-linked game module under a headless
 * dedicated server — no mock harness, no re-declared globals, no per-test
 * Makefile source list.  They call the same production functions the shipping
 * binary uses.  Build/run with:
 *
 *   make test-wow-engine                 # every test
 *   make test-wow-engine PATTERN='wow_smoke.*'
 *
 * The whole file is compiled only when the game module is built with
 * -DBZ_TESTS, so production builds contain none of this.
 */
#ifdef BZ_TESTS

#include "test.h"
#include "game/g_wow_local.h"
#include "renderer/r_game.h"
#include "common/stb_dbc.h"

/* Wow_Clamp is real production code (g_wow.c); testing it here proves the
 * runtime exercises the actual game module, not a copy. */
TEST(wow_smoke, clamp) {
    T_FEQ(Wow_Clamp(5.0f, 0.0f, 10.0f), 5.0f, 0.001f);
    T_FEQ(Wow_Clamp(-3.0f, 0.0f, 10.0f), 0.0f, 0.001f);
    T_FEQ(Wow_Clamp(42.0f, 0.0f, 10.0f), 10.0f, 0.001f);
}

TEST(wow_smoke, default_camera_authors_lens) {
    gameCamera_t cam;
    T_ASSERT(CL_GameDefaultCamera(&cam));
    T_EQ((DWORD)cam.fov, (DWORD)WOW_CAMERA_FOV);
    T_FEQ(cam.znear, WOW_WORLD_NEAR_CLIP, 0.001f);
    T_FEQ(cam.zfar, WOW_WORLD_FAR_CLIP, 0.001f);
}

TEST(wow_smoke, read32_little_endian) {
    BYTE bytes[4] = { 0x78, 0x56, 0x34, 0x12 };
    T_EQ(Stb_DbcRead32(bytes), 0x12345678u);
}

TEST(wow_smoke, read_float_roundtrip) {
    FLOAT expected = 1.5f;
    BYTE bytes[4];
    memcpy(bytes, &expected, sizeof(bytes));
    T_FEQ(Stb_DbcReadFloat(bytes), expected, 0.0f);
}

TEST(wow_smoke, wmo_floor_ray) {
    VECTOR3 start = { 0.25f, 0.25f, 2.0f }, end = { 0.25f, 0.25f, -2.0f };
    VECTOR3 a = { 0.0f, 0.0f, 0.0f }, b = { 1.0f, 0.0f, 0.0f }, c = { 0.0f, 1.0f, 0.0f };
    FLOAT fraction = -1.0f;
    T_ASSERT(CM_WowRayTriangle(&start, &end, &a, &b, &c, &fraction));
    T_FEQ(fraction, 0.5f, 0.0001f);
    start.x = end.x = 2.0f;
    T_ASSERT(!CM_WowRayTriangle(&start, &end, &a, &b, &c, &fraction));
}

TEST(wow_smoke, wmo_bsp_traversal) {
    VECTOR3 start = { 0.75f, 0.10f, 2.0f }, end = { 0.75f, 0.10f, -2.0f };
    FLOAT fraction;
    T_ASSERT(CM_WowTestBspRay(&start, &end, &fraction));
    T_FEQ(fraction, 0.5f, 0.0001f);
    start.x = end.x = 0.25f;
    T_ASSERT(!CM_WowTestBspRay(&start, &end, &fraction));
}

TEST(wow_smoke, wmo_wall_trace_rejects_floor_triangles) {
    T_ASSERT(CM_WowTestWallRay(true));
    T_ASSERT(!CM_WowTestWallRay(false));
}

TEST(wow_smoke, terrain_slope_blocks_mountains_but_not_wmo_steps) {
    VECTOR3 from = { 0, 0, 0 }, gentle = { 0.7f, 0, 0.5f }, steep = { 0.7f, 0, 1.0f };
    T_ASSERT(Wow_TerrainMoveWalkable(&from, &gentle, gentle.z));
    T_ASSERT(!Wow_TerrainMoveWalkable(&from, &steep, steep.z));
    T_ASSERT(Wow_TerrainMoveWalkable(&from, &steep, 0.0f));
    steep.z = -1.0f;
    T_ASSERT(!Wow_TerrainMoveWalkable(&from, &steep, steep.z));
}

TEST(wow_smoke, missing_m2_uses_fallback_model_and_disables_static_instancing) {
    model_t model = { 0 };
    struct { void *file; } fake = { NULL };

    model.modeltype = ID_MD20;
    model.m2 = (m2Model_t *)&fake;
    T_ASSERT(!R_ModelCanStaticInstance(&model));
}

#endif /* BZ_TESTS */
