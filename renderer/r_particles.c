#include "r_local.h"

#define NUM_PARTICLE_VERTICES 6
#define MAX_PARTICLES 10000

typedef struct particle_vertex {
    VECTOR3 position;
    COLOR32 color;
    float size;
    BYTE uv[2];
    BYTE axis[2];
} particleVertex_t;

static struct {
    LPSHADER shader;
//    LPRENDERTARGET rt[FOW_RT_COUNT];
    LPBUFFER particles;
    LPTEXTURE texture;
    particleVertex_t vertices[MAX_PARTICLES * NUM_PARTICLE_VERTICES];
} particles_resources = { 0 };

cparticle_t *active_particles, *free_particles;
cparticle_t particles[MAX_PARTICLES];
int cl_numparticles = MAX_PARTICLES;

void R_ClearParticles(void) {
    free_particles = &particles[0];
    active_particles = NULL;
    FOR_LOOP(i, cl_numparticles) {
        particles[i].next = &particles[i+1];
    }
    particles[cl_numparticles-1].next = NULL;
}

cparticle_t *R_SpawnParticle(void) {
    if (!free_particles)
//        return NULL;
        return NULL;
    cparticle_t *p = free_particles;
    free_particles = p->next;
    p->next = active_particles;
    active_particles = p;
    return p;
}

LPCSTR vs_particle =
"#version 140\n"
"in vec3 i_position;\n"
"in vec4 i_color;\n"
"in vec2 i_texcoord;\n"
"in float i_size;\n"
"in vec2 i_axis;\n"
"out vec4 v_color;\n"
"out vec2 v_texcoord;\n"
"uniform mat4 uViewProjectionMatrix;\n"
"uniform mat4 uModelMatrix;\n"
"void main() {\n"
"    mat4 m = uViewProjectionMatrix;\n"
"    vec3 left = normalize(vec3(m[0][0], m[1][0], m[2][0])) * i_size;\n"
"    vec3 up = normalize(vec3(m[0][1], m[1][1], m[2][1])) * i_size;\n"
"    mat3 bb_mat = mat3(left, up, i_position);\n"
"    vec3 pos = bb_mat * vec3(i_axis - vec2(0.5), 1.0);\n"
"    gl_Position = uViewProjectionMatrix * vec4(pos, 1.0);\n"
"    v_color = i_color;\n"
"    v_texcoord = i_texcoord;\n"
"}\n";

LPCSTR fs_particle =
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
"}\n";

particleVertex_t *
R_AddParticle(particleVertex_t *buffer,
              LPCVECTOR3 point,
              COLOR32 uvr,
              COLOR32 color,
              float size)
{
    BYTE a = 0x00, b = 0xff;
    LPBYTE uv = (LPBYTE)&uvr;
    particleVertex_t const data[NUM_PARTICLE_VERTICES] = {
        { .position = *point, .uv = {uv[0],uv[1]}, .axis = {a,a}, .color = color, .size = size },
        { .position = *point, .uv = {uv[2],uv[1]}, .axis = {b,a}, .color = color, .size = size },
        { .position = *point, .uv = {uv[2],uv[3]}, .axis = {b,b}, .color = color, .size = size },
        { .position = *point, .uv = {uv[2],uv[3]}, .axis = {b,b}, .color = color, .size = size },
        { .position = *point, .uv = {uv[0],uv[3]}, .axis = {a,b}, .color = color, .size = size },
        { .position = *point, .uv = {uv[0],uv[1]}, .axis = {a,a}, .color = color, .size = size },
    };
    memcpy(buffer, data, sizeof(data));
    return buffer + NUM_PARTICLE_VERTICES;
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

COLOR32 FX_LerpColor(COLOR32 a, COLOR32 b, float t) {
    return (COLOR32) {
        .r = LerpNumber(a.r, b.r, t),
        .g = LerpNumber(a.g, b.g, t),
        .b = LerpNumber(a.b, b.b, t),
        .a = LerpNumber(a.a, b.a, t),
    };
}

float FX_BlendFloat(BYTE const *values, float k, float midtime) {
    if (k > midtime) {
        return LerpNumber(values[1], values[2], (k - midtime) / (1 - midtime));
    } else {
        return LerpNumber(values[0], values[1], k / midtime);
    }
}

COLOR32 FX_BlendColor(cparticle_t const *p) {
    float k = p->time / p->lifespan;
    float t = (float)p->midtime / (float)0xff;
    if (k > t) {
        return FX_LerpColor(p->color[1], p->color[2], (k - t) / (1 - t));
    } else {
        return FX_LerpColor(p->color[0], p->color[1], k / t);
    }
}

static void R_FlushParticles(LPCTEXTURE texture, LPCMATRIX4 matrix, particleVertex_t *pv) {
    R_Call(glBindVertexArray, particles_resources.particles->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, particles_resources.particles->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(particleVertex_t) * (pv - particles_resources.vertices), particles_resources.vertices, GL_STATIC_DRAW);
    R_Call(glUseProgram, particles_resources.shader->progid);
    R_Call(glUniformMatrix4fv, particles_resources.shader->uModelMatrix, 1, GL_FALSE, matrix->v);
    R_Call(glUniformMatrix4fv, particles_resources.shader->uViewProjectionMatrix, 1, GL_FALSE, tr.viewDef.viewProjectionMatrix.v);
    R_Call(glActiveTexture, GL_TEXTURE0);
    R_Call(glBindTexture, GL_TEXTURE_2D, (texture?texture:particles_resources.texture)->texid);
    R_Call(glDepthMask, GL_FALSE);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, (GLsizei)(pv - particles_resources.vertices));
}

