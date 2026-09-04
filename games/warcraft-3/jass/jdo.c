/* jdo.c — JASS VM execution engine, public C API, and coroutine scheduler.
 *
 * Mirrors the combined role of Lua's ldo.c (execution/coroutines) and
 * lapi.c (stack API).  Internal struct definitions live in jstate.h.
 */

#include "jass.h"
#include "jstate.h"
#include "jvm.h"
#include "jparser.h"

//#define DEBUG_JASS

#define JASS_CONSTANT  "constant"
#define JASS_ARRAY     "array"
#define JASS_NULL      "null"
#define JASS_FALSE     "false"
#define JASS_TRUE      "true"
#define JASS_UNM       "-"
#define JASS_COMMA     ","
#define JASS_OPERATOR(NAME) { #NAME, NAME }
#define INF_LOOP_PROTECTION 1000000  /* SC2 Galaxy scripts have large but legitimate loops */
#define SYNTAX_C_OPERATORS 1 // bitmask; enables Galaxy symbolic logic and shift operators
#define SYNTAX_INCLUDES    2 // bitmask; enables Galaxy include preprocessing
#define BZ_JASS_SNAPSHOT_VERSION 2 // format version; adds relocatable VM-owned and function handle records
#define BZ_JASS_SNAPSHOT_MAX_COUNT (1u << 20) // records; bounds allocations and list walks from corrupt snapshots
#define BZ_JASS_SNAPSHOT_MAX_STRING (1u << 20) // bytes; bounds strings from corrupt snapshots

#define assert_type(var, type) assert(jass_checktype(var, type))
#define JASSALLOC(type) jass_alloc(sizeof(type))

static void jass_setnull(LPJASSVAR var);
static void jass_deletedict(LPJASSDICT dict);

#define JASS_ADD_STACK(j, VAR, TYPE) \
LPJASSVAR VAR = &j->stack[j->num_stack++]; \
memset(VAR, 0, sizeof(*VAR)); \
VAR->type = &jass_types[TYPE];

static void jass_store_value(LPJASSVAR var, LPCVOID value, DWORD size) {
    jass_setnull(var);
    if (!value) {
        return;
    }
    var->value = jass_alloc(size);
    memcpy(var->value, value, size);
}

#define JASS_CMPOP(NAME, OP) \
DWORD NAME(LPJASS j) { \
    return jass_pushboolean(j, jass_checknumber(j, 1) OP jass_checknumber(j, 2)); \
}

#define JASS_NUMOP(NAME, OP) \
DWORD NAME(LPJASS j) { \
    if (jass_gettype(j, 1) == jasstype_integer && jass_gettype(j, 2) == jasstype_integer) { \
        return jass_pushinteger(j, jass_checkinteger(j, 1) OP jass_checkinteger(j, 2)); \
    } else { \
        return jass_pushnumber(j, jass_checknumber(j, 1) OP jass_checknumber(j, 2)); \
    } \
}

#ifdef TOKENFUNC
#undef TOKENFUNC
#endif
#ifdef TOKENEVAL
#undef TOKENEVAL
#endif
#define TOKENFUNC(NAME) void eval_##NAME(LPJASS j, LPCTOKEN token)
#define TOKENEVAL(NAME) { #NAME, TT_##NAME, eval_##NAME }

LPPLAYER currentplayer = NULL;
LPEDICT currentunit = NULL;
LPPLAYER currentenumplayer = NULL;
static HANDLE currenttimer = NULL;

LPCSTR keywords[] = {
    "elseif", "else", "endif", "set", "endfunction", "local", "then", NULL
};

static JASSHOST jass_host;

typedef struct {
    LPCSTR delimiters;
    LPTOKEN (*parse)(LPPARSER p);
    DWORD flags;
} JASSSYNTAX;

static const JASSSYNTAX jass_syntax[] = {
    [JASS_MODE_JASS] = { ",;()[]+-/*=<>!", JASS_ParseTokens, 0 },
    [JASS_MODE_GALAXY] = { ",;()[]+-/*={}!<>&|", GALAXY_ParseTokens, SYNTAX_C_OPERATORS | SYNTAX_INCLUDES },
};


/* =========================================================================
 * Primitive type table (indexed by JASSTYPEID)
 * ========================================================================= */

JASSTYPE jass_types[] = {
    { NULL, NULL, "integer" },
    { NULL, NULL, "real" },
    { NULL, NULL, "string" },
    { NULL, NULL, "boolean" },
    { NULL, NULL, "code" },
    { NULL, NULL, "handle" },
    { NULL, NULL, "cfunction" },
};

/* =========================================================================
 * Forward declarations
 * ========================================================================= */

static LPJASSVAR jass_stackvalue(LPJASS j, int index);
static LPJASSVAR jass_topvalue(LPJASS j);
static JASSTYPEID jass_getvarbasetype(LPCJASSVAR var);
static DWORD jass_dotoken(LPJASS j, LPCTOKEN token);
static LPCJASSFUNC find_function(LPCJASS j, LPCSTR name);
static LPJASSVAR find_global(LPCJASS j, LPCSTR name);
static void eval_SINGLETOKEN(LPJASS j, LPCTOKEN token);
static void eval_VARDECL(LPJASS j, LPCTOKEN token);
void eval_TOKENS(LPJASS j, LPCTOKEN token);
static void jass_copy(LPJASS j, LPJASSVAR var, LPCJASSVAR other);
void jass_setreturn(LPJASS j);
BOOL jass_mustreturn(LPJASS j);
BOOL uses_localplayer(LPCTOKEN token);
static void jass_setnull(LPJASSVAR var);
static LPCJASSTYPE find_type(LPCJASS j, LPCSTR name);
static LPCJASSTYPE get_base_type(LPCJASSTYPE type);
static LPJASSVAR ensure_array_value(LPJASS j, LPJASSVAR dest, DWORD index);
static void jass_discard(LPJASS j, DWORD count);

/* =========================================================================
 * Host interface
 * ========================================================================= */

static BOOL jass_atob(LPCSTR str) {
    return !strcmp(str, "true");
}

static void jass_default_error(LPCSTR message) { fprintf(stderr, "JASS runtime error: %s\n", message); }

void jass_sethost(JASSHOST const *host) {
    jass_host = *host;
    if (!jass_host.RuntimeError) jass_host.RuntimeError = jass_default_error;
}

HANDLE jass_alloc(long size) {
    return jass_host.MemAlloc(size);
}

void jass_free(HANDLE ptr) {
    jass_host.MemFree(ptr);
}

static DWORD jass_gettime(void) {
    return jass_host.GetTime ? jass_host.GetTime() : 0;
}

static LPPLAYER jass_getplayerbyindex(DWORD number) {
    return jass_host.GetPlayerByNumber ? jass_host.GetPlayerByNumber(number) : NULL;
}

/* =========================================================================
 * Operators (built-in native functions for arithmetic / comparison)
 * ========================================================================= */

DWORD __add(LPJASS j) {
    if (jass_gettype(j, 1) == jasstype_string && jass_gettype(j, 2) == jasstype_string) {
        LPCSTR a = jass_checkstring(j, 1);
        LPCSTR b = jass_checkstring(j, 2);
        DWORD alen = a ? (DWORD)strlen(a) : 0;
        DWORD blen = b ? (DWORD)strlen(b) : 0;
        LPSTR text = jass_alloc(alen + blen + 1);

        if (alen) {
            memcpy(text, a, alen);
        }
        if (blen) {
            memcpy(text + alen, b, blen);
        }
        text[alen + blen] = '\0';
        jass_pushstringlen(j, text, alen + blen);
        jass_free(text);
        return 1;
    }

    if (jass_gettype(j, 1) == jasstype_integer && jass_gettype(j, 2) == jasstype_integer) {
        return jass_pushinteger(j, jass_checkinteger(j, 1) + jass_checkinteger(j, 2));
    }
    return jass_pushnumber(j, jass_checknumber(j, 1) + jass_checknumber(j, 2));
}

DWORD __unm(LPJASS j) {
    if (jass_gettype(j, 1) == jasstype_integer) {
        return jass_pushinteger(j, -jass_checkinteger(j, 1));
    } else {
        return jass_pushnumber(j, -jass_checknumber(j, 1));
    }
}

JASS_NUMOP(__sub, -);
JASS_NUMOP(__mul, *);
JASS_NUMOP(__div, /);
JASS_CMPOP(__le, <=);
JASS_CMPOP(__ge, >=);
JASS_CMPOP(__gt, >);
JASS_CMPOP(__lt, <);

static BOOL jass_valuehandle(LPCSTR type) {
    static LPCSTR const value_handles[] = {
        "race", "alliancetype", "racepreference", "igamestate", "fgamestate", "playerstate",
        "playergameresult", "unitstate", "gameevent", "playerevent", "playerunitevent", "widgetevent",
        "dialogevent", "unitevent", "limitop", "unittype", "gamespeed", "placement", "startlocprio",
        "gamedifficulty", "aidifficulty", "gametype", "mapflag", "mapvisibility", "mapsetting", "mapdensity", "mapcontrol",
        "playercolor", "playerslotstate", "volumegroup", "camerafield", "blendmode", "raritycontrol",
        "texmapflags", "fogstate", "effecttype"
    };
    FOR_LOOP(i, sizeof(value_handles) / sizeof(value_handles[0])) if (!strcmp(type, value_handles[i])) return true;
    return false;
}

static BOOL var_eq(LPCJASSVAR a, LPCJASSVAR b) {
    switch ((a->value == NULL) + (b->value == NULL)) {
        case 2: return true;
        case 1: return false;
    }
    if (jass_getvarbasetype(a) != jass_getvarbasetype(b)) return false;
    switch (jass_getvarbasetype(a)) {
        case jasstype_integer: return !memcmp(a->value, b->value, sizeof(LONG));
        case jasstype_real: return !memcmp(a->value, b->value, sizeof(FLOAT));
        case jasstype_string: return !strcmp(a->value, b->value);
        case jasstype_boolean: return !memcmp(a->value, b->value, sizeof(BOOL));
        case jasstype_code: return !memcmp(a->value, b->value, sizeof(HANDLE));
        case jasstype_cfunction: return !memcmp(a->value, b->value, sizeof(HANDLE));
        case jasstype_handle:
            if (a->value == b->value) return true;
            if (a->type != b->type) return false;
            return jass_valuehandle(a->type->name) && !memcmp(a->value, b->value, sizeof(DWORD));
    }
    return false;
}

DWORD __eq(LPJASS j) {
    return jass_pushboolean(j, var_eq(jass_stackvalue(j, 1), jass_stackvalue(j, 2)));
}

DWORD __ne(LPJASS j) {
    return jass_pushboolean(j, !var_eq(jass_stackvalue(j, 1), jass_stackvalue(j, 2)));
}

DWORD __and(LPJASS j) {
    return jass_pushboolean(j, jass_toboolean(j, 1) && jass_toboolean(j, 2));
}

DWORD __or(LPJASS j) {
    return jass_pushboolean(j, jass_toboolean(j, 1) || jass_toboolean(j, 2));
}

DWORD __not(LPJASS j) {
    return jass_pushboolean(j, !jass_toboolean(j, 1));
}

DWORD __lsh(LPJASS j) {
    return jass_pushinteger(j, (LONG)jass_checkinteger(j, 1) << (LONG)jass_checkinteger(j, 2));
}
DWORD __rsh(LPJASS j) {
    return jass_pushinteger(j, (LONG)jass_checkinteger(j, 1) >> (LONG)jass_checkinteger(j, 2));
}
DWORD __bor(LPJASS j) {
    return jass_pushinteger(j, (LONG)jass_checkinteger(j, 1) | (LONG)jass_checkinteger(j, 2));
}
DWORD __band(LPJASS j) {
    return jass_pushinteger(j, (LONG)jass_checkinteger(j, 1) & (LONG)jass_checkinteger(j, 2));
}
DWORD __xor(LPJASS j) {
    return jass_pushinteger(j, (LONG)jass_checkinteger(j, 1) ^ (LONG)jass_checkinteger(j, 2));
}

JASSMODULE jass_operators[] = {
    JASS_OPERATOR(__add),
    JASS_OPERATOR(__sub),
    JASS_OPERATOR(__mul),
    JASS_OPERATOR(__div),
    JASS_OPERATOR(__ne),
    JASS_OPERATOR(__eq),
    JASS_OPERATOR(__ge),
    JASS_OPERATOR(__le),
    JASS_OPERATOR(__gt),
    JASS_OPERATOR(__lt),
    JASS_OPERATOR(__and),
    JASS_OPERATOR(__or),
    JASS_OPERATOR(__unm),
    JASS_OPERATOR(__not),
    JASS_OPERATOR(__lsh),
    JASS_OPERATOR(__rsh),
    JASS_OPERATOR(__bor),
    JASS_OPERATOR(__band),
    JASS_OPERATOR(__xor),
    { NULL },
};

/* =========================================================================
 * String utilities
 * ========================================================================= */

void removeDoubleBackslashes(LPSTR str) {
    size_t len = strlen(str);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        str[j++] = str[i];
        if (str[i] == '\\' && str[i + 1] == '\\') {
            i++;
        }
    }
    str[j] = '\0';
}

