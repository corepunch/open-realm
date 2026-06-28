#include "r_mdx.h"
#include "renderer/r_local.h"
#include <stdlib.h>

#ifdef USE_SHADOWMAPS
extern bool is_rendering_lights;
#endif

#define MDX_SHADER_MAX_LIGHTS 8
#define MDLX_STACK_DRAW_ORDER 64

typedef struct mdxShaderLight_s {
    MATRIX4 matrix;
} mdxShaderLight_t;

#define GET_PARTICLE_ANIM_PARAM(MODEL, EMITTER, NAME) \
float NAME = EMITTER->NAME; \
if (EMITTER->keytracks.NAME) { \
    MDLX_GetModelKeytrackValue(MODEL, EMITTER->keytracks.NAME, frame, &NAME); \
}

static VECTOR3 FX_GenerateRandomDirection(float latitude) {
    float theta = (float)(((double)rand() / (double)RAND_MAX) * 2.0 * M_PI);
    float phi = (float)(((double)rand() / (double)RAND_MAX) * latitude);
    VECTOR3 direction = {
        .x = sin(phi) * cos(theta),
        .y = sin(phi) * sin(theta),
        .z = cos(phi),
    };
    return direction;
}

static VECTOR3 FX_GenerateRandomOrigin(float length, float width) {
    VECTOR3 origin = {
        .x = (float)(((double)rand() / (double)RAND_MAX - 0.5) * length),
        .y = (float)(((double)rand() / (double)RAND_MAX - 0.5) * width),
        .z = 0,
    };
    return origin;
}

static LPCTEXTURE MDLX_GetTexture(mdxModel_t const *model,
                                 DWORD teamID,
                                 DWORD textureID,
                                 DWORD replaceableID,
                                 LPCTEXTURE overrideTexture) {
    mdxTexture_t const *modeltex = &model->textures[textureID];
    switch (replaceableID) {
        case TEXREPL_TEAMCOLOR: return tr.texture[TEX_TEAM_COLOR + teamID];
        case TEXREPL_TEAMGLOW: return tr.texture[TEX_TEAM_GLOW + teamID];
        default:
            if (replaceableID != TEXREPL_NONE && overrideTexture) {
                return overrideTexture;
            }
            return R_FindTextureByID(modeltex->texid);
    }
}

static COLOR32 MDLX_GetEmitterColor(mdxParticleEmitter_t const *emitter, DWORD seg) {
    return (COLOR32) {
        emitter->SegmentColor[seg*3+0] * 0xff,
        emitter->SegmentColor[seg*3+1] * 0xff,
        emitter->SegmentColor[seg*3+2] * 0xff,
        emitter->Alpha[seg],
    };
}

static void MDLX_RenderEmitter(mdxModel_t const *model,
                              mdxParticleEmitter_t const *emitter,
                              LPCMATRIX4 modelMatrix,
                              float frame,
                              DWORD teamID)
{
    GET_PARTICLE_ANIM_PARAM(model, emitter, EmissionRate);
    GET_PARTICLE_ANIM_PARAM(model, emitter, Width);
    GET_PARTICLE_ANIM_PARAM(model, emitter, Length);
    GET_PARTICLE_ANIM_PARAM(model, emitter, Speed);
    GET_PARTICLE_ANIM_PARAM(model, emitter, Latitude);
    GET_PARTICLE_ANIM_PARAM(model, emitter, Gravity);
    GET_PARTICLE_ANIM_PARAM(model, emitter, Variation);
    
    if (EmissionRate <= 0)
        return;
    
    EmissionRate = MAX(1, EmissionRate);
    MATRIX4 matrix;
    if (emitter->node.node_id >= MDX_MAX_NODES) {
        return;
    }
    LPCMATRIX4 nodeMatrix = &node_matrices[emitter->node.node_id];
    DWORD lastFrameTime = tr.viewDef.time - tr.viewDef.deltaTime;
    DWORD start = lastFrameTime - lastFrameTime % 1000;

    Matrix4_multiply(modelMatrix, nodeMatrix, &matrix);

    for (float time = start, spawning = 0;
         time < tr.viewDef.time;
         time += 1000 / EmissionRate)
    {
        if (time >= lastFrameTime) {
            spawning = 1;
        }
        if (spawning) {
            cparticle_t *p = R_SpawnParticle();
            if (!p) return;
            VECTOR3 origin = FX_GenerateRandomOrigin(emitter->Length, emitter->Width);
            VECTOR3 pivot = { 0, 0, 0 };
            if (emitter->node.node_id < (DWORD)model->num_pivots) {
                pivot = model->pivots[emitter->node.node_id];
            }
            VECTOR3 pivoted = Vector3_add(&origin, &pivot);
            VECTOR3 direction = FX_GenerateRandomDirection(emitter->Latitude * M_PI / 180);
            p->org = Matrix4_multiply_vector3(&matrix, &pivoted);
            p->vel = Vector3_scale(&direction, Speed);
            p->accel = Vector3_scale(&(VECTOR3){0,0,-1}, Gravity);
            p->lifespan = emitter->LifeSpan;
            p->time = 0;
            p->midtime = emitter->Time * 0xff;
            p->texture = MDLX_GetTexture(model, teamID, emitter->TextureID, emitter->ReplaceableId, NULL);
            p->columns = emitter->Columns;
            p->rows = emitter->Rows;
            p->color[0] = MDLX_GetEmitterColor(emitter, 0);
            p->color[1] = MDLX_GetEmitterColor(emitter, 1);
            p->color[2] = MDLX_GetEmitterColor(emitter, 2);
            p->size[0] = emitter->ParticleScaling[0];
            p->size[1] = emitter->ParticleScaling[1];
            p->size[2] = emitter->ParticleScaling[2];
        }
    }
}

