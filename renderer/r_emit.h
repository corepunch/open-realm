#ifndef r_emit_h
#define r_emit_h

#include "r_local.h"
#include <math.h>
#include <stdlib.h>

/* Preserve sub-unit authored sizes in the shared compact particle curve. */
static inline void R_EncodeParticleSize(cparticle_t *particle, FLOAT const values[3]) {
    FLOAT peak = MAX(values[0], MAX(values[1], values[2]));
    particle->size_value_scale = peak > 0 ? peak / 255.0f : 1.0f;
    FOR_LOOP(i, 3) particle->size[i] = peak > 0 ? (BYTE)MIN(255, MAX(0, values[i] / peak * 255.0f + 0.5f)) : 0;
    particle->size_time_scale = 1.0f / MAX(particle->lifespan, 0.001f);
}

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

/* File-mapped effects have no runtime accumulator; derive emissions from the shared render clock. */
__attribute__((unused))
static void R_EmitParticlesAtTime(float rate, DWORD now_ms, DWORD delta_ms,
                                  void (*spawn)(void *), void *ctx) {
	DWORD last_ms, start_ms;
	float interval_ms;
	if (rate <= 0.0f || delta_ms == 0) return;
	interval_ms = 1000.0f / rate;
	last_ms = now_ms - delta_ms;
	start_ms = last_ms - last_ms % 1000;
	for (float t = (float)start_ms; t < (float)now_ms; t += interval_ms)
		if (t >= (float)last_ms) spawn(ctx);
}

#endif
