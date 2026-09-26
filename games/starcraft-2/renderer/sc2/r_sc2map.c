#include "r_sc2map.h"
#include "renderer/r_shader.h"

#include "games/starcraft-2/common/sc2_map.c"
#include "games/starcraft-2/renderer/m3/r_m3.h"
#include "games/starcraft-2/renderer/sc2/sc2_shadow.h"
#define BZ_SC2_STR_INNER(x) #x
#define BZ_SC2_STR(x) BZ_SC2_STR_INNER(x)
#include "renderer/r_cliff.h"
#include "r_sc2_ramps.h"
#include "games/warcraft-3/renderer/w3m/r_terrain_layers.c"

#define SC2_TERRAIN_BLEND_LAYERS    8
#define SC2_TERRAIN_BLEND_GROUPS    2
#define SC2_TERRAIN_PASS_LAYERS     4
#define SC2_M3_MAX_BONES            128
#define SC2_CLIFF_BLOCK_SPAN        2u      /* cliff cells are 2x2 grid units */
#define SC2_CLIFF_BLOCK_ORIGIN(X)   ((X) & ~(SC2_CLIFF_BLOCK_SPAN - 1u))
#define SC2_CLIFF_BLOCK_CENTER(X)   ((X) + 1u)  /* centre grid coord within a 2-unit block */
#define SC2_CLIFF_CELL_ACTIVE       0x01u
#define SC2_CLIFF_CELL_RAMP         0x02u
#define SC2_CLIFF_LEVEL_PACKED_MIN  0x40u   /* packed format threshold; values >= this are shifted */
#define SC2_CLIFF_LEVEL_SHIFT       6
#define SC2_TERRAIN_UV_SCALE        8.0f
#define SC2_CLIFF_VARIANT_MASK      3u
#define SC2_M3_UV_SCALE             2048.0f /* M3 UV coords are stored as 1/2048 fixed-point */
#define SC2_M3_NORMAL_SCALE         (2.0f / 255.0f)  /* byte-packed snorm: val/255*2-1 */
#define SC2_EPSILON                 0.001f  /* near-zero threshold for spans and scale guards */
#define SC2_TRACE_EPSILON           0.0001f /* near-zero direction threshold in ray-slab test */
#define SC2_TRACE_INF               1.0e30f /* infinity sentinel for axis-aligned ray marching */
#define SC2_CLIFF_MODEL_FOOTPRINT   2.0f    /* loaded SC2 cliff M3 footprint: local -1..1 */
#define SC2_HARD_TILE_CURVE_STEPS    8u      /* samples per HRDT span; bends generated local-Y road geometry */
#define SC2_HARD_TILE_BODY_V         0.5f    /* normalized atlas V; lower half is the seamless horizontal road body */
#define SC2_HARD_TILE_BODY_ASPECT    2.0f    /* length/width; 1024x512 lower-half road body in the diffuse atlas */
#define SC2_HARD_TILE_START          0x0001u // HRDT flags; first control point in a road curve
#define SC2_HARD_TILE_END            0x0100u // HRDT flags; last control point in a road curve
#define SC2_MAP_WIDTH(MAP)          ((MAP)->MapInfo.width)
#define SC2_MAP_HEIGHT(MAP)         ((MAP)->MapInfo.height)
#define SC2_CLIFF_WIDTH(MAP)        (MAX(1, (SC2_MAP_WIDTH(MAP) + 1) / 2))
#define BZ_SC2_FIX_FLAT_TERRAIN_TIER_MISMATCH

/* BL=0, BR=1, TR=2, TL=3 - matches SC2 cliff model corner convention */
#define SC2_CLIFF_BLOCK_LEVELS(LEVEL, MAP, X, Y) \
do { \
    (LEVEL)[0] = r_sc2_cliff_level_at_grid((MAP), (X),                          (Y)); \
    (LEVEL)[1] = r_sc2_cliff_level_at_grid((MAP), (X) + SC2_CLIFF_BLOCK_SPAN,   (Y)); \
    (LEVEL)[2] = r_sc2_cliff_level_at_grid((MAP), (X) + SC2_CLIFF_BLOCK_SPAN,   (Y) + SC2_CLIFF_BLOCK_SPAN); \
    (LEVEL)[3] = r_sc2_cliff_level_at_grid((MAP), (X),                          (Y) + SC2_CLIFF_BLOCK_SPAN); \
} while (0)

/* SC2 terrain uses its own vertex shader for blend UVs. */
extern m3SequenceTimeline_t const *M3_FindAnimationAtTime(m3Model_t const *model, uint32_t time, uint32_t *localtime);
extern vector3_t M3_GetVector3AnimValue(m3Model_t const *model,
                                      m3SequenceTimeline_t const *timeline,
                                      m3Vector3AnimRef_t const *animref,
                                      uint32_t time);
extern vector4_t M3_GetVector4AnimValue(m3Model_t const *model,
                                      m3SequenceTimeline_t const *timeline,
                                      m3Vector4AnimRef_t const *animref,
                                      uint32_t time);
extern void M3_RenderModel(renderEntity_t const *entity, m3Model_t const *model, matrix4_t const *transform);
extern void M3_RenderBuffer(renderEntity_t const *entity, m3Model_t const *model, buffer_t const *buffer, uint32_t vertices, uint32_t indices);

#include "r_sc2_road_draw.h"

static mapsegment_t *sc2_terrain_segment;
static maplayer_t *sc2_hard_tile_layers;
static model_t const *sc2_hard_tile_model;
static renderEntity_t sc2_hard_tile_entity;
static bool sc2_terrain_shader_loaded;
static bool sc2_cliff_shader_loaded;
static texture_t *sc2_terrain_textures[SC2_TERRAIN_BLEND_LAYERS];
static texture_t *sc2_terrain_masks[SC2_TERRAIN_BLEND_GROUPS];
static uint32_t sc2_num_terrain_layers;
static cameraHeightMap_t sc2_camera_height;
/* Shared normal grid: (MAP_W+1)*(MAP_H+1) normals derived from the heightmap.
   Both ground vertices and cliff boundary vertices index into this so seams never appear. */
static vector3_t *sc2_terrain_normals;

static float r_sc2_camera_grid_height(void const *data, uint32_t x, uint32_t y) {
    return sc2_map_height_at_grid(data, x, y);
}

typedef struct sc2CliffModel_s {
    PATHSTR path;
    model_t const *model;
    struct sc2CliffModel_s *next;
} sc2CliffModel_t;

typedef struct rSc2CliffPlacement_s {
    float base_z;
    float model_z_offset;
    float z_scale;
    uint32_t join_edges;
    sc2Ramp_t const *ramp;
} rSc2CliffPlacement_t;

typedef struct rSc2CliffBakeBatch_s {
    rCliffBakeList_t list;
    texture_t const *texture;
    struct rSc2CliffBakeBatch_s *next;
} rSc2CliffBakeBatch_t;

/* Borrow the terrain bake until road projection finishes; no second cliff geometry representation. */
static rSc2CliffBakeBatch_t *sc2_road_cliffs;

typedef enum {
    SC2_CLIFF_JOIN_LEFT = 1 << 0,
    SC2_CLIFF_JOIN_RIGHT = 1 << 1,
    SC2_CLIFF_JOIN_BOTTOM = 1 << 2,
    SC2_CLIFF_JOIN_TOP = 1 << 3,
} rSc2CliffJoin_t;

static sc2CliffModel_t *sc2_cliff_models;

static void r_sc2_release_cliff_models(void);
static sc2CliffCell_t const *r_sc2_find_cliff_cell(sc2Map_t const *map, uint32_t index);

typedef struct sc2TerrainState_s {
    matrix4_t viewProjection;
    matrix4_t textureMatrix;
    matrix4_t model;
    matrix4_t lightMatrix;
    vector3_t lightAmbient;
    vector3_t lightDir[SC2_MAX_DIRECTIONAL_LIGHTS];
    vector3_t lightColor[SC2_MAX_DIRECTIONAL_LIGHTS];
    int layer0;
    int layer1;
    int layer2;
    int layer3;
    int mask;
    int shadowmap;
    vector2_t worldUVOffset;
    vector2_t worldUVScale;
    vector4_t fogColor;
    vector4_t fogParams;
} sc2TerrainState_t;


typedef struct sc2TerrainProg_s {
    shaderProg_t prog;
    sc2TerrainState_t state;
} sc2TerrainProg_t;



typedef struct sc2CliffState_s {
    matrix4_t viewProjection;
    matrix4_t model;
    matrix4_t lightMatrix;
    vector3_t lightAmbient;
    vector3_t lightDir[SC2_MAX_DIRECTIONAL_LIGHTS];
    vector3_t lightColor[SC2_MAX_DIRECTIONAL_LIGHTS];
    int texture;
    int shadowmap;
    vector4_t fogColor;
    vector4_t fogParams;
} sc2CliffState_t;


typedef struct sc2CliffProg_s {
    shaderProg_t prog;
    sc2CliffState_t state;
} sc2CliffProg_t;



static sc2TerrainProg_t sc2_terrain_shader;
static sc2CliffProg_t sc2_cliff_shader;

