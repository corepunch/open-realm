#include "r_local.h"
#include "r_game.h"
#ifdef WOW
#include "common/ui_constants.h"
#include "common/wow_view.h"
#endif
#include <float.h>
#include <math.h>
#include <stdlib.h>

/* The Warcraft III native model basis is identity. Skip the full 4x4
 * multiplication when a game supplies that basis, while leaving rotated,
 * translated or axis-converted native bases on the original composition path.
 * Checking all 16 coefficients avoids assuming anything about the current
 * game module or model/attachment type. */
static mat4_t const r_identity_entity_basis = { .v = {
    1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
} };

/* With no pitch or roll, the generic orientation quaternion reduces to
 * (0, 0, sin(yaw/2), cos(yaw/2)). Construct the same column-major scaled
 * placement directly instead of evaluating four unused trig functions and
 * converting a general quaternion. Keep the half-angle convention: sin(yaw)
 * and cos(yaw) do not round identically to the original quaternion path.
 * The rotation origin supplied by R_GetEntityMatrix is always zero. */
static void R_YawEntityPlacement(mat4_t *out, modelPose_t const *pose) {
    float const half = pose->angles.yaw * 0.5f;
    float const sine = sinf(half), cosine = cosf(half);
    float const doubled_sine = sine + sine;
    float const diagonal = 1.0f - sine * doubled_sine;
    float const off_diagonal = cosine * doubled_sine;
    float const scale = pose->scale;

    *out = (mat4_t){ .v = {
        diagonal * scale, off_diagonal * scale, 0, 0,
        -off_diagonal * scale, diagonal * scale, 0, 0,
        0, 0, scale, 0,
        pose->origin.x, pose->origin.y, pose->origin.z, 1,
    } };
}

void R_GetEntityMatrix(renderEntity_t const *entity, mat4_t *matrix) {
    modelPose_t pose = { .origin = entity->origin, .angles.yaw = entity->angle, .scale = entity->scale };
    mat4_t const *basis = R_EntityPose(entity, &pose);
    mat4_t placement;

    /* R_EntityPose can author pitch/roll or non-finite data (e.g. WoW
     * doodads). Preserve the generic path for those cases, including its
     * NaN/Inf propagation, rather than changing culling in invalid poses. */
    if (pose.angles.pitch == 0.0f && pose.angles.roll == 0.0f &&
        isfinite(pose.angles.yaw) && isfinite(pose.scale) &&
        isfinite(pose.origin.x) && isfinite(pose.origin.y) &&
        isfinite(pose.origin.z)) {
        R_YawEntityPlacement(&placement, &pose);
    } else {
        quaternion_t rotation = Quaternion_fromOrientation(&pose.angles);
        Matrix4_from_rotation_translation_scale_origin(&placement, &rotation, &pose.origin,
            &MAKE(vec3_t, pose.scale, pose.scale, pose.scale), &MAKE(vec3_t, 0, 0, 0));
    }
    /* Native skinning precedes this basis. All world-space consumers use this
     * same composition. An identity basis leaves the placement unchanged;
     * don't run 16 four-term dot products during every entity cull. */
    if (!memcmp(basis, &r_identity_entity_basis, sizeof(r_identity_entity_basis))) {
        *matrix = placement;
    } else {
        Matrix4_multiply(&placement, basis, matrix);
    }
}

/* A socket already includes the parent's model basis. Apply only the child's local pose. */
void R_GetAttachmentMatrix(renderEntity_t const *entity, mat4_t const *socket, mat4_t *matrix) {
    quaternion_t rotation = Quaternion_fromOrientation(&entity->attachment.angles);
    *matrix = *socket;
    Matrix4_rotateQuat(matrix, &rotation);
}

static int R_DebugEntities(void) {
    return atoi(ri.CvarString ? ri.CvarString("r_debug_entities", "0") : "0");
}

