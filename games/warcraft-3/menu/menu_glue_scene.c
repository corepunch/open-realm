/*
 * scene.c - shared glue background/sprite-layer model cache.
 */

#include "menu_local.h"
#include "menu_glue_motion.h"

#define UI_GLUE_ANIM_NAME 96 // chars; fits Blizzard glue sequence names and suffixes; used as animation storage.
#define UI_GLUE_BIRTH_TIME 1000 // ms; every named RoC/TFT panel Birth interval has this length.
#define UI_GLUE_DEATH_TIME 667 // ms; every named RoC/TFT panel Death interval rounds to this length.

#define BZ_GLUE_MAX_TABS 3 // tabs; largest authored group is custom-game browser/create/options.

typedef struct { LPCSTR stand, enter, leave; } GLUETAB;
typedef GLUETAB *LPGLUETAB;
typedef const GLUETAB *LPCGLUETAB;
typedef struct { LPCSTR name; GLUETAB tabs[BZ_GLUE_MAX_TABS]; } GLUEPANEL;
typedef GLUEPANEL *LPGLUEPANEL;
typedef const GLUEPANEL *LPCGLUEPANEL;

typedef enum {
    UI_GLUE_PANEL_IDLE,
    UI_GLUE_PANEL_EXIT,
    UI_GLUE_PANEL_ENTER,
} uiGluePanelPhase_t;

typedef struct {
    GLUEDEST current, target;
    DWORD start;
    uiGluePanelPhase_t phase;
} GLUELAYER;
typedef GLUELAYER *LPGLUELAYER;
typedef const GLUELAYER *LPCGLUELAYER;

typedef struct {
    BOOL loaded;
    LPCMODEL background, top_left_panel, top_right_panel;
    GLUELAYER layers[UI_GLUE_SIDE_COUNT];
    uiGluePanelChanged_f exited, changed;
} GLUESCENE;
typedef GLUESCENE *LPGLUESCENE;
typedef const GLUESCENE *LPCGLUESCENE;

static GLUESCENE scene;

/* Sequence families differ in the MDX: Create has Birth/Death, while morph-only
 * tabs retain their authored final pose instead of inventing a Stand sequence. */
static const GLUEPANEL glue_panels[UI_GLUE_PANEL_COUNT] = {
    [UI_GLUE_MAIN_MENU] = { .name = "MainMenu" },
    [UI_GLUE_REALM_SELECTION] = { .name = "RealmSelection" },
    [UI_GLUE_SINGLE_PLAYER] = { .name = "SinglePlayer", .tabs = {
        [1] = { "SinglePlayerSkirmish Stand", "SinglePlayerSkirmish Morph", "SinglePlayerSkirmish Morph Alternate" },
    } },
    [UI_GLUE_OPTIONS] = { .name = "Options", .tabs = {
        [1] = { "Options Stand Alternate", "Options Morph", "Options Morph Alternate" },
    } },
    [UI_GLUE_MULTIPLAYER_PRE_GAME_CHAT] = { .name = "MultiplayerPreGameChat", .tabs = {
        [1] = { "MultiplayerSubmenu Morph@1.0000", "MultiplayerSubmenu Morph", "MultiplayerSubmenu Morph Alternate" },
    } },
    [UI_GLUE_BATTLENET_CUSTOM] = { .name = "BattlenetCustom", .tabs = {
        [1] = { "BattlenetCustomCreate Stand", "BattlenetCustomCreate Birth", "BattlenetCustomCreate Death" },
        [2] = { "BattlenetAdvancedOptions Morph@1.0000", "BattlenetAdvancedOptions Morph", "BattlenetAdvancedOptions Morph Alternate" },
    } },
};

/* Content without a moving left mesh (logo/profile) follows the navigation's
 * sampled travel. Each side intentionally moves as one rigid FDF partition. */
