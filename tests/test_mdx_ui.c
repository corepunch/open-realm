#include "test.h"
#include "renderer/r_local.h"
#include "games/warcraft-3/renderer/mdx/r_mdx.h"

struct render_globals tr;
refImport_t ri;
mat4_t node_matrices[MDX_MAX_NODES];
static viewDef_t drawn;
static uint32_t drawn_frame, drawn_skin_slot;
static texture_t const *drawn_skin;
static handle_t calloc_test(long size) { return calloc(1, size); }

/* Retain simulation and sprite view setup; replace only GPU/world submission. */
#undef R_Call
#define R_Call(func, ...) ((void)0)
#include "renderer/r_particles.c"
#include "games/warcraft-3/renderer/mdx/r_mdx_render.c"
#include "games/warcraft-3/renderer/r_weather.c"

void R_RenderView(void) { drawn = tr.viewDef; drawn_frame = tr.viewDef.entities[0].frame; drawn_skin = tr.viewDef.entities[0].skin; drawn_skin_slot = tr.viewDef.entities[0].skin_slot; R_UpdateParticles(); }

mdlx_state_t mdlx;
rect_t R_UISceneRect(void) { return (rect_t){0, 0, 0.8f, 0.6f}; }
texture_t *R_AllocateTexture(uint32_t w, uint32_t h) { (void)w; (void)h; return NULL; }
void R_LoadTextureMipLevel(texture_t *tex, texMip_t const *mip) { (void)tex; (void)mip; }
void R_LoadShaderState(shaderLoad_t const *load) { (void)load; }
static texture_t weather_test_texture;
texture_t *R_LoadTexture(cstring_t path) { (void)path; return &weather_test_texture; }
float R_GetHeightAtPoint(float x, float y) { (void)x; (void)y; return 0.0f; }
bool R_MapAssetCandidate(cstring_t asset, string_t candidate, uint32_t candidate_size) { (void)asset; (void)candidate; (void)candidate_size; return false; }
void R_DeleteShader(shaderProg_t *prog) { (void)prog; }
void R_UploadShader(shaderProg_t *prog, void const *state) { (void)prog; (void)state; }
modelProg_t *R_ModelShader(void) { return NULL; }
void R_ReleaseVertexArrayObject(buffer_t *buffer) { (void)buffer; }
void R_SetAlphaKeyState(bool enabled) { (void)enabled; }
void R_StatsDraw(GLenum mode, uint32_t count, uint32_t instances) { (void)mode; (void)count; (void)instances; }
void MDLX_GetModelKeytrackValue(mdxModel_t const *model, mdxKeyTrack_t const *track, uint32_t time, handle_t out) {
    (void)model; (void)track; (void)time; (void)out; T_ASSERT(false);
}

TEST(mdx_ui, particle_uv_curve_uses_start_mid_end_frames) {
    cparticle_t p = {
        .columns = 4, .rows = 2, .lifespan = 1.0f, .midtime = 128,
        .use_uv_curve = true, .uv_start = 1, .uv_mid = 5, .uv_end = 7,
    };
    color32_t uv;

    p.time = 0.0f;
    uv = FX_GetFrame(&p);
    T_EQ(uv.r, 64); T_EQ(uv.g, 127); T_EQ(uv.b, 127); T_EQ(uv.a, 0);

    p.time = BYTE2FLOAT(p.midtime);
    uv = FX_GetFrame(&p);
    T_EQ(uv.r, 64); T_EQ(uv.g, 255); T_EQ(uv.b, 127); T_EQ(uv.a, 128);

    p.time = 1.0f;
    uv = FX_GetFrame(&p);
    T_EQ(uv.r, 192); T_EQ(uv.g, 255); T_EQ(uv.b, 255); T_EQ(uv.a, 128);
}

TEST(mdx_ui, particle_uv_default_still_advances_over_lifetime) {
    cparticle_t p = { .columns = 4, .rows = 1, .lifespan = 1.0f, .time = 0.5f };
    color32_t uv = FX_GetFrame(&p);

    T_EQ(uv.r, 128); T_EQ(uv.g, 255); T_EQ(uv.b, 191); T_EQ(uv.a, 0);
}

