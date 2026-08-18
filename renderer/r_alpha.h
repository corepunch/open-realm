#ifndef __r_alpha_h__
#define __r_alpha_h__

#define BZ_MSAA_DEFAULT 4
#define BZ_MSAA_MAX     16
#define BZ_ALPHA_STR_INNER(x) #x
#define BZ_ALPHA_STR(x) BZ_ALPHA_STR_INNER(x)
#define BZ_MSAA_DEFAULT_STRING BZ_ALPHA_STR(BZ_MSAA_DEFAULT)

/* SDL accepts arbitrary integers, but the renderer contract is off or at least 2x MSAA. */
static int R_MsaaRequest(int value) { return value < 2 ? 0 : MIN(value, BZ_MSAA_MAX); }

/* SAMPLE_BUFFERS is authoritative; GL_SAMPLES alone may retain an irrelevant implementation value. */
static int R_MsaaActiveSamples(int buffers, int samples) { return buffers > 0 && samples > 1 ? samples : 0; }

#endif