static LPCGLUEMOTION const motion[UI_GLUE_PANEL_COUNT][UI_GLUE_SIDE_COUNT][BZ_GLUE_MAX_TABS] = {
    [UI_GLUE_MAIN_MENU] = {{&motion_main}, {&motion_main}},
    [UI_GLUE_REALM_SELECTION] = {{&motion_realm}, {&motion_main}},
    [UI_GLUE_SINGLE_PLAYER] = {{&motion_main, &motion_main}, {&motion_main}},
    [UI_GLUE_OPTIONS] = {{&motion_opts, &motion_tab}, {&motion_opts}},
    [UI_GLUE_MULTIPLAYER_PRE_GAME_CHAT] = {{&motion_chat, &motion_chat}, {&motion_chatnav}},
    [UI_GLUE_BATTLENET_CUSTOM] = {{&motion_lan, &motion_create, &motion_lan}, {&motion_lannav}},
};

static LPCSTR const phases[] = { "Stand", "Death", "Birth" };
static DWORD const durations[] = { 0, UI_GLUE_DEATH_TIME, UI_GLUE_BIRTH_TIME };
static void UI_GlueReleaseModel(LPCMODEL model) {
    mi.GetRenderer()->ReleaseModel((LPMODEL)model);
}

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

/* Both clocks use the renderer's normalized sequence-time contract. */
static void UI_GlueProgress(LPSTR anim, DWORD start, DWORD duration) {
    size_t len = strlen(anim);
    snprintf(anim + len, UI_GLUE_ANIM_NAME - len, "@%.4f", (FLOAT)MIN(M_Time() - start, duration) / duration);
}

/* A non-default left panel must enter/leave its authored pose. Options Birth
 * animates only the base layout; using it before Stand Alternate caused a snap. */
static LPCSTR UI_GlueLayerAnimation(LPCGLUELAYER layer, LPSTR anim) {
    LPCGLUEPANEL panel = &glue_panels[layer->current.panel];
    if (layer->current.tab) {
        LPCGLUETAB tab = &panel->tabs[layer->current.tab];
        LPCSTR names[] = { tab->stand, tab->leave, tab->enter };
        snprintf(anim, UI_GLUE_ANIM_NAME, "%s", names[layer->phase]);
    } else snprintf(anim, UI_GLUE_ANIM_NAME, "%s %s", panel->name, phases[layer->phase]);
    if (layer->phase != UI_GLUE_PANEL_IDLE)
        UI_GlueProgress(anim, layer->start, durations[layer->phase]);
    return anim;
}

#ifdef WC3_DEBUG_GLUE
/* Boundary diagnostics share the exact sequence resolver used for drawing. */
static void UI_GlueDebugPhase(LPCGLUELAYER layer) {
    char anim[UI_GLUE_ANIM_NAME];
    fprintf(stderr, "WC3 glue: side=%ld current=%u/%d/%d target=%u/%d/%d anim=\"%s\"\n",
            layer - scene.layers, layer->current.panel, layer->current.tab, layer->current.page,
            layer->target.panel, layer->target.tab, layer->target.page,
            layer->current.panel ? UI_GlueLayerAnimation(layer, anim) : "none");
}
#else
#define UI_GlueDebugPhase(LAYER) ((void)0)
#endif

void UI_ResetGlueTransitions(void) {
    memset(scene.layers, 0, sizeof(scene.layers));
    scene.exited = scene.changed = NULL;
}

void UI_ResetGlueSceneModels(void) {
    memset(&scene, 0, sizeof(scene));
}

void UI_ReleaseGlueSceneModels(void) {
    SAFE_DELETE(scene.background, UI_GlueReleaseModel);
    SAFE_DELETE(scene.top_left_panel, UI_GlueReleaseModel);
    SAFE_DELETE(scene.top_right_panel, UI_GlueReleaseModel);
    UI_ResetGlueSceneModels();
}

/* Cache one load attempt per scene lifetime, but identify every missing sprite layer. */
static LPCMODEL UI_GlueLoadModel(LPCSTR path) {
    LPCMODEL model = mi.GetRenderer()->LoadModel(path);
    if (!model) fprintf(stderr, "UI: failed to load glue model '%s'\n", path);
    return model;
}

