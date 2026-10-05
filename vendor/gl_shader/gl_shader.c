#include "gl_shader.h"
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const gs_types[] = {
  "float", "vec2", "vec3", "vec4", "vec4", "int", "ivec2", "bool", "mat3", "mat3", "mat4",
  "sampler2D", "sampler2DRect", "sampler2DArray"
};
static const char *const gs_versions[] = { "#version 120\n", "#version 140\n", "#version 150\n",
  "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision lowp sampler2D;\nprecision lowp sampler2DArray;\n" };

static bool gs_error(const char *name, const char *format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(stderr, "[gl_shader] %s: ", name ? name : "unnamed");
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  fflush(stderr);
  return false;
}
static bool gs_dialect(glsl_dialect_t dialect) { return dialect >= GLSL_DIALECT_120 && dialect <= GLSL_DIALECT_ES3; }
static gs_allocator_t gs_memory(const gs_options_t *options) {
  return options && options->memory.alloc ? options->memory : (gs_allocator_t){malloc, free};
}
static bool gs_options(const gs_options_t *options) {
  return options && gs_dialect(options->dialect) && !!options->memory.alloc == !!options->memory.free;
}
static bool gs_type(uniformType_t type, glsl_dialect_t dialect) {
  return type >= UT_FLOAT && type < UT_COUNT && !(dialect == GLSL_DIALECT_ES3 && type == UT_SAMPLER_2D_RECT) &&
         !(dialect == GLSL_DIALECT_120 && type == UT_SAMPLER_2D_ARRAY);
}

/* A sizing pass and a bounded writing pass share the same declaration grammar. */
static bool gs_append(char *buf, size_t size, size_t *used, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int n = vsnprintf(buf ? buf + *used : NULL, buf ? size - *used : 0, format, args);
  va_end(args);
  if (n < 0 || (buf && (size_t)n >= size - *used) || (size_t)n > INT_MAX - *used) return false;
  *used += (size_t)n;
  return true;
}
int gs_declarations(char *buf, size_t size, const shader_desc_t *desc, bool vertex, glsl_dialect_t dialect) {
  if (!desc || !gs_dialect(dialect) || (buf && !size)) { gs_error(NULL, "invalid declaration request"); return -1; }
  size_t n = 0;
  if (buf) buf[0] = 0;
  bool legacy = dialect == GLSL_DIALECT_120;
  if (!vertex && legacy && !gs_append(buf, size, &n, "#define texture texture2D\n")) goto fail;
  for (size_t i = 0; i < MAX_SHADER_UNIFORMS && desc->Uniforms[i].name; i++) {
    const shaderUniform_t *u = &desc->Uniforms[i];
    if (!gs_type(u->type, dialect) || u->count > INT_MAX || u->precision > PRECISION_DEFAULT) goto fail;
    if (u->count) {
      if (!gs_append(buf, size, &n, "uniform %s %s[%u];\n", gs_types[u->type], u->name, u->count)) goto fail;
    } else if (!gs_append(buf, size, &n, "uniform %s %s;\n", gs_types[u->type], u->name)) goto fail;
  }
  if (vertex) for (size_t i = 0; i < MAX_SHADER_ATTRIBS && desc->Attributes[i].name; i++) {
    const shaderAttrib_t *a = &desc->Attributes[i];
    if (a->type < UT_FLOAT || a->type > UT_FLOAT_MAT4 || a->type == UT_BOOL ||
        !gs_append(buf, size, &n, "%s %s %s;\n", legacy ? "attribute" : "in", gs_types[a->type], a->name)) goto fail;
  }
  for (size_t i = 0; i < MAX_SHADER_SHARED && desc->Shared[i].name; i++) {
    const shaderVarying_t *v = &desc->Shared[i];
    if (v->type < UT_FLOAT || v->type > UT_COLOR ||
        !gs_append(buf, size, &n, "%s %s %s;\n", legacy ? "varying" : vertex ? "out" : "in", gs_types[v->type], v->name)) goto fail;
  }
  if (!vertex && !legacy && !gs_append(buf, size, &n, "out vec4 o_color;\n")) goto fail;
  return (int)n;
fail:
  if (buf) buf[0] = 0;
  gs_error(desc->Name, "invalid interface or declaration buffer too small");
  return -1;
}
int gs_main(char *buf, size_t size, bool vertex, glsl_dialect_t dialect) {
  size_t n = 0;
  if (!gs_dialect(dialect) || (buf && !size) ||
      !gs_append(buf, size, &n, "void main() { %s = %s(); }\n",
                 vertex ? "gl_Position" : dialect == GLSL_DIALECT_120 ? "gl_FragColor" : "o_color", vertex ? "vert" : "frag")) {
    if (buf && size) buf[0] = 0;
    gs_error(NULL, "invalid main wrapper request");
    return -1;
  }
  return (int)n;
}
size_t gs_type_size(uniformType_t type) {
  static const uint8_t widths[] = {1, 2, 3, 4, 4, 1, 2, 1, 9, 9, 16, 1, 1, 1};
  if (type < UT_FLOAT || type >= UT_COUNT) return 0;
  if (type == UT_BOOL) return sizeof(bool);
  if ((type >= UT_INT && type <= UT_INT_VEC2) || type >= UT_SAMPLER_2D) return widths[type] * sizeof(GLint);
  return widths[type] * sizeof(GLfloat);
}