BOOL is_integer(LPCSTR tok) {
    LPSTR endptr;
    strtol(tok, &endptr, 10);
    return *endptr == '\0';
}

BOOL is_float(LPCSTR tok) {
    LPSTR endptr;
    strtod(tok, &endptr);
    return *endptr == '\0';
}

BOOL is_fourcc(LPCSTR tok) {
    return *tok == '\'';
}

BOOL is_string(LPCSTR tok) {
    return *tok == '\"';
}

BOOL is_identifier(LPCSTR str) {
    if (!isalpha(*str) && *str != '_')
        return false;
    for (LPCSTR s = str; *s; ++s) {
        if (!isalnum(*s) && *s != '_')
            return false;
    }
    for (LPCSTR *kw = keywords; *kw; kw++) {
        if (!strcmp(str, *kw)) {
            return false;
        }
    }
    return true;
}

BOOL is_comma(LPCSTR str) {
    return !strcmp(str, ",");
}

/* =========================================================================
 * Context
 * ========================================================================= */

LPCJASSCONTEXT jass_getcontext(LPJASS j) {
    return &j->context;
}

static LPJASS jass_root(LPJASS j) { return j->root ? j->root : j; }
LPJASS jass_getroot(LPJASS j)     { return jass_root(j); }
BOOL jass_isrunning(LPJASS j)     { return jass_root(j)->current_coroutine != NULL; }
void jass_haltevents(LPJASS j)    { jass_root(j)->halt_events = true; }

/* =========================================================================
 * Coroutine frame management
 * ========================================================================= */

static void jass_free_frame(LPJASSCOROUTINE co, LPJASSCOROUTINEFRAME frame) {
    if (frame->locals) {
        FOR_LOOP(i, co->state->num_stack)
            if (co->state->stack[i].env.locals == frame->locals) co->state->stack[i].env.locals = NULL;
        jass_deletedict(frame->locals);
    }
    jass_free(frame);
}

static void jass_free_coroutine(LPJASSCOROUTINE co) {
    while (co->frames) {
        LPJASSCOROUTINEFRAME next = co->frames->next;
        jass_free_frame(co, co->frames);
        co->frames = next;
    }
    FOR_LOOP(i, co->state->num_stack) jass_setnull(co->state->stack + i);
    SAFE_DELETE(co->state, jass_free);
    jass_free(co);
}

static LPJASSCOROUTINEFRAME jass_coroutine_pushframe(LPJASSCOROUTINE co,
                                                     JASSFRAMETYPE type,
                                                     LPCJASSFUNC func,
                                                     LPCTOKEN body,
                                                     LPJASSDICT locals) {
    LPJASSCOROUTINEFRAME frame = JASSALLOC(JASSCOROUTINEFRAME);
    frame->type = type;
    frame->func = func;
    frame->body = body;
    frame->pc = body;
    frame->locals = locals;
    frame->loop_count = 0;
    ADD_TO_LIST(frame, co->frames);
    return frame;
}

static void jass_coroutine_popframe(LPJASSCOROUTINE co) {
    LPJASSCOROUTINEFRAME frame = co->frames;
    if (frame) {
        co->frames = frame->next;
        jass_free_frame(co, frame);
    }
}

static LPJASSCOROUTINEFRAME jass_coroutine_functionframe(LPJASSCOROUTINE co) {
    FOR_EACH_LIST(JASSCOROUTINEFRAME, frame, co->frames) {
        if (frame->type == JASS_FRAME_FUNCTION) {
            return frame;
        }
    }
    return NULL;
}

static void jass_coroutine_useframe(LPJASS j, LPJASSCOROUTINE co) {
    LPJASSCOROUTINEFRAME frame = jass_coroutine_functionframe(co);
    if (j->num_stack && frame) {
        j->stack[0].env.locals = frame->locals;
    }
}

static LPJASSDICT jass_coroutine_buildlocals(LPJASS j, LPCJASSFUNC func, LPCTOKEN args) {
    LPJASSDICT locals = NULL;
    LPCTOKEN arg_token = args;

    FOR_EACH_LIST(JASSARG, arg, func->args) {
        LPJASSDICT local = JASSALLOC(JASSDICT);
        local->key = arg->name;
        local->value.type = arg->type;
        if (arg_token) {
            DWORD count = jass_dotoken(j, arg_token);
            /* Match synchronous calls: a void or unresolved argument becomes null instead of underflowing the stack. */
            if (!count) { jass_pushnull(j); count = 1; }
            jass_copy(j, &local->value, jass_topvalue(j));
            jass_discard(j, count);
            arg_token = arg_token->next;
        }
        PUSH_BACK(JASSDICT, local, locals);
    }
    return locals;
}

/* =========================================================================
 * Coroutine lifecycle
 * ========================================================================= */

LPJASSCOROUTINE jass_startcoroutine(LPJASS j, LPCJASSCONTEXT context) {
    LPJASS root = jass_root(j);
    LPJASS co_state = JASSALLOC(JASS);
    memcpy(co_state, root, sizeof(JASS));
    memset(co_state->stack, 0, sizeof(co_state->stack));
    co_state->stack_pointer = co_state->stack;
    co_state->num_stack = 0;
    co_state->context = *context;
    /* Event-response state and GetLocalPlayer() selection are independent.
     * Nested TriggerExecute/ExecuteFunc calls inherit the event response from
     * their parent coroutine, while local-player branches inherit only the
     * active presentation selector. */
    if (!co_state->context.playerState) {
        co_state->context.playerState = jass_getcontext(j)->playerState;
    }
    if (!co_state->context.localPlayerState) {
        co_state->context.localPlayerState = currentplayer;
    }
    if (!co_state->context.unit) {
        co_state->context.unit = currentunit;
    }
    co_state->root = root;
    co_state->coroutines = NULL;
    co_state->current_coroutine = NULL;

    LPJASSCOROUTINE co = JASSALLOC(JASSCOROUTINE);
    co->state = co_state;
    co->frames = NULL;
    co->wake_time = jass_gettime();
    co->yielded = false;
    co->done = false;
    co->next = NULL;
    if (context->func) {
        jass_coroutine_pushframe(co,
                                 JASS_FRAME_FUNCTION,
                                 context->func,
                                 context->func->code,
                                 NULL);
    }

    PUSH_BACK(JASSCOROUTINE, co, root->coroutines);
    return co;
}

LPJASSCOROUTINE jass_startcoroutinebyname(LPJASS j, LPCSTR name) {
    LPCJASSFUNC func = find_function(jass_root(j), name);
    JASSCONTEXT context = *jass_getcontext(j);

    if (!func) {
        fprintf(stderr, "Function not found %s\n", name);
        return NULL;
    }
    context.func = func;
    return jass_startcoroutine(j, &context);
}

/* AI roots have no ambient map-trigger context, so their entrypoint must carry the owning player explicitly. */
LPJASSCOROUTINE jass_startcoroutinebynameforplayer(LPJASS j, LPCSTR name, struct playerState_s *player) {
    LPCJASSFUNC func = find_function(jass_root(j), name);
    JASSCONTEXT context = *jass_getcontext(j);
    if (!func) {
        fprintf(stderr, "Function not found %s\n", name);
        return NULL;
    }
    context.func = func;
    context.playerState = player;
    return jass_startcoroutine(j, &context);
}

/* Dynamic wait-done calls must share the active coroutine so yielded child frames resume before their caller. */
BOOL jass_callcoroutinebyname(LPJASS j, LPCSTR name) {
    LPJASS root = jass_root(j);
    LPJASSCOROUTINE co = root->current_coroutine;
    LPCJASSFUNC func = find_function(root, name);

    if (!co || !func || func->nativefunc) return false;
    jass_coroutine_pushframe(co, JASS_FRAME_FUNCTION, func, func->code, NULL);
    return true;
}

LPCSTR jass_functionname(LPCJASSFUNC func) {
    return func ? func->name : NULL;
}

LPCJASSFUNC jass_functionbyname(LPJASS j, LPCSTR name) { return find_function(jass_root(j), name); }
void jass_settimercontext(HANDLE timer) { currenttimer = timer; }

BOOL jass_triggerdisabled(LPTRIGGER trigger) {
    return trigger ? trigger->disabled : false;
}

/* =========================================================================
 * Sleep / yield
 * ========================================================================= */

void jass_sleep(LPJASS j, DWORD msec) {
    LPJASS root = jass_root(j);
    LPJASSCOROUTINE co = root->current_coroutine;
    if (!co) {
        return;
    }
    co->wake_time = jass_gettime() + msec;
    co->yielded = true;
}

static BOOL jass_yielded(LPJASS j) {
    LPJASS root = jass_root(j);
    LPJASSCOROUTINE co = root->current_coroutine;
    return co && co->yielded;
}

/* =========================================================================
 * Runtime error boundary  (JASS equivalent of Lua error())
 * ========================================================================= */

static void jass_setruntimeerror(LPJASS j, LPCSTR message) {
    LPJASS root = jass_root(j);
    root->rterror_pending = true;
    snprintf(root->rterror_message, sizeof(root->rterror_message), "%s", message ? message : "(nil)");
    jass_host.RuntimeError(root->rterror_message);
}

static void jass_unimplementednative(LPJASS j, LPCSTR name) {
    LPJASS root = jass_root(j);
    char message[256];
    snprintf(message, sizeof(message), "unimplemented native: %s", name ? name : "(nil)");
    jass_setruntimeerror(root, message);
    if (root->current_coroutine && root->current_coroutine->rterror_jmp_set)
        longjmp(root->current_coroutine->rterror_jmp, 1);
    if (root->sync_rterror_jmp_set) longjmp(root->sync_rterror_jmp, 1);
}

void jass_rterror(LPJASS j, LPCSTR message) {
    LPJASS root = jass_root(j);
    jass_setruntimeerror(root, message);

    LPJASSCOROUTINE co = root->current_coroutine;
    if (co && co->rterror_jmp_set) {
        longjmp(co->rterror_jmp, 1);
    }
    if (root->sync_rterror_jmp_set) longjmp(root->sync_rterror_jmp, 1);
    /* No active coroutine boundary — abort process (parser-path fallback). */
    abort();
}

BOOL jass_rterror_pending(LPJASS j) {
    return jass_root(j)->rterror_pending;
}

LPCSTR jass_rterror_message(LPJASS j) {
    return jass_root(j)->rterror_message;
}

void jass_rterror_clear(LPJASS j) {
    LPJASS root = jass_root(j);
    root->rterror_pending = false;
    root->rterror_message[0] = '\0';
}

/* =========================================================================
 * Player event helpers
 * ========================================================================= */

static LPPLAYER jass_eventplayer(LPEDICT unit) {
    if (!unit) {
        return NULL;
    }
    if (unit->client) {
        return &unit->client->ps;
    }
    return jass_getplayerbyindex(unit->s.player);
}

/* =========================================================================
 * Coroutine resume engine
 * ========================================================================= */

static BOOL jass_coroutine_callstatement(LPJASS j, LPJASSCOROUTINE co, LPCTOKEN token) {
    LPCJASSFUNC func = NULL;
    LPJASSDICT locals;

    if (token->type != TT_CALL || !(func = find_function(j, token->primary))) {
        return false;
    }
    if (func->nativefunc) {
        return false;
    }
    /* Native declarations without host bindings must not run as empty script functions. */
    if (func->native) {
        jass_unimplementednative(j, func->name);
        return true;
    }
    locals = jass_coroutine_buildlocals(j, func, token->args);
    jass_coroutine_pushframe(co, JASS_FRAME_FUNCTION, func, func->code, locals);
    return true;
}

static LPCTOKEN jass_coroutine_selectifbody(LPJASS j, LPCTOKEN token) {
    if (uses_localplayer(token->condition) && currentplayer) {
        if (jass_dotoken(j, token->condition) && jass_popboolean(j)) {
            return token->body;
        }
        return NULL;
    }

    while (token) {
        if (!token->condition) {
            return token->body;
        }
        jass_dotoken(j, token->condition);
        if (jass_popboolean(j)) {
            return token->body;
        }
        token = token->elseblock;
    }
    return NULL;
}

static BOOL jass_coroutine_runlocalplayerif(LPJASS j, LPJASSCOROUTINE co, LPCTOKEN token) {
    LPPLAYER previous_player;

    if (!uses_localplayer(token->condition) || currentplayer) {
        return false;
    }

    previous_player = currentplayer;
    FOR_LOOP(i, MAX_PLAYERS) {
        currentplayer = jass_getplayerbyindex(i);
        jass_dotoken(j, token->condition);
        if (jass_popboolean(j)) {
            eval_TOKENS(j, token->body);
            if (jass_yielded(j)) {
                currentplayer = previous_player;
                return true;
            }
        }
    }
    currentplayer = previous_player;
    return true;
}

static void jass_coroutine_return(LPJASSCOROUTINE co) {
    while (co->frames) {
        JASSFRAMETYPE type = co->frames->type;
        jass_coroutine_popframe(co);
        if (type == JASS_FRAME_FUNCTION) {
            return;
        }
    }
}

