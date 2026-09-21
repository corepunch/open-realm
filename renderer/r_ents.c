#include "r_local.h"
#include "r_game.h"
#ifdef WOW
#include "common/ui_constants.h"
#include "common/wow_view.h"
#endif
#include <float.h>
#include <stdlib.h>

void R_GetEntityMatrix(renderEntity_t const *entity, LPMATRIX4 matrix) {
    VECTOR3 origin = entity->origin;

    if (R_EntityMatrix(entity, matrix)) {
        return;
    }

    Matrix4_identity(matrix);
    Matrix4_translate(matrix, &origin);
    Matrix4_rotate(matrix, &(VECTOR3){0, 0, entity->angle * 180 / M_PI}, ROTATE_XYZ);
    Matrix4_scale(matrix, &(VECTOR3){entity->scale, entity->scale, entity->scale});
}

static int R_DebugEntities(void) {
    return atoi(ri.CvarString ? ri.CvarString("r_debug_entities", "0") : "0");
}

static FLOAT R_EntityRingZ(renderEntity_t const *entity) {
    BOX3 bounds;

    if (R_GetEntityBounds(entity, &bounds))
        return entity->origin.z + bounds.min.z * entity->scale - 1.0f;
    return entity->origin.z - 1.0f;
}

static BOOL R_EntityInView(renderEntity_t const *entity) {
    BOX3 bounds;
    MATRIX4 matrix;
    float radius;

    if (!entity || (entity->flags & RF_HIDDEN) || !entity->model) {
        return false;
    }
    if (tr.viewDef.rdflags & RDF_NOFRUSTUMCULL) {
        return true;
    }

    /* Model bounds cover tall map art whose gameplay selection radius is only
     * the ground footprint; keep the radius path for models without bounds. */
    if (R_GetEntityBounds(entity, &bounds)) {
        R_GetEntityMatrix(entity, &matrix);
        return Frustum_ContainsBox(&tr.viewDef.frustum, &bounds, &matrix);
    }

    radius = MAX(entity->radius * MAX(entity->scale, 1.0f), 16.0f);
    return Frustum_ContainsSphere(&tr.viewDef.frustum, &(SPHERE3){
        .center = entity->origin,
        .radius = radius,
    });
}

static void R_DrawEntityShadows(BOOL shad);

