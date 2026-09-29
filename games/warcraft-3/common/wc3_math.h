#ifndef BZ_WC3_MATH_H
#define BZ_WC3_MATH_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "wc3_math_tables.h"

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

/* 6f070120 preserves x86's modulo-32 shifts even for exceptional raw helper inputs. */
static inline uint32_t wc3_int_bits(uint32_t w) {
    int exp = (w >> 23) & 255, shift = exp - 150;
    if (exp < 127) return 0;
    uint32_t mag = (w & 0x7fffff) | 0x800000;
    mag = shift < 0 ? mag >> (-shift & 31) : mag << (shift & 31);
    return w & 0x80000000u ? 0u - mag : mag;
}

/* 6f070d80 truncates the integer significand; host int-to-float conversion rounds instead. */
static inline uint32_t wc3_from_int(uint32_t w) {
    uint32_t mag = w & 0x80000000u ? 0u - w : w;
    if (!mag) return 0;
    int exp = 31 - __builtin_clz(mag);
    uint32_t mant = exp > 23 ? mag >> (exp - 23) : mag << (23 - exp);
    return ((uint32_t)(exp + 127) << 23) | (mant & 0x7fffff) | (w & 0x80000000u);
}

/* 6f071280/070790: quarter-wave fixed-point interpolation, then truncating scalar conversion. */
static inline float wc3_trig(float angle, bool cosine) {
    uint32_t phase = wc3_int_bits(wc3_mul_bits(wc3_float_bits(angle), 0x4822f983));
    uint32_t quad = ((phase >> 18) + cosine) & 3, idx = (phase >> 8) & 1023;
    uint32_t weight = (phase & 255) * 0x01010101u, val;
    if (quad & 1) {
        val = wc3_sines[1024 - idx];
        val -= (uint64_t)(val - wc3_sines[1023 - idx]) * weight >> 32;
    } else {
        val = wc3_sines[idx];
        val += (uint64_t)(wc3_sines[idx + 1] - val) * weight >> 32;
    }
    uint32_t w = wc3_from_int(quad & 2 ? 0u - val : val);
    return wc3_float(w - (w & 0x7f800000 ? 0x0f800000u : 0));
}

static inline float wc3_sin(float a) { return wc3_trig(a, false); }
static inline float wc3_cos(float a) { return wc3_trig(a, true); }

/* Restoring integer root reproduces 6f071530 without a host libm operation. */
static inline uint32_t wc3_isqrt(uint32_t n) {
    uint32_t root = 0, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= root + bit) { n -= root + bit; root = (root >> 1) + bit; }
        else root >>= 1;
        bit >>= 2;
    }
    return root;
}

/* 6f071480 reconstructs a scalar from a replicated significand's integer root. */
static inline float wc3_sqrt(float a) {
    uint32_t w = wc3_float_bits(a), frac = w & 0x7fffff;
    if (w & 0x80000000u || !w) return 0;
    uint32_t root = wc3_isqrt(((frac | 0x800000) << 8) | (frac >> 15));
    uint32_t coeff = (((root - 0xb504u) * 0xb505u) >> 8) | 0x3f800000;
    int exp = ((w >> 23) & 255) - 127, half = (exp - (exp & 1)) / 2;
    uint32_t scale = ((uint32_t)(half + 127) << 23) | (exp & 1 ? 0x3504f3 : 0);
    return wc3_float(wc3_mul_bits(scale, coeff));
}

/* 6f0711e0 uses the same interpolation word repetition and signed range guard as retail. */
static inline float wc3_recip(float a) {
    uint32_t w = wc3_float_bits(a), frac = w & 0x7fffff, idx = frac >> 13, rem = frac & 8191;
    uint32_t weight = (rem << 19) | (rem << 6) | (rem >> 7), val = wc3_recips[idx];
    val -= (uint64_t)(val - wc3_recips[idx + 1]) * weight >> 32;
    val = val - (w & 0x7f800000) + 0x7e800000;
    return wc3_float((val - 0x800000) & 0x80000000u ? 0 : val | (w & 0x80000000u));
}

static inline float wc3_div(float a, float b) {
    return wc3_float_bits(a) == wc3_float_bits(b) ? 1 : wc3_mul(a, wc3_recip(b));
}

