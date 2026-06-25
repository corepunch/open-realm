#include <stdlib.h>
#include "ui_layout.h"


#define MAX_LISTBOX_TEXT 2048

static LPCSTR active_tooltip = NULL;
static HANDLE layout_layers[MAX_LAYOUT_LAYERS];
static LPTEXTURE layout_dynamic_pics[MAX_DYNAMIC_IMAGES];
static char layout_dynamic_pic_names[MAX_DYNAMIC_IMAGES][512];
static DWORD layout_dynamic_pic_cursor;
static BOOL layout_left_down;
static DWORD layout_hovered_number;

static RECT Rect_inset(LPCRECT r, FLOAT inset);

static VECTOR2 SCR_LayoutScreenToFdf(int x, int y) {
    LPRENDERER renderer = uiimport.GetRenderer();
    size2_t window = renderer->GetWindowSize();
    FLOAT window_aspect = UI_MIN_ASPECT;
    FLOAT x_scale = 1.0f;
    FLOAT y_scale = 1.0f;
    RECT scene;
    FLOAT nx = 0;
    FLOAT ny = 0;

    if (window.width > 0 && window.height > 0) {
        window_aspect = (FLOAT)window.width / (FLOAT)window.height;
        nx = (FLOAT)x / (FLOAT)window.width;
        ny = (FLOAT)y / (FLOAT)window.height;
    }
    if (window_aspect > UI_MIN_ASPECT) {
        x_scale = window_aspect / UI_MIN_ASPECT;
    } else if (window_aspect < UI_MIN_ASPECT) {
        y_scale = UI_MIN_ASPECT / window_aspect;
    }

    scene.w = UI_BASE_WIDTH * x_scale;
    scene.h = UI_BASE_HEIGHT * y_scale;
    scene.x = (UI_BASE_WIDTH - scene.w) * 0.5f;
    scene.y = (UI_BASE_HEIGHT - scene.h) * 0.5f;

    return MAKE(VECTOR2, scene.x + nx * scene.w, scene.y + ny * scene.h);
}

static RECT get_uvrect(uint8_t const *texcoord) {
    RECT const uv = {
        texcoord[0],
        texcoord[2],
        texcoord[1] - texcoord[0],
        texcoord[3] - texcoord[2],
    };
    return uv;
}

static LPCTEXTURE SCR_LayoutGetDynamicTexture(LPCSTR resource) {
    DWORD slot = MAX_DYNAMIC_IMAGES;

    if (!resource || !*resource || !strcmp(resource, " ")) {
        return NULL;
    }

    FOR_LOOP(i, MAX_DYNAMIC_IMAGES) {
        if (layout_dynamic_pics[i] && !strcmp(layout_dynamic_pic_names[i], resource)) {
            return layout_dynamic_pics[i];
        }
        if (!layout_dynamic_pics[i] && slot == MAX_DYNAMIC_IMAGES) {
            slot = i;
        }
    }

    if (slot == MAX_DYNAMIC_IMAGES) {
        slot = layout_dynamic_pic_cursor++ % MAX_DYNAMIC_IMAGES;
        SAFE_DELETE(layout_dynamic_pics[slot], re.ReleaseTexture);
        layout_dynamic_pic_names[slot][0] = '\0';
    }

    layout_dynamic_pics[slot] = re.LoadTexture(resource);
    if (!layout_dynamic_pics[slot]) {
        return NULL;
    }
    snprintf(layout_dynamic_pic_names[slot], sizeof(layout_dynamic_pic_names[slot]), "%s", resource);
    return layout_dynamic_pics[slot];
}

static BOOL SCR_LayoutShouldSkipLayoutLayer(DWORD layer) {
    if (uiimport.GetPlayerState()->client_ui_state != CLIENT_UI_CINEMATIC &&
        uiimport.GetPlayerState()->client_ui_state != CLIENT_UI_LOADING) {
        return false;
    }

    switch (layer) {
        case LAYER_PORTRAIT:
        case LAYER_CONSOLE:
        case LAYER_COMMANDBAR:
        case LAYER_INFOPANEL:
        case LAYER_INVENTORY:
            return true;
        default:
            return false;
    }
}

static RECT scale_rect(LPCRECT rect, FLOAT factor) {
    VECTOR2 diff = {
        rect->w * (1 - factor),
        rect->h * (1 - factor)
    };
    return (RECT) {
        .x = rect->x + diff.x / 2,
        .y = rect->y + diff.y / 2,
        .w = rect->w - diff.x,
        .h = rect->h - diff.y,
    };
}

