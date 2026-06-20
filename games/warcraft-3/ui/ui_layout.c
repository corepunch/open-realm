#include <stdlib.h>
#include "ui_local.h"


#define MAX_LISTBOX_TEXT 2048

static LPCSTR active_tooltip = NULL;
static HANDLE layout_layers[MAX_LAYOUT_LAYERS];
static LPTEXTURE layout_dynamic_pics[MAX_DYNAMIC_IMAGES];
static char layout_dynamic_pic_names[MAX_DYNAMIC_IMAGES][512];
static VECTOR2 layout_last_mouse;
static DWORD layout_dynamic_pic_cursor;

static RECT Rect_inset(LPCRECT r, FLOAT inset);

static PLAYER const empty_player;

static LPRENDERER UI_LayoutRenderer(void) {
    return uiimport.GetRenderer ? uiimport.GetRenderer() : NULL;
}

#define re (*UI_LayoutRenderer())

static LPCPLAYER UI_LayoutPlayerState(void) {
    LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;
    return ps ? ps : &empty_player;
}

static DWORD UI_LayoutTime(void) {
    return 0;
}

static VECTOR2 UI_LayoutMouseToFdf(void) {
    return layout_last_mouse;
}

static VECTOR2 UI_LayoutScreenToFdf(int x, int y) {
    LPRENDERER renderer = UI_LayoutRenderer();
    size2_t window = renderer && renderer->GetWindowSize ? renderer->GetWindowSize() : MAKE(size2_t, 0, 0);
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

static LPCTEXTURE UI_LayoutTexture(DWORD index) {
    return uiimport.GetTexture ? uiimport.GetTexture(index) : NULL;
}

static LPCTEXTURE *UI_LayoutTextures(void) {
    return uiimport.GetTextures ? uiimport.GetTextures() : NULL;
}

static LPCFONT UI_LayoutFont(DWORD index) {
    return uiimport.GetFont ? uiimport.GetFont(index) : NULL;
}

static LPCUIFRAME UI_LayoutClear(HANDLE data) {
    return uiimport.LayoutClear ? uiimport.LayoutClear(data) : NULL;
}

static DWORD UI_LayoutNumFrames(void) {
    return uiimport.LayoutNumFrames ? uiimport.LayoutNumFrames() : 0;
}

static LPUIFRAME UI_LayoutFrame(DWORD number) {
    return uiimport.LayoutFrame ? uiimport.LayoutFrame(number) : NULL;
}

static LPCRECT UI_LayoutLayoutRect(LPCUIFRAME frame) {
    static RECT empty_rect;
    LPCRECT rect = uiimport.LayoutRect ? uiimport.LayoutRect(frame) : NULL;
    return rect ? rect : &empty_rect;
}

static LPCSTR UI_LayoutGetStringValue(LPCUIFRAME frame) {
    return uiimport.LayoutStringValue ? uiimport.LayoutStringValue(frame) : "";
}

static drawText_t UI_LayoutGetDrawText(LPCUIFRAME frame,
                                       FLOAT avl_width,
                                       LPCSTR text,
                                       uiLabel_t const *label) {
    if (uiimport.LayoutDrawText) {
        return uiimport.LayoutDrawText(frame, avl_width, text, label);
    }
    return MAKE(drawText_t, .text = text, .textWidth = avl_width);
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

static LPCTEXTURE UI_LayoutGetDynamicTexture(LPCSTR resource) {
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

static BOOL UI_LayoutShouldSkipLayoutLayer(DWORD layer) {
    if (UI_LayoutPlayerState()->client_ui_state != CLIENT_UI_CINEMATIC &&
        UI_LayoutPlayerState()->client_ui_state != CLIENT_UI_LOADING) {
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

static LPCENTITYSTATE UI_LayoutSelectedEntity(void) {
    DWORD num_entities = uiimport.GetNumEntities ? uiimport.GetNumEntities() : 0;

    FOR_LOOP(index, num_entities) {
        LPCENTITYSTATE ent = uiimport.GetEntity ? uiimport.GetEntity(index) : NULL;
        if (ent && (ent->renderfx & RF_SELECTED)) {
            return ent;
        }
    }
    return NULL;
}

void UI_LayoutDrawStatusbar(LPCUIFRAME frame, LPCRECT screen) {
    RECT const uv = { 0, 0, 255, 255 };
    RECT screen2 = *screen;
    RECT uv2 = uv;
//    if (frame->stat > 0 ) {
//        screen2.w *= UI_LayoutPlayerState()->unit_stats[frame->stat] / (FLOAT)255;
//        uv2.w *= UI_LayoutPlayerState()->unit_stats[frame->stat] / (FLOAT)255;
//    } else {
        screen2.w *= frame->value;
        uv2.w *= frame->value;
//    }
    RECT const suv2 = Rect_div(&uv2, 0xff);
    re.DrawImage(UI_LayoutTexture(frame->tex.index), &screen2, &suv2, frame->color);
    if (frame->tex.index2 > 0) {
        RECT const suv = Rect_div(&uv, 0xff);
        re.DrawImage(UI_LayoutTexture(frame->tex.index2), screen, &suv, COLOR32_WHITE);
    }
}

void UI_LayoutDrawTexture(LPCUIFRAME frame, LPCRECT screen) {
    RECT const uv = get_uvrect(frame->tex.coord);
    RECT const suv = Rect_div(&uv, 0xff);
    LPCTEXTURE texture = UI_LayoutTexture(frame->tex.index);
    if (frame->stat >= MAX_STATS && frame->stat - MAX_STATS < MAX_STATS) {
        LPCSTR resource = UI_LayoutPlayerState()->texts[frame->stat - MAX_STATS];
        LPCTEXTURE dynamicTexture = UI_LayoutGetDynamicTexture(resource);
        if (dynamicTexture) {
            texture = dynamicTexture;
        }
    }
    re.DrawImage(texture, screen, &suv, frame->color);
}

static void UI_LayoutDrawHighlightData(uiHighlight_t const *highlight, LPCRECT screen) {
    if (!highlight || !highlight->alphaFile) {
        return;
    }
    re.DrawImageEx(&MAKE(drawImage_t,
                         .texture = UI_LayoutTexture(highlight->alphaFile),
                         .alphamode = highlight->alphaMode,
                         .screen = *screen,
                         .uv = MAKE(RECT,0,0,1,1),
                         .color = COLOR32_WHITE,
                         .rotate = false,
                         .shader = SHADER_UI));
}

void UI_LayoutDrawHighlight(LPCUIFRAME frame, LPCRECT screen) {
    UI_LayoutDrawHighlightData(frame->buffer.data, screen);
}

void UI_LayoutSimpleButton(LPCUIFRAME frame, LPCRECT screen) {
    uiSimpleButton_t *button = frame->buffer.data;
    LPCSTR label = frame->text;
    RECT const uv = get_uvrect((BYTE *)&button->normal.texcoord);
    RECT const suv = Rect_div(&uv, 0xff);
    re.DrawImage(UI_LayoutTexture(button->normal.texture), screen, &suv, COLOR32_WHITE);
    re.DrawText(&MAKE(drawText_t,
                      .rect = *screen,
                      .font = UI_LayoutFont(button->normal.font),
                      .text = label,
                      .color = button->normal.fontcolor,
                      .textWidth = screen->w));
}

typedef enum {
    LAYOUT_BACKDROPINSET_RIGHT,
    LAYOUT_BACKDROPINSET_TOP,
    LAYOUT_BACKDROPINSET_BOTTOM,
    LAYOUT_BACKDROPINSET_LEFT,
} UI_LAYOUT_BACKDROPINSET;

#define NUM_BACKDROP_CORNERS 8

void backdrop_rects(LPCRECT screen, LPRECT rects, FLOAT corner_size) {
    FLOAT x[] = { 0, corner_size, screen->w - corner_size, screen->w };
    FLOAT y[] = { 0, corner_size, screen->h - corner_size, screen->h };
    FOR_LOOP(i, BACKDROP_SIZE) {
        rects[i].x = screen->x + x[i % 3];
        rects[i].y = screen->y + y[i / 3];
        rects[i].w = x[(i % 3) + 1] - x[i % 3];
        rects[i].h = y[(i / 3) + 1] - y[i / 3];
    }
}

FLOAT backdrop_edge_tile(LPCRECT rect, BACKDROPCORNER edge, FLOAT imagesize) {
    switch (edge) {
        case BACKDROP_LEFT_EDGE:
        case BACKDROP_RIGHT_EDGE:
            return ceil(rect->h / imagesize);
        case BACKDROP_TOP_EDGE:
        case BACKDROP_BOTTOM_EDGE:
            return ceil(rect->w / imagesize);
        default:
            return 1;
    }
}

BOOL backdrop_edge_flip(BACKDROPCORNER edge) {
    switch (edge) {
        case BACKDROP_TOP_EDGE:
        case BACKDROP_BOTTOM_EDGE:
            return true;
        default:
            return false;
    }
}

void UI_LayoutDrawBackdrop2(LPCUIFRAME frame, LPCRECT screen, uiBackdrop_t const *backdrop) {
    BACKDROPCORNER const corners[NUM_BACKDROP_CORNERS] = {
        BACKDROP_LEFT_EDGE,
        BACKDROP_RIGHT_EDGE,
        BACKDROP_TOP_EDGE,
        BACKDROP_BOTTOM_EDGE,
        BACKDROP_TOP_LEFT_CORNER,
        BACKDROP_TOP_RIGHT_CORNER,
        BACKDROP_BOTTOM_LEFT_CORNER,
        BACKDROP_BOTTOM_RIGHT_CORNER,
    };
    RECT rects[BACKDROP_SIZE];
    backdrop_rects(screen, rects, backdrop->CornerSize);

#ifdef DIAG_OUTPUT
    (void)frame;
    (void)screen;
    (void)backdrop;
#endif

    size2_t backSize = re.GetTextureSize(UI_LayoutTexture(backdrop->Background));
    size2_t edgeSize = re.GetTextureSize(UI_LayoutTexture(backdrop->EdgeFile));

    RECT uv = { backdrop->Mirrored ? 1 : 0, 0, backdrop->Mirrored ? -1 : 1, 1};
    RECT background = *screen;
    background.x += backdrop->BackgroundInsets[LAYOUT_BACKDROPINSET_LEFT];
    background.y += backdrop->BackgroundInsets[LAYOUT_BACKDROPINSET_TOP];
    background.w -= backdrop->BackgroundInsets[LAYOUT_BACKDROPINSET_LEFT];
    background.w -= backdrop->BackgroundInsets[LAYOUT_BACKDROPINSET_RIGHT];
    background.h -= backdrop->BackgroundInsets[LAYOUT_BACKDROPINSET_TOP];
    background.h -= backdrop->BackgroundInsets[LAYOUT_BACKDROPINSET_BOTTOM];
    if (backdrop->TileBackground) {
        uv.w = background.w / (backSize.width / 1000.f);
        if (backdrop->Mirrored) {
            uv.x = uv.w;
            uv.w = -uv.w;
        }
        uv.h = background.h / (backSize.height / 1000.f);
    }
    re.DrawImageEx(&MAKE(drawImage_t,
                         .texture = UI_LayoutTexture(backdrop->Background),
                         .alphamode = BLEND_MODE_BLEND,
                         .screen = background,
                         .uv = uv,
                         .color = frame->color,
                         .rotate = false,
                         .shader = SHADER_UI));

    FOR_LOOP(i, NUM_BACKDROP_CORNERS) {
        if ((backdrop->CornerFlags & (1 << corners[i])) == 0)
            continue;
        FLOAT const k = 1.0 / NUM_BACKDROP_CORNERS;
        FLOAT const h = edgeSize.height / 1000.f;
        FLOAT const tile = backdrop_edge_tile(rects+corners[i], corners[i], h);
        BOOL const flip = backdrop_edge_flip(corners[i]);
        RECT const rect = { i * k, 0, k, tile };
        re.DrawImageEx(&MAKE(drawImage_t,
                             .texture = UI_LayoutTexture(backdrop->EdgeFile),
                             .alphamode = BLEND_MODE_BLEND,
                             .screen = rects[corners[i]],
                             .uv = rect,
                             .color = frame->color,
                             .rotate = flip,
                             .shader = SHADER_UI));
    }
}

void UI_LayoutDrawBackdrop(LPCUIFRAME frame, LPCRECT screen) {
    UI_LayoutDrawBackdrop2(frame, screen, frame->buffer.data);
}

static BOOL UI_LayoutBackdropHasArt(uiBackdrop_t const *backdrop) {
    return backdrop && (backdrop->Background || backdrop->EdgeFile);
}

static void UI_LayoutDrawBackdropPart(LPCUIFRAME frame, LPCRECT screen, uiBackdrop_t const *backdrop) {
    if (UI_LayoutBackdropHasArt(backdrop) && screen->w > 0 && screen->h > 0) {
        UI_LayoutDrawBackdrop2(frame, screen, backdrop);
    }
}

void UI_LayoutDrawScrollBar(LPCUIFRAME frame, LPCRECT screen) {
    uiScrollBar_t const *scrollbar = frame->buffer.data;
    FLOAT button_height;
    RECT inc;
    RECT dec;
    RECT track;
    RECT thumb;

    if (!scrollbar || screen->w <= 0 || screen->h <= 0) {
        return;
    }

    UI_LayoutDrawBackdropPart(frame, screen, &scrollbar->background);

    button_height = MIN(screen->w, screen->h * 0.5f);
    inc = MAKE(RECT, screen->x, screen->y + screen->h - button_height, screen->w, button_height);
    dec = MAKE(RECT, screen->x, screen->y, screen->w, button_height);
    track = MAKE(RECT, screen->x, dec.y + dec.h, screen->w, inc.y - (dec.y + dec.h));
    UI_LayoutDrawBackdropPart(frame, &inc, &scrollbar->incButton);
    UI_LayoutDrawBackdropPart(frame, &dec, &scrollbar->decButton);

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
    UI_LayoutDrawBackdropPart(frame, &thumb, &scrollbar->thumbButton);
}

static BOOL UI_LayoutFrameHasClickCommand(LPCUIFRAME frame) {
    return frame && frame->onclick && *frame->onclick;
}

static BOOL UI_LayoutGlueTextButtonIsPushed(LPCUIFRAME frame, LPCRECT screen) {
    VECTOR2 const m = UI_LayoutMouseToFdf();
    return UI_LayoutFrameHasClickCommand(frame) && Rect_contains(screen, &m) &&
           (frame->ui_flags & UIFLAG_PRESSED);
}

static void UI_LayoutFormatOnClickCommand(LPCSTR source, LPSTR dest, DWORD dest_size) {
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

void UI_LayoutGlueTextButton(LPCUIFRAME frame, LPCRECT screen) {
    uiGlueTextButton_t const *gluetextbutton = frame->buffer.data;
    BOOL const enabled = UI_LayoutFrameHasClickCommand(frame);
    uiBackdrop_t const *backdrop = &gluetextbutton->normal;
    if (!enabled) {
        backdrop = UI_LayoutGlueTextButtonIsPushed(frame, screen) ? &gluetextbutton->disabledPushed : &gluetextbutton->disabled;
    } else if (UI_LayoutGlueTextButtonIsPushed(frame, screen)) {
        backdrop = &gluetextbutton->pushed;
    }

    UI_LayoutDrawBackdrop2(frame, screen, backdrop);
}

static void UI_LayoutDrawGlueTextButtonHighlight(LPCUIFRAME frame) {
    uiGlueTextButton_t const *gluetextbutton = frame->buffer.data;
    RECT const *screen = UI_LayoutLayoutRect(frame);
    VECTOR2 const m = UI_LayoutMouseToFdf();
    BOOL const enabled = UI_LayoutFrameHasClickCommand(frame);
    BOOL const mouse_over = Rect_contains(screen, &m);

    if (enabled && mouse_over) {
        UI_LayoutDrawHighlightData(&gluetextbutton->highlight, screen);
    }
}

void UI_LayoutDrawBuildQueue(LPCUIFRAME frame, LPCRECT scrn) {
    RECT screen = *scrn;
    RECT const uv = { 0, 0, 1, 1 };
    uiBuildQueue_t const *queue = frame->buffer.data;
    DWORD active = queue->numitems;

    FOR_LOOP(i, queue->numitems) {
        if (UI_LayoutTime() < queue->items[i].endtime) {
            active = i;
            break;
        }
    }
    for (DWORD i = active + 1; i < queue->numitems; i++) {
        if (UI_LayoutTime() < queue->items[i].endtime) {
            re.DrawImage(UI_LayoutTexture(queue->items[i].image), &screen, &uv, frame->color);
            screen.x += queue->itemoffset;
        }
    }
}

void UI_LayoutUpdateBuildQueue(LPCUIFRAME frame, LPCRECT screen) {
    uiBuildQueue_t const *queue = frame->buffer.data;
    LPUIFRAME buildtimer = UI_LayoutFrame(queue->buildtimer);
    LPUIFRAME firstitem = UI_LayoutFrame(queue->firstitem);

    FOR_LOOP(i, queue->numitems) {
        uiBuildQueueItem_t const *item = &queue->items[i];
        if (UI_LayoutTime() < item->endtime) {
            FLOAT duration = item->endtime - item->starttime;
            FLOAT elapsed = UI_LayoutTime() > item->starttime ? (FLOAT)(UI_LayoutTime() - item->starttime) : 0;
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

void UI_LayoutDrawMultiSelect(LPCUIFRAME frame, LPCRECT scrn) {
    RECT screen = *scrn;
    uiMultiselect_t const *multiselect = frame->buffer.data;
    DWORD column = 0;
    FOR_LOOP(i, multiselect->numitems) {
        RECT uv = { 0, 0, 1, 1 };
        uiMultiselectItem_t const *item = &multiselect->items[i];
        re.DrawImage(UI_LayoutTexture(item->image), &screen, &uv, frame->color);
        LPCENTITYSTATE ent = uiimport.GetEntity ? uiimport.GetEntity(item->entity) : NULL;
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
            re.DrawImage(UI_LayoutTexture(multiselect->hp_bar), &rect, &uv, MAKE(COLOR32,0,255,0,255));
            uv.w = mana;
            rect.w = screen.w * mana;
            rect.y += screen.h * (HP_BAR_HEIGHT_RATIO + HP_BAR_SPACING_RATIO);
            re.DrawImage(UI_LayoutTexture(multiselect->mana_bar), &rect, &uv, MAKE(COLOR32,0,255,255,255));
        }
        if (++column >= multiselect->numcolumns) {
            column = 0;
            screen.x = UI_LayoutLayoutRect(frame)->x;
            screen.y += multiselect->offset.y;
        } else {
            screen.x += multiselect->offset.x;
        }
    }
}

void UI_LayoutDrawPortrait(LPCUIFRAME frame, LPCRECT screen) {
    RECT const viewport = {
        screen->x / UI_BASE_WIDTH,
        (UI_BASE_HEIGHT - screen->y - screen->h) / UI_BASE_HEIGHT,
        screen->w / UI_BASE_WIDTH,
        screen->h / UI_BASE_HEIGHT
    };
    LPCMODEL port = uiimport.GetPortrait ? uiimport.GetPortrait(frame->tex.index) : NULL;
    LPCMODEL model = uiimport.GetModel ? uiimport.GetModel(frame->tex.index) : NULL;
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

void UI_LayoutDrawSprite(LPCUIFRAME frame, LPCRECT screen) {
    LPCSTR anim = frame->text;
    LPCMODEL model = uiimport.GetModel ? uiimport.GetModel(frame->tex.index) : NULL;

    if (anim && *anim) {
        re.DrawSprite(model, anim, screen->x, screen->y);
    } else {
        re.DrawSprite(model, "Stand", screen->x, screen->y);
    }
}

void UI_LayoutDrawCommandButton(LPCUIFRAME frame, LPCRECT screen) {
    LPCENTITYSTATE selentity = UI_LayoutSelectedEntity();
    RECT const uv = get_uvrect(frame->tex.coord);
    RECT const suv = Rect_div(&uv, 0xff);
    VECTOR2 const m = UI_LayoutMouseToFdf();
    RECT scrn = scale_rect(screen, 0.925);
    if (Rect_contains(screen, &m)) {
        if (frame->ui_flags & UIFLAG_PRESSED) {
            scrn = scale_rect(screen, 0.875);
        }
    }
    re.DrawImageEx(&MAKE(drawImage_t,
                         .texture = UI_LayoutTexture(frame->tex.index),
                         .screen = scrn,
                         .uv = suv,
                         .color = COLOR32_WHITE,
                         .rotate = false,
                         .shader = SHADER_COMMANDBUTTON,
                         .uActiveGlow = selentity ? selentity->ability == frame->stat : 0));
    /* Cooldown shade: while an ability recharges, darken its icon in proportion
     * to the cooldown remaining (frame->value, 1=just used -> 0=ready), the WC3
     * "this ability is on cooldown" cue. */
    if (frame->value > 0.0f) {
        FLOAT const frac = MIN(frame->value, 1.0f);
        re.DrawImageEx(&MAKE(drawImage_t,
                             .texture = UI_LayoutTexture(frame->tex.index),
                             .screen = scrn,
                             .uv = suv,
                             .color = MAKE(COLOR32, 0, 0, 0, (BYTE)(110.0f + 130.0f * frac)),
                             .rotate = false,
                             .shader = SHADER_COMMANDBUTTON));
    }
}

void layout_text(LPCUIFRAME frame, LPCRECT screen, LPCSTR text) {
    drawText_t drawtext = UI_LayoutGetDrawText(frame, screen->w, text, frame->buffer.data);
    drawtext.rect = *screen;
    drawtext.wordWrap = true;
    re.DrawText(&drawtext);
}

static void UI_LayoutApplyPushedTextOffset(LPCUIFRAME frame, LPRECT screen) {
    if (frame->parent >= UI_LayoutNumFrames()) {
        return;
    }

    LPCUIFRAME parent = UI_LayoutFrame(frame->parent);
    if (!parent) {
        return;
    }
    if (parent->flags.type != FT_GLUETEXTBUTTON && parent->flags.type != FT_GLUEBUTTON) {
        return;
    }
    if (!UI_LayoutFrameHasClickCommand(parent)) {
        return;
    }

    LPCRECT parent_screen = UI_LayoutLayoutRect(parent);
    if (!UI_LayoutGlueTextButtonIsPushed(parent, parent_screen)) {
        return;
    }

    uiGlueTextButton_t const *button = parent->buffer.data;
    screen->x += button->pushedTextOffset.x;
    screen->y -= button->pushedTextOffset.y;
}

void UI_LayoutDrawString(LPCUIFRAME frame, LPCRECT screen) {
    uiLabel_t const *label = frame->buffer.data;
    RECT scr = *screen;
    scr.x += label->offsetx;
    scr.y += label->offsety;
    UI_LayoutApplyPushedTextOffset(frame, &scr);
    layout_text(frame, &scr, UI_LayoutGetStringValue(frame));
}

void UI_LayoutDrawTextArea(LPCUIFRAME frame, LPCRECT screen) {
    uiTextArea_t const *textArea = frame->buffer.data;
    RECT scr = {
        screen->x + textArea->inset,
        screen->y + textArea->inset,
        screen->w - textArea->inset * 2,
        screen->h - textArea->inset * 2,
    };
    re.DrawText(&MAKE(drawText_t,
                      .font = UI_LayoutFont(textArea->font),
                      .text = frame->text ? frame->text : "",
                      .color = frame->color.a ? frame->color : COLOR32_WHITE,
                      .halign = FONT_JUSTIFYLEFT,
                      .valign = FONT_JUSTIFYTOP,
                      .icons = UI_LayoutTextures(),
                      .lineHeight = 1.33,
                      .textWidth = scr.w,
                      .rect = scr,
                      .wordWrap = true));
}

void UI_LayoutDrawListBox(LPCUIFRAME frame, LPCRECT screen) {
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

    UI_LayoutDrawBackdrop2(frame, screen, &listbox->background);

    FOR_LOOP(i, UI_LayoutNumFrames()) {
        LPCUIFRAME child = UI_LayoutFrame(i);
        if (child && child->parent == frame->number && child->flags.type == FT_SCROLLBAR) {
            scrollbar = child;
            break;
        }
    }
    if (scrollbar) {
        LPCRECT scroll_rect = UI_LayoutLayoutRect(scrollbar);
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
            re.DrawImage(UI_LayoutTexture(0), &row, &MAKE(RECT, 0, 0, 1, 1), MAKE(COLOR32, 32, 64, 180, 128));
        }
        re.DrawText(&MAKE(drawText_t,
                          .font = UI_LayoutFont(listbox->text.font),
                          .text = display,
                          .color = frame->color.a ? frame->color : COLOR32_WHITE,
                          .halign = FONT_JUSTIFYLEFT,
                          .valign = FONT_JUSTIFYMIDDLE,
                          .icons = UI_LayoutTextures(),
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

void UI_LayoutDrawTooltip(LPCUIFRAME frame, LPCRECT scrn) {
    if (active_tooltip) {
        RECT screen = *scrn;
        uiTooltip_t const *tooltip = frame->buffer.data;
        FLOAT const PADDING = 0.005;
        FLOAT const avlspace = screen.w - PADDING * 2;
        drawText_t drawtext = UI_LayoutGetDrawText(frame, avlspace, active_tooltip, &tooltip->text);
        drawtext.wordWrap = true;
        VECTOR2 textsize = re.GetTextSize(&drawtext);
        textsize.y += PADDING * 2;
        screen.y += screen.h - textsize.y;
        screen.h = textsize.y;
        RECT text = Rect_inset(&screen, PADDING);
        UI_LayoutDrawBackdrop(frame, &screen);
        drawtext = UI_LayoutGetDrawText(frame, text.w, active_tooltip, &tooltip->text);
        drawtext.rect = text;
        drawtext.wordWrap = true;
        re.DrawText(&drawtext);
    }
}

void UI_LayoutUpdateCommandButton(LPCUIFRAME frame, LPCRECT screen) {
    VECTOR2 const m = UI_LayoutMouseToFdf();
    if (Rect_contains(screen, &m) && frame->tooltip) {
        active_tooltip = frame->tooltip;
    }
}

typedef struct {
    FRAMETYPE type;
    void (*func)(LPCUIFRAME, LPCRECT);
} drawer_t;

static drawer_t updaters[] = {
    { FT_COMMANDBUTTON, UI_LayoutUpdateCommandButton },
    { FT_BUILDQUEUE, UI_LayoutUpdateBuildQueue },
};

static drawer_t drawers[] = {
    { FT_TEXTURE, UI_LayoutDrawTexture },
    { FT_HIGHLIGHT, UI_LayoutDrawHighlight },
    { FT_BACKDROP, UI_LayoutDrawBackdrop },
    { FT_SIMPLESTATUSBAR, UI_LayoutDrawStatusbar },
    { FT_COMMANDBUTTON, UI_LayoutDrawCommandButton },
    { FT_STRING, UI_LayoutDrawString },
    { FT_TEXT, UI_LayoutDrawString },
    { FT_TEXTAREA, UI_LayoutDrawTextArea },
    { FT_LISTBOX, UI_LayoutDrawListBox },
    { FT_SCROLLBAR, UI_LayoutDrawScrollBar },
    { FT_TOOLTIPTEXT, UI_LayoutDrawTooltip },
    { FT_MODEL, UI_LayoutDrawPortrait },
    { FT_SPRITE, UI_LayoutDrawSprite },
    { FT_PORTRAIT, UI_LayoutDrawPortrait },
    { FT_BUILDQUEUE, UI_LayoutDrawBuildQueue },
    { FT_MULTISELECT, UI_LayoutDrawMultiSelect },
    { FT_SIMPLEBUTTON, UI_LayoutSimpleButton },
    { FT_BUTTON, UI_LayoutGlueTextButton },
    { FT_TEXTBUTTON, UI_LayoutGlueTextButton },
    { FT_POPUPMENU, UI_LayoutGlueTextButton },
    { FT_GLUEPOPUPMENU, UI_LayoutGlueTextButton },
    { FT_GLUETEXTBUTTON, UI_LayoutGlueTextButton },
    { FT_GLUEBUTTON, UI_LayoutGlueTextButton },
//    { FT_NONE, NULL },
};

void UI_LayoutDrawFrame(LPCUIFRAME frame) {
    VECTOR2 const m = UI_LayoutMouseToFdf();
    RECT const *screen = UI_LayoutLayoutRect(frame);
    FOR_LOOP(j, sizeof(drawers)/sizeof(*drawers)) {
        if (drawers[j].type == frame->flags.type) {
            drawers[j].func(frame, screen);
            break;
        }
    }
    if (Rect_contains(screen, &m) &&
        UI_LayoutFrameHasClickCommand(frame))
    {
        char command[CMDARG_LEN * 2];

        UI_LayoutFormatOnClickCommand(frame->onclick, command, sizeof(command));
        if (uiimport.ServerCommand) {
            uiimport.ServerCommand(command);
        }
    }
}

void UI_LayoutUpdateFrame(LPCUIFRAME frame) {
    RECT const *screen = UI_LayoutLayoutRect(frame);
    FOR_LOOP(j, sizeof(updaters)/sizeof(*updaters)) {
        if (updaters[j].type == frame->flags.type) {
            updaters[j].func(frame, screen);
            break;
        }
    }
}

void UI_LayoutUpdateTooltip(HANDLE _frames) {
    FOR_LOOP(i, UI_LayoutNumFrames()) {
        LPCUIFRAME frame = UI_LayoutFrame(i);
        if (frame) {
            UI_LayoutUpdateFrame(frame);
        }
    }
}

void UI_LayoutDrawOverlay(HANDLE _frames) {
    FOR_LOOP(i, UI_LayoutNumFrames()) {
        LPCUIFRAME frame = UI_LayoutFrame(i);
        if (frame && frame->flags.type == FT_SPRITE) {
            UI_LayoutDrawFrame(frame);
        }
    }
    FOR_LOOP(i, UI_LayoutNumFrames()) {
        LPCUIFRAME frame = UI_LayoutFrame(i);
        if (frame && frame->flags.type != FT_SPRITE) {
            UI_LayoutDrawFrame(frame);
        }
    }
    FOR_LOOP(i, UI_LayoutNumFrames()) {
        LPCUIFRAME frame = UI_LayoutFrame(i);
        if (frame && (frame->flags.type == FT_GLUETEXTBUTTON || frame->flags.type == FT_GLUEBUTTON)) {
            UI_LayoutDrawGlueTextButtonHighlight(frame);
        }
    }
}

void UI_LayoutDrawOverlays(void) {
    active_tooltip = NULL;
    
    if (UI_LayoutPlayerState()->cinefade > 0) {
        COLOR32 color = COLOR32_BLACK;
        color.a = 255 * UI_LayoutPlayerState()->cinefade;
        re.DrawImage(UI_LayoutTexture(0), &MAKE(RECT,0,0,1,1), &MAKE(RECT,0,0,1,1), color);
    }
    
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        if ((1 << layer) & UI_LayoutPlayerState()->uiflags)
            continue;
        if (UI_LayoutShouldSkipLayoutLayer(layer))
            continue;
        HANDLE layout = layout_layers[layer];
        if (layout) {
            UI_LayoutClear(layout);
            UI_LayoutUpdateTooltip(layout);
        }
    }
    
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        if ((1 << layer) & UI_LayoutPlayerState()->uiflags)
            continue;
        if (UI_LayoutShouldSkipLayoutLayer(layer))
            continue;
        HANDLE layout = layout_layers[layer];
        if (layout) {
            UI_LayoutClear(layout);
            UI_LayoutUpdateTooltip(layout);
            UI_LayoutDrawOverlay(layout);
        }
    }
}

void UI_LayoutSetLayer(DWORD layer, HANDLE data) {
    if (layer >= MAX_LAYOUT_LAYERS) {
        return;
    }
    layout_layers[layer] = data;
}

void UI_LayoutClearLayer(DWORD layer) {
    if (layer >= MAX_LAYOUT_LAYERS) {
        return;
    }
    layout_layers[layer] = NULL;
}

BOOL UI_LayoutHitTest(int x, int y) {
    VECTOR2 const point = UI_LayoutScreenToFdf(x, y);

    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        HANDLE layout = layout_layers[layer];

        if (!layout) {
            continue;
        }
        if ((1 << layer) & UI_LayoutPlayerState()->uiflags) {
            continue;
        }
        if (UI_LayoutShouldSkipLayoutLayer(layer)) {
            continue;
        }
        UI_LayoutClear(layout);
        FOR_LOOP(i, UI_LayoutNumFrames()) {
            LPCUIFRAME frame = UI_LayoutFrame(i);
            if (!frame || frame->flags.type != FT_TEXTURE) {
                continue;
            }
            if (Rect_contains(UI_LayoutLayoutRect(frame), &point)) {
                return true;
            }
        }
    }
    return false;
}

void UI_LayoutSetMouseState(VECTOR2 fdf_pos, BOOL button_held) {
    layout_last_mouse = fdf_pos;
}
