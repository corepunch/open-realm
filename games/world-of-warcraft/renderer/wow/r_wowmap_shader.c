#include "r_wowmap.h"

LPSHADER wow_terrain_shader;
LPSHADER wow_grass_shader;
GLint wow_uTexture0 = -1;
GLint wow_uTexture1 = -1;
GLint wow_uTexture2 = -1;
GLint wow_uTexture3 = -1;
GLint wow_uAlphaTexture = -1;
GLint wow_uUseWeightedBlend = -1;
GLint wow_uAlphaOrigin = -1;
GLint wow_uAlphaAtlasChunks = -1;
GLint wow_uGrassTime = -1;
GLint wow_uGrassCameraOrigin = -1;
GLint wow_uGrassDrawDistance = -1;

void Wow_InitTerrainShader(void) {
    static LPCSTR vs_wow_terrain =
    "#version 140\n"
    "in vec3 i_position;\n"
    "in vec3 i_normal;\n"
    "in vec2 i_texcoord;\n"
    "in vec4 i_color;\n"
    "out vec2 v_texcoord;\n"
    "out vec4 v_color;\n"
    "out float v_lighting;\n"
    "uniform mat4 uViewProjectionMatrix;\n"
    "uniform mat4 uModelMatrix;\n"
    "uniform mat4 uLightMatrix;\n"
    "uniform vec3 uLightDir;\n"
    "uniform mat3 uNormalMatrix;\n"
    "void main() {\n"
    "    vec4 pos = uModelMatrix * vec4(i_position, 1.0);\n"
    "    v_texcoord = i_texcoord;\n"
    "    vec3 normal = normalize(uNormalMatrix * i_normal);\n"
    "    vec3 lightDir = normalize(uLightDir) * 1.2;\n"
    "    float light = clamp(dot(normal, lightDir), 0.0, 1.0);\n"
    "    v_lighting = mix(0.5, 1.0, light);\n"
    "    v_color = i_color;\n"
    "    gl_Position = uViewProjectionMatrix * pos;\n"
    "}\n";
    static LPCSTR fs_wow_terrain =
    "#version 140\n"
    "in vec2 v_texcoord;\n"
    "in vec4 v_color;\n"
    "in float v_lighting;\n"
    "out vec4 o_color;\n"
    "uniform sampler2D uTexture0;\n"
    "uniform sampler2D uTexture1;\n"
    "uniform sampler2D uTexture2;\n"
    "uniform sampler2D uTexture3;\n"
    "uniform sampler2D uAlphaTexture;\n"
    "uniform int uUseWeightedBlend;\n"
    "uniform vec2 uAlphaOrigin;\n"
    "uniform float uAlphaAtlasChunks;\n"
    "vec2 adtAlphaCoord(vec2 chunkCoord) {\n"
    "    const float alphaTexelsPerChunk = 64.0;\n"
    "    float alphaAtlasSize = alphaTexelsPerChunk * uAlphaAtlasChunks;\n"
    "    chunkCoord = clamp(chunkCoord, vec2(0.0), vec2(1.0));\n"
    "    vec2 atlasTexel = uAlphaOrigin * alphaTexelsPerChunk + chunkCoord * (alphaTexelsPerChunk - 1.0) + vec2(0.5);\n"
    "    return atlasTexel / alphaAtlasSize;\n"
    "}\n"
    "void main() {\n"
    "    vec2 alphaCoord = adtAlphaCoord(v_texcoord * 0.125);\n"
    "    vec3 alphaBlend = texture(uAlphaTexture, alphaCoord).gba;\n"
    "    vec4 tex1 = texture(uTexture0, v_texcoord);\n"
    "    vec4 tex2 = texture(uTexture1, v_texcoord);\n"
    "    vec4 tex3 = texture(uTexture2, v_texcoord);\n"
    "    vec4 tex4 = texture(uTexture3, v_texcoord);\n"
    "    vec4 color;\n"
    "    if (uUseWeightedBlend != 0) {\n"
    "        float baseWeight = 1.0 - clamp(dot(alphaBlend, vec3(1.0)), 0.0, 1.0);\n"
    "        vec4 weights = vec4(baseWeight, alphaBlend);\n"
    "        color = tex1 * weights.r + tex2 * weights.g + tex3 * weights.b + tex4 * weights.a;\n"
    "    } else {\n"
    "        color = mix(mix(mix(tex1, tex2, alphaBlend.r), tex3, alphaBlend.g), tex4, alphaBlend.b);\n"
    "    }\n"
    "    color.rgb *= 2.0 * v_color.rgb * v_lighting;\n"
    "    color.a = 1.0;\n"
    "    o_color = color;\n"
    "}\n";

    if (wow_terrain_shader) {
        return;
    }

    wow_terrain_shader = R_InitShader(vs_wow_terrain, fs_wow_terrain);
    if (!wow_terrain_shader) {
        return;
    }

    wow_uTexture0 = glGetUniformLocation(wow_terrain_shader->progid, "uTexture0");
    wow_uTexture1 = glGetUniformLocation(wow_terrain_shader->progid, "uTexture1");
    wow_uTexture2 = glGetUniformLocation(wow_terrain_shader->progid, "uTexture2");
    wow_uTexture3 = glGetUniformLocation(wow_terrain_shader->progid, "uTexture3");
    wow_uAlphaTexture = glGetUniformLocation(wow_terrain_shader->progid, "uAlphaTexture");
    wow_uUseWeightedBlend = glGetUniformLocation(wow_terrain_shader->progid, "uUseWeightedBlend");
    wow_uAlphaOrigin = glGetUniformLocation(wow_terrain_shader->progid, "uAlphaOrigin");
    wow_uAlphaAtlasChunks = glGetUniformLocation(wow_terrain_shader->progid, "uAlphaAtlasChunks");
    R_Call(glUseProgram, wow_terrain_shader->progid);
    R_Call(glUniform1i, wow_uTexture0, 0);
    R_Call(glUniform1i, wow_uTexture1, 1);
    R_Call(glUniform1i, wow_uTexture2, 2);
    R_Call(glUniform1i, wow_uTexture3, 3);
    R_Call(glUniform1i, wow_uAlphaTexture, 4);
    R_Call(glUniform1f, wow_uAlphaAtlasChunks, (GLfloat)WOW_ALPHA_ATLAS_CHUNKS);
}

