#include "test.h"
#include "renderer/r_local.h"
#include "renderer/r_emit.h"
#include <stdarg.h>
#include <stdlib.h>

refImport_t ri;
static DWORD load_count, release_count, register_count;
static BOOL fail_load, touch_during_registration;
static DWORD spawn_count;

static HANDLE test_alloc(long size) { return calloc(1, (size_t)size); }
static void test_free(HANDLE memory) { free(memory); }
static void test_error(LPCSTR format, ...) { (void)format; T_ASSERT(false); }
static void test_spawn(void *context) { (*(DWORD *)context)++; }

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