void R_DrawEntities(void) {
    static BYTE prev_state[MAX_GAME_ENTITIES];
    static BOOL initialized = false;
    BYTE state[MAX_GAME_ENTITIES];
    int debug_entities = R_DebugEntities();
    DWORD drawn = 0;
    DWORD culled = 0;

    if (!R_CvarEnabled("r_entities", "1")) return;
    if (debug_entities) {
        memset(state, 0, sizeof(state));
    } else {
        initialized = false;
    }

    BOOL shad = R_CvarEnabled("r_unit_shadows", "1");

    R_DrawEntityShadows(shad);

    FOR_LOOP(i, tr.viewDef.num_entities) {
        renderEntity_t const *ent = tr.viewDef.entities+i;
        R_UpdateEntityPresentation(ent);
        BOOL in_view = R_EntityInView(ent);

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
        BOX3 bounds;

        if (rect->maxs.x <= rect->mins.x || rect->maxs.y <= rect->mins.y) {
            continue;
        }
        if (!(tr.viewDef.rdflags & RDF_NOFRUSTUMCULL)) {
            bounds = (BOX3){
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
    FOR_LOOP(i, tr.viewDef.num_decals) {
        renderDecal_t const *decal = tr.viewDef.decals + i;
        BOX3 bounds;

        if (!decal->texture || decal->radius <= 0.0f) {
            continue;
        }
        if (!(tr.viewDef.rdflags & RDF_NOFRUSTUMCULL)) {
            bounds = (BOX3){
                .min = { decal->origin.x - decal->radius, decal->origin.y - decal->radius, -4096.0f },
                .max = { decal->origin.x + decal->radius, decal->origin.y + decal->radius, 4096.0f },
            };
            if (!Frustum_ContainsAABox(&tr.viewDef.frustum, &bounds)) {
                continue;
            }
        }
        R_RenderSplat(&decal->origin, decal->radius, decal->texture, R_SPLAT_SHADER(&tr.shader_splat), decal->color);
    }
}

DWORD selCircles[NUM_SELECTION_CIRCLES] = { 100, 300, 100000 };

static void R_RenderUberSplat(const renderEntity_t *entity, LPCVECTOR2 origin) {
    if (entity->splat && !(entity->flags & RF_NO_UBERSPLAT)) {
        R_RenderSplat(origin, entity->splatsize, entity->splat, R_SPLAT_SHADER(&tr.shader_default), COLOR32_WHITE);
    }
}

static void R_DrawEntityShadow(const renderEntity_t *entity, LPCVECTOR2 origin, BOOL shad) {
#ifndef USE_SHADOWMAPS
    LPCTEXTURE shadow = entity->shadow;
    BOX3 bounds;

    if (R_RenderShadow(entity, origin)) {
        return;
    }
    if (!shad || !shadow || (entity->flags & RF_NO_SHADOW) || !tr.world) {
        return;
    }

    VECTOR2 mins;
    VECTOR2 maxs;
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

    COLOR32 shadowColor = {0, 0, 0, 128};
    if (!(tr.viewDef.rdflags & RDF_NOFRUSTUMCULL)) {
        bounds = (BOX3){
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
static void R_DrawEntityShadows(BOOL shad) {
#ifndef USE_SHADOWMAPS
    if (!shad) return;
    R_BeginSplatBatch(R_SPLAT_SHADER(&tr.shader_shadowSplat));
    FOR_LOOP(i, tr.viewDef.num_entities) {
        renderEntity_t const *ent = tr.viewDef.entities + i;
        if ((ent->flags & RF_HIDDEN) || !ent->model) {
            continue;
        }
        R_DrawEntityShadow(ent, (LPCVECTOR2)&ent->origin, shad);
    }
    R_EndSplatBatch();
#endif
}

static void R_RenderSelectedCircle(const renderEntity_t *entity, LPCVECTOR2 origin) {
    if (entity->flags & RF_SELECTED) {
        COLOR32 color;
        if (entity->flags & RF_HOSTILE) {
            color = MAKE(COLOR32, 255, 80, 80, 255);
        } else if (entity->flags & RF_NEUTRAL) {
            color = MAKE(COLOR32, 255, 220, 80, 255);
        } else {
            color = MAKE(COLOR32, 80, 200, 80, 255);
        }
        float radius = R_SelectionRadius(entity);
        FOR_LOOP(i, NUM_SELECTION_CIRCLES) {
            if ((radius * 2) > selCircles[i])
                continue;
            VECTOR2 mins = { origin->x - radius, origin->y - radius };
            VECTOR2 maxs = { origin->x + radius, origin->y + radius };
            /* Flying units carry their selection circle with them; ground units
             * retain terrain-conforming rings for ramps and uneven terrain. */
            if (entity->ground_offset > 0.0f)
                R_RenderFlatRectSplat(&mins, &maxs, R_EntityRingZ(entity),
                                      tr.texture[TEX_SELECTION_CIRCLE+i], R_SPLAT_SHADER(&tr.shader_splat), color);
            else
                R_RenderSplat(origin, radius, tr.texture[TEX_SELECTION_CIRCLE+i],
                              R_SPLAT_SHADER(&tr.shader_splat), color);
            break;
        }
    }
}

static void R_RenderEntityIndicator(renderEntity_t const *entity, LPCVECTOR2 origin) {
    if (!entity->indicator.a) return;

    float radius = R_SelectionRadius(entity);
    FOR_LOOP(i, NUM_SELECTION_CIRCLES) {
        if ((radius * 2) > selCircles[i]) continue;
        VECTOR2 mins = { entity->origin.x - radius, entity->origin.y - radius };
        VECTOR2 maxs = { entity->origin.x + radius, entity->origin.y + radius };
        if (entity->ground_offset > 0.0f)
            R_RenderFlatRectSplat(&mins, &maxs, R_EntityRingZ(entity),
                                  tr.texture[TEX_SELECTION_CIRCLE+i], R_SPLAT_SHADER(&tr.shader_splat), entity->indicator);
        else
            R_RenderSplat(origin, radius, tr.texture[TEX_SELECTION_CIRCLE+i],
                          R_SPLAT_SHADER(&tr.shader_splat), entity->indicator);
        break;
    }
}

/* Subtle highlight circle for the entity under the mouse cursor. */
static void R_RenderHoverHighlight(renderEntity_t const *entity) {
    if (entity->number != tr.viewDef.hover_entity || entity->number == 0) {
        return;
    }
    if (entity->flags & RF_SELECTED) {
        return; /* selection circle already visible, skip hover */
    }
    COLOR32 color;
    if (entity->flags & RF_HOSTILE) {
        color = MAKE(COLOR32, 255, 80, 80, 128);   /* enemy: faint red */
    } else if (entity->flags & RF_NEUTRAL) {
        color = MAKE(COLOR32, 255, 220, 80, 128);  /* neutral/passive ally: faint yellow */
    } else {
        color = MAKE(COLOR32, 80, 200, 80, 128);   /* own/shared-control: faint green */
    }
    float radius = R_SelectionRadius(entity);
    FOR_LOOP(i, NUM_SELECTION_CIRCLES) {
        if ((radius * 2) > selCircles[i])
            continue;
        VECTOR2 mins = { entity->origin.x - radius, entity->origin.y - radius };
        VECTOR2 maxs = { entity->origin.x + radius, entity->origin.y + radius };
        if (entity->ground_offset > 0.0f)
            R_RenderFlatRectSplat(&mins, &maxs, R_EntityRingZ(entity),
                                  tr.texture[TEX_SELECTION_CIRCLE+i], R_SPLAT_SHADER(&tr.shader_splat), color);
        else
            R_RenderSplat(&(VECTOR2){ entity->origin.x, entity->origin.y },
                          radius, tr.texture[TEX_SELECTION_CIRCLE+i],
                          R_SPLAT_SHADER(&tr.shader_splat), color);
        break;
    }
}

void R_DrawEntity(renderEntity_t const *entity, BOOL shad) {
    if ((entity->flags & RF_HIDDEN) || !entity->model)
        return;

#ifdef USE_SHADOWMAPS
    if (tr.render_phase == RENDER_PHASE_LIGHTS) {
        if (shad && !(entity->flags & RF_NO_SHADOW))
            R_RenderModel(entity);
        return;
    }
#endif

    R_RenderUberSplat(entity, (LPCVECTOR2)&entity->origin);
    R_RenderModel(entity);
    R_RenderSelectedCircle(entity, (LPCVECTOR2)&entity->origin);
    R_RenderHoverHighlight(entity);
    R_RenderEntityIndicator(entity, (LPCVECTOR2)&entity->origin);
}