#define SHADER_TYPE sc2TerrainState_t
static const shader_desc_t sd_sc2_terrain = {
    .Name = "sc2_terrain",
    .Uniforms = {
        UNIFORM(viewProjection, UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(textureMatrix,  UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(model,          UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(lightMatrix,    UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(lightAmbient,   UT_FLOAT_VEC3, PRECISION_LOW),
        UNIFORM(lightDir,         UT_FLOAT_VEC3, PRECISION_LOW, SC2_MAX_DIRECTIONAL_LIGHTS),
        UNIFORM(lightColor,       UT_FLOAT_VEC3, PRECISION_LOW, SC2_MAX_DIRECTIONAL_LIGHTS),
        UNIFORM(layer0,         UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(layer1,         UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(layer2,         UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(layer3,         UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(mask,           UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(shadowmap,      UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(worldUVOffset,  UT_FLOAT_VEC2, PRECISION_LOW),
        UNIFORM(worldUVScale,   UT_FLOAT_VEC2, PRECISION_LOW),
        UNIFORM(fogColor,       UT_FLOAT_VEC4, PRECISION_LOW),
        UNIFORM(fogParams,      UT_FLOAT_VEC4, PRECISION_LOW),
    },
    .Attributes = {
        ATTRIB(position, attrib_position, UT_FLOAT_VEC3),
        ATTRIB(normal,   attrib_normal,   UT_FLOAT_VEC3),
        ATTRIB(color,    attrib_color,    UT_COLOR),
    },
    .Shared = {
        SHARED(texcoord2, UT_FLOAT_VEC2),
        SHARED(worldpos,  UT_FLOAT_VEC3),
        SHARED(light,     UT_FLOAT_VEC3),
        SHARED(key,       UT_FLOAT_VEC3),
        SHARED(shadow,    UT_FLOAT_VEC4),
        SHARED(color,     UT_COLOR),
    },
    .VertexBody =
        "vec3 vertex_lighting(vec3 normal) {\n"
        "  vec3 n = normalize(normal);\n"
        "  vec3 light = u_lightAmbient;\n"
        "  v_key = vec3(0.0);\n"
        "  for (int i = 0; i < " BZ_SC2_STR(SC2_DIFFUSE_LIGHTS) "; i++) {\n"
        "    vec3 l = normalize(u_lightDir[i]);\n"
        "    vec3 contribution = u_lightColor[i] * max(dot(n, l), 0.0);\n"
        "    light += contribution;\n"
        "    if (i == 0) v_key = contribution;\n"
        "  }\n"
        "  return max(light, vec3(0.0));\n"
        "}\n"
        "vec4 vert() {\n"
        "  vec4 pos = u_model * vec4(a_position, 1.0);\n"
        "  v_texcoord2 = (u_textureMatrix * pos).xy;\n"
        "  v_worldpos = pos.xyz;\n"
        "  v_shadow = u_lightMatrix * pos;\n"
        "  v_light = vertex_lighting(mat3(u_model) * a_normal);\n"
        "  v_color = a_color;\n"
        "  return u_viewProjection * pos;\n"
        "}\n",
    .FragmentBody =
        "vec2 get_mask_coord() {\n"
        "  return clamp(v_texcoord2 * 0.5 + vec2(0.5), vec2(0.0), vec2(1.0));\n"
        "}\n"
        "vec2 get_terrain_coord() {\n"
        "  return (v_texcoord2 * 0.5 + vec2(0.5)) * u_worldUVScale + u_worldUVOffset;\n"
        "}\n"
        "float get_height_fog() {\n"
        "  if (u_fogParams.w <= 0.0) return 0.0;\n"
        "  float above = max(v_worldpos.z - u_fogParams.x, 0.0);\n"
        "  float vertical = v_worldpos.z <= u_fogParams.x ? 1.0 : exp(-above * max(u_fogParams.z, 0.0001));\n"
        "  return clamp((u_fogParams.y / max(u_fogParams.z, 0.0001)) * vertical, 0.0, 1.0) * u_fogColor.a;\n"
        "}\n"
        BZ_SHADOW_GLSL
        "vec4 frag() {\n"
        "  vec2 mc = get_mask_coord();\n"
        "  vec2 tc = get_terrain_coord();\n"
        "  vec4 w = texture(u_mask, mc);\n"
        "  vec4 color = texture(u_layer0, tc) * w.r +\n"
        "               texture(u_layer1, tc) * w.g +\n"
        "               texture(u_layer2, tc) * w.b +\n"
        "               texture(u_layer3, tc) * w.a;\n"
        "  color.rgb *= v_color.rgb;\n"
        "  color.rgb *= (v_light - v_key * (1.0 - shadow_visibility(u_shadowmap, v_shadow)));\n"
        "  color.rgb = mix(color.rgb, u_fogColor.rgb, get_height_fog());\n"
        "  color.a = 1.0;\n"
        "  return color;\n"
        "}\n",
};
#undef SHADER_TYPE

#define SHADER_TYPE sc2CliffState_t
static const shader_desc_t sd_sc2_cliff = {
    .Name = "sc2_cliff",
    .Uniforms = {
        UNIFORM(viewProjection, UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(model,          UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(lightMatrix,    UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(lightAmbient,   UT_FLOAT_VEC3, PRECISION_LOW),
        UNIFORM(lightDir,         UT_FLOAT_VEC3, PRECISION_LOW, SC2_MAX_DIRECTIONAL_LIGHTS),
        UNIFORM(lightColor,       UT_FLOAT_VEC3, PRECISION_LOW, SC2_MAX_DIRECTIONAL_LIGHTS),
        UNIFORM(texture,        UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(shadowmap,      UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(fogColor,       UT_FLOAT_VEC4, PRECISION_LOW),
        UNIFORM(fogParams,      UT_FLOAT_VEC4, PRECISION_LOW),
    },
    .Attributes = {
        ATTRIB(position, attrib_position, UT_FLOAT_VEC3),
        ATTRIB(texcoord, attrib_texcoord, UT_FLOAT_VEC2),
        ATTRIB(normal,   attrib_normal,   UT_FLOAT_VEC3),
        ATTRIB(color,    attrib_color,    UT_COLOR),
    },
    .Shared = {
        SHARED(texcoord, UT_FLOAT_VEC2),
        SHARED(worldpos, UT_FLOAT_VEC3),
        SHARED(light,    UT_FLOAT_VEC3),
        SHARED(key,      UT_FLOAT_VEC3),
        SHARED(shadow,   UT_FLOAT_VEC4),
        SHARED(color,    UT_COLOR),
    },
    .VertexBody =
        "vec3 vertex_lighting(vec3 normal) {\n"
        "  vec3 n = normalize(normal);\n"
        "  vec3 light = u_lightAmbient;\n"
        "  v_key = vec3(0.0);\n"
        "  for (int i = 0; i < " BZ_SC2_STR(SC2_DIFFUSE_LIGHTS) "; i++) {\n"
        "    vec3 l = normalize(u_lightDir[i]);\n"
        "    vec3 contribution = u_lightColor[i] * max(dot(n, l), 0.0);\n"
        "    light += contribution;\n"
        "    if (i == 0) v_key = contribution;\n"
        "  }\n"
        "  return max(light, vec3(0.0));\n"
        "}\n"
        "vec4 vert() {\n"
        "  vec4 pos = u_model * vec4(a_position, 1.0);\n"
        "  v_texcoord = a_texcoord;\n"
        "  v_worldpos = pos.xyz;\n"
        "  v_shadow = u_lightMatrix * pos;\n"
        "  v_light = vertex_lighting(mat3(u_model) * a_normal);\n"
        "  v_color = a_color;\n"
        "  return u_viewProjection * pos;\n"
        "}\n",
    .FragmentBody =
        "float get_height_fog() {\n"
        "  if (u_fogParams.w <= 0.0) return 0.0;\n"
        "  float above = max(v_worldpos.z - u_fogParams.x, 0.0);\n"
        "  float vertical = v_worldpos.z <= u_fogParams.x ? 1.0 : exp(-above * max(u_fogParams.z, 0.0001));\n"
        "  return clamp((u_fogParams.y / max(u_fogParams.z, 0.0001)) * vertical, 0.0, 1.0) * u_fogColor.a;\n"
        "}\n"
        BZ_SHADOW_GLSL
        "vec4 frag() {\n"
        "  vec4 color = texture(u_texture, v_texcoord) * v_color;\n"
        "  color.rgb *= (v_light - v_key * (1.0 - shadow_visibility(u_shadowmap, v_shadow)));\n"
        "  color.rgb = mix(color.rgb, u_fogColor.rgb, get_height_fog());\n"
        "  return color;\n"
        "}\n",
};
#undef SHADER_TYPE

static void r_sc2_init_cliff_shader(void) {
    if (sc2_cliff_shader_loaded) return;
    R_LoadShader(&sd_sc2_cliff, NULL, &sc2_cliff_shader);
    sc2_cliff_shader_loaded = true;
}

static void r_sc2_init_terrain_shader(void) {
    if (!sc2_terrain_shader_loaded) {
        R_LoadShader(&sd_sc2_terrain, NULL, &sc2_terrain_shader);
        sc2_terrain_shader_loaded = true;
    }
    r_sc2_init_cliff_shader();
}

static handle_t r_sc2_read_file(cstring_t filename, uint32_t *size) {
    void *buffer = NULL;
    int result = ri.FS_ReadFile(filename, &buffer);
    if (result < 0) {
        if (size) *size = 0;
        return NULL;
    }
    if (size) *size = (uint32_t)result;
    return buffer;
}

static void r_sc2_free_file(handle_t file) {
    ri.FS_FreeFile(file);
}

static void r_sc2_push_vertex_normal(vertex_t *v,
                                     float x,
                                     float y,
                                     float z,
                                     float u,
                                     float t,
                                     uint8_t alpha,
                                     vector3_t normal) {
    v->position = (vector3_t){ x, y, z };
    v->texcoord = (vector2_t){ u, t };
    v->normal = normal;
    v->color = (color32_t){ 255, 255, 255, alpha };
}

static uint16_t r_sc2_cliff_level_at_grid(sc2Map_t const *map, uint32_t x, uint32_t y) {
    uint16_t value;

    if (!map->t3SyncCliffLevel || !map->t3SyncCliffLevel->width || !map->t3SyncCliffLevel->height) {
        return 0;
    }
    x = MIN(map->t3SyncCliffLevel->width - 1, x);
    y = MIN(map->t3SyncCliffLevel->height - 1, y);
    value = map->t3SyncCliffLevel->data[x + y * map->t3SyncCliffLevel->width];
    return value >= SC2_CLIFF_LEVEL_PACKED_MIN ? value >> SC2_CLIFF_LEVEL_SHIFT : value;
}

static bool r_sc2_cliff_block_is_flat(sc2Map_t const *map, uint32_t x, uint32_t y) {
    uint16_t level[4];

    SC2_CLIFF_BLOCK_LEVELS(level, map, x, y);
    return level[1] == level[0] && level[2] == level[0] && level[3] == level[0];
}

static uint32_t r_sc2_cliff_index_at_grid(sc2Map_t const *map, uint32_t x, uint32_t y) {
    uint32_t cliff_width = SC2_CLIFF_WIDTH(map);

    return (x / SC2_CLIFF_BLOCK_SPAN) + (y / SC2_CLIFF_BLOCK_SPAN) * cliff_width;
}

static bool r_sc2_cliff_block_is_ramp(sc2Map_t const *map, uint32_t x, uint32_t y) {
    sc2CliffCell_t const *cell;

    if (!map)
        return false;
    cell = r_sc2_find_cliff_cell(map, r_sc2_cliff_index_at_grid(map, x, y));
    return cell && (cell->flags & SC2_CLIFF_CELL_RAMP);
}

static bool r_sc2_skip_ground_cell(sc2Map_t const *map, uint32_t x, uint32_t y) {
    uint32_t block_x = SC2_CLIFF_BLOCK_ORIGIN(x);
    uint32_t block_y = SC2_CLIFF_BLOCK_ORIGIN(y);

    if (r_sc2_cliff_block_is_ramp(map, block_x, block_y))
        return r_sc2_ramp_covers_ground(map, (vector2_t){x+0.5f,y+0.5f});
    return !r_sc2_cliff_block_is_flat(map, block_x, block_y);
}

#ifdef BZ_SC2_FIX_FLAT_TERRAIN_TIER_MISMATCH
/* HACK: fixes flat SC2 ground tiles whose shared HMAP vertex carries lower cliff-side
   height/extra data while the drawn flat tile and adjacent cliff edge are one tier higher. */
static bool r_sc2_ground_cell_tier(sc2Map_t const *map, uint32_t x, uint32_t y, uint16_t *tier) {
    uint32_t block_x;
    uint32_t block_y;
    uint16_t level[4];

    if (!map || x >= SC2_MAP_WIDTH(map) || y >= SC2_MAP_HEIGHT(map))
        return false;
    block_x = SC2_CLIFF_BLOCK_ORIGIN(x);
    block_y = SC2_CLIFF_BLOCK_ORIGIN(y);
    SC2_CLIFF_BLOCK_LEVELS(level, map, block_x, block_y);
    if (level[1] != level[0] || level[2] != level[0] || level[3] != level[0])
        return false;
    if (tier)
        *tier = level[0];
    return true;
}

static bool r_sc2_ground_cell_base_height(sc2Map_t const *map, uint32_t tile_x, uint32_t tile_y, uint16_t tier, uint16_t *height) {
    uint32_t const vx[4] = { tile_x, tile_x + 1, tile_x + 1, tile_x };
    uint32_t const vy[4] = { tile_y, tile_y, tile_y + 1, tile_y + 1 };
    uint32_t sum = 0;
    uint32_t count = 0;

    FOR_LOOP(i, 4) {
        uint32_t x = MIN(map->t3HeightMap->width - 1, vx[i]);
        uint32_t y = MIN(map->t3HeightMap->height - 1, vy[i]);
        sc2MapHeightSample_t const *sample = &map->t3HeightMap->data[x + y * map->t3HeightMap->width];

        if (sample->extra != tier)
            continue;
        sum += sample->height;
        count++;
    }
    if (!count)
        return false;
    *height = (uint16_t)(sum / count);
    return true;
}

static float r_sc2_ground_height_at_grid(sc2Map_t const *map, uint32_t grid_x, uint32_t grid_y) {
    int tile_x[4] = { (int)grid_x - 1, (int)grid_x,     (int)grid_x - 1, (int)grid_x };
    int tile_y[4] = { (int)grid_y - 1, (int)grid_y - 1, (int)grid_y,     (int)grid_y };
    sc2MapHeightSample_t const *sample;
    uint32_t base_sum = 0;
    uint32_t base_count = 0;

    if (!map || !map->t3HeightMap || !map->t3HeightMap->width || !map->t3HeightMap->height)
        return 0.0f;
    grid_x = MIN(map->t3HeightMap->width - 1, grid_x);
    grid_y = MIN(map->t3HeightMap->height - 1, grid_y);
    sample = &map->t3HeightMap->data[grid_x + grid_y * map->t3HeightMap->width];
    FOR_LOOP(i, 4) {
        uint16_t tier;
        uint16_t height;

        if (tile_x[i] < 0 || tile_y[i] < 0)
            continue;
        if (!r_sc2_ground_cell_tier(map, (uint32_t)tile_x[i], (uint32_t)tile_y[i], &tier))
            continue;
        if (sample->extra == tier)
            return sc2_map_height_at_grid(map, grid_x, grid_y);
        if (!r_sc2_ground_cell_base_height(map, (uint32_t)tile_x[i], (uint32_t)tile_y[i], tier, &height))
            continue;
        base_sum += height;
        base_count++;
    }
    if (!base_count)
        return sc2_map_height_at_grid(map, grid_x, grid_y);
    return ((float)(base_sum / base_count) + (float)sample->adjustment) *
           sc2_map_height_scale(map) - sc2_map_height_offset(map);
}
#else
static float r_sc2_ground_height_at_grid(sc2Map_t const *map, uint32_t grid_x, uint32_t grid_y) {
    return sc2_map_height_at_grid(map, grid_x, grid_y);
}
#endif

static float r_sc2_normal_height(void const *data, uint32_t x, uint32_t y) {
    return r_sc2_ground_height_at_grid(data, x, y);
}

static float r_sc2_ground_height_at_point(sc2Map_t const *map, float x, float y) {
    sc2MapHeightPoint_t p;

    if (!sc2_map_height_point(map, x, y, &p))
        return 0.0f;
    float height[] = { r_sc2_ground_height_at_grid(map, p.x0, p.y0), r_sc2_ground_height_at_grid(map, p.x1, p.y0),
        r_sc2_ground_height_at_grid(map, p.x0, p.y1), r_sc2_ground_height_at_grid(map, p.x1, p.y1) };
    return r_sc2_ground_triangle_height(height, (vector2_t){p.tx, p.ty});
}

/* Only cliff sides bordering emitted ground have a seam that must share the height-grid edge. */
static uint32_t r_sc2_cliff_join_edges(sc2Map_t const *map, uint32_t x, uint32_t y) {
    uint32_t edges = 0;

    FOR_LOOP(i, SC2_CLIFF_BLOCK_SPAN) {
        if (x && !r_sc2_skip_ground_cell(map, x - 1, y + i)) edges |= SC2_CLIFF_JOIN_LEFT;
        if (x + SC2_CLIFF_BLOCK_SPAN < SC2_MAP_WIDTH(map) &&
            !r_sc2_skip_ground_cell(map, x + SC2_CLIFF_BLOCK_SPAN, y + i)) edges |= SC2_CLIFF_JOIN_RIGHT;
        if (y && !r_sc2_skip_ground_cell(map, x + i, y - 1)) edges |= SC2_CLIFF_JOIN_BOTTOM;
        if (y + SC2_CLIFF_BLOCK_SPAN < SC2_MAP_HEIGHT(map) &&
            !r_sc2_skip_ground_cell(map, x + i, y + SC2_CLIFF_BLOCK_SPAN)) edges |= SC2_CLIFF_JOIN_TOP;
    }
    return edges;
}

static bool r_sc2_cliff_vertex_joins_ground(vector3_t const *pos, uint32_t edges) {
    return ((edges & SC2_CLIFF_JOIN_LEFT) && fabsf(pos->x + 1.0f) < SC2_EPSILON) ||
           ((edges & SC2_CLIFF_JOIN_RIGHT) && fabsf(pos->x - 1.0f) < SC2_EPSILON) ||
           ((edges & SC2_CLIFF_JOIN_BOTTOM) && fabsf(pos->y + 1.0f) < SC2_EPSILON) ||
           ((edges & SC2_CLIFF_JOIN_TOP) && fabsf(pos->y - 1.0f) < SC2_EPSILON);
}

static void r_sc2_build_terrain_normal_grid(sc2Map_t const *map) {
    uint32_t w = SC2_MAP_WIDTH(map);
    uint32_t h = SC2_MAP_HEIGHT(map);
    terrainNormals_t grid = { map, r_sc2_normal_height, w + 1, h + 1, map->cell_size };
    uint32_t n = (w + 1) * (h + 1);

    ri.MemFree(sc2_terrain_normals);
    sc2_terrain_normals = ri.MemAlloc(n * sizeof(*sc2_terrain_normals));
    FOR_LOOP(i, n)
        sc2_terrain_normals[i] = R_TerrainGridNormal(&grid, i % (w + 1), i / (w + 1));
}

static vector3_t r_sc2_terrain_normal_at_world(sc2Map_t const *map, float wx, float wy) {
    box2_t bounds = SC2_MapBounds();
    uint32_t w = SC2_MAP_WIDTH(map);
    uint32_t h = SC2_MAP_HEIGHT(map);
    float gx = (wx - bounds.min.x) / map->cell_size;
    float gy = (wy - bounds.min.y) / map->cell_size;
    int x0 = (int)floorf(gx), y0 = (int)floorf(gy);
    float tx = gx - (float)x0, ty = gy - (float)y0;
    int x1, y1;
    vector3_t n00, n10, n01, n11, n0, n1, result;

    if (!sc2_terrain_normals) return (vector3_t){ 0.0f, 0.0f, 1.0f };
    x0 = MAX(0, MIN((int)w, x0));
    y0 = MAX(0, MIN((int)h, y0));
    x1 = MIN((int)w, x0 + 1);
    y1 = MIN((int)h, y0 + 1);
    n00 = sc2_terrain_normals[x0 + y0 * (w + 1)];
    n10 = sc2_terrain_normals[x1 + y0 * (w + 1)];
    n01 = sc2_terrain_normals[x0 + y1 * (w + 1)];
    n11 = sc2_terrain_normals[x1 + y1 * (w + 1)];
    n0 = Vector3_lerp(&n00, &n10, tx);
    n1 = Vector3_lerp(&n01, &n11, tx);
    result = Vector3_lerp(&n0, &n1, ty);
    Vector3_normalize(&result);
    return result;
}

static void r_sc2_build_ground_vertex_normals(sc2Map_t const *map,
                                              vertex_t *vertices,
                                              uint32_t w,
                                              uint32_t h) {
    uint32_t num_vertices = (w + 1) * (h + 1);

    if (sc2_terrain_normals) {
        FOR_LOOP(i, num_vertices)
            vertices[i].normal = sc2_terrain_normals[i];
    }
}

static void r_sc2_release_layer(maplayer_t *layer) {
    while (layer) {
        maplayer_t *next = layer->next;
        R_ReleaseVertexArrayObject((buffer_t *)layer->buffer);
        R_ReleaseTexture((texture_t *)layer->texture);
        ri.MemFree(layer);
        layer = next;
    }
}

static void r_sc2_release_terrain(void) {
    R_FreeCameraHeightMap(&sc2_camera_height);
    while (sc2_terrain_segment) {
        mapsegment_t *next = sc2_terrain_segment->next;
        r_sc2_release_layer(sc2_terrain_segment->layers);
        ri.MemFree(sc2_terrain_segment);
        sc2_terrain_segment = next;
    }
    FOR_LOOP(i, SC2_TERRAIN_BLEND_LAYERS) {
        SAFE_DELETE(sc2_terrain_textures[i], R_ReleaseTexture);
    }
    FOR_LOOP(i, SC2_TERRAIN_BLEND_GROUPS) {
        SAFE_DELETE(sc2_terrain_masks[i], R_ReleaseTexture);
    }
    r_sc2_release_cliff_models();
    while (sc2_hard_tile_layers) {
        maplayer_t *next = sc2_hard_tile_layers->next;
        R_ReleaseVertexArrayObject((buffer_t *)sc2_hard_tile_layers->buffer);
        ri.MemFree(sc2_hard_tile_layers); sc2_hard_tile_layers = next;
    }
    if (sc2_hard_tile_model) R_ReleaseModel((model_t *)sc2_hard_tile_model);
    sc2_hard_tile_model = NULL;
    sc2_num_terrain_layers = 0;
    ri.MemFree(sc2_terrain_normals);
    sc2_terrain_normals = NULL;
}

/* Count or emit the same ground-triangle intersections, including cliff/ramp omissions. */
static uint32_t r_sc2_project_road(sc2Map_t const *map, sc2RoadTri_t const *tri, vertex_t *out) {
    vertex_t const *road = tri->verts;
    box2_t bounds = SC2_MapBounds();
    vector2_t lo = {road[0].position.x, road[0].position.y}, hi = lo;
    uint32_t total = 0;
    FOR_LOOP(i, 3) {
        lo.x = MIN(lo.x, road[i].position.x); lo.y = MIN(lo.y, road[i].position.y);
        hi.x = MAX(hi.x, road[i].position.x); hi.y = MAX(hi.y, road[i].position.y);
    }
    int x0 = MAX(0, (int)floorf((lo.x-bounds.min.x)/map->cell_size));
    int y0 = MAX(0, (int)floorf((lo.y-bounds.min.y)/map->cell_size));
    int x1 = MIN((int)SC2_MAP_WIDTH(map)-1, (int)floorf((hi.x-bounds.min.x)/map->cell_size));
    int y1 = MIN((int)SC2_MAP_HEIGHT(map)-1, (int)floorf((hi.y-bounds.min.y)/map->cell_size));
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            vertex_t corners[4] = {0};
            if (r_sc2_skip_ground_cell(map, x, y)) continue;
            FOR_LOOP(i, 4) {
                uint32_t gx = x + (i == 1 || i == 2), gy = y + (i >= 2);
                corners[i].position = (vector3_t){bounds.min.x+gx*map->cell_size, bounds.min.y+gy*map->cell_size, r_sc2_ground_height_at_grid(map, gx, gy)};
                corners[i].normal = sc2_terrain_normals[gx + gy*(SC2_MAP_WIDTH(map)+1)];
            }
            FOR_LOOP(i, 2) {
                vertex_t ground[] = {corners[0], corners[i+1], corners[i+2]};
                total += r_sc2_clip_road(road, ground, out ? out+total : NULL);
            }
        }
    }
    /* A cliff cell has real M3 top geometry instead of grid triangles, including bridge approaches. */
    for (rSc2CliffBakeBatch_t const *batch = sc2_road_cliffs; batch; batch = batch->next) {
        for (uint32_t i = 0; i < batch->list.num_vertices; i += 3) {
            vertex_t const *cliff = batch->list.vertices+i;
            float minx = MIN(cliff[0].position.x, MIN(cliff[1].position.x, cliff[2].position.x));
            float maxx = MAX(cliff[0].position.x, MAX(cliff[1].position.x, cliff[2].position.x));
            float miny = MIN(cliff[0].position.y, MIN(cliff[1].position.y, cliff[2].position.y));
            float maxy = MAX(cliff[0].position.y, MAX(cliff[1].position.y, cliff[2].position.y));
            if (minx >= hi.x || maxx <= lo.x || miny >= hi.y || maxy <= lo.y) continue;
            total += r_sc2_clip_road_cliff(tri, cliff, out ? out+total : NULL);
        }
    }
    return total;
}

/* Bake exact-size draped geometry; split M3 buffers before their 16-bit indices overflow. */
static void r_sc2_bake_roads(sc2Map_t const *map, sc2RoadTri_t const *roads, uint32_t count) {
    uint32_t total = 0, used = 0;
    FOR_LOOP(i, count) total += r_sc2_project_road(map, roads+i, NULL);
    if (!total) return;
    vertex_t *baked = ri.MemAlloc(total * sizeof(*baked));
    if (!baked) { fprintf(stderr, "SC2 road build: allocation failed for %u vertices\n", total); return; }
    FOR_LOOP(i, count) used += r_sc2_project_road(map, roads+i, baked+used);
    for (uint32_t first = 0; first < total;) {
        uint32_t n = MIN(total-first, 65535u); /* Complete triangles, addressable by M3's uint16_t indices. */
        uint16_t *faces = ri.MemAlloc(n * sizeof(*faces));
        maplayer_t *layer = ri.MemAlloc(sizeof(*layer));
        if (!faces || !layer) {
            fprintf(stderr, "SC2 road build: buffer allocation failed for %u vertices\n", n);
            ri.MemFree(faces); ri.MemFree(layer); break;
        }
        FOR_LOOP(i, n) faces[i] = i;
        memset(layer, 0, sizeof(*layer));
        layer->num_vertices = layer->num_indices = n;
        buffer_t *buffer = R_MakeVertexArrayObject(baked+first, n);
        layer->buffer = buffer;
        R_Call(glBindVertexArray, layer->buffer->vao);
        R_Call(glGenBuffers, 1, &buffer->ibo);
        R_Call(glBindBuffer, GL_ELEMENT_ARRAY_BUFFER, layer->buffer->ibo);
        R_Call(glBufferData, GL_ELEMENT_ARRAY_BUFFER, n*sizeof(*faces), faces, GL_STATIC_DRAW);
        layer->next = sc2_hard_tile_layers; sc2_hard_tile_layers = layer;
        ri.MemFree(faces); first += n;
    }
    ri.MemFree(baked);
}

/* Build the original Bezier ribbon as projection triangles; terrain owns the final surface Z. */
static void r_sc2_build_hard_tiles(sc2Map_t const *map) {
    uint32_t count = 0;
    float distance = 0;
    bool chain = false;
    vertex_t prev[2] = {0};

    if (!map || IS_ARRAY_EMPTY(map->hard_tiles)) return;
    sc2RoadTri_t *roads = ri.MemAlloc(ARRAY_COUNT(map->hard_tiles)*SC2_HARD_TILE_CURVE_STEPS*2*sizeof(*roads));
    if (!roads) { fprintf(stderr, "SC2 road build: ribbon allocation failed\n"); return; }
    FOR_EACH_ARRAY(sc2MapHardTile_t, tile, map->hard_tiles) {
        uint32_t index = tile-map->hard_tiles;
        sc2MapHardTile_t const *next = tile+1;
        if (!tile->model[0]) {
            fprintf(stderr, "SC2 road build[%u]: CTile='%s' has no resolved model\n", index, tile->tile);
            chain = false; continue;
        }
        if (index+1 >= ARRAY_COUNT(map->hard_tiles) || tile->flags & SC2_HARD_TILE_END || next->flags & SC2_HARD_TILE_START) {
            chain = false; continue;
        }
        if (!sc2_hard_tile_model) sc2_hard_tile_model = R_LoadModel(tile->model);
        if (!sc2_hard_tile_model || sc2_hard_tile_model->modeltype != ID_43DM || !sc2_hard_tile_model->m3) {
            fprintf(stderr, "SC2 road build[%u]: M3 load failed CTile='%s' model='%s'\n", index, tile->tile, tile->model);
            chain = false; continue;
        }
        if (!chain) distance = 0;
        for (uint32_t step = chain ? 1 : 0; step <= SC2_HARD_TILE_CURVE_STEPS; step++) {
            float t = step/(float)SC2_HARD_TILE_CURVE_STEPS;
            vector3_t point = r_sc2_hard_tile_curve_point(tile, next, t);
            vector3_t tangent = r_sc2_hard_tile_curve_tangent(tile, next, t);
            vector3_t normal = Vector3_lerp(&tile->normal, &next->normal, t), side;
            float width = LerpNumber(tile->scale.x, next->scale.x, t);
            vertex_t cur[2] = {0};
            if (step) {
                vector3_t p = r_sc2_hard_tile_curve_point(tile, next, (step-1)/(float)SC2_HARD_TILE_CURVE_STEPS);
                distance += Vector3_distance(&point, &p)/MAX(width*2*SC2_HARD_TILE_BODY_ASPECT, SC2_EPSILON);
            }
            Vector3_normalize(&normal); side = Vector3_cross(&tangent, &normal); Vector3_normalize(&side);
            FOR_LOOP(edge, 2) {
                float sign = edge ? 1 : -1;
                cur[edge].position = Vector3_add(&point, &(vector3_t){side.x*width*sign, side.y*width*sign, side.z*width*sign});
                cur[edge].normal = normal;
                cur[edge].texcoord = (vector2_t){distance, SC2_HARD_TILE_BODY_V + (edge ? .5f : 0)};
                cur[edge].color = COLOR32_WHITE; cur[edge].boneWeight[0] = 255;
            }
            if (step) {
                float depth = MAX(tile->scale.y, next->scale.y);
                roads[count++] = (sc2RoadTri_t){ {prev[0], cur[0], prev[1]}, depth };
                roads[count++] = (sc2RoadTri_t){ {prev[1], cur[0], cur[1]}, depth };
            }
            memcpy(prev, cur, sizeof(prev));
        }
        chain = !(next->flags & SC2_HARD_TILE_END);
    }
    r_sc2_bake_roads(map, roads, count);
    sc2_hard_tile_entity.model = sc2_hard_tile_model; sc2_hard_tile_entity.scale = 1;
    ri.MemFree(roads);
}

static void r_sc2_release_cliff_models(void) {
    while (sc2_cliff_models) {
        sc2CliffModel_t *next = sc2_cliff_models->next;
        R_ReleaseModel((model_t *)sc2_cliff_models->model);
        ri.MemFree(sc2_cliff_models);
        sc2_cliff_models = next;
    }
}

static void r_sc2_add_layer(maplayer_t * *list, maplayer_t *layer) {
    maplayer_t * *tail = list;

    if (!layer) {
        return;
    }
    while (*tail) {
        tail = &(*tail)->next;
    }
    *tail = layer;
}

static uint8_t r_sc2_texture_mask_nibble(uint8_t byte, uint32_t pixel) {
    return pixel & 1 ? byte & 0x0F : byte >> 4;
}

static uint32_t r_sc2_texture_mask_layer_stride(sc2Map_t const *map) {
    uint32_t w, h, blocks_x, blocks_y, packed_layer_size, block_layer_size;

    if (!map || !map->t3TextureMasks)
        return 0;
    w = map->t3TextureMasks->width;
    h = map->t3TextureMasks->height;
    packed_layer_size = (w * h) / 2;
    blocks_x = (w + 63) / 64;
    blocks_y = (h + 63) / 64;
    block_layer_size = blocks_x * blocks_y * 64 * 32;
    if (block_layer_size && map->t3TextureMasksSize >= sizeof(*map->t3TextureMasks) + block_layer_size &&
        (map->t3TextureMasksSize - sizeof(*map->t3TextureMasks)) % block_layer_size == 0)
        return block_layer_size;
    return packed_layer_size;
}

static uint32_t r_sc2_texture_mask_layers(sc2Map_t const *map) {
    uint32_t stride = r_sc2_texture_mask_layer_stride(map);

    if (!map || !map->t3TextureMasks || !stride || map->t3TextureMasksSize < sizeof(*map->t3TextureMasks))
        return 0;
    return (map->t3TextureMasksSize - sizeof(*map->t3TextureMasks)) / stride;
}

static void r_sc2_decode_texture_mask_block(uint8_t *out, uint8_t *src, uint32_t width, uint32_t height, uint32_t blocks_x, uint32_t block) {
    uint32_t bx = block % blocks_x;
    uint32_t by = block / blocks_x;

    FOR_LOOP(y, 64) {
        FOR_LOOP(x, 64) {
            uint32_t px = bx * 64 + x;
            uint32_t py = by * 64 + y;
            if (px >= width || py >= height)
                continue;
            out[px + py * width] = r_sc2_texture_mask_nibble(src[y * 32 + x / 2], x);
        }
    }
}

static void r_sc2_decode_texture_mask_layer(sc2Map_t const *map, uint32_t layer, uint8_t *values) {
    uint32_t w, h, stride, block_stride, blocks_x, blocks_y;
    uint8_t *src;

    if (!map->t3TextureMasks || !map->t3TextureMasks->width || !map->t3TextureMasks->height || layer >= r_sc2_texture_mask_layers(map))
        return;
    w = map->t3TextureMasks->width;
    h = map->t3TextureMasks->height;
    stride = r_sc2_texture_mask_layer_stride(map);
    blocks_x = (w + 63) / 64;
    blocks_y = (h + 63) / 64;
    block_stride = blocks_x * blocks_y * 64 * 32;
    src = map->t3TextureMasks->data + layer * stride;
    if (stride == block_stride && block_stride > 0) {
        FOR_LOOP(block, blocks_x * blocks_y) {
            r_sc2_decode_texture_mask_block(values, src + block * 64 * 32, w, h, blocks_x, block);
        }
    } else {
        FOR_LOOP(i, w * h) {
            values[i] = r_sc2_texture_mask_nibble(src[i / 2], i);
        }
    }
}

static texture_t *r_sc2_build_mask_texture(sc2Map_t const *map, uint32_t group) {
    uint32_t w = 1;
    uint32_t h = 1;
    uint8_t *values;
    color32_t *pixels;
    texture_t *texture;

    if (map->t3TextureMasks && map->t3TextureMasks->width && map->t3TextureMasks->height) {
        w = map->t3TextureMasks->width;
        h = map->t3TextureMasks->height;
    }
    values = ri.MemAlloc(w * h * SC2_TERRAIN_BLEND_LAYERS);
    pixels = ri.MemAlloc(w * h * sizeof(*pixels));
    memset(values, 0, w * h * SC2_TERRAIN_BLEND_LAYERS);
    if (map->t3TextureMasks && map->t3TextureMasks->width && map->t3TextureMasks->height) {
        FOR_LOOP(layer, MIN(sc2_num_terrain_layers, SC2_TERRAIN_BLEND_LAYERS)) {
            r_sc2_decode_texture_mask_layer(map, layer, values + layer * w * h);
        }
    }
    FOR_LOOP(i, w * h) {
        uint32_t total = 0;
        uint8_t mask[SC2_TERRAIN_BLEND_LAYERS];
        FOR_LOOP(layer, SC2_TERRAIN_BLEND_LAYERS) {
            mask[layer] = (uint8_t)(values[layer * w * h + i] * 17);
            if (layer < sc2_num_terrain_layers) {
                total += mask[layer];
            }
        }
        if (!total) {
            memset(mask, 0, sizeof(mask));
            mask[0] = 255;
            total = 255;
        }
        /* Layer weights are RGBA on every backend, just like the common texture upload contract. */
        pixels[i] = (color32_t){
                               (uint8_t)((mask[group * SC2_TERRAIN_PASS_LAYERS + 0] * 255u + total / 2) / total),
                               (uint8_t)((mask[group * SC2_TERRAIN_PASS_LAYERS + 1] * 255u + total / 2) / total),
                               (uint8_t)((mask[group * SC2_TERRAIN_PASS_LAYERS + 2] * 255u + total / 2) / total),
                               (uint8_t)((mask[group * SC2_TERRAIN_PASS_LAYERS + 3] * 255u + total / 2) / total)};
    }
    texture = R_AllocateTexture(w, h);
    R_LoadTextureMipLevel(texture, &(texMip_t){ pixels, w, h, 0, PIXEL_RGBA });
    R_SetTextureWrap(texture, false, false);
    ri.MemFree(values);
    ri.MemFree(pixels);
    return texture;
}

static void r_sc2_load_terrain_textures(sc2Map_t const *map) {
    sc2_num_terrain_layers = MIN(MAX(1, map->t3Terrain.num_terrain_textures), SC2_TERRAIN_BLEND_LAYERS);
    FOR_LOOP(i, sc2_num_terrain_layers) {
        if (map->t3Terrain.num_terrain_textures > i) {
            sc2_terrain_textures[i] = R_LoadTexture(map->t3Terrain.terrain_textures[i].diffuse);
            R_SetTextureWrap(sc2_terrain_textures[i], true, true);
        }
    }
    FOR_LOOP(i, SC2_TERRAIN_BLEND_GROUPS) {
        sc2_terrain_masks[i] = r_sc2_build_mask_texture(map, i);
    }
}

static maplayer_t *r_sc2_build_ground_layer(sc2Map_t const *map) {
    box2_t bounds;
    uint32_t w, h, num_vertices, num_indices;
    vertex_t *vertices;
    uint32_t *indices;
    uint32_t *out;
    maplayer_t *map_layer;

    if (!map || !SC2_MAP_WIDTH(map) || !SC2_MAP_HEIGHT(map))
        return NULL;

    w = SC2_MAP_WIDTH(map);
    h = SC2_MAP_HEIGHT(map);
    num_vertices = (w + 1) * (h + 1);
    num_indices = w * h * 6;
    vertices = ri.MemAlloc(num_vertices * sizeof(*vertices));
    indices = ri.MemAlloc(num_indices * sizeof(*indices));
    bounds = SC2_MapBounds();

    for (uint32_t y = 0; y <= h; y++) {
        for (uint32_t x = 0; x <= w; x++) {
            float px = bounds.min.x + x * map->cell_size;
            float py = bounds.min.y + y * map->cell_size;
            float u = px / SC2_TERRAIN_UV_SCALE;
            float v = py / SC2_TERRAIN_UV_SCALE;
            /* SC2 lighting needs mesh normals rebuilt after tier height correction. */
//            r_sc2_push_vertex(&vertices[x + y * (w + 1)], px, py, r_sc2_ground_height_at_grid(map, x, y), u, v, 255);
            r_sc2_push_vertex_normal(&vertices[x + y * (w + 1)],
                                     px,
                                     py,
                                     r_sc2_ground_height_at_grid(map, x, y),
                                     u,
                                     v,
                                     255,
                                     (vector3_t){ 0.0f, 0.0f, 1.0f });
        }
    }
    r_sc2_build_ground_vertex_normals(map, vertices, w, h);

    out = indices;
    FOR_LOOP(y, h) {
        FOR_LOOP(x, w) {
            uint32_t i00 = x + y * (w + 1);
            uint32_t i10 = i00 + 1;
            uint32_t i01 = i00 + w + 1;
            uint32_t i11 = i01 + 1;

            if (r_sc2_skip_ground_cell(map, x, y)) {
                continue;
            }
            *out++ = i00; *out++ = i10; *out++ = i11;
            *out++ = i00; *out++ = i11; *out++ = i01;
        }
    }

    map_layer = ri.MemAlloc(sizeof(*map_layer));
    memset(map_layer, 0, sizeof(*map_layer));
    map_layer->type = MAPLAYERTYPE_GROUND;
    map_layer->buffer = R_MakeIndexedVertexArrayObject(vertices, num_vertices, indices, (uint32_t)(out - indices));
    map_layer->num_vertices = num_vertices;
    map_layer->num_indices = (uint32_t)(out - indices);
    ri.MemFree(vertices);
    ri.MemFree(indices);
    return map_layer;
}

static bool r_sc2_file_exists(cstring_t path) {
    void *buffer = NULL;
    int size;

    size = ri.FS_ReadFile(path, &buffer);
    if (size < 0 || !buffer)
        return false;
    ri.FS_FreeFile(buffer);
    return true;
}

static model_t const *r_sc2_load_cliff_model(cstring_t path) {
    sc2CliffModel_t *cliff;

    for (cliff = sc2_cliff_models; cliff; cliff = cliff->next) {
        if (!strcmp(cliff->path, path))
            return cliff->model;
    }
    cliff = ri.MemAlloc(sizeof(*cliff));
    memset(cliff, 0, sizeof(*cliff));
    snprintf(cliff->path, sizeof(cliff->path), "%s", path);
    cliff->model = R_LoadModel(path);
    ADD_TO_LIST(cliff, sc2_cliff_models);
    return cliff->model;
}

static bool r_sc2_cliff_model_path(string_t path, size_t size, cstring_t mesh, char const config[5], uint32_t variant) {
    snprintf(path, size, "Assets\\Cliffs\\%s\\%s_%s_%02u.m3", mesh, mesh, config, variant & SC2_CLIFF_VARIANT_MASK);
    if (r_sc2_file_exists(path))
        return true;
    snprintf(path, size, "Assets\\Cliffs\\%s\\%s_%s_00.m3", mesh, mesh, config);
    if (r_sc2_file_exists(path))
        return true;
    return false;
}

static bool r_sc2_cliff_config(sc2Map_t const *map,
                               uint32_t x,
                               uint32_t y,
                               char config[5],
                               uint16_t *baselevel,
                               uint16_t *toplevel,
                               int *rotation) {
    uint16_t level[4];
    uint16_t base;
    uint16_t top;
    char raw[5];
    int best;

    /* SC2 cliff models use BL, BR, TR, TL corner order. */
    SC2_CLIFF_BLOCK_LEVELS(level, map, x, y);
    base = MIN(MIN(level[0], level[1]), MIN(level[2], level[3]));
    top = MAX(MAX(level[0], level[1]), MAX(level[2], level[3]));
    FOR_LOOP(i, 4) {
        raw[i] = (char)('A' + MIN(level[i], 25));
    }
    raw[4] = 0;

    best = 0;
    FOR_LOOP(r, 4) {
        FOR_LOOP(k, 4) {
            char a = raw[(best + k) & 3];
            char b = raw[(r + k) & 3];
            if (b < a) { best = r; break; }
            if (b > a) break;
        }
    }
    FOR_LOOP(i, 4) {
        config[i] = raw[(best + i) & 3];
    }
    config[4] = 0;

    if (baselevel) {
        *baselevel = base;
    }
    if (toplevel) {
        *toplevel = top;
    }
    if (rotation) {
        *rotation = best;
    }
    return config[0] != config[1] || config[1] != config[2] || config[2] != config[3];
}

static vector3_t r_sc2_rotate_cliff_vec(vector3_t pos, int rotation) {
    switch (rotation & 3) {
        case 1: return (vector3_t){ -pos.y,  pos.x, pos.z };
        case 2: return (vector3_t){ -pos.x, -pos.y, pos.z };
        case 3: return (vector3_t){  pos.y, -pos.x, pos.z };
        default: return pos;
    }
}

static vector3_t r_sc2_m3_raw_normal(m3Vertex_t const *vertex) {
    vector3_t normal = {
        (float)vertex->normal[0] * SC2_M3_NORMAL_SCALE - 1.0f,
        (float)vertex->normal[1] * SC2_M3_NORMAL_SCALE - 1.0f,
        (float)vertex->normal[2] * SC2_M3_NORMAL_SCALE - 1.0f,
    };
    Vector3_normalize(&normal);
    return normal;
}

static vector3_t r_sc2_matrix_transform(matrix4_t const *matrix, vector3_t const *v, float w) {
    return (vector3_t){
        matrix->v[0] * v->x + matrix->v[4] * v->y + matrix->v[8]  * v->z + matrix->v[12] * w,
        matrix->v[1] * v->x + matrix->v[5] * v->y + matrix->v[9]  * v->z + matrix->v[13] * w,
        matrix->v[2] * v->x + matrix->v[6] * v->y + matrix->v[10] * v->z + matrix->v[14] * w,
    };
}

static void r_sc2_m3_make_bone_matrix(vector3_t const *position,
                                      vector4_t const *rotation,
                                      vector3_t const *scale,
                                      matrix4_t const *parent,
                                      matrix4_t *matrix) {
    matrix4_t local;

    Matrix4_identity(&local);
    Matrix4_translate(&local, position);
    Matrix4_rotate4(&local, rotation);
    Matrix4_scale(&local, scale);
    Matrix4_multiply(parent, &local, matrix);
}

static void r_sc2_m3_build_cliff_bones(m3Model_t const *m3, matrix4_t bones[SC2_M3_MAX_BONES]) {
    matrix4_t tmp[SC2_M3_MAX_BONES];
    matrix4_t identity;
    m3SequenceTimeline_t const *timeline;
    uint32_t localtime = 0;

    Matrix4_identity(&identity);
    FOR_LOOP(i, SC2_M3_MAX_BONES) {
        Matrix4_identity(&tmp[i]);
        Matrix4_identity(&bones[i]);
    }
    if (!m3 || !m3->bones || !m3->boneLookup || !m3->absoluteInverseBoneRestPositions)
        return;
    timeline = M3_FindAnimationAtTime(m3, 0, &localtime);
    FOR_LOOP(i, MIN(m3->bonesNum, SC2_M3_MAX_BONES)) {
        m3Bone_t const *bone = &m3->bones[i];
        vector3_t position = M3_GetVector3AnimValue(m3, timeline, &bone->position, localtime);
        vector4_t rotation = M3_GetVector4AnimValue(m3, timeline, &bone->rotation, localtime);
        vector3_t scale = M3_GetVector3AnimValue(m3, timeline, &bone->scale, localtime);
        matrix4_t const *parent = bone->parent >= 0 && bone->parent < (int16_t)m3->bonesNum ? &tmp[bone->parent] : &identity;

        r_sc2_m3_make_bone_matrix(&position, &rotation, &scale, parent, &tmp[i]);
    }
    FOR_LOOP(i, MIN(m3->boneLookupNum, SC2_M3_MAX_BONES)) {
        m3Uint16_t bone_index = m3->boneLookup[i];
        if (bone_index >= m3->bonesNum || bone_index >= m3->absoluteInverseBoneRestPositionsNum ||
            bone_index >= SC2_M3_MAX_BONES)
            continue;
        Matrix4_multiply(&tmp[bone_index], &m3->absoluteInverseBoneRestPositions[bone_index], &bones[i]);
    }
}

static vector3_t r_sc2_m3_skin_vertex(m3Vertex_t const *vertex,
                                    m3Region_t const *region,
                                    matrix4_t const bones[SC2_M3_MAX_BONES],
                                    float w) {
    vector3_t out = { 0 };
    vector3_t raw = w == 0.0f ? r_sc2_m3_raw_normal(vertex) : vertex->pos;

    FOR_LOOP(i, 4) {
        uint32_t bone = region->firstBoneLookupIndex + vertex->boneIndex[i];
        float weight = (float)vertex->boneWeight[i] / 255.0f;
        vector3_t part;

        if (bone >= SC2_M3_MAX_BONES || weight <= 0.0f)
            continue;
        part = r_sc2_matrix_transform(&bones[bone], &raw, w);
        out.x += part.x * weight;
        out.y += part.y * weight;
        out.z += part.z * weight;
    }
    return out;
}

static bool r_sc2_cliff_model_bounds(model_t const *model, box3_t *bounds) {
    m3Model_t const *m3;
    matrix4_t bones[SC2_M3_MAX_BONES];
    bool have_vertex = false;

    if (!model || model->modeltype != ID_43DM || !model->m3 || !model->m3->verticesNum)
        return false;
    m3 = model->m3;
    r_sc2_m3_build_cliff_bones(m3, bones);
    FOR_LOOP(div_i, m3->divisionsNum) {
        m3Divisions_t const *div = &m3->divisions[div_i];
        FOR_LOOP(region_i, div->regionsNum) {
            m3Region_t const *region = &div->regions[region_i];
            for (uint32_t index_i = 0; index_i + 2 < region->triangleIndicesCount; index_i += 3) {
                uint32_t fi[3], vi[3];
                bool valid = true;
                FOR_LOOP(k, 3) {
                    fi[k] = region->firstTriangleIndex + index_i + k;
                    if (fi[k] >= div->facesNum) { valid = false; break; }
                    vi[k] = div->faces[fi[k]] + region->firstVertexIndex;
                    if (vi[k] >= m3->verticesNum) { valid = false; break; }
                }
                if (!valid)
                    continue;
                FOR_LOOP(k, 3) {
                    vector3_t pos = r_sc2_m3_skin_vertex(&m3->vertices[vi[k]], region, bones, 1.0f);
                    if (!have_vertex) {
                        bounds->min = pos;
                        bounds->max = pos;
                        have_vertex = true;
                        continue;
                    }
                    bounds->min.x = MIN(bounds->min.x, pos.x);
                    bounds->min.y = MIN(bounds->min.y, pos.y);
                    bounds->min.z = MIN(bounds->min.z, pos.z);
                    bounds->max.x = MAX(bounds->max.x, pos.x);
                    bounds->max.y = MAX(bounds->max.y, pos.y);
                    bounds->max.z = MAX(bounds->max.z, pos.z);
                }
            }
        }
    }
    return have_vertex;
}

static vector2_t r_sc2_cliff_vertex_xy(sc2Map_t const *map,
                                     vector2_t const *offset,
                                     vector3_t const *rotated) {
    float scale = SC2_CLIFF_BLOCK_SPAN * map->cell_size / SC2_CLIFF_MODEL_FOOTPRINT;

    return (vector2_t){
        offset->x + rotated->x * scale,
        offset->y + rotated->y * scale,
    };
}

static void r_sc2_bake_cliff_region(rCliffBakeList_t *list,
                                    sc2Map_t const *map,
                                    m3Model_t const *m3,
                                    m3Divisions_t const *div,
                                    m3Region_t const *region,
                                    matrix4_t const bones[SC2_M3_MAX_BONES],
                                    rSc2CliffPlacement_t const *placement,
                                    vector2_t const *offset,
                                    int rotation) {
    for (uint32_t index_i = 0; index_i + 2 < region->triangleIndicesCount; index_i += 3) {
        uint32_t fi[3], vi[3];
        bool valid = true;

        FOR_LOOP(k, 3) {
            fi[k] = region->firstTriangleIndex + index_i + k;
            if (fi[k] >= div->facesNum) { valid = false; break; }
            vi[k] = div->faces[fi[k]] + region->firstVertexIndex;
            if (vi[k] >= m3->verticesNum) { valid = false; break; }
        }
        if (!valid)
            continue;
        FOR_LOOP(k, 3) {
            m3Vertex_t const *vertex = &m3->vertices[vi[k]];
            vertex_t *out = R_CliffBakeVertex(list);
            vector3_t local;
            vector3_t normal;
            vector3_t rotated;
            vector3_t position;
            vector2_t xy;
            vector2_t uv;

            local = r_sc2_m3_skin_vertex(vertex, region, bones, 1.0f);
            normal = r_sc2_m3_skin_vertex(vertex, region, bones, 0.0f);
            rotated = r_sc2_rotate_cliff_vec(local, rotation);
            xy = r_sc2_cliff_vertex_xy(map, offset, &rotated);
            position = (vector3_t){
                xy.x,
                xy.y,
                placement->base_z + (placement->model_z_offset + local.z) * placement->z_scale +
                    sc2_map_height_adjust_at_point(map, xy.x, xy.y),
            };
            /* The M3 silhouette is decorative; vertices at an emitted ground edge must use that exact surface height. */
            {
                bool at_ground_edge = r_sc2_cliff_vertex_joins_ground(&rotated, placement->join_edges) &&
                    fabsf(position.z - r_sc2_ground_height_at_point(map, xy.x, xy.y)) < map->cell_size;
                if (placement->ramp) {
                    vector2_t grid;
                    /* Authored boxes and SC2 world XY share the map's height-grid origin. */
                    box2_t world = SC2_MapBounds();
                    grid = (vector2_t){ (xy.x-world.min.x)/map->cell_size, (xy.y-world.min.y)/map->cell_size };
                    bool border = false;
                    int ix = (int)floorf(grid.x), iy = (int)floorf(grid.y);
                    for (int y = iy-1; y <= iy; y++) for (int x = ix-1; x <= ix; x++) {
                        if (x < 0 || y < 0 || x >= SC2_MAP_WIDTH(map) || y >= SC2_MAP_HEIGHT(map)) continue;
                        float u = grid.x-x, v = grid.y-y;
                        if (u < -SC2_EPSILON || u > 1+SC2_EPSILON || v < -SC2_EPSILON || v > 1+SC2_EPSILON) continue;
                        if (!r_sc2_skip_ground_cell(map, x, y) &&
                            (fabsf(u) < SC2_EPSILON || fabsf(u-1) < SC2_EPSILON || fabsf(v) < SC2_EPSILON || fabsf(v-1) < SC2_EPSILON)) border = true;
                    }
                    at_ground_edge = border && fabsf(position.z-r_sc2_ground_height_at_point(map, xy.x, xy.y)) < map->cell_size;
                }
                if (at_ground_edge)
                    position.z = r_sc2_ground_height_at_point(map, xy.x, xy.y);
                uv = (vector2_t){ vertex->uv[0][0] / SC2_M3_UV_SCALE, vertex->uv[0][1] / SC2_M3_UV_SCALE };
                normal = r_sc2_rotate_cliff_vec(normal, rotation);
                Vector3_normalize(&normal);
                /* Ground-edge cliff vertices share the heightmap normal so cliff and ground mesh
                   use the same source and produce no lighting seam at their boundary. */
                if (at_ground_edge)
                    normal = r_sc2_terrain_normal_at_world(map, position.x, position.y);
                r_sc2_push_vertex_normal(out,
                                         position.x,
                                         position.y,
                                         position.z,
                                         uv.x,
                                         uv.y,
                                         255,
                                         normal);
            }
        }
    }
}

static void r_sc2_bake_cliff_model(rCliffBakeList_t *list,
                                   sc2Map_t const *map,
                                   model_t const *model,
                                   uint32_t grid_x,
                                   uint32_t grid_y,
                                   int rotation,
                                   uint16_t baselevel) {
    box2_t map_bounds = SC2_MapBounds();
    box3_t bounds;
    rSc2CliffPlacement_t placement = {0};
    vector2_t offset;
    m3Model_t const *m3;
    matrix4_t bones[SC2_M3_MAX_BONES];

    if (!model || model->modeltype != ID_43DM || !model->m3 || !model->m3->verticesNum)
        return;
    list->current_group++;
    m3 = model->m3;
    r_sc2_m3_build_cliff_bones(m3, bones);
    if (!r_sc2_cliff_model_bounds(model, &bounds)) {
        bounds.min = (vector3_t){ -map->cell_size, -map->cell_size, 0.0f };
        bounds.max = (vector3_t){  map->cell_size,  map->cell_size, 0.0f };
    }
    offset = (vector2_t){
        map_bounds.min.x + SC2_CLIFF_BLOCK_CENTER(grid_x) * map->cell_size,
        map_bounds.min.y + SC2_CLIFF_BLOCK_CENTER(grid_y) * map->cell_size,
    };
    {
        float scale = sc2_map_height_scale(map);
        float offset_z = sc2_map_height_offset(map);
        uint32_t corner_x[4] = { grid_x, grid_x + SC2_CLIFF_BLOCK_SPAN, grid_x + SC2_CLIFF_BLOCK_SPAN, grid_x };
        uint32_t corner_y[4] = { grid_y, grid_y, grid_y + SC2_CLIFF_BLOCK_SPAN, grid_y + SC2_CLIFF_BLOCK_SPAN };
        uint16_t level[4];
        float base_z = 0.0f;
        uint32_t num_base = 0;

        SC2_CLIFF_BLOCK_LEVELS(level, map, grid_x, grid_y);
        FOR_LOOP(i, 4) {
            uint32_t x = MIN(map->t3HeightMap->width - 1, corner_x[i]);
            uint32_t y = MIN(map->t3HeightMap->height - 1, corner_y[i]);
            sc2MapHeightSample_t const *sample = &map->t3HeightMap->data[x + y * map->t3HeightMap->width];

            if (level[i] != baselevel)
                continue;
            base_z += (float)sample->height * scale - offset_z;
            num_base++;
        }
        placement.base_z = num_base ? base_z / (float)num_base : -bounds.min.z;
        placement.model_z_offset = -bounds.min.z;
        placement.z_scale = 1.0f;
        placement.join_edges = r_sc2_cliff_join_edges(map, grid_x, grid_y);
    }
    FOR_LOOP(div_i, m3->divisionsNum) {
        m3Divisions_t const *div = &m3->divisions[div_i];
        FOR_LOOP(region_i, div->regionsNum) {
            r_sc2_bake_cliff_region(list,
                                    map,
                                    m3,
                                    div,
                                    &div->regions[region_i],
                                    bones,
                                    &placement,
                                    &offset,
                                    rotation);
        }
    }
}

static texture_t const *r_sc2_cliff_diffuse_texture(model_t const *model) {
    m3Model_t const *m3;

    if (!model || model->modeltype != ID_43DM || !model->m3)
        return NULL;
    m3 = model->m3;
    FOR_LOOP(i, m3->materialStandardNum) {
        m3Material_t const *mat = &m3->materialStandard[i];
        if (mat->diffuseLayer && mat->diffuseLayer->texture)
            return mat->diffuseLayer->texture;
    }
    return NULL;
}

static rSc2CliffBakeBatch_t *r_sc2_cliff_bake_batch(rSc2CliffBakeBatch_t **batches, texture_t const *texture) {
    rSc2CliffBakeBatch_t *batch;

    for (batch = *batches; batch; batch = batch->next)
        if (batch->texture == texture) return batch;
    batch = ri.MemAlloc(sizeof(*batch)); memset(batch, 0, sizeof(*batch));
    batch->texture = texture; ADD_TO_LIST(batch, *batches);
    return batch;
}

static sc2CliffCell_t const *r_sc2_find_cliff_cell(sc2Map_t const *map, uint32_t index) {
    FOR_LOOP(i, map->t3Terrain.num_cliff_cells) {
        if (map->t3Terrain.cliff_cells[i].index == index)
            return &map->t3Terrain.cliff_cells[i];
    }
    return NULL;
}

static bool r_sc2_cliff_corner_touches_neighbor(uint32_t corner, int dx, int dy) {
    switch (corner) {
        case 0: return dx <= 0 && dy <= 0;
        case 1: return dx >= 0 && dy <= 0;
        case 2: return dx >= 0 && dy >= 0;
        case 3: return dx <= 0 && dy >= 0;
        default: return false;
    }
}

static int r_sc2_cliff_neighbor_score(uint16_t level[4], uint16_t top, int dx, int dy) {
    int score = 0;

    FOR_LOOP(i, 4) {
        if (level[i] == top && r_sc2_cliff_corner_touches_neighbor(i, dx, dy)) {
            score += 10;
        }
    }
    if (!dx || !dy) {
        score++;
    }
    return score;
}

static sc2CliffCell_t r_sc2_cliff_cell_for_index(sc2Map_t const *map, uint32_t index, uint32_t cliff_width) {
    sc2CliffCell_t cell = { .index = index };
    sc2CliffCell_t const *xml_cell = r_sc2_find_cliff_cell(map, index);
    uint32_t cliff_height = MAX(1, (SC2_MAP_HEIGHT(map) + 1) / 2);
    uint32_t cx = index % cliff_width;
    uint32_t cy = index / cliff_width;
    uint32_t grid_x = cx * SC2_CLIFF_BLOCK_SPAN;
    uint32_t grid_y = cy * SC2_CLIFF_BLOCK_SPAN;
    uint16_t level[4];
    uint16_t base;
    uint16_t top;
    int best_score = 0;

    if (xml_cell)
        return *xml_cell;
    SC2_CLIFF_BLOCK_LEVELS(level, map, grid_x, grid_y);
    base = MIN(MIN(level[0], level[1]), MIN(level[2], level[3]));
    top = MAX(MAX(level[0], level[1]), MAX(level[2], level[3]));
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = (int)cx + dx;
            int ny = (int)cy + dy;
            int score;

            if (!dx && !dy)
                continue;
            if (nx >= 0 && ny >= 0 && nx < (int)cliff_width && ny < (int)cliff_height) {
                xml_cell = r_sc2_find_cliff_cell(map, (uint32_t)nx + (uint32_t)ny * cliff_width);
                if (xml_cell) {
                    score = top > base ? r_sc2_cliff_neighbor_score(level, top, dx, dy) : 1;
                    if (score > best_score) {
                        cell = *xml_cell;
                        cell.index = index;
                        best_score = score;
                    }
                }
            }
        }
    }
    return cell;
}

/* Bake the authored transition footprint instead of discarding every cc flagged as a ramp. */
static void r_sc2_build_ramp_cliffs(sc2Map_t const *map, rSc2CliffBakeBatch_t **batches) {
    box2_t world = SC2_MapBounds();
    FOR_EACH_ARRAY(sc2Ramp_t, ramp, map->t3Terrain.ramps) FOR_LOOP(edge, 4) {
        sc2RampBox_t const *box = &ramp->edge[edge];
        if (ramp->variant[edge] == ~0u || box->width <= 0 || box->height <= 0) continue;
        if (ramp->cid >= map->t3Terrain.num_cliff_sets || !map->t3SyncCliffLevel) {
            fprintf(stderr, "SC2 ramp: unresolved cliff set %u or missing level grid\n", ramp->cid); continue;
        }
        sc2RampPiece_t piece = r_sc2_ramp_piece(map, ramp, edge);
        cstring_t mesh = map->t3Terrain.cliff_sets[ramp->cid].mesh;
        PATHSTR path;
        bool found = false;
        FOR_LOOP(turn, 4) {
            char config[5] = {0};
            FOR_LOOP(i, 4) config[i] = piece.config[(turn+i)&3];
            if (!r_sc2_cliff_model_path(path, sizeof(path), mesh, config, ramp->variant[edge])) continue;
            piece.rotation = (piece.rotation+turn)&3; found = true; break;
        }
        if (!found) {
            fprintf(stderr, "SC2 ramp: missing model '%s' at %.1f %.1f\n", path, box->center.x, box->center.y); continue;
        }
        model_t const *model = r_sc2_load_cliff_model(path);
        box3_t bounds;
        if (!model || model->modeltype != ID_43DM || !model->m3 || !r_sc2_cliff_model_bounds(model, &bounds)) {
            fprintf(stderr, "SC2 ramp: invalid M3 '%s'\n", path); continue;
        }
        rSc2CliffPlacement_t place = { .model_z_offset = -bounds.min.z, .z_scale = 1,
            .ramp = ramp };
        vector2_t corners[] = { piece.bounds.min, {piece.bounds.max.x, piece.bounds.min.y},
            piece.bounds.max, {piece.bounds.min.x, piece.bounds.max.y} };
        uint32_t count = 0;
        FOR_LOOP(i, 4) {
            uint32_t raw = r_sc2_ramp_sample(map, corners[i]);
            /* Fractional CLIF samples lie inside the ramp, not on its low tier plane. */
            if (r_sc2_ramp_level(map, corners[i]) != piece.level || (raw >= 64 && (raw & 63))) continue;
            uint32_t x = MIN(map->t3HeightMap->width-1, MAX(0, (int)lroundf(corners[i].x)));
            uint32_t y = MIN(map->t3HeightMap->height-1, MAX(0, (int)lroundf(corners[i].y)));
            /* The HMAP base is the tier plane; adjustment is applied per vertex in the common baker. */
            place.base_z += map->t3HeightMap->data[x+y*map->t3HeightMap->width].height * sc2_map_height_scale(map) - sc2_map_height_offset(map);
            count++;
        }
        if (!count) { fprintf(stderr, "SC2 ramp: no base-tier sample for '%s'\n", path); continue; }
        place.base_z /= count;
        vector2_t offset = { world.min.x + box->center.x*map->cell_size, world.min.y + box->center.y*map->cell_size };
        matrix4_t bones[SC2_M3_MAX_BONES];
        m3Model_t const *m3 = model->m3;
        rSc2CliffBakeBatch_t *batch = r_sc2_cliff_bake_batch(batches, r_sc2_cliff_diffuse_texture(model));
        batch->list.current_group++; r_sc2_m3_build_cliff_bones(m3, bones);
        FOR_LOOP(d, m3->divisionsNum) {
            m3Divisions_t const *div = &m3->divisions[d];
            FOR_LOOP(r, div->regionsNum)
                r_sc2_bake_cliff_region(&batch->list, map, m3, div, &div->regions[r], bones, &place, &offset, piece.rotation);
        }
    }
}

static maplayer_t *r_sc2_build_cliff_layer(sc2Map_t const *map) {
    rSc2CliffBakeBatch_t *batches = NULL, *batch;
    maplayer_t *layers = NULL;
    uint32_t cliff_width;
    uint32_t cliff_height;

    if (!map || !map->t3Terrain.num_cliff_sets || !map->t3SyncCliffLevel)
        return NULL;
    cliff_width = SC2_CLIFF_WIDTH(map);
    cliff_height = MAX(1, (SC2_MAP_HEIGHT(map) + 1) / 2);
    FOR_LOOP(cy, cliff_height) {
        FOR_LOOP(cx, cliff_width) {
            uint32_t index = cx + cy * cliff_width;
            sc2CliffCell_t cell = r_sc2_cliff_cell_for_index(map, index, cliff_width);
            sc2CliffSet_t const *set;
            char config[5];
            PATHSTR path;
            uint32_t grid_x = cx * SC2_CLIFF_BLOCK_SPAN;
            uint32_t grid_y = cy * SC2_CLIFF_BLOCK_SPAN;
            uint16_t baselevel;
            int rotation;
            model_t const *model;

            if (r_sc2_cliff_block_is_flat(map, grid_x, grid_y))
                continue;
            if (r_sc2_cliff_block_is_ramp(map, grid_x, grid_y))
                continue;
            if (cell.cliff_set >= map->t3Terrain.num_cliff_sets)
                continue;
            set = &map->t3Terrain.cliff_sets[cell.cliff_set];
            if (!r_sc2_cliff_config(map, grid_x, grid_y, config, &baselevel, NULL, &rotation))
                continue;
            if (!r_sc2_cliff_model_path(path, sizeof(path), set->mesh[0] ? set->mesh : set->name, config, cell.variant))
                continue;
            model = r_sc2_load_cliff_model(path);
            if (!model || model->modeltype != ID_43DM || !model->m3) {
                fprintf(stderr, "SC2 cliff: M3 load failed '%s'\n", path);
                continue;
            }
            batch = r_sc2_cliff_bake_batch(&batches, r_sc2_cliff_diffuse_texture(model));
            r_sc2_bake_cliff_model(&batch->list, map, model, grid_x, grid_y, rotation, baselevel);
        }
    }
    r_sc2_build_ramp_cliffs(map, &batches);
    sc2_road_cliffs = batches;
    for (batch = batches; batch; batch = batch->next) {
        maplayer_t *layer;
        if (!batch->list.num_vertices) continue;
        R_CliffWeldNormals(&batch->list, map->cell_size * 0.5f);
        layer = ri.MemAlloc(sizeof(*layer)); memset(layer, 0, sizeof(*layer));
        layer->type = MAPLAYERTYPE_CLIFF;
        layer->texture = batch->texture ? batch->texture : tr.texture[TEX_WHITE];
        layer->buffer = R_MakeVertexArrayObject(batch->list.vertices, batch->list.num_vertices);
        layer->num_vertices = batch->list.num_vertices;
        r_sc2_add_layer(&layers, layer);
    }
    return layers;
}

static void r_sc2_build_terrain(sc2Map_t const *map) {
    box2_t bounds;
    float max_z = 1.0f;
    uint32_t radius;

    r_sc2_release_terrain();
    if (!map || !SC2_MAP_WIDTH(map) || !SC2_MAP_HEIGHT(map))
        return;
    /* Four sparse taps repeated cliff edges in the camera surface; average every cell in the footprint. */
    radius = (uint32_t)ceilf(SC2_BROAD_HEIGHT_RADIUS / map->cell_size);
    R_BuildCameraHeightMap(&(cameraHeightBuild_t){ .map = &sc2_camera_height, .data = map,
        .width = map->t3HeightMap ? map->t3HeightMap->width : 0,
        .height_count = map->t3HeightMap ? map->t3HeightMap->height : 0,
        .radius = radius, .samples = radius * 2 + 1,
        .origin = map->origin, .cell_size = map->cell_size, .get_height = r_sc2_camera_grid_height });

    r_sc2_init_terrain_shader();
    r_sc2_load_terrain_textures(map);
    r_sc2_build_terrain_normal_grid(map);
    sc2_terrain_segment = ri.MemAlloc(sizeof(*sc2_terrain_segment));
    memset(sc2_terrain_segment, 0, sizeof(*sc2_terrain_segment));
    r_sc2_add_layer(&sc2_terrain_segment->layers, r_sc2_build_ground_layer(map));
    r_sc2_add_layer(&sc2_terrain_segment->layers, r_sc2_build_cliff_layer(map));
    r_sc2_build_hard_tiles(map);
    while (sc2_road_cliffs) {
        rSc2CliffBakeBatch_t *next = sc2_road_cliffs->next;
        ri.MemFree(sc2_road_cliffs->list.vertices); ri.MemFree(sc2_road_cliffs->list.groups);
        ri.MemFree(sc2_road_cliffs); sc2_road_cliffs = next;
    }

    bounds = SC2_MapBounds();
    if (map->t3HeightMap) {
        FOR_LOOP(y, SC2_MAP_HEIGHT(map) + 1) {
            FOR_LOOP(x, SC2_MAP_WIDTH(map) + 1) {
                max_z = MAX(max_z, r_sc2_ground_height_at_grid(map, x, y) + 1.0f);
            }
        }
    }
    sc2_terrain_segment->bbox = (box3_t){
        .min = { bounds.min.x, bounds.min.y, -1.0f },
        .max = { bounds.max.x, bounds.max.y, max_z },
    };
}


static texture_t *r_sc2_terrain_layer_texture(uint32_t index) {
    if (index < sc2_num_terrain_layers && sc2_terrain_textures[index])
        return sc2_terrain_textures[index];
    return sc2_terrain_textures[0];
}

static void r_sc2_set_fog_state(vector4_t *u_color, vector4_t *u_params) {
    sc2Map_t const *map = SC2_MapCurrent();
    sc2MapTerrain_t const *terrain = map ? &map->t3Terrain : NULL;
    color32_t color = terrain ? terrain->fog_color : COLOR32_BLACK;
    float enabled = terrain && terrain->fog_enabled && terrain->fog_density > 0.0f ? 1.0f : 0.0f;

    *u_color = (vector4_t){ color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f };
    *u_params = (vector4_t){ terrain ? terrain->fog_start_height : 0.0f, terrain ? terrain->fog_density : 0.0f, terrain ? terrain->fog_falloff : 0.0f, enabled };
}

static void r_sc2_set_light_state(vector3_t *ambient_out, vector3_t *dirs, vector3_t *colors) {
    sc2Map_t const *map = SC2_MapCurrent();
    sc2MapLighting_t const *lighting = map ? &map->lighting : NULL;
    vector3_t ambient = sc2_light_ambient(lighting && lighting->enabled ? lighting : NULL);

    *ambient_out = (vector3_t){ ambient.x, ambient.y, ambient.z };
    /* Adding Fill/Back as unshadowed suns lit faces opposite the visible key shadow. */
    FOR_LOOP(i, SC2_DIFFUSE_LIGHTS) {
        sc2DirectionalLight_t const *light = lighting && lighting->enabled ? &lighting->directional[i] : NULL;
        float enabled = light && light->enabled ? 1.0f : 0.0f;
        vector3_t direction = enabled ? (vector3_t){ -light->direction.x, -light->direction.y, -light->direction.z } : (vector3_t){ 0.0f, 0.0f, 1.0f };
        vector3_t color = enabled ? light->color : (vector3_t){ 0.0f, 0.0f, 0.0f };
        float multiplier = enabled ? light->color_multiplier : 0.0f;

        dirs[i] = (vector3_t){ direction.x, direction.y, direction.z };
        colors[i] = (vector3_t){ color.x * multiplier, color.y * multiplier, color.z * multiplier };
    }
}

static void r_sc2_begin_terrain_shader(matrix4_t const *model_matrix) {

    sc2_terrain_shader.state.viewProjection = tr.render_phase == RENDER_PHASE_LIGHTS ? tr.viewDef.lightMatrix : tr.viewDef.viewProjectionMatrix;
    sc2_terrain_shader.state.lightMatrix = tr.viewDef.lightMatrix;
    sc2_terrain_shader.state.model = *model_matrix;
    sc2_terrain_shader.state.textureMatrix = tr.viewDef.textureMatrix;
    r_sc2_set_fog_state(&sc2_terrain_shader.state.fogColor, &sc2_terrain_shader.state.fogParams);
    r_sc2_set_light_state(&sc2_terrain_shader.state.lightAmbient, sc2_terrain_shader.state.lightDir, sc2_terrain_shader.state.lightColor);
    /* Depth pass samples a white texture; receivers sample the light-space depth map. */
    R_Call(glActiveTexture, GL_TEXTURE0 + sc2_terrain_shader.state.shadowmap);
    R_Call(glBindTexture, GL_TEXTURE_2D, tr.render_phase == RENDER_PHASE_LIGHTS ? tr.texture[TEX_WHITE]->texid : tr.rt[RT_DEPTHMAP]->texture);
}

static void r_sc2_begin_terrain_pass(uint32_t group) {
    FOR_LOOP(i, SC2_TERRAIN_PASS_LAYERS) {
        R_BindTexture(r_sc2_terrain_layer_texture(group * SC2_TERRAIN_PASS_LAYERS + i), i);
    }
    R_BindTexture(sc2_terrain_masks[group], SC2_TERRAIN_PASS_LAYERS);
    if (group) {
        R_Call(glEnable, GL_BLEND);
        R_Call(glBlendFunc, GL_ONE, GL_ONE);
        R_Call(glDepthMask, GL_FALSE);
    } else {
        R_Call(glDisable, GL_BLEND);
        R_Call(glDepthMask, GL_TRUE);
    }
}

static void r_sc2_set_terrain_uv(void) {
    box2_t bounds = SC2_MapBounds();
    float map_w = bounds.max.x - bounds.min.x;
    float map_h = bounds.max.y - bounds.min.y;

    sc2_terrain_shader.state.worldUVScale = (vector2_t){ map_w / SC2_TERRAIN_UV_SCALE, map_h / SC2_TERRAIN_UV_SCALE };
    sc2_terrain_shader.state.worldUVOffset = (vector2_t){ bounds.min.x / SC2_TERRAIN_UV_SCALE, bounds.min.y / SC2_TERRAIN_UV_SCALE };
}

static void r_sc2_draw_terrain_indexed(maplayer_t const *layer) {
    uint32_t groups = tr.render_phase == RENDER_PHASE_LIGHTS ? 1 : SC2_TERRAIN_BLEND_GROUPS;

    r_sc2_set_terrain_uv();
    FOR_LOOP(group, groups) {
        r_sc2_begin_terrain_pass(group);
        R_ApplyShader(&sc2_terrain_shader);
        R_DrawIndexedBuffer(layer->buffer, layer->num_indices);
    }
    R_Call(glDisable, GL_BLEND);
    R_Call(glDepthMask, GL_TRUE);
}

static void r_sc2_draw_terrain_vertices(maplayer_t const *layer) {
    uint32_t groups = tr.render_phase == RENDER_PHASE_LIGHTS ? 1 : SC2_TERRAIN_BLEND_GROUPS;

    r_sc2_set_terrain_uv();
    FOR_LOOP(group, groups) {
        r_sc2_begin_terrain_pass(group);
        R_ApplyShader(&sc2_terrain_shader);
        R_DrawBuffer(layer->buffer, layer->num_vertices);
    }
    R_Call(glDisable, GL_BLEND);
    R_Call(glDepthMask, GL_TRUE);
}

static void r_sc2_draw_ground_layer(mapsegment_t const *segment) {
    maplayer_t const *layer;
    matrix4_t model_matrix;

    if (!sc2_terrain_shader_loaded || !segment)
        return;
    layer = segment->layers;
    while (layer && layer->type != MAPLAYERTYPE_GROUND) {
        layer = layer->next;
    }
    if (!layer)
        return;

    Matrix4_identity(&model_matrix);
    r_sc2_begin_terrain_shader(&model_matrix);
    r_sc2_draw_terrain_indexed(layer);
}

static void r_sc2_load_minimap(cstring_t mapFileName) {
    static cstring_t const candidates[] = { "Minimap.tga", "Minimap.dds", NULL };
    PATHSTR path;
    SAFE_DELETE(tr.minimap, R_ReleaseTexture);
    for (int i = 0; candidates[i]; i++) {
        snprintf(path, sizeof(path), "%s\\%s", mapFileName, candidates[i]);
        tr.minimap = R_LoadTexture(path);
        if (tr.minimap) return;
    }
}

/* World shaders survive map changes but must be released with their renderer context. */
void R_SC2ShutdownShaders(void) {
    /* Only shader ownership ends here: the renderer has already reclaimed registered models. */
    R_DeleteShader(&sc2_terrain_shader.prog);
    R_DeleteShader(&sc2_cliff_shader.prog);
    sc2_terrain_shader_loaded = sc2_cliff_shader_loaded = false;
}

void R_SC2RegisterMap(cstring_t mapFileName) {
    SC2_MapSetHost(&(sc2MapHost_t){
        .read_file = r_sc2_read_file,
        .free_file = r_sc2_free_file,
        .mem_alloc = ri.MemAlloc,
        .mem_free = ri.MemFree,
        .cvar_string = ri.CvarString,
    });
    SC2_MapLoad(mapFileName);
    r_sc2_build_terrain(SC2_MapCurrent());
    r_sc2_load_minimap(mapFileName);
}

static void r_sc2_draw_cliff_layer(mapsegment_t const *segment) {
    maplayer_t const *layer;
    matrix4_t model_matrix;

    if (!sc2_terrain_shader_loaded || !sc2_cliff_shader_loaded || !segment)
        return;
    Matrix4_identity(&model_matrix);

    /* Pass 1: terrain blend - fills depth and lays down terrain color seamlessly.
       Use world XY position for terrain UV so tiling matches the ground exactly. */
    r_sc2_begin_terrain_shader(&model_matrix);
    for (layer = segment->layers; layer; layer = layer->next)
        if (layer->type == MAPLAYERTYPE_CLIFF) r_sc2_draw_terrain_vertices(layer);
    /* The terrain pass already wrote cliff depth; the material overlay belongs only in the color pass. */
    if (tr.render_phase == RENDER_PHASE_LIGHTS)
        return;

    /* Pass 2: cliff M3 texture alpha-blended on top, pulled slightly forward */
    R_Call(glEnable, GL_POLYGON_OFFSET_FILL);
    R_Call(glPolygonOffset, -1.0f, -1.0f);

    sc2_cliff_shader.state.viewProjection = tr.render_phase == RENDER_PHASE_LIGHTS ? tr.viewDef.lightMatrix : tr.viewDef.viewProjectionMatrix;
    sc2_cliff_shader.state.lightMatrix = tr.viewDef.lightMatrix;
    sc2_cliff_shader.state.model = model_matrix;
    r_sc2_set_fog_state(&sc2_cliff_shader.state.fogColor, &sc2_cliff_shader.state.fogParams);
    r_sc2_set_light_state(&sc2_cliff_shader.state.lightAmbient, sc2_cliff_shader.state.lightDir, sc2_cliff_shader.state.lightColor);
    R_Call(glActiveTexture, GL_TEXTURE0 + sc2_cliff_shader.state.shadowmap);
    R_Call(glBindTexture, GL_TEXTURE_2D, tr.render_phase == RENDER_PHASE_LIGHTS ? tr.texture[TEX_WHITE]->texid : tr.rt[RT_DEPTHMAP]->texture);
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (layer = segment->layers; layer; layer = layer->next) {
        if (layer->type != MAPLAYERTYPE_CLIFF) continue;
        R_BindTexture(layer->texture, 0);
        R_ApplyShader(&sc2_cliff_shader);
        R_DrawBuffer(layer->buffer, layer->num_vertices);
    }
    R_Call(glDisable, GL_POLYGON_OFFSET_FILL);
    R_Call(glPolygonOffset, 0.0f, 0.0f);
}

void R_SC2DrawWorld(void) {
    matrix4_t model_matrix;

    if (!sc2_terrain_segment || (tr.viewDef.rdflags & RDF_NOWORLDMODEL))
        return;

    if (tr.render_phase == RENDER_PHASE_LIGHTS) {
        sc2Map_t const *map = SC2_MapCurrent();
        sc2shadowview_t input = {
            .camera = tr.viewDef.viewProjectionMatrix,
            .target = Vector3_lerp(&tr.viewDef.camerastate[1].origin, &tr.viewDef.camerastate[0].origin, tr.viewDef.lerpfrac),
            .light = map->lighting.directional[SC2_LIGHT_KEY].direction,
            .reach = LerpNumber(tr.viewDef.camerastate[1].distance, tr.viewDef.camerastate[0].distance, tr.viewDef.lerpfrac),
        };
        input.target.z = R_SC2GetHeightAtPoint(input.target.x, input.target.y);
        if (!sc2_shadow_matrix(&input, &tr.viewDef.lightMatrix))
            ri.error("SC2 shadow: invalid camera or missing authored key light for %s", map->t3Terrain.tile_set);
    }
    Matrix4_identity(&model_matrix);

    tr.shader_ui.state.viewProjection = tr.viewDef.viewProjectionMatrix;
    tr.shader_ui.state.model = model_matrix;
    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_TRUE);
    R_Call(glDepthFunc, GL_LEQUAL);
    R_Call(glColorMask, GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    r_sc2_draw_ground_layer(sc2_terrain_segment);
    r_sc2_draw_cliff_layer(sc2_terrain_segment);
    r_sc2_draw_road_layers(sc2_hard_tile_layers, &sc2_hard_tile_entity);
}

static bool r_sc2_clip_trace_to_bounds(line3_t const *line, box2_t const *bounds, float *t0, float *t1) {
    float const bounds_min[2] = { bounds->min.x, bounds->min.y };
    float const bounds_max[2] = { bounds->max.x, bounds->max.y };
    float const start[2] = { line->a.x, line->a.y };
    float const finish[2] = { line->b.x, line->b.y };

    *t0 = 0.0f;
    *t1 = 1.0f;
    FOR_LOOP(axis, 2) {
        float const dir = finish[axis] - start[axis];
        float near_t;
        float far_t;

        if (fabsf(dir) < SC2_TRACE_EPSILON) {
            if (start[axis] < bounds_min[axis] || start[axis] > bounds_max[axis])
                return false;
            continue;
        }

        near_t = (bounds_min[axis] - start[axis]) / dir;
        far_t = (bounds_max[axis] - start[axis]) / dir;
        if (near_t > far_t) {
            float const swap = near_t;
            near_t = far_t;
            far_t = swap;
        }
        *t0 = MAX(*t0, near_t);
        *t1 = MIN(*t1, far_t);
        if (*t0 > *t1)
            return false;
    }
    return true;
}

static bool r_sc2_trace_heightmap_tile(sc2Map_t const *map, uint32_t x, uint32_t y, line3_t const *line, vector3_t *output) {
    box2_t bounds = SC2_MapBounds();
    float x0 = bounds.min.x + x * map->cell_size;
    float y0 = bounds.min.y + y * map->cell_size;
    float x1 = x0 + map->cell_size;
    float y1 = y0 + map->cell_size;
    triangle3_t const tri1 = {
        { x0, y0, r_sc2_ground_height_at_grid(map, x, y) },
        { x1, y0, r_sc2_ground_height_at_grid(map, x + 1, y) },
        { x1, y1, r_sc2_ground_height_at_grid(map, x + 1, y + 1) },
    };
    triangle3_t const tri2 = {
        { x1, y1, r_sc2_ground_height_at_grid(map, x + 1, y + 1) },
        { x0, y1, r_sc2_ground_height_at_grid(map, x, y + 1) },
        { x0, y0, r_sc2_ground_height_at_grid(map, x, y) },
    };

    if (Line3_intersect_triangle(line, &tri1, output))
        return true;
    return Line3_intersect_triangle(line, &tri2, output);
}

bool R_SC2TraceLocation(viewDef_t const *viewdef, float x, float y, vector3_t *output) {
    sc2Map_t const *map = SC2_MapCurrent();
    box2_t bounds;
    line3_t line;
    float t0, t1;
    float dir_x, dir_y;
    int tile_x, tile_y;
    int step_x, step_y;
    float t_max_x, t_max_y;
    float t_delta_x, t_delta_y;

    if (!viewdef || !output || !map || !SC2_MAP_WIDTH(map) || !SC2_MAP_HEIGHT(map))
        return false;
    line = R_LineForScreenPoint(viewdef, x, y);
    bounds = SC2_MapBounds();
    if (!r_sc2_clip_trace_to_bounds(&line, &bounds, &t0, &t1))
        return false;

    dir_x = line.b.x - line.a.x;
    dir_y = line.b.y - line.a.y;
    tile_x = (int)floorf((line.a.x + dir_x * t0 - bounds.min.x) / map->cell_size);
    tile_y = (int)floorf((line.a.y + dir_y * t0 - bounds.min.y) / map->cell_size);
    tile_x = MAX(0, MIN((int)SC2_MAP_WIDTH(map) - 1, tile_x));
    tile_y = MAX(0, MIN((int)SC2_MAP_HEIGHT(map) - 1, tile_y));

    if (fabsf(dir_x) < SC2_TRACE_EPSILON) {
        step_x = 0;
        t_max_x = SC2_TRACE_INF;
        t_delta_x = SC2_TRACE_INF;
    } else {
        step_x = dir_x > 0.0f ? 1 : -1;
        t_max_x = (bounds.min.x + (tile_x + (step_x > 0 ? 1.0f : 0.0f)) * map->cell_size - line.a.x) / dir_x;
        t_delta_x = fabsf(map->cell_size / dir_x);
    }

    if (fabsf(dir_y) < SC2_TRACE_EPSILON) {
        step_y = 0;
        t_max_y = SC2_TRACE_INF;
        t_delta_y = SC2_TRACE_INF;
    } else {
        step_y = dir_y > 0.0f ? 1 : -1;
        t_max_y = (bounds.min.y + (tile_y + (step_y > 0 ? 1.0f : 0.0f)) * map->cell_size - line.a.y) / dir_y;
        t_delta_y = fabsf(map->cell_size / dir_y);
    }

    while (tile_x >= 0 && tile_y >= 0 && tile_x < (int)SC2_MAP_WIDTH(map) && tile_y < (int)SC2_MAP_HEIGHT(map)) {
        if (r_sc2_trace_heightmap_tile(map, (uint32_t)tile_x, (uint32_t)tile_y, &line, output))
            return true;
        if (t_max_x < t_max_y) {
            if (t_max_x > t1)
                break;
            tile_x += step_x;
            t_max_x += t_delta_x;
        } else {
            if (t_max_y > t1)
                break;
            tile_y += step_y;
            t_max_y += t_delta_y;
        }
    }
    return false;
}

float R_SC2GetHeightAtPoint(float x, float y) {
    return sc2_map_height_at_point(SC2_MapCurrent(), x, y);
}

float R_SC2GetCameraHeightAtPoint(float x, float y) { return R_SampleCameraHeightMap(&sc2_camera_height, x, y); }

vector2_t R_SC2WorldSize(void) {
    sc2Map_t const *map = SC2_MapCurrent();
    if (!map) return (vector2_t){ 0.0f, 0.0f };
    return (vector2_t){ SC2_MAP_WIDTH(map) * map->cell_size, SC2_MAP_HEIGHT(map) * map->cell_size };
}