TEST(mdx_ui, particle_billboard_top_samples_top_texture_row) {
    particleVertex_t vertices[NUM_PARTICLE_VERTICES];
    vec3_t point = {0};
    color32_t uv = { .r = 0, .g = 255, .b = 255, .a = 0 };
    R_AddParticle(vertices, &point, NULL, NULL, NULL, uv, (color32_t){0}, 1.0f);

    T_EQ(vertices[2].axis[1], 255);
    T_FEQ(vertices[2].uv[1], 0.0f, 0.0001f);
    T_EQ(vertices[0].axis[1], 0);
    T_FEQ(vertices[0].uv[1], 1.0f, 0.0001f);
}

static wc3WeatherEffect_t weather_test_state;
static texture_t weather_test_effect_texture;

static void weather_test_setup(w3WeatherArt_t const *art) {
    R_ClearParticles();
    R_WeatherInit();
    weather_rng = 0x7f4a7c15u;
    tr.viewDef = (viewDef_t){0};
    tr.viewDef.num_weather_effects = 1;
    tr.viewDef.weather_effects = &weather_test_state;
    weather_test_state = (wc3WeatherEffect_t){ .handle = 7, .effect_id = art->id, .enabled = 1,
        .bounds = { .min = {-100, -100}, .max = {100, 100} } };
    weather_effects[0] = (renderWeatherEffect_t){ .inuse = true, .enabled = true,
        .handle = weather_test_state.handle, .effect_id = weather_test_state.effect_id,
        .bounds = weather_test_state.bounds, .art = art, .texture = &weather_test_effect_texture };
}

static void weather_test_step(uint32_t delta_ms) {
    tr.viewDef.deltaTime = delta_ms;
    R_WeatherEmit();
}

static cparticle_t *weather_test_run(w3WeatherArt_t const *art, uint32_t delta_ms, uint32_t count) {
    weather_test_setup(art);
    weather_test_step(delta_ms);
    if (art->head || art->tail) T_EQ(R_CountParticlesForEmitter(weather_test_state.handle), count);
    else T_EQ(active_particles, NULL);
    return active_particles;
}

TEST(mdx_ui, wc3_weather_emits_authored_alpha_and_tail_primitive) {
    w3WeatherArt_t art = { .id = MAKEFOURCC('R','L','l','r'), .emissionRate = 1.0f,
        .lifespan = 1.0f, .particles = 10, .head = false, .tail = true,
        .alphaStart = 150, .rows = 1, .columns = 1, .tailLength = 2.0f,
        .tailUVStart = 3, .tailUVMid = 4, .tailUVEnd = 5, .velocity = 10.0f };
    cparticle_t *p = weather_test_run(&art, 1000, 1);
    T_EQ(p->blend_mode, BLEND_MODE_BLEND);
    T_EQ(p->color[0].a, 150);
    T_EQ(p->uv_start, 3); T_EQ(p->uv_mid, 4); T_EQ(p->uv_end, 5);
    T_ASSERT(p->tail.x != 0 || p->tail.y != 0 || p->tail.z != 0);
}

TEST(mdx_ui, wc3_weather_rejects_authored_row_without_head_or_tail) {
    w3WeatherArt_t art = { .id = MAKEFOURCC('T','E','S','T'), .emissionRate = 1.0f,
        .lifespan = 1.0f, .particles = 10, .head = false, .tail = false };
    (void)weather_test_run(&art, 1000, 0);
    T_NULL(active_particles);
}

TEST(mdx_ui, wc3_weather_head_and_tail_count_as_one_logical_particle) {
    w3WeatherArt_t art = { .id = MAKEFOURCC('T','E','S','T'), .emissionRate = 1.0f,
        .lifespan = 1.0f, .particles = 1, .head = true, .tail = true,
        .rows = 1, .columns = 1, .tailLength = 2.0f, .velocity = 10.0f };
    cparticle_t *p = weather_test_run(&art, 1000, 1);

    T_NOT_NULL(p);
    T_NOT_NULL(p->next);
    T_EQ(R_CountParticlesForEmitter(7), 1);
}

TEST(mdx_ui, wc3_weather_particle_cap_recovers_after_particle_expiry) {
    w3WeatherArt_t art = { .id = MAKEFOURCC('T','E','S','T'), .emissionRate = 1.0f,
        .lifespan = 0.05f, .particles = 1, .head = false, .tail = true,
        .rows = 1, .columns = 1, .tailLength = 2.0f, .velocity = 10.0f };
    cparticle_t *p = weather_test_run(&art, 1000, 1);

    T_NOT_NULL(p);
    tr.viewDef.deltaTime = 51;
    R_UpdateParticles();
    T_NULL(active_particles);
    tr.viewDef.deltaTime = 1000;
    R_WeatherEmit();
    T_EQ(R_CountParticlesForEmitter(7), 1);
}

