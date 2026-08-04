/*
 * test.c — Registry and runner for the in-engine test runtime (see test.h).
 *
 * Lives in libshared so the linked list head and counters are a single
 * instance shared by the executable and every game/renderer/ui module that
 * links libshared.  Game-side TEST() constructors call Test_Register here; the
 * engine's `+test` command calls Test_Run here.
 */
#include <stdio.h>
#include <string.h>

#include "test.h"

int test_asserts = 0;
int test_failures = 0;

static test_t *test_head = NULL;

/* Append preserves source/registration order for stable, readable output. */
void Test_Register(test_t *t) {
    test_t **tail = &test_head;

    if (!t) return;
    while (*tail) tail = &(*tail)->next;
    t->next = NULL;
    *tail = t;
}

void Test_Fail(const char *file, int line, const char *expr) {
    test_failures++;
    fprintf(stderr, "    FAIL [%s:%d]: %s\n", file, line, expr);
}

/* Case-insensitive glob: "*" matches all, a trailing "*" is a prefix match,
 * otherwise an exact name match. */
static int Test_NameMatches(const char *name, const char *pattern) {
    size_t len;

    if (!pattern || !pattern[0] || !strcmp(pattern, "*")) return 1;
    len = strlen(pattern);
    if (pattern[len - 1] == '*') return !strncasecmp(name, pattern, len - 1);
    return !strcasecmp(name, pattern);
}

int Test_Run(const char *pattern) {
    int total_failures = 0;
    int total_asserts = 0;
    int ran = 0;

    fprintf(stderr, "=== running tests: %s ===\n", pattern ? pattern : "*");
    for (test_t *t = test_head; t; t = t->next) {
        int before;

        if (!Test_NameMatches(t->name, pattern)) continue;
        test_failures = 0;
        test_asserts = 0;
        before = total_failures;
        t->fn();
        total_failures += test_failures;
        total_asserts += test_asserts;
        ran++;
        fprintf(stderr, "  %-52s %s\n", t->name,
                (total_failures == before) ? "PASS" : "FAIL");
    }
    fprintf(stderr, "=== %d/%d assertions passed in %d test(s)",
            total_asserts - total_failures, total_asserts, ran);
    if (total_failures) fprintf(stderr, ", %d failed", total_failures);
    fprintf(stderr, " ===\n");
    return total_failures;
}
