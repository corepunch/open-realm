/* galaxy_trigger.h — trigger natives */

#define MAX_SC2_TRIGGERS  512
typedef struct {
    int32_t id; cstring_t func; bool mapinit, enabled, wrapper_conds[2]; int32_t exec_count;
} sc2trig_t;
static sc2trig_t sc2_trigs[MAX_SC2_TRIGGERS];
static uint32_t sc2_trig_n;
static int32_t  sc2_trig_next_id = 1;
static int32_t  sc2_trig_current;

/* Compile the no-arg wrapper Galaxy needs around `bool f(bool testConds, bool runActions)`. */
static bool sc2_prepare_trigger_wrapper(jass_t *j, cstring_t funcname, bool testConds, char *name, uint32_t name_n) {
    jass_t *root = jass_getroot(j);
    sc2trig_t *trig = NULL;
    uint32_t slot = testConds ? 1 : 0;
    snprintf(name, name_n, "__trig_%llx_%d", (unsigned long long)(uintptr_t)funcname, testConds ? 1 : 0);
    for (uint32_t i = 0; i < sc2_trig_n; i++)
        if (sc2_trigs[i].func == funcname) { trig = &sc2_trigs[i]; break; }
    if (trig && trig->wrapper_conds[slot]) return true;
    char code[512];
    snprintf(code, sizeof(code), "void %s() { %s(%s, true); }", name, funcname, testConds ? "true" : "false");
    char *buf = strdup(code);
    /* Earlier unsupported calls are logged where they occur; they must not be attributed to this wrapper parse. */
    jass_rterror_clear(root);
    bool ok = jass_dobuffer_ex(root, buf, JASS_MODE_GALAXY);
    free(buf);
    if (!ok || jass_rterror_pending(root)) {
        fprintf(stderr, "sc2_fire_trigger_func: wrapper compile error for %s: %s\n", funcname, jass_rterror_message(root));
        jass_rterror_clear(root);
        return false;
    }
    if (trig) trig->wrapper_conds[slot] = true;
    return true;
}

static void sc2_log_trigger_error(jass_t *root, cstring_t funcname) {
    if (!jass_rterror_pending(root)) return;
    fprintf(stderr, "galaxy trigger %s: runtime error: %s\n", funcname, jass_rterror_message(root));
    jass_rterror_clear(root);
}

/* Map-init and TriggerExecute. waitDone shares the active coroutine; a fresh fire starts one. */
static void sc2_fire_trigger_func(jass_t *j, cstring_t funcname, bool testConds, bool as_coroutine) {
    jass_t *root = jass_getroot(j);
    char name[128];
#ifdef SC2_DEBUG_CUTSCENE
    fprintf(stderr, "sc2_fire_trigger_func: calling %s (coroutine=%d)\n", funcname, as_coroutine);
#endif
    if (!sc2_prepare_trigger_wrapper(j, funcname, testConds, name, sizeof(name))) return;
    if (as_coroutine) jass_startcoroutinebyname(root, name);
    else if (!jass_callcoroutinebyname(root, name)) jass_callbyname(root, name, false);
    if (!as_coroutine) sc2_log_trigger_error(root, funcname);
}

/* Event callbacks run on the VM stack so Event* natives see the response for the whole call.
 * testConds stays true so an authored filter can return before the actions. */
static bool sc2_fire_named_sync(jass_t *j, cstring_t funcname) {
    jass_t *root = jass_getroot(j);
    char name[128];
    if (!sc2_prepare_trigger_wrapper(j, funcname, true, name, sizeof(name))) return false;
    jass_callbyname(root, name, false);
    sc2_log_trigger_error(root, funcname);
    return true;
}

/* Triggers are opaque handles; extract the integer ID via pointer cast. */
static int32_t sc2_trigger_id(jass_t *j, int index) {
    handle_t h = jass_checkhandle(j, index, "trigger");
    return h ? (int32_t)(uintptr_t)h : 0;
}

static uint32_t sc2_TriggerCreate(jass_t *j) {
    cstring_t name = jass_checkstring(j, 1);
    int32_t id = 0;
    if (name && sc2_trig_n < MAX_SC2_TRIGGERS) {
        id = sc2_trig_next_id++;
        /* strdup: the JASS VM may GC the string after this call returns. */
        sc2_trigs[sc2_trig_n++] = (sc2trig_t){ .id = id, .func = strdup(name), .enabled = true };
    }
    return id ? jass_pushlighthandle(j, (handle_t)(uintptr_t)id, "trigger")
              : jass_pushnullhandle(j, "trigger");
}

static sc2trig_t *sc2_trig_by_id(int32_t id) {
    for (uint32_t i = 0; i < sc2_trig_n; i++) if (sc2_trigs[i].id == id) return &sc2_trigs[i];
    return NULL;
}
static uint32_t sc2_TriggerEnable(jass_t *j) {
    sc2trig_t *t = sc2_trig_by_id(sc2_trigger_id(j, 1));
    if (t) t->enabled = jass_checkboolean(j, 2);
    return 0;
}
static uint32_t sc2_TriggerStop(jass_t *j)        { (void)j; return jass_pushnull(j); }
static uint32_t sc2_TriggerGetCurrent(jass_t *j) {
    return sc2_trig_current ? jass_pushlighthandle(j, (handle_t)(uintptr_t)sc2_trig_current, "trigger")
                            : jass_pushnullhandle(j, "trigger");
}
static uint32_t sc2_TriggerGetExecCount(jass_t *j) {
    sc2trig_t *t = sc2_trig_by_id(sc2_trigger_id(j, 1));
    return jass_pushinteger(j, t ? t->exec_count : 0);
}
static uint32_t sc2_TriggerIsEnabled(jass_t *j) {
    sc2trig_t *t = sc2_trig_by_id(sc2_trigger_id(j, 1));
    return jass_pushboolean(j, t && t->enabled);
}

