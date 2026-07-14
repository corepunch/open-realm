#include "client.h"
#include "ui_layout.h"
#include <ctype.h>
#include <stdlib.h>  /* getenv (BZ_FPS_LOG diagnostic) */
#include <SDL2/SDL.h>

BOOL scr_initialized;

#define SCR_FPS_HEIGHT 8
#define SCR_FPS_BOTTOM_MARGIN 4

static void SCR_DrawString(int x, int y, LPCSTR string) {
    if (!string) {
        return;
    }
    for (DWORD i = 0; string[i]; i++) {
        re.DrawChar(x + i * 8, y, (BYTE)string[i]);
    }
}

static void SCR_DrawFPS(DWORD msec) {
    static DWORD elapsed = 0;
    static DWORD frames_drawn = 0;
    static DWORD fps = 0;
    char text[32];
    size2_t window = re.GetWindowSize();
    DWORD inset = SCR_FPS_HEIGHT + SCR_FPS_BOTTOM_MARGIN;
    DWORD y = window.height > inset ? window.height - inset : 0;

    elapsed += msec;
    frames_drawn++;
    if (elapsed >= 500) {
        fps = frames_drawn * 1000 / elapsed;
        elapsed = 0;
        frames_drawn = 0;
        if (getenv("BZ_FPS_LOG")) fprintf(stderr, "BZ_FPS %u\n", (unsigned)fps);
    } else if (!fps && msec > 0) {
        fps = 1000 / msec;
    }

    if (fps) {
        snprintf(text, sizeof(text), "FPS: %u", (unsigned)fps);
    } else {
        snprintf(text, sizeof(text), "FPS: --");
    }
    SCR_DrawString(10, y, text);
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
        SCR_DrawLayout();
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

    re.EndFrame();
}