static bool MDLX_SetBlendMode(const mdxMaterialLayer_t *layer, DWORD layerID) {
    switch (layer->blendMode) {
        case BLEND_MODE_NONE:
            R_Call(glDisable, GL_BLEND);
            if (layerID == 0) {
                R_Call(glBlendFunc, GL_ONE, GL_ZERO);
            } else {
                R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }
            R_Call(glDepthMask, GL_TRUE);
            break;
        case BLEND_MODE_ALPHAKEY:
            R_Call(glDisable, GL_BLEND);
            R_Call(glUniform1i, mdlx.shader->uUseDiscard, 1);
            R_Call(glBlendFunc, GL_ONE, GL_ZERO);
            R_Call(glDepthMask, GL_TRUE);
            break;
        case BLEND_MODE_BLEND:
#ifdef USE_SHADOWMAPS
            if (is_rendering_lights)
                return false;
#endif
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            R_Call(glDepthMask, GL_FALSE);
            break;
#ifdef DEBUG_PATHFINDING
        default:
            return false;
#else
        case BLEND_MODE_ADD:
#ifdef USE_SHADOWMAPS
            if (is_rendering_lights)
                return false;
#endif
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_ONE, GL_ONE);
            R_Call(glDepthMask, GL_FALSE);
            break;
        case BLEND_MODE_ADDALPHA:
#ifdef USE_SHADOWMAPS
            if (is_rendering_lights)
                return false;
#endif
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE);
            R_Call(glDepthMask, GL_FALSE);
            break;
        case BLEND_MODE_MODULATE:
#ifdef USE_SHADOWMAPS
            if (is_rendering_lights)
                return false;
#endif
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_DST_COLOR, GL_ZERO);
            R_Call(glDepthMask, GL_FALSE);
            break;
        case BLEND_MODE_MODULATE_2X:
#ifdef USE_SHADOWMAPS
            if (is_rendering_lights)
                return false;
#endif
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_DST_COLOR, GL_SRC_COLOR);
            R_Call(glDepthMask, GL_FALSE);
            break;
        default:
            R_Call(glDisable, GL_BLEND);
            R_Call(glBlendFunc, GL_ONE, GL_ZERO);
            R_Call(glDepthMask, GL_TRUE);
            break;
#endif
    }
    return true;
}

static void MDLX_ApplyLayerFlags(const mdxMaterialLayer_t *layer) {
    if (layer->flags & MODEL_GEO_TWOSIDED) {
        R_Call(glDisable, GL_CULL_FACE);
    }
    if (layer->flags & MODEL_GEO_NO_DEPTH_TEST) {
        R_Call(glDisable, GL_DEPTH_TEST);
    }
    if (layer->flags & MODEL_GEO_NO_DEPTH_SET) {
        R_Call(glDepthMask, GL_FALSE);
    }
}

static bool MDLX_IsBlendedLayer(mdxMaterialLayer_t const *layer) {
    return layer->blendMode >= BLEND_MODE_BLEND;
}