static uint32_t sc2_TriggerExecute(jass_t *j) {
    int32_t   id        = sc2_trigger_id(j, 1);
    bool   testConds = jass_checkboolean(j, 2);
    bool   waitDone  = jass_checkboolean(j, 3);
    for (uint32_t i = 0; i < sc2_trig_n; i++) {
        if (sc2_trigs[i].id == id && sc2_trigs[i].func) {
#ifdef SC2_DEBUG_CUTSCENE
            fprintf(stderr, "TriggerExecute: %s testConds=%d waitDone=%d\n",
                    sc2_trigs[i].func, testConds, waitDone);
#endif
            sc2_fire_trigger_func(j, sc2_trigs[i].func, testConds, !waitDone);
            break;
        }
    }
    return jass_pushnull(j);
}

static uint32_t sc2_TriggerAddEventMapInit(jass_t *j) {
    int32_t id = sc2_trigger_id(j, 1);
    for (uint32_t i = 0; i < sc2_trig_n; i++) {
        if (sc2_trigs[i].id == id) { sc2_trigs[i].mapinit = true; break; }
    }
    return jass_pushnull(j);
}

static uint32_t sc2_TriggerSkippableBegin(jass_t *j) { (void)j; return jass_pushnull(j); }
static uint32_t sc2_TriggerSkippableEnd(jass_t *j)   { (void)j; return jass_pushnull(j); }
static uint32_t sc2_TriggerQueueEnter(jass_t *j)     { (void)j; return jass_pushnull(j); }
static uint32_t sc2_TriggerQueueExit(jass_t *j)      { (void)j; return jass_pushnull(j); }
static uint32_t sc2_TriggerQueueIsEmpty(jass_t *j)   { return jass_pushboolean(j, true); }
static uint32_t sc2_TriggerQueuePause(jass_t *j)     { (void)j; return jass_pushnull(j); }
static uint32_t sc2_TriggerQueueClear(jass_t *j)     { (void)j; return jass_pushnull(j); }

static uint32_t sc2_Wait(jass_t *j) {
    float secs = jass_checknumber(j, 1);
    jass_sleep(j, (uint32_t)(secs * 1000.0f));
    return 0;
}

/* Registrations mirror WC3 gevent_s. The response stack mirrors the gameEvent_t fields copied into
 * jass context: EventUnit is the subject; damage, source, point, region, timer, and player are the payload.
 * A null unitref matches any unit. Player -1 is c_playerAny. A nested publish pushes and pops. */
#define MAX_SC2_EVENTS 1024 // registrations; campaign library event rows stay under this
#define SC2_EV_RESP_STACK 8 // frames; nested Event* responses restored after the inner publish returns
#define SC2_EV_TEXT 64 // chars; filter text copied from a script argument
#define SC2_EV_RESP_TEXT 96 // chars; chat or effect text stored on the active response
#define SC2_EVENT_TICK_MS FRAMETIME // ms; one galaxy_tick; same length as a server frame
#define SC2_EF_STATE 1u // registration bit; enter, load, select, idle, key-down, or exact chat
#define SC2_EF_FIRED 2u // one-shot elapsed registration has already run
#define SC2_EF_SHARED 4u // ability registration includes shared abilities; read by a future combat producer
#define SC2_EV_ANY_PLAYER (-1) // player id; natives.galaxy c_playerAny matches every player
#define MAX_SC2_TIMERS 128 // handles; map-script Galaxy timers

enum {
    SC2_EV_DIED = 1, SC2_EV_REMOVED, SC2_EV_CREATED, SC2_EV_REVIVE, SC2_EV_ATTACKED, SC2_EV_DAMAGED,
    SC2_EV_ACQUIRED, SC2_EV_STARTED, SC2_EV_ATTR, SC2_EV_XP, SC2_EV_LEVEL, SC2_EV_IDLE, SC2_EV_INVENTORY,
    SC2_EV_PROP, SC2_EV_CARGO, SC2_EV_ORDER, SC2_EV_ABILITY, SC2_EV_REGION, SC2_EV_RANGE, SC2_EV_RANGE_PT,
    SC2_EV_SELECT, SC2_EV_CLICK, SC2_EV_HIGHLIGHT, SC2_EV_CONSTRUCT, SC2_EV_TRAIN, SC2_EV_RESEARCH,
    SC2_EV_MAGAZINE, SC2_EV_SPECIALIZE, SC2_EV_POWERUP, SC2_EV_ELAPSED, SC2_EV_PERIODIC, SC2_EV_TIMER,
    SC2_EV_ALLIANCE, SC2_EV_LEFT, SC2_EV_PLAYER_PROP, SC2_EV_AI_WAVE, SC2_EV_CHAT, SC2_EV_CHEAT,
    SC2_EV_DIALOG, SC2_EV_SAVE, SC2_EV_SAVE_DONE, SC2_EV_ABORT, SC2_EV_CUSTOM_DIALOG, SC2_EV_MENU,
    SC2_EV_MOUSE, SC2_EV_KEY, SC2_EV_BUTTON, SC2_EV_CREDITS, SC2_EV_PURCHASE, SC2_EV_PURCHASE_EXIT,
    SC2_EV_PURCHASE_ITEM, SC2_EV_PURCHASE_CAT
};

typedef struct {
    uint8_t type, flags;
    int32_t trig, unit, other, player, ival, ival2;
    int16_t mod_s, mod_c, mod_a;
    uint32_t start_ms;
    float fval;
    char text[SC2_EV_TEXT], text2[SC2_EV_TEXT];
    bool inuse;
} sc2evreg_t;

typedef struct {
    uint8_t type;
    int32_t unit, target, cargo, source, created, item, progress, powerup;
    int32_t player, source_player, ival, ival2, ival3;
    int32_t region, order, abil, timer;
    int16_t mod_s, mod_c, mod_a;
    float amount, x, y, z;
    bool has_point, death, dmg, down, shift, ctrl, alt;
    char text[SC2_EV_RESP_TEXT], text2[SC2_EV_TEXT], text3[SC2_EV_TEXT];
} sc2evresp_t;

