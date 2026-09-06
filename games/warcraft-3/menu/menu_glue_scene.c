/*
 * menu_glue_scene.c - shared glue background/sprite-layer model cache.
 */

#include "menu_local.h"

#define UI_GLUE_ANIM_NAME 96

typedef struct {
    char requested[UI_GLUE_ANIM_NAME];
    char active[UI_GLUE_ANIM_NAME];
    DWORD start_time;
    DWORD duration;
    BOOL one_shot;
    BOOL complete;
} uiGlueAnimState_t;

typedef struct {
    BOOL loaded;
    LPCMODEL background;
    LPCMODEL top_left_panel;
    LPCMODEL top_right_panel;
    BOOL background_intro_started;
    BOOL background_intro_complete;
    DWORD background_intro_start;
    DWORD background_intro_duration;
    uiGlueAnimState_t left_anim;
    uiGlueAnimState_t right_anim;
} uiGlueSceneState_t;

static uiGlueSceneState_t menu_glue_scene;

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

/* The stock sprite layers are authored as a 4:3 pair: the left layer remains
 * at the scene origin and the right layer's origin must follow the extra
 * widescreen canvas width. */
static FLOAT UI_GlueRightPanelOffset(LPRENDERER renderer) {
    size2_t win = renderer->GetWindowSize();
    FLOAT aspect;

    if (win.height <= 0) return 0.0f;
    aspect = (FLOAT)win.width / (FLOAT)win.height;
    return aspect > UI_MIN_ASPECT ? UI_BASE_HEIGHT * aspect - UI_BASE_WIDTH : 0.0f;
}

static BOOL UI_GlueSequenceDuration(LPRENDERER renderer, LPCMODEL model, LPCSTR anim, LPDWORD duration) {
    if (!model || !anim || !*anim || !duration) {
        return false;
    }
    return renderer->GetModelAnimationDuration(model, anim, duration) && *duration > 0;
}

static BOOL UI_GlueIsOneShot(LPCSTR anim) {
    if (!anim || !*anim) return false;
    return !strcmp(anim, "Birth") || !strcmp(anim, "Death") ||
           strstr(anim, " Birth") != NULL || strstr(anim, " Death") != NULL;
}

/* Stable glue states are named "<screen> Stand". Retail/Warsmash enters them
 * through the matching non-looping Birth sequence; preserve any suffix such as
 * "Alternate" while swapping the primary tag. */
static BOOL UI_GlueBirthForStand(LPCSTR stand, LPSTR birth, DWORD birth_size) {
    LPCSTR marker;
    size_t prefix;

    if (!stand || !birth || birth_size == 0) return false;
    if (!strcmp(stand, "Stand")) {
        snprintf(birth, birth_size, "Birth");
        return true;
    }
    marker = strstr(stand, " Stand");
    if (!marker) return false;
    prefix = (size_t)(marker - stand);
    if (prefix + strlen(" Birth") + strlen(marker + strlen(" Stand")) + 1 > birth_size) {
        return false;
    }
    memcpy(birth, stand, prefix);
    birth[prefix] = '\0';
    strncat(birth, " Birth", birth_size - strlen(birth) - 1);
    strncat(birth, marker + strlen(" Stand"), birth_size - strlen(birth) - 1);
    return true;
}

static void UI_GlueBeginLayerAnimation(LPRENDERER renderer,
                                       LPCMODEL model,
                                       LPCSTR requested,
                                       uiGlueAnimState_t *state) {
    char birth[UI_GLUE_ANIM_NAME];
    DWORD duration = 0;

    memset(state, 0, sizeof(*state));
    snprintf(state->requested, sizeof(state->requested), "%s", requested ? requested : "Stand");
    snprintf(state->active, sizeof(state->active), "%s", state->requested);
    state->start_time = M_Time();
    state->complete = true;

    if (UI_GlueBirthForStand(state->requested, birth, sizeof(birth)) &&
        UI_GlueSequenceDuration(renderer, model, birth, &duration)) {
        snprintf(state->active, sizeof(state->active), "%s", birth);
        state->duration = duration;
        state->one_shot = true;
        state->complete = false;
        return;
    }
    if (UI_GlueIsOneShot(state->requested) &&
        UI_GlueSequenceDuration(renderer, model, state->requested, &duration)) {
        state->duration = duration;
        state->one_shot = true;
        state->complete = false;
    }
}

