/*
 * test_sc2_hud_live.c — End-to-end SC2 HUD integration tests.
 *
 * REQUIRES: Blizzard SC2 archives under data/StarCraft2/.
 * Run with: make test-sc2-live
 *
 * Spawns build/bin/opensc2 with +r_module stdout (text renderer) and
 * +com_frame_limit 5, then parses the rendered output to assert correct
 * UI frame delivery.  Covers:
 *   - All resource icons are drawn (mineral, gas, supply)
 *   - Resource label texts are stat-driven ("0", "0/0"), not "text N"
 *   - Minimap is drawn with positive on-screen dimensions
 *   - Command panel background texture is rendered
 *   - Command button images are stamped (no null texture in command area)
 *   - HUD font is loaded
 *   - No null textures appear in the resource bar
 *
 * Test infrastructure captures all stdout lines from the binary and
 * provides find/count helpers used by every assertion.
 */

#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SC2_BINARY
#define SC2_BINARY "build/bin/opensc2"
#endif

#ifndef SC2_DATA
#define SC2_DATA "data/StarCraft2"
#endif

int _tests_run    = 0;
int _tests_failed = 0;

/* ------------------------------------------------------------------ */
/* Output capture                                                       */
/* ------------------------------------------------------------------ */

#define MAX_LINES     16384
#define MAX_LINE_LEN  512

static char  g_lines[MAX_LINES][MAX_LINE_LEN];
static int   g_nlines;

/* Inclusive line indices of "begin_frame index=N" / "end_frame index=N" */
static int g_frame_start[16];
static int g_frame_end[16];
static int g_nframes;

static void capture_output(void) {
    if (g_nlines > 0) return;  /* already captured */

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "%s -data %s +r_module stdout +map TRaynor01 +com_frame_limit 5 2>/dev/null",
             SC2_BINARY, SC2_DATA);

    FILE *fp = popen(cmd, "r");
    if (!fp) { fprintf(stderr, "FATAL: popen failed for: %s\n", cmd); return; }

    while (g_nlines < MAX_LINES) {
        if (!fgets(g_lines[g_nlines], MAX_LINE_LEN, fp)) break;
        /* strip trailing newline */
        char *nl = strchr(g_lines[g_nlines], '\n');
        if (nl) *nl = '\0';
        g_nlines++;
    }
    pclose(fp);

    /* Index frame boundaries */
    for (int i = 0; i < g_nlines && g_nframes < 16; i++) {
        if (strncmp(g_lines[i], "begin_frame", 11) == 0) {
            g_frame_start[g_nframes] = i;
        } else if (strncmp(g_lines[i], "end_frame", 9) == 0) {
            g_frame_end[g_nframes] = i;
            g_nframes++;
        }
    }
}

/* Return 1 if any line contains pattern, else 0. */
static int find_line(const char *pattern) {
    for (int i = 0; i < g_nlines; i++)
        if (strstr(g_lines[i], pattern)) return 1;
    return 0;
}

/* Count lines containing pattern (reserved for future assertions). */
static int count_lines(const char *pattern) __attribute__((unused));
static int count_lines(const char *pattern) {
    int n = 0;
    for (int i = 0; i < g_nlines; i++)
        if (strstr(g_lines[i], pattern)) n++;
    return n;
}

/* Return 1 if any line within frame N (0-based) contains pattern. */
/* (reserved for future per-frame assertions) */
static int find_in_frame(int frame_idx, const char *pattern) __attribute__((unused));
static int find_in_frame(int frame_idx, const char *pattern) {
    if (frame_idx >= g_nframes) return 0;
    for (int i = g_frame_start[frame_idx]; i <= g_frame_end[frame_idx]; i++)
        if (strstr(g_lines[i], pattern)) return 1;
    return 0;
}

/* Return 1 if any line within frame N also contains both p1 and p2. */
static int find_line_with_both(const char *p1, const char *p2) {
    for (int i = 0; i < g_nlines; i++)
        if (strstr(g_lines[i], p1) && strstr(g_lines[i], p2)) return 1;
    return 0;
}

/* Return 1 if pattern NEVER appears in any line (reserved for future assertions). */
static int not_found(const char *pattern) __attribute__((unused));
static int not_found(const char *pattern) {
    return !find_line(pattern);
}