typedef struct {
    uint32_t duration_ms, elapsed_ms;
    uint8_t kind;
    bool inuse, periodic, paused, running, infinite;
} sc2timer_t;

static sc2evreg_t sc2_evregs[MAX_SC2_EVENTS];
static sc2evresp_t sc2_evstack[SC2_EV_RESP_STACK];
static int sc2_evdepth;
static char sc2_ev_matched[SC2_EV_RESP_TEXT];
static uint32_t sc2_clock_ms[3]; /* game, real, AI; ms since galaxy_reset; real shares the server tick */
static bool sc2_ai_paused;
static sc2timer_t sc2_timers[MAX_SC2_TIMERS];
static int32_t sc2_timer_last;

static sc2evresp_t const *sc2_ev_now(void) { return sc2_evdepth ? &sc2_evstack[sc2_evdepth - 1] : NULL; }
static int32_t sc2_href(jass_t *j, int arg, cstring_t type) {
    return (int32_t)(uintptr_t)jass_checkhandle(j, arg, type);
}
static int32_t sc2_ev_owner(int32_t h) {
    void *ent = h > 0 && h <= (int32_t)sc2_gunit_n ? sc2_gunits[h - 1] : NULL;
    return ent && sc2_galaxy_unit_owner ? sc2_galaxy_unit_owner(ent) : SC2_EV_ANY_PLAYER;
}
static int32_t sc2_ev_player(jass_t *j, int arg) {
    int32_t p = jass_checkinteger(j, arg);
    if (p != SC2_EV_ANY_PLAYER && (p < 0 || p >= 32)) {
        jass_rterror(j, "Player index outside [0,31]"); return SC2_EV_ANY_PLAYER;
    }
    return p;
}
static int32_t sc2_ev_time_type(jass_t *j, int arg) {
    int32_t t = jass_checkinteger(j, arg);
    if (t < 0 || t > 2) { jass_rterror(j, "Galaxy time type outside [0,2]"); return 0; }
    return t;
}
static uint32_t sc2_ev_ms(jass_t *j, float seconds) {
    long ms = lroundf(seconds * 1000.0f);
    if (ms <= 0 || ms > 86400000L) { jass_rterror(j, "Galaxy time interval is outside 1 ms..24 h"); return 0; }
    return (uint32_t)ms;
}
static int16_t sc2_ev_mod(jass_t *j, int arg) {
    int32_t v = jass_checkinteger(j, arg);
    if (v < -32768 || v > 32767) { jass_rterror(j, "Galaxy key modifier is out of range"); return 0; }
    return (int16_t)v;
}
static void sc2_ev_str(jass_t *j, char *dst, int arg) {
    cstring_t s = jass_checkstring(j, arg);
    uint32_t n = s ? (uint32_t)strlen(s) : 0;
    if (n >= SC2_EV_TEXT) { jass_rterror(j, "Galaxy event string exceeds 63 characters"); return; }
    if (n) memcpy(dst, s, n);
    dst[n] = 0;
}
static void sc2_ev_flag(sc2evreg_t *r, jass_t *j, int arg) { if (jass_checkboolean(j, arg)) r->flags |= SC2_EF_STATE; }
static sc2evreg_t sc2_ev_new(jass_t *j, uint8_t type) {
    return (sc2evreg_t){ .type = type, .trig = sc2_trigger_id(j, 1), .player = SC2_EV_ANY_PLAYER };
}
static void sc2_ev_add(jass_t *j, sc2evreg_t const *reg) {
    if (jass_rterror_pending(j)) return;
    if (!reg->trig) { jass_rterror(j, "Galaxy event needs a trigger"); return; }
    for (uint32_t i = 0; i < MAX_SC2_EVENTS; i++) if (!sc2_evregs[i].inuse) {
        sc2_evregs[i] = *reg; sc2_evregs[i].inuse = true; return;
    }
    jass_rterror(j, "Galaxy event table full");
}
static bool sc2_ev_state(sc2evreg_t const *r, sc2evresp_t const *e) {
    return ((r->flags & SC2_EF_STATE) != 0) == (e->ival != 0);
}
static bool sc2_ev_match(sc2evreg_t const *r, sc2evresp_t const *e) {
    if (!r->inuse || r->type != e->type) return false;
    if (r->unit && r->unit != e->unit) return false;
    if (r->player != SC2_EV_ANY_PLAYER && r->player != e->player) return false;
    switch (r->type) {
    case SC2_EV_DAMAGED:
        if (r->ival != -1 && r->ival != e->ival) return false; /* -1 is c_unitDamageTypeAny */
        if (r->ival2 == 1 && !e->death) return false;
        if (r->ival2 == 2 && e->death) return false;
        return !r->text[0] || !strcmp(r->text, e->text);
    case SC2_EV_CREATED:
        if (r->text[0] && strcmp(r->text, e->text)) return false;
        return !r->text2[0] || !strcmp(r->text2, e->text2);
    case SC2_EV_PROP: case SC2_EV_PLAYER_PROP: case SC2_EV_LEFT: case SC2_EV_CHEAT:
    case SC2_EV_CONSTRUCT: case SC2_EV_TRAIN: case SC2_EV_RESEARCH: case SC2_EV_MAGAZINE: case SC2_EV_SPECIALIZE:
    case SC2_EV_PURCHASE: case SC2_EV_PURCHASE_ITEM: case SC2_EV_PURCHASE_CAT:
    case SC2_EV_MENU: case SC2_EV_CUSTOM_DIALOG:
        return r->ival == e->ival;
    case SC2_EV_ORDER: return !r->other || r->other == e->abil;
    case SC2_EV_ABILITY:
        if (r->other && r->other != e->abil) return false;
        return r->ival == -1 || r->ival == e->ival; /* -1 is c_unitAbilStageAll */
    case SC2_EV_INVENTORY:
        if (r->ival != e->ival) return false;
        return !r->other || r->other == e->item;
    case SC2_EV_CARGO: case SC2_EV_IDLE: case SC2_EV_SELECT: case SC2_EV_HIGHLIGHT: return sc2_ev_state(r, e);
    case SC2_EV_DIALOG:
        if (r->other != -1 && r->other != e->ival2) return false; /* -1 is c_dialogControlAny */
        return r->ival == -1 || r->ival == e->ival;
    case SC2_EV_CHAT:
        if (!r->text[0]) return true;
        return (r->flags & SC2_EF_STATE) ? !strcmp(r->text, e->text) : strstr(e->text, r->text) != NULL;
    case SC2_EV_KEY:
        if (r->ival != e->ival || ((r->flags & SC2_EF_STATE) != 0) != e->down) return false;
        return r->mod_s == e->mod_s && r->mod_c == e->mod_c && r->mod_a == e->mod_a;
    case SC2_EV_MOUSE: return r->ival == e->ival && ((r->flags & SC2_EF_STATE) != 0) == e->down;
    default: return true;
    }
}
static void sc2_ev_fire_reg(jass_t *j, sc2evreg_t const *r, sc2evresp_t e) {
    sc2trig_t *t = sc2_trig_by_id(r->trig);
    char matched[SC2_EV_RESP_TEXT];
    int32_t saved;
    if (!t || !t->enabled || !t->func) return;
    if (sc2_evdepth >= SC2_EV_RESP_STACK) { jass_rterror(j, "Galaxy event response stack full"); return; }
    e.type = r->type;
    saved = sc2_trig_current;
    snprintf(matched, sizeof(matched), "%s", sc2_ev_matched);
    snprintf(sc2_ev_matched, sizeof(sc2_ev_matched), "%s", r->text);
    sc2_evstack[sc2_evdepth++] = e;
    sc2_trig_current = t->id;
    t->exec_count++;
    sc2_fire_named_sync(j, t->func);
    sc2_trig_current = saved;
    snprintf(sc2_ev_matched, sizeof(sc2_ev_matched), "%s", matched);
    sc2_evdepth--;
}
static void sc2_ev_emit(jass_t *j, sc2evresp_t e) {
    for (uint32_t i = 0; i < MAX_SC2_EVENTS; i++)
        if (sc2_ev_match(&sc2_evregs[i], &e)) sc2_ev_fire_reg(j, &sc2_evregs[i], e);
}