static LPCSTR UI_GlueLayerAnimation(LPRENDERER renderer,
                                    LPCMODEL model,
                                    LPCSTR requested,
                                    uiGlueAnimState_t *state,
                                    LPSTR scrubbed,
                                    DWORD scrubbed_size) {
    DWORD elapsed;
    FLOAT ratio;

    if (!requested || !*requested) requested = "Stand";
    if (strcmp(state->requested, requested)) {
        UI_GlueBeginLayerAnimation(renderer, model, requested, state);
    }
    if (!state->one_shot) {
        state->complete = true;
        return state->active;
    }

    elapsed = M_Time() - state->start_time;
    if (elapsed >= state->duration) {
        state->complete = true;
        /* A synthesized Birth hands off to the requested looping Stand. A
         * directly requested Death/Birth holds its authored final pose until
         * the caller changes state. */
        if (strcmp(state->active, state->requested)) {
            snprintf(state->active, sizeof(state->active), "%s", state->requested);
            state->one_shot = false;
            return state->active;
        }
        snprintf(scrubbed, scrubbed_size, "%s@1.0000", state->active);
        return scrubbed;
    }

    ratio = (FLOAT)elapsed / (FLOAT)state->duration;
    snprintf(scrubbed, scrubbed_size, "%s@%.4f", state->active, ratio);
    state->complete = false;
    return scrubbed;
}

static LPCSTR UI_GlueBackgroundAnimation(LPRENDERER renderer, LPSTR scrubbed, DWORD scrubbed_size) {
    DWORD elapsed;
    FLOAT ratio;

    if (!menu_glue_scene.background_intro_started) {
        menu_glue_scene.background_intro_started = true;
        menu_glue_scene.background_intro_start = M_Time();
        menu_glue_scene.background_intro_complete =
            !UI_GlueSequenceDuration(renderer,
                                     menu_glue_scene.background,
                                     "Birth",
                                     &menu_glue_scene.background_intro_duration);
    }
    if (menu_glue_scene.background_intro_complete) {
        return "Stand";
    }

    elapsed = M_Time() - menu_glue_scene.background_intro_start;
    if (elapsed >= menu_glue_scene.background_intro_duration) {
        menu_glue_scene.background_intro_complete = true;
        return "Stand";
    }
    ratio = (FLOAT)elapsed / (FLOAT)menu_glue_scene.background_intro_duration;
    snprintf(scrubbed, scrubbed_size, "Birth@%.4f", ratio);
    return scrubbed;
}

void UI_ResetGlueSceneModels(void) {
    memset(&menu_glue_scene, 0, sizeof(menu_glue_scene));
}

void UI_RestartGlueSceneAnimations(void) {
    menu_glue_scene.background_intro_started = false;
    menu_glue_scene.background_intro_complete = false;
    menu_glue_scene.background_intro_start = 0;
    menu_glue_scene.background_intro_duration = 0;
    memset(&menu_glue_scene.left_anim, 0, sizeof(menu_glue_scene.left_anim));
    memset(&menu_glue_scene.right_anim, 0, sizeof(menu_glue_scene.right_anim));
}

BOOL UI_GlueSceneAnimationComplete(void) {
    return menu_glue_scene.left_anim.complete && menu_glue_scene.right_anim.complete;
}