void UI_PreloadGlueSceneModels(void) {
    if (scene.loaded) return;
    scene.background = UI_GlueLoadModel(UI_GlueBackgroundPath());
    scene.top_left_panel = UI_GlueLoadModel(UI_GlueTopLeftPanelPath());
    scene.top_right_panel = UI_GlueLoadModel(UI_GlueTopRightPanelPath());
    scene.loaded = true;
}

/* Page identity makes two content tabs sharing one MDX pose transition too. */
static BOOL UI_GlueSameDest(GLUEDEST a, GLUEDEST b) {
    return a.panel == b.panel && a.tab == b.tab && a.page == b.page;
}

BOOL UI_GlueSideReady(uiGlueSide_t side) {
    LPCGLUELAYER layer = &scene.layers[side];
    return layer->phase == UI_GLUE_PANEL_IDLE && UI_GlueSameDest(layer->current, layer->target);
}

/* Sample the same phase clock as the MDX; retain overshoot and the native exit curve. */
FLOAT UI_GlueSideOffset(uiGlueSide_t side) {
    LPCGLUELAYER layer = &scene.layers[side];
    if (layer->phase == UI_GLUE_PANEL_IDLE || !layer->current.panel) return 0;
    LPCGLUEMOTION track = motion[layer->current.panel][side][layer->current.tab];
    FLOAT pos = (BZ_GLUE_SAMPLES - 1) * (FLOAT)MIN(M_Time() - layer->start, durations[layer->phase]) / durations[layer->phase];
    int idx = MIN((int)pos, BZ_GLUE_SAMPLES - 2);
    FLOAT const *curve = layer->phase == UI_GLUE_PANEL_ENTER ? track->enter : track->leave;
    return curve[idx] + (curve[idx + 1] - curve[idx]) * (pos - idx);
}

BOOL UI_GlueIsTransitioning(void) {
    return !UI_GlueSideReady(UI_GLUE_LEFT) || !UI_GlueSideReady(UI_GLUE_RIGHT);
}

/* Finish the running sequence before honoring a retarget. A tab leaves through
 * its base pose; a different family leaves through the closed-panel state. */
static void UI_GlueAdvanceLayer(LPGLUELAYER layer) {
    if (layer->phase != UI_GLUE_PANEL_IDLE) {
        if (M_Time() - layer->start < durations[layer->phase]) return;
        if (layer->phase == UI_GLUE_PANEL_EXIT) {
            if (layer->current.tab && layer->current.panel == layer->target.panel)
                layer->current = (GLUEDEST){ .panel = layer->current.panel };
            else layer->current = (GLUEDEST){0};
        }
        layer->phase = UI_GLUE_PANEL_IDLE;
    }
    if (!UI_GlueSameDest(layer->current, layer->target)) {
        if (layer->current.panel && (layer->current.panel != layer->target.panel || layer->current.tab))
            layer->phase = UI_GLUE_PANEL_EXIT;
        else {
            layer->current = layer->target;
            layer->phase = layer->current.panel ? UI_GLUE_PANEL_ENTER : UI_GLUE_PANEL_IDLE;
        }
        layer->start = M_Time();
    }
    UI_GlueDebugPhase(layer);
}

/* Clear callback ownership before dispatch: screen installation may queue work.
 * Both sides must reach the destination before any of its controls are installed. */
static void UI_GlueAdvanceTransition(void) {
    BOOL arrived = true;
    FOR_LOOP(i, UI_GLUE_SIDE_COUNT) {
        LPGLUELAYER layer = &scene.layers[i];
        if (!UI_GlueSideReady(i)) UI_GlueAdvanceLayer(layer);
        if (layer->phase == UI_GLUE_PANEL_EXIT || !UI_GlueSameDest(layer->current, layer->target)) arrived = false;
    }
    if (arrived && scene.exited) {
        uiGluePanelChanged_f exited = scene.exited;
        scene.exited = NULL;
        exited();
    }
    if (!UI_GlueIsTransitioning() && scene.changed) {
        uiGluePanelChanged_f changed = scene.changed;
        scene.changed = NULL;
        changed();
    }
}

