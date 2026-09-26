#include "r_local.h"
#include "r_game.h"
#include "r_shader.h"

#define SIGHT_SIZE 64
#define SIGHT_DISTANCE 2000
#define FOW_UPDATE_INTERVAL_MS 100
#define MAX_FOGOFWAR_CASTERS 20000
#define MAX_FOGOFWAR_REVEALERS MAX_FOGOFWAR_CASTERS
#define NUM_SIGHT_SECIONS 5

typedef struct fowraycaststate_s {
    matrix4_t viewProjection;
    matrix4_t model;
    vector2_t eyePosition;
} fowRaycastState_t;


typedef struct fowraycastprog_s {
    shaderProg_t prog;
    fowRaycastState_t state;
} fowRaycastProg_t;



#define SHADER_TYPE fowRaycastState_t
static const shader_desc_t sd_fow_raycast = {
    .Name = "fow_raycast",
    .Uniforms = {
        UNIFORM(viewProjection, UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(model,          UT_FLOAT_MAT4, PRECISION_HIGH),
        UNIFORM(eyePosition,    UT_FLOAT_VEC2, PRECISION_LOW),
    },
    .Attributes = {
        ATTRIB(position, attrib_position, UT_FLOAT_VEC3),
        ATTRIB(color,    attrib_color,    UT_COLOR),
        ATTRIB(texcoord, attrib_texcoord, UT_FLOAT_VEC2),
    },
    .Shared = {
        SHARED(color,    UT_COLOR),
        SHARED(texcoord, UT_FLOAT_VEC2),
    },
    .VertexBody =
        "vec4 vert() {\n"
        "  vec4 pos = vec4(a_position, 1.0);\n"
        "  vec3 eye = vec3(u_eyePosition, 0.0);\n"
        "  vec3 up = vec3(0.0, 0.0, 1.0);\n"
        "  vec3 dir = normalize(a_position - eye);\n"
        "  vec3 side = normalize(cross(dir, up));\n"
        "  if (distance(eye.xy, a_position.xy) > 100.0) {\n"
        "    float x = (a_texcoord.x - 0.5) * 2.0;\n"
        "    float y = a_texcoord.y;\n"
        "    pos.xy += side.xy * x * 100.0;\n"
        "    pos.xy += normalize(pos.xyz - eye).xy * y * 2000.0;\n"
        "  }\n"
        "  pos.z = 0.0;\n"
        "  v_color = a_color;\n"
        "  v_texcoord = a_texcoord;\n"
        "  return u_viewProjection * u_model * pos;\n"
        "}\n",
    .FragmentBody =
        "vec4 frag() {\n"
        "  float f = 2.0 * abs(v_texcoord.x - 0.5);\n"
        "  float k = smoothstep(0.0, 0.2, v_texcoord.y);\n"
        "  return vec4(mix(1.0, f*f, k));\n"
        "}\n",
};
#undef SHADER_TYPE

enum {
    FOW_RT_IMMEDIATE,
    FOW_RT_HISTORY,
    FOW_RT_RESULT,
    FOW_RT_COUNT,
};

static struct {
    fowRaycastProg_t shader;
    rendertarget_t * rt[FOW_RT_COUNT];
    buffer_t * casters;
    texture_t * sight;
    texture_t * network;
    uint8_t const *network_data;
    uint32_t network_generation;
    uint32_t last_update_time;
} fow_resources = { 0 };

typedef struct caster_vertex {
    struct { short x, y, z; } position;
    struct { uint8_t x, y; } texcoord;
} castervertex_t;

static castervertex_t casters[MAX_FOGOFWAR_CASTERS * NUM_SIGHT_SECIONS * NUM_RECT_VERTICES];
static renderEntity_t const *revealers[MAX_FOGOFWAR_REVEALERS];

texture_t * R_AllocateSightTexture(void) {
    texture_t * texture = R_AllocateTexture(SIGHT_SIZE, SIGHT_SIZE);
    color32_t col[SIGHT_SIZE * SIGHT_SIZE];
    uint32_t mid = SIGHT_SIZE/2;
    vector2_t center = {mid,mid};
    FOR_LOOP(x, SIGHT_SIZE) {
        FOR_LOOP(y, SIGHT_SIZE) {
            float const d = Vector2_distance(&center, &(vector2_t){x,y});
            float const f = MAX(0, 1.0 - d / mid);
            uint32_t c = MIN(1, f * 2.0) * 0xff;
            col[x+y*SIGHT_SIZE].r = 0xff;
            col[x+y*SIGHT_SIZE].g = 0xff;
            col[x+y*SIGHT_SIZE].b = 0xff;
            col[x+y*SIGHT_SIZE].a = c;
        }
    }
    R_LoadTextureMipLevel(texture, &(texMip_t){ col, SIGHT_SIZE, SIGHT_SIZE, 0, PIXEL_RGBA });
    return texture;
}

static void R_MakeSightMatrix(renderEntity_t const *ent, matrix4_t * model_matrix) {
    Matrix4_identity(model_matrix);
    Matrix4_translate(model_matrix, &(vector3_t) {
        ent->origin.x - tr.world->center.x - SIGHT_DISTANCE / 2,
        ent->origin.y - tr.world->center.y - SIGHT_DISTANCE / 2,
        0
    });
    Matrix4_scale(model_matrix, &(vector3_t) {
        SIGHT_DISTANCE,
        SIGHT_DISTANCE,
        SIGHT_DISTANCE,
    });

    tr.shader_ui.state.model = *model_matrix;
}

static uint32_t R_CollectRevealers(renderEntity_t const **out, uint32_t max_revealers) {
    uint32_t count = 0;

    FOR_LOOP(i, tr.viewDef.num_entities) {
        renderEntity_t const *ent = &tr.viewDef.entities[i];

        if (ent->team != tr.viewDef.player) {
            continue;
        }
        if (ent->flags & RF_HIDDEN) {
            continue;
        }
        if (!(ent->flags & RF_FOW_REVEALER)) {
            continue;
        }
        if (count >= max_revealers) {
            break;
        }
        out[count++] = ent;
    }
    return count;
}

static bool R_CasterNearRevealers(renderEntity_t const *caster,
                                  renderEntity_t const **revealer_list,
                                  uint32_t num_revealers)
{
    float const range = SIGHT_DISTANCE + caster->radius + 100.0f;
    float const range_sq = range * range;

    FOR_LOOP(i, num_revealers) {
        renderEntity_t const *revealer = revealer_list[i];
        float const dx = caster->origin.x - revealer->origin.x;
        float const dy = caster->origin.y - revealer->origin.y;

        if (caster == revealer) {
            continue;
        }
        if (dx * dx + dy * dy <= range_sq) {
            return true;
        }
    }
    return false;
}

static uint32_t R_AddCastersToBuffer(buffer_t const * buffer,
                                  renderEntity_t const **revealer_list,
                                  uint32_t num_revealers)
{
    castervertex_t *caster_writer = casters;
    castervertex_t *caster_end = casters + sizeof(casters) / sizeof(*casters);
    color32_t white = {255,255,255,255};
    vertex_t rect[NUM_RECT_VERTICES];

    FOR_LOOP(i, tr.viewDef.num_entities) {
        renderEntity_t *ent = &tr.viewDef.entities[i];

        if (!(ent->flags & RF_FOW_BLOCKER) || ent->radius < 10) {
            continue;
        }
        if (!R_CasterNearRevealers(ent, revealer_list, num_revealers)) {
            continue;
        }
        rect_t screen = { ent->origin.x, ent->origin.y, 0, 0, };
        FOR_LOOP(j, NUM_SIGHT_SECIONS) {
            if (caster_writer + NUM_RECT_VERTICES > caster_end) {
                goto upload;
            }
            rect_t uv = {((float)j)/NUM_SIGHT_SECIONS,0,1.0/NUM_SIGHT_SECIONS,1};
            vertex_t const * end = R_AddQuad(rect, &screen, &uv, white, ent->radius);
            for (vertex_t const * v = rect; v != end; v++) {
                caster_writer->position.x = v->position.x;
                caster_writer->position.y = v->position.y;
                caster_writer->position.z = v->position.z;
                caster_writer->texcoord.x = v->texcoord.x * 0xff;
                caster_writer->texcoord.y = v->texcoord.y * 0xff;
                caster_writer++;
            }
        }
    }

upload:
    R_Call(glBindVertexArray, buffer->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, buffer->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(castervertex_t) * (caster_writer - casters), casters, GL_DYNAMIC_DRAW);
    return (uint32_t)(caster_writer - casters);
}

static uint32_t R_PushRectToBuffer(uint32_t buffer_id, rect_t const * value, float alpha) {
    color32_t white = {255*alpha,255*alpha,255*alpha,255*alpha};
    rect_t uv = {0,0,1,1};
    vertex_t rect[NUM_RECT_VERTICES];
    R_AddQuad(rect, value, &uv, white, 0);
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(vertex_t) * NUM_RECT_VERTICES, rect, GL_DYNAMIC_DRAW);
    return NUM_RECT_VERTICES;
}

