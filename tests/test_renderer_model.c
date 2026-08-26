#include "test.h"
#include "renderer/r_local.h"
#include "renderer/r_emit.h"
#include "games/warcraft-3/renderer/w3m/r_war3map.h"
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
static void BZ_TestUniform1i(GLint loc, GLint val) { (void)loc; (void)val; }
static void BZ_TestUniform1f(GLint loc, GLfloat val) { (void)loc; (void)val; }
static void BZ_TestUniformMatrix3(GLint loc, GLsizei count, GLboolean transpose, const GLfloat *val) {
    (void)loc; (void)count; (void)transpose; (void)val;
}
static void BZ_TestUniformMatrix4(GLint loc, GLsizei count, GLboolean transpose, const GLfloat *val) {
    (void)loc; (void)transpose;
    shader_test.uploads++;
    T_EQ(count, BZ_BONE_PALETTE_MAX);
    FOR_LOOP(i, count * 16) T_EQ(val[i], i % 16 % 5 == 0 ? 1.0f : 0.0f);
}
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
#define glUniform1f BZ_TestUniform1f
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
#undef glUniform1f
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
#undef glTexImage2D
#undef glGenTextures
#undef glBindTexture
#undef glTexParameteri
#define glTexImage2D test_teximage
#define glGenTextures test_gentex
#define glBindTexture test_bindtex
#define glTexParameteri test_texparam
#undef glGetString
#define glGetString test_glstring
#define SDL_GL_ExtensionSupported test_hasext
#include "renderer/r_texture.c"
#include "renderer/r_blp1.c"
#include "renderer/r_blp2.c"
#include "renderer/r_pcx.c"
#include "renderer/r_dds.c"

static HANDLE test_alloc(long size) { alloc_count++; return calloc(1, (size_t)size); }
static void test_free(HANDLE memory) { free_count++; free(memory); }
static void test_error(LPCSTR format, ...) { (void)format; T_ASSERT(false); }
static void test_spawn(void *context) { (*(DWORD *)context)++; }

LPTEXTURE R_LoadTexture(LPCSTR filename) { (void)filename; return texture_load_result; }

LPMODEL R_GameLoadModel(LPCSTR filename) {
    (void)filename; load_count++;
    return fail_load ? NULL : test_alloc(sizeof(model_t));
}

void R_GameReleaseModel(LPMODEL model) { release_count++; test_free(model); }

void R_GameRegisterMap(LPCSTR map) {
    (void)map; register_count++;
    if (touch_during_registration) R_LoadModel("models/touched.mdx");
}

static void reset_registry(void) {
    R_ShutdownModels();
    ri.MemAlloc = test_alloc; ri.MemFree = test_free; ri.error = test_error;
    load_count = release_count = register_count = 0;
    fail_load = touch_during_registration = false;
}

TEST(renderer_model, filename_cache_hit_and_miss) {
    LPMODEL first, second;
    reset_registry();
    first = R_LoadModel("Models/Foo.mdx");
    second = R_LoadModel("models/foo.mdx");
    T_ASSERT(first == second); T_EQ(load_count, 1); T_EQ(release_count, 0);
    R_ReleaseModel(first); R_ReleaseModel(second);
    R_RegisterMapAssets("next");
    T_EQ(register_count, 1); T_EQ(release_count, 1);
}

TEST(renderer_model, registration_keeps_touched_model_then_reclaims_it) {
    LPMODEL model;
    reset_registry();
    model = R_LoadModel("models/touched.mdx"); R_ReleaseModel(model);
    touch_during_registration = true; R_RegisterMapAssets("current");
    T_EQ(load_count, 1); T_EQ(release_count, 0);
    R_ReleaseModel(model); touch_during_registration = false; R_RegisterMapAssets("next");
    T_EQ(release_count, 1);
}

TEST(renderer_model, missing_model_placeholder_is_cached) {
    LPMODEL first, second;
    reset_registry(); fail_load = true;
    first = R_LoadModel("models/missing.mdx"); second = R_LoadModel("models/missing.mdx");
    T_ASSERT(first == second); T_EQ(load_count, 1);
    R_ReleaseModel(first); R_ReleaseModel(second); R_RegisterMapAssets("next");
    T_EQ(release_count, 1);
}

