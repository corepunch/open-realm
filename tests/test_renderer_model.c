#include "test.h"
#include "renderer/r_local.h"
#include "renderer/r_emit.h"
#include "games/warcraft-3/renderer/w3m/r_war3map.h"
#include "games/warcraft-3/renderer/mdx/r_mdx.h"
#include "renderer/r_shader.h"
#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>

static char shader_src[16384];

/* Capture the real shader source submission without requiring a window in the unit suite. */
static void BZ_TestShaderSource(GLuint shader, GLsizei count, const GLchar *const *strings, const GLint *lengths) {
    size_t used = 0;
    (void)shader;
    FOR_LOOP(i, count) {
        size_t size = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
        T_ASSERT(used + size < sizeof(shader_src));
        memcpy(shader_src + used, strings[i], size); used += size;
    }
    shader_src[used] = 0;
}
/* Mock only GL submission/status calls; shader creation and cache logic stay production code. */
static struct {
    GLenum fail;
    GLint logsize;
    int creates, links, uses, logs, deleted, uploads, exitcode;
    BOOL noalloc;
    HANDLE memory;
} shader_test;
static jmp_buf shader_exit;
static GLuint BZ_TestCreateShader(GLenum type) { shader_test.creates++; return type; }
static GLuint BZ_TestCreateProgram(void) { return GL_LINK_STATUS; }
static void BZ_TestCompileShader(GLuint obj) { (void)obj; }
static void BZ_TestAttachShader(GLuint obj, GLuint shader) { (void)obj; (void)shader; }
static void BZ_TestBindAttrib(GLuint obj, GLuint idx, const GLchar *name) { (void)obj; (void)idx; (void)name; }
static void BZ_TestLinkProgram(GLuint obj) { (void)obj; shader_test.links++; }
static void BZ_TestUseProgram(GLuint obj) { (void)obj; shader_test.uses++; }
static void BZ_TestDeleteShader(GLuint obj) { (void)obj; shader_test.deleted++; }
static GLint BZ_TestUniformLocation(GLuint obj, const GLchar *name) { (void)obj; (void)name; return 0; }
static struct { int calls, width, integer; GLsizei count; GLboolean transpose; float data[2048]; } upload;
static void capture_float(int width, GLsizei count, const GLfloat *val) {
    upload.calls++; upload.width = width; upload.count = count;
    memcpy(upload.data, val, width * count * sizeof(float));
}
static void BZ_TestUniform1i(GLint loc, GLint val) { (void)loc; upload.calls++; upload.integer = val; }
static void BZ_TestUniform1iv(GLint loc, GLsizei n, const GLint *v) { (void)n; BZ_TestUniform1i(loc, *v); }
static void BZ_TestUniform2iv(GLint loc, GLsizei n, const GLint *v) { (void)n; BZ_TestUniform1i(loc, v[1]); }
static void BZ_TestUniform1fv(GLint loc, GLsizei n, const GLfloat *v) { (void)loc; capture_float(1, n, v); }
static void BZ_TestUniform2fv(GLint loc, GLsizei n, const GLfloat *v) { (void)loc; capture_float(2, n, v); }
static void BZ_TestUniform3fv(GLint loc, GLsizei n, const GLfloat *v) { (void)loc; capture_float(3, n, v); }
static void BZ_TestUniform4fv(GLint loc, GLsizei n, const GLfloat *v) { (void)loc; capture_float(4, n, v); }
static void BZ_TestUniformMatrix3(GLint loc, GLsizei count, GLboolean transpose, const GLfloat *val) {
    (void)loc; capture_float(9, count, val); upload.transpose = transpose;
}
static void BZ_TestUniformMatrix4(GLint loc, GLsizei count, GLboolean transpose, const GLfloat *val) {
    (void)loc; capture_float(16, count, val); upload.transpose = transpose; shader_test.uploads++;
}
static int deleted_programs;
static void BZ_TestDeleteProgram(GLuint id) { (void)id; deleted_programs++; }
static void BZ_TestShaderStatus(GLuint obj, GLenum check, GLint *val) {
    *val = check == GL_INFO_LOG_LENGTH ? shader_test.logsize : obj != shader_test.fail;
}
static void BZ_TestShaderLog(GLuint obj, GLsizei size, GLsizei *length, GLchar *log) {
    (void)obj; (void)length; shader_test.logs++;
    snprintf(log, size, "mock driver rejection");
}
static void *BZ_TestShaderMalloc(size_t size) { return shader_test.noalloc ? NULL : malloc(size); }
static _Noreturn void BZ_TestShaderExit(int code) { shader_test.exitcode = code; longjmp(shader_exit, 1); }
#define glShaderSource BZ_TestShaderSource
#define glCreateShader BZ_TestCreateShader
#define glCreateProgram BZ_TestCreateProgram
#define glCompileShader BZ_TestCompileShader
#define glAttachShader BZ_TestAttachShader
#define glBindAttribLocation BZ_TestBindAttrib
#define glLinkProgram BZ_TestLinkProgram
#define glUseProgram BZ_TestUseProgram
#define glDeleteShader BZ_TestDeleteShader
#define glGetUniformLocation BZ_TestUniformLocation
#define glUniform1i BZ_TestUniform1i
#define glUniform1iv BZ_TestUniform1iv
#define glUniform2iv BZ_TestUniform2iv
#define glUniform1fv BZ_TestUniform1fv
#define glUniform2fv BZ_TestUniform2fv
#define glUniform3fv BZ_TestUniform3fv
#define glUniform4fv BZ_TestUniform4fv
#define glDeleteProgram BZ_TestDeleteProgram
#define glUniformMatrix3fv BZ_TestUniformMatrix3
#define glUniformMatrix4fv BZ_TestUniformMatrix4
#define glGetShaderiv BZ_TestShaderStatus
#define glGetProgramiv BZ_TestShaderStatus
#define glGetShaderInfoLog BZ_TestShaderLog
#define glGetProgramInfoLog BZ_TestShaderLog
#define malloc BZ_TestShaderMalloc
#define exit BZ_TestShaderExit
#include "renderer/r_shader.c"
#undef glShaderSource
#undef glCreateShader
#undef glCreateProgram
#undef glCompileShader
#undef glAttachShader
#undef glBindAttribLocation
#undef glLinkProgram
#undef glUseProgram
#undef glDeleteShader
#undef glGetUniformLocation
#undef glUniform1i
#undef glUniform1iv
#undef glUniform2iv
#undef glUniform1fv
#undef glUniform2fv
#undef glUniform3fv
#undef glUniform4fv
#undef glDeleteProgram
#undef glUniformMatrix3fv
#undef glUniformMatrix4fv
#undef glGetShaderiv
#undef glGetProgramiv
#undef glGetShaderInfoLog
#undef glGetProgramInfoLog
#undef malloc
#undef exit

refImport_t ri;
struct render_globals tr;
static DWORD load_count, release_count, register_count;
static BOOL fail_load, touch_during_registration;
static DWORD spawn_count;
static LPTEXTURE texture_load_result;
static GLenum upload_format, upload_internal;
static COLOR32 upload_pixel;
static DWORD upload_count;
static LPCVOID upload_data;
static LPCSTR test_version = "3.1", test_extension = "";
static DWORD alloc_count, free_count, ext_count;

static GLubyte const *test_glstring(GLenum name) { (void)name; return (GLubyte const *)test_version; }
static SDL_bool test_hasext(char const *name) { ext_count++; return !strcmp(name, test_extension) ? SDL_TRUE : SDL_FALSE; }

/* Capture the actual GL upload contract without requiring a display or a particular GL backend. */
static void test_teximage(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void *data) {
    (void)target; (void)level; (void)w; (void)h; (void)border;
    T_EQ(type, GL_UNSIGNED_BYTE);
    upload_format = format; upload_internal = internal; upload_data = data; upload_count++;
    if (data) upload_pixel = *(LPCCOLOR32)data;
}
static void test_gentex(GLsizei n, GLuint *ids) { while (n--) *ids++ = 99; }
static void test_bindtex(GLenum target, GLuint id) { (void)target; (void)id; }
static void test_texparam(GLenum target, GLenum name, GLint value) { (void)target; (void)name; (void)value; }
static DWORD texture_delete_count;
static void test_deletetex(GLsizei n, const GLuint *ids) { (void)ids; texture_delete_count += n; }
#undef glTexImage2D
#undef glGenTextures
#undef glBindTexture
#undef glTexParameteri
#undef glDeleteTextures
#define glTexImage2D test_teximage
#define glGenTextures test_gentex
#define glBindTexture test_bindtex
#define glTexParameteri test_texparam
#define glDeleteTextures test_deletetex
#undef glGetString
#define glGetString test_glstring
#define SDL_GL_ExtensionSupported test_hasext
#include "renderer/r_texture.c"
#include "renderer/r_blp1.c"
#include "renderer/r_blp2.c"
#include "renderer/r_pcx.c"
#include "renderer/r_dds.c"

