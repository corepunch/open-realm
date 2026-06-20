/*
 * cl_layout_draw.c — Client-side layout frame rendering
 *
 * Draws server-authored HUD layout frames directly in the client.
 * No DLL involvement — the client owns layout frame storage, deserialization,
 * rect computation, and rendering.
 */
#include "client.h"

#define HP_BAR_HEIGHT_RATIO   0.15f
#define HP_BAR_SPACING_RATIO  0.05f
#define SCROLLBAR_WIDTH       0.004f

/* Texture helpers */
static LPCTEXTURE CL_LayoutTexture(DWORD index) {
    return index < MAX_IMAGES ? cl.pics[index] : NULL;
}

static LPCTEXTURE *CL_LayoutTextures(void) {
    return cl.pics;
}

static LPCFONT CL_LayoutFont(DWORD index) {
    return index < MAX_FONTSTYLES ? cl.fonts[index] : NULL;
}

static LPCPLAYER CL_LayoutPlayerState(void) {
    return &cl.playerstate;
}

static DWORD CL_LayoutTime(void) {
    return cl.time;
}

static BOOL CL_LayoutShouldSkipLayoutLayer(DWORD layer) {
    DWORD state = CL_LayoutPlayerState()->client_ui_state;
    return (state == CLIENT_UI_CINEMATIC || state == CLIENT_UI_LOADING) && layer > 0;
}

/* Format onclick command with {param} interpolation */
static void CL_LayoutFormatOnClickCommand(LPCSTR source, LPSTR dest, DWORD dest_size) {
    DWORD out = 0;
    if (!dest || dest_size == 0) return;
    dest[0] = '\0';
    if (!source) return;
    for (DWORD i = 0; source[i] && out + 1 < dest_size; i++) {
        if (source[i] == '{' && source[i + 1] == 'p' && source[i + 2] == 'a' &&
            source[i + 3] == 'r' && source[i + 4] == 'a' && source[i + 5] == 'm' &&
            source[i + 6] == '}') {
            dest[out++] = '%'; dest[out++] = 'u'; i += 6;
        } else {
            dest[out++] = source[i];
        }
    }
    dest[out] = '\0';
}

static LPCSTR CL_LayoutStringValue(LPCUIFRAME frame) {
    if (frame->stat >= MAX_STATS) {
        if (cl.playerstate.texts[frame->stat - MAX_STATS]) {
            return cl.playerstate.texts[frame->stat - MAX_STATS];
        }
    }
    return frame->text ? frame->text : "";
}

static drawText_t CL_LayoutGetDrawText(LPCUIFRAME frame, FLOAT avl_width, LPCSTR text, void const *label_data) {
    return SCR_GetDrawText(frame, avl_width, text, label_data);
}

/* Dynamic texture cache (for texture-over-time effects) */
static struct { LPCSTR name; LPCTEXTURE tex; DWORD frame; } dynamic_textures[64];

static LPCTEXTURE CL_LayoutGetDynamicTexture(LPCSTR name) {
    if (!name || !*name) return NULL;
    FOR_LOOP(i, 64) {
        if (dynamic_textures[i].name && !strcmp(dynamic_textures[i].name, name)) {
            return dynamic_textures[i].tex;
        }
    }
    FOR_LOOP(i, 64) {
        if (!dynamic_textures[i].name) {
            dynamic_textures[i].name = name;
            dynamic_textures[i].tex = re.LoadTexture(name);
            return dynamic_textures[i].tex;
        }
    }
    return NULL;
}

/* Selected entity for command button glow */
static LPCENTITYSTATE CL_LayoutSelectedEntity(void) {
    FOR_LOOP(i, cl.num_entities) {
        if (cl.ents[i].current.flags & RF_SELECTED) {
            return &cl.ents[i].current;
        }
    }
    return NULL;
}

/* Backdrop drawing (background + 8-piece border) */
static RECT CL_GetUVRect(BYTE const *coord) {
    return MAKE(RECT, coord[0], coord[1], coord[2] - coord[0], coord[3] - coord[1]);
}