static COLOR32 FX_GetFrame(const cparticle_t *p) {
    DWORD frame = (tr.viewDef.time / 100) % (p->columns * p->rows);
    DWORD u = frame % p->columns;
    DWORD v = frame / p->rows;
    DWORD usize = 256 / p->columns;
    DWORD vsize = 256 / p->rows;
    return (COLOR32) {
        usize * u,
        vsize * v,
        usize * (u + 1) - 1,
        vsize * (v + 1) - 1,
    };
}

void R_DrawParticles(void) {
    if (!active_particles)
        return;

    MATRIX4 matrix;
    LPCTEXTURE texture = active_particles->texture;
    particleVertex_t *pv = particles_resources.vertices;
    
    Matrix4_identity(&matrix);
    R_UpdateParticles();
    
    FOR_EACH_LIST(cparticle_t const, p, active_particles) {
        if (p->texture != texture) {
            R_FlushParticles(texture, &matrix, pv);
            pv = particles_resources.vertices;
        }
        VECTOR3 vel = Vector3_mad(&p->vel, p->time, &p->accel);
        VECTOR3 org = Vector3_mad(&p->org, p->time, &vel);
        COLOR32 col = FX_BlendColor(p);
        float size = FX_BlendFloat(p->size, p->time, BYTE2FLOAT(p->midtime));
        pv = R_AddParticle(pv, &org, FX_GetFrame(p), col, size * 2.0);
        texture = p->texture;
    }
    
    R_FlushParticles(texture, &matrix, pv);
}

static LPBUFFER R_MakeParticlesVertexArrayObject(void) {
    LPBUFFER buf = ri.MemAlloc(sizeof(BUFFER));

    R_Call(glGenVertexArrays, 1, &buf->vao);
    R_Call(glGenBuffers, 1, &buf->vbo);
    R_Call(glBindVertexArray, buf->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, buf->vbo);

    R_Call(glEnableVertexAttribArray, attrib_position);
    R_Call(glEnableVertexAttribArray, attrib_color);
    R_Call(glEnableVertexAttribArray, attrib_texcoord);
    R_Call(glEnableVertexAttribArray, attrib_particleSize);
    R_Call(glEnableVertexAttribArray, attrib_particleAxis);
    
    R_Call(glVertexAttribPointer, attrib_position, 3, GL_FLOAT, GL_FALSE, sizeof(struct particle_vertex), FOFS(particle_vertex, position));
    R_Call(glVertexAttribPointer, attrib_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct particle_vertex), FOFS(particle_vertex, color));
    R_Call(glVertexAttribPointer, attrib_texcoord, 2, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct particle_vertex), FOFS(particle_vertex, uv));
    R_Call(glVertexAttribPointer, attrib_particleSize, 1, GL_FLOAT, GL_FALSE, sizeof(struct particle_vertex), FOFS(particle_vertex, size));
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
    COLOR32 data[DOT_TEXTURE][DOT_TEXTURE];
    FOR_LOOP(x, DOT_TEXTURE) FOR_LOOP(y, DOT_TEXTURE) {
        data[x][y].r = 0xff;
        data[x][y].g = 0xff;
        data[x][y].b = 0xff;
        data[x][y].a = dottexture[x][y] * 127;
    }
    
    particles_resources.texture = R_AllocateTexture(DOT_TEXTURE, DOT_TEXTURE);
    R_LoadTextureMipLevel(particles_resources.texture, 0, (LPCCOLOR32)data, DOT_TEXTURE, DOT_TEXTURE);

    particles_resources.shader = R_InitShader(vs_particle, fs_particle);
    particles_resources.particles = R_MakeParticlesVertexArrayObject();
    R_ClearParticles();
}

void R_ShutdownParticles(void) {
    R_ReleaseVertexArrayObject(particles_resources.particles);
    R_ReleaseShader(particles_resources.shader);
}
