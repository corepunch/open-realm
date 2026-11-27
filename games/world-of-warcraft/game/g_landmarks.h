#ifndef WOW_LANDMARKS_H
#define WOW_LANDMARKS_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t id, row;
    float x, y, u, v;
    char name[128];
} wowLandmark_t;

/* Validate one AreaPOI row against the physical map and current map bounds. */
bool Wow_LandmarkRead(uint8_t const *data, uint32_t size, uint32_t row,
                      uint32_t map, float left, float right, float top, float bottom,
                      wowLandmark_t *out);
uint32_t Wow_LandmarkRows(uint8_t const *data, uint32_t size);
#endif
