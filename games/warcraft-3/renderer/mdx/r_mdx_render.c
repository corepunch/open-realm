#include "r_mdx.h"
#include "renderer/r_local.h"

//typedef enum {
//    vertexattr_position,
//    vertexattr_color,
//    vertexattr_texcoord,
//    vertexattr_normal,
//    vertexattr_skin1,
//    vertexattr_skin2,
//    vertexattr_boneWeight1,
//    vertexattr_boneWeight2,
//} mdxVertexAttribute_t;

static LPCSTR mdx_vs =
"#version 140\n"
"in vec3 i_position;\n"
"in vec4 i_color;\n"
"in vec2 i_texcoord;\n"
"in vec3 i_normal;\n"
"in vec4 i_skin1;\n"
"in vec4 i_boneWeight1;\n"
#if MAX_SKIN_BONES > 4
"in vec4 i_boneWeight2;\n"
"in vec4 i_skin2;\n"
#endif
"out vec4 v_color;\n"
#ifdef USE_SHADOWMAPS
"out vec4 v_shadow;\n"
#endif
"out vec2 v_texcoord;\n"
"out vec2 v_texcoord2;\n"
"out vec3 v_lighting;\n"
"out float v_fogDist;\n"
"uniform mat4 uBones[128];\n"
"uniform mat4 uMdxLights[8];\n"
"uniform mat4 uViewProjectionMatrix;\n"
"uniform mat4 uTextureMatrix;\n"
"uniform mat4 uModelMatrix;\n"
"uniform mat4 uLightMatrix;\n"
"uniform mat3 uNormalMatrix;\n"
"uniform int uMdxLightCount;\n"
"uniform vec2 uMdxFallbackLighting;\n"
"uniform float uMdxLightFill;\n"
"const int MDX_LIGHT_OMNI = 0;\n"
"const int MDX_LIGHT_DIRECT = 1;\n"
"const int MDX_LIGHT_AMBIENT = 2;\n"
"vec3 get_vertex_lighting(vec3 normal, vec3 worldPosition) {\n"
"    if (uMdxLightCount == 0) {\n"
"        vec3 fallbackDir = -normalize(vec3(uLightMatrix[0][2], uLightMatrix[1][2], uLightMatrix[2][2]));\n"
"        return vec3(uMdxFallbackLighting.x) + vec3(uMdxFallbackLighting.y) * max(dot(normal, fallbackDir), 0.0);\n"
"    }\n"
"    vec3 lighting = vec3(uMdxLightFill);\n"
"    for (int i = 0; i < 8; ++i) {\n"
"        if (i >= uMdxLightCount) break;\n"
"        mat4 light = uMdxLights[i];\n"
"        vec4 positionType = light[0];\n"
"        vec4 directionAtten = light[1];\n"
"        vec4 colorIntensity = light[2];\n"
"        vec4 ambientIntensity = light[3];\n"
"        int lightType = int(positionType.w + 0.5);\n"
"        vec3 color = colorIntensity.rgb * colorIntensity.a;\n"
"        vec3 ambient = ambientIntensity.rgb * ambientIntensity.a;\n"
"        if (lightType == MDX_LIGHT_AMBIENT) {\n"
"            lighting += color + ambient;\n"
"        } else if (lightType == MDX_LIGHT_DIRECT) {\n"
"            vec3 dir = normalize(-directionAtten.xyz);\n"
"            lighting += clamp(color * max(dot(normal, dir), 0.0), vec3(0.0), vec3(1.0)) + ambient;\n"
"        } else {\n"
"            vec3 delta = positionType.xyz - worldPosition;\n"
"            vec3 dir = normalize(delta);\n"
"            float dist = length(delta) / 64.0 + 1.0;\n"
"            float atten = 1.0 / (dist * dist);\n"
"            lighting += clamp(color * atten * max(dot(normal, dir), 0.0), vec3(0.0), vec3(1.0));\n"
"            lighting += ambient * atten;\n"
"        }\n"
"    }\n"
"    return min(lighting, vec3(1.0));\n"
"}\n"
"void main() {\n"
"    vec4 pos4 = vec4(i_position, 1.0);\n"
"    vec4 norm4 = vec4(i_normal, 0.0);\n"
"    vec4 position = vec4(0.0);\n"
"    vec4 normal = vec4(0.0);\n"
"    for (int i = 0; i < 4; ++i) {\n"
"        position += uBones[int(i_skin1[i])] * pos4 * i_boneWeight1[i];\n"
"        normal += uBones[int(i_skin1[i])] * norm4 * i_boneWeight1[i];\n"
#if MAX_SKIN_BONES > 4
"        position += uBones[int(i_skin2[i])] * pos4 * i_boneWeight2[i];\n"
"        normal += uBones[int(i_skin2[i])] * norm4 * i_boneWeight2[i];\n"
#endif
"    }\n"
"    position.w = 1.0;\n"
"    v_color = i_color;\n"
"    v_texcoord = i_texcoord;\n"
"    v_texcoord2 = (uTextureMatrix * uModelMatrix * position).xy;\n"
"    vec3 worldNormal = normalize(uNormalMatrix * normal.xyz);\n"
"    vec3 worldPosition = (uModelMatrix * position).xyz;\n"
"    v_lighting = get_vertex_lighting(worldNormal, worldPosition);\n"
#ifdef USE_SHADOWMAPS
"    v_shadow = uLightMatrix * uModelMatrix * position;\n"
#endif
"    gl_Position = uViewProjectionMatrix * uModelMatrix * position;\n"
"    v_fogDist = gl_Position.w;\n"
"}\n";

