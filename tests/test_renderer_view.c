#include "test.h"
#include "renderer/r_local.h"
#include "renderer/r_game.h"

struct render_globals tr;
static viewDef_t drawn;
static bool camera;
static int entities, scenes;

/* Exercise production view ownership while replacing only game/GPU passes. */
#undef R_Call
#define R_Call(func, ...) ((void)0)
#include "renderer/r_view.c"

void R_SetupEnvironmentLighting(void) {}
void R_ConformGroundSurfaces(viewDef_t *view) { (void)view; }
void R_SetupViewport(rect_t const * rect) { (void)rect; }
void R_SetupScissor(rect_t const * rect) { (void)rect; }
void R_SetupGL(bool light) { (void)light; }
void R_RevertSettings(void) {}
void R_RenderFogOfWar(void) {}
uint32_t R_GetFogOfWarTexture(void) { return 0; }
void R_DrawEntities(void) { drawn = tr.viewDef; entities++; }
void R_RenderView(void) { drawn = tr.viewDef; scenes++; }

/* Shadow batches must follow fog changes and never inherit world fog in a portrait view. */
TEST(renderer_view, shadow_fog_follows_each_view) {
    renderEntity_t ent = {0};
    viewDef_t view = { .time = 1, .fogEnable = true, .fogStart = 800, .fogEnd = 3500,
        .fogColor = {0.2f, 0.3f, 0.4f}, .entities = &ent, .num_entities = 1 };
    LPCSPRITESTATE fog = &tr.shader_shadowSplat.state;
    FOR_LOOP(i, 2) {
        view.rdflags = i ? RDF_USE_ENTITY_CAMERA : 0;
        view.fogEnable = true;
        R_RenderFrame(&view);
        T_ASSERT(fog->fogEnable);
        T_FEQ(fog->fogParams.x, view.fogStart, 0.0001f);
        T_FEQ(fog->fogParams.y, view.fogEnd, 0.0001f);
        T_EQ(memcmp(&fog->fogColor, &view.fogColor, sizeof(view.fogColor)), 0);
        view.rdflags |= RDF_NOWORLDMODEL;
        R_RenderFrame(&view);
        T_ASSERT(!fog->fogEnable);
        view.rdflags &= ~RDF_NOWORLDMODEL;
        R_RenderFrame(&view);
        T_ASSERT(fog->fogEnable);
        view.fogEnable = false;
        R_RenderFrame(&view);
        T_ASSERT(!fog->fogEnable);
        view.fogStart = 2200; view.fogEnd = 6000;
        view.fogColor = (VECTOR3){0.4f, 0.5f, 0.6f};
    }
}

/* A model camera changes the projection as well as the portrait viewport/flags. */
bool R_ExtractEntityCamera(renderEntity_t const *ent, float aspect, viewDef_t *view) {
    (void)ent; (void)aspect;
    Matrix4_identity(&view->viewProjectionMatrix);
    return camera;
}

/* HUD consumers must still see the world camera after every kind of no-world scene. */
TEST(renderer_view, portrait_preserves_world_camera) {
    renderEntity_t ent = {0};
    viewDef_t world = { .time = 1234, .viewport = {0, 0.22f, 1, 0.76f} };
    viewDef_t portrait = { .time = 5678, .viewport = {0.32f, 0.04f, 0.08f, 0.14f}, .entities = &ent };
    Matrix4_identity(&world.viewProjectionMatrix);
    world.viewProjectionMatrix.v[0] = 2;
    R_RenderFrame(&world);
    viewDef_t saved = tr.viewDef;
    FOR_LOOP(i, 4) {
        portrait.rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL;
        if (i < 3) portrait.rdflags |= RDF_USE_ENTITY_CAMERA;
        portrait.num_entities = i < 2 ? 1 : 0;
        camera = i == 0;
        entities = scenes = 0;
        R_RenderFrame(&portrait);
        T_EQ(entities, i < 2 ? 1 : 0);
        T_EQ(scenes, i < 2 ? 0 : 1);
        T_EQ(drawn.rdflags, portrait.rdflags);
        T_FEQ(drawn.viewport.w, portrait.viewport.w, 0.0001f);
        T_EQ(drawn.time, portrait.time);
        T_EQ(memcmp(&tr.viewDef, &saved, sizeof(saved)), 0);
    }
}

/* A new world render must replace the previous camera, including entity-camera views. */
TEST(renderer_view, world_camera_advances) {
    renderEntity_t ent = {0};
    FOR_LOOP(i, 2) {
        viewDef_t world = { .time = 1234 + i, .viewport = {0, 0.22f, 1, 0.76f}, .entities = &ent, .num_entities = 1 };
        world.rdflags = i ? RDF_USE_ENTITY_CAMERA : 0;
        Matrix4_identity(&world.viewProjectionMatrix);
        tr.viewDef = (viewDef_t){ .time = 100, .rdflags = RDF_NOWORLDMODEL };
        camera = true;
        entities = scenes = 0;
        R_RenderFrame(&world);
        T_EQ(entities, i ? 1 : 0);
        T_EQ(scenes, i ? 0 : 1);
        T_EQ(tr.viewDef.time, world.time);
        T_EQ(tr.viewDef.rdflags, world.rdflags);
        T_EQ(memcmp(&tr.viewDef, &drawn, sizeof(drawn)), 0);
    }
}
