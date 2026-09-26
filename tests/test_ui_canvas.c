#include "test.h"
#include "renderer/r_local.h"
#include "common/ui_canvas.h"

/* The resolver is the single canvas contract; pin every policy at the aspects that matter. */
TEST(ui_canvas, resolver_matrix_covers_stretch_expand_and_centered_policies) {
    struct { size2_t window; UICANVASPOLICY policy; float width, root_x, root_w; UICANVASCLASS chrome; } cases[] = {
        { {1024,768}, UI_CANVAS_STRETCH, 0.8f, 0, 0.8f, UI_CANVAS_STANDARD },
        { {1920,1080}, UI_CANVAS_STRETCH, 0.8f, 0, 0.8f, UI_CANVAS_STANDARD },
        { {720,1280}, UI_CANVAS_STRETCH, 0.8f, 0, 0.8f, UI_CANVAS_STANDARD },
        { {1024,768}, UI_CANVAS_EXPAND, 0.8f, 0, 0.8f, UI_CANVAS_STANDARD },
        { {1920,1080}, UI_CANVAS_EXPAND, 1.0666667f, 0, 1.0666667f, UI_CANVAS_STANDARD },
        { {1024,768}, UI_CANVAS_EXPAND_CENTER, 0.8f, 0, 0.8f, UI_CANVAS_STANDARD },
        { {1280,1024}, UI_CANVAS_EXPAND_CENTER, 0.8f, 0, 0.8f, UI_CANVAS_STANDARD },
        { {720,1280}, UI_CANVAS_EXPAND_CENTER, 0.8f, 0, 0.8f, UI_CANVAS_STANDARD },
        { {1920,1200}, UI_CANVAS_EXPAND_CENTER, 0.96f, 0.08f, 0.8f, UI_CANVAS_WIDE },
        { {1920,1080}, UI_CANVAS_EXPAND_CENTER, 1.0666667f, 0.1333333f, 0.8f, UI_CANVAS_WIDE },
        { {3440,1440}, UI_CANVAS_EXPAND_CENTER, 1.4333334f, 0.3166667f, 0.8f, UI_CANVAS_WIDE },
        { {0,0}, UI_CANVAS_EXPAND_CENTER, 0.8f, 0, 0.8f, UI_CANVAS_STANDARD },
        { {1280,0}, UI_CANVAS_EXPAND, 0.8f, 0, 0.8f, UI_CANVAS_STANDARD },
    };
    FOR_LOOP(i, sizeof(cases) / sizeof(cases[0])) {
        uiCanvas_t canvas = UI_ResolveCanvas(cases[i].window, cases[i].policy);
        T_FEQ(canvas.scene.x, 0, 0.0001f); T_FEQ(canvas.scene.y, 0, 0.0001f);
        T_FEQ(canvas.scene.w, cases[i].width, 0.0001f); T_FEQ(canvas.scene.h, 0.6f, 0.0001f);
        T_FEQ(canvas.root.x, cases[i].root_x, 0.0001f); T_FEQ(canvas.root.y, 0, 0.0001f);
        T_FEQ(canvas.root.w, cases[i].root_w, 0.0001f); T_FEQ(canvas.root.h, 0.6f, 0.0001f);
        T_EQ(canvas.chrome, cases[i].chrome);
        T_EQ(canvas.policy, cases[i].policy);
        T_EQ(canvas.window.width, cases[i].window.width);
    }
}

/* The renderer projects whatever the client pushed; before any push, and for empty rects, the authored scene
 * applies. */
TEST(ui_canvas, renderer_projects_the_pushed_scene_and_rejects_empty_ones) {
    rect_t saved = tr.uiScene;
    tr.uiScene = (rect_t){ 0 };
    rect_t scene = R_UISceneRect();
    T_FEQ(scene.w, 0.8f, 0.0001f); T_FEQ(scene.h, 0.6f, 0.0001f);
    R_SetUIScene(&MAKE(rect_t, 0, 0, 1.0666667f, 0.6f));
    scene = R_UISceneRect();
    T_FEQ(scene.x, 0, 0.0001f); T_FEQ(scene.w, 1.0666667f, 0.0001f); T_FEQ(scene.h, 0.6f, 0.0001f);
    R_SetUIScene(&MAKE(rect_t, 0, 0, 0, 0.6f));
    R_SetUIScene(NULL);
    T_FEQ(R_UISceneRect().w, 1.0666667f, 0.0001f);
    R_SetUIScene(&MAKE(rect_t, 0, 0, 0.8f, 0.6f));
    T_FEQ(R_UISceneRect().w, 0.8f, 0.0001f);
    tr.uiScene = saved;
}