static LPCSTR mdx_fs =
"#version 140\n"
"in vec2 v_texcoord;\n"
"in vec2 v_texcoord2;\n"
#ifdef USE_SHADOWMAPS
"in vec4 v_shadow;\n"
#endif
"in vec3 v_lighting;\n"
"in float v_fogDist;\n"
"out vec4 o_color;\n"
"uniform sampler2D uTexture;\n"
"uniform bool uFogEnable;\n"
"uniform vec3 uFogColor;\n"
"uniform vec2 uFogParams;\n" // x = fog start, y = fog end (GL_LINEAR)

#ifdef USE_SHADOWMAPS
"uniform sampler2D uShadowmap;\n"
#endif
"uniform sampler2D uFogOfWar;\n"
"uniform mat4 uLightMatrix;\n"
"uniform bool uUseDiscard;\n"
"uniform bool uUnshaded;\n"
"uniform float uLayerAlpha;\n"
"uniform vec4 uGeosetColor;\n"
"uniform vec2 uUvTrans;\n"
"uniform vec2 uUvRot;\n"
"uniform vec2 uUvScale;\n"
"vec2 quat_transform(vec2 q, vec2 v) {\n"
"    float c = q.y * q.y - q.x * q.x;\n"
"    float s = 2.0 * q.x * q.y;\n"
"    return vec2(v.x * c - v.y * s, v.x * s + v.y * c);\n"
"}\n"
"float get_fogofwar() {\n"
"    return texture(uFogOfWar, v_texcoord2).r;\n"
"}\n"
"void main() {\n"
"    vec2 uv = v_texcoord;\n"
"    uv += uUvTrans;\n"
"    uv = quat_transform(uUvRot, uv - 0.5) + 0.5;\n"
"    uv = uUvScale * (uv - 0.5) + 0.5;\n"
"    vec4 col = texture(uTexture, uv);\n"
"    col *= uGeosetColor;\n"
"    col *= uLayerAlpha;\n"
"    if (!uUnshaded) {\n"
"        col.rgb *= get_fogofwar() * v_lighting;\n"
"    }\n"
"    o_color = col;\n"
"    if (uFogEnable) {\n"
"        float f = clamp((uFogParams.y - v_fogDist) / (uFogParams.y - uFogParams.x), 0.0, 1.0);\n"
"        o_color.rgb = mix(uFogColor, o_color.rgb, f);\n"
"    }\n"
"    if (o_color.a < 0.5 && uUseDiscard) discard;\n"
"}\n";

