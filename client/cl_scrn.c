#include "client.h"
#include "menu_text_input.h"
#include "ui_layout.h"
#include <ctype.h>
#include <SDL2/SDL.h>

bool scr_initialized;

#define SCR_FPS_HEIGHT 8
#define SCR_FPS_BOTTOM_MARGIN 4
#define SCR_ALERT_PULSE_HALF_MS 250 // milliseconds; triangle-wave half period for transient command-button alert tint
#define SCR_ALERT_PULSE_MIN_GB 80 // color channel value; preserves portrait detail at the red peak of a transient alert

float SCR_UICanvasWidth(void) { return CL_Canvas()->scene.w; }

/*
 * SDL mouse positions are window pixels, while UI/layout coordinates use the
 * engine's virtual canvas.  Cursor drawing and FDF hit-testing must share this
 * mapping, including each game's authored widescreen scene.  The canvas keeps
 * the window it was resolved from, so a frame never mixes an old scene with a
 * new window size.
 */
vector2_t SCR_ScreenToUI(int x, int y) {
    uiCanvas_t const *canvas = CL_Canvas();
    float nx = 0.0f, ny = 0.0f;

    if (canvas->window.width > 0 && canvas->window.height > 0) {
        nx = (float)x / (float)canvas->window.width;
        ny = (float)y / (float)canvas->window.height;
    }

    return MAKE(vector2_t, nx * canvas->scene.w, ny * canvas->scene.h);
}

static void SCR_DrawString(int x, int y, cstring_t string) {
    if (string) re.DrawString(x, y, string);
}

static void SCR_DrawFPS(uint32_t msec) {
    static uint32_t elapsed = 0;
    static uint32_t frames_drawn = 0;
    static uint32_t fps = 0;
    char text[64];
    size2_t window = re.GetWindowSize();
    uint32_t inset = SCR_FPS_HEIGHT + SCR_FPS_BOTTOM_MARGIN;
    uint32_t y = window.height > inset ? window.height - inset : 0;

    elapsed += msec;
    frames_drawn++;
    if (elapsed >= 500) {
        fps = frames_drawn * 1000 / elapsed;
        elapsed = 0;
        frames_drawn = 0;
    } else if (!fps && msec > 0) {
        fps = 1000 / msec;
    }

    if (fps) {
        snprintf(text, sizeof(text), "FPS %u  Drawcalls %u", (unsigned)fps, (unsigned)re.GetDrawCalls());
    } else {
        snprintf(text, sizeof(text), "FPS --  Drawcalls %u", (unsigned)re.GetDrawCalls());
    }
    SCR_DrawString(10, y, text);
}

/*
 * Preserve the SDL cursor selected by the input module while the game renderer
 * temporarily replaces it with authored cursor content.
 */
static void SCR_UpdateSystemCursor(bool game_cursor_active) {
    static int previous_visibility = -2;

    if (game_cursor_active) {
        if (previous_visibility == -2) {
            previous_visibility = SDL_ShowCursor(SDL_QUERY);
            if (previous_visibility < 0) previous_visibility = SDL_ENABLE;
            SDL_ShowCursor(SDL_DISABLE);
        }
        return;
    }

    if (previous_visibility != -2) {
        SDL_ShowCursor(previous_visibility ? SDL_ENABLE : SDL_DISABLE);
        previous_visibility = -2;
    }
}

static color32_t SCR_CursorTint(void) {
    uint32_t const entnum = cl.hover_entity;

    if (entnum && entnum < MAX_CLIENT_ENTITIES) {
        entityState_t const *state = &cl.ents[entnum].current;
        if (CL_EntityAllowsWorldHover(state)) {
            if (state->flags & EF_HOSTILE) {
                return MAKE(color32_t, 255, 0, 0, 255);
            }
            if (state->flags & EF_NEUTRAL) {
                return MAKE(color32_t, 255, 220, 80, 255);
            }
        }
    }
    return COLOR32_WHITE;
}

static void SCR_DrawCursor(void) {
    int const x = (int)mouse.origin.x, y = (int)mouse.origin.y;
    bool drawn = false;

    /* Loading plaques are non-interactive. Hide both the authored cursor and
     * the native SDL cursor until normal menu/game presentation resumes. */
    if (cl.playerstate.client_ui_state == CLIENT_UI_LOADING) {
        SCR_UpdateSystemCursor(true);
        return;
    }

    if (Cvar_Integer("r_cursor", 0) == 1) {
        vector2_t const pos = SCR_ScreenToUI(x, y);
        drawn = re.DrawCursor(pos.x, pos.y, SCR_CursorTint());
    }
    SCR_UpdateSystemCursor(drawn);
}

void SCR_BeginLoadingPlaque(void) {
    if (cls.disable_screen)
        return;
    if (cls.state == ca_disconnected)
        return;
    if (cls.key_dest == key_console)
        return;
    SCR_UpdateScreen(0);
    cls.disable_screen = SDL_GetTicks();
    cls.disable_servercount = -1;
}

void SCR_EndLoadingPlaque(void) {
    cls.disable_screen = 0;
}

void SCR_DrawScreenField(uint32_t msec) {
    re.BeginFrame();
    if (CL_MovieActive()) {
        SCR_UpdateSystemCursor(true);
        CL_MovieDraw();
#ifndef BZ_TESTS
        if (CL_ScreenshotReady()) re.Screenshot();
#endif
        re.EndFrame();
        return;
    }
    switch (cls.state) {
    default:
        Com_Error(ERR_FATAL, "SCR_DrawScreenField: bad cls.state");
        break;
    case ca_disconnected:
        if (CL_MenuActive()) menu.Refresh(cl.time);
        break;
    case ca_connecting:
    case ca_connected:
        if (cl.playerstate.client_ui_state == CLIENT_UI_LOADING) SCR_DrawLoadingLayout();
        else if (CL_MenuActive()) menu.Refresh(cl.time);
        break;
    case ca_active:
        V_RenderView();
        if (Cvar_Integer("r_hud", 1)) {
            SCR_DrawLayout();
        }
        break;
    }

    CON_DrawConsole();
    if (Cvar_Integer("scr_showfps", 0)) {
        SCR_DrawFPS(msec);
    }

    /* Cursor is deliberately last so it stays above world, HUD, menus and debug text. */
    SCR_DrawCursor();
#ifndef BZ_TESTS
    if (CL_ScreenshotReady()) {
        re.Screenshot();
    }
#endif
    re.EndFrame();
}

void SCR_UpdateLoadingPlaque(void) {
    if (!scr_initialized) return;
    if (!cls.disable_screen) return;
    if (Cvar_Integer("r_norefresh", 0)) return;
    if (cl.playerstate.client_ui_state != CLIENT_UI_LOADING) return;
    SCR_DrawScreenField(0);
}

void SCR_UpdateScreen(uint32_t msec) {
    static int recursive;
    static uint32_t no_refresh_elapsed, no_refresh_frames;

    if (!scr_initialized) {
        return;
    }

    /* Quake-style no-refresh mode preserves input, snapshots, and server work while submitting no screen frame. */
    if (Cvar_Integer("r_norefresh", 0)) {
        if (Cvar_Integer("r_stats", 0)) {
            no_refresh_elapsed += msec; no_refresh_frames++;
            if (no_refresh_elapsed >= 1000) {
                fprintf(stderr, "[R_NOREFRESH] loops=%u\n", (unsigned)((uint64_t)no_refresh_frames * 1000 / no_refresh_elapsed));
                no_refresh_elapsed = 0; no_refresh_frames = 0;
            }
        } else no_refresh_elapsed = no_refresh_frames = 0;
        return;
    }
    no_refresh_elapsed = 0; no_refresh_frames = 0;

    if (cls.disable_screen) {
        if (SDL_GetTicks() - cls.disable_screen > 120000) {
            cls.disable_screen = 0;
            fprintf(stderr, "Loading plaque timed out.\n");
        }
        return;
    }

    if (++recursive > 2) {
        Com_Error(ERR_FATAL, "SCR_UpdateScreen: recursively called");
    }
    recursive = 1;

    SCR_DrawScreenField(msec);

    recursive = 0;
}

/* --------------------------------------------------------------------------
 * Layout system — server-authored UI frame rendering and hit testing.
 * Previously in cl_unit_layout.c; merged here because the "unit_" prefix
 * was misleading — this is general-purpose layout, not unit-specific.
 * -------------------------------------------------------------------------- */

#define MAX_LISTBOX_TEXT 2048

static cstring_t active_tooltip = NULL;
static handle_t layout_layers[MAX_LAYOUT_LAYERS];
static texture_t *layout_dynamic_pics[MAX_DYNAMIC_IMAGES];
static char layout_dynamic_pic_names[MAX_DYNAMIC_IMAGES][512];
static uint32_t layout_dynamic_pic_cursor;
static bool layout_left_down;
static char layout_held_command[CMDARG_LEN * 2];
static uint32_t layout_hovered_number;
static uint32_t layout_hovered_layer;
static uint32_t layout_current_layer;
static handle_t layout_hovered;
static handle_t layout_current;
static bool layout_current_window;

/* Project a world point through the active camera into the virtual UI canvas.
 * The world scissor is authoritative: callers should not turn an off-screen
 * world event into a HUD notification pinned to the nearest edge. */
bool SCR_ProjectWorldPoint(vector3_t const *point, vector2_t *screen) {
    float const *m;
    float cx, cy, cw, vx, vy;

    if (!point || !screen) return false;
    m = cl.viewDef.viewProjectionMatrix.v;
    cx = m[0] * point->x + m[4] * point->y + m[8] * point->z + m[12];
    cy = m[1] * point->x + m[5] * point->y + m[9] * point->z + m[13];
    cw = m[3] * point->x + m[7] * point->y + m[11] * point->z + m[15];
    if (cw <= 0.0001f) return false;
    vx = cl.viewDef.viewport.x + (cx / cw * 0.5f + 0.5f) * cl.viewDef.viewport.w;
    vy = cl.viewDef.viewport.y + (cy / cw * 0.5f + 0.5f) * cl.viewDef.viewport.h;
    if (vx < cl.viewDef.scissor.x || vx > cl.viewDef.scissor.x + cl.viewDef.scissor.w ||
        vy < cl.viewDef.scissor.y || vy > cl.viewDef.scissor.y + cl.viewDef.scissor.h) return false;
    *screen = MAKE(vector2_t, vx * SCR_UICanvasWidth(), (1.0f - vy) * UI_BASE_HEIGHT);
    return true;
}