static bool MDLX_MaterialHasPass(mdxMaterial_t const *material, bool blendedPass) {
    if (!material) {
        return false;
    }
    FOR_LOOP(layerID, material->num_layers) {
        if (MDLX_IsBlendedLayer(&material->layers[layerID]) == blendedPass) {
            return true;
        }
    }
    return false;
}

static VECTOR4 MDLX_EvaluateGeosetColor(mdxModel_t const *model,
                                        mdxGeoset_t const *geoset,
                                        DWORD frame);

typedef struct mdxGeosetDrawOrder_s {
    mdxGeoset_t const *geoset;
    mdxMaterial_t const *material;
    int priority;
    DWORD order;
} mdxGeosetDrawOrder_t;

static mdxMaterial_t *MDLX_GetMaterialAtIndex(mdxGeoset_t const *geoset, mdxModel_t const *model) {
    mdxMaterial_t *material = model->materials;
    for (DWORD materialID = geoset->materialID; materialID > 0; materialID--) {
        material = material->next;
    }
    return material;
}

static int MDLX_CompareGeosetDrawOrder(const void *a, const void *b) {
    mdxGeosetDrawOrder_t const *lhs = a;
    mdxGeosetDrawOrder_t const *rhs = b;

    if (lhs->priority != rhs->priority) {
        return lhs->priority < rhs->priority ? -1 : 1;
    }
    if (lhs->order != rhs->order) {
        return lhs->order < rhs->order ? -1 : 1;
    }
    return 0;
}

static bool MDLX_IsGeosetVisible(mdxModel_t const *model,
                                 mdxGeoset_t const *geoset,
                                 DWORD frame)
{
    if (geoset->geosetAnim) {
        VECTOR4 geosetColor = MDLX_EvaluateGeosetColor(model, geoset, frame);
        if (geosetColor.w < EPSILON) {
            return false;
        }
    }
    return true;
}

static mdxTextureAnim_t *MDLX_GetTextureAnimAtIndex(mdxModel_t const *model, DWORD textureAnimId) {
    mdxTextureAnim_t *textureAnim = model->textureAnims;
    if (textureAnimId == 0xFFFFFFFF) {
        return NULL;
    }
    for (DWORD id = textureAnimId; textureAnim && id > 0; id--) {
        textureAnim = textureAnim->next;
    }
    return textureAnim;
}

static void MDLX_BindLayerTextureAnimation(mdxModel_t const *model,
                                           mdxMaterialLayer_t const *layer,
                                           DWORD frame)
{
    VECTOR3 translation = { 0, 0, 0 };
    QUATERNION rotation = { 0, 0, 0, 1 };
    VECTOR3 scale = { 1, 1, 1 };
    mdxTextureAnim_t const *textureAnim = MDLX_GetTextureAnimAtIndex(model, layer->transformId);

    if (textureAnim) {
        if (textureAnim->translation) {
            MDLX_GetModelKeytrackValue(model, textureAnim->translation, frame, &translation);
        }
        if (textureAnim->rotation) {
            MDLX_GetModelKeytrackValue(model, textureAnim->rotation, frame, &rotation);
        }
        if (textureAnim->scale) {
            MDLX_GetModelKeytrackValue(model, textureAnim->scale, frame, &scale);
        }
    }

    if (!isfinite(translation.x) || !isfinite(translation.y)) {
        translation = (VECTOR3){ 0, 0, 0 };
    }
    if (!isfinite(rotation.z) || !isfinite(rotation.w)) {
        rotation = (QUATERNION){ 0, 0, 0, 1 };
    }
    if (!isfinite(scale.x) || !isfinite(scale.y)) {
        scale = (VECTOR3){ 1, 1, 1 };
    }

    R_Call(glUniform2f, mdlx.shader->uUvTrans, translation.x, translation.y);
    R_Call(glUniform2f, mdlx.shader->uUvRot, rotation.z, rotation.w);
    R_Call(glUniform2f, mdlx.shader->uUvScale, scale.x, scale.y);
}

static void MDLX_GetLightKeytrackValue(mdxModel_t const *model,
                                       mdxKeyTrack_t const *keytrack,
                                       DWORD frame,
                                       void *value)
{
    if (keytrack) {
        MDLX_GetModelKeytrackValue(model, keytrack, frame, value);
    }
}

