#ifndef __r_alpha_h__
#define __r_alpha_h__

#ifndef BZ_MSAA_SAMPLES
#define BZ_MSAA_SAMPLES 0 // samples/pixel; bandwidth-safe default; selected by make MSAA=0/2/4/8
#endif

/* SAMPLE_BUFFERS is authoritative; GL_SAMPLES alone may retain an irrelevant implementation value. */
static inline int R_MsaaActiveSamples(int buffers, int samples) { return buffers > 0 && samples > 1 ? samples : 0; }

#endif
