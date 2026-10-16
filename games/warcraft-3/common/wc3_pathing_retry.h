#ifndef BZ_WC3_PATHING_RETRY_H
#define BZ_WC3_PATHING_RETRY_H

#include "wc3_pathing_random.h"

typedef struct { float source[2], goal[2]; uint32_t members; } wc3RetryInput_t;

/* 1689d0 measures the adjusted fine goal. 1d62b0 uses low mantissa bits,
 * unlike the public integer native's full-width multiply-high draw. */
static inline uint32_t wc3_retry_init(wc3RetryInput_t const *in, wc3Random_t *random) {
    float x=wc3_sub(in->source[0],in->goal[0]), y=wc3_sub(in->source[1],in->goal[1]);
    if (wc3_add(wc3_mul(x,x),wc3_mul(y,y))<=144 && in->members<=1) return 2;
    return 7+((wc3_random_next(random)>>22)&1u);
}

/* Null-target 167290: terminal1 retains every buffer; other counts reset only
 * the fine buffer and decrement. The caller owns that buffer transaction. */
static inline uint32_t wc3_retry_advance(uint32_t *count, wc3RetryInput_t const *in, wc3Random_t *random) {
    if (!*count) *count=wc3_retry_init(in,random);
    if (*count==1) return 4;
    --*count;
    return 1;
}

#endif