/* Entity-context layouts use a server-authored tree rooted at the client-projected model top. */
bool SCR_LayoutWorldHoverRoot(rect_t *root) {
    entityState_t const *ent = SCR_LayoutContextEntity();
    vector3_t top;
    vector2_t screen;

    if (!root || !ent) return false;
    FOR_LOOP(i, cl.viewDef.num_entities) {
        if (cl.viewDef.entities[i].number != cl.hover_entity) continue;
        if (!re.GetEntityOverheadPosition(&cl.viewDef.entities[i], &top)) return false;
        if (!SCR_ProjectWorldPoint(&top, &screen)) return false;
        *root = MAKE(rect_t, screen.x, screen.y, 0, 0);
        return true;
    }
    return false;
}

static rect_t get_uvrect(uint8_t const *tc) {
    return (rect_t){ tc[0], tc[2], tc[1]-tc[0], tc[3]-tc[2] };
}

static texture_t const *SCR_LayoutPic(RESOURCE image) {
    return image && image < MAX_IMAGES ? cl.pics[image] : NULL;
}

static texture_t const *SCR_LayoutGetDynamicTexture(cstring_t resource) {
    if (!resource || !*resource || !strcmp(resource, " ")) return NULL;

    uint32_t slot = MAX_DYNAMIC_IMAGES;
    FOR_LOOP(i, MAX_DYNAMIC_IMAGES) {
        if (layout_dynamic_pics[i] && !strcmp(layout_dynamic_pic_names[i], resource))
            return layout_dynamic_pics[i];
        if (!layout_dynamic_pics[i] && slot == MAX_DYNAMIC_IMAGES)
            slot = i;
    }
    if (slot == MAX_DYNAMIC_IMAGES) {
        slot = layout_dynamic_pic_cursor++ % MAX_DYNAMIC_IMAGES;
        SAFE_DELETE(layout_dynamic_pics[slot], re.ReleaseTexture);
        layout_dynamic_pic_names[slot][0] = '\0';
    }
    layout_dynamic_pics[slot] = re.LoadTexture(resource);
    if (!layout_dynamic_pics[slot]) return NULL;
    snprintf(layout_dynamic_pic_names[slot], sizeof(layout_dynamic_pic_names[slot]), "%s", resource);
    return layout_dynamic_pics[slot];
}

static rect_t scale_rect(rect_t const *r, float f) {
    float dx = r->w * (1-f), dy = r->h * (1-f);
    return (rect_t){ r->x + dx/2, r->y + dy/2, r->w - dx, r->h - dy };
}

static entityState_t const *SCR_LayoutSelectedEntity(void) {
    FOR_LOOP(i, cl.num_entities) {
        entityState_t const *ent = &cl.ents[i].current;
        if (ent && (ent->renderfx & RF_SELECTED)) return ent;
    }
    return NULL;
}

void SCR_LayoutDrawSegmentedStatusbar(uiFrame_t const *frame, rect_t const *screen) {
    entityState_t const *ent;
    uint32_t count, capacity;
    float gap, width;
    rect_t const uv = { 0, 0, 1, 1 };

    if (!frame || !screen || !SCR_LayoutEntityContextActive()) return;
    ent = SCR_LayoutContextEntity();
    if (!ent || frame->stat >= ENT_STAT_COUNT || !frame->tex.index) return;
    count = EntityCargoCount(ent->stats[frame->stat]);
    capacity = EntityCargoCapacity(ent->stats[frame->stat]);
    if (!capacity) return;
    count = MIN(count, capacity);

    /* Segmented status bars use the primary art for filled slots and optional
     * secondary art for empty slots. Capacity owns the slot geometry even when
     * the current count is zero. */
    gap = MAX(0.0f, frame->value);
    width = (screen->w - gap * (float)(capacity - 1)) / (float)capacity;
    if (width <= 0.0f) return;

    FOR_LOOP(i, capacity) {
        bool const occupied = i < count;
        RESOURCE const image = occupied ? frame->tex.index : frame->tex.index2;
        rect_t segment = *screen;
        if (!image) continue;
        segment.x += (float)i * (width + gap);
        segment.w = width;
        re.DrawImage(cl.pics[image], &segment, &uv, occupied ? frame->color : COLOR32_WHITE);
    }
}

void SCR_LayoutDrawStatusbar(uiFrame_t const *frame, rect_t const *screen) {
    rect_t const uv = { 0, 0, 255, 255 };
    rect_t screen2 = *screen, uv2 = uv;
    float value = frame->value;
    SCR_LayoutContextValue(frame->stat, &value);
    if (frame->stat == UI_STAT_SELECTION_TIMED_STATUS && Cvar_Integer("ui_layout_debug", 0) >= 3) {
        static int last_bucket = -1;
        int const bucket = (int)(MAX(0.0f, MIN(1.0f, value)) * 20.0f);
        if (bucket != last_bucket || Cvar_Integer("ui_layout_debug", 0) >= 4) {
            fprintf(stderr,
                    "UI_TIMED_STATUS draw frame=%u layer=%u value=%.4f raw=%u rect=(%.4f,%.4f %.4fx%.4f) tex=%u border=%u\n",
                    (unsigned)frame->number, (unsigned)layout_current_layer, value,
                    (unsigned)cl.playerstate.stats[UI_PLAYERSTAT_SELECTION_TIMED_STATUS],
                    screen->x, screen->y, screen->w, screen->h,
                    (unsigned)frame->tex.index, (unsigned)frame->tex.index2);
            last_bucket = bucket;
        }
    }
    screen2.w *= value;
    uv2.w    *= value;
    rect_t const suv2 = Rect_div(&uv2, 0xff);
    re.DrawImage(cl.pics[frame->tex.index], &screen2, &suv2, frame->color);
    if (frame->tex.index2 > 0) {
        rect_t const suv = Rect_div(&uv, 0xff);
        re.DrawImage(cl.pics[frame->tex.index2], screen, &suv, COLOR32_WHITE);
    }
}

void SCR_LayoutDrawTexture(uiFrame_t const *frame, rect_t const *screen) {
    float value = 0;
    bool const has = SCR_LayoutContextValue(frame->stat, &value);
    bool const ctx = SCR_LayoutEntityContextActive() &&
                     (frame->stat == UI_STAT_CONTEXT_HEALTH || frame->stat == UI_STAT_CONTEXT_MANA);
    /* Mana hides by flag so an empty pool still draws the bar; health still
     * drops at value 0 because that snapshot means the unit is dead. */
    if (has ? (value <= 0.0f && frame->stat != UI_STAT_CONTEXT_MANA) : ctx) return;
    if (!frame->tex.index) return;  /* unresolved texture — skip to avoid drawing cl.pics[0] */
    texture_t const *tex = cl.pics[frame->tex.index];
    if (frame->stat >= MAX_STATS && frame->stat - MAX_STATS < PLAYERTEXT_COUNT) {
        cstring_t resource = cl.playerstate.texts[frame->stat - MAX_STATS];
        texture_t const *dyn = SCR_LayoutGetDynamicTexture(resource);
        if (dyn) tex = dyn;
    }
    if (frame->buffer.data && frame->buffer.size >= sizeof(uiTextureUV_t)) {
        uiTextureUV_t const *uv = frame->buffer.data;
        color32_t color = uv->color.a ? uv->color : frame->color;
        re.DrawImageEx(&MAKE(drawImage_t,
                             .texture = tex,
                             .shader = SHADER_UI,
                             .alphamode = uv->alphamode,
                             .screen = *screen,
                             .uv = MAKE(rect_t, uv->l, uv->t, uv->r - uv->l, uv->b - uv->t),
                             .color = color));
    } else {
        rect_t const uv = get_uvrect(frame->tex.coord);
        rect_t const suv = Rect_div(&uv, 0xff);
        re.DrawImage(tex, screen, &suv, frame->color);
    }
}

static void SCR_LayoutDrawHighlightData(uiHighlight_t const *h, rect_t const *screen) {
    texture_t const *texture;
    if (!h || !screen || !(texture = SCR_LayoutPic(h->alphaFile))) return;
    re.DrawImageEx(&MAKE(drawImage_t,
        .texture   = texture,
        .alphamode = h->alphaMode,
        .screen    = *screen,
        .uv        = MAKE(rect_t,0,0,1,1),
        .color     = COLOR32_WHITE,
        .shader    = SHADER_UI));
}

void SCR_LayoutDrawHighlight(uiFrame_t const *frame, rect_t const *screen) {
    SCR_LayoutDrawHighlightData(frame->buffer.data, screen);
}

static bool SCR_LayoutFrameIsHovered(uiFrame_t const *frame);

void SCR_LayoutSimpleButton(uiFrame_t const *frame, rect_t const *screen) {
    uiSimpleButton_t const *b = frame->buffer.data;
    bool const enabled = SCR_LayoutFrameHasClickCommand(frame);
    bool const hovered = SCR_LayoutFrameIsHovered(frame);
    bool const pushed = enabled && hovered && layout_left_down;
    uiSimpleButtonState_t const *state = !enabled ? &b->disabled : pushed ? &b->pushed : &b->normal;
    texture_t const *texture = SCR_LayoutPic(state->texture);
    if (!texture) {
        state = &b->normal;
        texture = SCR_LayoutPic(state->texture);
    }
    if (texture) {
        rect_t const uv = get_uvrect((uint8_t *)&state->texcoord);
        rect_t const suv = Rect_div(&uv, 0xff);
        re.DrawImage(texture, screen, &suv, COLOR32_WHITE);
    }
    re.DrawText(&MAKE(drawText_t,
        .rect      = *screen,
        .font      = cl.fonts[state->font],
        .text      = frame->text,
        .color     = state->fontcolor,
        .textWidth = screen->w));
}

