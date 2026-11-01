#include "test.h"
#include "renderer/r_local.h"
#include "games/warcraft-3/renderer/mdx/r_mdx.h"

struct render_globals tr;
refImport_t ri;
mat4_t node_matrices[MDX_MAX_NODES];
static viewDef_t drawn;
static uint32_t drawn_frame, drawn_skin_slot;
static texture_t const *drawn_skin;
static particlePerf_t particle_perf;
void R_ProfileParticles(particlePerf_t const *stats) { particle_perf = *stats; }
static struct {
    uint32_t uploads, draws, verts;
    GLsizeiptr bytes;
    uint8_t *buffer, *drawn;
    GLuint texture, textures[16];
    GLenum src, dst, sources[16], dests[16];
    GLboolean depth, depths[16];
    uint32_t first[16], count[16];
    GLenum unit;
    GLuint bound[8], texsets[16][4];
    GLint uvsize;
    GLsizei stride;
    uintptr_t uvoffset;
    uint32_t texbinds, cullgets, cullon, culloff;
    bool culling;
} pcap;
static buffer_t particle_buffer;
static texture_t particle_textures[2] = {{ .texid = 41 }, { .texid = 42 }};
static void test_particle_upload(GLenum target, GLsizeiptr bytes, void const *data, GLenum usage);
static void test_particle_draw(GLenum mode, GLint first, GLsizei count);
static void test_particle_texture(GLenum target, GLuint texture) {
    (void)target; pcap.texture = texture;
    pcap.texbinds++;
    if (pcap.unit >= GL_TEXTURE0 && pcap.unit < GL_TEXTURE0 + 8) pcap.bound[pcap.unit - GL_TEXTURE0] = texture;
}
static void test_particle_active(GLenum unit) { pcap.unit = unit; }
static void test_particle_attrib(GLuint index, GLint size, GLenum type, GLboolean norm, GLsizei stride, void const *data) {
    (void)type; (void)norm;
    if (index == attrib_texcoord) { pcap.uvsize = size; pcap.stride = stride; pcap.uvoffset = (uintptr_t)data; }
}
static void test_particle_blend(GLenum src, GLenum dst) { pcap.src = src; pcap.dst = dst; }
static void test_particle_depth(GLboolean depth) { pcap.depth = depth; }
static GLboolean test_particle_enabled(GLenum cap) {
    if (cap != GL_CULL_FACE) return GL_FALSE;
    pcap.cullgets++; return pcap.culling;
}
static void test_particle_enable(GLenum cap) {
    if (cap == GL_CULL_FACE) { pcap.cullon++; pcap.culling = true; }
}
static void test_particle_disable(GLenum cap) {
    if (cap == GL_CULL_FACE) { pcap.culloff++; pcap.culling = false; }
}
static handle_t calloc_test(long size) { return calloc(1, size); }