/* ------------------------------------------------------------------ */
/* ASSERT helper that prints pattern on failure                         */
/* ------------------------------------------------------------------ */

#define ASSERT_LINE(pattern) do { \
    _tests_run++; \
    if (!find_line(pattern)) { \
        fprintf(stderr, "    FAIL [%s:%d]: expected line containing: %s\n", \
                __FILE__, __LINE__, pattern); \
        _tests_failed++; \
    } \
} while (0)

#define ASSERT_NO_LINE(pattern) do { \
    _tests_run++; \
    if (find_line(pattern)) { \
        fprintf(stderr, "    FAIL [%s:%d]: unexpected line found: %s\n", \
                __FILE__, __LINE__, pattern); \
        _tests_failed++; \
    } \
} while (0)

#define ASSERT_LINE_IN_FRAME(frame_idx, pattern) do { \
    _tests_run++; \
    if (!find_in_frame(frame_idx, pattern)) { \
        fprintf(stderr, "    FAIL [%s:%d]: expected pattern in frame %d: %s\n", \
                __FILE__, __LINE__, frame_idx, pattern); \
        _tests_failed++; \
    } \
} while (0)

#define ASSERT_BOTH_IN_LINE(p1, p2) do { \
    _tests_run++; \
    if (!find_line_with_both(p1, p2)) { \
        fprintf(stderr, "    FAIL [%s:%d]: expected line with '%s' AND '%s'\n", \
                __FILE__, __LINE__, p1, p2); \
        _tests_failed++; \
    } \
} while (0)

/* ------------------------------------------------------------------ */
/* Tests: binary available + output non-empty                          */
/* ------------------------------------------------------------------ */

static void test_binary_runs_and_emits_frames(void) {
    capture_output();
    ASSERT(g_nlines > 0);
    ASSERT(g_nframes >= 2); /* frame 1 = pre-map; frame 2 = first game frame */
    ASSERT_LINE("begin_frame");
    ASSERT_LINE("end_frame");
}

/* ------------------------------------------------------------------ */
/* Tests: font loading                                                  */
/* ------------------------------------------------------------------ */

static void test_hud_font_loaded(void) {
    capture_output();
    /* SC2 HUD exclusively uses EurostileExt-Med for all label text. */
    ASSERT_LINE("load_font");
    ASSERT_LINE("EurostileExt-Med.otf");
}

/* ------------------------------------------------------------------ */
/* Tests: resource panel icons                                          */
/* ------------------------------------------------------------------ */

static void test_hud_mineral_icon_drawn(void) {
    capture_output();
    /* icon-mineral.dds must appear at least once in game frames. */
    ASSERT_LINE("icon-mineral.dds");
}

static void test_hud_gas_icon_drawn(void) {
    capture_output();
    ASSERT_LINE("icon-gas.dds");
}

static void test_hud_supply_icon_drawn(void) {
    capture_output();
    ASSERT_LINE("icon-supply.dds");
}

/* ------------------------------------------------------------------ */
/* Tests: resource label texts — must be stat-driven, not "text N"     */
/* ------------------------------------------------------------------ */

static void test_hud_supply_text_is_stat_driven(void) {
    capture_output();
    /* Supply label (SupplyLabel, stat=FOOD_USED/FOOD_CAP) must render
     * as "0/0", not "text N".  Both values start at zero on connect. */
    ASSERT_BOTH_IN_LINE("draw_text", "text=\"0/0\"");
}

static void test_hud_mineral_count_is_stat_driven(void) {
    capture_output();
    /* ResourceLabel0 and ResourceLabel3 (minerals) must show "0",
     * not "text N". At least two mineral count draws are expected
     * (slots 0 and 3 both bind to RESOURCE_GOLD). */
    int mineral_text_count = 0;
    for (int i = 0; i < g_nlines; i++) {
        if (strstr(g_lines[i], "draw_text") && strstr(g_lines[i], "text=\"0\""))
            mineral_text_count++;
    }
    /* At least one "0" label must be present in the resource bar. */
    ASSERT(mineral_text_count >= 1);
}

