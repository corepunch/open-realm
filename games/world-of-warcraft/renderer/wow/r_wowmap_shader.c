#include "r_wowmap.h"

#define BZ_WOW_STR_INNER(x) #x
#define BZ_WOW_STR(x) BZ_WOW_STR_INNER(x)

LPSHADER wow_terrain_shader;
LPSHADER wow_grass_shader;
GLint wow_uTexture0 = -1;
GLint wow_uTexture1 = -1;
GLint wow_uTexture2 = -1;
GLint wow_uTexture3 = -1;
GLint wow_uAlphaTexture = -1;
GLint wow_uUseWeightedBlend = -1;
GLint wow_uSingleTexture = -1;
GLint wow_uWmoIndoor = -1;
GLint wow_uWmoAmbient = -1;
GLint wow_uWmoLightAdd = -1;
GLint wow_uWmoBlendMode = -1;
GLint wow_uAlphaOrigin = -1;
GLint wow_uAlphaAtlasChunks = -1;
GLint wow_uFogEnable = -1;
GLint wow_uFogColor = -1;
GLint wow_uFogParams = -1;
GLint wow_uFogCamera = -1;
GLint wow_uSunDir = -1;
GLint wow_uSunAmbient = -1;
GLint wow_uSunDiffuse = -1;
GLint wow_uGrassTime = -1;
GLint wow_uGrassCameraOrigin = -1;
GLint wow_uGrassDrawDistance = -1;
GLint wow_uGrassFadeStartDistance = -1;
GLint wow_uHeightAtlas = -1;
GLint wow_uAtlasOriginWorld = -1;
GLint wow_uAtlasChunkSize = -1;
GLint wow_uAtlasUnitSize = -1;
GLint wow_uGrassCtrl = -1;
GLint wow_uCtrlOriginWorld = -1;
GLint wow_uCtrlCellSize = -1;
GLint wow_uCameraXZ = -1;
GLint wow_uGrassSlotSpacing = -1;
GLint wow_uGrassSunDir = -1;
GLint wow_uGrassSunAmbient = -1;
GLint wow_uGrassSunDiffuse = -1;

/* Keep terrain and grass on the same exact MCVT diamond interpolation contract. */
#define WOW_HEIGHT_ATLAS_GLSL \
    "uniform sampler2D uHeightAtlas;\n" \
    "uniform vec2 uAtlasOriginWorld;\n" \
    "uniform float uAtlasChunkSize;\n" \
    "uniform float uAtlasUnitSize;\n" \
    "bool HeightAtlas_Coord(vec2 worldXY, out ivec2 tile, out vec2 cell) {\n" \
    "    vec2 rel = (uAtlasOriginWorld - worldXY) / uAtlasChunkSize;\n" \
    "    tile = ivec2(floor(rel.y), floor(rel.x));\n" \
    "    cell = fract(rel) * (uAtlasChunkSize / uAtlasUnitSize);\n" \
    "    ivec2 tiles = textureSize(uHeightAtlas, 0) / ivec2(17, 9);\n" \
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
    "    float tl = texelFetch(uHeightAtlas, base, 0).r;\n" \
    "    float tr = texelFetch(uHeightAtlas, base + ivec2(1, 0), 0).r;\n" \
    "    float bl = texelFetch(uHeightAtlas, base + ivec2(0, 1), 0).r;\n" \
    "    float br = texelFetch(uHeightAtlas, base + ivec2(1, 1), 0).r;\n" \
    "    float ct = texelFetch(uHeightAtlas, tile*ivec2(17, 9) + ivec2(9+cell.y, cell.x), 0).r;\n" \
    "    if (p.y <= p.x && p.y <= 1.0-p.x)\n" \
    "        return HeightAtlas_Bary(p, vec2(.5), ct, vec2(0), tl, vec2(1,0), bl);\n" \
    "    if (p.x <= p.y && p.x <= 1.0-p.y)\n" \
    "        return HeightAtlas_Bary(p, vec2(.5), ct, vec2(0,1), tr, vec2(0), tl);\n" \
    "    if (p.y >= p.x && p.y >= 1.0-p.x)\n" \
    "        return HeightAtlas_Bary(p, vec2(.5), ct, vec2(1), br, vec2(0,1), tr);\n" \
    "    return HeightAtlas_Bary(p, vec2(.5), ct, vec2(1,0), bl, vec2(1), br);\n" \
    "}\n"

