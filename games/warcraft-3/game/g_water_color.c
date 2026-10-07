#include "g_local.h"

static uint8_t G_WaterColorComponent(int32_t value) {
    return (uint8_t)MAX(0, MIN(255, value));
}

/* Publish the authoritative render-only water tint through the generic scene contract. */
void G_WaterBaseColorPublish(void) {
    color32_t const color = level.water_base_color;
    char value[MAX_PATHLEN];

    snprintf(value, sizeof(value), "%.9g %.9g %.9g %.9g",
             (double)color.r / 255.0, (double)color.g / 255.0,
             (double)color.b / 255.0, (double)color.a / 255.0);
    gi.configstring(CS_SCENE_WATER_COLOR, value);
}

void G_WaterBaseColorInitMap(void) {
    level.water_base_color = (color32_t){ 255, 255, 255, 255 };
    G_WaterBaseColorPublish();
}

void G_WaterBaseColorSet(int32_t red, int32_t green, int32_t blue, int32_t alpha) {
    level.water_base_color = (color32_t){
        G_WaterColorComponent(red), G_WaterColorComponent(green),
        G_WaterColorComponent(blue), G_WaterColorComponent(alpha),
    };
    G_WaterBaseColorPublish();
}
