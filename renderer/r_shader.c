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

/* One depth/colour equation keeps terrain, models and alpha-composited shadows in the same fog. */
#define BZ_SCENE_FOG_GLSL \
    "  if (u_fogEnable) {\n" \
    "    float fogRange = u_fogParams.y - u_fogParams.x;\n" \
    "    float depth = gl_FragCoord.z / gl_FragCoord.w;\n" \
    "    float fogFactor = abs(fogRange) > 0.0001 ?\n" \
    "      clamp((u_fogParams.y - depth) / fogRange, 0.0, 1.0) :\n" \
    "      (depth <= u_fogParams.x ? 1.0 : 0.0);\n" \
    "    col.rgb = mix(u_fogColor, col.rgb, fogFactor);\n" \
    "  }\n"

/* --- unlit / ui: texture * vertex-color, no lighting ------------------- */
#define SHADER_TYPE spriteState_t
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
#define SHADER_TYPE spriteState_t
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
#define SHADER_TYPE spriteState_t
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
#define SHADER_TYPE spriteState_t
const shader_desc_t sd_shadow_splat = {
    .Name = "shadow_splat",
    .Uniforms = {
        UNIFORM(viewProjection, UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(model,          UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(texture,        UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(fogEnable,      UT_BOOL,       PRECISION_LOW),
        UNIFORM(fogColor,       UT_FLOAT_VEC3, PRECISION_LOW),
        UNIFORM(fogParams,      UT_FLOAT_VEC2, PRECISION_HIGH),
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
        "  vec4 col = vec4(0.0, 0.0, 0.0, tex.a * v_color.a * crop_edges(v_texcoord));\n"
        /* Fog the shadow colour before blending, so it no longer darkens already-fogged terrain. */
        BZ_SCENE_FOG_GLSL
        "  return col;\n"
        "}\n",
};
#undef SHADER_TYPE

/* --- commandbutton: edge glow controlled by u_activeGlow ---------------- */
#define SHADER_TYPE spriteState_t
const shader_desc_t sd_commandbutton = {
    .Name = "commandbutton",
    .Uniforms = {
        UNIFORM(viewProjection, UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(model,          UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(texture,        UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(activeGlow,     UT_FLOAT,      PRECISION_LOW),
        UNIFORM(radialShade,    UT_FLOAT,      PRECISION_LOW),
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
        "  vec2 radial = v_texcoord - vec2(0.5);\n"
        "  float angle = atan(radial.x, -radial.y) / 6.28318530718;\n"
        "  angle = angle < 0.0 ? angle + 1.0 : angle;\n"
        "  float elapsed = 1.0 - clamp(u_radialShade, 0.0, 1.0);\n"
        "  float shade = step(elapsed, angle) * step(0.000001, u_radialShade);\n"
        "  col.rgb *= mix(1.0, 0.35, shade);\n"
        "  float crop = step(abs(v_texcoord.x - 0.5), 0.5) * step(abs(v_texcoord.y - 0.5), 0.5);\n"
        "  col.a *= crop;\n"
        "  return col;\n"
        "}\n",
};
#undef SHADER_TYPE

/* --- minimap fog: fog-of-war overlay with y-flip ------------------------ */
#define SHADER_TYPE spriteState_t
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
#define SHADER_TYPE defaultState_t
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
        UNIFORM(fogEnable,      UT_BOOL,       PRECISION_LOW),
        UNIFORM(fogColor,       UT_FLOAT_VEC3, PRECISION_LOW),
        UNIFORM(fogParams,      UT_FLOAT_VEC2, PRECISION_LOW),
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
        BZ_SCENE_FOG_GLSL
        "  return col;\n"
        "}\n",
};
#undef SHADER_TYPE

/* --- model: shared skinned shader for MDX/M2/M3, compiled twice -----------
 * (normal + BZ_USE_INSTANCING).  USE_SHADOWMAPS/USE_FOGOFWAR/BZ_USE_MSAA are
 * injected as GLSL defines from the matching C preprocessor macros. */
#define SHADER_TYPE modelState_t
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
        "  }\n"
        BZ_SCENE_FOG_GLSL
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
static cstring_t R_ShaderDefines(bool instancing) {
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

#include "vendor/gl_shader/gl_shader.c"

static gs_options_t R_ShaderOptions(char const *defines) {
    return (gs_options_t){ .dialect =
#ifdef BZ_GL_ES3
        GLSL_DIALECT_ES3,
#elif defined(BZ_GLSL_120)
        GLSL_DIALECT_120,
#elif defined(BZ_GLSL_150)
        GLSL_DIALECT_150,
#else
        GLSL_DIALECT_140,
#endif
        .defines = defines,
    };
}

/* Public source inspection uses the same generator as compilation. */
int R_BuildShaderDeclarations(char *buf, int size, shader_desc_t const *desc, bool vertex, glsl_dialect_t dialect) {
    return gs_declarations(buf, size > 0 ? (size_t)size : 0, desc, vertex, dialect);
}
int R_BuildShaderMain(char *buf, int size, bool vertex, glsl_dialect_t dialect) {
    return gs_main(buf, size > 0 ? (size_t)size : 0, vertex, dialect);
}

/* The reusable module returns failure; the engine retains its fatal built-in shader policy. */
void R_LoadShaderState(shaderLoad_t const *load) {
    gs_options_t options = R_ShaderOptions(load->defines);
    if (!gs_load(load->prog, load->desc, load->state, load->state_size, &options)) exit(EXIT_FAILURE);
}
void R_DeleteShader(shaderProg_t *prog) { gs_delete(prog); }
void R_UploadShader(shaderProg_t *prog, void const *state) {
    if (!gs_apply(prog, state)) exit(EXIT_FAILURE);
}

static modelProg_t model_shader;
static modelProg_t instanced_shader;
static bool model_shader_loaded;
static bool instanced_shader_loaded;

static void R_LoadModelShader(modelProg_t *out, bool instancing) {
    memset(out, 0, sizeof(*out));
    R_LoadShader(&sd_model, R_ShaderDefines(instancing), out);

    out->state.alphaCutoff = 0.5f;
}

/* Returns the shared model shader, compiling it on first call. All three model
   formats (MDX/M2/M3) use this single shader; per-format data is normalised at
   load time so the GPU path is identical. */
modelProg_t *R_ModelShader(void) {
    if (!model_shader_loaded) {
        R_LoadModelShader(&model_shader, false);
        model_shader_loaded = true;
    }
    return &model_shader;
}

/* Instanced model shader for static meshes (ground-effect clutter). Uses the
   model shader compiled with BZ_USE_INSTANCING to replace uModelMatrix with
   per-instance attributes. */
modelProg_t *R_ModelShaderInstanced(void) {
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
void R_SetDefaultLighting(defaultProg_t *shader, modelLighting_t const *lighting) {
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
void R_SetModelLighting(modelProg_t *shader, modelLighting_t const *lighting) {
    if (!lighting || lighting->count < 1 || lighting->count > BZ_MODEL_LIGHT_MAX) {
        ri.error("R_SetModelLighting: light count must be 1..%u, got %u", BZ_MODEL_LIGHT_MAX,
                 lighting ? lighting->count : 0);
        return;
    }
    R_PackModelLighting(shader->state.lights, lighting);
    shader->state.lightCount = lighting->count;
}

/* Grass uses the same proxy boundary so game code never uploads its packed matrix directly. */
void R_SetModelGrass(modelProg_t *shader, modelGrass_t const *grass) {
    R_PackModelGrass(&shader->state.grassParams, grass);
}

void R_ShutdownModelShader(void) {
    R_DeleteShader(&model_shader.prog);
    R_DeleteShader(&instanced_shader.prog);
    model_shader_loaded = instanced_shader_loaded = false;
}

/* Map the public SHADERTYPE selector to the matching sprite program. */
spriteProg_t *R_SpriteShader(SHADERTYPE type) {
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
static struct { shader_desc_t const *desc; spriteProg_t *shader; } builtin_shaders[] = {
    { &sd_unlit, &tr.shader_ui }, { &sd_splat, &tr.shader_splat },
    { &sd_shadow_splat, &tr.shader_shadowSplat }, { &sd_commandbutton, &tr.shader_commandButton },
    { &sd_minimap, &tr.shader_minimap }, { &sd_minimap_fog, &tr.shader_minimapFog },
    { &sd_unlit, &tr.shader_unlit },
};

void R_LoadBuiltinShaders(void) {
    FOR_LOOP(i, sizeof(builtin_shaders) / sizeof(*builtin_shaders)) {
        spriteProg_t *shader = builtin_shaders[i].shader;
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
