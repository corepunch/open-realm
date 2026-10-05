#ifndef BZ_WC3_MATH_H
#define BZ_WC3_MATH_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
#include "wc3_math_tables.h"

static uint32_t const wc3_exp_coeffs[] = { 0x3c1a534c, 0x3d296ec9, 0x3e2ab479, 0x3effffbd, 0x3f800000, 0x3f800000 };

/* Retail 1.27 software scalars truncate significands rather than rounding to nearest.
 * Integer operations keep the simulation independent of host FP rounding and contraction. */
static inline uint32_t wc3_float_bits(float f) { uint32_t w; memcpy(&w, &f, sizeof(w)); return w; }
static inline float wc3_float(uint32_t w) { float f; memcpy(&f, &w, sizeof(f)); return f; }

/* Arithmetic alignment in the emulated 32-bit word, without implementation-
 * defined signed shifts or widening/sign-extending both operands to 64 bits. */
static inline uint32_t wc3_align(uint32_t word, unsigned shift) {
    uint32_t sign = 0u - (word >> 31);
    return ((word ^ sign) >> shift) ^ sign;
}

/* 6f06fbb0: doubled significands have at most 25 magnitude bits; their
 * aligned sum fits signed 27 bits. Unsigned word addition therefore retains
 * the exact signed result, including negative alignment truncation. */
