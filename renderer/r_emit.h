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

__attribute__((unused))
static VECTOR3 FX_GenerateRandomOrigin(float length, float width) {
	return (VECTOR3){
		(float)(((double)rand() / (double)RAND_MAX - 0.5) * length),
		(float)(((double)rand() / (double)RAND_MAX - 0.5) * width),
		0.0f,
	};
}

/* Frame-relative accumulator emission.  Each caller supplies a per-emitter float accumulator
   that survives across frames; rate * dt is added to it and particles are spawned whenever the
   accumulator crosses 1.0.  Caps at 2.0 to suppress bursts after lag spikes.
   Pattern derived from WoWee's M2Renderer::emitParticles. */
__attribute__((unused))
static void R_EmitParticles(float rate, float *accum, DWORD delta_ms,
                            void (*spawn)(void *), void *ctx) {
	if (rate <= 0.0f || delta_ms == 0 || !accum) return;
	*accum += rate * (float)delta_ms / 1000.0f;
	while (*accum >= 1.0f) {
		*accum -= 1.0f;
		spawn(ctx);
	}
	if (*accum > 2.0f) *accum = 0.0f;
}

#endif