static struct { DWORD calls, first, count, instances, stats_count, stats_instances; } draw_test;
void R_StatsDraw(GLenum mode, DWORD count, DWORD instances) {
    (void)mode; draw_test.stats_count = count; draw_test.stats_instances = instances;
}
static void test_genva(GLsizei n, GLuint *ids) { while (n--) *ids++ = 7; }
static void test_bindva(GLuint id) { (void)id; }
static void test_bindbuf(GLenum target, GLuint id) { (void)target; (void)id; }
static void test_enableattr(GLuint index) { (void)index; }
static void test_attrptr(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *ptr) {
    (void)index; (void)size; (void)type; (void)normalized; (void)stride; (void)ptr;
}
static void test_divisor(GLuint index, GLuint divisor) { (void)index; (void)divisor; }
static void test_drawarrays(GLenum mode, GLint first, GLsizei count) {
    (void)mode; draw_test.calls++; draw_test.first = first; draw_test.count = count; draw_test.instances = 1;
}
static void test_drawarrays_inst(GLenum mode, GLint first, GLsizei count, GLsizei instances) {
    (void)mode; draw_test.calls++; draw_test.first = first; draw_test.count = count; draw_test.instances = instances;
}
#define glGenVertexArrays test_genva
#define glBindVertexArray test_bindva
#define glBindBuffer test_bindbuf
#define glEnableVertexAttribArray test_enableattr
#define glVertexAttribPointer test_attrptr
#define glVertexAttribDivisor test_divisor
#define glDrawArrays test_drawarrays
#define glDrawArraysInstanced test_drawarrays_inst
#include "renderer/r_buffer.c"
#undef glGenVertexArrays
#undef glBindVertexArray
#undef glBindBuffer
#undef glEnableVertexAttribArray
#undef glVertexAttribPointer
#undef glVertexAttribDivisor
#undef glDrawArrays
#undef glDrawArraysInstanced

static HANDLE test_alloc(long size) { alloc_count++; return calloc(1, (size_t)size); }
static void test_free(HANDLE memory) { free_count++; free(memory); }
static void test_error(LPCSTR format, ...) { (void)format; T_ASSERT(false); }
static void test_spawn(void *context) { (*(DWORD *)context)++; }

LPTEXTURE R_LoadTexture(LPCSTR filename) { (void)filename; return texture_load_result; }

LPMODEL R_LoadModel(LPCSTR filename) {
    (void)filename; load_count++;
    return fail_load ? NULL : test_alloc(sizeof(model_t));
}

void R_ReleaseModel(LPMODEL model) { release_count++; test_free(model); }

void R_RegisterMap(LPCSTR map) {
    (void)map; register_count++;
    if (touch_during_registration) R_LoadRegisteredModel("models/touched.mdx");
}

static void reset_registry(void) {
    R_ShutdownModels();
    ri.MemAlloc = test_alloc; ri.MemFree = test_free; ri.error = test_error;
    load_count = release_count = register_count = 0;
    fail_load = touch_during_registration = false;
}

static LPTEXTURE reset_texture_registry(void) {
    R_ShutdownTextureCache();
    ri.MemAlloc = test_alloc; ri.MemFree = test_free;
    r_load_streamed = false; r_stream_generation = 0; texture_delete_count = 0;
    return test_alloc(sizeof(TEXTURE));
}

TEST(renderer_model, mdx_keytrack_binary_lookup_preserves_sequence_semantics) {
    BYTE storage[sizeof(mdxKeyTrack_t) + 5 * (sizeof(int) + sizeof(float))] = { 0 };
    mdxKeyTrack_t *track = (mdxKeyTrack_t *)storage;
    int times[] = { 50, 150, 200, 300, 400 };
    float values[] = { 0.5f, 1.5f, 2.0f, 3.0f, 4.0f };
    mdxSequence_t seq = { .interval = { 100, 350 } };
    mdxModel_t model = { .sequences = &seq, .num_sequences = 1 };
    float value = 0;

    track->keyframeCount = 5; track->datatype = TDATA_FLOAT1;
    track->linetype = TRACK_LINEAR; track->globalSeqId = (DWORD)-1;
    FOR_LOOP(i, 5) {
        mdxKeyFrame_t *key = (mdxKeyFrame_t *)((BYTE *)track->values + i * (sizeof(int) + sizeof(float)));
        key->time = times[i]; memcpy(key->data, &values[i], sizeof(values[i]));
    }
    MDLX_GetModelKeytrackValue(&model, track, 100, &value); T_FEQ(value, 1.5f, 0.001f);
    MDLX_GetModelKeytrackValue(&model, track, 250, &value); T_FEQ(value, 2.5f, 0.001f);
    MDLX_GetModelKeytrackValue(&model, track, 325, &value); T_FEQ(value, 2.625f, 0.001f);
}

TEST(renderer_model, mdx_attachment_positions_follow_authored_pivot_and_model_transform) {
    mdxAttachment_t sprite = { 0 }, other = { 0 };
    mdxAttachmentPosition_t positions[2] = { 0 };
    VECTOR3 pivots[] = { { 4.0f, 5.0f, 6.0f }, { 1.0f, 2.0f, 3.0f } };
    mdxModel_t model = { .attachments = &sprite, .pivots = pivots, .num_pivots = 2 };
    MATRIX4 transform;
    DWORD count;

    snprintf(sprite.node.name, sizeof(sprite.node.name), "Sprite First Ref");
    sprite.node.node_id = 0; sprite.node.parent_id = (DWORD)-1; sprite.next = &other;
    snprintf(other.node.name, sizeof(other.node.name), "Origin Ref");
    other.node.node_id = 1; other.node.parent_id = (DWORD)-1;
    model.nodes[0] = &sprite.node; model.nodes[1] = &other.node;
    model.node_list[0] = &sprite.node; model.node_list[1] = &other.node;
    model.num_nodes = 2;

    Matrix4_identity(&transform);
    Matrix4_translate(&transform, &(VECTOR3){ 10.0f, 20.0f, 30.0f });
    count = MDLX_CollectAttachmentPositions(&model, &transform, 0, 0,
                                            "Sprite ", positions,
                                            sizeof(positions) / sizeof(*positions));

    T_EQ(count, 1);
    T_STREQ(positions[0].name, "Sprite First Ref");
    T_FEQ(positions[0].origin.x, 14.0f, 0.001f);
    T_FEQ(positions[0].origin.y, 25.0f, 0.001f);
    T_FEQ(positions[0].origin.z, 36.0f, 0.001f);
}


TEST(renderer_model, mdx_geometry_packs_two_geosets_into_model_ranges) {
    VECTOR3 pos[] = {{1,2,3}, {4,5,6}, {7,8,9}}, normals[] = {{0,0,1}, {0,1,0}, {1,0,0}};
    VECTOR2 uv[] = {{0,0}, {1,0}, {0,1}};
    short first_idx[] = {0,1,0}, second_idx[] = {0};
    mdxGeoset_t second = {.vertices=pos+2,.normals=normals+2,.texcoord=uv+2,.triangles=second_idx,
        .num_vertices=1,.num_normals=1,.num_texcoord=1,.num_triangles=1};
    mdxGeoset_t first = {.vertices=pos,.normals=normals,.texcoord=uv,.triangles=first_idx,
        .num_vertices=2,.num_normals=2,.num_texcoord=2,.num_triangles=3,.next=&second};
    mdxModel_t model = {.geosets=&first}; VERTEX vertices[3]; USHORT indices[4];
    ri.MemAlloc = test_alloc; ri.MemFree = test_free;
    MDX_PackModelGeometry(&model, vertices, indices);
    T_FEQ(vertices[2].position.x, 7, 0.001f);
    T_EQ(vertices[0].color.r, 255); T_EQ(vertices[0].color.g, 255);
    T_EQ(vertices[0].color.b, 255); T_EQ(vertices[0].color.a, 255);
    T_EQ(indices[2], 0); T_EQ(indices[3], 0); T_EQ((int)first.indexofs, 0); T_EQ((int)second.indexofs, 6);
    ri.MemFree(first.matrixPalette); ri.MemFree(second.matrixPalette);
}