static bool R_InitUIModelView(LPCMODEL model,
                              viewDef_t *viewdef,
                              renderEntity_t *entity,
                              mdxSequence_t const *seq,
                              FLOAT frame_ratio) {
    if (!model || !model->mdx) {
        return false;
    }

    memset(entity, 0, sizeof(*entity));
    memset(viewdef, 0, sizeof(*viewdef));

    /* UI/glue scenes (e.g. the main-menu background) animate and emit
     * particles continuously, so they need a real, advancing clock.  Without
     * this viewdef->time/deltaTime stay 0 and particle emitters spawn nothing
     * (no snow, no light-ray flares) and time-driven sequences freeze. */
    {
        static DWORD ui_lastTime = 0;
        DWORD now = SDL_GetTicks();
        DWORD delta = (ui_lastTime && now > ui_lastTime) ? now - ui_lastTime : 16;
        if (delta > 100) {
            delta = 100;
        }
        ui_lastTime = now;
        viewdef->time = now;
        viewdef->deltaTime = delta;
    }

    entity->scale = 1;
    entity->model = model;
    if (!seq) {
        return false;
    }

    if (frame_ratio >= 0.0f) {
        DWORD seq_len = seq->interval[1] - seq->interval[0];

        if (seq_len == 0) {
            seq_len = 1;
        }
        if (frame_ratio > 1.0f) {
            frame_ratio = 1.0f;
        }
        entity->frame = seq->interval[0] + (DWORD)((FLOAT)(seq_len - 1) * frame_ratio);
        if (entity->frame >= seq->interval[1]) {
            entity->frame = seq->interval[1] - 1;
        }
        entity->oldframe = entity->frame;
    } else {
        DWORD seq_len = seq->interval[1] - seq->interval[0];
        if (seq_len == 0) {
            seq_len = 1;
        }
        entity->frame = seq->interval[0] + (viewdef->time % seq_len);
        entity->oldframe = entity->frame;
    }

    viewdef->scissor = (RECT) { 0, 0, 1, 1 };
    viewdef->num_entities = 1;
    viewdef->entities = entity;
    viewdef->rdflags |= RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL;

    return true;
}

void Matrix4_fromViewAngles(LPCVECTOR3 target, LPCVECTOR3 angles, float distance, LPMATRIX4 output) {
    VECTOR3 const vieworg = Vector3_unm(target);
    Matrix4_identity(output);
    Matrix4_translate(output, &(VECTOR3){0, 0, -distance});
    Matrix4_rotate(output, angles, ROTATE_ZYX);
    Matrix4_translate(output, &vieworg);
}

void Matrix4_getLightMatrix(LPCVECTOR3 sunangles, LPCVECTOR3 target, float scale, LPMATRIX4 output) {
    MATRIX4 proj, view;
    Matrix4_ortho(&proj, -scale, scale, -scale, scale, 100.0, 3500.0);
    Matrix4_fromViewAngles(target, sunangles, 1000, &view);
    Matrix4_multiply(&proj, &view, output);
}