void SCR_LayoutDrawBackdrop2(uiFrame_t const *frame, rect_t const *screen, uiBackdrop_t const *bd) {
    texture_t const *background, *edge;
    if (!bd || !screen || screen->w <= 0 || screen->h <= 0) return;
    background = SCR_LayoutPic(bd->Background);
    edge = SCR_LayoutPic(bd->EdgeFile);
    if (!background && !edge) return;
    re.DrawBackdrop(&MAKE(drawBackdrop_t,
        .screen        = *screen,
        .bg.texture    = background,
        .bg.color      = frame->color,
        .edge.texture  = edge,
        .edge.color    = frame->color,
        .corner.flags  = bd->CornerFlags,
        .corner.size   = bd->CornerSize,
        .insets.right  = bd->BackgroundInsets[0],
        .insets.top    = bd->BackgroundInsets[1],
        .insets.bottom = bd->BackgroundInsets[2],
        .insets.left   = bd->BackgroundInsets[3],
        .flags = (bd->TileBackground ? DRAW_TILE     : 0)
               | (bd->Mirrored       ? DRAW_MIRRORED : 0)));
}

void SCR_LayoutDrawBackdrop(uiFrame_t const *frame, rect_t const *screen) {
    SCR_LayoutDrawBackdrop2(frame, screen, frame->buffer.data);
}

static bool SCR_LayoutBackdropHasArt(uiBackdrop_t const *bd) {
    return bd && (bd->Background || bd->EdgeFile);
}

static void SCR_LayoutDrawBackdropPart(uiFrame_t const *frame, rect_t const *screen, uiBackdrop_t const *bd) {
    if (SCR_LayoutBackdropHasArt(bd) && screen->w > 0 && screen->h > 0)
        SCR_LayoutDrawBackdrop2(frame, screen, bd);
}

/* WoW sliders use compact cropped textures; retain backdrop drawing for legacy FDF scrollbars. */
static bool SCR_LayoutDrawScrollImage(RESOURCE texture, uint8_t const *texcoord, rect_t const *screen) {
    rect_t uv, suv;
    texture_t const *image = SCR_LayoutPic(texture);
    if (!image) return false;
    uv = get_uvrect(texcoord); suv = Rect_div(&uv, 0xff);
    re.DrawImage(image, screen, &suv, COLOR32_WHITE);
    return true;
}


static uiFrame_t const *SCR_LayoutTextAreaScrollBar(uiFrame_t const *frame) {
    if (!frame || !frame->number || frame->flags.type != FT_TEXTAREA) return NULL;
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *child = SCR_Frame(i);
        if (child && child->parent == frame->number && child->flags.type == FT_SCROLLBAR)
            return child;
    }
    return NULL;
}

static rect_t SCR_LayoutTextAreaView(uiFrame_t const *frame, rect_t const *screen) {
    uiTextArea_t const *ta = frame && frame->buffer.data ? frame->buffer.data : NULL;
    float inset = ta ? ta->inset : 0.0f;
    rect_t view = { screen->x + inset, screen->y + inset,
                  MAX(0.0f, screen->w - inset * 2), MAX(0.0f, screen->h - inset * 2) };
    uiFrame_t const *scrollbar = SCR_LayoutTextAreaScrollBar(frame);
    if (scrollbar) {
        float sw = SCR_LayoutRect(scrollbar)->w;
        if (sw > 0.0f && sw < view.w) view.w = MAX(0.0f, view.w - sw);
    }
    return view;
}

float SCR_LayoutTextAreaMaxScroll(uiFrame_t const *frame) {
    uiTextArea_t const *ta;
    rect_t view;
    drawText_t measure;
    cstring_t value;

    if (!frame || frame->flags.type != FT_TEXTAREA || !frame->buffer.data || !re.GetTextSize)
        return 0.0f;
    ta = frame->buffer.data;
    view = SCR_LayoutTextAreaView(frame, SCR_LayoutRect(frame));
    if (view.w <= 0.0f || view.h <= 0.0f) return 0.0f;
    value = SCR_GetStringValue(frame);
    measure = SCR_GetDrawText(frame, view.w, value ? value : "", &(uiLabel_t){
        .font = ta->font, .textalignx = FONT_JUSTIFYLEFT, .textaligny = FONT_JUSTIFYTOP });
    measure.flags |= DRAW_WORD_WRAP;
    return MAX(0.0f, re.GetTextSize(&measure).y - view.h);
}

static int SCR_LayoutListBoxVisibleRows(uiFrame_t const *frame, rect_t const *view) {
    uiListBox_t const *lb;
    float item_height;

    if (!frame || !view || frame->flags.type != FT_LISTBOX || !frame->buffer.data ||
        frame->buffer.size < sizeof(uiListBox_t) || view->h <= 0.0f) return 0;
    lb = frame->buffer.data;
    item_height = lb->itemHeight > 0.0f ? lb->itemHeight : 0.018f;
    return MAX((int)floorf(view->h / item_height), 1);
}

static int SCR_LayoutListBoxMaxScroll(uiFrame_t const *frame) {
    uiListBox_t const *lb;
    rect_t view;
    int count = 0, visible;

    if (!frame || frame->flags.type != FT_LISTBOX || !frame->buffer.data ||
        frame->buffer.size < sizeof(uiListBox_t)) return 0;
    lb = frame->buffer.data;
    view = Rect_inset(SCR_LayoutRect(frame), lb->border);
    if (frame->text && *frame->text) {
        count = 1;
        for (cstring_t p = frame->text; *p; p++) if (*p == '\n') count++;
    }
    visible = SCR_LayoutListBoxVisibleRows(frame, &view);
    return MAX(count - visible, 0);
}

void SCR_LayoutDrawScrollBar(uiFrame_t const *frame, rect_t const *screen) {
    uiScrollBarImage_t const *art = frame->buffer.size == sizeof(*art) ? frame->buffer.data : NULL;
    uiScrollBar_t const *sb = !art && frame->buffer.size >= sizeof(*sb) ? frame->buffer.data : NULL;
    uiFrame_t const *parent = frame->parent < SCR_NumFrames() ? SCR_Frame(frame->parent) : NULL;
    if ((!art && !sb) || screen->w <= 0 || screen->h <= 0) return;

    if (parent && parent->flags.type == FT_TEXTAREA) {
        if (SCR_LayoutTextAreaMaxScroll(parent) <= 0.0f) return;
        ((uiFrame_t *)frame)->value = parent->value;
    } else if (parent && parent->flags.type == FT_LISTBOX) {
        if (SCR_LayoutListBoxMaxScroll(parent) <= 0) return;
        ((uiFrame_t *)frame)->value = parent->value;
    }

    if (sb) SCR_LayoutDrawBackdropPart(frame, screen, &sb->background);

    /* Equal authored pixel dimensions need a taller Y span in WoW's normalized square UI scene. */
    float bh = MIN(screen->w * UI_PIXEL_ASPECT, screen->h * 0.5f);
    rect_t inc   = MAKE(rect_t, screen->x, screen->y + screen->h - bh, screen->w, bh);
    rect_t dec   = MAKE(rect_t, screen->x, screen->y, screen->w, bh);
    rect_t track = MAKE(rect_t, screen->x, dec.y + dec.h, screen->w, inc.y - (dec.y + dec.h));
    if (art) {
        SCR_LayoutDrawScrollImage(art->image[0], art->texcoord, &inc);
        SCR_LayoutDrawScrollImage(art->image[1], art->texcoord, &dec);
    } else {
        SCR_LayoutDrawBackdropPart(frame, &inc, &sb->incButton);
        SCR_LayoutDrawBackdropPart(frame, &dec, &sb->decButton);
    }
    if (track.h <= 0) return;

#ifdef UI_STRETCHED_SCROLLBAR_THUMB
    float th = MIN(MAX(bh, track.h * 0.25f), track.h);
#else
    float th = MIN(art ? bh : MIN(bh, 0.010f), track.h);
#endif
    float tw = art ? screen->w : MIN(screen->w, 0.010f);
    rect_t thumb = {
        screen->x + (screen->w - tw) * 0.5f,
        /* Slider value zero is the top of a top-origin UI; the old formula inverted it. */
        track.y + (track.h - th) * MIN(MAX(frame->value, 0.0f), 1.0f),
        tw, th
    };
    if (art) SCR_LayoutDrawScrollImage(art->image[2], art->texcoord, &thumb);
    else SCR_LayoutDrawBackdropPart(frame, &thumb, &sb->thumbButton);
}

bool SCR_LayoutFrameHasClickCommand(uiFrame_t const *frame) {
    return frame && frame->onclick && *frame->onclick;
}
static bool SCR_LayoutGlueTextButtonIsPushed(uiFrame_t const *frame) {
    /* The left button is global, but the pushed state belongs only to the hovered layout frame. */
    return layout_left_down && SCR_LayoutFrameHasClickCommand(frame) && SCR_LayoutFrameIsHovered(frame);
}
static bool SCR_LayoutFrameIsHovered(uiFrame_t const *frame) {
    if (!frame || frame->number != layout_hovered_number) return false;
    /* Transient windows identify the hovered layout by handle. Persistent HUD
     * layers identify it by layer number; layout_current is also used while
     * drawing those layers, so it must not make ordinary HUD hover look like a
     * window hover. */
    if (layout_hovered)
        return layout_current_window && layout_current == layout_hovered;
    return !layout_current_window && layout_current_layer == layout_hovered_layer;
}

static void SCR_LayoutFormatOnClickCommand(cstring_t src, string_t dst, uint32_t dsz) {
    if (!dst || dsz == 0) return;
    dst[0] = '\0';
    if (!src) return;

    uint32_t out = 0;
    for (uint32_t i = 0; src[i] && out + 1 < dsz; i++) {
        if (src[i] == '{') {
            char name[80]; uint32_t nlen = 0;
            uint32_t j = i + 1;
            while (src[j] && src[j] != '}' && nlen + 1 < sizeof(name))
                name[nlen++] = src[j++];
            if (src[j] == '}') {
                char val[16];
                name[nlen] = '\0';
                snprintf(val, sizeof(val), "%d", 0);
                for (uint32_t k = 0; val[k] && out + 1 < dsz; k++)
                    dst[out++] = val[k];
                i = j;
                continue;
            }
        }
        dst[out++] = src[i];
    }
    dst[out] = '\0';
}

