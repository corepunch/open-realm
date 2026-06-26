/*
 * g_ui.c — Server-authored WoW HUD via svc_layout.
 *
 * Reproduces the classic WoW 1.12 HUD layout (action bar, targeting frame,
 * minimap, copper) using the actual WoW assets and pixel positions from the
 * virtual 1024×768 canvas, exactly matching what ui.dll rendered before
 * in-game UI was moved server-side.
 */

#include "g_wow_local.h"

#define VW 1024.0f
#define VH 768.0f
#define PX(x) ((x) / VW)
#define PY(y) ((y) / VH)
#define PW(w) ((w) / VW)
#define PH(h) ((h) / VH)
#define UV(v) ((BYTE)((v) * 255.0f + 0.5f))

#define HUD_FONT_SIZE 10

static DWORD ui_next_frame_number;

static void UI_SetFramePoint(uiFramePoint_t *point, uiFramePointPos_t target, DWORD relative, FLOAT offset, BOOL y_axis) {
    point->used = 1;
    point->targetPos = target;
    point->relativeTo = (BYTE)relative;
    point->offset = (SHORT)((y_axis ? -offset : offset) * UI_FRAMEPOINT_SCALE);
}

static void UI_SetFrameRect(LPUIFRAME frame, FLOAT x, FLOAT y, FLOAT w, FLOAT h) {
    UI_SetFramePoint(&frame->points.x[FPP_MIN], FPP_MIN, 0, x, false);
    UI_SetFramePoint(&frame->points.y[FPP_MIN], FPP_MIN, 0, y, true);
    frame->size.width = w;
    frame->size.height = h;
}

static void UI_WriteProxyFrame(LPUIFRAME frame, HANDLE data, DWORD data_size) {
    frame->number = ui_next_frame_number++;
    frame->parent = 0;
    frame->color = frame->color.a ? frame->color : COLOR32_WHITE;
    frame->tex.coord[1] = 0xff;
    frame->tex.coord[3] = 0xff;
    frame->buffer.data = data;
    frame->buffer.size = data_size;
    gi.Write(PF_UIFRAME, frame);
}