static void jass_resumecoroutine(LPJASSCOROUTINE co) {
    LPJASS j = co->state;

    if (!j->num_stack) {
        jass_pushfunction(j, j->context.func);
        j->stack_pointer = &j->stack[0];
        j->stack[0].env.done = false;
        j->stack[0].env.returnstack = -1;
    }

    /* Establish the runtime-error abort boundary for this resume. */
    co->rterror_jmp_set = true;
    if (setjmp(co->rterror_jmp) != 0) {
        /* jass_rterror() jumped here — mark coroutine done and return. */
        co->rterror_jmp_set = false;
        co->done = true;
        return;
    }

    co->yielded = false;
    while (co->frames && !co->yielded) {
        LPJASSCOROUTINEFRAME frame = co->frames;
        LPCTOKEN token = frame->pc;
        LPCTOKEN next;

        jass_coroutine_useframe(j, co);
        if (!token) {
            if (frame->type == JASS_FRAME_LOOP) {
                frame->pc = frame->body;
                /* Keep the increment outside assert: NDEBUG used to remove it together with the diagnostic check. */
                frame->loop_count++;
                assert(frame->loop_count <= INF_LOOP_PROTECTION);
            } else {
                jass_coroutine_popframe(co);
            }
            continue;
        }

        next = token->next;
        if (token->flags & TF_DEBUG) { frame->pc = next; continue; }
        switch (token->type) {
            case TT_CALL:
                frame->pc = next;
                if (!jass_coroutine_callstatement(j, co, token)) {
                    eval_SINGLETOKEN(j, token);
                }
                break;
            case TT_IF: {
                LPCTOKEN body;
                frame->pc = next;
                if (jass_coroutine_runlocalplayerif(j, co, token)) {
                    break;
                }
                body = jass_coroutine_selectifbody(j, token);
                if (body) {
                    jass_coroutine_pushframe(co, JASS_FRAME_BLOCK, NULL, body, NULL);
                }
                break;
            }
            case TT_LOOP:
                frame->pc = next;
                jass_coroutine_pushframe(co, JASS_FRAME_LOOP, NULL, token->body, NULL);
                break;
            case TT_EXITWHEN:
                frame->pc = next;
                jass_dotoken(j, token->condition);
                if (jass_popboolean(j)) {
                    /* Pop frames up to and including the enclosing LOOP frame. */
                    while (co->frames) {
                        JASSFRAMETYPE ft = co->frames->type;
                        jass_coroutine_popframe(co);
                        if (ft == JASS_FRAME_LOOP) {
                            break;
                        }
                    }
                }
                break;
            case TT_RETURN:
                frame->pc = NULL;
                if (token->body) {
                    jass_dotoken(j, token->body);
                }
                jass_coroutine_return(co);
                break;
            case TT_VARDECL:
                frame->pc = next;
                eval_VARDECL(j, token);
                /* Sync the new local back into the current function frame. */
                {
                    LPJASSCOROUTINEFRAME fn_frame = jass_coroutine_functionframe(co);
                    if (fn_frame) {
                        fn_frame->locals = j->stack[0].env.locals;
                    }
                }
                break;
            default:
                frame->pc = next;
                eval_SINGLETOKEN(j, token);
                break;
        }
    }

    co->rterror_jmp_set = false;
    if (!co->yielded && !co->frames) {
        co->done = true;
    }
}

BOOL jass_coroutinedone(LPCJASSCOROUTINE co) {
    return !co || co->done;
}

BOOL jass_resume(LPJASS j, LPJASSCOROUTINE co) {
    LPJASS root = jass_root(j);
    DWORD now = jass_gettime();

    if (!co || co->done || co->wake_time > now) {
        return false;
    }

    LPPLAYER previous_player = currentplayer;
    LPEDICT previous_unit = currentunit;

    root->current_coroutine = co;
    currentplayer = co->state->context.localPlayerState;
    currentunit = co->state->context.unit;
    jass_resumecoroutine(co);
    currentunit = previous_unit;
    currentplayer = previous_player;
    root->current_coroutine = NULL;

    return true;
}

void jass_runevents(LPJASS j) {
    LPJASS root = jass_root(j);
    LPJASSCOROUTINE prev = NULL;
    LPJASSCOROUTINE co = root->coroutines;

    while (co) {
        LPJASSCOROUTINE next;
        jass_resume(root, co);

        next = co->next;
        if (co->done) {
            if (prev) {
                prev->next = next;
            } else {
                root->coroutines = next;
            }
            jass_free_coroutine(co);
        } else {
            prev = co;
        }
        if (root->halt_events) break;
        co = next;
    }
}

/* =========================================================================
 * Trigger evaluation / execution
 * ========================================================================= */

static BOOL jass_evaluatetriggercontext(LPJASS j,
                                        LPTRIGGER trigger,
                                        LPEDICT unit,
                                        LPEDICT source) {
    LPPLAYER player = jass_eventplayer(unit);

    if (trigger->disabled) {
        return false;
    }
    JASS tmp_state;
    FOR_EACH_LIST(TRIGGERCONDITION, cond, trigger->conditions) {
        memcpy(&tmp_state, j, sizeof(struct jass_s));
        memset(tmp_state.stack, 0, sizeof(tmp_state.stack));
        tmp_state.num_stack = 0;
        tmp_state.context.trigger = trigger;
        tmp_state.context.unit = unit;
        tmp_state.context.source = source;
        tmp_state.context.playerState = player;
        tmp_state.context.localPlayerState = currentplayer;
        tmp_state.context.timer = currenttimer;
        jass_pushfunction(&tmp_state, cond->expr);
        LPEDICT previous_unit = currentunit;
        currentunit = unit;
        DWORD result_count = jass_call(&tmp_state, 0);
        currentunit = previous_unit;
        if (result_count != 1 || !jass_popboolean(&tmp_state)) {
            return false;
        }
    }
    return true;
}

BOOL jass_evaluatetrigger(LPJASS j, LPTRIGGER trigger, LPEDICT unit) {
    return jass_evaluatetriggercontext(j, trigger, unit, NULL);
}

/* Evaluate a single boolexpr (a Condition()/Filter() code) against a candidate
 * unit, the way jass_evaluatetrigger evaluates a trigger condition: run the
 * function in a scratch state with the unit bound as the context/current unit
 * so GetFilterUnit()/GetEnumUnit() inside the filter resolve to it.  Returns
 * the filter's boolean result; a null filter passes (matches "no filter"). */
BOOL jass_evaluateboolexpr(LPJASS j, LPCJASSFUNC expr, LPEDICT unit) {
    if (!expr) {
        return true;
    }
    JASS tmp_state;
    memcpy(&tmp_state, j, sizeof(struct jass_s));
    memset(tmp_state.stack, 0, sizeof(tmp_state.stack));
    tmp_state.num_stack = 0;
    tmp_state.context.unit = unit;
    jass_pushfunction(&tmp_state, expr);
    LPEDICT previous_unit = currentunit;
    currentunit = unit;
    DWORD result_count = jass_call(&tmp_state, 0);
    currentunit = previous_unit;
    return result_count == 1 && jass_popboolean(&tmp_state);
}

/* Force filters bind players through the same scratch-state context used by
 * trigger callbacks, keeping GetFilterPlayer isolated across nested calls. */
BOOL jass_evaluateplayerexpr(LPJASS j, LPCJASSFUNC expr, LPPLAYER player) {
    if (!expr) return true;
    JASS tmp_state;
    memcpy(&tmp_state, j, sizeof(struct jass_s));
    memset(tmp_state.stack, 0, sizeof(tmp_state.stack));
    tmp_state.num_stack = 0;
    tmp_state.context.playerState = player;
    jass_pushfunction(&tmp_state, expr);
    DWORD result_count = jass_call(&tmp_state, 0);
    return result_count == 1 && jass_popboolean(&tmp_state);
}

static void jass_executetriggercontext(LPJASS j,
                                       LPTRIGGER trigger,
                                       LPEDICT unit,
                                       LPEDICT source) {
    FOR_EACH_LIST(TRIGGERACTION, action, trigger->actions) {
        LPPLAYER player = jass_eventplayer(unit);
        jass_startcoroutine(j, &MAKE(JASSCONTEXT,
                                  .trigger = trigger,
                                  .func = action->func,
                                  .unit = unit,
                                  .source = source,
                                  .playerState = player,
                                  .localPlayerState = currentplayer,
                                  .timer = currenttimer,
                              ));
    }
}

void jass_executetrigger(LPJASS j, LPTRIGGER trigger, LPEDICT unit) {
    jass_executetriggercontext(j, trigger, unit, NULL);
}

BOOL jass_calltrigger(LPJASS j,
                      LPTRIGGER trigger,
                      LPEDICT unit,
                      LPEDICT source) {
    if (jass_evaluatetriggercontext(j, trigger, unit, source)) {
        jass_executetriggercontext(j, trigger, unit, source);
        return true;
    } else {
        return false;
    }
}

/* =========================================================================
 * Lookup helpers
 * ========================================================================= */

static LPJASSCFUNCTION find_cfunction(LPCJASS j, LPCSTR name) {
    for (LPCJASSMODULE m = jass_operators; m->name; m++) {
        if (!strcmp(m->name, name)) {
            return m->func;
        }
    }
    if (jass_host.natives) {
        for (LPCJASSMODULE m = jass_host.natives; m->name; m++) {
            if (!strcmp(m->name, name)) {
                return m->func;
            }
        }
    }
    if (jass_host.galaxy_natives) {
        for (LPCJASSMODULE m = jass_host.galaxy_natives; m->name; m++) {
            if (!strcmp(m->name, name)) {
                return m->func;
            }
        }
    }
    return NULL;
}

/* Root declarations use separate bucket links so list order and duplicate-name behavior stay unchanged. */
static DWORD jass_hash(LPCSTR name) {
    DWORD hash = 0;
    while (*name) hash = (BYTE)*name++ + (hash << 6) + (hash << 16) - hash;
    return hash & (BZ_JASS_HASH_SIZE - 1);
}

static LPCJASSFUNC find_function(LPCJASS j, LPCSTR name) {
    for (LPCJASSFUNC func = j->function_hash[jass_hash(name)]; func; func = func->hash_next) {
        if (!strcmp(func->name, name)) {
            return func;
        }
    }
    return NULL;
}

static LPJASSVAR find_dict(LPJASSDICT dict, LPCSTR name) {
    FOR_EACH_LIST(JASSDICT, item, dict) {
        if (!strcmp(item->key, name)) {
            return &item->value;
        }
    }
    return NULL;
}

static LPJASSVAR find_global(LPCJASS j, LPCSTR name) {
    for (LPJASSDICT item = j->global_hash[jass_hash(name)]; item; item = item->hash_next) {
        if (!strcmp(item->key, name)) return &item->value;
    }
    return NULL;
}

static LPCJASSTYPE find_type(LPCJASS j, LPCSTR name) {
    FOR_LOOP(i, sizeof(jass_types)/sizeof(*jass_types)) {
        if (!strcmp(jass_types[i].name, name)) {
            return &jass_types[i];
        }
    }
    FOR_EACH_LIST(JASSTYPE, type, j->types) {
        if (!strcmp(type->name, name)) {
            return type;
        }
    }
    return NULL;
}

LPCJASSTYPE get_base_type(LPCJASSTYPE type) {
    if (!type) return &jass_types[jasstype_handle];  /* Galaxy SC2 types → opaque handle */
    while (type->inherit) {
        type = type->inherit;
    }
    return type;
}

/* =========================================================================
 * Stack: return / done flags
 * ========================================================================= */

void jass_setreturn(LPJASS j) {
    jass_stackvalue(j, 0)->env.done = true;
    jass_stackvalue(j, 0)->env.returnstack = j->num_stack;
}

BOOL jass_mustreturn(LPJASS j) {
    return jass_stackvalue(j, 0)->env.done;
}

DWORD jass_top(LPJASS j) {
    return j->num_stack - 1;
}

LPJASSVAR jass_topvalue(LPJASS j) {
    return j->stack + jass_top(j);
}

LPJASSVAR jass_stackvalue(LPJASS j, int index) {
    if (index < 0) {
        return j->stack + (j->num_stack + index);
    } else {
        return j->stack_pointer + index;
    }
}

JASSTYPEID jass_getvarbasetype(LPCJASSVAR var) {
    return (JASSTYPEID)(get_base_type(var->type) - jass_types);
}

JASSTYPEID jass_gettype(LPJASS j, int index) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    return jass_getvarbasetype(var);
}

BOOL jass_checktype(LPCJASSVAR var, JASSTYPEID type) {
    return get_base_type(var->type) == jass_types + type;
}

void jass_pop(LPJASS j, DWORD count) {
    j->num_stack -= count;
}

static void jass_discard(LPJASS j, DWORD count) {
    while (count-- && j->num_stack) {
        jass_setnull(jass_topvalue(j));
        jass_pop(j, 1);
    }
}

/* =========================================================================
 * Memory: null / copy / free
 * ========================================================================= */

static void jass_deletedict(LPJASSDICT dict) {
    SAFE_DELETE(dict->next, jass_deletedict);
    jass_setnull(&dict->value);
    jass_free(dict);
}

