#include "r_mdx.h"
#include "renderer/r_emit.h"
#include "renderer/r_local.h"
#include "renderer/r_shader.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>

#define MDLX_STACK_DRAW_ORDER 64
texture_t const *MDLX_GetTexture(mdxModel_t const *model,
                                 uint32_t teamID,
                                 uint32_t textureID,
                                 uint32_t replaceableID,
                                 texture_t const *overrideTexture) {
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

bool MDLX_SetLayerBlend(mdxMaterialLayer_t const *layer, uint32_t layerID) {
    R_SetAlphaKeyState(false);
#ifdef USE_SHADOWMAPS
    switch (tr.render_phase == RENDER_PHASE_LIGHTS ? (int)layer->blendMode : -1) {
        case BLEND_MODE_BLEND:
        case BLEND_MODE_ADD:
        case BLEND_MODE_ADDALPHA:
        case BLEND_MODE_MODULATE:
        case BLEND_MODE_MODULATE_2X:
            return false;
    }
#endif
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
            mdlx.shader->state.alphaKey = 1;
            R_SetAlphaKeyState(true);
            break;
        case BLEND_MODE_BLEND:
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            R_Call(glDepthMask, GL_FALSE);
            break;
        case BLEND_MODE_ADD:
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_ONE, GL_ONE);
            R_Call(glDepthMask, GL_FALSE);
            break;
        case BLEND_MODE_ADDALPHA:
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE);
            R_Call(glDepthMask, GL_FALSE);
            break;
        case BLEND_MODE_MODULATE:
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_DST_COLOR, GL_ZERO);
            R_Call(glDepthMask, GL_FALSE);
            break;
        case BLEND_MODE_MODULATE_2X:
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_DST_COLOR, GL_SRC_COLOR);
            R_Call(glDepthMask, GL_FALSE);
            break;
        default:
            R_Call(glDisable, GL_BLEND);
            R_Call(glBlendFunc, GL_ONE, GL_ZERO);
            R_Call(glDepthMask, GL_TRUE);
            break;
    }
    return true;
}

