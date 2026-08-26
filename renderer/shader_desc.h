#ifndef shader_desc_h
#define shader_desc_h

/*
 * Descriptor-based shader declarations.
 *
 * Each shader is two things:
 *
 *   1. A typed program struct — one named GLint field per uniform.
 *      Call sites write:  glUniformMatrix4fv(prog.mvp, ...)
 *
 *   2. A shader_desc_t — static const data that drives GLSL source generation.
 *      The linker generates the version prologue and all uniform/in/out
 *      declarations from the tables, so bodies contain only GLSL logic.
 *
 * Both come from the same UNIFORM / ATTRIB / SHARED entries in the descriptor
 * initialiser; each entry stores the field's offsetof so the loader can write
 * the resolved GL location directly into the typed struct.
 *
 * Naming convention (prefixes are applied automatically):
 *   UNIFORM(mvp, ...)       → C field .mvp    /  GLSL uniform "u_mvp"
 *   ATTRIB(position, ...)   →                    GLSL attribute "a_position"
 *   SHARED(texcoord0, ...)  →                    GLSL varying "v_texcoord0"
 *
 * Usage pattern:
 *
 *   // 1. Declare the typed struct first:
 *   typedef struct {
 *       GLuint progid;
 *       GLint  mvp;
 *       GLint  texture;
 *   } simple_prog_t;
 *
 *   // 2. Bind SHADER_TYPE, declare the descriptor, unbind:
 *   #define SHADER_TYPE simple_prog_t
 *   static const shader_desc_t sd_simple = {
 *       .Name = "simple",
 *       .Uniforms = {
 *           UNIFORM(mvp,     UT_FLOAT_MAT4, PRECISION_HIGH),
 *           UNIFORM(texture, UT_SAMPLER_2D, PRECISION_LOW),
 *       },
 *       .Attributes = {
 *           ATTRIB(position,  attrib_position, UT_FLOAT_VEC4),
 *           ATTRIB(texcoord0, attrib_texcoord, UT_FLOAT_VEC2),
 *       },
 *       .Shared = {
 *           SHARED(texcoord0, UT_FLOAT_VEC2),
 *       },
 *       .VertexBody =
 *           "void main() {\n"
 *           "  gl_Position = u_mvp * a_position;\n"
 *           "  v_texcoord0 = a_texcoord0;\n"
 *           "}\n",
 *       .FragmentBody =
 *           "void main() {\n"
 *           "  " GLSL_FRAGCOLOR " = " GLSL_TEX "(u_texture, v_texcoord0);\n"
 *           "}\n",
 *   };
 *   #undef SHADER_TYPE
 *
 *   // 3. Load — writes progid and resolves uniform locations into the struct:
 *   static simple_prog_t simple_prog;
 *   R_LoadShaderDescInto(&sd_simple, NULL, &simple_prog.progid, &simple_prog);
 */

#include <stddef.h>
#include "common/common.h"

/* -----------------------------------------------------------------------
 * Compile-time dialect tokens for use inside VertexBody / FragmentBody.
 * Everything else (version line, declarations) is generated from the descriptor.
 * ----------------------------------------------------------------------- */
#ifdef BZ_GL_ES3
#  define GLSL_ATTR       "in"
#  define GLSL_VS_OUT     "out"
#  define GLSL_FS_IN      "in"
#  define GLSL_FRAGCOLOR  "o_color"
#  define GLSL_TEX        "texture"
#  define GLSL_TEXFETCH   "texelFetch"
#elif defined(BZ_GLSL_120)
#  define GLSL_ATTR       "attribute"
#  define GLSL_VS_OUT     "varying"
#  define GLSL_FS_IN      "varying"
#  define GLSL_FRAGCOLOR  "gl_FragColor"
#  define GLSL_TEX        "texture2D"
#  define GLSL_TEXFETCH   "texture2D"
#else /* GLSL 140 default */
#  define GLSL_ATTR       "in"
#  define GLSL_VS_OUT     "out"
#  define GLSL_FS_IN      "in"
#  define GLSL_FRAGCOLOR  "o_color"
#  define GLSL_TEX        "texture"
#  define GLSL_TEXFETCH   "texelFetch"
#endif

