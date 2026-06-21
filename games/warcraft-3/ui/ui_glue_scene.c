/*
 * ui_glue_scene.c - shared glue background/sprite-layer model cache.
 */

#include "ui_local.h"

typedef struct {
    BOOL loaded;
    LPCMODEL background;
    LPCMODEL top_left_panel;
    LPCMODEL top_right_panel;
} uiGlueSceneState_t;

static uiGlueSceneState_t ui_glue_scene;

LPCSTR UI_GlueBackgroundPath(void) {
    LPCSTR model = Theme_String("GlueSpriteLayerBackground", "Default");

    if (!model || !*model || !strcmp(model, "GlueSpriteLayerBackground")) {
        model = Theme_String("MainMenu", "Default");
    }
    if (!model || !*model || !strcmp(model, "MainMenu")) {
        model = "UI\\Glues\\MainMenu\\MainMenu3d\\MainMenu3d.mdx";
    }
    return model;
}

LPCSTR UI_GlueTopLeftPanelPath(void) {
    return Theme_String("GlueSpriteLayerTopLeft", "UI\\Glues\\SpriteLayers\\TopLeftPanel.mdx");
}

LPCSTR UI_GlueTopRightPanelPath(void) {
    return Theme_String("GlueSpriteLayerTopRight", "UI\\Glues\\SpriteLayers\\TopRightPanel.mdx");
}

void UI_ResetGlueSceneModels(void) {
    memset(&ui_glue_scene, 0, sizeof(ui_glue_scene));
}

void UI_PreloadGlueSceneModels(void) {
    LPRENDERER renderer;

    if (ui_glue_scene.loaded) {
        return;
    }

    renderer = uiimport.GetRenderer ? uiimport.GetRenderer() : NULL;
    if (!renderer || !renderer->LoadModel) {
        return;
    }

    ui_glue_scene.background = renderer->LoadModel(UI_GlueBackgroundPath());
    ui_glue_scene.top_left_panel = renderer->LoadModel(UI_GlueTopLeftPanelPath());
    ui_glue_scene.top_right_panel = renderer->LoadModel(UI_GlueTopRightPanelPath());
    ui_glue_scene.loaded = true;
}

void UI_DrawGlueSceneLayers(LPCSTR left_panel_anim, LPCSTR right_panel_anim) {
    LPRENDERER renderer = uiimport.GetRenderer ? uiimport.GetRenderer() : NULL;

    if (!renderer) {
        return;
    }

    UI_PreloadGlueSceneModels();

    if (renderer->RenderFrame && ui_glue_scene.background) {
        renderEntity_t entity = {0};
        entity.model = ui_glue_scene.background;
        entity.scale = 1.0f;
        entity.flags = RF_NO_SHADOW | RF_NO_FOGOFWAR | RF_PORTRAIT_LIGHTING;
        renderer->SetEntityAnimFrame(ui_glue_scene.background, "Stand", &entity);

        viewDef_t viewdef = {0};
        viewdef.viewport = (RECT){0, 0, 1, 1};
        viewdef.rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG | RDF_USE_ENTITY_CAMERA;
        viewdef.num_entities = 1;
        viewdef.entities = &entity;

        renderer->RenderFrame(&viewdef);
    }

    if (renderer->DrawSprite) {
        if (ui_glue_scene.top_left_panel) {
            renderer->DrawSprite(ui_glue_scene.top_left_panel, left_panel_anim, 0.0f, UI_BASE_HEIGHT);
        }
        if (ui_glue_scene.top_right_panel) {
            renderer->DrawSprite(ui_glue_scene.top_right_panel, right_panel_anim, 0.0f, UI_BASE_HEIGHT);
        }
    }
}

void UI_DrawGlueScene(LPCSTR panel_anim) {
    UI_DrawGlueSceneLayers(panel_anim, panel_anim);
}

/* Spawn background + side-panel frames as children of root (idempotent).
 * left_anim / right_anim are the animation names (may be equal). */
void UI_SpawnGlueSceneFrames(LPFRAMEDEF root, LPCSTR left_anim, LPCSTR right_anim,
                              LPFRAMEDEF *out_bg, LPFRAMEDEF *out_left, LPFRAMEDEF *out_right) {
    if (!root) return;

    if (out_bg && !*out_bg) {
        LPFRAMEDEF f = UI_Spawn(FT_MODEL, root);
        if (f) {
            /* Full-screen viewport: anchor TOPLEFT to scene, size 1×1 (RenderFrame uses 0..1 normalized). */
            UI_SetPoint(f, FRAMEPOINT_TOPLEFT, NULL, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
            UI_SetSize(f, 1.0f, 1.0f);
            f->Portrait.model = UI_LoadModel(UI_GlueBackgroundPath(), false);
            UI_SetTextPointer(f, left_anim);
        }
        *out_bg = f;
    }
    if (out_left && !*out_left) {
        LPFRAMEDEF f = UI_Spawn(FT_SPRITE, root);
        if (f) {
            UI_SetAllPoints(f);
            f->Portrait.model = UI_LoadModel(UI_GlueTopLeftPanelPath(), false);
            UI_SetTextPointer(f, left_anim);
        }
        *out_left = f;
    }
    if (out_right && !*out_right) {
        LPFRAMEDEF f = UI_Spawn(FT_SPRITE, root);
        if (f) {
            UI_SetAllPoints(f);
            f->Portrait.model = UI_LoadModel(UI_GlueTopRightPanelPath(), false);
            UI_SetTextPointer(f, right_anim);
        }
        *out_right = f;
    }
}

/* Update glue scene frame animations (call when scene state changes). */
void UI_SetGlueSceneAnim(LPFRAMEDEF bg, LPFRAMEDEF left, LPFRAMEDEF right,
                          LPCSTR left_anim, LPCSTR right_anim) {
    if (bg)    UI_SetTextPointer(bg, left_anim);
    if (left)  UI_SetTextPointer(left, left_anim);
    if (right) UI_SetTextPointer(right, right_anim);
}