void MDLX_ApplyLayerFlags(mdxMaterialLayer_t const *layer) {
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

static vec4_t MDLX_EvaluateGeosetColor(mdxModel_t const *model,
                                        mdxGeoset_t const *geoset,
                                        uint32_t frame);

typedef struct mdxGeosetDrawOrder_s {
    mdxGeoset_t const *geoset;
    mdxMaterial_t const *material;
    int priority;
    uint32_t order;
} mdxGeosetDrawOrder_t;

static mdxMaterial_t *MDLX_GetMaterialAtIndex(mdxGeoset_t const *geoset, mdxModel_t const *model) {
    mdxMaterial_t *material = model->materials;
    for (uint32_t materialID = geoset->materialID; materialID > 0; materialID--) {
        material = material->next;
    }
    return material;
}

static int MDLX_CompareGeosetDrawOrder(void const *a, void const *b) {
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
                                 uint32_t frame)
{
    if (geoset->geosetAnim) {
        vec4_t geosetColor = MDLX_EvaluateGeosetColor(model, geoset, frame);
        if (geosetColor.w < EPSILON) {
            return false;
        }
    }
    return true;
}

static mdxTextureAnim_t *MDLX_GetTextureAnimAtIndex(mdxModel_t const *model, uint32_t textureAnimId) {
    mdxTextureAnim_t *textureAnim = model->textureAnims;
    if (textureAnimId == 0xFFFFFFFF) {
        return NULL;
    }
    for (uint32_t id = textureAnimId; textureAnim && id > 0; id--) {
        textureAnim = textureAnim->next;
    }
    return textureAnim;
}

/* Resolve the texture slot used by a layer at the current frame.  Layers with
 * a flipbook (KMTF) track animate their TEXS index over time (e.g. the menu
 * ocean cycling ocean_h.01..30); without evaluating it the layer is stuck on
 * its static base texture and renders wrong (white). */
static uint32_t MDLX_EvaluateLayerTextureId(mdxModel_t const *model,
                                         mdxMaterialLayer_t const *layer,
                                         uint32_t frame) {
    uint32_t textureId = layer->textureId;
    if (layer->flipbook) {
        int value = (int)textureId;
        MDLX_GetModelKeytrackValue(model, layer->flipbook, frame, &value);
        if (value >= 0 && value < model->num_textures) {
            textureId = (uint32_t)value;
        }
    }
    return textureId;
}

static void MDLX_BindLayerTextureAnimation(mdxModel_t const *model,
                                           mdxMaterialLayer_t const *layer,
                                           uint32_t frame)
{
    vec3_t translation = { 0, 0, 0 };
    quaternion_t rotation = { 0, 0, 0, 1 };
    vec3_t scale = { 1, 1, 1 };
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
        translation = (vec3_t){ 0, 0, 0 };
    }
    if (!isfinite(rotation.z) || !isfinite(rotation.w)) {
        rotation = (quaternion_t){ 0, 0, 0, 1 };
    }
    if (!isfinite(scale.x) || !isfinite(scale.y)) {
        scale = (vec3_t){ 1, 1, 1 };
    }

    {
        /* Build the UV affine matrix.  Operations applied in order:
             1. translate by (T.x, T.y)
             2. rotate around UV centre (0.5,0.5) using quaternion zw components
             3. scale around UV centre
           Combined as a mat3: uv_out = (M * vec3(uv, 1)).xy
           Column-major for glUniformMatrix3fv. */
        float c = rotation.w * rotation.w - rotation.z * rotation.z;
        float s = 2.0f * rotation.z * rotation.w;
        float tx = scale.x * (c * (translation.x - 0.5f) - s * (translation.y - 0.5f)) + 0.5f;
        float ty = scale.y * (s * (translation.x - 0.5f) + c * (translation.y - 0.5f)) + 0.5f;
        GLfloat m[9] = { scale.x*c, scale.y*s, 0, -scale.x*s, scale.y*c, 0, tx, ty, 1 };
        memcpy(&mdlx.shader->state.uvMatrix, m, (1) * sizeof(mat3_t));
    }
}

static void MDLX_BindGeosetMatrixPalette(mdxModel_t const *model, mdxGeoset_t const *geoset) {
    mat4_t matrixPalette[MDX_MATRIX_PALETTE];
    /* Skin indices are geoset-local (0..num_matrixPalette-1), so only the
     * palette entries the geoset actually references need to reach the shader.
     * Uploading the full BZ_BONE_PALETTE_MAX per geoset was wasted uniform
     * traffic for every draw. */
    uint32_t const count = MIN((uint32_t)geoset->num_matrixPalette, BZ_BONE_PALETTE_MAX);

    FOR_LOOP(i, count) {
        int node_id = geoset->matrixPalette[i];
        if (node_id >= 0 && node_id < MDX_MAX_NODES && model->nodes[node_id]) {
            matrixPalette[i] = node_matrices[node_id];
        } else {
            Matrix4_identity(&matrixPalette[i]);
        }
    }

    memcpy(&mdlx.shader->state.bones, matrixPalette->v, (count) * sizeof(mat4_t));
    mdlx.shader->state.boneCount = count;
}

