#include "r_local.h"
#include "r_game.h"
#ifdef WOW
#include "common/ui_constants.h"
#include "common/wow_view.h"
#endif
#include <float.h>
#include <stdlib.h>

#define DEST_DEBUG_INTERVAL 1000 // milliseconds; rate-limit each dead entity's shadow diagnostics

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

static void R_DrawEntityShadows(void);

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

    R_DrawEntityShadows();

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
        R_RenderSplat(&decal->origin, decal->radius, decal->texture, R_SPLAT_SHADER(&tr.shader_splat), decal->color);
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

        if (!ent->number || !ent->model || (ent->flags & (RF_HIDDEN | RF_NOT_SELECTABLE))) {
            continue;
        }
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
        if (!ent->number || !ent->model || (ent->flags & (RF_HIDDEN | RF_NOT_SELECTABLE))) {
            continue;
        }
        VECTOR3 const org = Matrix4_multiply_vector3(&viewdef->viewProjectionMatrix, &ent->origin);
        if (Rect_contains(&screen, (LPVECTOR2)&org)) {
            if (count >= max) {
                break;
            }
            array[count++] = ent->number;
        }
    }
    return count;
}

DWORD selCircles[NUM_SELECTION_CIRCLES] = { 100, 300, 100000 };

static void R_RenderUberSplat(const renderEntity_t *entity, LPCVECTOR2 origin) {
    if (entity->splat && !(entity->flags & RF_NO_UBERSPLAT)) {
        R_RenderSplat(origin, entity->splatsize, entity->splat, R_SPLAT_SHADER(&tr.shader_default), COLOR32_WHITE);
    }
}

static void R_RenderShadow(const renderEntity_t *entity, LPCVECTOR2 origin) {
#ifndef USE_SHADOWMAPS
    static DWORD next_log[MAX_GAME_ENTITIES];
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
    if ((entity->flags & RF_NOT_SELECTABLE) && entity->number < MAX_GAME_ENTITIES &&
        atoi(ri.CvarString ? ri.CvarString("r_debug_destructables", "0") : "0") &&
        tr.viewDef.time >= next_log[entity->number]) {
        fprintf(stderr, "WC3 dest render: shadow submitted time=%u ent=%u flags=0x%x texture=%u "
                        "rect=(%.1f %.1f %.1f %.1f) bounds=(%.1f %.1f)-(%.1f %.1f)\n",
                (unsigned)tr.viewDef.time, (unsigned)entity->number, (unsigned)entity->flags,
                (unsigned)shadow->texid, entity->shadow_rect.x, entity->shadow_rect.y,
                entity->shadow_rect.w, entity->shadow_rect.h, mins.x, mins.y, maxs.x, maxs.y);
        next_log[entity->number] = tr.viewDef.time + DEST_DEBUG_INTERVAL;
    }
    R_AddRectSplat(&mins, &maxs, shadow, shadowColor);
#endif
}

/* Unit shadows are ground decals that share one shader and differ only by
 * texture and rect.  Drawing each in isolation re-uploaded the vertex buffer
 * and re-issued all splat GL state per unit; batching them across the scene
 * collapses runs of same-texture shadows into one upload + draw (flushing on
 * texture change or buffer capacity), instead of one per unit. */
static void R_DrawEntityShadows(void) {
#ifndef USE_SHADOWMAPS
    R_BeginSplatBatch(R_SPLAT_SHADER(&tr.shader_shadowSplat));
    FOR_LOOP(i, tr.viewDef.num_entities) {
        renderEntity_t const *ent = tr.viewDef.entities + i;
        if ((ent->flags & RF_HIDDEN) || !ent->model) {
            continue;
        }
        R_RenderShadow(ent, (LPCVECTOR2)&ent->origin);
    }
    R_EndSplatBatch();
#endif
}

static void R_RenderSelectedCircle(const renderEntity_t *entity, LPCVECTOR2 origin) {
    if (entity->flags & RF_SELECTED) {
        COLOR32 color = { 0, 255, 0, 255 };
        float radius = R_GameSelectionRadius(entity);
        FOR_LOOP(i, NUM_SELECTION_CIRCLES) {
            if ((radius * 2) > selCircles[i])
                continue;
            /* A flat actor-Z quad intersects sloped terrain; the splat path fits the ring to terrain samples. */
            R_RenderSplat(origin, radius, tr.texture[TEX_SELECTION_CIRCLE+i], R_SPLAT_SHADER(&tr.shader_splat), color);
            break;
        }
    }
}

