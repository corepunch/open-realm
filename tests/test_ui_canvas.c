#include "test.h"
#include "renderer/r_local.h"

/* Exercise the renderer's actual projection extent, not a second copy of its math. */
TEST(ui_canvas, wc3_renderer_stretches_authored_extent_after_resize) {
    size2_t saved = tr.drawableSize;
    size2_t sizes[] = { {1024,768}, {1920,1200}, {1920,1080}, {3440,1440}, {720,1280}, {0,0}, {1024,768} };
    FOR_LOOP(i, sizeof(sizes) / sizeof(sizes[0])) {
        tr.drawableSize = sizes[i];
        RECT scene = R_UISceneRect();
        T_FEQ(scene.x, 0, 0.0001f); T_FEQ(scene.y, 0, 0.0001f);
        T_FEQ(scene.w, 0.8f, 0.0001f); T_FEQ(scene.h, 0.6f, 0.0001f);
    }
    tr.drawableSize = saved;
}