static vec4_t MDLX_EvaluateGeosetColor(mdxModel_t const *model,
                                        mdxGeoset_t const *geoset,
                                        uint32_t frame)
{
    vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };

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
        vec3_t geosetColor = { 1.0f, 1.0f, 1.0f };

        /* Warsmash swizzles both the static GeosetAnimation base color and
         * animated KGAC values. Keep this at the semantic VECTOR3 layer rather
         * than the platform-specific texture BGRA upload path. */
        MDLX_GetGeosetAnimationStaticColor(geoset->geosetAnim, &geosetColor);
        if (geoset->geosetAnim->colors)
            MDLX_GetAnimatedColorTrackValue(model, geoset->geosetAnim->colors, frame, &geosetColor);
        color.x = geosetColor.x;
        color.y = geosetColor.y;
        color.z = geosetColor.z;
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
                                     uint32_t frame)
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
                             uint32_t team,
                             texture_t const *overrideTexture,
                             bool forceUnshaded,
                             uint32_t frame,
                             vec4_t const *tint,
                             bool blendedPass)
{
    bool force_two_sided = model && !model->cameras;
    modelProg_t *shader = mdlx.shader;
    vec4_t geosetColor;

    if (!MDLX_MaterialHasPass(material, blendedPass)) {
        return;
    }

    geosetColor = MDLX_EvaluateGeosetColor(model, geoset, frame);
    {
        float *component = &geosetColor.x;
        float const *factor = &tint->x;
        FOR_LOOP(i, 4) component[i] *= factor[i];
    }
    MDLX_BindGeosetMatrixPalette(model, geoset);
    shader->state.layerAlpha = 1.0f;
    shader->state.geosetColor = (vec4_t){ geosetColor.x, geosetColor.y, geosetColor.z, geosetColor.w };

    FOR_LOOP(layerID, material->num_layers) {
        mdxMaterialLayer_t const *layer = &material->layers[layerID];
        float alpha;

        if (MDLX_IsBlendedLayer(layer) != blendedPass) {
            continue;
        }
        R_Call(glEnable, GL_DEPTH_TEST);
        shader->state.alphaKey = 0;
        if (force_two_sided) {
            R_Call(glDisable, GL_CULL_FACE);
        } else {
            R_Call(glEnable, GL_CULL_FACE);
            R_Call(glCullFace, GL_BACK);
        }
        R_Call(glDepthMask, GL_TRUE);
        if (!MDLX_SetLayerBlend(layer, layerID))
            continue;
        /* Instance tint alpha is presentation opacity, not authored material
         * alpha. Opaque and alpha-key layers therefore need a real blend path
         * when the caller supplies a translucent instance; multiplying the
         * shader alpha alone would otherwise leave opaque layers fully opaque. */
        if (tint->w < 1.0f - EPSILON &&
            (layer->blendMode == BLEND_MODE_NONE ||
             layer->blendMode == BLEND_MODE_ALPHAKEY)) {
            shader->state.alphaKey = 0;
            R_SetAlphaKeyState(false);
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            R_Call(glDepthMask, GL_FALSE);
        }
        MDLX_ApplyLayerFlags(layer);
        bool unshaded = forceUnshaded || (layer->flags & MODEL_GEO_UNSHADED);
        shader->state.unshaded = unshaded;
        /* Fog only affects opaque/alpha-blended geometry.  Additive and
         * modulate layers (glows, the blue spire flare) must NOT mix toward
         * the fog colour or they turn into solid fog-coloured quads. */
        {
            bool layerFog = tr.viewDef.fogEnable &&
                !(layer->flags & MODEL_GEO_UNFOGGED) &&
                (layer->blendMode == BLEND_MODE_NONE ||
                 layer->blendMode == BLEND_MODE_ALPHAKEY ||
                 layer->blendMode == BLEND_MODE_BLEND);
            shader->state.fogEnable = layerFog ? 1 : 0;
        }
        alpha = MDLX_EvaluateLayerAlpha(model, material, layer, frame);
        if (alpha < EPSILON)
            continue;
        shader->state.layerAlpha = alpha;
        MDLX_BindLayerTextureAnimation(model, layer, frame);
        uint32_t textureId = MDLX_EvaluateLayerTextureId(model, layer, frame);
        mdxTexture_t const *modeltex = &model->textures[textureId];
        texture_t const *texture = MDLX_GetTexture(model, team, textureId, modeltex->replaceableID, overrideTexture);
        R_BindTexture(texture, 0);
        R_Call(glBindVertexArray, geoset->vertexArrayBuffer);
        /* The geoset VAO already binds the model-owned index buffer. */
        R_StatsDraw(GL_TRIANGLES, geoset->num_triangles, 1);
        R_ApplyShader(shader);
        R_Call(glDrawElements, GL_TRIANGLES, geoset->num_triangles, GL_UNSIGNED_SHORT, (void *)(uintptr_t)geoset->indexofs);
    }

    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glEnable, GL_CULL_FACE);
    R_SetAlphaKeyState(false);
    R_Call(glCullFace, GL_BACK);
    R_Call(glDepthMask, GL_TRUE);
    shader->state.unshaded = forceUnshaded;
    shader->state.layerAlpha = 1.0f;
    shader->state.geosetColor = (vec4_t){ 1.0f, 1.0f, 1.0f, 1.0f };
}

