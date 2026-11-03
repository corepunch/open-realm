#include "test.h"
#include "renderer/r_local.h"

refImport_t ri;
struct render_globals tr;
static rect_t font_test_scene;
static bool font_test_enabled;

static cstring_t font_test_cvar(cstring_t name, cstring_t fallback) {
    if (!strcmp(name, "r_font_lowres_sharp")) return font_test_enabled ? "1" : "0";
    return fallback;
}

rect_t R_UISceneRect(void) { return font_test_scene; }

/* Include production helpers so the test exercises the renderer's thresholds
 * and coordinate conversion without requiring a GL context or font asset. */
#include "renderer/r_font.c"

TEST(renderer_font, low_resolution_anchor_snapping_uses_drawable_pixel_scale) {
    size2_t saved_size = tr.drawableSize;
    rect_t saved_scene = font_test_scene;
    cstring_t (*saved_cvar)(cstring_t, cstring_t) = ri.CvarString;
    vec2_t snapped;

    ri.CvarString = font_test_cvar;
    font_test_enabled = true;
    tr.drawableSize = (size2_t){640, 480};
    T_ASSERT(R_LowResSharpFonts());
    font_test_scene = (rect_t){100.0f, 50.0f, 400.0f, 300.0f};
    snapped = R_SnapTextAnchor((vec2_t){111.4f, 64.4f});
    T_FEQ(snapped.x, 111.25f, 0.0001f);
    T_FEQ(snapped.y, 64.375f, 0.0001f);

    font_test_enabled = false;
    T_ASSERT(!R_LowResSharpFonts());
    font_test_enabled = true;
    tr.drawableSize = (size2_t){1024, 768};
    T_ASSERT(!R_LowResSharpFonts());
    tr.drawableSize = (size2_t){640, 700};
    T_ASSERT(!R_LowResSharpFonts());

    tr.drawableSize = saved_size;
    font_test_scene = saved_scene;
    ri.CvarString = saved_cvar;
}