static int MDLX_CollectModelLights(mdxModel_t const *model,
                                   LPCMATRIX4 modelMatrix,
                                   DWORD frame,
                                   mdxShaderLight_t *lights,
                                   int maxLights)
{
    int count = 0;

    FOR_EACH_LIST(mdxLight_t, light, model->lights) {
        if (count >= maxLights) {
            break;
        }

        float visibility = 1.0f;
        VECTOR3 color = light->Color;
        VECTOR3 ambient = light->AmbColor;
        float intensity = light->Intensity;
        float ambientIntensity = light->AmbIntensity;
        float attenuationStart = light->AttenuationStart;
        float attenuationEnd = light->AttenuationEnd;

        MDLX_GetLightKeytrackValue(model, light->keytracks.Visibility, frame, &visibility);
        if (visibility < EPSILON) {
            continue;
        }
        MDLX_GetLightKeytrackValue(model, light->keytracks.Color, frame, &color);
        MDLX_GetLightKeytrackValue(model, light->keytracks.Intensity, frame, &intensity);
        MDLX_GetLightKeytrackValue(model, light->keytracks.AmbColor, frame, &ambient);
        MDLX_GetLightKeytrackValue(model, light->keytracks.AmbIntensity, frame, &ambientIntensity);
        MDLX_GetLightKeytrackValue(model, light->keytracks.AttenuationStart, frame, &attenuationStart);
        MDLX_GetLightKeytrackValue(model, light->keytracks.AttenuationEnd, frame, &attenuationEnd);

        VECTOR3 pivot = { 0, 0, 0 };
        if (light->node.node_id < (DWORD)model->num_pivots) {
            pivot = model->pivots[light->node.node_id];
        }
        VECTOR3 localPosition = pivot;
        VECTOR3 localDirectionTarget = { pivot.x, pivot.y, pivot.z - 1.0f };
        if (light->node.node_id < MDX_MAX_NODES && model->nodes[light->node.node_id]) {
            localPosition = Matrix4_multiply_vector3(&node_matrices[light->node.node_id], &pivot);
            localDirectionTarget = Matrix4_multiply_vector3(&node_matrices[light->node.node_id], &localDirectionTarget);
        }

        VECTOR3 worldPosition = Matrix4_multiply_vector3(modelMatrix, &localPosition);
        VECTOR3 worldDirectionTarget = Matrix4_multiply_vector3(modelMatrix, &localDirectionTarget);
        VECTOR3 worldDirection = Vector3_sub(&worldDirectionTarget, &worldPosition);
        if (Vector3_lengthsq(&worldDirection) < EPSILON) {
            worldDirection = (VECTOR3){ 0, 0, -1 };
        } else {
            Vector3_normalize(&worldDirection);
        }

        lights[count].matrix = (MATRIX4){ .v = {
            worldPosition.x, worldPosition.y, worldPosition.z, (float)light->type,
            worldDirection.x, worldDirection.y, worldDirection.z, attenuationStart,
            color.x, color.y, color.z, intensity * visibility,
            ambient.x, ambient.y, ambient.z, ambientIntensity * visibility,
        }};
        count++;
    }

    return count;
}

static void MDLX_BindShaderLights(mdxShaderLight_t const *lights, int numLights) {
    LPSHADER shader = mdlx.shader;
    numLights = MIN(numLights, MDX_SHADER_MAX_LIGHTS);
    R_Call(glUniform1i, shader->uMdxLightCount, numLights);
    if (numLights > 0) {
        R_Call(glUniformMatrix4fv, shader->uMdxLights, numLights, GL_FALSE, lights[0].matrix.v);
    }
}

static void MDLX_BindGeosetMatrixPalette(mdxModel_t const *model, mdxGeoset_t const *geoset) {
    MATRIX4 matrixPalette[MDX_MATRIX_PALETTE];

    FOR_LOOP(i, MDX_MATRIX_PALETTE) {
        Matrix4_identity(&matrixPalette[i]);
        if (i >= geoset->num_matrixPalette) {
            continue;
        }
        int node_id = geoset->matrixPalette[i];
        if (node_id >= 0 && node_id < MDX_MAX_NODES && model->nodes[node_id]) {
            matrixPalette[i] = node_matrices[node_id];
        }
    }

    R_Call(glUniformMatrix4fv, mdlx.shader->uBones, MDX_MATRIX_PALETTE, GL_FALSE, matrixPalette->v);
}