/* Project a world point through the active view-projection into UI scene
 * coordinates (the space R_DrawImageEx draws in).  Returns false when the
 * point is behind the camera (clip w <= 0). */
static bool R_WorldToUI(LPCVECTOR3 world, FLOAT *out_x, FLOAT *out_y) {
    FLOAT const *m = tr.viewDef.viewProjectionMatrix.v;
    FLOAT const cx = m[0] * world->x + m[4] * world->y + m[8]  * world->z + m[12];
    FLOAT const cy = m[1] * world->x + m[5] * world->y + m[9]  * world->z + m[13];
    FLOAT const cw = m[3] * world->x + m[7] * world->y + m[11] * world->z + m[15];
    if (cw <= 0.0001f) {
        return false;
    }
    RECT const scene = R_UISceneRect();
    *out_x = scene.x + (cx / cw * 0.5f + 0.5f) * scene.w;
    *out_y = scene.y + (1.0f - (cy / cw * 0.5f + 0.5f)) * scene.h;
    return true;
}

/* A two-layer status bar (dark backing + colored fill) in UI scene coords. */
static void R_DrawStatusBar(FLOAT x, FLOAT y, FLOAT w, FLOAT h, FLOAT frac, COLOR32 fill) {
    frac = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
    R_DrawImageEx(&MAKE(drawImage_t, .texture = tr.texture[TEX_WHITE],
        .screen = MAKE(RECT, x, y, w, h), .uv = MAKE(RECT, 0, 0, 1, 1),
        .color = MAKE(COLOR32, 0, 0, 0, 200), .shader = SHADER_UI, .alphamode = BLEND_MODE_BLEND));
    if (frac > 0.0f) {
        R_DrawImageEx(&MAKE(drawImage_t, .texture = tr.texture[TEX_WHITE],
            .screen = MAKE(RECT, x, y, w * frac, h), .uv = MAKE(RECT, 0, 0, 1, 1),
            .color = fill, .shader = SHADER_UI, .alphamode = BLEND_MODE_BLEND));
    }
}

/* WC3-style health/mana bars floating above selected, living units.  Runs as a
 * 2D overlay pass after the 3D scene (R_DrawImageEx rebinds UI shader/state, so
 * it must not run mid-model). */
