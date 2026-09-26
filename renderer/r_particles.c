#include "r_local.h"
#include "r_shader.h"

#define NUM_PARTICLE_VERTICES 6
#define MAX_PARTICLES 10000

typedef struct particle_vertex {
    vec3_t position;
    color32_t color;
    float size;
    vec3_t tail;
    float uv[2];
    uint8_t axis[2];
} particleVertex_t;

typedef enum {
    PARTICLE_UV_BILLBOARD,
    PARTICLE_UV_RIBBON,
} PARTICLEUVORDER;

typedef struct particlequad_s {
    vec3_t const *point, *tail;
    float u0, v0, u1, v1;
    color32_t color;
    float size;
} particleQuad_t;



typedef struct particleState_s {
    mat4_t viewProjection;
    mat4_t textureMatrix;
    mat4_t model;
    vec3_t eye;
    int texture;
    int fogOfWar;
    bool alphaKey;
    float alphaCutoff;
} particleState_t;


typedef struct particleProg_s {
    shaderProg_t prog;
    particleState_t state;
} particleProg_t;



static struct {
    particleProg_t shader;
//    renderTarget_t *rt[FOW_RT_COUNT];
    buffer_t *particles;
    texture_t *texture;
    particleVertex_t vertices[MAX_PARTICLES * NUM_PARTICLE_VERTICES];
} particles_resources = { 0 };

cparticle_t *active_particles, *free_particles;
cparticle_t particles[MAX_PARTICLES];
int cl_numparticles = MAX_PARTICLES;
static uint32_t particle_generation;

void R_ClearParticles(void) {
    particle_generation++;
    free_particles = &particles[0];
    active_particles = NULL;
    FOR_LOOP(i, cl_numparticles) {
        particles[i].next = &particles[i+1];
    }
    particles[cl_numparticles-1].next = NULL;
}

/* Reuse the particle pool while keeping independent views out of each other's draw/update lists. */
cparticle_t *R_BeginParticleScene(particleScene_t *scene) {
    cparticle_t *previous = active_particles;
    active_particles = scene->generation == particle_generation ? scene->active : NULL;
    scene->generation = particle_generation;
    return previous;
}

void R_EndParticleScene(particleScene_t *scene, cparticle_t *previous) {
    scene->active = active_particles;
    active_particles = previous;
}

/* Pool resets invalidate retained scene pointers without walking already recycled particles. */
void R_ClearParticleScene(particleScene_t *scene) {
    if (scene->generation == particle_generation) {
        while (scene->active) {
            cparticle_t *p = scene->active;
            scene->active = p->next; p->next = free_particles; free_particles = p;
        }
    }
    *scene = (particleScene_t){0};
}

cparticle_t *R_SpawnParticle(void) {
    if (!free_particles)
//        return NULL;
        return NULL;
    cparticle_t *p = free_particles;
    free_particles = p->next;
    p->next = active_particles;
    active_particles = p;
    p->blend_mode = BLEND_MODE_ADD;
    p->tail = (vec3_t){0};
    p->size_value_scale = p->size_time_scale = 1.0f;
    return p;
}

