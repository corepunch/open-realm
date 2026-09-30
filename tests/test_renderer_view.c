#include "test.h"
#include "renderer/r_local.h"
#include "renderer/r_game.h"

struct render_globals tr;
static viewDef_t drawn;
static bool camera;
static int entities, scenes;
static int particle_scene_begins, particle_scene_ends, particle_draws;
static particleScene_t *captured_particle_scene;
static particleScene_t *captured_particle_scenes[16];
static int particle_scene_clears;
static int particle_order;
static handle_t test_particle_scene_alloc(long size);
static void test_particle_scene_free(handle_t ptr);

TEST(renderer_view, building_ubersplat_tracks_runtime_structure_flag) {
    renderEntity_t entity = { .splat = (texture_t const *)(uintptr_t)1 };

    T_ASSERT(!R_ShouldRenderUberSplat(&entity));
    entity.flags |= RF_BUILDING;
    T_ASSERT(R_ShouldRenderUberSplat(&entity));
    entity.flags |= RF_NO_UBERSPLAT;
    T_ASSERT(!R_ShouldRenderUberSplat(&entity));
    entity.flags &= ~RF_NO_UBERSPLAT;
    entity.flags &= ~RF_BUILDING;
    T_ASSERT(!R_ShouldRenderUberSplat(&entity));
}

/* Exercise production view ownership while replacing only game/GPU passes. */
#undef R_Call
#define R_Call(func, ...) ((void)0)
#include "renderer/r_view.c"

void R_SetupEnvironmentLighting(void) {}
void R_ConformGroundSurfaces(viewDef_t *view) { (void)view; }
void R_SetupViewport(rect_t const *rect) { (void)rect; }
void R_SetupScissor(rect_t const *rect) { (void)rect; }
void R_SetupGL(bool light) { (void)light; }
void R_RevertSettings(void) {}
void R_RenderFogOfWar(void) {}
uint32_t R_GetFogOfWarTexture(void) { return 0; }
void R_DrawEntityCameraEventSpawns(model_t const *source_model, uintptr_t source_instance_id) {
    (void)source_model;
    (void)source_instance_id;
}
void R_ReleaseGameEntityCameraEvents(uintptr_t instance_id) { (void)instance_id; }
void R_DrawEntities(void) {
    drawn = tr.viewDef;
    entities++;
    if (particle_scene_begins > particle_scene_ends) particle_order = particle_order * 10 + 4;
}
void R_RenderView(void) { drawn = tr.viewDef; scenes++; }
cparticle_t *R_BeginParticleScene(particleScene_t *scene) {
    captured_particle_scene = scene;
    if (particle_scene_begins < (int)(sizeof(captured_particle_scenes) / sizeof(captured_particle_scenes[0])))
        captured_particle_scenes[particle_scene_begins] = scene;
    particle_scene_begins++;
    particle_order = particle_order * 10 + 1;
    return NULL;
}
void R_EndParticleScene(particleScene_t *scene, cparticle_t *previous) {
    T_ASSERT(scene == captured_particle_scene);
    T_NULL(previous);
    particle_scene_ends++;
    particle_order = particle_order * 10 + 3;
}
void R_ClearParticleScene(particleScene_t *scene) {
    *scene = (particleScene_t){0};
    particle_scene_clears++;
}
void R_DrawParticles(void) {
    drawn = tr.viewDef;
    particle_draws++;
    particle_order = particle_order * 10 + 2;
}

/* Shadow batches must follow fog changes and never inherit world fog in a portrait view. */
TEST(renderer_view, shadow_fog_follows_each_view) {
    renderEntity_t ent = {0};
    viewDef_t view = { .time = 1, .fogEnable = true, .fogStart = 800, .fogEnd = 3500,
        .fogColor = {0.2f, 0.3f, 0.4f}, .entities = &ent, .num_entities = 1 };
    spriteState_t const *fog = &tr.shader_shadowSplat.state;
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
        view.fogColor = (vec3_t){0.4f, 0.5f, 0.6f};
    }
}

/* A model camera changes the projection as well as the portrait viewport/flags. */
bool R_ExtractEntityCamera(renderEntity_t const *ent, float aspect, viewDef_t *view) {
    (void)ent; (void)aspect;
    Matrix4_identity(&view->viewProjectionMatrix);
    return camera;
}

