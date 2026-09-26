#include "g_local.h"
#include "jass/jass.h"

static uint32_t TimerDialogPlayerMask(void) {
    uint32_t mask = 0;
    FOR_LOOP(i, MIN((uint32_t)game.max_clients, (uint32_t)MAX_CLIENTS)) {
        uint32_t number = game.clients[i].ps.number;
        if (number < MAX_CLIENTS) mask |= 1u << number;
    }
    return mask;
}

static uint32_t TimerDialogDisplaySeconds(gtimer_t const * timer) {
    uint32_t millis = G_TimerRemaining(timer);
    return millis / 1000u + (millis % 1000u != 0);
}

static timerdialog_t * VisibleTimerDialogForPlayer(uint32_t player_num, int32_t *index) {
    if (index) *index = -1;
    if (player_num >= MAX_CLIENTS) return NULL;
    FOR_LOOP(i, MAX_TIMERDIALOGS) {
        timerdialog_t * dialog = &level.timer_dialogs[i];
        if (!dialog->inuse || !(dialog->visible_clients & (1u << player_num))) continue;
        if (index) *index = (int32_t)i;
        return dialog;
    }
    return NULL;
}

gtimer_t * G_AllocJassTimer(void) {
    if (level.num_timers >= MAX_TIMERS) return NULL;
    gtimer_t * timer = &level.timers[level.num_timers++];
    memset(timer, 0, sizeof(*timer)); return timer;
}

timerdialog_t * G_AllocTimerDialog(gtimer_t * timer) {
    FOR_LOOP(i, MAX_TIMERDIALOGS) if (!level.timer_dialogs[i].inuse) {
        timerdialog_t * dialog = &level.timer_dialogs[i];
        memset(dialog, 0, sizeof(*dialog));
        dialog->inuse = true;
        dialog->timer = timer;
        WC3_TIMERDIALOG_LOG("create dialog=%ld timer=%p running=%d remaining=%u\n",
                            (long)i, (void *)timer, timer ? timer->running : 0,
                            (unsigned)G_TimerRemaining(timer));
        return dialog;
    }
    return NULL;
}

void G_FreeTimerDialog(timerdialog_t * dialog) {
    uint32_t dirty;
    if (!dialog || !dialog->inuse) return;
    dirty = dialog->visible_clients;
    memset(dialog, 0, sizeof(*dialog));
    level.timer_dialog_dirty_clients |= dirty;
}

void G_SetTimerDialogVisible(timerdialog_t * dialog, player_t * player, bool visible) {
    uint32_t mask, old_mask;
    if (!dialog || !dialog->inuse) return;
    if (player) {
        uint32_t number = PLAYER_NUM(player);
        if (number >= MAX_CLIENTS) return;
        mask = 1u << number;
    } else {
        mask = TimerDialogPlayerMask();
    }
    old_mask = dialog->visible_clients;
    dialog->visible_clients = visible ? (old_mask | mask) : (old_mask & ~mask);
    level.timer_dialog_dirty_clients |= mask;
    WC3_TIMERDIALOG_LOG("display dialog=%ld player=%d visible=%d clients=0x%08x->0x%08x timer_running=%d remaining=%u\n",
                        (long)(dialog - level.timer_dialogs), player ? (int)PLAYER_NUM(player) : -1,
                        visible, (unsigned)old_mask, (unsigned)dialog->visible_clients,
                        dialog->timer ? dialog->timer->running : 0,
                        (unsigned)G_TimerRemaining(dialog->timer));
}

bool G_IsTimerDialogVisible(timerdialog_t const * dialog, player_t const * player) {
    uint32_t mask;
    if (!dialog || !dialog->inuse) return false;
    if (player) {
        uint32_t number = PLAYER_NUM(player);
        return number < MAX_CLIENTS && (dialog->visible_clients & (1u << number));
    }
    mask = TimerDialogPlayerMask();
    return mask && (dialog->visible_clients & mask) == mask;
}

void G_MarkTimerDialogDirty(timerdialog_t const * dialog) {
    if (dialog && dialog->inuse) level.timer_dialog_dirty_clients |= dialog->visible_clients;
}

void G_FormatTimerDialogValue(gtimer_t const * timer, string_t out, size_t out_size) {
    uint32_t seconds = TimerDialogDisplaySeconds(timer);
    uint32_t minutes = seconds / 60u;
    if (!out || !out_size) return;
    snprintf(out, out_size, "%02u:%02u", (unsigned)minutes, (unsigned)(seconds % 60u));
}