static inline uint32_t wc3_add_bits(uint32_t a, uint32_t b) {
    int ea = (a >> 23) & 255, eb = (b >> 23) & 255, exp = ea > eb ? ea : eb;
    if (!ea || eb - ea >= 23) return b;
    if (!eb || ea - eb >= 23) return a;
    uint32_t ma = ((a & 0x7fffff) | 0x800000) * 2, mb = ((b & 0x7fffff) | 0x800000) * 2;
    uint32_t sum = wc3_align(a & 0x80000000u ? 0u - ma : ma, exp - ea);
    sum += wc3_align(b & 0x80000000u ? 0u - mb : mb, exp - eb);
    if (!sum) return 0;
    uint32_t sign = sum & 0x80000000u, mag = sign ? 0u - sum : sum;
    int shift = 8 - __builtin_clz(mag);
    uint32_t mant = shift < 0 ? mag << -shift : mag >> shift;
    return ((uint32_t)(exp + shift - 1) << 23) | (mant & 0x7fffff) | sign;
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

/* Public R2I uses070170, whose overflow policy differs from raw070120. */
static inline uint32_t wc3_saturating_int_bits(uint32_t w) {
    if (((w >> 23) & 255) >= 158)
        return w & 0x80000000u ? 0x80000000u : 0x7fffffffu;
    return wc3_int_bits(w);
}

/* The paired and single helpers use the same quarter-wave interpolation after phase reduction. */
static inline float wc3_trig_phase(uint32_t phase, bool cosine) {
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

/* 6f071280/070790: quarter-wave fixed-point interpolation, then truncating scalar conversion. */
static inline float wc3_trig(float angle, bool cosine) {
    return wc3_trig_phase(wc3_int_bits(wc3_mul_bits(wc3_float_bits(angle), 0x4822f983)), cosine);
}

static inline float wc3_sin(float a) { return wc3_trig(a, false); }
static inline float wc3_cos(float a) { return wc3_trig(a, true); }

/* 6f071340 reduces once, then stores sine before cosine; aliased outputs retain the latter. */
static inline void wc3_sincos(float angle, float *sine, float *cosine) {
    uint32_t phase = wc3_int_bits(wc3_mul_bits(wc3_float_bits(angle), 0x4822f983));
    *sine = wc3_trig_phase(phase, false);
    *cosine = wc3_trig_phase(phase, true);
}

/* Both 6f071530 and the adaptive Newton loop return floor(sqrt(n)). Use a
 * hardware estimate where available, then prove/correct it with integer
 * products. Host rounding mode cannot change the returned integer. The
 * restoring path also supports CPUs without scalar hardware square root. */
static inline uint32_t wc3_isqrt(uint32_t n) {
#if defined(__SSE2__)
    __m128d value = _mm_set_sd((double)n);
    uint32_t root = (uint32_t)_mm_cvtsd_f64(_mm_sqrt_sd(value, value));
    while ((uint64_t)root * root > n) root--;
    while ((uint64_t)(root + 1) * (root + 1) <= n) root++;
    return root;
#else
    uint32_t root = 0, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= root + bit) { n -= root + bit; root = (root >> 1) + bit; }
        else root >>= 1;
        bit >>= 2;
    }
    return root;
#endif
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

/* Original071180 for a nonnegative integer exponent; parser070de0 uses base10. */
static inline float wc3_integer_power(float base, uint32_t exponent) {
    float result = 1.0f;
    while (exponent) {
        if (exponent & 1) result = wc3_mul(result, base);
        base = wc3_mul(base, base);
        exponent >>= 1;
    }
    return result;
}

/* 0715c0 truncates the raw significand, including signed zero and exceptional words. */
static inline uint32_t wc3_trunc_bits(uint32_t word) {
    int exp = ((word >> 23) & 255) - 127;
    return exp < 0 ? 0 : exp >= 23 ? word : word & (UINT32_MAX << (23 - exp));
}

/* 070c80 masks fractional bits and increments negative magnitude; both zero signs become +0. */
static inline uint32_t wc3_floor_bits(uint32_t word) {
    int exp = ((word >> 23) & 255) - 127;
    if (exp < 0) return word & 0x80000000u && (word & 0x7fffffffu) ? 0xbf800000u : 0;
    if (exp >= 23) return word;
    uint32_t mask = (1u << (23 - exp)) - 1u, result = word & ~mask;
    return result + (word & 0x80000000u && (word & mask) ? mask + 1u : 0);
}

/* 070700 returns one for either zero sign; only negative nonzero sub-unit words become +0. */
static inline uint32_t wc3_ceil_bits(uint32_t word) {
    int exp = ((word >> 23) & 255) - 127;
    bool neg = word & 0x80000000u && (word & 0x7fffffffu);
    if (exp < 0) return neg ? 0 : 0x3f800000u;
    if (exp >= 23) return word;
    uint32_t mask = (1u << (23 - exp)) - 1u, result = word & ~mask;
    return result + (!neg && (word & mask) ? mask + 1u : 0);
}

/* 071250 is scalar-add-half followed by floor; its truncating addition matters near boundaries. */
static inline uint32_t wc3_round_bits(uint32_t word) { return wc3_floor_bits(wc3_add_bits(word, 0x3f000000u)); }

/* 06fd50 retains the rational log curve's scalar order and explicit numerator doubling. */
static inline float wc3_ln_core(float value) {
    float ratio = wc3_div(wc3_add(value, -1.0f), wc3_add(value, 1.0f));
    float square = wc3_mul(ratio, ratio);
    float numer = wc3_mul(ratio, wc3_add(1.0f, wc3_mul(wc3_float(0xbe88d424), square)));
    uint32_t word = wc3_float_bits(numer);
    if (word & 0x7f800000u) word += 0x800000u;
    return wc3_div(wc3_float(word), wc3_add(1.0f, wc3_mul(wc3_float(0xbf19bf59), square)));
}

/* 06ff20 selects the exact reduction thresholds; unordered compares take the low branch. */
static inline float wc3_ln_reduced(float value) {
    if (value >= wc3_float(0x3f612ad1)) return wc3_ln_core(value);
    if (!(value >= wc3_float(0x3f28e5a3)))
        return wc3_add(wc3_ln_core(wc3_mul(value, wc3_float(0x3fdedc67))), wc3_float(0xbf0df4e0));
    return wc3_add(wc3_ln_core(wc3_mul(value, wc3_float(0x3fa8e5a3))), wc3_float(0xbe8df4e0));
}

/* 070f70 decomposes magnitude, then retains reciprocal-ln2/exponent/ln2 scalar scaling. */
static inline float wc3_ln(float value) {
    uint32_t word = wc3_float_bits(value);
    float mant = wc3_float((word & 0x7fffffu) | 0x3f800000u);
    float frac = wc3_mul(wc3_ln_reduced(mant), wc3_float(0x3fb8aa3b));
    float exp = wc3_float(wc3_from_int((uint32_t)((int)((word >> 23) & 255) - 127)));
    return wc3_mul(wc3_add(exp, frac), wc3_float(0x3f317218));
}

/* 070c20/06fe10: quarter-step integer power and ordered polynomial, reciprocal for negative input.
 * False denotes the original signed-SAR exponent loop's nonterminating domain; output is untouched. */
static inline bool wc3_exp(float value, float *output) {
    uint32_t word = wc3_float_bits(value);
    bool neg = word & 0x80000000u && (word & 0x7fffffffu);
    if (neg) word ^= 0x80000000u;
    uint32_t scaled = word + (word & 0x7f800000u ? 0x1000000u : 0);
    uint32_t whole = wc3_trunc_bits(scaled), exp = wc3_int_bits(whole);
    if (exp & 0x80000000u) return false;
    uint32_t frac = wc3_float_bits(wc3_sub(wc3_float(scaled), wc3_float(whole)));
    frac = ((frac - 0x1800000u) ^ frac) & 0x80000000u ? 0 : frac - 0x1000000u;
    float part = wc3_float(frac), result = wc3_float(wc3_exp_coeffs[0]);
    for (unsigned i = 1; i < sizeof(wc3_exp_coeffs) / sizeof(wc3_exp_coeffs[0]); i++)
        result = wc3_add(wc3_mul(result, part), wc3_float(wc3_exp_coeffs[i]));
    result = wc3_mul(result, wc3_integer_power(wc3_float(0x3fa45af2), exp));
    *output = neg ? wc3_recip(result) : result;
    return true;
}

/* 0710e0 uses integer powers only for exact nonnegative integral exponents; other powers use ln/exp. */
static inline bool wc3_pow(float base, float power, float *output) {
    uint32_t word = wc3_float_bits(power);
    if (wc3_float(wc3_trunc_bits(word)) == power && !(word & 0x80000000u && (word & 0x7fffffffu))) {
        uint32_t exp = wc3_int_bits(word);
        if (exp & 0x80000000u) return false;
        *output = wc3_integer_power(base, exp);
        return true;
    }
    if (!(wc3_float_bits(base) & 0x7f800000u)) {
        *output = 0.0f;
        return true;
    }
    return wc3_exp(wc3_mul(power, wc3_ln(base)), output);
}


/* Source integer actions925210/925490/925350 wrap each decimal/octal/hex
 * digit to32 bits. The lexer owns syntax and unary signs; prefix lengths are
 * 0 for decimal,1 for octal/$hex and2 for0xhex. Host long saturation differs. */
static inline uint32_t wc3_integer_literal_bits(char const *text) {
    uint32_t radix = 10, value = 0;
    if (*text == '$') {
        radix = 16;
        text++;
    } else if (*text == '0') {
        radix = text[1] == 'x' || text[1] == 'X' ? 16 : 8;
        text += radix == 16 ? 2 : 1;
    }
    for (; *text; text++) {
        uint32_t digit = *text >= 'a' && *text <= 'f' ? *text - 'a' + 10 :
                         *text >= 'A' && *text <= 'F' ? *text - 'A' + 10 : *text - '0';
        value = value * radix + digit;
    }
    return value;
}

/* Compiled JASS real token925260: unsigned decimal text, signed32 wrapping
 * accumulators, then software division/addition. The lexer owns validation and
 * unary signs; unlike S2R, every fractional digit participates in the wrap. */
static inline float wc3_literal(char const *text) {
    uint32_t whole = 0, numerator = 0, denominator = 1;
    for (; *text && *text != '.'; text++) whole = whole * 10 + (*text - '0');
    if (!*text) return wc3_float(wc3_from_int(whole));
    for (text++; *text; text++) {
        numerator = numerator * 10 + (*text - '0');
        denominator *= 10;
    }
    float fraction = wc3_div(wc3_float(wc3_from_int(numerator)), wc3_float(wc3_from_int(denominator)));
    return wc3_add(wc3_float(wc3_from_int(whole)), fraction);
}

/* Public S2R's decimal parser070de0: optional sign, one point, nine significant
 * digits, no whitespace skipping/exponents. Scalar scaling also truncates.
 * Exact sibling CRT isdigit0f1d5/default C table accepts only ASCII digits,
 * including the signed-char table prefix for high bytes. JASS strings are byte
 * strings. Alternative CRT locale creation remains outside this verified domain. */
static inline float wc3_decimal(char const *text) {
    if (!text) return 0.0f;
    bool negative = *text == '-';
    if (*text == '-' || *text == '+') text++;
    uint32_t accumulator = 0, significant = 0, scale = 0;
    int fractional = 0;
    bool started = false, point = false;
    for (; *text; text++) {
        if (*text >= '0' && *text <= '9') {
            started |= *text != '0';
            significant += started;
            if (significant <= 9) {
                scale += fractional;
                accumulator = accumulator * 10 + (*text - '0');
            } else {
                if (significant == 10) fractional--;
                scale += fractional;
            }
        } else if (*text == '.' && !point) {
            point = true;
            fractional++;
        } else {
            break;
        }
    }
    float value = wc3_float(wc3_from_int(negative ? 0u - accumulator : accumulator));
    bool multiply = (scale & 0x80000000u) != 0;
    float power = wc3_integer_power(10.0f, multiply ? 0u - scale : scale);
    return multiply ? wc3_mul(value, power) : wc3_div(value, power);
}

/* Original06ffa0/0703a0 share the inverse curve; Asin subtracts the fixed-point
 * half turn BEFORE integer conversion, preserving its distinct rounding. */
static inline float wc3_inverse_curve(float a, bool sine) {
    uint32_t w = wc3_float_bits(a), mag = w & 0x7fffffffu, val;
    if (mag <= 0x3f7e8000) {
        uint32_t phase = wc3_int_bits(w + 0x0f000000u);
        int64_t n = phase & 0x80000000u ? (int64_t)phase - 0x100000000ll : phase;
        if (n > 0x3fffffff) n = 0x3fffffff;
        if (n < -0x3fffffff) n = -0x3fffffff;
        if (sine) n = -n;
        uint32_t idx = ((uint32_t)n >> 20) & 1023, frac = (uint32_t)n << 12;
        uint32_t weight = frac | (frac >> 20), delta;
        if (n < 0) {
            val = 0x6487ed51u - wc3_acos_curve[1024 - idx];
            delta = wc3_acos_curve[1023 - idx] - wc3_acos_curve[1024 - idx];
        } else {
            val = wc3_acos_curve[idx];
            delta = val - wc3_acos_curve[idx + 1];
        }
        val -= (uint64_t)delta * weight >> 32;
        if (sine) val -= 0x3243f6a8u;
        val = wc3_from_int(val);
        return wc3_float(val - (val & 0x7f800000 ? 0x0e800000u : 0));
    }
    if (mag > 0x3f800000) mag = 0x3f800000;
    uint32_t diff = 0x3f800000u - mag, zeros = diff ? __builtin_clz(diff) : 32;
    uint32_t frac = zeros < 32 ? ~diff << zeros : 0, idx = (zeros * 8 - 120) | (frac >> 28);
    val = wc3_acos_near[idx];
    val -= (uint64_t)(val - wc3_acos_near[idx + 1]) * (frac << 4) >> 32;
    val = wc3_from_int(val);
    float result = wc3_float(val - (val & 0x7f800000 ? 0x11000000u : 0));
    if (sine)
        return w & 0x80000000u ? wc3_add(wc3_float(0xbfc90fdb), result) : wc3_sub(wc3_float(0x3fc90fdb), result);
    return w & 0x80000000u ? wc3_sub(wc3_float(0x40490fdb), result) : result;
}

static inline float wc3_acos(float a) { return wc3_inverse_curve(a, false); }
static inline float wc3_asin(float a) { return wc3_inverse_curve(a, true); }

/* Original0705b0: preserve the scalar operation order of the reduced rational
 * polynomial, including both reciprocal and half-pi correction branches. */
static inline float wc3_atan(float a) {
    uint32_t word = wc3_float_bits(a);
    float absolute = wc3_float(word & 0x7fffffffu);
    float x = absolute > 1.0f ? wc3_recip(absolute) : absolute;
    bool reduced = x > wc3_float(0x3e8930a3);
    if (reduced) {
        float numerator = wc3_add(x, wc3_float(0xbf13cd3a));
        float denominator = wc3_add(1.0f, wc3_mul(wc3_float(0x3f13cd3a), x));
        x = wc3_div(numerator, denominator);
    }
    float square = wc3_mul(x, x);
    float denominator = wc3_add(1.0f, wc3_mul(wc3_float(0x3f17592e), square));
    float numerator = wc3_mul(x, wc3_add(wc3_float(0x3f7ffff0), wc3_mul(wc3_float(0x3e8415a6), square)));
    float result = wc3_div(numerator, denominator);
    if (reduced) result = wc3_add(result, wc3_float(0x3f060a92));
    if (absolute > 1.0f) result = wc3_sub(wc3_float(0x3fc90fdb), result);
    return word & 0x80000000u && word & 0x7fffffffu ? wc3_float(wc3_float_bits(result) ^ 0x80000000u) : result;
}

/* Original070530 ignores the sign of zero in its quadrant corrections. */
static inline float wc3_atan2(float y, float x) {
    uint32_t wx = wc3_float_bits(x), wy = wc3_float_bits(y);
    float result = wx & 0x7f800000u ? wc3_atan(wc3_float(wc3_float_bits(wc3_div(y, x)) & 0x7fffffffu)) : wc3_float(0x3fc90fdb);
    if (wx & 0x80000000u && wx & 0x7fffffffu) result = wc3_sub(wc3_float(0x40490fdb), result);
    return wy & 0x80000000u && wy & 0x7fffffffu ? wc3_float(wc3_float_bits(result) ^ 0x80000000u) : result;
}

static inline float wc3_tan(float angle) {
    float sine, cosine;
    wc3_sincos(angle, &sine, &cosine);
    return wc3_div(sine, cosine);
}

static inline float wc3_degrees_to_radians(float angle) { return wc3_mul(angle, wc3_float(0x3c8efa35)); }
static inline float wc3_radians_to_degrees(float angle) { return wc3_mul(angle, wc3_float(0x42652ee1)); }


/* 6f1d4c80 takes length separately; the tiny-angle guard runs after acos as well.
 * Its quotient and acos destination occupy distinct stack slots. The value API
 * preserves negative X; the raw acos helper's aliased sign loss is unreachable here. */
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

/* Original054190 advances once and rebases one epoch. The caller dispatches
 * due requests at the span boundary before rebasing their deadlines. */
static inline bool wc3_clock_advance(wc3Clock_t *clock, float increment, uint32_t flags) {
    if (flags & 1u) return false;
    float next = wc3_add(clock->time, increment);
    bool wrapped = clock->span <= next;
    if (wrapped) {
        next = wc3_sub(next, clock->span);
        float residual = wc3_float(wc3_float_bits(wc3_sub(next, 0)) & 0x7fffffffu);
        if (residual < wc3_float(0x3556bf95)) next = 0;
        clock->epoch++;
    }
    clock->time = next;
    return wrapped;
}

/* 6f1603d0 commits old velocity before a requested velocity change, then explicit displacement. */
static inline void wc3_integrate(float pos[2], float const vel[2], float elapsed) {
    for (unsigned i = 0; i < 2; i++) pos[i] = wc3_add(pos[i], wc3_mul(vel[i], elapsed));
}

static inline void wc3_velocity_limit(wc3Velocity_t *v) {
    float sq = wc3_add(wc3_mul(v->vel[0], v->vel[0]), wc3_mul(v->vel[1], v->vel[1]));
    if (sq < wc3_float(0x3456bf95)) { v->vel[0] = v->vel[1] = 0; return; }
    float len = wc3_sqrt(sq);
    if (len > v->limit) {
        float scale = wc3_mul(v->limit, wc3_recip(len));
        for (unsigned i = 0; i < 2; i++) v->vel[i] = wc3_mul(v->vel[i], scale);
    }
}

/* Desired-minus-old is added back before testing length; cancellation truncation is observable. */
static inline void wc3_velocity_update(wc3Velocity_t *v) {
    float dir[2];
    wc3_sincos(v->heading, &dir[1], &dir[0]);
    for (unsigned i = 0; i < 2; i++) {
        float want = v->speed > 0 ? wc3_mul(v->speed, dir[i]) : 0;
        v->vel[i] = wc3_add(v->vel[i], wc3_sub(want, v->vel[i]));
    }
    wc3_velocity_limit(v);
}

/* Original15ff40 changes a cap without requesting a new heading. Only excess
 * old velocity enters15f7e0, adds a zero delta and clamps through1606e0/15fc70.
 * The caller commits old velocity through its current clock before this change. */
static inline bool wc3_velocity_cap(wc3Velocity_t *v) {
    float sq = wc3_add(wc3_mul(v->vel[0], v->vel[0]), wc3_mul(v->vel[1], v->vel[1]));
    float length = wc3_float(wc3_float_bits(wc3_sqrt(sq)) & 0x7fffffffu);
    if (!(length > v->limit)) return false;
    for (unsigned i = 0; i < 2; i++) v->vel[i] = wc3_add(v->vel[i], 0);
    wc3_velocity_limit(v);
    return true;
}

static inline bool wc3_velocity_cap_world(wc3Velocity_t *v) {
    wc3Velocity_t grid = *v;
    for (unsigned i = 0; i < 2; i++) grid.vel[i] = wc3_mul(grid.vel[i], wc3_float(0x3d000000));
    grid.limit = wc3_mul(grid.limit, wc3_float(0x3d000000));
    if (!wc3_velocity_cap(&grid)) return false;
    for (unsigned i = 0; i < 2; i++) v->vel[i] = wc3_mul(grid.vel[i], wc3_float(0x42000000));
    return true;
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