TEST(renderer_model, unknown_model_release_is_immediate) {
    LPMODEL model;
    reset_registry(); model = test_alloc(sizeof(*model));
    R_ReleaseModel(model); T_EQ(release_count, 1);
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
    R_SetShaderSource(1, model_vs, NULL);
    T_NOT_NULL(strstr(shader_src, "uniform mat4 uBones[128];"));
    T_NULL(strstr(shader_src, "BZ_BONE_COUNT"));
    /* Slot 83 must stay 83: the old clamp redirected it to 63 with a 64-matrix palette. */
    T_NOT_NULL(strstr(shader_src, "int boneIdx = int(i_skin1[i]) + int(uFirstBoneLookupIndex);"));
    T_NULL(strstr(shader_src, "#define BZ_USE_INSTANCING"));
}

TEST(renderer_bones, instanced_shader_uses_the_same_palette_contract) {
    R_SetShaderSource(1, model_vs, "#define BZ_USE_INSTANCING 1\n");
    T_NOT_NULL(strstr(shader_src, "#define BZ_USE_INSTANCING 1\n"));
    T_NOT_NULL(strstr(shader_src, "uniform mat4 uBones[128];"));
    T_NOT_NULL(strstr(shader_src, "int boneIdx = int(i_skin1[i]) + int(uFirstBoneLookupIndex);"));
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
    LPSHADER shader = R_ModelShader();
    T_NOT_NULL(shader); T_ASSERT(R_ModelShader() == shader);
    T_EQ(shader_test.creates, 2); T_EQ(shader_test.links, 1); T_EQ(shader_test.deleted, 2);
    T_EQ(shader_test.exitcode, 0); T_EQ(shader_test.logs, 0);
    R_ShutdownModelShader();
}

TEST(renderer_shader, instanced_cache_uploads_full_identity_palette_once) {
    reset_shader();
    LPSHADER shader = R_ModelShaderInstanced();
    T_NOT_NULL(shader); T_ASSERT(R_ModelShaderInstanced() == shader);
    T_EQ(shader_test.creates, 2); T_EQ(shader_test.links, 1); T_EQ(shader_test.uploads, 1);
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
        T_EQ(shader_test.logs, 1); T_NULL(model_shader);
        free(shader_test.memory);
    }
}

TEST(renderer_shader, instanced_link_failure_is_fatal_without_palette_upload) {
    reset_shader(); shader_test.fail = GL_LINK_STATUS;
    if (!setjmp(shader_exit)) {
        R_ModelShaderInstanced(); T_ASSERT(false);
    }
    T_EQ(shader_test.exitcode, EXIT_FAILURE); T_EQ(shader_test.uses, 0); T_EQ(shader_test.uploads, 0);
    T_NULL(instanced_shader); free(shader_test.memory);
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

/* The shadow and non-shadow builds share lighting; only the key's direct contribution is occluded. */
TEST(renderer_shader, shadow_receiver_contract) {
    R_SetShaderSource(1, model_vs, NULL);
    T_NOT_NULL(strstr(shader_src, "return lighting;"));
#ifdef USE_SHADOWMAPS
    T_NOT_NULL(strstr(shader_src, "out vec3 v_shadowlight;"));
    T_NOT_NULL(strstr(shader_src, "contribution - uLights[i][3].rgb * uLights[i][3].a"));
#else
    T_NULL(strstr(shader_src, "v_shadowlight"));
#endif
    R_SetShaderSource(1, model_fs, NULL);
    T_NOT_NULL(strstr(shader_src, "light = min(light, vec3(1.0))"));
#ifdef USE_SHADOWMAPS
    T_NOT_NULL(strstr(shader_src, "in vec3 v_shadowlight;"));
    T_NOT_NULL(strstr(shader_src, "light -= v_shadowlight * (1.0 - shadow_visibility"));
    T_NOT_NULL(strstr(shader_src, "textureSize(depths, 0)"));
#else
    T_NULL(strstr(shader_src, "shadow_visibility"));
#endif
}