static float R_EntityRingZ(renderEntity_t const *entity) {
    box3_t bounds;

    if (R_GetEntityBounds(entity, &bounds))
        return entity->origin.z + bounds.min.z * entity->scale - 1.0f;
    return entity->origin.z - 1.0f;
}

static bool R_EntityNeedsSupportedOverlay(renderEntity_t const *entity) {
    return !!(entity->flags & (RF_SELECTION_CIRCLE_ON_WATER | RF_GROUND_SURFACE_SUPPORT));
}

static float R_EntitySupportRingZ(renderEntity_t const *entity) {
    /* WC3 supplies the water/deck support height in origin.z-ground_offset.
     * Lift the overlay slightly to pass the support surface's depth test. */
    return entity->origin.z - entity->ground_offset + 1.0f;
}

static bool R_EntityInView(renderEntity_t const *entity) {
    box3_t bounds;
    mat4_t matrix;
    float radius;

    if (!entity || (entity->flags & RF_HIDDEN) || !entity->model) {
        return false;
    }
    if (tr.viewDef.rdflags & RDF_NOFRUSTUMCULL) {
        return true;
    }

    /* Model bounds cover tall map art whose gameplay selection radius is only
     * the ground footprint; keep the radius path for models without bounds. */
    if (R_GetEntityBoundsAndMatrix(entity, &bounds, &matrix)) {
        return Frustum_ContainsBox(&tr.viewDef.frustum, &bounds, &matrix);
    }

    radius = MAX(entity->radius * MAX(entity->scale, 1.0f), 16.0f);
    return Frustum_ContainsSphere(&tr.viewDef.frustum, &(sphere3_t){
        .center = entity->origin,
        .radius = radius,
    });
}

/* WC3's alpha pass can reuse the outer opaque pass's frustum result rather
 * than revisiting offscreen models. Store by SCENE SLOT, never entity number:
 * transient model children can carry the same number as their parent.
 * The generation avoids clearing 16,000 bytes on every view (including UI
 * portraits); a second view or an interrupted pass cannot reuse old results. */
static struct {
    bool active;
    uint16_t generation;
    uint16_t slot_generation[MAX_GAME_ENTITIES];
    uint8_t slot_visible[MAX_GAME_ENTITIES];
    renderEntity_t const *scene;
    uint32_t count, time, rdflags;
    mat4_t view_projection;
    frustum3_t frustum;
} solid_visibility;

void R_SolidVisibilityBegin(void) {
    if (++solid_visibility.generation == 0) {
        memset(solid_visibility.slot_generation, 0, sizeof(solid_visibility.slot_generation));
        solid_visibility.generation = 1;
    }
    solid_visibility.active = true;
    solid_visibility.scene = tr.viewDef.entities;
    solid_visibility.count = tr.viewDef.num_entities;
    solid_visibility.time = tr.viewDef.time;
    solid_visibility.rdflags = tr.viewDef.rdflags;
    solid_visibility.view_projection = tr.viewDef.viewProjectionMatrix;
    solid_visibility.frustum = tr.viewDef.frustum;
}

void R_SolidVisibilityEnd(void) {
    solid_visibility.active = false;
}

/* Check the view only once at the start of the alpha pass; no matrix memcmp
 * or pointer arithmetic is needed per alpha entity. A negative status means
 * unknown (e.g. a camera-only view or r_entities=0) and must NOT be culled. */
bool R_SolidVisibilityMatchesView(void) {
    return solid_visibility.active &&
           solid_visibility.scene == tr.viewDef.entities &&
           solid_visibility.count == tr.viewDef.num_entities &&
           solid_visibility.time == tr.viewDef.time &&
           solid_visibility.rdflags == tr.viewDef.rdflags &&
           !memcmp(&solid_visibility.view_projection, &tr.viewDef.viewProjectionMatrix,
                   sizeof(solid_visibility.view_projection)) &&
           !memcmp(&solid_visibility.frustum, &tr.viewDef.frustum,
                   sizeof(solid_visibility.frustum));
}

