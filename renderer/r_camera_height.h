#ifndef R_CAMERA_HEIGHT_H
#define R_CAMERA_HEIGHT_H

#include "r_local.h"

typedef struct {
    float *samples;
    uint32_t width, height;
    VECTOR2 origin;
    float cell_size;
} cameraHeightMap_t;

typedef struct {
    cameraHeightMap_t *map;
    void const * data;
    uint32_t width, height_count, radius, samples;
    VECTOR2 origin;
    float cell_size;
    float (*get_height)(void const * data, uint32_t x, uint32_t y);
} cameraHeightBuild_t;

void R_BuildCameraHeightMap(cameraHeightBuild_t const *params);
void R_FreeCameraHeightMap(cameraHeightMap_t *map);
float R_SampleCameraHeightMap(cameraHeightMap_t const *map, float x, float y);

#endif
