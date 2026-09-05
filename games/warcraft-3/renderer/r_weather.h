#ifndef wc3_r_weather_h
#define wc3_r_weather_h

#include "renderer/r_local.h"

void R_WeatherInit(void);
void R_WeatherShutdown(void);
void R_WeatherRegisterMap(void);
void R_WeatherEmit(void);

#endif