bool gs_source(GLuint stage, const shader_desc_t *desc, bool vertex, const gs_options_t *options) {
  if (!stage || !desc || !gs_options(options) || !(vertex ? desc->VertexBody : desc->FragmentBody))
    return gs_error(desc ? desc->Name : NULL, "invalid shader source request");
  int n = gs_declarations(NULL, 0, desc, vertex, options->dialect);
  if (n < 0) return false;
  gs_allocator_t memory = gs_memory(options);
  char *decls = memory.alloc((size_t)n + 1);
  if (!decls) return gs_error(desc->Name, "declaration allocation failed");
  char wrapper[128];
  bool ok = gs_declarations(decls, (size_t)n + 1, desc, vertex, options->dialect) >= 0 &&
            gs_main(wrapper, sizeof(wrapper), vertex, options->dialect) >= 0;
  if (ok) {
    const char *strings[] = {gs_versions[options->dialect], options->defines ? options->defines : "", decls,
                            vertex ? desc->VertexBody : desc->FragmentBody, "\n", wrapper};
    glShaderSource(stage, 6, strings, NULL);
  }
  memory.free(decls);
  return ok;
}
static bool gs_check(GLuint obj, bool program, const char *name, gs_allocator_t memory) {
  GLint ok = 0, size = 0;
  if (program) glGetProgramiv(obj, GL_LINK_STATUS, &ok);
  else glGetShaderiv(obj, GL_COMPILE_STATUS, &ok);
  if (ok) return true;
  if (program) glGetProgramiv(obj, GL_INFO_LOG_LENGTH, &size);
  else glGetShaderiv(obj, GL_INFO_LOG_LENGTH, &size);
  char *log = size > 1 ? memory.alloc((size_t)size) : NULL;
  if (log) {
    log[0] = 0;
    if (program) glGetProgramInfoLog(obj, size, NULL, log);
    else glGetShaderInfoLog(obj, size, NULL, log);
    log[size - 1] = 0;
  }
  fprintf(stderr, "[gl_shader] %s: %s failed: %s\n", name ? name : "unnamed", program ? "link" : "compile",
          log ? log : size > 1 ? "cannot allocate driver log" : "no driver log");
  fflush(stderr);
  if (log) memory.free(log);
  return false;
}
static GLuint gs_link(GLuint vs, GLuint fs, const shaderAttrib_t *attrs, size_t count, const char *name, gs_allocator_t memory) {
  GLuint id = glCreateProgram();
  if (!id) { gs_error(name, "program allocation failed"); return 0; }
  for (size_t i = 0; i < count && attrs[i].name; i++) glBindAttribLocation(id, attrs[i].attrib, attrs[i].name);
  glAttachShader(id, vs);
  glAttachShader(id, fs);
  glLinkProgram(id);
  if (!gs_check(id, true, name, memory)) { glDeleteProgram(id); return 0; }
  return id;
}
static bool gs_range(size_t offset, size_t width, size_t count, size_t size) {
  return width && offset <= size && count <= (size - offset) / width;
}

