#include "r_sc2map.h"

#include "games/starcraft-2/common/sc2_map.c"
#include "games/starcraft-2/renderer/m3/r_m3.h"
#define SC2_REUSE_WAR3_CLIFF_BAKER
#include "games/warcraft-3/renderer/w3m/r_war3map_cliffs.c"
#undef SC2_REUSE_WAR3_CLIFF_BAKER
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

/* extern LPCSTR vs_default; SC2 terrain uses its own vertex shader for blend UVs. */
extern LPCSTR fs_ui;
extern m3SequenceTimeline_t const *M3_FindAnimationAtTime(m3Model_t const *model, DWORD time, DWORD *localtime);
extern VECTOR3 M3_GetVector3AnimValue(m3Model_t const *model,
                                      m3SequenceTimeline_t const *timeline,
                                      m3Vector3AnimRef_t const *animref,
                                      DWORD time);
extern VECTOR4 M3_GetVector4AnimValue(m3Model_t const *model,
                                      m3SequenceTimeline_t const *timeline,
                                      m3Vector4AnimRef_t const *animref,
                                      DWORD time);

static LPMAPSEGMENT sc2_terrain_segment;
static LPSHADER sc2_terrain_shader;
static LPSHADER sc2_cliff_shader;
static GLint sc2_u_layer[SC2_TERRAIN_PASS_LAYERS];
static GLint sc2_u_mask = -1;
static GLint sc2_u_world_uv_offset = -1;
static GLint sc2_u_world_uv_scale = -1;
static GLint sc2_u_fog_color = -1;
static GLint sc2_u_fog_params = -1;
static GLint sc2_u_cliff_fog_color = -1;
static GLint sc2_u_cliff_fog_params = -1;
static LPTEXTURE sc2_terrain_textures[SC2_TERRAIN_BLEND_LAYERS];
static LPTEXTURE sc2_terrain_masks[SC2_TERRAIN_BLEND_GROUPS];
static DWORD sc2_num_terrain_layers;

typedef struct sc2CliffModel_s {
    PATHSTR path;
    LPCMODEL model;
    struct sc2CliffModel_s *next;
} sc2CliffModel_t;

typedef struct rSc2CliffPlacement_s {
    FLOAT base_z;
    FLOAT model_z_offset;
    FLOAT z_scale;
} rSc2CliffPlacement_t;

typedef struct rSc2LightUniforms_s {
    GLint ambient;
    GLint direction[SC2_MAX_DIRECTIONAL_LIGHTS];
    GLint color[SC2_MAX_DIRECTIONAL_LIGHTS];
} rSc2LightUniforms_t;

static sc2CliffModel_t *sc2_cliff_models;
static rSc2LightUniforms_t sc2_u_terrain_light;
static rSc2LightUniforms_t sc2_u_cliff_light;

static void r_sc2_release_cliff_models(void);
static sc2CliffCell_t const *r_sc2_find_cliff_cell(sc2Map_t const *map, DWORD index);

static LPCSTR sc2_vs_terrain =
"#version 140\n"
"in vec3 i_position;\n"
"in vec3 i_normal;\n"
"in vec4 i_color;\n"
"out vec2 v_texcoord2;\n"
"out vec3 v_worldpos;\n"
"out vec3 v_light;\n"
"out vec4 v_color;\n"
"uniform mat4 uViewProjectionMatrix;\n"
"uniform mat4 uTextureMatrix;\n"
"uniform mat4 uModelMatrix;\n"
"uniform vec3 uLightAmbient;\n"
"uniform vec3 uLightDir[3];\n"
"uniform vec3 uLightColor[3];\n"
"vec3 vertex_lighting(vec3 normal) {\n"
"    vec3 n = normalize(normal);\n"
"    vec3 light = uLightAmbient;\n"
"    for (int i = 0; i < 3; i++) {\n"
"        vec3 l = normalize(uLightDir[i]);\n"
"        light += uLightColor[i] * max(dot(n, l), 0.0);\n"
"    }\n"
"    return max(light, vec3(0.0));\n"
"}\n"
"void main() {\n"
"    vec4 pos = uModelMatrix * vec4(i_position, 1.0);\n"
"    v_texcoord2 = (uTextureMatrix * pos).xy;\n"
"    v_worldpos = pos.xyz;\n"
"    v_light = vertex_lighting(mat3(uModelMatrix) * i_normal);\n"
"    v_color = i_color;\n"
"    gl_Position = uViewProjectionMatrix * pos;\n"
"}\n";

static LPCSTR sc2_vs_cliff_texture =
"#version 140\n"
"in vec3 i_position;\n"
"in vec2 i_texcoord;\n"
"in vec3 i_normal;\n"
"in vec4 i_color;\n"
"out vec2 v_texcoord;\n"
"out vec3 v_worldpos;\n"
"out vec3 v_light;\n"
"out vec4 v_color;\n"
"uniform mat4 uViewProjectionMatrix;\n"
"uniform mat4 uModelMatrix;\n"
"uniform vec3 uLightAmbient;\n"
"uniform vec3 uLightDir[3];\n"
"uniform vec3 uLightColor[3];\n"
"vec3 vertex_lighting(vec3 normal) {\n"
"    vec3 n = normalize(normal);\n"
"    vec3 light = uLightAmbient;\n"
"    for (int i = 0; i < 3; i++) {\n"
"        vec3 l = normalize(uLightDir[i]);\n"
"        light += uLightColor[i] * max(dot(n, l), 0.0);\n"
"    }\n"
"    return max(light, vec3(0.0));\n"
"}\n"
"void main() {\n"
"    vec4 pos = uModelMatrix * vec4(i_position, 1.0);\n"
"    v_texcoord = i_texcoord;\n"
"    v_worldpos = pos.xyz;\n"
"    v_light = vertex_lighting(mat3(uModelMatrix) * i_normal);\n"
"    v_color = i_color;\n"
"    gl_Position = uViewProjectionMatrix * pos;\n"
"}\n";

static LPCSTR sc2_fs_terrain =
"#version 140\n"
"in vec2 v_texcoord2;\n"
"in vec3 v_worldpos;\n"
"in vec3 v_light;\n"
"in vec4 v_color;\n"
"out vec4 o_color;\n"
"uniform sampler2D uLayer0;\n"
"uniform sampler2D uLayer1;\n"
"uniform sampler2D uLayer2;\n"
"uniform sampler2D uLayer3;\n"
"uniform sampler2D uMask;\n"
"uniform vec2 uWorldUVOffset;\n"
"uniform vec2 uWorldUVScale;\n"
"uniform vec4 uFogColor;\n"
"uniform vec4 uFogParams;\n"
"vec2 get_mask_coord() {\n"
"    return clamp(v_texcoord2 * 0.5 + vec2(0.5), vec2(0.0), vec2(1.0));\n"
"}\n"
"vec2 get_terrain_coord() {\n"
"    return (v_texcoord2 * 0.5 + vec2(0.5)) * uWorldUVScale + uWorldUVOffset;\n"
"}\n"
"float get_height_fog() {\n"
"    if (uFogParams.w <= 0.0) return 0.0;\n"
"    float above = max(v_worldpos.z - uFogParams.x, 0.0);\n"
"    float vertical = v_worldpos.z <= uFogParams.x ? 1.0 : exp(-above * max(uFogParams.z, 0.0001));\n"
"    return clamp((uFogParams.y / max(uFogParams.z, 0.0001)) * vertical, 0.0, 1.0) * uFogColor.a;\n"
"}\n"
"void main() {\n"
"    vec2 mc = get_mask_coord();\n"
"    vec2 tc = get_terrain_coord();\n"
"    vec4 w = texture(uMask, mc);\n"
"    vec4 color = texture(uLayer0, tc) * w.r +\n"
"                 texture(uLayer1, tc) * w.g +\n"
"                 texture(uLayer2, tc) * w.b +\n"
"                 texture(uLayer3, tc) * w.a;\n"
"    color.rgb *= v_color.rgb;\n"
"    color.rgb *= v_light;\n"
"    color.rgb = mix(color.rgb, uFogColor.rgb, get_height_fog());\n"
"    color.a = 1.0;\n"
"    o_color = color;\n"
"}\n";