void SCR_LayoutSendFrameCommand(uiFrame_t const *frame) {
    char command[CMDARG_LEN * 2];
    if (!SCR_LayoutFrameHasClickCommand(frame)) return;
    SCR_LayoutFormatOnClickCommand(frame->onclick, command, sizeof(command));
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s", command);
}

void SCR_LayoutSetPointer(handle_t layout, uint32_t number, bool down) {
    layout_hovered = layout;
    layout_hovered_number = number;
    layout_left_down = down;
}

void SCR_WindowPrepare(handle_t layout, rect_t const *root) {
    layout_current_window = true;
    layout_current = layout;
    SCR_ClearWindow(layout);
    if (root) SCR_SetLayoutRoot(root);
}

bool SCR_WindowLayoutIsCurrent(handle_t layout) {
    return layout && layout_current_window && layout_current == layout;
}

static void SCR_LayoutCheckBox(uiFrame_t const *frame, rect_t const *screen) {
    uiCheckBox_t const *cb = frame->buffer.data;
    bool const enabled = SCR_LayoutFrameHasClickCommand(frame);
    bool const pushed = enabled && SCR_LayoutFrameIsHovered(frame) && layout_left_down;
    uiBackdrop_t const *bd = enabled
        ? (pushed ? &cb->pushed : &cb->normal)
        : (pushed ? &cb->disabledPushed : &cb->disabled);

    SCR_LayoutDrawBackdrop2(frame, screen, bd);
    if (frame->value >= 0.5f)
        SCR_LayoutDrawHighlightData(enabled ? &cb->checked : &cb->disabledChecked, screen);
    if (enabled && SCR_LayoutFrameIsHovered(frame))
        SCR_LayoutDrawHighlightData(&cb->mouseOver, screen);
}

void SCR_LayoutGlueTextButton(uiFrame_t const *frame, rect_t const *screen) {
    uiGlueTextButton_t const *gb = frame->buffer.data;
    bool const enabled = SCR_LayoutFrameHasClickCommand(frame);
    bool const pushed  = SCR_LayoutGlueTextButtonIsPushed(frame);
    uiBackdrop_t const *bd = enabled
        ? (pushed ? &gb->pushed : &gb->normal)
        : (pushed ? &gb->disabledPushed : &gb->disabled);
    SCR_LayoutDrawBackdrop2(frame, screen, bd);
}

static void SCR_LayoutDrawGlueTextButtonHighlight(uiFrame_t const *frame) {
    uiGlueTextButton_t const *gb = frame->buffer.data;
    if (SCR_LayoutFrameHasClickCommand(frame) && SCR_LayoutFrameIsHovered(frame))
        SCR_LayoutDrawHighlightData(&gb->highlight, SCR_LayoutRect(frame));
}

bool SCR_LayoutScrollTextAreaAt(handle_t layout, vector2_t const *point, int wheel_y) {
    (void)layout;
    if (!point || !wheel_y) return false;
    for (uint32_t i = SCR_NumFrames(); i > 0; i--) {
        uiFrame_t *frame = SCR_Frame(i - 1);
        if (!frame || frame->flags.type != FT_TEXTAREA ||
            !Rect_contains(SCR_LayoutRect(frame), point)) continue;
        if (SCR_LayoutTextAreaMaxScroll(frame) > 0.0f)
            frame->value = MIN(1.0f, MAX(0.0f, frame->value - wheel_y * 0.1f));
        return true;
    }
    return false;
}

void SCR_LayoutDrawBuildQueue(uiFrame_t const *frame, rect_t const *scrn) {
    rect_t screen = *scrn;
    rect_t const uv = { 0, 0, 1, 1 };
    uiBuildQueue_t const *queue = frame->buffer.data;
    bool const food_blocked = queue->numitems && queue->items[0].starttime == 0 && queue->items[0].endtime == 0;
    uint32_t active = food_blocked ? 0 : queue->numitems;

    if (!food_blocked) {
        FOR_LOOP(i, queue->numitems) {
            if (cl.time < queue->items[i].endtime) { active = i; break; }
        }
    }
    for (uint32_t i = active + 1; i < queue->numitems; i++) {
        if (food_blocked || cl.time < queue->items[i].endtime) {
            re.DrawImage(cl.pics[queue->items[i].image], &screen, &uv, frame->color);
            screen.x += queue->itemoffset;
        }
    }
}

static void SCR_LayoutDrawMessageQueue(uiFrame_t const *frame, rect_t const *screen) {
    uiMessageQueue_t const *message = frame->buffer.data;
    rect_t uv = MAKE(rect_t, 0, 0, 0.53125f, 0.6875f);
    if (!message || frame->buffer.size < sizeof(*message)) {
        fprintf(stderr, "SCR_LayoutDrawMessageQueue: invalid payload size %u\n", (unsigned)frame->buffer.size);
        return;
    }
    if (message->flags & UI_MESSAGE_UNREAD) {
        texture_t const *icon = SCR_LayoutPic(message->image);
        if (icon) re.DrawImage(icon, screen, &uv, COLOR32_WHITE);
    }
    if (message->flags & UI_MESSAGE_OPEN) {
        rect_t title = MAKE(rect_t, screen->x + 0.04f, screen->y + 0.02f, screen->w - 0.08f, 0.04f);
        rect_t body = MAKE(rect_t, screen->x + 0.06f, screen->y + 0.07f, screen->w - 0.12f, screen->h - 0.09f);
        re.DrawFill(screen, MAKE(color32_t, 10, 8, 5, 245));
        re.DrawText(&MAKE(drawText_t, .font = cl.fonts[message->title_font], .text = frame->text,
                          .rect = title, .color = MAKE(color32_t, 255, 215, 120, 255),
                          .textWidth = title.w, .halign = FONT_JUSTIFYCENTER, .valign = FONT_JUSTIFYMIDDLE));
        re.DrawText(&MAKE(drawText_t, .font = cl.fonts[message->body_font], .text = frame->tooltip,
                          .rect = body, .color = MAKE(color32_t, 240, 230, 205, 255),
                          .textWidth = body.w, .lineHeight = body.h, .flags = DRAW_WORD_WRAP,
                          .halign = FONT_JUSTIFYLEFT, .valign = FONT_JUSTIFYTOP));
    }
}

void SCR_LayoutUpdateBuildQueue(uiFrame_t const *frame, rect_t const *screen) {
    uiBuildQueue_t const *queue = frame->buffer.data;
    uiFrame_t *buildtimer = SCR_Frame(queue->buildtimer);
    uiFrame_t *firstitem  = SCR_Frame(queue->firstitem);

    if (queue->numitems && queue->items[0].starttime == 0 && queue->items[0].endtime == 0) {
        if (buildtimer) buildtimer->value = 0;
        if (firstitem) firstitem->tex.index = queue->items[0].image;
        return;
    }
    FOR_LOOP(i, queue->numitems) {
        uiBuildQueueItem_t const *item = &queue->items[i];
        if (cl.time < item->endtime) {
            float dur  = item->endtime - item->starttime;
            float elap = cl.time > item->starttime ? (float)(cl.time - item->starttime) : 0;
            float prog = MAX(0, MIN(dur > 0 ? elap / dur : 1, 1));
            if (buildtimer) buildtimer->value  = prog;
            if (firstitem)  firstitem->tex.index = item->image;
            break;
        }
    }
    (void)screen;
}

#define HP_BAR_HEIGHT_RATIO  0.175f
#define HP_BAR_SPACING_RATIO 0.02f

static uint32_t SCR_LayoutMultiselectEntityAt(uiFrame_t const *frame, vector2_t const *point) {
    uiMultiselect_t const *ms;
    uint32_t count;

    if (!frame || frame->flags.type != FT_MULTISELECT || !point ||
        !frame->buffer.data || frame->buffer.size < sizeof(uiMultiselect_t)) {
        return 0;
    }
    ms = frame->buffer.data;
    if (!ms->numcolumns) return 0;
    count = (frame->buffer.size - sizeof(uiMultiselect_t)) / sizeof(uiMultiselectItem_t);
    count = MIN((uint32_t)ms->numitems, count);
    FOR_LOOP(i, count) {
        rect_t cell = *SCR_LayoutRect(frame);
        uint32_t column = i % ms->numcolumns;
        uint32_t row = i / ms->numcolumns;

        cell.x += ms->offset.x * column;
        cell.y += ms->offset.y * row;
        if (Rect_contains(&cell, point)) return ms->items[i].entity;
    }
    return 0;
}

void SCR_LayoutDrawMultiSelect(uiFrame_t const *frame, rect_t const *scrn) {
    rect_t screen = *scrn;
    uiMultiselect_t const *ms = frame->buffer.data;
    uint32_t column = 0;
    FOR_LOOP(i, ms->numitems) {
        rect_t uv = { 0, 0, 1, 1 };
        uiMultiselectItem_t const *item = &ms->items[i];
        if ((item->flags & UI_MULTISELECT_ITEM_FOCUSED) && ms->focus_highlight) {
            rect_t highlight = {
                screen.x - screen.w * 0.185f,
                screen.y - screen.h * 0.10f,
                screen.w * 1.37f,
                screen.h * 1.75f
            };
            re.DrawImage(cl.pics[ms->focus_highlight], &highlight, &uv,
                         MAKE(color32_t, 255, 255, 0, 255));
        }
        re.DrawImage(cl.pics[item->image], &screen, &uv, frame->color);
        entityState_t const *ent = &cl.ents[item->entity].current;
        if (ent) {
            float hp   = BYTE2FLOAT(ent->stats[ENT_HEALTH]);
            float mana = BYTE2FLOAT(ent->stats[ENT_MANA]);
            rect_t rect  = { screen.x, screen.y + screen.h * (1 + HP_BAR_SPACING_RATIO),
                           screen.w * hp, screen.h * HP_BAR_HEIGHT_RATIO };
            uv.w = hp;
            re.DrawImage(cl.pics[ms->hp_bar],   &rect, &uv, MAKE(color32_t,0,255,0,255));
            uv.w  = mana; rect.w  = screen.w * mana;
            rect.y += screen.h * (HP_BAR_HEIGHT_RATIO + HP_BAR_SPACING_RATIO);
            re.DrawImage(cl.pics[ms->mana_bar], &rect, &uv, MAKE(color32_t,0,255,255,255));
        }
        if (++column >= ms->numcolumns) {
            column   = 0;
            screen.x = SCR_LayoutRect(frame)->x;
            screen.y += ms->offset.y;
        } else {
            screen.x += ms->offset.x;
        }
    }
}

