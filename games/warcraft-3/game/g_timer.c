#include "g_local.h"
#include "jass/jass.h"

/* The native script clock heap compares deadline, then unsigned registration
 * serial. Slots are identities, not priorities; canceled timers unlink in O(log n). */
static bool TimerLess(uint32_t a, uint32_t b) {
    gtimer_t const *left=level.timers+a,*right=level.timers+b;
    return left->scalar_deadline.time==right->scalar_deadline.time ?
        left->scalar_sequence<right->scalar_sequence : left->scalar_deadline.time<right->scalar_deadline.time;
}
static void TimerHeapPut(uint32_t index,uint32_t slot) {
    level.timer_heap[index]=slot;level.timers[slot].scalar_heap_index=(int32_t)index;
}
static void TimerHeapDown(uint32_t index,uint32_t slot) {
    while(index*2+1<level.timer_heap_count) {
        uint32_t child=index*2+1;
        if(child+1<level.timer_heap_count && TimerLess(level.timer_heap[child+1],level.timer_heap[child]))child++;
        if(TimerLess(slot,level.timer_heap[child]))break;
        TimerHeapPut(index,level.timer_heap[child]);index=child;
    }
    TimerHeapPut(index,slot);
}
static void TimerHeapRemove(gtimer_t *timer) {
    int32_t index=timer->scalar_heap_index;timer->scalar_heap_index=-1;
    if(index<0 || (uint32_t)index>=level.timer_heap_count || level.timer_heap[index]!=(uint32_t)(timer-level.timers))return;
    uint32_t last=level.timer_heap[--level.timer_heap_count];
    if((uint32_t)index==level.timer_heap_count)return;
    while(index>0 && TimerLess(last,level.timer_heap[(index-1)/2])) {
        TimerHeapPut(index,level.timer_heap[(index-1)/2]);index=(index-1)/2;
    }
    TimerHeapDown(index,last);
}
static void TimerHeapInsert(gtimer_t *timer) {
    uint32_t slot=(uint32_t)(timer-level.timers),index=level.timer_heap_count++;
    while(index && TimerLess(slot,level.timer_heap[(index-1)/2])) {
        TimerHeapPut(index,level.timer_heap[(index-1)/2]);index=(index-1)/2;
    }
    TimerHeapPut(index,slot);
}
/* C millisecond producers retain ascending-slot iteration, including mutations
 * of later slots. Public scalar timers never enter this small derived set. */
_Static_assert(MAX_TIMERS<=4096,"timer membership summary holds64 words");
static void TimerIntegerPut(gtimer_t const *timer,bool present) {
    uint32_t slot=(uint32_t)(timer-level.timers),word=slot/64;
    uint64_t bit=UINT64_C(1)<<(slot%64);
    if(present)level.timer_integer_bits[word]|=bit;
    else level.timer_integer_bits[word]&=~bit;
    if(level.timer_integer_bits[word])level.timer_integer_top|=UINT64_C(1)<<word;
    else level.timer_integer_top&=~(UINT64_C(1)<<word);
}
static uint32_t TimerIntegerNext(uint32_t from) {
    if(from>=MAX_TIMERS)return MAX_TIMERS;
    uint32_t word=from/64;
    uint64_t bits=level.timer_integer_bits[word]&(UINT64_MAX<<(from%64));
    if(bits)return word*64+__builtin_ctzll(bits);
    if(word>=63)return MAX_TIMERS;
    uint64_t words=level.timer_integer_top&~((UINT64_C(1)<<(word+1))-1);
    if(!words)return MAX_TIMERS;
    word=__builtin_ctzll(words);return word*64+__builtin_ctzll(level.timer_integer_bits[word]);
}
void G_RebuildTimerQueue(void) {
    level.timer_heap_count=0;level.timer_integer_top=0;
    memset(level.timer_integer_bits,0,sizeof(level.timer_integer_bits));
    FOR_LOOP(i,level.num_timers)level.timers[i].scalar_heap_index=-1;
    FOR_LOOP(i,level.num_timers) {
        gtimer_t *timer=level.timers+i;
        if(timer->running && !timer->paused) {
            if(timer->scalar_timing)TimerHeapInsert(timer);else TimerIntegerPut(timer,true);
        }
    }
}
wc3Clock_t G_TimerQueryClock(jassContext_t const *context) {
    if(context && context->hasTimerClock)return context->timer_clock;
    if(level.timer_clock_valid && level.timer_source_clock.epoch==level.pathing_clock.epoch &&
       wc3_float_bits(level.timer_source_clock.time)==wc3_float_bits(level.pathing_clock.time))return level.timer_clock;
    return level.pathing_clock;
}
static uint32_t TimerMillis(float seconds) {
    if(!(seconds>0))return 0;
    float millis=wc3_mul(seconds,1000);
    return millis>=4294967296.0f ? UINT32_MAX : (uint32_t)millis;
}
/* Registered002170 initializes the segment quantum to120, independently of
 * the primary clock's300-second epoch. Getter operation order is observable. */
