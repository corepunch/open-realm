#ifndef shader_desc_h
#define shader_desc_h

/*
 * Descriptor-based shader declarations.
 *
 * A shader is declared as data: a name, the GLSL dialect it targets, its
 * fragment output, and three interface tables (uniforms, attributes, and the
 * varyings shared between stages). The vertex/fragment bodies carry only the
 * GLSL they need; the linker walks the tables to bind attributes and resolve
 * uniform locations automatically.
 *
 * There is deliberately no global uniform registry. Each shader owns its list
 * via a local X-macro SHADER_UNIFORMS(X); the same list expands into both the
 * enum constants (enum_<name>) call sites index with, and the descriptor table
 * (name string + type + precision) the linker walks, so the two can never
 * drift apart.
 */

#include "common/common.h"

/*
 * GLSL dialect. GLES3 (BZ_GL_ES3) and desktop GLSL 120 (BZ_GLSL_120) differ
 * from the default GLSL 140 in the stage keywords, fragment output spelling,
 * and texture sampling builtin. One descriptor drives all three: the linker
 * generates the prologue (version, precision, fragment out) from
 * Version/Precision/FragmentOut, while bodies use these macros for the tokens
 * that change spelling per dialect.
 */
#ifdef BZ_GL_ES3
#define GLSL_VERSION_NUMBER 300
#define GLSL_ATTR "in"
#define GLSL_VS_OUT "out"
#define GLSL_FS_IN "in"
#define GLSL_FRAGOUT "out vec4 o_color;\n"
#define GLSL_FRAGCOLOR "o_color"
#define GLSL_TEX "texture"
#define GLSL_TEXELFETCH "texelFetch"
#elif defined(BZ_GLSL_120)
#define GLSL_VERSION_NUMBER 120
#define GLSL_ATTR "attribute"
#define GLSL_VS_OUT "varying"
#define GLSL_FS_IN "varying"
#define GLSL_FRAGOUT ""
#define GLSL_FRAGCOLOR "gl_FragColor"
#define GLSL_TEX "texture2D"
#define GLSL_TEXELFETCH "texture2D"
#else
#define GLSL_VERSION_NUMBER 140
#define GLSL_ATTR "in"
#define GLSL_VS_OUT "out"
#define GLSL_FS_IN "in"
#define GLSL_FRAGOUT "out vec4 o_color;\n"
#define GLSL_FRAGCOLOR "o_color"
#define GLSL_TEX "texture"
#define GLSL_TEXELFETCH "texelFetch"
#endif

typedef enum {
    PRECISION_LOW,
    PRECISION_MEDIUM,
    PRECISION_HIGH,
    PRECISION_DEFAULT,
} precisionType_t;

typedef enum {
    UT_FLOAT,
    UT_FLOAT_VEC2,
    UT_FLOAT_VEC3,
    UT_FLOAT_VEC4,
    UT_COLOR,        /* vec4, normalized byte semantics (vertex color) */
    UT_INT,
    UT_BOOL,
    UT_FLOAT_MAT3,
    UT_FLOAT_MAT4,
    UT_SAMPLER_2D,
    UT_COUNT,
} uniformType_t;

typedef struct {
    const char *name;
    uniformType_t type;
    precisionType_t precision;
} shaderUniform_t;

typedef struct {
    const char *name;      /* GLSL name, e.g. "i_position" */
    t_attrib_id attrib;    /* location from common.h t_attrib_id */
    uniformType_t type;
} shaderAttrib_t;

typedef struct {
    const char *name;      /* varying shared between vertex and fragment stage */
    uniformType_t type;
} shaderVarying_t;

typedef struct shader_desc {
    const char *Name;
    int Version;                 /* authored GLSL version: 120, 140, or 300 */
    precisionType_t Precision;   /* default precision for the whole shader */
    const char *FragmentOut;     /* fragment output variable name ("" -> gl_FragColor) */
    const shaderUniform_t *Uniforms;    /* NULL-terminated */
    const shaderAttrib_t *Attributes;   /* NULL-terminated */
    const shaderVarying_t *Shared;      /* NULL-terminated, documentation */
    const char *VertexShader;    /* body only; version/precision are generated */
    const char *FragmentShader;
} shader_desc_t;

typedef const struct shader_desc *LPCSHADERDESC;

/*
 * Per-shader interface declaration. Each shader defines its own
 * <PREFIX>_UNIFORMS(X) / <PREFIX>_ATTRIBUTES(X) / <PREFIX>_SHARED(X) lists,
 * then expands them through these helpers to produce, from one name token:
 *   - an enum constant  enum_<name>  (call sites index the location array)
 *   - a descriptor entry { #name, type, precision } (linker walks it)
 *
 * Uniforms:
 *   #define FOO_UNIFORMS(X) \
 *       X(u_modelViewProjectionTransform, UT_FLOAT_MAT4, PRECISION_HIGH) \
 *       X(u_normalTransform, UT_FLOAT_MAT3, PRECISION_HIGH) \
 *       X(u_opacity, UT_FLOAT, PRECISION_LOW) \
 *       X(u_texture, UT_SAMPLER_2D, PRECISION_LOW)
 *   enum { FOO_UNIFORMS(SHADER_UNIFORM_ENUM) };
 *   static const shaderUniform_t foo_uniforms[] = {
 *       FOO_UNIFORMS(SHADER_UNIFORM_ENTRY)
 *       SHADER_UNIFORM_END
 *   };
 *
 * Attributes (GLSL name -> t_attrib_id location):
 *   #define FOO_ATTRIBUTES(X) \
 *       X(a_position, attrib_position, UT_FLOAT_VEC4) \
 *       X(a_texcoord0, attrib_texcoord, UT_FLOAT_VEC2)
 *   static const shaderAttrib_t foo_attributes[] = {
 *       FOO_ATTRIBUTES(SHADER_ATTRIB_ENTRY)
 *       SHADER_ATTRIB_END
 *   };
 *
 * Shared varyings:
 *   #define FOO_SHARED(X) \
 *       X(v_normal, UT_FLOAT_VEC3) \
 *       X(v_texcoord0, UT_FLOAT_VEC2)
 *   static const shaderVarying_t foo_shared[] = {
 *       FOO_SHARED(SHADER_SHARED_ENTRY)
 *       SHADER_SHARED_END
 *   };
 */
#define SHADER_UNIFORM_ENUM(name, type, precision) enum_##name,
#define SHADER_UNIFORM_ENTRY(name, type, precision) { #name, type, precision },
#define SHADER_UNIFORM_END { NULL, UT_COUNT, PRECISION_DEFAULT },

#define SHADER_ATTRIB_ENTRY(name, attrib, type) { #name, attrib, type },
#define SHADER_ATTRIB_END { NULL, attrib_count, UT_COUNT },

#define SHADER_SHARED_ENTRY(name, type) { #name, type },
#define SHADER_SHARED_END { NULL, UT_COUNT },

#endif