TEST(renderer_model, filename_cache_hit_and_miss) {
    LPMODEL first, second;
    reset_registry();
    first = R_LoadRegisteredModel("Models/Foo.mdx");
    second = R_LoadRegisteredModel("models/foo.mdx");
    T_ASSERT(first == second); T_EQ(load_count, 1); T_EQ(release_count, 0);
    R_ReleaseRegisteredModel(first); R_ReleaseRegisteredModel(second);
    R_RegisterMapAssets("next");
    T_EQ(register_count, 1); T_EQ(release_count, 1);
}

TEST(renderer_model, registration_keeps_touched_model_then_reclaims_it) {
    LPMODEL model;
    reset_registry();
    model = R_LoadRegisteredModel("models/touched.mdx"); R_ReleaseRegisteredModel(model);
    touch_during_registration = true; R_RegisterMapAssets("current");
    T_EQ(load_count, 1); T_EQ(release_count, 0);
    R_ReleaseRegisteredModel(model); touch_during_registration = false; R_RegisterMapAssets("next");
    T_EQ(release_count, 1);
}

TEST(renderer_model, missing_model_placeholder_is_cached) {
    LPMODEL first, second;
    reset_registry(); fail_load = true;
    first = R_LoadRegisteredModel("models/missing.mdx"); second = R_LoadRegisteredModel("models/missing.mdx");
    T_ASSERT(first == second); T_EQ(load_count, 1);
    R_ReleaseRegisteredModel(first); R_ReleaseRegisteredModel(second); R_RegisterMapAssets("next");
    T_EQ(release_count, 1);
}

TEST(renderer_model, unknown_model_release_is_immediate) {
    LPMODEL model;
    reset_registry(); model = test_alloc(sizeof(*model));
    R_ReleaseRegisteredModel(model); T_EQ(release_count, 1);
}

TEST(renderer_texture, cached_registration_preserves_newer_texture_indices) {
    TEXTURE first = { .texid = 100 }, second = { .texid = 101 };

    texture_load_result = &first; T_EQ(R_RegisterTextureFile("first"), 100);
    texture_load_result = &second; T_EQ(R_RegisterTextureFile("second"), 101);
    T_ASSERT(R_FindTextureByID(100) == &first); T_ASSERT(R_FindTextureByID(101) == &second);
    texture_load_result = &first; T_EQ(R_RegisterTextureFile("first"), 100);
    T_ASSERT(R_FindTextureByID(100) == &first); T_ASSERT(R_FindTextureByID(101) == &second);
}

TEST(renderer_texture, resident_registry_keeps_entries_beyond_configstring_limit) {
    static TEXTURE placeholder = { .texid = 77 };
    char path[64];

    ri.MemAlloc = test_alloc; ri.MemFree = test_free; tr.texture[TEX_PLACEHOLDER] = &placeholder;
    FOR_LOOP(i, MAX_IMAGES * 4 + 1) {
        snprintf(path, sizeof(path), "Textures/Registry/%u.blp", i);
        R_CacheLoadedTexture(path, &placeholder);
    }
    T_ASSERT(R_FindLoadedTexture("textures/registry/1024.BLP") == &placeholder);
}

TEST(renderer_texture, persistent_then_streamed_remains_pinned) {
    LPTEXTURE texture = reset_texture_registry();

    R_CacheLoadedTexture("textures/shared.blp", texture);
    T_ASSERT(r_image_cache->pinned); T_ASSERT(!r_image_cache->streamed);
    r_load_streamed = true; T_ASSERT(R_FindLoadedTexture("textures/shared.blp") == texture); r_load_streamed = false;
    R_AdvanceTextureGeneration(); R_ReclaimStreamedTextures(0);
    T_ASSERT(r_image_cache && r_image_cache->texture == texture); T_EQ(texture_delete_count, 0);
    R_ShutdownTextureCache();
}

TEST(renderer_texture, streamed_then_persistent_becomes_pinned) {
    LPTEXTURE texture = reset_texture_registry();

    r_load_streamed = true; R_CacheLoadedTexture("textures/shared.blp", texture); r_load_streamed = false;
    T_ASSERT(r_image_cache->streamed); T_ASSERT(!r_image_cache->pinned);
    T_ASSERT(R_FindLoadedTexture("textures/shared.blp") == texture);
    T_ASSERT(r_image_cache->pinned); T_ASSERT(!r_image_cache->streamed);
    R_AdvanceTextureGeneration(); R_ReclaimStreamedTextures(0);
    T_ASSERT(r_image_cache && r_image_cache->texture == texture); T_EQ(texture_delete_count, 0);
    R_ShutdownTextureCache();
}

TEST(renderer_texture, stale_streamed_texture_is_reclaimed) {
    LPTEXTURE texture = reset_texture_registry();

    r_load_streamed = true; R_CacheLoadedTexture("textures/streamed.blp", texture); r_load_streamed = false;
    R_AdvanceTextureGeneration(); R_ReclaimStreamedTextures(0);
    T_NULL(r_image_cache); T_EQ(texture_delete_count, 1);
}

TEST(renderer_texture, current_streamed_generation_is_retained) {
    LPTEXTURE texture = reset_texture_registry();

    r_load_streamed = true; R_CacheLoadedTexture("textures/current.blp", texture); r_load_streamed = false;
    R_ReclaimStreamedTextures(0);
    T_ASSERT(r_image_cache && r_image_cache->texture == texture); T_EQ(texture_delete_count, 0);
    R_ShutdownTextureCache();
}

TEST(renderer_texture, persistent_alias_pins_streamed_owner) {
    LPTEXTURE texture = reset_texture_registry();

    r_load_streamed = true; R_CacheLoadedTexture("textures/owner.blp", texture); r_load_streamed = false;
    R_CacheLoadedTexture("textures/alias.blp", texture);
    rImageCacheEntry_t *owner = R_TextureOwner(r_image_cache);
    T_ASSERT(owner->owns_texture); T_ASSERT(owner->pinned); T_ASSERT(!owner->streamed);
    R_AdvanceTextureGeneration(); R_ReclaimStreamedTextures(0);
    T_EQ(texture_delete_count, 0);
    R_ShutdownTextureCache();
}

TEST(renderer_texture, reclaim_removes_streamed_aliases_with_owner) {
    LPTEXTURE texture = reset_texture_registry();

    r_load_streamed = true;
    R_CacheLoadedTexture("textures/owner.blp", texture);
    R_CacheLoadedTexture("textures/alias.blp", texture);
    r_load_streamed = false;
    R_AdvanceTextureGeneration(); R_ReclaimStreamedTextures(0);
    T_NULL(r_image_cache); T_NULL(R_FindLoadedTexture("textures/alias.blp")); T_EQ(texture_delete_count, 1);
}

TEST(renderer_model, clock_emission_needs_no_instance_accumulator) {
    spawn_count = 0;
    R_EmitParticlesAtTime(10.0f, 1050, 100, test_spawn, &spawn_count);
    T_EQ(spawn_count, 1);
}

TEST(renderer_model, clock_emission_ignores_zero_rate_and_delta) {
    spawn_count = 0;
    R_EmitParticlesAtTime(0.0f, 1050, 100, test_spawn, &spawn_count);
    R_EmitParticlesAtTime(10.0f, 1050, 0, test_spawn, &spawn_count);
    T_EQ(spawn_count, 0);
}

TEST(renderer_alpha, active_samples_require_a_real_multisample_buffer) {
    T_EQ(R_MsaaActiveSamples(0, 4), 0); T_EQ(R_MsaaActiveSamples(1, 1), 0);
    T_EQ(R_MsaaActiveSamples(1, 4), 4);
}

TEST(renderer_instances, upload_size_uses_wide_arithmetic) {
    T_EQ(R_InstanceBufferBytes(465524), (size_t)29793536);
}

