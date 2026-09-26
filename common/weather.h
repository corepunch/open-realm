#ifndef common_weather_h
#define common_weather_h

#include "common/shared.h"

#define MAX_WEATHER_EFFECTS 256 // effects; bounds the per-client weather snapshot and stable server registry

#include "common/game_datagram.h"

#define MAX_LIGHTNING_EFFECTS 128 // effects; bounds the per-client lightning snapshot and stable server registry

typedef struct {
    uint32_t handle;
    uint32_t effect_id;
    box2_t bounds;
    uint32_t enabled;
} wc3WeatherEffect_t;

typedef struct lightningeffect_s {
    uint32_t handle;
    uint32_t effect_id; /* producer-defined presentation record ID */
    vec3_t source;
    vec3_t target;
    color32_t color;   /* multiplicative RGBA tint */
    uint32_t start_time;
    uint32_t end_time;  /* 0 = persistent until removed */
} lightningEffect_t;



#endif