mdxSequence_t const *MDLX_FindSequenceByName(mdxModel_t const *model, cstring_t name) {
    FOR_LOOP(i, model->num_sequences) {
        if (!strcmp(model->sequences[i].name, name)) {
            return &model->sequences[i];
        }
    }
    return NULL;
}

uint32_t MDLX_RemapAnimation(mdxModel_t const *model, uint32_t frame, cstring_t str) {
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

static bool MDLX_TraceModelMesh(renderEntity_t const *ent, line3_t const *line, vec3_t *intersection) {
    mat4_t invmodel, matmodel;
    vec3_t best_point = { 0 };
    float best_distance = FLT_MAX;
    bool hit = false;
    mdxModel_t const *model;

    if (!ent || !line || !ent->model)
        return false;
    model = ent->model->mdx;
    if (!model)
        return false;

    R_GetEntityMatrix(ent, &matmodel);
    Matrix4_inverse(&matmodel, &invmodel);
    line3_t linelocal = {
        Matrix4_multiply_vector3(&invmodel, &line->a),
        Matrix4_multiply_vector3(&invmodel, &line->b),
    };

    /* Warsmash's walkable-object height query calls
     * intersectRayWithCollision(..., true, true), which means "only use the
     * visible/selectable mesh" even when authored CollisionShapes exist.
     * Keep that behavior separate from normal model picking, where WC3 uses
     * CollisionShapes preferentially. */
    FOR_EACH_LIST(mdxGeoset_t, geoset, model->geosets) {
        box3_t box;
        vec3_t bounds_hit;

        if ((geoset->selectable & 4) ||
            !MDLX_IsGeosetVisible(model, geoset, ent->frame))
            continue;

        box = (box3_t) {
            .min = *(vec3_t const *)&geoset->default_bounds.box.min,
            .max = *(vec3_t const *)&geoset->default_bounds.box.max,
        };
        if (!Line3_intersect_box3(&linelocal, &box, &bounds_hit))
            continue;

        FOR_LOOP(i, geoset->num_triangles / 3) {
            vec3_t local_point;
            triangle3_t tri = {
                .a = geoset->vertices[geoset->triangles[i*3+0]],
                .b = geoset->vertices[geoset->triangles[i*3+1]],
                .c = geoset->vertices[geoset->triangles[i*3+2]],
            };
            if (Line3_intersect_triangle(&linelocal, &tri, &local_point)) {
                vec3_t const point = Matrix4_multiply_vector3(&matmodel, &local_point);
                float const distance = Vector3_distance(&line->a, &point);
                if (distance < best_distance) {
                    best_distance = distance;
                    best_point = point;
                    hit = true;
                }
            }
        }
    }

    if (hit && intersection)
        *intersection = best_point;
    return hit;
}

bool MDLX_TraceWalkableSurface(renderEntity_t const *ent, line3_t const *line, vec3_t *intersection) {
    return MDLX_TraceModelMesh(ent, line, intersection);
}

bool MDLX_TraceModel(renderEntity_t const *ent, line3_t const *line, vec3_t *intersection) {
    mat4_t invmodel, matmodel;
    vec3_t best_point = { 0 };
    float best_distance = FLT_MAX;
    bool hit = false;
    mdxModel_t const *model;

    if (!ent || !line || !ent->model)
        return false;
    model = ent->model->mdx;
    if (!model)
        return false;

    R_GetEntityMatrix(ent, &matmodel);
    Matrix4_inverse(&matmodel, &invmodel);
    line3_t linelocal = {
        Matrix4_multiply_vector3(&invmodel, &line->a),
        Matrix4_multiply_vector3(&invmodel, &line->b),
    };

    if (model->collisionShapes) {
        FOR_EACH_LIST(mdxCollisionShape_t, collisionShape, model->collisionShapes) {
            vec3_t point;
            bool shape_hit = false;

            if (collisionShape->type == SHAPETYPE_BOX) {
                box3_t box = {
                    .min = collisionShape->vertex[0],
                    .max = collisionShape->vertex[1],
                };
                vec3_t local_point;
                if (Line3_intersect_box3(&linelocal, &box, &local_point)) {
                    point = Matrix4_multiply_vector3(&matmodel, &local_point);
                    shape_hit = true;
                }
            } else if (collisionShape->type == SHAPETYPE_SPHERE) {
                vec3_t center;
                memcpy(&center, &collisionShape->vertex[0], sizeof(vec3_t));
                sphere3_t sphere = {
                    .center = Matrix4_multiply_vector3(&matmodel, &center),
                    .radius = collisionShape->radius * ent->scale,
                };
                shape_hit = Line3_intersect_sphere3(line, &sphere, &point);
            }

            if (shape_hit) {
                float const distance = Vector3_distance(&line->a, &point);
                if (distance < best_distance) {
                    best_distance = distance;
                    best_point = point;
                    hit = true;
                }
            }
        }
    } else {
        return MDLX_TraceModelMesh(ent, line, intersection);
    }

    if (hit && intersection)
        *intersection = best_point;
    return hit;
}

static void MDLX_RenderGeosets(renderEntity_t const *entity,
                               mdxModel_t const *model)
{
    bool forceUnshaded = (entity->flags & RF_NO_LIGHTING) != 0;
    color32_t const color = (entity->tint_valid || entity->tint.a) ? entity->tint : COLOR32_WHITE;
    vec4_t const tint = {
        BYTE2FLOAT(color.r), BYTE2FLOAT(color.g),
        BYTE2FLOAT(color.b), BYTE2FLOAT(color.a)
    };
    uint32_t geosetCount = 0;
    uint32_t drawCount = 0;
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
            MDLX_RenderGeoset(model, geoset, material, entity->team&TEAM_MASK, entity->skin, forceUnshaded, entity->frame, &tint, false);
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
                MDLX_RenderGeoset(model, geoset, material, entity->team&TEAM_MASK, entity->skin, forceUnshaded, entity->frame, &tint, true);
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
        MDLX_RenderGeoset(model, geoset, drawOrder[i].material, entity->team&TEAM_MASK, entity->skin, forceUnshaded, entity->frame, &tint, true);
    }

    if (drawOrder != stackDrawOrder) {
        ri.MemFree(drawOrder);
    }
}

