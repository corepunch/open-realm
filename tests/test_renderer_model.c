#include "test.h"
#include "renderer/r_local.h"
#include "renderer/r_emit.h"
#include <stdarg.h>
#include <stdlib.h>

refImport_t ri;
struct render_globals tr;
static DWORD load_count, release_count, register_count;
static BOOL fail_load, touch_during_registration;
static DWORD spawn_count;
static LPTEXTURE texture_load_result;

static HANDLE test_alloc(long size) { return calloc(1, (size_t)size); }
static void test_free(HANDLE memory) { free(memory); }
static void test_error(LPCSTR format, ...) { (void)format; T_ASSERT(false); }
static void test_spawn(void *context) { (*(DWORD *)context)++; }

LPTEXTURE R_LoadTexture(LPCSTR filename) { (void)filename; return texture_load_result; }

LPMODEL R_GameLoadModel(LPCSTR filename) {
    (void)filename; load_count++;
    return fail_load ? NULL : test_alloc(sizeof(model_t));
}

void R_GameReleaseModel(LPMODEL model) { release_count++; test_free(model); }

void R_GameRegisterMap(LPCSTR map) {
    (void)map; register_count++;
    if (touch_during_registration) R_LoadModel("models/touched.mdx");
}

static void reset_registry(void) {
    R_ShutdownModels();
    ri.MemAlloc = test_alloc; ri.MemFree = test_free; ri.error = test_error;
    load_count = release_count = register_count = 0;
    fail_load = touch_during_registration = false;
}

TEST(renderer_model, filename_cache_hit_and_miss) {
    LPMODEL first, second;
    reset_registry();
    first = R_LoadModel("Models/Foo.mdx");
    second = R_LoadModel("models/foo.mdx");
    T_ASSERT(first == second); T_EQ(load_count, 1); T_EQ(release_count, 0);
    R_ReleaseModel(first); R_ReleaseModel(second);
    R_RegisterMapAssets("next");
    T_EQ(register_count, 1); T_EQ(release_count, 1);
}

TEST(renderer_model, registration_keeps_touched_model_then_reclaims_it) {
    LPMODEL model;
    reset_registry();
    model = R_LoadModel("models/touched.mdx"); R_ReleaseModel(model);
    touch_during_registration = true; R_RegisterMapAssets("current");
    T_EQ(load_count, 1); T_EQ(release_count, 0);
    R_ReleaseModel(model); touch_during_registration = false; R_RegisterMapAssets("next");
    T_EQ(release_count, 1);
}

TEST(renderer_model, missing_model_placeholder_is_cached) {
    LPMODEL first, second;
    reset_registry(); fail_load = true;
    first = R_LoadModel("models/missing.mdx"); second = R_LoadModel("models/missing.mdx");
    T_ASSERT(first == second); T_EQ(load_count, 1);
    R_ReleaseModel(first); R_ReleaseModel(second); R_RegisterMapAssets("next");
    T_EQ(release_count, 1);
}

TEST(renderer_model, unknown_model_release_is_immediate) {
    LPMODEL model;
    reset_registry(); model = test_alloc(sizeof(*model));
    R_ReleaseModel(model); T_EQ(release_count, 1);
}

TEST(renderer_texture, cached_registration_preserves_newer_texture_indices) {
    TEXTURE first = { .texid = 100 }, second = { .texid = 101 };

    texture_load_result = &first; T_EQ(R_RegisterTextureFile("first"), 100);
    texture_load_result = &second; T_EQ(R_RegisterTextureFile("second"), 101);
    T_ASSERT(R_FindTextureByID(100) == &first); T_ASSERT(R_FindTextureByID(101) == &second);
    texture_load_result = &first; T_EQ(R_RegisterTextureFile("first"), 100);
    T_ASSERT(R_FindTextureByID(100) == &first); T_ASSERT(R_FindTextureByID(101) == &second);
}

TEST(renderer_texture, resident_registry_keeps_entries_beyond_configstring_limit) {
    static TEXTURE placeholder = { .texid = 77 };
    char path[64];

    ri.MemAlloc = test_alloc; ri.MemFree = test_free; tr.texture[TEX_PLACEHOLDER] = &placeholder;
    FOR_LOOP(i, MAX_IMAGES * 4 + 1) {
        snprintf(path, sizeof(path), "Textures/Registry/%u.blp", i);
        R_CacheLoadedTexture(path, &placeholder);
    }
    T_ASSERT(R_FindLoadedTexture("textures/registry/1024.BLP") == &placeholder);
}

TEST(renderer_model, clock_emission_needs_no_instance_accumulator) {
    spawn_count = 0;
    R_EmitParticlesAtTime(10.0f, 1050, 100, test_spawn, &spawn_count);
    T_EQ(spawn_count, 1);
}

TEST(renderer_model, clock_emission_ignores_zero_rate_and_delta) {
    spawn_count = 0;
    R_EmitParticlesAtTime(0.0f, 1050, 100, test_spawn, &spawn_count);
    R_EmitParticlesAtTime(10.0f, 1050, 0, test_spawn, &spawn_count);
    T_EQ(spawn_count, 0);
}

TEST(renderer_alpha, msaa_request_normalizes_off_and_caps_driver_input) {
    T_EQ(R_MsaaRequest(-1), 0); T_EQ(R_MsaaRequest(1), 0);
    T_EQ(R_MsaaRequest(4), 4); T_EQ(R_MsaaRequest(64), BZ_MSAA_MAX);
}

TEST(renderer_alpha, active_samples_require_a_real_multisample_buffer) {
    T_EQ(R_MsaaActiveSamples(0, 4), 0); T_EQ(R_MsaaActiveSamples(1, 1), 0);
    T_EQ(R_MsaaActiveSamples(1, 4), 4);
}

TEST(renderer_instances, upload_size_uses_wide_arithmetic) {
    T_EQ(R_InstanceBufferBytes(465524), (size_t)29793536);
}

TEST(renderer_instances, dynamic_capacity_reuses_and_grows_power_of_two) {
    T_EQ(R_InstanceBufferCapacity(0, 1), (DWORD)16);
    T_EQ(R_InstanceBufferCapacity(16, 16), (DWORD)16);
    T_EQ(R_InstanceBufferCapacity(16, 17), (DWORD)32);
}

TEST(renderer_stats, triangles_include_instanced_amplification) {
    T_EQ(R_PrimitiveTriangles(GL_TRIANGLES, 12, 100), (uint64_t)400);
    T_EQ(R_PrimitiveTriangles(GL_LINES, 12, 100), (uint64_t)0);
}