/* Capture the actual particle upload/draw boundary; unrelated GPU setup stays headless. */
#undef R_Call
#define R_Call(func, ...) do { \
    if (!strcmp(#func, "glBufferData") || !strcmp(#func, "glDrawArrays") || \
        !strcmp(#func, "glBindTexture") || !strcmp(#func, "glBlendFunc") || !strcmp(#func, "glDepthMask") || \
        !strcmp(#func, "glActiveTexture") || !strcmp(#func, "glVertexAttribPointer") || \
        !strcmp(#func, "glEnable") || !strcmp(#func, "glDisable")) \
        func(__VA_ARGS__); \
} while (0)
#define glBufferData test_particle_upload
#define glDrawArrays test_particle_draw
#define glBindTexture test_particle_texture
#define glBlendFunc test_particle_blend
#define glDepthMask test_particle_depth
#define glIsEnabled test_particle_enabled
#define glActiveTexture test_particle_active
#define glVertexAttribPointer test_particle_attrib
#define glEnable test_particle_enable
#define glDisable test_particle_disable
#include "renderer/r_particles.c"
#undef glBufferData
#undef glDrawArrays
#undef glBindTexture
#undef glBlendFunc
#undef glDepthMask
#undef glIsEnabled
#undef glActiveTexture
#undef glVertexAttribPointer
#undef glEnable
#undef glDisable
#undef R_Call
#define R_Call(func, ...) ((void)0)
/* Retain simulation and sprite view setup; replace only GPU/world submission. */
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
uint32_t R_GetFogOfWarTexture(void) { return 13; }
void R_StatsDraw(GLenum mode, uint32_t count, uint32_t instances) { (void)mode; (void)count; (void)instances; }
void MDLX_GetModelKeytrackValue(mdxModel_t const *model, mdxKeyTrack_t const *track, uint32_t time, handle_t out) {
    (void)model; (void)track; (void)time; (void)out; T_ASSERT(false);
}

/* Emulate VBO replacement and range draws so stale offsets or overwritten batches fail without a GL context. */
static void test_particle_upload(GLenum target, GLsizeiptr bytes, void const *data, GLenum usage) {
    T_EQ(target, GL_ARRAY_BUFFER); T_EQ(usage, GL_DYNAMIC_DRAW);
    T_ASSERT(bytes >= 0 && bytes <= sizeof(particles_resources.vertices));
    pcap.uploads++; pcap.bytes = bytes;
    pcap.buffer = realloc(pcap.buffer, MAX(bytes, 1));
    memcpy(pcap.buffer, data, bytes);
}

static void test_particle_draw(GLenum mode, GLint first, GLsizei count) {
    T_EQ(mode, GL_TRIANGLES); T_ASSERT(first >= 0 && count > 0);
    T_ASSERT(!pcap.culling);
    T_ASSERT((first + count) * sizeof(particleVertex_t) <= pcap.bytes);
    T_ASSERT(pcap.draws < 16);
    if (pcap.draws >= 16 || first < 0 || count <= 0) return;
    pcap.first[pcap.draws] = first; pcap.count[pcap.draws] = count;
    pcap.textures[pcap.draws] = pcap.texture;
    FOR_LOOP(i, 4) pcap.texsets[pcap.draws][i] = pcap.bound[i ? i + 2 : 0];
    pcap.sources[pcap.draws] = pcap.src; pcap.dests[pcap.draws] = pcap.dst;
    pcap.depths[pcap.draws++] = pcap.depth;
    pcap.drawn = realloc(pcap.drawn, (pcap.verts + count) * sizeof(particleVertex_t));
    memcpy(pcap.drawn + pcap.verts * sizeof(particleVertex_t), pcap.buffer + first * sizeof(particleVertex_t), count * sizeof(particleVertex_t));
    pcap.verts += count;
}

static void test_particles_reset(void) {
    free(pcap.buffer); free(pcap.drawn); memset(&pcap, 0, sizeof(pcap));
    particles_resources.particles = &particle_buffer;
    particles_resources.texture = particle_textures;
    ri.CvarString = NULL; tr.viewDef = (viewDef_t){0};
    R_ClearParticles();
}

TEST(mdx_ui, particles_upload_once_preserving_material_runs_and_vertices) {
    BLEND_MODE const modes[] = {BLEND_MODE_BLEND, BLEND_MODE_BLEND, BLEND_MODE_ADD, BLEND_MODE_ADD,
        BLEND_MODE_ALPHAKEY, BLEND_MODE_MODULATE, BLEND_MODE_ADDALPHA, BLEND_MODE_NONE};
    uint32_t const counts[] = {12, 12, 6, 6, 6, 6};
    GLenum const sources[] = {GL_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE, GL_ONE};
    GLenum const dests[] = {GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO, GL_SRC_COLOR, GL_ONE, GL_ZERO};
    test_particles_reset();
    for (int i = 7; i >= 0; --i) {
        cparticle_t *p = R_SpawnParticle();
        *p = (cparticle_t){ .next = p->next, .texture = particle_textures + (i == 2 || i == 3),
            .blend_mode = modes[i], .org = {i, 2*i, 3*i}, .vel = {4, 0, 0}, .accel = {0, 8, 0},
            .tail = {0, 0, -2}, .quad_right = {1, 0, 0}, .quad_up = {0, 1, 0},
            .color = {{20*i, 60, 90, 200}, {20*i, 60, 90, 200}, {20*i, 60, 90, 200}},
            .size = {4, 4, 4}, .size_value_scale = 2, .size_time_scale = 1,
            .lifespan = 1, .time = 0.25f, .midtime = 128, .columns = 2, .rows = 2 };
    }
    R_DrawParticles();
    T_EQ(pcap.uploads, 1); T_EQ(pcap.draws, 6); T_EQ(pcap.verts, 48);
    uint32_t first = 0;
    FOR_LOOP(i, 6) {
        T_EQ(pcap.first[i], first); T_EQ(pcap.count[i], counts[i]); first += counts[i];
        T_EQ(pcap.textures[i], particle_textures[i == 1].texid);
        T_EQ(pcap.sources[i], sources[i]); T_EQ(pcap.dests[i], dests[i]);
        T_EQ(pcap.depths[i], i == 2 || i == 5);
    }
    particleVertex_t const *vertices = (particleVertex_t const *)pcap.drawn;
    FOR_LOOP(i, 48) {
        particleVertex_t const *v = vertices + i;
        uint32_t n = i / 6;
        uint8_t const axes[6][2] = {{0,0}, {255,0}, {255,255}, {255,255}, {0,255}, {0,0}};
        T_FEQ(v->position.x, n + 1, 0.0001f); T_FEQ(v->position.y, 2*n + 0.25f, 0.0001f);
        T_FEQ(v->position.z, 3*n, 0.0001f); T_FEQ(v->size, 8, 0.0001f);
        T_EQ(v->color.r, 20*n); T_EQ(v->color.g, 60); T_EQ(v->color.b, 90); T_EQ(v->color.a, 200);
        T_FEQ(v->tail.z, -2, 0.0001f); T_FEQ(v->quadRight.x, 1, 0.0001f); T_FEQ(v->quadUp.y, 1, 0.0001f);
        T_EQ(v->axis[0], axes[i % 6][0]); T_EQ(v->axis[1], axes[i % 6][1]);
        T_FEQ(v->uv[0], axes[i % 6][0] ? 1 : 128 / 255.0f, 0.0001f);
        T_FEQ(v->uv[1], axes[i % 6][1] ? 0 : 127 / 255.0f, 0.0001f);
    }
    test_particles_reset();
}

TEST(mdx_ui, particles_expire_before_selecting_first_material_and_skip_empty_uploads) {
    test_particles_reset();
    cparticle_t *live = R_SpawnParticle();
    *live = (cparticle_t){ .next = live->next, .texture = particle_textures, .lifespan = 1, .midtime = 128 };
    cparticle_t *dead = R_SpawnParticle();
    *dead = (cparticle_t){ .next = dead->next, .texture = particle_textures + 1, .lifespan = 0.01f };
    tr.viewDef.deltaTime = 100;
    R_DrawParticles();
    T_EQ(active_particles, live); T_EQ(pcap.uploads, 1); T_EQ(pcap.draws, 1);
    T_EQ(pcap.textures[0], particle_textures[0].texid); T_EQ(pcap.count[0], 6);
    test_particles_reset();
    dead = R_SpawnParticle(); dead->lifespan = 0.01f; dead->time = 0;
    tr.viewDef.deltaTime = 100;
    R_DrawParticles();
    T_NULL(active_particles); T_EQ(pcap.uploads, 0); T_EQ(pcap.draws, 0);
    test_particles_reset();
}

TEST(mdx_ui, full_particle_pool_uses_one_upload_and_standalone_draws_replace_it) {
    test_particles_reset();
    FOR_LOOP(i, MAX_PARTICLES) {
        cparticle_t *p = R_SpawnParticle();
        *p = (cparticle_t){ .next = p->next, .texture = particle_textures + (i >= MAX_PARTICLES / 2),
            .lifespan = 1, .midtime = 128, .size = {1, 1, 1}, .blend_mode = BLEND_MODE_BLEND };
    }
    R_DrawParticles();
    T_EQ(pcap.uploads, 1); T_EQ(pcap.draws, 1); T_EQ(pcap.verts, MAX_PARTICLES * 6);
    T_EQ(pcap.bytes, sizeof(particles_resources.vertices)); T_EQ(pcap.count[0], MAX_PARTICLES * 6);
    T_EQ(pcap.texsets[0][0], particle_textures[1].texid); T_EQ(pcap.texsets[0][1], particle_textures[0].texid);
    vec3_t const points[] = {{1, 2, 3}, {2, 2, 3}, {2, 4, 3}};
    R_DrawBillboardSprite(particle_textures, points, 7, COLOR32_WHITE);
    T_EQ(pcap.uploads, 2); T_EQ(pcap.first[1], 0); T_EQ(pcap.count[1], 6);
    ribbonDraw_t const ribbon = { .texture = particle_textures + 1, .points = points, .point_count = 3,
        .width = 2, .texcoord_scale = 0.5f, .color = COLOR32_WHITE, .blend_mode = BLEND_MODE_ADD };
    R_DrawRibbon(&ribbon);
    T_EQ(pcap.uploads, 3); T_EQ(pcap.first[2], 0); T_EQ(pcap.count[2], 12);
    T_EQ(pcap.textures[2], particle_textures[1].texid);
    particleVertex_t const *v = (particleVertex_t const *)pcap.drawn + MAX_PARTICLES * 6;
    T_FEQ(v->position.x, 1, 0.0001f); T_FEQ(v->size, 7, 0.0001f);
    T_FEQ(v[6].tail.x, 1, 0.0001f); T_FEQ(v[12].tail.y, 2, 0.0001f);
    test_particles_reset();
}

/* Alternating textures must retain vertex order while sharing a draw within a blend mode and sampler budget. */
TEST(mdx_ui, particles_batch_interleaved_textures_without_reordering) {
    texture_t const textures[] = {{ .texid = 70 }, { .texid = 71 }, { .texid = 72 }, { .texid = 73 }, { .texid = 74 }};
    uint32_t const order[] = {0, 1, 0, 2, 3, 0, 4, 0, 1, 0, 0};
    uint32_t const slots[] = {0, 1, 0, 2, 3, 0, 0, 1, 2, 0, 0};
    uint32_t const counts[] = {36, 18, 6, 6};
    uint32_t const texsets[4][4] = {{70, 71, 72, 73}, {74, 70, 71, 74}, {70, 70, 70, 70}, {70, 70, 70, 70}};
    test_particles_reset();
    memset(&particle_perf, 0, sizeof(particle_perf));
    pcap.bound[1] = 999;
    for (int i = 10; i >= 0; --i) {
        cparticle_t *p = R_SpawnParticle();
        *p = (cparticle_t){ .next = p->next, .texture = textures + order[i], .org = {i, 0, 0},
            .lifespan = 1, .midtime = 128, .size = {1, 1, 1},
            .blend_mode = i == 9 ? BLEND_MODE_ADD : BLEND_MODE_BLEND };
    }
    R_DrawParticles();
    T_EQ(particle_perf.alive, 11); T_EQ(particle_perf.batches, 4);
    T_EQ(particle_perf.texfull, 1); T_EQ(particle_perf.blends, 2);
    T_EQ(particle_perf.texsum, 9); T_EQ(particle_perf.bytes, 66 * sizeof(particleVertex_t));
    T_EQ(pcap.uploads, 1); T_EQ(pcap.draws, 4); T_EQ(pcap.verts, 66);
    uint32_t first = 0;
    FOR_LOOP(i, 4) {
        T_EQ(pcap.first[i], first); T_EQ(pcap.count[i], counts[i]); first += counts[i];
        FOR_LOOP(j, 4) T_EQ(pcap.texsets[i][j], texsets[i][j]);
        T_EQ(pcap.sources[i], GL_SRC_ALPHA); T_EQ(pcap.dests[i], i == 2 ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
    }
    T_EQ(pcap.bound[2], 13); T_EQ(pcap.bound[1], 999);
    particleVertex_t const *vertices = (particleVertex_t const *)pcap.drawn;
    T_EQ(sizeof(vertices[0].uv) / sizeof(float), 3);
    FOR_LOOP(i, 66) {
        T_FEQ(vertices[i].position.x, i / 6, 0.0001f);
        if (sizeof(vertices[0].uv) / sizeof(float) == 3) T_FEQ(vertices[i].uv[2], slots[i / 6], 0.0001f);
    }
    test_particles_reset();
}

TEST(mdx_ui, particle_default_texture_shares_slot_and_standalone_draw_resets_selector) {
    test_particles_reset();
    FOR_LOOP(i, 2) {
        cparticle_t *p = R_SpawnParticle();
        *p = (cparticle_t){ .next = p->next, .texture = i ? NULL : particles_resources.texture,
            .lifespan = 1, .midtime = 128, .blend_mode = BLEND_MODE_BLEND };
    }
    R_DrawParticles();
    T_EQ(pcap.draws, 1); T_EQ(pcap.count[0], 12);
    R_DrawBillboardSprite(particle_textures + 1, &(vec3_t){0}, 3, COLOR32_WHITE);
    T_EQ(pcap.uploads, 2); T_EQ(pcap.draws, 2); T_EQ(pcap.first[1], 0);
    FOR_LOOP(i, 4) T_EQ(pcap.texsets[1][i], particle_textures[1].texid);
    particleVertex_t const *vertices = (particleVertex_t const *)pcap.drawn;
    if (sizeof(vertices[0].uv) / sizeof(float) == 3)
        FOR_LOOP(i, 18) T_FEQ(vertices[i].uv[2], 0, 0.0001f);
    test_particles_reset();
}

/* Other model draws may change bindings between passes; only one uninterrupted particle pass owns the cache. */
TEST(mdx_ui, particle_pass_reuses_bindings_and_restores_culling_before_immediate_sprites) {
    test_particles_reset();
    pcap.culling = true; pcap.bound[1] = 999;
    FOR_LOOP(i, 4) {
        cparticle_t *p = R_SpawnParticle();
        *p = (cparticle_t){ .next = p->next, .texture = particle_textures, .lifespan = 1, .midtime = 128,
            .blend_mode = i % 2 ? BLEND_MODE_BLEND : BLEND_MODE_ADD };
    }
    R_DrawParticles();
    T_EQ(pcap.draws, 4); T_EQ(pcap.texbinds, 5); T_EQ(pcap.cullgets, 1);
    T_EQ(pcap.cullon, 1); T_EQ(pcap.culloff, 1); T_ASSERT(pcap.culling);
    T_EQ(pcap.bound[1], 999); T_EQ(pcap.bound[2], 13);
    /* Deliberately clobber every particle unit as an intervening model/UI pass could. */
    pcap.bound[0] = pcap.bound[3] = pcap.bound[4] = pcap.bound[5] = 888;
    R_DrawParticles();
    T_EQ(pcap.draws, 8); T_EQ(pcap.texbinds, 10); T_EQ(pcap.cullgets, 2);
    FOR_LOOP(i, 4) T_EQ(pcap.texsets[7][i], particle_textures[0].texid);
    pcap.bound[0] = pcap.bound[3] = pcap.bound[4] = pcap.bound[5] = 777;
    R_DrawBillboardSprite(particle_textures + 1, &(vec3_t){0}, 3, COLOR32_WHITE);
    T_EQ(pcap.draws, 9); T_EQ(pcap.texbinds, 15); T_EQ(pcap.cullgets, 3);
    T_EQ(pcap.cullon, 3); T_EQ(pcap.culloff, 3); T_ASSERT(pcap.culling);
    FOR_LOOP(i, 4) T_EQ(pcap.texsets[8][i], particle_textures[1].texid);
    test_particles_reset();
    cparticle_t *p = R_SpawnParticle();
    *p = (cparticle_t){ .next = p->next, .lifespan = 1, .midtime = 128 };
    R_DrawParticles();
    T_EQ(pcap.cullgets, 1); T_EQ(pcap.cullon, 0); T_EQ(pcap.culloff, 0); T_ASSERT(!pcap.culling);
    test_particles_reset();
}

TEST(mdx_ui, particle_sampler_array_keeps_fog_unit_and_eight_attribute_limit) {
    test_particles_reset();
    ri.MemAlloc = calloc_test;
    R_InitParticles();
    T_EQ(pcap.uvsize, 3); T_EQ(pcap.stride, sizeof(particleVertex_t));
    T_EQ(pcap.uvoffset, offsetof(particleVertex_t, uv));
    T_EQ(particles_resources.shader.state.fogOfWar, 2);
    T_EQ(sizeof(particles_resources.shader.state.texture) / sizeof(int), 4);
    if (sizeof(particles_resources.shader.state.texture) / sizeof(int) == 4) {
        int const *units = (int const *)&particles_resources.shader.state.texture;
        T_EQ(units[0], 0); T_EQ(units[1], 3); T_EQ(units[2], 4); T_EQ(units[3], 5);
    }
    FOR_LOOP(i, MAX_SHADER_UNIFORMS)
        if (sd_particle.Uniforms[i].name && !strcmp(sd_particle.Uniforms[i].name, "u_texture"))
            T_EQ(sd_particle.Uniforms[i].count, 4);
    T_EQ(MAX_SHADER_ATTRIBS, 8);
    FOR_LOOP(i, 4) {
        char sampler[64];
        snprintf(sampler, sizeof(sampler), "texture(u_texture[%u], v_texcoord.xy)", i);
        T_NOT_NULL(strstr(sd_particle.FragmentBody, sampler));
    }
    free(particles_resources.particles);
    test_particles_reset();
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
    T_EQ(drawn_frame, 716);
    tr.viewDef.time = 999;
    MDLX_DrawSpriteInstance(&sprite, COLOR32_WHITE);
    T_EQ(drawn_frame, 716);
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