/* -----------------------------------------------------------------------
 * Dialect enum — for R_BuildShaderDeclarations (runtime dialect selection).
 * ----------------------------------------------------------------------- */
typedef enum {
    GLSL_DIALECT_120,
    GLSL_DIALECT_140,
    GLSL_DIALECT_ES3,
} glsl_dialect_t;

/* -----------------------------------------------------------------------
 * Types
 * ----------------------------------------------------------------------- */
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
    UT_COLOR,           /* vec4, normalised-byte vertex-color semantics */
    UT_INT,
    UT_INT_VEC2,
    UT_BOOL,
    UT_FLOAT_MAT3,
    UT_FLOAT_MAT4,
    UT_SAMPLER_2D,
    UT_SAMPLER_2D_RECT,
    UT_SAMPLER_2D_ARRAY,
    UT_COUNT,
} uniformType_t;

/* Uniform: stores the offsetof the corresponding GLint field in the typed
   program struct so the loader can write the resolved location directly. */
typedef struct {
    size_t          offset;    /* offsetof(SHADER_TYPE, field) */
    const char     *name;      /* GLSL name, e.g. "u_mvp" */
    uniformType_t   type;
    precisionType_t precision;
} shaderUniform_t;

typedef struct {
    const char     *name;      /* GLSL name, e.g. "a_position" */
    t_attrib_id     attrib;    /* explicit location for glBindAttribLocation */
    uniformType_t   type;
} shaderAttrib_t;

typedef struct {
    const char     *name;      /* GLSL name, e.g. "v_texcoord0" */
    uniformType_t   type;
} shaderVarying_t;

/* -----------------------------------------------------------------------
 * Descriptor.  Declare as static const; zero-initialised slots (name==NULL)
 * are sentinels that stop the linker's table walk.
 * ----------------------------------------------------------------------- */
#define MAX_SHADER_UNIFORMS  16
#define MAX_SHADER_ATTRIBS    8
#define MAX_SHADER_SHARED     8

typedef struct shader_desc {
    const char      *Name;
    shaderUniform_t  Uniforms[MAX_SHADER_UNIFORMS];
    shaderAttrib_t   Attributes[MAX_SHADER_ATTRIBS];
    shaderVarying_t  Shared[MAX_SHADER_SHARED];
    const char      *VertexBody;    /* GLSL logic only — no declarations */
    const char      *FragmentBody;
} shader_desc_t;

typedef const shader_desc_t *LPCSHADERDESC;

/* -----------------------------------------------------------------------
 * Descriptor entry macros.
 *
 * SHADER_TYPE must be #defined to the per-shader typed struct for the
 * duration of the descriptor initialiser, then #undef'd.
 *
 * UNIFORM(field, type, prec)
 *   Records offsetof(SHADER_TYPE, field) so the loader can write the
 *   resolved GL uniform location directly into the typed struct.
 *   GLSL name:  "u_" #field
 *
 * ATTRIB(field, attrib_id, type)
 *   Explicit attribute location binding.
 *   GLSL name:  "a_" #field
 *
 * SHARED(field, type)
 *   Varying declared as 'out' in the vertex stage and 'in' in the fragment.
 *   GLSL name:  "v_" #field
 * ----------------------------------------------------------------------- */
#define UNIFORM(field, type, prec) \
    { offsetof(SHADER_TYPE, field), "u_" #field, type, prec }
#define ATTRIB(field, attrib_id, type) \
    { "a_" #field, attrib_id, type }
#define SHARED(field, type) \
    { "v_" #field, type }

/* -----------------------------------------------------------------------
 * R_BuildShaderDeclarations
 *   Writes the uniform/in/out declaration block for one stage of desc into
 *   buf[size] for the given dialect.  Returns bytes written (excluding NUL).
 *   Pass the result as one of the strings in a glShaderSource call — no heap
 *   allocation needed.
 * ----------------------------------------------------------------------- */
int R_BuildShaderDeclarations(char *buf, int size, const shader_desc_t *desc,
                              bool is_vertex, glsl_dialect_t dialect);

#endif /* shader_desc_h */
