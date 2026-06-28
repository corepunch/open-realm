#include "r_local.h"
#include "r_game.h"
#include <float.h>
#include <stdlib.h>

void R_GetEntityMatrix(renderEntity_t const *entity, LPMATRIX4 matrix) {
    VECTOR3 origin = entity->origin;

    if (R_GameEntityMatrix(entity, matrix)) {
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

static BOOL R_EntityInView(renderEntity_t const *entity) {
    float radius;

    if (!entity || (entity->flags & RF_HIDDEN) || !entity->model) {
        return false;
    }
    if (tr.viewDef.rdflags & RDF_NOFRUSTUMCULL) {
        return true;
    }

    radius = MAX(entity->radius * MAX(entity->scale, 1.0f), 16.0f);
    return Frustum_ContainsSphere(&tr.viewDef.frustum, &(SPHERE3){
        .center = entity->origin,
        .radius = radius,
    });
}

void R_DrawEntities(void) {
    static BYTE prev_state[MAX_GAME_ENTITIES];
    static BOOL initialized = false;
    BYTE state[MAX_GAME_ENTITIES];
    int debug_entities = R_DebugEntities();
    DWORD drawn = 0;
    DWORD culled = 0;

    if (debug_entities) {
        memset(state, 0, sizeof(state));
    } else {
        initialized = false;
    }

    FOR_LOOP(i, tr.viewDef.num_entities) {
        renderEntity_t const *ent = tr.viewDef.entities+i;
        BOOL in_view = R_EntityInView(ent);

        if (debug_entities && ent->number < MAX_GAME_ENTITIES) {
            state[ent->number] = in_view ? 2 : 1;
        }
        if (in_view) {
            drawn++;
            R_RenderModel(ent);
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
        R_RenderSplat(&decal->origin, decal->radius, decal->texture, tr.shader[SHADER_SPLAT], decal->color);
    }
}

VECTOR2 R_PointToScreenSpace(float x, float y) {
    size2_t window = R_GetWindowSize();
    VECTOR2 p = {
        .x = (x / window.width - 0.5) * 2,
        .y = (0.5 - y / window.height) * 2,
    };
    return p;
}

LINE3 R_LineForScreenPoint(viewDef_t const *viewdef, float x, float y) {
    MATRIX4 invproj;
    Matrix4_inverse(&viewdef->viewProjectionMatrix, &invproj);
    VECTOR2 const p = R_PointToScreenSpace(x, y);
    LINE3 const line = {
        Matrix4_multiply_vector3(&invproj, &(VECTOR3 const) { p.x, p.y, 0 }),
        Matrix4_multiply_vector3(&invproj, &(VECTOR3 const) { p.x, p.y, 1 }),
    };
    return line;
}

bool R_TraceEntity(viewDef_t const *viewdef, float x, float y, LPDWORD number) {
    if (!viewdef || !number) {
        return false;
    }
    LINE3 const line = R_LineForScreenPoint(viewdef, x, y);
    FLOAT best = FLT_MAX;
    DWORD best_number = 0;

    FOR_LOOP(i, viewdef->num_entities) {
        renderEntity_t *ent = &viewdef->entities[i];
        FLOAT distance;

        if (R_GameTraceModel(ent, &line, &distance) && distance < best) {
            best = distance;
            best_number = ent->number;
        }
    }
    if (best_number) {
        *number = best_number;
        return true;
    }
    return false;
}

DWORD R_EntitiesInRect(viewDef_t const *viewdef, LPCRECT rect, DWORD max, LPDWORD array) {
    if (!viewdef || !rect || !array || max == 0) {
        return 0;
    }
    tr.viewDef = *viewdef;
    VECTOR2 const a = R_PointToScreenSpace(rect->x, rect->y);
    VECTOR2 const b = R_PointToScreenSpace(rect->x+rect->w, rect->y+rect->h);
    RECT const screen = {
        .x = MIN(a.x, b.x),
        .y = MIN(a.y, b.y),
        .w = MAX(a.x, b.x) - MIN(a.x, b.x),
        .h = MAX(a.y, b.y) - MIN(a.y, b.y),
    };
    DWORD count = 0;
    FOR_LOOP(i, viewdef->num_entities) {
        renderEntity_t const *ent = &viewdef->entities[i];
        VECTOR3 const org = Matrix4_multiply_vector3(&viewdef->viewProjectionMatrix, &ent->origin);
        if (ent->number != 0 && Rect_contains(&screen, (LPVECTOR2)&org)) {
            if (count >= max) {
                break;
            }
            array[count++] = ent->number;
        }
    }
    return count;
}

#ifdef USE_SHADOWMAPS
extern bool is_rendering_lights;
#endif
DWORD selCircles[NUM_SELECTION_CIRCLES] = { 100, 300, 100000 };

static void R_RenderUberSplat(const renderEntity_t *entity, LPCVECTOR2 origin) {
    if (entity->splat && !(entity->flags & RF_NO_UBERSPLAT)) {
        R_RenderSplat(origin, entity->splatsize, entity->splat, tr.shader[SHADER_DEFAULT], COLOR32_WHITE);
    }
}

static void R_RenderShadow(const renderEntity_t *entity, LPCVECTOR2 origin) {
    LPCTEXTURE shadow = entity->shadow;
    BOX3 bounds;

    if (R_GameRenderShadow(entity, origin)) {
        return;
    }
    if (!shadow || (entity->flags & RF_NO_SHADOW) || !tr.world) {
        return;
    }

    VECTOR2 mins;
    VECTOR2 maxs;
    if (entity->shadow_w > 0 && entity->shadow_h > 0) {
        mins.x = origin->x - entity->shadow_x;
        mins.y = origin->y - entity->shadow_y;
        maxs.x = mins.x + entity->shadow_w;
        maxs.y = mins.y + entity->shadow_h;
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
    R_RenderRectSplat(&mins, &maxs, shadow, tr.shader[SHADER_SHADOWSPLAT], shadowColor);
}

static void R_RenderSelectedCircle(const renderEntity_t *entity, LPCVECTOR2 origin) {
    if (entity->flags & RF_SELECTED) {
        COLOR32 color = { 0, 255, 0, 255 };
        float radius = entity->radius;
        FOR_LOOP(i, NUM_SELECTION_CIRCLES) {
            if ((radius * 2) > selCircles[i])
                continue;
            R_RenderSplat(origin, radius, tr.texture[TEX_SELECTION_CIRCLE+i], tr.shader[SHADER_SPLAT], color);
            break;
        }
    }
}

void R_RenderModel(renderEntity_t const *entity) {
    if ((entity->flags & RF_HIDDEN) || !entity->model)
        return;
    
#ifdef USE_SHADOWMAPS
    if (is_rendering_lights && (entity->flags & RF_NO_SHADOW))
        return;
#endif

#ifdef USE_SHADOWMAPS
    if (!is_rendering_lights)
#endif
        R_RenderUberSplat(entity, (LPCVECTOR2)&entity->origin);
#ifdef USE_SHADOWMAPS
    if (!is_rendering_lights)
#endif
        R_RenderShadow(entity, (LPCVECTOR2)&entity->origin);
    
    R_GameRenderModel(entity);
    
#ifdef USE_SHADOWMAPS
    if (is_rendering_lights)
        return;
#endif

    R_RenderSelectedCircle(entity, (LPCVECTOR2)&entity->origin);
}
