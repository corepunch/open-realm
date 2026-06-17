#include "r_local.h"

LPCSTR vs_default =
"#version 140\n"
"in vec3 i_position;\n"
"in vec2 i_texcoord;\n"
"in vec3 i_normal;\n"
"in vec4 i_color;\n"
#ifdef USE_SHADOWMAPS
"out vec4 v_shadow;\n"
#endif
"out vec2 v_texcoord;\n"
"out vec2 v_texcoord2;\n"
"out vec3 v_normal;\n"
"out vec3 v_lightDir;\n"
"out vec4 v_color;\n"
"uniform mat4 uViewProjectionMatrix;\n"
"uniform mat4 uTextureMatrix;\n"
"uniform mat4 uModelMatrix;\n"
"uniform mat4 uLightMatrix;\n"
"uniform mat3 uNormalMatrix;\n"
"void main() {\n"
"    vec4 pos = uModelMatrix * vec4(i_position, 1.0);"
"    v_texcoord = i_texcoord;\n"
"    v_texcoord2 = (uTextureMatrix * pos).xy;\n"
"    v_normal = normalize(uNormalMatrix * i_normal);\n"
#ifdef USE_SHADOWMAPS
"    v_shadow = uLightMatrix * pos;\n"
#endif
"    v_color = i_color;\n"
"    v_lightDir = -normalize(vec3(uLightMatrix[0][2], uLightMatrix[1][2], uLightMatrix[2][2]))*1.2;\n"
"    gl_Position = uViewProjectionMatrix * uModelMatrix * vec4(i_position, 1.0);\n"
"}\n";

LPCSTR fs_default =
"#version 140\n"
"in vec2 v_texcoord;\n"
"in vec2 v_texcoord2;\n"
#ifdef USE_SHADOWMAPS
"in vec4 v_shadow;\n"
#endif
"in vec3 v_normal;\n"
"in vec4 v_color;\n"
"in vec3 v_lightDir;\n"
"out vec4 o_color;\n"
"uniform sampler2D uTexture;\n"
#if defined(USE_SHADOWMAPS) || defined(DEBUG_PATHFINDING)
"uniform sampler2D uShadowmap;\n"
#endif
"uniform sampler2D uFogOfWar;\n"
"float get_light() {\n"
"    return dot(v_normal, v_lightDir);\n"
"}\n"
#ifdef USE_SHADOWMAPS
"float get_shadow() {\n"
"    float depth = texture(uShadowmap, vec2(v_shadow.x + 1.0, v_shadow.y + 1.0) * 0.5).r;\n"
"    return depth < (v_shadow.z + 0.99) * 0.5 ? 0.0 : 1.0;\n"
"}\n"
#endif
"float get_lighting() {\n"
#ifdef USE_SHADOWMAPS
"    return min(1.0, mix(0.35, 1.0, get_shadow() * get_light()) * 1.1);"
#else
"    return min(1.0, mix(0.35, 1.0, get_light()) * 1.1);"
#endif
"}\n"
"float get_fogofwar() {\n"
"    return texture(uFogOfWar, v_texcoord2).r;\n"
"}\n"
"void main() {\n"
#ifdef DEBUG_PATHFINDING
"    vec4 debug = texture(uShadowmap, v_texcoord2);\n"
"    vec4 color = texture(uTexture, v_texcoord);\n"
"    float sin_factor = sin(debug.g * 3.14159 * 2.0);\n"
"    float cos_factor = cos(debug.g * 3.14159 * 2.0);\n"
"    vec2 tc = fract(v_texcoord2 * 384.0);\n"
"    tc = (tc - 0.5) * mat2(cos_factor, sin_factor, -sin_factor, cos_factor);\n"
"    tc += 0.5;\n"
"    float stp = step(abs(0.5 - tc.y), tc.x * 0.25);"
"    /*debug.a = color.a*/;\n"
"    o_color = debug * 0.7;// mix(debug, color, 0.5) + vec4(stp);\n"
"    return;\n"
#endif
"    vec4 col = texture(uTexture, v_texcoord) * v_color;\n"
#ifdef WOW
"    o_color = col;\n"
#else
"    col.rgb *= get_fogofwar() * get_lighting();\n"
"    o_color = col;\n"
#endif
"}\n";

