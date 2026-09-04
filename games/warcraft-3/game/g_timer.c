#include "g_local.h"
#include "jass/jass.h"

LPGTIMER G_AllocJassTimer(void) {
    if (level.num_timers >= MAX_TIMERS) return NULL;
    LPGTIMER timer = &level.timers[level.num_timers++];
    memset(timer, 0, sizeof(*timer)); return timer;
}

DWORD G_TimerRemaining(LPCGTIMER timer) {
    if (!timer || !timer->running) return timer ? timer->remaining : 0;
    if (timer->paused) return timer->remaining;
    /* Signed clock deltas keep absolute deadlines valid across DWORD wraparound. */
    return (LONG)(level.time - timer->end) >= 0 ? 0 : timer->end - level.time;
}

void G_TimerStart(LPGTIMER timer, DWORD timeout, BOOL periodic, LPCJASSFUNC handler) {
    timer->handler = handler; timer->duration = timeout; timer->end = level.time + timeout; timer->remaining = timeout;
    timer->periodic = periodic; timer->paused = false; timer->running = true;
}

void G_TimerPause(LPGTIMER timer) {
    if (!timer || !timer->running || timer->paused) return;
    timer->remaining = G_TimerRemaining(timer); timer->paused = true;
}

void G_TimerResume(LPGTIMER timer) {
    if (!timer || !timer->running || !timer->paused) return;
    timer->end = level.time + timer->remaining; timer->paused = false;
}

/* Timer callbacks enter the same coroutine/event path as authored map triggers. */
void G_RunTimers(void) {
    FOR_LOOP(i, level.num_timers) {
        LPGTIMER timer = &level.timers[i];
        if (!timer->running || timer->paused || (LONG)(level.time - timer->end) < 0) continue;
        timer->remaining = timer->periodic ? timer->duration : 0;
        timer->end = level.time + timer->duration; timer->running = timer->periodic;
        if (timer->handler)
            jass_startcoroutine(level.vm, &MAKE(JASSCONTEXT, .func = timer->handler, .timer = timer));
        jass_settimercontext(timer);
        FOR_EACH_EVENT(event)
            if (event->type == EVENT_GAME_TIMER_EXPIRED && event->timer == timer)
                jass_calltrigger(level.vm, event->trigger, NULL, NULL);
        jass_settimercontext(NULL);
    }
}