TEST(renderer_instances, dynamic_capacity_reuses_and_grows_power_of_two) {
    T_EQ(R_InstanceBufferCapacity(0, 1), (DWORD)16);
    T_EQ(R_InstanceBufferCapacity(16, 16), (DWORD)16);
    T_EQ(R_InstanceBufferCapacity(16, 17), (DWORD)32);
}

TEST(renderer_shader, directional_light_uses_array_schema) {
    MATRIX4 packed[BZ_MODEL_LIGHT_MAX];
    MODELLIGHTING state = {
        .ambient = { 1, 1, 1 }, .count = 1,
        .lights[0] = {
            .dir = { 1, 2, 3 }, .color = { 4, 5, 6 }, .ambient = { 7, 8, 9 },
            .intensity = 0.5f, .ambient_intensity = 0.5f, .type = R_MODEL_LIGHT_DIRECT,
        },
    };
    R_PackModelLighting(packed, &state);
    T_EQ(packed[0].v[3], 1.0f); T_EQ(packed[0].v[4], -1.0f); T_EQ(packed[0].v[6], -3.0f);
    T_EQ(packed[0].v[8], 4.0f); T_EQ(packed[0].v[11], 0.5f);
    T_EQ(packed[0].v[12], 4.5f); T_EQ(packed[0].v[14], 5.5f); T_EQ(packed[0].v[15], 1.0f);
}

TEST(renderer_shader, lighting_state_packs_all_sources) {
    MATRIX4 packed[BZ_MODEL_LIGHT_MAX];
    MODELLIGHTING state = { .count = 3 };
    FOR_LOOP(i, state.count) {
        state.lights[i].type = R_MODEL_LIGHT_DIRECT;
        state.lights[i].color.x = (FLOAT)i + 1.0f;
        state.lights[i].intensity = 1.0f;
    }
    R_PackModelLighting(packed, &state);
    T_EQ(packed[0].v[8], 1.0f); T_EQ(packed[1].v[8], 2.0f); T_EQ(packed[2].v[8], 3.0f);
}

TEST(renderer_shader, grass_state_uses_one_matrix) {
    MATRIX4 packed;
    MODELGRASS grass = {
        .camera = { 1, 2 }, .fade = { 3, 4 }, .time = 5, .wind = { 6, 7, 8 },
        .phase = { 9, 10, 11, 12 }, .height = { 13, 14 }, .enabled = true,
    };
    R_PackModelGrass(&packed, &grass);
    FOR_LOOP(i, 14) T_EQ(packed.v[i], (FLOAT)i + 1.0f);
    T_EQ(packed.v[14], 1.0f); T_EQ(packed.v[15], 0.0f);
    grass.enabled = false; R_PackModelGrass(&packed, &grass); T_EQ(packed.v[14], 0.0f); T_EQ(packed.v[15], 0.0f);
}

TEST(renderer_bones, model_shader_preserves_high_palette_indices) {
    memset(&tr, 0, sizeof(tr));
    R_SetShaderSourceFromDesc(1, &sd_model, true, NULL);
    T_NOT_NULL(strstr(shader_src, "uniform mat4 u_bones[128];"));
    T_EQ(sd_model.Uniforms[0].count, BZ_BONE_PALETTE_MAX);
    T_EQ(sd_model.Uniforms[0].count_offset, offsetof(MODELSTATE, boneCount));
    T_ASSERT(sd_model.Uniforms[0].counted);
    T_NULL(strstr(shader_src, "BZ_BONE_COUNT"));
    /* Slot 83 must stay 83: the old clamp redirected it to 63 with a 64-matrix palette. */
    T_NOT_NULL(strstr(shader_src, "int boneIdx = int(a_skin1[i]) + int(u_firstBoneLookupIndex);"));
    T_NULL(strstr(shader_src, "#define BZ_USE_INSTANCING"));
}

TEST(renderer_bones, instanced_shader_uses_the_same_palette_contract) {
    R_SetShaderSourceFromDesc(1, &sd_model, true, "#define BZ_USE_INSTANCING 1\n");
    T_NOT_NULL(strstr(shader_src, "#define BZ_USE_INSTANCING 1\n"));
    T_NOT_NULL(strstr(shader_src, "uniform mat4 u_bones[128];"));
    T_NOT_NULL(strstr(shader_src, "int boneIdx = int(a_skin1[i]) + int(u_firstBoneLookupIndex);"));
}

TEST(renderer_shader, normal_model_defines_do_not_inherit_instancing) {
    T_NOT_NULL(strstr(R_ShaderDefines(true), "#define BZ_USE_INSTANCING 1\n"));
    T_NULL(strstr(R_ShaderDefines(false), "BZ_USE_INSTANCING"));
}

/* GLSL 120 does not accept implicit integer-to-float conversion in these fog-raycast expressions. */
TEST(renderer_shader, fog_raycast_uses_float_literals_for_glsl120) {
    FILE *file = fopen("renderer/r_fogofwar.c", "rb");
    char line[256];
    BOOL up = false, z = false, invalid = false;

    T_NOT_NULL(file);
    while (file && fgets(line, sizeof(line), file)) {
        if (strstr(line, "vec3 up = vec3(0.0, 0.0, 1.0)")) up = true;
        if (strstr(line, "pos.z = 0.0")) z = true;
        if (strstr(line, "vec3 up = vec3(0, 0, 1)") || strstr(line, "pos.z = 0;")) invalid = true;
    }
    if (file) fclose(file);
    T_ASSERT(up); T_ASSERT(z); T_ASSERT(!invalid);
}

/* WC3 waterfalls are PRE2-only MDX models, so their shared particle shader must consume the world FOW mask. */
TEST(renderer_shader, world_particles_sample_fog_of_war) {
    FILE *file = fopen("renderer/r_particles.c", "rb");
    char line[256];
    BOOL matrix = false, sampler = false, coord = false, shade = false, define = false;

    T_NOT_NULL(file);
    while (file && fgets(line, sizeof(line), file)) {
        if (strstr(line, "UNIFORM(textureMatrix,")) matrix = true;
        if (strstr(line, "UNIFORM(fogOfWar,")) sampler = true;
        if (strstr(line, "v_texcoord2 = (u_textureMatrix * vec4(pos, 1.0)).xy")) coord = true;
        if (strstr(line, "col.rgb *= texture(u_fogOfWar, v_texcoord2).r")) shade = true;
        if (strstr(line, "#define USE_FOGOFWAR 1\\n")) define = true;
    }
    if (file) fclose(file);
    T_ASSERT(matrix); T_ASSERT(sampler); T_ASSERT(coord); T_ASSERT(shade); T_ASSERT(define);
}

static HANDLE shader_alloc(long size) { return shader_test.memory = test_alloc(size); }

/* Each case starts with empty caches and independent driver counters. */
static void reset_shader(void) {
    ri.MemAlloc = shader_alloc; ri.MemFree = test_free;
    R_ShutdownModelShader();
    memset(&shader_test, 0, sizeof(shader_test));
    shader_test.logsize = 64;
}

TEST(renderer_shader, model_cache_checks_compile_and_link_once) {
    reset_shader();
    MODELPROG * shader = R_ModelShader();
    T_NOT_NULL(shader); T_ASSERT(R_ModelShader() == shader);
    T_EQ(shader_test.creates, 2); T_EQ(shader_test.links, 1); T_EQ(shader_test.deleted, 2);
    T_EQ(shader_test.exitcode, 0); T_EQ(shader_test.logs, 0);
    R_ShutdownModelShader();
}

TEST(renderer_shader, instanced_cache_initializes_full_identity_palette_once) {
    reset_shader();
    MODELPROG * shader = R_ModelShaderInstanced();
    T_NOT_NULL(shader); T_ASSERT(R_ModelShaderInstanced() == shader);
    T_EQ(shader_test.creates, 2); T_EQ(shader_test.links, 1); T_EQ(shader_test.uploads, 0);
    FOR_LOOP(i, BZ_BONE_PALETTE_MAX) FOR_LOOP(j, 16) T_EQ(shader->state.bones[i].v[j], j % 5 == 0 ? 1.0f : 0.0f);
    T_EQ(shader_test.deleted, 2); T_EQ(shader_test.exitcode, 0);
    R_ShutdownModelShader();
}

