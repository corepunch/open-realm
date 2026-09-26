#include "test.h"
#include "renderer/r_local.h"
#include "renderer/r_game.h"
#include "renderer/r_camera_height.h"

refImport_t ri;
static float camera_step(void const *data, uint32_t x, uint32_t y) { (void)data; (void)y; return x < 24 ? 0 : 10; }
static handle_t camera_alloc(long size) { return calloc(1, (size_t)size); }

size2_t R_GetWindowSize(void) { return (size2_t){ 1024, 768 }; }
bool R_TraceModel(renderEntity_t const *ent, line3_t const *line, float *distance) {
    (void)ent; (void)line; (void)distance; return false;
}

/* Exact snapshot terrain can change while the rendered target stays above a narrow depression. */
TEST(renderer_view, pan_plane_uses_rendered_target) {
    viewDef_t view = { .viewport = { 0, 0.22f, 1, 0.76f }, .target = { 0, 0, 10 } };
    matrix4_t proj, camera;
    vector3_t point, eye = { 0, -10, 20 }, dir = { 0, 10, -10 };
    Matrix4_lookAt(&camera, &eye, &dir, &(vector3_t){ 0, 0, 1 });
    Matrix4_perspective(&proj, 60, 4.0f / 3.0f, 1, 1000);
    Matrix4_multiply(&proj, &camera, &view.viewProjectionMatrix);
    FOR_LOOP(i, 3) {
        view.camerastate[0].origin.z = i * 10.0f;
        T_ASSERT(R_TraceCameraPlane(&view, 512, 307.2f, &point));
        T_FEQ(point.x, 0, 0.001f); T_FEQ(point.y, 0, 0.001f); T_FEQ(point.z, 10, 0.001f);
    }
    view.target.z = 15;
    T_ASSERT(R_TraceCameraPlane(&view, 512, 307.2f, &point));
    T_FEQ(point.z, 15, 0.001f);
    T_ASSERT(!R_TraceCameraPlane(NULL, 512, 307.2f, &point));
    T_ASSERT(!R_TraceCameraPlane(&view, 512, 307.2f, NULL));
}

/* A dense box filter spreads a cliff over the entire footprint, including between grid vertices. */
TEST(renderer_view, camera_height_dense_blur) {
    cameraHeightMap_t map = {0};
    refImport_t saved = ri;
    ri.MemAlloc = camera_alloc; ri.MemFree = free;
    R_BuildCameraHeightMap(&(cameraHeightBuild_t){ .map = &map, .width = 49, .height_count = 3,
        .radius = 8, .samples = 17, .origin = {10, 20}, .cell_size = 2, .get_height = camera_step });
    float prev = R_SampleCameraHeightMap(&map, 40, 22);
    T_FEQ(prev, 0, 0.0001f);
    for (float x = 40.5f; x <= 74; x += 0.5f) {
        float cur = R_SampleCameraHeightMap(&map, x, 22);
        T_FEQ(cur - prev, 10.0f / (17 * 4), 0.0001f);
        prev = cur;
    }
    T_FEQ(prev, 10, 0.0001f);
    T_FEQ(R_SampleCameraHeightMap(&map, -100, -100), 0, 0.0001f);
    T_FEQ(R_SampleCameraHeightMap(&map, 200, 200), 10, 0.0001f);
    R_FreeCameraHeightMap(&map);
    T_NULL(map.samples);
    ri = saved;
}
