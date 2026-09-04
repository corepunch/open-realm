#include "g_local.h"
#include "jass/jass.h"

LPGTIMER G_AllocJassTimer(void) {
    if (level.num_timers >= MAX_TIMERS) return NULL;
    LPGTIMER timer = &level.timers[level.num_timers++];
    memset(timer, 0, sizeof(*timer)); return timer;
}

DWORD G_TimerRemaining(LPCGTIMER timer) { return timer ? timer->remaining : 0; }

void G_TimerStart(LPGTIMER timer, DWORD timeout, BOOL periodic, LPCJASSFUNC handler) {
    timer->handler = handler; timer->duration = timeout; timer->remaining = timeout;
    timer->periodic = periodic; timer->paused = false; timer->running = true;
}

void G_TimerPause(LPGTIMER timer) {
    if (!timer || !timer->running || timer->paused) return;
    timer->paused = true;
}

void G_TimerResume(LPGTIMER timer) {
    if (!timer || !timer->running || !timer->paused) return;
    timer->paused = false;
}

/* Timer callbacks enter the same coroutine/event path as authored map triggers. */
void G_RunTimers(void) {
    FOR_LOOP(i, level.num_timers) {
        LPGTIMER timer = &level.timers[i];
        if (!timer->running || timer->paused) continue;
        /* Countdown rather than a level.time deadline: a save carries no clock-absolute
         * state, so a loaded timer resumes with exactly the time it had left. */
        timer->remaining = timer->remaining > FRAMETIME ? timer->remaining - FRAMETIME : 0;
        if (timer->remaining) continue;
        timer->remaining = timer->periodic ? timer->duration : 0;
        timer->running = timer->periodic;
        if (timer->handler)
            jass_startcoroutine(level.vm, &MAKE(JASSCONTEXT, .func = timer->handler, .timer = timer));
        jass_settimercontext(timer);
        FOR_EACH_EVENT(event)
            if (event->type == EVENT_GAME_TIMER_EXPIRED && event->timer == timer)
                jass_calltrigger(level.vm, event->trigger, NULL, NULL);
        jass_settimercontext(NULL);
    }
}