static float TimerSegment(void) {return wc3_float(0x42f00000);}
static float TimerMinimum(void) {return wc3_float(0x38d1b717);}
float G_TimerRemainingScalar(gtimer_t const *timer,wc3Clock_t const *clock) {
    if(!timer)return 0;
    if(!timer->scalar_timing)return G_TimerRemaining(timer)/1000.0f;
    if(!timer->running || timer->paused)return timer->scalar_paused_remaining;
    float deadline=timer->scalar_deadline.time;
    /* Native rebase applies to queued raw deadlines at the epoch boundary.
     * A callback in the earlier drain still reads the unre-based words. */
    int32_t epochs=(int32_t)(clock->epoch-timer->scalar_deadline.epoch);
    if(epochs==1)deadline=wc3_sub(deadline,clock->span);
    else if(epochs==-1)deadline=wc3_add(deadline,clock->span);
    else if(epochs)return 0; /* Queued requests are rebased every single-wrap quantum. */
    float remaining=wc3_sub(deadline,clock->time);
    if(!timer->scalar_segments)return remaining;
    float result=0;
    if(timer->scalar_remaining_segments>1)
        result=wc3_add(result,wc3_mul(wc3_float(wc3_from_int(timer->scalar_remaining_segments-1)),TimerSegment()));
    result=wc3_add(result,remaining);
    if(timer->scalar_remaining_segments)result=wc3_add(result,timer->scalar_residual);
    return result;
}
float G_TimerElapsedScalar(gtimer_t const *timer,wc3Clock_t const *clock) {
    if(!timer)return 0;
    if(!timer->scalar_timing)return (timer->duration-G_TimerRemaining(timer))/1000.0f;
    float period=timer->scalar_timeout;
    if(timer->running && !timer->paused)period=timer->scalar_segments ?
        wc3_add(wc3_mul(wc3_float(wc3_from_int(timer->scalar_segments)),TimerSegment()),timer->scalar_residual) : timer->scalar_period;
    return wc3_sub(period,G_TimerRemainingScalar(timer,clock));
}
static void TimerPrepare(gtimer_t *timer,float timeout,wc3Clock_t const *clock) {
    timer->scalar_segments=(uint16_t)((int32_t)wc3_int_bits(wc3_float_bits(timeout))/120);
    timer->scalar_remaining_segments=timer->scalar_segments;
    timer->scalar_residual=wc3_modulo(timeout,TimerSegment());
    timer->scalar_segmented=timer->scalar_segments!=0;
    timer->scalar_period=timer->scalar_segmented ? TimerSegment() : timer->scalar_residual;
    if(timer->scalar_period<TimerMinimum())timer->scalar_period=TimerMinimum();
    timer->scalar_deadline=*clock;
    timer->scalar_deadline.time=wc3_add(clock->time,timer->scalar_period);
    timer->scalar_sequence=++level.timer_sequence;
    timer->scalar_timing=true;timer->running=true;timer->paused=false;
    TimerIntegerPut(timer,false);TimerHeapInsert(timer);
}

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
    memset(timer, 0, sizeof(*timer)); timer->scalar_heap_index=-1;
    if(level.num_timers==1) {
        level.timer_clock_valid=false;level.timer_integer_top=0;level.timer_heap_count=0;
        memset(level.timer_integer_bits,0,sizeof(level.timer_integer_bits));
    }
    return timer;
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
    if(timer->scalar_timing){wc3Clock_t clock=G_TimerQueryClock(NULL);return TimerMillis(G_TimerRemainingScalar(timer,&clock));}
    uint32_t elapsed = timer->running && !timer->paused ? level.time - timer->updated : 0;
    return timer->remaining > elapsed ? timer->remaining - elapsed : 0;
}