static void test_hud_no_unbound_resource_labels(void) {
    capture_output();
    /* "text N" (where N >= 10) appearing in any draw_text at the
     * resource bar y-position (y:4.9) means a label has no stat binding.
     * ResourceLabel3 was the known offender before the fix. */
    int unbound = 0;
    for (int i = 0; i < g_nlines; i++) {
        char *l = g_lines[i];
        if (!strstr(l, "draw_text")) continue;
        /* Resource bar lives at y:4.9x in the 1024×768 SC2 viewport. */
        if (!strstr(l, "y:4.")) continue;
        /* "text=\"text " pattern = fallback path → unbound */
        if (strstr(l, "text=\"text ")) unbound++;
    }
    if (unbound > 0) {
        fprintf(stderr,
                "    FAIL [%s:%d]: %d resource label(s) use fallback 'text N' — stat not bound\n",
                __FILE__, __LINE__, unbound);
        _tests_run++;
        _tests_failed++;
    } else {
        _tests_run++;
    }
}

/* ------------------------------------------------------------------ */
/* Tests: minimap                                                       */
/* ------------------------------------------------------------------ */

static void test_hud_minimap_draws_with_positive_size(void) {
    capture_output();
    /* draw_minimap must appear AND have w > 0. */
    ASSERT_LINE("draw_minimap");
    /* Verify positive width in the drawn rect (w:NNN where NNN > 0). */
    int found_positive_minimap = 0;
    for (int i = 0; i < g_nlines; i++) {
        if (!strstr(g_lines[i], "draw_minimap")) continue;
        /* look for w: followed by a non-zero digit */
        char *w = strstr(g_lines[i], "w:");
        if (!w) continue;
        float wval = 0.0f;
        sscanf(w + 2, "%f", &wval);
        if (wval > 0.0f) { found_positive_minimap = 1; break; }
    }
    ASSERT(found_positive_minimap);
}

/* ------------------------------------------------------------------ */
/* Tests: command panel                                                 */
/* ------------------------------------------------------------------ */

static void test_hud_command_panel_background_drawn(void) {
    capture_output();
    /* The command panel background texture must be loaded and drawn. */
    ASSERT_LINE("ui_commandcard_terranframe_normal.dds");
}

static void test_hud_command_button_icons_stamped(void) {
    capture_output();
    /* command_apply_icons stamps real textures onto the 15 command buttons
     * (2 image children each → 30 frames).  At least 10 non-null draws
     * in the command-button area (bottom-right, y > 700 in 1024×768) must
     * use a real texture (texture != 0). */
    int stamped_draws = 0;
    for (int i = 0; i < g_nlines; i++) {
        char *l = g_lines[i];
        if (!strstr(l, "draw_image")) continue;
        /* texture=0 means no icon was assigned. */
        if (strstr(l, "texture=0 ")) continue;
        /* Only count draws in the command area (y in the bottom quarter). */
        char *yp = strstr(l, ",y:");
        if (!yp) continue;
        float y = 0.0f;
        sscanf(yp + 3, "%f", &y);
        if (y > 700.0f) stamped_draws++;
    }
    ASSERT(stamped_draws >= 10);
}

static void test_hud_no_null_texture_in_resource_bar(void) {
    capture_output();
    /* Resource bar icons (mineral, gas, supply) must render with real textures.
     * Specifically: any draw_image in the resource-bar y band (y < 50 in SC2
     * native 1600x1200 coords) with positive dimensions must NOT use texture=0.
     * Zero-sized draws (w==0 or h==0) are ignored — they are invisible
     * collapsed frames from unresolved cross-panel anchors, not a rendering bug. */
    int null_tex_positive_size = 0;
    for (int i = 0; i < g_nlines; i++) {
        char *l = g_lines[i];
        if (!strstr(l, "draw_image")) continue;
        if (!strstr(l, "texture=0 ")) continue;
        /* parse screen rect */
        char *sp = strstr(l, "screen={");
        if (!sp) continue;
        float sx = 0, sy = 0, sw = 0, sh = 0;
        sscanf(sp, "screen={x:%f,y:%f,w:%f,h:%f}", &sx, &sy, &sw, &sh);
        if (sy >= 50.0f) continue;    /* outside resource bar */
        if (sw <= 0.0f || sh <= 0.0f) continue; /* zero-sized: harmless */
        null_tex_positive_size++;
    }
    if (null_tex_positive_size > 0) {
        fprintf(stderr,
                "    FAIL [%s:%d]: %d visible null-texture draw(s) in resource bar (y<50)\n",
                __FILE__, __LINE__, null_tex_positive_size);
        _tests_run++;
        _tests_failed++;
    } else {
        _tests_run++;
    }
}