static void jass_deletearray(LPJASSARRAY array) {
    if (!array) return;
    jass_deletearray(array->next);
    jass_setnull(&array->value);
    jass_free(array);
}

void jass_setnull(LPJASSVAR var) {
    if (!var || !var->type) {
        return;
    }
    uintptr_t typeaddr = (uintptr_t)var->type;
    if ((typeaddr & (sizeof(void *) - 1)) || typeaddr < 4096 || typeaddr >= 0x0000800000000000ULL) {
        memset(var, 0, sizeof(*var));
        return;
    }
    SAFE_DELETE(var->_array, jass_deletearray);
    JASSTYPEID type = jass_getvarbasetype(var);
    if (type == jasstype_code || type == jasstype_cfunction) {
        SAFE_DELETE(var->env.locals, jass_deletedict);
    }
    switch (type) {
        case jasstype_handle:
            if (var->ref && var->ref->refs > 0) {
                var->ref->refs--;
                var->value = NULL;
                var->ref = NULL;
            } else {
                SAFE_DELETE(var->value, jass_free);
                SAFE_DELETE(var->ref, jass_free);
            }
            break;
        case jasstype_code:
        case jasstype_cfunction:
            break;
        default:
            SAFE_DELETE(var->value, jass_free);
            break;
    }
}

BOOL is_handle_convertible(LPCJASSTYPE from, LPCJASSTYPE to) {
    if (from == to) {
        return true;
    } else if (from->inherit) {
        return is_handle_convertible(from->inherit, to);
    } else {
        return false;
    }
}

static LPJASSVAR ensure_array_value(LPJASS j, LPJASSVAR dest, DWORD index) {
    (void)j;
    FOR_EACH_LIST(JASSARRAY, var, dest->_array) {
        if (var->index == index) {
            return &var->value;
        }
    }
    LPJASSARRAY jv = JASSALLOC(JASSARRAY);
    jv->value.type = dest->type;
    jv->index = index;
    ADD_TO_LIST(jv, dest->_array);
    return &jv->value;
}

void jass_copy(LPJASS j, LPJASSVAR var, LPCJASSVAR other) {
    FLOAT fval = 0;
    jass_setnull(var);
    if (other->_array) {
        var->type = other->type;
        FOR_EACH_LIST(JASSARRAY, srcar, other->_array) {
            jass_copy(j, ensure_array_value(j, var, srcar->index), &srcar->value);
        }
        return;
    } else if (!other->value) {
        return;
    } else switch (jass_getvarbasetype(var)) {
        case jasstype_integer:
            jass_store_value(var, other->value, sizeof(LONG));
            break;
        case jasstype_handle:
            if (var->type && other->type && !is_handle_convertible(other->type, var->type)) {
                fprintf(stderr, "Warning: Passing %s to %s type\n", other->type->name, var->type->name);
            }
            var->value = other->value;
            var->ref = other->ref;
            if (var->ref) {
                var->ref->refs++;
            }
            break;
        case jasstype_real:
            switch (jass_getvarbasetype(other)) {
                case jasstype_real:
                    fval = *(FLOAT const *)other->value;
                    break;
                case jasstype_integer:
                    fval = *(LONG const *)other->value;
                    break;
                default:
                    fval = 0.0f;
                    break;
            }
            jass_store_value(var, &fval, sizeof(FLOAT));
            break;
        case jasstype_boolean:
            jass_store_value(var, other->value, sizeof(BOOL));
            break;
        case jasstype_string:
            jass_store_value(var, other->value, strlen((char *)other->value)+1);
            break;
        case jasstype_code:
        case jasstype_cfunction:
            var->value = other->value;
            break;
        default:
            /* Unknown type (e.g. Galaxy SC2 handle subtype) — copy as handle. */
            var->value = other->value;
            break;
    }
}

/* =========================================================================
 * Public C API — stack push / check / pop (mirrors Lua's lapi.c)
 * ========================================================================= */

DWORD jass_pushnull(LPJASS j) {
    JASS_ADD_STACK(j, var, jasstype_handle);
    return 1;
}

DWORD jass_pushinteger(LPJASS j, LONG value) {
    JASS_ADD_STACK(j, var, jasstype_integer);
    jass_store_value(var, &value, sizeof(value));
    return 1;
}

DWORD jass_pushhandle(LPJASS j, HANDLE value, LPCSTR type) {
    JASS_ADD_STACK(j, var, jasstype_handle);
    jass_setnull(var);
    var->type = find_type(j, type);
    if (value) {
        var->value = value;
        var->ref = jass_alloc(sizeof(*var->ref));
        *var->ref = (JASSREF){ 0 };
    }
    return 1;
}

DWORD jass_pushnullhandle(LPJASS j, LPCSTR type) {
    return jass_pushhandle(j, 0, type);
}

HANDLE jass_newhandle(LPJASS j, DWORD size, LPCSTR type) {
    HANDLE data = size ? jass_alloc(size) : NULL;
    jass_pushhandle(j, data, type);
    if (data) {
        LPJASSVAR var = jass_topvalue(j);
        var->ref->size = size;
        var->ref->id = ++jass_root(j)->next_handle_id;
    }
    return data;
}

DWORD jass_pushlighthandle(LPJASS j, HANDLE value, LPCSTR type) {
    JASS_ADD_STACK(j, var, jasstype_handle);
    jass_setnull(var);
    var->type = find_type(j, type);
    var->value = value;
    var->ref = jass_alloc(sizeof(*var->ref));
    *var->ref = (JASSREF){ .refs = 1 };
    return 1;
}

DWORD jass_pushnumber(LPJASS j, FLOAT value) {
    JASS_ADD_STACK(j, var, jasstype_real);
    jass_store_value(var, &value, sizeof(value));
    return 1;
}

DWORD jass_pushboolean(LPJASS j, BOOL value) {
    JASS_ADD_STACK(j, var, jasstype_boolean);
    jass_store_value(var, &value, sizeof(value));
    return 1;
}

DWORD jass_pushstringlen(LPJASS j, LPCSTR value, DWORD len) {
    JASS_ADD_STACK(j, var, jasstype_string);
    jass_store_value(var, value, len+1);
    ((LPSTR)var->value)[len] = '\0';
    removeDoubleBackslashes(var->value);
    return 1;
}

DWORD jass_pushstring(LPJASS j, LPCSTR value) {
    /* Tolerate a NULL string from a native (several are stubs that return 0);
     * strlen(NULL) would crash.  An unset JASS string is the empty string. */
    if (!value)
        value = "";
    jass_pushstringlen(j, value, (DWORD)strlen(value));
    return 1;
}

DWORD jass_pushcfunction(LPJASS j, LPJASSCFUNCTION func) {
    JASS_ADD_STACK(j, var, jasstype_cfunction);
    jass_store_value(var, &func, sizeof(LPJASSCFUNCTION));
    return 1;
}

DWORD jass_pushfunction(LPJASS j, LPCJASSFUNC func) {
    if (func->nativefunc) {
        return jass_pushcfunction(j, func->nativefunc);
    } else {
        JASS_ADD_STACK(j, var, jasstype_code);
        var->value = (LPJASSFUNC)func;
        return 1;
    }
}

DWORD jass_pushvalue(LPJASS j, LPCJASSVAR other) {
    LPCJASSTYPE type = other->type;
    LPJASSVAR var = &j->stack[j->num_stack++];
    memset(var, 0, sizeof(*var));
    var->type = type;
    jass_copy(j, var, other);
    return 1;
}

LONG jass_checkinteger(LPJASS j, int index) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    assert_type(var, jasstype_integer);
    return var->value ? *(LONG *)var->value : 0;
}

FLOAT jass_checknumber(LPJASS j, int index) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    if (!var->value) {
        return 0;
    }
    if (jass_checktype(var, jasstype_real)) {
        return *(FLOAT *)var->value;
    }
    if (jass_checktype(var, jasstype_integer)) {
        return *(LONG *)var->value;
    }
    if (jass_checktype(var, jasstype_boolean)) {
        return *(BOOL *)var->value ? 1 : 0;
    }
    return 0.0f;  /* Galaxy: treat unknown numeric type as 0 */
}

BOOL jass_checkboolean(LPJASS j, int index) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    assert_type(var, jasstype_boolean);
    return var->value ? *(BOOL *)var->value : 0;
}

BOOL jass_toboolean(LPJASS j, int index) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    JASSTYPEID type = jass_getvarbasetype(var);
    if (var->value == NULL)
        return false;
    switch (type) {
        case jasstype_integer: return *(LONG *)var->value != 0;
        case jasstype_real: return *(FLOAT *)var->value != 0;
        case jasstype_string: return strlen(var->value) > 0;
        case jasstype_boolean: return *(BOOL *)var->value != 0;
        case jasstype_handle: return true;
        case jasstype_code: return true;
        default: return false;
    }
}

LPCSTR jass_checkstring(LPJASS j, int index) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    /* JASS null is polymorphic.  The VM stores it as a null handle, but Blizzard's
     * cinematic helpers pass null through string parameters; treat that value as
     * the empty string while retaining the type assertion for non-null values. */
    if (jass_getvarbasetype(var) == jasstype_handle && !var->value)
        return "";
    assert_type(var, jasstype_string);
    return var->value;
}

LPCJASSFUNC jass_checkcode(LPJASS j, int index) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    assert_type(var, jasstype_code);
    return var->value;
}

HANDLE jass_checkhandle(LPJASS j, int index, LPCSTR type) {
    LPCJASSVAR var = jass_stackvalue(j, index);
    if (!var->value) {
        return NULL;
    }
    /* Skip type check for Galaxy — SC2 types are unregistered (type == NULL). */
    if (var->type) {
        LPCJASSTYPE expected = find_type(j, type);
        if (expected && !is_handle_convertible(var->type, expected)) {
            fprintf(stderr, "Warning: jass_checkhandle type mismatch\n");
        }
    }
    return var->value;
}

BOOL jass_popboolean(LPJASS j) {
    BOOL value = jass_toboolean(j, -1);
    jass_pop(j, 1);
    return value;
}

static DWORD jass_popinteger(LPJASS j) {
    DWORD value = jass_checkinteger(j, -1);
    jass_pop(j, 1);
    return value;
}

/* =========================================================================
 * Token evaluators — expression evaluation (jass_dotoken)
 * ========================================================================= */

DWORD VM_EvalInteger(LPJASS j, LPCTOKEN token) {
    return jass_pushinteger(j, atoi(token->primary));
}

DWORD VM_EvalReal(LPJASS j, LPCTOKEN token) {
    return jass_pushnumber(j, atof(token->primary));
}

DWORD VM_EvalString(LPJASS j, LPCTOKEN token) {
    return jass_pushstring(j, token->primary);
}

DWORD VM_EvalBoolean(LPJASS j, LPCTOKEN token) {
    return jass_pushboolean(j, jass_atob(token->primary));
}

DWORD VM_EvalIdentifier(LPJASS j, LPCTOKEN token) {
    LPCJASSFUNC f = NULL;
    LPCJASSVAR v = NULL;
    if (token->flags & TF_FUNCTION) {
        if ((f = find_function(j, token->primary))) {
            return jass_pushfunction(j, f);
        } else {
            return jass_pushnull(j);
        }
    } else if ((v = find_global(j, token->primary))) {
        return jass_pushvalue(j, v);
    } else if ((v = find_dict(jass_stackvalue(j, 0)->env.locals, token->primary))) {
        return jass_pushvalue(j, v);
    } else {
        return jass_pushnull(j);
    }
}

/* Resolve every authored dimension through the VM's existing nested sparse-array representation. */
static LPJASSVAR jass_array_value(LPJASS j, LPJASSVAR var, LPCTOKEN token) {
    while (token) {
        /* Evaluate before asserting: release builds must not erase the VM operation. */
        DWORD count = jass_dotoken(j, token->index);
        if (count != 1) jass_pushnull(j);
        var = ensure_array_value(j, var, jass_popinteger(j));
        token = token->body;
    }
    return var;
}

DWORD VM_EvalArrayAccess(LPJASS j, LPCTOKEN token) {
    DWORD count = jass_dotoken(j, token->index);
    if (count != 1) { jass_pushnull(j); }
    DWORD index_val = jass_popinteger(j);
    VM_EvalIdentifier(j, token);
    LPJASSVAR var = jass_stackvalue(j, -1);
    LPJASSVAR item = ensure_array_value(j, var, index_val);
    if (token->body) item = jass_array_value(j, item, token->body);
    jass_pop(j, 1);
    JASSVAR tmp;
    memcpy(&tmp, item, sizeof(JASSVAR));
    jass_pushvalue(j, &tmp);
    return 1;
}

DWORD VM_EvalFourCC(LPJASS j, LPCTOKEN token) {
    return jass_pushinteger(j, *(DWORD *)token->primary);
}