static sc2timer_t *sc2_timer(jass_t *j, int arg) {
    int32_t h = sc2_href(j, arg, "timer");
    /* InitLibs never creates libNtve_gv__GameTimer. Pausing that null must not abort the intro. */
    if (!h) return NULL;
    if (h < 0 || h >= MAX_SC2_TIMERS || !sc2_timers[h].inuse) { jass_rterror(j, "Invalid Galaxy timer"); return NULL; }
    return &sc2_timers[h];
}
static uint32_t sc2_TimerCreate(jass_t *j) {
    for (int32_t i = 1; i < MAX_SC2_TIMERS; i++) if (!sc2_timers[i].inuse) {
        sc2_timers[i] = (sc2timer_t){ .inuse = true };
        return jass_pushlighthandle(j, (handle_t)(uintptr_t)i, "timer");
    }
    jass_rterror(j, "Galaxy timer table full");
    return 0;
}
static uint32_t sc2_TimerStart(jass_t *j) {
    sc2timer_t *t = sc2_timer(j, 1);
    float duration = jass_checknumber(j, 2);
    bool periodic = jass_checkboolean(j, 3);
    int32_t kind = sc2_ev_time_type(j, 4);
    uint32_t ms;
    if (!t || jass_rterror_pending(j)) return 0;
    sc2_timer_last = (int32_t)(t - sc2_timers);
    t->kind = (uint8_t)kind; t->periodic = periodic; t->paused = false; t->elapsed_ms = 0;
    if (duration == -1.0f) { t->infinite = true; t->running = false; return 0; } /* c_timerDurationInfinite */
    ms = sc2_ev_ms(j, duration);
    if (!ms) return 0;
    t->infinite = false; t->duration_ms = ms; t->running = true;
    return 0;
}
static uint32_t sc2_TimerLastStarted(jass_t *j) {
    return sc2_timer_last && sc2_timers[sc2_timer_last].inuse
        ? jass_pushlighthandle(j, (handle_t)(uintptr_t)sc2_timer_last, "timer") : jass_pushnullhandle(j, "timer");
}
static uint32_t sc2_TimerPause(jass_t *j) {
    sc2timer_t *t = sc2_timer(j, 1);
    if (!t) {
        if (!jass_rterror_pending(j)) {
            static bool logged;
            if (!logged) fprintf(stderr, "SC2 galaxy: TimerPause skipped, timer handle is null\n");
            logged = true;
        }
        return 0;
    }
    t->paused = jass_checkboolean(j, 2);
    return 0;
}

/* Game and real clocks share the server frame. AITimePause freezes only the AI clock. */
static void sc2_ev_tick(jass_t *j) {
    sc2_clock_ms[0] += SC2_EVENT_TICK_MS; sc2_clock_ms[1] += SC2_EVENT_TICK_MS;
    if (!sc2_ai_paused) sc2_clock_ms[2] += SC2_EVENT_TICK_MS;
    for (int32_t i = 1; i < MAX_SC2_TIMERS; i++) {
        sc2timer_t *t = &sc2_timers[i];
        int guard = 0;
        if (!t->inuse || !t->running || t->paused || t->infinite || (t->kind == 2 && sc2_ai_paused)) continue;
        t->elapsed_ms += SC2_EVENT_TICK_MS;
        while (t->running && t->elapsed_ms >= t->duration_ms && guard < 4) {
            sc2evresp_t e = { .type = SC2_EV_TIMER, .timer = i, .ival = t->kind };
            if (t->periodic) t->elapsed_ms -= t->duration_ms; else t->running = false;
            for (uint32_t n = 0; n < MAX_SC2_EVENTS; n++)
                if (sc2_evregs[n].inuse && sc2_evregs[n].type == SC2_EV_TIMER && sc2_evregs[n].other == i)
                    sc2_ev_fire_reg(j, &sc2_evregs[n], e);
            guard++;
        }
    }
    for (uint32_t n = 0; n < MAX_SC2_EVENTS; n++) {
        sc2evreg_t *r = &sc2_evregs[n];
        uint32_t need, guard = 0;
        sc2evresp_t e;
        if (!r->inuse || r->ival < 0 || r->ival > 2) continue;
        if (r->type != SC2_EV_ELAPSED && r->type != SC2_EV_PERIODIC) continue;
        need = (uint32_t)lroundf(r->fval * 1000.0f);
        if (!need) continue;
        e = (sc2evresp_t){ .type = r->type, .ival = r->ival };
        if (r->type == SC2_EV_ELAPSED) {
            if ((r->flags & SC2_EF_FIRED) || sc2_clock_ms[r->ival] - r->start_ms < need) continue;
            r->flags |= SC2_EF_FIRED; sc2_ev_fire_reg(j, r, e); continue;
        }
        while (sc2_clock_ms[r->ival] - r->start_ms >= need && guard < 4) {
            r->start_ms += need; guard++; sc2_ev_fire_reg(j, r, e);
        }
    }
}

