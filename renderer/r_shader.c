#include "r_local.h"
#include "r_shader.h"

/* -----------------------------------------------------------------------
 * Built-in renderer programs, described entirely as shader_desc_t.  Bodies
 * define vec4 vert()/frag(); the version prologue, declarations, and main()
 * are generated from the descriptor tables at load time.
 *
 * Each program owns GL handles plus a separate typed value state. Descriptor
 * offsets address values; R_ApplyShader submits them at the draw boundary.
 * ----------------------------------------------------------------------- */

/* --- unlit / ui: texture * vertex-color, no lighting ------------------- */
#define SHADER_TYPE SPRITESTATE
const shader_desc_t sd_unlit = {
    .Name = "unlit",
    .Uniforms = {
        UNIFORM(viewProjection, UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(model,          UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(texture,        UT_SAMPLER_2D, PRECISION_LOW),
    },
    .Attributes = {
        ATTRIB(position, attrib_position, UT_FLOAT_VEC3),
        ATTRIB(texcoord, attrib_texcoord, UT_FLOAT_VEC2),
        ATTRIB(color,    attrib_color,    UT_COLOR),
    },
    .Shared = {
        SHARED(texcoord, UT_FLOAT_VEC2),
        SHARED(color,    UT_COLOR),
    },
    .VertexBody =
        "vec4 vert() {\n"
        "  v_texcoord = a_texcoord;\n"
        "  v_color = a_color;\n"
        "  return u_viewProjection * u_model * vec4(a_position, 1.0);\n"
        "}\n",
    .FragmentBody =
        "vec4 frag() {\n"
        "  return texture(u_texture, v_texcoord) * v_color;\n"
        "}\n",
};
#undef SHADER_TYPE

/* --- minimap: circular mask applied to alpha ---------------------------- */
#define SHADER_TYPE SPRITESTATE
const shader_desc_t sd_minimap = {
    .Name = "minimap",
    .Uniforms = {
        UNIFORM(viewProjection, UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(model,          UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(texture,        UT_SAMPLER_2D, PRECISION_LOW),
    },
    .Attributes = {
        ATTRIB(position, attrib_position, UT_FLOAT_VEC3),
        ATTRIB(texcoord, attrib_texcoord, UT_FLOAT_VEC2),
        ATTRIB(color,    attrib_color,    UT_COLOR),
    },
    .Shared = {
        SHARED(texcoord, UT_FLOAT_VEC2),
        SHARED(color,    UT_COLOR),
    },
    .VertexBody =
        "vec4 vert() {\n"
        "  v_texcoord = a_texcoord;\n"
        "  v_color = a_color;\n"
        "  return u_viewProjection * u_model * vec4(a_position, 1.0);\n"
        "}\n",
    .FragmentBody =
        "vec4 frag() {\n"
        "  float mask = 1.0 - smoothstep(0.49, 0.5, length(v_color.rg - vec2(0.5)));\n"
        "  vec4 tex = texture(u_texture, v_texcoord);\n"
        "  return vec4(tex.rgb, tex.a * mask);\n"
        "}\n",
};
#undef SHADER_TYPE

/* --- splat: crop edges to [0,1] bounds ---------------------------------- */
#define SHADER_TYPE SPRITESTATE
const shader_desc_t sd_splat = {
    .Name = "splat",
    .Uniforms = {
        UNIFORM(viewProjection, UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(model,          UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(texture,        UT_SAMPLER_2D, PRECISION_LOW),
    },
    .Attributes = {
        ATTRIB(position, attrib_position, UT_FLOAT_VEC3),
        ATTRIB(texcoord, attrib_texcoord, UT_FLOAT_VEC2),
        ATTRIB(color,    attrib_color,    UT_COLOR),
    },
    .Shared = {
        SHARED(texcoord, UT_FLOAT_VEC2),
        SHARED(color,    UT_COLOR),
    },
    .VertexBody =
        "vec4 vert() {\n"
        "  v_texcoord = a_texcoord;\n"
        "  v_color = a_color;\n"
        "  return u_viewProjection * u_model * vec4(a_position, 1.0);\n"
        "}\n",
    .FragmentBody =
        "float crop_edges(vec2 tc) {\n"
        "  return step(abs(tc.x - 0.5), 0.5) * step(abs(tc.y - 0.5), 0.5);\n"
        "}\n"
        "vec4 frag() {\n"
        "  vec4 col = texture(u_texture, v_texcoord) * v_color;\n"
        "  col.a *= crop_edges(v_texcoord);\n"
        "  return col;\n"
        "}\n",
};
#undef SHADER_TYPE

/* --- shadow splat: black silhouette with texture alpha ------------------ */
#define SHADER_TYPE SPRITESTATE
const shader_desc_t sd_shadow_splat = {
    .Name = "shadow_splat",
    .Uniforms = {
        UNIFORM(viewProjection, UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(model,          UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(texture,        UT_SAMPLER_2D, PRECISION_LOW),
    },
    .Attributes = {
        ATTRIB(position, attrib_position, UT_FLOAT_VEC3),
        ATTRIB(texcoord, attrib_texcoord, UT_FLOAT_VEC2),
        ATTRIB(color,    attrib_color,    UT_COLOR),
    },
    .Shared = {
        SHARED(texcoord, UT_FLOAT_VEC2),
        SHARED(color,    UT_COLOR),
    },
    .VertexBody =
        "vec4 vert() {\n"
        "  v_texcoord = a_texcoord;\n"
        "  v_color = a_color;\n"
        "  return u_viewProjection * u_model * vec4(a_position, 1.0);\n"
        "}\n",
    .FragmentBody =
        "float crop_edges(vec2 tc) {\n"
        "  return step(abs(tc.x - 0.5), 0.5) * step(abs(tc.y - 0.5), 0.5);\n"
        "}\n"
        "vec4 frag() {\n"
        "  vec4 tex = texture(u_texture, v_texcoord);\n"
        "  return vec4(0.0, 0.0, 0.0, tex.a * v_color.a * crop_edges(v_texcoord));\n"
        "}\n",
};
#undef SHADER_TYPE

/* --- commandbutton: edge glow controlled by u_activeGlow ---------------- */
#define SHADER_TYPE SPRITESTATE
const shader_desc_t sd_commandbutton = {
    .Name = "commandbutton",
    .Uniforms = {
        UNIFORM(viewProjection, UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(model,          UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(texture,        UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(activeGlow,     UT_FLOAT,      PRECISION_LOW),
    },
    .Attributes = {
        ATTRIB(position, attrib_position, UT_FLOAT_VEC3),
        ATTRIB(texcoord, attrib_texcoord, UT_FLOAT_VEC2),
        ATTRIB(color,    attrib_color,    UT_COLOR),
    },
    .Shared = {
        SHARED(texcoord, UT_FLOAT_VEC2),
        SHARED(color,    UT_COLOR),
    },
    .VertexBody =
        "vec4 vert() {\n"
        "  v_texcoord = a_texcoord;\n"
        "  v_color = a_color;\n"
        "  return u_viewProjection * u_model * vec4(a_position, 1.0);\n"
        "}\n",
    .FragmentBody =
        "vec4 frag() {\n"
        "  vec4 col = texture(u_texture, v_texcoord) * v_color;\n"
        "  float glow = max(abs(v_texcoord.x - 0.5), abs(v_texcoord.y - 0.5));\n"
        "  glow = smoothstep(0.33, 0.5, glow) * 0.75 * u_activeGlow;\n"
        "  col.rgb = mix(col.rgb, vec3(0.5, 1.0, 0.5), glow);\n"
        "  float crop = step(abs(v_texcoord.x - 0.5), 0.5) * step(abs(v_texcoord.y - 0.5), 0.5);\n"
        "  col.a *= crop;\n"
        "  return col;\n"
        "}\n",
};
#undef SHADER_TYPE

/* --- minimap fog: fog-of-war overlay with y-flip ------------------------ */
#define SHADER_TYPE SPRITESTATE
const shader_desc_t sd_minimap_fog = {
    .Name = "minimap_fog",
    .Uniforms = {
        UNIFORM(viewProjection, UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(model,          UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(texture,        UT_SAMPLER_2D, PRECISION_LOW),
    },
    .Attributes = {
        ATTRIB(position, attrib_position, UT_FLOAT_VEC3),
        ATTRIB(texcoord, attrib_texcoord, UT_FLOAT_VEC2),
        ATTRIB(color,    attrib_color,    UT_COLOR),
    },
    .Shared = {
        SHARED(texcoord, UT_FLOAT_VEC2),
        SHARED(color,    UT_COLOR),
    },
    .VertexBody =
        "vec4 vert() {\n"
        "  v_texcoord = a_texcoord;\n"
        "  v_color = a_color;\n"
        "  return u_viewProjection * u_model * vec4(a_position, 1.0);\n"
        "}\n",
    .FragmentBody =
        "vec4 frag() {\n"
        "  float visibility = texture(u_texture, vec2(v_texcoord.x, 1.0 - v_texcoord.y)).r;\n"
        "  float alpha = clamp(1.0 - visibility, 0.0, 1.0) * v_color.a;\n"
        "  return vec4(v_color.rgb, alpha);\n"
        "}\n",
};
#undef SHADER_TYPE

/* --- default: ground/world sprite with per-vertex lighting --------------- */
#define SHADER_TYPE DEFAULTSTATE
const shader_desc_t sd_default = {
    .Name = "default",
    .Uniforms = {
        UNIFORM(viewProjection, UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(textureMatrix,  UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(model,          UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(lightMatrix,    UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(normalMatrix,   UT_FLOAT_MAT3_TRANSPOSE, PRECISION_HIGH),
        UNIFORM(lightCount,     UT_INT,        PRECISION_LOW),
        UNIFORM(lights,         UT_FLOAT_MAT4, PRECISION_HIGH, BZ_MODEL_LIGHT_MAX),
        UNIFORM(texture,        UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(shadowmap,      UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(fogOfWar,       UT_SAMPLER_2D, PRECISION_LOW),
    },
    .Attributes = {
        ATTRIB(position, attrib_position, UT_FLOAT_VEC3),
        ATTRIB(texcoord, attrib_texcoord, UT_FLOAT_VEC2),
        ATTRIB(normal,   attrib_normal,   UT_FLOAT_VEC3),
        ATTRIB(color,    attrib_color,    UT_COLOR),
    },
    .Shared = {
        SHARED(texcoord,    UT_FLOAT_VEC2),
        SHARED(texcoord2,   UT_FLOAT_VEC2),
        SHARED(normal,      UT_FLOAT_VEC3),
        SHARED(lightDir,    UT_FLOAT_VEC3),
        SHARED(lighting,    UT_FLOAT_VEC3),
        SHARED(shadowlight, UT_FLOAT_VEC3),
        SHARED(color,       UT_COLOR),
        SHARED(shadow,      UT_FLOAT_VEC4),
    },
    .VertexBody =
        "const int MODEL_LIGHT_OMNI = 0;\n"
        "const int MODEL_LIGHT_DIRECT = 1;\n"
        "const int MODEL_LIGHT_AMBIENT = 2;\n"
        "vec3 apply_environment_light(mat4 light, vec3 n, vec3 worldPos) {\n"
        "  int type = int(light[0].w + 0.5);\n"
        "  vec3 color = light[2].rgb * light[2].a;\n"
        "  vec3 ambient = light[3].rgb * light[3].a;\n"
        "  if (type == MODEL_LIGHT_AMBIENT) return color + ambient;\n"
        "  if (type == MODEL_LIGHT_DIRECT) {\n"
        "    vec3 l = normalize(-light[1].xyz);\n"
        "    return clamp(color * max(dot(n, l), 0.0), vec3(0.0), vec3(1.0)) + ambient;\n"
        "  }\n"
        "  vec3 delta = light[0].xyz - worldPos;\n"
        "  vec3 l = normalize(delta);\n"
        "  float dist = length(delta) / 64.0 + 1.0;\n"
        "  float atten = 1.0 / (dist * dist);\n"
        "  return clamp(color * atten * max(dot(n, l), 0.0), vec3(0.0), vec3(1.0)) + ambient * atten;\n"
        "}\n"
        "vec3 environment_lighting(vec3 normal, vec3 worldPos) {\n"
        "  vec3 n = normalize(normal);\n"
        "  vec3 result = vec3(0.0);\n"
        "  v_shadowlight = vec3(0.0);\n"
        "  for (int i = 0; i < 8; ++i) {\n"
        "    if (i >= u_lightCount) break;\n"
        "    vec3 contribution = apply_environment_light(u_lights[i], n, worldPos);\n"
        "    result += contribution;\n"
        "    if (i == 0 && int(u_lights[i][0].w + 0.5) == MODEL_LIGHT_DIRECT)\n"
        "      v_shadowlight = contribution - u_lights[i][3].rgb * u_lights[i][3].a;\n"
        "  }\n"
        "  return result;\n"
        "}\n"
        "vec4 vert() {\n"
        "  vec4 pos = u_model * vec4(a_position, 1.0);\n"
        "  v_texcoord = a_texcoord;\n"
        "  v_texcoord2 = (u_textureMatrix * pos).xy;\n"
        "  v_normal = normalize(u_normalMatrix * a_normal);\n"
        "  v_shadowlight = vec3(0.0);\n"
        "  v_lighting = u_lightCount > 0 ? environment_lighting(v_normal, pos.xyz) : vec3(0.0);\n"
        "#ifdef USE_SHADOWMAPS\n"
        "  v_shadow = u_lightMatrix * pos;\n"
        "#endif\n"
        "  v_color = a_color;\n"
        "  v_lightDir = -normalize(vec3(u_lightMatrix[0][2], u_lightMatrix[1][2], u_lightMatrix[2][2])) * 1.2;\n"
        "  return u_viewProjection * pos;\n"
        "}\n",
    .FragmentBody =
        "float get_light() {\n"
        "  return dot(v_normal, v_lightDir);\n"
        "}\n"
        "#ifdef USE_SHADOWMAPS\n"
        "float get_shadow() {\n"
        "  float depth = texture(u_shadowmap, vec2(v_shadow.x + 1.0, v_shadow.y + 1.0) * 0.5).r;\n"
        "  return depth < (v_shadow.z + 0.99) * 0.5 ? 0.0 : 1.0;\n"
        "}\n"
        "vec3 get_lighting() {\n"
        "  if (u_lightCount > 0) return clamp(v_lighting - v_shadowlight * (1.0 - get_shadow()), vec3(0.0), vec3(1.0));\n"
        "  return vec3(min(1.0, mix(0.35, 1.0, get_shadow() * get_light()) * 1.1));\n"
        "}\n"
        "#else\n"
        "vec3 get_lighting() {\n"
        "  if (u_lightCount > 0) return clamp(v_lighting, vec3(0.0), vec3(1.0));\n"
        "  return vec3(min(1.0, mix(0.35, 1.0, get_light()) * 1.1));\n"
        "}\n"
        "#endif\n"
        "#ifdef USE_FOGOFWAR\n"
        "float get_fogofwar() {\n"
        "  return texture(u_fogOfWar, v_texcoord2).r;\n"
        "}\n"
        "#endif\n"
        "vec4 frag() {\n"
        "  vec4 col = texture(u_texture, v_texcoord) * v_color;\n"
        "#ifdef USE_FOGOFWAR\n"
        "  col.rgb *= get_fogofwar() * get_lighting();\n"
        "#else\n"
        "  col.rgb *= get_lighting();\n"
        "#endif\n"
        "  return col;\n"
        "}\n",
};
#undef SHADER_TYPE

/* --- model: shared skinned shader for MDX/M2/M3, compiled twice -----------
 * (normal + BZ_USE_INSTANCING).  USE_SHADOWMAPS/USE_FOGOFWAR/BZ_USE_MSAA are
 * injected as GLSL defines from the matching C preprocessor macros. */
#define SHADER_TYPE MODELSTATE
const shader_desc_t sd_model = {
    .Name = "model",
    .Uniforms = {
        UNIFORM(bones, UT_FLOAT_MAT4, PRECISION_HIGH, BZ_BONE_PALETTE_MAX, boneCount),
        UNIFORM(viewProjection,            UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(lightMatrix,               UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(textureMatrix,             UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(lightCount,                UT_INT,        PRECISION_LOW),
        UNIFORM(firstBoneLookupIndex,      UT_FLOAT,      PRECISION_LOW),
        UNIFORM(lights,                    UT_FLOAT_MAT4, PRECISION_HIGH, BZ_MODEL_LIGHT_MAX),
        UNIFORM(grassParams,               UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(model,                     UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(normalMatrix,       UT_FLOAT_MAT3_TRANSPOSE, PRECISION_HIGH),
        UNIFORM(texture,                   UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(shadowmap,                 UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(fogOfWar,                  UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(layerAlpha,                UT_FLOAT,      PRECISION_LOW),
        UNIFORM(geosetColor,               UT_FLOAT_VEC4, PRECISION_LOW),
        UNIFORM(uvMatrix,                  UT_FLOAT_MAT3, PRECISION_HIGH),
        UNIFORM(alphaKey,                  UT_BOOL,       PRECISION_LOW),
        UNIFORM(alphaCutoff,               UT_FLOAT,      PRECISION_LOW),
        UNIFORM(unshaded,                  UT_BOOL,       PRECISION_LOW),
        UNIFORM(fogEnable,                 UT_BOOL,       PRECISION_LOW),
        UNIFORM(fogColor,                  UT_FLOAT_VEC3, PRECISION_LOW),
        UNIFORM(fogParams,                 UT_FLOAT_VEC2, PRECISION_LOW),
    },
    .Attributes = {
        ATTRIB(position,     attrib_position,     UT_FLOAT_VEC3),
        ATTRIB(color,        attrib_color,        UT_COLOR),
        ATTRIB(texcoord,     attrib_texcoord,     UT_FLOAT_VEC2),
        ATTRIB(normal,       attrib_normal,       UT_FLOAT_VEC3),
        ATTRIB(skin1,        attrib_skin1,        UT_FLOAT_VEC4),
        ATTRIB(boneWeight1,  attrib_boneWeight1,  UT_FLOAT_VEC4),
        ATTRIB(instance,     attrib_instance,     UT_FLOAT_MAT4),
    },
    .Shared = {
        SHARED(color,       UT_COLOR),
        SHARED(shadow,      UT_FLOAT_VEC4),
        SHARED(shadowlight, UT_FLOAT_VEC3),
        SHARED(texcoord,    UT_FLOAT_VEC2),
        SHARED(texcoord2,   UT_FLOAT_VEC2),
        SHARED(lighting,    UT_FLOAT_VEC3),
    },
    .VertexBody =
        "const int MODEL_LIGHT_OMNI = 0;\n"
        "const int MODEL_LIGHT_DIRECT = 1;\n"
        "const int MODEL_LIGHT_AMBIENT = 2;\n"
        "vec3 apply_light(mat4 light, vec3 n, vec3 worldPos) {\n"
        "  int type = int(light[0].w + 0.5);\n"
        "  vec3 color = light[2].rgb * light[2].a;\n"
        "  vec3 ambient = light[3].rgb * light[3].a;\n"
        "  if (type == MODEL_LIGHT_AMBIENT) return color + ambient;\n"
        "  if (type == MODEL_LIGHT_DIRECT) {\n"
        "    vec3 l = normalize(-light[1].xyz);\n"
        "    return clamp(color * max(dot(n, l), 0.0), vec3(0.0), vec3(1.0)) + ambient;\n"
        "  }\n"
        "  vec3 delta = light[0].xyz - worldPos;\n"
        "  vec3 l = normalize(delta);\n"
        "  float dist = length(delta) / 64.0 + 1.0;\n"
        "  float atten = 1.0 / (dist * dist);\n"
        "  return clamp(color * atten * max(dot(n, l), 0.0), vec3(0.0), vec3(1.0)) + ambient * atten;\n"
        "}\n"
        "vec3 vertex_lighting(vec3 normal, vec3 worldPos) {\n"
        "  vec3 n = normalize(normal);\n"
        "  vec3 lighting = vec3(0.0);\n"
        "#ifdef USE_SHADOWMAPS\n"
        "  v_shadowlight = vec3(0.0);\n"
        "#endif\n"
        "  for (int i = 0; i < 8; ++i) {\n"
        "    if (i >= u_lightCount) break;\n"
        "    vec3 contribution = apply_light(u_lights[i], n, worldPos);\n"
        "    lighting += contribution;\n"
        "#ifdef USE_SHADOWMAPS\n"
        "    if (i == 0 && int(u_lights[i][0].w + 0.5) == MODEL_LIGHT_DIRECT)\n"
        "      v_shadowlight = contribution - u_lights[i][3].rgb * u_lights[i][3].a;\n"
        "#endif\n"
        "  }\n"
        "  return lighting;\n"
        "}\n"
        "vec4 vert() {\n"
        "  vec4 pos4 = vec4(a_position, 1.0);\n"
        "  vec4 norm4 = vec4(a_normal, 0.0);\n"
        "  vec4 position = vec4(0.0);\n"
        "  vec4 normal = vec4(0.0);\n"
        "  for (int i = 0; i < 4; ++i) {\n"
        "    int boneIdx = int(a_skin1[i]) + int(u_firstBoneLookupIndex);\n"
        "    position += u_bones[boneIdx] * pos4 * a_boneWeight1[i];\n"
        "    normal += u_bones[boneIdx] * norm4 * a_boneWeight1[i];\n"
        "  }\n"
        "  position.w = 1.0;\n"
        "#ifdef BZ_USE_INSTANCING\n"
        "  if (u_grassParams[3].z > 0.5) {\n"
        "    float grassHeight = max(u_grassParams[3].y - u_grassParams[3].x, 0.001);\n"
        "    float grassTop = smoothstep(u_grassParams[1].w, 1.0, clamp((position.z - u_grassParams[3].x) / grassHeight, 0.0, 1.0));\n"
        "    float grassPhase = dot(a_instance[3].xy, u_grassParams[2].xy);\n"
        "    float grassSway = sin(u_grassParams[1].x * u_grassParams[1].y + grassPhase) * u_grassParams[1].z * grassHeight * grassTop;\n"
        "    position.xy += u_grassParams[2].zw * grassSway;\n"
        "  }\n"
        "  vec4 worldPos4 = a_instance * position;\n"
        "  v_color = a_color;\n"
        "  if (u_grassParams[3].z > 0.5) {\n"
        "    float fadeDist = length(worldPos4.xy - u_grassParams[0].xy);\n"
        "    v_color.a *= 1.0 - smoothstep(u_grassParams[0].z, u_grassParams[0].w, fadeDist);\n"
        "  }\n"
        "  v_texcoord = a_texcoord;\n"
        "  v_texcoord2 = (u_textureMatrix * worldPos4).xy;\n"
        "  v_lighting = vertex_lighting(normalize(mat3(a_instance) * normal.xyz), worldPos4.xyz);\n"
        "#ifdef USE_SHADOWMAPS\n"
        "  v_shadow = u_lightMatrix * worldPos4;\n"
        "#endif\n"
        "  return u_viewProjection * worldPos4;\n"
        "#else\n"
        "  v_color = a_color;\n"
        "  v_texcoord = a_texcoord;\n"
        "  v_texcoord2 = (u_textureMatrix * u_model * position).xy;\n"
        "  vec3 worldNormal = normalize(u_normalMatrix * normal.xyz);\n"
        "  vec3 worldPos = (u_model * position).xyz;\n"
        "  v_lighting = vertex_lighting(worldNormal, worldPos);\n"
        "#ifdef USE_SHADOWMAPS\n"
        "  v_shadow = u_lightMatrix * u_model * position;\n"
        "#endif\n"
        "  return u_viewProjection * u_model * position;\n"
        "#endif\n"
        "}\n",
    .FragmentBody =
        "#ifdef USE_FOGOFWAR\n"
        "float get_fogofwar() {\n"
        "  return texture(u_fogOfWar, v_texcoord2).r;\n"
        "}\n"
        "#endif\n"
        "#ifdef USE_SHADOWMAPS\n"
        BZ_SHADOW_GLSL
        "#endif\n"
        "vec4 frag() {\n"
        "  vec2 uv = (u_uvMatrix * vec3(v_texcoord, 1.0)).xy;\n"
        "  vec4 col = texture(u_texture, uv);\n"
        "  col *= u_geosetColor;\n"
        "  col *= u_layerAlpha;\n"
        "  col *= v_color;\n"
        "  if (!u_unshaded) {\n"
        "    vec3 light = v_lighting;\n"
        "#ifdef USE_SHADOWMAPS\n"
        "    light -= v_shadowlight * (1.0 - shadow_visibility(u_shadowmap, v_shadow));\n"
        "#endif\n"
        "    light = min(light, vec3(1.0));\n"
        "#ifdef USE_FOGOFWAR\n"
        "    col.rgb *= get_fogofwar() * light;\n"
        "#else\n"
        "    col.rgb *= light;\n"
        "#endif\n"
        "    if (u_fogEnable) {\n"
        "      float fogFactor = clamp((u_fogParams.y - gl_FragCoord.z / gl_FragCoord.w) / (u_fogParams.y - u_fogParams.x), 0.0, 1.0);\n"
        "      col.rgb = mix(u_fogColor, col.rgb, fogFactor);\n"
        "    }\n"
        "  }\n"
        "  if (u_alphaKey) {\n"
        "#ifndef BZ_USE_MSAA\n"
        "    if (col.a < u_alphaCutoff) discard;\n"
        "#else\n"
        "    float edge = max(fwidth(col.a), 1.0 / 255.0);\n"
        "    col.a = smoothstep(u_alphaCutoff - edge, u_alphaCutoff + edge, col.a);\n"
        "#endif\n"
        "  }\n"
        "  return col;\n"
        "}\n",
};
#undef SHADER_TYPE

/* Compile-time GLSL defines derived from C build macros; prepended to every
   built-in program.  Extra per-variant defines (e.g. BZ_USE_INSTANCING) are
   added by the callers. */
static char shader_defines_buf[256];
static LPCSTR R_ShaderDefines(BOOL instancing) {
    int n = 0;
    /* Each variant owns its defines; the old retained instancing after the grass program compiled first. */
    shader_defines_buf[0] = '\0';
    if (instancing)
        n += snprintf(shader_defines_buf + n, sizeof(shader_defines_buf) - n, "#define BZ_USE_INSTANCING 1\n");
#ifdef USE_SHADOWMAPS
    n += snprintf(shader_defines_buf + n, sizeof(shader_defines_buf) - n, "#define USE_SHADOWMAPS 1\n");
#endif
#ifdef USE_FOGOFWAR
    n += snprintf(shader_defines_buf + n, sizeof(shader_defines_buf) - n, "#define USE_FOGOFWAR 1\n");
#endif
#ifdef BZ_USE_MSAA
    n += snprintf(shader_defines_buf + n, sizeof(shader_defines_buf) - n, "#define BZ_USE_MSAA 1\n");
#endif
    (void)n;
    return shader_defines_buf;
}

/* A compiled shader can still exceed resources at link time. Never draw with a failed program.
   ri.error only prints in the client, so termination must not rely on that callback. */
static void R_CheckShader(GLuint obj, GLenum check, LPCSTR label) {
    GLint ok = GL_FALSE, size = 0;
    if (check == GL_LINK_STATUS) glGetProgramiv(obj, check, &ok);
    else glGetShaderiv(obj, check, &ok);
    if (ok) return;
    if (check == GL_LINK_STATUS) glGetProgramiv(obj, GL_INFO_LOG_LENGTH, &size);
    else glGetShaderiv(obj, GL_INFO_LOG_LENGTH, &size);
    char *log = size > 1 ? malloc(size) : NULL;
    if (log) {
        log[0] = 0;
        if (check == GL_LINK_STATUS) glGetProgramInfoLog(obj, size, NULL, log);
        else glGetShaderInfoLog(obj, size, NULL, log);
    }
    fprintf(stderr, "%s failed: %s\n", label, log ? log : size > 1 ? "cannot allocate driver log" : "no driver log");
    free(log);
    exit(EXIT_FAILURE);
}

static const char *R_GLSLTypeStr(uniformType_t type) {
    switch (type) {
        case UT_FLOAT:            return "float";
        case UT_FLOAT_VEC2:       return "vec2";
        case UT_FLOAT_VEC3:       return "vec3";
        case UT_FLOAT_VEC4:       return "vec4";
        case UT_COLOR:            return "vec4";
        case UT_INT:              return "int";
        case UT_INT_VEC2:         return "ivec2";
        case UT_BOOL:             return "bool";
        case UT_FLOAT_MAT3:       return "mat3";
        case UT_FLOAT_MAT3_TRANSPOSE: return "mat3";
        case UT_FLOAT_MAT4:       return "mat4";
        case UT_SAMPLER_2D:       return "sampler2D";
        case UT_SAMPLER_2D_RECT:  return "sampler2DRect";
        case UT_SAMPLER_2D_ARRAY: return "sampler2DArray";
        default:                  return "float";
    }
}

int R_BuildShaderDeclarations(char *buf, int size, const shader_desc_t *desc,
                              bool is_vertex, glsl_dialect_t dialect) {
    const char *attr_kw  = (dialect == GLSL_DIALECT_120) ? "attribute" : "in";
    const char *vsout_kw = (dialect == GLSL_DIALECT_120) ? "varying"   : "out";
    const char *fsin_kw  = (dialect == GLSL_DIALECT_120) ? "varying"   : "in";
    int n = 0;

    /* GLSL 120 has no `texture` builtin; alias it so fragment bodies can
       call texture() regardless of dialect. */
    if (!is_vertex && dialect == GLSL_DIALECT_120)
        n += snprintf(buf + n, size - n, "#define texture texture2D\n");

    for (int i = 0; i < MAX_SHADER_UNIFORMS && desc->Uniforms[i].name; i++) {
        if (desc->Uniforms[i].count > 1)
            n += snprintf(buf + n, size - n, "uniform %s %s[%u];\n",
                          R_GLSLTypeStr(desc->Uniforms[i].type), desc->Uniforms[i].name,
                          desc->Uniforms[i].count);
        else
            n += snprintf(buf + n, size - n, "uniform %s %s;\n",
                          R_GLSLTypeStr(desc->Uniforms[i].type), desc->Uniforms[i].name);
    }

    if (is_vertex) {
        for (int i = 0; i < MAX_SHADER_ATTRIBS && desc->Attributes[i].name; i++)
            n += snprintf(buf + n, size - n, "%s %s %s;\n",
                          attr_kw, R_GLSLTypeStr(desc->Attributes[i].type), desc->Attributes[i].name);
        for (int i = 0; i < MAX_SHADER_SHARED && desc->Shared[i].name; i++)
            n += snprintf(buf + n, size - n, "%s %s %s;\n",
                          vsout_kw, R_GLSLTypeStr(desc->Shared[i].type), desc->Shared[i].name);
    } else {
        for (int i = 0; i < MAX_SHADER_SHARED && desc->Shared[i].name; i++)
            n += snprintf(buf + n, size - n, "%s %s %s;\n",
                          fsin_kw, R_GLSLTypeStr(desc->Shared[i].type), desc->Shared[i].name);
        if (dialect != GLSL_DIALECT_120)
            n += snprintf(buf + n, size - n, "out vec4 o_color;\n");
    }
    return n;
}

int R_BuildShaderMain(char *buf, int size, bool is_vertex, glsl_dialect_t dialect) {
    if (is_vertex)
        return snprintf(buf, size, "void main() { gl_Position = vert(); }\n");
    return snprintf(buf, size, "void main() { %s = frag(); }\n",
                    dialect == GLSL_DIALECT_120 ? "gl_FragColor" : "o_color");
}

static void R_SetShaderSourceFromDesc(GLuint stage, const shader_desc_t *desc,
                                      bool is_vertex, const char *defines) {
    /* Indexed by glsl_dialect_t — must stay in enum order. */
    static const char *const version_prefix[] = {
        "#version 120\n",
        "#version 140\n",
        "#version 150\n",
        "#version 300 es\nprecision highp float;\nprecision highp int;\n",
    };
    glsl_dialect_t dialect =
#ifdef BZ_GL_ES3
        GLSL_DIALECT_ES3;
#elif defined(BZ_GLSL_120)
        GLSL_DIALECT_120;
#elif defined(BZ_GLSL_150)
        GLSL_DIALECT_150;
#else
        GLSL_DIALECT_140;
#endif
    char decls[2048], main_wrapper[128];
    R_BuildShaderDeclarations(decls, sizeof(decls), desc, is_vertex, dialect);
    R_BuildShaderMain(main_wrapper, sizeof(main_wrapper), is_vertex, dialect);
    const char *strings[] = {
        version_prefix[dialect],
        defines ? defines : "",
        decls,
        is_vertex ? desc->VertexBody : desc->FragmentBody,
        main_wrapper,
    };
    R_Call(glShaderSource, stage, 5, strings, NULL);
}

/* Return exact CPU storage consumed by one value so cached comparisons never include struct padding. */
static size_t R_UniformTypeSize(uniformType_t type) {
    static BYTE const widths[UT_COUNT] = { 1, 2, 3, 4, 4, 1, 2, 1, 9, 9, 16, 1, 1, 1 };
    if (type >= UT_COUNT) return 0;
    if (type == UT_BOOL) return sizeof(bool);
    if (type >= UT_INT && type <= UT_INT_VEC2) return widths[type] * sizeof(int);
    if (type >= UT_SAMPLER_2D) return sizeof(int);
    return widths[type] * sizeof(FLOAT);
}

static GLuint shader_bound;

/* Descriptor compilation and lookup never overwrite caller-owned non-sampler values. */
void R_LoadShaderState(LPCSHADERLOAD load) {
    LPCSHADERDESC desc = load->desc;
    LPCSTR defines = load->defines;
    LPSHADERPROG prog = load->prog;
    void *state = load->state;
    GLuint vs = R_Call(glCreateShader, GL_VERTEX_SHADER);
    GLuint fs = R_Call(glCreateShader, GL_FRAGMENT_SHADER);

    R_SetShaderSourceFromDesc(vs, desc, true, defines);
    R_Call(glCompileShader, vs);
    R_CheckShader(vs, GL_COMPILE_STATUS, desc->Name);

    R_SetShaderSourceFromDesc(fs, desc, false, defines);
    R_Call(glCompileShader, fs);
    R_CheckShader(fs, GL_COMPILE_STATUS, desc->Name);

    GLuint progid = R_Call(glCreateProgram, );
    for (int i = 0; i < MAX_SHADER_ATTRIBS && desc->Attributes[i].name; i++)
        R_Call(glBindAttribLocation, progid, desc->Attributes[i].attrib, desc->Attributes[i].name);
    R_Call(glAttachShader, progid, vs);
    R_Call(glAttachShader, progid, fs);
    R_Call(glLinkProgram, progid);
    R_CheckShader(progid, GL_LINK_STATUS, desc->Name);
    R_Call(glDeleteShader, vs);
    R_Call(glDeleteShader, fs);
    R_Call(glUseProgram, progid);
    shader_bound = progid;

    prog->progid = progid;
    prog->desc = desc;
    int unit = 0;
    size_t cache_size = 0;
    for (int i = 0; i < MAX_SHADER_UNIFORMS && desc->Uniforms[i].name; i++) {
        const shaderUniform_t *u = &desc->Uniforms[i];
        size_t end = u->offset + R_UniformTypeSize(u->type) * (u->count ? u->count : 1);
        prog->locs[i] = glGetUniformLocation(progid, u->name);
        cache_size = MAX(cache_size, end);
        if (u->type >= UT_SAMPLER_2D && u->type <= UT_SAMPLER_2D_ARRAY)
            *(int *)((char *)state + u->offset) = unit++;
    }
    prog->cache = ri.MemAlloc((long)cache_size);
    if (!prog->cache) ri.error("R_LoadShaderState: cache allocation failed for %s", desc->Name);
    memset(prog->cache, 0, cache_size);
}

/* Release linked programs before their GL context is destroyed. */
void R_DeleteShader(LPSHADERPROG prog) {
    if (prog->progid) glDeleteProgram(prog->progid);
    if (shader_bound == prog->progid) shader_bound = 0;
    if (prog->cache) ri.MemFree(prog->cache);
    memset(prog, 0, sizeof(*prog));
}

/* One typed state submission owns all uniforms; exact per-program caching avoids redundant driver calls. */
void R_UploadShader(LPSHADERPROG prog, LPCVOID state) {
    if (shader_bound != prog->progid) {
        R_Call(glUseProgram, prog->progid);
        shader_bound = prog->progid;
    }
    for (int i = 0; i < MAX_SHADER_UNIFORMS && prog->desc->Uniforms[i].name; i++) {
        const shaderUniform_t *u = &prog->desc->Uniforms[i];
        const void *data = (const char *)state + u->offset;
        GLint loc = prog->locs[i];
        GLsizei count = u->counted ? *(DWORD const *)((char const *)state + u->count_offset) :
                                     (u->count ? u->count : 1);
        size_t bytes = R_UniformTypeSize(u->type) * count;
        if (loc < 0) continue; /* Linked shader optimised this declared input away. */
        if (u->counted && (count < 1 || count > (GLsizei)u->count)) {
            fprintf(stderr, "R_UploadShader: invalid count %d for %s.%s[%u]\n", count, prog->desc->Name, u->name, u->count);
            exit(EXIT_FAILURE);
        }
        if (prog->cache && !memcmp((char *)prog->cache + u->offset, data, bytes))
            continue;
        switch (u->type) {
            case UT_FLOAT: R_Call(glUniform1fv, loc, count, data); break;
            case UT_FLOAT_VEC2: R_Call(glUniform2fv, loc, count, data); break;
            case UT_FLOAT_VEC3: R_Call(glUniform3fv, loc, count, data); break;
            case UT_FLOAT_VEC4: case UT_COLOR: R_Call(glUniform4fv, loc, count, data); break;
            case UT_INT: case UT_SAMPLER_2D: case UT_SAMPLER_2D_RECT: case UT_SAMPLER_2D_ARRAY:
                R_Call(glUniform1iv, loc, count, data); break;
            case UT_INT_VEC2: R_Call(glUniform2iv, loc, count, data); break;
            case UT_BOOL: {
                GLint values[count];
                FOR_LOOP(j, count) values[j] = ((const bool *)data)[j];
                R_Call(glUniform1iv, loc, count, values); break;
            }
            case UT_FLOAT_MAT3: R_Call(glUniformMatrix3fv, loc, count, GL_FALSE, data); break;
            case UT_FLOAT_MAT3_TRANSPOSE: R_Call(glUniformMatrix3fv, loc, count, GL_TRUE, data); break;
            case UT_FLOAT_MAT4: R_Call(glUniformMatrix4fv, loc, count, GL_FALSE, data); break;
            default: fprintf(stderr, "R_UploadShader: invalid type for %s.%s\n", prog->desc->Name, u->name); exit(EXIT_FAILURE);
        }
        if (prog->cache)
            memcpy((char *)prog->cache + u->offset, data, bytes);
    }
}

static MODELPROG model_shader;
static MODELPROG instanced_shader;
static BOOL model_shader_loaded;
static BOOL instanced_shader_loaded;

static void R_LoadModelShader(MODELPROG *out, BOOL instancing) {
    memset(out, 0, sizeof(*out));
    R_LoadShader(&sd_model, R_ShaderDefines(instancing), out);

    out->state.alphaCutoff = 0.5f;
}

/* Returns the shared model shader, compiling it on first call. All three model
   formats (MDX/M2/M3) use this single shader; per-format data is normalised at
   load time so the GPU path is identical. */
MODELPROG *R_ModelShader(void) {
    if (!model_shader_loaded) {
        R_LoadModelShader(&model_shader, false);
        model_shader_loaded = true;
    }
    return &model_shader;
}

/* Instanced model shader for static meshes (ground-effect clutter). Uses the
   model shader compiled with BZ_USE_INSTANCING to replace uModelMatrix with
   per-instance attributes. */
MODELPROG *R_ModelShaderInstanced(void) {
    if (!instanced_shader_loaded) {
        R_LoadModelShader(&instanced_shader, true);
        FOR_LOOP(i, BZ_BONE_PALETTE_MAX) Matrix4_identity(&instanced_shader.state.bones[i]);
        instanced_shader.state.boneCount = BZ_BONE_PALETTE_MAX;
        instanced_shader_loaded = true;
    }
    return &instanced_shader;
}

/* Ground/world callers use the same semantic light schema as models. A zero
 * count explicitly selects the legacy fixed terrain light so games without an
 * environment-light model retain their existing appearance. */
void R_SetDefaultLighting(DEFAULTPROG *shader, LPCMODELLIGHTING lighting) {
    if (!shader) return;
    if (!lighting || lighting->count == 0) {
        shader->state.lightCount = 0;
        return;
    }
    if (lighting->count > BZ_MODEL_LIGHT_MAX) {
        ri.error("R_SetDefaultLighting: light count must be 0..%u, got %u", BZ_MODEL_LIGHT_MAX,
                 lighting->count);
        return;
    }
    R_PackModelLighting(shader->state.lights, lighting);
    shader->state.lightCount = lighting->count;
}

/* Model callers submit one semantic lighting state; only this proxy knows the uniform packing contract. */
void R_SetModelLighting(MODELPROG *shader, LPCMODELLIGHTING lighting) {
    if (!lighting || lighting->count < 1 || lighting->count > BZ_MODEL_LIGHT_MAX) {
        ri.error("R_SetModelLighting: light count must be 1..%u, got %u", BZ_MODEL_LIGHT_MAX,
                 lighting ? lighting->count : 0);
        return;
    }
    R_PackModelLighting(shader->state.lights, lighting);
    shader->state.lightCount = lighting->count;
}

/* Grass uses the same proxy boundary so game code never uploads its packed matrix directly. */
void R_SetModelGrass(MODELPROG *shader, LPCMODELGRASS grass) {
    R_PackModelGrass(&shader->state.grassParams, grass);
}

void R_ShutdownModelShader(void) {
    R_DeleteShader(&model_shader.prog);
    R_DeleteShader(&instanced_shader.prog);
    model_shader_loaded = instanced_shader_loaded = false;
}

/* Map the public SHADERTYPE selector to the matching sprite program. */
SPRITEPROG *R_SpriteShader(SHADERTYPE type) {
    switch (type) {
        case SHADER_SPLAT:         return &tr.shader_splat;
        case SHADER_SHADOWSPLAT:   return &tr.shader_shadowSplat;
        case SHADER_COMMANDBUTTON: return &tr.shader_commandButton;
        case SHADER_MINIMAP:       return &tr.shader_minimap;
        case SHADER_MINIMAP_FOG:   return &tr.shader_minimapFog;
        case SHADER_UNLIT:         return &tr.shader_unlit;
        default:                   return &tr.shader_ui;
    }
}

/* Builtin lifetime is renderer-owned; this table is shared by load and shutdown. */
static struct { LPCSHADERDESC desc; SPRITEPROG *shader; } builtin_shaders[] = {
    { &sd_unlit, &tr.shader_ui }, { &sd_splat, &tr.shader_splat },
    { &sd_shadow_splat, &tr.shader_shadowSplat }, { &sd_commandbutton, &tr.shader_commandButton },
    { &sd_minimap, &tr.shader_minimap }, { &sd_minimap_fog, &tr.shader_minimapFog },
    { &sd_unlit, &tr.shader_unlit },
};

void R_LoadBuiltinShaders(void) {
    FOR_LOOP(i, sizeof(builtin_shaders) / sizeof(*builtin_shaders)) {
        SPRITEPROG *shader = builtin_shaders[i].shader;
        memset(shader, 0, sizeof(*shader));
        R_LoadShader(builtin_shaders[i].desc, R_ShaderDefines(false), shader);
    }
    memset(&tr.shader_default, 0, sizeof(tr.shader_default));
    R_LoadShader(&sd_default, R_ShaderDefines(false), &tr.shader_default);
}

void R_ShutdownBuiltinShaders(void) {
    FOR_LOOP(i, sizeof(builtin_shaders) / sizeof(*builtin_shaders))
        R_DeleteShader(&builtin_shaders[i].shader->prog);
    R_DeleteShader(&tr.shader_default.prog);
}
