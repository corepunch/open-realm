/* GL Shader 1.0.0. Copyright (c) 2024 corepunch. MIT; see LICENSE. */
#ifndef __GL_SHADER_H__
#define __GL_SHADER_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GL_SHADER_VERSION "1.0.0"
#define MAX_SHADER_UNIFORMS 32 // uniforms; bounds descriptor, locations and validity mask
#define MAX_SHADER_ATTRIBS 8 // attributes; bounds the declared vertex interface
#define MAX_SHADER_SHARED 8 // varyings; bounds the inter-stage interface

typedef enum { GLSL_DIALECT_120, GLSL_DIALECT_140, GLSL_DIALECT_150, GLSL_DIALECT_ES3 } glsl_dialect_t;
typedef enum { PRECISION_LOW, PRECISION_MEDIUM, PRECISION_HIGH, PRECISION_DEFAULT } precisionType_t;
typedef enum {
  UT_FLOAT, UT_FLOAT_VEC2, UT_FLOAT_VEC3, UT_FLOAT_VEC4, UT_COLOR, UT_INT, UT_INT_VEC2, UT_BOOL,
  UT_FLOAT_MAT3, UT_FLOAT_MAT3_TRANSPOSE, UT_FLOAT_MAT4, UT_SAMPLER_2D, UT_SAMPLER_2D_RECT,
  UT_SAMPLER_2D_ARRAY, UT_COUNT
} uniformType_t;
typedef struct {
  size_t offset;
  const char *name;
  uniformType_t type;
  precisionType_t precision;
  uint32_t count;
  size_t count_offset;
  bool counted;
} shaderUniform_t;
typedef struct { const char *name; uint32_t attrib; uniformType_t type; } shaderAttrib_t;
typedef struct { const char *name; uniformType_t type; } shaderVarying_t;
typedef struct shader_desc {
  const char *Name;
  shaderUniform_t Uniforms[MAX_SHADER_UNIFORMS];
  shaderAttrib_t Attributes[MAX_SHADER_ATTRIBS];
  shaderVarying_t Shared[MAX_SHADER_SHARED];
  const char *VertexBody, *FragmentBody;
} shader_desc_t;
typedef struct { void *(*alloc)(size_t); void (*free)(void *); } gs_allocator_t;
typedef struct { glsl_dialect_t dialect; const char *defines; gs_allocator_t memory; } gs_options_t;
typedef struct shaderProg_s {
  GLuint progid;
  const shader_desc_t *desc;
  GLint locs[MAX_SHADER_UNIFORMS];
  void *cache, *scratch;
  size_t state_size;
  uint32_t valid, counts[MAX_SHADER_UNIFORMS];
  gs_allocator_t memory;
} shaderProg_t;

int gs_declarations(char *buf, size_t size, const shader_desc_t *desc, bool vertex, glsl_dialect_t dialect);
int gs_main(char *buf, size_t size, bool vertex, glsl_dialect_t dialect);
size_t gs_type_size(uniformType_t type);
bool gs_source(GLuint stage, const shader_desc_t *desc, bool vertex, const gs_options_t *options);
bool gs_load(shaderProg_t *prog, const shader_desc_t *desc, void *state, size_t state_size, const gs_options_t *options);
bool gs_apply(shaderProg_t *prog, const void *state);
void gs_delete(shaderProg_t *prog);
void gs_invalidate(shaderProg_t *prog);
GLuint gs_link_sources(const char *vertex, const char *fragment, const shaderAttrib_t *attrs,
                       size_t attr_count, const gs_options_t *options);

#endif