static VECTOR4 MDLX_EvaluateGeosetColor(mdxModel_t const *model,
                                        mdxGeoset_t const *geoset,
                                        DWORD frame)
{
    VECTOR4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

    if (!geoset->geosetAnim) {
        return color;
    }
    color.w = geoset->geosetAnim->staticAlpha;
    if (!isfinite(color.w)) {
        color.w = 1.0f;
    }
    if (geoset->geosetAnim->alphas) {
        MDLX_GetModelKeytrackValue(model, geoset->geosetAnim->alphas, frame, &color.w);
    }
    if (geoset->geosetAnim->flags & 0x2) {
        color.x = geoset->geosetAnim->staticColor.x;
        color.y = geoset->geosetAnim->staticColor.y;
        color.z = geoset->geosetAnim->staticColor.z;
        if (geoset->geosetAnim->colors) {
            VECTOR3 animated = geoset->geosetAnim->staticColor;
            MDLX_GetModelKeytrackValue(model, geoset->geosetAnim->colors, frame, &animated);
            color.x = animated.x;
            color.y = animated.y;
            color.z = animated.z;
        }
    }
    color.x = MIN(MAX(color.x, 0.0f), 1.0f);
    color.y = MIN(MAX(color.y, 0.0f), 1.0f);
    color.z = MIN(MAX(color.z, 0.0f), 1.0f);
    color.w = MIN(MAX(color.w, 0.0f), 1.0f);
    return color;
}

static float MDLX_EvaluateLayerAlpha(mdxModel_t const *model,
                                     mdxMaterial_t const *material,
                                     mdxMaterialLayer_t const *layer,
                                     DWORD frame)
{
    float alpha = layer->staticAlpha;

    if (!isfinite(alpha)) {
        alpha = 1.0f;
    }
    if (layer->alpha) {
        MDLX_GetModelKeytrackValue(model, layer->alpha, frame, &alpha);
    }
    if (material->alpha) {
        float materialAlpha = 1.0f;
        MDLX_GetModelKeytrackValue(model, material->alpha, frame, &materialAlpha);
        alpha *= materialAlpha;
    }
    if (!isfinite(alpha)) {
        alpha = 1.0f;
    }
    if (alpha < 0.0f) {
        alpha = 0.0f;
    } else if (alpha > 1.0f) {
        alpha = 1.0f;
    }
    return alpha;
}

static void MDLX_RenderGeoset(mdxModel_t const *model,
                             mdxGeoset_t const *geoset,
                             mdxMaterial_t const *material,
                             DWORD team,
                             LPCTEXTURE overrideTexture,
                             BOOL forceUnshaded,
                             DWORD frame,
                             bool blendedPass)
{
    BOOL force_two_sided = model && !model->cameras;
    LPSHADER shader = mdlx.shader;
    VECTOR4 geosetColor;

    if (!MDLX_MaterialHasPass(material, blendedPass)) {
        return;
    }

    geosetColor = MDLX_EvaluateGeosetColor(model, geoset, frame);
    MDLX_BindGeosetMatrixPalette(model, geoset);
    R_Call(glUniform1f, shader->uLayerAlpha, 1.0f);
    R_Call(glUniform4f, shader->uGeosetColor, geosetColor.x, geosetColor.y, geosetColor.z, geosetColor.w);

    FOR_LOOP(layerID, material->num_layers) {
        mdxMaterialLayer_t const *layer = &material->layers[layerID];
        float alpha;

        if (MDLX_IsBlendedLayer(layer) != blendedPass) {
            continue;
        }
        R_Call(glEnable, GL_DEPTH_TEST);
        R_Call(glUniform1i, shader->uUseDiscard, 0);
        if (force_two_sided) {
            R_Call(glDisable, GL_CULL_FACE);
        } else {
            R_Call(glEnable, GL_CULL_FACE);
            R_Call(glCullFace, GL_BACK);
        }
        R_Call(glDepthMask, GL_TRUE);
        if (!MDLX_SetBlendMode(layer, layerID))
            continue;
        MDLX_ApplyLayerFlags(layer);
        BOOL unshaded = forceUnshaded || (layer->flags & MODEL_GEO_UNSHADED);
        R_Call(glUniform1i, shader->uUnshaded, unshaded);
        alpha = MDLX_EvaluateLayerAlpha(model, material, layer, frame);
        if (alpha < EPSILON)
            continue;
        R_Call(glUniform1f, shader->uLayerAlpha, alpha);
        MDLX_BindLayerTextureAnimation(model, layer, frame);
        mdxTexture_t const *modeltex = &model->textures[layer->textureId];
        LPCTEXTURE texture = MDLX_GetTexture(model, team, layer->textureId, modeltex->replaceableID, overrideTexture);
        R_BindTexture(texture, 0);
        R_Call(glBindVertexArray, geoset->vertexArrayBuffer);
        R_Call(glBindBuffer, GL_ELEMENT_ARRAY_BUFFER, *geoset->buffer);
        R_Call(glDrawElements, GL_TRIANGLES, geoset->num_triangles, GL_UNSIGNED_SHORT, NULL);
    }

    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glEnable, GL_CULL_FACE);
    R_Call(glCullFace, GL_BACK);
    R_Call(glDepthMask, GL_TRUE);
    R_Call(glUniform1i, shader->uUnshaded, forceUnshaded);
    R_Call(glUniform1f, shader->uLayerAlpha, 1.0f);
    R_Call(glUniform4f, shader->uGeosetColor, 1.0f, 1.0f, 1.0f, 1.0f);
}

