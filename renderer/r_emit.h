#ifndef r_emit_h
#define r_emit_h

#include "r_local.h"
#include <math.h>
#include <stdlib.h>

static VECTOR3 FX_GenerateRandomDirection(float latitude) {
	float theta = (float)(((double)rand() / (double)RAND_MAX) * 2.0 * M_PI);
	float phi = (float)(((double)rand() / (double)RAND_MAX) * latitude);
	return (VECTOR3){ sinf(phi) * cosf(theta), sinf(phi) * sinf(theta), cosf(phi) };
}

static VECTOR3 FX_GenerateRandomOrigin(float length, float width) {
	return (VECTOR3){
		(float)(((double)rand() / (double)RAND_MAX - 0.5) * length),
		(float)(((double)rand() / (double)RAND_MAX - 0.5) * width),
		0.0f,
	};
}

__attribute__((unused))
static void R_EmitParticles(float rate, DWORD now_ms, DWORD delta_ms,
                            void (*spawn)(void *), void *ctx) {
	if (rate <= 0.0f || delta_ms == 0) return;
	float interval_ms = 1000.0f / rate;
	DWORD last_ms = now_ms - delta_ms;
	DWORD start_ms = last_ms - last_ms % 1000;
	for (float t = (float)start_ms; t < (float)now_ms; t += interval_ms) {
		if (t >= (float)last_ms)
			spawn(ctx);
	}
}

#endif