static int MDLX_CollectModelLights(mdxModel_t const *model,
                                   mat4_t const *modelMatrix,
                                   uint32_t frame,
                                   rModelLight_t *lights,
                                   int maxLights)
{
    int count = 0;

    FOR_EACH_LIST(mdxLight_t, light, model->lights) {
        rModelLight_t evaluated;
        if (!MDLX_EvaluateLight(model, light, modelMatrix, frame, true, &evaluated))
            continue;
        if (count < maxLights)
            lights[count] = evaluated;
        count++;
    }

    return MIN(count, maxLights);
}

static buffer_t ribbon_buf;
static bool ribbon_buf_ready;
static vertex_t ribbon_verts[TRAIL_MAX_EDGES * 6];

mdxMaterial_t *MDLX_MaterialAt(mdxModel_t const *model, uint32_t id) {
    mdxMaterial_t *material = model->materials;
    for (; material && id > 0; id--)
        material = material->next;
    return material;
}

static void MDLX_EnsureRibbonBuffer(void) {
    static const struct { GLuint attr; GLint size; GLenum type; GLboolean norm; size_t ofs; } attrs[] = {
        { attrib_position, 3, GL_FLOAT, GL_FALSE, offsetof(vertex_t, position) },
        { attrib_texcoord, 2, GL_FLOAT, GL_FALSE, offsetof(vertex_t, texcoord) },
        { attrib_normal, 3, GL_FLOAT, GL_FALSE, offsetof(vertex_t, normal) },
        { attrib_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(vertex_t, color) },
        { attrib_skin1, 4, GL_UNSIGNED_BYTE, GL_FALSE, offsetof(vertex_t, skin) },
        { attrib_boneWeight1, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(vertex_t, boneWeight) },
    };
    if (ribbon_buf_ready) return;
    R_Call(glGenVertexArrays, 1, &ribbon_buf.vao);
    R_Call(glGenBuffers, 1, &ribbon_buf.vbo);
    R_Call(glBindVertexArray, ribbon_buf.vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, ribbon_buf.vbo);
    FOR_LOOP(i, sizeof(attrs) / sizeof(*attrs)) {
        R_Call(glEnableVertexAttribArray, attrs[i].attr);
        R_Call(glVertexAttribPointer, attrs[i].attr, attrs[i].size, attrs[i].type, attrs[i].norm,
               sizeof(vertex_t), (void *)attrs[i].ofs);
    }
    ribbon_buf_ready = true;
}

