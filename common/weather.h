#ifndef common_weather_h
#define common_weather_h

#include "common/shared.h"

#define MAX_WEATHER_EFFECTS 256 // effects; bounds the per-client weather snapshot and stable server registry

#include "common/game_datagram.h"

#define MAX_LIGHTNING_EFFECTS 128 // effects; bounds the per-client lightning snapshot and stable server registry

typedef struct {
    uint32_t handle;
    uint32_t effect_id;
    BOX2 bounds;
    uint32_t enabled;
} wc3WeatherEffect_t;

typedef struct LIGHTNINGEFFECT {
    uint32_t handle;
    uint32_t effect_id; /* producer-defined presentation record ID */
    VECTOR3 source;
    VECTOR3 target;
    COLOR32 color;   /* multiplicative RGBA tint */
    uint32_t start_time;
    uint32_t end_time;  /* 0 = persistent until removed */
} LIGHTNINGEFFECT;
typedef LIGHTNINGEFFECT *LPLIGHTNINGEFFECT;
typedef LIGHTNINGEFFECT const *LPCLIGHTNINGEFFECT;

#endif