LPCSTR fs_ui =
"#version 140\n"
"in vec4 v_color;\n"
"in vec2 v_texcoord;\n"
"out vec4 o_color;\n"
"uniform sampler2D uTexture;\n"
"void main() {\n"
"    o_color = texture(uTexture, v_texcoord) * v_color;\n"
"}\n";

LPCSTR fs_splat =
"#version 140\n"
"in vec4 v_color;\n"
"in vec2 v_texcoord;\n"
"out vec4 o_color;\n"
"uniform sampler2D uTexture;\n"
"float crop_edges(vec2 tc) {\n"
"   return step(abs(tc.x - 0.5), 0.5) * step(abs(tc.y - 0.5), 0.5);\n"
"}\n"
"void main() {\n"
"    o_color = texture(uTexture, v_texcoord) * v_color;\n"
"    o_color.a *= crop_edges(v_texcoord);\n"
"}\n";

LPCSTR fs_shadow_splat =
"#version 140\n"
"in vec4 v_color;\n"
"in vec2 v_texcoord;\n"
"out vec4 o_color;\n"
"uniform sampler2D uTexture;\n"
"float crop_edges(vec2 tc) {\n"
"   return step(abs(tc.x - 0.5), 0.5) * step(abs(tc.y - 0.5), 0.5);\n"
"}\n"
"void main() {\n"
"    vec4 tex = texture(uTexture, v_texcoord);\n"
"    o_color = vec4(0.0, 0.0, 0.0, tex.a * v_color.a * crop_edges(v_texcoord));\n"
"}\n";

LPCSTR fs_commandbutton =
"#version 140\n"
"in vec4 v_color;\n"
"in vec2 v_texcoord;\n"
"out vec4 o_color;\n"
"uniform sampler2D uTexture;\n"
"uniform float uActiveGlow;\n"
"float crop_edges(vec2 tc) {\n"
"   return step(abs(tc.x - 0.5), 0.5) * step(abs(tc.y - 0.5), 0.5);\n"
"}\n"
"void main() {\n"
"    o_color = texture(uTexture, v_texcoord) * v_color;\n"
"    float glow = max(abs(v_texcoord.x - 0.5), abs(v_texcoord.y - 0.5));\n"
"    glow = smoothstep(0.33, 0.5, glow) * 0.75 * uActiveGlow;\n"
"    o_color.rgb = mix(o_color.rgb,vec3(0.5,1.0,0.5),glow);\n"
"    o_color.a *= crop_edges(v_texcoord);\n"
"}\n";

LPCSTR fs_minimap_fog =
"#version 140\n"
"in vec4 v_color;\n"
"in vec2 v_texcoord;\n"
"out vec4 o_color;\n"
"uniform sampler2D uTexture;\n"
"void main() {\n"
"    float visibility = texture(uTexture, vec2(v_texcoord.x, 1.0 - v_texcoord.y)).r;\n"
"    float alpha = clamp(1.0 - visibility, 0.0, 1.0) * v_color.a;\n"
"    o_color = vec4(v_color.rgb, alpha);\n"
"}\n";