static void R_BlitTexture(GLuint texid, float alpha) {
    matrix4_t model_matrix;
    matrix4_t proj_matrix;
    rect_t const uv = {0,0,1,1};
    
    Matrix4_ortho(&proj_matrix, 0, 1, 0, 1, -1, 1);
    Matrix4_identity(&model_matrix);

    // Set simple projection

    tr.shader_ui.state.model = model_matrix;
    tr.shader_ui.state.viewProjection = proj_matrix;

    R_Call(glBindTexture, GL_TEXTURE_2D, texid);
    {
        uint32_t count = R_PushRectToBuffer(RBUF_TEMP1, &uv, alpha);
        R_StatsDraw(GL_TRIANGLES, count, 1);
        R_ApplyShader(&tr.shader_ui);
        R_Call(glDrawArrays, GL_TRIANGLES, 0, count);
    }
}

void R_RenderFogOfWar(void) {
    R_UpdateFogOfWarData();
    if (!R_CvarEnabled("r_fogofwar", "1")) return;
    if (fow_resources.network) {
        return;
    }
    if (!tr.world ||
        (tr.viewDef.rdflags & (RDF_NOFOG | RDF_NOWORLDMODEL)) ||
        !fow_resources.rt[FOW_RT_IMMEDIATE] ||
        !fow_resources.rt[FOW_RT_HISTORY] ||
        !fow_resources.rt[FOW_RT_RESULT])
    {
        return;
    }

    if (fow_resources.last_update_time &&
        tr.viewDef.time - fow_resources.last_update_time < FOW_UPDATE_INTERVAL_MS)
    {
        return;
    }
    fow_resources.last_update_time = tr.viewDef.time;
    
    uint32_t const texture_width = (tr.world->width - 1) * 4;
    uint32_t const texture_height = (tr.world->height - 1) * 4;
    uint32_t const num_revealers = R_CollectRevealers(revealers, MAX_FOGOFWAR_REVEALERS);

    matrix4_t model_matrix;
    matrix4_t proj_matrix;
    vector2_t mapsize = R_WorldSize();

    Matrix4_identity(&model_matrix);
    Matrix4_translate(&model_matrix, &(vector3_t) { -tr.world->center.x, -tr.world->center.y, 0 });
    Matrix4_ortho(&proj_matrix, 0.0f, mapsize.x, 0.0f, mapsize.y, 0.0f, 100.0f);

    R_PushRectToBuffer(RBUF_TEMP1, &(rect_t const){0,0,1,1}, 1);

    uint32_t num_casters = 0;
    if (num_revealers > 0) {
        num_casters = R_AddCastersToBuffer(fow_resources.casters, revealers, num_revealers);

        fow_resources.shader.state.viewProjection = proj_matrix;
        fow_resources.shader.state.model = model_matrix;
    }

    tr.shader_ui.state.viewProjection = proj_matrix;

    R_Call(glViewport, 0, 0, texture_width, texture_height);
    R_Call(glScissor, 0, 0, texture_width, texture_height);
    R_Call(glBindFramebuffer, GL_FRAMEBUFFER, fow_resources.rt[FOW_RT_IMMEDIATE]->buffer);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_Call(glDepthMask, GL_FALSE);
    R_Call(glDepthFunc, GL_ALWAYS);
    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glEnable, GL_BLEND);
    R_Call(glClearColor, 0, 0, 0, 0);
    R_Call(glClear, GL_COLOR_BUFFER_BIT);
    R_Call(glActiveTexture, GL_TEXTURE0);
    R_Call(glBindTexture, GL_TEXTURE_2D, fow_resources.sight->texid);

    FOR_LOOP(p, num_revealers) {
        renderEntity_t const *ent = revealers[p];
        
        R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        R_Call(glColorMask, GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
        R_Call(glClear, GL_COLOR_BUFFER_BIT);

        // Draw smooth circle into dst alpha
        R_MakeSightMatrix(ent, &model_matrix);
        R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
        R_Call(glBindTexture, GL_TEXTURE_2D, fow_resources.sight->texid);
        R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
        R_StatsDraw(GL_TRIANGLES, NUM_RECT_VERTICES, 1);
        R_ApplyShader(&tr.shader_ui);
        R_Call(glDrawArrays, GL_TRIANGLES, 0, NUM_RECT_VERTICES);

        // Draw line of sight into dst alpha

        memcpy(&fow_resources.shader.state.eyePosition, (GLfloat *)&ent->origin, (1) * sizeof(vector2_t));
        R_Call(glBindVertexArray, fow_resources.casters->vao);
        R_Call(glBlendFunc, GL_DST_ALPHA, GL_ZERO);
        R_Call(glBindBuffer, GL_ARRAY_BUFFER, fow_resources.casters->vbo);
        R_StatsDraw(GL_TRIANGLES, num_casters, 1);
        R_ApplyShader(&fow_resources.shader);
        R_Call(glDrawArrays, GL_TRIANGLES, 0, num_casters);
        
        // Draw white rect using dst alpha
        R_MakeSightMatrix(ent, &model_matrix);
        R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
        R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
        R_Call(glColorMask, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        R_Call(glBlendFunc, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA);
        R_Call(glBindTexture, GL_TEXTURE_2D, tr.texture[TEX_WHITE]->texid);
        R_StatsDraw(GL_TRIANGLES, NUM_RECT_VERTICES, 1);
        R_ApplyShader(&tr.shader_ui);
        R_Call(glDrawArrays, GL_TRIANGLES, 0, NUM_RECT_VERTICES);
    }
    
    Matrix4_ortho(&proj_matrix, 0, 1, 0, 1, -1, 1);
    Matrix4_identity(&model_matrix);

    // Set simple projection

    tr.shader_ui.state.model = model_matrix;
    tr.shader_ui.state.viewProjection = proj_matrix;

    // Add current state to history
    R_Call(glBlendEquation, GL_MAX);
    R_Call(glBlendFunc, GL_ONE, GL_ONE);
    R_Call(glBindFramebuffer, GL_FRAMEBUFFER, fow_resources.rt[FOW_RT_HISTORY]->buffer);
    R_BlitTexture(fow_resources.rt[FOW_RT_IMMEDIATE]->texture, 1.0);
    
    // revert blend func
    R_Call(glBlendEquation, GL_FUNC_ADD);
    R_Call(glBlendFunc, GL_ONE, GL_ZERO);
    R_Call(glBindFramebuffer, GL_FRAMEBUFFER, fow_resources.rt[FOW_RT_RESULT]->buffer);
    if (tr.viewDef.rdflags & RDF_NOFOGMASK) {
        R_BlitTexture(tr.texture[TEX_WHITE]->texid, 0.5);
    } else {
        R_BlitTexture(fow_resources.rt[FOW_RT_HISTORY]->texture, 0.5);
    }
    R_Call(glBlendFunc, GL_ONE, GL_ONE);
    R_BlitTexture(fow_resources.rt[FOW_RT_IMMEDIATE]->texture, 0.5);

    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // revert changes
    R_Call(glBindFramebuffer, GL_FRAMEBUFFER, 0);
}

buffer_t * R_MakeCastersVertexArrayObject(void) {
    buffer_t * buf = ri.MemAlloc(sizeof(buffer_t));

    R_Call(glGenVertexArrays, 1, &buf->vao);
    R_Call(glGenBuffers, 1, &buf->vbo);
    R_Call(glBindVertexArray, buf->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, buf->vbo);

    R_Call(glEnableVertexAttribArray, attrib_position);
    R_Call(glEnableVertexAttribArray, attrib_texcoord);

    R_Call(glVertexAttribPointer, attrib_position, 3, GL_SHORT, GL_FALSE, sizeof(struct caster_vertex), FOFS(caster_vertex, position));
    R_Call(glVertexAttribPointer, attrib_texcoord, 2, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct caster_vertex), FOFS(caster_vertex, texcoord));

    return buf;
}