static void CL_LayoutDrawBackdrop2(LPCUIFRAME frame, LPCRECT screen, void const *data) {
    uiBackdrop_t const *bd = data;
    if (!bd) return;
    LPCTEXTURE bg_tex = bd->Background ? CL_LayoutTexture(bd->Background) : NULL;
    if (bg_tex) {
        RECT uv = {0, 0, 1, 1};
        if (bd->TileBackground) {
            size2_t ts = re.GetTextureSize(bg_tex);
            if (ts.width > 0 && ts.height > 0) {
                uv.w = screen->w / (ts.width * UI_BASE_WIDTH * 0.001f);
                uv.h = screen->h / (ts.height * UI_BASE_HEIGHT * 0.001f);
            }
        }
        re.DrawImageEx(&MAKE(drawImage_t, .texture = bg_tex, .screen = *screen, .uv = uv,
                             .color = COLOR32_WHITE, .shader = SHADER_UI));
    }
    LPCTEXTURE edge_tex = bd->EdgeFile ? CL_LayoutTexture(bd->EdgeFile) : NULL;
    if (edge_tex) {
        FLOAT edge_size = bd->CornerSize > 0 ? bd->CornerSize : 0.01f;
        RECT edges[4] = {
            { screen->x, screen->y, screen->w, edge_size },
            { screen->x, screen->y + screen->h - edge_size, screen->w, edge_size },
            { screen->x, screen->y, edge_size, screen->h },
            { screen->x + screen->w - edge_size, screen->y, edge_size, screen->h },
        };
        RECT edge_uvs[4] = {
            {0, 0, 1, 0.25f}, {0, 0.75f, 1, 1}, {0, 0, 0.25f, 1}, {0.75f, 0, 1, 1}
        };
        FOR_LOOP(i, 4) {
            re.DrawImageEx(&MAKE(drawImage_t, .texture = edge_tex, .screen = edges[i], .uv = edge_uvs[i],
                                 .color = COLOR32_WHITE, .shader = SHADER_UI));
        }
    }
}

static void CL_LayoutDrawBackdrop(LPCUIFRAME frame, LPCRECT screen) {
    CL_LayoutDrawBackdrop2(frame, screen, frame->buffer.data);
}

/* Per-type drawer functions */
static void CL_LayoutDrawTexture(LPCUIFRAME frame, LPCRECT screen) {
    RECT uv = CL_GetUVRect(frame->tex.coord);
    RECT suv = { uv.x * 255, uv.y * 255, uv.w * 255, uv.h * 255 };
    LPCTEXTURE texture = CL_LayoutTexture(frame->tex.index);
    if (frame->stat >= MAX_STATS && frame->stat - MAX_STATS < MAX_STATS) {
        LPCSTR resource = CL_LayoutPlayerState()->texts[frame->stat - MAX_STATS];
        LPCTEXTURE dyn = CL_LayoutGetDynamicTexture(resource);
        if (dyn) texture = dyn;
    }
    re.DrawImage(texture, screen, &suv, frame->color);
}

static void CL_LayoutDrawHighlightData(uiHighlight_t const *hl, LPCRECT screen) {
    if (!hl || !hl->alphaFile) return;
    re.DrawImageEx(&MAKE(drawImage_t, .texture = CL_LayoutTexture(hl->alphaFile),
                         .screen = *screen, .uv = MAKE(RECT, 0, 0, 1, 1),
                         .color = COLOR32_WHITE, .alphamode = hl->alphaMode,
                         .shader = SHADER_UI));
}

static void CL_LayoutDrawHighlight(LPCUIFRAME frame, LPCRECT screen) {
    CL_LayoutDrawHighlightData(frame->buffer.data, screen);
}

static void CL_LayoutDrawStatusbar(LPCUIFRAME frame, LPCRECT screen) {
    RECT uv = {0, 0, 255, 255};
    RECT screen2 = *screen;
    RECT uv2 = uv;
    screen2.w *= frame->value;
    uv2.w *= frame->value;
    RECT suv2 = { uv2.x, uv2.y, uv2.w, uv2.h };
    re.DrawImage(CL_LayoutTexture(frame->tex.index), &screen2, &suv2, frame->color);
    if (frame->tex.index2 > 0) {
        re.DrawImage(CL_LayoutTexture(frame->tex.index2), screen, &uv, COLOR32_WHITE);
    }
}

static void layout_text(LPCUIFRAME frame, LPCRECT screen, LPCSTR text) {
    drawText_t drawtext = CL_LayoutGetDrawText(frame, screen->w, text, frame->buffer.data);
    drawtext.rect = *screen;
    drawtext.wordWrap = true;
    re.DrawText(&drawtext);
}

