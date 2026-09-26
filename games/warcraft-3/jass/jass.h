#ifndef jass_h
#define jass_h

#include "game/g_local.h"
#include "game/api/api_macros.h"
#include "jass_api.h"

#define API_ALLOC(TYPE, NAME) TYPE *NAME = jass_newhandle(j, sizeof(TYPE), #NAME);

KNOWN_AS(jass_type, jassType_t);
KNOWN_AS(jass_var, jassVar_t);
KNOWN_AS(jass_context, jassContext_t);
KNOWN_AS(vm_program, vmprogram_t);

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
    vector3_t position;
    int32_t attached_entity;
    uint32_t attached_spawn_time;
    bool has_position;
} gsound_t;

struct vm_program {
    handle_t data;
    uint32_t size;
};

struct jass_context {
    trigger_t *trigger;
    edict_t *unit;
    edict_t *source;
    int32_t eventValue;
    vector2_t point;
    uint8_t hasPoint;
    player_t *playerState;
    player_t *localPlayerState;
    handle_t timer;
    handle_t region;
    uint32_t timer_generation;
    uint8_t timer_pending;
    jassFunc_t const *func;
};

int32_t jass_checkinteger(jass_t *j, int index);
float jass_checknumber(jass_t *j, int index);
bool jass_checkboolean(jass_t *j, int index);
cstring_t jass_checkstring(jass_t *j, int index);
jassFunc_t const *jass_checkcode(jass_t *j, int index);
handle_t jass_checkhandle(jass_t *j, int index, cstring_t type);
bool jass_toboolean(jass_t *j, int index);
uint32_t jass_call(jass_t *j, uint32_t args);
void jass_sethost(jassHost_t const *host);
jasscoroutine_t *jass_startcoroutine(jass_t *j, jassContext_t const *context);
jasscoroutine_t *jass_startcoroutinebyname(jass_t *j, cstring_t name);
bool jass_callcoroutinebyname(jass_t *j, cstring_t name);
bool jass_resume(jass_t *j, jasscoroutine_t *co);
bool jass_coroutinedone(jasscoroutine_t const *co);
void jass_runevents(jass_t *j);
void jass_sleep(jass_t *j, uint32_t msec);
cstring_t jass_functionname(jassFunc_t const *func);
cstring_t jass_currentfunctionname(jass_t *j);
uint32_t jass_formatcallchain(jass_t *j, string_t buffer, uint32_t size);
jassFunc_t const *jass_functionbyname(jass_t *j, cstring_t name);
void jass_settimercontext(handle_t timer);
bool jass_triggerdisabled(trigger_t *trigger);
JASSTYPEID jass_gettype(jass_t *j, int index);
uint32_t jass_pushnull(jass_t *j);
uint32_t jass_pushinteger(jass_t *j, int32_t value);
uint32_t jass_pushhandle(jass_t *j, handle_t value, cstring_t type);
uint32_t jass_pushlighthandle(jass_t *j, handle_t value, cstring_t type);
uint32_t jass_pushnumber(jass_t *j, float value);
uint32_t jass_pushboolean(jass_t *j, bool value);
uint32_t jass_pushstring(jass_t *j, cstring_t value);
uint32_t jass_pushstringlen(jass_t *j, cstring_t value, uint32_t len);
uint32_t jass_pushfunction(jass_t *j, jassFunc_t const *func);
uint32_t jass_pushnullhandle(jass_t *j, cstring_t type);
handle_t jass_newhandle(jass_t *j, uint32_t size, cstring_t type);
handle_t jass_alloc(long size);
void jass_free(handle_t ptr);
jassContext_t const *jass_getcontext(jass_t *j);
bool jass_calltriggerevent(jass_t *j, trigger_t *trigger, gameEvent_t const *event);
jass_t *jass_getroot(jass_t *j);
bool jass_isrunning(jass_t *j);
void jass_haltevents(jass_t *j);
bool jass_calltrigger(jass_t *j, trigger_t *trigger, edict_t *unit, edict_t *source);
bool jass_calltriggerwithvalue(jass_t *j, trigger_t *trigger, edict_t *unit, edict_t *source, int32_t eventValue);
bool jass_calltriggerwithtimer(jass_t *j, trigger_t *trigger, handle_t timer);
bool jass_popboolean(jass_t *j);
void jass_pop(jass_t *j, uint32_t count);
bool jass_evaluatetrigger(jass_t *j, trigger_t *trigger, edict_t *unit);
bool jass_evaluateboolexpr(jass_t *j, jassFunc_t const *expr, edict_t *unit);
bool jass_evaluateplayerexpr(jass_t *j, jassFunc_t const *expr, player_t *player);
void jass_executetrigger(jass_t *j, trigger_t *trigger, edict_t *unit);

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
void   jass_rterror(jass_t *j, cstring_t message);
bool   jass_rterror_pending(jass_t *j);
cstring_t jass_rterror_message(jass_t *j);
void   jass_rterror_clear(jass_t *j);

/* jass_callbyname — call a named JASS function.
 * spawn_coroutine=true: enqueue as a new coroutine (returns immediately).
 * spawn_coroutine=false: call synchronously on the current stack. */
void jass_callbyname(jass_t *j, cstring_t name, bool spawn_coroutine);

/* jass_dofile / jass_dobuffer — load and evaluate source (JASS mode).
 * jass_dofile auto-detects Galaxy mode for .galaxy filenames. */
bool jass_dofile(jass_t *j, cstring_t fileName);
bool jass_dobuffer(jass_t *j, string_t buffer);

/* _ex variants — explicit mode control. */
bool jass_dofile_ex(jass_t *j, cstring_t fileName, JASSMODE mode);
bool jass_dobuffer_ex(jass_t *j, string_t buffer, JASSMODE mode);

/* jass_newstate / jass_close — state lifecycle. */
jass_t *jass_newstate(void);
void   jass_close(jass_t *j);


#endif