void R_InitFogOfWar(uint32_t width, uint32_t height) {
    fow_resources.rt[FOW_RT_IMMEDIATE] = R_AllocateRenderTexture(width, height, GL_RGBA, GL_UNSIGNED_BYTE, GL_COLOR_ATTACHMENT0);
    fow_resources.rt[FOW_RT_HISTORY] = R_AllocateRenderTexture(width, height, GL_RGBA, GL_UNSIGNED_BYTE, GL_COLOR_ATTACHMENT0);
    fow_resources.rt[FOW_RT_RESULT] = R_AllocateRenderTexture(width, height, GL_RGBA, GL_UNSIGNED_BYTE, GL_COLOR_ATTACHMENT0);
    memset(&fow_resources.shader, 0, sizeof(fow_resources.shader));
    R_LoadShader(&sd_fow_raycast, NULL, &fow_resources.shader);
    fow_resources.casters = R_MakeCastersVertexArrayObject();
    fow_resources.sight = R_AllocateSightTexture();
    fow_resources.last_update_time = 0;
}

void R_ShutdownFogOfWar(void) {
    R_DeleteShader(&fow_resources.shader.prog);
    memset(&fow_resources.shader, 0, sizeof(fow_resources.shader));
    FOR_LOOP(i, FOW_RT_COUNT) {
        R_ReleaseRenderTexture(fow_resources.rt[i]);
        fow_resources.rt[i] = NULL;
    }
    R_ReleaseVertexArrayObject(fow_resources.casters);
    fow_resources.casters = NULL;
    R_ReleaseTexture(fow_resources.sight);
    fow_resources.sight = NULL;
    R_ReleaseTexture(fow_resources.network);
    fow_resources.network = NULL;
    fow_resources.last_update_time = 0;
}