void SCR_LayoutDrawPortrait(uiFrame_t const *frame, rect_t const *screen) {
    float const canvas_w = SCR_UICanvasWidth();
    rect_t const viewport = {
        screen->x / canvas_w,
        (UI_BASE_HEIGHT - screen->y - screen->h) / UI_BASE_HEIGHT,
        screen->w / canvas_w,
        screen->h / UI_BASE_HEIGHT
    };
    model_t const *port  = cl.portraits[frame->tex.index];
    model_t const *model = cl.models[frame->tex.index];
    model_t const *draw  = port ? port : model;

    if (!draw) return;

    cstring_t anim = (frame->text && *frame->text) ? frame->text : "Portrait";

    renderEntity_t entity = {0};
    entity.model = draw; entity.scale = 1.0f;
    entity.team = frame->stat;
    entity.flags = RF_NO_SHADOW | RF_NO_FOGOFWAR | RF_PORTRAIT_LIGHTING;
    re.SetEntityAnimFrame(draw, anim, &entity);

    viewDef_t vd = {0};
    vd.viewport     = viewport;
    vd.scissor      = viewport;
    vd.rdflags      = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG | RDF_USE_ENTITY_CAMERA;
    vd.num_entities = 1;
    vd.entities     = &entity;
    if (frame->buffer.size == sizeof(uiModel_t)) {
        size2_t size = re.GetWindowSize();
        float aspect = viewport.w * size.width / (viewport.h * size.height);
        /* The layout camera replaces the old radius guess and Stand-name mode switch. */
        M_ModelMatrix(frame->buffer.data, aspect, &vd.viewProjectionMatrix);
        Matrix4_identity(&vd.textureMatrix);
        Matrix4_identity(&vd.lightMatrix);
        vd.rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG | RDF_NOPARTICLES;
    }
    re.RenderFrame(&vd);
}

/* Loading bars are the one client-owned layout value; their art remains server-authored. */
void SCR_LayoutDrawLoadingBar(uiFrame_t const *frame, rect_t const *screen) {
    rect_t fill = *screen, uv = { 0, 0, 255, 255 };
    fill.w *= cl.loading_progress;
    uv.w *= cl.loading_progress;
    rect_t suv = Rect_div(&uv, 0xff);
    /* This frame declares an image. Model and image indices are independent namespaces. */
    re.DrawImage(SCR_LayoutPic(frame->tex.index), &fill, &suv, frame->color);
}

void SCR_LayoutDrawSprite(uiFrame_t const *frame, rect_t const *screen) {
    model_t const *model = cl.models[frame->tex.index];
    cstring_t anim = (frame->text && *frame->text) ? frame->text : "Stand";
    char sequence_anim[96];
    char phased_anim[96];
    float phase = 0.0f;

    /* Some server-authored sprites need one replicated stat for their
     * normalized animation phase and another for the authored sequence.  The
     * latter is opt-in because frame.value has unrelated meanings for other
     * frame types.  Only explicit #N selectors are rewritten. */
    if ((frame->flagsvalue & UIFLAG_SPRITE_STAT_SEQUENCE) &&
        frame->value > 0.0f && frame->value < (float)MAX_STATS && anim[0] == '#')
    {
        uint32_t const sequence_stat = (uint32_t)frame->value;
        cstring_t marker = strchr(anim, '@');
        if (marker) {
            snprintf(sequence_anim, sizeof(sequence_anim), "#%u%s",
                     (unsigned)cl.playerstate.stats[sequence_stat], marker);
        } else {
            snprintf(sequence_anim, sizeof(sequence_anim), "#%u",
                     (unsigned)cl.playerstate.stats[sequence_stat]);
        }
        anim = sequence_anim;
    }

    /* Sprite phase uses either a normalized snapshot stat or a generic local binding.
     * Loading sprites previously lost their geometry when converted into portrait bars. */
    if (SCR_LayoutContextValue(frame->stat, &phase) || (frame->stat > 0 && frame->stat < MAX_STATS)) {
        cstring_t marker = strchr(anim, '@');
        size_t base_len = marker ? (size_t)(marker - anim) : strlen(anim);
        if (frame->stat > 0 && frame->stat < MAX_STATS)
            phase = (float)cl.playerstate.stats[frame->stat] / (float)UINT16_MAX;

        if (base_len > sizeof(phased_anim) - 16) base_len = sizeof(phased_anim) - 16;
        snprintf(phased_anim, sizeof(phased_anim), "%.*s@%.6f", (int)base_len, anim, phase);
        anim = phased_anim;
    }
    re.DrawSprite(&MAKE(drawSprite_t, .model = model, .anim = anim, .x = screen->x, .y = screen->y, .id = frame, .scope = layout_current_window ? layout_current : (void *)(uintptr_t)(layout_current_layer + 1)));
}

/* Resolve the generic transient command-button alert tint from an absolute client/server clock deadline. */
static color32_t SCR_CommandButtonColor(uiFrame_t const *frame) {
    color32_t color = COLOR32_WHITE;
    uint32_t deadline;
    float phase, pulse;

    if (!frame || !(frame->flagsvalue & UIFLAG_ALERT_RED_PULSE) || frame->value <= 0.0f) return color;
    deadline = (uint32_t)frame->value;
    if ((int32_t)(cl.time - deadline) >= 0) return color;

    phase = (float)(cl.time % (SCR_ALERT_PULSE_HALF_MS * 2)) / (float)SCR_ALERT_PULSE_HALF_MS;
    pulse = 1.0f - fabsf(phase - 1.0f);
    color.g = color.b = (uint8_t)(255.0f - pulse * (255.0f - SCR_ALERT_PULSE_MIN_GB));
    return color;
}

static float SCR_CommandButtonRadialShade(uiFrame_t const *frame) {
    uiCommandButton_t const *state;
    uint32_t duration;

    if (!frame || !(frame->flagsvalue & UIFLAG_RADIAL_SHADE) ||
        frame->buffer.size != sizeof(uiCommandButton_t) || !frame->buffer.data) return 0.0f;
    state = frame->buffer.data;
    duration = (uint32_t)(state->radialEndTime - state->radialStartTime);
    if (!duration || (int32_t)(cl.time - state->radialEndTime) >= 0) return 0.0f;
    if ((int32_t)(cl.time - state->radialStartTime) <= 0) return 1.0f;
    return MIN(1.0f, (float)(uint32_t)(state->radialEndTime - cl.time) / (float)duration);
}

void SCR_LayoutDrawCommandButton(uiFrame_t const *frame, rect_t const *screen) {
    entityState_t const *sel = SCR_LayoutSelectedEntity();
    rect_t const uv = get_uvrect(frame->tex.coord);
    rect_t const suv = Rect_div(&uv, 0xff);
    rect_t scrn = scale_rect(screen, SCR_LayoutFrameIsHovered(frame) && layout_left_down ? 0.875f : 0.925f);
    re.DrawImageEx(&MAKE(drawImage_t,
        .texture     = cl.pics[frame->tex.index],
        .screen      = scrn,
        .uv          = suv,
        .color       = SCR_CommandButtonColor(frame),
        .shader      = SHADER_COMMANDBUTTON,
        .uActiveGlow = (frame->flagsvalue & UIFLAG_ALTERNATE_ACTIVE) ||
                       /* 255 means no ability on both sides, so idle units must not light every build choice. */
                       (sel && frame->stat != UINT8_MAX && sel->ability == frame->stat),
        .uRadialShade = SCR_CommandButtonRadialShade(frame)));
}

void layout_text(uiFrame_t const *frame, rect_t const *screen, cstring_t text) {
    drawText_t dt = SCR_GetDrawText(frame, screen->w, text, frame->buffer.data);
    dt.rect   = *screen;
    dt.flags |= DRAW_WORD_WRAP;
    re.DrawText(&dt);
}

static void SCR_LayoutApplyPushedTextOffset(uiFrame_t const *frame, rect_t *screen) {
    if (frame->parent >= SCR_NumFrames()) return;
    uiFrame_t const *parent = SCR_Frame(frame->parent);
    if (!parent) return;
    if (parent->flags.type != FT_GLUETEXTBUTTON && parent->flags.type != FT_GLUEBUTTON) return;
    if (!SCR_LayoutFrameHasClickCommand(parent) || !SCR_LayoutGlueTextButtonIsPushed(parent)) return;
    uiGlueTextButton_t const *b = parent->buffer.data;
    screen->x += b->pushedTextOffset.x;
    screen->y -= b->pushedTextOffset.y;
}

void SCR_LayoutDrawString(uiFrame_t const *frame, rect_t const *screen) {
    uiLabel_t const *label = frame->buffer.data;
    cstring_t value = SCR_GetStringValue(frame);
    uint32_t cursor = 0;

    /* Transient edit boxes draw their live value from the parent control.
     * Keeping the child STRING/TEXT only as the authored geometry carrier
     * avoids relying on generic label rendering for edit-control text. */
    if (frame->parent < SCR_NumFrames()) {
        uiFrame_t const *parent = SCR_Frame(frame->parent);
        if (parent && (parent->flags.type == FT_EDITBOX ||
                       parent->flags.type == FT_GLUEEDITBOX ||
                       parent->flags.type == FT_SLASHCHATBOX))
            return;
    }
    /* Label offsets use the same fixed-point wire representation as anchors. */
    rect_t scr = { screen->x + label->offsetx / UI_FRAMEPOINT_SCALE,
                 screen->y + label->offsety / UI_FRAMEPOINT_SCALE, screen->w, screen->h };
    SCR_LayoutApplyPushedTextOffset(frame, &scr);
    layout_text(frame, &scr, value);
    if (CL_WindowEditCursor(frame->number, &cursor) && re.GetTextSize) {
        drawText_t dt = SCR_GetDrawText(frame, scr.w, value, frame->buffer.data);
        dt.rect = scr;
        M_DrawTextInputCursor(&re, &dt, value, cursor, COLOR32_WHITE);
    }
}

