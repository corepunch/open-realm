#include "test.h"
#include "renderer/r_local.h"
#include "renderer/r_shader.h"
#include "games/warcraft-3/renderer/mdx/r_mdx.h"

struct render_globals tr;
refImport_t ri;
mdlx_state_t mdlx;
mat4_t node_matrices[MDX_MAX_NODES];
static uint32_t drawn[16], draw_count;

/* Keep material evaluation and submission intact; capture only the GPU boundary. */
#undef R_Call
#define R_Call(func, ...) ((void)0)
#include "games/warcraft-3/renderer/mdx/r_mdx_geoset.c"

texture_t const *R_FindTextureByID(uint32_t id) { (void)id; return NULL; }
void R_BindTexture(texture_t const *tex, uint32_t unit) { (void)tex; (void)unit; }
void R_SetAlphaKeyState(bool enabled) { (void)enabled; }
texture_t const *R_W3CliffReplacementAt(float x, float y) { (void)x; (void)y; return NULL; }
void R_UploadShader(shaderProg_t *prog, void const *state) { (void)prog; (void)state; }
void R_StatsDraw(GLenum mode, uint32_t count, uint32_t instances) {
    (void)mode; (void)instances; T_ASSERT(draw_count < sizeof(drawn) / sizeof(*drawn)); drawn[draw_count++] = count;
}
void MDLX_GetModelKeytrackValue(mdxModel_t const *model, mdxKeyTrack_t const *track, uint32_t time, handle_t out) {
    (void)model; (void)track; (void)time; (void)out; T_ASSERT(false);
}
void MDLX_GetAnimatedColorTrackValue(mdxModel_t const *model, mdxKeyTrack_t const *track, uint32_t time, vec3_t *out) {
    (void)model; (void)track; (void)time; (void)out; T_ASSERT(false);
}
void MDLX_GetGeosetAnimationStaticColor(mdxGeosetAnim_t const *anim, vec3_t *out) { *out = anim->staticColor; }

TEST(mdx_geoset, portal_base_blends_before_additive_texture_at_same_priority) {
    /* ShimmeringPortal has filters 4, 3, 2 in this file order; the final filter-2
     * white disk must be drawn first or it hides the textured planes and cylinder. */
    BLEND_MODE modes[] = { BLEND_MODE_ADDALPHA, BLEND_MODE_ADD, BLEND_MODE_BLEND };
    mdxMaterialLayer_t layers[3]; mdxMaterial_t mats[3]; mdxGeoset_t geos[3];
    mdxTexture_t texture = {0}; modelProg_t shader = {0};
    mdxModel_t model = { .materials = mats, .geosets = geos, .textures = &texture, .num_textures = 1 };
    renderEntity_t entity = {0}; uint32_t vao = 1;
    FOR_LOOP(i, 3) {
        layers[i] = (mdxMaterialLayer_t){ .blendMode = modes[i], .staticAlpha = 1, .transformId = UINT32_MAX };
        mats[i] = (mdxMaterial_t){ .layers = &layers[i], .num_layers = 1, .next = i < 2 ? &mats[i+1] : NULL };
        geos[i] = (mdxGeoset_t){ .materialID = i, .num_triangles = i+1, .num_texcoordChannels = 1,
            .vaos = &vao, .vaos_count = 1, .next = i < 2 ? &geos[i+1] : NULL };
    }
    tr.render_phase = RENDER_PHASE_ALPHA; mdlx.shader = &shader; draw_count = 0;
    MDLX_RenderGeosets(&entity, &model);
    T_EQ(draw_count, 3); T_EQ(drawn[0], 3); T_EQ(drawn[1], 2); T_EQ(drawn[2], 1);

    /* Authored priority still dominates filter order. Equal filters keep file order. */
    mats[0].priority = -1; layers[1].blendMode = BLEND_MODE_BLEND; draw_count = 0;
    MDLX_RenderGeosets(&entity, &model);
    T_EQ(draw_count, 3); T_EQ(drawn[0], 1); T_EQ(drawn[1], 2); T_EQ(drawn[2], 3);
    /* Sort individual layers of mixed materials, rather than using one material filter. */
    mdxMaterialLayer_t mixed[2] = { layers[0], layers[2] };
    mats[0].priority = 0; mats[0].layers = mixed; mats[0].num_layers = 2;
    layers[1].blendMode = BLEND_MODE_ADD; draw_count = 0;
    MDLX_RenderGeosets(&entity, &model);
    T_EQ(draw_count, 4); T_EQ(drawn[0], 1); T_EQ(drawn[1], 3); T_EQ(drawn[2], 2); T_EQ(drawn[3], 1);

    /* Opaque layers retain their solid-pass ownership when a material also has an overlay. */
    mixed[1].blendMode = BLEND_MODE_NONE; tr.render_phase = RENDER_PHASE_SOLID; draw_count = 0;
    MDLX_RenderGeosets(&entity, &model);
    T_EQ(draw_count, 1); T_EQ(drawn[0], 1);
    tr.render_phase = RENDER_PHASE_ALPHA; draw_count = 0;
    MDLX_RenderGeosets(&entity, &model);
    T_EQ(draw_count, 3); T_EQ(drawn[0], 3); T_EQ(drawn[1], 2); T_EQ(drawn[2], 1);
    mdlx.shader = NULL;
}