static uint32_t sc2_ev_on(jass_t *j, uint8_t type) { sc2evreg_t r = sc2_ev_new(j, type); sc2_ev_add(j, &r); return 0; }
static uint32_t sc2_ev_on_unit(jass_t *j, uint8_t type) {
    sc2evreg_t r = sc2_ev_new(j, type); r.unit = sc2_href(j, 2, "unitref"); sc2_ev_add(j, &r); return 0;
}
static uint32_t sc2_ev_on_state(jass_t *j, uint8_t type) {
    sc2evreg_t r = sc2_ev_new(j, type);
    r.unit = sc2_href(j, 2, "unitref");
    sc2_ev_flag(&r, j, 3);
    sc2_ev_add(j, &r);
    return 0;
}
static uint32_t sc2_ev_on_unit_int(jass_t *j, uint8_t type) {
    sc2evreg_t r = sc2_ev_new(j, type); r.unit = sc2_href(j, 2, "unitref"); r.ival = jass_checkinteger(j, 3);
    sc2_ev_add(j, &r); return 0;
}
static uint32_t sc2_ev_on_player(jass_t *j, uint8_t type) {
    sc2evreg_t r = sc2_ev_new(j, type); r.player = sc2_ev_player(j, 2); sc2_ev_add(j, &r); return 0;
}
static uint32_t sc2_ev_on_player_int(jass_t *j, uint8_t type) {
    sc2evreg_t r = sc2_ev_new(j, type); r.player = sc2_ev_player(j, 2); r.ival = jass_checkinteger(j, 3);
    sc2_ev_add(j, &r); return 0;
}

static uint32_t sc2_TriggerAddEventUnitDied(jass_t *j) { return sc2_ev_on_unit(j, SC2_EV_DIED); }
static uint32_t sc2_TriggerAddEventUnitRemoved(jass_t *j) { return sc2_ev_on_unit(j, SC2_EV_REMOVED); }
static uint32_t sc2_TriggerAddEventUnitRevive(jass_t *j) { return sc2_ev_on_unit(j, SC2_EV_REVIVE); }
static uint32_t sc2_TriggerAddEventUnitAttacked(jass_t *j) { return sc2_ev_on_unit(j, SC2_EV_ATTACKED); }
static uint32_t sc2_TriggerAddEventUnitAcquiredTarget(jass_t *j) { return sc2_ev_on_unit(j, SC2_EV_ACQUIRED); }
static uint32_t sc2_TriggerAddEventUnitStartedAttack(jass_t *j) { return sc2_ev_on_unit(j, SC2_EV_STARTED); }
static uint32_t sc2_TriggerAddEventUnitAttributeChange(jass_t *j) { return sc2_ev_on_unit(j, SC2_EV_ATTR); }
static uint32_t sc2_TriggerAddEventUnitGainExperience(jass_t *j) { return sc2_ev_on_unit(j, SC2_EV_XP); }
static uint32_t sc2_TriggerAddEventUnitGainLevel(jass_t *j) { return sc2_ev_on_unit(j, SC2_EV_LEVEL); }
static uint32_t sc2_TriggerAddEventUnitPowerup(jass_t *j) { return sc2_ev_on_unit(j, SC2_EV_POWERUP); }
static uint32_t sc2_TriggerAddEventUnitBecomesIdle(jass_t *j) { return sc2_ev_on_state(j, SC2_EV_IDLE); }
static uint32_t sc2_TriggerAddEventUnitCargo(jass_t *j) { return sc2_ev_on_state(j, SC2_EV_CARGO); }
static uint32_t sc2_TriggerAddEventUnitProperty(jass_t *j) { return sc2_ev_on_unit_int(j, SC2_EV_PROP); }
static uint32_t sc2_TriggerAddEventUnitConstructProgress(jass_t *j) { return sc2_ev_on_unit_int(j, SC2_EV_CONSTRUCT); }
static uint32_t sc2_TriggerAddEventUnitTrainProgress(jass_t *j) { return sc2_ev_on_unit_int(j, SC2_EV_TRAIN); }
static uint32_t sc2_TriggerAddEventUnitResearchProgress(jass_t *j) { return sc2_ev_on_unit_int(j, SC2_EV_RESEARCH); }
static uint32_t sc2_TriggerAddEventUnitArmMagazineProgress(jass_t *j) { return sc2_ev_on_unit_int(j, SC2_EV_MAGAZINE); }
static uint32_t sc2_TriggerAddEventUnitSpecializeProgress(jass_t *j) {
    return sc2_ev_on_unit_int(j, SC2_EV_SPECIALIZE);
}
static uint32_t sc2_TriggerAddEventPlayerAllianceChange(jass_t *j) { return sc2_ev_on_player(j, SC2_EV_ALLIANCE); }
static uint32_t sc2_TriggerAddEventPlayerAIWave(jass_t *j) { return sc2_ev_on_player(j, SC2_EV_AI_WAVE); }
static uint32_t sc2_TriggerAddEventAbortMission(jass_t *j) { return sc2_ev_on_player(j, SC2_EV_ABORT); }
static uint32_t sc2_TriggerAddEventGameCreditsFinished(jass_t *j) { return sc2_ev_on_player(j, SC2_EV_CREDITS); }
static uint32_t sc2_TriggerAddEventPurchaseExit(jass_t *j) { return sc2_ev_on_player(j, SC2_EV_PURCHASE_EXIT); }
static uint32_t sc2_TriggerAddEventSaveGame(jass_t *j) { return sc2_ev_on(j, SC2_EV_SAVE); }
static uint32_t sc2_TriggerAddEventSaveGameDone(jass_t *j) { return sc2_ev_on(j, SC2_EV_SAVE_DONE); }
static uint32_t sc2_TriggerAddEventPlayerLeft(jass_t *j) { return sc2_ev_on_player_int(j, SC2_EV_LEFT); }
static uint32_t sc2_TriggerAddEventPlayerPropChange(jass_t *j) { return sc2_ev_on_player_int(j, SC2_EV_PLAYER_PROP); }
static uint32_t sc2_TriggerAddEventCheatUsed(jass_t *j) { return sc2_ev_on_player_int(j, SC2_EV_CHEAT); }
static uint32_t sc2_TriggerAddEventGameMenuItemSelected(jass_t *j) { return sc2_ev_on_player_int(j, SC2_EV_MENU); }
static uint32_t sc2_TriggerAddEventCustomDialogDismissed(jass_t *j) {
    return sc2_ev_on_player_int(j, SC2_EV_CUSTOM_DIALOG);
}
static uint32_t sc2_TriggerAddEventPurchaseMade(jass_t *j) { return sc2_ev_on_player_int(j, SC2_EV_PURCHASE); }
static uint32_t sc2_TriggerAddEventSelectedPurchaseItemChanged(jass_t *j) {
    return sc2_ev_on_player_int(j, SC2_EV_PURCHASE_ITEM);
}
static uint32_t sc2_TriggerAddEventSelectedPurchaseCategoryChanged(jass_t *j) {
    return sc2_ev_on_player_int(j, SC2_EV_PURCHASE_CAT);
}

