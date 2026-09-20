#ifndef common_weather_h
#define common_weather_h

#include "common/shared.h"

#define MAX_WEATHER_EFFECTS 256 // effects; bounds the per-client weather snapshot and stable server registry

#include "common/game_datagram.h"

#define MAX_LIGHTNING_EFFECTS 128 // effects; bounds the per-client lightning snapshot and stable server registry

typedef struct {
    DWORD handle;
    DWORD effect_id;
    BOX2 bounds;
    DWORD enabled;
} wc3WeatherEffect_t;

typedef struct {
    DWORD handle;
    DWORD effect_id; /* producer-defined presentation record ID */
    VECTOR3 source;
    VECTOR3 target;
    COLOR32 color;   /* multiplicative RGBA tint */
    DWORD start_time;
    DWORD end_time;  /* 0 = persistent until removed */
} lightningEffect_t;

#endif
