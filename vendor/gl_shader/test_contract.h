#ifndef __GL_SHADER_TEST_CONTRACT_H__
#define __GL_SHADER_TEST_CONTRACT_H__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_shader.h"

static struct {
  GLuint next, fail_shader, bound;
  int fail_link, allocations, fail_alloc, deleted_stages, deleted_programs, uses, uploads;
  GLsizei count;
  GLboolean transpose;
  float floats[128];
  int ints[32];
  char source[16384];
} gs_mock;
static void *gs_mock_alloc(size_t size) {
  if (gs_mock.fail_alloc && ++gs_mock.allocations == gs_mock.fail_alloc) return NULL;
  return malloc(size);
}
static GLuint gs_mock_shader(GLenum type) { (void)type; return ++gs_mock.next; }
static GLuint gs_mock_program(void) { return ++gs_mock.next; }
static void gs_mock_source(GLuint id, GLsizei count, const GLchar *const *strings, const GLint *lengths) {
  (void)id; (void)lengths;
  gs_mock.source[0] = 0;
  for (int i = 0; i < count; i++) strcat(gs_mock.source, strings[i]);
}
static void gs_mock_compile(GLuint id) { (void)id; }
static void gs_mock_attach(GLuint id, GLuint stage) { (void)id; (void)stage; }
static void gs_mock_attrib(GLuint id, GLuint loc, const GLchar *name) { (void)id; (void)loc; (void)name; }
static void gs_mock_link(GLuint id) { (void)id; }
static void gs_mock_shader_status(GLuint id, GLenum query, GLint *value) {
  *value = query == GL_INFO_LOG_LENGTH ? 32 : id != gs_mock.fail_shader;
}
static void gs_mock_program_status(GLuint id, GLenum query, GLint *value) {
  (void)id; *value = query == GL_INFO_LOG_LENGTH ? 32 : !gs_mock.fail_link;
}
static void gs_mock_log(GLuint id, GLsizei size, GLsizei *length, GLchar *log) {
  (void)id; (void)length; snprintf(log, size, "driver rejected shader");
}
static void gs_mock_delete_stage(GLuint id) { (void)id; gs_mock.deleted_stages++; }
static void gs_mock_delete_program(GLuint id) { (void)id; gs_mock.deleted_programs++; }
static void gs_mock_use(GLuint id) { gs_mock.bound = id; gs_mock.uses++; }
static GLint gs_mock_location(GLuint id, const GLchar *name) { (void)id; return !strcmp(name, "unused") ? -1 : 0; }
static void gs_mock_float(int width, GLsizei count, const GLfloat *values) {
  gs_mock.uploads++; gs_mock.count = count; memcpy(gs_mock.floats, values, width * count * sizeof(float));
}
static void gs_mock_float1(GLint loc, GLsizei n, const GLfloat *v) { (void)loc; gs_mock_float(1, n, v); }
static void gs_mock_float2(GLint loc, GLsizei n, const GLfloat *v) { (void)loc; gs_mock_float(2, n, v); }
static void gs_mock_float3(GLint loc, GLsizei n, const GLfloat *v) { (void)loc; gs_mock_float(3, n, v); }
static void gs_mock_float4(GLint loc, GLsizei n, const GLfloat *v) { (void)loc; gs_mock_float(4, n, v); }
static void gs_mock_int1(GLint loc, GLsizei n, const GLint *v) {
  (void)loc; gs_mock.uploads++; gs_mock.count = n; memcpy(gs_mock.ints, v, n * sizeof(int));
}
static void gs_mock_int2(GLint loc, GLsizei n, const GLint *v) { gs_mock_int1(loc, 2 * n, v); }
static void gs_mock_mat3(GLint loc, GLsizei n, GLboolean transpose, const GLfloat *v) {
  (void)loc; gs_mock.transpose = transpose; gs_mock_float(9, n, v);
}
static void gs_mock_mat4(GLint loc, GLsizei n, GLboolean transpose, const GLfloat *v) {
  (void)loc; gs_mock.transpose = transpose; gs_mock_float(16, n, v);
}
#define glCreateShader gs_mock_shader
#define glCreateProgram gs_mock_program
#define glShaderSource gs_mock_source
#define glCompileShader gs_mock_compile
#define glAttachShader gs_mock_attach
#define glBindAttribLocation gs_mock_attrib
#define glLinkProgram gs_mock_link
#define glGetShaderiv gs_mock_shader_status
#define glGetProgramiv gs_mock_program_status
#define glGetShaderInfoLog gs_mock_log
#define glGetProgramInfoLog gs_mock_log
#define glDeleteShader gs_mock_delete_stage
#define glDeleteProgram gs_mock_delete_program
#define glUseProgram gs_mock_use
#define glGetUniformLocation gs_mock_location
#define glUniform1fv gs_mock_float1
#define glUniform2fv gs_mock_float2
#define glUniform3fv gs_mock_float3
#define glUniform4fv gs_mock_float4
#define glUniform1iv gs_mock_int1
#define glUniform2iv gs_mock_int2
#define glUniformMatrix3fv gs_mock_mat3
#define glUniformMatrix4fv gs_mock_mat4
#include "gl_shader.c"
#undef glCreateShader
#undef glCreateProgram
#undef glShaderSource
#undef glCompileShader
#undef glAttachShader
#undef glBindAttribLocation
#undef glLinkProgram
#undef glGetShaderiv
#undef glGetProgramiv
#undef glGetShaderInfoLog
#undef glGetProgramInfoLog
#undef glDeleteShader
#undef glDeleteProgram
#undef glUseProgram
#undef glGetUniformLocation
#undef glUniform1fv
#undef glUniform2fv
#undef glUniform3fv
#undef glUniform4fv
#undef glUniform1iv
#undef glUniform2iv
#undef glUniformMatrix3fv
#undef glUniformMatrix4fv