/* A rejected stage must terminate before binding or caching an invalid/unskinned program. */
TEST(renderer_shader, failed_compile_or_link_never_returns_a_model_fallback) {
    static const GLenum stages[] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER, GL_LINK_STATUS };
    FOR_LOOP(i, sizeof(stages) / sizeof(stages[0])) {
        reset_shader(); shader_test.fail = stages[i];
        if (!setjmp(shader_exit)) {
            R_ModelShader(); T_ASSERT(false);
        }
        T_EQ(shader_test.exitcode, EXIT_FAILURE); T_EQ(shader_test.uses, 0);
        T_EQ(shader_test.links, stages[i] == GL_LINK_STATUS ? 1 : 0);
        T_EQ(shader_test.logs, 1); T_ASSERT(!model_shader_loaded);
        free(shader_test.memory);
    }
}

TEST(renderer_shader, instanced_link_failure_is_fatal_without_palette_upload) {
    reset_shader(); shader_test.fail = GL_LINK_STATUS;
    if (!setjmp(shader_exit)) {
        R_ModelShaderInstanced(); T_ASSERT(false);
    }
    T_EQ(shader_test.exitcode, EXIT_FAILURE); T_EQ(shader_test.uses, 0); T_EQ(shader_test.uploads, 0);
    T_ASSERT(!instanced_shader_loaded); free(shader_test.memory);
}

TEST(renderer_shader, failures_remain_fatal_without_a_driver_log_buffer) {
    FOR_LOOP(i, 2) {
        reset_shader(); shader_test.fail = GL_LINK_STATUS;
        shader_test.logsize = i ? 64 : 0; shader_test.noalloc = i;
        if (!setjmp(shader_exit)) {
            R_ModelShader(); T_ASSERT(false);
        }
        T_EQ(shader_test.exitcode, EXIT_FAILURE); T_EQ(shader_test.logs, 0); T_EQ(shader_test.uses, 0);
        free(shader_test.memory);
    }
}

TEST(renderer_texture, red_blue_swap_preserves_other_channels) {
    BYTE rgba[] = { 1, 2, 3, 4, 5, 6, 7, 8 }, rgb[] = { 9, 10, 11 };
    R_SwapRedBlue(rgba, 2, 4); R_SwapRedBlue(rgb, 1, 3);
    T_EQ(rgba[0], (BYTE)3); T_EQ(rgba[1], (BYTE)2); T_EQ(rgba[2], (BYTE)1); T_EQ(rgba[3], (BYTE)4);
    T_EQ(rgba[4], (BYTE)7); T_EQ(rgba[7], (BYTE)8);
    T_EQ(rgb[0], (BYTE)11); T_EQ(rgb[1], (BYTE)10); T_EQ(rgb[2], (BYTE)9);
}

TEST(renderer_texture, rgba_upload_preserves_red_blue_and_alpha) {
    TEXTURE tex = { .texid = 99 };
    COLOR32 pixel = { 241, 37, 9, 123 };
    upload_count = 0;
    R_LoadTextureMipLevel(&tex, &(TEXMIP){ &pixel, 0, 1, 0, PIXEL_RGBA }); T_EQ(upload_count, 0);
    R_LoadTextureMipLevel(&tex, &(TEXMIP){ &pixel, 1, 1, 0, PIXEL_RGBA });
    T_EQ(upload_count, 1); T_EQ(upload_format, GL_RGBA);
    T_EQ(upload_pixel.r, 241); T_EQ(upload_pixel.g, 37); T_EQ(upload_pixel.b, 9); T_EQ(upload_pixel.a, 123);
}

/* Emulate the advertised API, not the host OS; check byte order, ownership and the EXT/APPLE format pairs. */
TEST(renderer_texture, source_format_and_context_select_upload_without_redundant_copies) {
    static const struct { LPCSTR version, extension; GLenum internal; } cases[] = {
        { "3.1", "", GL_RGBA },
        { "OpenGL ES 3.0", "GL_EXT_texture_format_BGRA8888", BZ_GL_BGRA },
        { "OpenGL ES 3.0", "GL_APPLE_texture_format_BGRA8888", GL_RGBA },
        { "OpenGL ES 3.0", "", 0 },
    };
    TEXTURE tex = { .texid = 99 };
    ri.MemAlloc = test_alloc; ri.MemFree = test_free;
    FOR_LOOP(i, sizeof(cases) / sizeof(cases[0])) {
        test_version = cases[i].version; test_extension = cases[i].extension;
        R_InitTextureFormats();
        T_EQ(r_bgra_internal, cases[i].internal);
        DWORD queries = ext_count;
        FOR_LOOP(src, 2) {
            COLOR32 pixel = src == PIXEL_BGRA ? (COLOR32){9, 37, 241, 123} : (COLOR32){241, 37, 9, 123};
            COLOR32 saved = pixel;
            BOOL convert = src == PIXEL_BGRA && !cases[i].internal;
            alloc_count = free_count = 0;
            R_LoadTextureMipLevel(&tex, &(TEXMIP){ &pixel, 1, 1, 1, src });
            T_EQ(upload_format, src == PIXEL_BGRA && !convert ? BZ_GL_BGRA : GL_RGBA);
            T_EQ(upload_internal, src == PIXEL_BGRA && !convert ? cases[i].internal : GL_RGBA);
            T_EQ(upload_pixel.r, src == PIXEL_BGRA && !convert ? 9 : 241);
            T_EQ(upload_pixel.b, src == PIXEL_BGRA && !convert ? 241 : 9);
            T_EQ(upload_pixel.g, 37); T_EQ(upload_pixel.a, 123);
            T_EQ(alloc_count, convert ? 1 : 0); T_EQ(free_count, alloc_count);
            T_ASSERT(!memcmp(&pixel, &saved, sizeof(pixel)));
            if (!convert) T_ASSERT(upload_data == &pixel);
            T_EQ(ext_count, queries);
        }
        alloc_count = 0;
        R_LoadTextureMipLevel(&tex, &(TEXMIP){ NULL, 1, 1, 0, PIXEL_BGRA });
        T_NULL(upload_data); T_EQ(alloc_count, 0);
    }
}

TEST(renderer_texture, blp1_palette_upload_is_rgba) {
    struct { struct tBLP1Header hdr; COLOR32 pal[256]; BYTE index; } file = {0};
    ri.MemAlloc = test_alloc; ri.MemFree = test_free;
    r_bgra_internal = 0;
    file.hdr.magic = ID_BLP1; file.hdr.type = 1; file.hdr.width = file.hdr.height = 1;
    file.hdr.offsets[0] = offsetof(__typeof__(file), index); file.hdr.lengths[0] = 1;
    file.pal[0] = (COLOR32){9, 37, 241, 0};
    test_free(R_LoadTextureBLP1(&file, sizeof(file)));
    T_EQ(upload_format, GL_RGBA);
    T_EQ(upload_pixel.r, 241); T_EQ(upload_pixel.g, 37); T_EQ(upload_pixel.b, 9); T_EQ(upload_pixel.a, 255);
}

TEST(renderer_texture, blp2_raw_palette_and_dxt_upload_are_rgba) {
    struct { struct tBLP2Header hdr; BYTE data[16]; } file = {0};
    ri.MemAlloc = test_alloc; ri.MemFree = test_free;
    r_bgra_internal = 0;
    file.hdr.magic = ID_BLP2; file.hdr.type = 1; file.hdr.width = file.hdr.height = 1;
    file.hdr.offsets[0] = offsetof(__typeof__(file), data); file.hdr.lengths[0] = 4;
    file.hdr.encoding = 3; file.hdr.alphaDepth = 8;
    memcpy(file.data, (BYTE[]){9, 37, 241, 123}, 4);
    test_free(R_LoadTextureBLP2(&file, sizeof(file)));
    T_EQ(upload_format, GL_RGBA);
    T_EQ(upload_pixel.r, 241); T_EQ(upload_pixel.b, 9); T_EQ(upload_pixel.a, 123);
    file.hdr.encoding = 1; file.hdr.alphaDepth = 0;
    file.hdr.palette[0] = (COLOR32){9, 37, 241, 0}; file.data[0] = 0;
    test_free(R_LoadTextureBLP2(&file, sizeof(file)));
    T_EQ(upload_pixel.r, 241); T_EQ(upload_pixel.b, 9); T_EQ(upload_pixel.a, 255);
    file.hdr.encoding = 2; file.hdr.width = file.hdr.height = 4; file.hdr.lengths[0] = 8;
    memcpy(file.data, (BYTE[]){0, 248, 31, 0, 0, 0, 0, 0}, 8);
    test_free(R_LoadTextureBLP2(&file, sizeof(file)));
    T_EQ(upload_pixel.r, 255); T_EQ(upload_pixel.g, 0); T_EQ(upload_pixel.b, 0); T_EQ(upload_pixel.a, 255);
}

