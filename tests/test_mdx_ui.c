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

void R_RenderView(void) { drawn = tr.viewDef; drawn_frame = tr.viewDef.entities[0].frame; drawn_skin = tr.viewDef.entities[0].skin; drawn_skin_slot = tr.viewDef.entities[0].skin_slot; R_UpdateParticles(); }

mdlx_state_t mdlx;
rect_t R_UISceneRect(void) { return (rect_t){0, 0, 0.8f, 0.6f}; }
texture_t *R_AllocateTexture(uint32_t w, uint32_t h) { (void)w; (void)h; return NULL; }
void R_LoadTextureMipLevel(texture_t *tex, texMip_t const *mip) { (void)tex; (void)mip; }
void R_LoadShaderState(shaderLoad_t const *load) { (void)load; }
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
    T_EQ(uv.r, 64); T_EQ(uv.g, 0); T_EQ(uv.b, 127); T_EQ(uv.a, 127);

    p.time = BYTE2FLOAT(p.midtime);
    uv = FX_GetFrame(&p);
    T_EQ(uv.r, 64); T_EQ(uv.g, 128); T_EQ(uv.b, 127); T_EQ(uv.a, 255);

    p.time = 1.0f;
    uv = FX_GetFrame(&p);
    T_EQ(uv.r, 192); T_EQ(uv.g, 128); T_EQ(uv.b, 255); T_EQ(uv.a, 255);
}

TEST(mdx_ui, particle_uv_default_still_advances_over_lifetime) {
    cparticle_t p = { .columns = 4, .rows = 1, .lifespan = 1.0f, .time = 0.5f };
    color32_t uv = FX_GetFrame(&p);

    T_EQ(uv.r, 128); T_EQ(uv.g, 0); T_EQ(uv.b, 191); T_EQ(uv.a, 255);
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