TEST(mdx_ui, wc3_weather_uses_authored_per_second_rate_without_aggregate_multiplier) {
    w3WeatherArt_t art = { .id = MAKEFOURCC('T','E','S','T'), .emissionRate = 1.0f,
        .lifespan = 2.0f, .particles = 100, .head = false, .tail = true,
        .rows = 1, .columns = 1, .tailLength = 2.0f, .velocity = 10.0f };
    cparticle_t *p = weather_test_run(&art, 1000, 1);

    T_NOT_NULL(p);
    T_NULL(p->next);
}

TEST(mdx_ui, wc3_weather_variation_and_latitude_stay_within_authored_bounds) {
    w3WeatherArt_t art = { .id = MAKEFOURCC('T','E','S','T'), .emissionRate = 1.0f,
        .lifespan = 2.0f, .particles = 10, .head = false, .tail = true,
        .velocity = 10.0f, .variation = 4.0f, .latitude = 30.0f };
    cparticle_t *p = weather_test_run(&art, 1000, 1);
    float speed = sqrtf(p->vel.x*p->vel.x + p->vel.y*p->vel.y + p->vel.z*p->vel.z);
    float angle = acosf(p->vel.z / speed) / WEATHER_DEG2RAD;

    T_ASSERT(speed >= 8.0f && speed <= 12.0f);
    T_ASSERT(speed != art.velocity);
    T_ASSERT(angle > 0.0f && angle <= art.latitude);
}

TEST(mdx_ui, wc3_weather_head_and_tail_share_spawn_and_use_independent_uv_curves) {
    w3WeatherArt_t art = { .id = MAKEFOURCC('T','E','S','T'), .emissionRate = 1.0f,
        .lifespan = 2.0f, .particles = 10, .head = true, .tail = true,
        .rows = 4, .columns = 4, .velocity = -10.0f, .tailLength = 2.0f,
        .headUVStart = 1, .headUVMid = 2, .headUVEnd = 3,
        .tailUVStart = 4, .tailUVMid = 5, .tailUVEnd = 6 };
    cparticle_t *tail = weather_test_run(&art, 1000, 1);
    cparticle_t *head = tail->next;

    T_EQ(tail->uv_start, 4); T_EQ(tail->uv_mid, 5); T_EQ(tail->uv_end, 6);
    T_EQ(head->uv_start, 1); T_EQ(head->uv_mid, 2); T_EQ(head->uv_end, 3);
    T_ASSERT(tail->tail.z != 0.0f);
    T_FEQ(head->tail.z, 0.0f, 0.0001f);
    T_FEQ(tail->org.x, head->org.x, 0.0001f);
    T_FEQ(tail->org.y, head->org.y, 0.0001f);
    T_FEQ(tail->org.z, head->org.z, 0.0001f);
    T_FEQ(tail->vel.x, head->vel.x, 0.0001f);
    T_FEQ(tail->vel.y, head->vel.y, 0.0001f);
    T_FEQ(tail->vel.z, head->vel.z, 0.0001f);
    T_FEQ(tail->time, head->time, 0.0001f);
}

TEST(mdx_ui, wc3_weather_accumulates_emission_independently_of_frame_partition) {
    w3WeatherArt_t art = { .id = MAKEFOURCC('T','E','S','T'), .emissionRate = 2.0f,
        .lifespan = 2.0f, .particles = 10, .head = false, .tail = true };
    uint32_t split_count, single_count;

    weather_test_setup(&art);
    FOR_LOOP(i, 10) weather_test_step(100);
    split_count = R_CountParticlesForEmitter(7);

    weather_test_setup(&art);
    weather_test_step(1000);
    single_count = R_CountParticlesForEmitter(7);
    T_EQ(split_count, 2);
    T_EQ(single_count, split_count);
}

