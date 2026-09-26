#ifndef jass_api_h
#define jass_api_h

#include "common/shared.h"

KNOWN_AS(jass_s, jass_t);
KNOWN_AS(jass_function, jassFunc_t);
KNOWN_AS(jass_coroutine, jasscoroutine_t);
KNOWN_AS(jass_module, jassModule_t);

typedef uint32_t (*jassCFunction_t)(jass_t *);
typedef bool (*jassSnapshotIo_t)(void *context, void *data, uint32_t size);

typedef struct {
    void *context;
    jassSnapshotIo_t transfer;
    void *handles;
} jassSnapshot_t;

struct jass_module {
    cstring_t name;
    jassCFunction_t func;
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
    jassModule_t const *natives;
    jassModule_t const *galaxy_natives;
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
} jassHost_t;

/* VM lifecycle */
void   jass_sethost(jassHost_t const *host);
jass_t *jass_newstate(void);
void   jass_close(jass_t *j);
bool   jass_dofile(jass_t *j, cstring_t fileName);
bool   jass_dofile_ex(jass_t *j, cstring_t fileName, JASSMODE mode);
bool   jass_dobuffer(jass_t *j, string_t buffer);
bool   jass_dobuffer_ex(jass_t *j, string_t buffer, JASSMODE mode);
void   jass_callbyname(jass_t *j, cstring_t name, bool spawn_coroutine);
void   jass_runevents(jass_t *j);
bool   jass_writesnapshot(jass_t *j, jassSnapshot_t *snapshot);
bool   jass_readsnapshot(jass_t *j, jassSnapshot_t *snapshot);
uint32_t  jass_programidentity(jass_t *j);

/* Heap allocation (via jass_host.MemAlloc/MemFree) */
handle_t jass_alloc(long size);
void   jass_free(handle_t ptr);

/* Return the root VM state (coroutine states share globals with the root). */
jass_t *jass_getroot(jass_t *j);
bool jass_isrunning(jass_t *j);
void jass_haltevents(jass_t *j);

/* Runtime error boundary */
void   jass_rterror(jass_t *j, cstring_t message);
bool   jass_rterror_pending(jass_t *j);
cstring_t jass_rterror_message(jass_t *j);
void   jass_rterror_clear(jass_t *j);
/* Unique missing calls persist until VM close, independently of the latest error. */
uint32_t  jass_missingcount(jass_t *j);
cstring_t jass_missingname(jass_t *j, uint32_t index);

/* Stack / value API */
int32_t   jass_checkinteger(jass_t *j, int index);
float  jass_checknumber(jass_t *j, int index);
bool   jass_checkboolean(jass_t *j, int index);
cstring_t jass_checkstring(jass_t *j, int index);
handle_t jass_checkhandle(jass_t *j, int index, cstring_t type);
uint32_t  jass_pushnull(jass_t *j);
uint32_t  jass_pushnullhandle(jass_t *j, cstring_t type);
uint32_t  jass_pushinteger(jass_t *j, int32_t value);
uint32_t  jass_pushnumber(jass_t *j, float value);
uint32_t  jass_pushboolean(jass_t *j, bool value);
uint32_t  jass_pushstring(jass_t *j, cstring_t value);
uint32_t  jass_pushstringlen(jass_t *j, cstring_t value, uint32_t len);
uint32_t  jass_pushlighthandle(jass_t *j, handle_t value, cstring_t type);
jasscoroutine_t *jass_startcoroutinebyname(jass_t *j, cstring_t name);
jasscoroutine_t *jass_startcoroutinebynameforplayer(jass_t *j, cstring_t name, struct playerState_s *player);
bool   jass_callcoroutinebyname(jass_t *j, cstring_t name);
void   jass_sleep(jass_t *j, uint32_t msec);
void   jass_settimercontext(handle_t timer);

#endif
