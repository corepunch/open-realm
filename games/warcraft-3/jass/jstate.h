#ifndef jstate_h
#define jstate_h

/* jstate.h — Internal VM state definitions.
 * Mirrors Lua's lstate.h: full struct layouts for the opaque types in jass.h,
 * plus the two global broadcast variables used during coroutine dispatch.
 * Only jdo.c and jcode.c need this header. */

#include <setjmp.h>

#include "jass.h"
#include "jparser.h"

KNOWN_AS(jass_array, jassArray_t);
KNOWN_AS(jass_dict, jassdict_t);
KNOWN_AS(jass_arg, jassarg_t);
KNOWN_AS(jass_coroutine_frame, jassCoroutineframe_t);
KNOWN_AS(jass_program, jassprogram_t);
KNOWN_AS(jass_ref, jassref_t);
KNOWN_AS(jass_missing, jassmissing_t);

struct jass_missing { jassmissing_t *next; char name[]; };

#define BZ_JASS_HASH_SIZE 4096 // buckets; keeps Galaxy lookup chains near one entry; used for root globals/functions

struct jass_var {
    jassType_t const *type;
    handle_t value;
    jassref_t *ref;
    bool constant;
    bool array;
    struct {
        jassdict_t *locals;
        uint32_t returnstack;
        bool done;
        bool break_pending;  /* set by Galaxy `break`; cleared by eval_LOOP */
    } env;
    jassArray_t *_array;
};

/* Shared handle metadata distinguishes VM-owned payloads from native light handles. */
struct jass_ref {
    uint32_t refs, size, id;
};

struct jass_type {
    jassType_t const *inherit;
    jassType_t *next;
    cstring_t name;
};

struct jass_arg {
    jassarg_t *next;
    jassType_t const *type;
    cstring_t name;
};

struct jass_function {
    jassarg_t *args;
    jassType_t const *returns;
    jassFunc_t *next;
    jassFunc_t *hash_next;
    cstring_t name;
    token_t const *code;
    uint32_t (*nativefunc)(jass_t *j);
    bool constant, native;
};

struct jass_array {
    jassArray_t *next;
    uint32_t index;
    jassVar_t value;
};

struct jass_dict {
    jassdict_t *next;
    jassdict_t *hash_next;
    cstring_t key;
    jassVar_t value;
};

typedef enum {
    JASS_FRAME_FUNCTION,
    JASS_FRAME_BLOCK,
    JASS_FRAME_LOOP,
} JASSFRAMETYPE;

struct jass_coroutine_frame {
    jassCoroutineframe_t *next;
    JASSFRAMETYPE type;
    jassFunc_t const *func;
    token_t const *body;
    token_t const *pc;
    jassdict_t *locals;
    uint32_t loop_count;
};

struct jass_coroutine {
    jasscoroutine_t *next;
    jass_t *state;
    jassCoroutineframe_t *frames;
    uint32_t wake_time;
    bool yielded;
    bool done;
    int32_t loop_a_index;
    bool loop_a_index_valid;
    /* Runtime error abort: set by jass_rterror(), caught in jass_resumecoroutine(). */
    jmp_buf rterror_jmp;
    bool rterror_jmp_set;
};

struct jass_program {
    jassprogram_t *next;
    token_t *tokens;
};

#define MAX_JASS_STACK 256

struct jass_s {
    jassdict_t *globals;
    jassdict_t *global_hash[BZ_JASS_HASH_SIZE];
    jassType_t *types;
    jassFunc_t *functions;
    jassFunc_t *function_hash[BZ_JASS_HASH_SIZE];
    jassprogram_t *programs;
    jassVar_t stack[MAX_JASS_STACK];
    uint32_t num_stack;
    jassVar_t *stack_pointer;
    jassContext_t context;
    jass_t *root;
    jasscoroutine_t *coroutines;
    jasscoroutine_t *current_coroutine;
    bool halt_events;
    /* Runtime error state — owned by root, written by jass_rterror(). */
    jassmissing_t *missing;
    bool rterror_pending;
    char rterror_message[512];
    jmp_buf sync_rterror_jmp;
    bool sync_rterror_jmp_set;
    uint32_t next_handle_id;
};

/* Primitive type table — indexed by JASSTYPEID. Defined in jdo.c. */
extern jassType_t jass_types[];

/* Current local-player selector/unit set during coroutine dispatch. Defined in jdo.c. */
extern player_t *currentplayer;
extern edict_t *currentunit;

#endif /* jstate_h */
