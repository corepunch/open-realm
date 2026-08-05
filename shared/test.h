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
#include <string.h>

typedef struct test_s {
    const char    *name;   /* e.g. "wow_combat.pain_interrupts_attack" */
    void         (*fn)(void);
    struct test_s *next;
} test_t;

/* Registry + runner (defined in shared/test.c, one instance in libshared). */
void Test_Register(test_t *t);
int  Test_Run(const char *pattern);                 /* returns failure count */
void Test_Fail(const char *file, int line, const char *expr);
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
                                            suite##_##name##_fn, 0 };          \
    __attribute__((constructor)) static void suite##_##name##_register(void) { \
        Test_Register(&suite##_##name##_node);                                 \
    }                                                                          \
    static void suite##_##name##_fn(void)

#define T_ASSERT(cond) do {                                                    \
    test_asserts++;                                                            \
    if (!(cond)) Test_Fail(__FILE__, __LINE__, #cond);                         \
} while (0)

#define T_EQ(a, b)        T_ASSERT((a) == (b))
#define T_NE(a, b)        T_ASSERT((a) != (b))
#define T_FEQ(a, b, eps)  T_ASSERT(fabsf((float)(a) - (float)(b)) <= (float)(eps))
#define T_STREQ(a, b)     T_ASSERT((a) && (b) && strcmp((a), (b)) == 0)
#define T_NULL(p)         T_ASSERT((p) == 0)
#define T_NOT_NULL(p)     T_ASSERT((p) != 0)

#endif /* test_h */
