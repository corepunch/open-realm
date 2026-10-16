#ifndef WC3_PATHING_SPEED_H
#define WC3_PATHING_SPEED_H

#include "wc3_math.h"

typedef struct {
    float value, minimum, maximum, default_minimum, default_maximum;
    bool disabled;
} wc3SpeedLimit_t;

/* Original48f410 keeps max(0, attached virtual184 bonuses), including AIms. */
static inline float wc3_speed_bonus_max(float current, float bonus) {
    return bonus > current ? bonus : current;
}

/* Original5fc900 ordinary profile-bound tail. A zero profile bound selects
 * Misc's default; a nonzero bound is first clamped to the immutable1..522
 * domain. Defaults themselves are copied unchanged. Special ability/type
 * overrides and the upstream effect stack remain MOVE-01.1/2. */
static inline float wc3_speed_limit_update(wc3SpeedLimit_t *s) {
    if (s->disabled) return 0;
    if (s->minimum == 0) s->minimum = s->default_minimum;
    else {
        if (s->minimum < 1) s->minimum = 1;
        if (s->minimum > 522) s->minimum = 522;
    }
    if (s->maximum == 0) s->maximum = s->default_maximum;
    else {
        if (s->maximum < 1) s->maximum = 1;
        if (s->maximum > 522) s->maximum = 522;
    }
    if (s->value < s->minimum) s->value = s->minimum;
    if (s->value > s->maximum) s->value = s->maximum;
    return s->value;
}

/* Fine-cell inputs to original16b5c0, after member decisions and before the
 * velocity commit. Keep the sampled destination distinct from the route goal. */
typedef struct {
    float requested, cap, target_maximum, target_velocity[2];
    float source[2], destination[2], arrival_range;
    uint32_t flags, unseen;
    bool target;
} wc3GroupSpeed_t;

static inline float wc3_group_commit_speed(wc3GroupSpeed_t const *s) {
    float speed=s->requested < s->cap ? s->requested : s->cap;
    if (!(s->flags&0x800) || s->unseen || !(speed>0) || !s->target ||
        !(speed>s->target_maximum)) return speed;
    float moving=wc3_add(wc3_mul(s->target_velocity[0],s->target_velocity[0]),
                         wc3_mul(s->target_velocity[1],s->target_velocity[1]));
    if (!(moving>0)) return speed;
    float threshold=wc3_add(s->arrival_range,4);
    float x=wc3_sub(s->source[0],s->destination[0]),y=wc3_sub(s->source[1],s->destination[1]);
    if (wc3_mul(threshold,threshold)>wc3_add(wc3_mul(x,x),wc3_mul(y,y)))
        speed=wc3_mul(s->target_maximum,wc3_float(0x3f733334)); /* Runtime-parsed 0.95. */
    return speed;
}

/* Attack49bc40 queries remaining (virtual18), then subtracts it from3.
 * Repeated notifications before half a second do not replace the request. */
static inline bool wc3_attack_speed_cap_rearm(bool active, float remaining) {
    return !active || wc3_sub(3,remaining)>=.5f;
}

/* Attack49d050 arms d01b2 at the committed hit. The native minimums are
 * scheduler gaps (cd53a4/cd53a8), independent of the model animation. */
#define WC3_ATTACK_SWING_GAP .01f
#define WC3_ATTACK_COOLDOWN_GAP .02f
static inline float wc3_attack_swing_delay(float backswing, float divisor, float *remaining) {
    float delay=wc3_div(backswing,divisor);
    if (delay<WC3_ATTACK_SWING_GAP) delay=WC3_ATTACK_SWING_GAP;
    if (*remaining<WC3_ATTACK_COOLDOWN_GAP) *remaining=WC3_ATTACK_COOLDOWN_GAP;
    float maximum=wc3_sub(*remaining,WC3_ATTACK_SWING_GAP);
    if (delay>maximum) delay=maximum;
    return delay;
}

#endif