/* 6f06ffa0 changes lookup resolution near abs(x)=3f7e8000; both curves use fixed-point interpolation. */
static inline float wc3_acos(float a) {
    uint32_t w = wc3_float_bits(a), mag = w & 0x7fffffffu, val;
    if (mag <= 0x3f7e8000) {
        uint32_t phase = wc3_int_bits(w + 0x0f000000u);
        int64_t n = phase & 0x80000000u ? (int64_t)phase - 0x100000000ll : phase;
        if (n > 0x3fffffff) n = 0x3fffffff;
        if (n < -0x3fffffff) n = -0x3fffffff;
        uint32_t idx = ((uint32_t)n >> 20) & 1023, frac = (uint32_t)n << 12;
        uint32_t weight = frac | (frac >> 20), delta;
        if (n < 0) {
            val = 0x6487ed51u - wc3_acos_curve[1024 - idx];
            delta = wc3_acos_curve[1023 - idx] - wc3_acos_curve[1024 - idx];
        } else {
            val = wc3_acos_curve[idx];
            delta = val - wc3_acos_curve[idx + 1];
        }
        val = wc3_from_int(val - ((uint64_t)delta * weight >> 32));
        return wc3_float(val - (val & 0x7f800000 ? 0x0e800000u : 0));
    }
    if (mag > 0x3f800000) mag = 0x3f800000;
    uint32_t diff = 0x3f800000u - mag, zeros = diff ? __builtin_clz(diff) : 32;
    uint32_t frac = zeros < 32 ? ~diff << zeros : 0, idx = (zeros * 8 - 120) | (frac >> 28);
    val = wc3_acos_near[idx];
    val -= (uint64_t)(val - wc3_acos_near[idx + 1]) * (frac << 4) >> 32;
    val = wc3_from_int(val);
    float result = wc3_float(val - (val & 0x7f800000 ? 0x11000000u : 0));
    return w & 0x80000000u ? wc3_sub(wc3_float(0x40490fdb), result) : result;
}

/* 6f1d4c80 takes length separately; the tiny-angle guard runs after acos as well. */
static inline float wc3_vector_heading(float x, float y) {
    float len = wc3_sqrt(wc3_add(wc3_mul(x, x), wc3_mul(y, y)));
    if (wc3_float(wc3_float_bits(len) & 0x7fffffffu) <= wc3_float(0x3727c5ac)) return 0;
    float angle = wc3_acos(wc3_div(x, len));
    if (wc3_float(wc3_float_bits(angle) & 0x7fffffffu) <= wc3_float(0x3727c5ac)) return 0;
    return y < 0 ? wc3_sub(wc3_float(0x40c90fdb), angle) : angle;
}

/* 6f173720 subtracts current-minus-target first; that operand order affects truncated wraps. */
static inline float wc3_heading_delta(float target, float current) {
    float reverse = wc3_sub(current, target), pi = wc3_float(0x40490fdb), tau = wc3_float(0x40c90fdb);
    if (reverse < 0) {
        if (-reverse > pi) return wc3_sub(-tau, reverse);
    } else if (reverse > pi) return wc3_sub(tau, reverse);
    return wc3_float(wc3_float_bits(reverse) ^ 0x80000000u);
}

/* 6f16f630's final deadzone is strict; equality keeps the signed error. */
static inline float wc3_turn_error(float target, float current) {
    float error = wc3_heading_delta(target, current);
    return wc3_float(wc3_float_bits(wc3_sub(error, 0)) & 0x7fffffffu) < wc3_float(0x3456bf95) ? 0 : error;
}

static inline float wc3_heading_error(float x, float y, float current) {
    return wc3_turn_error(wc3_vector_heading(x, y), current);
}

/* 6f062930: truncated reciprocal multiplication and negative correction; +tau remains +tau. */
static inline float wc3_angle(float angle) {
    uint32_t w = wc3_float_bits(wc3_mul(angle, wc3_float(0x3e22f983)));
    int exp = ((w >> 23) & 255) - 127;
    w = exp < 0 ? 0 : exp >= 23 ? w : w & ~((1u << (23 - exp)) - 1);
    float span = wc3_mul(wc3_float(0x40c90fdb), wc3_float(w));
    if (angle < 0) span = wc3_sub(span, wc3_float(0x40c90fdb));
    return wc3_sub(angle, span);
}

/* 070d20 subtracts the truncated integer word; integral magnitudes >=2^23 return +0. */
static inline float wc3_fraction(float value) {
    uint32_t word = wc3_float_bits(value);
    int exponent = ((word >> 23) & 255) - 127;
    if (exponent < 0) return value;
    if (exponent >= 23) return 0;
    uint32_t mask = ~((1u << (23 - exponent)) - 1);
    return wc3_sub(value, wc3_float(word & mask));
}

