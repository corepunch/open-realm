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
"in vec3 v_normal;\n"
"in vec4 v_color;\n"
"in vec3 v_lightDir;\n"
"out vec4 o_color;\n"
"uniform sampler2D uTexture;\n"
"uniform sampler2D uFogOfWar;\n"
"float get_light() {\n"
"    return dot(v_normal, v_lightDir);\n"
"}\n"
#ifdef USE_SHADOWMAPS
"uniform sampler2D uShadowmap;\n"
"in vec4 v_shadow;\n"
"float get_shadow() {\n"
"    float depth = texture(uShadowmap, vec2(v_shadow.x + 1.0, v_shadow.y + 1.0) * 0.5).r;\n"
"    return depth < (v_shadow.z + 0.99) * 0.5 ? 0.0 : 1.0;\n"
"}\n"
"float get_lighting() { return min(1.0, mix(0.35, 1.0, get_shadow() * get_light()) * 1.1); }\n"
#else
"float get_lighting() { return min(1.0, mix(0.35, 1.0, get_light()) * 1.1); }\n"
#endif
"float get_fogofwar() {\n"
"    return texture(uFogOfWar, v_texcoord2).r;\n"
"}\n"
"void main() {\n"
"    vec4 col = texture(uTexture, v_texcoord) * v_color;\n"
#ifdef WOW // TODO: Why is WoW not lit?? fogofwar shall be in #ifdef, disabled for WoW
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

/* Shared vertex shader for MDX/M2/M3 model formats.
   Compiled twice: once as-is (uModelMatrix path), and once with
   "#define BZ_USE_INSTANCING 1" injected to switch to per-instance
   matrix attributes and add ground-effect grass wind. */
static LPCSTR model_vs =
"#version 140\n"
"in vec3 i_position;\n"
"in vec4 i_color;\n"
"in vec2 i_texcoord;\n"
"in vec3 i_normal;\n"
"in vec4 i_skin1;\n"
"in vec4 i_boneWeight1;\n"
"#ifdef BZ_USE_INSTANCING\n"
"in vec4 i_instance0;\n"
"in vec4 i_instance1;\n"
"in vec4 i_instance2;\n"
"in vec4 i_instance3;\n"
"#endif\n"
"out vec4 v_color;\n"
#ifdef USE_SHADOWMAPS
"out vec4 v_shadow;\n"
#endif
"out vec2 v_texcoord;\n"
"out vec2 v_texcoord2;\n"
"out vec3 v_lighting;\n"
"uniform mat4 uBones[BZ_BONE_COUNT];\n"
"uniform mat4 uViewProjectionMatrix;\n"
"uniform mat4 uLightMatrix;\n"
"uniform mat4 uTextureMatrix;\n"
/* Single-light fallback: WoW/SC2 set uLightCount=0 and use these three
   uniforms for their one directional sun instead of the uLights[] array. */
