#ifndef jass_api_h
#define jass_api_h

#include "common/shared.h"

KNOWN_AS(jass_s, JASS);
KNOWN_AS(jass_function, JASSFUNC);
KNOWN_AS(jass_coroutine, JASSCOROUTINE);
KNOWN_AS(jass_module, JASSMODULE);

typedef uint32_t (*LPJASSCFUNCTION)(LPJASS);
typedef bool (*LPJASSSNAPSHOTIO)(void *context, void *data, uint32_t size);

typedef struct {
    void *context;
    LPJASSSNAPSHOTIO transfer;
    void *handles;
} JASSSNAPSHOT;

struct jass_module {
    cstring_t name;
    LPJASSCFUNCTION func;
};

typedef enum {
    JASS_MODE_JASS   = 0,
    JASS_MODE_GALAXY = 1,
} JASSMODE;

/* Forward-declared — WC3 defines playerState_s body; SC2 passes NULL for GetPlayerByNumber */
struct playerState_s;

typedef struct {
    handle_t (*MemAlloc)(long size);
    void   (*MemFree)(handle_t ptr);
    uint32_t  (*GetTime)(void);
    handle_t (*ReadFile)(cstring_t filename, uint32_t *size);
    LPCJASSMODULE natives;
    LPCJASSMODULE galaxy_natives;
    struct playerState_s *(*GetPlayerByNumber)(uint32_t number);
    void (*RuntimeError)(cstring_t message);
    bool (*SaveHandle)(cstring_t type, handle_t value, uint32_t *id);
    handle_t (*LoadHandle)(cstring_t type, uint32_t id);
    /* Optional host-side diagnostics for coroutine wake/resume. The VM keeps
     * this generic: trigger is the opaque context handle supplied by the host. */
    void (*CoroutineTrace)(handle_t trigger, cstring_t function, cstring_t phase,
                           uint32_t now, uint32_t wake_time, bool yielded, bool done);
    bool (*TimerCoroutineValid)(handle_t timer, uint32_t generation);
    void (*VariableChanged)(cstring_t name, float before, float after);
} JASSHOST;

/* VM lifecycle */
void   jass_sethost(JASSHOST const *host);
LPJASS jass_newstate(void);
void   jass_close(LPJASS j);
bool   jass_dofile(LPJASS j, cstring_t fileName);
bool   jass_dofile_ex(LPJASS j, cstring_t fileName, JASSMODE mode);
bool   jass_dobuffer(LPJASS j, string_t buffer);
bool   jass_dobuffer_ex(LPJASS j, string_t buffer, JASSMODE mode);
void   jass_callbyname(LPJASS j, cstring_t name, bool spawn_coroutine);
void   jass_runevents(LPJASS j);
bool   jass_writesnapshot(LPJASS j, JASSSNAPSHOT *snapshot);
bool   jass_readsnapshot(LPJASS j, JASSSNAPSHOT *snapshot);
uint32_t  jass_programidentity(LPJASS j);

/* Heap allocation (via jass_host.MemAlloc/MemFree) */
handle_t jass_alloc(long size);
void   jass_free(handle_t ptr);

/* Return the root VM state (coroutine states share globals with the root). */
LPJASS jass_getroot(LPJASS j);
bool jass_isrunning(LPJASS j);
void jass_haltevents(LPJASS j);

/* Runtime error boundary */
void   jass_rterror(LPJASS j, cstring_t message);
bool   jass_rterror_pending(LPJASS j);
cstring_t jass_rterror_message(LPJASS j);
void   jass_rterror_clear(LPJASS j);
/* Unique missing calls persist until VM close, independently of the latest error. */
uint32_t  jass_missingcount(LPJASS j);
cstring_t jass_missingname(LPJASS j, uint32_t index);

/* Stack / value API */
int32_t   jass_checkinteger(LPJASS j, int index);
float  jass_checknumber(LPJASS j, int index);
bool   jass_checkboolean(LPJASS j, int index);
cstring_t jass_checkstring(LPJASS j, int index);
handle_t jass_checkhandle(LPJASS j, int index, cstring_t type);
uint32_t  jass_pushnull(LPJASS j);
uint32_t  jass_pushnullhandle(LPJASS j, cstring_t type);
uint32_t  jass_pushinteger(LPJASS j, int32_t value);
uint32_t  jass_pushnumber(LPJASS j, float value);
uint32_t  jass_pushboolean(LPJASS j, bool value);
uint32_t  jass_pushstring(LPJASS j, cstring_t value);
uint32_t  jass_pushstringlen(LPJASS j, cstring_t value, uint32_t len);
uint32_t  jass_pushlighthandle(LPJASS j, handle_t value, cstring_t type);
LPJASSCOROUTINE jass_startcoroutinebyname(LPJASS j, cstring_t name);
LPJASSCOROUTINE jass_startcoroutinebynameforplayer(LPJASS j, cstring_t name, struct playerState_s *player);
bool   jass_callcoroutinebyname(LPJASS j, cstring_t name);
void   jass_sleep(LPJASS j, uint32_t msec);
void   jass_settimercontext(handle_t timer);

#endif