#define SHADER_TYPE particleState_t
static const shader_desc_t sd_particle = {
    .Name = "particle",
    .Uniforms = {
        UNIFORM(viewProjection, UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(textureMatrix,  UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(model,          UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(eye,            UT_FLOAT_VEC3, PRECISION_HIGH),
        UNIFORM(texture,        UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(fogOfWar,       UT_SAMPLER_2D, PRECISION_LOW),
        UNIFORM(alphaKey,       UT_BOOL,       PRECISION_LOW),
        UNIFORM(alphaCutoff,    UT_FLOAT,      PRECISION_LOW),
    },
    .Attributes = {
        ATTRIB(position, attrib_position,     UT_FLOAT_VEC3),
        ATTRIB(color,    attrib_color,        UT_COLOR),
        ATTRIB(texcoord, attrib_texcoord,     UT_FLOAT_VEC2),
        ATTRIB(size,     attrib_particleSize, UT_FLOAT),
        ATTRIB(tail,     attrib_particleTail, UT_FLOAT_VEC3),
        ATTRIB(axis,     attrib_particleAxis, UT_FLOAT_VEC2),
    },
    .Shared = {
        SHARED(color,    UT_COLOR),
        SHARED(texcoord, UT_FLOAT_VEC2),
        SHARED(texcoord2, UT_FLOAT_VEC2),
    },
    .VertexBody =
        "vec4 vert() {\n"
        "  mat4 m = u_viewProjection;\n"
        "  vec3 cameraLeft = normalize(vec3(m[0][0], m[1][0], m[2][0]));\n"
        "  vec3 left = cameraLeft * a_size;\n"
        "  vec3 up = normalize(vec3(m[0][1], m[1][1], m[2][1])) * a_size;\n"
        "  vec3 pos;\n"
        "  if (dot(a_tail, a_tail) > 0.0) {\n"
        "    vec3 point = a_position - a_tail * (1.0 - a_axis.y);\n"
        "    vec3 side = cross(normalize(a_tail), u_eye - point);\n"
        "    float sideLength = length(side);\n"
        "    if (sideLength < 0.0001) side = cameraLeft; else side /= sideLength;\n"
        "    pos = point + side * ((a_axis.x - 0.5) * a_size);\n"
        "  } else {\n"
        "    mat3 bb_mat = mat3(left, up, a_position);\n"
        "    pos = bb_mat * vec3(a_axis - vec2(0.5), 1.0);\n"
        "  }\n"
        "  v_color = a_color;\n"
        "  v_texcoord = a_texcoord;\n"
        "  v_texcoord2 = (u_textureMatrix * vec4(pos, 1.0)).xy;\n"
        "  return u_viewProjection * vec4(pos, 1.0);\n"
        "}\n",
    /* Waterfalls are PRE2-only models; the old unfogged particle pass exposed the whole effect through black FOW. */
    .FragmentBody =
        "vec4 frag() {\n"
        "  vec4 col = texture(u_texture, v_texcoord) * v_color;\n"
        "#ifdef USE_FOGOFWAR\n"
        "  col.rgb *= texture(u_fogOfWar, v_texcoord2).r;\n"
        "#endif\n"
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

/* Emit one billboard or ribbon quad from a shared vertex-order table. */
static particleVertex_t *R_AddParticleQuad(particleVertex_t *buffer,
                                           particleQuad_t const *quad, PARTICLEUVORDER order) {
    static uint8_t const axis[NUM_PARTICLE_VERTICES][2] = {{0,0}, {255,0}, {255,255}, {255,255}, {0,255}, {0,0}};
    static uint8_t const uv_index[2][NUM_PARTICLE_VERTICES][2] = {
        {{0,1}, {2,1}, {2,3}, {2,3}, {0,3}, {0,1}},
        {{0,3}, {0,1}, {2,1}, {2,1}, {2,3}, {0,3}},
    };
    float const uv[4] = {quad->u0, quad->v0, quad->u1, quad->v1};
    vec3_t const tail = quad->tail ? *quad->tail : (vec3_t){0};

    FOR_LOOP(i, NUM_PARTICLE_VERTICES) {
        particleVertex_t const vertex = {
            .position = *quad->point,
            .color = quad->color,
            .size = quad->size,
            .tail = tail,
            .uv = {uv[uv_index[order][i][0]], uv[uv_index[order][i][1]]},
            .axis = {axis[i][0], axis[i][1]},
        };
        *buffer++ = vertex;
    }
    return buffer;
}

particleVertex_t *R_AddParticle(particleVertex_t *buffer,
              vec3_t const *point,
              vec3_t const *tail,
              color32_t uvr,
              color32_t color,
              float size)
{
    uint8_t *uv = (uint8_t *)&uvr;
    particleQuad_t const quad = {
        .point = point, .tail = tail,
        .u0 = BYTE2FLOAT(uv[0]), .v0 = BYTE2FLOAT(uv[1]),
        .u1 = BYTE2FLOAT(uv[2]), .v1 = BYTE2FLOAT(uv[3]),
        .color = color, .size = size,
    };
    return R_AddParticleQuad(buffer, &quad, PARTICLE_UV_BILLBOARD);
}

void R_UpdateParticles(void) {
    cparticle_t *active = NULL;
    cparticle_t *tail = NULL;
    cparticle_t *next = NULL;
    float frameTime = tr.viewDef.deltaTime / 1000.f;
    
    for (cparticle_t *p = active_particles; p; p = next) {
        next = p->next;
        p->time += frameTime;
        if (p->time > p->lifespan) {
            p->next = free_particles;
            free_particles = p;
            continue;
        }
        p->next = NULL;
        if (!tail) {
            active = tail = p;
        } else {
            tail->next = p;
            tail = p;
        }
    }
    active_particles = active;
}

color32_t FX_LerpColor(color32_t a, color32_t b, float t) {
    return (color32_t) {
        .r = LerpNumber(a.r, b.r, t),
        .g = LerpNumber(a.g, b.g, t),
        .b = LerpNumber(a.b, b.b, t),
        .a = LerpNumber(a.a, b.a, t),
    };
}

float FX_BlendFloat(uint8_t const *values, float k, float midtime) {
    if (k > midtime) {
        return LerpNumber(values[1], values[2], (k - midtime) / (1 - midtime));
    } else {
        return LerpNumber(values[0], values[1], k / midtime);
    }
}

color32_t FX_BlendColor(cparticle_t const *p) {
    float k = p->time / p->lifespan;
    float t = (float)p->midtime / (float)0xff;
    if (k > t) {
        return FX_LerpColor(p->color[1], p->color[2], (k - t) / (1 - t));
    } else {
        return FX_LerpColor(p->color[0], p->color[1], k / t);
    }
}

static void R_FlushParticles(texture_t const *texture, mat4_t const *matrix, particleVertex_t *pv, BLEND_MODE blend_mode) {
    R_Call(glBindVertexArray, particles_resources.particles->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, particles_resources.particles->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(particleVertex_t) * (pv - particles_resources.vertices), particles_resources.vertices, GL_DYNAMIC_DRAW);

    particles_resources.shader.state.model = *matrix;
    particles_resources.shader.state.viewProjection = tr.viewDef.viewProjectionMatrix;
    particles_resources.shader.state.eye = tr.viewDef.camerastate[0].eye;
    particles_resources.shader.state.textureMatrix = tr.viewDef.textureMatrix;
#ifdef USE_FOGOFWAR
    R_Call(glActiveTexture, GL_TEXTURE2);
    R_Call(glBindTexture, GL_TEXTURE_2D, (tr.viewDef.rdflags & RDF_NOWORLDMODEL) ? tr.texture[TEX_WHITE]->texid : R_GetFogOfWarTexture());
#endif
    R_Call(glActiveTexture, GL_TEXTURE0);
    R_Call(glBindTexture, GL_TEXTURE_2D, (texture?texture:particles_resources.texture)->texid);
    particles_resources.shader.state.alphaKey = blend_mode == BLEND_MODE_ALPHAKEY;
    particles_resources.shader.state.alphaCutoff = 0.5f;
    R_SetAlphaKeyState(blend_mode == BLEND_MODE_ALPHAKEY);
    if (blend_mode == BLEND_MODE_NONE) {
        R_Call(glDisable, GL_BLEND);
        R_Call(glDepthMask, GL_TRUE);
        R_Call(glBlendFunc, GL_ONE, GL_ZERO);
    } else if (blend_mode == BLEND_MODE_ALPHAKEY) {
        /* Alpha-key particles must not inherit additive state from an earlier batch. */
        R_Call(glDisable, GL_BLEND);
        R_Call(glDepthMask, GL_TRUE);
        R_Call(glBlendFunc, GL_ONE, GL_ZERO);
    } else {
        R_Call(glEnable, GL_BLEND);
        R_Call(glDepthMask, GL_FALSE);
        switch (blend_mode) {
        case BLEND_MODE_ADD:
            /* Shared particle legacy: ADD means alpha-weighted additive. */
            R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE);
            break;
        case BLEND_MODE_ADDALPHA:
            /* Shared particle legacy: ADDALPHA means unweighted additive. */
            R_Call(glBlendFunc, GL_ONE, GL_ONE);
            break;
        case BLEND_MODE_MODULATE:
            R_Call(glBlendFunc, GL_ZERO, GL_SRC_COLOR);
            break;
        case BLEND_MODE_MODULATE_2X:
            R_Call(glBlendFunc, GL_DST_COLOR, GL_SRC_COLOR);
            break;
        default:
            R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        }
    }
    R_StatsDraw(GL_TRIANGLES, (uint32_t)(pv - particles_resources.vertices), 1);
    R_ApplyShader(&particles_resources.shader);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, (GLsizei)(pv - particles_resources.vertices));
}

static color32_t FX_GetFrame(cparticle_t const *p) {
    uint32_t columns = p->columns ? p->columns : 1;
    uint32_t rows = p->rows ? p->rows : 1;
    uint32_t total = columns * rows;
    /* The sprite-sheet frame advances over the particle's own lifetime, not a
     * global clock — otherwise every particle flips frames in unison, which
     * reads as a crude strobing "old game" effect. */
    float k = (p->lifespan > 0.0f) ? (p->time / p->lifespan) : 0.0f;
    uint32_t frame = (uint32_t)(k * (float)total);
    if (frame >= total) frame = total - 1;
    uint32_t u = frame % columns;
    uint32_t v = frame / columns;
    uint32_t usize = 256 / columns;
    uint32_t vsize = 256 / rows;
    return (color32_t) {
        usize * u,
        vsize * v,
        usize * (u + 1) - 1,
        vsize * (v + 1) - 1,
    };
}

void R_DrawParticles(void) {
    mat4_t matrix;
    particleVertex_t *pv = particles_resources.vertices;
    texture_t const *texture;
    BLEND_MODE blend_mode;

    if (!R_CvarEnabled("r_particles", "1") || !active_particles) return;
    texture = active_particles->texture; blend_mode = active_particles->blend_mode;
    
    Matrix4_identity(&matrix);
    R_UpdateParticles();
    
    FOR_EACH_LIST(cparticle_t const, p, active_particles) {
        if (p->texture != texture || p->blend_mode != blend_mode) {
            R_FlushParticles(texture, &matrix, pv, blend_mode);
            pv = particles_resources.vertices;
        }
        /* Kinematics: org = org0 + vel0*t + 1/2*accel*t^2. The original engine
         * integrates gravity per-frame (semi-implicit Euler), which over a
         * particle's life is the 1/2*a*t^2 closed form below; applying the full
         * a*t^2 made gravity-driven particles fall ~2x too fast. */
        vec3_t halfAccelT = Vector3_scale(&p->accel, 0.5f * p->time);
        vec3_t vel = Vector3_add(&p->vel, &halfAccelT);
        vec3_t org = Vector3_mad(&p->org, p->time, &vel);
        color32_t col = FX_BlendColor(p);
        float size = p->size_value_scale * FX_BlendFloat(p->size, p->time * p->size_time_scale,
                                                         BYTE2FLOAT(p->midtime));
        pv = R_AddParticle(pv, &org, &p->tail, FX_GetFrame(p), col, size);
        texture = p->texture;
        blend_mode = p->blend_mode;
    }
    
    R_FlushParticles(texture, &matrix, pv, blend_mode);
    R_SetAlphaKeyState(false);
}

/* Draw a single camera-facing (billboarded) sprite at a world position, reusing the particle
 * billboard pipeline. BLP textures are stored top-down and the particle shader maps a quad's top
 * vertex to V=1, so the UV rect is V-flipped to keep the sprite upright (top of image at top of quad). */
void R_DrawBillboardSprite(texture_t const *texture, vec3_t const *origin, float size, color32_t color) {
    mat4_t matrix;
    particleVertex_t *pv = particles_resources.vertices;
    color32_t const uv = { 0, 255, 255, 0 };

    if (!texture) texture = particles_resources.texture;
    Matrix4_identity(&matrix);
    pv = R_AddParticle(pv, origin, NULL, uv, color, size);
    R_FlushParticles(texture, &matrix, pv, BLEND_MODE_BLEND);
    R_SetAlphaKeyState(false);
}

/* Generic camera-facing textured polyline.  The particle shader expands each
 * segment into a camera-facing quad, while continuous U coordinates allow
 * tiled textures to move along the complete strip. */
void R_DrawRibbon(ribbonDraw_t const *draw) {
    mat4_t matrix;
    particleVertex_t *pv = particles_resources.vertices;
    GLboolean depth_enabled;
    float distance = 0.0f;
    ribbonDraw_t actual;

    if (!draw || !draw->points || draw->point_count < 2 || draw->width <= 0.0f) return;
    if (!draw->texture) { actual = *draw; actual.texture = particles_resources.texture; draw = &actual; }
    Matrix4_identity(&matrix);
    depth_enabled = glIsEnabled(GL_DEPTH_TEST);
    if (!draw->depth_test && depth_enabled) R_Call(glDisable, GL_DEPTH_TEST);
    FOR_LOOP(i, draw->point_count - 1) {
        vec3_t tail = Vector3_sub(draw->points + i + 1, draw->points + i);
        float length = Vector3_len(&tail);
        if (length <= 0.001f) continue;
        if (pv + NUM_PARTICLE_VERTICES > particles_resources.vertices + MAX_PARTICLES * NUM_PARTICLE_VERTICES) break;
        particleQuad_t const quad = {
            .point = draw->points + i + 1, .tail = &tail,
            .u0 = draw->texcoord_phase + distance * draw->texcoord_scale,
            .v0 = 0.0f,
            .u1 = draw->texcoord_phase + (distance + length) * draw->texcoord_scale,
            .v1 = 1.0f,
            .color = draw->color, .size = draw->width,
        };
        pv = R_AddParticleQuad(pv, &quad, PARTICLE_UV_RIBBON);
        distance += length;
    }
    if (pv != particles_resources.vertices) R_FlushParticles(draw->texture, &matrix, pv, draw->blend_mode);
    if (!draw->depth_test && depth_enabled) R_Call(glEnable, GL_DEPTH_TEST);
    R_SetAlphaKeyState(false);
}

static buffer_t *R_MakeParticlesVertexArrayObject(void) {
    buffer_t *buf = ri.MemAlloc(sizeof(buffer_t));

    R_Call(glGenVertexArrays, 1, &buf->vao);
    R_Call(glGenBuffers, 1, &buf->vbo);
    R_Call(glBindVertexArray, buf->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, buf->vbo);

    R_Call(glEnableVertexAttribArray, attrib_position);
    R_Call(glEnableVertexAttribArray, attrib_color);
    R_Call(glEnableVertexAttribArray, attrib_texcoord);
    R_Call(glEnableVertexAttribArray, attrib_particleSize);
    R_Call(glEnableVertexAttribArray, attrib_particleTail);
    R_Call(glEnableVertexAttribArray, attrib_particleAxis);
    
    R_Call(glVertexAttribPointer, attrib_position, 3, GL_FLOAT, GL_FALSE, sizeof(struct particle_vertex), FOFS(particle_vertex, position));
    R_Call(glVertexAttribPointer, attrib_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct particle_vertex), FOFS(particle_vertex, color));
    R_Call(glVertexAttribPointer, attrib_texcoord, 2, GL_FLOAT, GL_FALSE, sizeof(struct particle_vertex), FOFS(particle_vertex, uv));
    R_Call(glVertexAttribPointer, attrib_particleSize, 1, GL_FLOAT, GL_FALSE, sizeof(struct particle_vertex), FOFS(particle_vertex, size));
    R_Call(glVertexAttribPointer, attrib_particleTail, 3, GL_FLOAT, GL_FALSE, sizeof(struct particle_vertex), FOFS(particle_vertex, tail));
    R_Call(glVertexAttribPointer, attrib_particleAxis, 2, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct particle_vertex), FOFS(particle_vertex, axis));
    return buf;
}

#define DOT_TEXTURE 8

float dottexture[DOT_TEXTURE][DOT_TEXTURE] = {
    {0,0,0,0,0,0,0,0},
    {0,0,1,1,1,1,0,0},
    {0,1,2,2,2,2,1,0},
    {0,1,2,2,2,2,1,0},
    {0,1,2,2,2,2,1,0},
    {0,1,2,2,2,2,1,0},
    {0,0,1,1,1,1,0,0},
    {0,0,0,0,0,0,0,0},
};

void R_InitParticles(void) {
    color32_t data[DOT_TEXTURE][DOT_TEXTURE];
    FOR_LOOP(x, DOT_TEXTURE) FOR_LOOP(y, DOT_TEXTURE) {
        data[x][y].r = 0xff;
        data[x][y].g = 0xff;
        data[x][y].b = 0xff;
        data[x][y].a = dottexture[x][y] * 127;
    }
    
    particles_resources.texture = R_AllocateTexture(DOT_TEXTURE, DOT_TEXTURE);
    R_LoadTextureMipLevel(particles_resources.texture, &(texMip_t){ data, DOT_TEXTURE, DOT_TEXTURE, 0, PIXEL_RGBA });

    static char const *particle_defines =
#ifdef USE_FOGOFWAR
        "#define USE_FOGOFWAR 1\n"
#endif
#ifdef BZ_USE_MSAA
        "#define BZ_USE_MSAA 1\n"
#endif
        "";
    memset(&particles_resources.shader, 0, sizeof(particles_resources.shader));
    R_LoadShader(&sd_particle, particle_defines, &particles_resources.shader);
    /* The scene contract reserves unit 2 for FOW; particles omit the shadow sampler that normally occupies unit 1. */
    particles_resources.shader.state.fogOfWar = 2;
    particles_resources.particles = R_MakeParticlesVertexArrayObject();
    R_ClearParticles();
}

void R_ShutdownParticles(void) {
    R_ReleaseVertexArrayObject(particles_resources.particles);
    R_DeleteShader(&particles_resources.shader.prog);
    memset(&particles_resources.shader, 0, sizeof(particles_resources.shader));
}