"uniform vec3 uLightDir;\n"
"uniform vec3 uLightColor;\n"
"uniform vec3 uLightAmbient;\n"
"uniform int uLightCount;\n"
"uniform float uFirstBoneLookupIndex;\n"
"uniform mat4 uLights[8];\n"
"#ifdef BZ_USE_INSTANCING\n"
"uniform vec3 uGrassCameraPos;\n"
"uniform vec2 uGrassFade;\n"
"uniform float uGrassTime;\n"
"uniform vec3 uGrassWind;\n"
"uniform vec4 uGrassPhase;\n"
"uniform vec2 uGrassHeight;\n"
"uniform int uGroundEffect;\n"
"#else\n"
"uniform mat4 uModelMatrix;\n"
"uniform mat3 uNormalMatrix;\n"
"#endif\n"
"const int MODEL_LIGHT_OMNI = 0;\n"
"const int MODEL_LIGHT_DIRECT = 1;\n"
"const int MODEL_LIGHT_AMBIENT = 2;\n"
"vec3 vertex_lighting(vec3 normal, vec3 worldPos) {\n"
"    vec3 n = normalize(normal);\n"
"    if (uLightCount == 0)\n"
"        return uLightAmbient + uLightColor * max(dot(n, normalize(uLightDir)), 0.0);\n"
"    vec3 lighting = uLightAmbient;\n"
"    for (int i = 0; i < 8; ++i) {\n"
"        if (i >= uLightCount) break;\n"
"        vec4 positionType = uLights[i][0];\n"
"        vec4 direction = uLights[i][1];\n"
"        vec4 colorIntensity = uLights[i][2];\n"
"        vec4 ambientIntensity = uLights[i][3];\n"
"        int type = int(positionType.w + 0.5);\n"
"        vec3 color = colorIntensity.rgb * colorIntensity.a;\n"
"        vec3 ambient = ambientIntensity.rgb * ambientIntensity.a;\n"
"        if (type == MODEL_LIGHT_AMBIENT) {\n"
"            lighting += color + ambient;\n"
"        } else if (type == MODEL_LIGHT_DIRECT) {\n"
"            vec3 l = normalize(-direction.xyz);\n"
"            lighting += clamp(color * max(dot(n, l), 0.0), vec3(0.0), vec3(1.0)) + ambient;\n"
"        } else {\n"
"            vec3 delta = positionType.xyz - worldPos;\n"
"            vec3 l = normalize(delta);\n"
"            float dist = length(delta) / 64.0 + 1.0;\n"
"            float atten = 1.0 / (dist * dist);\n"
"            lighting += clamp(color * atten * max(dot(n, l), 0.0), vec3(0.0), vec3(1.0));\n"
"            lighting += ambient * atten;\n"
"        }\n"
"    }\n"
"    return min(lighting, vec3(1.0));\n"
"}\n"
"void main() {\n"
"    vec4 pos4 = vec4(i_position, 1.0);\n"
"    vec4 norm4 = vec4(i_normal, 0.0);\n"
"    vec4 position = vec4(0.0);\n"
"    vec4 normal = vec4(0.0);\n"
"    for (int i = 0; i < 4; ++i) {\n"
"        int boneIdx = min(int(i_skin1[i]) + int(uFirstBoneLookupIndex), BZ_BONE_COUNT - 1);\n"
"        position += uBones[boneIdx] * pos4 * i_boneWeight1[i];\n"
"        normal += uBones[boneIdx] * norm4 * i_boneWeight1[i];\n"
"    }\n"
"    position.w = 1.0;\n"
"#ifdef BZ_USE_INSTANCING\n"
"    mat4 i_instance = mat4(i_instance0, i_instance1, i_instance2, i_instance3);\n"
"    if (uGroundEffect != 0) {\n"
"        float grassHeight = max(uGrassHeight.y - uGrassHeight.x, 0.001);\n"
"        float grassTop = smoothstep(uGrassWind.z, 1.0, clamp((position.z - uGrassHeight.x) / grassHeight, 0.0, 1.0));\n"
"        float grassPhase = dot(i_instance3.xy, uGrassPhase.xy);\n"
"        float grassSway = sin(uGrassTime * uGrassWind.x + grassPhase) * uGrassWind.y * grassHeight * grassTop;\n"
"        position.xy += uGrassPhase.zw * grassSway;\n"
"    }\n"
"    vec4 worldPos4 = i_instance * position;\n"
"    v_color = i_color;\n"
"    if (uGroundEffect != 0) {\n"
"        float fadeDist = length(worldPos4.xy - uGrassCameraPos.xy);\n"
"        v_color.a *= 1.0 - smoothstep(uGrassFade.x, uGrassFade.y, fadeDist);\n"
"    }\n"
"    v_texcoord = i_texcoord;\n"
"    v_texcoord2 = (uTextureMatrix * worldPos4).xy;\n"
"    v_lighting = vertex_lighting(normalize(mat3(i_instance) * normal.xyz), worldPos4.xyz);\n"
#ifdef USE_SHADOWMAPS
"    v_shadow = uLightMatrix * worldPos4;\n"
#endif
"    gl_Position = uViewProjectionMatrix * worldPos4;\n"
"#else\n"
"    v_color = i_color;\n"
"    v_texcoord = i_texcoord;\n"
"    v_texcoord2 = (uTextureMatrix * uModelMatrix * position).xy;\n"
"    vec3 worldNormal = normalize(uNormalMatrix * normal.xyz);\n"
"    vec3 worldPos = (uModelMatrix * position).xyz;\n"
"    v_lighting = vertex_lighting(worldNormal, worldPos);\n"
#ifdef USE_SHADOWMAPS
"    v_shadow = uLightMatrix * uModelMatrix * position;\n"
#endif
"    gl_Position = uViewProjectionMatrix * uModelMatrix * position;\n"
"#endif\n"
"}\n";

static LPCSTR model_fs =
"#version 140\n"
"in vec2 v_texcoord;\n"
"in vec2 v_texcoord2;\n"
#ifdef USE_SHADOWMAPS
"in vec4 v_shadow;\n"
#endif
"in vec3 v_lighting;\n"
"in vec4 v_color;\n"
"out vec4 o_color;\n"
"uniform sampler2D uTexture;\n"
#ifdef USE_SHADOWMAPS
"uniform sampler2D uShadowmap;\n"
#endif
// TODO: Add USE_FOGOFWAR and disable it in WoW as it's not needed
"uniform sampler2D uFogOfWar;\n"
"uniform float uLayerAlpha;\n"
"uniform vec4 uGeosetColor;\n"
"uniform mat3 uUvMatrix;\n"
"uniform bool uAlphaKey;\n"
/* In MSAA mode, hard discard is replaced by smoothstep alpha-to-coverage
   to avoid edge aliasing; uAlphaCutoff is still used as the threshold. */
