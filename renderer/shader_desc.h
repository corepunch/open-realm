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

#include "vendor/gl_shader/gl_shader.h"

typedef struct shaderLoad_s {
    shader_desc_t const *desc;
    cstring_t defines;
    shaderProg_t *prog;
    void *state;
    size_t state_size;
} shaderLoad_t;


void R_LoadShaderState(shaderLoad_t const *load);
void R_DeleteShader(shaderProg_t *prog);
void R_UploadShader(shaderProg_t *prog, void const *state);
#define R_LoadShader(D, F, P) R_LoadShaderState(&(shaderLoad_t){ D, F, &(P)->prog, &(P)->state, sizeof((P)->state) })
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
int R_BuildShaderDeclarations(char *buf, int size, shader_desc_t const *desc,
                              bool is_vertex, glsl_dialect_t dialect);
int R_BuildShaderMain(char *buf, int size, bool is_vertex, glsl_dialect_t dialect);

#endif /* shader_desc_h */