int R_SolidVisibilityAt(uint32_t index) {
    if (index >= MAX_GAME_ENTITIES ||
        solid_visibility.slot_generation[index] != solid_visibility.generation)
        return -1;
    return solid_visibility.slot_visible[index] != 0;
}

static void R_SolidVisibilityRecord(uint32_t index, bool visible) {
    if (!solid_visibility.active || tr.render_phase != RENDER_PHASE_SOLID ||
        index >= MAX_GAME_ENTITIES ||
        solid_visibility.scene != tr.viewDef.entities ||
        solid_visibility.count != tr.viewDef.num_entities) return;
    solid_visibility.slot_generation[index] = solid_visibility.generation;
    solid_visibility.slot_visible[index] = visible;
}

static void R_DrawEntityShadows(bool shad);

void R_DrawEntities(void) {
    static uint8_t prev_state[MAX_GAME_ENTITIES];
    static bool initialized = false;
    uint8_t state[MAX_GAME_ENTITIES];
    int debug_entities = R_DebugEntities();
    uint32_t drawn = 0;
    uint32_t culled = 0;

    if (!R_CvarEnabled("r_entities", "1")) {
        R_SolidVisibilityEnd(); /* alpha must treat unrecorded entries as unknown */
        return;
    }
    if (debug_entities) {
        memset(state, 0, sizeof(state));
    } else {
        initialized = false;
    }

    bool shad = R_CvarEnabled("r_unit_shadows", "1");

    R_DrawEntityShadows(shad);

    FOR_LOOP(i, tr.viewDef.num_entities) {
        renderEntity_t const *ent = tr.viewDef.entities+i;
        R_UpdateEntityPresentation(ent);
        bool in_view = R_EntityInView(ent);
        R_SolidVisibilityRecord(i, in_view);

        if (debug_entities && ent->number < MAX_GAME_ENTITIES) {
            state[ent->number] = in_view ? 2 : 1;
        }
        if (in_view) {
            drawn++;
            R_DrawEntity(ent, shad);
        } else {
            culled++;
        }
    }

    if (debug_entities) {
        if (initialized) {
            FOR_LOOP(i, MAX_GAME_ENTITIES) {
                if (prev_state[i] == state[i]) {
                    continue;
                }
                if (!prev_state[i] && state[i]) {
                    fprintf(stderr,
                            "R entity entered frame_time=%u ent=%u state=%s\n",
                            (unsigned)tr.viewDef.time,
                            (unsigned)i,
                            state[i] == 2 ? "drawn" : "culled");
                } else if (prev_state[i] && !state[i]) {
                    fprintf(stderr,
                            "R entity left frame_time=%u ent=%u prev=%s\n",
                            (unsigned)tr.viewDef.time,
                            (unsigned)i,
                            prev_state[i] == 2 ? "drawn" : "culled");
                } else {
                    fprintf(stderr,
                            "R entity cull-change frame_time=%u ent=%u %s->%s\n",
                            (unsigned)tr.viewDef.time,
                            (unsigned)i,
                            prev_state[i] == 2 ? "drawn" : "culled",
                            state[i] == 2 ? "drawn" : "culled");
                }
            }
        }
        if (debug_entities > 1) {
            fprintf(stderr,
                    "R entity summary frame_time=%u view=%u drawn=%u culled=%u\n",
                    (unsigned)tr.viewDef.time,
                    (unsigned)tr.viewDef.num_entities,
                    (unsigned)drawn,
                    (unsigned)culled);
        }
        memcpy(prev_state, state, sizeof(prev_state));
        initialized = true;
    }
}

