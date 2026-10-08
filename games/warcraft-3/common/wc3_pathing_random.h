#ifndef BZ_WC3_PATHING_RANDOM_H
#define BZ_WC3_PATHING_RANDOM_H

#include "wc3_math.h"

typedef struct { uint32_t sum, index; } wc3Random_t;
#define BZ_WC3_RANDOM_STREAMS 45 // game-purpose states; original CRandData has45 two-word generators
enum { WC3_RANDOM_ITEMS = 35 }; /* ChooseRandomItem/ItemEx share this catalog stream. */

/* 1.27's prime-period lookup words at game.dll+ a92f10. */
static uint32_t const wc3_random_words[61] = {
    0x9927148eu, 0x08c7aafdu, 0x1f3ee6d5u, 0xda55bbf6u, 0x6a4aa075u, 0xff97bde8u,
    0x9fbc9bdeu, 0x46a18a81u, 0x63e30b6eu, 0x5d6c7a76u, 0xca69d388u, 0x25b947c3u,
    0x3fa2ab83u, 0xba7c41a6u, 0x0195ace5u, 0xc109cf7eu, 0x717062d9u, 0x0205db8du,
    0x54ef8724u, 0x3037d4c6u, 0x7bcb1bd0u, 0xecd8e4b8u, 0xdcadce49u, 0xc494a913u,
    0x0dae398fu, 0x0edd5218u, 0x85f5fa78u, 0x6dafd258u, 0x3b53b2a4u, 0xbe50a551u,
    0x11f42dfcu, 0xf1169848u, 0x663ddf86u, 0x2f2e445eu, 0x176b0736u, 0xb64c298bu,
    0xe75f89e2u, 0xe121a7cdu, 0xed65c94du, 0x239ceefeu, 0x04b77d33u, 0x402a9a9eu,
    0xf35b10b3u, 0x921c7782u, 0x571e4e20u, 0x8c067222u, 0xfb732c67u, 0xbf0ac259u,
    0x0cf95c79u, 0x68121a28u, 0x42193474u, 0xf884c0b1u, 0x9d15f038u, 0x6f3af260u,
    0x91eb90b4u, 0x61357f1du, 0x5603325au, 0x932bc5a3u, 0x434b0f80u, 0x3ce0a8f7u,
    0x2664d196u,
};

/* 1cc4e0: seed four byte-offset cycles; arithmetic wraps as unsigned x86 words. */
static inline void wc3_random_seed(wc3Random_t *state, uint32_t seed) {
    state->sum = seed;
    state->index = (seed % 59) * 0x400u | (seed % 61) * 4u | (seed % 53) * 0x40000u |
        ((seed / 47) * 17u + seed) * 0x4000000u;
}

/* 1b7130: rotate and XOR four table words, then accumulate one unsigned draw. */
static inline uint32_t wc3_random_next(wc3Random_t *state) {
    unsigned const shifts[4] = {24,16,8,0}, steps[4] = {4,12,24,28}, periods[4] = {188,212,236,244};
    uint32_t index = 0, mix = 0;
    for (unsigned i = 0; i < 4; i++) {
        int off = ((state->index >> shifts[i]) & 255) - steps[i];
        if (off < 0) off += periods[i];
        uint32_t word = wc3_random_words[off / 4];
        unsigned rot = i < 3 ? i + 1 : 0;
        mix ^= rot ? (word << rot) | (word >> (32 - rot)) : word;
        index |= (uint32_t)off << shifts[i];
    }
    state->index = index;
    return state->sum += mix;
}

/* 693710: a local generator seeds each purpose; the path owner is untouched. */
static inline void wc3_random_reseed(wc3Random_t streams[BZ_WC3_RANDOM_STREAMS], uint32_t seed) {
    wc3Random_t local;wc3_random_seed(&local,seed);
    for (unsigned i=0;i<BZ_WC3_RANDOM_STREAMS;i++)wc3_random_seed(streams+i,wc3_random_next(&local));
}

/* 693660 consumes one draw even for a zero or one-element span. */
static inline uint32_t wc3_random_range(wc3Random_t *state, uint32_t span) {
    return (uint32_t)(((uint64_t)wc3_random_next(state)*span)>>32);
}

/* Public scalar natives and overlap direction use the low 23 draw bits. */
static inline float wc3_random_unit(wc3Random_t *state) {
    return wc3_sub(wc3_float(0x3f800000u | (wc3_random_next(state) & 0x7fffffu)), 1);
}

/* 201e30: reversed bounds still anchor at lo; full-width spans wrap to zero. */
static inline int32_t wc3_random_int(wc3Random_t *state, int32_t lo, int32_t hi) {
    if (lo == hi) return lo;
    uint32_t span = (lo > hi ? (uint32_t)lo - (uint32_t)hi : (uint32_t)hi - (uint32_t)lo) + 1u;
    uint32_t word = (uint32_t)lo + (uint32_t)(((uint64_t)wc3_random_next(state) * span) >> 32);
    int32_t result; memcpy(&result, &word, sizeof(result)); return result;
}

/* 201e70: near-equal bounds consume no draw; retain software-scalar rounding. */
static inline float wc3_random_real(wc3Random_t *state, float lo, float hi) {
    float span = wc3_float(wc3_float_bits(wc3_sub(lo, hi)) & 0x7fffffffu);
    if (span < wc3_float(0x3456bf95)) return lo;
    span = lo <= hi ? wc3_sub(hi, lo) : wc3_sub(lo, hi);
    return wc3_add(lo, wc3_mul(span, wc3_random_unit(state)));
}

/* 1d19e0: one owner draw chooses a deterministic XY direction for exact overlaps. */
static inline void wc3_random_direction(wc3Random_t *state, float output[2]) {
    wc3_sincos(wc3_mul(wc3_random_unit(state), wc3_float(0x40c90fdb)), &output[1], &output[0]);
}

#endif