void SCR_UpdateScreen(DWORD msec) {
    static int recursive;

    if (!scr_initialized) {
        return;
    }

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
static DWORD layout_hovered_number;

static RECT Rect_inset(LPCRECT r, FLOAT inset) {
    return MAKE(RECT, r->x+inset, r->y+inset, r->w-inset*2, r->h-inset*2);
}

static VECTOR2 SCR_LayoutScreenToFdf(int x, int y) {
    LPRENDERER renderer = &re;
    size2_t window = renderer->GetWindowSize();
    FLOAT nx = 0, ny = 0;

    if (window.width > 0 && window.height > 0) {
        nx = (FLOAT)x / (FLOAT)window.width;
        ny = (FLOAT)y / (FLOAT)window.height;
    }
    return MAKE(VECTOR2, nx * UI_BASE_WIDTH, ny * UI_BASE_HEIGHT);
}

static RECT get_uvrect(uint8_t const *tc) {
    return (RECT){ tc[0], tc[2], tc[1]-tc[0], tc[3]-tc[2] };
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
    screen2.w *= frame->value;
    uv2.w    *= frame->value;
    RECT const suv2 = Rect_div(&uv2, 0xff);
    re.DrawImage(cl.pics[frame->tex.index], &screen2, &suv2, frame->color);
    if (frame->tex.index2 > 0) {
        RECT const suv = Rect_div(&uv, 0xff);
        re.DrawImage(cl.pics[frame->tex.index2], screen, &suv, COLOR32_WHITE);
    }
}

void SCR_LayoutDrawTexture(LPCUIFRAME frame, LPCRECT screen) {
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
    if (!h || !h->alphaFile) return;
    re.DrawImageEx(&MAKE(drawImage_t,
        .texture   = cl.pics[h->alphaFile],
        .alphamode = h->alphaMode,
        .screen    = *screen,
        .uv        = MAKE(RECT,0,0,1,1),
        .color     = COLOR32_WHITE,
        .shader    = SHADER_UI));
}

void SCR_LayoutDrawHighlight(LPCUIFRAME frame, LPCRECT screen) {
    SCR_LayoutDrawHighlightData(frame->buffer.data, screen);
}

void SCR_LayoutSimpleButton(LPCUIFRAME frame, LPCRECT screen) {
    uiSimpleButton_t *b = frame->buffer.data;
    RECT const uv = get_uvrect((BYTE *)&b->normal.texcoord);
    RECT const suv = Rect_div(&uv, 0xff);
    re.DrawImage(cl.pics[b->normal.texture], screen, &suv, COLOR32_WHITE);
    re.DrawText(&MAKE(drawText_t,
        .rect      = *screen,
        .font      = cl.fonts[b->normal.font],
        .text      = frame->text,
        .color     = b->normal.fontcolor,
        .textWidth = screen->w));
}

void SCR_LayoutDrawBackdrop2(LPCUIFRAME frame, LPCRECT screen, uiBackdrop_t const *bd) {
    if (!bd || !screen || screen->w <= 0 || screen->h <= 0) return;
    if (!bd->Background && !bd->EdgeFile) return;
    re.DrawBackdrop(&MAKE(drawBackdrop_t,
        .screen        = *screen,
        .bg.texture    = cl.pics[bd->Background],
        .bg.color      = frame->color,
        .edge.texture  = cl.pics[bd->EdgeFile],
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

void SCR_LayoutDrawScrollBar(LPCUIFRAME frame, LPCRECT screen) {
    uiScrollBar_t const *sb = frame->buffer.data;
    if (!sb || screen->w <= 0 || screen->h <= 0) return;

    SCR_LayoutDrawBackdropPart(frame, screen, &sb->background);

    FLOAT bh = MIN(screen->w, screen->h * 0.5f);
    RECT inc   = MAKE(RECT, screen->x, screen->y + screen->h - bh, screen->w, bh);
    RECT dec   = MAKE(RECT, screen->x, screen->y, screen->w, bh);
    RECT track = MAKE(RECT, screen->x, dec.y + dec.h, screen->w, inc.y - (dec.y + dec.h));
    SCR_LayoutDrawBackdropPart(frame, &inc, &sb->incButton);
    SCR_LayoutDrawBackdropPart(frame, &dec, &sb->decButton);
    if (track.h <= 0) return;

#ifdef UI_STRETCHED_SCROLLBAR_THUMB
    FLOAT th = MIN(MAX(bh, track.h * 0.25f), track.h);
#else
    FLOAT th = MIN(MIN(bh, 0.010f), track.h);
#endif
    FLOAT tw = MIN(screen->w, 0.010f);
    RECT thumb = {
        screen->x + (screen->w - tw) * 0.5f,
        track.y + track.h - th - (track.h - th) * MIN(MAX(frame->value, 0.0f), 1.0f),
        tw, th
    };
    SCR_LayoutDrawBackdropPart(frame, &thumb, &sb->thumbButton);
}

static BOOL SCR_LayoutFrameHasClickCommand(LPCUIFRAME frame) {
    return frame && frame->onclick && *frame->onclick;
}
static BOOL SCR_LayoutGlueTextButtonIsPushed(LPCUIFRAME frame) {
    return layout_left_down && SCR_LayoutFrameHasClickCommand(frame);
}
static BOOL SCR_LayoutFrameIsHovered(LPCUIFRAME frame) {
    return frame && frame->number == layout_hovered_number;
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

void SCR_LayoutDrawBuildQueue(LPCUIFRAME frame, LPCRECT scrn) {
    RECT screen = *scrn;
    RECT const uv = { 0, 0, 1, 1 };
    uiBuildQueue_t const *queue = frame->buffer.data;
    DWORD active = queue->numitems;
    FOR_LOOP(i, queue->numitems) {
        if (cl.time < queue->items[i].endtime) { active = i; break; }
    }
    for (DWORD i = active + 1; i < queue->numitems; i++) {
        if (cl.time < queue->items[i].endtime) {
            re.DrawImage(cl.pics[queue->items[i].image], &screen, &uv, frame->color);
            screen.x += queue->itemoffset;
        }
    }
}

void SCR_LayoutUpdateBuildQueue(LPCUIFRAME frame, LPCRECT screen) {
    uiBuildQueue_t const *queue = frame->buffer.data;
    LPUIFRAME buildtimer = SCR_Frame(queue->buildtimer);
    LPUIFRAME firstitem  = SCR_Frame(queue->firstitem);
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

void SCR_LayoutDrawMinimap(LPCUIFRAME frame, LPCRECT screen) {
    (void)frame; re.DrawMinimap(screen);
}

void SCR_LayoutDrawPortrait(LPCUIFRAME frame, LPCRECT screen) {
    RECT const viewport = {
        screen->x / UI_BASE_WIDTH,
        (UI_BASE_HEIGHT - screen->y - screen->h) / UI_BASE_HEIGHT,
        screen->w / UI_BASE_WIDTH,
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
    vd.rdflags      = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG | RDF_USE_ENTITY_CAMERA;
    vd.num_entities = 1;
    vd.entities     = &entity;
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
        .uActiveGlow = sel ? sel->ability == frame->stat : 0));
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
    RECT scr = { screen->x + label->offsetx, screen->y + label->offsety, screen->w, screen->h };
    SCR_LayoutApplyPushedTextOffset(frame, &scr);
    layout_text(frame, &scr, SCR_GetStringValue(frame));
}

void SCR_LayoutDrawTextArea(LPCUIFRAME frame, LPCRECT screen) {
    uiTextArea_t const *ta = frame->buffer.data;
    LPCSTR value = SCR_GetStringValue(frame);
    RECT scr = { screen->x + ta->inset, screen->y + ta->inset,
                 screen->w - ta->inset*2, screen->h - ta->inset*2 };
    re.DrawText(&MAKE(drawText_t,
        .font       = cl.fonts[ta->font],
        .text       = value ? value : "",
        .color      = frame->color.a ? frame->color : COLOR32_WHITE,
        .halign     = FONT_JUSTIFYLEFT,
        .valign     = FONT_JUSTIFYTOP,
        .icons      = cl.pics,
        .lineHeight = 1.33,
        .textWidth  = scr.w,
        .rect       = scr,
        .flags      = DRAW_WORD_WRAP));
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

void SCR_LayoutUpdateCommandButton(LPCUIFRAME frame, LPCRECT screen) {
    if (SCR_LayoutFrameIsHovered(frame) && frame->tooltip)
        active_tooltip = frame->tooltip;
}

typedef struct { FRAMETYPE type; void (*func)(LPCUIFRAME, LPCRECT); } drawer_t;

static drawer_t updaters[] = {
    { FT_COMMANDBUTTON, SCR_LayoutUpdateCommandButton },
    { FT_BUILDQUEUE,    SCR_LayoutUpdateBuildQueue },
};

static drawer_t drawers[] = {
    { FT_TEXTURE,        SCR_LayoutDrawTexture },
    { FT_HIGHLIGHT,      SCR_LayoutDrawHighlight },
    { FT_BACKDROP,       SCR_LayoutDrawBackdrop },
    { FT_SIMPLESTATUSBAR,SCR_LayoutDrawStatusbar },
    { FT_COMMANDBUTTON,  SCR_LayoutDrawCommandButton },
    { FT_STRING,         SCR_LayoutDrawString },
    { FT_TEXT,           SCR_LayoutDrawString },
    { FT_TEXTAREA,       SCR_LayoutDrawTextArea },
    { FT_LISTBOX,        SCR_LayoutDrawListBox },
    { FT_SCROLLBAR,      SCR_LayoutDrawScrollBar },
    { FT_TOOLTIPTEXT,    SCR_LayoutDrawTooltip },
    { FT_MODEL,          SCR_LayoutDrawPortrait },
    { FT_SPRITE,         SCR_LayoutDrawSprite },
    { FT_PORTRAIT,       SCR_LayoutDrawPortrait },
    { FT_MINIMAP,        SCR_LayoutDrawMinimap },
    { FT_BUILDQUEUE,     SCR_LayoutDrawBuildQueue },
    { FT_MULTISELECT,    SCR_LayoutDrawMultiSelect },
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
    (void)layout;
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
            SCR_Clear(layout);
            SCR_LayoutUpdateTooltip(layout);
            SCR_LayoutDrawOverlay(layout);
        }
    }
}

void SCR_SetLayoutLayer(DWORD layer, HANDLE data) {
    if (layer < MAX_LAYOUT_LAYERS) layout_layers[layer] = data;
}

void SCR_ClearLayoutLayer(DWORD layer) {
    if (layer < MAX_LAYOUT_LAYERS) layout_layers[layer] = NULL;
}

void SCR_LayoutMouseEvent(uiMouseEvent_t event, int x, int y, int32_t param) {
    VECTOR2 const point = SCR_LayoutScreenToFdf(x, y);

    layout_hovered_number = 0;
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        HANDLE layout = layout_layers[layer];
        DWORD flags = cl.playerstate.uiflags;
        if (!layout || (1 << layer) & flags) continue;
        SCR_Clear(layout);
        for (DWORD i = SCR_NumFrames(); i > 0; i--) {
            LPCUIFRAME frame = SCR_Frame(i - 1);
            if (!frame || !SCR_LayoutFrameHasClickCommand(frame)) continue;
            if (Rect_contains(SCR_LayoutRect(frame), &point)) {
                layout_hovered_number = frame->number; break;
            }
        }
        if (layout_hovered_number) break;
    }

    if (param == 1) {
        if      (event == UI_MOUSE_DOWN) layout_left_down = true;
        else if (event == UI_MOUSE_UP)   layout_left_down = false;
    }
    if (event != UI_MOUSE_UP || param != 1) return;

    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        HANDLE layout = layout_layers[layer];
        DWORD flags = cl.playerstate.uiflags;
        if (!layout || (1 << layer) & flags) continue;
        SCR_Clear(layout);
        for (DWORD i = SCR_NumFrames(); i > 0; i--) {
            LPCUIFRAME frame = SCR_Frame(i - 1);
            if (!frame || !SCR_LayoutFrameHasClickCommand(frame)) continue;
            if (Rect_contains(SCR_LayoutRect(frame), &point)) {
                char command[CMDARG_LEN * 2];
                SCR_LayoutFormatOnClickCommand(frame->onclick, command, sizeof(command));
                MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
                SZ_Printf(&cls.netchan.message, "%s", command);
                return;
            }
        }
    }
}

/* Dispatch a command-button hotkey the same way a mouse click on that */
BOOL SCR_LayoutKeyEvent(int key) {
    int const upper = toupper(key);

    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        HANDLE layout = layout_layers[layer];
        DWORD flags = cl.playerstate.uiflags;
        if (!layout || (1 << layer) & flags) continue;
        SCR_Clear(layout);
        for (DWORD i = SCR_NumFrames(); i > 0; i--) {
            LPCUIFRAME frame = SCR_Frame(i - 1);
            if (!frame || !frame->hotkey || !SCR_LayoutFrameHasClickCommand(frame)) continue;
            if (toupper(frame->hotkey) == upper) {
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

BOOL SCR_LayoutHitTest(int x, int y) {
    VECTOR2 const point = SCR_LayoutScreenToFdf(x, y);
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        HANDLE layout = layout_layers[layer];
        DWORD flags = cl.playerstate.uiflags;
        if (!layout || (1 << layer) & flags) continue;
        SCR_Clear(layout);
        FOR_LOOP(i, SCR_NumFrames()) {
            LPCUIFRAME frame = SCR_Frame(i);
            if (!frame || frame->flags.type != FT_TEXTURE) continue;
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
