#ifndef WC3_PATHING_SPEED_H
#define WC3_PATHING_SPEED_H

#include "wc3_math.h"

typedef struct {
    float value, minimum, maximum, default_minimum, default_maximum;
    bool disabled;
} wc3SpeedLimit_t;

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

#endif
