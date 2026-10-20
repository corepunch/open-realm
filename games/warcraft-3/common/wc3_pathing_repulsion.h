#ifndef BZ_WC3_PATHING_REPULSION_H
#define BZ_WC3_PATHING_REPULSION_H
#include "wc3_pathing_random.h"

typedef struct { float radius, minimum, cap, weight, damping; } wc3RepulseConfig_t;
typedef struct { float vector[2]; uint32_t packed; } wc3Repulse_t;
typedef struct { float source[2], other[2]; wc3RepulseConfig_t config; wc3Random_t *random; } wc3RepulsePair_t;

/* Complete original004790 decimal parser/CRT results; host decimal literals differ. */
static uint32_t const wc3_repulse_rows[16][5] = {
    {0x40a00000u, 0x3c23d70bu, 0x3f000000u, 0x3eccccceu, 0x3f333334u},
    {0x40e00001u, 0x3c23d70bu, 0x3e4cccceu, 0x3eccccceu, 0x3f333334u},
    {0x41000000u, 0x3c23d70bu, 0x3e4cccceu, 0x3eccccceu, 0x3f333334u},
    {0x41100000u, 0x3c23d70bu, 0x3e4cccceu, 0x3eccccceu, 0x3f333334u},
    {0x41200000u, 0x3c23d70bu, 0x3e4cccceu, 0x3eccccceu, 0x3f333334u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
};

static inline wc3RepulseConfig_t wc3_repulse_config(uint32_t packed) {
    uint32_t const *row = wc3_repulse_rows[(packed >> 16) & 15];
    return (wc3RepulseConfig_t){wc3_float(row[0]),wc3_float(row[1]),wc3_float(row[2]),
        wc3_float(row[3]),wc3_float(row[4])};
}

/* 695090: authentic CUnit slotEC returns its player word; flag60 bit0 forces nibble15. */
static inline uint32_t wc3_repulse_category(uint32_t player, uint32_t group, bool override) {
    return ((override ? 15 : player & 15) << 4) | (group & 15);
}

/* 1710e0 runs selector, category, then rank setters. Category overwrites
 * selector's high nibble; every authored integer retains its low bits. */
static inline uint32_t wc3_repulse_policy(uint32_t prior, uint32_t selector, uint32_t category, uint32_t rank) {
    return (prior & 0xffffu) | ((selector & 15u) << 16) |
        ((category & 255u) << 20) | ((rank & 15u) << 28);
}

/* 15fc70 resizes unconditionally; velocity cap guards belong to its callers. */
static inline void wc3_repulse_length(float vector[2], float length) {
    float sq = wc3_add(wc3_mul(vector[0],vector[0]),wc3_mul(vector[1],vector[1]));
    float scale = wc3_mul(length,wc3_recip(wc3_sqrt(sq)));
    for (unsigned i = 0; i < 2; i++) vector[i] = wc3_mul(vector[i],scale);
}

/* Preserve candidate order and retained vector; a tiny overlap consumes exactly one owner draw. */
static inline void wc3_repulse_pair(wc3Repulse_t *state, wc3RepulsePair_t const *pair) {
    float delta[2];
    for (unsigned i = 0; i < 2; i++) delta[i] = wc3_sub(pair->source[i],pair->other[i]);
    float length = wc3_sqrt(wc3_add(wc3_mul(delta[0],delta[0]),wc3_mul(delta[1],delta[1])));
    if (length < wc3_float(0x3a83126f)) {
        wc3_random_direction(pair->random,delta);
        length = wc3_sqrt(wc3_add(wc3_mul(delta[0],delta[0]),wc3_mul(delta[1],delta[1])));
    }
    if (!(length < pair->config.radius)) return;
    float frac = wc3_sub(1,wc3_mul(length,wc3_div(1,pair->config.radius)));
    float weight = wc3_mul(wc3_mul(pair->config.weight,frac),frac);
    wc3_repulse_length(delta,weight);
    for (unsigned i = 0; i < 2; i++) state->vector[i] = wc3_add(state->vector[i],delta[i]);
}

/* Damp and cap once after all neighbors; zero/deadzone installs seven skipped visits. */
static inline void wc3_repulse_tail(wc3Repulse_t *state, wc3RepulseConfig_t const *config) {
    float length = wc3_sqrt(wc3_add(wc3_mul(state->vector[0],state->vector[0]),
        wc3_mul(state->vector[1],state->vector[1])));
    length = wc3_mul(length,config->damping);
    if (length < config->minimum) {
        state->vector[0] = state->vector[1] = 0;
        state->packed = (state->packed & 0xffff0000u) | 7; return;
    }
    if (length > config->cap) length = config->cap;
    wc3_repulse_length(state->vector,length);
}

/* 171320 consumes the low ushort while preserving selector/category/rank. */
static inline bool wc3_repulse_cooldown(wc3Repulse_t *state) {
    uint16_t count = state->packed;
    if (!count) return false;
    state->packed = (state->packed & 0xffff0000u) | (uint16_t)(count - 1); return true;
}
#endif