DWORD VM_EvalCall(LPJASS j, LPCTOKEN token) {
    LPCJASSFUNC f = NULL;
    LPJASSCFUNCTION cf = NULL;
    DWORD stacksize = j->num_stack;
    if (!strcmp(token->primary, "CommentString") && token->args) {
        fprintf(stdout, "%s\n", token->args->primary);
        return 0;
    } else if ((f = find_function(j, token->primary))) {
        /* An unresolved native used to execute as an empty JASS function, hiding missing engine behavior. */
        if (f->native && !f->nativefunc) {
            jass_unimplementednative(j, f->name);
            return 0;
        }
        DWORD args = 0;
        jass_pushfunction(j, f);
        FOR_EACH_LIST(TOKEN, arg, token->args) {
            if (!jass_dotoken(j, arg)) {
                jass_pushnull(j);
            }
            args++;
        }
        jass_call(j, args);
        return j->num_stack - stacksize;
    } else if ((cf = find_cfunction(j, token->primary))) {
        DWORD args = 0;
        jass_pushcfunction(j, cf);
        FOR_EACH_LIST(TOKEN, arg, token->args) {
            if (!jass_dotoken(j, arg)) {
                jass_pushnull(j);
            }
            args++;
        }
        jass_call(j, args);
        return j->num_stack - stacksize;
    } else {
        fprintf(stderr, "Can't find function %s\n", token->primary);
        jass_root(j)->rterror_pending = true;
        snprintf(jass_root(j)->rterror_message, sizeof(jass_root(j)->rterror_message),
                 "unknown function: %s", token->primary ? token->primary : "(null)");
        return 0;
    }
}

static struct {
    TOKENTYPE tokentype;
    DWORD (*func)(LPJASS j, LPCTOKEN token);
} vm_token_types[] = {
    { TT_INTEGER,     VM_EvalInteger     },
    { TT_REAL,        VM_EvalReal        },
    { TT_STRING,      VM_EvalString      },
    { TT_BOOLEAN,     VM_EvalBoolean     },
    { TT_IDENTIFIER,  VM_EvalIdentifier  },
    { TT_ARRAYACCESS, VM_EvalArrayAccess },
    { TT_FOURCC,      VM_EvalFourCC      },
    { TT_CALL,        VM_EvalCall        },
};

DWORD jass_dotoken(LPJASS j, LPCTOKEN token) {
    if (!token)
        return 0;
    FOR_LOOP(idx, sizeof(vm_token_types)/sizeof(*vm_token_types)) {
        if (vm_token_types[idx].tokentype == token->type) {
            return vm_token_types[idx].func(j, token);
        }
    }
    fprintf(stderr, "Can't evaluate expression token of type %d\n", token->type); fflush(stderr);
    return 0;
}

/* =========================================================================
 * Statement evaluators
 * ========================================================================= */

static void jass_set_value(LPJASS j, LPJASSVAR dest, LPCTOKEN init) {
    DWORD stack = jass_dotoken(j, init);
    /* Normally an initializer expression yields exactly one value.  Tolerate
     * other counts instead of aborting: a value-returning function whose body
     * fell through, or an unimplemented/void native (returns 0), would
     * otherwise crash the whole VM mid-map.  Assign the top value when one was
     * produced and pop exactly what was pushed so the stack stays balanced. */
    if (stack >= 1) {
        jass_copy(j, dest, j->stack + jass_top(j));
        jass_pop(j, stack);
    }
}

static void jass_set_array_value(LPJASS j, LPJASSVAR dest, LPCTOKEN token, LPCTOKEN init) {
    /* Evaluate before asserting: NDEBUG previously skipped both expressions and copied an unrelated stack value. */
    DWORD count = jass_dotoken(j, token->index);
    if (count != 1) { jass_pushnull(j); }
    DWORD index_val = jass_popinteger(j);
    LPJASSVAR index_dest = ensure_array_value(j, dest, index_val);
    if (token->body) index_dest = jass_array_value(j, index_dest, token->body);
    count = jass_dotoken(j, init);
    if (count != 1) { jass_pushnull(j); }
    jass_copy(j, index_dest, j->stack + jass_top(j));
    jass_pop(j, 1);
}

static LPJASSDICT parse_dict(LPJASS j, LPCTOKEN token) {
    LPJASSDICT item = JASSALLOC(JASSDICT);
    item->value.constant = token->flags & TF_CONSTANT;
    item->value.array = token->flags & TF_ARRAY;
    item->value.type = find_type(j, token->primary);
    item->key = token->secondary;
    if (token->init) {
        jass_set_value(j, &item->value, token->init);
    }
    return item;
}

TOKENFUNC(TOKENS);
TOKENFUNC(SINGLETOKEN);

TOKENFUNC(TYPEDEF) {
    LPJASSTYPE type = JASSALLOC(JASSTYPE);
    type->name = token->primary;
    type->inherit = find_type(j, token->secondary);
    ADD_TO_LIST(type, j->types);
}

BOOL uses_localplayer(LPCTOKEN token) {
    if (!token) return false;
    if (token->type == TT_CALL && token->primary && !strcmp(token->primary, "GetLocalPlayer")) {
        return true;
    }
    FOR_EACH_LIST(TOKEN, arg, token->args) {
        if (uses_localplayer(arg)) {
            return true;
        }
    }
    return false;
}

TOKENFUNC(IF) {
    if (token->condition && uses_localplayer(token->condition)) {
        FOR_LOOP(i, MAX_PLAYERS) {
            currentplayer = jass_getplayerbyindex(i);
            jass_dotoken(j, token->condition);
            if (jass_popboolean(j)) {
                eval_TOKENS(j, token->body);
            }
            currentplayer = NULL;
        }
        /* Galaxy: if-else with localplayer ignored */
    } else while (token) {
        if (!token->condition) {
            eval_TOKENS(j, token->body);
            return;
        }
        jass_dotoken(j, token->condition);
        if (jass_popboolean(j)) {
            eval_TOKENS(j, token->body);
            return;
        }
        token = token->elseblock;
    }
}

TOKENFUNC(SET) {
    LPJASSVAR v = NULL;
    if ((v = find_global(j, token->secondary))) {
        if (token->index) {
            return jass_set_array_value(j, v, token, token->init);
        } else {
            return jass_set_value(j, v, token->init);
        }
    } else if ((v = find_dict(jass_stackvalue(j, 0)->env.locals, token->secondary))) {
        if (token->index) {
            return jass_set_array_value(j, v, token, token->init);
        } else {
            return jass_set_value(j, v, token->init);
        }
    } else {
        fprintf(stderr, "Can't find variable %s\n",
                token->secondary ? token->secondary : "(null)");
    }
}

TOKENFUNC(VARDECL) {
    LPJASSDICT vardecl = parse_dict(j, token);
    ADD_TO_LIST(vardecl, jass_stackvalue(j, 0)->env.locals);
}

TOKENFUNC(GLOBAL) {
    LPJASSDICT global = parse_dict(j, token);
    LPJASSDICT *bucket = &j->global_hash[jass_hash(global->key)];
    ADD_TO_LIST(global, j->globals);
    global->hash_next = *bucket;
    *bucket = global;
}

TOKENFUNC(FUNCTION) {
    LPJASSFUNC func = JASSALLOC(JASSFUNC);
    LPJASSFUNC *bucket;
    func->name = token->primary;
    func->code = token->body;
    func->native = token->flags & TF_NATIVE;
    func->returns = find_type(j, token->secondary);
    FOR_EACH_LIST(TOKEN, arg, token->args) {
        LPJASSARG jarg = JASSALLOC(JASSARG);
        jarg->name = arg->secondary;
        jarg->type = find_type(j, arg->primary);
        PUSH_BACK(JASSARG, jarg, func->args);
    }
    if (token->flags & TF_NATIVE) {
        if (jass_host.natives) {
            LPCJASSMODULE mod = find_in_array(jass_host.natives, sizeof(JASSMODULE), func->name);
            if (mod) func->nativefunc = mod->func;
        }
        if (!func->nativefunc && jass_host.galaxy_natives) {
            LPCJASSMODULE mod = find_in_array(jass_host.galaxy_natives, sizeof(JASSMODULE), func->name);
            if (mod) func->nativefunc = mod->func;
        }
    }
    bucket = &j->function_hash[jass_hash(func->name)];
    ADD_TO_LIST(func, j->functions);
    func->hash_next = *bucket;
    *bucket = func;
}

TOKENFUNC(CALL) {
    jass_discard(j, jass_dotoken(j, token));
}

TOKENFUNC(LOOP) {
    for (DWORD i = 0;; i++) {
        FOR_EACH_LIST(TOKEN const, tok, token->body) {
            if (jass_mustreturn(j) || jass_yielded(j)) {
                return;
            }
            /* Galaxy `break` bubbled up through nested blocks — consume it here. */
            if (jass_stackvalue(j, 0)->env.break_pending) {
                jass_stackvalue(j, 0)->env.break_pending = false;
                return;
            }
            if (tok->type == TT_RETURN) {
                jass_setreturn(j);
                jass_dotoken(j, tok->body);
                return;
            } else if (tok->type == TT_EXITWHEN) {
                jass_dotoken(j, tok->condition);
                if (jass_popboolean(j)) {
                    return;
                }
            } else {
                eval_SINGLETOKEN(j, tok);
                if (jass_yielded(j)) {
                    return;
                }
                /* break_pending set by a nested block — exit loop cleanly. */
                if (jass_stackvalue(j, 0)->env.break_pending) {
                    jass_stackvalue(j, 0)->env.break_pending = false;
                    return;
                }
            }
        }
        assert(i < INF_LOOP_PROTECTION);
    }
}

static struct {
    LPCSTR name;
    TOKENTYPE type;
    void (*func)(LPJASS, LPCTOKEN);
} token_eval[] = {
    TOKENEVAL(TYPEDEF),
    TOKENEVAL(FUNCTION),
    TOKENEVAL(VARDECL),
    TOKENEVAL(GLOBAL),
    TOKENEVAL(CALL),
    TOKENEVAL(IF),
    TOKENEVAL(SET),
    TOKENEVAL(LOOP),
};

TOKENFUNC(SINGLETOKEN) {
    FOR_LOOP(index, sizeof(token_eval) / sizeof(*token_eval)) {
        if (token->type == token_eval[index].type) {
            token_eval[index].func(j, token);
            return;
        }
    }
    fprintf(stderr, "Can't evaluate token of type %d\n", token->type); fflush(stderr);
    /* Don't assert: Galaxy TT_EXITWHEN may appear here via break-in-nested-block. */
}

TOKENFUNC(TOKENS) {
    FOR_EACH_LIST(TOKEN const, tok, token) {
        if (jass_mustreturn(j) || jass_yielded(j)) {
            return;
        } else if (jass_stackvalue(j, 0)->env.break_pending) {
            return;
        } else if (tok->type == TT_RETURN) {
            jass_setreturn(j);
            jass_dotoken(j, tok->body);
        } else if (tok->type == TT_EXITWHEN) {
            /* Galaxy `break` — fire exitwhen condition; if true, signal loop exit. */
            jass_dotoken(j, tok->condition);
            if (jass_popboolean(j)) {
                jass_stackvalue(j, 0)->env.break_pending = true;
                return;
            }
        } else {
            eval_SINGLETOKEN(j, tok);
        }
        if (jass_yielded(j)) {
            return;
        }
    }
}

/* =========================================================================
 * Buffer / file execution
 * ========================================================================= */

static void jass_remove_comments(LPSTR buf) {
    BOOL in_line  = false;
    BOOL in_block = false;
    DWORD quotes  = 0;
    char *src = buf, *dst = buf;
    while (*src) {
        if (!in_line && !in_block) {
            if (*src == '"') { quotes++; *dst++ = *src++; }
            else if (quotes & 1) { *dst++ = *src++; }
            else if (src[0] == '/' && src[1] == '/') { in_line  = true;  src += 2; }
            else if (src[0] == '/' && src[1] == '*') { in_block = true;  src += 2; }
            else { *dst++ = *src++; }
        } else if (in_line  && *src == '\n')                        { in_line  = false; *dst++ = *src++; }
          else if (in_block && src[0] == '*' && src[1] == '/')      { in_block = false; src += 2; }
          else { src++; }
    }
    *dst = '\0';
}

static void jass_remove_bom(LPSTR buf) {
    unsigned char *u = (unsigned char *)buf;
    if (u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) { memmove(buf, buf + 3, strlen(buf + 3) + 1); return; }
    if (u[0] == 0xFF && u[1] == 0xFE)                  { memmove(buf, buf + 2, strlen(buf + 2) + 1); return; }
    if (u[0] == 0xFE && u[1] == 0xFF)                  { memmove(buf, buf + 2, strlen(buf + 2) + 1); }
}

/* Forward declaration — galaxy_preprocess_includes calls jass_dofile_ex. */
BOOL jass_dofile_ex(LPJASS j, LPCSTR fileName, JASSMODE mode);

/* Include-once guard: tracks files already loaded in this VM to prevent
 * re-parsing when multiple files include the same library. */
#define GALAXY_MAX_INCLUDES 256
static char galaxy_loaded[GALAXY_MAX_INCLUDES][512];
static DWORD galaxy_loaded_n;

