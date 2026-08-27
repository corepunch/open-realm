#include "r_wowmap.h"

#define BZ_WOW_STR_INNER(x) #x
#define BZ_WOW_STR(x) BZ_WOW_STR_INNER(x)

WOWTERRAINPROG wow_terrain_shader;
WOWGRASSPROG wow_grass_shader;

/* Keep terrain and grass on the same exact MCVT diamond interpolation contract. */
#define WOW_HEIGHT_ATLAS_GLSL \
    "bool HeightAtlas_Coord(vec2 worldXY, out ivec2 tile, out vec2 cell) {\n" \
    "    vec2 rel = (u_atlasOriginWorld - worldXY) / u_atlasChunkSize;\n" \
    "    tile = ivec2(floor(rel.y), floor(rel.x));\n" \
    "    cell = fract(rel) * (u_atlasChunkSize / u_atlasUnitSize);\n" \
    "    ivec2 tiles = textureSize(u_heightAtlas, 0) / ivec2(17, 9);\n" \
    "    return all(greaterThanEqual(tile, ivec2(0))) && all(lessThan(tile, tiles));\n" \
    "}\n" \
    "float HeightAtlas_Bary(vec2 p, vec2 a, float ah, vec2 b, float bh, vec2 c, float ch) {\n" \
    "    float d = (b.y-c.y)*(a.x-c.x) + (c.x-b.x)*(a.y-c.y);\n" \
    "    float wa = ((b.y-c.y)*(p.x-c.x) + (c.x-b.x)*(p.y-c.y)) / d;\n" \
    "    float wb = ((c.y-a.y)*(p.x-c.x) + (a.x-c.x)*(p.y-c.y)) / d;\n" \
    "    return wa*ah + wb*bh + (1.0-wa-wb)*ch;\n" \
    "}\n" \
    "float HeightAtlas_SampleDiamond(vec2 worldXY) {\n" \
    "    ivec2 tile; vec2 local;\n" \
    "    if (!HeightAtlas_Coord(worldXY, tile, local)) return 0.0;\n" \
    "    ivec2 cell = ivec2(clamp(floor(local), vec2(0.0), vec2(7.0)));\n" \
    "    vec2 p = clamp(local - vec2(cell), vec2(0.0), vec2(1.0));\n" \
    "    ivec2 base = tile * ivec2(17, 9) + ivec2(cell.y, cell.x);\n" \
    "    float tl = texelFetch(u_heightAtlas, base, 0).r;\n" \
    "    float tr = texelFetch(u_heightAtlas, base + ivec2(1, 0), 0).r;\n" \
    "    float bl = texelFetch(u_heightAtlas, base + ivec2(0, 1), 0).r;\n" \
    "    float br = texelFetch(u_heightAtlas, base + ivec2(1, 1), 0).r;\n" \
    "    float ct = texelFetch(u_heightAtlas, tile*ivec2(17, 9) + ivec2(9+cell.y, cell.x), 0).r;\n" \
    "    if (p.y <= p.x && p.y <= 1.0-p.x)\n" \
    "        return HeightAtlas_Bary(p, vec2(.5), ct, vec2(0), tl, vec2(1,0), bl);\n" \
    "    if (p.x <= p.y && p.x <= 1.0-p.y)\n" \
    "        return HeightAtlas_Bary(p, vec2(.5), ct, vec2(0,1), tr, vec2(0), tl);\n" \
    "    if (p.y >= p.x && p.y >= 1.0-p.x)\n" \
    "        return HeightAtlas_Bary(p, vec2(.5), ct, vec2(1), br, vec2(0,1), tr);\n" \
    "    return HeightAtlas_Bary(p, vec2(.5), ct, vec2(1,0), bl, vec2(1), br);\n" \
    "}\n"