TEST(mdx_ui, wc3_weather_does_not_emit_half_dual_primitive_on_pool_exhaustion) {
    w3WeatherArt_t art = { .id = MAKEFOURCC('T','E','S','T'), .emissionRate = 1.0f,
        .lifespan = 1.0f, .particles = 1, .head = true, .tail = true,
        .rows = 1, .columns = 1, .tailLength = 2.0f, .velocity = 10.0f };
    cparticle_t *p;

    static wc3WeatherEffect_t state;
    static texture_t texture;
    R_ClearParticles();
    R_WeatherInit();
    weather_rng = 0x7f4a7c15u;
    tr.viewDef = (viewDef_t){0};
    tr.viewDef.deltaTime = 1000;
    tr.viewDef.num_weather_effects = 1;
    tr.viewDef.weather_effects = &state;
    state = (wc3WeatherEffect_t){ .handle = 7, .effect_id = art.id, .enabled = 1,
        .bounds = { .min = {-100, -100}, .max = {100, 100} } };
    weather_effects[0] = (renderWeatherEffect_t){ .inuse = true, .enabled = true,
        .handle = state.handle, .effect_id = state.effect_id, .bounds = state.bounds,
        .art = &art, .texture = &texture };
    cl_numparticles = 2;
    R_ClearParticles();
    T_NOT_NULL(R_SpawnParticle());
    tr.viewDef.deltaTime = 1000;
    R_WeatherEmit();
    p = active_particles;
    T_EQ(R_CountParticlesForEmitter(7), 0);
    T_NOT_NULL(p);
    T_NULL(p->next);
    cl_numparticles = MAX_PARTICLES;
}

TEST(mdx_ui, sprite_clock_and_particle_scenes_are_isolated) {
    mdxSequence_t seq = { .name = "Stand", .interval = {833, 2500} };
    mdxParticleEmitter_t emitter = {0};
    mdxModel_t mdx = { .sequences = &seq, .num_sequences = 1, .emitters = &emitter };
    model_t model = { .mdx = &mdx };
    drawSprite_t sprite = { .model = &model, .anim = "#0", .id = &model };
    cparticle_t *world, *ui;
    particleScene_t scene = {0};
    ri.MemAlloc = calloc_test; ri.MemFree = free;
    R_ClearParticles();
    world = R_SpawnParticle(); world->lifespan = 2;
    tr.viewDef.time = 1000; tr.viewDef.deltaTime = 20;
    MDLX_DrawSpriteInstance(&sprite, COLOR32_WHITE);
    T_EQ(drawn.time, 1000); T_EQ(drawn.deltaTime, 20);
    T_EQ(active_particles, world); T_FEQ(world->time, 0, 0.0001f);
    mdxSprite_t *first = mdx.sprites;
    first->emitters->accumulator = 0.75f;
    sprite.id = &mdx;
    MDLX_DrawSpriteInstance(&sprite, COLOR32_WHITE);
    T_NE(mdx.sprites, first); T_NE(mdx.sprites->emitters, first->emitters);
    T_FEQ(first->emitters->accumulator, 0.75f, 0.0001f);
    T_FEQ(mdx.sprites->emitters->accumulator, 0, 0.0001f);
    cparticle_t *old = R_BeginParticleScene(&scene);
    T_NULL(active_particles);
    ui = R_SpawnParticle(); ui->lifespan = 1;
    R_UpdateParticles();
    R_EndParticleScene(&scene, old);
    T_EQ(active_particles, world); T_EQ(scene.active, ui);
    T_FEQ(ui->time, 0.02f, 0.0001f); T_FEQ(world->time, 0, 0.0001f);
    R_ClearParticles();
    world = R_SpawnParticle(); world->lifespan = 2;
    R_ClearParticleScene(&scene);
    T_EQ(active_particles, world);
    MDLX_ReleaseSprites(&mdx);
}

/* Cursor direction changes start a new authored loop without changing scene time. */
TEST(mdx_ui, sprite_animation_epoch) {
    mdxSequence_t seq = { .name = "Scroll Right", .interval = {4833, 5033} };
    mdxModel_t mdx = { .sequences = &seq, .num_sequences = 1 };
    model_t model = { .mdx = &mdx };
    drawSprite_t sprite = { .model = &model, .anim = "#0", .start_time = 1234 };
    tr.viewDef.time = 1234;
    MDLX_DrawSpriteInstance(&sprite, COLOR32_WHITE);
    T_EQ(drawn_frame, 4833);
    tr.viewDef.time = 1484;
    MDLX_DrawSpriteInstance(&sprite, COLOR32_WHITE);
    T_EQ(drawn_frame, 4883);
    T_EQ(tr.viewDef.time, 1484);
}