void G_TimerStart(gtimer_t *timer, uint32_t timeout, bool periodic, jassFunc_t const *handler) {
    if (!timer) return;
    TimerHeapRemove(timer);
    timer->generation++;
    timer->scalar_timing=false;
    timer->scalar_timeout=timeout/1000.0f;
    timer->handler = handler; timer->duration = timeout; timer->remaining = timeout; timer->updated = level.time;
    timer->periodic = periodic; timer->paused = false; timer->running = true;
    TimerIntegerPut(timer,true);
    FOR_LOOP(i, MAX_TIMERDIALOGS) if (level.timer_dialogs[i].inuse && level.timer_dialogs[i].timer == timer)
        WC3_TIMERDIALOG_LOG("start dialog=%d duration=%u periodic=%d visible=0x%08x\n",
                            i, (unsigned)timeout, periodic,
                            (unsigned)level.timer_dialogs[i].visible_clients);
}

/* Public Start retains its authored scalar independently of counted periods. */
void G_TimerStartScalarAt(gtimer_t *timer,float timeout,bool periodic,jassFunc_t const *handler,wc3Clock_t const *clock) {
    G_TimerStart(timer,TimerMillis(timeout),periodic,handler);
    if(!timer)return;
    timer->scalar_timeout=timeout;
    if(isfinite(timeout) && clock->span>0)TimerPrepare(timer,timeout,clock);
}
void G_TimerStartScalar(gtimer_t *timer,float timeout,bool periodic,jassFunc_t const *handler) {
    wc3Clock_t clock=G_TimerQueryClock(NULL);
    G_TimerStartScalarAt(timer,timeout,periodic,handler,&clock);
}
void G_TimerPauseAt(gtimer_t *timer,wc3Clock_t const *clock) {
    if(!timer)return;
    if(!timer->scalar_timing){G_TimerPause(timer);return;}
    timer->scalar_paused_remaining=G_TimerRemainingScalar(timer,clock);
    timer->remaining=TimerMillis(timer->scalar_paused_remaining);timer->updated=level.time;
    TimerHeapRemove(timer);timer->generation++;timer->paused=true;
    /* Original0606c0 clears control periodic2 as well as the request. */
    timer->periodic=false;
}
void G_TimerResumeAt(gtimer_t *timer,wc3Clock_t const *clock) {
    if(!timer)return;
    if(!timer->scalar_timing){G_TimerResume(timer);return;}
    if(timer->running && !timer->paused)return;
    TimerHeapRemove(timer);timer->generation++;
    TimerPrepare(timer,timer->scalar_paused_remaining,clock);
}

void G_TimerPause(gtimer_t *timer) {
    if(timer && timer->scalar_timing){wc3Clock_t clock=G_TimerQueryClock(NULL);G_TimerPauseAt(timer,&clock);return;}
    if (!timer || !timer->running || timer->paused) return;
    timer->remaining = G_TimerRemaining(timer); timer->updated = level.time;
    timer->generation++;
    timer->paused = true;TimerIntegerPut(timer,false);
}

void G_TimerResume(gtimer_t *timer) {
    if(timer && timer->scalar_timing){wc3Clock_t clock=G_TimerQueryClock(NULL);G_TimerResumeAt(timer,&clock);return;}
    if (!timer || !timer->running || !timer->paused) return;
    timer->updated = level.time; timer->paused = false;TimerIntegerPut(timer,true);
}