mdxSequence_t const *MDLX_FindSequenceByName(mdxModel_t const *model, LPCSTR name) {
    FOR_LOOP(i, model->num_sequences) {
        if (!strcmp(model->sequences[i].name, name)) {
            return &model->sequences[i];
        }
    }
    return NULL;
}

DWORD MDLX_RemapAnimation(mdxModel_t const *model, DWORD frame, LPCSTR str) {
    mdxSequence_t const *seq = R_FindSequenceAtTime(model, frame);
    size_t seq_name_len;

    if (!seq) return frame;
    seq_name_len = strlen(seq->name);
    FOR_LOOP(i, model->num_sequences) {
        mdxSequence_t const *other = &model->sequences[i];
        if (!strncmp(other->name, seq->name, seq_name_len) &&
            other->name[seq_name_len] == ' ' &&
            !strcmp(other->name + seq_name_len + 1, str)) {
            return frame + other->interval[0] - seq->interval[0];
        }
    }
    return frame;
}

bool MDLX_TraceModel(renderEntity_t const *ent, LPCLINE3 line) {
    MATRIX4 invmodel, matmodel;
    R_GetEntityMatrix(ent, &matmodel);
    mdxModel_t const *model = ent->model->mdx;
    if (!model) return false;
    FOR_EACH_LIST(mdxCollisionShape_t, collisionShape, model->collisionShapes) {
        if (collisionShape->type != SHAPETYPE_SPHERE)
            continue;;
        VECTOR3 center;
        memcpy(&center, &collisionShape->vertex[0], sizeof(VECTOR3));
        SPHERE3 sphere = {
            .center = Matrix4_multiply_vector3(&matmodel, &center),
            .radius = collisionShape->radius * ent->scale,
        };
        if (Line3_intersect_sphere3(line, &sphere, NULL))
            return true;
    }
    Matrix4_inverse(&matmodel, &invmodel);
    LINE3 linelocal = {
        Matrix4_multiply_vector3(&invmodel, &line->a),
        Matrix4_multiply_vector3(&invmodel, &line->b),
    };
    if (!model->collisionShapes)
        goto check_geosets;
    FOR_EACH_LIST(mdxCollisionShape_t, collisionShape, model->collisionShapes) {
        if (collisionShape->type == SHAPETYPE_BOX) {
            BOX3 box = {
                .min = collisionShape->vertex[0],
                .max = collisionShape->vertex[1],
            };
            if (Line3_intersect_box3(&linelocal, &box, NULL))
                return true;
        } else if (collisionShape->type == SHAPETYPE_SPHERE) {
            VECTOR3 center;
            memcpy(&center, &collisionShape->vertex[0], sizeof(VECTOR3));
            SPHERE3 sphere = {
                .center = center,
                .radius = collisionShape->radius,
            };
            if (Line3_intersect_sphere3(&linelocal, &sphere, NULL))
                return true;
        }
    }
    return false;
check_geosets:
    FOR_EACH_LIST(mdxGeoset_t, geoset, model->geosets) {
        BOX3 box2 = {
            .min = *(LPCVECTOR3)&geoset->default_bounds.box.min,
            .max = *(LPCVECTOR3)&geoset->default_bounds.box.max,
        };
        if (Line3_intersect_box3(&linelocal, &box2, NULL))
            goto check_geometry;
    }
    return false;
check_geometry:
    FOR_EACH_LIST(mdxGeoset_t, geoset, model->geosets) {
        FOR_LOOP(i, geoset->num_triangles / 3) {
            TRIANGLE3 tri = {
                .a = geoset->vertices[geoset->triangles[i*3+0]],
                .b = geoset->vertices[geoset->triangles[i*3+1]],
                .c = geoset->vertices[geoset->triangles[i*3+2]],
            };
            if (Line3_intersect_triangle(&linelocal, &tri, NULL))
                return true;
        }
    }
    return false;
}

