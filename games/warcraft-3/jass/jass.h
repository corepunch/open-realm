#ifndef jass_h
#define jass_h

#include "game/g_local.h"
#include "game/api/api_macros.h"
#include "jass_api.h"

#define API_ALLOC(TYPE, NAME) TYPE *NAME = jass_newhandle(j, sizeof(TYPE), #NAME);

KNOWN_AS(jass_type, JASSTYPE);
KNOWN_AS(jass_var, JASSVAR);
KNOWN_AS(jass_context, JASSCONTEXT);
KNOWN_AS(vm_program, VMPROGRAM);

typedef enum {
    CAMERA_FIELD_TARGET_DISTANCE,
    CAMERA_FIELD_FARZ,
    CAMERA_FIELD_ANGLE_OF_ATTACK,
    CAMERA_FIELD_FIELD_OF_VIEW,
    CAMERA_FIELD_ROLL,
    CAMERA_FIELD_ROTATION,
    CAMERA_FIELD_ZOFFSET,
    CAMERA_FIELD_NEARZ,
    CAMERA_FIELD_LOCAL_PITCH,
    CAMERA_FIELD_LOCAL_YAW,
    CAMERA_FIELD_LOCAL_ROLL,
} CAMERAFIELD;

typedef enum {
    UNIT_STATE_LIFE,
    UNIT_STATE_MAX_LIFE,
    UNIT_STATE_MANA,
    UNIT_STATE_MAX_MANA,
} UNITSTATE;

typedef enum {
    jasstype_integer,
    jasstype_real,
    jasstype_string,
    jasstype_boolean,
    jasstype_code,
    jasstype_handle,
    jasstype_cfunction,
} JASSTYPEID;

/* JASSHOST and JASSMODE moved to jass_api.h — included above */

typedef gameCache_t ggamecache_t;

typedef struct {
    PATHSTR fileName;
    bool looping;
    bool is3D;
    bool stopwhenoutofrange;
    int32_t fadeInRate;
    int32_t fadeOutRate;
    uint32_t duration;
    int soundIndex; /* CS_SOUNDS configstring index; populated by CreateSound */
    float volume;
    VECTOR3 position;
    int32_t attached_entity;
    uint32_t attached_spawn_time;
    bool has_position;
} gsound_t;

struct vm_program {
    handle_t data;
    uint32_t size;
};

struct jass_context {
    LPTRIGGER trigger;
    LPEDICT unit;
    LPEDICT source;
    int32_t eventValue;
    VECTOR2 point;
    uint8_t hasPoint;
    LPPLAYER playerState;
    LPPLAYER localPlayerState;
    handle_t timer;
    handle_t region;
    uint32_t timer_generation;
    uint8_t timer_pending;
    LPCJASSFUNC func;
};

