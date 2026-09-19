#ifndef common_weather_h
#define common_weather_h

#include "common/shared.h"
#include "common/terrain_mask.h"

#define MAX_WEATHER_EFFECTS 256 // effects; bounds the per-client weather snapshot and stable server registry
#define BZ_GAME_DATAGRAM_ENTITY_TINTS 0x8000u // bit mask; reserves the first weather-count bit for entity RGBA data
_Static_assert(MAX_WEATHER_EFFECTS < BZ_GAME_DATAGRAM_TERRAIN_MASK, "weather count must leave the extension bits free");

typedef struct {
    DWORD handle;
    DWORD effect_id;
    BOX2 bounds;
    DWORD enabled;
} wc3WeatherEffect_t;

#endif