void UI_ReleaseGlueSceneModels(void) {
    LPRENDERER renderer = menuimport.GetRenderer();

    if (menu_glue_scene.background) renderer->ReleaseModel((LPMODEL)menu_glue_scene.background);
    if (menu_glue_scene.top_left_panel) renderer->ReleaseModel((LPMODEL)menu_glue_scene.top_left_panel);
    if (menu_glue_scene.top_right_panel) renderer->ReleaseModel((LPMODEL)menu_glue_scene.top_right_panel);
    UI_ResetGlueSceneModels();
}

void UI_PreloadGlueSceneModels(void) {
    LPRENDERER renderer;

    if (menu_glue_scene.loaded) {
        return;
    }

    renderer = menuimport.GetRenderer();
    if (!renderer || !renderer->LoadModel) {
        return;
    }

    menu_glue_scene.background = renderer->LoadModel(UI_GlueBackgroundPath());
    menu_glue_scene.top_left_panel = renderer->LoadModel(UI_GlueTopLeftPanelPath());
    menu_glue_scene.top_right_panel = renderer->LoadModel(UI_GlueTopRightPanelPath());
    menu_glue_scene.loaded = true;
    UI_RestartGlueSceneAnimations();
}

void UI_DrawGlueSceneLayers(LPCSTR left_panel_anim, LPCSTR right_panel_anim) {
    LPRENDERER renderer = menuimport.GetRenderer();
    FLOAT right_offset;
    char background_anim[UI_GLUE_ANIM_NAME];
    char left_anim[UI_GLUE_ANIM_NAME];
    char right_anim[UI_GLUE_ANIM_NAME];

    if (!renderer) {
        return;
    }
    right_offset = UI_GlueRightPanelOffset(renderer);

    UI_PreloadGlueSceneModels();

    if (renderer->RenderFrame && menu_glue_scene.background) {
        renderEntity_t entity = {0};
        LPCSTR anim = UI_GlueBackgroundAnimation(renderer, background_anim, sizeof(background_anim));
        entity.model = menu_glue_scene.background;
        entity.scale = 1.0f;
        entity.flags = RF_NO_SHADOW | RF_NO_FOGOFWAR | RF_PORTRAIT_LIGHTING;
        renderer->SetEntityAnimFrame(menu_glue_scene.background, anim, &entity);

        viewDef_t viewdef = {0};
        viewdef.viewport = (RECT){0, 0, 1, 1};
        viewdef.rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG | RDF_USE_ENTITY_CAMERA;
        viewdef.num_entities = 1;
        viewdef.entities = &entity;

        renderer->RenderFrame(&viewdef);
    }

    if (renderer->DrawSprite) {
        if (menu_glue_scene.top_left_panel) {
            LPCSTR anim = UI_GlueLayerAnimation(renderer,
                                                menu_glue_scene.top_left_panel,
                                                left_panel_anim,
                                                &menu_glue_scene.left_anim,
                                                left_anim,
                                                sizeof(left_anim));
            renderer->DrawSprite(menu_glue_scene.top_left_panel, anim, 0.0f, UI_BASE_HEIGHT);
        } else {
            menu_glue_scene.left_anim.complete = true;
        }
        if (menu_glue_scene.top_right_panel) {
            LPCSTR anim = UI_GlueLayerAnimation(renderer,
                                                menu_glue_scene.top_right_panel,
                                                right_panel_anim,
                                                &menu_glue_scene.right_anim,
                                                right_anim,
                                                sizeof(right_anim));
            renderer->DrawSprite(menu_glue_scene.top_right_panel, anim, right_offset, UI_BASE_HEIGHT);
        } else {
            menu_glue_scene.right_anim.complete = true;
        }
    } else {
        menu_glue_scene.left_anim.complete = true;
        menu_glue_scene.right_anim.complete = true;
    }
}

void UI_DrawGlueScene(LPCSTR panel_anim) {
    UI_DrawGlueSceneLayers(panel_anim, panel_anim);
}