void Wow_InitTerrainShader(void) {
    static LPCSTR vs_wow_terrain =
    "#version 140\n"
    "in vec3 i_position;\n"
    "in vec3 i_normal;\n"
    "in vec2 i_texcoord;\n"
    "in vec4 i_color;\n"
    "out vec2 v_texcoord;\n"
    "out vec4 v_color;\n"
    "out vec3 v_lighting;\n"
    "out vec3 v_world;\n"
    "uniform mat4 uViewProjectionMatrix;\n"
    "uniform mat4 uModelMatrix;\n"
    "uniform mat3 uNormalMatrix;\n"
    "uniform vec3 uSunDir;\n"
    "uniform vec3 uSunAmbient;\n"
    "uniform vec3 uSunDiffuse;\n"
    WOW_HEIGHT_ATLAS_GLSL
    "void main() {\n"
    "    vec4 pos = uModelMatrix * vec4(i_position, 1.0);\n"
    "    v_texcoord = i_texcoord;\n"
    "    vec3 normal = normalize(uNormalMatrix * i_normal);\n"
    "    v_lighting = uSunAmbient + uSunDiffuse * clamp(dot(normal, uSunDir), 0.0, 1.0);\n"
    "    v_color = i_color;\n"
    "    v_world = pos.xyz;\n"
    "    gl_Position = uViewProjectionMatrix * pos;\n"
    "}\n";
    static LPCSTR fs_wow_terrain =
    "#version 140\n"
    "in vec2 v_texcoord;\n"
    "in vec4 v_color;\n"
    "in vec3 v_lighting;\n"
    "in vec3 v_world;\n"
    "out vec4 o_color;\n"
    "uniform sampler2D uTexture0;\n"
    "uniform sampler2D uTexture1;\n"
    "uniform sampler2D uTexture2;\n"
    "uniform sampler2D uTexture3;\n"
    "uniform sampler2D uAlphaTexture;\n"
    "uniform int uUseWeightedBlend;\n"
    "uniform int uSingleTexture;\n"
    "uniform int uWmoIndoor;\n"
    "uniform vec3 uWmoAmbient;\n"
    "uniform vec3 uWmoLightAdd;\n"
    "uniform int uWmoBlendMode;\n"
    "uniform vec2 uAlphaOrigin;\n"
    "uniform float uAlphaAtlasChunks;\n"
    "uniform bool uFogEnable;\n"
    "uniform vec3 uFogColor;\n"
    "uniform vec2 uFogParams;\n"
    "uniform vec3 uFogCamera;\n"
    "vec2 adtAlphaCoord(vec2 chunkCoord) {\n"
    "    const float alphaTexelsPerChunk = 64.0;\n"
    "    float alphaAtlasSize = alphaTexelsPerChunk * uAlphaAtlasChunks;\n"
    "    chunkCoord = clamp(chunkCoord, vec2(0.0), vec2(1.0));\n"
    "    vec2 atlasTexel = uAlphaOrigin * alphaTexelsPerChunk + chunkCoord * (alphaTexelsPerChunk - 1.0) + vec2(0.5);\n"
    "    return atlasTexel / alphaAtlasSize;\n"
    "}\n"
    "void main() {\n"
    "    vec2 alphaCoord = adtAlphaCoord(v_texcoord * 0.125);\n"
    "    vec4 tex1 = texture(uTexture0, v_texcoord);\n"
    "    vec4 color;\n"
    "    if (uSingleTexture != 0) {\n"
    "        color = tex1;\n"
    "    } else {\n"
    "        vec3 alphaBlend = texture(uAlphaTexture, alphaCoord).gba;\n"
    "        vec4 tex2 = texture(uTexture1, v_texcoord);\n"
    "        vec4 tex3 = texture(uTexture2, v_texcoord);\n"
    "        vec4 tex4 = texture(uTexture3, v_texcoord);\n"
    "        if (uUseWeightedBlend != 0) {\n"
    "            float baseWeight = 1.0 - clamp(dot(alphaBlend, vec3(1.0)), 0.0, 1.0);\n"
    "            vec4 weights = vec4(baseWeight, alphaBlend);\n"
    "            color = tex1 * weights.r + tex2 * weights.g + tex3 * weights.b + tex4 * weights.a;\n"
    "        } else {\n"
    "            color = mix(mix(mix(tex1, tex2, alphaBlend.r), tex3, alphaBlend.g), tex4, alphaBlend.b);\n"
    "        }\n"
    "    }\n"
    /* WMO path: MOCV was fixup-divided-by-2; multiply by 2 cancels that.
       Ambient/MOLT are ADDITIVE (not multiplicative) and only apply to indoor
       batches — exterior MOCV already has sun lighting pre-baked. v_lighting
       (ambient + diffuse·N·L) is for terrain only; WMO lighting is baked into MOCV.
       Terrain path: vertex color is white (vanilla has no MCCV) and the dynamic
       sun carried by v_lighting supplies the diffuse term. */
    "    if (uSingleTexture != 0) {\n"
    "        float extBlend = v_color.a;\n"
    "        color.rgb = color.rgb * 2.0 * v_color.rgb + (uWmoAmbient + uWmoLightAdd) * (1.0 - extBlend);\n"
    "    } else {\n"
    "        color.rgb *= v_color.rgb * v_lighting;\n"
    "    }\n"
    "    if (uFogEnable) {\n"
    "        float fog = clamp((uFogParams.y-distance(v_world, uFogCamera))/(uFogParams.y-uFogParams.x), 0.0, 1.0);\n"
    "        color.rgb = mix(uFogColor, color.rgb, fog);\n"
    "    }\n"
    "    if (uSingleTexture != 0 && uWmoBlendMode == 1 && color.a < 0.5) discard;\n"
    "    if (uSingleTexture == 0 || uWmoBlendMode < 2) color.a = 1.0;\n"
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
    wow_uSingleTexture = glGetUniformLocation(wow_terrain_shader->progid, "uSingleTexture");
    wow_uWmoIndoor = glGetUniformLocation(wow_terrain_shader->progid, "uWmoIndoor");
    wow_uWmoAmbient = glGetUniformLocation(wow_terrain_shader->progid, "uWmoAmbient");
    wow_uWmoLightAdd = glGetUniformLocation(wow_terrain_shader->progid, "uWmoLightAdd");
    wow_uWmoBlendMode = glGetUniformLocation(wow_terrain_shader->progid, "uWmoBlendMode");
    wow_uAlphaOrigin = glGetUniformLocation(wow_terrain_shader->progid, "uAlphaOrigin");
    wow_uAlphaAtlasChunks = glGetUniformLocation(wow_terrain_shader->progid, "uAlphaAtlasChunks");
    wow_uFogEnable = glGetUniformLocation(wow_terrain_shader->progid, "uFogEnable");
    wow_uFogColor = glGetUniformLocation(wow_terrain_shader->progid, "uFogColor");
    wow_uFogParams = glGetUniformLocation(wow_terrain_shader->progid, "uFogParams");
    wow_uFogCamera = glGetUniformLocation(wow_terrain_shader->progid, "uFogCamera");
    wow_uSunDir = glGetUniformLocation(wow_terrain_shader->progid, "uSunDir");
    wow_uSunAmbient = glGetUniformLocation(wow_terrain_shader->progid, "uSunAmbient");
    wow_uSunDiffuse = glGetUniformLocation(wow_terrain_shader->progid, "uSunDiffuse");
    R_Call(glUseProgram, wow_terrain_shader->progid);
    R_Call(glUniform1i, wow_uTexture0, 0);
    R_Call(glUniform1i, wow_uTexture1, 1);
    R_Call(glUniform1i, wow_uTexture2, 2);
    R_Call(glUniform1i, wow_uTexture3, 3);
    R_Call(glUniform1i, wow_uAlphaTexture, 4);
    R_Call(glUniform1f, wow_uAlphaAtlasChunks, (GLfloat)WOW_ALPHA_ATLAS_CHUNKS);
    R_Call(glUniform1i, wow_uSingleTexture, 0);
    R_Call(glUniform1i, wow_uWmoIndoor, 0);
    R_Call(glUniform3f, wow_uWmoAmbient, 0.0f, 0.0f, 0.0f);
    R_Call(glUniform3f, wow_uWmoLightAdd, 0.0f, 0.0f, 0.0f);
    R_Call(glUniform1i, wow_uWmoBlendMode, 0);
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
    "out vec3 v_lighting;\n"
    "uniform mat4 uViewProjectionMatrix;\n"
    "uniform vec3 uSunDir;\n"
    "uniform vec3 uSunAmbient;\n"
    "uniform vec3 uSunDiffuse;\n"
    "uniform float uGrassTime;\n"
    "uniform sampler2D uGrassCtrl;\n"
    "uniform vec2 uCtrlOriginWorld;\n"
    "uniform float uCtrlCellSize;\n"
    "uniform vec2 uCameraXZ;\n"
    "uniform float uGrassSlotSpacing;\n"
    WOW_HEIGHT_ATLAS_GLSL
    "float GrassHash(vec2 p) { return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453); }\n"
    "void main() {\n"
    "    float top = clamp(i_texcoord.y, 0.0, 1.0);\n"
    "    int gx = gl_InstanceID % " BZ_WOW_STR(WOW_GRASS_GRID_SIDE) " - " BZ_WOW_STR(WOW_GRASS_GRID_HALF) ";\n"
    "    int gy = gl_InstanceID / " BZ_WOW_STR(WOW_GRASS_GRID_SIDE) " - " BZ_WOW_STR(WOW_GRASS_GRID_HALF) ";\n"
    "    vec2 cell = floor(uCameraXZ / uGrassSlotSpacing) + vec2(gx, gy);\n"
    "    vec2 jitter = vec2(GrassHash(cell), GrassHash(cell + vec2(19.19,73.73))) - vec2(0.5);\n"
    "    vec2 worldXY = (cell + jitter * 0.72) * uGrassSlotSpacing;\n"
    "    ivec2 htile; vec2 hcell;\n"
    "    float keep = HeightAtlas_Coord(worldXY, htile, hcell) ? 1.0 : 0.0;\n"
    "    vec2 crel = (uCtrlOriginWorld - worldXY) / uCtrlCellSize;\n"
    "    ivec2 cc = ivec2(floor(crel.y), floor(crel.x));\n"
    "    ivec2 csize = textureSize(uGrassCtrl, 0);\n"
    "    bool cin = all(greaterThanEqual(cc, ivec2(0))) && all(lessThan(cc, csize));\n"
    "    vec4 ctrl = cin ? texelFetch(uGrassCtrl, cc, 0) : vec4(1.0, 0.0, 0.0, 0.0);\n"
    "    float seed = GrassHash(cell + vec2(41.41,17.17));\n"
    "    keep *= (1.0-step(0.5, ctrl.r)) * step(seed, ctrl.g);\n"
    "    float scale = 0.65 + 0.7 * GrassHash(cell + vec2(5.13,91.7));\n"
    "    float yaw = seed * 6.2831853;\n"
    "    float cy = cos(yaw), sy = sin(yaw);\n"
    "    vec3 pos = vec3(cy*i_position.x-sy*i_position.z, sy*i_position.x+cy*i_position.z, i_position.y) * scale;\n"
    "    float phase = GrassHash(cell + vec2(3.71,53.9));\n"
    "    float wave = sin(uGrassTime * 1.7 + phase * 6.2831853) * 0.22 * top;\n"
    "    pos.xy += vec2(wave, wave * 0.35);\n"
    "    pos += vec3(worldXY, HeightAtlas_SampleDiamond(worldXY));\n"
    "    pos *= keep;\n"
    "    v_world = pos;\n"
    "    v_color = vec4(0.28, 0.62, 0.18, keep);\n"
    "    v_uv = i_texcoord;\n"
    "    v_lighting = uSunAmbient + uSunDiffuse * clamp(uSunDir.z, 0.0, 1.0);\n"
    "    gl_Position = uViewProjectionMatrix * vec4(pos, 1.0);\n"
    "}\n";
    static LPCSTR fs_wow_grass =
    "#version 140\n"
    "in vec4 v_color;\n"
    "in vec2 v_uv;\n"
    "in vec3 v_world;\n"
    "in vec3 v_lighting;\n"
    "out vec4 o_color;\n"
    "uniform vec3 uGrassCameraOrigin;\n"
    "uniform float uGrassDrawDistance;\n"
    "uniform float uGrassFadeStartDistance;\n"
    "void main() {\n"
    "    float d = distance(v_world.xy, uGrassCameraOrigin.xy);\n"
    "    float fade = 1.0 - smoothstep(uGrassFadeStartDistance, uGrassDrawDistance, d);\n"
    "    float width = 1.0 - abs(v_uv.x * 2.0 - 1.0);\n"
    "    float edge = smoothstep(0.24, 0.46, width);\n"
    "    float root = smoothstep(0.02, 0.14, v_uv.y);\n"
    "    float tip = 1.0 - smoothstep(0.84, 1.00, v_uv.y);\n"
    "    float blade = edge * root * tip;\n"
    "    float alpha = v_color.a * fade * blade;\n"
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
    wow_uGrassFadeStartDistance = glGetUniformLocation(wow_grass_shader->progid, "uGrassFadeStartDistance");
    wow_uHeightAtlas = glGetUniformLocation(wow_grass_shader->progid, "uHeightAtlas");
    wow_uAtlasOriginWorld = glGetUniformLocation(wow_grass_shader->progid, "uAtlasOriginWorld");
    wow_uAtlasChunkSize = glGetUniformLocation(wow_grass_shader->progid, "uAtlasChunkSize");
    wow_uAtlasUnitSize = glGetUniformLocation(wow_grass_shader->progid, "uAtlasUnitSize");
    wow_uGrassCtrl = glGetUniformLocation(wow_grass_shader->progid, "uGrassCtrl");
    wow_uCtrlOriginWorld = glGetUniformLocation(wow_grass_shader->progid, "uCtrlOriginWorld");
    wow_uCtrlCellSize = glGetUniformLocation(wow_grass_shader->progid, "uCtrlCellSize");
    wow_uCameraXZ = glGetUniformLocation(wow_grass_shader->progid, "uCameraXZ");
    wow_uGrassSlotSpacing = glGetUniformLocation(wow_grass_shader->progid, "uGrassSlotSpacing");
    wow_uGrassSunDir = glGetUniformLocation(wow_grass_shader->progid, "uSunDir");
    wow_uGrassSunAmbient = glGetUniformLocation(wow_grass_shader->progid, "uSunAmbient");
    wow_uGrassSunDiffuse = glGetUniformLocation(wow_grass_shader->progid, "uSunDiffuse");
}