static void CL_LayoutApplyPushedTextOffset(LPCUIFRAME frame, RECT *scr) {
    /* Simplified: skip pushed offset for now */
    (void)frame; (void)scr;
}

static void CL_LayoutDrawString(LPCUIFRAME frame, LPCRECT screen) {
    uiLabel_t const *label = frame->buffer.data;
    RECT scr = *screen;
    scr.x += label->offsetx;
    scr.y += label->offsety;
    CL_LayoutApplyPushedTextOffset(frame, &scr);
    layout_text(frame, &scr, CL_LayoutStringValue(frame));
}

static void CL_LayoutDrawTextArea(LPCUIFRAME frame, LPCRECT screen) {
    uiTextArea_t const *ta = frame->buffer.data;
    RECT scr = { screen->x + ta->inset, screen->y + ta->inset,
                 screen->w - ta->inset * 2, screen->h - ta->inset * 2 };
    re.DrawText(&MAKE(drawText_t,
                      .font = CL_LayoutFont(ta->font),
                      .text = frame->text ? frame->text : "",
                      .color = frame->color,
                      .halign = FONT_JUSTIFYLEFT,
                      .valign = FONT_JUSTIFYTOP,
                      .icons = CL_LayoutTextures(),
                      .lineHeight = 1.33f,
                      .textWidth = scr.w,
                      .rect = scr,
                      .wordWrap = true));
}

static void CL_LayoutDrawTooltip(LPCUIFRAME frame, LPCRECT scrn) {
    /* Tooltip rendering — simplified for now */
    (void)frame; (void)scrn;
}

static void CL_LayoutDrawPortrait(LPCUIFRAME frame, LPCRECT screen) {
    LPCMODEL model = cl.portraits[frame->tex.index];
    if (!model) model = cl.models[frame->tex.index];
    if (!model) return;
    renderEntity_t entity = {0};
    entity.model = model;
    entity.flags = RF_NO_SHADOW | RF_NO_FOGOFWAR | RF_PORTRAIT_LIGHTING;
    re.SetEntityAnimFrame(model, "Stand", &entity);
    viewDef_t viewdef = {0};
    viewdef.viewport = *screen;
    viewdef.rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG | RDF_USE_ENTITY_CAMERA;
    viewdef.num_entities = 1;
    viewdef.entities = &entity;
    re.RenderFrame(&viewdef);
}

static void CL_LayoutDrawSprite(LPCUIFRAME frame, LPCRECT screen) {
    LPCSTR anim = frame->text;
    LPCMODEL model = cl.models[frame->tex.index];
    re.DrawSprite(model, anim && *anim ? anim : "Stand", screen->x, screen->y);
}

static void CL_LayoutDrawSimpleButton(LPCUIFRAME frame, LPCRECT screen) {
    uiSimpleButton_t *button = frame->buffer.data;
    LPCSTR label = frame->text;
    RECT uv = CL_GetUVRect((BYTE *)&button->normal.texcoord);
    RECT suv = { uv.x * 255, uv.y * 255, uv.w * 255, uv.h * 255 };
    re.DrawImage(CL_LayoutTexture(button->normal.texture), screen, &suv, COLOR32_WHITE);
    re.DrawText(&MAKE(drawText_t, .rect = *screen,
                      .font = CL_LayoutFont(button->normal.font),
                      .text = label,
                      .color = button->normal.fontcolor,
                      .textWidth = screen->w));
}

static void CL_LayoutDrawBuildQueue(LPCUIFRAME frame, LPCRECT scrn) {
    RECT screen = *scrn;
    RECT uv = {0, 0, 1, 1};
    uiBuildQueue_t const *queue = frame->buffer.data;
    DWORD active = queue->numitems;
    FOR_LOOP(i, queue->numitems) {
        if (CL_LayoutTime() < queue->items[i].endtime) { active = i; break; }
    }
    for (DWORD i = active + 1; i < queue->numitems; i++) {
        if (CL_LayoutTime() < queue->items[i].endtime) {
            re.DrawImage(CL_LayoutTexture(queue->items[i].image), &screen, &uv, frame->color);
            screen.x += queue->itemoffset;
        }
    }
}