uint32_t R_GetFogOfWarTexture(void) {
    if (fow_resources.network &&
        !(tr.viewDef.rdflags & (RDF_NOFOG | RDF_NOWORLDMODEL))) {
        return fow_resources.network->texid;
    }
    if (fow_resources.rt[FOW_RT_RESULT] &&
        !(tr.viewDef.rdflags & (RDF_NOFOG | RDF_NOWORLDMODEL))) {
        return fow_resources.rt[FOW_RT_RESULT]->texture;
    }
    return tr.texture[TEX_WHITE]->texid;
}

uint32_t R_GetMinimapFogOfWarTexture(void) {
    if (fow_resources.network) {
        return fow_resources.network->texid;
    }
    if (fow_resources.rt[FOW_RT_RESULT]) {
        return fow_resources.rt[FOW_RT_RESULT]->texture;
    }
    return tr.texture[TEX_WHITE]->texid;
}

void R_UpdateFogOfWarData(void) {
    uint32_t width = tr.viewDef.fow_width, height = tr.viewDef.fow_height;
    uint8_t const *data = tr.viewDef.fow_data;
    bool allocate;

    if (!width || !height || !data) {
        R_ReleaseTexture(fow_resources.network);
        fow_resources.network = NULL;
        fow_resources.network_data = NULL;
        fow_resources.network_generation = 0;
        return;
    }

    /* Client parsing may receive many row chunks; pointer+generation keeps one GPU upload per assembled update. */
    if (fow_resources.network && fow_resources.network_data == data &&
        fow_resources.network_generation == tr.viewDef.fow_generation) return;

    allocate = !fow_resources.network ||
               fow_resources.network->width != width ||
               fow_resources.network->height != height;
    if (allocate) {
        R_ReleaseTexture(fow_resources.network);
        fow_resources.network = R_AllocateTexture(width, height);
    }

    R_Call(glBindTexture, GL_TEXTURE_2D, fow_resources.network->texid);
    R_Call(glPixelStorei, GL_UNPACK_ALIGNMENT, 1);
    /* Existing storage must be updated, not redefined as the old per-FOW-chunk path did. */
    if (allocate) {
        R_Call(glTexImage2D, GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, data);
    } else {
        R_Call(glTexSubImage2D, GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, data);
    }
    R_Call(glPixelStorei, GL_UNPACK_ALIGNMENT, 4);
    if (allocate) {
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    fow_resources.network_data = data;
    fow_resources.network_generation = tr.viewDef.fow_generation;
}