static LPCSTR sc2_fs_cliff =
"#version 140\n"
"in vec4 v_color;\n"
"in vec2 v_texcoord;\n"
"in vec3 v_worldpos;\n"
"in vec3 v_light;\n"
"out vec4 o_color;\n"
"uniform sampler2D uTexture;\n"
"uniform vec4 uFogColor;\n"
"uniform vec4 uFogParams;\n"
"float get_height_fog() {\n"
"    if (uFogParams.w <= 0.0) return 0.0;\n"
"    float above = max(v_worldpos.z - uFogParams.x, 0.0);\n"
"    float vertical = v_worldpos.z <= uFogParams.x ? 1.0 : exp(-above * max(uFogParams.z, 0.0001));\n"
"    return clamp((uFogParams.y / max(uFogParams.z, 0.0001)) * vertical, 0.0, 1.0) * uFogColor.a;\n"
"}\n"
"void main() {\n"
"    vec4 color = texture(uTexture, v_texcoord) * v_color;\n"
"    color.rgb *= v_light;\n"
"    color.rgb = mix(color.rgb, uFogColor.rgb, get_height_fog());\n"
"    o_color = color;\n"
"}\n";

static void r_sc2_init_light_uniforms(GLuint program, rSc2LightUniforms_t *uniforms) {
    if (!uniforms)
        return;
    uniforms->ambient = glGetUniformLocation(program, "uLightAmbient");
    FOR_LOOP(i, SC2_MAX_DIRECTIONAL_LIGHTS) {
        char name[32];

        snprintf(name, sizeof(name), "uLightDir[%u]", (unsigned)i);
        uniforms->direction[i] = glGetUniformLocation(program, name);
        snprintf(name, sizeof(name), "uLightColor[%u]", (unsigned)i);
        uniforms->color[i] = glGetUniformLocation(program, name);
    }
}

static void r_sc2_init_cliff_shader(void) {
    if (sc2_cliff_shader) {
        return;
    }
    sc2_cliff_shader = R_InitShader(sc2_vs_cliff_texture, sc2_fs_cliff);
    if (!sc2_cliff_shader) {
        return;
    }
    R_Call(glUseProgram, sc2_cliff_shader->progid);
    R_Call(glUniform1i, sc2_cliff_shader->uTexture, 0);
    sc2_u_cliff_fog_color = glGetUniformLocation(sc2_cliff_shader->progid, "uFogColor");
    sc2_u_cliff_fog_params = glGetUniformLocation(sc2_cliff_shader->progid, "uFogParams");
    r_sc2_init_light_uniforms(sc2_cliff_shader->progid, &sc2_u_cliff_light);
}

static void r_sc2_init_terrain_shader(void) {
    static LPCSTR layer_names[SC2_TERRAIN_PASS_LAYERS] = {
        "uLayer0", "uLayer1", "uLayer2", "uLayer3",
    };

    if (sc2_terrain_shader) {
        r_sc2_init_cliff_shader();
        return;
    }
    sc2_terrain_shader = R_InitShader(sc2_vs_terrain, sc2_fs_terrain);
    if (!sc2_terrain_shader) {
        return;
    }
    R_Call(glUseProgram, sc2_terrain_shader->progid);
    FOR_LOOP(i, SC2_TERRAIN_PASS_LAYERS) {
        sc2_u_layer[i] = glGetUniformLocation(sc2_terrain_shader->progid, layer_names[i]);
        R_Call(glUniform1i, sc2_u_layer[i], (GLint)i);
    }
    sc2_u_mask             = glGetUniformLocation(sc2_terrain_shader->progid, "uMask");
    sc2_u_world_uv_offset  = glGetUniformLocation(sc2_terrain_shader->progid, "uWorldUVOffset");
    sc2_u_world_uv_scale   = glGetUniformLocation(sc2_terrain_shader->progid, "uWorldUVScale");
    sc2_u_fog_color        = glGetUniformLocation(sc2_terrain_shader->progid, "uFogColor");
    sc2_u_fog_params       = glGetUniformLocation(sc2_terrain_shader->progid, "uFogParams");
    r_sc2_init_light_uniforms(sc2_terrain_shader->progid, &sc2_u_terrain_light);
    R_Call(glUniform1i, sc2_u_mask, SC2_TERRAIN_PASS_LAYERS);
    r_sc2_init_cliff_shader();
}

static HANDLE r_sc2_read_file(LPCSTR filename, LPDWORD size) {
    void *buffer = NULL;
    int result = ri.FS_ReadFile(filename, &buffer);
    if (result < 0) {
        if (size) *size = 0;
        return NULL;
    }
    if (size) *size = (DWORD)result;
    return buffer;
}

static void r_sc2_free_file(HANDLE file) {
    ri.FS_FreeFile(file);
}

static void r_sc2_push_vertex_normal(VERTEX *v,
                                     FLOAT x,
                                     FLOAT y,
                                     FLOAT z,
                                     FLOAT u,
                                     FLOAT t,
                                     BYTE alpha,
                                     VECTOR3 normal) {
    v->position = (VECTOR3){ x, y, z };
    v->texcoord = (VECTOR2){ u, t };
    v->normal = normal;
    v->color = (COLOR32){ 255, 255, 255, alpha };
}

static USHORT r_sc2_cliff_level_at_grid(sc2Map_t const *map, DWORD x, DWORD y) {
    USHORT value;

    if (!map->t3SyncCliffLevel || !map->t3SyncCliffLevel->width || !map->t3SyncCliffLevel->height) {
        return 0;
    }
    x = MIN(map->t3SyncCliffLevel->width - 1, x);
    y = MIN(map->t3SyncCliffLevel->height - 1, y);
    value = map->t3SyncCliffLevel->data[x + y * map->t3SyncCliffLevel->width];
    return value >= SC2_CLIFF_LEVEL_PACKED_MIN ? value >> SC2_CLIFF_LEVEL_SHIFT : value;
}

static BOOL r_sc2_cliff_block_is_flat(sc2Map_t const *map, DWORD x, DWORD y) {
    USHORT level[4];

    SC2_CLIFF_BLOCK_LEVELS(level, map, x, y);
    return level[1] == level[0] && level[2] == level[0] && level[3] == level[0];
}