static LPCENTITYSTATE SCR_LayoutSelectedEntity(void) {
    DWORD num_entities = uiimport.GetNumEntities();

    FOR_LOOP(index, num_entities) {
        LPCENTITYSTATE ent = uiimport.GetEntity(index);
        if (ent && (ent->renderfx & RF_SELECTED)) {
            return ent;
        }
    }
    return NULL;
}

void SCR_LayoutDrawStatusbar(LPCUIFRAME frame, LPCRECT screen) {
    RECT const uv = { 0, 0, 255, 255 };
    RECT screen2 = *screen;
    RECT uv2 = uv;
//    if (frame->stat > 0 ) {
//        screen2.w *= uiimport.GetPlayerState()->unit_stats[frame->stat] / (FLOAT)255;
//        uv2.w *= uiimport.GetPlayerState()->unit_stats[frame->stat] / (FLOAT)255;
//    } else {
        screen2.w *= frame->value;
        uv2.w *= frame->value;
//    }
    RECT const suv2 = Rect_div(&uv2, 0xff);
    re.DrawImage(uiimport.GetTexture(frame->tex.index), &screen2, &suv2, frame->color);
    if (frame->tex.index2 > 0) {
        RECT const suv = Rect_div(&uv, 0xff);
        re.DrawImage(uiimport.GetTexture(frame->tex.index2), screen, &suv, COLOR32_WHITE);
    }
}

void SCR_LayoutDrawTexture(LPCUIFRAME frame, LPCRECT screen) {
    RECT const uv = get_uvrect(frame->tex.coord);
    RECT const suv = Rect_div(&uv, 0xff);
    LPCTEXTURE texture = uiimport.GetTexture(frame->tex.index);
    if (frame->stat >= MAX_STATS && frame->stat - MAX_STATS < MAX_STATS) {
        LPCSTR resource = uiimport.GetPlayerState()->texts[frame->stat - MAX_STATS];
        LPCTEXTURE dynamicTexture = SCR_LayoutGetDynamicTexture(resource);
        if (dynamicTexture) {
            texture = dynamicTexture;
        }
    }
    re.DrawImage(texture, screen, &suv, frame->color);
}

static void SCR_LayoutDrawHighlightData(uiHighlight_t const *highlight, LPCRECT screen) {
    if (!highlight || !highlight->alphaFile) {
        return;
    }
    re.DrawImageEx(&MAKE(drawImage_t,
                         .texture = uiimport.GetTexture(highlight->alphaFile),
                         .alphamode = highlight->alphaMode,
                         .screen = *screen,
                         .uv = MAKE(RECT,0,0,1,1),
                         .color = COLOR32_WHITE,
                         .shader = SHADER_UI));
}

void SCR_LayoutDrawHighlight(LPCUIFRAME frame, LPCRECT screen) {
    SCR_LayoutDrawHighlightData(frame->buffer.data, screen);
}

void SCR_LayoutSimpleButton(LPCUIFRAME frame, LPCRECT screen) {
    uiSimpleButton_t *button = frame->buffer.data;
    LPCSTR label = frame->text;
    RECT const uv = get_uvrect((BYTE *)&button->normal.texcoord);
    RECT const suv = Rect_div(&uv, 0xff);
    re.DrawImage(uiimport.GetTexture(button->normal.texture), screen, &suv, COLOR32_WHITE);
    re.DrawText(&MAKE(drawText_t,
                      .rect = *screen,
                      .font = uiimport.GetFont(button->normal.font),
                      .text = label,
                      .color = button->normal.fontcolor,
                      .textWidth = screen->w));
}

void SCR_LayoutDrawBackdrop2(LPCUIFRAME frame, LPCRECT screen, uiBackdrop_t const *backdrop) {
    if (!backdrop || !screen || screen->w <= 0 || screen->h <= 0) {
        return;
    }
    if (!backdrop->Background && !backdrop->EdgeFile) {
        return;
    }
    re.DrawBackdrop(&MAKE(drawBackdrop_t,
                          .screen = *screen,
                          .bg_texture = uiimport.GetTexture(backdrop->Background),
                          .edge_texture = uiimport.GetTexture(backdrop->EdgeFile),
                          .bg_color = frame->color,
                          .edge_color = frame->color,
                          .corner_flags = backdrop->CornerFlags,
                          .corner_size = backdrop->CornerSize,
                          .bg_insets = { backdrop->BackgroundInsets[0], backdrop->BackgroundInsets[1],
                                         backdrop->BackgroundInsets[2], backdrop->BackgroundInsets[3] },
                          .tile_bg = backdrop->TileBackground,
                          .mirrored = backdrop->Mirrored));
}

