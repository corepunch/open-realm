#include "r_camera_height.h"

/* Build a client-only box-filtered terrain source once per map; camera queries then remain a single bilinear lookup. */
void R_BuildCameraHeightMap(cameraHeightBuild_t const *params) {
    cameraHeightMap_t *map;
    float step;

    if (!params || !params->map || !params->get_height || !params->width || !params->height_count ||
        params->samples < 2 || !params->cell_size)
        return;
    map = params->map;
    R_FreeCameraHeightMap(map);
    map->samples = ri.MemAlloc((long)((size_t)params->width * params->height_count * sizeof(*map->samples)));
    if (!map->samples) {
        fprintf(stderr, "Renderer camera height: failed to allocate %ux%u blurred samples\n",
                (unsigned)params->width, (unsigned)params->height_count);
        return;
    }
    map->width = params->width;
    map->height = params->height_count;
    map->origin = params->origin;
    map->cell_size = params->cell_size;
    step = params->radius * 2.0f / (params->samples - 1);
    FOR_LOOP(y, params->height_count) {
        FOR_LOOP(x, params->width) {
            float sum = 0.0f;
            uint32_t count = 0;
            FOR_LOOP(iy, params->samples) {
                /* Even kernels have no center tap; rounded offsets keep the footprint symmetric around the source cell. */
                int sy = (int)y + (int)lroundf(-(float)params->radius + iy * step);
                sy = MAX(0, MIN((int)params->height_count - 1, sy));
                FOR_LOOP(ix, params->samples) {
                    int sx = (int)x + (int)lroundf(-(float)params->radius + ix * step);
                    sx = MAX(0, MIN((int)params->width - 1, sx));
                    sum += params->get_height(params->data, (uint32_t)sx, (uint32_t)sy);
                    count++;
                }
            }
            map->samples[x + y * params->width] = sum / count;
        }
    }
}

void R_FreeCameraHeightMap(cameraHeightMap_t *map) {
    if (!map) return;
    SAFE_DELETE(map->samples, ri.MemFree);
    map->width = map->height = 0;
}

float R_SampleCameraHeightMap(cameraHeightMap_t const *map, float x, float y) {
    float gx, gy, tx, ty, h0, h1;
    uint32_t x0, y0, x1, y1;

    if (!map || !map->samples || !map->width || !map->height || !map->cell_size) return 0.0f;
    gx = (x - map->origin.x) / map->cell_size; gy = (y - map->origin.y) / map->cell_size;
    gx = MAX(0.0f, MIN((float)map->width - 1.0f, gx)); gy = MAX(0.0f, MIN((float)map->height - 1.0f, gy));
    x0 = (uint32_t)floorf(gx); y0 = (uint32_t)floorf(gy); x1 = MIN(map->width - 1, x0 + 1); y1 = MIN(map->height - 1, y0 + 1);
    tx = gx - x0; ty = gy - y0;
    h0 = LerpNumber(map->samples[x0 + y0 * map->width], map->samples[x1 + y0 * map->width], tx);
    h1 = LerpNumber(map->samples[x0 + y1 * map->width], map->samples[x1 + y1 * map->width], tx);
    return LerpNumber(h0, h1, ty);
}
