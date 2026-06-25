/*
 * g_ui.c — Server-authored WoW HUD via svc_layout.
 *
 * Generates compact uiFrame_t trees for the gameplay HUD (health bar,
 * mana bar, level/XP/copper text, character portrait, minimap rect)
 * and sends them through svc_layout, following the WC3 ConsoleUI pattern.
 */

#include "g_wow_local.h"

#define HUD_FONT_SIZE 10
#define HUD_SMALL_FONT_SIZE 8

/* WoW HUD layout coordinates (normalized 0-1 screen space) */
#define WOW_PORTRAIT_X 0.010f
#define WOW_PORTRAIT_Y 0.850f
#define WOW_PORTRAIT_SIZE 0.080f

#define WOW_HEALTH_BAR_X 0.100f
#define WOW_HEALTH_BAR_Y 0.905f
#define WOW_HEALTH_BAR_W 0.150f
#define WOW_HEALTH_BAR_H 0.015f

#define WOW_MANA_BAR_X 0.100f
#define WOW_MANA_BAR_Y 0.925f
#define WOW_MANA_BAR_W 0.150f
#define WOW_MANA_BAR_H 0.015f

#define WOW_LEVEL_X 0.100f
#define WOW_LEVEL_Y 0.880f
#define WOW_LEVEL_W 0.050f
#define WOW_LEVEL_H 0.015f

#define WOW_XP_BAR_X 0.100f
#define WOW_XP_BAR_Y 0.945f
#define WOW_XP_BAR_W 0.150f
#define WOW_XP_BAR_H 0.008f

#define WOW_COPPER_X 0.010f
#define WOW_COPPER_Y 0.970f
#define WOW_COPPER_W 0.100f
#define WOW_COPPER_H 0.015f

#define WOW_MINIMAP_X 0.880f
#define WOW_MINIMAP_Y 0.020f
#define WOW_MINIMAP_SIZE 0.110f

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

