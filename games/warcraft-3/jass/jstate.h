#ifndef jstate_h
#define jstate_h

/* jstate.h — Internal VM state definitions.
 * Mirrors Lua's lstate.h: full struct layouts for the opaque types in jass.h,
 * plus the two global broadcast variables used during coroutine dispatch.
 * Only jdo.c and jcode.c need this header. */

#include <setjmp.h>

#include "jass.h"
#include "jparser.h"

KNOWN_AS(jass_array, JASSARRAY);
KNOWN_AS(jass_dict, JASSDICT);
KNOWN_AS(jass_arg, JASSARG);
KNOWN_AS(jass_coroutine_frame, JASSCOROUTINEFRAME);
KNOWN_AS(jass_program, JASSPROGRAM);
KNOWN_AS(jass_ref, JASSREF);

#define BZ_JASS_HASH_SIZE 4096 // buckets; keeps Galaxy lookup chains near one entry; used for root globals/functions

struct jass_var {
    LPCJASSTYPE type;
    HANDLE value;
    LPJASSREF ref;
    BOOL constant;
    BOOL array;
    struct {
        LPJASSDICT locals;
        DWORD returnstack;
        BOOL done;
        BOOL break_pending;  /* set by Galaxy `break`; cleared by eval_LOOP */
    } env;
    LPJASSARRAY _array;
};

/* Shared handle metadata distinguishes VM-owned payloads from native light handles. */
struct jass_ref {
    DWORD refs, size, id;
};

struct jass_type {
    LPCJASSTYPE inherit;
    LPJASSTYPE next;
    LPCSTR name;
};

struct jass_arg {
    LPJASSARG next;
    LPCJASSTYPE type;
    LPCSTR name;
};

struct jass_function {
    LPJASSARG args;
    LPCJASSTYPE returns;
    LPJASSFUNC next;
    LPJASSFUNC hash_next;
    LPCSTR name;
    LPCTOKEN code;
    DWORD (*nativefunc)(LPJASS j);
    BOOL constant, native;
};

struct jass_array {
    LPJASSARRAY next;
    DWORD index;
    JASSVAR value;
};

struct jass_dict {
    LPJASSDICT next;
    LPJASSDICT hash_next;
    LPCSTR key;
    JASSVAR value;
};

typedef enum {
    JASS_FRAME_FUNCTION,
    JASS_FRAME_BLOCK,
    JASS_FRAME_LOOP,
} JASSFRAMETYPE;

struct jass_coroutine_frame {
    LPJASSCOROUTINEFRAME next;
    JASSFRAMETYPE type;
    LPCJASSFUNC func;
    LPCTOKEN body;
    LPCTOKEN pc;
    LPJASSDICT locals;
    DWORD loop_count;
};

struct jass_coroutine {
    LPJASSCOROUTINE next;
    LPJASS state;
    LPJASSCOROUTINEFRAME frames;
    DWORD wake_time;
    BOOL yielded;
    BOOL done;
    /* Runtime error abort: set by jass_rterror(), caught in jass_resumecoroutine(). */
    jmp_buf rterror_jmp;
    BOOL rterror_jmp_set;
};

struct jass_program {
    LPJASSPROGRAM next;
    LPTOKEN tokens;
};

#define MAX_JASS_STACK 256

struct jass_s {
    LPJASSDICT globals;
    LPJASSDICT global_hash[BZ_JASS_HASH_SIZE];
    LPJASSTYPE types;
    LPJASSFUNC functions;
    LPJASSFUNC function_hash[BZ_JASS_HASH_SIZE];
    LPJASSPROGRAM programs;
    JASSVAR stack[MAX_JASS_STACK];
    DWORD num_stack;
    LPJASSVAR stack_pointer;
    JASSCONTEXT context;
    LPJASS root;
    LPJASSCOROUTINE coroutines;
    LPJASSCOROUTINE current_coroutine;
    BOOL halt_events;
    /* Runtime error state — owned by root, written by jass_rterror(). */
    BOOL rterror_pending;
    char rterror_message[512];
    jmp_buf sync_rterror_jmp;
    BOOL sync_rterror_jmp_set;
    DWORD next_handle_id;
};

/* Primitive type table — indexed by JASSTYPEID. Defined in jdo.c. */
extern JASSTYPE jass_types[];

/* Current local-player selector/unit set during coroutine dispatch. Defined in jdo.c. */
extern LPPLAYER currentplayer;
extern LPEDICT currentunit;

#endif /* jstate_h */