/* Maiev's authored portrait has numbered idle/talk variants, never exact names. */
TEST(mdx_ui, cinematic_portrait_talk_and_idle) {
    mdxSequence_t sequences[] = {
        { .name = "Portrait - 1", .interval = {1667, 3167} },
        { .name = "Portrait Talk - 1", .interval = {5000, 8000} }
    };
    mdxCamera_t camera = {0};
    mdxModel_t mdx = { .sequences = sequences, .num_sequences = 2, .cameras = &camera };
    model_t model = { .mdx = &mdx };
    renderEntity_t entity = {0};
    tr.viewDef.time = 1100;
    T_ASSERT(MDLX_SetEntityAnimationFrame(&model, "Portrait Talk", &entity));
    T_EQ(entity.frame, 6100);
    tr.viewDef.time = 1250;
    MDLX_SetEntityAnimationFrame(&model, "Portrait Talk", &entity);
    T_EQ(entity.frame, 6250);
    MDLX_SetEntityAnimationFrame(&model, "Portrait", &entity);
    T_EQ(entity.frame, 2917);
    /* Sequence order must not make the idle portrait speak. */
    mdxSequence_t tmp = sequences[0]; sequences[0] = sequences[1]; sequences[1] = tmp;
    MDLX_SetEntityAnimationFrame(&model, "Portrait", &entity);
    T_EQ(entity.frame, 2917);
}

TEST(mdx_ui, huntress_lowercase_portrait_and_idle_only_model) {
    mdxSequence_t sequences[] = {
        { .name = "portrait", .interval = {25400, 26667} },
        { .name = "portrait talk", .interval = {28000, 30667} }
    };
    mdxCamera_t camera = {0};
    mdxModel_t mdx = { .sequences = sequences, .num_sequences = 2, .cameras = &camera };
    model_t model = { .mdx = &mdx };
    renderEntity_t entity = {0};
    tr.viewDef.time = 700;
    MDLX_SetEntityAnimationFrame(&model, "Portrait Talk", &entity);
    T_EQ(entity.frame, 28700);
    MDLX_SetEntityAnimationFrame(&model, "Portrait", &entity);
    T_EQ(entity.frame, 26100);
    mdx.num_sequences = 1;
    MDLX_SetEntityAnimationFrame(&model, "Portrait Talk", &entity);
    T_EQ(entity.frame, 26100);
}

TEST(mdx_ui, nonlooping_sprite_holds_authored_endpoint) {
    mdxSequence_t sequence = { .name = "Normal", .interval = {600, 717}, .flags = 1 };
    mdxModel_t mdx = { .sequences = &sequence, .num_sequences = 1 };
    model_t model = { .mdx = &mdx };
    drawSprite_t sprite = { .model = &model, .anim = "Normal", .start_time = 100 };
    tr.viewDef.time = 200;
    MDLX_DrawSpriteInstance(&sprite, COLOR32_WHITE);
    T_EQ(drawn_frame, 700);
    tr.viewDef.time = 217;
    MDLX_DrawSpriteInstance(&sprite, COLOR32_WHITE);
    T_EQ(drawn_frame, 717);
    tr.viewDef.time = 999;
    MDLX_DrawSpriteInstance(&sprite, COLOR32_WHITE);
    T_EQ(drawn_frame, 717);
}

TEST(mdx_ui, held_item_skin_reaches_model_draw_and_clears) {
    mdxSequence_t sequence = { .name = "HoldItem", .interval = {4233, 4400} };
    mdxModel_t mdx = { .sequences = &sequence, .num_sequences = 1 };
    model_t model = { .mdx = &mdx };
    texture_t icon = {0};
    drawSprite_t sprite = { .model = &model, .anim = "HoldItem", .skin = &icon, .skin_slot = 21 };
    MDLX_DrawSpriteInstance(&sprite, COLOR32_WHITE);
    T_ASSERT(drawn_skin == &icon); T_EQ(drawn_skin_slot, 21);
    sprite.skin = NULL;
    MDLX_DrawSpriteInstance(&sprite, COLOR32_WHITE);
    T_NULL(drawn_skin);
}