static void CL_LayoutDrawMultiSelect(LPCUIFRAME frame, LPCRECT scrn) {
    RECT screen = *scrn;
    uiMultiselect_t const *ms = frame->buffer.data;
    FOR_LOOP(i, ms->numitems) {
        RECT uv = {0, 0, 1, 1};
        uiMultiselectItem_t const *item = &ms->items[i];
        re.DrawImage(CL_LayoutTexture(item->image), &screen, &uv, frame->color);
        LPCENTITYSTATE ent = cl.ents[item->entity].current.number ? &cl.ents[item->entity].current : NULL;
        if (ent) {
            FLOAT health = BYTE2FLOAT(ent->stats[ENT_HEALTH]);
            FLOAT mana = BYTE2FLOAT(ent->stats[ENT_MANA]);
            RECT rect = { screen.x, screen.y + screen.h * (1 + HP_BAR_SPACING_RATIO),
                          screen.w * health, screen.h * HP_BAR_HEIGHT_RATIO };
            uv.w = health;
            re.DrawImage(CL_LayoutTexture(ms->hp_bar), &rect, &uv, MAKE(COLOR32,0,255,0,255));
            uv.w = mana;
            rect.w = screen.w * mana;
            rect.y += screen.h * (HP_BAR_HEIGHT_RATIO + HP_BAR_SPACING_RATIO);
            re.DrawImage(CL_LayoutTexture(ms->mana_bar), &rect, &uv, MAKE(COLOR32,0,255,255,255));
        }
        screen.x += screen.w;
    }
}

/* Frame draw dispatch */
typedef void (*layout_draw_fn)(LPCUIFRAME, LPCRECT);
static struct { FRAMETYPE type; layout_draw_fn func; } cl_drawers[] = {
    { FT_TEXTURE,        CL_LayoutDrawTexture },
    { FT_HIGHLIGHT,      CL_LayoutDrawHighlight },
    { FT_BACKDROP,       CL_LayoutDrawBackdrop },
    { FT_SIMPLESTATUSBAR, CL_LayoutDrawStatusbar },
    { FT_STRING,         CL_LayoutDrawString },
    { FT_TEXT,           CL_LayoutDrawString },
    { FT_TEXTAREA,       CL_LayoutDrawTextArea },
    { FT_TOOLTIPTEXT,    CL_LayoutDrawTooltip },
    { FT_MODEL,          CL_LayoutDrawPortrait },
    { FT_PORTRAIT,       CL_LayoutDrawPortrait },
    { FT_SPRITE,         CL_LayoutDrawSprite },
    { FT_SIMPLEBUTTON,   CL_LayoutDrawSimpleButton },
    { FT_BUILDQUEUE,     CL_LayoutDrawBuildQueue },
    { FT_MULTISELECT,    CL_LayoutDrawMultiSelect },
};

static void CL_LayoutDrawFrame(LPCUIFRAME frame) {
    RECT const *screen = SCR_LayoutRect(frame);
    FOR_LOOP(j, sizeof(cl_drawers) / sizeof(cl_drawers[0])) {
        if (cl_drawers[j].type == frame->flags.type) {
            cl_drawers[j].func(frame, screen);
            return;
        }
    }
}

static void CL_LayoutDrawOverlay(void) {
    /* Pass 1: sprites (behind everything) */
    FOR_LOOP(i, SCR_NumFrames()) {
        LPCUIFRAME frame = SCR_Frame(i);
        if (frame && frame->flags.type == FT_SPRITE) {
            CL_LayoutDrawFrame(frame);
        }
    }
    /* Pass 2: all non-sprite types */
    FOR_LOOP(i, SCR_NumFrames()) {
        LPCUIFRAME frame = SCR_Frame(i);
        if (frame && frame->flags.type != FT_SPRITE) {
            CL_LayoutDrawFrame(frame);
        }
    }
}

/* Entry point — called from SCR_UpdateScreen */
void CL_LayoutDrawOverlays(void) {
    LPCPLAYER ps = CL_LayoutPlayerState();

    /* Cinefade blackout */
    if (ps->cinefade > 0) {
        COLOR32 color = COLOR32_BLACK;
        color.a = 255 * ps->cinefade;
        re.DrawImage(CL_LayoutTexture(0), &MAKE(RECT, 0, 0, 1, 1), &MAKE(RECT, 0, 0, 1, 1), color);
    }

    /* Draw each layout layer */
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        if ((1 << layer) & ps->uiflags) continue;
        if (CL_LayoutShouldSkipLayoutLayer(layer)) continue;
        if (!cl.layout[layer]) continue;
        SCR_Clear(cl.layout[layer]);
        CL_LayoutDrawOverlay();
    }
}