void R_DrawSplatRects(void) {
    if (!tr.viewDef.num_splat_rects || !tr.viewDef.splat_rects) {
        return;
    }

    R_BeginSplatBatch(R_SPLAT_SHADER(&tr.shader_splat));
    FOR_LOOP(i, tr.viewDef.num_splat_rects) {
        renderSplatRect_t const *rect = tr.viewDef.splat_rects + i;
        box3_t bounds;

        if (rect->maxs.x <= rect->mins.x || rect->maxs.y <= rect->mins.y) {
            continue;
        }
        if (!(tr.viewDef.rdflags & RDF_NOFRUSTUMCULL)) {
            bounds = (box3_t){
                .min = { rect->mins.x, rect->mins.y, -4096.0f },
                .max = { rect->maxs.x, rect->maxs.y, 4096.0f },
            };
            if (!Frustum_ContainsAABox(&tr.viewDef.frustum, &bounds)) {
                continue;
            }
        }
        R_AddRectSplat(&rect->mins, &rect->maxs, tr.texture[TEX_WHITE], rect->color);
    }
    R_EndSplatBatch();
}

void R_DrawDecals(void) {
    /* Consecutive decals preserve array order and share texture/shader state. */
    R_BeginSplatBatch(R_SPLAT_SHADER(&tr.shader_splat));
    FOR_LOOP(i, tr.viewDef.num_decals) {
        renderDecal_t const *decal = tr.viewDef.decals + i;
        box3_t bounds;

        if (!decal->texture || decal->radius <= 0.0f) {
            continue;
        }
        if (!(tr.viewDef.rdflags & RDF_NOFRUSTUMCULL)) {
            bounds = (box3_t){
                .min = { decal->origin.x - decal->radius, decal->origin.y - decal->radius, -4096.0f },
                .max = { decal->origin.x + decal->radius, decal->origin.y + decal->radius, 4096.0f },
            };
            if (!Frustum_ContainsAABox(&tr.viewDef.frustum, &bounds)) {
                continue;
            }
        }
        vec2_t const mins = { decal->origin.x - decal->radius, decal->origin.y - decal->radius };
        vec2_t const maxs = { decal->origin.x + decal->radius, decal->origin.y + decal->radius };
        R_AddRectSplat(&mins, &maxs, decal->texture, decal->color);
    }
    R_EndSplatBatch();
}

uint32_t selCircles[NUM_SELECTION_CIRCLES] = { 100, 300, 100000 };

static void R_RenderUberSplat(renderEntity_t const *entity, vec2_t const *origin) {
    if (R_ShouldRenderUberSplat(entity)) {
        R_RenderSplat(origin, entity->splatsize, entity->splat, R_SPLAT_SHADER(&tr.shader_default), COLOR32_WHITE);
    }
}

static void R_DrawEntityShadow(renderEntity_t const *entity, vec2_t const *origin, bool shad) {
#ifndef USE_SHADOWMAPS
    texture_t const *shadow = entity->shadow;
    box3_t bounds;

    if (R_RenderShadow(entity, origin)) {
        return;
    }
    if (!shad || !shadow || (entity->flags & RF_NO_SHADOW) || !tr.world) {
        return;
    }

    vec2_t mins;
    vec2_t maxs;
    if (entity->shadow_rect.w > 0 && entity->shadow_rect.h > 0) {
        mins.x = origin->x - entity->shadow_rect.x;
        mins.y = origin->y - entity->shadow_rect.y;
        maxs.x = mins.x + entity->shadow_rect.w;
        maxs.y = mins.y + entity->shadow_rect.h;
    } else {
        int pivot_x = (int)(shadow->width * 0.3f + 0.5f);
        int pivot_y = (int)(shadow->height * 0.7f + 0.5f);
        float width = shadow->width * 32.0f;
        float height = shadow->height * 32.0f;
        mins.x = origin->x - pivot_x * 32.0f;
        mins.y = origin->y - (shadow->height - pivot_y) * 32.0f;
        maxs.x = mins.x + width;
        maxs.y = mins.y + height;
    }

    color32_t shadowColor = {0, 0, 0, 128};
    if (!(tr.viewDef.rdflags & RDF_NOFRUSTUMCULL)) {
        bounds = (box3_t){
            .min = { mins.x, mins.y, entity->origin.z - 32.0f },
            .max = { maxs.x, maxs.y, entity->origin.z + 32.0f },
        };
        if (!Frustum_ContainsAABox(&tr.viewDef.frustum, &bounds)) {
            return;
        }
    }
    R_AddRectSplat(&mins, &maxs, shadow, shadowColor);
#endif
}