static uint32_t sc2_TriggerAddEventUnitCreated(jass_t *j) {
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_CREATED);
    r.unit = sc2_href(j, 2, "unitref");
    sc2_ev_str(j, r.text, 3);
    sc2_ev_str(j, r.text2, 4);
    sc2_ev_add(j, &r);
    return 0;
}
static uint32_t sc2_TriggerAddEventUnitDamaged(jass_t *j) {
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_DAMAGED);
    r.unit = sc2_href(j, 2, "unitref"); r.ival = jass_checkinteger(j, 3); r.ival2 = jass_checkinteger(j, 4);
    sc2_ev_str(j, r.text, 5); sc2_ev_add(j, &r); return 0;
}
static uint32_t sc2_TriggerAddEventUnitInventoryChange(jass_t *j) {
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_INVENTORY);
    r.unit = sc2_href(j, 2, "unitref"); r.ival = jass_checkinteger(j, 3); r.other = sc2_href(j, 4, "unitref");
    sc2_ev_add(j, &r); return 0;
}
/* jass.h owns these base-type indexes; null abilcmd arrives as a null handle. */
enum { SC2_JASS_INTEGER = 0, SC2_JASS_HANDLE = 5 };
int jass_gettype(jass_t *j, int index);
/* null abilcmd is the any-order filter used by gt_UnitMovementCheck. */
static int32_t sc2_ev_abil(jass_t *j, int arg) {
    int type = jass_gettype(j, arg);
    if (type == SC2_JASS_INTEGER) return jass_checkinteger(j, arg);
    if (type == SC2_JASS_HANDLE && !jass_checkhandle(j, arg, "abilcmd")) return 0;
    jass_rterror(j, "Galaxy ability command is not an integer");
    return 0;
}
static uint32_t sc2_TriggerAddEventUnitOrder(jass_t *j) {
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_ORDER);
    r.unit = sc2_href(j, 2, "unitref"); r.other = sc2_ev_abil(j, 3); sc2_ev_add(j, &r); return 0;
}
static uint32_t sc2_TriggerAddEventUnitAbility(jass_t *j) {
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_ABILITY);
    r.unit = sc2_href(j, 2, "unitref"); r.other = sc2_ev_abil(j, 3); r.ival = jass_checkinteger(j, 4);
    if (jass_checkboolean(j, 5)) r.flags |= SC2_EF_SHARED;
    sc2_ev_add(j, &r); return 0;
}
static uint32_t sc2_TriggerAddEventUnitRegion(jass_t *j) {
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_REGION);
    r.unit = sc2_href(j, 2, "unitref"); r.other = sc2_href(j, 3, "region"); sc2_ev_flag(&r, j, 4);
    /* RegionFromId is still unresolved. A script error here aborts InitTriggers before the intro. */
    if (!r.other) {
        fprintf(stderr, "SC2 galaxy: TriggerAddEventUnitRegion skipped, region handle is null\n");
        return 0;
    }
    sc2_ev_add(j, &r); return 0;
}
static uint32_t sc2_TriggerAddEventUnitRange(jass_t *j) {
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_RANGE);
    r.unit = sc2_href(j, 2, "unitref");
    r.other = sc2_href(j, 3, "unit");
    r.fval = jass_checknumber(j, 4);
    sc2_ev_flag(&r, j, 5);
    if (!(r.fval >= 0.0f)) jass_rterror(j, "Galaxy range event needs a non-negative distance");
    sc2_ev_add(j, &r); return 0;
}
static uint32_t sc2_TriggerAddEventUnitRangePoint(jass_t *j) {
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_RANGE_PT);
    r.unit = sc2_href(j, 2, "unitref");
    r.other = sc2_href(j, 3, "point");
    r.fval = jass_checknumber(j, 4);
    sc2_ev_flag(&r, j, 5);
    if (!(r.fval >= 0.0f)) { jass_rterror(j, "Galaxy point-range distance must be non-negative"); return 0; }
    if (!r.other) {
        fprintf(stderr, "SC2 galaxy: TriggerAddEventUnitRangePoint skipped, point handle is null\n");
        return 0;
    }
    sc2_ev_add(j, &r); return 0;
}
static uint32_t sc2_TriggerAddEventUnitSelected(jass_t *j) {
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_SELECT);
    r.unit = sc2_href(j, 2, "unitref");
    r.player = sc2_ev_player(j, 3);
    sc2_ev_flag(&r, j, 4);
    sc2_ev_add(j, &r);
    return 0;
}
static uint32_t sc2_TriggerAddEventUnitHighlight(jass_t *j) {
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_HIGHLIGHT);
    r.unit = sc2_href(j, 2, "unitref");
    r.player = sc2_ev_player(j, 3);
    sc2_ev_flag(&r, j, 4);
    sc2_ev_add(j, &r);
    return 0;
}
static uint32_t sc2_TriggerAddEventUnitClick(jass_t *j) {
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_CLICK);
    r.unit = sc2_href(j, 2, "unitref"); r.player = sc2_ev_player(j, 3); sc2_ev_add(j, &r); return 0;
}
static uint32_t sc2_TriggerAddEventTimeElapsed(jass_t *j) {
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_ELAPSED);
    r.fval = jass_checknumber(j, 2); r.ival = sc2_ev_time_type(j, 3);
    if (!sc2_ev_ms(j, r.fval) || jass_rterror_pending(j)) return 0;
    r.start_ms = sc2_clock_ms[r.ival]; sc2_ev_add(j, &r); return 0;
}
static uint32_t sc2_TriggerAddEventTimePeriodic(jass_t *j) {
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_PERIODIC);
    r.fval = jass_checknumber(j, 2); r.ival = sc2_ev_time_type(j, 3);
    if (!sc2_ev_ms(j, r.fval) || jass_rterror_pending(j)) return 0;
    r.start_ms = sc2_clock_ms[r.ival]; sc2_ev_add(j, &r); return 0;
}
static uint32_t sc2_TriggerAddEventTimer(jass_t *j) {
    sc2timer_t *t = sc2_timer(j, 2);
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_TIMER);
    if (!t) return 0;
    r.other = (int32_t)(t - sc2_timers); sc2_ev_add(j, &r); return 0;
}
static uint32_t sc2_TriggerAddEventChatMessage(jass_t *j) {
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_CHAT);
    r.player = sc2_ev_player(j, 2); sc2_ev_str(j, r.text, 3); sc2_ev_flag(&r, j, 4); sc2_ev_add(j, &r); return 0;
}
static uint32_t sc2_TriggerAddEventDialogControl(jass_t *j) {
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_DIALOG);
    r.player = sc2_ev_player(j, 2); r.other = jass_checkinteger(j, 3); r.ival = jass_checkinteger(j, 4);
    sc2_ev_add(j, &r); return 0;
}
static uint32_t sc2_TriggerAddEventMouseClicked(jass_t *j) {
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_MOUSE);
    r.player = sc2_ev_player(j, 2);
    r.ival = jass_checkinteger(j, 3);
    sc2_ev_flag(&r, j, 4);
    sc2_ev_add(j, &r);
    return 0;
}
static uint32_t sc2_TriggerAddEventKeyPressed(jass_t *j) {
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_KEY);
    r.player = sc2_ev_player(j, 2); r.ival = jass_checkinteger(j, 3); sc2_ev_flag(&r, j, 4);
    r.mod_s = sc2_ev_mod(j, 5); r.mod_c = sc2_ev_mod(j, 6); r.mod_a = sc2_ev_mod(j, 7); sc2_ev_add(j, &r); return 0;
}
static uint32_t sc2_TriggerAddEventButtonPressed(jass_t *j) {
    sc2evreg_t r = sc2_ev_new(j, SC2_EV_BUTTON);
    r.player = sc2_ev_player(j, 2); sc2_ev_str(j, r.text, 3); sc2_ev_add(j, &r); return 0;
}