static void MDLX_RenderGeosets(const renderEntity_t *entity,
                               const mdxModel_t *model)
{
    BOOL forceUnshaded = (entity->flags & RF_NO_LIGHTING) != 0;
    DWORD geosetCount = 0;
    DWORD drawCount = 0;
    mdxGeosetDrawOrder_t stackDrawOrder[MDLX_STACK_DRAW_ORDER];
    mdxGeosetDrawOrder_t *drawOrder;

    FOR_EACH_LIST(mdxGeoset_t, geoset, model->geosets) {
        mdxMaterial_t const *material;
        geosetCount++;
        if (!MDLX_IsGeosetVisible(model, geoset, entity->frame)) {
            continue;
        }
        material = MDLX_GetMaterialAtIndex(geoset, model);
        if (MDLX_MaterialHasPass(material, false)) {
            MDLX_RenderGeoset(model, geoset, material, entity->team&TEAM_MASK, entity->skin, forceUnshaded, entity->frame, false);
        }
    }

    if (geosetCount == 0) {
        return;
    }

    drawOrder = geosetCount <= MDLX_STACK_DRAW_ORDER ?
                stackDrawOrder :
                ri.MemAlloc(sizeof(*drawOrder) * geosetCount);
    if (!drawOrder) {
        FOR_EACH_LIST(mdxGeoset_t, geoset, model->geosets) {
            mdxMaterial_t const *material = MDLX_GetMaterialAtIndex(geoset, model);
            if (MDLX_IsGeosetVisible(model, geoset, entity->frame) &&
                MDLX_MaterialHasPass(material, true))
            {
                MDLX_RenderGeoset(model, geoset, material, entity->team&TEAM_MASK, entity->skin, forceUnshaded, entity->frame, true);
            }
        }
        return;
    }
    FOR_EACH_LIST(mdxGeoset_t, geoset, model->geosets) {
        mdxMaterial_t const *material;
        if (!MDLX_IsGeosetVisible(model, geoset, entity->frame)) {
            continue;
        }
        material = MDLX_GetMaterialAtIndex(geoset, model);
        if (!MDLX_MaterialHasPass(material, true)) {
            continue;
        }
        drawOrder[drawCount] = (mdxGeosetDrawOrder_t) {
            .geoset = geoset,
            .material = material,
            .priority = material ? material->priority : 0,
            .order = drawCount,
        };
        drawCount++;
    }

    if (drawCount > 1) {
        qsort(drawOrder, drawCount, sizeof(*drawOrder), MDLX_CompareGeosetDrawOrder);
    }

    FOR_LOOP(i, drawCount) {
        mdxGeoset_t const *geoset = drawOrder[i].geoset;
        MDLX_RenderGeoset(model, geoset, drawOrder[i].material, entity->team&TEAM_MASK, entity->skin, forceUnshaded, entity->frame, true);
    }

    if (drawOrder != stackDrawOrder) {
        ri.MemFree(drawOrder);
    }
}

static void MDLX_RenderParticleEmitters(const renderEntity_t *entity, const mdxModel_t *model, LPCMATRIX4 model_matrix) {
    FOR_EACH_LIST(mdxParticleEmitter_t, emitter, model->emitters) {
        if (emitter->keytracks.Visibility) {
            float fVisibility = 1.f;
            MDLX_GetModelKeytrackValue(model, emitter->keytracks.Visibility, entity->frame, &fVisibility);
            if (fVisibility < EPSILON)
                continue;
        }
        float const frame = LerpNumber(entity->oldframe, entity->frame, tr.viewDef.lerpfrac);
        MDLX_RenderEmitter(model, emitter, model_matrix, frame, entity->team&TEAM_MASK);
    }
}