static BOOL galaxy_already_loaded(LPCSTR path) {
    for (DWORD i = 0; i < galaxy_loaded_n; i++) {
        if (!strcmp(galaxy_loaded[i], path)) return true;
    }
    assert(galaxy_loaded_n < GALAXY_MAX_INCLUDES);
    strlcpy(galaxy_loaded[galaxy_loaded_n++], path, 512);
    return false;
}

void galaxy_loaded_reset(void) {
    galaxy_loaded_n = 0;
}

/* galaxy_preprocess_includes — scan buffer for `include "path"` directives,
 * overwrite each with spaces (preserving newlines for line-number stability),
 * and recursively load the included file before the main buffer is parsed.
 * Each unique path is loaded at most once per VM lifetime. */
static void galaxy_preprocess_includes(LPJASS j, LPSTR buf, JASSMODE mode) {
    LPSTR cur = buf;
    while (*cur) {
        while (*cur && isspace((unsigned char)*cur)) cur++;
        if (strncmp(cur, "include", 7) == 0 &&
            (cur[7] == ' ' || cur[7] == '\t' || cur[7] == '"')) {
            LPSTR line_start = cur;
            cur += 7;
            while (*cur == ' ' || *cur == '\t') cur++;
            if (*cur == '"') {
                cur++;
                LPSTR path_start = cur;
                while (*cur && *cur != '"' && *cur != '\n') cur++;
                if (*cur == '"') {
                    DWORD path_len = (DWORD)(cur - path_start);
                    cur++;
                    char path[512];
                    DWORD copy_len = path_len < 500 ? path_len : 500;
                    memcpy(path, path_start, copy_len);
                    path[copy_len] = '\0';
                    /* Append .galaxy if path has no extension. */
                    LPCSTR slash = strrchr(path, '/');
                    LPCSTR dot   = strrchr(path, '.');
                    if (!dot || (slash && dot < slash)) {
                        strlcat(path, ".galaxy", sizeof(path));
                    }
                    /* Erase directive in place; keep newlines for line numbers. */
                    for (LPSTR p = line_start; p < cur; p++) {
                        if (*p != '\n') *p = ' ';
                    }
                    /* Include-once guard: skip if already loaded. */
                    if (!galaxy_already_loaded(path)) {
                        jass_dofile_ex(j, path, mode);
                        /* Don't let parse errors in included files abort the outer file. */
                        jass_rterror_clear(j);
                    }
                    continue;
                }
            }
        }
        while (*cur && *cur != '\n') cur++;
    }
}

BOOL jass_dobuffer_ex(LPJASS j, LPSTR buffer, JASSMODE mode) {
    jass_remove_comments(buffer);
    jass_remove_bom(buffer);
    if ((DWORD)mode >= sizeof(jass_syntax) / sizeof(jass_syntax[0])) {
        LPJASS root = jass_root(j);
        root->rterror_pending = true;
        snprintf(root->rterror_message, sizeof(root->rterror_message), "unknown syntax mode");
        return false;
    }
    const JASSSYNTAX *syntax = &jass_syntax[mode];
    if (syntax->flags & SYNTAX_INCLUDES)
        galaxy_preprocess_includes(j, buffer, mode);
    PARSER parser = MAKE(PARSER, .buffer = buffer, .start = buffer, .delimiters = syntax->delimiters);
    LPTOKEN program = syntax->parse(&parser);
    if (parser.error) {
        LPJASS root = jass_root(j);
        root->rterror_pending = true;
        snprintf(root->rterror_message, sizeof(root->rterror_message), "parse error");
        return false;
    }
    LPJASSPROGRAM owned = JASSALLOC(JASSPROGRAM);
    owned->tokens = program;
    ADD_TO_LIST(owned, jass_root(j)->programs);
    eval_TOKENS(j, program);
    return !jass_rterror_pending(j);
}

BOOL jass_dobuffer(LPJASS j, LPSTR buffer) {
    return jass_dobuffer_ex(j, buffer, JASS_MODE_JASS);
}

typedef struct {
    DWORD magic, version, identity, globals, coroutines;
} JASSSNAPSHOTHEADER;

static DWORD const jass_snapshot_magic = MAKEFOURCC('J', 'S', 'V', 'M');

/* Snapshot identity hashes immutable parser metadata, never process-local addresses. */
static DWORD jass_snapshot_hashbytes(DWORD hash, LPCVOID data, size_t size) {
    BYTE const *bytes = data;
    while (size--) hash = (hash ^ *bytes++) * 16777619u;
    return hash;
}

static DWORD jass_snapshot_hashstr(DWORD hash, LPCSTR text) {
    DWORD len = text ? (DWORD)strlen(text) : 0;
    hash = jass_snapshot_hashbytes(hash, &len, sizeof(len));
    return len ? jass_snapshot_hashbytes(hash, text, len) : hash;
}

static DWORD jass_snapshot_hashtokens(DWORD hash, LPCTOKEN token) {
    FOR_EACH_LIST(TOKEN const, item, token) {
        hash = jass_snapshot_hashbytes(hash, &item->type, sizeof(item->type));
        hash = jass_snapshot_hashbytes(hash, &item->flags, sizeof(item->flags));
        hash = jass_snapshot_hashstr(hash, item->primary);
        hash = jass_snapshot_hashstr(hash, item->secondary);
        hash = jass_snapshot_hashtokens(hash, item->init);
        hash = jass_snapshot_hashtokens(hash, item->body);
        hash = jass_snapshot_hashtokens(hash, item->args);
        hash = jass_snapshot_hashtokens(hash, item->condition);
        hash = jass_snapshot_hashtokens(hash, item->elseblock);
        hash = jass_snapshot_hashtokens(hash, item->index);
    }
    return hash;
}

static DWORD jass_snapshot_identity(LPCJASS j) {
    DWORD hash = 2166136261u;
    FOR_EACH_LIST(JASSPROGRAM const, program, j->programs) hash = jass_snapshot_hashtokens(hash, program->tokens);
    return hash;
}

DWORD jass_programidentity(LPJASS j) { return jass_snapshot_identity(jass_root(j)); }

static BOOL jass_snapshot_io(JASSSNAPSHOT *snapshot, void *data, size_t size) {
    return size <= UINT32_MAX && snapshot && snapshot->transfer && snapshot->transfer(snapshot->context, data, (DWORD)size);
}

static BOOL jass_snapshot_writestr(JASSSNAPSHOT *snapshot, LPCSTR text) {
    DWORD len = text ? (DWORD)strlen(text) + 1 : 0;
    return jass_snapshot_io(snapshot, &len, sizeof(len)) && (!len || jass_snapshot_io(snapshot, (void *)text, len));
}

static BOOL jass_snapshot_readstr(JASSSNAPSHOT *snapshot, LPSTR *text) {
    DWORD len;
    LPSTR value = NULL;
    if (!jass_snapshot_io(snapshot, &len, sizeof(len)) || len > BZ_JASS_SNAPSHOT_MAX_STRING) return false;
    if (len) {
        value = jass_alloc(len);
        if (!value || !jass_snapshot_io(snapshot, value, len) || value[len - 1]) { SAFE_DELETE(value, jass_free); return false; }
    }
    *text = value;
    return true;
}

static DWORD jass_snapshot_arraycount(LPCJASSARRAY array) {
    DWORD count = 0;
    FOR_EACH_LIST(JASSARRAY const, item, array) count++;
    return count;
}

typedef enum {
    JASS_SNAPSHOT_HANDLE_NULL = 0,
    JASS_SNAPSHOT_HANDLE_VALUE = 1,
    JASS_SNAPSHOT_HANDLE_HOST,
    JASS_SNAPSHOT_HANDLE_OWNED,
    JASS_SNAPSHOT_HANDLE_FUNCTION,
} jassSnapshotHandleType_t;

typedef struct jass_snapshot_handle_s {
    struct jass_snapshot_handle_s *next;
    DWORD id, size;
    LPCSTR type;
    HANDLE value;
    LPJASSREF ref;
} jassSnapshotHandle_t;

static BOOL jass_snapshot_ownedhandle(LPCSTR type) {
    static LPCSTR const types[] = {
        "sound", "camerasetup", "rect", "location", "force", "gamecache", "region", "fogmodifier",
        "version", "itemtype", "attacktype", "damagetype", "weapontype", "soundtype", "pathingtype",
        "mousebuttontype", "aidifficulty", "playerscore"
    };
    FOR_LOOP(i, sizeof(types) / sizeof(*types)) if (!strcmp(type, types[i])) return true;
    return false;
}

static BOOL jass_snapshot_functionhandle(LPCSTR type) {
    static LPCSTR const types[] = { "boolexpr", "conditionfunc", "filterfunc" };
    FOR_LOOP(i, sizeof(types) / sizeof(*types)) if (!strcmp(type, types[i])) return true;
    return false;
}

static jassSnapshotHandle_t *jass_snapshot_findhandle(JASSSNAPSHOT *snapshot, DWORD id) {
    jassSnapshotHandle_t *handles = snapshot->handles;
    FOR_EACH_LIST(jassSnapshotHandle_t, item, handles) if (item->id == id) return item;
    return NULL;
}

static void jass_snapshot_freehandles(JASSSNAPSHOT *snapshot) {
    while (snapshot->handles) {
        jassSnapshotHandle_t *item = snapshot->handles;
        snapshot->handles = item->next;
        jass_free(item);
    }
}

/* Handles use explicit encodings: native IDs relocate through the host, while safe VM-owned payloads carry identity+bytes. */
static BOOL jass_snapshot_writehandle(LPJASS j, JASSSNAPSHOT *snapshot, LPCJASSVAR var) {
    DWORD encoding, id;
    (void)j;
    if (jass_valuehandle(var->type->name)) {
        encoding = JASS_SNAPSHOT_HANDLE_VALUE;
        return jass_snapshot_io(snapshot, &encoding, sizeof(encoding)) &&
            jass_snapshot_io(snapshot, var->value, sizeof(DWORD));
    }
    /* VM-owned and function handles must bypass the host: a host miss means stale only for host-owned domains. */
    if (jass_snapshot_ownedhandle(var->type->name) && var->ref && var->ref->size) {
        encoding = JASS_SNAPSHOT_HANDLE_OWNED;
        return jass_snapshot_io(snapshot, &encoding, sizeof(encoding)) &&
            jass_snapshot_io(snapshot, &var->ref->id, sizeof(var->ref->id)) &&
            jass_snapshot_io(snapshot, &var->ref->size, sizeof(var->ref->size)) &&
            jass_snapshot_io(snapshot, var->value, var->ref->size);
    }
    if (jass_snapshot_functionhandle(var->type->name)) {
        encoding = JASS_SNAPSHOT_HANDLE_FUNCTION;
        return jass_snapshot_io(snapshot, &encoding, sizeof(encoding)) &&
            jass_snapshot_writestr(snapshot, jass_functionname(var->value));
    }
    if (jass_host.SaveHandle) {
        if (jass_host.SaveHandle(var->type->name, var->value, &id)) {
            encoding = JASS_SNAPSHOT_HANDLE_HOST;
            return jass_snapshot_io(snapshot, &encoding, sizeof(encoding)) && jass_snapshot_io(snapshot, &id, sizeof(id));
        }
        /* Host owns this type but the handle is stale (freed unit/item/etc.) — save as null. */
        encoding = JASS_SNAPSHOT_HANDLE_NULL;
        return jass_snapshot_io(snapshot, &encoding, sizeof(encoding));
    }
    fprintf(stderr, "JASS snapshot: cannot encode %s handle\n", var->type->name);
    return false;
}

