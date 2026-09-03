#include "client.h"
#include "ui_layout.h"
#include <ctype.h>
#include <SDL2/SDL.h>

BOOL scr_initialized;

#define SCR_FPS_HEIGHT 8
#define SCR_FPS_BOTTOM_MARGIN 4

/* Returns the UI canvas width for the current window aspect.  For SC2
 * widescreen the canvas expands horizontally while the height stays fixed
 * (matches cl_layout.c::SCR_GetUISceneRect and r_draw.c::R_UISceneRect). */
FLOAT SCR_UICanvasWidth(void) {
#ifdef SC2
    size2_t win = re.GetWindowSize();
    if (win.height > 0) {
        FLOAT aspect = (FLOAT)win.width / (FLOAT)win.height;
        if (aspect > UI_MIN_ASPECT)
            return UI_BASE_HEIGHT * aspect;
    }
#endif
    return UI_BASE_WIDTH;
}

/*
 * SDL mouse positions are window pixels, while UI/layout coordinates use the
 * engine's virtual canvas.  Cursor drawing and FDF hit-testing must share this
 * mapping, including SC2's widened canvas on widescreen displays.
 */
VECTOR2 SCR_ScreenToUI(int x, int y) {
    size2_t window = re.GetWindowSize();
    FLOAT nx = 0.0f, ny = 0.0f;

    if (window.width > 0 && window.height > 0) {
        nx = (FLOAT)x / (FLOAT)window.width;
        ny = (FLOAT)y / (FLOAT)window.height;
    }

    return MAKE(VECTOR2, nx * SCR_UICanvasWidth(), ny * UI_BASE_HEIGHT);
}

static void SCR_DrawString(int x, int y, LPCSTR string) {
    if (string) re.DrawString(x, y, string);
}

