#ifndef BZ_WC3_MATH_H
#define BZ_WC3_MATH_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Retail 1.27 software scalars truncate significands rather than rounding to nearest.
 * Integer operations keep the simulation independent of host FP rounding and contraction. */
static inline uint32_t wc3_float_bits(float f) { uint32_t w; memcpy(&w, &f, sizeof(w)); return w; }
static inline float wc3_float(uint32_t w) { float f; memcpy(&f, &w, sizeof(f)); return f; }

/* Explicit sign extension avoids relying on a host's right shift of negative integers. */
static inline int64_t wc3_align(int32_t n, unsigned shift) {
    uint32_t w = (uint32_t)n;
    if (shift) w = (w >> shift) | (n < 0 ? UINT32_MAX << (32 - shift) : 0);
    return w & 0x80000000u ? (int64_t)w - 0x100000000ll : w;
}

/* 6f06fbb0: align doubled signed significands, then truncate the normalized sum. */
static inline uint32_t wc3_add_bits(uint32_t a, uint32_t b) {
    int ea = (a >> 23) & 255, eb = (b >> 23) & 255, exp = ea > eb ? ea : eb;
    if (!ea || eb - ea >= 23) return b;
    if (!eb || ea - eb >= 23) return a;
    int32_t ma = ((a & 0x7fffff) | 0x800000) * 2, mb = ((b & 0x7fffff) | 0x800000) * 2;
    int64_t sum = wc3_align(a & 0x80000000u ? -ma : ma, exp - ea);
    sum += wc3_align(b & 0x80000000u ? -mb : mb, exp - eb);
    if (!sum) return 0;
    uint32_t mag = sum < 0 ? -sum : sum;
    int shift = 8 - __builtin_clz(mag);
    uint32_t mant = shift < 0 ? mag << -shift : mag >> shift;
    return ((uint32_t)(exp + shift - 1) << 23) | (mant & 0x7fffff) | (sum < 0 ? 0x80000000u : 0);
}

/* 6f06f9c0: the exponent guard runs BEFORE product normalization, including overflow wrap. */
static inline uint32_t wc3_mul_bits(uint32_t a, uint32_t b) {
    int ea = (a >> 23) & 255, eb = (b >> 23) & 255, exp = ea + eb - 127;
    uint32_t ma = a & 0x7fffff, mb = b & 0x7fffff, sign = (a ^ b) & 0x80000000u;
    if (!ma || !mb) {
        if (!ea || !eb || exp < 1 || exp > 256) return 0;
        return ((uint32_t)exp << 23) | ma | mb | sign;
    }
    if (exp < 1 || exp > 256) return 0;
    uint64_t prod = (uint64_t)(ma | 0x800000) * (mb | 0x800000);
    unsigned shift = prod & (1ull << 47) ? 24 : 23;
    return ((uint32_t)(exp + shift - 23) << 23) | ((uint32_t)(prod >> shift) & 0x7fffff) | sign;
}

static inline float wc3_add(float a, float b) { return wc3_float(wc3_add_bits(wc3_float_bits(a), wc3_float_bits(b))); }
static inline float wc3_sub(float a, float b) { return wc3_float(wc3_add_bits(wc3_float_bits(a), wc3_float_bits(b) ^ 0x80000000u)); }
static inline float wc3_mul(float a, float b) { return wc3_float(wc3_mul_bits(wc3_float_bits(a), wc3_float_bits(b))); }

/* 6f062930: truncated reciprocal multiplication and negative correction; +tau remains +tau. */
static inline float wc3_angle(float angle) {
    uint32_t w = wc3_float_bits(wc3_mul(angle, wc3_float(0x3e22f983)));
    int exp = ((w >> 23) & 255) - 127;
    w = exp < 0 ? 0 : exp >= 23 ? w : w & ~((1u << (23 - exp)) - 1);
    float span = wc3_mul(wc3_float(0x40c90fdb), wc3_float(w));
    if (angle < 0) span = wc3_sub(span, wc3_float(0x40c90fdb));
    return wc3_sub(angle, span);
}

typedef struct {
    float speed, heading, error, increment, turn, window;
    bool stop;
} wc3Motion_t;

/* 6f170880 tests the PRE-TURN error; turning continues while translation is stopped. */
static inline bool wc3_motion_update(wc3Motion_t *m) {
    float mag = wc3_float(wc3_float_bits(m->error) & 0x7fffffffu);
    bool move = !(m->stop || mag >= m->window);
    m->speed = move ? wc3_add(m->speed, m->increment) : 0;
    if (mag > 0) {
        float turn = mag < m->turn ? mag : m->turn;
        m->heading = wc3_add(m->heading, m->error < 0 ? -turn : turn);
    }
    return move;
}

#endif