uint32_t G_TimerRemaining(gtimer_t const * timer) { return timer ? timer->remaining : 0; }

void G_TimerStart(gtimer_t * timer, uint32_t timeout, bool periodic, jassFunc_t const * handler) {
    if (!timer) return;
    timer->generation++;
    timer->handler = handler; timer->duration = timeout; timer->remaining = timeout;
    timer->periodic = periodic; timer->paused = false; timer->running = true;
    FOR_LOOP(i, MAX_TIMERDIALOGS) if (level.timer_dialogs[i].inuse && level.timer_dialogs[i].timer == timer)
        WC3_TIMERDIALOG_LOG("start dialog=%d duration=%u periodic=%d visible=0x%08x\n",
                            i, (unsigned)timeout, periodic,
                            (unsigned)level.timer_dialogs[i].visible_clients);
}

void G_TimerPause(gtimer_t * timer) {
    if (!timer || !timer->running || timer->paused) return;
    timer->generation++;
    timer->paused = true;
}

void G_TimerResume(gtimer_t * timer) {
    if (!timer || !timer->running || !timer->paused) return;
    timer->paused = false;
}

void G_TimerDestroy(gtimer_t * timer) {
    if (!timer) return;
    timer->generation++;
    timer->running = false;
    timer->paused = true;
}

bool G_TimerCoroutineValid(handle_t handle, uint32_t generation) {
    gtimer_t const * timer = handle;
    /* A one-shot timer is marked not-running when it expires, but its handler
     * still must run. Periodic timers remain running until explicitly paused. */
    return timer && !timer->paused && timer->generation == generation &&
        (!timer->periodic || timer->running);
}

void G_UpdateTimerDialogs(void) {
    FOR_LOOP(i, MIN((uint32_t)game.max_clients, (uint32_t)MAX_CLIENTS)) {
        gameClient_t * client = &game.clients[i];
        uint32_t player_num = client->ps.number;
        edict_t * ent;
        timerdialog_t * dialog;
        int32_t dialog_index;
        int32_t seconds = -1;
        bool dirty;

        if (!client->connected || player_num >= MAX_CLIENTS) continue;
        dialog = VisibleTimerDialogForPlayer(player_num, &dialog_index);
        if (dialog && dialog->timer)
            seconds = (int32_t)TimerDialogDisplaySeconds(dialog->timer);
        dirty = (level.timer_dialog_dirty_clients & (1u << player_num)) != 0;
        if (!dirty && level.timer_dialog_last_index[player_num] == dialog_index &&
            level.timer_dialog_last_seconds[player_num] == seconds) continue;

        ent = G_GetPlayerEntityByNumber(player_num);
        if (dialog && (dirty || (seconds >= 0 && (seconds % 30) == 0)) )
            WC3_TIMERDIALOG_LOG("hud update client_slot=%u player=%u dialog=%ld seconds=%ld dirty=%d player_ent=%d client=%d\n",
                                (unsigned)i, (unsigned)player_num, (long)dialog_index,
                                (long)seconds, dirty,
                                ent != NULL, ent && ent->client != NULL);
        if (!ent || !ent->client) continue;
        UI_WriteTimerDialogs(ent);
        level.timer_dialog_last_index[player_num] = dialog_index;
        level.timer_dialog_last_seconds[player_num] = seconds;
        level.timer_dialog_dirty_clients &= ~(1u << player_num);
    }
}

/* Timer callbacks enter the same coroutine/event path as authored map triggers. */
void G_RunTimers(void) {
    FOR_LOOP(i, level.num_timers) {
        gtimer_t * timer = &level.timers[i];
        if (!timer->running || timer->paused) continue;
        /* Countdown rather than a level.time deadline: a save carries no clock-absolute
         * state, so a loaded timer resumes with exactly the time it had left. */
        timer->remaining = timer->remaining > FRAMETIME ? timer->remaining - FRAMETIME : 0;
        if (timer->remaining) continue;
        timer->remaining = timer->periodic ? timer->duration : 0;
        timer->running = timer->periodic;
        if (timer->handler)
            jass_startcoroutine(level.vm, &MAKE(jassContext_t,
                .func = timer->handler, .timer = timer,
                .timer_generation = timer->generation, .timer_pending = true));
        jass_settimercontext(timer);
        FOR_EACH_EVENT(event)
            if (event->type == EVENT_GAME_TIMER_EXPIRED && event->timer == timer)
                jass_calltriggerwithtimer(level.vm, event->trigger, timer);
        jass_settimercontext(NULL);
    }
}