static DWORD r_sc2_cliff_index_at_grid(sc2Map_t const *map, DWORD x, DWORD y) {
    DWORD cliff_width = SC2_CLIFF_WIDTH(map);

    return (x / SC2_CLIFF_BLOCK_SPAN) + (y / SC2_CLIFF_BLOCK_SPAN) * cliff_width;
}

static BOOL r_sc2_cliff_block_is_ramp(sc2Map_t const *map, DWORD x, DWORD y) {
    sc2CliffCell_t const *cell;

    if (!map)
        return false;
    cell = r_sc2_find_cliff_cell(map, r_sc2_cliff_index_at_grid(map, x, y));
    return cell && (cell->flags & SC2_CLIFF_CELL_RAMP);
}

static BOOL r_sc2_skip_ground_cell(sc2Map_t const *map, DWORD x, DWORD y) {
    DWORD block_x = SC2_CLIFF_BLOCK_ORIGIN(x);
    DWORD block_y = SC2_CLIFF_BLOCK_ORIGIN(y);

    if (r_sc2_cliff_block_is_ramp(map, block_x, block_y))
        return false;
    return !r_sc2_cliff_block_is_flat(map, block_x, block_y);
}

#ifdef BZ_SC2_FIX_FLAT_TERRAIN_TIER_MISMATCH
/* HACK: fixes flat SC2 ground tiles whose shared HMAP vertex carries lower cliff-side
   height/extra data while the drawn flat tile and adjacent cliff edge are one tier higher. */