/* Draw a nameplate whose backdrop and text share the measured content rect. */
void SCR_LayoutDrawNameTag(uiFrame_t const *frame, rect_t const *screen) {
    uiNameTag_t const *tag = frame->buffer.data;
    rect_t text = { screen->x + tag->padding_x, screen->y + tag->padding_y,
                  screen->w - tag->padding_x * 2, screen->h - tag->padding_y * 2 };
    drawText_t dt = SCR_GetDrawText(frame, text.w, SCR_GetStringValue(frame), &tag->text);
    SCR_LayoutDrawBackdrop2(frame, screen, &tag->background);
    dt.rect = text; dt.flags |= DRAW_WORD_WRAP; re.DrawText(&dt);
}

void SCR_LayoutDrawTextArea(uiFrame_t const *frame, rect_t const *screen) {
    uiTextArea_t const *ta = frame->buffer.data;
    cstring_t value = SCR_GetStringValue(frame);
    rect_t view = SCR_LayoutTextAreaView(frame, screen);
    drawText_t dt = SCR_GetDrawText(frame, view.w, value ? value : "", &(uiLabel_t){
        .font = ta->font, .textalignx = FONT_JUSTIFYLEFT, .textaligny = FONT_JUSTIFYTOP });
    float full_height = view.h;
    float max_scroll = 0.0f;

    dt.flags |= DRAW_WORD_WRAP;
    if (re.GetTextSize && view.w > 0.0f && view.h > 0.0f) {
        full_height = MAX(view.h, re.GetTextSize(&dt).y);
        max_scroll = MAX(0.0f, full_height - view.h);
    }
    dt.rect = MAKE(rect_t, view.x,
                   view.y - max_scroll * MIN(MAX(frame->value, 0.0f), 1.0f),
                   view.w, full_height);
    dt.flags |= DRAW_CLIP;
    dt.clip = view;
    re.DrawText(&dt);
}

void SCR_LayoutDrawEditBox(uiFrame_t const *frame, rect_t const *screen) {
    uiEditBox_t const *edit = frame->buffer.data;
    uiFrame_t const *text_frame = NULL;
    cstring_t value = NULL;
    uint32_t cursor = 0;
    uiLabel_t label = { 0 };
    drawText_t dt;
    rect_t text_rect;

    if (!edit || frame->buffer.size < sizeof(*edit)) return;
    SCR_LayoutDrawBackdrop2(frame, screen, &edit->background);

    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *child = SCR_Frame(i);
        if (!child || child->parent != frame->number) continue;
        if (child->flags.type == FT_STRING || child->flags.type == FT_TEXT) {
            text_frame = child;
            break;
        }
    }
    if (!text_frame) return;

    value = CL_WindowEditTextValue(text_frame->number);
    if (!value) value = SCR_GetStringValue(text_frame);
    if (!value) value = "";

    if (text_frame->buffer.data && text_frame->buffer.size >= sizeof(uiLabel_t))
        label = *(uiLabel_t const *)text_frame->buffer.data;
    if (edit->font) label.font = edit->font;

    /* The retail edit box text child is frequently authored as a narrow,
     * centred STRING.  That is suitable when the native edit-box renderer
     * owns clipping, but it is not the editable viewport itself.  Use the
     * parent control's inner rectangle horizontally so rendered text, cursor,
     * and clipping match the complete edit-box background.  Preserve the
     * authored child Y/height for vertical placement. */
    rect_t inner = Rect_inset(screen, MAX(edit->borderSize, 0.0f));
    rect_t authored = *SCR_LayoutRect(text_frame);
    text_rect = MAKE(rect_t,
        .x = inner.x,
        .y = authored.y,
        .w = inner.w,
        .h = authored.h > 0.0f ? authored.h : inner.h);

    dt = SCR_GetDrawText(text_frame, text_rect.w, value, &label);
    dt.rect = text_rect;
    dt.font = edit->font ? cl.fonts[edit->font] : dt.font;
    dt.color = edit->textColor.a ? edit->textColor : COLOR32_WHITE;
    dt.halign = FONT_JUSTIFYLEFT;
    dt.flags |= DRAW_CLIP;
    dt.clip = text_rect;

    if (Cvar_Integer("ui_window_debug", 0) >= 2)
        fprintf(stderr,
                "UI_WINDOW_DEBUG edit-draw frame=%u textFrame=%u rect=(%.4f,%.4f %.4fx%.4f) "
                "editRect=(%.4f,%.4f %.4fx%.4f) border=%.4f "
                "font=%u color=(%u,%u,%u,%u) text=\"%s\"\n",
                (unsigned)frame->number, (unsigned)text_frame->number,
                text_rect.x, text_rect.y, text_rect.w, text_rect.h,
                screen->x, screen->y, screen->w, screen->h, edit->borderSize,
                (unsigned)label.font, (unsigned)dt.color.r, (unsigned)dt.color.g,
                (unsigned)dt.color.b, (unsigned)dt.color.a, value);

    re.DrawText(&dt);
    if (CL_WindowEditCursor(text_frame->number, &cursor) && re.GetTextSize)
        M_DrawTextInputCursor(&re, &dt, value, cursor, COLOR32_WHITE);
}

void SCR_LayoutDrawListBox(uiFrame_t const *frame, rect_t const *screen) {
    uiListBox_t const *lb = frame->buffer.data;
    rect_t list_rect = Rect_inset(screen, lb->border);
    float item_height = lb->itemHeight > 0 ? lb->itemHeight : 0.018f;
    int16_t selectedIndex = lb->selectedIndex;
    uint32_t scrollOffset = 0;
    char items[MAX_LISTBOX_TEXT];

    SCR_LayoutDrawBackdrop2(frame, screen, &lb->background);

    uiFrame_t const *scrollbar = NULL;
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *child = SCR_Frame(i);
        if (child && child->parent == frame->number && child->flags.type == FT_SCROLLBAR) {
            scrollbar = child; break;
        }
    }
    if (scrollbar) {
        float sw = MAX(SCR_LayoutRect(scrollbar)->w, 0.0f);
        if (sw > 0 && sw < list_rect.w) list_rect.w -= sw;
    }
    uint32_t maxScroll = (uint32_t)SCR_LayoutListBoxMaxScroll(frame);
    scrollOffset = maxScroll
        ? (uint32_t)lroundf(MIN(MAX(frame->value, 0.0f), 1.0f) * maxScroll)
        : 0;
    if (scrollbar) ((uiFrame_t *)scrollbar)->value = maxScroll ? scrollOffset / (float)maxScroll : 0.0f;
    if (!frame->text || !*frame->text) return;

    snprintf(items, sizeof(items), "%s", frame->text);
    char *save = NULL;
    char *line = strtok_r(items, "\n", &save);
    int index  = 0;
    while (line && index < (int)scrollOffset) { line = strtok_r(NULL, "\n", &save); index++; }

    /* Warsmash creates floor(viewHeight / itemHeight) row frames.  Keep the
     * renderer on the same whole-row count used by scrolling and hit-testing;
     * drawing a partial extra row makes the visual list disagree with both. */
    uint32_t const visible_rows = (uint32_t)SCR_LayoutListBoxVisibleRows(frame, &list_rect);
    uint32_t drawn_rows = 0;
    float item_y = list_rect.y;
    while (line && drawn_rows < visible_rows) {
        char *hidden = strchr(line, '\t');
        if (hidden) *hidden = '\0';
        rect_t row = { list_rect.x, item_y, list_rect.w, item_height };
        if (index == selectedIndex) {
            rect_t selection = row;
            selection.x += 0.0025f;
            selection.y += 0.0020f;
            selection.w = MAX(0.0f, selection.w - 0.0050f);
            selection.h = MAX(0.0f, selection.h - 0.0040f);
            re.DrawImage(cl.pics[0], &selection, &MAKE(rect_t,0,0,1,1),
                         MAKE(color32_t,32,64,180,128));
        }
        re.DrawText(&MAKE(drawText_t,
            .font       = cl.fonts[lb->text.font],
            .text       = line,
            .color      = frame->color.a ? frame->color : COLOR32_WHITE,
            .halign     = FONT_JUSTIFYLEFT,
            .valign     = FONT_JUSTIFYMIDDLE,
            .icons      = cl.pics,
            .lineHeight = 1.33,
            .textWidth  = row.w,
            /* A list row owns only its row rectangle.  Row-local clipping is
             * stricter than clipping the batch to the entire chooser and also
             * prevents glyph overhang from leaking into neighbouring UI. */
            .flags      = DRAW_CLIP,
            .clip       = row,
            .rect       = row));
        item_y += item_height;
        drawn_rows++;
        line = strtok_r(NULL, "\n", &save);
        index++;
    }
}

void SCR_LayoutDrawTooltip(uiFrame_t const *frame, rect_t const *scrn) {
    if (!active_tooltip) return;
    /* Several HUD layers may carry the shared tooltip presentation frame.
     * Draw it only in the layer/window that owns the hovered source frame so a
     * passive resource tooltip is not overdrawn again by command-card layers. */
    if (layout_hovered) {
        if (!layout_current_window || layout_current != layout_hovered) return;
    } else if (layout_current_window || layout_current_layer != layout_hovered_layer) {
        return;
    }
    uiTooltip_t const *tt = frame->buffer.data;
    float const PAD = 0.005f;
    rect_t screen = *scrn;
    drawText_t dt = SCR_GetDrawText(frame, screen.w - PAD*2, active_tooltip, &tt->text);
    dt.flags |= DRAW_WORD_WRAP;
    vector2_t tsz = re.GetTextSize(&dt);
    tsz.y    += PAD * 2;
    screen.y += screen.h - tsz.y;
    screen.h  = tsz.y;
    rect_t text  = Rect_inset(&screen, PAD);
    SCR_LayoutDrawBackdrop(frame, &screen);
    dt = SCR_GetDrawText(frame, text.w, active_tooltip, &tt->text);
    dt.rect   = text;
    dt.flags |= DRAW_WORD_WRAP;
    re.DrawText(&dt);
}

