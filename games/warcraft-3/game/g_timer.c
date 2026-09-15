#include "g_local.h"
#include "jass/jass.h"

static DWORD TimerDialogClientMask(void) {
    DWORD count = MIN((DWORD)game.max_clients, (DWORD)MAX_CLIENTS);
    return count ? (DWORD)((1ull << count) - 1ull) : 0;
}

static DWORD TimerDialogDisplaySeconds(LPCGTIMER timer) {
    DWORD millis = G_TimerRemaining(timer);
    return millis / 1000u + (millis % 1000u != 0);
}

static LPTIMERDIALOG VisibleTimerDialogForClient(DWORD client_num, LONG *index) {
    if (index) *index = -1;
    if (client_num >= MAX_CLIENTS) return NULL;
    FOR_LOOP(i, MAX_TIMERDIALOGS) {
        LPTIMERDIALOG dialog = &level.timer_dialogs[i];
        if (!dialog->inuse || !(dialog->visible_clients & (1u << client_num))) continue;
        if (index) *index = (LONG)i;
        return dialog;
    }
    return NULL;
}

LPGTIMER G_AllocJassTimer(void) {
    if (level.num_timers >= MAX_TIMERS) return NULL;
    LPGTIMER timer = &level.timers[level.num_timers++];
    memset(timer, 0, sizeof(*timer)); return timer;
}

LPTIMERDIALOG G_AllocTimerDialog(LPGTIMER timer) {
    FOR_LOOP(i, MAX_TIMERDIALOGS) if (!level.timer_dialogs[i].inuse) {
        LPTIMERDIALOG dialog = &level.timer_dialogs[i];
        memset(dialog, 0, sizeof(*dialog));
        dialog->inuse = true;
        dialog->timer = timer;
        return dialog;
    }
    return NULL;
}

void G_FreeTimerDialog(LPTIMERDIALOG dialog) {
    DWORD dirty;
    if (!dialog || !dialog->inuse) return;
    dirty = dialog->visible_clients;
    memset(dialog, 0, sizeof(*dialog));
    level.timer_dialog_dirty_clients |= dirty;
}

void G_SetTimerDialogVisible(LPTIMERDIALOG dialog, LPPLAYER player, BOOL visible) {
    DWORD mask;
    if (!dialog || !dialog->inuse) return;
    if (player) {
        DWORD number = PLAYER_NUM(player);
        if (number >= MAX_CLIENTS) return;
        mask = 1u << number;
    } else {
        mask = TimerDialogClientMask();
    }
    if (visible) dialog->visible_clients |= mask;
    else dialog->visible_clients &= ~mask;
    level.timer_dialog_dirty_clients |= mask;
}

BOOL G_IsTimerDialogVisible(LPCTIMERDIALOG dialog, LPCPLAYER player) {
    DWORD mask;
    if (!dialog || !dialog->inuse) return false;
    if (player) {
        DWORD number = PLAYER_NUM(player);
        return number < MAX_CLIENTS && (dialog->visible_clients & (1u << number));
    }
    mask = TimerDialogClientMask();
    return mask && (dialog->visible_clients & mask) == mask;
}

void G_MarkTimerDialogDirty(LPCTIMERDIALOG dialog) {
    if (dialog && dialog->inuse) level.timer_dialog_dirty_clients |= dialog->visible_clients;
}

void G_FormatTimerDialogValue(LPCGTIMER timer, LPSTR out, size_t out_size) {
    DWORD seconds = TimerDialogDisplaySeconds(timer);
    DWORD minutes = seconds / 60u;
    if (!out || !out_size) return;
    snprintf(out, out_size, "%02u:%02u", (unsigned)minutes, (unsigned)(seconds % 60u));
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

void G_UpdateTimerDialogs(void) {
    FOR_LOOP(i, MIN((DWORD)game.max_clients, (DWORD)MAX_CLIENTS)) {
        LPGAMECLIENT client = &game.clients[i];
        LPEDICT ent;
        LPTIMERDIALOG dialog;
        LONG dialog_index;
        LONG seconds = -1;
        BOOL dirty;

        if (!client->connected) continue;
        dialog = VisibleTimerDialogForClient(i, &dialog_index);
        if (dialog && dialog->timer)
            seconds = (LONG)TimerDialogDisplaySeconds(dialog->timer);
        dirty = (level.timer_dialog_dirty_clients & (1u << i)) != 0;
        if (!dirty && level.timer_dialog_last_index[i] == dialog_index &&
            level.timer_dialog_last_seconds[i] == seconds) continue;

        ent = G_GetPlayerEntityByNumber(i);
        if (!ent || !ent->client) continue;
        UI_WriteTimerDialogs(ent);
        level.timer_dialog_last_index[i] = dialog_index;
        level.timer_dialog_last_seconds[i] = seconds;
        level.timer_dialog_dirty_clients &= ~(1u << i);
    }
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
