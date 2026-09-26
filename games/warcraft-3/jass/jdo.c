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
#define BZ_JASS_SNAPSHOT_VERSION 6 // format version; persists region trigger context
#define BZ_JASS_SNAPSHOT_MAX_COUNT (1u << 20) // records; bounds allocations and list walks from corrupt snapshots
#define BZ_JASS_SNAPSHOT_MAX_STRING (1u << 20) // bytes; bounds strings from corrupt snapshots

typedef struct {
    trigger_t * trigger;
    edict_t * unit;
    edict_t * source;
    int32_t value;
    vector2_t const * point;
    bool has_point;
    handle_t timer;
    handle_t region;
    bool timer_pending;
} jassTriggerContextParams_t;

#define assert_type(var, type) do { if (!jass_checktype(var, type)) jass_rterror(j, "invalid native argument: expected " #type); } while (0)
#define JASSALLOC(type) jass_alloc(sizeof(type))
#define BZ_JASS_REQUIRE_STACK(j) if (j->num_stack >= MAX_JASS_STACK) jass_rterror(j, "stack overflow")

static void jass_setnull(jassVar_t * var);
static void jass_deletedict(jassdict_t * dict);

#define JASS_ADD_STACK(j, VAR, TYPE) \
BZ_JASS_REQUIRE_STACK(j); \
jassVar_t * VAR = &j->stack[j->num_stack++]; \
memset(VAR, 0, sizeof(*VAR)); \
VAR->type = &jass_types[TYPE];

static void jass_store_value(jassVar_t * var, void const * value, uint32_t size) {
    jass_setnull(var);
    if (!value) {
        return;
    }
    var->value = jass_alloc(size);
    memcpy(var->value, value, size);
}

#define JASS_CMPOP(NAME, OP) \
uint32_t NAME(jass_t * j) { \
    return jass_pushboolean(j, jass_checknumber(j, 1) OP jass_checknumber(j, 2)); \
}

