#ifndef shader_desc_h
#define shader_desc_h

/* Descriptors generate GLSL declarations and map typed CPU state to program-owned locations.
 * Define SHADER_TYPE as the value struct, then describe its members with UNIFORM.
 * R_LoadShader(desc, defines, &shader) links a { SHADERPROG prog; STATE state; }.
 * Fill shader.state and call R_ApplyShader(&shader) before drawing. The backend
 * binds the program and uploads changed state; callers never handle locations.
 */

#include <stddef.h>
#include "common/common.h"

/* -----------------------------------------------------------------------
 * Dialect differences.  Bodies define vec4 vert()/frag() and call texture();
 * the linker generates main() which assigns the result to gl_Position and to
 * gl_FragColor (120) or o_color (otherwise).  GLSL 120 fragment bodies also
 * get a leading `#define texture texture2D`, since that dialect's builtin has
 * a different name.  Keyword selection (attribute/varying vs in/out) is
 * likewise generated, so bodies carry no per-dialect conditionals.
 *
 * Controlled by the GLSL Make variable (default 140):
 *   make GLSL=120   → -DBZ_GLSL_120   (attribute/varying + texture alias)
 *   make GLSL=140   → (no define)     (in/out)
 *   make GLSL=150   → -DBZ_GLSL_150   (same keywords as 140, only #version differs)
 *   make GL_BACKEND=gles3 → -DBZ_GL_ES3  (300 es, precision qualifiers)
 *
 * GL_BACKEND=gles3 overrides GLSL: the ES3 define takes precedence regardless
 * of the GLSL= value because GLES uses a separate version namespace (300 es).
 * ----------------------------------------------------------------------- */

/* -----------------------------------------------------------------------
 * Dialect enum — passed to R_BuildShaderDeclarations for the version line
 * and keyword selection.  Compile-time macros map to these at build time;
 * pass a specific value to generate source for a different version at runtime.
 * ----------------------------------------------------------------------- */
typedef enum {
    GLSL_DIALECT_120,   /* attribute/varying/texture2D/gl_FragColor          */
    GLSL_DIALECT_140,   /* in/out/texture/o_color                            */
    GLSL_DIALECT_150,   /* same keywords as 140; version line is "#version 150" */
    GLSL_DIALECT_ES3,   /* 300 es, same keywords as 140 + precision prologue */
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
    UT_FLOAT_MAT3_TRANSPOSE,
    UT_FLOAT_MAT4,
    UT_SAMPLER_2D,
    UT_SAMPLER_2D_RECT,
    UT_SAMPLER_2D_ARRAY,
    UT_COUNT,
} uniformType_t;

/* Uniform offsets address typed CPU values. count=0 is scalar; arrays upload as one block. */
typedef struct {
    size_t          offset;    /* offsetof(SHADER_TYPE, field) */
    const char     *name;      /* GLSL name, e.g. "u_mvp" */
    uniformType_t   type;
    precisionType_t precision;
    DWORD           count;     /* array size; 0 = scalar */
    size_t          count_offset; /* runtime upload count for a fixed-capacity GLSL array */
    bool            counted;
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
#define MAX_SHADER_UNIFORMS 32 // uniforms; largest game descriptor fits; bounds descriptor and location tables
#define MAX_SHADER_ATTRIBS 8 // attributes; shared model input count; bounds descriptor table
#define MAX_SHADER_SHARED 8 // varyings; shared shader interface capacity; bounds descriptor table

typedef struct shader_desc {
    const char      *Name;
    shaderUniform_t  Uniforms[MAX_SHADER_UNIFORMS];
    shaderAttrib_t   Attributes[MAX_SHADER_ATTRIBS];
    shaderVarying_t  Shared[MAX_SHADER_SHARED];
    const char      *VertexBody;    /* defines vec4 vert() → clip-space position */
    const char      *FragmentBody;  /* defines vec4 frag() → fragment color */
} shader_desc_t;

typedef const shader_desc_t *LPCSHADERDESC;

/* GL handles stay in the program, never in the typed value state. */
typedef struct SHADERPROG {
    GLuint progid;
    LPCSHADERDESC desc;
    GLint locs[MAX_SHADER_UNIFORMS];
    void *cache; /* shadow of last-uploaded state for change detection */
} SHADERPROG;
typedef struct SHADERPROG *LPSHADERPROG;
typedef const struct SHADERPROG *LPCSHADERPROG;

typedef struct SHADERLOAD {
    LPCSHADERDESC desc;
    LPCSTR defines;
    LPSHADERPROG prog;
    void *state;
} SHADERLOAD;
typedef struct SHADERLOAD *LPSHADERLOAD;
typedef const struct SHADERLOAD *LPCSHADERLOAD;
void R_LoadShaderState(LPCSHADERLOAD load);
void R_DeleteShader(LPSHADERPROG prog);
void R_UploadShader(LPSHADERPROG prog, LPCVOID state);
#define R_LoadShader(D, F, P) R_LoadShaderState(&(SHADERLOAD){ D, F, &(P)->prog, &(P)->state })
#define R_ApplyShader(P) R_UploadShader(&(P)->prog, &(P)->state)

/* SHADER_TYPE names the CPU value struct. A fourth argument makes a fixed array;
 * a fifth names the state field containing its active upload count. */
#define BZ_UNIFORM_3(field, type, prec) \
    { offsetof(SHADER_TYPE, field), "u_" #field, type, prec, 0, 0, false }
#define BZ_UNIFORM_4(field, type, prec, count) \
    { offsetof(SHADER_TYPE, field), "u_" #field, type, prec, count, 0, false }
#define BZ_UNIFORM_5(field, type, prec, count, count_field) \
    { offsetof(SHADER_TYPE, field), "u_" #field, type, prec, count, offsetof(SHADER_TYPE, count_field), true }
#define BZ_UNIFORM_SELECT(_1, _2, _3, _4, _5, NAME, ...) NAME
#define UNIFORM(...) BZ_UNIFORM_SELECT(__VA_ARGS__, BZ_UNIFORM_5, BZ_UNIFORM_4, BZ_UNIFORM_3)(__VA_ARGS__)
#define ATTRIB(field, attrib_id, type) \
    { "a_" #field, attrib_id, type }
#define SHARED(field, type) \
    { "v_" #field, type }

/* -----------------------------------------------------------------------
 * R_BuildShaderDeclarations
 *   Writes the declaration block for one stage of desc into buf[size] for the
 *   given dialect: uniforms, then attributes/varyings with the dialect's
 *   keywords (attribute/varying for 120, in/out otherwise), plus `out vec4
 *   o_color` in the fragment stage for non-120.  GLSL 120 fragment shaders get
 *   a leading `#define texture texture2D` since that dialect has no `texture`
 *   builtin.  Returns bytes written (excluding NUL).
 *
 * R_BuildShaderMain
 *   Writes the generated main() for one stage: `gl_Position = vert();` for the
 *   vertex stage, or `gl_FragColor = frag();` (120) / `o_color = frag();`
 *   (otherwise) for the fragment stage.  Bodies define vert()/frag() and never
 *   reference gl_Position, gl_FragColor, or o_color directly.
 * ----------------------------------------------------------------------- */
int R_BuildShaderDeclarations(char *buf, int size, const shader_desc_t *desc,
                              bool is_vertex, glsl_dialect_t dialect);
int R_BuildShaderMain(char *buf, int size, bool is_vertex, glsl_dialect_t dialect);

#endif /* shader_desc_h */