TEST(renderer_view, isolated_entity_camera_draws_its_particle_scene) {
    renderEntity_t entity = {0};
    viewDef_t saved = tr.viewDef;
    void *(*saved_alloc)(long) = ri.MemAlloc;
    void (*saved_free)(handle_t) = ri.MemFree;
    viewDef_t view = {
        .time = 1234, .deltaTime = 16,
        .rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL |
            RDF_USE_ENTITY_CAMERA | RDF_ISOLATED_PARTICLES,
        .entities = &entity, .num_entities = 1,
    };

    camera = true;
    particle_scene_begins = particle_scene_ends = particle_draws = particle_order = 0;
    ri.MemAlloc = test_particle_scene_alloc;
    ri.MemFree = test_particle_scene_free;
    R_RenderFrame(&view);

    T_EQ(particle_scene_begins, 1);
    T_EQ(particle_draws, 1);
    T_EQ(particle_scene_ends, 1);
    T_EQ(particle_order, 1423); /* isolate, emit entities, draw, restore */
    T_EQ(drawn.time, view.time);
    T_EQ(drawn.deltaTime, view.deltaTime);
    T_EQ(memcmp(&tr.viewDef, &saved, sizeof(saved)), 0);
    R_ReleaseEntityCameraEvents(entity.instance_id);
    R_ClearEntityCameraParticleScenes();
    ri.MemAlloc = saved_alloc;
    ri.MemFree = saved_free;
}

static handle_t test_particle_scene_alloc(long size) { return calloc(1, (size_t)size); }
static void test_particle_scene_free(handle_t ptr) { free(ptr); }

TEST(renderer_view, entity_camera_particle_scenes_follow_instance_lifecycle) {
    renderEntity_t entity = {0};
    viewDef_t view = {
        .time = 1234, .deltaTime = 16,
        .rdflags = RDF_NOWORLDMODEL | RDF_USE_ENTITY_CAMERA | RDF_ISOLATED_PARTICLES,
        .entities = &entity, .num_entities = 1,
    };
    void *(*saved_alloc)(long) = ri.MemAlloc;
    void (*saved_free)(handle_t) = ri.MemFree;
    particle_scene_begins = particle_scene_ends = particle_draws = particle_scene_clears = 0;
    memset(captured_particle_scenes, 0, sizeof(captured_particle_scenes));
    ri.MemAlloc = test_particle_scene_alloc;
    ri.MemFree = test_particle_scene_free;
    camera = true;

    entity.instance_id = 101;
    entity.model = (model_t const *)(uintptr_t)1;
    R_RenderFrame(&view);
    entity.instance_id = 202;
    R_RenderFrame(&view);
    entity.instance_id = 101;
    R_RenderFrame(&view);
    T_NE(captured_particle_scenes[0], captured_particle_scenes[1]);
    T_EQ(captured_particle_scenes[0], captured_particle_scenes[2]);

    int const clears_before_rebind = particle_scene_clears;
    entity.model = (model_t const *)(uintptr_t)2;
    R_RenderFrame(&view);
    T_EQ(captured_particle_scenes[3], captured_particle_scenes[0]);
    T_EQ(particle_scene_clears, clears_before_rebind + 1);
    entity.generation++;
    R_RenderFrame(&view);
    T_EQ(particle_scene_clears, clears_before_rebind + 2);

    R_ReleaseEntityCameraEvents(101);
    T_EQ(particle_scene_clears, clears_before_rebind + 3);
    R_RenderFrame(&view);
    T_NE(captured_particle_scenes[0], captured_particle_scenes[5]);
    R_ReleaseEntityCameraEvents(101);
    R_ReleaseEntityCameraEvents(202);
    R_ClearEntityCameraParticleScenes();

    view.rdflags &= ~RDF_ISOLATED_PARTICLES;
    R_RenderFrame(&view);
    T_EQ(particle_scene_clears, clears_before_rebind + 6); /* released instances plus transient scene */
    view.rdflags = RDF_NOWORLDMODEL | RDF_NOPARTICLES;
    entity.instance_id = 303;
    R_RenderFrame(&view);
    T_EQ(particle_scene_clears, clears_before_rebind + 7); /* suppressed no-world particles are discarded */
    ri.MemAlloc = saved_alloc;
    ri.MemFree = saved_free;
}

TEST(renderer_view, missing_particle_state_free_keeps_table_reachable) {
    renderEntity_t entity = { .instance_id = 404 };
    viewDef_t view = {
        .time = 1234, .deltaTime = 16,
        .rdflags = RDF_NOWORLDMODEL | RDF_USE_ENTITY_CAMERA | RDF_ISOLATED_PARTICLES,
        .entities = &entity, .num_entities = 1,
    };
    void *(*saved_alloc)(long) = ri.MemAlloc;
    void (*saved_free)(handle_t) = ri.MemFree;

    camera = true;
    ri.MemAlloc = test_particle_scene_alloc;
    ri.MemFree = test_particle_scene_free;
    R_RenderFrame(&view);
    entityCameraParticleState_t *const states = entity_camera_particle_states;
    T_NOT_NULL(states);

    ri.MemFree = NULL;
    R_ClearEntityCameraParticleScenes();
    T_EQ(entity_camera_particle_states, states);
    T_EQ(entity_camera_particle_count, 0);
    T_ASSERT(entity_camera_particle_capacity > 0);

    ri.MemFree = test_particle_scene_free;
    R_ClearEntityCameraParticleScenes();
    T_NULL(entity_camera_particle_states);
    ri.MemAlloc = saved_alloc;
    ri.MemFree = saved_free;
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