/* Validate CPU storage before compiling or changing caller-owned sampler slots. */
bool gs_load(shaderProg_t *prog, const shader_desc_t *desc, void *state, size_t state_size, const gs_options_t *options) {
  if (!prog || prog->progid || prog->cache || !desc || !state || !state_size || !gs_options(options))
    return gs_error(desc ? desc->Name : NULL, "invalid program load request");
  size_t scratch_size = 0;
  for (size_t i = 0; i < MAX_SHADER_UNIFORMS && desc->Uniforms[i].name; i++) {
    const shaderUniform_t *u = &desc->Uniforms[i];
    uint32_t count = u->count ? u->count : 1;
    if (!gs_type(u->type, options->dialect) || count > INT_MAX ||
        !gs_range(u->offset, gs_type_size(u->type), count, state_size) ||
        (u->counted && (!u->count || !gs_range(u->count_offset, sizeof(uint32_t), 1, state_size))))
      return gs_error(desc->Name, "uniform=%s type=%d offset=%zu count=%u count_offset=%zu state_size=%zu rejected",
                      u->name, u->type, u->offset, count, u->count_offset, state_size);
    size_t converted = u->type == UT_BOOL ? sizeof(GLint) : u->type == UT_FLOAT_MAT3_TRANSPOSE ? 9 * sizeof(GLfloat) : 0;
    if (converted && count > SIZE_MAX / converted) return gs_error(desc->Name, "scratch size overflow");
    if (converted * count > scratch_size) scratch_size = converted * count;
  }
  shaderProg_t next = {.desc = desc, .state_size = state_size, .memory = gs_memory(options)};
  next.cache = next.memory.alloc(state_size);
  if (scratch_size) next.scratch = next.memory.alloc(scratch_size);
  if (!next.cache || (scratch_size && !next.scratch)) {
    gs_error(desc->Name, "uniform storage allocation failed");
    gs_delete(&next);
    return false;
  }
  GLuint vs = glCreateShader(GL_VERTEX_SHADER), fs = glCreateShader(GL_FRAGMENT_SHADER);
  if (!vs || !fs) gs_error(desc->Name, "shader allocation failed");
  bool ok = vs && fs && gs_source(vs, desc, true, options);
  if (ok) { glCompileShader(vs); ok = gs_check(vs, false, desc->Name, next.memory); }
  if (ok) ok = gs_source(fs, desc, false, options);
  if (ok) { glCompileShader(fs); ok = gs_check(fs, false, desc->Name, next.memory); }
  if (ok) next.progid = gs_link(vs, fs, desc->Attributes, MAX_SHADER_ATTRIBS, desc->Name, next.memory);
  if (vs) glDeleteShader(vs);
  if (fs) glDeleteShader(fs);
  if (!next.progid) { gs_delete(&next); return false; }
  int unit = 0;
  for (size_t i = 0; i < MAX_SHADER_UNIFORMS && desc->Uniforms[i].name; i++) {
    const shaderUniform_t *u = &desc->Uniforms[i];
    next.locs[i] = glGetUniformLocation(next.progid, u->name);
    if (u->type >= UT_SAMPLER_2D) for (uint32_t j = 0; j < (u->count ? u->count : 1); j++) {
      memcpy((char *)state + u->offset + j * sizeof(GLint), &unit, sizeof(unit));
      unit++;
    }
  }
  *prog = next;
  return true;
}