static BOOL jass_snapshot_readhandle(LPJASS j, JASSSNAPSHOT *snapshot, LPJASSVAR var) {
    DWORD encoding, id, size;
    HANDLE value;
    LPSTR name = NULL;
    if (!jass_snapshot_io(snapshot, &encoding, sizeof(encoding))) return false;
    if (encoding == JASS_SNAPSHOT_HANDLE_NULL) {
        var->value = NULL;
        return true;
    }
    if (encoding == JASS_SNAPSHOT_HANDLE_VALUE) {
        if (!jass_valuehandle(var->type->name) || !(var->value = jass_alloc(sizeof(DWORD))) ||
            !jass_snapshot_io(snapshot, var->value, sizeof(DWORD))) return false;
        var->ref = jass_alloc(sizeof(*var->ref));
        if (!var->ref) return false;
        *var->ref = (JASSREF){ .size = sizeof(DWORD) };
        return true;
    }
    if (encoding == JASS_SNAPSHOT_HANDLE_HOST) {
        if (!jass_snapshot_io(snapshot, &id, sizeof(id))) return false;
        if (!jass_host.LoadHandle || !(value = jass_host.LoadHandle(var->type->name, id))) {
            fprintf(stderr, "JASS snapshot: cannot resolve %s handle %u\n", var->type->name, id);
            return false;
        }
        var->ref = jass_alloc(sizeof(*var->ref));
        if (!var->ref) return false;
        var->value = value; *var->ref = (JASSREF){ .refs = 1 };
        return true;
    }
    if (encoding == JASS_SNAPSHOT_HANDLE_FUNCTION) {
        if (!jass_snapshot_functionhandle(var->type->name) || !jass_snapshot_readstr(snapshot, &name) || !name ||
            !(var->value = (HANDLE)find_function(j, name))) { SAFE_DELETE(name, jass_free); return false; }
        SAFE_DELETE(name, jass_free);
        var->ref = jass_alloc(sizeof(*var->ref));
        if (!var->ref) return false;
        *var->ref = (JASSREF){ .refs = 1 };
        return true;
    }
    if (encoding != JASS_SNAPSHOT_HANDLE_OWNED || !jass_snapshot_ownedhandle(var->type->name) ||
        !jass_snapshot_io(snapshot, &id, sizeof(id)) || !id ||
        !jass_snapshot_io(snapshot, &size, sizeof(size)) || !size || size > BZ_JASS_SNAPSHOT_MAX_STRING) return false;
    jassSnapshotHandle_t *item = jass_snapshot_findhandle(snapshot, id);
    if (item) {
        if (item->size != size || strcmp(item->type, var->type->name) ||
            !jass_snapshot_io(snapshot, item->value, size)) return false;
        var->value = item->value; var->ref = item->ref; var->ref->refs++;
        return true;
    }
    item = jass_alloc(sizeof(*item));
    if (!item || !(item->value = jass_alloc(size)) || !(item->ref = jass_alloc(sizeof(*item->ref))) ||
        !jass_snapshot_io(snapshot, item->value, size)) {
        if (item) { SAFE_DELETE(item->value, jass_free); SAFE_DELETE(item->ref, jass_free); jass_free(item); }
        return false;
    }
    item->id = id; item->size = size; item->type = var->type->name;
    *item->ref = (JASSREF){ .size = size, .id = id };
    ADD_TO_LIST(item, snapshot->handles);
    var->value = item->value; var->ref = item->ref;
    jass_root(j)->next_handle_id = MAX(jass_root(j)->next_handle_id, id);
    return true;
}

/* Values carry their declared type so changed scripts and corrupt tags reject before mutation. */
static BOOL jass_snapshot_writevar(LPJASS j, JASSSNAPSHOT *snapshot, LPCJASSVAR var) {
    DWORD present = var->value || var->_array, count = jass_snapshot_arraycount(var->_array);
    JASSTYPEID base;
    if (!var->type) { fprintf(stderr, "JASS snapshot: value has no declared type\n"); return false; }
    base = jass_getvarbasetype(var);
    if (!jass_snapshot_writestr(snapshot, var->type ? var->type->name : NULL) ||
        !jass_snapshot_io(snapshot, &present, sizeof(present)) ||
        !jass_snapshot_io(snapshot, &count, sizeof(count))) return false;
    FOR_EACH_LIST(JASSARRAY const, item, var->_array)
        if (!jass_snapshot_io(snapshot, (void *)&item->index, sizeof(item->index)) ||
            !jass_snapshot_writevar(j, snapshot, &item->value)) return false;
    if (!present || count) return true;
    switch (base) {
    case jasstype_integer: return jass_snapshot_io(snapshot, var->value, sizeof(LONG));
    case jasstype_real: return jass_snapshot_io(snapshot, var->value, sizeof(FLOAT));
    case jasstype_boolean: return jass_snapshot_io(snapshot, var->value, sizeof(BOOL));
    case jasstype_string: return jass_snapshot_writestr(snapshot, var->value);
    case jasstype_code: return jass_snapshot_writestr(snapshot, jass_functionname(var->value));
    case jasstype_handle: return jass_snapshot_writehandle(j, snapshot, var);
    default: fprintf(stderr, "JASS snapshot: unsupported value type %s\n", var->type->name); return false;
    }
}

static BOOL jass_snapshot_readvar(LPJASS j, JASSSNAPSHOT *snapshot, LPJASSVAR var) {
    LPSTR type = NULL, text = NULL;
    DWORD present, count;
    LPCJASSTYPE declared;
    if (!jass_snapshot_readstr(snapshot, &type) || !type || !(declared = find_type(j, type)) ||
        !jass_snapshot_io(snapshot, &present, sizeof(present)) || present > 1 ||
        !jass_snapshot_io(snapshot, &count, sizeof(count)) || count > BZ_JASS_SNAPSHOT_MAX_COUNT) {
        SAFE_DELETE(type, jass_free); return false;
    }
    SAFE_DELETE(type, jass_free);
    var->type = declared;
    FOR_LOOP(i, count) {
        DWORD index;
        if (!jass_snapshot_io(snapshot, &index, sizeof(index)) ||
            !jass_snapshot_readvar(j, snapshot, ensure_array_value(j, var, index))) return false;
    }
    if (!present || count) return present == !!count;
    switch (jass_getvarbasetype(var)) {
    case jasstype_integer: return (var->value = jass_alloc(sizeof(LONG))) && jass_snapshot_io(snapshot, var->value, sizeof(LONG));
    case jasstype_real: return (var->value = jass_alloc(sizeof(FLOAT))) && jass_snapshot_io(snapshot, var->value, sizeof(FLOAT));
    case jasstype_boolean: return (var->value = jass_alloc(sizeof(BOOL))) && jass_snapshot_io(snapshot, var->value, sizeof(BOOL));
    case jasstype_string:
        if (!jass_snapshot_readstr(snapshot, &text) || !text) return false;
        var->value = text; return true;
    case jasstype_code:
        if (!jass_snapshot_readstr(snapshot, &text) || !text) return false;
        var->value = (HANDLE)find_function(j, text); SAFE_DELETE(text, jass_free); return var->value != NULL;
    case jasstype_handle: return jass_snapshot_readhandle(j, snapshot, var);
    default: fprintf(stderr, "JASS snapshot: unsupported value type %s\n", var->type->name); return false;
    }
}

static DWORD jass_snapshot_globalcount(LPCJASS j) {
    DWORD count = 0;
    FOR_EACH_LIST(JASSDICT const, item, j->globals) if (!item->value.constant) count++;
    return count;
}

static DWORD jass_snapshot_coroutinecount(LPCJASS j) {
    DWORD count = 0;
    FOR_EACH_LIST(JASSCOROUTINE const, co, j->coroutines) if (!co->done) count++;
    return count;
}

static BOOL jass_snapshot_findtoken(LPCTOKEN token, LPCTOKEN wanted, DWORD *ordinal, DWORD *found) {
    FOR_EACH_LIST(TOKEN const, item, token) {
        DWORD current = (*ordinal)++;
        if (item == wanted) { *found = current; return true; }
        if (jass_snapshot_findtoken(item->init, wanted, ordinal, found) ||
            jass_snapshot_findtoken(item->body, wanted, ordinal, found) ||
            jass_snapshot_findtoken(item->args, wanted, ordinal, found) ||
            jass_snapshot_findtoken(item->condition, wanted, ordinal, found) ||
            jass_snapshot_findtoken(item->elseblock, wanted, ordinal, found) ||
            jass_snapshot_findtoken(item->index, wanted, ordinal, found)) return true;
    }
    return false;
}

static DWORD jass_snapshot_tokenid(LPCJASS j, LPCTOKEN wanted) {
    DWORD ordinal = 0, found = UINT32_MAX;
    if (!wanted) return UINT32_MAX;
    FOR_EACH_LIST(JASSPROGRAM const, program, j->programs)
        if (jass_snapshot_findtoken(program->tokens, wanted, &ordinal, &found)) return found;
    return UINT32_MAX;
}

static LPCTOKEN jass_snapshot_gettoken(LPCTOKEN token, DWORD wanted, DWORD *ordinal) {
    FOR_EACH_LIST(TOKEN const, item, token) {
        if ((*ordinal)++ == wanted) return item;
        LPCTOKEN found = jass_snapshot_gettoken(item->init, wanted, ordinal);
        if (!found) found = jass_snapshot_gettoken(item->body, wanted, ordinal);
        if (!found) found = jass_snapshot_gettoken(item->args, wanted, ordinal);
        if (!found) found = jass_snapshot_gettoken(item->condition, wanted, ordinal);
        if (!found) found = jass_snapshot_gettoken(item->elseblock, wanted, ordinal);
        if (!found) found = jass_snapshot_gettoken(item->index, wanted, ordinal);
        if (found) return found;
    }
    return NULL;
}

static LPCTOKEN jass_snapshot_token(LPCJASS j, DWORD wanted) {
    DWORD ordinal = 0;
    if (wanted == UINT32_MAX) return NULL;
    FOR_EACH_LIST(JASSPROGRAM const, program, j->programs) {
        LPCTOKEN found = jass_snapshot_gettoken(program->tokens, wanted, &ordinal);
        if (found) return found;
    }
    return NULL;
}

static DWORD jass_snapshot_dictcount(LPCJASSDICT dict) {
    DWORD count = 0;
    FOR_EACH_LIST(JASSDICT const, item, dict) count++;
    return count;
}

static BOOL jass_snapshot_writedict(LPJASS j, JASSSNAPSHOT *snapshot, LPCJASSDICT dict) {
    DWORD count = jass_snapshot_dictcount(dict);
    if (!jass_snapshot_io(snapshot, &count, sizeof(count))) return false;
    FOR_EACH_LIST(JASSDICT const, item, dict)
        if (!jass_snapshot_writestr(snapshot, item->key) || !jass_snapshot_writevar(j, snapshot, &item->value)) return false;
    return true;
}

static BOOL jass_snapshot_readdict(LPJASS j, JASSSNAPSHOT *snapshot, LPJASSDICT *dict) {
    DWORD count;
    if (!jass_snapshot_io(snapshot, &count, sizeof(count)) || count > BZ_JASS_SNAPSHOT_MAX_COUNT) return false;
    FOR_LOOP(i, count) {
        LPJASSDICT item = JASSALLOC(JASSDICT);
        LPSTR name = NULL;
        if (!jass_snapshot_readstr(snapshot, &name) || !name || find_dict(*dict, name)) {
            SAFE_DELETE(name, jass_free); jass_free(item); return false;
        }
        item->key = name;
        if (!jass_snapshot_readvar(j, snapshot, &item->value)) { jass_deletedict(item); return false; }
        PUSH_BACK(JASSDICT, item, *dict);
    }
    return true;
}

static BOOL jass_snapshot_writecontext(JASSSNAPSHOT *snapshot, LPCJASSCONTEXT context) {
    struct { LPCSTR type; HANDLE value; } handles[] = {
        { "trigger", context->trigger }, { "unit", context->unit }, { "unit", context->source },
        { "player", context->playerState }, { "player", context->localPlayerState }, { "timer", context->timer },
    };
    if (!jass_snapshot_writestr(snapshot, jass_functionname(context->func))) return false;
    FOR_LOOP(i, sizeof(handles) / sizeof(*handles)) {
        DWORD present = handles[i].value != NULL, id = 0;
        if (!jass_snapshot_io(snapshot, &present, sizeof(present))) return false;
        if (present && (!jass_host.SaveHandle || !jass_host.SaveHandle(handles[i].type, handles[i].value, &id) ||
            !jass_snapshot_io(snapshot, &id, sizeof(id)))) return false;
    }
    return true;
}

static BOOL jass_snapshot_readcontext(LPJASS j, JASSSNAPSHOT *snapshot, LPJASSCONTEXT context) {
    struct { LPCSTR type; HANDLE *value; } handles[] = {
        { "trigger", (HANDLE *)&context->trigger }, { "unit", (HANDLE *)&context->unit },
        { "unit", (HANDLE *)&context->source }, { "player", (HANDLE *)&context->playerState },
        { "player", (HANDLE *)&context->localPlayerState }, { "timer", &context->timer },
    };
    LPSTR func = NULL;
    BOOL has_func;
    if (!jass_snapshot_readstr(snapshot, &func)) return false;
    has_func = func != NULL;
    context->func = func ? find_function(j, func) : NULL;
    SAFE_DELETE(func, jass_free);
    if (has_func && !context->func) return false;
    FOR_LOOP(i, sizeof(handles) / sizeof(*handles)) {
        DWORD present, id;
        if (!jass_snapshot_io(snapshot, &present, sizeof(present)) || present > 1) return false;
        if (present && (!jass_snapshot_io(snapshot, &id, sizeof(id)) || !jass_host.LoadHandle ||
            !(*handles[i].value = jass_host.LoadHandle(handles[i].type, id)))) return false;
    }
    return true;
}