void SCR_LayoutDrawBackdrop(LPCUIFRAME frame, LPCRECT screen) {
    SCR_LayoutDrawBackdrop2(frame, screen, frame->buffer.data);
}

static BOOL SCR_LayoutBackdropHasArt(uiBackdrop_t const *backdrop) {
    return backdrop && (backdrop->Background || backdrop->EdgeFile);
}

static void SCR_LayoutDrawBackdropPart(LPCUIFRAME frame, LPCRECT screen, uiBackdrop_t const *backdrop) {
    if (SCR_LayoutBackdropHasArt(backdrop) && screen->w > 0 && screen->h > 0) {
        SCR_LayoutDrawBackdrop2(frame, screen, backdrop);
    }
}

void SCR_LayoutDrawScrollBar(LPCUIFRAME frame, LPCRECT screen) {
    uiScrollBar_t const *scrollbar = frame->buffer.data;
    FLOAT button_height;
    RECT inc;
    RECT dec;
    RECT track;
    RECT thumb;

    if (!scrollbar || screen->w <= 0 || screen->h <= 0) {
        return;
    }

    SCR_LayoutDrawBackdropPart(frame, screen, &scrollbar->background);

    button_height = MIN(screen->w, screen->h * 0.5f);
    inc = MAKE(RECT, screen->x, screen->y + screen->h - button_height, screen->w, button_height);
    dec = MAKE(RECT, screen->x, screen->y, screen->w, button_height);
    track = MAKE(RECT, screen->x, dec.y + dec.h, screen->w, inc.y - (dec.y + dec.h));
    SCR_LayoutDrawBackdropPart(frame, &inc, &scrollbar->incButton);
    SCR_LayoutDrawBackdropPart(frame, &dec, &scrollbar->decButton);

    if (track.h <= 0) {
        return;
    }

#ifdef UI_STRETCHED_SCROLLBAR_THUMB
    thumb.h = MIN(MAX(button_height, track.h * 0.25f), track.h);
#else
    thumb.h = MIN(MIN(button_height, 0.010f), track.h);
#endif
    thumb.w = MIN(screen->w, 0.010f);
    thumb.x = screen->x + (screen->w - thumb.w) * 0.5f;
    thumb.y = track.y + track.h - thumb.h - (track.h - thumb.h) * MIN(MAX(frame->value, 0.0f), 1.0f);
    SCR_LayoutDrawBackdropPart(frame, &thumb, &scrollbar->thumbButton);
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

static void SCR_LayoutFormatOnClickCommand(LPCSTR source, LPSTR dest, DWORD dest_size) {
    DWORD out = 0;

    if (!dest || dest_size == 0) {
        return;
    }
    dest[0] = '\0';
    if (!source) {
        return;
    }

    for (DWORD i = 0; source[i] && out + 1 < dest_size; i++) {
        if (source[i] == '{') {
            char name[80];
            DWORD name_len = 0;
            DWORD j = i + 1;

            while (source[j] && source[j] != '}' && name_len + 1 < sizeof(name)) {
                name[name_len++] = source[j++];
            }
            if (source[j] == '}') {
                char value[16];

                name[name_len] = '\0';
                snprintf(value, sizeof(value), "%d", 0);
                for (DWORD k = 0; value[k] && out + 1 < dest_size; k++) {
                    dest[out++] = value[k];
                }
                i = j;
                continue;
            }
        }
        dest[out++] = source[i];
    }
    dest[out] = '\0';
}

void SCR_LayoutGlueTextButton(LPCUIFRAME frame, LPCRECT screen) {
    uiGlueTextButton_t const *gluetextbutton = frame->buffer.data;
    BOOL const enabled = SCR_LayoutFrameHasClickCommand(frame);
    uiBackdrop_t const *backdrop = &gluetextbutton->normal;
    if (!enabled) {
        backdrop = SCR_LayoutGlueTextButtonIsPushed(frame) ? &gluetextbutton->disabledPushed : &gluetextbutton->disabled;
    } else if (SCR_LayoutGlueTextButtonIsPushed(frame)) {
        backdrop = &gluetextbutton->pushed;
    }

    SCR_LayoutDrawBackdrop2(frame, screen, backdrop);
}

static void SCR_LayoutDrawGlueTextButtonHighlight(LPCUIFRAME frame) {
    uiGlueTextButton_t const *gluetextbutton = frame->buffer.data;
    RECT const *screen = SCR_LayoutRect(frame);
    BOOL const enabled = SCR_LayoutFrameHasClickCommand(frame);
    BOOL const mouse_over = SCR_LayoutFrameIsHovered(frame);

    if (enabled && mouse_over) {
        SCR_LayoutDrawHighlightData(&gluetextbutton->highlight, screen);
    }
}

void SCR_LayoutDrawBuildQueue(LPCUIFRAME frame, LPCRECT scrn) {
    RECT screen = *scrn;
    RECT const uv = { 0, 0, 1, 1 };
    uiBuildQueue_t const *queue = frame->buffer.data;
    DWORD active = queue->numitems;

    FOR_LOOP(i, queue->numitems) {
        if (cl.time < queue->items[i].endtime) {
            active = i;
            break;
        }
    }
    for (DWORD i = active + 1; i < queue->numitems; i++) {
        if (cl.time < queue->items[i].endtime) {
            re.DrawImage(uiimport.GetTexture(queue->items[i].image), &screen, &uv, frame->color);
            screen.x += queue->itemoffset;
        }
    }
}

void SCR_LayoutUpdateBuildQueue(LPCUIFRAME frame, LPCRECT screen) {
    uiBuildQueue_t const *queue = frame->buffer.data;
    LPUIFRAME buildtimer = SCR_Frame(queue->buildtimer);
    LPUIFRAME firstitem = SCR_Frame(queue->firstitem);

    FOR_LOOP(i, queue->numitems) {
        uiBuildQueueItem_t const *item = &queue->items[i];
        if (cl.time < item->endtime) {
            FLOAT duration = item->endtime - item->starttime;
            FLOAT elapsed = cl.time > item->starttime ? (FLOAT)(cl.time - item->starttime) : 0;
            FLOAT progress = duration > 0 ? elapsed / duration : 1;
            progress = MAX(0, MIN(progress, 1));
            if (buildtimer) buildtimer->value = progress;
            if (firstitem) firstitem->tex.index = item->image;
            break;
        }
    }
    (void)screen;
}

#define HP_BAR_HEIGHT_RATIO 0.175f
#define HP_BAR_SPACING_RATIO 0.02f

void SCR_LayoutDrawMultiSelect(LPCUIFRAME frame, LPCRECT scrn) {
    RECT screen = *scrn;
    uiMultiselect_t const *multiselect = frame->buffer.data;
    DWORD column = 0;
    FOR_LOOP(i, multiselect->numitems) {
        RECT uv = { 0, 0, 1, 1 };
        uiMultiselectItem_t const *item = &multiselect->items[i];
        re.DrawImage(uiimport.GetTexture(item->image), &screen, &uv, frame->color);
        LPCENTITYSTATE ent = uiimport.GetEntity(item->entity);
        if (ent) {
            FLOAT health = BYTE2FLOAT(ent->stats[ENT_HEALTH]);
            FLOAT mana = BYTE2FLOAT(ent->stats[ENT_MANA]);
            RECT rect = {
                screen.x,
                screen.y + screen.h * (1 + HP_BAR_SPACING_RATIO),
                screen.w * health,
                screen.h * HP_BAR_HEIGHT_RATIO
            };
            uv.w = health;
            re.DrawImage(uiimport.GetTexture(multiselect->hp_bar), &rect, &uv, MAKE(COLOR32,0,255,0,255));
            uv.w = mana;
            rect.w = screen.w * mana;
            rect.y += screen.h * (HP_BAR_HEIGHT_RATIO + HP_BAR_SPACING_RATIO);
            re.DrawImage(uiimport.GetTexture(multiselect->mana_bar), &rect, &uv, MAKE(COLOR32,0,255,255,255));
        }
        if (++column >= multiselect->numcolumns) {
            column = 0;
            screen.x = SCR_LayoutRect(frame)->x;
            screen.y += multiselect->offset.y;
        } else {
            screen.x += multiselect->offset.x;
        }
    }
}

void SCR_LayoutDrawPortrait(LPCUIFRAME frame, LPCRECT screen) {
    RECT const viewport = {
        screen->x / UI_BASE_WIDTH,
        (UI_BASE_HEIGHT - screen->y - screen->h) / UI_BASE_HEIGHT,
        screen->w / UI_BASE_WIDTH,
        screen->h / UI_BASE_HEIGHT
    };
    LPCMODEL port = uiimport.GetPortrait(frame->tex.index);
    LPCMODEL model = uiimport.GetModel(frame->tex.index);
    LPCMODEL draw_model = port ? port : model;
    if (!draw_model) return;

    renderEntity_t entity = {0};
    entity.model = draw_model;
    entity.scale = 1.0f;
    entity.flags = RF_NO_SHADOW | RF_NO_FOGOFWAR | RF_PORTRAIT_LIGHTING;
    re.SetEntityAnimFrame(draw_model, "Stand", &entity);

    viewDef_t viewdef = {0};
    viewdef.viewport = viewport;
    viewdef.rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG | RDF_USE_ENTITY_CAMERA;
    viewdef.num_entities = 1;
    viewdef.entities = &entity;

    re.RenderFrame(&viewdef);
}

void SCR_LayoutDrawSprite(LPCUIFRAME frame, LPCRECT screen) {
    LPCSTR anim = frame->text;
    LPCMODEL model = uiimport.GetModel(frame->tex.index);

    if (anim && *anim) {
        re.DrawSprite(model, anim, screen->x, screen->y);
    } else {
        re.DrawSprite(model, "Stand", screen->x, screen->y);
    }
}

void SCR_LayoutDrawCommandButton(LPCUIFRAME frame, LPCRECT screen) {
    LPCENTITYSTATE selentity = SCR_LayoutSelectedEntity();
    RECT const uv = get_uvrect(frame->tex.coord);
    RECT const suv = Rect_div(&uv, 0xff);
    RECT scrn = scale_rect(screen, 0.925);
    if (SCR_LayoutFrameIsHovered(frame)) {
        if (layout_left_down) {
            scrn = scale_rect(screen, 0.875);
        }
    }
    re.DrawImageEx(&MAKE(drawImage_t,
                         .texture = uiimport.GetTexture(frame->tex.index),
                         .screen = scrn,
                         .uv = suv,
                         .color = COLOR32_WHITE,
                         .shader = SHADER_COMMANDBUTTON,
                          .uActiveGlow = selentity ? selentity->ability == frame->stat : 0));
    /* TODO: Cooldown shade — SCR_LayoutTexture is not available in the client
     * layer. Need to pass the texture from the layout pass or move to UI module. */
    (void)frame;
}

void layout_text(LPCUIFRAME frame, LPCRECT screen, LPCSTR text) {
    drawText_t drawtext = SCR_GetDrawText(frame, screen->w, text, frame->buffer.data);
    drawtext.rect = *screen;
    drawtext.wordWrap = true;
    re.DrawText(&drawtext);
}

static void SCR_LayoutApplyPushedTextOffset(LPCUIFRAME frame, LPRECT screen) {
    if (frame->parent >= SCR_NumFrames()) {
        return;
    }

    LPCUIFRAME parent = SCR_Frame(frame->parent);
    if (!parent) {
        return;
    }
    if (parent->flags.type != FT_GLUETEXTBUTTON && parent->flags.type != FT_GLUEBUTTON) {
        return;
    }
    if (!SCR_LayoutFrameHasClickCommand(parent)) {
        return;
    }

    if (!SCR_LayoutGlueTextButtonIsPushed(parent)) {
        return;
    }

    uiGlueTextButton_t const *button = parent->buffer.data;
    screen->x += button->pushedTextOffset.x;
    screen->y -= button->pushedTextOffset.y;
}

void SCR_LayoutDrawString(LPCUIFRAME frame, LPCRECT screen) {
    uiLabel_t const *label = frame->buffer.data;
    RECT scr = *screen;
    scr.x += label->offsetx;
    scr.y += label->offsety;
    SCR_LayoutApplyPushedTextOffset(frame, &scr);
    layout_text(frame, &scr, SCR_GetStringValue(frame));
}

void SCR_LayoutDrawTextArea(LPCUIFRAME frame, LPCRECT screen) {
    uiTextArea_t const *textArea = frame->buffer.data;
    RECT scr = {
        screen->x + textArea->inset,
        screen->y + textArea->inset,
        screen->w - textArea->inset * 2,
        screen->h - textArea->inset * 2,
    };
    re.DrawText(&MAKE(drawText_t,
                      .font = uiimport.GetFont(textArea->font),
                      .text = frame->text ? frame->text : "",
                      .color = frame->color.a ? frame->color : COLOR32_WHITE,
                      .halign = FONT_JUSTIFYLEFT,
                      .valign = FONT_JUSTIFYTOP,
                      .icons = uiimport.GetTextures(),
                      .lineHeight = 1.33,
                      .textWidth = scr.w,
                      .rect = scr,
                      .wordWrap = true));
}

void SCR_LayoutDrawListBox(LPCUIFRAME frame, LPCRECT screen) {
    uiListBox_t const *listbox = frame->buffer.data;
    RECT list_rect = Rect_inset(screen, listbox->border);
    LPCUIFRAME scrollbar = NULL;
    FLOAT item_y = list_rect.y + list_rect.h;
    FLOAT item_height = listbox->itemHeight > 0 ? listbox->itemHeight : 0.018f;
    LPCSTR text = frame->text;
    SHORT selectedIndex = listbox->selectedIndex;
    DWORD scrollOffset = 0;
    DWORD numRows = 0;
    DWORD visibleRows;
    char items[MAX_LISTBOX_TEXT];
    char *line = NULL;
    char *save = NULL;
    int index = 0;

    SCR_LayoutDrawBackdrop2(frame, screen, &listbox->background);

    FOR_LOOP(i, SCR_NumFrames()) {
        LPCUIFRAME child = SCR_Frame(i);
        if (child && child->parent == frame->number && child->flags.type == FT_SCROLLBAR) {
            scrollbar = child;
            break;
        }
    }
    if (scrollbar) {
        LPCRECT scroll_rect = SCR_LayoutRect(scrollbar);
        FLOAT scroll_inset = MAX(scroll_rect->w, 0.0f);

        if (scroll_inset > 0 && scroll_inset < list_rect.w) {
            list_rect.w -= scroll_inset;
        }
    }
    visibleRows = MAX((DWORD)floorf(list_rect.h / item_height), 1);
    if (scrollbar) {
        DWORD maxScroll = numRows > visibleRows ? numRows - visibleRows : 0;

        ((LPUIFRAME)scrollbar)->value = maxScroll ? scrollOffset / (FLOAT)maxScroll : 0.0f;
    }

    if (!text || !*text) {
        return;
    }

    snprintf(items, sizeof(items), "%s", text);
    line = strtok_r(items, "\n", &save);
    while (line && index < (int)scrollOffset) {
        line = strtok_r(NULL, "\n", &save);
        index++;
    }
    while (line && item_y > list_rect.y) {
        RECT row = list_rect;
        char *display = line;
        char *hidden = strchr(display, '\t');
        int rowIndex = index;

        if (hidden) {
            *hidden = '\0';
        }
        row.h = MIN(item_height, item_y - list_rect.y);
        row.y = item_y - row.h;
        if (rowIndex == selectedIndex) {
            re.DrawImage(uiimport.GetTexture(0), &row, &MAKE(RECT, 0, 0, 1, 1), MAKE(COLOR32, 32, 64, 180, 128));
        }
        re.DrawText(&MAKE(drawText_t,
                          .font = uiimport.GetFont(listbox->text.font),
                          .text = display,
                          .color = frame->color.a ? frame->color : COLOR32_WHITE,
                          .halign = FONT_JUSTIFYLEFT,
                          .valign = FONT_JUSTIFYMIDDLE,
                          .icons = uiimport.GetTextures(),
                          .lineHeight = 1.33,
                          .textWidth = row.w,
                          .rect = row,
                          .wordWrap = false));
        item_y -= item_height;
        line = strtok_r(NULL, "\n", &save);
        index++;
    }
}

static RECT Rect_inset(LPCRECT r, FLOAT inset) {
    return MAKE(RECT,r->x+inset,r->y+inset,r->w-inset*2,r->h-inset*2);
}

void SCR_LayoutDrawTooltip(LPCUIFRAME frame, LPCRECT scrn) {
    if (active_tooltip) {
        RECT screen = *scrn;
        uiTooltip_t const *tooltip = frame->buffer.data;
        FLOAT const PADDING = 0.005;
        FLOAT const avlspace = screen.w - PADDING * 2;
        drawText_t drawtext = SCR_GetDrawText(frame, avlspace, active_tooltip, &tooltip->text);
        drawtext.wordWrap = true;
        VECTOR2 textsize = re.GetTextSize(&drawtext);
        textsize.y += PADDING * 2;
        screen.y += screen.h - textsize.y;
        screen.h = textsize.y;
        RECT text = Rect_inset(&screen, PADDING);
        SCR_LayoutDrawBackdrop(frame, &screen);
        drawtext = SCR_GetDrawText(frame, text.w, active_tooltip, &tooltip->text);
        drawtext.rect = text;
        drawtext.wordWrap = true;
        re.DrawText(&drawtext);
    }
}

void SCR_LayoutUpdateCommandButton(LPCUIFRAME frame, LPCRECT screen) {
    if (SCR_LayoutFrameIsHovered(frame) && frame->tooltip) {
        active_tooltip = frame->tooltip;
    }
}

typedef struct {
    FRAMETYPE type;
    void (*func)(LPCUIFRAME, LPCRECT);
} drawer_t;

static drawer_t updaters[] = {
    { FT_COMMANDBUTTON, SCR_LayoutUpdateCommandButton },
    { FT_BUILDQUEUE, SCR_LayoutUpdateBuildQueue },
};

static drawer_t drawers[] = {
    { FT_TEXTURE, SCR_LayoutDrawTexture },
    { FT_HIGHLIGHT, SCR_LayoutDrawHighlight },
    { FT_BACKDROP, SCR_LayoutDrawBackdrop },
    { FT_SIMPLESTATUSBAR, SCR_LayoutDrawStatusbar },
    { FT_COMMANDBUTTON, SCR_LayoutDrawCommandButton },
    { FT_STRING, SCR_LayoutDrawString },
    { FT_TEXT, SCR_LayoutDrawString },
    { FT_TEXTAREA, SCR_LayoutDrawTextArea },
    { FT_LISTBOX, SCR_LayoutDrawListBox },
    { FT_SCROLLBAR, SCR_LayoutDrawScrollBar },
    { FT_TOOLTIPTEXT, SCR_LayoutDrawTooltip },
    { FT_MODEL, SCR_LayoutDrawPortrait },
    { FT_SPRITE, SCR_LayoutDrawSprite },
    { FT_PORTRAIT, SCR_LayoutDrawPortrait },
    { FT_BUILDQUEUE, SCR_LayoutDrawBuildQueue },
    { FT_MULTISELECT, SCR_LayoutDrawMultiSelect },
    { FT_SIMPLEBUTTON, SCR_LayoutSimpleButton },
    { FT_BUTTON, SCR_LayoutGlueTextButton },
    { FT_TEXTBUTTON, SCR_LayoutGlueTextButton },
    { FT_POPUPMENU, SCR_LayoutGlueTextButton },
    { FT_GLUEPOPUPMENU, SCR_LayoutGlueTextButton },
    { FT_GLUETEXTBUTTON, SCR_LayoutGlueTextButton },
    { FT_GLUEBUTTON, SCR_LayoutGlueTextButton },
//    { FT_NONE, NULL },
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

void SCR_LayoutUpdateTooltip(HANDLE _frames) {
    FOR_LOOP(i, SCR_NumFrames()) {
        LPCUIFRAME frame = SCR_Frame(i);
        if (frame) {
            SCR_LayoutUpdateFrame(frame);
        }
    }
}

void SCR_LayoutDrawOverlay(HANDLE _frames) {
    FOR_LOOP(i, SCR_NumFrames()) {
        LPCUIFRAME frame = SCR_Frame(i);
        if (frame && frame->flags.type == FT_SPRITE) {
            SCR_LayoutDrawFrame(frame);
        }
    }
    FOR_LOOP(i, SCR_NumFrames()) {
        LPCUIFRAME frame = SCR_Frame(i);
        if (frame && frame->flags.type != FT_SPRITE) {
            SCR_LayoutDrawFrame(frame);
        }
    }
    FOR_LOOP(i, SCR_NumFrames()) {
        LPCUIFRAME frame = SCR_Frame(i);
        if (frame && (frame->flags.type == FT_GLUETEXTBUTTON || frame->flags.type == FT_GLUEBUTTON)) {
            SCR_LayoutDrawGlueTextButtonHighlight(frame);
        }
    }
}

void SCR_DrawLayout(void) {
    active_tooltip = NULL;
    
    if (uiimport.GetPlayerState()->cinefade > 0) {
        COLOR32 color = COLOR32_BLACK;
        color.a = 255 * uiimport.GetPlayerState()->cinefade;
        re.DrawImage(uiimport.GetTexture(0), &MAKE(RECT,0,0,1,1), &MAKE(RECT,0,0,1,1), color);
    }
    
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        if ((1 << layer) & uiimport.GetPlayerState()->uiflags)
            continue;
        if (SCR_LayoutShouldSkipLayoutLayer(layer))
            continue;
        HANDLE layout = layout_layers[layer];
        if (layout) {
            SCR_Clear(layout);
            SCR_LayoutUpdateTooltip(layout);
        }
    }
    
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        if ((1 << layer) & uiimport.GetPlayerState()->uiflags)
            continue;
        if (SCR_LayoutShouldSkipLayoutLayer(layer))
            continue;
        HANDLE layout = layout_layers[layer];
        if (layout) {
            SCR_Clear(layout);
            SCR_LayoutUpdateTooltip(layout);
            SCR_LayoutDrawOverlay(layout);
        }
    }
}