int32_t jass_checkinteger(LPJASS j, int index);
float jass_checknumber(LPJASS j, int index);
bool jass_checkboolean(LPJASS j, int index);
cstring_t jass_checkstring(LPJASS j, int index);
LPCJASSFUNC jass_checkcode(LPJASS j, int index);
handle_t jass_checkhandle(LPJASS j, int index, cstring_t type);
bool jass_toboolean(LPJASS j, int index);
uint32_t jass_call(LPJASS j, uint32_t args);
void jass_sethost(JASSHOST const *host);
LPJASSCOROUTINE jass_startcoroutine(LPJASS j, LPCJASSCONTEXT context);
LPJASSCOROUTINE jass_startcoroutinebyname(LPJASS j, cstring_t name);
bool jass_callcoroutinebyname(LPJASS j, cstring_t name);
bool jass_resume(LPJASS j, LPJASSCOROUTINE co);
bool jass_coroutinedone(LPCJASSCOROUTINE co);
void jass_runevents(LPJASS j);
void jass_sleep(LPJASS j, uint32_t msec);
cstring_t jass_functionname(LPCJASSFUNC func);
cstring_t jass_currentfunctionname(LPJASS j);
uint32_t jass_formatcallchain(LPJASS j, string_t buffer, uint32_t size);
LPCJASSFUNC jass_functionbyname(LPJASS j, cstring_t name);
void jass_settimercontext(handle_t timer);
bool jass_triggerdisabled(LPTRIGGER trigger);
JASSTYPEID jass_gettype(LPJASS j, int index);
uint32_t jass_pushnull(LPJASS j);
uint32_t jass_pushinteger(LPJASS j, int32_t value);
uint32_t jass_pushhandle(LPJASS j, handle_t value, cstring_t type);
uint32_t jass_pushlighthandle(LPJASS j, handle_t value, cstring_t type);
uint32_t jass_pushnumber(LPJASS j, float value);
uint32_t jass_pushboolean(LPJASS j, bool value);
uint32_t jass_pushstring(LPJASS j, cstring_t value);
uint32_t jass_pushstringlen(LPJASS j, cstring_t value, uint32_t len);
uint32_t jass_pushfunction(LPJASS j, LPCJASSFUNC func);
uint32_t jass_pushnullhandle(LPJASS j, cstring_t type);
handle_t jass_newhandle(LPJASS j, uint32_t size, cstring_t type);
handle_t jass_alloc(long size);
void jass_free(handle_t ptr);
LPCJASSCONTEXT jass_getcontext(LPJASS j);
bool jass_calltriggerevent(LPJASS j, LPTRIGGER trigger, GAMEEVENT const *event);
LPJASS jass_getroot(LPJASS j);
bool jass_isrunning(LPJASS j);
void jass_haltevents(LPJASS j);
bool jass_calltrigger(LPJASS j, LPTRIGGER trigger, LPEDICT unit, LPEDICT source);
bool jass_calltriggerwithvalue(LPJASS j, LPTRIGGER trigger, LPEDICT unit, LPEDICT source, int32_t eventValue);
bool jass_calltriggerwithtimer(LPJASS j, LPTRIGGER trigger, handle_t timer);
bool jass_popboolean(LPJASS j);
void jass_pop(LPJASS j, uint32_t count);
bool jass_evaluatetrigger(LPJASS j, LPTRIGGER trigger, LPEDICT unit);
bool jass_evaluateboolexpr(LPJASS j, LPCJASSFUNC expr, LPEDICT unit);
bool jass_evaluateplayerexpr(LPJASS j, LPCJASSFUNC expr, LPPLAYER player);
void jass_executetrigger(LPJASS j, LPTRIGGER trigger, LPEDICT unit);

/* -------------------------------------------------------------------------
 * Runtime error / test-assertion boundary.
 *
 * jass_rterror() aborts the currently-executing coroutine via longjmp and
 * records a failure message on the root state.  It is the JASS equivalent
 * of Lua's error() — it never returns to the caller.
 *
 * jass_rterror_pending() returns true when a runtime error was recorded
 * since the last jass_rterror_clear().
 * jass_rterror_message() returns the message string (valid until cleared).
 * jass_rterror_clear() resets the error state.
 * ------------------------------------------------------------------------- */
void   jass_rterror(LPJASS j, cstring_t message);
bool   jass_rterror_pending(LPJASS j);
cstring_t jass_rterror_message(LPJASS j);
void   jass_rterror_clear(LPJASS j);

/* jass_callbyname — call a named JASS function.
 * spawn_coroutine=true: enqueue as a new coroutine (returns immediately).
 * spawn_coroutine=false: call synchronously on the current stack. */
void jass_callbyname(LPJASS j, cstring_t name, bool spawn_coroutine);

/* jass_dofile / jass_dobuffer — load and evaluate source (JASS mode).
 * jass_dofile auto-detects Galaxy mode for .galaxy filenames. */
bool jass_dofile(LPJASS j, cstring_t fileName);
bool jass_dobuffer(LPJASS j, string_t buffer);

/* _ex variants — explicit mode control. */
bool jass_dofile_ex(LPJASS j, cstring_t fileName, JASSMODE mode);
bool jass_dobuffer_ex(LPJASS j, string_t buffer, JASSMODE mode);

/* jass_newstate / jass_close — state lifecycle. */
LPJASS jass_newstate(void);
void   jass_close(LPJASS j);


#endif