static bool
R_GetModelCameraMatrix(mdxModel_t const *model, DWORD frame, float aspect, LPMATRIX4 output, LPVECTOR3 root)
{
    if (!model || !model->cameras) {
        return false;
    }

    mdxCamera_t const *camera = model->cameras;
    MATRIX4 projection, view;
    VECTOR3 eye = camera->pivot;
    VECTOR3 target = camera->targetPivot;
    VECTOR3 dir;
    VECTOR3 up = {0, 0, 1};
    float fov_deg = camera->fieldOfView * (180.0f / (float)M_PI);
    float near_clip = camera->nearClip;
    float far_clip = camera->farClip;
    float roll = 0.0f;

    if (!isfinite(fov_deg) || fov_deg <= 1.0f || fov_deg >= 179.0f) {
        fov_deg = 35.0f;
    }
    if (!isfinite(near_clip) || near_clip < 0.01f) {
        near_clip = 1.0f;
    }
    if (!isfinite(far_clip) || far_clip <= near_clip + 1.0f) {
        far_clip = near_clip + 5000.0f;
    }
    if (!isfinite(aspect) || aspect <= 0.0f) {
        aspect = 1.0f;
    }
    if (camera->translation) {
        VECTOR3 translation = {0, 0, 0};
        MDLX_GetModelKeytrackValue(model, camera->translation, frame, &translation);
        eye = Vector3_add(&eye, &translation);
    }
    if (camera->targetTranslation) {
        VECTOR3 targetTranslation = {0, 0, 0};
        MDLX_GetModelKeytrackValue(model, camera->targetTranslation, frame, &targetTranslation);
        target = Vector3_add(&target, &targetTranslation);
    }
    dir = Vector3_sub(&target, &eye);
    if (Vector3_len(&dir) < 0.001f) {
        return false;
    }
    if (camera->roll) {
        MDLX_GetModelKeytrackValue(model, camera->roll, frame, &roll);
        if (isfinite(roll) && fabsf(roll) > 0.0001f) {
            up = Vector3_rotateAroundAxis(&up, &dir, roll);
        }
    }
    float const camera_aspect = 1.66f;
    fov_deg = 2.0f * atanf(tanf(fov_deg * (float)M_PI / 360.0f) / camera_aspect) * 180.0f / (float)M_PI;
    Matrix4_perspective(&projection, fov_deg, aspect, near_clip, far_clip);
    Matrix4_lookAt(&view, &eye, &dir, &up);
    Matrix4_multiply(&projection, &view, output);
    *root = target;
    return true;
}

static mdxSequence_t const *R_SelectUISequence(mdxModel_t const *mdx, LPCSTR anim) {
    mdxSequence_t const *seq = NULL;
    LPCSTR sequence = anim;

    if (!mdx) {
        return NULL;
    }
    if (sequence && sequence[0] == '#' && sequence[1] == '!') {
        sequence += 2;
    } else if (sequence && sequence[0] == '#') {
        sequence++;
    }
    if (anim && anim[0] == '#') {
        char *end = NULL;
        unsigned long index = strtoul(sequence, &end, 10);
        if (end && (*end == '\0' || *end == '@') && index < (unsigned long)mdx->num_sequences) {
            seq = &mdx->sequences[index];
        }
    } else if (anim && *anim) {
        LPCSTR ratio = strchr(anim, '@');
        if (ratio) {
            char sequence_name[sizeof(mdxObjectName_t) + 1];
            size_t len = (size_t)(ratio - anim);

            if (len >= sizeof(sequence_name)) {
                len = sizeof(sequence_name) - 1;
            }
            memcpy(sequence_name, anim, len);
            sequence_name[len] = '\0';
            seq = MDLX_FindSequenceByName(mdx, sequence_name);
        } else {
            seq = MDLX_FindSequenceByName(mdx, anim);
        }
    }
    if (!seq && mdx->cameras && anim && (!strcmp(anim, "Stand") || !strcmp(anim, "Portrait"))) {
        FOR_LOOP(i, mdx->num_sequences) {
            LPCSTR name = mdx->sequences[i].name;
            size_t len = strlen("Portrait");
            if (!strncmp(name, "Portrait", len) && (name[len] == '\0' || name[len] == ' ' || name[len] == '-')) {
                seq = &mdx->sequences[i];
                break;
            }
        }
    }
    if (!seq && mdx->sequences && mdx->num_sequences > 0) {
        seq = &mdx->sequences[0];
    }
    return seq;
}