typedef struct { FRAMETYPE type; void (*func)(uiFrame_t const *, rect_t const *); } drawer_t;

static drawer_t updaters[] = {
    { FT_BUILDQUEUE, SCR_LayoutUpdateBuildQueue },
};

static drawer_t drawers[] = {
    { FT_TEXTURE,        SCR_LayoutDrawTexture },
    { FT_HIGHLIGHT,      SCR_LayoutDrawHighlight },
    { FT_BACKDROP,       SCR_LayoutDrawBackdrop },
    { FT_SIMPLESTATUSBAR,SCR_LayoutDrawStatusbar },
    { FT_SEGMENTED_STATUSBAR, SCR_LayoutDrawSegmentedStatusbar },
    { FT_LOADING_BAR,    SCR_LayoutDrawLoadingBar },
    { FT_COMMANDBUTTON,  SCR_LayoutDrawCommandButton },
    { FT_STRING,         SCR_LayoutDrawString },
    { FT_NAMETAG,        SCR_LayoutDrawNameTag },
    { FT_TEXT,           SCR_LayoutDrawString },
    { FT_TEXTAREA,       SCR_LayoutDrawTextArea },
    { FT_EDITBOX,        SCR_LayoutDrawEditBox },
    { FT_GLUEEDITBOX,    SCR_LayoutDrawEditBox },
    { FT_SLASHCHATBOX,   SCR_LayoutDrawEditBox },
    { FT_LISTBOX,        SCR_LayoutDrawListBox },
    { FT_SCROLLBAR,      SCR_LayoutDrawScrollBar },
    { FT_TOOLTIPTEXT,    SCR_LayoutDrawTooltip },
    { FT_MODEL,          SCR_LayoutDrawPortrait },
    { FT_SPRITE,         SCR_LayoutDrawSprite },
    { FT_PORTRAIT,       SCR_LayoutDrawPortrait },
    { FT_MINIMAP,        CL_LayoutDrawMinimap },
    { FT_BUILDQUEUE,     SCR_LayoutDrawBuildQueue },
    { FT_MESSAGE_QUEUE,  SCR_LayoutDrawMessageQueue },
    { FT_MULTISELECT,    SCR_LayoutDrawMultiSelect },
    { FT_CHECKBOX,       SCR_LayoutCheckBox },
    { FT_GLUECHECKBOX,   SCR_LayoutCheckBox },
    { FT_SIMPLECHECKBOX, SCR_LayoutCheckBox },
    { FT_SIMPLEBUTTON,   SCR_LayoutSimpleButton },
    { FT_BUTTON,         SCR_LayoutGlueTextButton },
    { FT_TEXTBUTTON,     SCR_LayoutGlueTextButton },
    { FT_POPUPMENU,      SCR_LayoutGlueTextButton },
    { FT_GLUEPOPUPMENU,  SCR_LayoutGlueTextButton },
    { FT_GLUETEXTBUTTON, SCR_LayoutGlueTextButton },
    { FT_GLUEBUTTON,     SCR_LayoutGlueTextButton },
};

void SCR_LayoutDrawFrame(uiFrame_t const *frame) {
    rect_t const *screen = SCR_LayoutRect(frame);
    /* Model sprites and dynamic drawers legitimately have no authored size.
     * Only absent context bindings suppress drawing, just as they collapse layout. */
    if (!SCR_LayoutContextFrameVisible(frame)) return;
    FOR_LOOP(j, sizeof(drawers)/sizeof(*drawers)) {
        if (drawers[j].type == frame->flags.type) {
            drawers[j].func(frame, screen);
            break;
        }
    }
}

void SCR_LayoutUpdateFrame(uiFrame_t const *frame) {
    rect_t const *screen = SCR_LayoutRect(frame);

    /* Tooltip ownership is a generic frame contract, not a command-button
     * behavior. Resource-bar labels/icons and other passive HUD frames may
     * carry tooltip text without becoming clickable controls. */
    if (SCR_LayoutFrameIsHovered(frame) && frame->tooltip && *frame->tooltip)
        active_tooltip = SCR_GetTooltipText(frame);

    FOR_LOOP(j, sizeof(updaters)/sizeof(*updaters)) {
        if (updaters[j].type == frame->flags.type) {
            updaters[j].func(frame, screen);
            break;
        }
    }
}

static void SCR_LayoutRunFrames(handle_t layout, void (*fn)(uiFrame_t const *)) {
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *frame = SCR_Frame(i);
        if (frame) fn(frame);
    }
}

void SCR_LayoutUpdateTooltip(handle_t layout) {
    SCR_LayoutRunFrames(layout, SCR_LayoutUpdateFrame);
}

void SCR_LayoutDrawOverlay(handle_t layout) {
    layout_current = layout;
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *f = SCR_Frame(i);
        if (f && f->flags.type == FT_SPRITE && !(f->flagsvalue & UIFLAG_SPRITE_OVERLAY)) SCR_LayoutDrawFrame(f);
    }
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *f = SCR_Frame(i);
        if (f && f->flags.type != FT_SPRITE) SCR_LayoutDrawFrame(f);
    }
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *f = SCR_Frame(i);
        if (f && (f->flags.type == FT_GLUETEXTBUTTON || f->flags.type == FT_GLUEBUTTON))
            SCR_LayoutDrawGlueTextButtonHighlight(f);
    }
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *f = SCR_Frame(i);
        if (f && f->flags.type == FT_SPRITE && (f->flagsvalue & UIFLAG_SPRITE_OVERLAY)) SCR_LayoutDrawFrame(f);
    }
}

void SCR_DrawLayout(void) {
    active_tooltip = NULL;

    if (cl.playerstate.cinefade > 0) {
        color32_t color = COLOR32_BLACK;
        rect_t const screen = MAKE(rect_t, 0, 0, SCR_UICanvasWidth(), UI_BASE_HEIGHT);
        color.a = 255 * cl.playerstate.cinefade;
        re.DrawImage(cl.pics[0], &screen, &MAKE(rect_t,0,0,1,1), color);
    }

    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        uint32_t flags = cl.playerstate.uiflags;
        if (layer == LAYER_LOADING) continue;
        if ((1 << layer) & flags) continue;
        handle_t layout = layout_layers[layer];
        if (layout) {
            rect_t root;
            layout_current_window = false;
            layout_current_layer = layer;
            SCR_ClearLayer(layout, layer);
            if (layer == LAYER_WORLD_HOVER) {
                if (!SCR_LayoutWorldHoverRoot(&root)) continue;
                SCR_SetLayoutRoot(&root);
            }
            SCR_LayoutUpdateTooltip(layout);
            SCR_LayoutDrawOverlay(layout);
        }
    }
    /* Transient windows are frontmost gameplay UI. The window manager already
     * preserves server-authored z-order, but previously had no screen caller. */
    CL_WindowDraw();
}

/* The initial layout is a loading-only packet, not a gameplay HUD layer. */
void SCR_DrawLoadingLayout(void) {
    handle_t layout = layout_layers[LAYER_LOADING];
    rect_t root;

    if (!layout) return;
    layout_current_window = false;
    layout_current_layer = LAYER_LOADING;
    SCR_ClearLayer(layout, LAYER_LOADING);
    root = SCR_LayoutSceneRect();
    SCR_SetLayoutRoot(&root);
    SCR_LayoutDrawOverlay(layout);
}

void SCR_SetLayoutLayer(uint32_t layer, handle_t data) {
    if (layer < MAX_LAYOUT_LAYERS) layout_layers[layer] = data;
}

void SCR_ClearLayoutLayer(uint32_t layer) {
    if (layer < MAX_LAYOUT_LAYERS) layout_layers[layer] = NULL;
}

void SCR_ClearLayoutResources(void) {
    FOR_LOOP(i, MAX_DYNAMIC_IMAGES) {
        SAFE_DELETE(layout_dynamic_pics[i], re.ReleaseTexture);
        layout_dynamic_pic_names[i][0] = '\0';
    }
    layout_dynamic_pic_cursor = 0;
}

static bool SCR_LayoutLayerVisible(uint32_t layer) { return layer < MAX_LAYOUT_LAYERS && layout_layers[layer] && !((1u << layer) & cl.playerstate.uiflags); }

/* Result screens outrank Quest when malformed/server-overlapping modal layers coexist. */
static int SCR_LayoutModalLayer(void) {
    if (SCR_LayoutLayerVisible(LAYER_GAME_RESULT)) return LAYER_GAME_RESULT;
    if (SCR_LayoutLayerVisible(LAYER_QUESTDIALOG)) return LAYER_QUESTDIALOG;
    return -1;
}

bool SCR_LayoutModalActive(void) { return SCR_LayoutModalLayer() >= 0; }