/* Unit shadows are ground decals that share one shader and differ only by
 * texture and rect.  Drawing each in isolation re-uploaded the vertex buffer
 * and re-issued all splat GL state per unit; batching them across the scene
 * collapses runs of same-texture shadows into one upload + draw (flushing on
 * texture change or buffer capacity), instead of one per unit. */
static void R_DrawEntityShadows(bool shad) {
#ifndef USE_SHADOWMAPS
    if (!shad) return;
    R_BeginSplatBatch(R_SPLAT_SHADER(&tr.shader_shadowSplat));
    FOR_LOOP(i, tr.viewDef.num_entities) {
        renderEntity_t const *ent = tr.viewDef.entities + i;
        if ((ent->flags & RF_HIDDEN) || !ent->model) {
            continue;
        }
        R_DrawEntityShadow(ent, (vec2_t const *)&ent->origin, shad);
    }
    R_EndSplatBatch();
#endif
}

static void R_RenderSelectedCircle(renderEntity_t const *entity, vec2_t const *origin, bool supported_pass) {
    if (entity->flags & RF_SELECTED) {
        bool const supported_overlay = R_EntityNeedsSupportedOverlay(entity);
        if (supported_overlay != supported_pass) return;
        color32_t color;
        if (entity->flags & RF_HOSTILE) {
            color = MAKE(color32_t, 255, 80, 80, 255);
        } else if (entity->flags & RF_NEUTRAL) {
            color = MAKE(color32_t, 255, 220, 80, 255);
        } else {
            color = MAKE(color32_t, 80, 200, 80, 255);
        }
        float radius = R_SelectionRadius(entity);
        FOR_LOOP(i, NUM_SELECTION_CIRCLES) {
            if ((radius * 2) > selCircles[i])
                continue;
            vec2_t mins = { origin->x - radius, origin->y - radius };
            vec2_t maxs = { origin->x + radius, origin->y + radius };
            /* Rings on water or walkable destructables are flat and deferred
             * until after surfaces draw. Ordinary ground rings follow terrain. */
            if (supported_overlay)
                R_RenderFlatRectSplat(&mins, &maxs, R_EntitySupportRingZ(entity),
                                      tr.texture[TEX_SELECTION_CIRCLE+i], R_SPLAT_SHADER(&tr.shader_splat), color);
            else if (entity->ground_offset > 0.0f)
                R_RenderFlatRectSplat(&mins, &maxs, R_EntityRingZ(entity),
                                      tr.texture[TEX_SELECTION_CIRCLE+i], R_SPLAT_SHADER(&tr.shader_splat), color);
            else
                R_RenderSplat(origin, radius, tr.texture[TEX_SELECTION_CIRCLE+i],
                              R_SPLAT_SHADER(&tr.shader_splat), color);
            break;
        }
    }
}

static void R_RenderEntityIndicator(renderEntity_t const *entity, vec2_t const *origin, bool supported_pass) {
    if (!entity->indicator.a) return;
    bool const supported_overlay = R_EntityNeedsSupportedOverlay(entity);
    if (supported_overlay != supported_pass) return;

    float radius = R_SelectionRadius(entity);
    FOR_LOOP(i, NUM_SELECTION_CIRCLES) {
        if ((radius * 2) > selCircles[i]) continue;
        vec2_t mins = { entity->origin.x - radius, entity->origin.y - radius };
        vec2_t maxs = { entity->origin.x + radius, entity->origin.y + radius };
        if (supported_overlay)
            R_RenderFlatRectSplat(&mins, &maxs, R_EntitySupportRingZ(entity),
                                  tr.texture[TEX_SELECTION_CIRCLE+i], R_SPLAT_SHADER(&tr.shader_splat), entity->indicator);
        else if (entity->ground_offset > 0.0f)
            R_RenderFlatRectSplat(&mins, &maxs, R_EntityRingZ(entity),
                                  tr.texture[TEX_SELECTION_CIRCLE+i], R_SPLAT_SHADER(&tr.shader_splat), entity->indicator);
        else
            R_RenderSplat(origin, radius, tr.texture[TEX_SELECTION_CIRCLE+i],
                          R_SPLAT_SHADER(&tr.shader_splat), entity->indicator);
        break;
    }
}

