/*
 * test.h — Lightweight in-engine test runtime.
 *
 * Tests live next to the code they exercise and self-register at load time via
 * a file-scope constructor.  The registry, counters and runner live in
 * libshared so a single instance is shared between the main executable and the
 * game module (a separate dynamic library) — the game's TEST() constructors
 * register into the same list the engine's `+test` command walks.
 *
 * A test is a plain function.  Nothing needs a Makefile source list, a main(),
 * or hand-rolled global/mocked-function boilerplate:
 *
 *   #ifdef BZ_TESTS
 *   #include "test.h"
 *   TEST(wow_combat, pain_interrupts_attack) {
 *       edict_t *e = G_SpawnCreature("orc_grunt");
 *       Wow_TakeDamage(e, 50);
 *       T_STREQ(e->currentmove->animation, "Pain");
 *   }
 *   #endif
 *
 * Run with:  <binary> +dedicated 1 +test '<pattern>'
 *   pattern "*"        — every registered test
 *   pattern "area.*"   — every test whose name starts with "area."
 *   pattern "area.foo" — exactly that test (case-insensitive)
 */
#ifndef test_h
#define test_h

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct test_s {
    const char    *name;   /* e.g. "wow_combat.pain_interrupts_attack" */
    void         (*fn)(void);
    const char    *file;   /* TEST() definition site, for reports */
    int            line;
    struct test_s *next;
} test_t;

/* Registry + runner (defined in shared/test.c, one instance in libshared). */
void Test_Register(test_t *t);
int  Test_Run(const char *pattern);                 /* returns failure count */
void Test_Fail(const char *func, const char *file, int line, const char *expr);
void Test_SetBeforeEach(void (*fn)(void));

/* Per-test counters (reset by Test_Run before each test). */
extern int test_asserts;
extern int test_failures;

/*
 * TEST(suite, name) { ... } — define and self-register a test.  The display
 * name is "suite.name", so a suite prefix groups tests for pattern matching
 * (e.g. `+test 'wow_combat.*'`).  suite and name must be valid C identifiers.
 */
#define TEST(suite, name)                                                      \
    static void suite##_##name##_fn(void);                                     \
    static test_t suite##_##name##_node = { #suite "." #name,                  \
                                            suite##_##name##_fn, __FILE__,      \
                                            __LINE__, 0 };                      \
    __attribute__((constructor)) static void suite##_##name##_register(void) { \
        Test_Register(&suite##_##name##_node);                                 \
    }                                                                          \
    static void suite##_##name##_fn(void)

#define T_ASSERT(cond) do {                                                    \
    test_asserts++;                                                            \
    if (!(cond)) Test_Fail(__func__, __FILE__, __LINE__, #cond);               \
} while (0)

#define T_EQ(a, b)        T_ASSERT((a) == (b))
#define T_NE(a, b)        T_ASSERT((a) != (b))
#define T_FEQ(a, b, eps)  T_ASSERT(fabsf((float)(a) - (float)(b)) <= (float)(eps))

static inline int Test_StringsEqual(const char *a, const char *b) {
    return a && b && strcmp(a, b) == 0;
}

#define T_STREQ(a, b)     T_ASSERT(Test_StringsEqual((a), (b)))
#define T_NULL(p)         T_ASSERT((p) == 0)
#define T_NOT_NULL(p)     T_ASSERT((p) != 0)

/* T_RUN_UNTIL(step, cond, n) — advance simulation up to n steps until cond
 * becomes true, then assert it.  Fails the test if cond never fires.
 *
 * Example: fire a projectile, run up to 200 ticks, assert it hit the target.
 *   T_RUN_UNTIL(Wow_RunProjectile(proj), !proj->inuse, 200);
 *   T_RUN_UNTIL(game->RunFrame(), target_local->dead, 1000);
 */
#define T_RUN_UNTIL(step, cond, n) do {                                        \
    for (unsigned _r = 0; !(cond) && _r < (unsigned)(n); _r++) (step);        \
    T_ASSERT(cond);                                                            \
} while (0)

/* T_BENCH(label, iters, expr) — run expr iters times, print wall-clock ms/call.
 * Does not assert; use for perf baselines and regression spotting. */
#define T_BENCH(label, iters, expr) do {                                       \
    struct timespec _t0, _t1;                                                  \
    clock_gettime(CLOCK_MONOTONIC, &_t0);                                      \
    for (int _bi = 0; _bi < (iters); _bi++) { expr; }                         \
    clock_gettime(CLOCK_MONOTONIC, &_t1);                                      \
    double _ms = ((_t1.tv_sec  - _t0.tv_sec)  * 1e3)                          \
               + ((_t1.tv_nsec - _t0.tv_nsec) * 1e-6);                        \
    printf("[BENCH] %-52s  %7.2f ms/call  (%d iters)\n",                      \
           (label), _ms / (double)(iters), (iters));                           \
} while (0)

#endif /* test_h */