void R_DrawHealthBars(void) {
    if (tr.viewDef.rdflags & RDF_NOWORLDMODEL) {
        return; /* portrait / offscreen views have no overhead bars */
    }
    BOOL const show_all = (tr.viewDef.rdflags & RDF_SHOW_ALL_HEALTHBARS) != 0;
    RECT const scene = R_UISceneRect();
    FLOAT const w = scene.w * 0.045f;
    FLOAT const h = scene.h * 0.008f;
#ifdef WOW
    static LPFONT wow_name_font;
    static BOOL wow_name_font_tried;
    viewCamera_t const *old = tr.viewDef.camerastate + 1, *cur = tr.viewDef.camerastate;
    VECTOR3 wow_cam = Vector3_lerp(&old->origin, &cur->origin, tr.viewDef.lerpfrac);
    VECTOR3 wow_ang = {
        Wow_LerpDegrees(old->viewangles.x, cur->viewangles.x, tr.viewDef.lerpfrac),
        Wow_LerpDegrees(old->viewangles.y, cur->viewangles.y, tr.viewDef.lerpfrac),
        Wow_LerpDegrees(old->viewangles.z, cur->viewangles.z, tr.viewDef.lerpfrac),
    };
    VECTOR3 wow_fwd, wow_off;
    FOR_LOOP(i, tr.viewDef.num_entities)
        if (tr.viewDef.entities[i].number == tr.viewDef.player) {
            wow_cam.z = tr.viewDef.entities[i].origin.z + WOW_CAMERA_EYE_HEIGHT; break;
        }
    wow_fwd = Wow_ViewForward(&wow_ang);
    wow_off = Vector3_scale(&wow_fwd, -LerpNumber(old->distance, cur->distance, tr.viewDef.lerpfrac));
    wow_cam = Vector3_add(&wow_cam, &wow_off);
#endif
    FOR_LOOP(i, tr.viewDef.num_entities) {
        renderEntity_t const *e = tr.viewDef.entities + i;
#ifdef WOW
        if (e->name && *e->name) {
            VECTOR3 top;
            VECTOR3 delta;
            FLOAT alpha, ux, uy;
            R_GameEntityOverheadPosition(e, &top);
            delta = Vector3_sub(&top, &wow_cam);
            alpha = Wow_WorldLabelAlpha(Vector3_len(&delta), e->flags & RF_SELECTED, e->number == tr.viewDef.player);
            if (alpha > 0.0f && R_WorldToUI(&top, &ux, &uy)) {
                if (!wow_name_font_tried) {
                    wow_name_font_tried = true;
                    wow_name_font = R_LoadFont("Fonts\\FRIZQT__.TTF", 14);
                    if (!wow_name_font) fprintf(stderr, "WoW: NPC name font Fonts\\FRIZQT__.TTF could not be loaded\n");
                }
                if (wow_name_font) {
                    /* Attachment 18 is the model-authored point below the name, so the label ends at its projection. */
                    RECT label = MAKE(RECT, ux - scene.w * 0.12f, uy - scene.h * 0.03f,
                                      scene.w * 0.24f, scene.h * 0.03f);
                    drawText_t text = MAKE(drawText_t, .font = wow_name_font, .text = e->name, .rect = label,
                        .color = MAKE(COLOR32, 0, 255, 0, (BYTE)(255.0f * alpha)), .textWidth = label.w, .lineHeight = label.h,
                        .halign = FONT_JUSTIFYCENTER, .valign = FONT_JUSTIFYMIDDLE);
                    R_DrawText(&text);
                }
            }
        }
#endif
        /* Always for the current selection; for hovered entity; for every unit while ALT is held. */
        if (e->health == 0 || (!show_all && !(e->flags & RF_SELECTED) && e->number != tr.viewDef.hover_entity)) {
            continue;
        }
        VECTOR3 top = e->origin;
        top.z += e->radius * 2.0f + 48.0f; /* float above the unit */
        FLOAT ux, uy;
        if (!R_WorldToUI(&top, &ux, &uy)) {
            continue;
        }
        FLOAT const x  = ux - w * 0.5f;
        VECTOR2 const bars = R_StatusBarOffsets(h, e->mana > 0);
        FLOAT const hp = e->health / 255.0f;
        /* Green at full -> yellow at half -> red when low, like the original. */
        COLOR32 const hpcol = hp > 0.5f
            ? MAKE(COLOR32, (BYTE)(255.0f * (1.0f - hp) * 2.0f), 200, 0, 255)
            : MAKE(COLOR32, 220, (BYTE)(200.0f * hp * 2.0f), 0, 255);
        R_DrawStatusBar(x, uy + bars.x, w, h, hp, hpcol);
        if (e->mana > 0) {
            R_DrawStatusBar(x, uy + bars.y, w, h, e->mana / 255.0f, MAKE(COLOR32, 60, 90, 235, 255));
        }
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
    COLOR32 color = (entity->flags & RF_HOSTILE)
        ? MAKE(COLOR32, 255, 80, 80, 160)   /* red for hostile */
        : MAKE(COLOR32, 80, 200, 80, 160);   /* green for friendly */
    float radius = entity->radius;
    FOR_LOOP(i, NUM_SELECTION_CIRCLES) {
        if ((radius * 2) > selCircles[i])
            continue;
        R_RenderSplat(&(VECTOR2){ entity->origin.x, entity->origin.y },
                      radius, tr.texture[TEX_SELECTION_CIRCLE+i],
                      R_SPLAT_SHADER(&tr.shader_splat), color);
        break;
    }
}

void R_RenderModel(renderEntity_t const *entity) {
    if ((entity->flags & RF_HIDDEN) || !entity->model)
        return;

#ifdef USE_SHADOWMAPS
    if (tr.render_phase == RENDER_PHASE_LIGHTS) {
        if (!(entity->flags & RF_NO_SHADOW))
            R_GameRenderModel(entity);
        return;
    }
#endif

    R_RenderUberSplat(entity, (LPCVECTOR2)&entity->origin);
    R_GameRenderModel(entity);
    R_RenderSelectedCircle(entity, (LPCVECTOR2)&entity->origin);
    R_RenderHoverHighlight(entity);
}