/* Health bar: green fill with border */
static void UI_WriteHealthBar(FLOAT current, FLOAT max) {
    uiFrame_t frame;

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_SIMPLESTATUSBAR;
    frame.color = MAKE(COLOR32, 0, 200, 0, 255);
    frame.tex.index = gi.ImageIndex("UI\\Widgets\\Console\\Human\\human-statbar-fill.blp");
    frame.tex.index2 = gi.ImageIndex("UI\\Widgets\\Console\\Human\\human-statbar-border.blp");
    frame.value = max > 0 ? current / max : 0.0f;
    UI_SetFrameRect(&frame, WOW_HEALTH_BAR_X, WOW_HEALTH_BAR_Y, WOW_HEALTH_BAR_W, WOW_HEALTH_BAR_H);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

/* Mana bar: blue fill with border */
static void UI_WriteManaBar(FLOAT current, FLOAT max) {
    uiFrame_t frame;

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_SIMPLESTATUSBAR;
    frame.color = MAKE(COLOR32, 0, 100, 255, 255);
    frame.tex.index = gi.ImageIndex("UI\\Widgets\\Console\\Human\\human-statbar-mana-fill.blp");
    frame.tex.index2 = gi.ImageIndex("UI\\Widgets\\Console\\Human\\human-statbar-mana-border.blp");
    frame.value = max > 0 ? current / max : 0.0f;
    UI_SetFrameRect(&frame, WOW_MANA_BAR_X, WOW_MANA_BAR_Y, WOW_MANA_BAR_W, WOW_MANA_BAR_H);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

/* XP bar: gold fill with border */
static void UI_WriteXpBar(FLOAT current, FLOAT max) {
    uiFrame_t frame;

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_SIMPLESTATUSBAR;
    frame.color = MAKE(COLOR32, 200, 170, 0, 255);
    frame.tex.index = gi.ImageIndex("UI\\Widgets\\Console\\Human\\human-statbar-xp-fill.blp");
    frame.tex.index2 = gi.ImageIndex("UI\\Widgets\\Console\\Human\\human-statbar-xp-border.blp");
    frame.value = max > 0 ? current / max : 0.0f;
    UI_SetFrameRect(&frame, WOW_XP_BAR_X, WOW_XP_BAR_Y, WOW_XP_BAR_W, WOW_XP_BAR_H);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

/* Character portrait frame */
static void UI_WritePortraitFrame(LPEDICT ent) {
    uiFrame_t frame;

    if (!ent || !ent->s.model) {
        return;
    }
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_PORTRAIT;
    frame.color = COLOR32_WHITE;
    frame.tex.index = ent->s.model;
    UI_SetFrameRect(&frame, WOW_PORTRAIT_X, WOW_PORTRAIT_Y, WOW_PORTRAIT_SIZE, WOW_PORTRAIT_SIZE);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

/* Minimap position frame (server drives rect, client renders the actual map) */
static void UI_WriteMinimapFrame(void) {
    uiFrame_t frame;

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_BACKDROP;
    frame.color = MAKE(COLOR32, 255, 255, 255, 200);
    UI_SetFrameRect(&frame, WOW_MINIMAP_X, WOW_MINIMAP_Y, WOW_MINIMAP_SIZE, WOW_MINIMAP_SIZE);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

/* Build and unicast the WoW HUD layer for a player */
void UI_WriteWowHud(LPEDICT ent) {
    LPPLAYER ps;
    char buffer[128];

    if (!ent || !ent->client) {
        return;
    }
    ps = &ent->client->ps;

    gi.Write(PF_BYTE, &(LONG){svc_layout});
    gi.Write(PF_BYTE, &(LONG){LAYER_CONSOLE});
    ui_next_frame_number = 1;

    /* Character portrait */
    UI_WritePortraitFrame(ent);

    /* Health bar + text */
    UI_WriteHealthBar((FLOAT)ps->stats[WOW_STAT_HEALTH], (FLOAT)ps->stats[WOW_STAT_HEALTH_MAX]);
    snprintf(buffer, sizeof(buffer), "%d / %d",
             (int)ps->stats[WOW_STAT_HEALTH], (int)ps->stats[WOW_STAT_HEALTH_MAX]);
    UI_WriteTextFrame(WOW_HEALTH_BAR_X + WOW_HEALTH_BAR_W + 0.005f, WOW_HEALTH_BAR_Y,
                      0.050f, 0.015f, buffer, COLOR32_WHITE, FONT_JUSTIFYLEFT);

    /* Mana bar + text */
    UI_WriteManaBar((FLOAT)ps->stats[WOW_STAT_POWER], (FLOAT)ps->stats[WOW_STAT_POWER_MAX]);
    snprintf(buffer, sizeof(buffer), "%d / %d",
             (int)ps->stats[WOW_STAT_POWER], (int)ps->stats[WOW_STAT_POWER_MAX]);
    UI_WriteTextFrame(WOW_MANA_BAR_X + WOW_MANA_BAR_W + 0.005f, WOW_MANA_BAR_Y,
                      0.050f, 0.015f, buffer, COLOR32_WHITE, FONT_JUSTIFYLEFT);

    /* Level text */
    snprintf(buffer, sizeof(buffer), "Level %d", (int)ps->stats[WOW_STAT_LEVEL]);
    UI_WriteTextFrame(WOW_LEVEL_X, WOW_LEVEL_Y, WOW_LEVEL_W, WOW_LEVEL_H,
                      buffer, MAKE(COLOR32, 252, 210, 18, 255), FONT_JUSTIFYLEFT);

    /* XP bar */
    UI_WriteXpBar((FLOAT)ps->stats[WOW_STAT_XP], (FLOAT)ps->stats[WOW_STAT_XP_MAX]);

    /* Copper display */
    snprintf(buffer, sizeof(buffer), "%d g %d s %d c",
             (int)ps->stats[WOW_STAT_COPPER] / 10000,
             ((int)ps->stats[WOW_STAT_COPPER] % 10000) / 100,
             (int)ps->stats[WOW_STAT_COPPER] % 100);
    UI_WriteTextFrame(WOW_COPPER_X, WOW_COPPER_Y, WOW_COPPER_W, WOW_COPPER_H,
                      buffer, MAKE(COLOR32, 255, 200, 0, 255), FONT_JUSTIFYLEFT);

    /* Minimap position frame */
    UI_WriteMinimapFrame();

    gi.Write(PF_LONG, &(LONG){0});
    gi.Write(PF_SHORT, &(LONG){0});
    gi.unicast(ent);
}