static FLOAT R_UISequenceFrameRatio(LPCSTR anim) {
    LPCSTR ratio;
    char *end = NULL;
    FLOAT value;

    if (!anim) {
        return -1.0f;
    }
    ratio = strchr(anim, '@');
    if (!ratio) {
        return -1.0f;
    }
    value = strtof(ratio + 1, &end);
    if (end == ratio + 1) {
        return -1.0f;
    }
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

void MDLX_DrawPortrait(LPCMODEL model, LPCRECT viewport, LPCSTR anim) {
    VECTOR3 root;
    VECTOR3 lightAngles = { 10, 270, 0 };
    renderEntity_t entity;
    viewDef_t viewdef;
    
    if (!model || !model->mdx) {
        return;
    }
    
    mdxModel_t const *mdx = model->mdx;
    mdxSequence_t const *seq = R_SelectUISequence(mdx, anim);

    float viewport_width = viewport->w * tr.drawableSize.width;
    float viewport_height = viewport->h * tr.drawableSize.height;
    float aspect = viewport_height > 0.0f ? viewport_width / viewport_height : 1.0f;

    if (!R_InitUIModelView(model, &viewdef, &entity, seq, R_UISequenceFrameRatio(anim))) {
        return;
    }

    entity.flags |= RF_NO_FOGOFWAR | RF_NO_SHADOW | RF_PORTRAIT_LIGHTING;
    viewdef.viewport = *viewport;

    /* The full-screen glue scene (main-menu background) uses the original's
     * atmospheric haze, which it draws with fixed-function GL linear fog
     * (glFogi GL_FOG_MODE=LINEAR; glFogf START/END; glFogfv COLOR; see the
     * 1.29 client's GL state-apply routine).  We replicate it in-shader.
     * Unit portraits use a small viewport and stay fog-free. */
    if (viewport->x == 0 && viewport->y == 0 &&
        viewport->w >= 1.0f && viewport->h >= 1.0f) {
        viewdef.fogEnable = true;
        viewdef.fogColor = (VECTOR3){ 0.74f, 0.80f, 0.88f };
        viewdef.fogStart = 1400.0f;
        viewdef.fogEnd = 6500.0f;
    }

    if (!R_GetModelCameraMatrix(mdx, entity.frame, aspect, &viewdef.viewProjectionMatrix, &root)) {
        return;
    }

    Matrix4_getLightMatrix(&lightAngles, &root, PORTRAIT_SHADOW_SIZE, &viewdef.lightMatrix);

    R_Call(glActiveTexture, GL_TEXTURE2);
    R_Call(glBindTexture, GL_TEXTURE_2D, tr.texture[TEX_WHITE]->texid);
    R_Call(glActiveTexture, GL_TEXTURE0);

    tr.viewDef = viewdef;

#ifdef USE_SHADOWMAPS
    R_RenderShadowMap();
#endif
    R_RenderView();
}

void MDLX_DrawSprite(LPCMODEL model, LPCSTR anim, float x, float y) {
    renderEntity_t entity;
    viewDef_t viewdef;
    bool const fdf_sprite_coords = anim && anim[0] == '#' && anim[1] == '!';

    if (!model || !model->mdx) {
        return;
    }
    mdxModel_t const *mdx = model->mdx;
    mdxSequence_t const *seq = R_SelectUISequence(mdx, anim);

    if (!R_InitUIModelView(model, &viewdef, &entity, seq, R_UISequenceFrameRatio(anim))) {
        return;
    }

    viewdef.viewport = (struct rect) {0,0,1,1};

    entity.flags |= RF_NO_FOGOFWAR | RF_NO_SHADOW | RF_NO_LIGHTING;
    /* 2D sprite/panel pass: it must not touch the shared 3D particle pool,
     * or particles get aged once per UI element drawn (running the scene
     * several times too fast) and drawn with this ortho projection. */
    viewdef.rdflags |= RDF_NOPARTICLES;

    RECT screen = R_UISceneRect();
    entity.origin = fdf_sprite_coords
        ? (VECTOR3){x, y, 0}
        : (VECTOR3){x, screen.y + screen.h - y, 0};
    Matrix4_ortho(&viewdef.viewProjectionMatrix, screen.x, screen.x + screen.w, screen.y, screen.y + screen.h, 0.0f, 100.0f);
    Matrix4_scale(&viewdef.viewProjectionMatrix, &(VECTOR3){1, 1, 0});

    tr.viewDef = viewdef;

#ifdef USE_SHADOWMAPS
    R_RenderShadowMap();
#endif
    R_RenderView();
}

void MDLX_Init(void) {
    mdlx.shader = R_InitShader(mdx_vs, mdx_fs);
}

void MDLX_Shutdown(void) {
    R_ReleaseShader(mdlx.shader);
}