void MDX_RenderModel(renderEntity_t const *entity,
                     mdxModel_t const *model,
                     LPCMATRIX4 transform)
{
    if (!(tr.viewDef.rdflags & RDF_NOFRUSTUMCULL)) {
        VECTOR3 const center = Box3_Center(&model->bounds.box);
        SPHERE3 const sphere = {
            .center = Matrix4_multiply_vector3(transform, &center),
            .radius = model->bounds.radius * entity->scale,
        };
        if (!Frustum_ContainsSphere(&tr.viewDef.frustum, &sphere))
            return;
        if (!Frustum_ContainsBox(&tr.viewDef.frustum, &model->bounds.box, transform))
            return;
    }

    if (entity->flags & RF_HAS_LUMBER) {
        renderEntity_t ent = *entity;
        ent.frame = MDLX_RemapAnimation(model, ent.frame, "Lumber");
        ent.oldframe = MDLX_RemapAnimation(model, ent.oldframe, "Lumber");
        entity = &ent;
    } else if (entity->flags & RF_HAS_GOLD) {
        renderEntity_t ent = *entity;
        ent.frame = MDLX_RemapAnimation(model, ent.frame, "Gold");
        ent.oldframe = MDLX_RemapAnimation(model, ent.oldframe, "Gold");
        entity = &ent;
    }
    
    LPSHADER shader = mdlx.shader;
    MATRIX3 normalMatrix;
    GLfloat const *viewProjectionMatrix =
#ifdef USE_SHADOWMAPS
        is_rendering_lights ? tr.viewDef.lightMatrix.v :
#endif
        tr.viewDef.viewProjectionMatrix.v;
    Matrix3_normal(&normalMatrix, transform);
    R_Call(glUseProgram, shader->progid);
    R_Call(glUniformMatrix4fv, shader->uViewProjectionMatrix, 1, GL_FALSE, viewProjectionMatrix);
    R_Call(glUniformMatrix4fv, shader->uModelMatrix, 1, GL_FALSE, transform->v);
    R_Call(glUniformMatrix3fv, shader->uNormalMatrix, 1, GL_TRUE, normalMatrix.v);
    R_Call(glUniformMatrix4fv, shader->uTextureMatrix, 1, GL_FALSE, tr.viewDef.textureMatrix.v);
    R_Call(glUniformMatrix4fv, shader->uLightMatrix, 1, GL_FALSE, tr.viewDef.lightMatrix.v);
    R_Call(glUniform1i, shader->uUnshaded, (entity->flags & RF_NO_LIGHTING) != 0);
    if (entity->flags & RF_PORTRAIT_LIGHTING) {
        R_Call(glUniform2f, shader->uMdxFallbackLighting, 0.58f, 0.62f);
        R_Call(glUniform1f, shader->uMdxLightFill, 0.22f);
    } else {
        R_Call(glUniform2f, shader->uMdxFallbackLighting, 0.35f, 0.75f);
        R_Call(glUniform1f, shader->uMdxLightFill, 0.0f);
    }

    MDLX_BindBoneMatrices(model, transform, entity->frame, entity->oldframe);
    mdxShaderLight_t lights[MDX_SHADER_MAX_LIGHTS];
    int numLights = MDLX_CollectModelLights(model, transform, entity->frame, lights, MDX_SHADER_MAX_LIGHTS);
    MDLX_BindShaderLights(lights, numLights);

    if (entity->flags & RF_NO_FOGOFWAR) {
        R_Call(glActiveTexture, GL_TEXTURE2);
        R_Call(glBindTexture, GL_TEXTURE_2D, tr.texture[TEX_WHITE]->texid);
        R_Call(glActiveTexture, GL_TEXTURE0);
    }
    
    MDLX_RenderGeosets(entity, model);
    
    MDLX_RenderParticleEmitters(entity, model, transform);

    if ((entity->flags & RF_NO_FOGOFWAR) && tr.world) {
        R_Call(glActiveTexture, GL_TEXTURE2);
        R_Call(glBindTexture, GL_TEXTURE_2D, R_GetFogOfWarTexture());
        R_Call(glActiveTexture, GL_TEXTURE0);
    }
}