/* 070fe0 uses reciprocal/fraction multiplication, including its negative correction. */
static inline float wc3_modulo(float value, float divisor) {
    divisor = wc3_float(wc3_float_bits(divisor) & 0x7fffffffu);
    float result = wc3_mul(wc3_fraction(wc3_mul(value, wc3_recip(divisor))), divisor);
    uint32_t word = wc3_float_bits(result);
    if (!(word & 0x80000000u) && (word & 0x7fffffffu)) {
        if (result >= divisor) result = wc3_sub(result, divisor);
    } else if (-divisor >= result) result = wc3_add(result, -divisor);
    return result;
}

/* 15ffd0/16fe20 facing uses this remainder; 062930 parameter normalization is different. */
static inline float wc3_facing_angle(float heading) {
    float tau = wc3_float(0x40c90fdb);
    if (wc3_float(wc3_float_bits(heading) & 0x7fffffffu) >= tau)
        heading = wc3_modulo(heading, tau);
    return heading < 0 ? wc3_add(heading, tau) : heading;
}

/* 160060 preserves facing below the squared-velocity threshold; equality recomputes it. */
static inline float wc3_velocity_heading(float x, float y, float current) {
    float sq = wc3_sub(wc3_add(wc3_mul(x, x), wc3_mul(y, y)), 0);
    if (wc3_float(wc3_float_bits(sq) & 0x7fffffffu) < wc3_float(0x34d6bf95)) return current;
    return wc3_vector_heading(x, y);
}

typedef struct {
    float speed, heading, error, increment, turn, window;
    bool stop;
} wc3Motion_t;

typedef struct {
    float vel[2], speed, heading, limit;
} wc3Velocity_t;

typedef struct {
    float time;
    uint32_t epoch;
    float span;
} wc3Clock_t;

/* 6f15cea0 clears only sub-deadzone fractional deltas, then adds the signed epoch span. */
static inline float wc3_elapsed(wc3Clock_t const *cur, wc3Clock_t const *old) {
    float dt = wc3_sub(cur->time, old->time);
    if (wc3_float(wc3_float_bits(dt) & 0x7fffffffu) < wc3_float(0x38d1b717)) dt = 0;
    if (cur->epoch != old->epoch)
        dt = wc3_add(dt, wc3_mul(cur->span, wc3_float(wc3_from_int(cur->epoch - old->epoch))));
    return dt;
}

/* 6f1603d0 commits old velocity before a requested velocity change, then explicit displacement. */
static inline void wc3_integrate(float pos[2], float const vel[2], float elapsed) {
    for (unsigned i = 0; i < 2; i++) pos[i] = wc3_add(pos[i], wc3_mul(vel[i], elapsed));
}

/* Desired-minus-old is added back before testing length; cancellation truncation is observable. */
static inline void wc3_velocity_update(wc3Velocity_t *v) {
    float dir[2] = { wc3_cos(v->heading), wc3_sin(v->heading) };
    for (unsigned i = 0; i < 2; i++) {
        float want = v->speed > 0 ? wc3_mul(v->speed, dir[i]) : 0;
        v->vel[i] = wc3_add(v->vel[i], wc3_sub(want, v->vel[i]));
    }
    float sq = wc3_add(wc3_mul(v->vel[0], v->vel[0]), wc3_mul(v->vel[1], v->vel[1]));
    if (sq < wc3_float(0x3456bf95)) { v->vel[0] = v->vel[1] = 0; return; }
    float len = wc3_sqrt(sq);
    if (len > v->limit) {
        float scale = wc3_mul(v->limit, wc3_recip(len));
        for (unsigned i = 0; i < 2; i++) v->vel[i] = wc3_mul(v->vel[i], scale);
    }
}

/* Engine velocities are world units; retail16fe20 measures its guards in32-unit fine cells. */
static inline void wc3_velocity_update_world(wc3Velocity_t *v) {
    float speed = v->speed, limit = v->limit;
    for (unsigned i = 0; i < 2; i++) v->vel[i] = wc3_mul(v->vel[i], wc3_float(0x3d000000));
    v->speed = wc3_mul(speed, wc3_float(0x3d000000));
    v->limit = wc3_mul(limit, wc3_float(0x3d000000));
    wc3_velocity_update(v);
    for (unsigned i = 0; i < 2; i++) v->vel[i] = wc3_mul(v->vel[i], wc3_float(0x42000000));
    v->speed = speed; v->limit = limit;
}

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