void SCR_SetLayoutLayer(DWORD layer, HANDLE data) {
    if (layer >= MAX_LAYOUT_LAYERS) {
        return;
    }
    layout_layers[layer] = data;
}

void SCR_ClearLayoutLayer(DWORD layer) {
    if (layer >= MAX_LAYOUT_LAYERS) {
        return;
    }
    layout_layers[layer] = NULL;
}

/* Server-authored layout clicks are handled at event time, not during drawing. */
void SCR_LayoutMouseEvent(uiMouseEvent_t event, int x, int y, int32_t param) {
    VECTOR2 const point = SCR_LayoutScreenToFdf(x, y);

    /* Track hover state on every mouse event */
    layout_hovered_number = 0;
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        HANDLE layout = layout_layers[layer];
        if (!layout || ((1 << layer) & uiimport.GetPlayerState()->uiflags) || SCR_LayoutShouldSkipLayoutLayer(layer)) {
            continue;
        }
        SCR_Clear(layout);
        for (DWORD i = SCR_NumFrames(); i > 0; i--) {
            LPCUIFRAME frame = SCR_Frame(i - 1);
            if (!frame || !SCR_LayoutFrameHasClickCommand(frame)) {
                continue;
            }
            if (Rect_contains(SCR_LayoutRect(frame), &point)) {
                layout_hovered_number = frame->number;
                break;
            }
        }
        if (layout_hovered_number) {
            break;
        }
    }

    if (param == 1) {
        if (event == UI_MOUSE_DOWN) {
            layout_left_down = true;
        } else if (event == UI_MOUSE_UP) {
            layout_left_down = false;
        }
    }
    if (event != UI_MOUSE_UP || param != 1) {
        return;
    }
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        HANDLE layout = layout_layers[layer];
        if (!layout || ((1 << layer) & uiimport.GetPlayerState()->uiflags) || SCR_LayoutShouldSkipLayoutLayer(layer)) {
            continue;
        }
        SCR_Clear(layout);
        for (DWORD i = SCR_NumFrames(); i > 0; i--) {
            LPCUIFRAME frame = SCR_Frame(i - 1);
            if (!frame || !SCR_LayoutFrameHasClickCommand(frame)) {
                continue;
            }
            if (Rect_contains(SCR_LayoutRect(frame), &point)) {
                char command[CMDARG_LEN * 2];
                SCR_LayoutFormatOnClickCommand(frame->onclick, command, sizeof(command));
                if (uiimport.ServerCommand) {
                    uiimport.ServerCommand(command);
                }
                return;
            }
        }
    }
}

BOOL SCR_LayoutHitTest(int x, int y) {
    VECTOR2 const point = SCR_LayoutScreenToFdf(x, y);

    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        HANDLE layout = layout_layers[layer];

        if (!layout) {
            continue;
        }
        if ((1 << layer) & uiimport.GetPlayerState()->uiflags) {
            continue;
        }
        if (SCR_LayoutShouldSkipLayoutLayer(layer)) {
            continue;
        }
        SCR_Clear(layout);
        FOR_LOOP(i, SCR_NumFrames()) {
            LPCUIFRAME frame = SCR_Frame(i);
            if (!frame || frame->flags.type != FT_TEXTURE) {
                continue;
            }
            if (Rect_contains(SCR_LayoutRect(frame), &point)) {
                return true;
            }
        }
    }
    return false;
}