static BOOL r_sc2_ground_cell_tier(sc2Map_t const *map, DWORD x, DWORD y, USHORT *tier) {
    DWORD block_x;
    DWORD block_y;
    USHORT level[4];

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

static BOOL r_sc2_ground_cell_base_height(sc2Map_t const *map, DWORD tile_x, DWORD tile_y, USHORT tier, USHORT *height) {
    DWORD const vx[4] = { tile_x, tile_x + 1, tile_x + 1, tile_x };
    DWORD const vy[4] = { tile_y, tile_y, tile_y + 1, tile_y + 1 };
    DWORD sum = 0;
    DWORD count = 0;

    FOR_LOOP(i, 4) {
        DWORD x = MIN(map->t3HeightMap->width - 1, vx[i]);
        DWORD y = MIN(map->t3HeightMap->height - 1, vy[i]);
        sc2MapHeightSample_t const *sample = &map->t3HeightMap->data[x + y * map->t3HeightMap->width];

        if (sample->extra != tier)
            continue;
        sum += sample->height;
        count++;
    }
    if (!count)
        return false;
    *height = (USHORT)(sum / count);
    return true;
}

static FLOAT r_sc2_ground_height_at_grid(sc2Map_t const *map, DWORD grid_x, DWORD grid_y) {
    int tile_x[4] = { (int)grid_x - 1, (int)grid_x,     (int)grid_x - 1, (int)grid_x };
    int tile_y[4] = { (int)grid_y - 1, (int)grid_y - 1, (int)grid_y,     (int)grid_y };
    sc2MapHeightSample_t const *sample;
    DWORD base_sum = 0;
    DWORD base_count = 0;

    if (!map || !map->t3HeightMap || !map->t3HeightMap->width || !map->t3HeightMap->height)
        return 0.0f;
    grid_x = MIN(map->t3HeightMap->width - 1, grid_x);
    grid_y = MIN(map->t3HeightMap->height - 1, grid_y);
    sample = &map->t3HeightMap->data[grid_x + grid_y * map->t3HeightMap->width];
    FOR_LOOP(i, 4) {
        USHORT tier;
        USHORT height;

        if (tile_x[i] < 0 || tile_y[i] < 0)
            continue;
        if (!r_sc2_ground_cell_tier(map, (DWORD)tile_x[i], (DWORD)tile_y[i], &tier))
            continue;
        if (sample->extra == tier)
            return sc2_map_height_at_grid(map, grid_x, grid_y);
        if (!r_sc2_ground_cell_base_height(map, (DWORD)tile_x[i], (DWORD)tile_y[i], tier, &height))
            continue;
        base_sum += height;
        base_count++;
    }
    if (!base_count)
        return sc2_map_height_at_grid(map, grid_x, grid_y);
    return ((FLOAT)(base_sum / base_count) + (FLOAT)sample->adjustment) *
           sc2_map_height_scale(map) - sc2_map_height_offset(map);
}
#else
static FLOAT r_sc2_ground_height_at_grid(sc2Map_t const *map, DWORD grid_x, DWORD grid_y) {
    return sc2_map_height_at_grid(map, grid_x, grid_y);
}
#endif

static VECTOR3 r_sc2_ground_face_normal(LPCVECTOR3 a, LPCVECTOR3 b, LPCVECTOR3 c) {
    VECTOR3 ab = Vector3_sub(b, a);
    VECTOR3 ac = Vector3_sub(c, a);
    VECTOR3 normal = Vector3_cross(&ab, &ac);

    if (Vector3_lengthsq(&normal) <= 0.000001f)
        return (VECTOR3){ 0.0f, 0.0f, 1.0f };
    Vector3_normalize(&normal);
    if (normal.z < 0.0f)
        normal = Vector3_scale(&normal, -1.0f);
    return normal;
}

static void r_sc2_accumulate_ground_triangle_normal(VERTEX *vertices,
                                                    DWORD i0,
                                                    DWORD i1,
                                                    DWORD i2) {
    VECTOR3 normal = r_sc2_ground_face_normal(&vertices[i0].position,
                                              &vertices[i1].position,
                                              &vertices[i2].position);

    vertices[i0].normal = Vector3_add(&vertices[i0].normal, &normal);
    vertices[i1].normal = Vector3_add(&vertices[i1].normal, &normal);
    vertices[i2].normal = Vector3_add(&vertices[i2].normal, &normal);
}

static void r_sc2_build_ground_vertex_normals(sc2Map_t const *map,
                                              VERTEX *vertices,
                                              DWORD w,
                                              DWORD h) {
    DWORD num_vertices = (w + 1) * (h + 1);

    FOR_LOOP(i, num_vertices) {
        vertices[i].normal = (VECTOR3){ 0.0f, 0.0f, 0.0f };
    }
    FOR_LOOP(y, h) {
        FOR_LOOP(x, w) {
            DWORD i00 = x + y * (w + 1);
            DWORD i10 = i00 + 1;
            DWORD i01 = i00 + w + 1;
            DWORD i11 = i01 + 1;

            if (r_sc2_skip_ground_cell(map, x, y))
                continue;
            r_sc2_accumulate_ground_triangle_normal(vertices, i00, i10, i11);
            r_sc2_accumulate_ground_triangle_normal(vertices, i00, i11, i01);
        }
    }
    FOR_LOOP(i, num_vertices) {
        if (Vector3_lengthsq(&vertices[i].normal) <= 0.000001f) {
            vertices[i].normal = (VECTOR3){ 0.0f, 0.0f, 1.0f };
            continue;
        }
        Vector3_normalize(&vertices[i].normal);
    }
}

static void r_sc2_release_layer(LPMAPLAYER layer) {
    while (layer) {
        LPMAPLAYER next = layer->next;
        R_ReleaseVertexArrayObject((LPBUFFER)layer->buffer);
        R_ReleaseTexture((LPTEXTURE)layer->texture);
        ri.MemFree(layer);
        layer = next;
    }
}

static void r_sc2_release_terrain(void) {
    while (sc2_terrain_segment) {
        LPMAPSEGMENT next = sc2_terrain_segment->next;
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
    sc2_num_terrain_layers = 0;
}

static void r_sc2_release_cliff_models(void) {
    while (sc2_cliff_models) {
        sc2CliffModel_t *next = sc2_cliff_models->next;
        R_ReleaseModel((LPMODEL)sc2_cliff_models->model);
        ri.MemFree(sc2_cliff_models);
        sc2_cliff_models = next;
    }
}

static void r_sc2_add_layer(LPMAPLAYER *list, LPMAPLAYER layer) {
    LPMAPLAYER *tail = list;

    if (!layer) {
        return;
    }
    while (*tail) {
        tail = &(*tail)->next;
    }
    *tail = layer;
}

static BYTE r_sc2_texture_mask_nibble(BYTE byte, DWORD pixel) {
    return pixel & 1 ? byte & 0x0F : byte >> 4;
}

static DWORD r_sc2_texture_mask_layer_stride(sc2Map_t const *map) {
    DWORD w, h, blocks_x, blocks_y, packed_layer_size, block_layer_size;

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

static DWORD r_sc2_texture_mask_layers(sc2Map_t const *map) {
    DWORD stride = r_sc2_texture_mask_layer_stride(map);

    if (!map || !map->t3TextureMasks || !stride || map->t3TextureMasksSize < sizeof(*map->t3TextureMasks))
        return 0;
    return (map->t3TextureMasksSize - sizeof(*map->t3TextureMasks)) / stride;
}

static void r_sc2_decode_texture_mask_block(LPBYTE out, LPBYTE src, DWORD width, DWORD height, DWORD blocks_x, DWORD block) {
    DWORD bx = block % blocks_x;
    DWORD by = block / blocks_x;

    FOR_LOOP(y, 64) {
        FOR_LOOP(x, 64) {
            DWORD px = bx * 64 + x;
            DWORD py = by * 64 + y;
            if (px >= width || py >= height)
                continue;
            out[px + py * width] = r_sc2_texture_mask_nibble(src[y * 32 + x / 2], x);
        }
    }
}

static void r_sc2_decode_texture_mask_layer(sc2Map_t const *map, DWORD layer, LPBYTE values) {
    DWORD w, h, stride, block_stride, blocks_x, blocks_y;
    LPBYTE src;

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

static void r_sc2_store_mask_pixel(LPCOLOR32 pixel, BYTE r, BYTE g, BYTE b, BYTE a) {
#if __linux__
    *pixel = (COLOR32){ r, g, b, a };
#else
    *pixel = (COLOR32){ b, g, r, a };
#endif
}

static LPTEXTURE r_sc2_build_mask_texture(sc2Map_t const *map, DWORD group) {
    DWORD w = 1;
    DWORD h = 1;
    BYTE *values;
    COLOR32 *pixels;
    LPTEXTURE texture;

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
        DWORD total = 0;
        BYTE mask[SC2_TERRAIN_BLEND_LAYERS];
        FOR_LOOP(layer, SC2_TERRAIN_BLEND_LAYERS) {
            mask[layer] = (BYTE)(values[layer * w * h + i] * 17);
            if (layer < sc2_num_terrain_layers) {
                total += mask[layer];
            }
        }
        if (!total) {
            memset(mask, 0, sizeof(mask));
            mask[0] = 255;
            total = 255;
        }
        r_sc2_store_mask_pixel(&pixels[i],
                               (BYTE)((mask[group * SC2_TERRAIN_PASS_LAYERS + 0] * 255u + total / 2) / total),
                               (BYTE)((mask[group * SC2_TERRAIN_PASS_LAYERS + 1] * 255u + total / 2) / total),
                               (BYTE)((mask[group * SC2_TERRAIN_PASS_LAYERS + 2] * 255u + total / 2) / total),
                               (BYTE)((mask[group * SC2_TERRAIN_PASS_LAYERS + 3] * 255u + total / 2) / total));
    }
    texture = R_AllocateTexture(w, h);
    R_LoadTextureMipLevel(texture, 0, pixels, w, h);
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

static LPMAPLAYER r_sc2_build_ground_layer(sc2Map_t const *map) {
    BOX2 bounds;
    DWORD w, h, num_vertices, num_indices;
    VERTEX *vertices;
    DWORD *indices;
    DWORD *out;
    LPMAPLAYER map_layer;

    if (!map || !SC2_MAP_WIDTH(map) || !SC2_MAP_HEIGHT(map))
        return NULL;

    w = SC2_MAP_WIDTH(map);
    h = SC2_MAP_HEIGHT(map);
    num_vertices = (w + 1) * (h + 1);
    num_indices = w * h * 6;
    vertices = ri.MemAlloc(num_vertices * sizeof(*vertices));
    indices = ri.MemAlloc(num_indices * sizeof(*indices));
    bounds = SC2_MapBounds();

    for (DWORD y = 0; y <= h; y++) {
        for (DWORD x = 0; x <= w; x++) {
            FLOAT px = bounds.min.x + x * map->cell_size;
            FLOAT py = bounds.min.y + y * map->cell_size;
            FLOAT u = px / SC2_TERRAIN_UV_SCALE;
            FLOAT v = py / SC2_TERRAIN_UV_SCALE;
            /* SC2 lighting needs mesh normals rebuilt after tier height correction. */
//            r_sc2_push_vertex(&vertices[x + y * (w + 1)], px, py, r_sc2_ground_height_at_grid(map, x, y), u, v, 255);
            r_sc2_push_vertex_normal(&vertices[x + y * (w + 1)],
                                     px,
                                     py,
                                     r_sc2_ground_height_at_grid(map, x, y),
                                     u,
                                     v,
                                     255,
                                     (VECTOR3){ 0.0f, 0.0f, 1.0f });
        }
    }
    r_sc2_build_ground_vertex_normals(map, vertices, w, h);

    out = indices;
    FOR_LOOP(y, h) {
        FOR_LOOP(x, w) {
            DWORD i00 = x + y * (w + 1);
            DWORD i10 = i00 + 1;
            DWORD i01 = i00 + w + 1;
            DWORD i11 = i01 + 1;

            if (r_sc2_skip_ground_cell(map, x, y)) {
                continue;
            }
            *out++ = i00;
            *out++ = i10;
            *out++ = i11;
            *out++ = i00;
            *out++ = i11;
            *out++ = i01;
        }
    }

    map_layer = ri.MemAlloc(sizeof(*map_layer));
    memset(map_layer, 0, sizeof(*map_layer));
    map_layer->type = MAPLAYERTYPE_GROUND;
    map_layer->buffer = R_MakeIndexedVertexArrayObject(vertices, num_vertices, indices, (DWORD)(out - indices));
    map_layer->num_vertices = num_vertices;
    map_layer->num_indices = (DWORD)(out - indices);
    ri.MemFree(vertices);
    ri.MemFree(indices);
    return map_layer;
}

static BOOL r_sc2_file_exists(LPCSTR path) {
    void *buffer = NULL;
    int size;

    size = ri.FS_ReadFile(path, &buffer);
    if (size < 0 || !buffer)
        return false;
    ri.FS_FreeFile(buffer);
    return true;
}

static LPCMODEL r_sc2_load_cliff_model(LPCSTR path) {
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

static BOOL r_sc2_cliff_model_path(LPSTR path, size_t size, LPCSTR mesh, char const config[5], DWORD variant) {
    snprintf(path, size, "Assets\\Cliffs\\%s\\%s_%s_%02u.m3", mesh, mesh, config, variant & SC2_CLIFF_VARIANT_MASK);
    if (r_sc2_file_exists(path))
        return true;
    snprintf(path, size, "Assets\\Cliffs\\%s\\%s_%s_00.m3", mesh, mesh, config);
    if (r_sc2_file_exists(path))
        return true;
    return false;
}

static BOOL r_sc2_cliff_config(sc2Map_t const *map,
                               DWORD x,
                               DWORD y,
                               char config[5],
                               USHORT *baselevel,
                               USHORT *toplevel,
                               int *rotation) {
    USHORT level[4];
    USHORT base;
    USHORT top;
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

static VECTOR3 r_sc2_rotate_cliff_vec(VECTOR3 pos, int rotation) {
    switch (rotation & 3) {
        case 1: return (VECTOR3){ -pos.y,  pos.x, pos.z };
        case 2: return (VECTOR3){ -pos.x, -pos.y, pos.z };
        case 3: return (VECTOR3){  pos.y, -pos.x, pos.z };
        default: return pos;
    }
}

static VECTOR3 r_sc2_m3_raw_normal(m3Vertex_t const *vertex) {
    VECTOR3 normal = {
        (FLOAT)vertex->normal[0] * SC2_M3_NORMAL_SCALE - 1.0f,
        (FLOAT)vertex->normal[1] * SC2_M3_NORMAL_SCALE - 1.0f,
        (FLOAT)vertex->normal[2] * SC2_M3_NORMAL_SCALE - 1.0f,
    };
    Vector3_normalize(&normal);
    return normal;
}

static VECTOR3 r_sc2_matrix_transform(LPCMATRIX4 matrix, LPCVECTOR3 v, FLOAT w) {
    return (VECTOR3){
        matrix->v[0] * v->x + matrix->v[4] * v->y + matrix->v[8]  * v->z + matrix->v[12] * w,
        matrix->v[1] * v->x + matrix->v[5] * v->y + matrix->v[9]  * v->z + matrix->v[13] * w,
        matrix->v[2] * v->x + matrix->v[6] * v->y + matrix->v[10] * v->z + matrix->v[14] * w,
    };
}

static void r_sc2_m3_make_bone_matrix(LPCVECTOR3 position,
                                      LPCVECTOR4 rotation,
                                      LPCVECTOR3 scale,
                                      LPCMATRIX4 parent,
                                      LPMATRIX4 matrix) {
    MATRIX4 local;

    Matrix4_identity(&local);
    Matrix4_translate(&local, position);
    Matrix4_rotate4(&local, rotation);
    Matrix4_scale(&local, scale);
    Matrix4_multiply(parent, &local, matrix);
}

static void r_sc2_m3_build_cliff_bones(m3Model_t const *m3, MATRIX4 bones[SC2_M3_MAX_BONES]) {
    MATRIX4 tmp[SC2_M3_MAX_BONES];
    MATRIX4 identity;
    m3SequenceTimeline_t const *timeline;
    DWORD localtime = 0;

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
        VECTOR3 position = M3_GetVector3AnimValue(m3, timeline, &bone->position, localtime);
        VECTOR4 rotation = M3_GetVector4AnimValue(m3, timeline, &bone->rotation, localtime);
        VECTOR3 scale = M3_GetVector3AnimValue(m3, timeline, &bone->scale, localtime);
        LPCMATRIX4 parent = bone->parent >= 0 && bone->parent < (SHORT)m3->bonesNum ? &tmp[bone->parent] : &identity;

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

static VECTOR3 r_sc2_m3_skin_vertex(m3Vertex_t const *vertex,
                                    m3Region_t const *region,
                                    MATRIX4 const bones[SC2_M3_MAX_BONES],
                                    FLOAT w) {
    VECTOR3 out = { 0 };
    VECTOR3 raw = w == 0.0f ? r_sc2_m3_raw_normal(vertex) : vertex->pos;

    FOR_LOOP(i, 4) {
        DWORD bone = region->firstBoneLookupIndex + vertex->boneIndex[i];
        FLOAT weight = (FLOAT)vertex->boneWeight[i] / 255.0f;
        VECTOR3 part;

        if (bone >= SC2_M3_MAX_BONES || weight <= 0.0f)
            continue;
        part = r_sc2_matrix_transform(&bones[bone], &raw, w);
        out.x += part.x * weight;
        out.y += part.y * weight;
        out.z += part.z * weight;
    }
    return out;
}

static BOOL r_sc2_cliff_model_bounds(LPCMODEL model, LPBOX3 bounds) {
    m3Model_t const *m3;
    MATRIX4 bones[SC2_M3_MAX_BONES];
    BOOL have_vertex = false;

    if (!model || model->modeltype != ID_43DM || !model->m3 || !model->m3->verticesNum)
        return false;
    m3 = model->m3;
    r_sc2_m3_build_cliff_bones(m3, bones);
    FOR_LOOP(div_i, m3->divisionsNum) {
        m3Divisions_t const *div = &m3->divisions[div_i];
        FOR_LOOP(region_i, div->regionsNum) {
            m3Region_t const *region = &div->regions[region_i];
            for (DWORD index_i = 0; index_i + 2 < region->triangleIndicesCount; index_i += 3) {
                DWORD fi[3], vi[3];
                BOOL valid = true;
                FOR_LOOP(k, 3) {
                    fi[k] = region->firstTriangleIndex + index_i + k;
                    if (fi[k] >= div->facesNum) { valid = false; break; }
                    vi[k] = div->faces[fi[k]] + region->firstVertexIndex;
                    if (vi[k] >= m3->verticesNum) { valid = false; break; }
                }
                if (!valid)
                    continue;
                FOR_LOOP(k, 3) {
                    VECTOR3 pos = r_sc2_m3_skin_vertex(&m3->vertices[vi[k]], region, bones, 1.0f);
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

static VECTOR2 r_sc2_cliff_vertex_xy(sc2Map_t const *map,
                                     LPCVECTOR2 offset,
                                     LPCVECTOR3 rotated) {
    FLOAT scale = SC2_CLIFF_BLOCK_SPAN * map->cell_size / SC2_CLIFF_MODEL_FOOTPRINT;

    return (VECTOR2){
        offset->x + rotated->x * scale,
        offset->y + rotated->y * scale,
    };
}

static void r_sc2_bake_cliff_region(rCliffBakeList_t *list,
                                    sc2Map_t const *map,
                                    m3Model_t const *m3,
                                    m3Divisions_t const *div,
                                    m3Region_t const *region,
                                    MATRIX4 const bones[SC2_M3_MAX_BONES],
                                    rSc2CliffPlacement_t const *placement,
                                    LPCVECTOR2 offset,
                                    int rotation) {
    for (DWORD index_i = 0; index_i + 2 < region->triangleIndicesCount; index_i += 3) {
        DWORD fi[3], vi[3];
        BOOL valid = true;

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
            VERTEX *out = R_CliffBakeVertex(list);
            VECTOR3 local;
            VECTOR3 normal;
            VECTOR3 rotated;
            VECTOR3 position;
            VECTOR2 xy;
            VECTOR2 uv;

            local = r_sc2_m3_skin_vertex(vertex, region, bones, 1.0f);
            normal = r_sc2_m3_skin_vertex(vertex, region, bones, 0.0f);
            rotated = r_sc2_rotate_cliff_vec(local, rotation);
            xy = r_sc2_cliff_vertex_xy(map, offset, &rotated);
            position = (VECTOR3){
                xy.x,
                xy.y,
                placement->base_z + (placement->model_z_offset + local.z) * placement->z_scale +
                    sc2_map_height_adjust_at_point(map, xy.x, xy.y),
            };
            uv = (VECTOR2){ vertex->uv[0][0] / SC2_M3_UV_SCALE, vertex->uv[0][1] / SC2_M3_UV_SCALE };
            normal = r_sc2_rotate_cliff_vec(normal, rotation);
            Vector3_normalize(&normal);
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

static void r_sc2_bake_cliff_model(rCliffBakeList_t *list,
                                   sc2Map_t const *map,
                                   LPCMODEL model,
                                   DWORD grid_x,
                                   DWORD grid_y,
                                   int rotation,
                                   USHORT baselevel) {
    BOX2 map_bounds = SC2_MapBounds();
    BOX3 bounds;
    rSc2CliffPlacement_t placement;
    VECTOR2 offset;
    m3Model_t const *m3;
    MATRIX4 bones[SC2_M3_MAX_BONES];

    if (!model || model->modeltype != ID_43DM || !model->m3 || !model->m3->verticesNum)
        return;
    m3 = model->m3;
    r_sc2_m3_build_cliff_bones(m3, bones);
    if (!r_sc2_cliff_model_bounds(model, &bounds)) {
        bounds.min = (VECTOR3){ -map->cell_size, -map->cell_size, 0.0f };
        bounds.max = (VECTOR3){  map->cell_size,  map->cell_size, 0.0f };
    }
    offset = (VECTOR2){
        map_bounds.min.x + SC2_CLIFF_BLOCK_CENTER(grid_x) * map->cell_size,
        map_bounds.min.y + SC2_CLIFF_BLOCK_CENTER(grid_y) * map->cell_size,
    };
    {
        FLOAT scale = sc2_map_height_scale(map);
        FLOAT offset_z = sc2_map_height_offset(map);
        DWORD corner_x[4] = { grid_x, grid_x + SC2_CLIFF_BLOCK_SPAN, grid_x + SC2_CLIFF_BLOCK_SPAN, grid_x };
        DWORD corner_y[4] = { grid_y, grid_y, grid_y + SC2_CLIFF_BLOCK_SPAN, grid_y + SC2_CLIFF_BLOCK_SPAN };
        USHORT level[4];
        FLOAT base_z = 0.0f;
        DWORD num_base = 0;

        SC2_CLIFF_BLOCK_LEVELS(level, map, grid_x, grid_y);
        FOR_LOOP(i, 4) {
            DWORD x = MIN(map->t3HeightMap->width - 1, corner_x[i]);
            DWORD y = MIN(map->t3HeightMap->height - 1, corner_y[i]);
            sc2MapHeightSample_t const *sample = &map->t3HeightMap->data[x + y * map->t3HeightMap->width];

            if (level[i] != baselevel)
                continue;
            base_z += (FLOAT)sample->height * scale - offset_z;
            num_base++;
        }
        placement.base_z = num_base ? base_z / (FLOAT)num_base : -bounds.min.z;
        placement.model_z_offset = -bounds.min.z;
        placement.z_scale = 1.0f;
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

static LPCTEXTURE r_sc2_cliff_diffuse_texture(LPCMODEL model) {
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

static sc2CliffCell_t const *r_sc2_find_cliff_cell(sc2Map_t const *map, DWORD index) {
    FOR_LOOP(i, map->t3Terrain.num_cliff_cells) {
        if (map->t3Terrain.cliff_cells[i].index == index)
            return &map->t3Terrain.cliff_cells[i];
    }
    return NULL;
}

static BOOL r_sc2_cliff_corner_touches_neighbor(DWORD corner, int dx, int dy) {
    switch (corner) {
        case 0: return dx <= 0 && dy <= 0;
        case 1: return dx >= 0 && dy <= 0;
        case 2: return dx >= 0 && dy >= 0;
        case 3: return dx <= 0 && dy >= 0;
        default: return false;
    }
}

static int r_sc2_cliff_neighbor_score(USHORT level[4], USHORT top, int dx, int dy) {
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

static sc2CliffCell_t r_sc2_cliff_cell_for_index(sc2Map_t const *map, DWORD index, DWORD cliff_width) {
    sc2CliffCell_t cell = { .index = index };
    sc2CliffCell_t const *xml_cell = r_sc2_find_cliff_cell(map, index);
    DWORD cliff_height = MAX(1, (SC2_MAP_HEIGHT(map) + 1) / 2);
    DWORD cx = index % cliff_width;
    DWORD cy = index / cliff_width;
    DWORD grid_x = cx * SC2_CLIFF_BLOCK_SPAN;
    DWORD grid_y = cy * SC2_CLIFF_BLOCK_SPAN;
    USHORT level[4];
    USHORT base;
    USHORT top;
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
                xml_cell = r_sc2_find_cliff_cell(map, (DWORD)nx + (DWORD)ny * cliff_width);
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

static LPMAPLAYER r_sc2_build_cliff_layer(sc2Map_t const *map) {
    rCliffBakeList_t list = {0};
    LPMAPLAYER map_layer;
    DWORD cliff_width;
    DWORD cliff_height;
    LPCTEXTURE diffuse = NULL;

    if (!map || !map->t3Terrain.num_cliff_sets || !map->t3SyncCliffLevel)
        return NULL;
    cliff_width = SC2_CLIFF_WIDTH(map);
    cliff_height = MAX(1, (SC2_MAP_HEIGHT(map) + 1) / 2);
    FOR_LOOP(cy, cliff_height) {
        FOR_LOOP(cx, cliff_width) {
            DWORD index = cx + cy * cliff_width;
            sc2CliffCell_t cell = r_sc2_cliff_cell_for_index(map, index, cliff_width);
            sc2CliffSet_t const *set;
            char config[5];
            PATHSTR path;
            DWORD grid_x = cx * SC2_CLIFF_BLOCK_SPAN;
            DWORD grid_y = cy * SC2_CLIFF_BLOCK_SPAN;
            USHORT baselevel;
            int rotation;
            LPCMODEL model;

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
            if (!model || model->modeltype != ID_43DM || !model->m3)
                continue;
            if (!diffuse)
                diffuse = r_sc2_cliff_diffuse_texture(model);
            r_sc2_bake_cliff_model(&list, map, model, grid_x, grid_y, rotation, baselevel);
        }
    }
    if (!list.num_vertices) {
        ri.MemFree(list.vertices);
        return NULL;
    }
    map_layer = ri.MemAlloc(sizeof(*map_layer));
    memset(map_layer, 0, sizeof(*map_layer));
    map_layer->type = MAPLAYERTYPE_CLIFF;
    map_layer->texture = diffuse ? diffuse : tr.texture[TEX_WHITE];
    map_layer->buffer = R_MakeVertexArrayObject(list.vertices, list.num_vertices);
    map_layer->num_vertices = list.num_vertices;
    ri.MemFree(list.vertices);
    return map_layer;
}

static void r_sc2_build_terrain(sc2Map_t const *map) {
    BOX2 bounds;
    FLOAT max_z = 1.0f;

    r_sc2_release_terrain();
    if (!map || !SC2_MAP_WIDTH(map) || !SC2_MAP_HEIGHT(map))
        return;

    r_sc2_init_terrain_shader();
    r_sc2_load_terrain_textures(map);
    sc2_terrain_segment = ri.MemAlloc(sizeof(*sc2_terrain_segment));
    memset(sc2_terrain_segment, 0, sizeof(*sc2_terrain_segment));
    r_sc2_add_layer(&sc2_terrain_segment->layers, r_sc2_build_ground_layer(map));
    r_sc2_add_layer(&sc2_terrain_segment->layers, r_sc2_build_cliff_layer(map));

    bounds = SC2_MapBounds();
    if (map->t3HeightMap) {
        FOR_LOOP(y, SC2_MAP_HEIGHT(map) + 1) {
            FOR_LOOP(x, SC2_MAP_WIDTH(map) + 1) {
                max_z = MAX(max_z, r_sc2_ground_height_at_grid(map, x, y) + 1.0f);
            }
        }
    }
    sc2_terrain_segment->bbox = (BOX3){
        .min = { bounds.min.x, bounds.min.y, -1.0f },
        .max = { bounds.max.x, bounds.max.y, max_z },
    };
}

static LPTEXTURE r_sc2_terrain_layer_texture(DWORD index) {
    if (index < sc2_num_terrain_layers && sc2_terrain_textures[index])
        return sc2_terrain_textures[index];
    return sc2_terrain_textures[0];
}

static void r_sc2_set_fog_uniforms(GLint u_color, GLint u_params) {
    sc2Map_t const *map = SC2_MapCurrent();
    sc2MapTerrain_t const *terrain = map ? &map->t3Terrain : NULL;
    COLOR32 color = terrain ? terrain->fog_color : COLOR32_BLACK;
    FLOAT enabled = terrain && terrain->fog_enabled && terrain->fog_density > 0.0f ? 1.0f : 0.0f;

    R_Call(glUniform4f, u_color,
           color.r / 255.0f,
           color.g / 255.0f,
           color.b / 255.0f,
           color.a / 255.0f);
    R_Call(glUniform4f, u_params,
           terrain ? terrain->fog_start_height : 0.0f,
           terrain ? terrain->fog_density : 0.0f,
           terrain ? terrain->fog_falloff : 0.0f,
           enabled);
}

static void r_sc2_set_light_uniforms(rSc2LightUniforms_t const *uniforms) {
    sc2Map_t const *map = SC2_MapCurrent();
    sc2MapLighting_t const *lighting = map ? &map->lighting : NULL;
    VECTOR3 ambient = lighting && lighting->enabled ? lighting->ambient_color : (VECTOR3){ 1.0f, 1.0f, 1.0f };

    if (!uniforms)
        return;
    R_Call(glUniform3f, uniforms->ambient, ambient.x, ambient.y, ambient.z);
    FOR_LOOP(i, SC2_MAX_DIRECTIONAL_LIGHTS) {
        sc2DirectionalLight_t const *light = lighting && lighting->enabled ? &lighting->directional[i] : NULL;
        FLOAT enabled = light && light->enabled ? 1.0f : 0.0f;
        VECTOR3 direction = enabled ? (VECTOR3){ -light->direction.x, -light->direction.y, -light->direction.z } : (VECTOR3){ 0.0f, 0.0f, 1.0f };
        VECTOR3 color = enabled ? light->color : (VECTOR3){ 0.0f, 0.0f, 0.0f };
        FLOAT multiplier = enabled ? light->color_multiplier : 0.0f;

        R_Call(glUniform3f, uniforms->direction[i], direction.x, direction.y, direction.z);
        R_Call(glUniform3f, uniforms->color[i], color.x * multiplier, color.y * multiplier, color.z * multiplier);
    }
}

static void r_sc2_begin_terrain_shader(MATRIX4 const *model_matrix) {
    R_Call(glUseProgram, sc2_terrain_shader->progid);
    R_Call(glUniformMatrix4fv, sc2_terrain_shader->uViewProjectionMatrix, 1, GL_FALSE, tr.viewDef.viewProjectionMatrix.v);
    R_Call(glUniformMatrix4fv, sc2_terrain_shader->uModelMatrix, 1, GL_FALSE, model_matrix->v);
    R_Call(glUniformMatrix4fv, sc2_terrain_shader->uTextureMatrix, 1, GL_FALSE, tr.viewDef.textureMatrix.v);
    r_sc2_set_fog_uniforms(sc2_u_fog_color, sc2_u_fog_params);
    r_sc2_set_light_uniforms(&sc2_u_terrain_light);
}

static void r_sc2_begin_terrain_pass(DWORD group) {
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
    BOX2 bounds = SC2_MapBounds();
    FLOAT map_w = bounds.max.x - bounds.min.x;
    FLOAT map_h = bounds.max.y - bounds.min.y;

    R_Call(glUniform2f, sc2_u_world_uv_scale, map_w / SC2_TERRAIN_UV_SCALE, map_h / SC2_TERRAIN_UV_SCALE);
    R_Call(glUniform2f, sc2_u_world_uv_offset, bounds.min.x / SC2_TERRAIN_UV_SCALE, bounds.min.y / SC2_TERRAIN_UV_SCALE);
}

static void r_sc2_draw_terrain_indexed(LPCMAPLAYER layer) {
    r_sc2_set_terrain_uv();
    FOR_LOOP(group, SC2_TERRAIN_BLEND_GROUPS) {
        r_sc2_begin_terrain_pass(group);
        R_DrawIndexedBuffer(layer->buffer, layer->num_indices);
    }
    R_Call(glDisable, GL_BLEND);
    R_Call(glDepthMask, GL_TRUE);
}

static void r_sc2_draw_terrain_vertices(LPCMAPLAYER layer) {
    r_sc2_set_terrain_uv();
    FOR_LOOP(group, SC2_TERRAIN_BLEND_GROUPS) {
        r_sc2_begin_terrain_pass(group);
        R_DrawBuffer(layer->buffer, layer->num_vertices);
    }
    R_Call(glDisable, GL_BLEND);
    R_Call(glDepthMask, GL_TRUE);
}

static void r_sc2_draw_ground_layer(LPCMAPSEGMENT segment) {
    LPCMAPLAYER layer;
    MATRIX4 model_matrix;

    if (!sc2_terrain_shader || !segment)
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

static void r_sc2_load_minimap(LPCSTR mapFileName) {
    static LPCSTR const candidates[] = { "Minimap.tga", "Minimap.dds", NULL };
    PATHSTR path;
    SAFE_DELETE(tr.minimap, R_ReleaseTexture);
    for (int i = 0; candidates[i]; i++) {
        snprintf(path, sizeof(path), "%s\\%s", mapFileName, candidates[i]);
        tr.minimap = R_LoadTexture(path);
        if (tr.minimap) return;
    }
}

void R_SC2RegisterMap(LPCSTR mapFileName) {
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

static void r_sc2_draw_cliff_layer(LPCMAPSEGMENT segment) {
    LPCMAPLAYER layer;
    MATRIX4 model_matrix;

    if (!sc2_terrain_shader || !sc2_cliff_shader || !segment)
        return;
    layer = segment->layers;
    while (layer && layer->type != MAPLAYERTYPE_CLIFF) {
        layer = layer->next;
    }
    if (!layer)
        return;

    Matrix4_identity(&model_matrix);

    /* Pass 1: terrain blend - fills depth and lays down terrain color seamlessly.
       Use world XY position for terrain UV so tiling matches the ground exactly. */
    r_sc2_begin_terrain_shader(&model_matrix);
    r_sc2_draw_terrain_vertices(layer);

    /* Pass 2: cliff M3 texture alpha-blended on top, pulled slightly forward */
    R_Call(glEnable, GL_POLYGON_OFFSET_FILL);
    R_Call(glPolygonOffset, -1.0f, -1.0f);
    R_Call(glUseProgram, sc2_cliff_shader->progid);
    R_Call(glUniformMatrix4fv, sc2_cliff_shader->uViewProjectionMatrix, 1, GL_FALSE, tr.viewDef.viewProjectionMatrix.v);
    R_Call(glUniformMatrix4fv, sc2_cliff_shader->uModelMatrix, 1, GL_FALSE, model_matrix.v);
    R_Call(glUniformMatrix4fv, sc2_cliff_shader->uTextureMatrix, 1, GL_FALSE, tr.viewDef.textureMatrix.v);
    r_sc2_set_fog_uniforms(sc2_u_cliff_fog_color, sc2_u_cliff_fog_params);
    r_sc2_set_light_uniforms(&sc2_u_cliff_light);
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_BindTexture(layer->texture, 0);
    R_DrawBuffer(layer->buffer, layer->num_vertices);
    R_Call(glDisable, GL_POLYGON_OFFSET_FILL);
    R_Call(glPolygonOffset, 0.0f, 0.0f);
}

void R_SC2DrawWorld(void) {
    MATRIX4 model_matrix;

    if (!sc2_terrain_segment || (tr.viewDef.rdflags & RDF_NOWORLDMODEL))
        return;

    Matrix4_identity(&model_matrix);
    R_Call(glUseProgram, tr.shader[SHADER_UI]->progid);
    R_Call(glUniformMatrix4fv, tr.shader[SHADER_UI]->uViewProjectionMatrix, 1, GL_FALSE, tr.viewDef.viewProjectionMatrix.v);
    R_Call(glUniformMatrix4fv, tr.shader[SHADER_UI]->uModelMatrix, 1, GL_FALSE, model_matrix.v);
    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_TRUE);
    R_Call(glDepthFunc, GL_LEQUAL);
    R_Call(glColorMask, GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    r_sc2_draw_ground_layer(sc2_terrain_segment);
    r_sc2_draw_cliff_layer(sc2_terrain_segment);
}

static BOOL r_sc2_clip_trace_to_bounds(LPCLINE3 line, LPCBOX2 bounds, LPFLOAT t0, LPFLOAT t1) {
    FLOAT const bounds_min[2] = { bounds->min.x, bounds->min.y };
    FLOAT const bounds_max[2] = { bounds->max.x, bounds->max.y };
    FLOAT const start[2] = { line->a.x, line->a.y };
    FLOAT const finish[2] = { line->b.x, line->b.y };

    *t0 = 0.0f;
    *t1 = 1.0f;
    FOR_LOOP(axis, 2) {
        FLOAT const dir = finish[axis] - start[axis];
        FLOAT near_t;
        FLOAT far_t;

        if (fabsf(dir) < SC2_TRACE_EPSILON) {
            if (start[axis] < bounds_min[axis] || start[axis] > bounds_max[axis])
                return false;
            continue;
        }

        near_t = (bounds_min[axis] - start[axis]) / dir;
        far_t = (bounds_max[axis] - start[axis]) / dir;
        if (near_t > far_t) {
            FLOAT const swap = near_t;
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

static BOOL r_sc2_trace_heightmap_tile(sc2Map_t const *map, DWORD x, DWORD y, LPCLINE3 line, LPVECTOR3 output) {
    BOX2 bounds = SC2_MapBounds();
    FLOAT x0 = bounds.min.x + x * map->cell_size;
    FLOAT y0 = bounds.min.y + y * map->cell_size;
    FLOAT x1 = x0 + map->cell_size;
    FLOAT y1 = y0 + map->cell_size;
    TRIANGLE3 const tri1 = {
        { x0, y0, r_sc2_ground_height_at_grid(map, x, y) },
        { x1, y0, r_sc2_ground_height_at_grid(map, x + 1, y) },
        { x1, y1, r_sc2_ground_height_at_grid(map, x + 1, y + 1) },
    };
    TRIANGLE3 const tri2 = {
        { x1, y1, r_sc2_ground_height_at_grid(map, x + 1, y + 1) },
        { x0, y1, r_sc2_ground_height_at_grid(map, x, y + 1) },
        { x0, y0, r_sc2_ground_height_at_grid(map, x, y) },
    };

    if (Line3_intersect_triangle(line, &tri1, output))
        return true;
    return Line3_intersect_triangle(line, &tri2, output);
}

bool R_SC2TraceLocation(viewDef_t const *viewdef, FLOAT x, FLOAT y, LPVECTOR3 output) {
    sc2Map_t const *map = SC2_MapCurrent();
    BOX2 bounds;
    LINE3 line;
    FLOAT t0, t1;
    FLOAT dir_x, dir_y;
    int tile_x, tile_y;
    int step_x, step_y;
    FLOAT t_max_x, t_max_y;
    FLOAT t_delta_x, t_delta_y;

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
        if (r_sc2_trace_heightmap_tile(map, (DWORD)tile_x, (DWORD)tile_y, &line, output))
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

FLOAT R_SC2GetHeightAtPoint(FLOAT x, FLOAT y) {
    return sc2_map_height_at_point(SC2_MapCurrent(), x, y);
}

VECTOR2 R_SC2WorldSize(void) {
    sc2Map_t const *map = SC2_MapCurrent();
    if (!map) return (VECTOR2){ 0.0f, 0.0f };
    return (VECTOR2){ SC2_MAP_WIDTH(map) * map->cell_size, SC2_MAP_HEIGHT(map) * map->cell_size };
}