"uniform float uAlphaCutoff;\n"
/* uUnshaded: MDX emissive layers skip lighting and fog-of-war. */
"uniform bool uUnshaded;\n"
// TODO: Add USE_FOG to skip compilation in games that never use it
"uniform bool uFogEnable;\n"
"uniform vec3 uFogColor;\n"
"uniform vec2 uFogParams;\n"
"float get_fogofwar() {\n"
"    return texture(uFogOfWar, v_texcoord2).r;\n"
"}\n"
"void main() {\n"
"    vec2 uv = (uUvMatrix * vec3(v_texcoord, 1.0)).xy;\n"
"    vec4 col = texture(uTexture, uv);\n"
"    col *= uGeosetColor;\n"
"    col *= uLayerAlpha;\n"
"    col *= v_color;\n"
"    if (!uUnshaded) {\n"
"        col.rgb *= get_fogofwar() * v_lighting;\n"
"        if (uFogEnable) {\n"
"            float fogFactor = clamp((uFogParams.y - gl_FragCoord.z / gl_FragCoord.w) / (uFogParams.y - uFogParams.x), 0.0, 1.0);\n"
"            col.rgb = mix(uFogColor, col.rgb, fogFactor);\n"
"        }\n"
"    }\n"
"    if (uAlphaKey) {\n"
#ifndef BZ_USE_MSAA
"        if (col.a < uAlphaCutoff) discard;\n"
#else
"        float edge = max(fwidth(col.a), 1.0 / 255.0);\n"
"        col.a = smoothstep(uAlphaCutoff - edge, uAlphaCutoff + edge, col.a);\n"
#endif
"    }\n"
"    o_color = col;\n"
"}\n";

static LPSHADER model_shader;

/* Returns the shared model shader, compiling it on first call. All three model
   formats (MDX/M2/M3) use this single shader; per-format data is normalised at
   load time so the GPU path is identical. */
LPSHADER R_ModelShader(void) {
    if (!model_shader) {
        model_shader = R_InitShader(model_vs, model_fs);
        if (model_shader) {
            R_Call(glUseProgram, model_shader->progid);
            R_Call(glUniform1f, model_shader->uAlphaCutoff, 0.5f);
        }
    }
    return model_shader ? model_shader : tr.shader[SHADER_DEFAULT];
}


static LPSHADER R_InitShaderDefines(LPCSTR vs_src, LPCSTR fs_src, LPCSTR extra_defines);

static LPSHADER instanced_shader;

/* Instanced model shader for static meshes (ground-effect clutter). Uses model_vs
   compiled with BZ_USE_INSTANCING to replace uModelMatrix with per-instance attributes. */
LPSHADER R_ModelShaderInstanced(void) {
    if (!instanced_shader) {
        MATRIX4 bones[128];

        instanced_shader = R_InitShaderDefines(model_vs, model_fs, "#define BZ_USE_INSTANCING 1\n");
        if (instanced_shader) {
            FOR_LOOP(i, 128) Matrix4_identity(&bones[i]);
            R_Call(glUseProgram, instanced_shader->progid);
            R_Call(glUniform1f, instanced_shader->uAlphaCutoff, 0.5f);
            /* Static grass has no keyed bones; install its identity palette once, not once per frame. */
            R_Call(glUniformMatrix4fv, instanced_shader->uBones, tr.bone_count, GL_FALSE, bones[0].v);
        }
    }
    return instanced_shader;
}

/* GLES 3 uses GLSL ES 300; shader bodies otherwise share GLSL 1.40 syntax.
   extra_defines is injected between the version/BZ_BONE_COUNT prefix and the shader body. */
static void R_SetShaderSource(GLuint shader, LPCSTR source, LPCSTR extra_defines) {
    char prefix[160];
    LPCSTR body = strchr(source, '\n');
    snprintf(prefix, sizeof(prefix),
#ifdef BZ_GL_ES3
             "#version 300 es\nprecision highp float;\nprecision highp int;\n#define BZ_BONE_COUNT %u\n",
#else
             "#version 140\n#define BZ_BONE_COUNT %u\n",
#endif
             (unsigned)tr.bone_count);
    LPCSTR strings[] = { prefix, extra_defines ? extra_defines : "", body ? body + 1 : source };
    GLint lengths[] = { (GLint)strlen(strings[0]), (GLint)strlen(strings[1]), (GLint)strlen(strings[2]) };
    R_Call(glShaderSource, shader, 3, strings, lengths);
}

