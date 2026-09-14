#include "test.h"
#include "renderer/r_local.h"
#include "games/warcraft-3/renderer/mdx/r_mdx.h"

struct render_globals tr;
refImport_t ri;
MATRIX4 node_matrices[MDX_MAX_NODES];
static viewDef_t drawn;
static HANDLE calloc_test(long size) { return calloc(1, size); }

/* Retain simulation and sprite view setup; replace only GPU/world submission. */
#undef R_Call
#define R_Call(func, ...) ((void)0)
#include "renderer/r_particles.c"
#include "games/warcraft-3/renderer/mdx/r_mdx_render.c"

void R_RenderView(void) { drawn = tr.viewDef; R_UpdateParticles(); }

mdlx_state_t mdlx;
RECT R_UISceneRect(void) { return (RECT){0, 0, 0.8f, 0.6f}; }
LPTEXTURE R_AllocateTexture(DWORD w, DWORD h) { (void)w; (void)h; return NULL; }
void R_LoadTextureMipLevel(LPCTEXTURE tex, LPCTEXMIP mip) { (void)tex; (void)mip; }
void R_LoadShaderState(LPCSHADERLOAD load) { (void)load; }
void R_DeleteShader(LPSHADERPROG prog) { (void)prog; }
void R_UploadShader(LPSHADERPROG prog, LPCVOID state) { (void)prog; (void)state; }
MODELPROG *R_ModelShader(void) { return NULL; }
void R_ReleaseVertexArrayObject(LPBUFFER buffer) { (void)buffer; }
void R_SetAlphaKeyState(BOOL enabled) { (void)enabled; }
void R_StatsDraw(GLenum mode, DWORD count, DWORD instances) { (void)mode; (void)count; (void)instances; }
mdxSequence_t const *MDLX_FindSequenceByName(mdxModel_t const *model, LPCSTR name) {
    (void)model; (void)name; T_ASSERT(false); return NULL;
}
void MDLX_GetModelKeytrackValue(mdxModel_t const *model, mdxKeyTrack_t const *track, DWORD time, HANDLE out) {
    (void)model; (void)track; (void)time; (void)out; T_ASSERT(false);
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
