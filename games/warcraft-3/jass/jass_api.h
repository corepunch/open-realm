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

LONG jass_checkinteger(LPJASS j, int index);
FLOAT jass_checknumber(LPJASS j, int index);
BOOL jass_checkboolean(LPJASS j, int index);
LPCSTR jass_checkstring(LPJASS j, int index);
DWORD jass_pushnull(LPJASS j);
DWORD jass_pushinteger(LPJASS j, LONG value);
DWORD jass_pushnumber(LPJASS j, FLOAT value);
DWORD jass_pushboolean(LPJASS j, BOOL value);
DWORD jass_pushstring(LPJASS j, LPCSTR value);
LPJASSCOROUTINE jass_startcoroutinebyname(LPJASS j, LPCSTR name);
void jass_sleep(LPJASS j, DWORD msec);

#endif