TEST(renderer_texture, pcx_palette_upload_is_rgba) {
    BYTE file[128 + 1 + 769] = {0};
    ri.MemAlloc = test_alloc; ri.MemFree = test_free;
    file[0] = 10; file[2] = 1; file[3] = 8; file[65] = 1; file[66] = 1;
    file[129] = 12; file[130] = 241; file[131] = 37; file[132] = 9;
    test_free(R_LoadTexturePCX(file, sizeof(file)));
    T_EQ(upload_format, GL_RGBA);
    T_EQ(upload_pixel.r, 241); T_EQ(upload_pixel.g, 37); T_EQ(upload_pixel.b, 9); T_EQ(upload_pixel.a, 255);
}

TEST(renderer_texture, dds_channel_masks_use_the_common_upload_capabilities) {
    /* DDS header: 124 bytes after magic, 32-bit RGB+alpha, one 1x1 mip. */
    DWORD file[33] = { [0] = MAKEFOURCC('D','D','S',' '), [1] = 124, [3] = 1, [4] = 1, [7] = 1,
        [19] = 32, [20] = 0x41, [22] = 32, [24] = 0xff00, [26] = 0xff000000 };
    ri.MemAlloc = test_alloc; ri.MemFree = test_free;
    FOR_LOOP(bgra, 2) {
        file[23] = bgra ? 0xff0000 : 0xff; file[25] = bgra ? 0xff : 0xff0000;
        COLOR32 pixel = bgra ? (COLOR32){9, 37, 241, 123} : (COLOR32){241, 37, 9, 123};
        memcpy(file + 32, &pixel, sizeof(pixel));
        FOR_LOOP(support, 2) {
            r_bgra_internal = support ? GL_RGBA : 0;
            test_free(R_LoadTextureDDS(file, sizeof(file)));
            T_EQ(upload_format, bgra && support ? BZ_GL_BGRA : GL_RGBA);
            T_EQ(upload_pixel.r, bgra && support ? 9 : 241);
            T_EQ(upload_pixel.b, bgra && support ? 241 : 9);
            T_EQ(upload_pixel.a, 123);
        }
    }
}

TEST(renderer_stats, triangles_include_instanced_amplification) {
    T_EQ(R_PrimitiveTriangles(GL_TRIANGLES, 12, 100), (uint64_t)400);
    T_EQ(R_PrimitiveTriangles(GL_LINES, 12, 100), (uint64_t)0);
}

/* File lookup probes the exact reference first and only substitutes the supported BLP representation. */
static LPCSTR texture_file;
static DWORD texture_reads;
static int test_texture_read(LPCSTR name, void **buffer) {
    texture_reads++;
    *buffer = !strcmp(name, texture_file) ? (void *)&texture_reads : NULL;
    return *buffer ? sizeof(texture_reads) : -1;
}

TEST(renderer_texture, authored_extensions_resolve_without_losing_real_files) {
    static const struct { LPCSTR name, file; DWORD reads; BOOL found; } cases[] = {
        { "White_mask.tga", "White_mask.blp", 2, true },
        { "Cliff0.TGA", "Cliff0.blp", 2, true },
        { "Cliff0.tga", "Cliff0.tga", 1, true },
        { "Tree", "Tree.blp", 2, true },
        { "Tree.blp", "Tree.blp", 1, true },
        { "Missing.tga", "", 2, false },
        { "Missing.blp", "", 1, false },
        { "Missing.dds", "", 1, false },
        { "Missing.pcx", "", 1, false },
        { "Missing", "", 2, false },
    };
    int (*read_file)(LPCSTR, void **) = ri.FS_ReadFile;
    ri.FS_ReadFile = test_texture_read;
    FOR_LOOP(i, sizeof(cases) / sizeof(cases[0])) {
        PATHSTR path; void *buffer = NULL;
        texture_file = cases[i].file; texture_reads = 0;
        T_EQ(R_ReadTextureFile(cases[i].name, path, &buffer) >= 0, cases[i].found);
        T_EQ(texture_reads, cases[i].reads);
        if (cases[i].found) { T_NOT_NULL(buffer); T_STREQ(path, cases[i].file); }
        else T_NULL(buffer);
    }
    ri.FS_ReadFile = read_file;
}

TEST(renderer_terrain, cliff_ramps_require_adjacent_corners_one_level_apart) {
    static const BYTE edge[][2] = { {0,1}, {1,3}, {3,2}, {2,0} };
    WAR3MAPVERTEX tile[4] = {0};
    T_ASSERT(!R_IsCliffRamp(tile));
    FOR_LOOP(i, 4) {
        memset(tile, 0, sizeof(tile));
        tile[edge[i][0]].ramp = tile[edge[i][1]].ramp = 1;
        tile[edge[i][0]].level = tile[edge[i][1]].level = 1;
        T_ASSERT(!R_IsCliffRamp(tile)); /* Human01 HABH had two high corners, not a ramp slope. */
        tile[edge[i][0]].level = 0;
        T_ASSERT(R_IsCliffRamp(tile));
        tile[edge[i][1]].level = 2;
        T_ASSERT(!R_IsCliffRamp(tile)); /* Native transitions contain LH/HX, never LX. */
        tile[edge[i][0]].level = 1;
        T_ASSERT(R_IsCliffRamp(tile));
        tile[edge[i][0]].level = 2; tile[edge[i][1]].level = 1;
        T_ASSERT(R_IsCliffRamp(tile));
    }
    memset(tile, 0, sizeof(tile));
    tile[0].ramp = 1; tile[0].level = 1;
    T_ASSERT(!R_IsCliffRamp(tile));
    tile[3].ramp = 1;
    T_ASSERT(!R_IsCliffRamp(tile)); /* Diagonals are not transition edges. */
    tile[1].ramp = 1;
    T_ASSERT(!R_IsCliffRamp(tile));
    tile[2].ramp = 1;
    T_ASSERT(!R_IsCliffRamp(tile));
}

/* The shadow and non-shadow builds share lighting; only the key's direct contribution is occluded.
   The descriptor always emits the receiver wiring and gates it behind GLSL `#ifdef USE_SHADOWMAPS`,
   so the raw source carries the same body in both builds. */
TEST(renderer_shader, shadow_receiver_contract) {
    R_SetShaderSourceFromDesc(1, &sd_model, true, NULL);
    T_NOT_NULL(strstr(shader_src, "return lighting;")); /* clamp moved out of vertex_lighting */
#ifdef BZ_GLSL_120
    /* GLSL 120 uses varying for both stages; the old test incorrectly required 140+ in/out syntax. */
    T_NOT_NULL(strstr(shader_src, "varying vec3 v_shadowlight;"));
#else
    T_NOT_NULL(strstr(shader_src, "out vec3 v_shadowlight;"));
#endif
    T_NOT_NULL(strstr(shader_src, "v_shadowlight = vec3(0.0);"));
    T_NOT_NULL(strstr(shader_src, "contribution - u_lights[i][3].rgb * u_lights[i][3].a"));

    R_SetShaderSourceFromDesc(1, &sd_model, false, NULL);
    T_NOT_NULL(strstr(shader_src, "light = min(light, vec3(1.0));")); /* clamp applied after occlusion */
#ifdef BZ_GLSL_120
    T_NOT_NULL(strstr(shader_src, "varying vec3 v_shadowlight;"));
#else
    T_NOT_NULL(strstr(shader_src, "in vec3 v_shadowlight;"));
#endif
    T_NOT_NULL(strstr(shader_src, "light -= v_shadowlight * (1.0 - shadow_visibility(u_shadowmap, v_shadow));"));
    T_NOT_NULL(strstr(shader_src, "textureSize(depths, 0)"));
}

/* -----------------------------------------------------------------------
 * shader_desc: UNIFORM/ATTRIB/SHARED macro expansion, R_BuildShaderDeclarations,
 * and R_LoadShaderDescInto for each supported GLSL dialect.
 * ----------------------------------------------------------------------- */