static void SCR_DrawFPS(DWORD msec) {
    static DWORD elapsed = 0;
    static DWORD frames_drawn = 0;
    static DWORD fps = 0;
    char text[64];
    size2_t window = re.GetWindowSize();
    DWORD inset = SCR_FPS_HEIGHT + SCR_FPS_BOTTOM_MARGIN;
    DWORD y = window.height > inset ? window.height - inset : 0;

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
static void SCR_UpdateSystemCursor(BOOL game_cursor_active) {
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

static COLOR32 SCR_CursorTint(void) {
    DWORD const entnum = cl.hover_entity;

    if (entnum && entnum < MAX_CLIENT_ENTITIES) {
        LPCENTITYSTATE state = &cl.ents[entnum].current;
        if (state->flags & EF_HOVER_HEALTH) {
            if (state->flags & EF_HOSTILE) {
                return MAKE(COLOR32, 255, 0, 0, 255);
            }
            if (state->flags & EF_NEUTRAL) {
                return MAKE(COLOR32, 255, 220, 80, 255);
            }
        }
    }
    return COLOR32_WHITE;
}

static void SCR_DrawCursor(void) {
    int const x = (int)mouse.origin.x, y = (int)mouse.origin.y;
    BOOL drawn = false;

    if (Cvar_Integer("r_cursor", 0) == 1) {
        VECTOR2 const pos = SCR_ScreenToUI(x, y);
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

void SCR_DrawScreenField(DWORD msec) {
    re.BeginFrame();

    switch (cls.state) {
    default:
        Com_Error(ERR_FATAL, "SCR_DrawScreenField: bad cls.state");
        break;
    case ca_disconnected:
        ui.Refresh(cl.time);
        break;
    case ca_connecting:
    case ca_connected:
        ui.Refresh(cl.time);
        break;
    case ca_active:
        V_RenderView();
        if (Cvar_Integer("r_hud", 1)) {
            SCR_DrawLayout();
        }
        /* TODO: research whether to replace key_dest enum with a keyCatchers bitmask
        * like Q3 — multiple input consumers can be active simultaneously. */
        if (cls.key_dest == key_menu) {
            ui.Refresh(cl.time);
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

void SCR_UpdateScreen(DWORD msec) {
    static int recursive;
    static DWORD no_refresh_elapsed, no_refresh_frames;

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

static LPCSTR active_tooltip = NULL;
static HANDLE layout_layers[MAX_LAYOUT_LAYERS];
static LPTEXTURE layout_dynamic_pics[MAX_DYNAMIC_IMAGES];
static char layout_dynamic_pic_names[MAX_DYNAMIC_IMAGES][512];
static DWORD layout_dynamic_pic_cursor;
static BOOL layout_left_down;
static char layout_held_command[CMDARG_LEN * 2];
static DWORD layout_hovered_number;
static DWORD layout_hovered_layer;
static DWORD layout_current_layer;
static HANDLE layout_hovered;
static HANDLE layout_current;
static BOOL layout_current_window;

/* Entity-context layouts use a server-authored tree rooted at the client-projected model top. */
BOOL SCR_LayoutWorldHoverRoot(LPRECT root) {
    LPCENTITYSTATE ent = SCR_LayoutContextEntity();
    FLOAT const *m;
    FLOAT cx, cy, cw, vx, vy;
    VECTOR3 top;

    if (!root || !ent) return false;
    FOR_LOOP(i, cl.viewDef.num_entities) {
        if (cl.viewDef.entities[i].number != cl.hover_entity) continue;
        if (!re.GetEntityOverheadPosition(&cl.viewDef.entities[i], &top)) return false;
        m = cl.viewDef.viewProjectionMatrix.v;
        cx = m[0] * top.x + m[4] * top.y + m[8] * top.z + m[12];
        cy = m[1] * top.x + m[5] * top.y + m[9] * top.z + m[13];
        cw = m[3] * top.x + m[7] * top.y + m[11] * top.z + m[15];
        if (cw <= 0.0001f) return false;
        vx = cl.viewDef.viewport.x + (cx / cw * 0.5f + 0.5f) * cl.viewDef.viewport.w;
        vy = cl.viewDef.viewport.y + (cy / cw * 0.5f + 0.5f) * cl.viewDef.viewport.h;
        if (vx < cl.viewDef.scissor.x || vx > cl.viewDef.scissor.x + cl.viewDef.scissor.w ||
            vy < cl.viewDef.scissor.y || vy > cl.viewDef.scissor.y + cl.viewDef.scissor.h) return false;
        *root = MAKE(RECT, vx * UI_BASE_WIDTH, (1.0f - vy) * UI_BASE_HEIGHT, 0, 0);
        return true;
    }
    return false;
}

static RECT Rect_inset(LPCRECT r, FLOAT inset) {
    return MAKE(RECT, r->x+inset, r->y+inset, r->w-inset*2, r->h-inset*2);
}

static RECT get_uvrect(uint8_t const *tc) {
    return (RECT){ tc[0], tc[2], tc[1]-tc[0], tc[3]-tc[2] };
}

static LPCTEXTURE SCR_LayoutPic(RESOURCE image) {
    return image && image < MAX_IMAGES ? cl.pics[image] : NULL;
}

static LPCTEXTURE SCR_LayoutGetDynamicTexture(LPCSTR resource) {
    if (!resource || !*resource || !strcmp(resource, " ")) return NULL;

    DWORD slot = MAX_DYNAMIC_IMAGES;
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

static RECT scale_rect(LPCRECT r, FLOAT f) {
    FLOAT dx = r->w * (1-f), dy = r->h * (1-f);
    return (RECT){ r->x + dx/2, r->y + dy/2, r->w - dx, r->h - dy };
}

static LPCENTITYSTATE SCR_LayoutSelectedEntity(void) {
    FOR_LOOP(i, cl.num_entities) {
        LPCENTITYSTATE ent = &cl.ents[i].current;
        if (ent && (ent->renderfx & RF_SELECTED)) return ent;
    }
    return NULL;
}

void SCR_LayoutDrawStatusbar(LPCUIFRAME frame, LPCRECT screen) {
    RECT const uv = { 0, 0, 255, 255 };
    RECT screen2 = *screen, uv2 = uv;
    FLOAT value = frame->value;
    SCR_LayoutContextValue(frame->stat, &value);
    screen2.w *= value;
    uv2.w    *= value;
    RECT const suv2 = Rect_div(&uv2, 0xff);
    re.DrawImage(cl.pics[frame->tex.index], &screen2, &suv2, frame->color);
    if (frame->tex.index2 > 0) {
        RECT const suv = Rect_div(&uv, 0xff);
        re.DrawImage(cl.pics[frame->tex.index2], screen, &suv, COLOR32_WHITE);
    }
}

void SCR_LayoutDrawTexture(LPCUIFRAME frame, LPCRECT screen) {
    FLOAT value;
    if (SCR_LayoutContextValue(frame->stat, &value) && value <= 0.0f) return;
    if (!frame->tex.index) return;  /* unresolved texture — skip to avoid drawing cl.pics[0] */
    LPCTEXTURE tex = cl.pics[frame->tex.index];
    if (frame->stat >= MAX_STATS && frame->stat - MAX_STATS < MAX_STATS) {
        LPCSTR resource = cl.playerstate.texts[frame->stat - MAX_STATS];
        LPCTEXTURE dyn = SCR_LayoutGetDynamicTexture(resource);
        if (dyn) tex = dyn;
    }
    if (frame->buffer.data && frame->buffer.size >= sizeof(uiTextureUV_t)) {
        uiTextureUV_t const *uv = frame->buffer.data;
        COLOR32 color = uv->color.a ? uv->color : frame->color;
        re.DrawImageEx(&MAKE(drawImage_t,
                             .texture = tex,
                             .shader = SHADER_UI,
                             .alphamode = uv->alphamode,
                             .screen = *screen,
                             .uv = MAKE(RECT, uv->l, uv->t, uv->r - uv->l, uv->b - uv->t),
                             .color = color));
    } else {
        RECT const uv = get_uvrect(frame->tex.coord);
        RECT const suv = Rect_div(&uv, 0xff);
        re.DrawImage(tex, screen, &suv, frame->color);
    }
}

static void SCR_LayoutDrawHighlightData(uiHighlight_t const *h, LPCRECT screen) {
    LPCTEXTURE texture;
    if (!h || !screen || !(texture = SCR_LayoutPic(h->alphaFile))) return;
    re.DrawImageEx(&MAKE(drawImage_t,
        .texture   = texture,
        .alphamode = h->alphaMode,
        .screen    = *screen,
        .uv        = MAKE(RECT,0,0,1,1),
        .color     = COLOR32_WHITE,
        .shader    = SHADER_UI));
}

void SCR_LayoutDrawHighlight(LPCUIFRAME frame, LPCRECT screen) {
    SCR_LayoutDrawHighlightData(frame->buffer.data, screen);
}

static BOOL SCR_LayoutFrameIsHovered(LPCUIFRAME frame);

void SCR_LayoutSimpleButton(LPCUIFRAME frame, LPCRECT screen) {
    uiSimpleButton_t const *b = frame->buffer.data;
    BOOL const enabled = SCR_LayoutFrameHasClickCommand(frame);
    BOOL const hovered = SCR_LayoutFrameIsHovered(frame);
    BOOL const pushed = enabled && hovered && layout_left_down;
    uiSimpleButtonState_t const *state = !enabled ? &b->disabled : pushed ? &b->pushed : &b->normal;
    LPCTEXTURE texture = SCR_LayoutPic(state->texture);
    if (!texture) {
        state = &b->normal;
        texture = SCR_LayoutPic(state->texture);
    }
    if (texture) {
        RECT const uv = get_uvrect((BYTE *)&state->texcoord);
        RECT const suv = Rect_div(&uv, 0xff);
        re.DrawImage(texture, screen, &suv, COLOR32_WHITE);
    }
    re.DrawText(&MAKE(drawText_t,
        .rect      = *screen,
        .font      = cl.fonts[state->font],
        .text      = frame->text,
        .color     = state->fontcolor,
        .textWidth = screen->w));
}

void SCR_LayoutDrawBackdrop2(LPCUIFRAME frame, LPCRECT screen, uiBackdrop_t const *bd) {
    LPCTEXTURE background, edge;
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

void SCR_LayoutDrawBackdrop(LPCUIFRAME frame, LPCRECT screen) {
    SCR_LayoutDrawBackdrop2(frame, screen, frame->buffer.data);
}

static BOOL SCR_LayoutBackdropHasArt(uiBackdrop_t const *bd) {
    return bd && (bd->Background || bd->EdgeFile);
}

static void SCR_LayoutDrawBackdropPart(LPCUIFRAME frame, LPCRECT screen, uiBackdrop_t const *bd) {
    if (SCR_LayoutBackdropHasArt(bd) && screen->w > 0 && screen->h > 0)
        SCR_LayoutDrawBackdrop2(frame, screen, bd);
}

/* WoW sliders use compact cropped textures; retain backdrop drawing for legacy FDF scrollbars. */
static BOOL SCR_LayoutDrawScrollImage(RESOURCE texture, BYTE const *texcoord, LPCRECT screen) {
    RECT uv, suv;
    LPCTEXTURE image = SCR_LayoutPic(texture);
    if (!image) return false;
    uv = get_uvrect(texcoord); suv = Rect_div(&uv, 0xff);
    re.DrawImage(image, screen, &suv, COLOR32_WHITE);
    return true;
}


static LPCUIFRAME SCR_LayoutTextAreaScrollBar(LPCUIFRAME frame) {
    if (!frame || !frame->number || frame->flags.type != FT_TEXTAREA) return NULL;
    FOR_LOOP(i, SCR_NumFrames()) {
        LPCUIFRAME child = SCR_Frame(i);
        if (child && child->parent == frame->number && child->flags.type == FT_SCROLLBAR)
            return child;
    }
    return NULL;
}

static RECT SCR_LayoutTextAreaView(LPCUIFRAME frame, LPCRECT screen) {
    uiTextArea_t const *ta = frame && frame->buffer.data ? frame->buffer.data : NULL;
    FLOAT inset = ta ? ta->inset : 0.0f;
    RECT view = { screen->x + inset, screen->y + inset,
                  MAX(0.0f, screen->w - inset * 2), MAX(0.0f, screen->h - inset * 2) };
    LPCUIFRAME scrollbar = SCR_LayoutTextAreaScrollBar(frame);
    if (scrollbar) {
        FLOAT sw = SCR_LayoutRect(scrollbar)->w;
        if (sw > 0.0f && sw < view.w) view.w = MAX(0.0f, view.w - sw);
    }
    return view;
}

FLOAT SCR_LayoutTextAreaMaxScroll(LPCUIFRAME frame) {
    uiTextArea_t const *ta;
    RECT view;
    drawText_t measure;
    LPCSTR value;

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

void SCR_LayoutDrawScrollBar(LPCUIFRAME frame, LPCRECT screen) {
    uiScrollBarImage_t const *art = frame->buffer.size == sizeof(*art) ? frame->buffer.data : NULL;
    uiScrollBar_t const *sb = !art && frame->buffer.size >= sizeof(*sb) ? frame->buffer.data : NULL;
    LPCUIFRAME parent = frame->parent < SCR_NumFrames() ? SCR_Frame(frame->parent) : NULL;
    if ((!art && !sb) || screen->w <= 0 || screen->h <= 0) return;

    if (parent && parent->flags.type == FT_TEXTAREA) {
        if (SCR_LayoutTextAreaMaxScroll(parent) <= 0.0f) return;
        ((LPUIFRAME)frame)->value = parent->value;
    }

    if (sb) SCR_LayoutDrawBackdropPart(frame, screen, &sb->background);

    /* Equal authored pixel dimensions need a taller Y span in WoW's normalized square UI scene. */
    FLOAT bh = MIN(screen->w * UI_PIXEL_ASPECT, screen->h * 0.5f);
    RECT inc   = MAKE(RECT, screen->x, screen->y + screen->h - bh, screen->w, bh);
    RECT dec   = MAKE(RECT, screen->x, screen->y, screen->w, bh);
    RECT track = MAKE(RECT, screen->x, dec.y + dec.h, screen->w, inc.y - (dec.y + dec.h));
    if (art) {
        SCR_LayoutDrawScrollImage(art->image[0], art->texcoord, &inc);
        SCR_LayoutDrawScrollImage(art->image[1], art->texcoord, &dec);
    } else {
        SCR_LayoutDrawBackdropPart(frame, &inc, &sb->incButton);
        SCR_LayoutDrawBackdropPart(frame, &dec, &sb->decButton);
    }
    if (track.h <= 0) return;

#ifdef UI_STRETCHED_SCROLLBAR_THUMB
    FLOAT th = MIN(MAX(bh, track.h * 0.25f), track.h);
#else
    FLOAT th = MIN(art ? bh : MIN(bh, 0.010f), track.h);
#endif
    FLOAT tw = art ? screen->w : MIN(screen->w, 0.010f);
    RECT thumb = {
        screen->x + (screen->w - tw) * 0.5f,
        /* Slider value zero is the top of a top-origin UI; the old formula inverted it. */
        track.y + (track.h - th) * MIN(MAX(frame->value, 0.0f), 1.0f),
        tw, th
    };
    if (art) SCR_LayoutDrawScrollImage(art->image[2], art->texcoord, &thumb);
    else SCR_LayoutDrawBackdropPart(frame, &thumb, &sb->thumbButton);
}

BOOL SCR_LayoutFrameHasClickCommand(LPCUIFRAME frame) {
    return frame && frame->onclick && *frame->onclick;
}
static BOOL SCR_LayoutGlueTextButtonIsPushed(LPCUIFRAME frame) {
    /* The left button is global, but the pushed state belongs only to the hovered layout frame. */
    return layout_left_down && SCR_LayoutFrameHasClickCommand(frame) && SCR_LayoutFrameIsHovered(frame);
}
static BOOL SCR_LayoutFrameIsHovered(LPCUIFRAME frame) {
    if (!frame || frame->number != layout_hovered_number) return false;
    /* Transient windows identify the hovered layout by handle. Persistent HUD
     * layers identify it by layer number; layout_current is also used while
     * drawing those layers, so it must not make ordinary HUD hover look like a
     * window hover. */
    if (layout_hovered)
        return layout_current_window && layout_current == layout_hovered;
    return !layout_current_window && layout_current_layer == layout_hovered_layer;
}

static void SCR_LayoutFormatOnClickCommand(LPCSTR src, LPSTR dst, DWORD dsz) {
    if (!dst || dsz == 0) return;
    dst[0] = '\0';
    if (!src) return;

    DWORD out = 0;
    for (DWORD i = 0; src[i] && out + 1 < dsz; i++) {
        if (src[i] == '{') {
            char name[80]; DWORD nlen = 0;
            DWORD j = i + 1;
            while (src[j] && src[j] != '}' && nlen + 1 < sizeof(name))
                name[nlen++] = src[j++];
            if (src[j] == '}') {
                char val[16];
                name[nlen] = '\0';
                snprintf(val, sizeof(val), "%d", 0);
                for (DWORD k = 0; val[k] && out + 1 < dsz; k++)
                    dst[out++] = val[k];
                i = j;
                continue;
            }
        }
        dst[out++] = src[i];
    }
    dst[out] = '\0';
}

void SCR_LayoutSendFrameCommand(LPCUIFRAME frame) {
    char command[CMDARG_LEN * 2];
    if (!SCR_LayoutFrameHasClickCommand(frame)) return;
    SCR_LayoutFormatOnClickCommand(frame->onclick, command, sizeof(command));
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s", command);
}

void SCR_LayoutSetPointer(HANDLE layout, DWORD number, BOOL down) {
    layout_hovered = layout;
    layout_hovered_number = number;
    layout_left_down = down;
}

void SCR_WindowPrepare(HANDLE layout, LPCRECT root) {
    layout_current_window = true;
    SCR_ClearWindow(layout);
    if (root) SCR_SetLayoutRoot(root);
}

static void SCR_LayoutCheckBox(LPCUIFRAME frame, LPCRECT screen) {
    uiCheckBox_t const *cb = frame->buffer.data;
    BOOL const enabled = SCR_LayoutFrameHasClickCommand(frame);
    BOOL const pushed = enabled && SCR_LayoutFrameIsHovered(frame) && layout_left_down;
    uiBackdrop_t const *bd = enabled
        ? (pushed ? &cb->pushed : &cb->normal)
        : (pushed ? &cb->disabledPushed : &cb->disabled);

    SCR_LayoutDrawBackdrop2(frame, screen, bd);
    if (frame->value >= 0.5f)
        SCR_LayoutDrawHighlightData(enabled ? &cb->checked : &cb->disabledChecked, screen);
    if (enabled && SCR_LayoutFrameIsHovered(frame))
        SCR_LayoutDrawHighlightData(&cb->mouseOver, screen);
}

void SCR_LayoutGlueTextButton(LPCUIFRAME frame, LPCRECT screen) {
    uiGlueTextButton_t const *gb = frame->buffer.data;
    BOOL const enabled = SCR_LayoutFrameHasClickCommand(frame);
    BOOL const pushed  = SCR_LayoutGlueTextButtonIsPushed(frame);
    uiBackdrop_t const *bd = enabled
        ? (pushed ? &gb->pushed : &gb->normal)
        : (pushed ? &gb->disabledPushed : &gb->disabled);
    SCR_LayoutDrawBackdrop2(frame, screen, bd);
}

static void SCR_LayoutDrawGlueTextButtonHighlight(LPCUIFRAME frame) {
    uiGlueTextButton_t const *gb = frame->buffer.data;
    if (SCR_LayoutFrameHasClickCommand(frame) && SCR_LayoutFrameIsHovered(frame))
        SCR_LayoutDrawHighlightData(&gb->highlight, SCR_LayoutRect(frame));
}

BOOL SCR_LayoutScrollTextAreaAt(HANDLE layout, LPCVECTOR2 point, int wheel_y) {
    (void)layout;
    if (!point || !wheel_y) return false;
    for (DWORD i = SCR_NumFrames(); i > 0; i--) {
        LPUIFRAME frame = SCR_Frame(i - 1);
        if (!frame || frame->flags.type != FT_TEXTAREA ||
            !Rect_contains(SCR_LayoutRect(frame), point)) continue;
        if (SCR_LayoutTextAreaMaxScroll(frame) > 0.0f)
            frame->value = MIN(1.0f, MAX(0.0f, frame->value - wheel_y * 0.1f));
        return true;
    }
    return false;
}

void SCR_LayoutDrawBuildQueue(LPCUIFRAME frame, LPCRECT scrn) {
    RECT screen = *scrn;
    RECT const uv = { 0, 0, 1, 1 };
    uiBuildQueue_t const *queue = frame->buffer.data;
    BOOL const food_blocked = queue->numitems && queue->items[0].starttime == 0 && queue->items[0].endtime == 0;
    DWORD active = food_blocked ? 0 : queue->numitems;

    if (!food_blocked) {
        FOR_LOOP(i, queue->numitems) {
            if (cl.time < queue->items[i].endtime) { active = i; break; }
        }
    }
    for (DWORD i = active + 1; i < queue->numitems; i++) {
        if (food_blocked || cl.time < queue->items[i].endtime) {
            re.DrawImage(cl.pics[queue->items[i].image], &screen, &uv, frame->color);
            screen.x += queue->itemoffset;
        }
    }
}

static void SCR_LayoutDrawMessageQueue(LPCUIFRAME frame, LPCRECT screen) {
    uiMessageQueue_t const *message = frame->buffer.data;
    RECT uv = MAKE(RECT, 0, 0, 0.53125f, 0.6875f);
    if (!message || frame->buffer.size < sizeof(*message)) {
        fprintf(stderr, "SCR_LayoutDrawMessageQueue: invalid payload size %u\n", (unsigned)frame->buffer.size);
        return;
    }
    if (message->flags & UI_MESSAGE_UNREAD) {
        LPCTEXTURE icon = SCR_LayoutPic(message->image);
        if (icon) re.DrawImage(icon, screen, &uv, COLOR32_WHITE);
    }
    if (message->flags & UI_MESSAGE_OPEN) {
        RECT title = MAKE(RECT, screen->x + 0.04f, screen->y + 0.02f, screen->w - 0.08f, 0.04f);
        RECT body = MAKE(RECT, screen->x + 0.06f, screen->y + 0.07f, screen->w - 0.12f, screen->h - 0.09f);
        re.DrawFill(screen, MAKE(COLOR32, 10, 8, 5, 245));
        re.DrawText(&MAKE(drawText_t, .font = cl.fonts[message->title_font], .text = frame->text,
                          .rect = title, .color = MAKE(COLOR32, 255, 215, 120, 255),
                          .textWidth = title.w, .halign = FONT_JUSTIFYCENTER, .valign = FONT_JUSTIFYMIDDLE));
        re.DrawText(&MAKE(drawText_t, .font = cl.fonts[message->body_font], .text = frame->tooltip,
                          .rect = body, .color = MAKE(COLOR32, 240, 230, 205, 255),
                          .textWidth = body.w, .lineHeight = body.h, .flags = DRAW_WORD_WRAP,
                          .halign = FONT_JUSTIFYLEFT, .valign = FONT_JUSTIFYTOP));
    }
}

void SCR_LayoutUpdateBuildQueue(LPCUIFRAME frame, LPCRECT screen) {
    uiBuildQueue_t const *queue = frame->buffer.data;
    LPUIFRAME buildtimer = SCR_Frame(queue->buildtimer);
    LPUIFRAME firstitem  = SCR_Frame(queue->firstitem);

    if (queue->numitems && queue->items[0].starttime == 0 && queue->items[0].endtime == 0) {
        if (buildtimer) buildtimer->value = 0;
        if (firstitem) firstitem->tex.index = queue->items[0].image;
        return;
    }
    FOR_LOOP(i, queue->numitems) {
        uiBuildQueueItem_t const *item = &queue->items[i];
        if (cl.time < item->endtime) {
            FLOAT dur  = item->endtime - item->starttime;
            FLOAT elap = cl.time > item->starttime ? (FLOAT)(cl.time - item->starttime) : 0;
            FLOAT prog = MAX(0, MIN(dur > 0 ? elap / dur : 1, 1));
            if (buildtimer) buildtimer->value  = prog;
            if (firstitem)  firstitem->tex.index = item->image;
            break;
        }
    }
    (void)screen;
}

#define HP_BAR_HEIGHT_RATIO  0.175f
#define HP_BAR_SPACING_RATIO 0.02f

static DWORD SCR_LayoutMultiselectEntityAt(LPCUIFRAME frame, LPCVECTOR2 point) {
    uiMultiselect_t const *ms;
    DWORD count;

    if (!frame || frame->flags.type != FT_MULTISELECT || !point ||
        !frame->buffer.data || frame->buffer.size < sizeof(uiMultiselect_t)) {
        return 0;
    }
    ms = frame->buffer.data;
    if (!ms->numcolumns) return 0;
    count = (frame->buffer.size - sizeof(uiMultiselect_t)) / sizeof(uiMultiselectItem_t);
    count = MIN((DWORD)ms->numitems, count);
    FOR_LOOP(i, count) {
        RECT cell = *SCR_LayoutRect(frame);
        DWORD column = i % ms->numcolumns;
        DWORD row = i / ms->numcolumns;

        cell.x += ms->offset.x * column;
        cell.y += ms->offset.y * row;
        if (Rect_contains(&cell, point)) return ms->items[i].entity;
    }
    return 0;
}

void SCR_LayoutDrawMultiSelect(LPCUIFRAME frame, LPCRECT scrn) {
    RECT screen = *scrn;
    uiMultiselect_t const *ms = frame->buffer.data;
    DWORD column = 0;
    FOR_LOOP(i, ms->numitems) {
        RECT uv = { 0, 0, 1, 1 };
        uiMultiselectItem_t const *item = &ms->items[i];
        re.DrawImage(cl.pics[item->image], &screen, &uv, frame->color);
        LPCENTITYSTATE ent = &cl.ents[item->entity].current;
        if (ent) {
            FLOAT hp   = BYTE2FLOAT(ent->stats[ENT_HEALTH]);
            FLOAT mana = BYTE2FLOAT(ent->stats[ENT_MANA]);
            RECT rect  = { screen.x, screen.y + screen.h * (1 + HP_BAR_SPACING_RATIO),
                           screen.w * hp, screen.h * HP_BAR_HEIGHT_RATIO };
            uv.w = hp;
            re.DrawImage(cl.pics[ms->hp_bar],   &rect, &uv, MAKE(COLOR32,0,255,0,255));
            uv.w  = mana; rect.w  = screen.w * mana;
            rect.y += screen.h * (HP_BAR_HEIGHT_RATIO + HP_BAR_SPACING_RATIO);
            re.DrawImage(cl.pics[ms->mana_bar], &rect, &uv, MAKE(COLOR32,0,255,255,255));
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

void SCR_LayoutDrawPortrait(LPCUIFRAME frame, LPCRECT screen) {
    FLOAT const canvas_w = SCR_UICanvasWidth();
    RECT const viewport = {
        screen->x / canvas_w,
        (UI_BASE_HEIGHT - screen->y - screen->h) / UI_BASE_HEIGHT,
        screen->w / canvas_w,
        screen->h / UI_BASE_HEIGHT
    };
    LPCMODEL port  = cl.portraits[frame->tex.index];
    LPCMODEL model = cl.models[frame->tex.index];
    LPCMODEL draw  = port ? port : model;
    if (!draw) return;

    LPCSTR anim = (frame->text && *frame->text) ? frame->text : "Portrait";

    renderEntity_t entity = {0};
    entity.model = draw; entity.scale = 1.0f;
    entity.flags = RF_NO_SHADOW | RF_NO_FOGOFWAR | RF_PORTRAIT_LIGHTING;
    re.SetEntityAnimFrame(draw, anim, &entity);

    viewDef_t vd = {0};
    vd.viewport     = viewport;
    vd.scissor      = viewport;
    vd.rdflags      = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG | RDF_USE_ENTITY_CAMERA;
    vd.num_entities = 1;
    vd.entities     = &entity;
    if (frame->buffer.size == sizeof(UIMODEL)) {
        size2_t size = re.GetWindowSize();
        FLOAT aspect = viewport.w * size.width / (viewport.h * size.height);
        /* The layout camera replaces the old radius guess and Stand-name mode switch. */
        UI_ModelMatrix(frame->buffer.data, aspect, &vd.viewProjectionMatrix);
        Matrix4_identity(&vd.textureMatrix);
        Matrix4_identity(&vd.lightMatrix);
        vd.rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG | RDF_NOPARTICLES;
    }
    re.RenderFrame(&vd);
}

void SCR_LayoutDrawSprite(LPCUIFRAME frame, LPCRECT screen) {
    LPCMODEL model = cl.models[frame->tex.index];
    LPCSTR anim    = (frame->text && *frame->text) ? frame->text : "Stand";
    re.DrawSprite(model, anim, screen->x, screen->y);
}

void SCR_LayoutDrawCommandButton(LPCUIFRAME frame, LPCRECT screen) {
    LPCENTITYSTATE sel = SCR_LayoutSelectedEntity();
    RECT const uv = get_uvrect(frame->tex.coord);
    RECT const suv = Rect_div(&uv, 0xff);
    RECT scrn = scale_rect(screen, SCR_LayoutFrameIsHovered(frame) && layout_left_down ? 0.875f : 0.925f);
    re.DrawImageEx(&MAKE(drawImage_t,
        .texture     = cl.pics[frame->tex.index],
        .screen      = scrn,
        .uv          = suv,
        .color       = COLOR32_WHITE,
        .shader      = SHADER_COMMANDBUTTON,
        .uActiveGlow = (frame->flagsvalue & UIFLAG_ALTERNATE_ACTIVE) ||
                       (sel && sel->ability == frame->stat)));
    (void)frame;
}

void layout_text(LPCUIFRAME frame, LPCRECT screen, LPCSTR text) {
    drawText_t dt = SCR_GetDrawText(frame, screen->w, text, frame->buffer.data);
    dt.rect   = *screen;
    dt.flags |= DRAW_WORD_WRAP;
    re.DrawText(&dt);
}

static void SCR_LayoutApplyPushedTextOffset(LPCUIFRAME frame, LPRECT screen) {
    if (frame->parent >= SCR_NumFrames()) return;
    LPCUIFRAME parent = SCR_Frame(frame->parent);
    if (!parent) return;
    if (parent->flags.type != FT_GLUETEXTBUTTON && parent->flags.type != FT_GLUEBUTTON) return;
    if (!SCR_LayoutFrameHasClickCommand(parent) || !SCR_LayoutGlueTextButtonIsPushed(parent)) return;
    uiGlueTextButton_t const *b = parent->buffer.data;
    screen->x += b->pushedTextOffset.x;
    screen->y -= b->pushedTextOffset.y;
}

void SCR_LayoutDrawString(LPCUIFRAME frame, LPCRECT screen) {
    uiLabel_t const *label = frame->buffer.data;
    /* Label offsets use the same fixed-point wire representation as anchors. */
    RECT scr = { screen->x + label->offsetx / UI_FRAMEPOINT_SCALE,
                 screen->y + label->offsety / UI_FRAMEPOINT_SCALE, screen->w, screen->h };
    SCR_LayoutApplyPushedTextOffset(frame, &scr);
    layout_text(frame, &scr, SCR_GetStringValue(frame));
}

/* Draw a nameplate whose backdrop and text share the measured content rect. */
void SCR_LayoutDrawNameTag(LPCUIFRAME frame, LPCRECT screen) {
    uiNameTag_t const *tag = frame->buffer.data;
    RECT text = { screen->x + tag->padding_x, screen->y + tag->padding_y,
                  screen->w - tag->padding_x * 2, screen->h - tag->padding_y * 2 };
    drawText_t dt = SCR_GetDrawText(frame, text.w, SCR_GetStringValue(frame), &tag->text);
    SCR_LayoutDrawBackdrop2(frame, screen, &tag->background);
    dt.rect = text; dt.flags |= DRAW_WORD_WRAP; re.DrawText(&dt);
}

void SCR_LayoutDrawTextArea(LPCUIFRAME frame, LPCRECT screen) {
    uiTextArea_t const *ta = frame->buffer.data;
    LPCSTR value = SCR_GetStringValue(frame);
    RECT view = SCR_LayoutTextAreaView(frame, screen);
    drawText_t dt = SCR_GetDrawText(frame, view.w, value ? value : "", &(uiLabel_t){
        .font = ta->font, .textalignx = FONT_JUSTIFYLEFT, .textaligny = FONT_JUSTIFYTOP });
    FLOAT full_height = view.h;
    FLOAT max_scroll = 0.0f;

    dt.flags |= DRAW_WORD_WRAP;
    if (re.GetTextSize && view.w > 0.0f && view.h > 0.0f) {
        full_height = MAX(view.h, re.GetTextSize(&dt).y);
        max_scroll = MAX(0.0f, full_height - view.h);
    }
    dt.rect = MAKE(RECT, view.x,
                   view.y - max_scroll * MIN(MAX(frame->value, 0.0f), 1.0f),
                   view.w, full_height);
    dt.flags |= DRAW_CLIP;
    dt.clip = view;
    re.DrawText(&dt);
}

void SCR_LayoutDrawListBox(LPCUIFRAME frame, LPCRECT screen) {
    uiListBox_t const *lb = frame->buffer.data;
    RECT list_rect = Rect_inset(screen, lb->border);
    FLOAT item_height = lb->itemHeight > 0 ? lb->itemHeight : 0.018f;
    SHORT selectedIndex = lb->selectedIndex;
    DWORD scrollOffset = 0, numRows = 0;
    char items[MAX_LISTBOX_TEXT];

    SCR_LayoutDrawBackdrop2(frame, screen, &lb->background);

    LPCUIFRAME scrollbar = NULL;
    FOR_LOOP(i, SCR_NumFrames()) {
        LPCUIFRAME child = SCR_Frame(i);
        if (child && child->parent == frame->number && child->flags.type == FT_SCROLLBAR) {
            scrollbar = child; break;
        }
    }
    if (scrollbar) {
        FLOAT sw = MAX(SCR_LayoutRect(scrollbar)->w, 0.0f);
        if (sw > 0 && sw < list_rect.w) list_rect.w -= sw;
    }
    DWORD visibleRows = MAX((DWORD)floorf(list_rect.h / item_height), 1);
    if (scrollbar) {
        DWORD maxScroll = numRows > visibleRows ? numRows - visibleRows : 0;
        ((LPUIFRAME)scrollbar)->value = maxScroll ? scrollOffset / (FLOAT)maxScroll : 0.0f;
    }
    if (!frame->text || !*frame->text) return;

    snprintf(items, sizeof(items), "%s", frame->text);
    char *save = NULL;
    char *line = strtok_r(items, "\n", &save);
    int index  = 0;
    while (line && index < (int)scrollOffset) { line = strtok_r(NULL, "\n", &save); index++; }

    FLOAT item_y = list_rect.y + list_rect.h;
    while (line && item_y > list_rect.y) {
        char *hidden = strchr(line, '\t');
        if (hidden) *hidden = '\0';
        RECT row = { list_rect.x, 0, list_rect.w, MIN(item_height, item_y - list_rect.y) };
        row.y = item_y - row.h;
        if (index == selectedIndex)
            re.DrawImage(cl.pics[0], &row, &MAKE(RECT,0,0,1,1), MAKE(COLOR32,32,64,180,128));
        re.DrawText(&MAKE(drawText_t,
            .font       = cl.fonts[lb->text.font],
            .text       = line,
            .color      = frame->color.a ? frame->color : COLOR32_WHITE,
            .halign     = FONT_JUSTIFYLEFT,
            .valign     = FONT_JUSTIFYMIDDLE,
            .icons      = cl.pics,
            .lineHeight = 1.33,
            .textWidth  = row.w,
            .rect       = row));
        item_y -= item_height;
        line = strtok_r(NULL, "\n", &save);
        index++;
    }
}

void SCR_LayoutDrawTooltip(LPCUIFRAME frame, LPCRECT scrn) {
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
    FLOAT const PAD = 0.005f;
    RECT screen = *scrn;
    drawText_t dt = SCR_GetDrawText(frame, screen.w - PAD*2, active_tooltip, &tt->text);
    dt.flags |= DRAW_WORD_WRAP;
    VECTOR2 tsz = re.GetTextSize(&dt);
    tsz.y    += PAD * 2;
    screen.y += screen.h - tsz.y;
    screen.h  = tsz.y;
    RECT text  = Rect_inset(&screen, PAD);
    SCR_LayoutDrawBackdrop(frame, &screen);
    dt = SCR_GetDrawText(frame, text.w, active_tooltip, &tt->text);
    dt.rect   = text;
    dt.flags |= DRAW_WORD_WRAP;
    re.DrawText(&dt);
}

typedef struct { FRAMETYPE type; void (*func)(LPCUIFRAME, LPCRECT); } drawer_t;

static drawer_t updaters[] = {
    { FT_BUILDQUEUE, SCR_LayoutUpdateBuildQueue },
};

static drawer_t drawers[] = {
    { FT_TEXTURE,        SCR_LayoutDrawTexture },
    { FT_HIGHLIGHT,      SCR_LayoutDrawHighlight },
    { FT_BACKDROP,       SCR_LayoutDrawBackdrop },
    { FT_SIMPLESTATUSBAR,SCR_LayoutDrawStatusbar },
    { FT_COMMANDBUTTON,  SCR_LayoutDrawCommandButton },
    { FT_STRING,         SCR_LayoutDrawString },
    { FT_NAMETAG,        SCR_LayoutDrawNameTag },
    { FT_TEXT,           SCR_LayoutDrawString },
    { FT_TEXTAREA,       SCR_LayoutDrawTextArea },
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

void SCR_LayoutDrawFrame(LPCUIFRAME frame) {
    RECT const *screen = SCR_LayoutRect(frame);
    FOR_LOOP(j, sizeof(drawers)/sizeof(*drawers)) {
        if (drawers[j].type == frame->flags.type) {
            drawers[j].func(frame, screen);
            break;
        }
    }
}

void SCR_LayoutUpdateFrame(LPCUIFRAME frame) {
    RECT const *screen = SCR_LayoutRect(frame);

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

static void SCR_LayoutRunFrames(HANDLE layout, void (*fn)(LPCUIFRAME)) {
    FOR_LOOP(i, SCR_NumFrames()) {
        LPCUIFRAME frame = SCR_Frame(i);
        if (frame) fn(frame);
    }
}

void SCR_LayoutUpdateTooltip(HANDLE layout) {
    SCR_LayoutRunFrames(layout, SCR_LayoutUpdateFrame);
}

void SCR_LayoutDrawOverlay(HANDLE layout) {
    layout_current = layout;
    FOR_LOOP(i, SCR_NumFrames()) {
        LPCUIFRAME f = SCR_Frame(i);
        if (f && f->flags.type == FT_SPRITE) SCR_LayoutDrawFrame(f);
    }
    FOR_LOOP(i, SCR_NumFrames()) {
        LPCUIFRAME f = SCR_Frame(i);
        if (f && f->flags.type != FT_SPRITE) SCR_LayoutDrawFrame(f);
    }
    FOR_LOOP(i, SCR_NumFrames()) {
        LPCUIFRAME f = SCR_Frame(i);
        if (f && (f->flags.type == FT_GLUETEXTBUTTON || f->flags.type == FT_GLUEBUTTON))
            SCR_LayoutDrawGlueTextButtonHighlight(f);
    }
}

void SCR_DrawLayout(void) {
    active_tooltip = NULL;

    if (cl.playerstate.cinefade > 0) {
        COLOR32 color = COLOR32_BLACK;
        color.a = 255 * cl.playerstate.cinefade;
        re.DrawImage(cl.pics[0], &MAKE(RECT,0,0,1,1), &MAKE(RECT,0,0,1,1), color);
    }

    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        DWORD flags = cl.playerstate.uiflags;
        if ((1 << layer) & flags) continue;
        HANDLE layout = layout_layers[layer];
        if (layout) {
            RECT root;
            layout_current_window = false;
            layout_current_layer = layer;
            SCR_Clear(layout);
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

void SCR_SetLayoutLayer(DWORD layer, HANDLE data) {
    if (layer < MAX_LAYOUT_LAYERS) layout_layers[layer] = data;
}

void SCR_ClearLayoutLayer(DWORD layer) {
    if (layer < MAX_LAYOUT_LAYERS) layout_layers[layer] = NULL;
}

static BOOL SCR_LayoutLayerVisible(DWORD layer) { return layer < MAX_LAYOUT_LAYERS && layout_layers[layer] && !((1u << layer) & cl.playerstate.uiflags); }

/* Result screens outrank Quest when malformed/server-overlapping modal layers coexist. */
static int SCR_LayoutModalLayer(void) {
    if (SCR_LayoutLayerVisible(LAYER_GAME_RESULT)) return LAYER_GAME_RESULT;
    if (SCR_LayoutLayerVisible(LAYER_QUESTDIALOG)) return LAYER_QUESTDIALOG;
    return -1;
}

BOOL SCR_LayoutModalActive(void) { return SCR_LayoutModalLayer() >= 0; }

BOOL SCR_LayoutMouseEvent(uiMouseEvent_t event, int x, int y, int32_t param) {
    VECTOR2 const point = SCR_ScreenToUI(x, y);
    LPCUIFRAME hovered_frame = NULL;
    int const modal_layer = SCR_LayoutModalLayer();
    /* This path handles persistent layout layers, not client-managed windows. */
    layout_current_window = false;
    layout_hovered = NULL;
    layout_hovered_number = 0;
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        HANDLE layout = layout_layers[layer];
        if (modal_layer >= 0 && (int)layer != modal_layer) continue;
        DWORD flags = cl.playerstate.uiflags;
        if (!layout || layer == LAYER_WORLD_HOVER || (1 << layer) & flags) continue;
        SCR_Clear(layout);
        for (DWORD i = SCR_NumFrames(); i > 0; i--) {
            LPCUIFRAME frame = SCR_Frame(i - 1);
            BOOL hit;

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
        if (event == UI_MOUSE_DOWN) return true;
        if (event != UI_MOUSE_UP) return false;
        MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
        SZ_Printf(&cls.netchan.message, "%s", hovered_frame->text);
        return true;
    }

    if (param != SDL_BUTTON_LEFT) return false;
    if (event == UI_MOUSE_DOWN) {
        char command[CMDARG_LEN * 2];
        layout_left_down = true;
        if (!hovered_frame || layout_held_command[0]) return false;
        SCR_LayoutFormatOnClickCommand(hovered_frame->onclick, command, sizeof(command));
        if (command[0] != '+') return false;
        strlcpy(layout_held_command, command, sizeof(layout_held_command));
        MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
        SZ_Printf(&cls.netchan.message, "%s", layout_held_command);
        return false;
    }
    if (event != UI_MOUSE_UP) return false;
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
        HANDLE layout = layout_layers[layer];
        DWORD flags = cl.playerstate.uiflags;
        if (modal_layer >= 0 && (int)layer != modal_layer) continue;
        if (!layout || layer == LAYER_WORLD_HOVER || (1 << layer) & flags) continue;
        SCR_Clear(layout);
        for (DWORD i = SCR_NumFrames(); i > 0; i--) {
            LPCUIFRAME frame = SCR_Frame(i - 1);

            if (!frame) continue;
            if (frame->flags.type == FT_MULTISELECT) {
                DWORD entity = SCR_LayoutMultiselectEntityAt(frame, &point);
                if (entity) {
                    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
                    SZ_Printf(&cls.netchan.message, "focus %u", (unsigned)entity);
                    return false;
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
BOOL SCR_LayoutKeyEvent(int key) {
    int const upper = toupper(key);
    int const modal_layer = SCR_LayoutModalLayer();

    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        HANDLE layout = layout_layers[layer];
        if (modal_layer >= 0 && (int)layer != modal_layer) continue;
        DWORD flags = cl.playerstate.uiflags;
        if (!layout || layer == LAYER_WORLD_HOVER || (1 << layer) & flags) continue;
        SCR_Clear(layout);
        for (DWORD i = SCR_NumFrames(); i > 0; i--) {
            LPCUIFRAME frame = SCR_Frame(i - 1);
            if (!frame || !SCR_LayoutFrameHasClickCommand(frame)) continue;
            BOOL const is_cancel = key == K_ESCAPE && !strcmp(frame->onclick, "button CmdCancel");
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

static BOOL SCR_LayoutSelectionBlockerType(FRAMETYPE type) {
    switch (type) {
        case FT_TEXTURE:
        case FT_BACKDROP:
        case FT_SIMPLESTATUSBAR:
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

static BOOL SCR_RangesOverlap(FLOAT a0, FLOAT a1, FLOAT b0, FLOAT b1) {
    return MAX(a0, b0) < MIN(a1, b1);
}

/* The WC3 command console is not a flat rectangle: the info/status and command
 * panels protrude above the world viewport's bottom edge.  Selection starts in
 * the world, so constrain its moving corner against visible bottom-console
 * frames before the renderer sees the marquee.  This keeps selection geometry
 * and selection hit-testing out of authored HUD regions instead of relying on
 * transparent console art to hide the line.  Full-screen world games have a
 * scissor bottom at UI_BASE_HEIGHT, making this a no-op. */
void SCR_LayoutClampSelectionRect(LPRECT rect) {
    VECTOR2 start, finish, clamped;
    FLOAT world_bottom;
    FLOAT xmin, xmax;
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
        HANDLE layout = layout_layers[layer];
        DWORD flags = cl.playerstate.uiflags;
        if (!layout || layer == LAYER_WORLD_HOVER || ((1 << layer) & flags)) continue;
        SCR_Clear(layout);
        FOR_LOOP(i, SCR_NumFrames()) {
            LPCUIFRAME frame = SCR_Frame(i);
            LPCRECT r;
            FLOAT bottom;
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

BOOL SCR_LayoutHitTest(int x, int y) {
    VECTOR2 const point = SCR_ScreenToUI(x, y);
    if (SCR_LayoutModalActive()) return true;
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        HANDLE layout = layout_layers[layer];
        DWORD flags = cl.playerstate.uiflags;
        if (!layout || layer == LAYER_WORLD_HOVER || (1 << layer) & flags) continue;
        SCR_Clear(layout);
        FOR_LOOP(i, SCR_NumFrames()) {
            LPCUIFRAME frame = SCR_Frame(i);
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

/* --------------------------------------------------------------------------
 * Legacy unit UI response parser.
 *
 * Normal selection HUD updates are local now; this parser is retained for
 * compatibility with svc_unit_ui messages.
 * -------------------------------------------------------------------------- */

void CL_ParseUnitUI(LPSIZEBUF msg) {
    BYTE num_units = MSG_ReadByte(msg);

    if (num_units == 0 || num_units > 12) {
        if (num_units == 0 && ui.UpdateUnitUI) {
            ui.UpdateUnitUI(0, NULL);
        }
        return;
    }

    uiUnitData_t *units = (uiUnitData_t *)MemAlloc(sizeof(uiUnitData_t) * num_units);
    memset(units, 0, sizeof(uiUnitData_t) * num_units);

    for (BYTE i = 0; i < num_units; i++) {
        uiUnitData_t *unit = &units[i];
        unit->entity_num = MSG_ReadShort(msg);

        unit->num_buttons = MSG_ReadByte(msg);
        if (unit->num_buttons > MAX_COMMAND_BUTTONS) {
            unit->num_buttons = MAX_COMMAND_BUTTONS;
        }
        for (BYTE j = 0; j < unit->num_buttons; j++) {
            uiCommandButton_t *btn = &unit->buttons[j];

            strncpy(btn->art, MSG_ReadString2(msg), sizeof(btn->art) - 1);
            strncpy(btn->tooltip, MSG_ReadString2(msg), sizeof(btn->tooltip) - 1);
            strncpy(btn->ubertip, MSG_ReadString2(msg), sizeof(btn->ubertip) - 1);
            strncpy(btn->command, MSG_ReadString2(msg), sizeof(btn->command) - 1);
            btn->hotkey = MSG_ReadByte(msg);
        }

        unit->num_inventory = MSG_ReadByte(msg);
        if (unit->num_inventory > MAX_INVENTORY_SLOTS) {
            unit->num_inventory = MAX_INVENTORY_SLOTS;
        }
        for (BYTE j = 0; j < unit->num_inventory; j++) {
            uiInventoryItem_t *item = &unit->inventory[j];

            strncpy(item->art, MSG_ReadString2(msg), sizeof(item->art) - 1);
            strncpy(item->tooltip, MSG_ReadString2(msg), sizeof(item->tooltip) - 1);
            strncpy(item->ubertip, MSG_ReadString2(msg), sizeof(item->ubertip) - 1);
            item->slot = MSG_ReadByte(msg);
        }

        unit->num_queue = MSG_ReadByte(msg);
        if (unit->num_queue > MAX_BUILD_QUEUE_ITEMS) {
            unit->num_queue = MAX_BUILD_QUEUE_ITEMS;
        }
        for (BYTE j = 0; j < unit->num_queue; j++) {
            uiQueueItem_t *queue_item = &unit->queue[j];
            LPCSTR art = MSG_ReadString2(msg);

            strncpy(queue_item->art, art, sizeof(queue_item->art) - 1);
            queue_item->entity = MSG_ReadShort(msg);
        }
    }

    ui.UpdateUnitUI((DWORD)num_units, units);
    MemFree(units);
}