static BOOL jass_snapshot_writecoroutines(LPJASS j, JASSSNAPSHOT *snapshot) {
    DWORD count = jass_snapshot_coroutinecount(j), now = jass_gettime();
    if (!jass_snapshot_io(snapshot, &count, sizeof(count))) return false;
    FOR_EACH_LIST(JASSCOROUTINE const, co, j->coroutines) {
        DWORD frames = 0, remaining = co->wake_time > now ? co->wake_time - now : 0;
        if (co->done) continue;
        FOR_EACH_LIST(JASSCOROUTINEFRAME const, frame, co->frames) frames++;
        if (!jass_snapshot_writecontext(snapshot, &co->state->context) ||
            !jass_snapshot_io(snapshot, &remaining, sizeof(remaining)) ||
            !jass_snapshot_io(snapshot, &frames, sizeof(frames))) return false;
        FOR_EACH_LIST(JASSCOROUTINEFRAME const, frame, co->frames) {
            DWORD body = jass_snapshot_tokenid(j, frame->body), pc = jass_snapshot_tokenid(j, frame->pc);
            if ((frame->body && body == UINT32_MAX) || (frame->pc && pc == UINT32_MAX) ||
                !jass_snapshot_io(snapshot, (void *)&frame->type, sizeof(frame->type)) ||
                !jass_snapshot_writestr(snapshot, jass_functionname(frame->func)) ||
                !jass_snapshot_io(snapshot, &body, sizeof(body)) || !jass_snapshot_io(snapshot, &pc, sizeof(pc)) ||
                !jass_snapshot_io(snapshot, (void *)&frame->loop_count, sizeof(frame->loop_count)) ||
                !jass_snapshot_writedict(j, snapshot, frame->locals)) return false;
        }
    }
    return true;
}

static BOOL jass_snapshot_readcoroutines(LPJASS j, JASSSNAPSHOT *snapshot, LPJASSCOROUTINE *list) {
    DWORD count, now = jass_gettime();
    if (!jass_snapshot_io(snapshot, &count, sizeof(count)) || count > BZ_JASS_SNAPSHOT_MAX_COUNT) return false;
    FOR_LOOP(i, count) {
        LPJASS state = JASSALLOC(JASS);
        LPJASSCOROUTINE co = JASSALLOC(JASSCOROUTINE);
        LPJASSCOROUTINEFRAME *tail = &co->frames;
        DWORD remaining, frames;
        memcpy(state, j, sizeof(*state));
        memset(state->stack, 0, sizeof(state->stack));
        memset(&state->context, 0, sizeof(state->context));
        state->stack_pointer = state->stack; state->num_stack = 0; state->root = j;
        state->coroutines = NULL; state->current_coroutine = NULL;
        co->state = state; co->wake_time = now; co->yielded = true;
        if (!jass_snapshot_readcontext(j, snapshot, &state->context) ||
            !jass_snapshot_io(snapshot, &remaining, sizeof(remaining)) ||
            !jass_snapshot_io(snapshot, &frames, sizeof(frames)) || !frames || frames > BZ_JASS_SNAPSHOT_MAX_COUNT) {
            jass_free_coroutine(co); return false;
        }
        co->wake_time += remaining;
        FOR_LOOP(k, frames) {
            LPJASSCOROUTINEFRAME frame = JASSALLOC(JASSCOROUTINEFRAME);
            LPSTR func = NULL;
            DWORD body, pc;
            memset(frame, 0, sizeof(*frame));
            if (!jass_snapshot_io(snapshot, &frame->type, sizeof(frame->type)) || frame->type > JASS_FRAME_LOOP ||
                !jass_snapshot_readstr(snapshot, &func) ||
                !jass_snapshot_io(snapshot, &body, sizeof(body)) || !jass_snapshot_io(snapshot, &pc, sizeof(pc)) ||
                !jass_snapshot_io(snapshot, &frame->loop_count, sizeof(frame->loop_count)) ||
                !jass_snapshot_readdict(j, snapshot, &frame->locals)) {
                SAFE_DELETE(func, jass_free); jass_free(frame); jass_free_coroutine(co); return false;
            }
            frame->func = func ? find_function(j, func) : NULL;
            SAFE_DELETE(func, jass_free);
            frame->body = jass_snapshot_token(j, body); frame->pc = jass_snapshot_token(j, pc);
            if ((body != UINT32_MAX && !frame->body) || (pc != UINT32_MAX && !frame->pc) ||
                (frame->type == JASS_FRAME_FUNCTION && !frame->func)) {
                jass_free_frame(co, frame); jass_free_coroutine(co); return false;
            }
            *tail = frame; tail = &frame->next;
        }
        PUSH_BACK(JASSCOROUTINE, co, *list);
    }
    return true;
}

BOOL jass_writesnapshot(LPJASS j, JASSSNAPSHOT *snapshot) {
    LPJASS root = jass_root(j);
    JASSSNAPSHOTHEADER header = {
        jass_snapshot_magic, BZ_JASS_SNAPSHOT_VERSION, jass_snapshot_identity(root), jass_snapshot_globalcount(root),
        jass_snapshot_coroutinecount(root)
    };
    if (root->current_coroutine || root->sync_rterror_jmp_set) {
        fprintf(stderr, "JASS snapshot: save requested inside an active VM frame\n"); return false;
    }
    if (!jass_snapshot_io(snapshot, &header, sizeof(header))) return false;
    FOR_EACH_LIST(JASSDICT const, item, root->globals)
        if (!item->value.constant && (!jass_snapshot_writestr(snapshot, item->key) ||
            !jass_snapshot_writevar(root, snapshot, &item->value))) {
            fprintf(stderr, "JASS snapshot: global '%s' could not be saved\n", item->key);
            return false;
        }
    return jass_snapshot_writecoroutines(root, snapshot);
}

BOOL jass_readsnapshot(LPJASS j, JASSSNAPSHOT *snapshot) {
    LPJASS root = jass_root(j);
    JASSSNAPSHOTHEADER header;
    LPJASSDICT staged = NULL;
    LPJASSCOROUTINE coroutines = NULL;
    BOOL ok = false;
    if (root->current_coroutine || root->sync_rterror_jmp_set || !jass_snapshot_io(snapshot, &header, sizeof(header)) ||
        header.magic != jass_snapshot_magic || header.version != BZ_JASS_SNAPSHOT_VERSION ||
        header.identity != jass_snapshot_identity(root) || header.globals != jass_snapshot_globalcount(root) ||
        header.globals > BZ_JASS_SNAPSHOT_MAX_COUNT || header.coroutines > BZ_JASS_SNAPSHOT_MAX_COUNT) return false;
    FOR_LOOP(i, header.globals) {
        LPSTR name = NULL;
        LPJASSDICT item = NULL;
        LPJASSVAR live;
        if (!jass_snapshot_readstr(snapshot, &name) || !name || find_dict(staged, name) ||
            !(live = find_global(root, name)) || live->constant) { SAFE_DELETE(name, jass_free); goto done; }
        item = JASSALLOC(JASSDICT);
        item->key = name;
        if (!jass_snapshot_readvar(root, snapshot, &item->value)) { jass_deletedict(item); goto done; }
        ADD_TO_LIST(item, staged);
    }
    if (!jass_snapshot_readcoroutines(root, snapshot, &coroutines) ||
        header.coroutines != jass_snapshot_coroutinecount(&(JASS){ .coroutines = coroutines })) goto done;
    FOR_EACH_LIST(JASSDICT, item, staged) {
        LPJASSVAR live = find_global(root, item->key);
        if (live->type != item->value.type) goto done;
    }
    FOR_EACH_LIST(JASSDICT, item, staged) jass_copy(root, find_global(root, item->key), &item->value);
    while (root->coroutines) {
        LPJASSCOROUTINE next = root->coroutines->next;
        jass_free_coroutine(root->coroutines); root->coroutines = next;
    }
    root->coroutines = coroutines; coroutines = NULL;
    ok = true;
done:
    SAFE_DELETE(staged, jass_deletedict);
    while (coroutines) { LPJASSCOROUTINE next = coroutines->next; jass_free_coroutine(coroutines); coroutines = next; }
    jass_snapshot_freehandles(snapshot);
    return ok;
}

LPJASS jass_newstate(void) {
    LPJASS j = JASSALLOC(JASS);
    j->stack_pointer = j->stack;
    j->root = j;
    galaxy_loaded_reset(); /* each new VM session starts with a fresh include-once guard */
    return j;
}

void jass_close(LPJASS j) {
    LPJASS root = jass_root(j);
    LPJASSCOROUTINE co = root->coroutines;
    while (co) {
        LPJASSCOROUTINE next = co->next;
        jass_free_coroutine(co);
        co = next;
    }
    FOR_LOOP(i, root->num_stack) jass_setnull(root->stack + i);
    SAFE_DELETE(root->globals, jass_deletedict);
    while (root->functions) {
        LPJASSFUNC func = root->functions, next = func->next;
        DELETE_LIST(JASSARG, func->args, jass_free);
        jass_free(func);
        root->functions = next;
    }
    DELETE_LIST(JASSTYPE, root->types, jass_free);
    while (root->programs) {
        LPJASSPROGRAM program = root->programs, next = program->next;
        JASS_FreeTokens(program->tokens);
        jass_free(program);
        root->programs = next;
    }
    jass_free(root);
}

BOOL jass_dofile_ex(LPJASS j, LPCSTR fileName, JASSMODE mode) {
    DWORD size = 0;
    LPSTR buffer = jass_host.ReadFile(fileName, &size);
    if (buffer) {
        LPSTR nul_terminated = jass_alloc(size + 1);
        memcpy(nul_terminated, buffer, size);
        nul_terminated[size] = '\0';
        jass_free(buffer);
        BOOL success = jass_dobuffer_ex(j, nul_terminated, mode);
        jass_free(nul_terminated);
        return success;
    } else {
        return false;
    }
}

BOOL jass_dofile(LPJASS j, LPCSTR fileName) {
    /* Auto-detect Galaxy mode from file extension. */
    LPCSTR dot = strrchr(fileName, '.');
    JASSMODE mode = (dot && !strcmp(dot, ".galaxy")) ? JASS_MODE_GALAXY : JASS_MODE_JASS;
    return jass_dofile_ex(j, fileName, mode);
}

/* =========================================================================
 * jass_call — invoke function on stack
 * ========================================================================= */

#ifdef DEBUG_JASS
static int depth = 0, callnum = 0;
#endif

static DWORD jass_call_impl(LPJASS j, DWORD args) {
    LPJASSVAR root = &j->stack[j->num_stack - args - 1];
    LPJASSVAR old_stack_pointer = j->stack_pointer;
    DWORD ret = 0;
    j->stack_pointer = &j->stack[j->num_stack - args - 1];
#ifdef DEBUG_JASS
    callnum++;
    depth++;
    FOR_LOOP(i, depth) printf(" ");
#endif
    if (jass_getvarbasetype(root) == jasstype_cfunction) {
        LPJASSCFUNCTION func = *(LPJASSCFUNCTION *)root->value;
#ifdef DEBUG_JASS
        for (DWORD i = 0; jass_host.natives[i].name; i++) {
            if (jass_host.natives[i].func == func) {
                printf("%s (native)", jass_host.natives[i].name);
                break;
            }
        }
        for (DWORD i = 0; jass_operators[i].name; i++) {
            if (jass_operators[i].func == func) {
                printf("%s (native)", jass_operators[i].name);
                break;
            }
        }
        printf("\n");
#endif
        ret = func(j);
    } else {
        LPCJASSFUNC func = root->value;
        LPJASSDICT locals = NULL;
        DWORD argnum = 1;
#ifdef DEBUG_JASS
        printf("%s\n", func->name);
#endif
        FOR_EACH_LIST(JASSARG, arg, func->args) {
            LPJASSDICT local = JASSALLOC(JASSDICT);
            local->key = arg->name;
            local->value.type = arg->type;
            jass_copy(j, &local->value, &j->stack_pointer[argnum]);
            PUSH_BACK(JASSDICT, local, locals);
            argnum++;
        }
        root->env.done = false;
        root->env.returnstack = -1;
        root->env.locals = locals;
        eval_TOKENS(j, func->code);
        if (root->env.returnstack != -1) {
            ret = j->num_stack - root->env.returnstack;
        }
    }
    LPJASSVAR last = &j->stack[j->num_stack - ret];
    for (LPJASSVAR it = root; it < last; it++) jass_setnull(it);
    memmove(root, last, ret * sizeof(JASSVAR));
    j->num_stack -= last - root;
    j->stack_pointer = old_stack_pointer;
#ifdef DEBUG_JASS
    depth--;
#endif
    return ret;
}

/* Synchronous native failures unwind to the outer call instead of executing later script statements. */
DWORD jass_call(LPJASS j, DWORD args) {
    LPJASS root = jass_root(j);
    LPJASSVAR stack_pointer = j->stack_pointer;
    DWORD stack_base = j->num_stack - args - 1;
    DWORD ret;

    if (root->current_coroutine || root->sync_rterror_jmp_set) return jass_call_impl(j, args);
    root->sync_rterror_jmp_set = true;
    if (setjmp(root->sync_rterror_jmp) != 0) {
        root->sync_rterror_jmp_set = false;
        jass_discard(j, j->num_stack - stack_base);
        j->stack_pointer = stack_pointer;
        return 0;
    }
    ret = jass_call_impl(j, args);
    root->sync_rterror_jmp_set = false;
    return ret;
}

void jass_callbyname(LPJASS j, LPCSTR name, BOOL spawn_coroutine) {
    LPCJASSFUNC func = find_function(j, name);
    if (!func) {
        fprintf(stderr, "Function not found %s\n", name);
        return;
    }
    if (spawn_coroutine) {
        (void)jass_startcoroutinebyname(j, name);
    } else {
        jass_pushfunction(j, func);
        jass_call(j, 0);
    }
}

#undef TOKENFUNC
#undef TOKENEVAL