static uint32_t sc2_ev_h(jass_t *j, int32_t h, cstring_t type) {
    return h ? jass_pushlighthandle(j, (handle_t)(uintptr_t)h, type) : jass_pushnullhandle(j, type);
}
static uint32_t sc2_EventUnit(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return sc2_ev_h(j, e ? e->unit : 0, "unit");
}
static uint32_t sc2_EventUnitTarget(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return sc2_ev_h(j, e ? e->target : 0, "unit");
}
static uint32_t sc2_EventUnitTargetUnit(jass_t *j) { return sc2_EventUnitTarget(j); }
static uint32_t sc2_EventUnitCargo(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return sc2_ev_h(j, e ? e->cargo : 0, "unit");
}
static uint32_t sc2_EventUnitCreatedUnit(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return sc2_ev_h(j, e ? e->created : 0, "unit");
}
static uint32_t sc2_EventUnitDamageSourceUnit(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return sc2_ev_h(j, e ? e->source : 0, "unit");
}
static uint32_t sc2_EventUnitInventoryItem(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return sc2_ev_h(j, e ? e->item : 0, "unit");
}
static uint32_t sc2_EventUnitInventoryItemTargetUnit(jass_t *j) { return sc2_EventUnitTarget(j); }
static uint32_t sc2_EventUnitProgressUnit(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return sc2_ev_h(j, e ? e->progress : 0, "unit");
}
static uint32_t sc2_EventUnitPowerupUnit(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return sc2_ev_h(j, e ? e->powerup : 0, "unit");
}
static uint32_t sc2_EventUnitOrder(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return sc2_ev_h(j, e ? e->order : 0, "order");
}
static uint32_t sc2_EventUnitRegion(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return sc2_ev_h(j, e ? e->region : 0, "region");
}
static uint32_t sc2_EventTimer(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return sc2_ev_h(j, e ? e->timer : 0, "timer");
}
static uint32_t sc2_EventUnitAbility(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return jass_pushinteger(j, e ? e->abil : 0);
}
static uint32_t sc2_EventUnitAbilityStage(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return jass_pushinteger(j, e ? e->ival : 0);
}
static uint32_t sc2_EventUnitAttributePoints(jass_t *j) { return sc2_EventUnitAbilityStage(j); }
static uint32_t sc2_EventUnitInventoryItemContainer(jass_t *j) { return sc2_EventUnitAbilityStage(j); }
static uint32_t sc2_EventUnitInventoryItemSlot(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return jass_pushinteger(j, e ? e->ival2 : 0);
}
static uint32_t sc2_EventPlayer(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return jass_pushinteger(j, e ? e->player : 0);
}
static uint32_t sc2_EventPlayerProperty(jass_t *j) { return sc2_EventUnitAbilityStage(j); }
static uint32_t sc2_EventUnitDamageSourcePlayer(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now(); return jass_pushinteger(j, e ? e->source_player : 0);
}
static uint32_t sc2_EventCheatUsed(jass_t *j) { return sc2_EventUnitAbilityStage(j); }
static uint32_t sc2_EventDialogControl(jass_t *j) { return sc2_EventUnitInventoryItemSlot(j); }
static uint32_t sc2_EventDialogControlEventType(jass_t *j) { return sc2_EventUnitAbilityStage(j); }
static uint32_t sc2_EventCustomDialogResult(jass_t *j) { return sc2_EventUnitAbilityStage(j); }
static uint32_t sc2_EventGameMenuItemSelected(jass_t *j) { return sc2_EventUnitAbilityStage(j); }
static uint32_t sc2_EventPurchaseMade(jass_t *j) { return sc2_EventUnitAbilityStage(j); }
static uint32_t sc2_EventMouseClickedButton(jass_t *j) { return sc2_EventUnitAbilityStage(j); }
static uint32_t sc2_EventMouseClickedPosXUI(jass_t *j) { return sc2_EventUnitInventoryItemSlot(j); }
static uint32_t sc2_EventMouseClickedPosYUI(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return jass_pushinteger(j, e ? e->ival3 : 0);
}
static uint32_t sc2_EventKeyPressed(jass_t *j) { return sc2_EventUnitAbilityStage(j); }
static uint32_t sc2_EventUnitDamageAmount(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now(); return jass_pushnumber(j, e && e->dmg ? e->amount : 0);
}
static uint32_t sc2_EventUnitXPDelta(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now(); return jass_pushnumber(j, e && e->type == SC2_EV_XP ? e->amount : 0);
}
static uint32_t sc2_EventMouseClickedPosXWorld(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now(); return jass_pushnumber(j, e && e->type == SC2_EV_MOUSE ? e->x : 0);
}
static uint32_t sc2_EventMouseClickedPosYWorld(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now(); return jass_pushnumber(j, e && e->type == SC2_EV_MOUSE ? e->y : 0);
}
static uint32_t sc2_EventMouseClickedPosZWorld(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now(); return jass_pushnumber(j, e && e->type == SC2_EV_MOUSE ? e->z : 0);
}
static uint32_t sc2_EventUnitDamageDeathCheck(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    int32_t kind = jass_checkinteger(j, 1);
    if (!e || !e->dmg) return jass_pushboolean(j, false);
    if (kind == 0) return jass_pushboolean(j, true);
    if (kind == 1) return jass_pushboolean(j, e->death);
    if (kind == 2) return jass_pushboolean(j, !e->death);
    jass_rterror(j, "Damage death check is not 0, 1, or 2");
    return 0;
}
static uint32_t sc2_EventKeyShift(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return jass_pushboolean(j, e && e->shift);
}
static uint32_t sc2_EventKeyControl(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return jass_pushboolean(j, e && e->ctrl);
}
static uint32_t sc2_EventKeyAlt(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return jass_pushboolean(j, e && e->alt);
}
static uint32_t sc2_ev_text_field(jass_t *j, bool ok, cstring_t text) {
    return jass_pushstring(j, ok && text ? text : "");
}
static uint32_t sc2_EventUnitCreatedAbil(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now(); return sc2_ev_text_field(j, e && e->type == SC2_EV_CREATED, e ? e->text : "");
}
static uint32_t sc2_EventUnitCreatedBehavior(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now(); return sc2_ev_text_field(j, e && e->type == SC2_EV_CREATED, e ? e->text2 : "");
}
static uint32_t sc2_EventUnitDamageEffect(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now(); return sc2_ev_text_field(j, e && e->dmg, e ? e->text : "");
}
static uint32_t sc2_EventUnitBehavior(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    return sc2_ev_text_field(j, e && (e->type == SC2_EV_XP || e->type == SC2_EV_LEVEL), e ? e->text : "");
}
static uint32_t sc2_EventUnitProgressObjectType(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    bool progress = e && (e->type == SC2_EV_CONSTRUCT || e->type == SC2_EV_TRAIN || e->type == SC2_EV_RESEARCH ||
                          e->type == SC2_EV_MAGAZINE || e->type == SC2_EV_SPECIALIZE);
    return sc2_ev_text_field(j, progress, e ? e->text : "");
}
static uint32_t sc2_EventButtonPressed(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now(); return sc2_ev_text_field(j, e && e->type == SC2_EV_BUTTON, e ? e->text : "");
}
static uint32_t sc2_EventChatMessage(jass_t *j) {
    sc2evresp_t const *e = sc2_ev_now();
    bool matched = jass_checkboolean(j, 1);
    if (!e || e->type != SC2_EV_CHAT) return jass_pushnull(j);
    return jass_pushstring(j, matched ? sc2_ev_matched : e->text);
}
static uint32_t sc2_TriggerDebugOutput(jass_t *j) { (void)j; return jass_pushnull(j); }
