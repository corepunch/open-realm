/*
 * test.c — Registry and runner for the in-engine test runtime (see test.h).
 *
 * Lives in libshared so the linked list head and counters are a single
 * instance shared by the executable and every game/renderer/ui module that
 * links libshared.  Game-side TEST() constructors call Test_Register here; the
 * engine's `+test` command calls Test_Run here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"

int test_asserts = 0;
int test_failures = 0;

static test_t *test_head = NULL;
static void (*test_before_each)(void);
static const char *test_current_name = NULL;
static FILE *test_junit_body = NULL;

/* Modules with stateful in-engine tests register one reset hook for their binary. */
void Test_SetBeforeEach(void (*fn)(void)) { test_before_each = fn; }

/* Append preserves source/registration order for stable, readable output. */
void Test_Register(test_t *t) {
    test_t **tail = &test_head;

    if (!t) return;
    while (*tail) tail = &(*tail)->next;
    t->next = NULL;
    *tail = t;
}

static void Test_WriteXmlEscaped(FILE *out, const char *text, size_t len) {
    size_t i;

    if (!out || !text) return;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];

        switch (c) {
        case '&': fputs("&amp;", out); break;
        case '<': fputs("&lt;", out); break;
        case '>': fputs("&gt;", out); break;
        case '\"': fputs("&quot;", out); break;
        case '\'': fputs("&apos;", out); break;
        default:
            if (c >= 0x20 || c == '\t' || c == '\n' || c == '\r') fputc(c, out);
            break;
        }
    }
}

static void Test_JUnitBeginCase(FILE *out, const test_t *test) {
    const char *dot;

    if (!out || !test || !test->name) return;
    dot = strchr(test->name, '.');
    fputs("  <testcase name=\"", out);
    Test_WriteXmlEscaped(out, test->name, strlen(test->name));
    fputs("\" classname=\"", out);
    if (dot) Test_WriteXmlEscaped(out, test->name, (size_t)(dot - test->name));
    else fputs("OpenRealm", out);
    fputs("\"", out);
    if (test->file) {
        fputs(" file=\"", out);
        Test_WriteXmlEscaped(out, test->file, strlen(test->file));
        fprintf(out, "\" line=\"%d\"", test->line);
    }
    fputs(">\n", out);
}

static void Test_WriteJUnitReport(const char *path, const char *suite_name,
                                  FILE *body, int tests, int failed_tests,
                                  int assertions) {
    FILE *out;
    char copybuf[4096];
    size_t count;

    if (!path || !path[0] || !body) return;
    out = fopen(path, "wb");
    if (!out) {
        fprintf(stderr, "warning: could not write JUnit report: %s\n", path);
        return;
    }

    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", out);
    fputs("<testsuite name=\"", out);
    if (suite_name && suite_name[0])
        Test_WriteXmlEscaped(out, suite_name, strlen(suite_name));
    else
        fputs("OpenRealm", out);
    fprintf(out,
            "\" tests=\"%d\" failures=\"%d\" errors=\"0\" skipped=\"0\" assertions=\"%d\">\n",
            tests, failed_tests, assertions);

    rewind(body);
    while ((count = fread(copybuf, 1, sizeof(copybuf), body)) != 0)
        fwrite(copybuf, 1, count, out);
    fputs("</testsuite>\n", out);

    if (fclose(out) != 0)
        fprintf(stderr, "warning: failed closing JUnit report: %s\n", path);
}

void Test_Fail(const char *func, const char *file, int line, const char *expr) {
    test_failures++;
    fprintf(stderr, "    FAIL [%s] %s() [%s:%d]: %s\n",
            test_current_name ? test_current_name : "<direct>", func, file, line, expr);
    if (test_junit_body) {
        fputs("    <failure message=\"Assertion failed: ", test_junit_body);
        Test_WriteXmlEscaped(test_junit_body, expr, strlen(expr));
        fputs("\" type=\"assertion\">", test_junit_body);
        Test_WriteXmlEscaped(test_junit_body, file, strlen(file));
        fprintf(test_junit_body, ":%d: ", line);
        Test_WriteXmlEscaped(test_junit_body, expr, strlen(expr));
        fputs("</failure>\n", test_junit_body);
    }
    if (getenv("GITHUB_ACTIONS")) {
        fprintf(stderr,
                "::error file=%s,line=%d,title=Test assertion failed::%s\n",
                file, line, expr);
    }
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
    const char *junit_path = getenv("TEST_JUNIT");
    const char *junit_suite = getenv("TEST_JUNIT_SUITE");
    FILE *junit_body = NULL;
    int total_failures = 0;
    int total_asserts = 0;
    int failed_tests = 0;
    int ran = 0;

    if (junit_path && junit_path[0]) {
        junit_body = tmpfile();
        if (!junit_body)
            fprintf(stderr, "warning: could not create temporary JUnit report body\n");
    }
    test_junit_body = junit_body;

    fprintf(stderr, "=== running tests: %s ===\n", pattern ? pattern : "*");
    for (test_t *t = test_head; t; t = t->next) {
        int before;

        if (!Test_NameMatches(t->name, pattern)) continue;
        test_failures = 0;
        test_asserts = 0;
        before = total_failures;
        test_current_name = t->name;
        if (junit_body) Test_JUnitBeginCase(junit_body, t);
        if (test_before_each) test_before_each();
        t->fn();
        if (junit_body) fputs("  </testcase>\n", junit_body);
        test_current_name = NULL;
        total_failures += test_failures;
        total_asserts += test_asserts;
        if (total_failures != before) failed_tests++;
        ran++;
        fprintf(stderr, "  %-52s %s\n", t->name,
                (total_failures == before) ? "PASS" : "FAIL");
    }
    test_junit_body = NULL;
    fprintf(stderr, "=== %d/%d assertions passed in %d test(s)",
            total_asserts - total_failures, total_asserts, ran);
    if (total_failures) fprintf(stderr, ", %d failed", total_failures);
    fprintf(stderr, " ===\n");

    if (junit_body) {
        Test_WriteJUnitReport(junit_path, junit_suite, junit_body,
                              ran, failed_tests, total_asserts);
        fclose(junit_body);
    }
    return total_failures;
}
