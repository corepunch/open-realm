#ifndef BZ_FRAME_BUDGET_H
#define BZ_FRAME_BUDGET_H

#include <stdint.h>
#include <stdbool.h>

/* A presentation deadline, independent of the authoritative simulation clock.
 * Reserve the largest recent client cost plus one millisecond of headroom. */
typedef struct {
    uint64_t start, frequency, period, render[16];
    uint32_t cursor;
} hostFrameBudget_t;

static inline uint64_t Host_FrameWorkBudget(hostFrameBudget_t const *frame) {
    uint64_t reserve = 0;
    for (uint32_t i = 0; i < 16; i++)
        if (frame->render[i] > reserve) reserve = frame->render[i];
    reserve += frame->frequency / 1000;
    uint64_t minimum = frame->frequency / 4000;
    /* A render that already exhausts the deadline cannot be paid for by
     * redrawing after 250 microseconds of simulation. Give the simulation a
     * whole work period in this saturated state; otherwise repeated stale
     * frames amplify the overrun and prevent the next snapshot completing. */
    return frame->period > reserve + minimum ? frame->period - reserve : frame->period;
}

static inline bool Host_FrameCheckpointDue(hostFrameBudget_t const *frame, uint64_t now) {
    return now - frame->start >= Host_FrameWorkBudget(frame);
}

static inline void Host_FrameRendered(hostFrameBudget_t *frame, uint64_t cost) {
    frame->render[frame->cursor++ & 15] = cost;
}

#endif
