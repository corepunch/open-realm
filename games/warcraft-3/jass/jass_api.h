#ifndef jass_api_h
#define jass_api_h

#include "common/shared.h"

KNOWN_AS(jass_s, JASS);
KNOWN_AS(jass_function, JASSFUNC);
KNOWN_AS(jass_coroutine, JASSCOROUTINE);
KNOWN_AS(jass_module, JASSMODULE);

typedef DWORD (*LPJASSCFUNCTION)(LPJASS);

struct jass_module {
    LPCSTR name;
    LPJASSCFUNCTION func;
};

typedef enum {
    JASS_MODE_JASS   = 0,
    JASS_MODE_GALAXY = 1,
} JASSMODE;

/* Forward-declared — WC3 defines playerState_s body; SC2 passes NULL for GetPlayerByNumber */
struct playerState_s;

typedef struct {
    HANDLE (*MemAlloc)(long size);
    void   (*MemFree)(HANDLE ptr);
    DWORD  (*GetTime)(void);
    HANDLE (*ReadFile)(LPCSTR filename, DWORD *size);
    LPCJASSMODULE natives;
    LPCJASSMODULE galaxy_natives;
    struct playerState_s *(*GetPlayerByNumber)(DWORD number);
} JASSHOST;

/* VM lifecycle */
void   jass_sethost(JASSHOST const *host);
LPJASS jass_newstate(void);
void   jass_close(LPJASS j);
BOOL   jass_dofile(LPJASS j, LPCSTR fileName);
BOOL   jass_dofile_ex(LPJASS j, LPCSTR fileName, JASSMODE mode);
BOOL   jass_dobuffer(LPJASS j, LPSTR buffer);
BOOL   jass_dobuffer_ex(LPJASS j, LPSTR buffer, JASSMODE mode);
void   jass_callbyname(LPJASS j, LPCSTR name, BOOL spawn_coroutine);
void   jass_runevents(LPJASS j);

/* Heap allocation (via jass_host.MemAlloc/MemFree) */
HANDLE jass_alloc(long size);
void   jass_free(HANDLE ptr);

/* Return the root VM state (coroutine states share globals with the root). */
LPJASS jass_getroot(LPJASS j);

/* Runtime error boundary */
void   jass_rterror(LPJASS j, LPCSTR message);
BOOL   jass_rterror_pending(LPJASS j);
LPCSTR jass_rterror_message(LPJASS j);
void   jass_rterror_clear(LPJASS j);

/* Stack / value API */
LONG   jass_checkinteger(LPJASS j, int index);
FLOAT  jass_checknumber(LPJASS j, int index);
BOOL   jass_checkboolean(LPJASS j, int index);
LPCSTR jass_checkstring(LPJASS j, int index);
HANDLE jass_checkhandle(LPJASS j, int index, LPCSTR type);
DWORD  jass_pushnull(LPJASS j);
DWORD  jass_pushnullhandle(LPJASS j, LPCSTR type);
DWORD  jass_pushinteger(LPJASS j, LONG value);
DWORD  jass_pushnumber(LPJASS j, FLOAT value);
DWORD  jass_pushboolean(LPJASS j, BOOL value);
DWORD  jass_pushstring(LPJASS j, LPCSTR value);
DWORD  jass_pushstringlen(LPJASS j, LPCSTR value, DWORD len);
DWORD  jass_pushlighthandle(LPJASS j, HANDLE value, LPCSTR type);
LPJASSCOROUTINE jass_startcoroutinebyname(LPJASS j, LPCSTR name);
void   jass_sleep(LPJASS j, DWORD msec);

#endif