/* Upload one strip and run it through the material's layer stack. Shared by live
 * trails and detached (entity-less) orphans so both execute identical GL. */
void MDLX_DrawRibbonVerts(mdxModel_t const *model, vertex_t *verts, uint32_t nverts,
                          mdxMaterial_t const *material, uint32_t team)
{
    modelProg_t *shader = mdlx.shader;
    mat4_t identity;

    if (!shader || !model || !verts || !nverts || !material) return;
    MDLX_EnsureRibbonBuffer();
    Matrix4_identity(&identity);
    shader->state.model = identity;
    shader->state.bones[0] = identity;
    shader->state.boneCount = 1;
    shader->state.geosetColor = (vec4_t){ 1, 1, 1, 1 };
    shader->state.layerAlpha = 1.0f;
    R_Call(glBindVertexArray, ribbon_buf.vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, ribbon_buf.vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, nverts * sizeof(vertex_t), verts, GL_STREAM_DRAW);
    FOR_LOOP(layerID, material->num_layers) {
        mdxMaterialLayer_t const *layer = &material->layers[layerID];
        uint32_t textureId = layer->textureId;
        mdxTexture_t const *modeltex;
        texture_t const *texture;
        bool layerFog;

        if (textureId >= (uint32_t)model->num_textures) continue;
        R_Call(glEnable, GL_DEPTH_TEST);
        R_Call(glDisable, GL_CULL_FACE);
        shader->state.alphaKey = 0;
        if (!MDLX_SetLayerBlend(layer, layerID)) continue;
        MDLX_ApplyLayerFlags(layer);
        shader->state.unshaded = (layer->flags & MODEL_GEO_UNSHADED) ? 1 : 0;
        layerFog = tr.viewDef.fogEnable &&
            !(layer->flags & MODEL_GEO_UNFOGGED) &&
            (layer->blendMode == BLEND_MODE_NONE ||
             layer->blendMode == BLEND_MODE_ALPHAKEY ||
             layer->blendMode == BLEND_MODE_BLEND);
        shader->state.fogEnable = layerFog ? 1 : 0;
        modeltex = &model->textures[textureId];
        texture = MDLX_GetTexture(model, team & TEAM_MASK, textureId, modeltex->replaceableID, NULL);
        R_BindTexture(texture, 0);
        R_StatsDraw(GL_TRIANGLES, nverts, 1);
        R_ApplyShader(shader);
        R_Call(glDrawArrays, GL_TRIANGLES, 0, (GLsizei)nverts);
    }
}

