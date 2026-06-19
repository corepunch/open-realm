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

static LPCSTR UI_GlueBackgroundPath(void) {
    LPCSTR model = Theme_String("GlueSpriteLayerBackground", "Default");

    if (!model || !*model || !strcmp(model, "GlueSpriteLayerBackground")) {
        model = Theme_String("MainMenu", "Default");
    }
    if (!model || !*model || !strcmp(model, "MainMenu")) {
        model = "UI\\Glues\\MainMenu\\MainMenu3d\\MainMenu3d.mdx";
    }
    return model;
}

static LPCSTR UI_GlueTopLeftPanelPath(void) {
    return Theme_String("GlueSpriteLayerTopLeft", "UI\\Glues\\SpriteLayers\\TopLeftPanel.mdx");
}

static LPCSTR UI_GlueTopRightPanelPath(void) {
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

    if (renderer->DrawPortrait && ui_glue_scene.background) {
        RECT viewport = { 0, 0, 1, 1 };
        PORTRAITDEF p = {
            .model = ui_glue_scene.background,
            .viewport = &viewport,
            .anim = "Stand",
            .frame = 0
        };
        renderer->DrawPortrait(&p);
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