/* Left content and right navigation own separate clocks, targets, and readiness.
 * Only startup overrides can replace an unfinished Birth without first leaving. */
void UI_GotoGluePanel(GLUEDEST dest, uiGluePanelChanged_f exited, uiGluePanelChanged_f changed) {
    if (dest.panel < UI_GLUE_NONE || dest.panel >= UI_GLUE_PANEL_COUNT || dest.tab < 0 ||
        dest.tab >= BZ_GLUE_MAX_TABS || (dest.tab && !glue_panels[dest.panel].tabs[dest.tab].stand)) {
        fprintf(stderr, "UI: invalid glue destination %u/%d\n", (unsigned)dest.panel, dest.tab);
        return;
    }
    if (dest.panel) UI_PreloadGlueSceneModels();
    BOOL startup = dest.panel && scene.layers[UI_GLUE_RIGHT].phase == UI_GLUE_PANEL_ENTER &&
        dest.panel != scene.layers[UI_GLUE_RIGHT].current.panel;
    scene.exited = exited;
    scene.changed = changed;
    FOR_LOOP(i, UI_GLUE_SIDE_COUNT) {
        LPGLUELAYER layer = &scene.layers[i];
        layer->target = i == UI_GLUE_LEFT ? dest : (GLUEDEST){ .panel = dest.panel };
        if (startup) {
            layer->current = layer->target;
            layer->phase = UI_GLUE_PANEL_ENTER;
            layer->start = M_Time();
            UI_GlueDebugPhase(layer);
        }
    }
    UI_GlueAdvanceTransition();
}

void UI_CloseGluePanel(uiGluePanelChanged_f changed) { UI_GotoGluePanel((GLUEDEST){0}, NULL, changed); }

void UI_DrawGlueScene(void) {
    LPRENDERER renderer = mi.GetRenderer();
    FLOAT right_offset;
    char left_anim[UI_GLUE_ANIM_NAME];
    char right_anim[UI_GLUE_ANIM_NAME];

    UI_GlueAdvanceTransition();
    if (!scene.layers[UI_GLUE_LEFT].current.panel && !scene.layers[UI_GLUE_RIGHT].current.panel) return;
    UI_PreloadGlueSceneModels();
    right_offset = UI_GlueRightPanelOffset(renderer);

    if (scene.background) {
        renderEntity_t entity = {
            .model = scene.background, .scale = 1.0f,
            .flags = RF_NO_SHADOW | RF_NO_FOGOFWAR | RF_PORTRAIT_LIGHTING,
        };
        renderer->SetEntityAnimFrame(scene.background, "Stand", &entity);

        viewDef_t viewdef = {
            .viewport = {0, 0, 1, 1}, .num_entities = 1, .entities = &entity,
            .rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG | RDF_USE_ENTITY_CAMERA,
        };
        renderer->RenderFrame(&viewdef);
    }

    if (scene.top_left_panel && scene.layers[UI_GLUE_LEFT].current.panel)
        renderer->DrawSprite(&MAKE(drawSprite_t, .model = scene.top_left_panel, .anim = UI_GlueLayerAnimation(&scene.layers[UI_GLUE_LEFT], left_anim), .y = UI_BASE_HEIGHT, .id = &scene.layers[UI_GLUE_LEFT]));
    if (scene.top_right_panel && scene.layers[UI_GLUE_RIGHT].current.panel)
        renderer->DrawSprite(&MAKE(drawSprite_t, .model = scene.top_right_panel, .anim = UI_GlueLayerAnimation(&scene.layers[UI_GLUE_RIGHT], right_anim), .x = right_offset, .y = UI_BASE_HEIGHT, .id = &scene.layers[UI_GLUE_RIGHT]));
}