/* Rebind on every submission: GL state belongs to a context and may be changed by other renderers. */
bool gs_apply(shaderProg_t *prog, const void *state) {
  if (!prog || !prog->progid || !prog->desc || !state || !prog->state_size)
    return gs_error(NULL, "unavailable program or state");
  uint32_t counts[MAX_SHADER_UNIFORMS] = {0};
  for (size_t i = 0; i < MAX_SHADER_UNIFORMS && prog->desc->Uniforms[i].name; i++) {
    const shaderUniform_t *u = &prog->desc->Uniforms[i];
    counts[i] = u->count ? u->count : 1;
    if (u->counted) memcpy(&counts[i], (const char *)state + u->count_offset, sizeof(uint32_t));
    if (!counts[i] || counts[i] > (u->count ? u->count : 1) ||
        !gs_range(u->offset, gs_type_size(u->type), counts[i], prog->state_size))
      return gs_error(prog->desc->Name, "uniform=%s count=%u capacity=%u offset=%zu state_size=%zu rejected",
                      u->name, counts[i], u->count ? u->count : 1, u->offset, prog->state_size);
  }
  glUseProgram(prog->progid);
  for (size_t i = 0; i < MAX_SHADER_UNIFORMS && prog->desc->Uniforms[i].name; i++) {
    const shaderUniform_t *u = &prog->desc->Uniforms[i];
    const void *data = (const char *)state + u->offset;
    GLint loc = prog->locs[i];
    GLsizei count = (GLsizei)counts[i];
    size_t bytes = gs_type_size(u->type) * counts[i];
    if (loc < 0) continue;
    uint32_t bit = UINT32_C(1) << i;
    if (prog->cache && (prog->valid & bit) && prog->counts[i] == counts[i] &&
        !memcmp((char *)prog->cache + u->offset, data, bytes)) continue;
    switch (u->type) {
      case UT_FLOAT:      glUniform1fv(loc, count, data); break;
      case UT_FLOAT_VEC2: glUniform2fv(loc, count, data); break;
      case UT_FLOAT_VEC3: glUniform3fv(loc, count, data); break;
      case UT_FLOAT_VEC4: case UT_COLOR: glUniform4fv(loc, count, data); break;
      case UT_INT: case UT_SAMPLER_2D: case UT_SAMPLER_2D_RECT: case UT_SAMPLER_2D_ARRAY:
        glUniform1iv(loc, count, data); break;
      case UT_INT_VEC2: glUniform2iv(loc, count, data); break;
      case UT_BOOL: {
        if (!prog->scratch) return gs_error(prog->desc->Name, "missing bool conversion storage");
        GLint *values = prog->scratch;
        for (GLsizei j = 0; j < count; j++) values[j] = ((const bool *)data)[j];
        glUniform1iv(loc, count, values);
        break;
      }
      case UT_FLOAT_MAT3: glUniformMatrix3fv(loc, count, GL_FALSE, data); break;
      case UT_FLOAT_MAT3_TRANSPOSE: {
        if (!prog->scratch) return gs_error(prog->desc->Name, "missing matrix conversion storage");
        GLfloat *values = prog->scratch;
        const GLfloat *src = data;
        for (GLsizei j = 0; j < count; j++) for (int row = 0; row < 3; row++) for (int col = 0; col < 3; col++)
          values[j * 9 + col * 3 + row] = src[j * 9 + row * 3 + col];
        glUniformMatrix3fv(loc, count, GL_FALSE, values);
        break;
      }
      case UT_FLOAT_MAT4: glUniformMatrix4fv(loc, count, GL_FALSE, data); break;
      default: return gs_error(prog->desc->Name, "invalid uniform type");
    }
    if (prog->cache) memcpy((char *)prog->cache + u->offset, data, bytes);
    prog->valid |= bit;
    prog->counts[i] = counts[i];
  }
  return true;
}
void gs_invalidate(shaderProg_t *prog) { if (prog) prog->valid = 0; }
void gs_delete(shaderProg_t *prog) {
  if (!prog) return;
  if (prog->progid) glDeleteProgram(prog->progid);
  if (prog->cache) prog->memory.free(prog->cache);
  if (prog->scratch) prog->memory.free(prog->scratch);
  memset(prog, 0, sizeof(*prog));
}

/* Complete modern GLSL sources use the same prologue and failure cleanup as descriptor programs. */
GLuint gs_link_sources(const char *vertex, const char *fragment, const shaderAttrib_t *attrs,
                       size_t attr_count, const gs_options_t *options) {
  if (!vertex || !fragment || (!attrs && attr_count) || !gs_options(options)) {
    gs_error(NULL, "invalid source program request");
    return 0;
  }
  gs_allocator_t memory = gs_memory(options);
  GLuint stages[2] = {glCreateShader(GL_VERTEX_SHADER), glCreateShader(GL_FRAGMENT_SHADER)}, id = 0;
  const char *sources[] = {vertex, fragment};
  bool ok = stages[0] && stages[1];
  if (!ok) gs_error(NULL, "shader allocation failed");
  for (int i = 0; i < 2 && ok; i++) {
    const char *body = sources[i];
    while (*body == ' ' || *body == '\t' || *body == '\r' || *body == '\n') body++;
    if (!strncmp(body, "#version", 8)) {
      const char *newline = strchr(body, '\n');
      body = newline ? newline + 1 : "";
    }
    const char *parts[] = {gs_versions[options->dialect], options->defines ? options->defines : "", body, "\n"};
    glShaderSource(stages[i], 4, parts, NULL);
    glCompileShader(stages[i]);
    ok = gs_check(stages[i], false, i ? "fragment source" : "vertex source", memory);
  }
  if (ok) id = gs_link(stages[0], stages[1], attrs, attr_count, "source program", memory);
  for (int i = 0; i < 2; i++) if (stages[i]) glDeleteShader(stages[i]);
  return id;
}