void Wow_InitGrassShader(void) {
    static LPCSTR vs_wow_grass =
    "#version 140\n"
    "in vec3 i_position;\n"
    "in vec3 i_normal;\n"
    "in vec2 i_texcoord;\n"
    "in vec4 i_color;\n"
    "out vec4 v_color;\n"
    "out vec2 v_uv;\n"
    "out vec3 v_world;\n"
    "out float v_lighting;\n"
    "uniform mat4 uViewProjectionMatrix;\n"
    "uniform mat4 uLightMatrix;\n"
    "uniform vec3 uLightDir;\n"
    "uniform float uGrassTime;\n"
    "void main() {\n"
    "    float top = clamp(i_texcoord.y, 0.0, 1.0);\n"
    "    vec3 pos = i_position;\n"
    "    float wave = sin(uGrassTime * 1.7 + dot(pos.xy, vec2(0.071, 0.049))) * 0.22;\n"
    "    pos.xy += vec2(wave, wave * 0.35) * top;\n"
    "    v_world = pos;\n"
    "    v_color = i_color;\n"
    "    v_uv = i_texcoord;\n"
    "    vec3 lightDir = normalize(uLightDir) * 1.2;\n"
    "    float light = abs(dot(normalize(i_normal), lightDir));\n"
    "    v_lighting = mix(0.55, 1.0, clamp(light, 0.0, 1.0));\n"
    "    gl_Position = uViewProjectionMatrix * vec4(pos, 1.0);\n"
    "}\n";
    static LPCSTR fs_wow_grass =
    "#version 140\n"
    "in vec4 v_color;\n"
    "in vec2 v_uv;\n"
    "in vec3 v_world;\n"
    "in float v_lighting;\n"
    "out vec4 o_color;\n"
    "uniform vec3 uGrassCameraOrigin;\n"
    "uniform float uGrassDrawDistance;\n"
    "void main() {\n"
    "    float d = distance(v_world.xy, uGrassCameraOrigin.xy);\n"
    "    float fade = 1.0 - smoothstep(uGrassDrawDistance * 0.72, uGrassDrawDistance, d);\n"
    "    float width = 1.0 - abs(v_uv.x * 2.0 - 1.0);\n"
    "    float edge = smoothstep(0.24, 0.46, width);\n"
    "    float root = smoothstep(0.02, 0.14, v_uv.y);\n"
    "    float tip = 1.0 - smoothstep(0.84, 1.00, v_uv.y);\n"
    "    float blade = edge * root * tip;\n"
    "    float alpha = v_color.a * fade * blade * 0.55;\n"
    "    if (alpha <= 0.02) discard;\n"
    "    o_color = vec4(v_color.rgb * v_lighting, alpha);\n"
    "}\n";

    if (wow_grass_shader) {
        return;
    }

    wow_grass_shader = R_InitShader(vs_wow_grass, fs_wow_grass);
    if (!wow_grass_shader) {
        return;
    }

    wow_uGrassTime = glGetUniformLocation(wow_grass_shader->progid, "uGrassTime");
    wow_uGrassCameraOrigin = glGetUniformLocation(wow_grass_shader->progid, "uGrassCameraOrigin");
    wow_uGrassDrawDistance = glGetUniformLocation(wow_grass_shader->progid, "uGrassDrawDistance");
}