static void UI_WriteTextFrame(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR text, COLOR32 color, uiFontJustificationH_t align) {
    uiFrame_t frame;
    uiLabel_t label;

    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    frame.flags.type = FT_STRING;
    frame.text = text;
    frame.color = color;
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", HUD_FONT_SIZE);
    label.textalignx = align;
    label.textaligny = FONT_JUSTIFYTOP;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

/* Write an FT_TEXTURE frame; uv = {l, r, t, b} in [0,1] */
static void UI_WriteImageUV(LPCSTR path, FLOAT x, FLOAT y, FLOAT w, FLOAT h,
                            FLOAT l, FLOAT r, FLOAT t, FLOAT b, COLOR32 color) {
    uiFrame_t frame;

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_TEXTURE;
    frame.color = color;
    frame.tex.index = gi.ImageIndex(path);
    frame.tex.coord[0] = UV(l);
    frame.tex.coord[1] = UV(r);
    frame.tex.coord[2] = UV(t);
    frame.tex.coord[3] = UV(b);
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

static void UI_WriteImage(LPCSTR path, FLOAT x, FLOAT y, FLOAT w, FLOAT h, COLOR32 color) {
    UI_WriteImageUV(path, x, y, w, h, 0.0f, 1.0f, 0.0f, 1.0f, color);
}

/* Solid-color quad via a null texture slot */
static void UI_WriteColorRect(FLOAT x, FLOAT y, FLOAT w, FLOAT h, COLOR32 color) {
    uiFrame_t frame;

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_TEXTURE;
    frame.color = color;
    frame.tex.index = 0;
    frame.tex.coord[1] = 0xff;
    frame.tex.coord[3] = 0xff;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

/* Solid health/mana bar drawn as two color rects (dark background + colored fill) */
static void UI_WriteColorBar(FLOAT x, FLOAT y, FLOAT w, FLOAT h,
                             FLOAT value, FLOAT maxvalue,
                             COLOR32 fill_color) {
    FLOAT p = maxvalue > 0.0f ? value / maxvalue : 0.0f;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    UI_WriteColorRect(x, y, w, h, MAKE(COLOR32, 12, 10, 8, 220));
    if (p > 0.0f)
        UI_WriteColorRect(x + PW(2), y + PH(2), (w - PW(4)) * p, h - PH(4), fill_color);
}

/* Minimap: border image + actual minimap viewport */
static void UI_WriteMinimapFrames(void) {
    uiFrame_t minimap;

    /* Minimap border overlay */
    UI_WriteImage("Interface\\Minimap\\UI-Minimap-Border.blp",
                  PX(879), PY(8), PW(128), PH(128), COLOR32_WHITE);

    /* Minimap viewport — FT_MINIMAP; client calls DrawMinimap() for this rect. */
    memset(&minimap, 0, sizeof(minimap));
    minimap.flags.type = FT_MINIMAP;
    minimap.color = COLOR32_WHITE;
    UI_SetFrameRect(&minimap, PX(896), PY(25), PW(91), PH(91));
    UI_WriteProxyFrame(&minimap, NULL, 0);
}

/* Main action bar: four 256×53 strips + two end-caps from UI-MainMenuBar-Dwarf.blp */
static void UI_WriteActionBar(void) {
    static LPCSTR const bar = "Interface\\MainMenuBar\\UI-MainMenuBar-Dwarf.blp";
    static LPCSTR const cap = "Interface\\MainMenuBar\\UI-MainMenuBar-EndCap-Dwarf.blp";
    /* Each strip covers a different vertical slice of the texture (v slices at 53/256 intervals) */
    static FLOAT const strips[4][4] = {
        /* {l, r, t, b}, screen x starts at 0 */
        { 0.0f, 1.0f, 0.79296875f, 1.0f },
        { 0.0f, 1.0f, 0.54296875f, 0.75f },
        { 0.0f, 1.0f, 0.29296875f, 0.5f },
        { 0.0f, 1.0f, 0.04296875f, 0.25f },
    };

    FOR_LOOP(i, 4)
        UI_WriteImageUV(bar,
                        PX((FLOAT)(i * 256)), PY(715), PW(256), PH(53),
                        strips[i][0], strips[i][1], strips[i][2], strips[i][3],
                        COLOR32_WHITE);

    /* Left end-cap (normal orientation) */
    UI_WriteImage(cap, PX(-96), PY(640), PW(128), PH(128), COLOR32_WHITE);
    /* Right end-cap (horizontally flipped: l=1, r=0) */
    UI_WriteImageUV(cap, PX(992), PY(640), PW(128), PH(128),
                    1.0f, 0.0f, 0.0f, 1.0f, COLOR32_WHITE);
}

/* Action button slot at grid position i (0..11 = left row, 12..15 = right empty slots) */
static void UI_WriteActionButtonSlot(FLOAT x, FLOAT y, DWORD image_index) {
    /* Slot frame */
    UI_WriteImage("Interface\\Buttons\\UI-Quickslot2.blp",
                  x + PX(-14), y + PY(-13), PW(64), PH(64), COLOR32_WHITE);
    /* Icon (may be 0 = empty slot, renderer draws nothing for index 0) */
    if (image_index) {
        uiFrame_t icon;
        memset(&icon, 0, sizeof(icon));
        icon.flags.type = FT_TEXTURE;
        icon.color = COLOR32_WHITE;
        icon.tex.index = image_index;
        icon.tex.coord[1] = 0xff;
        icon.tex.coord[3] = 0xff;
        UI_SetFrameRect(&icon, x + PX(2), y + PY(2), PW(32), PH(32));
        UI_WriteProxyFrame(&icon, NULL, 0);
    }
}

/* Targeting frame: the WoW character frame backdrop + health/mana bars + name/level text */
static void UI_WriteTargetingFrame(LPEDICT ent) {
    LPPLAYER ps = &ent->client->ps;
    char name_buf[64], level_buf[32];

    /* Character frame backdrop — drawn with a slight tint matching the original */
    UI_WriteImageUV("Interface\\TargetingFrame\\UI-TargetingFrame.blp",
                    PX(-19), PY(4), PW(232), PH(100),
                    1.0f, 0.09375f, 0.0f, 0.78125f,
                    MAKE(COLOR32, 96, 92, 84, 230));

    /* Dark name area */
    UI_WriteColorRect(PX(87), PY(22), PW(119), PH(41), MAKE(COLOR32, 0, 0, 0, 128));

    /* Name */
    snprintf(name_buf, sizeof(name_buf), "%s",
             ps->name && *ps->name ? ps->name : "Player");
    UI_WriteTextFrame(PX(72), PY(18), PW(100), PH(12),
                      name_buf, MAKE(COLOR32, 255, 215, 120, 255), FONT_JUSTIFYCENTER);

    /* Level */
    snprintf(level_buf, sizeof(level_buf), "Lvl %d", (int)ps->stats[WOW_STAT_LEVEL]);
    UI_WriteTextFrame(PX(24), PY(58), PW(42), PH(12),
                      level_buf, MAKE(COLOR32, 235, 225, 190, 255), FONT_JUSTIFYCENTER);

    /* Health bar */
    UI_WriteColorBar(PX(105), PY(41), PW(119), PH(12),
                     (FLOAT)ps->stats[WOW_STAT_HEALTH], (FLOAT)ps->stats[WOW_STAT_HEALTH_MAX],
                     MAKE(COLOR32, 20, 178, 48, 235));

    /* Mana/power bar */
    UI_WriteColorBar(PX(105), PY(54), PW(119), PH(11),
                     (FLOAT)ps->stats[WOW_STAT_POWER], (FLOAT)ps->stats[WOW_STAT_POWER_MAX],
                     MAKE(COLOR32, 26, 82, 210, 235));
}

/* Build and unicast the WoW HUD layer for a player */
void UI_WriteWowHud(LPEDICT ent) {
    LPPLAYER ps;
    wowClient_t *wc;
    char copper_buf[64];

    if (!ent || !ent->client)
        return;
    ps = &ent->client->ps;
    wc = (wowClient_t *)ent->client;

    gi.Write(PF_BYTE, &(LONG){svc_layout});
    gi.Write(PF_BYTE, &(LONG){LAYER_CONSOLE});
    ui_next_frame_number = 1;

    /* Character/targeting frame (portrait area top-left) */
    UI_WriteTargetingFrame(ent);

    /* Main action bar + end-caps */
    UI_WriteActionBar();

    /* 12 action buttons, left row */
    FOR_LOOP(i, 12) {
        DWORD img = wc->actions[i].icon[0] ? gi.ImageIndex(wc->actions[i].icon) : 0;
        UI_WriteActionButtonSlot(PX(8.0f + (FLOAT)i * 42.0f), PY(728), img);
    }

    /* 4 empty button slots, right side */
    FOR_LOOP(i, 4)
        UI_WriteActionButtonSlot(PX(939.0f - (FLOAT)i * 42.0f), PY(728), 0);

    /* Backpack */
    UI_WriteImage("Interface\\Buttons\\Button-Backpack-Up.blp",
                  PX(981), PY(729), PW(37), PH(37), COLOR32_WHITE);

    /* Minimap border + viewport */
    UI_WriteMinimapFrames();

    /* Quest log icon + label */
    UI_WriteImage("Interface\\QuestFrame\\UI-QuestLog-BookIcon.blp",
                  PX(840), PY(162), PW(32), PH(32), COLOR32_WHITE);
    UI_WriteTextFrame(PX(876), PY(164), PW(110), PH(20),
                      "Quests", MAKE(COLOR32, 255, 215, 120, 255), FONT_JUSTIFYLEFT);

    /* Copper display */
    snprintf(copper_buf, sizeof(copper_buf), "Copper %d", (int)ps->stats[WOW_STAT_COPPER]);
    UI_WriteTextFrame(PX(816), PY(704), PW(150), PH(20),
                      copper_buf, MAKE(COLOR32, 255, 210, 100, 255), FONT_JUSTIFYRIGHT);

    gi.Write(PF_LONG, &(LONG){0});
    gi.Write(PF_SHORT, &(LONG){0});
    gi.unicast(ent);
}