#define JASS_NUMOP(NAME, OP) \
uint32_t NAME(jass_t * j) { \
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
#define TOKENFUNC(NAME) void eval_##NAME(jass_t * j, token_t const * token)
#define TOKENEVAL(NAME) { #NAME, TT_##NAME, eval_##NAME }

player_t * currentplayer = NULL;
edict_t * currentunit = NULL;
player_t * currentenumplayer = NULL;
static handle_t currenttimer = NULL;

cstring_t keywords[] = {
    "elseif", "else", "endif", "set", "endfunction", "local", "then", NULL
};

static jassHost_t jass_host;

typedef struct {
    cstring_t delimiters;
    token_t * (*parse)(wordExtractor_t * p);
    uint32_t flags;
} jassSyntax_t;

static const jassSyntax_t jass_syntax[] = {
    [JASS_MODE_JASS] = { ",;()[]+-/*=<>!", JASS_ParseTokens, 0 },
    [JASS_MODE_GALAXY] = { ",;()[]+-/*={}!<>&|", GALAXY_ParseTokens, SYNTAX_C_OPERATORS | SYNTAX_INCLUDES },
};


/* =========================================================================
 * Primitive type table (indexed by JASSTYPEID)
 * ========================================================================= */

jassType_t jass_types[] = {
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

static void jass_missingcall(jass_t * j, cstring_t name, bool native);
static jassVar_t * jass_stackvalue(jass_t * j, int index);
static jassVar_t * jass_topvalue(jass_t * j);
static JASSTYPEID jass_getvarbasetype(jassVar_t const * var);
static uint32_t jass_dotoken(jass_t * j, token_t const * token);
static jassFunc_t const * find_function(jass_t const * j, cstring_t name);
static jassVar_t * find_global(jass_t const * j, cstring_t name);
static void eval_SINGLETOKEN(jass_t * j, token_t const * token);
static void eval_VARDECL(jass_t * j, token_t const * token);
void eval_TOKENS(jass_t * j, token_t const * token);
static void jass_copy(jass_t * j, jassVar_t * var, jassVar_t const * other);
void jass_setreturn(jass_t * j);
bool jass_mustreturn(jass_t * j);
bool uses_localplayer(token_t const * token);
static void jass_setnull(jassVar_t * var);
static jassType_t const * find_type(jass_t const * j, cstring_t name);
static jassType_t const * get_base_type(jassType_t const * type);
static jassVar_t * ensure_array_value(jass_t * j, jassVar_t * dest, uint32_t index);
static void jass_discard(jass_t * j, uint32_t count);

/* =========================================================================
 * Host interface
 * ========================================================================= */

static bool jass_atob(cstring_t str) {
    return !strcmp(str, "true");
}

static void jass_default_error(cstring_t message) { fprintf(stderr, "JASS runtime error: %s\n", message); }

void jass_sethost(jassHost_t const *host) {
    jass_host = *host;
    if (!jass_host.RuntimeError) jass_host.RuntimeError = jass_default_error;
}

handle_t jass_alloc(long size) {
    return jass_host.MemAlloc(size);
}

void jass_free(handle_t ptr) {
    jass_host.MemFree(ptr);
}

static uint32_t jass_gettime(void) {
    return jass_host.GetTime ? jass_host.GetTime() : 0;
}

static player_t * jass_getplayerbyindex(uint32_t number) {
    return jass_host.GetPlayerByNumber ? jass_host.GetPlayerByNumber(number) : NULL;
}

/* =========================================================================
 * Operators (built-in native functions for arithmetic / comparison)
 * ========================================================================= */

uint32_t __add(jass_t * j) {
    if (jass_gettype(j, 1) == jasstype_string && jass_gettype(j, 2) == jasstype_string) {
        cstring_t a = jass_checkstring(j, 1);
        cstring_t b = jass_checkstring(j, 2);
        uint32_t alen = a ? (uint32_t)strlen(a) : 0;
        uint32_t blen = b ? (uint32_t)strlen(b) : 0;
        string_t text = jass_alloc(alen + blen + 1);

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

uint32_t __unm(jass_t * j) {
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

static bool jass_valuehandle(cstring_t type) {
    static cstring_t const value_handles[] = {
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

static bool var_eq(jassVar_t const * a, jassVar_t const * b) {
    switch ((a->value == NULL) + (b->value == NULL)) {
        case 2: return true;
        case 1: return false;
    }
    if (jass_getvarbasetype(a) != jass_getvarbasetype(b)) return false;
    switch (jass_getvarbasetype(a)) {
        case jasstype_integer: return !memcmp(a->value, b->value, sizeof(int32_t));
        case jasstype_real: return !memcmp(a->value, b->value, sizeof(float));
        case jasstype_string: return !strcmp(a->value, b->value);
        case jasstype_boolean: return !memcmp(a->value, b->value, sizeof(bool));
        case jasstype_code: return !memcmp(a->value, b->value, sizeof(handle_t));
        case jasstype_cfunction: return !memcmp(a->value, b->value, sizeof(handle_t));
        case jasstype_handle:
            if (a->value == b->value) return true;
            if (a->type != b->type) return false;
            /* Galaxy opaque types have no JASS declaration; unequal identities must not dereference a null type. */
            return a->type && jass_valuehandle(a->type->name) && !memcmp(a->value, b->value, sizeof(uint32_t));
    }
    return false;
}

uint32_t __eq(jass_t * j) {
    return jass_pushboolean(j, var_eq(jass_stackvalue(j, 1), jass_stackvalue(j, 2)));
}

uint32_t __ne(jass_t * j) {
    return jass_pushboolean(j, !var_eq(jass_stackvalue(j, 1), jass_stackvalue(j, 2)));
}

uint32_t __and(jass_t * j) {
    return jass_pushboolean(j, jass_toboolean(j, 1) && jass_toboolean(j, 2));
}

uint32_t __or(jass_t * j) {
    return jass_pushboolean(j, jass_toboolean(j, 1) || jass_toboolean(j, 2));
}

uint32_t __not(jass_t * j) {
    return jass_pushboolean(j, !jass_toboolean(j, 1));
}

uint32_t __lsh(jass_t * j) {
    return jass_pushinteger(j, (int32_t)jass_checkinteger(j, 1) << (int32_t)jass_checkinteger(j, 2));
}
uint32_t __rsh(jass_t * j) {
    return jass_pushinteger(j, (int32_t)jass_checkinteger(j, 1) >> (int32_t)jass_checkinteger(j, 2));
}
uint32_t __bor(jass_t * j) {
    return jass_pushinteger(j, (int32_t)jass_checkinteger(j, 1) | (int32_t)jass_checkinteger(j, 2));
}
uint32_t __band(jass_t * j) {
    return jass_pushinteger(j, (int32_t)jass_checkinteger(j, 1) & (int32_t)jass_checkinteger(j, 2));
}
uint32_t __xor(jass_t * j) {
    return jass_pushinteger(j, (int32_t)jass_checkinteger(j, 1) ^ (int32_t)jass_checkinteger(j, 2));
}

jassModule_t jass_operators[] = {
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

void removeDoubleBackslashes(string_t str) {
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

bool is_integer(cstring_t tok) {
    string_t endptr;
    if (!tok || !*tok) return false;
    if (*tok == '$') {
        if (!tok[1]) return false;
        strtol(tok + 1, &endptr, 16);
        return *endptr == '\0';
    }
    strtol(tok, &endptr, 0); /* base 0 accepts 0x hex used by Vexorian output */
    return *endptr == '\0';
}

bool is_float(cstring_t tok) {
    string_t endptr;
    strtod(tok, &endptr);
    return *endptr == '\0';
}

bool is_fourcc(cstring_t tok) {
    return *tok == '\'';
}

bool is_string(cstring_t tok) {
    return *tok == '\"';
}

bool is_identifier(cstring_t str) {
    if (!isalpha(*str) && *str != '_')
        return false;
    for (cstring_t s = str; *s; ++s) {
        if (!isalnum(*s) && *s != '_')
            return false;
    }
    for (cstring_t *kw = keywords; *kw; kw++) {
        if (!strcmp(str, *kw)) {
            return false;
        }
    }
    return true;
}

bool is_comma(cstring_t str) {
    return !strcmp(str, ",");
}

/* =========================================================================
 * Context
 * ========================================================================= */

jassContext_t const * jass_getcontext(jass_t * j) {
    return &j->context;
}

static jass_t * jass_root(jass_t * j) { return j->root ? j->root : j; }
jass_t * jass_getroot(jass_t * j)     { return jass_root(j); }
bool jass_isrunning(jass_t * j)     { return jass_root(j)->current_coroutine != NULL; }
void jass_haltevents(jass_t * j)    { jass_root(j)->halt_events = true; }

/* =========================================================================
 * Coroutine frame management
 * ========================================================================= */

static void jass_free_frame(jasscoroutine_t * co, jassCoroutineframe_t * frame) {
    if (frame->locals) {
        FOR_LOOP(i, co->state->num_stack)
            if (co->state->stack[i].env.locals == frame->locals) co->state->stack[i].env.locals = NULL;
        jass_deletedict(frame->locals);
    }
    jass_free(frame);
}

static void jass_free_coroutine(jasscoroutine_t * co) {
    while (co->frames) {
        jassCoroutineframe_t * next = co->frames->next;
        jass_free_frame(co, co->frames);
        co->frames = next;
    }
    FOR_LOOP(i, co->state->num_stack) jass_setnull(co->state->stack + i);
    SAFE_DELETE(co->state, jass_free);
    jass_free(co);
}

static jassCoroutineframe_t * jass_coroutine_pushframe(jasscoroutine_t * co,
                                                     JASSFRAMETYPE type,
                                                     jassFunc_t const * func,
                                                     token_t const * body,
                                                     jassdict_t * locals) {
    jassCoroutineframe_t * frame = JASSALLOC(jassCoroutineframe_t);
    frame->type = type;
    frame->func = func;
    frame->body = body;
    frame->pc = body;
    frame->locals = locals;
    frame->loop_count = 0;
    ADD_TO_LIST(frame, co->frames);
    return frame;
}

static void jass_coroutine_popframe(jasscoroutine_t * co) {
    jassCoroutineframe_t * frame = co->frames;
    if (frame) {
        JASSFRAMETYPE type = frame->type;
        co->frames = frame->next;
        jass_free_frame(co, frame);
        /* Coroutine calls used as statements discard returned values.  The old path retained one value per return. */
        if (type == JASS_FRAME_FUNCTION && co->state->num_stack > 1)
            jass_discard(co->state, co->state->num_stack - 1);
    }
}

static jassCoroutineframe_t * jass_coroutine_functionframe(jasscoroutine_t * co) {
    FOR_EACH_LIST(jassCoroutineframe_t, frame, co->frames) {
        if (frame->type == JASS_FRAME_FUNCTION) {
            return frame;
        }
    }
    return NULL;
}

static void jass_coroutine_useframe(jass_t * j, jasscoroutine_t * co) {
    jassCoroutineframe_t * frame = jass_coroutine_functionframe(co);
    if (j->num_stack && frame) {
        j->stack[0].env.locals = frame->locals;
    }
}

static jassdict_t * jass_coroutine_buildlocals(jass_t * j, jassFunc_t const * func, token_t const * args) {
    jassdict_t * locals = NULL;
    token_t const * arg_token = args;

    FOR_EACH_LIST(jassarg_t, arg, func->args) {
        jassdict_t * local = JASSALLOC(jassdict_t);
        local->key = arg->name;
        local->value.type = arg->type;
        if (arg_token) {
            uint32_t count = jass_dotoken(j, arg_token);
            /* Match synchronous calls: a void or unresolved argument becomes null instead of underflowing the stack. */
            if (!count) { jass_pushnull(j); count = 1; }
            jass_copy(j, &local->value, jass_topvalue(j));
            jass_discard(j, count);
            arg_token = arg_token->next;
        }
        PUSH_BACK(jassdict_t, local, locals);
    }
    return locals;
}

/* =========================================================================
 * Coroutine lifecycle
 * ========================================================================= */

jasscoroutine_t * jass_startcoroutine(jass_t * j, jassContext_t const * context) {
    jass_t * root = jass_root(j);
    jass_t * co_state = JASSALLOC(jass_t);
    memcpy(co_state, root, sizeof(jass_t));
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
    if (!co_state->context.region) {
        co_state->context.region = jass_getcontext(j)->region;
    }
    co_state->root = root;
    co_state->coroutines = NULL;
    co_state->current_coroutine = NULL;

    jasscoroutine_t * co = JASSALLOC(jasscoroutine_t);
    co->state = co_state;
    co->frames = NULL;
    co->wake_time = jass_gettime();
    co->yielded = false;
    co->done = false;
    co->loop_a_index = 0;
    co->loop_a_index_valid = false;
    co->next = NULL;
    if (context->func) {
        jass_coroutine_pushframe(co,
                                 JASS_FRAME_FUNCTION,
                                 context->func,
                                 context->func->code,
                                 NULL);
    }

    PUSH_BACK(jasscoroutine_t, co, root->coroutines);
    return co;
}

jasscoroutine_t * jass_startcoroutinebyname(jass_t * j, cstring_t name) {
    jassFunc_t const * func = find_function(jass_root(j), name);
    jassContext_t context = *jass_getcontext(j);

    if (!func) {
        jass_missingcall(j, name, false);
        return NULL;
    }
    context.func = func;
    return jass_startcoroutine(j, &context);
}

/* AI roots have no ambient map-trigger context, so their entrypoint must carry the owning player explicitly. */
jasscoroutine_t * jass_startcoroutinebynameforplayer(jass_t * j, cstring_t name, struct playerState_s *player) {
    jassFunc_t const * func = find_function(jass_root(j), name);
    jassContext_t context = *jass_getcontext(j);
    if (!func) {
        jass_missingcall(j, name, false);
        return NULL;
    }
    context.func = func;
    context.playerState = player;
    return jass_startcoroutine(j, &context);
}

/* Dynamic wait-done calls must share the active coroutine so yielded child frames resume before their caller. */
bool jass_callcoroutinebyname(jass_t * j, cstring_t name) {
    jass_t * root = jass_root(j);
    jasscoroutine_t * co = root->current_coroutine;
    jassFunc_t const * func = find_function(root, name);

    if (!co || !func || func->nativefunc) return false;
    jass_coroutine_pushframe(co, JASS_FRAME_FUNCTION, func, func->code, NULL);
    return true;
}

cstring_t jass_functionname(jassFunc_t const * func) {
    return func ? func->name : NULL;
}

cstring_t jass_currentfunctionname(jass_t * j) {
    jass_t * root = jass_root(j);
    jasscoroutine_t * co = root->current_coroutine;
    jassCoroutineframe_t * frame = co ? jass_coroutine_functionframe(co) : NULL;
    jassFunc_t const * func = frame ? frame->func : j->context.func;
    return jass_functionname(func);
}

uint32_t jass_formatcallchain(jass_t * j, string_t buffer, uint32_t size) {
    jass_t * root;
    jasscoroutine_t * co;
    uint32_t used = 0;
    bool any = false;

    if (!buffer || !size) return 0;
    buffer[0] = '\0';
    if (!j) return 0;
    root = jass_root(j);
    co = root->current_coroutine;
    if (co) {
        FOR_EACH_LIST(jassCoroutineframe_t, frame, co->frames) {
            cstring_t name;
            int wrote;
            if (frame->type != JASS_FRAME_FUNCTION || !frame->func) continue;
            name = jass_functionname(frame->func);
            if (!name || !*name) continue;
            wrote = snprintf(buffer + used, size - used, "%s%s", any ? " <- " : "", name);
            if (wrote < 0) break;
            if ((uint32_t)wrote >= size - used) { used = size - 1; break; }
            used += (uint32_t)wrote;
            any = true;
        }
    }
    if (!any && j->context.func) {
        cstring_t name = jass_functionname(j->context.func);
        if (name) snprintf(buffer, size, "%s", name);
    }
    return (uint32_t)strlen(buffer);
}

jassFunc_t const * jass_functionbyname(jass_t * j, cstring_t name) { return find_function(jass_root(j), name); }
void jass_settimercontext(handle_t timer) { currenttimer = timer; }

bool jass_triggerdisabled(trigger_t * trigger) {
    return trigger ? trigger->disabled : false;
}

/* =========================================================================
 * Sleep / yield
 * ========================================================================= */

void jass_sleep(jass_t * j, uint32_t msec) {
    jass_t * root = jass_root(j);
    jasscoroutine_t * co = root->current_coroutine;
    if (!co) {
        return;
    }
    co->wake_time = jass_gettime() + msec;
    co->yielded = true;
}

static bool jass_yielded(jass_t * j) {
    jass_t * root = jass_root(j);
    jasscoroutine_t * co = root->current_coroutine;
    return co && co->yielded;
}

/* =========================================================================
 * Runtime error boundary  (JASS equivalent of Lua error())
 * ========================================================================= */

static void jass_setruntimeerror(jass_t * j, cstring_t message) {
    jass_t * root = jass_root(j);
    root->rterror_pending = true;
    snprintf(root->rterror_message, sizeof(root->rterror_message), "%s", message ? message : "(nil)");
    jass_host.RuntimeError(root->rterror_message);
}

/* Missing calls unwind like native errors; returning no value used to let broken callers continue. */
static void jass_missingcall(jass_t * j, cstring_t name, bool native) {
    jass_t * root = jass_root(j);
    jassmissing_t * item = root->missing;
    char message[256];
    while (item && strcmp(item->name, name)) item = item->next;
    if (!item) {
        item = jass_alloc(sizeof(*item) + strlen(name) + 1);
        strcpy(item->name, name);
        item->next = root->missing;
        root->missing = item;
    }
    snprintf(message, sizeof(message), "%s: %s", native ? "unimplemented native" : "unknown function", name);
    if (root->current_coroutine || root->sync_rterror_jmp_set) jass_rterror(j, message);
    else jass_setruntimeerror(j, message);
}

uint32_t jass_missingcount(jass_t * j) {
    uint32_t count = 0;
    for (jassmissing_t * item = jass_root(j)->missing; item; item = item->next) count++;
    return count;
}

cstring_t jass_missingname(jass_t * j, uint32_t index) {
    jassmissing_t * item = jass_root(j)->missing;
    while (item && index--) item = item->next;
    return item ? item->name : NULL;
}

void jass_rterror(jass_t * j, cstring_t message) {
    jass_t * root = jass_root(j);
    jass_setruntimeerror(root, message);

    jasscoroutine_t * co = root->current_coroutine;
    if (co && co->rterror_jmp_set) {
        longjmp(co->rterror_jmp, 1);
    }
    if (root->sync_rterror_jmp_set) longjmp(root->sync_rterror_jmp, 1);
    /* No active coroutine boundary — abort process (parser-path fallback). */
    abort();
}

bool jass_rterror_pending(jass_t * j) {
    return jass_root(j)->rterror_pending;
}

cstring_t jass_rterror_message(jass_t * j) {
    return jass_root(j)->rterror_message;
}

void jass_rterror_clear(jass_t * j) {
    jass_t * root = jass_root(j);
    root->rterror_pending = false;
    root->rterror_message[0] = '\0';
}

/* =========================================================================
 * Player event helpers
 * ========================================================================= */

static player_t * jass_eventplayer(edict_t * unit) {
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

static bool jass_coroutine_callstatement(jass_t * j, jasscoroutine_t * co, token_t const * token) {
    jassFunc_t const * func = NULL;
    jassdict_t * locals;

    if (token->type != TT_CALL || !(func = find_function(j, token->primary))) {
        return false;
    }
    if (func->nativefunc) {
        return false;
    }
    /* Native declarations without host bindings must not run as empty script functions. */
    if (func->native) {
        jass_missingcall(j, func->name, true);
        return true;
    }
    locals = jass_coroutine_buildlocals(j, func, token->args);
    jass_coroutine_pushframe(co, JASS_FRAME_FUNCTION, func, func->code, locals);
    return true;
}

static token_t const * jass_coroutine_selectifbody(jass_t * j, token_t const * token) {
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

static bool jass_coroutine_runlocalplayerif(jass_t * j, jasscoroutine_t * co, token_t const * token) {
    player_t * previous_player;

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

static void jass_coroutine_return(jasscoroutine_t * co) {
    while (co->frames) {
        JASSFRAMETYPE type = co->frames->type;
        jass_coroutine_popframe(co);
        if (type == JASS_FRAME_FUNCTION) {
            return;
        }
    }
}

static void jass_resumecoroutine(jasscoroutine_t * co) {
    jass_t * j = co->state;

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
        jassCoroutineframe_t * frame = co->frames;
        token_t const * token = frame->pc;
        token_t const * next;

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
                token_t const * body;
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
                    jassCoroutineframe_t * fn_frame = jass_coroutine_functionframe(co);
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

bool jass_coroutinedone(jasscoroutine_t const * co) {
    return !co || co->done;
}

bool jass_resume(jass_t * j, jasscoroutine_t * co) {
    jass_t * root = jass_root(j);
    jasscoroutine_t * previous_coroutine = root->current_coroutine;
    uint32_t now = jass_gettime();
    player_t * previous_player;
    edict_t * previous_unit;
    jassVar_t * loop_index = find_global(jass_root(j), "bj_forLoopAIndex");
    int32_t previous_loop_index = 0;
    bool restore_loop_index = co && co->loop_a_index_valid && loop_index && loop_index->value &&
        jass_getvarbasetype(loop_index) == jasstype_integer;

    if (!co || co->done || co->wake_time > now) {
        return false;
    }

    /* PauseTimer can run after a timer event queues its action but before the
     * action gets its first resume. Invalidate only that not-yet-started
     * coroutine; a callback that has already begun may legitimately yield. */
    if (co->state->context.timer_pending) {
        co->state->context.timer_pending = false;
        if (jass_host.TimerCoroutineValid &&
            !jass_host.TimerCoroutineValid(co->state->context.timer,
                                           co->state->context.timer_generation)) {
            co->done = true;
            return false;
        }
    }

    previous_player = currentplayer;
    previous_unit = currentunit;

    root->current_coroutine = co;
    if (restore_loop_index) {
        previous_loop_index = *(int32_t *)loop_index->value;
        /* Restore the captured index while this coroutine resumes; the original shared
         * global is put back below so its caller continues with its own loop state. */
        jass_pushinteger(root, co->loop_a_index);
        jass_copy(root, loop_index, jass_topvalue(root));
        jass_discard(root, 1);
    }
    currentplayer = co->state->context.localPlayerState;
    currentunit = co->state->context.unit;
    if (jass_host.CoroutineTrace) {
        jassCoroutineframe_t * frame = jass_coroutine_functionframe(co);
        jass_host.CoroutineTrace(co->state->context.trigger,
                                frame && frame->func ? jass_functionname(frame->func) : NULL,
                                "resume", now, co->wake_time,
                                co->yielded, co->done);
    }
    jass_resumecoroutine(co);
    if (jass_host.CoroutineTrace) {
        jassCoroutineframe_t * frame = jass_coroutine_functionframe(co);
        jass_host.CoroutineTrace(co->state->context.trigger,
                                frame && frame->func ? jass_functionname(frame->func) : NULL,
                                co->done ? "done" : (co->yielded ? "yield" : "return"),
                                jass_gettime(), co->wake_time,
                                co->yielded, co->done);
    }
    currentunit = previous_unit;
    currentplayer = previous_player;
    root->current_coroutine = previous_coroutine;
    if (restore_loop_index) {
        jass_pushinteger(root, previous_loop_index);
        jass_copy(root, loop_index, jass_topvalue(root));
        jass_discard(root, 1);
    }

    return true;
}

void jass_runevents(jass_t * j) {
    jass_t * root = jass_root(j);
    jasscoroutine_t * prev = NULL;
    jasscoroutine_t * co = root->coroutines;

    while (co) {
        jasscoroutine_t * next;
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

static bool jass_evaluatetriggercontext(jass_t * j, jassTriggerContextParams_t const *params) {
    player_t * player = jass_eventplayer(params->unit);

    if (params->trigger->disabled) {
        return false;
    }
    jass_t tmp_state;
    FOR_EACH_LIST(gTriggerCondition_t, cond, params->trigger->conditions) {
        memcpy(&tmp_state, j, sizeof(struct jass_s));
        memset(tmp_state.stack, 0, sizeof(tmp_state.stack));
        tmp_state.num_stack = 0;
        tmp_state.context.trigger = params->trigger;
        tmp_state.context.unit = params->unit;
        tmp_state.context.source = params->source;
        tmp_state.context.eventValue = params->value;
        tmp_state.context.point = params->point ? *params->point : (vector2_t){ 0.0f, 0.0f };
        tmp_state.context.hasPoint = params->has_point;
        tmp_state.context.playerState = player;
        tmp_state.context.localPlayerState = currentplayer;
        tmp_state.context.timer = currenttimer;
        tmp_state.context.region = params->region ? params->region : jass_getcontext(j)->region;
        jass_pushfunction(&tmp_state, cond->expr);
        edict_t * previous_unit = currentunit;
        currentunit = params->unit;
        uint32_t result_count = jass_call(&tmp_state, 0);
        currentunit = previous_unit;
        if (result_count != 1 || !jass_popboolean(&tmp_state)) {
            return false;
        }
    }
    return true;
}

bool jass_evaluatetrigger(jass_t * j, trigger_t * trigger, edict_t * unit) {
    return jass_evaluatetriggercontext(j, &(jassTriggerContextParams_t){ .trigger = trigger, .unit = unit });
}

/* Evaluate a single boolexpr (a Condition()/Filter() code) against a candidate
 * unit, the way jass_evaluatetrigger evaluates a trigger condition: run the
 * function in a scratch state with the unit bound as the context/current unit
 * so GetFilterUnit()/GetEnumUnit() inside the filter resolve to it.  Returns
 * the filter's boolean result; a null filter passes (matches "no filter"). */
bool jass_evaluateboolexpr(jass_t * j, jassFunc_t const * expr, edict_t * unit) {
    if (!expr) {
        return true;
    }
    jass_t tmp_state;
    memcpy(&tmp_state, j, sizeof(struct jass_s));
    memset(tmp_state.stack, 0, sizeof(tmp_state.stack));
    tmp_state.num_stack = 0;
    tmp_state.context.unit = unit;
    jass_pushfunction(&tmp_state, expr);
    edict_t * previous_unit = currentunit;
    currentunit = unit;
    uint32_t result_count = jass_call(&tmp_state, 0);
    currentunit = previous_unit;
    return result_count == 1 && jass_popboolean(&tmp_state);
}

/* Force filters bind players through the same scratch-state context used by
 * trigger callbacks, keeping GetFilterPlayer isolated across nested calls. */
bool jass_evaluateplayerexpr(jass_t * j, jassFunc_t const * expr, player_t * player) {
    if (!expr) return true;
    jass_t tmp_state;
    memcpy(&tmp_state, j, sizeof(struct jass_s));
    memset(tmp_state.stack, 0, sizeof(tmp_state.stack));
    tmp_state.num_stack = 0;
    tmp_state.context.playerState = player;
    jass_pushfunction(&tmp_state, expr);
    uint32_t result_count = jass_call(&tmp_state, 0);
    return result_count == 1 && jass_popboolean(&tmp_state);
}

static void jass_executetriggercontext(jass_t * j, jassTriggerContextParams_t const *params, bool immediate) {
    jasscoroutine_t * first = NULL, *last = NULL;
    FOR_EACH_LIST(gTriggerAction_t, action, params->trigger->actions) {
        player_t * player = jass_eventplayer(params->unit);
        jasscoroutine_t * co = jass_startcoroutine(j, &MAKE(jassContext_t,
                                  .trigger = params->trigger,
                                  .func = action->func,
                                  .unit = params->unit,
                                  .source = params->source,
                                  .eventValue = params->value,
                                  .point = params->point ? *params->point : (vector2_t){ 0.0f, 0.0f },
                                  .hasPoint = params->has_point,
                                  .playerState = player,
                                  .localPlayerState = currentplayer,
                                  .timer = params->timer,
                                  .region = params->region,
                                  .timer_generation = params->timer ? ((gtimer_t const *)params->timer)->generation : 0,
                                  .timer_pending = params->timer_pending,
                              ));
        jassVar_t * loop_index = find_global(j, "bj_forLoopAIndex");
        /* Keep queued and suspended actions on the loop index captured at dispatch. */
        if (co && loop_index && loop_index->value && jass_getvarbasetype(loop_index) == jasstype_integer) {
            co->loop_a_index = *(int32_t *)loop_index->value;
            co->loop_a_index_valid = true;
        }
        if (co) {
            if (!first) first = co;
            last = co;
        }
    }
    /* Run only the coroutines created for this dispatch. Callbacks can mutate
     * the trigger's action list or append coroutines through nested executes. */
    if (immediate) {
        jass_t * root = jass_root(j);
        for (jasscoroutine_t * co = first; co;) {
            jasscoroutine_t * next = co->next;
            jass_resume(root, co);
            if (co == last) break;
            co = next;
        }
    }
}

void jass_executetrigger(jass_t * j, trigger_t * trigger, edict_t * unit) {
    jass_executetriggercontext(j, &(jassTriggerContextParams_t){ .trigger = trigger, .unit = unit }, true);
}

static bool jass_calltriggercontext(jass_t * j, jassTriggerContextParams_t const *params) {
    if (!jass_evaluatetriggercontext(j, params))
        return false;
    jass_executetriggercontext(j, params, false);
    return true;
}

bool jass_calltriggerwithvalue(jass_t * j,
                               trigger_t * trigger,
                               edict_t * unit,
                               edict_t * source,
                               int32_t eventValue) {
    return jass_calltriggercontext(j, &(jassTriggerContextParams_t){
        .trigger = trigger, .unit = unit, .source = source, .value = eventValue });
}

bool jass_calltriggerevent(jass_t * j, trigger_t * trigger, gameEvent_t const *event) {
    if (!event) return false;
    return jass_calltriggercontext(j, &(jassTriggerContextParams_t){
        .trigger = trigger, .unit = event->edict, .source = event->source, .value = event->value,
        .point = event->has_point ? &event->point : NULL, .has_point = event->has_point,
        .region = event->responseTo && (event->type == EVENT_GAME_ENTER_REGION || event->type == EVENT_GAME_LEAVE_REGION)
            ? event->responseTo->region : NULL });
}

bool jass_calltriggerwithtimer(jass_t * j, trigger_t * trigger, handle_t timer) {
    bool queued;
    jassTriggerContextParams_t params = { .trigger = trigger, .timer = timer, .timer_pending = true };
    currenttimer = timer;
    queued = jass_calltriggercontext(j, &params);
    currenttimer = NULL;
    return queued;
}

bool jass_calltrigger(jass_t * j,
                      trigger_t * trigger,
                      edict_t * unit,
                      edict_t * source) {
    return jass_calltriggerwithvalue(j, trigger, unit, source, 0);
}

/* =========================================================================
 * Lookup helpers
 * ========================================================================= */

static jassCFunction_t find_cfunction(jass_t const * j, cstring_t name) {
    for (jassModule_t const * m = jass_operators; m->name; m++) {
        if (!strcmp(m->name, name)) {
            return m->func;
        }
    }
    if (jass_host.natives) {
        for (jassModule_t const * m = jass_host.natives; m->name; m++) {
            if (!strcmp(m->name, name)) {
                return m->func;
            }
        }
    }
    if (jass_host.galaxy_natives) {
        for (jassModule_t const * m = jass_host.galaxy_natives; m->name; m++) {
            if (!strcmp(m->name, name)) {
                return m->func;
            }
        }
    }
    return NULL;
}

/* Root declarations use separate bucket links so list order and duplicate-name behavior stay unchanged. */
static uint32_t jass_hash(cstring_t name) {
    uint32_t hash = 0;
    while (*name) hash = (uint8_t)*name++ + (hash << 6) + (hash << 16) - hash;
    return hash & (BZ_JASS_HASH_SIZE - 1);
}

static jassFunc_t const * find_function(jass_t const * j, cstring_t name) {
    for (jassFunc_t const * func = j->function_hash[jass_hash(name)]; func; func = func->hash_next) {
        if (!strcmp(func->name, name)) {
            return func;
        }
    }
    return NULL;
}

static jassVar_t * find_dict(jassdict_t * dict, cstring_t name) {
    FOR_EACH_LIST(jassdict_t, item, dict) {
        if (!strcmp(item->key, name)) {
            return &item->value;
        }
    }
    return NULL;
}

static jassVar_t * find_global(jass_t const * j, cstring_t name) {
    for (jassdict_t * item = j->global_hash[jass_hash(name)]; item; item = item->hash_next) {
        if (!strcmp(item->key, name)) return &item->value;
    }
    return NULL;
}

static jassType_t const * find_type(jass_t const * j, cstring_t name) {
    FOR_LOOP(i, sizeof(jass_types)/sizeof(*jass_types)) {
        if (!strcmp(jass_types[i].name, name)) {
            return &jass_types[i];
        }
    }
    FOR_EACH_LIST(jassType_t, type, j->types) {
        if (!strcmp(type->name, name)) {
            return type;
        }
    }
    return NULL;
}

jassType_t const * get_base_type(jassType_t const * type) {
    if (!type) return &jass_types[jasstype_handle];  /* Galaxy SC2 types → opaque handle */
    while (type->inherit) {
        type = type->inherit;
    }
    return type;
}

/* =========================================================================
 * Stack: return / done flags
 * ========================================================================= */

void jass_setreturn(jass_t * j) {
    jass_stackvalue(j, 0)->env.done = true;
    jass_stackvalue(j, 0)->env.returnstack = j->num_stack;
}

bool jass_mustreturn(jass_t * j) {
    return jass_stackvalue(j, 0)->env.done;
}

uint32_t jass_top(jass_t * j) {
    return j->num_stack - 1;
}

jassVar_t * jass_topvalue(jass_t * j) {
    return j->stack + jass_top(j);
}

jassVar_t * jass_stackvalue(jass_t * j, int index) {
    if (index < 0) {
        return j->stack + (j->num_stack + index);
    } else {
        return j->stack_pointer + index;
    }
}

JASSTYPEID jass_getvarbasetype(jassVar_t const * var) {
    return (JASSTYPEID)(get_base_type(var->type) - jass_types);
}

JASSTYPEID jass_gettype(jass_t * j, int index) {
    jassVar_t const * var = jass_stackvalue(j, index);
    return jass_getvarbasetype(var);
}

bool jass_checktype(jassVar_t const * var, JASSTYPEID type) {
    return get_base_type(var->type) == jass_types + type;
}

void jass_pop(jass_t * j, uint32_t count) {
    j->num_stack -= count;
}

static void jass_discard(jass_t * j, uint32_t count) {
    while (count-- && j->num_stack) {
        jass_setnull(jass_topvalue(j));
        jass_pop(j, 1);
    }
}

/* =========================================================================
 * Memory: null / copy / free
 * ========================================================================= */

static void jass_deletedict(jassdict_t * dict) {
    SAFE_DELETE(dict->next, jass_deletedict);
    jass_setnull(&dict->value);
    jass_free(dict);
}

static void jass_deletearray(jassArray_t * array) {
    if (!array) return;
    jass_deletearray(array->next);
    jass_setnull(&array->value);
    jass_free(array);
}

void jass_setnull(jassVar_t * var) {
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

bool is_handle_convertible(jassType_t const * from, jassType_t const * to) {
    if (from == to) {
        return true;
    } else if (from->inherit) {
        return is_handle_convertible(from->inherit, to);
    } else {
        return false;
    }
}

static jassVar_t * ensure_array_value(jass_t * j, jassVar_t * dest, uint32_t index) {
    (void)j;
    FOR_EACH_LIST(jassArray_t, var, dest->_array) {
        if (var->index == index) {
            return &var->value;
        }
    }
    jassArray_t * jv = JASSALLOC(jassArray_t);
    jv->value.type = dest->type;
    jv->index = index;
    ADD_TO_LIST(jv, dest->_array);
    return &jv->value;
}

void jass_copy(jass_t * j, jassVar_t * var, jassVar_t const * other) {
    float fval = 0;
    jass_setnull(var);
    if (other->_array) {
        var->type = other->type;
        FOR_EACH_LIST(jassArray_t, srcar, other->_array) {
            jass_copy(j, ensure_array_value(j, var, srcar->index), &srcar->value);
        }
        return;
    } else if (!other->value) {
        return;
    } else switch (jass_getvarbasetype(var)) {
        case jasstype_integer:
            jass_store_value(var, other->value, sizeof(int32_t));
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
                    fval = *(float const *)other->value;
                    break;
                case jasstype_integer:
                    fval = *(int32_t const *)other->value;
                    break;
                default:
                    fval = 0.0f;
                    break;
            }
            jass_store_value(var, &fval, sizeof(float));
            break;
        case jasstype_boolean:
            jass_store_value(var, other->value, sizeof(bool));
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

uint32_t jass_pushnull(jass_t * j) {
    JASS_ADD_STACK(j, var, jasstype_handle);
    return 1;
}

uint32_t jass_pushinteger(jass_t * j, int32_t value) {
    JASS_ADD_STACK(j, var, jasstype_integer);
    jass_store_value(var, &value, sizeof(value));
    return 1;
}

uint32_t jass_pushhandle(jass_t * j, handle_t value, cstring_t type) {
    JASS_ADD_STACK(j, var, jasstype_handle);
    jass_setnull(var);
    var->type = find_type(j, type);
    if (value) {
        var->value = value;
        var->ref = jass_alloc(sizeof(*var->ref));
        *var->ref = (jassref_t){ 0 };
    }
    return 1;
}

uint32_t jass_pushnullhandle(jass_t * j, cstring_t type) {
    return jass_pushhandle(j, 0, type);
}

handle_t jass_newhandle(jass_t * j, uint32_t size, cstring_t type) {
    handle_t data = size ? jass_alloc(size) : NULL;
    jass_pushhandle(j, data, type);
    if (data) {
        jassVar_t * var = jass_topvalue(j);
        var->ref->size = size;
        var->ref->id = ++jass_root(j)->next_handle_id;
    }
    return data;
}

uint32_t jass_pushlighthandle(jass_t * j, handle_t value, cstring_t type) {
    JASS_ADD_STACK(j, var, jasstype_handle);
    jass_setnull(var);
    var->type = find_type(j, type);
    var->value = value;
    var->ref = jass_alloc(sizeof(*var->ref));
    *var->ref = (jassref_t){ .refs = 1 };
    return 1;
}

uint32_t jass_pushnumber(jass_t * j, float value) {
    JASS_ADD_STACK(j, var, jasstype_real);
    jass_store_value(var, &value, sizeof(value));
    return 1;
}

uint32_t jass_pushboolean(jass_t * j, bool value) {
    JASS_ADD_STACK(j, var, jasstype_boolean);
    jass_store_value(var, &value, sizeof(value));
    return 1;
}

uint32_t jass_pushstringlen(jass_t * j, cstring_t value, uint32_t len) {
    JASS_ADD_STACK(j, var, jasstype_string);
    jass_store_value(var, value, len+1);
    ((string_t)var->value)[len] = '\0';
    removeDoubleBackslashes(var->value);
    return 1;
}

uint32_t jass_pushstring(jass_t * j, cstring_t value) {
    /* Tolerate a NULL string from a native (several are stubs that return 0);
     * strlen(NULL) would crash.  An unset JASS string is the empty string. */
    if (!value)
        value = "";
    jass_pushstringlen(j, value, (uint32_t)strlen(value));
    return 1;
}

uint32_t jass_pushcfunction(jass_t * j, jassCFunction_t func) {
    JASS_ADD_STACK(j, var, jasstype_cfunction);
    jass_store_value(var, &func, sizeof(jassCFunction_t));
    return 1;
}

/* Code values retain their declaration so native and scripted callbacks share one stable representation. */
uint32_t jass_pushfunction(jass_t * j, jassFunc_t const * func) {
    JASS_ADD_STACK(j, var, jasstype_code);
    var->value = (jassFunc_t *)func;
    return 1;
}

uint32_t jass_pushvalue(jass_t * j, jassVar_t const * other) {
    jassType_t const * type = other->type;
    BZ_JASS_REQUIRE_STACK(j);
    jassVar_t * var = &j->stack[j->num_stack++];
    memset(var, 0, sizeof(*var));
    var->type = type;
    jass_copy(j, var, other);
    return 1;
}

int32_t jass_checkinteger(jass_t * j, int index) {
    jassVar_t const * var = jass_stackvalue(j, index);
    assert_type(var, jasstype_integer);
    return var->value ? *(int32_t *)var->value : 0;
}

float jass_checknumber(jass_t * j, int index) {
    jassVar_t const * var = jass_stackvalue(j, index);
    if (!var->value) {
        return 0;
    }
    if (jass_checktype(var, jasstype_real)) {
        return *(float *)var->value;
    }
    if (jass_checktype(var, jasstype_integer)) {
        return *(int32_t *)var->value;
    }
    if (jass_checktype(var, jasstype_boolean)) {
        return *(bool *)var->value ? 1 : 0;
    }
    return 0.0f;  /* Galaxy: treat unknown numeric type as 0 */
}

bool jass_checkboolean(jass_t * j, int index) {
    jassVar_t const * var = jass_stackvalue(j, index);
    assert_type(var, jasstype_boolean);
    return var->value ? *(bool *)var->value : 0;
}

bool jass_toboolean(jass_t * j, int index) {
    jassVar_t const * var = jass_stackvalue(j, index);
    JASSTYPEID type = jass_getvarbasetype(var);
    if (var->value == NULL)
        return false;
    switch (type) {
        case jasstype_integer: return *(int32_t *)var->value != 0;
        case jasstype_real: return *(float *)var->value != 0;
        case jasstype_string: return strlen(var->value) > 0;
        case jasstype_boolean: return *(bool *)var->value != 0;
        case jasstype_handle: return true;
        case jasstype_code: return true;
        default: return false;
    }
}

cstring_t jass_checkstring(jass_t * j, int index) {
    jassVar_t const * var = jass_stackvalue(j, index);
    /* JASS null is polymorphic.  The VM stores it as a null handle, but Blizzard's
     * cinematic helpers pass null through string parameters; treat that value as
     * the empty string while retaining argument validation for non-null values. */
    if (jass_getvarbasetype(var) == jasstype_handle && !var->value)
        return "";
    assert_type(var, jasstype_string);
    return var->value;
}

jassFunc_t const * jass_checkcode(jass_t * j, int index) {
    jassVar_t const * var = jass_stackvalue(j, index);
    assert_type(var, jasstype_code);
    return var->value;
}

handle_t jass_checkhandle(jass_t * j, int index, cstring_t type) {
    jassVar_t const * var = jass_stackvalue(j, index);
    if (!var->value) {
        return NULL;
    }
    /* Skip type check for Galaxy — SC2 types are unregistered (type == NULL). */
    if (var->type) {
        jassType_t const * expected = find_type(j, type);
        if (expected && !is_handle_convertible(var->type, expected)) {
            fprintf(stderr, "Warning: jass_checkhandle type mismatch\n");
        }
    }
    return var->value;
}

bool jass_popboolean(jass_t * j) {
    bool value = jass_toboolean(j, -1);
    jass_pop(j, 1);
    return value;
}

static uint32_t jass_popinteger(jass_t * j) {
    uint32_t value = jass_checkinteger(j, -1);
    jass_pop(j, 1);
    return value;
}

/* =========================================================================
 * Token evaluators — expression evaluation (jass_dotoken)
 * ========================================================================= */

uint32_t VM_EvalInteger(jass_t * j, token_t const * token) {
    cstring_t s = token->primary;
    if (s && *s == '$') return jass_pushinteger(j, (int32_t)strtol(s + 1, NULL, 16));
    return jass_pushinteger(j, (int32_t)strtol(s, NULL, 0));
}

uint32_t VM_EvalReal(jass_t * j, token_t const * token) {
    return jass_pushnumber(j, atof(token->primary));
}

uint32_t VM_EvalString(jass_t * j, token_t const * token) {
    return jass_pushstring(j, token->primary);
}

uint32_t VM_EvalBoolean(jass_t * j, token_t const * token) {
    return jass_pushboolean(j, jass_atob(token->primary));
}

uint32_t VM_EvalIdentifier(jass_t * j, token_t const * token) {
    jassFunc_t const * f = NULL;
    jassVar_t const * v = NULL;
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
static jassVar_t * jass_array_value(jass_t * j, jassVar_t * var, token_t const * token) {
    while (token) {
        /* Evaluate before asserting: release builds must not erase the VM operation. */
        uint32_t count = jass_dotoken(j, token->index);
        if (count != 1) jass_pushnull(j);
        var = ensure_array_value(j, var, jass_popinteger(j));
        token = token->body;
    }
    return var;
}

uint32_t VM_EvalArrayAccess(jass_t * j, token_t const * token) {
    uint32_t count = jass_dotoken(j, token->index);
    if (count != 1) { jass_pushnull(j); }
    uint32_t index_val = jass_popinteger(j);
    VM_EvalIdentifier(j, token);
    jassVar_t * var = jass_stackvalue(j, -1);
    jassVar_t * item = ensure_array_value(j, var, index_val);
    if (token->body) item = jass_array_value(j, item, token->body);
    jass_pop(j, 1);
    jassVar_t tmp;
    memcpy(&tmp, item, sizeof(jassVar_t));
    jass_pushvalue(j, &tmp);
    return 1;
}

uint32_t VM_EvalFourCC(jass_t * j, token_t const * token) {
    return jass_pushinteger(j, *(uint32_t *)token->primary);
}

uint32_t VM_EvalCall(jass_t * j, token_t const * token) {
    jassFunc_t const * f = NULL;
    jassCFunction_t cf = NULL;
    uint32_t stacksize = j->num_stack;
    if (!strcmp(token->primary, "CommentString") && token->args) {
        fprintf(stdout, "%s\n", token->args->primary);
        return 0;
    } else if ((f = find_function(j, token->primary))) {
        /* An unresolved native used to execute as an empty JASS function, hiding missing engine behavior. */
        if (f->native && !f->nativefunc) {
            jass_missingcall(j, f->name, true);
            return 0;
        }
        uint32_t args = 0;
        jass_pushfunction(j, f);
        FOR_EACH_LIST(token_t, arg, token->args) {
            if (!jass_dotoken(j, arg)) {
                jass_pushnull(j);
            }
            args++;
        }
        jass_call(j, args);
        return j->num_stack - stacksize;
    } else if ((cf = find_cfunction(j, token->primary))) {
        uint32_t args = 0;
        jass_pushcfunction(j, cf);
        FOR_EACH_LIST(token_t, arg, token->args) {
            if (!jass_dotoken(j, arg)) {
                jass_pushnull(j);
            }
            args++;
        }
        jass_call(j, args);
        return j->num_stack - stacksize;
    } else {
        jass_missingcall(j, token->primary, false);
        return 0;
    }
}

static struct {
    TOKENTYPE tokentype;
    uint32_t (*func)(jass_t * j, token_t const * token);
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

uint32_t jass_dotoken(jass_t * j, token_t const * token) {
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

static float jass_numbervalue(jassVar_t const * var) {
    if (!var || !var->value) return 0.0f;
    if (jass_getvarbasetype(var) == jasstype_real) return *(float *)var->value;
    if (jass_getvarbasetype(var) == jasstype_integer) return *(int32_t *)var->value;
    return 0.0f;
}

static void jass_set_value(jass_t * j, jassVar_t * dest, token_t const * init, cstring_t name) {
    uint32_t stack = jass_dotoken(j, init);
    /* Normally an initializer expression yields exactly one value.  Tolerate
     * other counts instead of aborting: a value-returning function whose body
     * fell through, or an unimplemented/void native (returns 0), would
     * otherwise crash the whole VM mid-map.  Assign the top value when one was
     * produced and pop exactly what was pushed so the stack stays balanced. */
    if (stack >= 1) {
        float before = jass_numbervalue(dest);
        jass_copy(j, dest, j->stack + jass_top(j));
        jass_pop(j, stack);
        if (name && jass_host.VariableChanged &&
            (jass_getvarbasetype(dest) == jasstype_integer || jass_getvarbasetype(dest) == jasstype_real))
            jass_host.VariableChanged(name, before, jass_numbervalue(dest));
    }
}

static void jass_set_array_value(jass_t * j, jassVar_t * dest, token_t const * token, token_t const * init) {
    /* Evaluate before asserting: NDEBUG previously skipped both expressions and copied an unrelated stack value. */
    uint32_t count = jass_dotoken(j, token->index);
    if (count != 1) { jass_pushnull(j); }
    uint32_t index_val = jass_popinteger(j);
    jassVar_t * index_dest = ensure_array_value(j, dest, index_val);
    if (token->body) index_dest = jass_array_value(j, index_dest, token->body);
    count = jass_dotoken(j, init);
    if (count != 1) { jass_pushnull(j); }
    jass_copy(j, index_dest, j->stack + jass_top(j));
    jass_pop(j, 1);
}

static jassdict_t * parse_dict(jass_t * j, token_t const * token) {
    jassdict_t * item = JASSALLOC(jassdict_t);
    item->value.constant = token->flags & TF_CONSTANT;
    item->value.array = token->flags & TF_ARRAY;
    item->value.type = find_type(j, token->primary);
    item->key = token->secondary;
    if (token->init) {
        jass_set_value(j, &item->value, token->init, NULL);
    }
    return item;
}

TOKENFUNC(TOKENS);
TOKENFUNC(SINGLETOKEN);

TOKENFUNC(TYPEDEF) {
    jassType_t * type = JASSALLOC(jassType_t);
    type->name = token->primary;
    type->inherit = find_type(j, token->secondary);
    ADD_TO_LIST(type, j->types);
}

bool uses_localplayer(token_t const * token) {
    if (!token) return false;
    if (token->type == TT_CALL && token->primary && !strcmp(token->primary, "GetLocalPlayer")) {
        return true;
    }
    FOR_EACH_LIST(token_t, arg, token->args) {
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
    jassVar_t * v = NULL;
    if ((v = find_global(j, token->secondary))) {
        if (token->index) {
            return jass_set_array_value(j, v, token, token->init);
        } else {
            return jass_set_value(j, v, token->init, token->secondary);
        }
    } else if ((v = find_dict(jass_stackvalue(j, 0)->env.locals, token->secondary))) {
        if (token->index) {
            return jass_set_array_value(j, v, token, token->init);
        } else {
            return jass_set_value(j, v, token->init, NULL);
        }
    } else {
        fprintf(stderr, "Can't find variable %s\n",
                token->secondary ? token->secondary : "(null)");
    }
}

TOKENFUNC(VARDECL) {
    jassdict_t * vardecl = parse_dict(j, token);
    ADD_TO_LIST(vardecl, jass_stackvalue(j, 0)->env.locals);
}

TOKENFUNC(GLOBAL) {
    jassdict_t * global = parse_dict(j, token);
    jassdict_t * *bucket = &j->global_hash[jass_hash(global->key)];
    ADD_TO_LIST(global, j->globals);
    global->hash_next = *bucket;
    *bucket = global;
}

TOKENFUNC(FUNCTION) {
    jassFunc_t * func = JASSALLOC(jassFunc_t);
    jassFunc_t * *bucket;
    func->name = token->primary;
    func->code = token->body;
    func->native = token->flags & TF_NATIVE;
    func->returns = find_type(j, token->secondary);
    FOR_EACH_LIST(token_t, arg, token->args) {
        jassarg_t * jarg = JASSALLOC(jassarg_t);
        jarg->name = arg->secondary;
        jarg->type = find_type(j, arg->primary);
        PUSH_BACK(jassarg_t, jarg, func->args);
    }
    if (token->flags & TF_NATIVE) {
        if (jass_host.natives) {
            jassModule_t const * mod = find_in_array(jass_host.natives, sizeof(jassModule_t), func->name);
            if (mod) func->nativefunc = mod->func;
        }
        if (!func->nativefunc && jass_host.galaxy_natives) {
            jassModule_t const * mod = find_in_array(jass_host.galaxy_natives, sizeof(jassModule_t), func->name);
            if (mod) func->nativefunc = mod->func;
        }
    }
    bucket = &j->function_hash[jass_hash(func->name)];
    ADD_TO_LIST(func, j->functions);
    func->hash_next = *bucket;
    *bucket = func;
}

TOKENFUNC(CALL) {
    /* Blizzard's melee reveal timer passes GetLocalPlayer() directly, outside an IF.
     * Evaluate the whole call per player; a null selector used to reach the text native. */
    if (!currentplayer && uses_localplayer(token)) {
        FOR_LOOP(i, MAX_PLAYERS) {
            currentplayer = jass_getplayerbyindex(i);
            jass_discard(j, jass_dotoken(j, token));
        }
        currentplayer = NULL;
    } else jass_discard(j, jass_dotoken(j, token));
}

TOKENFUNC(LOOP) {
    for (uint32_t i = 0;; i++) {
        FOR_EACH_LIST(token_t const, tok, token->body) {
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
    cstring_t name;
    TOKENTYPE type;
    void (*func)(jass_t *, token_t const *);
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
    FOR_EACH_LIST(token_t const, tok, token) {
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

static void jass_remove_comments(string_t buf) {
    bool in_line  = false;
    bool in_block = false;
    uint32_t quotes  = 0;
    char *src = buf, *dst = buf;
    while (*src) {
        if (!in_line && !in_block) {
            if (*src == '"') { quotes++; *dst++ = *src++; }
            else if (quotes & 1) { *dst++ = *src++; }
            else if (src[0] == '/' && src[1] == '/') { in_line  = true;  src += 2; }
            else if (src[0] == '/' && src[1] == '*') { in_block = true;  src += 2; }
            else { *dst++ = *src++; }
        } else if (in_line && (*src == '\n' || *src == '\r')) { in_line = false; *dst++ = *src++; }
          else if (in_block && src[0] == '*' && src[1] == '/')      { in_block = false; src += 2; }
          else { src++; }
    }
    *dst = '\0';
}

static void jass_remove_bom(string_t buf) {
    unsigned char *u = (unsigned char *)buf;
    if (u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) { memmove(buf, buf + 3, strlen(buf + 3) + 1); return; }
    if (u[0] == 0xFF && u[1] == 0xFE)                  { memmove(buf, buf + 2, strlen(buf + 2) + 1); return; }
    if (u[0] == 0xFE && u[1] == 0xFF)                  { memmove(buf, buf + 2, strlen(buf + 2) + 1); }
}

/* Forward declaration — galaxy_preprocess_includes calls jass_dofile_ex. */
bool jass_dofile_ex(jass_t * j, cstring_t fileName, JASSMODE mode);

/* Include-once guard: tracks files already loaded in this VM to prevent
 * re-parsing when multiple files include the same library. */
#define GALAXY_MAX_INCLUDES 256
static char galaxy_loaded[GALAXY_MAX_INCLUDES][512];
static uint32_t galaxy_loaded_n;

static bool galaxy_already_loaded(cstring_t path) {
    for (uint32_t i = 0; i < galaxy_loaded_n; i++) {
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
static void galaxy_preprocess_includes(jass_t * j, string_t buf, JASSMODE mode) {
    string_t cur = buf;
    while (*cur) {
        while (*cur && isspace((unsigned char)*cur)) cur++;
        if (strncmp(cur, "include", 7) == 0 &&
            (cur[7] == ' ' || cur[7] == '\t' || cur[7] == '"')) {
            string_t line_start = cur;
            cur += 7;
            while (*cur == ' ' || *cur == '\t') cur++;
            if (*cur == '"') {
                cur++;
                string_t path_start = cur;
                while (*cur && *cur != '"' && *cur != '\n') cur++;
                if (*cur == '"') {
                    uint32_t path_len = (uint32_t)(cur - path_start);
                    cur++;
                    char path[512];
                    uint32_t copy_len = path_len < 500 ? path_len : 500;
                    memcpy(path, path_start, copy_len);
                    path[copy_len] = '\0';
                    /* Append .galaxy if path has no extension. */
                    cstring_t slash = strrchr(path, '/');
                    cstring_t dot   = strrchr(path, '.');
                    if (!dot || (slash && dot < slash)) {
                        strlcat(path, ".galaxy", sizeof(path));
                    }
                    /* Erase directive in place; keep newlines for line numbers. */
                    for (string_t p = line_start; p < cur; p++) {
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

/* Global initializers can call natives before a script entry point establishes its call boundary. */
static void jass_evalprogram(jass_t * j, token_t const * program) {
    jass_t * root = jass_root(j);
    uint32_t base = j->num_stack;
    jassVar_t * saved = j->stack_pointer;
    if (root->current_coroutine || root->sync_rterror_jmp_set) { eval_TOKENS(j, program); return; }
    root->sync_rterror_jmp_set = true;
    if (!setjmp(root->sync_rterror_jmp)) eval_TOKENS(j, program);
    else { jass_discard(j, j->num_stack - base); j->stack_pointer = saved; }
    root->sync_rterror_jmp_set = false;
}

bool jass_dobuffer_ex(jass_t * j, string_t buffer, JASSMODE mode) {
    if (!buffer) {
        /* Missing mapscript used to SIGSEGV in jass_remove_comments(NULL). */
        jass_setruntimeerror(j, "null buffer");
        return false;
    }
    jass_remove_comments(buffer);
    jass_remove_bom(buffer);
    if ((uint32_t)mode >= sizeof(jass_syntax) / sizeof(jass_syntax[0])) {
        jass_t * root = jass_root(j);
        root->rterror_pending = true;
        snprintf(root->rterror_message, sizeof(root->rterror_message), "unknown syntax mode");
        return false;
    }
    const jassSyntax_t *syntax = &jass_syntax[mode];
    if (syntax->flags & SYNTAX_INCLUDES)
        galaxy_preprocess_includes(j, buffer, mode);
    wordExtractor_t parser = MAKE(wordExtractor_t, .buffer = buffer, .start = buffer, .delimiters = syntax->delimiters);
    token_t * program = syntax->parse(&parser);
    if (parser.error) {
        jass_t * root = jass_root(j);
        root->rterror_pending = true;
        snprintf(root->rterror_message, sizeof(root->rterror_message), "parse error");
        return false;
    }
    jassprogram_t * owned = JASSALLOC(jassprogram_t);
    owned->tokens = program;
    ADD_TO_LIST(owned, jass_root(j)->programs);
    jass_evalprogram(j, program);
    return !jass_rterror_pending(j);
}

bool jass_dobuffer(jass_t * j, string_t buffer) {
    return jass_dobuffer_ex(j, buffer, JASS_MODE_JASS);
}

typedef struct {
    uint32_t magic, version, identity, globals, coroutines;
} jassSnapshotHeader_t;

static uint32_t const jass_snapshot_magic = MAKEFOURCC('J', 'S', 'V', 'M');

/* Snapshot identity hashes immutable parser metadata, never process-local addresses. */
static uint32_t jass_snapshot_hashbytes(uint32_t hash, void const * data, size_t size) {
    uint8_t const *bytes = data;
    while (size--) hash = (hash ^ *bytes++) * 16777619u;
    return hash;
}

static uint32_t jass_snapshot_hashstr(uint32_t hash, cstring_t text) {
    uint32_t len = text ? (uint32_t)strlen(text) : 0;
    hash = jass_snapshot_hashbytes(hash, &len, sizeof(len));
    return len ? jass_snapshot_hashbytes(hash, text, len) : hash;
}

static uint32_t jass_snapshot_hashtokens(uint32_t hash, token_t const * token) {
    FOR_EACH_LIST(token_t const, item, token) {
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

static uint32_t jass_snapshot_identity(jass_t const * j) {
    uint32_t hash = 2166136261u;
    FOR_EACH_LIST(jassprogram_t const, program, j->programs) hash = jass_snapshot_hashtokens(hash, program->tokens);
    return hash;
}

uint32_t jass_programidentity(jass_t * j) { return jass_snapshot_identity(jass_root(j)); }

static bool jass_snapshot_io(jassSnapshot_t *snapshot, void *data, size_t size) {
    return size <= UINT32_MAX && snapshot && snapshot->transfer && snapshot->transfer(snapshot->context, data, (uint32_t)size);
}

static bool jass_snapshot_writestr(jassSnapshot_t *snapshot, cstring_t text) {
    uint32_t len = text ? (uint32_t)strlen(text) + 1 : 0;
    return jass_snapshot_io(snapshot, &len, sizeof(len)) && (!len || jass_snapshot_io(snapshot, (void *)text, len));
}

static bool jass_snapshot_readstr(jassSnapshot_t *snapshot, string_t *text) {
    uint32_t len;
    string_t value = NULL;
    if (!jass_snapshot_io(snapshot, &len, sizeof(len)) || len > BZ_JASS_SNAPSHOT_MAX_STRING) return false;
    if (len) {
        value = jass_alloc(len);
        if (!value || !jass_snapshot_io(snapshot, value, len) || value[len - 1]) { SAFE_DELETE(value, jass_free); return false; }
    }
    *text = value;
    return true;
}

static uint32_t jass_snapshot_arraycount(jassArray_t const * array) {
    uint32_t count = 0;
    FOR_EACH_LIST(jassArray_t const, item, array) count++;
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
    uint32_t id, size;
    cstring_t type;
    handle_t value;
    jassref_t * ref;
} jassSnapshotHandle_t;

static bool jass_snapshot_ownedhandle(cstring_t type) {
    static cstring_t const types[] = {
        "sound", "camerasetup", "rect", "location", "force", "gamecache", "region", "fogmodifier",
        "version", "itemtype", "attacktype", "damagetype", "weapontype", "soundtype", "pathingtype",
        "mousebuttontype", "aidifficulty", "playerscore"
    };
    FOR_LOOP(i, sizeof(types) / sizeof(*types)) if (!strcmp(type, types[i])) return true;
    return false;
}

static bool jass_snapshot_functionhandle(cstring_t type) {
    static cstring_t const types[] = { "boolexpr", "conditionfunc", "filterfunc" };
    FOR_LOOP(i, sizeof(types) / sizeof(*types)) if (!strcmp(type, types[i])) return true;
    return false;
}

static jassSnapshotHandle_t *jass_snapshot_findhandle(jassSnapshot_t *snapshot, uint32_t id) {
    jassSnapshotHandle_t *handles = snapshot->handles;
    FOR_EACH_LIST(jassSnapshotHandle_t, item, handles) if (item->id == id) return item;
    return NULL;
}

static void jass_snapshot_freehandles(jassSnapshot_t *snapshot) {
    while (snapshot->handles) {
        jassSnapshotHandle_t *item = snapshot->handles;
        snapshot->handles = item->next;
        jass_free(item);
    }
}

/* Handles use explicit encodings: native IDs relocate through the host, while safe VM-owned payloads carry identity+bytes. */
static bool jass_snapshot_writehandle(jass_t * j, jassSnapshot_t *snapshot, jassVar_t const * var) {
    uint32_t encoding, id;
    (void)j;
    if (jass_valuehandle(var->type->name)) {
        encoding = JASS_SNAPSHOT_HANDLE_VALUE;
        return jass_snapshot_io(snapshot, &encoding, sizeof(encoding)) &&
            jass_snapshot_io(snapshot, var->value, sizeof(uint32_t));
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

static bool jass_snapshot_readhandle(jass_t * j, jassSnapshot_t *snapshot, jassVar_t * var) {
    uint32_t encoding, id, size;
    handle_t value;
    string_t name = NULL;
    if (!jass_snapshot_io(snapshot, &encoding, sizeof(encoding))) return false;
    if (encoding == JASS_SNAPSHOT_HANDLE_NULL) {
        var->value = NULL;
        return true;
    }
    if (encoding == JASS_SNAPSHOT_HANDLE_VALUE) {
        if (!jass_valuehandle(var->type->name) || !(var->value = jass_alloc(sizeof(uint32_t))) ||
            !jass_snapshot_io(snapshot, var->value, sizeof(uint32_t))) return false;
        var->ref = jass_alloc(sizeof(*var->ref));
        if (!var->ref) return false;
        *var->ref = (jassref_t){ .size = sizeof(uint32_t) };
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
        var->value = value; *var->ref = (jassref_t){ .refs = 1 };
        return true;
    }
    if (encoding == JASS_SNAPSHOT_HANDLE_FUNCTION) {
        if (!jass_snapshot_functionhandle(var->type->name) || !jass_snapshot_readstr(snapshot, &name) || !name ||
            !(var->value = (handle_t)find_function(j, name))) { SAFE_DELETE(name, jass_free); return false; }
        SAFE_DELETE(name, jass_free);
        var->ref = jass_alloc(sizeof(*var->ref));
        if (!var->ref) return false;
        *var->ref = (jassref_t){ .refs = 1 };
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
    *item->ref = (jassref_t){ .size = size, .id = id };
    ADD_TO_LIST(item, snapshot->handles);
    var->value = item->value; var->ref = item->ref;
    jass_root(j)->next_handle_id = MAX(jass_root(j)->next_handle_id, id);
    return true;
}

/* Values carry their declared type so changed scripts and corrupt tags reject before mutation. */
static bool jass_snapshot_writevar(jass_t * j, jassSnapshot_t *snapshot, jassVar_t const * var) {
    uint32_t present = var->value || var->_array, count = jass_snapshot_arraycount(var->_array);
    JASSTYPEID base;
    if (!var->type) { fprintf(stderr, "JASS snapshot: value has no declared type\n"); return false; }
    base = jass_getvarbasetype(var);
    if (!jass_snapshot_writestr(snapshot, var->type ? var->type->name : NULL) ||
        !jass_snapshot_io(snapshot, &present, sizeof(present)) ||
        !jass_snapshot_io(snapshot, &count, sizeof(count))) return false;
    FOR_EACH_LIST(jassArray_t const, item, var->_array)
        if (!jass_snapshot_io(snapshot, (void *)&item->index, sizeof(item->index)) ||
            !jass_snapshot_writevar(j, snapshot, &item->value)) return false;
    if (!present || count) return true;
    switch (base) {
    case jasstype_integer: return jass_snapshot_io(snapshot, var->value, sizeof(int32_t));
    case jasstype_real: return jass_snapshot_io(snapshot, var->value, sizeof(float));
    case jasstype_boolean: return jass_snapshot_io(snapshot, var->value, sizeof(bool));
    case jasstype_string: return jass_snapshot_writestr(snapshot, var->value);
    case jasstype_code: return jass_snapshot_writestr(snapshot, jass_functionname(var->value));
    case jasstype_handle: return jass_snapshot_writehandle(j, snapshot, var);
    default: fprintf(stderr, "JASS snapshot: unsupported value type %s\n", var->type->name); return false;
    }
}

static bool jass_snapshot_readvar(jass_t * j, jassSnapshot_t *snapshot, jassVar_t * var) {
    string_t type = NULL, text = NULL;
    uint32_t present, count;
    jassType_t const * declared;
    if (!jass_snapshot_readstr(snapshot, &type) || !type || !(declared = find_type(j, type)) ||
        !jass_snapshot_io(snapshot, &present, sizeof(present)) || present > 1 ||
        !jass_snapshot_io(snapshot, &count, sizeof(count)) || count > BZ_JASS_SNAPSHOT_MAX_COUNT) {
        SAFE_DELETE(type, jass_free); return false;
    }
    SAFE_DELETE(type, jass_free);
    var->type = declared;
    FOR_LOOP(i, count) {
        uint32_t index;
        if (!jass_snapshot_io(snapshot, &index, sizeof(index)) ||
            !jass_snapshot_readvar(j, snapshot, ensure_array_value(j, var, index))) return false;
    }
    if (!present || count) return present == !!count;
    switch (jass_getvarbasetype(var)) {
    case jasstype_integer: return (var->value = jass_alloc(sizeof(int32_t))) && jass_snapshot_io(snapshot, var->value, sizeof(int32_t));
    case jasstype_real: return (var->value = jass_alloc(sizeof(float))) && jass_snapshot_io(snapshot, var->value, sizeof(float));
    case jasstype_boolean: return (var->value = jass_alloc(sizeof(bool))) && jass_snapshot_io(snapshot, var->value, sizeof(bool));
    case jasstype_string:
        if (!jass_snapshot_readstr(snapshot, &text) || !text) return false;
        var->value = text; return true;
    case jasstype_code:
        if (!jass_snapshot_readstr(snapshot, &text) || !text) return false;
        var->value = (handle_t)find_function(j, text); SAFE_DELETE(text, jass_free); return var->value != NULL;
    case jasstype_handle: return jass_snapshot_readhandle(j, snapshot, var);
    default: fprintf(stderr, "JASS snapshot: unsupported value type %s\n", var->type->name); return false;
    }
}

static uint32_t jass_snapshot_globalcount(jass_t const * j) {
    uint32_t count = 0;
    FOR_EACH_LIST(jassdict_t const, item, j->globals) if (!item->value.constant) count++;
    return count;
}

static uint32_t jass_snapshot_coroutinecount(jass_t const * j) {
    uint32_t count = 0;
    FOR_EACH_LIST(jasscoroutine_t const, co, j->coroutines) if (!co->done) count++;
    return count;
}

static bool jass_snapshot_findtoken(token_t const * token, token_t const * wanted, uint32_t *ordinal, uint32_t *found) {
    FOR_EACH_LIST(token_t const, item, token) {
        uint32_t current = (*ordinal)++;
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

static uint32_t jass_snapshot_tokenid(jass_t const * j, token_t const * wanted) {
    uint32_t ordinal = 0, found = UINT32_MAX;
    if (!wanted) return UINT32_MAX;
    FOR_EACH_LIST(jassprogram_t const, program, j->programs)
        if (jass_snapshot_findtoken(program->tokens, wanted, &ordinal, &found)) return found;
    return UINT32_MAX;
}

static token_t const * jass_snapshot_gettoken(token_t const * token, uint32_t wanted, uint32_t *ordinal) {
    FOR_EACH_LIST(token_t const, item, token) {
        if ((*ordinal)++ == wanted) return item;
        token_t const * found = jass_snapshot_gettoken(item->init, wanted, ordinal);
        if (!found) found = jass_snapshot_gettoken(item->body, wanted, ordinal);
        if (!found) found = jass_snapshot_gettoken(item->args, wanted, ordinal);
        if (!found) found = jass_snapshot_gettoken(item->condition, wanted, ordinal);
        if (!found) found = jass_snapshot_gettoken(item->elseblock, wanted, ordinal);
        if (!found) found = jass_snapshot_gettoken(item->index, wanted, ordinal);
        if (found) return found;
    }
    return NULL;
}

static token_t const * jass_snapshot_token(jass_t const * j, uint32_t wanted) {
    uint32_t ordinal = 0;
    if (wanted == UINT32_MAX) return NULL;
    FOR_EACH_LIST(jassprogram_t const, program, j->programs) {
        token_t const * found = jass_snapshot_gettoken(program->tokens, wanted, &ordinal);
        if (found) return found;
    }
    return NULL;
}

static uint32_t jass_snapshot_dictcount(jassdict_t const * dict) {
    uint32_t count = 0;
    FOR_EACH_LIST(jassdict_t const, item, dict) count++;
    return count;
}

static bool jass_snapshot_writedict(jass_t * j, jassSnapshot_t *snapshot, jassdict_t const * dict) {
    uint32_t count = jass_snapshot_dictcount(dict);
    if (!jass_snapshot_io(snapshot, &count, sizeof(count))) return false;
    FOR_EACH_LIST(jassdict_t const, item, dict)
        if (!jass_snapshot_writestr(snapshot, item->key) || !jass_snapshot_writevar(j, snapshot, &item->value)) return false;
    return true;
}

static bool jass_snapshot_readdict(jass_t * j, jassSnapshot_t *snapshot, jassdict_t * *dict) {
    uint32_t count;
    if (!jass_snapshot_io(snapshot, &count, sizeof(count)) || count > BZ_JASS_SNAPSHOT_MAX_COUNT) return false;
    FOR_LOOP(i, count) {
        jassdict_t * item = JASSALLOC(jassdict_t);
        string_t name = NULL;
        if (!jass_snapshot_readstr(snapshot, &name) || !name || find_dict(*dict, name)) {
            SAFE_DELETE(name, jass_free); jass_free(item); return false;
        }
        item->key = name;
        if (!jass_snapshot_readvar(j, snapshot, &item->value)) { jass_deletedict(item); return false; }
        PUSH_BACK(jassdict_t, item, *dict);
    }
    return true;
}

static bool jass_snapshot_writecontext_handle(jassSnapshot_t *snapshot, cstring_t type, handle_t value) {
    uint32_t present = value != NULL, id = 0;

    if (!present) return jass_snapshot_io(snapshot, &present, sizeof(present));
    if (!jass_host.SaveHandle) {
        fprintf(stderr, "JASS snapshot: no host codec for %s context handle\n", type);
        return false;
    }
    if (!jass_host.SaveHandle(type, value, &id)) {
        /* Match global-handle semantics: host-owned objects may disappear while a yielded
         * coroutine still retains them in its event context. A stale context handle is
         * observationally null after the object has been removed, so do not make the
         * entire save fail merely because that coroutine has not resumed yet. */
        present = false;
        fprintf(stderr, "JASS snapshot: stale %s context handle %p saved as null\n", type, value);
        return jass_snapshot_io(snapshot, &present, sizeof(present));
    }

    return jass_snapshot_io(snapshot, &present, sizeof(present)) &&
        jass_snapshot_io(snapshot, &id, sizeof(id));
}

static bool jass_snapshot_writecontext(jassSnapshot_t *snapshot, jassContext_t const * context) {
    struct { cstring_t type; handle_t value; } handles[] = {
        { "trigger", context->trigger }, { "unit", context->unit }, { "unit", context->source },
        { "player", context->playerState }, { "player", context->localPlayerState },
        { "timer", context->timer }, { "region", context->region },
    };
    if (!jass_snapshot_writestr(snapshot, jass_functionname(context->func)) ||
        !jass_snapshot_io(snapshot, (void *)&context->eventValue, sizeof(context->eventValue)) ||
        !jass_snapshot_io(snapshot, (void *)&context->point, sizeof(context->point)) ||
        !jass_snapshot_io(snapshot, (void *)&context->hasPoint, sizeof(context->hasPoint)) ||
        !jass_snapshot_io(snapshot, (void *)&context->timer_generation, sizeof(context->timer_generation)) ||
        !jass_snapshot_io(snapshot, (void *)&context->timer_pending, sizeof(context->timer_pending))) return false;
    FOR_LOOP(i, sizeof(handles) / sizeof(*handles))
        if (!jass_snapshot_writecontext_handle(snapshot, handles[i].type, handles[i].value)) return false;
    return true;
}

static bool jass_snapshot_readcontext(jass_t * j, jassSnapshot_t *snapshot, jassContext_t * context) {
    struct { cstring_t type; handle_t *value; } handles[] = {
        { "trigger", (handle_t *)&context->trigger }, { "unit", (handle_t *)&context->unit },
        { "unit", (handle_t *)&context->source }, { "player", (handle_t *)&context->playerState },
        { "player", (handle_t *)&context->localPlayerState }, { "timer", &context->timer },
        { "region", &context->region },
    };
    string_t func = NULL;
    bool has_func;
    if (!jass_snapshot_readstr(snapshot, &func)) return false;
    has_func = func != NULL;
    context->func = func ? find_function(j, func) : NULL;
    SAFE_DELETE(func, jass_free);
    if (has_func && !context->func) return false;
    if (!jass_snapshot_io(snapshot, &context->eventValue, sizeof(context->eventValue)) ||
        !jass_snapshot_io(snapshot, &context->point, sizeof(context->point)) ||
        !jass_snapshot_io(snapshot, &context->hasPoint, sizeof(context->hasPoint)) ||
        !jass_snapshot_io(snapshot, &context->timer_generation, sizeof(context->timer_generation)) ||
        !jass_snapshot_io(snapshot, &context->timer_pending, sizeof(context->timer_pending)) ||
        context->hasPoint > 1 || context->timer_pending > 1) return false;
    FOR_LOOP(i, sizeof(handles) / sizeof(*handles)) {
        uint32_t present, id;
        if (!jass_snapshot_io(snapshot, &present, sizeof(present)) || present > 1) return false;
        if (present && (!jass_snapshot_io(snapshot, &id, sizeof(id)) || !jass_host.LoadHandle ||
            !(*handles[i].value = jass_host.LoadHandle(handles[i].type, id)))) return false;
    }
    return true;
}

static bool jass_snapshot_writecoroutines(jass_t * j, jassSnapshot_t *snapshot) {
    uint32_t count = jass_snapshot_coroutinecount(j), now = jass_gettime();
    if (!jass_snapshot_io(snapshot, &count, sizeof(count))) return false;
    FOR_EACH_LIST(jasscoroutine_t const, co, j->coroutines) {
        uint32_t frames = 0, remaining = co->wake_time > now ? co->wake_time - now : 0;
        if (co->done) continue;
        FOR_EACH_LIST(jassCoroutineframe_t const, frame, co->frames) frames++;
        if (!jass_snapshot_writecontext(snapshot, &co->state->context) ||
            !jass_snapshot_io(snapshot, &remaining, sizeof(remaining)) ||
            !jass_snapshot_io(snapshot, &frames, sizeof(frames))) return false;
        FOR_EACH_LIST(jassCoroutineframe_t const, frame, co->frames) {
            uint32_t body = jass_snapshot_tokenid(j, frame->body), pc = jass_snapshot_tokenid(j, frame->pc);
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

static bool jass_snapshot_readcoroutines(jass_t * j, jassSnapshot_t *snapshot, jasscoroutine_t * *list) {
    uint32_t count, now = jass_gettime();
    if (!jass_snapshot_io(snapshot, &count, sizeof(count)) || count > BZ_JASS_SNAPSHOT_MAX_COUNT) return false;
    FOR_LOOP(i, count) {
        jass_t * state = JASSALLOC(jass_t);
        jasscoroutine_t * co = JASSALLOC(jasscoroutine_t);
        jassCoroutineframe_t * *tail = &co->frames;
        uint32_t remaining, frames;
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
            jassCoroutineframe_t * frame = JASSALLOC(jassCoroutineframe_t);
            string_t func = NULL;
            uint32_t body, pc;
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
        PUSH_BACK(jasscoroutine_t, co, *list);
    }
    return true;
}

bool jass_writesnapshot(jass_t * j, jassSnapshot_t *snapshot) {
    jass_t * root = jass_root(j);
    jassSnapshotHeader_t header = {
        jass_snapshot_magic, BZ_JASS_SNAPSHOT_VERSION, jass_snapshot_identity(root), jass_snapshot_globalcount(root),
        jass_snapshot_coroutinecount(root)
    };
    if (root->current_coroutine || root->sync_rterror_jmp_set) {
        fprintf(stderr, "JASS snapshot: save requested inside an active VM frame\n"); return false;
    }
    if (!jass_snapshot_io(snapshot, &header, sizeof(header))) return false;
    FOR_EACH_LIST(jassdict_t const, item, root->globals)
        if (!item->value.constant && (!jass_snapshot_writestr(snapshot, item->key) ||
            !jass_snapshot_writevar(root, snapshot, &item->value))) {
            fprintf(stderr, "JASS snapshot: global '%s' could not be saved\n", item->key);
            return false;
        }
    return jass_snapshot_writecoroutines(root, snapshot);
}

bool jass_readsnapshot(jass_t * j, jassSnapshot_t *snapshot) {
    jass_t * root = jass_root(j);
    jassSnapshotHeader_t header;
    jassdict_t * staged = NULL;
    jasscoroutine_t * coroutines = NULL;
    bool ok = false;
    if (root->current_coroutine || root->sync_rterror_jmp_set || !jass_snapshot_io(snapshot, &header, sizeof(header)) ||
        header.magic != jass_snapshot_magic || header.version != BZ_JASS_SNAPSHOT_VERSION ||
        header.identity != jass_snapshot_identity(root) || header.globals != jass_snapshot_globalcount(root) ||
        header.globals > BZ_JASS_SNAPSHOT_MAX_COUNT || header.coroutines > BZ_JASS_SNAPSHOT_MAX_COUNT) return false;
    FOR_LOOP(i, header.globals) {
        string_t name = NULL;
        jassdict_t * item = NULL;
        jassVar_t * live;
        if (!jass_snapshot_readstr(snapshot, &name) || !name || find_dict(staged, name) ||
            !(live = find_global(root, name)) || live->constant) { SAFE_DELETE(name, jass_free); goto done; }
        item = JASSALLOC(jassdict_t);
        item->key = name;
        if (!jass_snapshot_readvar(root, snapshot, &item->value)) { jass_deletedict(item); goto done; }
        ADD_TO_LIST(item, staged);
    }
    if (!jass_snapshot_readcoroutines(root, snapshot, &coroutines) ||
        header.coroutines != jass_snapshot_coroutinecount(&(jass_t){ .coroutines = coroutines })) goto done;
    FOR_EACH_LIST(jassdict_t, item, staged) {
        jassVar_t * live = find_global(root, item->key);
        if (live->type != item->value.type) goto done;
    }
    FOR_EACH_LIST(jassdict_t, item, staged) jass_copy(root, find_global(root, item->key), &item->value);
    while (root->coroutines) {
        jasscoroutine_t * next = root->coroutines->next;
        jass_free_coroutine(root->coroutines); root->coroutines = next;
    }
    root->coroutines = coroutines; coroutines = NULL;
    ok = true;
done:
    SAFE_DELETE(staged, jass_deletedict);
    while (coroutines) { jasscoroutine_t * next = coroutines->next; jass_free_coroutine(coroutines); coroutines = next; }
    jass_snapshot_freehandles(snapshot);
    return ok;
}

jass_t * jass_newstate(void) {
    jass_t * j = JASSALLOC(jass_t);
    j->stack_pointer = j->stack;
    j->root = j;
    galaxy_loaded_reset(); /* each new VM session starts with a fresh include-once guard */
    return j;
}

void jass_close(jass_t * j) {
    jass_t * root = jass_root(j);
    jasscoroutine_t * co = root->coroutines;
    while (co) {
        jasscoroutine_t * next = co->next;
        jass_free_coroutine(co);
        co = next;
    }
    FOR_LOOP(i, root->num_stack) jass_setnull(root->stack + i);
    SAFE_DELETE(root->globals, jass_deletedict);
    DELETE_LIST(jassmissing_t, root->missing, jass_free);
    while (root->functions) {
        jassFunc_t * func = root->functions, *next = func->next;
        DELETE_LIST(jassarg_t, func->args, jass_free);
        jass_free(func);
        root->functions = next;
    }
    DELETE_LIST(jassType_t, root->types, jass_free);
    while (root->programs) {
        jassprogram_t * program = root->programs, *next = program->next;
        JASS_FreeTokens(program->tokens);
        jass_free(program);
        root->programs = next;
    }
    jass_free(root);
}

bool jass_dofile_ex(jass_t * j, cstring_t fileName, JASSMODE mode) {
    uint32_t size = 0;
    string_t buffer = jass_host.ReadFile(fileName, &size);
    if (buffer) {
        string_t nul_terminated = jass_alloc(size + 1);
        memcpy(nul_terminated, buffer, size);
        nul_terminated[size] = '\0';
        jass_free(buffer);
        bool success = jass_dobuffer_ex(j, nul_terminated, mode);
        jass_free(nul_terminated);
        return success;
    } else {
        return false;
    }
}

bool jass_dofile(jass_t * j, cstring_t fileName) {
    /* Auto-detect Galaxy mode from file extension. */
    cstring_t dot = strrchr(fileName, '.');
    JASSMODE mode = (dot && !strcmp(dot, ".galaxy")) ? JASS_MODE_GALAXY : JASS_MODE_JASS;
    return jass_dofile_ex(j, fileName, mode);
}

/* =========================================================================
 * jass_call — invoke function on stack
 * ========================================================================= */

#ifdef DEBUG_JASS
static int depth = 0, callnum = 0;
#endif

static uint32_t jass_call_impl(jass_t * j, uint32_t args) {
    jassVar_t * root = &j->stack[j->num_stack - args - 1];
    jassVar_t * old_stack_pointer = j->stack_pointer;
    uint32_t ret = 0;
    j->stack_pointer = &j->stack[j->num_stack - args - 1];
#ifdef DEBUG_JASS
    callnum++;
    depth++;
    FOR_LOOP(i, depth) printf(" ");
#endif
    if (jass_getvarbasetype(root) == jasstype_cfunction) {
        jassCFunction_t func = *(jassCFunction_t *)root->value;
#ifdef DEBUG_JASS
        for (uint32_t i = 0; jass_host.natives[i].name; i++) {
            if (jass_host.natives[i].func == func) {
                printf("%s (native)", jass_host.natives[i].name);
                break;
            }
        }
        for (uint32_t i = 0; jass_operators[i].name; i++) {
            if (jass_operators[i].func == func) {
                printf("%s (native)", jass_operators[i].name);
                break;
            }
        }
        printf("\n");
#endif
        ret = func(j);
    } else {
        jassFunc_t const * func = root->value;
        jassdict_t * locals = NULL;
        uint32_t argnum = 1;
#ifdef DEBUG_JASS
        printf("%s\n", func->name);
#endif
        if (func->nativefunc) {
            ret = func->nativefunc(j);
        } else {
            FOR_EACH_LIST(jassarg_t, arg, func->args) {
                jassdict_t * local = JASSALLOC(jassdict_t);
                local->key = arg->name;
                local->value.type = arg->type;
                jass_copy(j, &local->value, &j->stack_pointer[argnum]);
                PUSH_BACK(jassdict_t, local, locals);
                argnum++;
            }
            root->env.done = false;
            root->env.returnstack = -1;
            root->env.locals = locals;
            eval_TOKENS(j, func->code);
            if (root->env.returnstack != -1) ret = j->num_stack - root->env.returnstack;
        }
    }
    jassVar_t * last = &j->stack[j->num_stack - ret];
    for (jassVar_t * it = root; it < last; it++) jass_setnull(it);
    memmove(root, last, ret * sizeof(jassVar_t));
    j->num_stack -= last - root;
    j->stack_pointer = old_stack_pointer;
#ifdef DEBUG_JASS
    depth--;
#endif
    return ret;
}

/* Synchronous native failures unwind to the outer call instead of executing later script statements. */
uint32_t jass_call(jass_t * j, uint32_t args) {
    jass_t * root = jass_root(j);
    jassVar_t * stack_pointer = j->stack_pointer;
    uint32_t stack_base = j->num_stack - args - 1;
    uint32_t ret;

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

void jass_callbyname(jass_t * j, cstring_t name, bool spawn_coroutine) {
    jassFunc_t const * func = find_function(j, name);
    if (!func) {
        jass_missingcall(j, name, false);
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