bool SCR_LayoutMouseEvent(menuMouseEvent_t event, int x, int y, int32_t param) {
    vector2_t const point = SCR_ScreenToUI(x, y);
    uiFrame_t const *hovered_frame = NULL;
    int const modal_layer = SCR_LayoutModalLayer();
    /* This path handles persistent layout layers, not client-managed windows. */
    layout_current_window = false;
    layout_hovered = NULL;
    layout_hovered_number = 0;
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        handle_t layout = layout_layers[layer];
        if (modal_layer >= 0 && (int)layer != modal_layer) continue;
        uint32_t flags = cl.playerstate.uiflags;
        if (!layout || layer == LAYER_WORLD_HOVER || (1 << layer) & flags) continue;
        SCR_Clear(layout);
        for (uint32_t i = SCR_NumFrames(); i > 0; i--) {
            uiFrame_t const *frame = SCR_Frame(i - 1);
            bool hit;

            if (!frame) continue;
            if (frame->flags.type == FT_MULTISELECT)
                hit = SCR_LayoutMultiselectEntityAt(frame, &point) != 0;
            else
                hit = (SCR_LayoutFrameHasClickCommand(frame) ||
                       (frame->tooltip && *frame->tooltip)) &&
                      Rect_contains(SCR_LayoutRect(frame), &point);
            if (hit) {
                layout_hovered_number = frame->number;
                hovered_frame = frame;
                layout_hovered_layer = layer;
                break;
            }
        }
        if (layout_hovered_number) break;
    }

    /* Proxy command buttons may carry a secondary command in text. Warcraft
     * uses this for right-click autocast toggles while preserving left-click
     * targeting on the same icon. Consume both right-button edges so the click
     * cannot also become a world Smart order. */
    if (param == SDL_BUTTON_RIGHT) {
        if (!hovered_frame || hovered_frame->flags.type != FT_COMMANDBUTTON ||
            !hovered_frame->text || !*hovered_frame->text) {
            return false;
        }
        if (event == MENU_MOUSE_DOWN) return true;
        if (event != MENU_MOUSE_UP) return false;
        MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
        SZ_Printf(&cls.netchan.message, "%s", hovered_frame->text);
        return true;
    }

    if (param != SDL_BUTTON_LEFT) return false;
    if (event == MENU_MOUSE_DOWN) {
        char command[CMDARG_LEN * 2];
        layout_left_down = true;
        /* Multiselect icons are server-authored gameplay controls. They do not
         * have an onclick string, so consume the press here and resolve the
         * concrete entity on release below. */
        if (hovered_frame && hovered_frame->flags.type == FT_MULTISELECT) return true;
        if (!hovered_frame || layout_held_command[0]) return false;
        SCR_LayoutFormatOnClickCommand(hovered_frame->onclick, command, sizeof(command));
        if (command[0] != '+') return false;
        strlcpy(layout_held_command, command, sizeof(layout_held_command));
        MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
        SZ_Printf(&cls.netchan.message, "%s", layout_held_command);
        return false;
    }
    if (event != MENU_MOUSE_UP) return false;
    layout_left_down = false;
    if (layout_held_command[0]) {
        char command[sizeof(layout_held_command)];
        strlcpy(command, layout_held_command, sizeof(command));
        command[0] = '-';
        layout_held_command[0] = '\0';
        MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
        SZ_Printf(&cls.netchan.message, "%s", command);
        return false;
    }

    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        handle_t layout = layout_layers[layer];
        uint32_t flags = cl.playerstate.uiflags;
        if (modal_layer >= 0 && (int)layer != modal_layer) continue;
        if (!layout || layer == LAYER_WORLD_HOVER || (1 << layer) & flags) continue;
        SCR_Clear(layout);
        for (uint32_t i = SCR_NumFrames(); i > 0; i--) {
            uiFrame_t const *frame = SCR_Frame(i - 1);

            if (!frame) continue;
            if (frame->flags.type == FT_MULTISELECT) {
                uint32_t entity = SCR_LayoutMultiselectEntityAt(frame, &point);
                if (entity) {
                    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
                    SZ_Printf(&cls.netchan.message, "focus %u", (unsigned)entity);
                    return true;
                }
                continue;
            }
            if (!SCR_LayoutFrameHasClickCommand(frame)) continue;
            if (Rect_contains(SCR_LayoutRect(frame), &point)) {
                char command[CMDARG_LEN * 2];
                SCR_LayoutFormatOnClickCommand(frame->onclick, command, sizeof(command));
                MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
                SZ_Printf(&cls.netchan.message, "%s", command);
                return false;
            }
        }
    }
    return false;
}

/* Dispatch a command-button hotkey the same way a mouse click on that */
bool SCR_LayoutKeyEvent(int key) {
    int const upper = toupper(key);
    int const modal_layer = SCR_LayoutModalLayer();

    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        handle_t layout = layout_layers[layer];
        if (modal_layer >= 0 && (int)layer != modal_layer) continue;
        uint32_t flags = cl.playerstate.uiflags;
        if (!layout || layer == LAYER_WORLD_HOVER || (1 << layer) & flags) continue;
        SCR_Clear(layout);
        for (uint32_t i = SCR_NumFrames(); i > 0; i--) {
            uiFrame_t const *frame = SCR_Frame(i - 1);
            if (!frame || !SCR_LayoutFrameHasClickCommand(frame)) continue;
            bool const is_cancel = key == K_ESCAPE && !strcmp(frame->onclick, "button CmdCancel");
            if (is_cancel || (frame->hotkey && toupper(frame->hotkey) == upper)) {
                char command[CMDARG_LEN * 2];
                SCR_LayoutFormatOnClickCommand(frame->onclick, command, sizeof(command));
                MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
                SZ_Printf(&cls.netchan.message, "%s", command);
                return true;
            }
        }
    }
    return false;
}

static bool SCR_LayoutSelectionBlockerType(FRAMETYPE type) {
    switch (type) {
        case FT_TEXTURE:
        case FT_BACKDROP:
        case FT_SIMPLESTATUSBAR:
        case FT_SEGMENTED_STATUSBAR:
        case FT_COMMANDBUTTON:
        case FT_MODEL:
        case FT_PORTRAIT:
        case FT_MINIMAP:
        case FT_BUILDQUEUE:
        case FT_MESSAGE_QUEUE:
        case FT_MULTISELECT:
        case FT_CHECKBOX:
        case FT_GLUECHECKBOX:
        case FT_SIMPLECHECKBOX:
        case FT_SIMPLEBUTTON:
        case FT_BUTTON:
        case FT_TEXTBUTTON:
        case FT_GLUETEXTBUTTON:
        case FT_GLUEBUTTON:
            return true;
        default:
            return false;
    }
}

static bool SCR_RangesOverlap(float a0, float a1, float b0, float b1) {
    return MAX(a0, b0) < MIN(a1, b1);
}

/* The WC3 command console is not a flat rectangle: the info/status and command
 * panels protrude above the world viewport's bottom edge.  Selection starts in
 * the world, so constrain its moving corner against visible bottom-console
 * frames before the renderer sees the marquee.  This keeps selection geometry
 * and selection hit-testing out of authored HUD regions instead of relying on
 * transparent console art to hide the line.  Full-screen world games have a
 * scissor bottom at UI_BASE_HEIGHT, making this a no-op. */
void SCR_LayoutClampSelectionRect(rect_t *rect) {
    vector2_t start, finish, clamped;
    float world_bottom;
    float xmin, xmax;
    size2_t window;

    if (!rect) {
        return;
    }
    window = re.GetWindowSize();
    if (window.width <= 0 || window.height <= 0 || cl.viewDef.scissor.y <= 0.0f) {
        return;
    }

    start = SCR_ScreenToUI((int)rect->x, (int)rect->y);
    finish = SCR_ScreenToUI((int)(rect->x + rect->w), (int)(rect->y + rect->h));
    clamped = finish;
    world_bottom = (1.0f - cl.viewDef.scissor.y) * UI_BASE_HEIGHT;
    xmin = MIN(start.x, finish.x);
    xmax = MAX(start.x, finish.x);

    /* Clamp vertical travel first.  Only frames that extend into the bottom
     * console count; upper resource/command strips are not part of this mask. */
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        handle_t layout = layout_layers[layer];
        uint32_t flags = cl.playerstate.uiflags;
        if (!layout || layer == LAYER_WORLD_HOVER || ((1 << layer) & flags)) continue;
        SCR_Clear(layout);
        FOR_LOOP(i, SCR_NumFrames()) {
            uiFrame_t const *frame = SCR_Frame(i);
            rect_t const *r;
            float bottom;
            if (!frame || !SCR_LayoutSelectionBlockerType(frame->flags.type)) continue;
            r = SCR_LayoutRect(frame);
            if (!r || r->w <= 0.0f || r->h <= 0.0f) continue;
            bottom = r->y + r->h;
            if (bottom < world_bottom || !SCR_RangesOverlap(xmin, xmax, r->x, r->x + r->w)) continue;

            if (finish.y > start.y && r->y > start.y && r->y < clamped.y) {
                clamped.y = r->y;
            } else if (finish.y < start.y && bottom < start.y && bottom > clamped.y) {
                clamped.y = bottom;
            }
        }
    }

    rect->w = (clamped.x / SCR_UICanvasWidth()) * window.width - rect->x;
    rect->h = (clamped.y / UI_BASE_HEIGHT) * window.height - rect->y;
}

bool SCR_LayoutHitTest(int x, int y) {
    vector2_t const point = SCR_ScreenToUI(x, y);
    if (SCR_LayoutModalActive()) return true;
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        handle_t layout = layout_layers[layer];
        uint32_t flags = cl.playerstate.uiflags;
        if (!layout || layer == LAYER_WORLD_HOVER || (1 << layer) & flags) continue;
        SCR_Clear(layout);
        FOR_LOOP(i, SCR_NumFrames()) {
            uiFrame_t const *frame = SCR_Frame(i);
            if (!frame) continue;
            /* Persistent controls may intentionally sit over the world instead
             * of over a console texture. Any clickable server-authored frame
             * is gameplay UI and must suppress world selection underneath it. */
            if (frame->flags.type == FT_MULTISELECT) {
                if (SCR_LayoutMultiselectEntityAt(frame, &point)) return true;
                continue;
            }
            if (frame->flags.type != FT_TEXTURE &&
                !SCR_LayoutFrameHasClickCommand(frame) &&
                !(frame->tooltip && *frame->tooltip)) continue;
            if (Rect_contains(SCR_LayoutRect(frame), &point)) return true;
        }
    }
    return false;
}

/* Consume the old wire shape to preserve packet alignment; current games author svc_layout. */
void CL_ParseUnitUI(sizeBuf_t *msg) {
    int count = MSG_ReadByte(msg);
    if (count) fprintf(stderr, "CL_ParseUnitUI: obsolete HUD packet; server must author svc_layout\n");
    for (int i = 0; i < count && msg->readcount < msg->cursize; i++) {
        MSG_ReadShort(msg);
        for (int kind = 0; kind < 3; kind++) {
            int items = MSG_ReadByte(msg), strings = kind == 0 ? 4 : kind == 1 ? 3 : 1;
            for (int j = 0; j < items && msg->readcount < msg->cursize; j++) {
                for (int k = 0; k < strings; k++) MSG_ReadString2(msg);
                if (kind == 2) MSG_ReadShort(msg);
                else MSG_ReadByte(msg);
            }
        }
    }
}