typedef struct SDTESTSTATE {
    MATRIX4 mvp;
    int texture;
    VECTOR4 color;
} SDTESTSTATE;
typedef struct SDTESTSTATE *LPSDTESTSTATE;
typedef const struct SDTESTSTATE *LPCSDTESTSTATE;
typedef struct SDTESTPROG { SHADERPROG prog; SDTESTSTATE state; } SDTESTPROG;
typedef struct SDTESTPROG *LPSDTESTPROG;
typedef const struct SDTESTPROG *LPCSDTESTPROG;

#define SHADER_TYPE SDTESTSTATE
static const shader_desc_t sd_test = {
    .Name = "test",
    .Uniforms = {
        UNIFORM(mvp,     UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(texture, UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(color,   UT_FLOAT_VEC4, PRECISION_LOW),
    },
    .Attributes = {
        ATTRIB(position, attrib_position, UT_FLOAT_VEC3),
        ATTRIB(texcoord, attrib_texcoord, UT_FLOAT_VEC2),
    },
    .Shared = {
        SHARED(texcoord, UT_FLOAT_VEC2),
    },
    .VertexBody =
        "vec4 vert() {\n"
        "  v_texcoord = a_texcoord;\n"
        "  return u_mvp * vec4(a_position, 1.0);\n"
        "}\n",
    .FragmentBody =
        "vec4 frag() {\n"
        "  return texture(u_texture, v_texcoord) * u_color;\n"
        "}\n",
};
#undef SHADER_TYPE

typedef struct { MATRIX4 fixed[2], counted[3]; DWORD count; } SDARRAYTESTSTATE;
#define SHADER_TYPE SDARRAYTESTSTATE
static const shader_desc_t sd_array_test = { .Uniforms = {
    UNIFORM(fixed,   UT_FLOAT_MAT4, PRECISION_HIGH, 2),
    UNIFORM(counted, UT_FLOAT_MAT4, PRECISION_HIGH, 3, count),
} };
#undef SHADER_TYPE

/* UNIFORM(field) stores offsetof(SHADER_TYPE, field); attrib/shared names get a_/v_ prefix. */
TEST(renderer_shader_desc, macro_expansion_records_offsets_and_prefixed_names) {
    T_EQ(sd_test.Uniforms[0].offset, offsetof(SDTESTSTATE, mvp));
    T_EQ(sd_test.Uniforms[1].offset, offsetof(SDTESTSTATE, texture));
    T_EQ(sd_test.Uniforms[2].offset, offsetof(SDTESTSTATE, color));
    T_STREQ(sd_test.Uniforms[0].name, "u_mvp");
    T_STREQ(sd_test.Uniforms[1].name, "u_texture");
    T_STREQ(sd_test.Uniforms[2].name, "u_color");
    T_EQ(sd_array_test.Uniforms[0].count, 2u); T_ASSERT(!sd_array_test.Uniforms[0].counted);
    T_EQ(sd_array_test.Uniforms[1].count, 3u);
    T_EQ(sd_array_test.Uniforms[1].count_offset, offsetof(SDARRAYTESTSTATE, count));
    T_ASSERT(sd_array_test.Uniforms[1].counted);
    T_STREQ(sd_test.Attributes[0].name, "a_position");
    T_STREQ(sd_test.Attributes[1].name, "a_texcoord");
    T_STREQ(sd_test.Shared[0].name, "v_texcoord");
    T_EQ(sd_test.Attributes[0].attrib, attrib_position);
    T_EQ(sd_test.Attributes[1].attrib, attrib_texcoord);
    T_NULL(sd_test.Uniforms[3].name);
    T_NULL(sd_test.Attributes[2].name);
    T_NULL(sd_test.Shared[1].name);
}

/* GLSL 120: attribute/varying keywords, no o_color declaration in FS. */
TEST(renderer_shader_desc, declarations_120_vertex_uses_attribute_and_varying) {
    char buf[1024];
    R_BuildShaderDeclarations(buf, sizeof(buf), &sd_test, true, GLSL_DIALECT_120);
    T_NOT_NULL(strstr(buf, "uniform mat4 u_mvp;\n"));
    T_NOT_NULL(strstr(buf, "uniform sampler2D u_texture;\n"));
    T_NOT_NULL(strstr(buf, "attribute vec3 a_position;\n"));
    T_NOT_NULL(strstr(buf, "attribute vec2 a_texcoord;\n"));
    T_NOT_NULL(strstr(buf, "varying vec2 v_texcoord;\n"));
    T_NULL(strstr(buf, " in "));
    T_NULL(strstr(buf, " out "));
    T_NULL(strstr(buf, "#define texture"));
}

TEST(renderer_shader_desc, declarations_120_fragment_aliases_texture) {
    char buf[1024];
    R_BuildShaderDeclarations(buf, sizeof(buf), &sd_test, false, GLSL_DIALECT_120);
    T_NOT_NULL(strstr(buf, "varying vec2 v_texcoord;\n"));
    T_NOT_NULL(strstr(buf, "#define texture texture2D\n"));
    T_NULL(strstr(buf, "out vec4 o_color"));
    T_NULL(strstr(buf, " in "));
}

/* GLSL 140: in/out keywords, o_color declared in FS. */
TEST(renderer_shader_desc, declarations_140_vertex_uses_in_out) {
    char buf[1024];
    R_BuildShaderDeclarations(buf, sizeof(buf), &sd_test, true, GLSL_DIALECT_140);
    T_NOT_NULL(strstr(buf, "uniform mat4 u_mvp;\n"));
    T_NOT_NULL(strstr(buf, "in vec3 a_position;\n"));
    T_NOT_NULL(strstr(buf, "in vec2 a_texcoord;\n"));
    T_NOT_NULL(strstr(buf, "out vec2 v_texcoord;\n"));
    T_NULL(strstr(buf, "attribute "));
    T_NULL(strstr(buf, "varying "));
}

TEST(renderer_shader_desc, declarations_140_fragment_declares_o_color) {
    char buf[1024];
    R_BuildShaderDeclarations(buf, sizeof(buf), &sd_test, false, GLSL_DIALECT_140);
    T_NOT_NULL(strstr(buf, "in vec2 v_texcoord;\n"));
    T_NOT_NULL(strstr(buf, "out vec4 o_color;\n"));
    T_NULL(strstr(buf, "attribute "));
    T_NULL(strstr(buf, "varying "));
    T_NULL(strstr(buf, "#define texture"));
}

/* GLSL 150 uses the same declaration keywords as 140; only the version line differs. */
TEST(renderer_shader_desc, declarations_150_identical_to_140) {
    char buf140[1024], buf150[1024];
    R_BuildShaderDeclarations(buf140, sizeof(buf140), &sd_test, true,  GLSL_DIALECT_140);
    R_BuildShaderDeclarations(buf150, sizeof(buf150), &sd_test, true,  GLSL_DIALECT_150);
    T_STREQ(buf140, buf150);
    R_BuildShaderDeclarations(buf140, sizeof(buf140), &sd_test, false, GLSL_DIALECT_140);
    R_BuildShaderDeclarations(buf150, sizeof(buf150), &sd_test, false, GLSL_DIALECT_150);
    T_STREQ(buf140, buf150);
}

/* GLES3 uses the same declaration keywords as 140; the precision prologue is in the version string. */
TEST(renderer_shader_desc, declarations_es3_identical_to_140) {
    char buf140[1024], bufES3[1024];
    R_BuildShaderDeclarations(buf140, sizeof(buf140), &sd_test, true, GLSL_DIALECT_140);
    R_BuildShaderDeclarations(bufES3, sizeof(bufES3), &sd_test, true, GLSL_DIALECT_ES3);
    T_STREQ(buf140, bufES3);
    R_BuildShaderDeclarations(buf140, sizeof(buf140), &sd_test, false, GLSL_DIALECT_140);
    R_BuildShaderDeclarations(bufES3, sizeof(bufES3), &sd_test, false, GLSL_DIALECT_ES3);
    T_STREQ(buf140, bufES3);
}

/* main() is generated: bodies define vert()/frag(), never gl_Position/o_color. */
TEST(renderer_shader_desc, main_vertex_assigns_gl_position) {
    char buf[128];
    FOR_LOOP(i, 4) {
        R_BuildShaderMain(buf, sizeof(buf), true, (glsl_dialect_t)i);
        T_STREQ(buf, "void main() { gl_Position = vert(); }\n");
    }
}

TEST(renderer_shader_desc, main_fragment_120_assigns_gl_fragcolor) {
    char buf[128];
    R_BuildShaderMain(buf, sizeof(buf), false, GLSL_DIALECT_120);
    T_STREQ(buf, "void main() { gl_FragColor = frag(); }\n");
}

TEST(renderer_shader_desc, main_fragment_140_assigns_o_color) {
    char buf[128];
    R_BuildShaderMain(buf, sizeof(buf), false, GLSL_DIALECT_140);
    T_STREQ(buf, "void main() { o_color = frag(); }\n");
    R_BuildShaderMain(buf, sizeof(buf), false, GLSL_DIALECT_150);
    T_STREQ(buf, "void main() { o_color = frag(); }\n");
    R_BuildShaderMain(buf, sizeof(buf), false, GLSL_DIALECT_ES3);
    T_STREQ(buf, "void main() { o_color = frag(); }\n");
}

/* Locations stay private to the program and loading preserves caller-owned non-sampler values. */
TEST(renderer_shader_desc, load_writes_locations_and_initializes_samplers) {
    SDTESTPROG shader = { .state.color = { 1, 2, 3, 4 } };
    reset_shader();
    R_LoadShader(&sd_test, NULL, &shader);
    T_EQ(shader.prog.progid, (GLuint)GL_LINK_STATUS);
    FOR_LOOP(i, 3) T_EQ(shader.prog.locs[i], 0);
    T_EQ(shader.state.texture, 0); T_EQ(shader.state.color.w, 4);
    T_EQ(shader_test.creates, 2); T_EQ(shader_test.links, 1); T_EQ(shader_test.deleted, 2);
    T_NOT_NULL(shader.prog.cache);
    R_DeleteShader(&shader.prog);
}

/* The program cache preserves complete typed state while unchanged draw fields issue no driver uploads.
   Zero-valued uniforms (including unit-0 samplers) match the link-time GL default, so they need no first upload. */
TEST(renderer_shader_desc, apply_uploads_only_changed_uniforms) {
    SDTESTPROG shader = { .state.color = { 1, 2, 3, 4 }, .state.mvp = { .v = { 1 } } };
    reset_shader(); R_LoadShader(&sd_test, NULL, &shader); memset(&upload, 0, sizeof(upload));
    int uses = shader_test.uses;
    R_ApplyShader(&shader); T_EQ(upload.calls, 2); T_EQ(shader_test.uses, uses);
    R_ApplyShader(&shader); T_EQ(upload.calls, 2);
    shader.state.color.x = 5; R_ApplyShader(&shader); T_EQ(upload.calls, 3);
    shader.state.mvp.v[0] = 2; R_ApplyShader(&shader); T_EQ(upload.calls, 4);
    R_DeleteShader(&shader.prog);
}

/* Fixed-capacity GLSL arrays upload only the active CPU prefix named by their count field. */
TEST(renderer_shader_desc, counted_array_uses_runtime_upload_count) {
    typedef struct { MATRIX4 values[4]; DWORD count; } TESTCOUNTSTATE;
    TESTCOUNTSTATE state = { .count = 2 };
    shader_desc_t desc = { .Name = "counted", .Uniforms = {{
        .name = "values", .type = UT_FLOAT_MAT4, .count = 4, .count_offset = offsetof(TESTCOUNTSTATE, count), .counted = true,
    }} };
    SHADERPROG prog = { .progid = 1, .desc = &desc, .locs = { 0 } };
    memset(&upload, 0, sizeof(upload)); R_UploadShader(&prog, &state);
    T_EQ(upload.calls, 1); T_EQ(upload.count, 2);
}

/* The descriptor chooses upload shape; arrays stay blocks and bools are not read as GLint storage. */
TEST(renderer_shader_desc, upload_dispatches_values_arrays_and_inactive_inputs) {
    union { float f[32]; int i[32]; bool b; } state = { 0 };
    shader_desc_t desc = { .Name = "upload" };
    SHADERPROG prog = { .progid = 1, .desc = &desc };
    static const int widths[] = { 1, 2, 3, 4, 4, 0, 0, 0, 9, 9, 16, 0, 0, 0 };
    FOR_LOOP(type, UT_COUNT) {
        desc.Uniforms[0] = (shaderUniform_t){ .name = "value", .type = type };
        memset(&upload, 0, sizeof(upload));
        if (widths[type]) {
            FOR_LOOP(i, 32) state.f[i] = i + 1;
            desc.Uniforms[0].count = 2;
        } else if (type == UT_BOOL) state.b = true;
        else state.i[0] = state.i[1] = 7;
        R_UploadShader(&prog, &state);
        T_EQ(upload.calls, 1);
        if (widths[type]) {
            T_EQ(upload.width, widths[type]); T_EQ(upload.count, 2);
            T_EQ(upload.data[widths[type] * 2 - 1], widths[type] * 2);
        } else T_EQ(upload.integer, type == UT_BOOL ? 1 : 7);
    }
    desc.Uniforms[0] = (shaderUniform_t){ .name = "bool", .type = UT_BOOL };
    state.b = false; R_UploadShader(&prog, &state); T_EQ(upload.integer, 0);
    desc.Uniforms[0].type = UT_FLOAT_MAT3_TRANSPOSE;
    R_UploadShader(&prog, &state); T_EQ(upload.transpose, GL_TRUE);
    int calls = upload.calls; prog.locs[0] = -1;
    R_UploadShader(&prog, &state); T_EQ(upload.calls, calls);
}

TEST(renderer_shader_desc, sampler_order_and_program_release) {
    struct { SHADERPROG prog; int state[3]; } shader = { 0 };
    /* A linked descriptor requires shader bodies even when the test only inspects sampler setup. */
    shader_desc_t desc = { .Name = "samplers", .VertexBody = sd_test.VertexBody, .FragmentBody = sd_test.FragmentBody };
    reset_shader();
    FOR_LOOP(i, 3) desc.Uniforms[i] = (shaderUniform_t){ .offset = i * sizeof(int), .name = "sampler", .type = UT_SAMPLER_2D };
    R_LoadShader(&desc, NULL, &shader);
    FOR_LOOP(i, 3) T_EQ(shader.state[i], i);
    int deleted = deleted_programs;
    R_DeleteShader(&shader.prog); T_EQ(deleted_programs, deleted + 1);
    T_EQ(shader.prog.progid, 0); T_NULL(shader.prog.desc);
    R_DeleteShader(&shader.prog); T_EQ(deleted_programs, deleted + 1);
}

/* Packed array batches preserve their non-zero first vertex and skip empty ranges. */
TEST(renderer_buffer, array_range_uses_first_and_count) {
    BUFFER buffer = { .vao = 3, .vbo = 4 };
    DRAWRANGE draw = { .first = 17, .count = 12 };
    memset(&draw_test, 0, sizeof(draw_test));
    R_DrawBufferRange(&buffer, &draw);
    T_EQ(draw_test.calls, 1); T_EQ(draw_test.first, 17); T_EQ(draw_test.count, 12); T_EQ(draw_test.instances, 1);
    T_EQ(draw_test.stats_count, 12); T_EQ(draw_test.stats_instances, 1);
    draw.count = 0; R_DrawBufferRange(&buffer, &draw); T_EQ(draw_test.calls, 1);
}

/* Instanced packed batches use the same range and reject an empty instance stream. */
TEST(renderer_buffer, instanced_array_range_uses_first_count_and_instances) {
    BUFFER buffer = { .vbo = 4 };
    INSTANCEBUFFER instances = { .vbo = 5, .count = 9 };
    DRAWRANGE draw = { .first = 23, .count = 18 };
    memset(&draw_test, 0, sizeof(draw_test));
    R_DrawBufferRangeInstanced(&buffer, &draw, &instances);
    T_EQ(draw_test.calls, 1); T_EQ(draw_test.first, 23); T_EQ(draw_test.count, 18); T_EQ(draw_test.instances, 9);
    T_EQ(draw_test.stats_count, 18); T_EQ(draw_test.stats_instances, 9);
    instances.count = 0; R_DrawBufferRangeInstanced(&buffer, &draw, &instances); T_EQ(draw_test.calls, 1);
}