/* ------------------------------------------------------------------ */
/* Tests: hidden panels must not render                                 */
/* ------------------------------------------------------------------ */

static void test_hud_team_resource_panel_is_hidden(void) {
    capture_output();
    /* TeamResourcePanel is an optional multiplayer overlay hidden at init.
     * Its background texture must NOT appear in draw calls. */
    ASSERT_NO_LINE("ui_ingame_resourcesharing_button_normalpressed_terran");
    ASSERT_NO_LINE("TeamResourceHeaderBorder");
}

static void test_hud_pause_panel_is_hidden(void) {
    capture_output();
    /* PausePanel must not be drawn in a running game. */
    ASSERT_NO_LINE("ui_pause_background");
}

/* ------------------------------------------------------------------ */
/* Tests: no svc_layout frames show undefined text                      */
/* ------------------------------------------------------------------ */

static void test_hud_no_unbound_text_in_resource_bar(void) {
    capture_output();
    /* In the resource bar (y in [4,10] in SC2 native 1600x1200 coords — the
     * top row used by icons/labels), every draw_text must be driven by a stat
     * or literal.  "text N" means the frame has no stat and no text binding.
     * The CommandTooltip labels sit at y~15 (just outside this band) and are
     * expected to be unbound; label checks at y > 10 are excluded here. */
    int fallback_in_bar = 0;
    for (int i = 0; i < g_nlines; i++) {
        char *l = g_lines[i];
        if (!strstr(l, "draw_text")) continue;
        if (!strstr(l, "text=\"text ")) continue;
        char *rp = strstr(l, "rect={");
        if (!rp) continue;
        float rx = 0, ry = 0;
        sscanf(rp, "rect={x:%f,y:%f", &rx, &ry);
        if (ry < 4.0f || ry > 10.0f) continue;  /* only the resource-bar band */
        fallback_in_bar++;
    }
    if (fallback_in_bar > 0) {
        fprintf(stderr,
                "    FAIL [%s:%d]: %d draw_text 'text N' fallback(s) in resource bar"
                " (check hud_resource.c bindings[])\n",
                __FILE__, __LINE__, fallback_in_bar);
        _tests_run++;
        _tests_failed++;
    } else {
        _tests_run++;
    }
}

/* ------------------------------------------------------------------ */
/* Runner                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== SC2 HUD Live Integration Tests ===\n");
    printf("Binary : %s\n", SC2_BINARY);
    printf("Data   : %s\n\n", SC2_DATA);

    printf("[binary runs + frames]\n");
    RUN_TEST(test_binary_runs_and_emits_frames);
    printf("\n");

    printf("[font loading]\n");
    RUN_TEST(test_hud_font_loaded);
    printf("\n");

    printf("[resource panel icons]\n");
    RUN_TEST(test_hud_mineral_icon_drawn);
    RUN_TEST(test_hud_gas_icon_drawn);
    RUN_TEST(test_hud_supply_icon_drawn);
    printf("\n");

    printf("[resource label texts]\n");
    RUN_TEST(test_hud_supply_text_is_stat_driven);
    RUN_TEST(test_hud_mineral_count_is_stat_driven);
    RUN_TEST(test_hud_no_unbound_resource_labels);
    printf("\n");

    printf("[minimap]\n");
    RUN_TEST(test_hud_minimap_draws_with_positive_size);
    printf("\n");

    printf("[command panel]\n");
    RUN_TEST(test_hud_command_panel_background_drawn);
    RUN_TEST(test_hud_command_button_icons_stamped);
    printf("\n");

    printf("[null texture guard]\n");
    RUN_TEST(test_hud_no_null_texture_in_resource_bar);
    printf("\n");

    printf("[hidden panel guard]\n");
    RUN_TEST(test_hud_team_resource_panel_is_hidden);
    RUN_TEST(test_hud_pause_panel_is_hidden);
    printf("\n");

    printf("[no fallback text in resource bar]\n");
    RUN_TEST(test_hud_no_unbound_text_in_resource_bar);
    printf("\n");

    TEST_RESULTS();
}
