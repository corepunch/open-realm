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

static uint32_t TimerDialogDisplaySeconds(gtimer_t const *timer) {
    uint32_t millis = G_TimerRemaining(timer);
    return millis / 1000u + (millis % 1000u != 0);
}

static timerdialog_t *VisibleTimerDialogForPlayer(uint32_t player_num, int32_t *index) {
    if (index) *index = -1;
    if (player_num >= MAX_CLIENTS) return NULL;
    FOR_LOOP(i, MAX_TIMERDIALOGS) {
        timerdialog_t *dialog = &level.timer_dialogs[i];
        if (!dialog->inuse || !(dialog->visible_clients & (1u << player_num))) continue;
        if (index) *index = (int32_t)i;
        return dialog;
    }
    return NULL;
}

gtimer_t *G_AllocJassTimer(void) {
    if (level.num_timers >= MAX_TIMERS) return NULL;
    gtimer_t *timer = &level.timers[level.num_timers++];
    memset(timer, 0, sizeof(*timer)); return timer;
}

timerdialog_t *G_AllocTimerDialog(gtimer_t *timer) {
    FOR_LOOP(i, MAX_TIMERDIALOGS) if (!level.timer_dialogs[i].inuse) {
        timerdialog_t *dialog = &level.timer_dialogs[i];
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

void G_FreeTimerDialog(timerdialog_t *dialog) {
    uint32_t dirty;
    if (!dialog || !dialog->inuse) return;
    dirty = dialog->visible_clients;
    memset(dialog, 0, sizeof(*dialog));
    level.timer_dialog_dirty_clients |= dirty;
}

void G_SetTimerDialogVisible(timerdialog_t *dialog, player_t *player, bool visible) {
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

bool G_IsTimerDialogVisible(timerdialog_t const *dialog, player_t const *player) {
    uint32_t mask;
    if (!dialog || !dialog->inuse) return false;
    if (player) {
        uint32_t number = PLAYER_NUM(player);
        return number < MAX_CLIENTS && (dialog->visible_clients & (1u << number));
    }
    mask = TimerDialogPlayerMask();
    return mask && (dialog->visible_clients & mask) == mask;
}

void G_MarkTimerDialogDirty(timerdialog_t const *dialog) {
    if (dialog && dialog->inuse) level.timer_dialog_dirty_clients |= dialog->visible_clients;
}

void G_FormatTimerDialogValue(gtimer_t const *timer, string_t out, size_t out_size) {
    uint32_t seconds = TimerDialogDisplaySeconds(timer);
    uint32_t minutes = seconds / 60u;
    if (!out || !out_size) return;
    snprintf(out, out_size, "%02u:%02u", (unsigned)minutes, (unsigned)(seconds % 60u));
}

uint32_t G_TimerRemaining(gtimer_t const *timer) {
    if (!timer) return 0;
    uint32_t elapsed = timer->running && !timer->paused ? level.time - timer->updated : 0;
    return timer->remaining > elapsed ? timer->remaining - elapsed : 0;
}

void G_TimerStart(gtimer_t *timer, uint32_t timeout, bool periodic, jassFunc_t const *handler) {
    if (!timer) return;
    timer->generation++;
    timer->scalar_timing=false;
    timer->scalar_timeout=timeout/1000.0f;
    timer->handler = handler; timer->duration = timeout; timer->remaining = timeout; timer->updated = level.time;
    timer->periodic = periodic; timer->paused = false; timer->running = true;
    FOR_LOOP(i, MAX_TIMERDIALOGS) if (level.timer_dialogs[i].inuse && level.timer_dialogs[i].timer == timer)
        WC3_TIMERDIALOG_LOG("start dialog=%d duration=%u periodic=%d visible=0x%08x\n",
                            i, (unsigned)timeout, periodic,
                            (unsigned)level.timer_dialogs[i].visible_clients);
}

/* Original249ca0/053630 preserve scalar timeout and rearm from the due clock.
 * The integer API remains useful to C callers; public JASS retains its word. */
void G_TimerStartScalar(gtimer_t *timer, float timeout, bool periodic, jassFunc_t const *handler) {
    G_TimerStart(timer,(uint32_t)(MAX(0.0f,timeout)*1000.0f),periodic,handler);
    if (!timer) return;
    timer->scalar_timeout=timeout;
    timer->scalar_deadline=level.pathing_clock;
    timer->scalar_timing=isfinite(timeout) && timeout>0 && timeout<level.pathing_clock.span;
    if (timer->scalar_timing) wc3_clock_advance(&timer->scalar_deadline,timeout,0);
    /* TODO NUM-02.9/11: general zero/negative/long timeout and epoch producers
     * still use the prior countdown policy until original boundary controls. */
    else if (timeout>0 && level.pathing_clock.span>0)
        fprintf(stderr,"WC3 timer: scalar timeout %.9g outside verified single-span scheduling\n",timeout);
}

void G_TimerPause(gtimer_t *timer) {
    if (!timer || !timer->running || timer->paused) return;
    timer->remaining = G_TimerRemaining(timer); timer->updated = level.time;
    timer->generation++;
    timer->paused = true;
    timer->scalar_timing=false; /* TODO NUM-02.10: retain original scalar paused remainder. */
}

void G_TimerResume(gtimer_t *timer) {
    if (!timer || !timer->running || !timer->paused) return;
    timer->updated = level.time; timer->paused = false;
}

void G_TimerDestroy(gtimer_t *timer) {
    if (!timer) return;
    timer->generation++;
    timer->running = false;
    timer->paused = true;
    /* Destroy releases the callback as well as cancelling its generation.
     * Save/load serializes every allocated timer slot, including retired ones. */
    timer->handler = NULL;
}

bool G_TimerCoroutineValid(handle_t handle, uint32_t generation) {
    gtimer_t const *timer = handle;
    /* A one-shot timer is marked not-running when it expires, but its handler
     * still must run. Periodic timers remain running until explicitly paused. */
    return timer && !timer->paused && timer->generation == generation &&
        (!timer->periodic || timer->running);
}

void G_UpdateTimerDialogs(void) {
    FOR_LOOP(i, MIN((uint32_t)game.max_clients, (uint32_t)MAX_CLIENTS)) {
        gameClient_t *client = &game.clients[i];
        uint32_t player_num = client->ps.number;
        edict_t *ent;
        timerdialog_t *dialog;
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
static void run_timers(wc3Clock_t const *before) {
    FOR_LOOP(i, level.num_timers) {
        gtimer_t *timer = &level.timers[i];
        if (!timer->running || timer->paused) continue;
        if(before) {
            if(!timer->scalar_timing || !level.scheduled_frame)continue;
            wc3Clock_t const *due=&timer->scalar_deadline;
            if(due->epoch==before->epoch ? due->time>=before->time :
                (int32_t)(due->epoch-before->epoch)>=0)continue;
        }
        /* RunFrame drains before every primary quantum and again at publication.
         * Consume elapsed simulation time once; repeated drains cannot age a timer.
         * Save restores both the countdown cursor and the owning server clock. */
        timer->remaining = G_TimerRemaining(timer); timer->updated = level.time;
        if (timer->scalar_timing && level.scheduled_frame) {
            /* Original timer stage advances and drains before primary publication. */
            wc3Clock_t next=level.pathing_clock;
            wc3_clock_advance(&next,wc3_float(0x3ba3d70a),0);
            wc3Clock_t const *due=&timer->scalar_deadline;
            bool ready=next.epoch==due->epoch ? next.time>=due->time :
                (int32_t)(next.epoch-due->epoch)>0;
            if (!ready) continue;
            if (timer->periodic) wc3_clock_advance(&timer->scalar_deadline,timer->scalar_timeout,0);
        } else if (timer->remaining) continue;
        /* TODO NUM-02.10: registration/tie order and overdue catch-up remain open. */
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

/* Original timer heap orders a script deadline before a later owner deadline
 * even when both mature in the same primary quantum. Equal-deadline heap
 * mutation/tie policy remains NUM-02.10; retain existing owner admission there. */
void G_RunTimersBeforePathOwner(wc3Clock_t const *deadline) {run_timers(deadline);}
void G_RunTimers(void) {run_timers(NULL);}
