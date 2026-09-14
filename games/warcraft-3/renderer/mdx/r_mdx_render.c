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

static BOOL R_UIAnimationRatio(LPCSTR anim, LPFLOAT ratio) {
    LPCSTR marker;
    char *end = NULL;
    FLOAT value;

    if (!anim || !ratio) return false;
    marker = strrchr(anim, '@');
    if (!marker || !marker[1]) return false;
    value = strtof(marker + 1, &end);
    if (end == marker + 1 || !end || *end || !isfinite(value)) return false;
    *ratio = MAX(0.0f, MIN(value, 1.0f));
    return true;
}

static DWORD R_UISequenceFrame(mdxSequence_t const *seq, LPCSTR anim, DWORD anim_time) {
    DWORD seq_len, offset;
    FLOAT ratio;

    if (!seq) return 0;
    seq_len = seq->interval[1] - seq->interval[0];
    if (seq_len == 0) seq_len = 1;
    if (!R_UIAnimationRatio(anim, &ratio))
        return seq->interval[0] + (anim_time % seq_len);

    offset = (DWORD)floorf(ratio * (FLOAT)seq_len);
    if (offset >= seq_len) offset = seq_len - 1;
    return seq->interval[0] + offset;
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
#ifdef WC3_DEBUG_GLUE
    if (!seq && anim && *anim && mdx->sequences && mdx->num_sequences > 0) {
        static char last_missing[sizeof(mdxObjectName_t) + 1];
        char missing[sizeof(last_missing)];
        LPCSTR ratio = strchr(anim, '@');
        size_t len = ratio ? (size_t)(ratio - anim) : strlen(anim);

        len = MIN(len, sizeof(missing) - 1);
        memcpy(missing, anim, len); missing[len] = '\0';
        if (strcmp(last_missing, missing)) {
            fprintf(stderr, "WC3 glue: missing sequence \"%s\"; falling back to sequence 0 \"%s\"\n",
                    missing, mdx->sequences[0].name);
            snprintf(last_missing, sizeof(last_missing), "%s", missing);
        }
    }
#endif
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
    /* Use the viewDef time when available. UI callers (glue scene, portraits)
     * zero-initialise their viewDef and call SetEntityAnimFrame before
     * RenderFrame, so tr.viewDef.time is still 0. Fall back to the wall clock
     * unless the animation string supplies an explicit @ratio. */
    DWORD anim_time = tr.viewDef.time;
    if (anim_time == 0) anim_time = SDL_GetTicks();
    entity->frame = R_UISequenceFrame(seq, anim, anim_time);
    entity->oldframe = entity->frame;
    return true;
}

/* State copies own only their runtime list and accumulator, never shared model keytracks. */
static void MDLX_FreeSprite(mdxSprite_t *state) {
    R_ClearParticleScene(&state->particles);
    while (state->emitters) {
        mdxParticleEmitter_t *emitter = state->emitters;
        state->emitters = emitter->next; ri.MemFree(emitter);
    }
    ri.MemFree(state);
}

/* Runtime particle state belongs to the UI instance, even when models and keytracks are shared. */
static mdxSprite_t *MDLX_SpriteState(mdxModel_t *model, drawSprite_t const *sprite) {
    void const *id = sprite->id ? sprite->id : model;
    mdxSprite_t *state = NULL, **link = &model->sprites;
    while (*link) {
        mdxSprite_t *item = *link;
        if (item->id == id && item->scope == sprite->scope) state = item;
        else if (item->time + tr.viewDef.deltaTime < tr.viewDef.time) {
            *link = item->next; MDLX_FreeSprite(item); continue;
        }
        link = &item->next;
    }
    if (!state) {
        mdxParticleEmitter_t **link;
        state = ri.MemAlloc(sizeof(*state));
        *state = (mdxSprite_t){ .id = id, .scope = sprite->scope, .next = model->sprites };
        model->sprites = state; link = &state->emitters;
        FOR_EACH_LIST(mdxParticleEmitter_t, src, model->emitters) {
            *link = ri.MemAlloc(sizeof(**link)); **link = *src;
            (*link)->accumulator = 0; (*link)->next = NULL; link = &(*link)->next;
        }
    }
    if (state->time + tr.viewDef.deltaTime < tr.viewDef.time) {
        R_ClearParticleScene(&state->particles);
        FOR_EACH_LIST(mdxParticleEmitter_t, emitter, state->emitters) emitter->accumulator = 0;
    }
    state->time = tr.viewDef.time;
    return state;
}

/* Particle lists must be returned before the model and its shared emitter tracks disappear. */
void MDLX_ReleaseSprites(mdxModel_t *model) {
    while (model->sprites) {
        mdxSprite_t *state = model->sprites;
        model->sprites = state->next;
        MDLX_FreeSprite(state);
    }
}

void MDLX_DrawSpriteInstance(drawSprite_t const *sprite, COLOR32 tint) {
    LPCMODEL model = sprite->model;
    LPCSTR anim = sprite->anim;
    FLOAT x = sprite->x, y = sprite->y;
    renderEntity_t entity;
    viewDef_t viewdef;
    viewDef_t saved_viewdef;
    bool const fdf_sprite_coords = anim && anim[0] == '#' && anim[1] == '!';

    if (!model || !model->mdx) {
        return;
    }
    mdxModel_t *mdx = model->mdx;
    mdxSequence_t const *seq = R_SelectUISequence(mdx, anim);

    if (!model || !model->mdx || !seq) {
        return;
    }

    memset(&entity, 0, sizeof(entity));
    memset(&viewdef, 0, sizeof(viewdef));
    entity.scale = 1;
    entity.model = model;
    entity.tint = tint.a ? tint : COLOR32_WHITE;
    entity.frame = R_UISequenceFrame(seq, anim, tr.viewDef.time);
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

    saved_viewdef = tr.viewDef;
    viewdef.time = saved_viewdef.time;
    viewdef.deltaTime = saved_viewdef.deltaTime;
    viewdef.camerastate[0].eye = (VECTOR3){0, 0, 100};
    particleScene_t empty = {0};
    mdxSprite_t *state = mdx->emitters ? MDLX_SpriteState(mdx, sprite) : NULL;
    particleScene_t *scene = state ? &state->particles : &empty;
    mdxParticleEmitter_t *emitters = mdx->emitters;
    cparticle_t *previous = R_BeginParticleScene(scene);
    if (state) mdx->emitters = state->emitters;
    tr.viewDef = viewdef;

#ifdef USE_SHADOWMAPS
    R_RenderShadowMap();
#endif
    R_RenderView();
    mdx->emitters = emitters;
    R_EndParticleScene(scene, previous);
    tr.viewDef = saved_viewdef;
}

void MDLX_DrawSpriteTinted(LPCMODEL model, LPCSTR anim, float x, float y, COLOR32 tint) {
    MDLX_DrawSpriteInstance(&MAKE(drawSprite_t, .model = model, .anim = anim, .x = x, .y = y, .id = model), tint);
}

void MDLX_DrawSprite(LPCMODEL model, LPCSTR anim, float x, float y) {
    MDLX_DrawSpriteTinted(model, anim, x, y, COLOR32_WHITE);
}

void MDLX_Init(void) {
    mdlx.shader = R_ModelShader();
}

void MDLX_Shutdown(void) {
    /* mdlx.shader is the shared model shader, released by the renderer. */
}
