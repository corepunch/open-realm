#ifndef BZ_WC3_PATHING_YIELD_H
#define BZ_WC3_PATHING_YIELD_H

#include "wc3_math.h"

typedef enum { WC3_YIELD_SKIP, WC3_YIELD_SELF, WC3_YIELD_PEER } wc3YieldDecision_t;
typedef struct {
    float velocity[2];
    uint32_t player, group_flags;
    bool grouped, same_group, blocked;
} wc3YieldPeer_t;

/* Original168360 compares actual scalar velocity lengths, independent of caps
 * and repulsion rank. Candidate order and identity resolution belong to callers. */
static inline wc3YieldDecision_t wc3_yield_decide(float const velocity[2], uint32_t player,
                                                wc3YieldPeer_t const *peer) {
    if (!peer->grouped || peer->blocked) return WC3_YIELD_SKIP;
    float other = wc3_add(wc3_mul(peer->velocity[0],peer->velocity[0]),
                          wc3_mul(peer->velocity[1],peer->velocity[1]));
    if (!other) return WC3_YIELD_SKIP;
    float speed = wc3_add(wc3_mul(velocity[0],velocity[0]),wc3_mul(velocity[1],velocity[1]));
    if ((peer->same_group && !(peer->group_flags&8)) || speed<=other || player!=peer->player)
        return WC3_YIELD_SELF;
    return WC3_YIELD_PEER;
}

/* Original165ae0 consumes one enabled eligible advance, including1 ->0. */
static inline bool wc3_yield_advance(uint32_t *delay, bool disabled) {
    if (disabled || !*delay) return false;
    --*delay;
    return true;
}

#endif