void MDLX_RenderRibbonEmitters(renderEntity_t const *entity, mdxModel_t const *model, mat4_t const *model_matrix) {
    modelProg_t *shader;
    mat4_t saved_model;
    int saved_unshaded, saved_fog;

    if (!entity || !model || !model->ribbons || !model_matrix) return;
    if ((entity->flags & RF_NOT_SELECTABLE) && entity->oldframe == entity->frame) return;
    shader = mdlx.shader;
    if (!shader) return;
    saved_model = shader->state.model;
    saved_unshaded = shader->state.unshaded;
    saved_fog = shader->state.fogEnable;
    FOR_EACH_LIST(mdxRibbonEmitter_t, ribbon, model->ribbons) {
        uint32_t nverts = MDLX_EmitRibbonVertices((mdxModel_t *)model, entity, model_matrix, ribbon,
                                               ribbon_verts, TRAIL_MAX_EDGES * 6);
        MDLX_DrawRibbonVerts(model, ribbon_verts, nverts,
                             MDLX_MaterialAt(model, ribbon->materialId), entity->team);
    }
    shader->state.model = saved_model;
    shader->state.unshaded = saved_unshaded;
    shader->state.fogEnable = saved_fog;
    shader->state.layerAlpha = 1.0f;
    shader->state.geosetColor = (vec4_t){ 1, 1, 1, 1 };
    R_Call(glEnable, GL_CULL_FACE);
    R_Call(glDepthMask, GL_TRUE);
    R_SetAlphaKeyState(false);
}