#define GS_CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); return false; } } while (0)
static bool gs_test_contract(void) {
  memset(&gs_mock, 0, sizeof(gs_mock));
  shader_desc_t desc = {.Name = "contract", .Attributes = {{"position", 0, UT_FLOAT_VEC2}},
    .Shared = {{"uv", UT_FLOAT_VEC2}}, .Uniforms = {{.name = "value", .type = UT_FLOAT}},
    .VertexBody = "vec4 vert() { return vec4(position, 0.0, 1.0); }",
    .FragmentBody = "vec4 frag() { return vec4(value); }"};
  char buf[2048];
  for (int d = GLSL_DIALECT_120; d <= GLSL_DIALECT_ES3; d++) {
    int n = gs_declarations(NULL, 0, &desc, false, (glsl_dialect_t)d);
    GS_CHECK(n > 0 && gs_declarations(buf, sizeof(buf), &desc, false, (glsl_dialect_t)d) == n);
    GS_CHECK(strstr(buf, d == GLSL_DIALECT_120 ? "varying vec2 uv;" : "in vec2 uv;"));
    GS_CHECK(gs_declarations(buf, sizeof(buf), &desc, true, (glsl_dialect_t)d) > 0);
    GS_CHECK(strstr(buf, d == GLSL_DIALECT_120 ? "attribute vec2 position;" : "in vec2 position;"));
    GS_CHECK(gs_main(buf, sizeof(buf), false, (glsl_dialect_t)d) > 0);
    GS_CHECK(strstr(buf, d == GLSL_DIALECT_120 ? "gl_FragColor = frag()" : "o_color = frag()"));
    gs_options_t options = {.dialect = (glsl_dialect_t)d};
    GS_CHECK(gs_source(1, &desc, true, &options));
    GS_CHECK(strstr(gs_mock.source, "#version") && strstr(gs_mock.source, "gl_Position = vert()"));
    if (d == GLSL_DIALECT_ES3) GS_CHECK(strstr(gs_mock.source, "precision highp float;"));
  }
  struct { char text[4]; char guard; } small = {{0}, 'Q'};
  GS_CHECK(gs_declarations(small.text, sizeof(small.text), &desc, true, GLSL_DIALECT_150) == -1);
  GS_CHECK(small.guard == 'Q' && !small.text[0]);
  GS_CHECK(gs_main(small.text, sizeof(small.text), true, GLSL_DIALECT_150) == -1);
  GS_CHECK(gs_declarations(buf, sizeof(buf), &desc, true, (glsl_dialect_t)-1) == -1);
  desc.Uniforms[0].type = UT_SAMPLER_2D_RECT;
  GS_CHECK(gs_declarations(buf, sizeof(buf), &desc, false, GLSL_DIALECT_ES3) == -1);
  desc.Uniforms[0].type = UT_SAMPLER_2D_ARRAY;
  GS_CHECK(gs_declarations(buf, sizeof(buf), &desc, false, GLSL_DIALECT_120) == -1);
  desc.Uniforms[0].type = UT_FLOAT;
  desc.Uniforms[0].count = 1;
  GS_CHECK(gs_declarations(buf, sizeof(buf), &desc, false, GLSL_DIALECT_150) > 0 && strstr(buf, "value[1]"));
  desc.Uniforms[0].count = 0;

  gs_options_t options = {.dialect = GLSL_DIALECT_ES3, .memory = {gs_mock_alloc, free}};
  shaderProg_t prog = {0};
  float state = 0;
  GS_CHECK(gs_load(&prog, &desc, &state, sizeof(state), &options));
  GS_CHECK(gs_apply(&prog, &state) && gs_mock.uploads == 1);
  gs_mock.bound = 999;
  GS_CHECK(gs_apply(&prog, &state) && gs_mock.uploads == 1 && gs_mock.bound == prog.progid);
  state = 2;
  GS_CHECK(gs_apply(&prog, &state) && gs_mock.uploads == 2 && gs_mock.floats[0] == 2);
  gs_invalidate(&prog);
  GS_CHECK(gs_apply(&prog, &state) && gs_mock.uploads == 3);
  gs_delete(&prog);
  GS_CHECK(!prog.progid && !prog.cache);
  int deleted = gs_mock.deleted_programs;
  gs_delete(&prog); GS_CHECK(gs_mock.deleted_programs == deleted);
  desc.Uniforms[0].offset = sizeof(state);
  GS_CHECK(!gs_load(&prog, &desc, &state, sizeof(state), &options) && !prog.progid);
  desc.Uniforms[0].offset = 0;
  for (int failure = 1; failure <= 4; failure++) {
    memset(&gs_mock, 0, sizeof(gs_mock));
    if (failure < 3) gs_mock.fail_shader = (GLuint)failure;
    if (failure == 3) gs_mock.fail_link = 1;
    if (failure == 4) gs_mock.fail_alloc = 1;
    GS_CHECK(!gs_load(&prog, &desc, &state, sizeof(state), &options));
    GS_CHECK(!prog.progid && !prog.cache && !gs_mock.uses);
    if (failure < 4) GS_CHECK(gs_mock.deleted_stages == 2);
    if (failure == 3) GS_CHECK(gs_mock.deleted_programs == 1);
  }

  typedef struct { float matrices[2][9]; uint32_t count; } array_state_t;
  array_state_t arrays = {.count = 1};
  for (int i = 0; i < 18; i++) ((float *)arrays.matrices)[i] = (float)i;
  desc.Uniforms[0] = (shaderUniform_t){.name = "matrices", .type = UT_FLOAT_MAT3_TRANSPOSE,
    .count = 2, .count_offset = offsetof(array_state_t, count), .counted = true};
  memset(&gs_mock, 0, sizeof(gs_mock));
  GS_CHECK(gs_load(&prog, &desc, &arrays, sizeof(arrays), &options));
  GS_CHECK(gs_apply(&prog, &arrays) && gs_mock.count == 1 && gs_mock.transpose == GL_FALSE);
  GS_CHECK(gs_mock.floats[1] == 3 && gs_mock.floats[3] == 1);
  arrays.count = 2;
  GS_CHECK(gs_apply(&prog, &arrays) && gs_mock.count == 2 && gs_mock.uploads == 2);
  arrays.count = 1; GS_CHECK(gs_apply(&prog, &arrays) && gs_mock.uploads == 3);
  arrays.count = 2; GS_CHECK(gs_apply(&prog, &arrays) && gs_mock.uploads == 4);
  int uses = gs_mock.uses;
  arrays.count = 0; GS_CHECK(!gs_apply(&prog, &arrays) && gs_mock.uses == uses);
  arrays.count = 3; GS_CHECK(!gs_apply(&prog, &arrays) && gs_mock.uses == uses);
  gs_delete(&prog);

  bool flags[2] = {true, false};
  desc.Uniforms[0] = (shaderUniform_t){.name = "flags", .type = UT_BOOL, .count = 2};
  GS_CHECK(gs_load(&prog, &desc, flags, sizeof(flags), &options));
  GS_CHECK(gs_apply(&prog, flags) && gs_mock.ints[0] == 1 && gs_mock.ints[1] == 0);
  gs_delete(&prog);
  int samplers[2] = {-1, -1};
  desc.Uniforms[0] = (shaderUniform_t){.name = "samplers", .type = UT_SAMPLER_2D, .count = 2};
  GS_CHECK(gs_load(&prog, &desc, samplers, sizeof(samplers), &options));
  GS_CHECK(samplers[0] == 0 && samplers[1] == 1);
  gs_delete(&prog);

  const char *vs = "  #version 150\nvoid main() { gl_Position = vec4(0.0); }";
  const char *fs = "#version 150\nout vec4 color;\nvoid main() { color = vec4(1.0); }";
  memset(&gs_mock, 0, sizeof(gs_mock));
  GLuint raw = gs_link_sources(vs, fs, NULL, 0, &options);
  GS_CHECK(raw && gs_mock.deleted_stages == 2 && strstr(gs_mock.source, "#version 300 es"));
  GS_CHECK(!strstr(gs_mock.source, "#version 150"));
  gs_mock_delete_program(raw);
  gs_mock.fail_link = 1;
  GS_CHECK(!gs_link_sources(vs, fs, NULL, 0, &options) && gs_mock.deleted_programs == 2);
  return true;
}
#undef GS_CHECK
#endif
