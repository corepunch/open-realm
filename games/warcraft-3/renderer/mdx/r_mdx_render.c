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


static void Matrix4_fromViewAngles(LPCVECTOR3 target, LPCVECTOR3 angles, float distance, LPMATRIX4 output) {
    VECTOR3 const vieworg = Vector3_unm(target);
    Matrix4_identity(output);
    Matrix4_translate(output, &(VECTOR3){0, 0, -distance});
    Matrix4_rotate(output, angles, ROTATE_ZYX);
    Matrix4_translate(output, &vieworg);
}

static void Matrix4_getLightMatrix(LPCVECTOR3 sunangles, LPCVECTOR3 target, float scale, LPMATRIX4 output) {
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
    if (!seq && mdx->cameras && anim &&
        (!strcmp(anim, "Stand") || !strcmp(anim, "Portrait") || !strcmp(anim, "Portrait Talk"))) {
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

bool MDLX_ExtractCamera(mdxModel_t const *model, DWORD frame, float aspect, LPMATRIX4 output, LPMATRIX4 light) {
    VECTOR3 root;
    VECTOR3 lightAngles = { 10, 270, 0 };
    bool ok = R_GetModelCameraMatrix(model, frame, aspect, output, &root);
    if (ok && light) {
        Matrix4_getLightMatrix(&lightAngles, &root, PORTRAIT_SHADOW_SIZE, light);
    }
    return ok;
}

bool MDLX_SetEntityAnimationFrame(LPCMODEL model, LPCSTR anim, renderEntity_t *entity) {
    if (!model || !model->mdx || !entity) {
        return false;
    }
    mdxModel_t const *mdx = model->mdx;
    mdxSequence_t const *seq = R_SelectUISequence(mdx, anim);
    if (!seq) {
        return false;
    }
    DWORD seq_len = seq->interval[1] - seq->interval[0];
    if (seq_len == 0) {
        seq_len = 1;
    }
    /* Use the viewDef time when available.  UI callers (glue scene, portraits)
     * zero-initialise their viewDef and call SetEntityAnimFrame *before*
     * RenderFrame, so tr.viewDef.time is still 0.  Fall back to the wall clock
     * to keep menu/model animation advancing. */
    DWORD anim_time = tr.viewDef.time;
    if (anim_time == 0) {
        anim_time = SDL_GetTicks();
    }
    entity->frame = seq->interval[0] + (anim_time % seq_len);
    entity->oldframe = entity->frame;
    return true;
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

    if (!model || !model->mdx || !seq) {
        return;
    }

    memset(&entity, 0, sizeof(entity));
    memset(&viewdef, 0, sizeof(viewdef));
    entity.scale = 1;
    entity.model = model;
    DWORD seq_len = seq->interval[1] - seq->interval[0];
    if (seq_len == 0) seq_len = 1;
    entity.frame = seq->interval[0] + (tr.viewDef.time % seq_len);
    entity.oldframe = entity.frame;
    viewdef.scissor = (RECT) { 0, 0, 1, 1 };
    viewdef.num_entities = 1;
    viewdef.entities = &entity;
    viewdef.rdflags |= RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL;
    viewdef.viewport = (struct rect) {0,0,1,1};

    entity.flags |= RF_NO_FOGOFWAR | RF_NO_SHADOW | RF_NO_LIGHTING;

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
    mdlx.shader = R_ModelShader();
}

void MDLX_Shutdown(void) {
    /* mdlx.shader is the shared model shader, released by the renderer. */
}