void G_TimerDestroy(gtimer_t *timer) {
    if (!timer) return;
    TimerHeapRemove(timer);TimerIntegerPut(timer,false);
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

/* Public callbacks execute at their original timer deadline before rearm.
 * Yield releases the borrowed clock context; later resumes use current time. */
static void TimerFireScalar(gtimer_t *timer) {
    wc3Clock_t due=timer->scalar_deadline;
    uint32_t generation=timer->generation;
    bool periodic=timer->periodic;
    if(timer->scalar_segmented) {
        if(--timer->scalar_remaining_segments) {
            timer->scalar_deadline.time=wc3_add(due.time,timer->scalar_period);TimerHeapInsert(timer);return;
        }
        if(timer->scalar_residual>=0) {
            timer->scalar_segmented=false;timer->scalar_period=MAX(timer->scalar_residual,TimerMinimum());
            timer->scalar_deadline.time=wc3_add(due.time,timer->scalar_period);
            timer->scalar_sequence=++level.timer_sequence;TimerHeapInsert(timer);return;
        }
    }
    timer->scalar_fired_clock=due;
    if(!periodic)timer->running=false;
    else if(timer->scalar_segments) {
        /* Original060d40 publishes the next counted request before invoking
         * authored code; a callback getter must see the new120-second phase. */
        timer->scalar_remaining_segments=timer->scalar_segments;timer->scalar_segmented=true;
        timer->scalar_period=TimerSegment();timer->scalar_sequence=++level.timer_sequence;
        timer->scalar_deadline=due;timer->scalar_deadline.time=wc3_add(due.time,timer->scalar_period);
        TimerHeapInsert(timer);
    }
    if(timer->handler) {
        jasscoroutine_t *co=jass_startcoroutine(level.vm,&MAKE(jassContext_t,
            .func=timer->handler,.timer=timer,.timer_generation=generation,.timer_pending=true,
            .timer_clock=due,.hasTimerClock=true));
        jass_resume(level.vm,co);
    }
    jass_settimercontext(timer);
    FOR_EACH_EVENT(event)
        if(event->type==EVENT_GAME_TIMER_EXPIRED && event->timer==timer)
            jass_calltriggerwithtimer(level.vm,event->trigger,timer);
    jass_settimercontext(NULL);jass_runnewevents(level.vm);
    if(generation!=timer->generation || timer->paused || !periodic || timer->scalar_segments)return;
    timer->scalar_deadline=due;timer->scalar_deadline.time=wc3_add(due.time,timer->scalar_period);
    TimerHeapInsert(timer);
}
static void TimerDrain(float limit,bool exclusive) {
    while(level.timer_heap_count) {
        gtimer_t *timer=level.timers+level.timer_heap[0];
        float due=timer->scalar_deadline.time;
        if(due>limit || (exclusive && due==limit))break;
        TimerHeapRemove(timer);TimerFireScalar(timer);
    }
}
static void RunScalarTimers(wc3Clock_t const *before) {
    wc3Clock_t target=level.pathing_clock;
    wc3_clock_advance(&target,wc3_float(0x3ba3d70a),0);
    if(before && (before->epoch!=target.epoch ? (int32_t)(before->epoch-target.epoch)<0 : before->time<target.time))target=*before;
    wc3Clock_t now=G_TimerQueryClock(NULL);
    level.timer_source_clock=level.pathing_clock;level.timer_clock_valid=true;
    if(now.epoch!=target.epoch) {
        TimerDrain(now.span,false);
        FOR_LOOP(i,level.timer_heap_count) {
            gtimer_t *timer=level.timers+level.timer_heap[i];
            timer->scalar_deadline.time=wc3_sub(timer->scalar_deadline.time,now.span);timer->scalar_deadline.epoch++;
        }
    }
    level.timer_clock=target;
    TimerDrain(target.time,before!=NULL);
}
/* Integer C producers keep their existing host-millisecond observation boundary. */
static void RunIntegerTimers(void) {
    for(uint32_t i=TimerIntegerNext(0);i<level.num_timers;i=TimerIntegerNext(i+1)) {
        gtimer_t *timer=level.timers+i;
        timer->remaining=G_TimerRemaining(timer);timer->updated=level.time;
        if(timer->remaining)continue;
        timer->remaining=timer->periodic ? timer->duration : 0;timer->running=timer->periodic;
        if(!timer->running)TimerIntegerPut(timer,false);
        if(timer->handler)jass_startcoroutine(level.vm,&MAKE(jassContext_t,
            .func=timer->handler,.timer=timer,.timer_generation=timer->generation,.timer_pending=true));
        jass_settimercontext(timer);
        FOR_EACH_EVENT(event)if(event->type==EVENT_GAME_TIMER_EXPIRED && event->timer==timer)
            jass_calltriggerwithtimer(level.vm,event->trigger,timer);
        jass_settimercontext(NULL);
    }
}
void G_RunTimersBeforePathOwner(wc3Clock_t const *deadline) {
    if(level.scheduled_frame)RunScalarTimers(deadline);
}
void G_RunTimers(void) {
    if(level.scheduled_frame)RunScalarTimers(NULL);
    RunIntegerTimers();
}
