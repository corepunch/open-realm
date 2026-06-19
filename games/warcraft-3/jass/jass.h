#ifndef jass_h
#define jass_h

#include "game/g_local.h"
#include "game/api/api_macros.h"

#define MAX_GROUP_SIZE 256

#define API_ALLOC(TYPE, NAME) TYPE *NAME = jass_newhandle(j, sizeof(TYPE), #NAME);

KNOWN_AS(jass_function, JASSFUNC);
KNOWN_AS(jass_type, JASSTYPE);
KNOWN_AS(jass_var, JASSVAR);
KNOWN_AS(jass_module, JASSMODULE);
KNOWN_AS(jass_context, JASSCONTEXT);
KNOWN_AS(jass_coroutine, JASSCOROUTINE);
KNOWN_AS(vm_program, VMPROGRAM);

typedef enum {
    CAMERA_FIELD_TARGET_DISTANCE,
    CAMERA_FIELD_FARZ,
    CAMERA_FIELD_ANGLE_OF_ATTACK,
    CAMERA_FIELD_FIELD_OF_VIEW,
    CAMERA_FIELD_ROLL,
    CAMERA_FIELD_ROTATION,
    CAMERA_FIELD_ZOFFSET,
} CAMERAFIELD;

typedef enum {
    UNIT_STATE_LIFE,
    UNIT_STATE_MAX_LIFE,
    UNIT_STATE_MANA,
    UNIT_STATE_MAX_MANA,
} UNITSTATE;

typedef DWORD (*LPJASSCFUNCTION)(LPJASS);

typedef enum {
    jasstype_integer,
    jasstype_real,
    jasstype_string,
    jasstype_boolean,
    jasstype_code,
    jasstype_handle,
    jasstype_cfunction,
} JASSTYPEID;

struct jass_module {
    LPCSTR name;
    LPJASSCFUNCTION func;
};

typedef struct {
    HANDLE (*MemAlloc)(long size);
    void (*MemFree)(HANDLE ptr);
    DWORD (*GetTime)(void);
    HANDLE (*ReadFile)(LPCSTR filename, DWORD *size);
    LPCJASSMODULE natives;
    LPPLAYER (*GetPlayerByNumber)(DWORD number);
} JASSHOST;

typedef struct gtriggeraction_s {
    LPCJASSFUNC func;
    struct gtriggeraction_s *next;
} TRIGGERACTION;

typedef struct gtriggercondition_s {
    LPCJASSFUNC expr;
    struct gtriggercondition_s *next;
} TRIGGERCONDITION;

typedef struct {
    UINAME campaign;
} ggamecache_t;

struct gtrigger_s {
    struct gtriggeraction_s *actions;
    struct gtriggercondition_s *conditions;
    BOOL disabled;
};

typedef struct {
    LPEDICT units[MAX_GROUP_SIZE];
    DWORD num_units;
} ggroup_t;

typedef struct {
    PATHSTR fileName;
    BOOL looping;
    BOOL is3D;
    BOOL stopwhenoutofrange;
    LONG fadeInRate;
    LONG fadeOutRate;
    DWORD duration;
} gsound_t;

struct vm_program {
    HANDLE data;
    DWORD size;
};

struct jass_context {
    LPTRIGGER trigger;
    LPEDICT unit;
    LPPLAYER playerState;
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
BOOL jass_resume(LPJASS j, LPJASSCOROUTINE co);
BOOL jass_coroutinedone(LPCJASSCOROUTINE co);
void jass_runevents(LPJASS j);
void jass_sleep(LPJASS j, DWORD msec);
LPCSTR jass_functionname(LPCJASSFUNC func);
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
BOOL jass_calltrigger(LPJASS j, LPTRIGGER trigger, LPEDICT unit);
BOOL jass_popboolean(LPJASS j);
void jass_pop(LPJASS j, DWORD count);
BOOL jass_evaluatetrigger(LPJASS j, LPTRIGGER trigger, LPEDICT unit);
BOOL jass_evaluateboolexpr(LPJASS j, LPCJASSFUNC expr, LPEDICT unit);
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

/* jass_dofile / jass_dobuffer — load and evaluate source. */
BOOL jass_dofile(LPJASS j, LPCSTR fileName);
BOOL jass_dobuffer(LPJASS j, LPSTR buffer);

/* jass_newstate / jass_close — state lifecycle. */
LPJASS jass_newstate(void);
void   jass_close(LPJASS j);


#endif