static LPSHADER R_InitShaderDefines(LPCSTR vs_src, LPCSTR fs_src, LPCSTR extra_defines) {
    GLuint vs = R_Call(glCreateShader, GL_VERTEX_SHADER);
    GLuint fs = R_Call(glCreateShader, GL_FRAGMENT_SHADER);

    R_SetShaderSource(vs, vs_src, extra_defines);
    R_Call(glCompileShader, vs);

    GLint status;
    R_Call(glGetShaderiv, vs, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLint logLength = 0;
        glGetShaderiv(vs, GL_INFO_LOG_LENGTH, &logLength);
        if (logLength > 1) {
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
    R_SetShaderSource(fs, fs_src, extra_defines);
    R_Call(glCompileShader, fs);

    R_Call(glGetShaderiv, fs, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLint logLength = 0;
        glGetShaderiv(fs, GL_INFO_LOG_LENGTH, &logLength);
        if (logLength > 1) {
            char *log = malloc(logLength);
            if (log) {
                glGetShaderInfoLog(fs, logLength, NULL, log);
                fprintf(stderr, "Fragment shader compilation failed:\n%s\n", log);
                free(log);
            } else {
                fprintf(stderr, "Fragment shader compilation failed (could not allocate log buffer)\n");
            }
        } else {
            fprintf(stderr, "Fragment shader compilation failed (no log)\n");
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
    R_Call(glBindAttribLocation, program->progid, attrib_boneWeight1, "i_boneWeight1");
    R_Call(glBindAttribLocation, program->progid, attrib_particleSize, "i_size");
    R_Call(glBindAttribLocation, program->progid, attrib_particleAxis, "i_axis");
    R_Call(glBindAttribLocation, program->progid, attrib_instance0, "i_instance0");
    R_Call(glBindAttribLocation, program->progid, attrib_instance1, "i_instance1");
    R_Call(glBindAttribLocation, program->progid, attrib_instance2, "i_instance2");
    R_Call(glBindAttribLocation, program->progid, attrib_instance3, "i_instance3");

    R_Call(glLinkProgram, program->progid);
    R_Call(glUseProgram, program->progid);

#define R_RegisterUniform(PROGRAM, NAME) PROGRAM->NAME = glGetUniformLocation(PROGRAM->progid, #NAME);

    R_RegisterUniform(program, uViewProjectionMatrix);
    R_RegisterUniform(program, uModelMatrix);
    R_RegisterUniform(program, uLightMatrix);
    R_RegisterUniform(program, uNormalMatrix);
    R_RegisterUniform(program, uTextureMatrix);
    R_RegisterUniform(program, uTexture);
#ifdef USE_SHADOWMAPS
    R_RegisterUniform(program, uShadowmap);
#endif
    R_RegisterUniform(program, uFogOfWar);
    R_RegisterUniform(program, uBones);
    R_RegisterUniform(program, uAlphaKey);
    R_RegisterUniform(program, uAlphaCutoff);
    R_RegisterUniform(program, uUnshaded);
    R_RegisterUniform(program, uLayerAlpha);
    R_RegisterUniform(program, uGeosetColor);
    R_RegisterUniform(program, uUvMatrix);
    R_RegisterUniform(program, uLightDir);
    R_RegisterUniform(program, uLightColor);
    R_RegisterUniform(program, uLightAmbient);
    R_RegisterUniform(program, uLightCount);
    program->uLights = glGetUniformLocation(program->progid, "uLights[0]");
    R_RegisterUniform(program, uEyePosition);
    R_RegisterUniform(program, uActiveGlow);
    R_RegisterUniform(program, uFogEnable);
    R_RegisterUniform(program, uFogColor);
    R_RegisterUniform(program, uFogParams);
    R_RegisterUniform(program, uFirstBoneLookupIndex);

    R_Call(glUniform1i, program->uTexture, 0);
#ifdef USE_SHADOWMAPS
    R_Call(glUniform1i, program->uShadowmap, 1);
#endif
    R_Call(glUniform1i, program->uFogOfWar, 2);
    /* UV transform defaults to identity so callers that don't animate UVs can skip the upload. */
    GLfloat identity_uv[9] = { 1,0,0, 0,1,0, 0,0,1 };
    R_Call(glUniformMatrix3fv, program->uUvMatrix, 1, GL_FALSE, identity_uv);

    return program;
}

LPSHADER R_InitShader(LPCSTR vs_default, LPCSTR fs_default) {
    return R_InitShaderDefines(vs_default, fs_default, NULL);
}

void R_ReleaseShader(LPSHADER shader) {
    ri.MemFree(shader);
}

void R_ShutdownModelShader(void) {
    SAFE_DELETE(model_shader, R_ReleaseShader);
    SAFE_DELETE(instanced_shader, R_ReleaseShader);
}
