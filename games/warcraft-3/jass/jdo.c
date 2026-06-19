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

#define JASS_DELIM     ",;()[]+-/*="
#define JASS_CONSTANT  "constant"
#define JASS_ARRAY     "array"
#define JASS_NULL      "null"
#define JASS_FALSE     "false"
#define JASS_TRUE      "true"
#define JASS_UNM       "-"
#define JASS_COMMA     ","
#define JASS_OPERATOR(NAME) { #NAME, NAME }
#define INF_LOOP_PROTECTION 1024

#define assert_type(var, type) assert(jass_checktype(var, type))
#define JASSALLOC(type) jass_alloc(sizeof(type))

static void jass_setnull(LPJASSVAR var);

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

LPCSTR keywords[] = {
    "elseif", "else", "endif", "set", "endfunction", "local", "then", NULL
};

static JASSHOST jass_host;


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

void jass_sethost(JASSHOST const *host) {
    jass_host = *host;
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

static BOOL var_eq(LPCJASSVAR a, LPCJASSVAR b) {
    if (jass_getvarbasetype(a) != jass_getvarbasetype(b)) {
        return false;
    }
    switch ((a->value == NULL) + (b->value == NULL)) {
        case 2: return true;
        case 1: return false;
    }
    switch (jass_getvarbasetype(a)) {
        case jasstype_integer: return !memcmp(a->value, b->value, sizeof(LONG));
        case jasstype_real: return !memcmp(a->value, b->value, sizeof(FLOAT));
        case jasstype_string: return !strcmp(a->value, b->value);
        case jasstype_boolean: return !memcmp(a->value, b->value, sizeof(BOOL));
        case jasstype_code: return !memcmp(a->value, b->value, sizeof(HANDLE));
        case jasstype_cfunction: return !memcmp(a->value, b->value, sizeof(HANDLE));
        case jasstype_handle: return !memcmp(a->value, b->value, sizeof(HANDLE));
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

static LPJASS jass_root(LPJASS j) {
    return j->root ? j->root : j;
}

/* =========================================================================
 * Coroutine frame management
 * ========================================================================= */

static void jass_free_coroutine(LPJASSCOROUTINE co) {
    DELETE_LIST(JASSCOROUTINEFRAME, co->frames, jass_free);
    if (co->state) {
        jass_free(co->state);
    }
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
        jass_free(frame);
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
            jass_dotoken(j, arg_token);
            jass_copy(j, &local->value, jass_topvalue(j));
            jass_pop(j, 1);
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
    if (!co_state->context.playerState) {
        co_state->context.playerState = currentplayer;
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

LPCSTR jass_functionname(LPCJASSFUNC func) {
    return func ? func->name : NULL;
}

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

void jass_rterror(LPJASS j, LPCSTR message) {
    LPJASS root = jass_root(j);
    root->rterror_pending = true;
    snprintf(root->rterror_message, sizeof(root->rterror_message), "%s", message ? message : "(nil)");
    fprintf(stderr, "JASS runtime error: %s\n", root->rterror_message);

    LPJASSCOROUTINE co = root->current_coroutine;
    if (co && co->rterror_jmp_set) {
        longjmp(co->rterror_jmp, 1);
    }
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
                assert(frame->loop_count++ < INF_LOOP_PROTECTION);
            } else {
                jass_coroutine_popframe(co);
            }
            continue;
        }

        next = token->next;
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
    currentplayer = co->state->context.playerState;
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
        co = next;
    }
}

/* =========================================================================
 * Trigger evaluation / execution
 * ========================================================================= */

BOOL jass_evaluatetrigger(LPJASS j, LPTRIGGER trigger, LPEDICT unit) {
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
        tmp_state.context.playerState = player;
        jass_pushfunction(&tmp_state, cond->expr);
        LPPLAYER previous_player = currentplayer;
        LPEDICT previous_unit = currentunit;
        currentplayer = player;
        currentunit = unit;
        DWORD result_count = jass_call(&tmp_state, 0);
        currentunit = previous_unit;
        currentplayer = previous_player;
        if (result_count != 1 || !jass_popboolean(&tmp_state)) {
            return false;
        }
    }
    return true;
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

void jass_executetrigger(LPJASS j, LPTRIGGER trigger, LPEDICT unit) {
    FOR_EACH_LIST(TRIGGERACTION, action, trigger->actions) {
        LPPLAYER player = jass_eventplayer(unit);
        jass_startcoroutine(j, &MAKE(JASSCONTEXT,
                                  .trigger = trigger,
                                  .func = action->func,
                                  .unit = unit,
                                  .playerState = player,
                              ));
    }
}

BOOL jass_calltrigger(LPJASS j, LPTRIGGER trigger, LPEDICT unit) {
    if (jass_evaluatetrigger(j, trigger, unit)) {
        jass_executetrigger(j, trigger, unit);
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
    return NULL;
}

static LPCJASSFUNC find_function(LPCJASS j, LPCSTR name) {
    FOR_EACH_LIST(JASSFUNC, func, j->functions) {
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

void jass_setnull(LPJASSVAR var) {
    if (!var || !var->type) {
        return;
    }
    uintptr_t typeaddr = (uintptr_t)var->type;
    if ((typeaddr & (sizeof(void *) - 1)) || typeaddr < 4096 || typeaddr >= 0x0000800000000000ULL) {
        memset(var, 0, sizeof(*var));
        return;
    }
    JASSTYPEID type = jass_getvarbasetype(var);
    if (type == jasstype_code || type == jasstype_cfunction) {
        SAFE_DELETE(var->env.locals, jass_deletedict);
    }
    switch (type) {
        case jasstype_handle:
            if (var->refcount && *var->refcount > 0) {
                (*var->refcount)--;
                var->value = NULL;
                var->refcount = NULL;
            } else {
                SAFE_DELETE(var->value, jass_free);
                SAFE_DELETE(var->refcount, jass_free);
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
            assert(other->type == var->type);
            jass_store_value(var, other->value, sizeof(LONG));
            break;
        case jasstype_handle:
            if (!is_handle_convertible(other->type, var->type)) {
                fprintf(stderr, "Warning: Passing %s to %s type\n", other->type->name, var->type->name);
            }
            var->value = other->value;
            var->refcount = other->refcount;
            if (var->refcount) {
                (*var->refcount)++;
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
                    assert(false);
                    return;
            }
            jass_store_value(var, &fval, sizeof(FLOAT));
            break;
        case jasstype_boolean:
            assert(other->type == var->type);
            jass_store_value(var, other->value, sizeof(BOOL));
            break;
        case jasstype_string:
            assert(other->type == var->type);
            jass_store_value(var, other->value, strlen(other->value)+1);
            break;
        case jasstype_code:
        case jasstype_cfunction:
            assert(other->type == var->type);
            var->value = other->value;
            break;
        default:
            assert(false);
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
        var->refcount = jass_alloc(sizeof(DWORD));
    }
    return 1;
}

DWORD jass_pushnullhandle(LPJASS j, LPCSTR type) {
    return jass_pushhandle(j, 0, type);
}

HANDLE jass_newhandle(LPJASS j, DWORD size, LPCSTR type) {
    HANDLE data = size ? jass_alloc(size) : NULL;
    jass_pushhandle(j, data, type);
    return data;
}

DWORD jass_pushlighthandle(LPJASS j, HANDLE value, LPCSTR type) {
    JASS_ADD_STACK(j, var, jasstype_handle);
    jass_setnull(var);
    var->type = find_type(j, type);
    var->value = value;
    var->refcount = jass_alloc(sizeof(DWORD));
    *var->refcount = 1;
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
    assert(false);
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
    assert(is_handle_convertible(var->type, find_type(j, type)));
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
    } else if ((v = find_dict(j->globals, token->primary))) {
        return jass_pushvalue(j, v);
    } else if ((v = find_dict(jass_stackvalue(j, 0)->env.locals, token->primary))) {
        return jass_pushvalue(j, v);
    } else {
        return jass_pushnull(j);
    }
}

DWORD VM_EvalArrayAccess(LPJASS j, LPCTOKEN token) {
    assert(jass_dotoken(j, token->index) == 1);
    DWORD index_val = jass_popinteger(j);
    VM_EvalIdentifier(j, token);
    LPJASSVAR var = jass_stackvalue(j, -1);
    LPJASSVAR item = ensure_array_value(j, var, index_val);
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
        assert(false);
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
    assert(false);
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

static void jass_set_array_value(LPJASS j, LPJASSVAR dest, LPCTOKEN index, LPCTOKEN init) {
    assert(jass_dotoken(j, index) == 1);
    DWORD index_val = jass_popinteger(j);
    LPJASSVAR index_dest = ensure_array_value(j, dest, index_val);
    assert(jass_dotoken(j, init) == 1);
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
    if (token->type == TT_CALL && !strcmp(token->primary, "GetLocalPlayer")) {
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
    if (uses_localplayer(token->condition)) {
        FOR_LOOP(i, MAX_PLAYERS) {
            currentplayer = jass_getplayerbyindex(i);
            jass_dotoken(j, token->condition);
            if (jass_popboolean(j)) {
                eval_TOKENS(j, token->body);
            }
            currentplayer = NULL;
        }
        assert(!token->elseblock);
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
    if ((v = find_dict(j->globals, token->secondary))) {
        if (token->index) {
            return jass_set_array_value(j, v, token->index, token->init);
        } else {
            return jass_set_value(j, v, token->init);
        }
    } else if ((v = find_dict(jass_stackvalue(j, 0)->env.locals, token->secondary))) {
        if (token->index) {
            return jass_set_array_value(j, v, token->index, token->init);
        } else {
            return jass_set_value(j, v, token->init);
        }
    } else {
        fprintf(stderr, "Can't find variable %s\n", token->primary);
    }
}

TOKENFUNC(VARDECL) {
    LPJASSDICT vardecl = parse_dict(j, token);
    ADD_TO_LIST(vardecl, jass_stackvalue(j, 0)->env.locals);
}

TOKENFUNC(GLOBAL) {
    LPJASSDICT global = parse_dict(j, token);
    ADD_TO_LIST(global, j->globals);
}

TOKENFUNC(FUNCTION) {
    LPJASSFUNC func = JASSALLOC(JASSFUNC);
    func->name = token->primary;
    func->code = token->body;
    func->returns = find_type(j, token->secondary);
    FOR_EACH_LIST(TOKEN, arg, token->args) {
        LPJASSARG jarg = JASSALLOC(JASSARG);
        jarg->name = arg->secondary;
        jarg->type = find_type(j, arg->primary);
        PUSH_BACK(JASSARG, jarg, func->args);
    }
    if (token->flags & TF_NATIVE) {
        LPCJASSMODULE mod = find_in_array(jass_host.natives, sizeof(JASSMODULE), func->name);
        if (mod) {
            func->nativefunc = mod->func;
        }
    }
    ADD_TO_LIST(func, j->functions);
}

TOKENFUNC(CALL) {
    jass_discard(j, jass_dotoken(j, token));
}

TOKENFUNC(LOOP) {
    for (DWORD i = 0;; i++) {
        FOR_EACH_LIST(TOKEN const, tok, token->body) {
            if (jass_mustreturn(j) || jass_yielded(j)) {
                return;
            } else if (tok->type == TT_RETURN) {
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
    fprintf(stderr, "Can't evaluate token of type %d\n", token->type);
    assert(false);
}

TOKENFUNC(TOKENS) {
    FOR_EACH_LIST(TOKEN const, tok, token) {
        if (jass_mustreturn(j) || jass_yielded(j)) {
            return;
        } else if (tok->type == TT_RETURN) {
            jass_setreturn(j);
            jass_dotoken(j, tok->body);
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

BOOL jass_dobuffer(LPJASS j, LPSTR buffer) {
    jass_remove_comments(buffer);
    jass_remove_bom(buffer);
    LPTOKEN program = JASS_ParseTokens(&MAKE(PARSER, .buffer = buffer, .delimiters = JASS_DELIM));
    eval_TOKENS(j, program);
    return true;
}

LPJASS jass_newstate(void) {
    LPJASS j = JASSALLOC(JASS);
    j->stack_pointer = j->stack;
    j->root = j;
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
    jass_free(j);
}

BOOL jass_dofile(LPJASS j, LPCSTR fileName) {
    DWORD size = 0;
    LPSTR buffer = jass_host.ReadFile(fileName, &size);
    if (buffer) {
        /* ReadFile does not NUL-terminate; append one so dobuffer can work in-place. */
        LPSTR nul_terminated = jass_alloc(size + 1);
        memcpy(nul_terminated, buffer, size);
        nul_terminated[size] = '\0';
        jass_free(buffer);
        BOOL success = jass_dobuffer(j, nul_terminated);
        jass_free(nul_terminated);
        return success;
    } else {
        return false;
    }
}

/* =========================================================================
 * jass_call — invoke function on stack
 * ========================================================================= */

#ifdef DEBUG_JASS
static int depth = 0, callnum = 0;
#endif

DWORD jass_call(LPJASS j, DWORD args) {
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