/* Subtle highlight circle for the entity under the mouse cursor. */
static void R_RenderHoverHighlight(renderEntity_t const *entity, bool supported_pass) {
    if (entity->number != tr.viewDef.hover_entity || entity->number == 0) {
        return;
    }
    if (entity->flags & RF_SELECTED) {
        return; /* selection circle already visible, skip hover */
    }
    bool const supported_overlay = R_EntityNeedsSupportedOverlay(entity);
    if (supported_overlay != supported_pass) return;
    color32_t color;
    if (entity->flags & RF_HOSTILE) {
        color = MAKE(color32_t, 255, 80, 80, 128);   /* enemy: faint red */
    } else if (entity->flags & RF_NEUTRAL) {
        color = MAKE(color32_t, 255, 220, 80, 128);  /* neutral/passive ally: faint yellow */
    } else {
        color = MAKE(color32_t, 80, 200, 80, 128);   /* own/shared-control: faint green */
    }
    float radius = R_SelectionRadius(entity);
    FOR_LOOP(i, NUM_SELECTION_CIRCLES) {
        if ((radius * 2) > selCircles[i])
            continue;
        vec2_t mins = { entity->origin.x - radius, entity->origin.y - radius };
        vec2_t maxs = { entity->origin.x + radius, entity->origin.y + radius };
        if (supported_overlay)
            R_RenderFlatRectSplat(&mins, &maxs, R_EntitySupportRingZ(entity),
                                  tr.texture[TEX_SELECTION_CIRCLE+i], R_SPLAT_SHADER(&tr.shader_splat), color);
        else if (entity->ground_offset > 0.0f)
            R_RenderFlatRectSplat(&mins, &maxs, R_EntityRingZ(entity),
                                  tr.texture[TEX_SELECTION_CIRCLE+i], R_SPLAT_SHADER(&tr.shader_splat), color);
        else
            R_RenderSplat(&(vec2_t){ entity->origin.x, entity->origin.y },
                          radius, tr.texture[TEX_SELECTION_CIRCLE+i],
                          R_SPLAT_SHADER(&tr.shader_splat), color);
        break;
    }
}

void R_DrawEntity(renderEntity_t const *entity, bool shad) {
    if ((entity->flags & RF_HIDDEN) || !entity->model)
        return;

#ifdef USE_SHADOWMAPS
    if (tr.render_phase == RENDER_PHASE_LIGHTS) {
        if (shad && !(entity->flags & RF_NO_SHADOW))
            R_RenderModel(entity);
        return;
    }
#endif

    R_RenderUberSplat(entity, (vec2_t const *)&entity->origin);
    R_RenderModel(entity);
    R_RenderSelectedCircle(entity, (vec2_t const *)&entity->origin, false);
    R_RenderHoverHighlight(entity, false);
    R_RenderEntityIndicator(entity, (vec2_t const *)&entity->origin, false);
}

void R_DrawSupportedEntityOverlays(void) {
    FOR_LOOP(i, tr.viewDef.num_entities) {
        renderEntity_t const *entity = tr.viewDef.entities + i;
        if ((entity->flags & RF_HIDDEN) || !entity->model || !R_EntityNeedsSupportedOverlay(entity))
            continue;
        R_RenderSelectedCircle(entity, (vec2_t const *)&entity->origin, true);
        R_RenderHoverHighlight(entity, true);
        R_RenderEntityIndicator(entity, (vec2_t const *)&entity->origin, true);
    }
}