void MDX_RenderModel(renderEntity_t const *entity,
                     mdxModel_t const *model,
                     mat4_t const *transform)
{
    if (!(tr.viewDef.rdflags & RDF_NOFRUSTUMCULL)) {
        vec3_t const center = Box3_Center(&model->bounds.box);
        sphere3_t const sphere = {
            .center = Matrix4_multiply_vector3(transform, &center),
            .radius = model->bounds.radius * entity->scale,
        };
        if (!Frustum_ContainsSphere(&tr.viewDef.frustum, &sphere))
            return;
        if (!Frustum_ContainsBox(&tr.viewDef.frustum, &model->bounds.box, transform))
            return;
    }

    renderEntity_t remappedEntity;
    if (entity->flags & RF_HAS_LUMBER) {
        remappedEntity = *entity;
        remappedEntity.frame = MDLX_RemapAnimation(model, remappedEntity.frame, "Lumber");
        remappedEntity.oldframe = MDLX_RemapAnimation(model, remappedEntity.oldframe, "Lumber");
        entity = &remappedEntity;
    } else if (entity->flags & RF_HAS_GOLD) {
        remappedEntity = *entity;
        remappedEntity.frame = MDLX_RemapAnimation(model, remappedEntity.frame, "Gold");
        remappedEntity.oldframe = MDLX_RemapAnimation(model, remappedEntity.oldframe, "Gold");
        entity = &remappedEntity;
    }
    
    modelProg_t *shader = mdlx.shader;
    mat3_t normalMatrix;
    GLfloat const *viewProjectionMatrix =
#ifdef USE_SHADOWMAPS
        tr.render_phase == RENDER_PHASE_LIGHTS ? tr.viewDef.lightMatrix.v :
#endif
        tr.viewDef.viewProjectionMatrix.v;
    Matrix3_normal(&normalMatrix, transform);

    shader->state.model = *transform;
    shader->state.normalMatrix = normalMatrix;
    shader->state.unshaded = (entity->flags & RF_NO_LIGHTING) != 0;

    /* uViewProjectionMatrix/uTextureMatrix/uLightMatrix/fog uniforms are the
       same for every entity drawn within one R_DrawEntities pass (one view).
       Re-uploading them per-instance was pure overhead; skip when unchanged. */
    static struct {
        GLfloat vp[16];
        mat4_t tex, light;
        bool fogEnable;
        vec3_t fogColor;
        float fogStart, fogEnd;
    } last;
    static bool last_valid = false;
    bool const view_changed = !last_valid
        || memcmp(last.vp, viewProjectionMatrix, sizeof(last.vp)) != 0
        || memcmp(&last.tex, &tr.viewDef.textureMatrix, sizeof(last.tex)) != 0
        || memcmp(&last.light, &tr.viewDef.lightMatrix, sizeof(last.light)) != 0
        || last.fogEnable != tr.viewDef.fogEnable
        || (tr.viewDef.fogEnable &&
            (memcmp(&last.fogColor, &tr.viewDef.fogColor, sizeof(last.fogColor)) != 0
             || last.fogStart != tr.viewDef.fogStart
             || last.fogEnd != tr.viewDef.fogEnd));
    if (view_changed) {
        memcpy(&shader->state.viewProjection, viewProjectionMatrix, (1) * sizeof(mat4_t));
        shader->state.textureMatrix = tr.viewDef.textureMatrix;
        shader->state.lightMatrix = tr.viewDef.lightMatrix;
        shader->state.fogEnable = tr.viewDef.fogEnable ? 1 : 0;
        shader->state.firstBoneLookupIndex = 0.0f;
        if (tr.viewDef.fogEnable) {
            shader->state.fogColor = (vec3_t){ tr.viewDef.fogColor.x, tr.viewDef.fogColor.y, tr.viewDef.fogColor.z };
            shader->state.fogParams = (vec2_t){ tr.viewDef.fogStart, tr.viewDef.fogEnd };
        }
        memcpy(last.vp, viewProjectionMatrix, sizeof(last.vp));
        last.tex = tr.viewDef.textureMatrix;
        last.light = tr.viewDef.lightMatrix;
        last.fogEnable = tr.viewDef.fogEnable;
        last.fogColor = tr.viewDef.fogColor;
        last.fogStart = tr.viewDef.fogStart;
        last.fogEnd = tr.viewDef.fogEnd;
        last_valid = true;
    }
    modelLighting_t lighting = { 0 };
    bool const portraitLighting = (entity->flags & RF_PORTRAIT_LIGHTING) != 0;
    environLight_t const *environment = !portraitLighting && tr.viewDef.entityLight.valid
        ? &tr.viewDef.entityLight
        : (!portraitLighting && tr.viewDef.terrainLight.valid ? &tr.viewDef.terrainLight : NULL);
    int numLights = 0;

    if (environment && R_LightingFromEnviron(environment, &lighting))
        numLights = lighting.count;

    /* Environment light is already on viewDef from R_SetupEnvironmentLighting.
     * Rebind this entity before evaluating its local lights or rendering. */
    MDLX_BindBoneMatrices(model, transform, entity->frame, entity->oldframe);
    numLights += MDLX_CollectModelLights(model, transform, entity->frame,
                                        &lighting.lights[numLights],
                                        BZ_MODEL_LIGHT_MAX - numLights);
    float ambient = numLights ? (portraitLighting ? 0.22f : 0.0f)
                              : (portraitLighting ? 0.58f : 0.35f);
    float directional = (entity->flags & RF_PORTRAIT_LIGHTING) ? 0.62f : 0.75f;
    vec3_t lightDir = {
        -tr.viewDef.lightMatrix.v[2],
        -tr.viewDef.lightMatrix.v[6],
        -tr.viewDef.lightMatrix.v[10],
    };
    rModelLight_t sun = {
        .dir = lightDir,
        .color = { directional, directional, directional },
        .intensity = 1.0f,
        .type = R_MODEL_LIGHT_DIRECT,
    };
    lighting.ambient = (vec3_t){ ambient, ambient, ambient };
    lighting.count = numLights ? numLights : 1;
    if (!numLights) lighting.lights[0] = sun;
    R_SetModelLighting(shader, &lighting);

    if (entity->flags & RF_NO_FOGOFWAR) {
        R_Call(glActiveTexture, GL_TEXTURE2);
        R_Call(glBindTexture, GL_TEXTURE_2D, tr.texture[TEX_WHITE]->texid);
        R_Call(glActiveTexture, GL_TEXTURE0);
    }
    MDLX_RenderGeosets(entity, model);
    
    MDLX_RenderParticleEmitters(entity, model, transform);
    MDLX_RenderRibbonEmitters(entity, model, transform);

    if ((entity->flags & RF_NO_FOGOFWAR) && tr.world) {
        R_Call(glActiveTexture, GL_TEXTURE2);
        R_Call(glBindTexture, GL_TEXTURE_2D, R_GetFogOfWarTexture());
        R_Call(glActiveTexture, GL_TEXTURE0);
    }
}