#define SHADER_TYPE WOWTERRAINSTATE
static const shader_desc_t sd_wow_terrain = {
    .Name = "wow_terrain",
    .Uniforms = {
        UNIFORM(viewProjection, UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(model,          UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(normalMatrix,   UT_FLOAT_MAT3_TRANSPOSE, PRECISION_HIGH),
        UNIFORM(sunDir,         UT_FLOAT_VEC3, PRECISION_LOW),
        UNIFORM(sunAmbient,     UT_FLOAT_VEC3, PRECISION_LOW),
        UNIFORM(sunDiffuse,     UT_FLOAT_VEC3, PRECISION_LOW),
        UNIFORM(texture0,       UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(texture1,       UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(texture2,       UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(texture3,       UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(alphaTexture,   UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(useWeightedBlend, UT_BOOL,      PRECISION_LOW),
        UNIFORM(singleTexture,  UT_BOOL,        PRECISION_LOW),
        UNIFORM(wmoIndoor,      UT_BOOL,        PRECISION_LOW),
        UNIFORM(wmoAmbient,     UT_FLOAT_VEC3, PRECISION_LOW),
        UNIFORM(wmoLightAdd,    UT_FLOAT_VEC3, PRECISION_LOW),
        UNIFORM(wmoBlendMode,   UT_INT,        PRECISION_LOW),
        UNIFORM(alphaOrigin,    UT_FLOAT_VEC2, PRECISION_LOW),
        UNIFORM(alphaAtlasChunks, UT_FLOAT,    PRECISION_LOW),
        UNIFORM(fogEnable,      UT_BOOL,       PRECISION_LOW),
        UNIFORM(fogColor,       UT_FLOAT_VEC3, PRECISION_LOW),
        UNIFORM(fogParams,      UT_FLOAT_VEC2, PRECISION_LOW),
        UNIFORM(fogCamera,      UT_FLOAT_VEC3, PRECISION_LOW),
    },
    .Attributes = {
        ATTRIB(position, attrib_position, UT_FLOAT_VEC3),
        ATTRIB(normal,   attrib_normal,   UT_FLOAT_VEC3),
        ATTRIB(texcoord, attrib_texcoord, UT_FLOAT_VEC2),
        ATTRIB(color,    attrib_color,    UT_COLOR),
    },
    .Shared = {
        SHARED(texcoord, UT_FLOAT_VEC2),
        SHARED(color,    UT_COLOR),
        SHARED(lighting, UT_FLOAT_VEC3),
        SHARED(world,    UT_FLOAT_VEC3),
    },
    .VertexBody =
        "vec4 vert() {\n"
        "  vec4 pos = u_model * vec4(a_position, 1.0);\n"
        "  v_texcoord = a_texcoord;\n"
        "  vec3 normal = normalize(u_normalMatrix * a_normal);\n"
        "  v_lighting = u_sunAmbient + u_sunDiffuse * clamp(dot(normal, u_sunDir), 0.0, 1.0);\n"
        "  v_color = a_color;\n"
        "  v_world = pos.xyz;\n"
        "  return u_viewProjection * pos;\n"
        "}\n",
    .FragmentBody =
        "vec2 adtAlphaCoord(vec2 chunkCoord) {\n"
        "  const float alphaTexelsPerChunk = 64.0;\n"
        "  float alphaAtlasSize = alphaTexelsPerChunk * u_alphaAtlasChunks;\n"
        "  chunkCoord = clamp(chunkCoord, vec2(0.0), vec2(1.0));\n"
        "  vec2 atlasTexel = u_alphaOrigin * alphaTexelsPerChunk + chunkCoord * (alphaTexelsPerChunk - 1.0) + vec2(0.5);\n"
        "  return atlasTexel / alphaAtlasSize;\n"
        "}\n"
        "vec4 frag() {\n"
        "  vec2 alphaCoord = adtAlphaCoord(v_texcoord * 0.125);\n"
        "  vec4 tex1 = texture(u_texture0, v_texcoord);\n"
        "  vec4 color;\n"
        "  if (u_singleTexture) {\n"
        "    color = tex1;\n"
        "  } else {\n"
        "    vec3 alphaBlend = texture(u_alphaTexture, alphaCoord).gba;\n"
        "    vec4 tex2 = texture(u_texture1, v_texcoord);\n"
        "    vec4 tex3 = texture(u_texture2, v_texcoord);\n"
        "    vec4 tex4 = texture(u_texture3, v_texcoord);\n"
        "    if (u_useWeightedBlend) {\n"
        "      float baseWeight = 1.0 - clamp(dot(alphaBlend, vec3(1.0)), 0.0, 1.0);\n"
        "      vec4 weights = vec4(baseWeight, alphaBlend);\n"
        "      color = tex1 * weights.r + tex2 * weights.g + tex3 * weights.b + tex4 * weights.a;\n"
        "    } else {\n"
        "      color = mix(mix(mix(tex1, tex2, alphaBlend.r), tex3, alphaBlend.g), tex4, alphaBlend.b);\n"
        "    }\n"
        "  }\n"
        "  if (u_singleTexture) {\n"
        "    vec3 mocv = 2.0 * v_color.rgb;\n"
        "    if (u_wmoIndoor) color.rgb = color.rgb * mocv + u_wmoAmbient + u_wmoLightAdd;\n"
        "    else color.rgb *= v_lighting * max(mocv, vec3(0.5));\n"
        "  } else {\n"
        "    color.rgb *= v_color.rgb * v_lighting;\n"
        "  }\n"
        "  if (u_fogEnable) {\n"
        "    float fog = clamp((u_fogParams.y - distance(v_world, u_fogCamera)) / (u_fogParams.y - u_fogParams.x), 0.0, 1.0);\n"
        "    color.rgb = mix(u_fogColor, color.rgb, fog);\n"
        "  }\n"
        "  if (u_singleTexture && u_wmoBlendMode == 1 && color.a < 0.5) discard;\n"
        "  if (!u_singleTexture || u_wmoBlendMode < 2) color.a = 1.0;\n"
        "  return color;\n"
        "}\n",
};
#undef SHADER_TYPE

#define SHADER_TYPE WOWGRASSSTATE
static const shader_desc_t sd_wow_grass = {
    .Name = "wow_grass",
    .Uniforms = {
        UNIFORM(viewProjection,        UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(sunDir,                UT_FLOAT_VEC3, PRECISION_LOW),
        UNIFORM(sunAmbient,            UT_FLOAT_VEC3, PRECISION_LOW),
        UNIFORM(sunDiffuse,            UT_FLOAT_VEC3, PRECISION_LOW),
        UNIFORM(grassTime,             UT_FLOAT,      PRECISION_LOW),
        UNIFORM(grassCtrl,             UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(ctrlOriginWorld,       UT_FLOAT_VEC2, PRECISION_LOW),
        UNIFORM(ctrlCellSize,          UT_FLOAT,      PRECISION_LOW),
        UNIFORM(cameraXZ,              UT_FLOAT_VEC2, PRECISION_LOW),
        UNIFORM(grassSlotSpacing,      UT_FLOAT,      PRECISION_LOW),
        UNIFORM(heightAtlas,           UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(atlasOriginWorld,      UT_FLOAT_VEC2, PRECISION_LOW),
        UNIFORM(atlasChunkSize,        UT_FLOAT,      PRECISION_LOW),
        UNIFORM(atlasUnitSize,         UT_FLOAT,      PRECISION_LOW),
        UNIFORM(grassCameraOrigin,     UT_FLOAT_VEC3, PRECISION_LOW),
        UNIFORM(grassDrawDistance,     UT_FLOAT,      PRECISION_LOW),
        UNIFORM(grassFadeStartDistance, UT_FLOAT,     PRECISION_LOW),
    },
    .Attributes = {
        ATTRIB(position, attrib_position, UT_FLOAT_VEC3),
        ATTRIB(normal,   attrib_normal,   UT_FLOAT_VEC3),
        ATTRIB(texcoord, attrib_texcoord, UT_FLOAT_VEC2),
        ATTRIB(color,    attrib_color,    UT_COLOR),
    },
    .Shared = {
        SHARED(color,    UT_COLOR),
        SHARED(uv,       UT_FLOAT_VEC2),
        SHARED(world,    UT_FLOAT_VEC3),
        SHARED(lighting, UT_FLOAT_VEC3),
    },
    .VertexBody =
        WOW_HEIGHT_ATLAS_GLSL
        "float GrassHash(vec2 p) { return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453); }\n"
        "vec4 vert() {\n"
        "  float top = clamp(a_texcoord.y, 0.0, 1.0);\n"
        "  int gx = gl_InstanceID % " BZ_WOW_STR(WOW_GRASS_GRID_SIDE) " - " BZ_WOW_STR(WOW_GRASS_GRID_HALF) ";\n"
        "  int gy = gl_InstanceID / " BZ_WOW_STR(WOW_GRASS_GRID_SIDE) " - " BZ_WOW_STR(WOW_GRASS_GRID_HALF) ";\n"
        "  vec2 cell = floor(u_cameraXZ / u_grassSlotSpacing) + vec2(gx, gy);\n"
        "  vec2 jitter = vec2(GrassHash(cell), GrassHash(cell + vec2(19.19,73.73))) - vec2(0.5);\n"
        "  vec2 worldXY = (cell + jitter * 0.72) * u_grassSlotSpacing;\n"
        "  ivec2 htile; vec2 hcell;\n"
        "  float keep = HeightAtlas_Coord(worldXY, htile, hcell) ? 1.0 : 0.0;\n"
        "  vec2 crel = (u_ctrlOriginWorld - worldXY) / u_ctrlCellSize;\n"
        "  ivec2 cc = ivec2(floor(crel.y), floor(crel.x));\n"
        "  ivec2 csize = textureSize(u_grassCtrl, 0);\n"
        "  bool cin = all(greaterThanEqual(cc, ivec2(0))) && all(lessThan(cc, csize));\n"
        "  vec4 ctrl = cin ? texelFetch(u_grassCtrl, cc, 0) : vec4(1.0, 0.0, 0.0, 0.0);\n"
        "  float seed = GrassHash(cell + vec2(41.41,17.17));\n"
        "  keep *= (1.0-step(0.5, ctrl.r)) * step(seed, ctrl.g);\n"
        "  float scale = 0.65 + 0.7 * GrassHash(cell + vec2(5.13,91.7));\n"
        "  float yaw = seed * 6.2831853;\n"
        "  float cy = cos(yaw), sy = sin(yaw);\n"
        "  vec3 pos = vec3(cy*a_position.x-sy*a_position.z, sy*a_position.x+cy*a_position.z, a_position.y) * scale;\n"
        "  float phase = GrassHash(cell + vec2(3.71,53.9));\n"
        "  float wave = sin(u_grassTime * 1.7 + phase * 6.2831853) * 0.22 * top;\n"
        "  pos.xy += vec2(wave, wave * 0.35);\n"
        "  pos += vec3(worldXY, HeightAtlas_SampleDiamond(worldXY));\n"
        "  pos *= keep;\n"
        "  v_world = pos;\n"
        "  v_color = vec4(0.28, 0.62, 0.18, keep);\n"
        "  v_uv = a_texcoord;\n"
        "  v_lighting = u_sunAmbient + u_sunDiffuse * clamp(u_sunDir.z, 0.0, 1.0);\n"
        "  return u_viewProjection * vec4(pos, 1.0);\n"
        "}\n",
    .FragmentBody =
        "vec4 frag() {\n"
        "  float d = distance(v_world.xy, u_grassCameraOrigin.xy);\n"
        "  float fade = 1.0 - smoothstep(u_grassFadeStartDistance, u_grassDrawDistance, d);\n"
        "  float width = 1.0 - abs(v_uv.x * 2.0 - 1.0);\n"
        "  float edge = smoothstep(0.24, 0.46, width);\n"
        "  float root = smoothstep(0.02, 0.14, v_uv.y);\n"
        "  float tip = 1.0 - smoothstep(0.84, 1.00, v_uv.y);\n"
        "  float blade = edge * root * tip;\n"
        "  float alpha = v_color.a * fade * blade;\n"
        "  return vec4(v_color.rgb * v_lighting, alpha);\n"
        "}\n",
};
#undef SHADER_TYPE

void Wow_InitTerrainShader(void) {
    if (wow_terrain_shader.prog.progid) {
        return;
    }

    memset(&wow_terrain_shader, 0, sizeof(wow_terrain_shader));
    R_LoadShader(&sd_wow_terrain, NULL, &wow_terrain_shader);

    wow_terrain_shader.state.texture0 = 0;
    wow_terrain_shader.state.texture1 = 1;
    wow_terrain_shader.state.texture2 = 2;
    wow_terrain_shader.state.texture3 = 3;
    wow_terrain_shader.state.alphaTexture = 4;
    wow_terrain_shader.state.alphaAtlasChunks = (GLfloat)WOW_ALPHA_ATLAS_CHUNKS;
    wow_terrain_shader.state.singleTexture = 0;
    wow_terrain_shader.state.wmoIndoor = 0;
    wow_terrain_shader.state.wmoAmbient = (VECTOR3){ 0.0f, 0.0f, 0.0f };
    wow_terrain_shader.state.wmoLightAdd = (VECTOR3){ 0.0f, 0.0f, 0.0f };
    wow_terrain_shader.state.wmoBlendMode = 0;
}

void Wow_InitGrassShader(void) {
    if (wow_grass_shader.prog.progid) {
        return;
    }

    memset(&wow_grass_shader, 0, sizeof(wow_grass_shader));
    R_LoadShader(&sd_wow_grass, NULL, &wow_grass_shader);

}
