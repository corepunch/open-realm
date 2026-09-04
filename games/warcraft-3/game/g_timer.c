#include "g_local.h"
#include "jass/jass.h"

BOOL G_RegisterJassTimer(LPGTIMER timer) {
    if (!timer || level.num_timers >= MAX_TIMERS) return false;
    level.timers[level.num_timers++] = timer;
    return true;
}

DWORD G_TimerRemaining(LPCGTIMER timer) {
    DWORD now;
    if (!timer || !timer->running) return timer ? timer->remaining : 0;
    if (timer->paused) return timer->remaining;
    now = gi.GetTime();
    return now - timer->started >= timer->timeout ? 0 : timer->timeout - (now - timer->started);
}

void G_TimerStart(LPGTIMER timer, DWORD timeout, BOOL periodic, LPCJASSFUNC handler) {
    timer->handler = handler; timer->started = gi.GetTime(); timer->duration = timeout;
    timer->timeout = timeout; timer->remaining = timeout;
    timer->periodic = periodic; timer->paused = false; timer->running = true;
}

void G_TimerPause(LPGTIMER timer) {
    if (!timer || !timer->running || timer->paused) return;
    timer->remaining = G_TimerRemaining(timer); timer->paused = true;
}

void G_TimerResume(LPGTIMER timer) {
    if (!timer || !timer->running || !timer->paused) return;
    timer->started = gi.GetTime(); timer->timeout = timer->remaining; timer->paused = false;
}

/* Timer callbacks enter the same coroutine/event path as authored map triggers. */
void G_RunTimers(void) {
    DWORD now = gi.GetTime();
    FOR_LOOP(i, level.num_timers) {
        LPGTIMER timer = level.timers[i];
        if (!timer || !timer->running || timer->paused || now - timer->started < timer->timeout) continue;
        timer->remaining = timer->periodic ? timer->duration : 0;
        timer->started = now; timer->timeout = timer->duration; timer->running = timer->periodic;
        if (timer->handler)
            jass_startcoroutine(level.vm, &MAKE(JASSCONTEXT, .func = timer->handler, .timer = timer));
        jass_settimercontext(timer);
        FOR_EACH_EVENT(event)
            if (event->type == EVENT_GAME_TIMER_EXPIRED && event->timer == timer)
                jass_calltrigger(level.vm, event->trigger, NULL, NULL);
        jass_settimercontext(NULL);
    }
}