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
    BOOL looping;
    BOOL is3D;
    BOOL stopwhenoutofrange;
    LONG fadeInRate;
    LONG fadeOutRate;
    DWORD duration;
    int soundIndex; /* CS_SOUNDS configstring index; populated by CreateSound */
} gsound_t;

struct vm_program {
    HANDLE data;
    DWORD size;
};

struct jass_context {
    LPTRIGGER trigger;
    LPEDICT unit;
    LPEDICT source;
    LPPLAYER playerState;
    LPPLAYER localPlayerState;
    HANDLE timer;
    LPCJASSFUNC func;
};

LONG jass_checkinteger(LPJASS j, int index);
FLOAT jass_checknumber(LPJASS j, int index);
BOOL jass_checkboolean(LPJASS j, int index);
LPCSTR jass_checkstring(LPJASS j, int index);
LPCJASSFUNC jass_checkcode(LPJASS j, int index);
HANDLE jass_checkhandle(LPJASS j, int index, LPCSTR type);
BOOL jass_toboolean(LPJASS j, int index);
DWORD jass_call(LPJASS j, DWORD args);
void jass_sethost(JASSHOST const *host);
LPJASSCOROUTINE jass_startcoroutine(LPJASS j, LPCJASSCONTEXT context);
LPJASSCOROUTINE jass_startcoroutinebyname(LPJASS j, LPCSTR name);
BOOL jass_callcoroutinebyname(LPJASS j, LPCSTR name);
BOOL jass_resume(LPJASS j, LPJASSCOROUTINE co);
BOOL jass_coroutinedone(LPCJASSCOROUTINE co);
void jass_runevents(LPJASS j);
void jass_sleep(LPJASS j, DWORD msec);
LPCSTR jass_functionname(LPCJASSFUNC func);
LPCJASSFUNC jass_functionbyname(LPJASS j, LPCSTR name);
void jass_settimercontext(HANDLE timer);
BOOL jass_triggerdisabled(LPTRIGGER trigger);
JASSTYPEID jass_gettype(LPJASS j, int index);
DWORD jass_pushnull(LPJASS j);
DWORD jass_pushinteger(LPJASS j, LONG value);
DWORD jass_pushhandle(LPJASS j, HANDLE value, LPCSTR type);
DWORD jass_pushlighthandle(LPJASS j, HANDLE value, LPCSTR type);
DWORD jass_pushnumber(LPJASS j, FLOAT value);
DWORD jass_pushboolean(LPJASS j, BOOL value);
DWORD jass_pushstring(LPJASS j, LPCSTR value);
DWORD jass_pushstringlen(LPJASS j, LPCSTR value, DWORD len);
DWORD jass_pushfunction(LPJASS j, LPCJASSFUNC func);
DWORD jass_pushnullhandle(LPJASS j, LPCSTR type);
HANDLE jass_newhandle(LPJASS j, DWORD size, LPCSTR type);
HANDLE jass_alloc(long size);
void jass_free(HANDLE ptr);
LPCJASSCONTEXT jass_getcontext(LPJASS j);
LPJASS jass_getroot(LPJASS j);
BOOL jass_isrunning(LPJASS j);
void jass_haltevents(LPJASS j);
BOOL jass_calltrigger(LPJASS j, LPTRIGGER trigger, LPEDICT unit, LPEDICT source);
BOOL jass_popboolean(LPJASS j);
void jass_pop(LPJASS j, DWORD count);
BOOL jass_evaluatetrigger(LPJASS j, LPTRIGGER trigger, LPEDICT unit);
BOOL jass_evaluateboolexpr(LPJASS j, LPCJASSFUNC expr, LPEDICT unit);
BOOL jass_evaluateplayerexpr(LPJASS j, LPCJASSFUNC expr, LPPLAYER player);
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
void   jass_rterror(LPJASS j, LPCSTR message);
BOOL   jass_rterror_pending(LPJASS j);
LPCSTR jass_rterror_message(LPJASS j);
void   jass_rterror_clear(LPJASS j);

/* jass_callbyname — call a named JASS function.
 * spawn_coroutine=true: enqueue as a new coroutine (returns immediately).
 * spawn_coroutine=false: call synchronously on the current stack. */
void jass_callbyname(LPJASS j, LPCSTR name, BOOL spawn_coroutine);

/* jass_dofile / jass_dobuffer — load and evaluate source (JASS mode).
 * jass_dofile auto-detects Galaxy mode for .galaxy filenames. */
BOOL jass_dofile(LPJASS j, LPCSTR fileName);
BOOL jass_dobuffer(LPJASS j, LPSTR buffer);

/* _ex variants — explicit mode control. */
BOOL jass_dofile_ex(LPJASS j, LPCSTR fileName, JASSMODE mode);
BOOL jass_dobuffer_ex(LPJASS j, LPSTR buffer, JASSMODE mode);

/* jass_newstate / jass_close — state lifecycle. */
LPJASS jass_newstate(void);
void   jass_close(LPJASS j);


#endif