LPSHADER R_InitShader(LPCSTR vs_default, LPCSTR fs_default){
    GLuint vs = R_Call(glCreateShader, GL_VERTEX_SHADER);
    GLuint fs = R_Call(glCreateShader, GL_FRAGMENT_SHADER);

    int length = (int)strlen(vs_default);
    R_Call(glShaderSource, vs, 1, (const GLchar **)&vs_default, &length);
    R_Call(glCompileShader, vs);

    GLint status;
    R_Call(glGetShaderiv, vs, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLint logLength = 0;
        glGetShaderiv(vs, GL_INFO_LOG_LENGTH, &logLength);
        if (logLength > 1) { // There's something to print
            char *log = malloc(logLength);
            if (log) {
                glGetShaderInfoLog(vs, logLength, NULL, log);
                fprintf(stderr, "Vertex shader compilation failed:\n%s\n", log);
                free(log);
            } else {
                fprintf(stderr, "Vertex shader compilation failed (could not allocate log buffer)\n");
            }
        } else {
            fprintf(stderr, "Vertex shader compilation failed (no log)\n");
        }
        return NULL;
    }
    length = (int)strlen(fs_default);
    R_Call(glShaderSource, fs, 1, (const GLchar **)&fs_default, &length);
    R_Call(glCompileShader, fs);

    R_Call(glGetShaderiv, fs, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLint logLength = 0;
        glGetShaderiv(fs, GL_INFO_LOG_LENGTH, &logLength);
        if (logLength > 1) { // There's something to print
            char *log = malloc(logLength);
            if (log) {
                glGetShaderInfoLog(fs, logLength, NULL, log);
                fprintf(stderr, "Vertex shader compilation failed:\n%s\n", log);
                free(log);
            } else {
                fprintf(stderr, "Vertex shader compilation failed (could not allocate log buffer)\n");
            }
        } else {
            fprintf(stderr, "Vertex shader compilation failed (no log)\n");
        }
        return NULL;
    }

    LPSHADER program = ri.MemAlloc(sizeof(struct shader_program));
    program->progid = R_Call(glCreateProgram, );

    R_Call(glAttachShader, program->progid, vs);
    R_Call(glAttachShader, program->progid, fs);

    R_Call(glBindAttribLocation, program->progid, attrib_position, "i_position");
    R_Call(glBindAttribLocation, program->progid, attrib_color, "i_color");
    R_Call(glBindAttribLocation, program->progid, attrib_texcoord, "i_texcoord");
    R_Call(glBindAttribLocation, program->progid, attrib_normal, "i_normal");
    R_Call(glBindAttribLocation, program->progid, attrib_skin1, "i_skin1");
    R_Call(glBindAttribLocation, program->progid, attrib_skin2, "i_skin2");
    R_Call(glBindAttribLocation, program->progid, attrib_boneWeight1, "i_boneWeight1");
    R_Call(glBindAttribLocation, program->progid, attrib_boneWeight2, "i_boneWeight2");
    R_Call(glBindAttribLocation, program->progid, attrib_particleSize, "i_size");
    R_Call(glBindAttribLocation, program->progid, attrib_particleAxis, "i_axis");

    R_Call(glLinkProgram, program->progid);
    R_Call(glUseProgram, program->progid);
    
#define R_RegisterUniform(PROGRAM, NAME) PROGRAM->NAME = glGetUniformLocation(PROGRAM->progid, #NAME);

    R_RegisterUniform(program, uViewProjectionMatrix);
    R_RegisterUniform(program, uModelMatrix);
    R_RegisterUniform(program, uLightMatrix);
    R_RegisterUniform(program, uNormalMatrix);
    R_RegisterUniform(program, uTextureMatrix);
    R_RegisterUniform(program, uTexture);
#if defined(USE_SHADOWMAPS) || defined(DEBUG_PATHFINDING)
    R_RegisterUniform(program, uShadowmap);
#endif
    R_RegisterUniform(program, uFogOfWar);
    R_RegisterUniform(program, uBones);
    R_RegisterUniform(program, uUseDiscard);
    R_RegisterUniform(program, uUnshaded);
    R_RegisterUniform(program, uLayerAlpha);
    R_RegisterUniform(program, uGeosetColor);
    R_RegisterUniform(program, uUvTrans);
    R_RegisterUniform(program, uUvRot);
    R_RegisterUniform(program, uUvScale);
    R_RegisterUniform(program, uMdxLightCount);
    program->uMdxLights = glGetUniformLocation(program->progid, "uMdxLights[0]");
    R_RegisterUniform(program, uMdxFallbackLighting);
    R_RegisterUniform(program, uMdxLightFill);
    R_RegisterUniform(program, uEyePosition);
    R_RegisterUniform(program, uActiveGlow);
    R_RegisterUniform(program, uFogEnable);
    R_RegisterUniform(program, uFogColor);
    R_RegisterUniform(program, uFogParams);

    R_Call(glUniform1i, program->uTexture, 0);
#if defined(USE_SHADOWMAPS) || defined(DEBUG_PATHFINDING)
    R_Call(glUniform1i, program->uShadowmap, 1);
#endif
    R_Call(glUniform1i, program->uFogOfWar, 2);

    return program;
}

void R_ReleaseShader(LPSHADER shader) {
    ri.MemFree(shader);
}
