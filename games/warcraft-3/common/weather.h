#ifndef wc3_weather_h
#define wc3_weather_h

#include "common/shared.h"

#define WC3_WEATHER_GAME_COMMAND "wc3_weather"

typedef enum {
    WC3_WEATHER_CMD_CLEAR = 1,
    WC3_WEATHER_CMD_ADD,
    WC3_WEATHER_CMD_ENABLE,
    WC3_WEATHER_CMD_REMOVE,
} wc3WeatherCommandType_t;

typedef struct {
    DWORD type;
    DWORD handle;
    DWORD effect_id;
    BOX2 bounds;
    DWORD enabled;
} wc3WeatherCommand_t;

#endif
