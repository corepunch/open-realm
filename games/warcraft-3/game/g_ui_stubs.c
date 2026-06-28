/*
 * g_ui_stubs.c — Server-authored in-game HUD proxy frames.
 *
 * Glue/FDF menus are client-side, but the gameplay HUD is data-heavy and
 * depends on server-owned unit state.  These helpers generate compact
 * uiFrame_t trees and send them through svc_layout, reusing the old client
 * layout renderer in cl_scrn/cl_menu.
 */

#include "g_local.h"

#define HUD_FONT_SIZE 10
#define HUD_SMALL_FONT_SIZE 8
#define HUD_TITLE_FONT_SIZE 12
#define UI_BASE_WIDTH 0.8f
#define UI_BASE_HEIGHT 0.6f

#define INFO_PANEL_X 0.310f
#define INFO_PANEL_Y 0.486f
#define INFO_PANEL_W 0.180f
#define INFO_PANEL_H 0.105f
#define BUILDQUEUE_OFFSET 0.0281f
#define BUILDQUEUE_BACKDROP_X INFO_PANEL_X
#define BUILDQUEUE_BACKDROP_Y (INFO_PANEL_Y + INFO_PANEL_H - 0.1000f)
#define BUILDQUEUE_BACKDROP_W INFO_PANEL_W
#define BUILDQUEUE_BACKDROP_H 0.1000f
#define BUILDQUEUE_FIRST_X (INFO_PANEL_X + 0.0100f)
#define BUILDQUEUE_FIRST_Y (INFO_PANEL_Y + 0.0390f)
#define BUILDQUEUE_FIRST_W 0.0280f
#define BUILDQUEUE_FIRST_H 0.0310f

static void UI_ClearCursorSplat(LPEDICT ent);
#define BUILDQUEUE_LIST_X (INFO_PANEL_X + 0.0095f)
#define BUILDQUEUE_LIST_Y (INFO_PANEL_Y + 0.0800f)
#define BUILDQUEUE_ITEM_W 0.0200f
#define BUILDQUEUE_ITEM_H 0.0215f
#define BUILDQUEUE_TIMER_X (INFO_PANEL_X + 0.061250f)
#define BUILDQUEUE_TIMER_Y (INFO_PANEL_Y + 0.038125f)
#define BUILDQUEUE_TIMER_W 0.1450f
#define BUILDQUEUE_TIMER_H 0.0120f
#define BUILDQUEUE_ACTION_X (INFO_PANEL_X + 0.061250f)
#define BUILDQUEUE_ACTION_Y (INFO_PANEL_Y + 0.022875f)
#define BUILDQUEUE_ACTION_W 0.1050f
#define BUILDQUEUE_ACTION_H 0.0140f
#define PORTRAIT_X 0.215f
#define PORTRAIT_Y 0.490f
#define PORTRAIT_SIZE 0.080f
#define COMMAND_BUTTON_SIZE 0.039f
#define COMMAND_BUTTON_CENTER_X(x) (UI_BASE_WIDTH * 0.5f + 0.2365f + (FLOAT)(x) * 0.0434f)
#define COMMAND_BUTTON_CENTER_Y(y) (UI_BASE_HEIGHT - (0.1131f - (FLOAT)(y) * 0.0434f))
#define INVENTORY_BUTTON_SIZE 0.033f
#define INVENTORY_BUTTON_CENTER_X(x) (UI_BASE_WIDTH * 0.5f + 0.1315f + (FLOAT)(x) * 0.0394f)
#define INVENTORY_BUTTON_CENTER_Y(y) (UI_BASE_HEIGHT - (0.0971f - (FLOAT)(y) * 0.0384f))
#define MULTISELECT_X 0.314f
#define MULTISELECT_Y 0.500f
#define MULTISELECT_SIZE 0.025f
#define TOOLTIP_X 0.5800f
#define TOOLTIP_Y 0.3400f
#define TOOLTIP_W 0.2200f
#define TOOLTIP_H 0.1000f
#define QUEST_X 0.1500f
#define QUEST_Y 0.0700f
#define QUEST_W 0.5000f
#define QUEST_H 0.4050f
#define QUEST_LIST_X (QUEST_X + 0.0200f)
#define QUEST_LIST_Y (QUEST_Y + 0.0950f)
#define QUEST_LIST_W 0.1800f
#define QUEST_ROW_H 0.0180f
#define QUEST_DETAIL_X (QUEST_X + 0.2250f)
#define QUEST_DETAIL_Y (QUEST_Y + 0.0950f)
#define QUEST_DETAIL_W 0.2500f
#define QUEST_MESSAGE_X 0.0500f
#define QUEST_MESSAGE_Y 0.3000f
#define QUEST_MESSAGE_W 0.3000f
#define QUEST_MESSAGE_H 0.1450f
static DWORD ui_next_frame_number;
static LPGAMECLIENT ui_current_client;

static void UI_WriteCommandButtonFrame(gameCommandButton_t const *button);

static LPCSTR UI_LevelStringSafe(LPCSTR text) {
    if (!text || !*text) {
        return " ";
    }
    return G_LevelString(text);
}

void UI_SetCurrentClient(LPGAMECLIENT client) {
    ui_current_client = client;
}

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
    frame->color = frame->color.a ? frame->color : COLOR32_WHITE;
    /* Set default full-UV only when caller left coords zeroed */
    if (!frame->tex.coord[1] && !frame->tex.coord[3]) {
        frame->tex.coord[1] = 0xff;
        frame->tex.coord[3] = 0xff;
    }
    frame->buffer.data = data;
    frame->buffer.size = data_size;
    gi.Write(PF_UIFRAME, frame);
}

static void UI_WriteProxyFrameToParent(LPUIFRAME frame, HANDLE data, DWORD data_size, DWORD parent) {
    frame->parent = parent;
    UI_WriteProxyFrame(frame, data, data_size);
}

static void UI_SetFramePointRelative(uiFramePoint_t *point, uiFramePointPos_t target, DWORD relative, FLOAT offset, BOOL y_axis) {
    point->used = 1;
    point->targetPos = target;
    point->relativeTo = (BYTE)relative;
    point->offset = (SHORT)((y_axis ? -offset : offset) * UI_FRAMEPOINT_SCALE);
}

static void UI_WriteTextFrame(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR text, COLOR32 color,
                              uiFontJustificationH_t align) {
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

static void UI_WriteTextFrameSized(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR text, COLOR32 color,
                                   uiFontJustificationH_t align, DWORD font_size) {
    uiFrame_t frame;
    uiLabel_t label;

    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    frame.flags.type = FT_STRING;
    frame.text = text && *text ? text : " ";
    frame.color = color;
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", font_size);
    label.textalignx = align;
    label.textaligny = FONT_JUSTIFYTOP;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

static void UI_WriteCommandTextFrame(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR text, LPCSTR command,
                                     COLOR32 color, uiFontJustificationH_t align, DWORD font_size) {
    uiFrame_t frame;
    uiLabel_t label;

    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    frame.flags.type = FT_STRING;
    frame.text = text && *text ? text : " ";
    frame.onclick = command;
    frame.color = color;
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", font_size);
    label.textalignx = align;
    label.textaligny = FONT_JUSTIFYTOP;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

static void UI_WriteBackdropFrame(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR background, LPCSTR edge) {
    uiFrame_t frame;
    uiBackdrop_t backdrop;

    memset(&frame, 0, sizeof(frame));
    memset(&backdrop, 0, sizeof(backdrop));
    frame.flags.type = FT_BACKDROP;
    frame.color = MAKE(COLOR32, 255, 255, 255, 235);
    backdrop.Background = gi.ImageIndex(Theme_String(background, background));
    backdrop.EdgeFile = gi.ImageIndex(Theme_String(edge, edge));
    backdrop.CornerFlags = 0x1ff;
    backdrop.CornerSize = 0.008f;
    backdrop.BackgroundSize = 0.036f;
    backdrop.BackgroundInsets[0] = 0.0025f;
    backdrop.BackgroundInsets[1] = 0.0025f;
    backdrop.BackgroundInsets[2] = 0.0025f;
    backdrop.BackgroundInsets[3] = 0.0025f;
    backdrop.TileBackground = true;
    backdrop.BlendAll = true;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &backdrop, sizeof(backdrop));
}

static void UI_WriteTextAreaFrame(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR text, COLOR32 color,
                                  DWORD font_size, FLOAT inset) {
    uiFrame_t frame;
    uiTextArea_t textarea;

    memset(&frame, 0, sizeof(frame));
    memset(&textarea, 0, sizeof(textarea));
    frame.flags.type = FT_TEXTAREA;
    frame.text = text && *text ? text : " ";
    frame.color = color;
    textarea.font = gi.FontIndex(Theme_String("MessageFont", "Fonts\\FRIZQT__.TTF"), font_size);
    textarea.inset = inset;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &textarea, sizeof(textarea));
}

static void UI_WriteTooltipFrame(void) {
    uiFrame_t frame;
    uiTooltip_t tooltip;

    memset(&frame, 0, sizeof(frame));
    memset(&tooltip, 0, sizeof(tooltip));
    frame.flags.type = FT_TOOLTIPTEXT;
    frame.color = COLOR32_WHITE;
    tooltip.background.Background = gi.ImageIndex(Theme_String("ToolTipBackground", "ToolTipBackground"));
    tooltip.background.EdgeFile = gi.ImageIndex(Theme_String("ToolTipBorder", "ToolTipBorder"));
    tooltip.background.CornerFlags = 0x1ff;
    tooltip.background.CornerSize = 0.008f;
    tooltip.background.BackgroundSize = 0.036f;
    tooltip.background.BackgroundInsets[0] = 0.0025f;
    tooltip.background.BackgroundInsets[1] = 0.0025f;
    tooltip.background.BackgroundInsets[2] = 0.0025f;
    tooltip.background.BackgroundInsets[3] = 0.0025f;
    tooltip.background.TileBackground = true;
    tooltip.background.BlendAll = true;
    tooltip.text.font = gi.FontIndex(Theme_String("MasterFont", "Fonts\\FRIZQT__.TTF"), HUD_FONT_SIZE);
    tooltip.text.textalignx = FONT_JUSTIFYLEFT;
    tooltip.text.textaligny = FONT_JUSTIFYTOP;
    UI_SetFrameRect(&frame, TOOLTIP_X, TOOLTIP_Y, TOOLTIP_W, TOOLTIP_H);
    UI_WriteProxyFrame(&frame, &tooltip, sizeof(tooltip));
}

static void UI_AppendMessageText(LPSTR out, DWORD out_size, LPCSTR text) {
    if (!out || out_size == 0 || !text) {
        return;
    }
    strncat(out, text, out_size - strlen(out) - 1);
}

static LPCSTR UI_FormatMessageText(LPCSTR text) {
    static char buffers[4][1024];
    static DWORD cursor;
    char temp[1024];
    LPSTR out = buffers[cursor++ & 3];
    LPCSTR source = text && *text ? text : " ";
    BOOL quest_message = strstr(source, "MAIN QUEST") || strstr(source, "OPTIONAL QUEST");
    BOOL inserted_heading_break = false;
    LPCSTR heading = quest_message ? strstr(source, "QUEST") : NULL;

    temp[0] = '\0';
    out[0] = '\0';

    for (LPCSTR p = source; *p && strlen(temp) < sizeof(temp) - 1;) {
        if (quest_message && p[0] == ' ' && p[1] == '-' && p[2] == ' ') {
            UI_AppendMessageText(temp, sizeof(temp), "|n- ");
            p += 3;
            continue;
        }
        strncat(temp, p, 1);
        p++;
    }

    source = temp;
    heading = quest_message ? strstr(source, "QUEST") : NULL;
    for (LPCSTR p = source; *p && strlen(out) < sizeof(buffers[0]) - 1;) {
        if (quest_message && !inserted_heading_break && heading &&
            (p == heading + 5 || (!strncmp(p, "|r", 2) && p > heading))) {
            if (!strncmp(p, "|r", 2)) {
                UI_AppendMessageText(out, sizeof(buffers[0]), "|r");
                p += 2;
            }
            if (strncmp(p, "|n", 2) && *p != '\n') {
                UI_AppendMessageText(out, sizeof(buffers[0]), "|n");
            }
            inserted_heading_break = true;
            continue;
        }
        strncat(out, p, 1);
        p++;
    }

    return out;
}

/* ConsoleUI backdrop parts — from the hardcoded table in ui_main.c.
 * uv: {u_min, u_max, v_min, v_max} as bytes 0-255 (coord[] encoding). */
typedef struct { LPCSTR key; RECT screen; BYTE uv[4]; } consoleBackdropPart_t;
static consoleBackdropPart_t const console_backdrop[] = {
    /* top strip */
    { "ConsoleTexture01", { 0.000f, 0.000f, 0.256f, 0.032f }, { 0,   255,  0,  32 } },
    { "ConsoleTexture02", { 0.256f, 0.000f, 0.087f, 0.032f }, { 0,    87,  0,  32 } },
    { "ConsoleTexture02", { 0.459f, 0.000f, 0.053f, 0.032f }, { 202, 255,  0,  32 } },
    { "ConsoleTexture03", { 0.512f, 0.000f, 0.256f, 0.032f }, { 0,   255,  0,  32 } },
    { "ConsoleTexture04", { 0.768f, 0.000f, 0.032f, 0.032f }, { 0,   255,  0,  32 } },
    /* bottom panel */
    { "ConsoleTexture01", { 0.000f, 0.424f, 0.256f, 0.176f }, { 0,   255,  80, 255 } },
    { "ConsoleTexture02", { 0.256f, 0.450f, 0.256f, 0.150f }, { 0,   255, 106, 255 } },
    { "ConsoleTexture03", { 0.512f, 0.424f, 0.256f, 0.176f }, { 0,   255,  80, 255 } },
    { "ConsoleTexture04", { 0.768f, 0.424f, 0.032f, 0.176f }, { 0,   255,  80, 255 } },
};

/* Write the ConsoleUI backdrop nine-part image mosaic as FT_TEXTURE frames. */
static void UI_WriteConsoleBackdrop(void) {
    FOR_LOOP(i, sizeof(console_backdrop) / sizeof(console_backdrop[0])) {
        consoleBackdropPart_t const *p = &console_backdrop[i];
        uiFrame_t frame;

        memset(&frame, 0, sizeof(frame));
        frame.flags.type = FT_TEXTURE;
        frame.color = COLOR32_WHITE;
        frame.tex.index = gi.ImageIndex(Theme_String(p->key, p->key));
        frame.tex.coord[0] = p->uv[0];
        frame.tex.coord[1] = p->uv[1];
        frame.tex.coord[2] = p->uv[2];
        frame.tex.coord[3] = p->uv[3];
        UI_SetFrameRect(&frame, p->screen.x, p->screen.y, p->screen.w, p->screen.h);
        UI_WriteProxyFrame(&frame, NULL, 0);
    }
}

/* Minimap viewport — FT_MINIMAP; client calls DrawMinimap() for this rect. */
static void UI_WriteMinimapFrame(void) {
    uiFrame_t frame;

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_MINIMAP;
    frame.color = COLOR32_WHITE;
    UI_SetFrameRect(&frame, 0.0070f, 0.4525f, 0.1395f, 0.1395f);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

static void UI_WriteResourceBar(LONG food_used) {
    uiFrame_t frame;
    uiLabel_t label;

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_FRAME;
    frame.color = COLOR32_WHITE;
    UI_SetFramePoint(&frame.points.x[FPP_MAX], FPP_MAX, 0, 0.0f, false);
    UI_SetFramePoint(&frame.points.y[FPP_MIN], FPP_MIN, 0, 0.0f, true);
    frame.size.width = 0.33828125f;
    frame.size.height = 0.0218f;
    UI_WriteProxyFrame(&frame, NULL, 0);
    DWORD root = frame.number;

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_TEXTURE;
    frame.color = COLOR32_WHITE;
    frame.tex.index = gi.ImageIndex(Theme_String("GoldIcon", "GoldIcon"));
    UI_SetFramePointRelative(&frame.points.x[FPP_MIN], FPP_MIN, root, 0.0000f, false);
    UI_SetFramePointRelative(&frame.points.y[FPP_MIN], FPP_MIN, root, -0.003125f, true);
    frame.size.width = 0.01640625f;
    frame.size.height = 0.01640625f;
    UI_WriteProxyFrameToParent(&frame, NULL, 0, root);

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_TEXTURE;
    frame.color = COLOR32_WHITE;
    frame.tex.index = gi.ImageIndex(Theme_String("LumberIcon", "LumberIcon"));
    UI_SetFramePointRelative(&frame.points.x[FPP_MIN], FPP_MIN, root, 0.0859375f, false);
    UI_SetFramePointRelative(&frame.points.y[FPP_MIN], FPP_MIN, root, -0.003125f, true);
    frame.size.width = 0.01640625f;
    frame.size.height = 0.01640625f;
    UI_WriteProxyFrameToParent(&frame, NULL, 0, root);

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_TEXTURE;
    frame.color = COLOR32_WHITE;
    frame.tex.index = gi.ImageIndex(Theme_String("SupplyIcon", "SupplyIcon"));
    UI_SetFramePointRelative(&frame.points.x[FPP_MIN], FPP_MIN, root, 0.17265625f, false);
    UI_SetFramePointRelative(&frame.points.y[FPP_MIN], FPP_MIN, root, -0.003125f, true);
    frame.size.width = 0.01640625f;
    frame.size.height = 0.01640625f;
    UI_WriteProxyFrameToParent(&frame, NULL, 0, root);

    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    frame.flags.type = FT_STRING;
    frame.stat = PLAYERSTATE_RESOURCE_GOLD;
    frame.color = COLOR32_WHITE;
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", 10);
    label.textalignx = FONT_JUSTIFYRIGHT;
    label.textaligny = FONT_JUSTIFYMIDDLE;
    UI_SetFramePointRelative(&frame.points.x[FPP_MAX], FPP_MAX, root, -0.26484375f, false);
    UI_SetFramePointRelative(&frame.points.y[FPP_MIN], FPP_MIN, root, -0.003125f, true);
    frame.size.height = 0.01640625f;
    UI_WriteProxyFrameToParent(&frame, &label, sizeof(label), root);

    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    frame.flags.type = FT_STRING;
    frame.stat = PLAYERSTATE_RESOURCE_LUMBER;
    frame.color = COLOR32_WHITE;
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", 10);
    label.textalignx = FONT_JUSTIFYRIGHT;
    label.textaligny = FONT_JUSTIFYMIDDLE;
    UI_SetFramePointRelative(&frame.points.x[FPP_MAX], FPP_MAX, root, -0.17812500f, false);
    UI_SetFramePointRelative(&frame.points.y[FPP_MIN], FPP_MIN, root, -0.003125f, true);
    frame.size.height = 0.01640625f;
    UI_WriteProxyFrameToParent(&frame, &label, sizeof(label), root);

    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    frame.flags.type = FT_STRING;
    frame.stat = PLAYERSTATE_RESOURCE_FOOD_USED;
    frame.color = COLOR32_WHITE;
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", 10);
    label.textalignx = FONT_JUSTIFYRIGHT;
    label.textaligny = FONT_JUSTIFYMIDDLE;
    UI_SetFramePointRelative(&frame.points.x[FPP_MAX], FPP_MAX, root, -0.09140625f, false);
    UI_SetFramePointRelative(&frame.points.y[FPP_MIN], FPP_MIN, root, -0.003125f, true);
    frame.size.height = 0.01640625f;
    UI_WriteProxyFrameToParent(&frame, &label, sizeof(label), root);

    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    frame.flags.type = FT_STRING;
    frame.text = food_used > 80 ? "Heavy Upkeep" : food_used > 50 ? "Low Upkeep" : "No Upkeep";
    frame.color = food_used > 80 ? MAKE(COLOR32, 255, 64, 64, 255)
                                 : food_used > 50 ? MAKE(COLOR32, 255, 200, 64, 255)
                                                  : MAKE(COLOR32, 96, 255, 96, 255);
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", 10);
    label.textalignx = FONT_JUSTIFYCENTER;
    label.textaligny = FONT_JUSTIFYMIDDLE;
    UI_SetFramePointRelative(&frame.points.x[FPP_MAX], FPP_MAX, root, -0.003125f, false);
    UI_SetFramePointRelative(&frame.points.y[FPP_MIN], FPP_MIN, root, -0.003125f, true);
    frame.size.width = 0.07578125f;
    frame.size.height = 0.01640625f;
    UI_WriteProxyFrameToParent(&frame, &label, sizeof(label), root);
}

/* Send the full LAYER_CONSOLE payload: backdrop + minimap + resource icons +
 * resource text values + top-bar buttons + tooltip. */
static void UI_SendConsoleLayer(LPEDICT ent, LONG food_used) {
    gi.Write(PF_BYTE, &(LONG){svc_layout});
    gi.Write(PF_BYTE, &(LONG){LAYER_CONSOLE});
    ui_next_frame_number = 1;

    UI_WriteConsoleBackdrop();
    UI_WriteMinimapFrame();
    UI_WriteResourceBar(food_used);

    UI_WriteCommandTextFrame(0.004f, 0.006f, 0.085f, 0.014f, "Quests (F9)", "quests",
                             COLOR32_WHITE, FONT_JUSTIFYCENTER, HUD_FONT_SIZE);
    UI_WriteCommandTextFrame(0.089f, 0.006f, 0.085f, 0.014f, "Menu (F10)", "menu",
                             COLOR32_WHITE, FONT_JUSTIFYCENTER, HUD_FONT_SIZE);
    UI_WriteTextFrame(0.174f, 0.006f, 0.085f, 0.014f, "Allies (F11)", COLOR32_WHITE, FONT_JUSTIFYCENTER);
    UI_WriteTextFrame(0.259f, 0.006f, 0.085f, 0.014f, "Chat (F12)", COLOR32_WHITE, FONT_JUSTIFYCENTER);
    UI_WriteTooltipFrame();

    gi.Write(PF_LONG, &(LONG){0});
    gi.Write(PF_SHORT, &(LONG){0});
    gi.unicast(ent);
}

static void UI_WriteServerConsoleShell(LPEDICT ent) {
    LPPLAYER ps = ent && ent->client ? &ent->client->ps : NULL;
    LONG gold    = ps ? (LONG)ps->stats[PLAYERSTATE_RESOURCE_GOLD]      : 0;
    LONG lumber  = ps ? (LONG)ps->stats[PLAYERSTATE_RESOURCE_LUMBER]    : 0;
    LONG food_u  = ps ? (LONG)ps->stats[PLAYERSTATE_RESOURCE_FOOD_USED] : 0;
    LONG food_c  = ps ? (LONG)ps->stats[PLAYERSTATE_RESOURCE_FOOD_CAP]  : 0;

    if (!ent)
        return;
    UI_SendConsoleLayer(ent, food_u);
    if (ent->client) {
        ent->client->resourcebar.gold      = gold;
        ent->client->resourcebar.lumber    = lumber;
        ent->client->resourcebar.food_used = food_u;
        ent->client->resourcebar.food_cap  = food_c;
    }
}

static void UI_WritePortraitFrame(LPEDICT ent) {
    uiFrame_t frame;

    if (!ent || !ent->s.model) {
        return;
    }
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_PORTRAIT;
    frame.color = COLOR32_WHITE;
    frame.tex.index = ent->s.model;
    UI_SetFrameRect(&frame, PORTRAIT_X, PORTRAIT_Y, PORTRAIT_SIZE, PORTRAIT_SIZE);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

static RECT UI_CommandButtonRect(BYTE x, BYTE y) {
    return MAKE(RECT,
                COMMAND_BUTTON_CENTER_X(x) - COMMAND_BUTTON_SIZE * 0.5f,
                COMMAND_BUTTON_CENTER_Y(y) - COMMAND_BUTTON_SIZE * 0.5f,
                COMMAND_BUTTON_SIZE,
                COMMAND_BUTTON_SIZE);
}

static RECT UI_InventoryButtonRect(BYTE slot) {
    return MAKE(RECT,
                INVENTORY_BUTTON_CENTER_X(slot % 2) - INVENTORY_BUTTON_SIZE * 0.5f,
                INVENTORY_BUTTON_CENTER_Y(slot / 2) - INVENTORY_BUTTON_SIZE * 0.5f,
                INVENTORY_BUTTON_SIZE,
                INVENTORY_BUTTON_SIZE);
}

static void UI_WriteCommandButton(LPCSTR code, BOOL research, DWORD level) {
    gameCommandButton_t buttons[1];
    LPEDICT ent = G_GetMainSelectedUnit(ui_current_client);

    if (!ent || !code || !*code) {
        return;
    }
    if (!G_BuildCommandButton(ent, code, research, level, buttons)) {
        return;
    }

    UI_WriteCommandButtonFrame(buttons);
}

static DWORD UI_ClassIdFromCode(LPCSTR code) {
    DWORD class_id = 0;

    if (IS_FOURCC(code)) {
        memcpy(&class_id, code, sizeof(class_id));
    }
    return class_id;
}

static void UI_FormatTooltip(LPCSTR code,
                             LPCSTR tip,
                             LPCSTR ubertip,
                             LPSTR out,
                             DWORD out_size) {
    DWORD class_id = UI_ClassIdFromCode(code);
    DWORD gold_cost = class_id ? UNIT_GOLD_COST(class_id) : 0;
    DWORD lumber_cost = class_id ? UNIT_LUMBER_COST(class_id) : 0;
    DWORD food_cost = class_id ? UNIT_FOOD_USED(class_id) : 0;
    DWORD gold_icon = 0;
    DWORD lumber_icon = 0;
    DWORD supply_icon = 0;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    snprintf(out, out_size, "%s", tip && *tip ? tip : " ");
    if (gold_cost || lumber_cost || food_cost) {
        gold_icon = gi.ImageIndex(Theme_String("ToolTipGoldIcon", "ToolTipGoldIcon"));
        lumber_icon = gi.ImageIndex(Theme_String("ToolTipLumberIcon", "ToolTipLumberIcon"));
        supply_icon = gi.ImageIndex(Theme_String("ToolTipSupplyIcon", "ToolTipSupplyIcon"));
        snprintf(out + strlen(out), out_size - strlen(out), "|n");
        if (gold_cost) {
            snprintf(out + strlen(out), out_size - strlen(out), "<Icon,%u> %u   ",
                     (unsigned)gold_icon, (unsigned)gold_cost);
        }
        if (lumber_cost) {
            snprintf(out + strlen(out), out_size - strlen(out), "<Icon,%u> %u   ",
                     (unsigned)lumber_icon, (unsigned)lumber_cost);
        }
        if (food_cost) {
            snprintf(out + strlen(out), out_size - strlen(out), "<Icon,%u> %u   ",
                     (unsigned)supply_icon, (unsigned)food_cost);
        }
    }
    if (ubertip && *ubertip) {
        snprintf(out + strlen(out), out_size - strlen(out), "|n%s", ubertip);
    }
}

void UI_ClearLayer(LPEDICT ent, DWORD layer) {
    if (!ent) {
        return;
    }
    gi.Write(PF_BYTE, &(LONG){svc_layout});
    gi.Write(PF_BYTE, &(LONG){layer});
    gi.Write(PF_LONG, &(LONG){0});
    gi.Write(PF_SHORT, &(LONG){0});
    gi.unicast(ent);
}

static DWORD UI_SelectedUnits(LPGAMECLIENT client, LPEDICT *out, DWORD max_out) {
    DWORD count = 0;

    FOR_SELECTED_UNITS(client, ent) {
        if (count < max_out) {
            out[count] = ent;
        }
        count++;
    }
    return MIN(count, max_out);
}

static void UI_WriteSingleInfo(LPEDICT ent) {
    char buffer[128];
    LPCSTR name = UNIT_PROPER_NAMES(ent->class_id);
    LPCSTR unit_name = UNIT_NAME(ent->class_id);
    /* A hero's primary attributes drive its current stats; show the LIVE values
     * (which reflect leveling/tomes) rather than the static data baselines. */
    BOOL const is_hero = UNIT_STRENGTH(ent->class_id) > 0 ||
                         UNIT_AGILITY(ent->class_id) > 0 ||
                         UNIT_INTELLIGENCE(ent->class_id) > 0;
    DWORD level = is_hero && ent->hero.level > 0 ? ent->hero.level
                                                 : MAX(1, UNIT_LEVEL(ent->class_id));
    LONG dice = ent->attack1.numberOfDice;
    LONG min_damage = dice ? (LONG)(ent->attack1.damageBase + dice) : 0;
    LONG max_damage = dice ? (LONG)(ent->attack1.damageBase + dice * ent->attack1.sidesPerDie) : 0;

    if (!name || !*name) {
        name = unit_name ? unit_name : GetClassName(ent->class_id);
    }

    UI_WriteTextFrame(INFO_PANEL_X, INFO_PANEL_Y, INFO_PANEL_W, 0.018f, name, COLOR32_WHITE, FONT_JUSTIFYCENTER);
    snprintf(buffer, sizeof(buffer), "Level %lu %s", (unsigned long)level, unit_name ? unit_name : "");
    UI_WriteTextFrame(INFO_PANEL_X, INFO_PANEL_Y + 0.020f, INFO_PANEL_W, 0.014f, buffer,
                      MAKE(COLOR32, 252, 210, 18, 255), FONT_JUSTIFYCENTER);

    UI_WriteTextFrame(INFO_PANEL_X + 0.010f, INFO_PANEL_Y + 0.042f, 0.055f, 0.012f, "Damage:",
                      MAKE(COLOR32, 252, 210, 18, 255), FONT_JUSTIFYLEFT);
    snprintf(buffer, sizeof(buffer), "%d - %d", (int)min_damage, (int)max_damage);
    UI_WriteTextFrame(INFO_PANEL_X + 0.062f, INFO_PANEL_Y + 0.042f, 0.050f, 0.012f, buffer,
                      COLOR32_WHITE, FONT_JUSTIFYLEFT);

    UI_WriteTextFrame(INFO_PANEL_X + 0.010f, INFO_PANEL_Y + 0.066f, 0.055f, 0.012f, "Armor:",
                      MAKE(COLOR32, 252, 210, 18, 255), FONT_JUSTIFYLEFT);
    snprintf(buffer, sizeof(buffer), "%d", (int)(ent->armor_value + 0.5f));
    UI_WriteTextFrame(INFO_PANEL_X + 0.062f, INFO_PANEL_Y + 0.066f, 0.050f, 0.012f, buffer,
                      COLOR32_WHITE, FONT_JUSTIFYLEFT);

    /* Hero primary attributes (STR/AGI/INT), primary one highlighted green, with
     * the experience toward the next level beneath. */
    if (is_hero) {
        LPCSTR const prim = UNIT_PRIMARY_ATTRIBUTE(ent->class_id);
        COLOR32 const lbl   = MAKE(COLOR32, 252, 210, 18, 255);
        COLOR32 const white = COLOR32_WHITE;
        COLOR32 const green = MAKE(COLOR32, 120, 230, 120, 255);
        struct { LPCSTR tag, code; DWORD val; } attrs[3] = {
            { "Str:", "STR", ent->hero.str },
            { "Agi:", "AGI", ent->hero.agi },
            { "Int:", "INT", ent->hero.intel },
        };
        FOR_LOOP(a, 3) {
            FLOAT const ay = INFO_PANEL_Y + 0.090f + a * 0.016f;
            BOOL const isprim = prim && !strcmp(prim, attrs[a].code);
            UI_WriteTextFrame(INFO_PANEL_X + 0.010f, ay, 0.040f, 0.012f, attrs[a].tag,
                              lbl, FONT_JUSTIFYLEFT);
            snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)attrs[a].val);
            UI_WriteTextFrame(INFO_PANEL_X + 0.050f, ay, 0.040f, 0.012f, buffer,
                              isprim ? green : white, FONT_JUSTIFYLEFT);
        }
        DWORD const need = G_HeroXPForLevel(level + 1);
        DWORD const have = G_HeroXPForLevel(level);
        if (need > have) {
            snprintf(buffer, sizeof(buffer), "XP: %lu / %lu",
                     (unsigned long)(ent->hero.xp - (ent->hero.xp < have ? ent->hero.xp : have)),
                     (unsigned long)(need - have));
            UI_WriteTextFrame(INFO_PANEL_X + 0.010f, INFO_PANEL_Y + 0.090f + 3 * 0.016f,
                              0.110f, 0.012f, buffer, MAKE(COLOR32, 200, 200, 200, 255),
                              FONT_JUSTIFYLEFT);
        }
    }

    /* Hit points (and mana for casters): current / max of the live unit, shown
     * in WC3's green/blue, centred just beneath the portrait. */
    if (ent->health.max_value > 0) {
        snprintf(buffer, sizeof(buffer), "%d / %d",
                 (int)(ent->health.value + 0.5f), (int)(ent->health.max_value + 0.5f));
        UI_WriteTextFrame(PORTRAIT_X, PORTRAIT_Y + PORTRAIT_SIZE + 0.002f, PORTRAIT_SIZE, 0.012f,
                          buffer, MAKE(COLOR32, 64, 224, 64, 255), FONT_JUSTIFYCENTER);
    }
    if (ent->mana.max_value > 0) {
        snprintf(buffer, sizeof(buffer), "%d / %d",
                 (int)(ent->mana.value + 0.5f), (int)(ent->mana.max_value + 0.5f));
        UI_WriteTextFrame(PORTRAIT_X, PORTRAIT_Y + PORTRAIT_SIZE + 0.015f, PORTRAIT_SIZE, 0.012f,
                          buffer, MAKE(COLOR32, 90, 160, 255, 255), FONT_JUSTIFYCENTER);
    }
}

static void UI_WriteInventory(LPEDICT ent) {
    gameInventoryItem_t items[MAX_INVENTORY];
    BYTE count = G_GetInventory(ent, items, MAX_INVENTORY);

    FOR_LOOP(i, count) {
        RECT rect = UI_InventoryButtonRect(items[i].slot);
        uiFrame_t frame;
        char onclick[128];

        memset(&frame, 0, sizeof(frame));
        frame.flags.type = FT_COMMANDBUTTON;
        frame.color = COLOR32_WHITE;
        frame.tex.index = gi.ImageIndex(items[i].art);
        frame.tooltip = items[i].ubertip[0] ? items[i].ubertip : items[i].tooltip;
        snprintf(onclick, sizeof(onclick), "inventory %u", (unsigned)items[i].slot);
        frame.onclick = onclick;
        UI_SetFrameRect(&frame, rect.x, rect.y, rect.w, rect.h);
        UI_WriteProxyFrame(&frame, NULL, 0);
    }
}

static void UI_WriteCommandButtonFrame(gameCommandButton_t const *button) {
    uiFrame_t frame;
    RECT rect;
    char onclick[320];
    char tooltip[1024];

    if (!button) {
        return;
    }
    rect = UI_CommandButtonRect(button->x, button->y);
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_COMMANDBUTTON;
    frame.color = COLOR32_WHITE;
    frame.tex.index = gi.ImageIndex(button->art);
    frame.stat = button->active;
    frame.value = button->cooldown; /* command-card cooldown shade (0 = ready) */
    UI_FormatTooltip(button->command,
                     button->tooltip,
                     button->ubertip,
                     tooltip,
                     sizeof(tooltip));
    frame.tooltip = tooltip;
    snprintf(onclick, sizeof(onclick), "%s %s", button->research ? "research" : "button", button->command);
    frame.onclick = onclick;
    UI_SetFrameRect(&frame, rect.x, rect.y, rect.w, rect.h);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

static void UI_WriteBuildQueue(LPEDICT ent) {
    gameQueueItem_t queue[MAX_BUILD_QUEUE];
    BYTE count = G_GetBuildQueue(ent, queue, MAX_BUILD_QUEUE);
    DWORD size;
    LPBYTE buffer;
    uiBuildQueue_t *buildqueue;
    uiFrame_t backdrop;
    uiFrame_t firstitem;
    uiFrame_t buildtimer;
    uiFrame_t list;

    if (!count) {
        return;
    }

    UI_WriteTextFrame(INFO_PANEL_X,
                      INFO_PANEL_Y,
                      INFO_PANEL_W,
                      0.018f,
                      UNIT_NAME(ent->class_id) ? UNIT_NAME(ent->class_id) : GetClassName(ent->class_id),
                      COLOR32_WHITE,
                      FONT_JUSTIFYCENTER);
    UI_WriteTextFrame(BUILDQUEUE_ACTION_X,
                      BUILDQUEUE_ACTION_Y,
                      BUILDQUEUE_ACTION_W,
                      BUILDQUEUE_ACTION_H,
                      ent->currentmove && ent->currentmove->think == ai_birth ? "Constructing" : "Training",
                      MAKE(COLOR32, 252, 210, 18, 255),
                      FONT_JUSTIFYCENTER);

    if (!ent->currentmove || ent->currentmove->think != ai_birth) {
        memset(&backdrop, 0, sizeof(backdrop));
        backdrop.flags.type = FT_TEXTURE;
        backdrop.color = COLOR32_WHITE;
        backdrop.tex.index = gi.ImageIndex("BuildQueueBackdrop");
        UI_SetFrameRect(&backdrop,
                        BUILDQUEUE_BACKDROP_X,
                        BUILDQUEUE_BACKDROP_Y,
                        BUILDQUEUE_BACKDROP_W,
                        BUILDQUEUE_BACKDROP_H);
        UI_WriteProxyFrame(&backdrop, NULL, 0);
    }

    memset(&firstitem, 0, sizeof(firstitem));
    firstitem.flags.type = FT_TEXTURE;
    firstitem.color = COLOR32_WHITE;
    firstitem.tex.index = gi.ImageIndex(queue[0].art);
    UI_SetFrameRect(&firstitem, BUILDQUEUE_FIRST_X, BUILDQUEUE_FIRST_Y, BUILDQUEUE_FIRST_W, BUILDQUEUE_FIRST_H);
    UI_WriteProxyFrame(&firstitem, NULL, 0);

    memset(&buildtimer, 0, sizeof(buildtimer));
    buildtimer.flags.type = FT_SIMPLESTATUSBAR;
    buildtimer.color = MAKE(COLOR32, 160, 0, 160, 255);
    buildtimer.tex.index = gi.ImageIndex("SimpleBuildTimeIndicator");
    buildtimer.tex.index2 = gi.ImageIndex("SimpleBuildTimeIndicatorBorder");
    UI_SetFrameRect(&buildtimer, BUILDQUEUE_TIMER_X, BUILDQUEUE_TIMER_Y, BUILDQUEUE_TIMER_W, BUILDQUEUE_TIMER_H);
    UI_WriteProxyFrame(&buildtimer, NULL, 0);

    size = sizeof(uiBuildQueue_t) + sizeof(uiBuildQueueItem_t) * count;
    buffer = gi.MemAlloc(size);
    memset(buffer, 0, size);
    buildqueue = (uiBuildQueue_t *)buffer;
    buildqueue->firstitem = (USHORT)firstitem.number;
    buildqueue->buildtimer = (USHORT)buildtimer.number;
    buildqueue->itemoffset = BUILDQUEUE_OFFSET;
    buildqueue->numitems = count;
    FOR_LOOP(i, count) {
        buildqueue->items[i].image = (USHORT)gi.ImageIndex(queue[i].art);
        buildqueue->items[i].starttime = queue[i].starttime;
        buildqueue->items[i].endtime = queue[i].endtime;
    }

    memset(&list, 0, sizeof(list));
    list.flags.type = FT_BUILDQUEUE;
    list.color = COLOR32_WHITE;
    UI_SetFrameRect(&list, BUILDQUEUE_LIST_X, BUILDQUEUE_LIST_Y, BUILDQUEUE_ITEM_W, BUILDQUEUE_ITEM_H);
    UI_WriteProxyFrame(&list, buffer, size);
    gi.MemFree(buffer);
}

static void UI_WriteMultiselect(LPEDICT *ents, DWORD count) {
    DWORD size = sizeof(uiMultiselect_t) + sizeof(uiMultiselectItem_t) * count;
    LPBYTE buffer = gi.MemAlloc(size);
    uiMultiselect_t *multi = (uiMultiselect_t *)buffer;
    uiFrame_t frame;

    memset(buffer, 0, size);
    multi->hp_bar = gi.ImageIndex(Theme_String("SimpleHpBarConsole", "UI\\Widgets\\Console\\Human\\human-statbar-fill.blp"));
    multi->mana_bar = gi.ImageIndex(Theme_String("SimpleManaBarConsole", "UI\\Widgets\\Console\\Human\\human-statbar-fill.blp"));
    multi->offset = MAKE(VECTOR2, 0.031f, 0.050f);
    multi->numcolumns = 6;
    multi->numitems = count;
    FOR_LOOP(i, count) {
        multi->items[i].entity = ents[i]->s.number;
        multi->items[i].image = gi.ImageIndex(FindConfigValue(GetClassName(ents[i]->class_id), STR_ART));
    }

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_MULTISELECT;
    frame.color = COLOR32_WHITE;
    UI_SetFrameRect(&frame, MULTISELECT_X, MULTISELECT_Y, MULTISELECT_SIZE, MULTISELECT_SIZE);
    UI_WriteProxyFrame(&frame, buffer, size);
    gi.MemFree(buffer);
}

void Get_Commands_f(LPEDICT ent) {
    LPEDICT selected = ent && ent->client ? G_GetMainSelectedUnit(ent->client) : NULL;
    gameCommandButton_t buttons[12];
    BYTE count;

    UI_ClearCursorSplat(ent);
    if (!ent || !ent->client || !selected) {
        UI_ClearLayer(ent, LAYER_COMMANDBAR);
        return;
    }
    memset(&ent->client->menu, 0, sizeof(ent->client->menu));

    gi.Write(PF_BYTE, &(LONG){svc_layout});
    gi.Write(PF_BYTE, &(LONG){LAYER_COMMANDBAR});
    ui_next_frame_number = 1;
    count = G_GetCommandButtons(selected, buttons, 12);
    FOR_LOOP(i, count) {
        UI_WriteCommandButtonFrame(&buttons[i]);
    }
    gi.Write(PF_LONG, &(LONG){0});
    gi.Write(PF_SHORT, &(LONG){0});
    gi.unicast(ent);
}

/* Record the HP/mana figures currently reflected by the single-unit info panel
 * so G_RefreshInfoPanel only re-sends LAYER_INFOPANEL when a value changes. */
static void UI_SeedInfoPanelCache(LPEDICT ent, LPEDICT *selected, DWORD count) {
    if (!ent->client) {
        return;
    }
    if (count == 1 && !selected[0]->build) {
        ent->client->infopanel.entity = selected[0]->s.number;
        ent->client->infopanel.hp = (LONG)(selected[0]->health.value + 0.5f);
        ent->client->infopanel.mana = (LONG)(selected[0]->mana.value + 0.5f);
        ent->client->infopanel.xp = (LONG)selected[0]->hero.xp;
    } else {
        ent->client->infopanel.entity = 0;
    }
}

/* Build and unicast the LAYER_INFOPANEL layer for the player's current
 * selection. Shared by the full portrait refresh and the per-frame HP/mana
 * resend so both keep the change-detection cache consistent. */
static void UI_SendInfoPanel(LPEDICT ent, LPEDICT *selected, DWORD count) {
    gi.Write(PF_BYTE, &(LONG){svc_layout});
    gi.Write(PF_BYTE, &(LONG){LAYER_INFOPANEL});
    ui_next_frame_number = 1;
    if (count == 1) {
        if (selected[0]->build) {
            UI_WriteBuildQueue(selected[0]);
        } else {
            UI_WriteSingleInfo(selected[0]);
        }
    } else if (count > 1) {
        UI_WriteMultiselect(selected, count);
    }
    gi.Write(PF_LONG, &(LONG){0});
    gi.Write(PF_SHORT, &(LONG){0});
    gi.unicast(ent);
    UI_SeedInfoPanelCache(ent, selected, count);
}

void Get_Portrait_f(LPEDICT ent) {
    LPEDICT selected[MAX_SELECTED_ENTITIES];
    DWORD count;

    if (!ent || !ent->client) {
        return;
    }

    count = UI_SelectedUnits(ent->client, selected, MAX_SELECTED_ENTITIES);

    gi.Write(PF_BYTE, &(LONG){svc_layout});
    gi.Write(PF_BYTE, &(LONG){LAYER_PORTRAIT});
    ui_next_frame_number = 1;
    if (count == 1) {
        UI_WritePortraitFrame(selected[0]);
    }
    gi.Write(PF_LONG, &(LONG){0});
    gi.Write(PF_SHORT, &(LONG){0});
    gi.unicast(ent);

    UI_SendInfoPanel(ent, selected, count);

    gi.Write(PF_BYTE, &(LONG){svc_layout});
    gi.Write(PF_BYTE, &(LONG){LAYER_INVENTORY});
    ui_next_frame_number = 1;
    if (count == 1) {
        UI_WriteInventory(selected[0]);
    }
    gi.Write(PF_LONG, &(LONG){0});
    gi.Write(PF_SHORT, &(LONG){0});
    gi.unicast(ent);
}

/* Re-send the single-unit info panel for one player if the displayed HP or mana
 * has changed since it was last sent. The portrait and inventory layers are left
 * alone; multiselect bars and the build-queue timer already update client-side. */
void G_RefreshInfoPanel(LPEDICT ent) {
    LPEDICT selected[MAX_SELECTED_ENTITIES];
    DWORD count;
    LONG hp, mana;

    if (!ent || !ent->client) {
        return;
    }
    count = UI_SelectedUnits(ent->client, selected, MAX_SELECTED_ENTITIES);
    if (count != 1 || selected[0]->build) {
        ent->client->infopanel.entity = 0;
        return;
    }
    hp = (LONG)(selected[0]->health.value + 0.5f);
    mana = (LONG)(selected[0]->mana.value + 0.5f);
    if (selected[0]->s.number == ent->client->infopanel.entity &&
        hp == ent->client->infopanel.hp &&
        mana == ent->client->infopanel.mana &&
        (LONG)selected[0]->hero.xp == ent->client->infopanel.xp) {
        return;
    }
    UI_SendInfoPanel(ent, selected, count);
}

/* Once per server frame, keep every player's info panel in sync with the live
 * HP/mana of their selected unit. */
void G_UpdateClientInfoPanels(void) {
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = g_edicts + i;
        if (ent->inuse && ent->client) {
            G_RefreshInfoPanel(ent);
        }
    }
}

/* Re-send LAYER_CONSOLE for one player only when a resource value changed. */
void G_RefreshResourceBar(LPEDICT ent) {
    LPPLAYER ps;
    LONG gold, lumber, food_u, food_c;

    if (!ent || !ent->client)
        return;
    ps     = &ent->client->ps;
    gold   = (LONG)ps->stats[PLAYERSTATE_RESOURCE_GOLD];
    lumber = (LONG)ps->stats[PLAYERSTATE_RESOURCE_LUMBER];
    food_u = (LONG)ps->stats[PLAYERSTATE_RESOURCE_FOOD_USED];
    food_c = (LONG)ps->stats[PLAYERSTATE_RESOURCE_FOOD_CAP];

    if (gold   == ent->client->resourcebar.gold   &&
        lumber == ent->client->resourcebar.lumber  &&
        food_u == ent->client->resourcebar.food_used &&
        food_c == ent->client->resourcebar.food_cap)
        return;

    UI_SendConsoleLayer(ent, food_u);
    ent->client->resourcebar.gold      = gold;
    ent->client->resourcebar.lumber    = lumber;
    ent->client->resourcebar.food_used = food_u;
    ent->client->resourcebar.food_cap  = food_c;
}

void G_UpdateClientResourceBars(void) {
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = g_edicts + i;
        if (ent->inuse && ent->client)
            G_RefreshResourceBar(ent);
    }
}

static DWORD UI_QuestIndex(LPCQUEST quest) {
    DWORD index = 0;

    FOR_EACH_LIST(QUEST, q, level.quests) {
        if (q == quest) {
            return index;
        }
        index++;
    }
    return index;
}

static DWORD UI_WriteQuestList(BOOL required, LPCQUEST selected, FLOAT x, FLOAT y) {
    DWORD row = 0;

    FOR_EACH_LIST(QUEST, quest, level.quests) {
        char text[256];
        char command[64];
        COLOR32 color;

        if (quest->required != required) {
            continue;
        }
        if (quest->discovered) {
            snprintf(text, sizeof(text), "%s%s",
                     quest == selected ? "> " : "  ",
                     UI_LevelStringSafe(quest->title));
        } else {
            snprintf(text, sizeof(text), "%sUndiscovered Quest", quest == selected ? "> " : "  ");
        }
        color = quest == selected ? MAKE(COLOR32, 252, 210, 18, 255) : COLOR32_WHITE;
        snprintf(command, sizeof(command), "quest %u", (unsigned)UI_QuestIndex(quest));
        UI_WriteCommandTextFrame(x,
                                 y + row * QUEST_ROW_H,
                                 QUEST_LIST_W,
                                 QUEST_ROW_H,
                                 text,
                                 command,
                                 color,
                                 FONT_JUSTIFYLEFT,
                                 HUD_SMALL_FONT_SIZE);
        row++;
    }
    return row;
}

static void UI_WriteQuestItems(LPCQUEST quest, FLOAT x, FLOAT y) {
    DWORD row = 0;

    if (!quest) {
        return;
    }
    FOR_EACH_LIST(QUESTITEM, item, quest->items) {
        char text[512];

        snprintf(text, sizeof(text), "%s %s",
                 item->completed ? "- |cff80ff80" : "-",
                 UI_LevelStringSafe(item->description));
        UI_WriteTextFrameSized(x,
                               y + row * QUEST_ROW_H,
                               QUEST_DETAIL_W,
                               QUEST_ROW_H,
                               text,
                               COLOR32_WHITE,
                               FONT_JUSTIFYLEFT,
                               HUD_SMALL_FONT_SIZE);
        row++;
    }
}

void UI_ShowQuest(LPEDICT ent, LPCQUEST quest) {
    char title[256];
    LPCSTR subtitle;

    if (!ent || !quest || !quest->discovered) {
        return;
    }

    gi.Write(PF_BYTE, &(LONG){svc_layout});
    gi.Write(PF_BYTE, &(LONG){LAYER_QUESTDIALOG});
    ui_next_frame_number = 1;

    UI_WriteBackdropFrame(QUEST_X, QUEST_Y, QUEST_W, QUEST_H, "ToolTipBackground", "ToolTipBorder");

    title[0] = '\0';
    if (level.mapinfo && level.mapinfo->loadingScreenTitle && *level.mapinfo->loadingScreenTitle) {
        snprintf(title, sizeof(title), "%s", UI_LevelStringSafe(level.mapinfo->loadingScreenTitle));
    } else {
        snprintf(title, sizeof(title), "Quests");
    }
    subtitle = level.mapinfo && level.mapinfo->loadingScreenSubtitle && *level.mapinfo->loadingScreenSubtitle
             ? UI_LevelStringSafe(level.mapinfo->loadingScreenSubtitle)
             : "Quest Log";

    UI_WriteTextFrameSized(QUEST_X,
                           QUEST_Y + 0.020f,
                           QUEST_W,
                           0.024f,
                           title,
                           MAKE(COLOR32, 252, 210, 18, 255),
                           FONT_JUSTIFYCENTER,
                           HUD_TITLE_FONT_SIZE);
    UI_WriteTextFrameSized(QUEST_X,
                           QUEST_Y + 0.046f,
                           QUEST_W,
                           0.018f,
                           subtitle,
                           COLOR32_WHITE,
                           FONT_JUSTIFYCENTER,
                           HUD_FONT_SIZE);

    UI_WriteTextFrameSized(QUEST_LIST_X,
                           QUEST_Y + 0.073f,
                           QUEST_LIST_W,
                           0.018f,
                           "Required",
                           MAKE(COLOR32, 252, 210, 18, 255),
                           FONT_JUSTIFYLEFT,
                           HUD_FONT_SIZE);
    UI_WriteQuestList(true, quest, QUEST_LIST_X, QUEST_LIST_Y);
    UI_WriteTextFrameSized(QUEST_LIST_X,
                           QUEST_LIST_Y + 0.150f,
                           QUEST_LIST_W,
                           0.018f,
                           "Optional",
                           MAKE(COLOR32, 252, 210, 18, 255),
                           FONT_JUSTIFYLEFT,
                           HUD_FONT_SIZE);
    UI_WriteQuestList(false, quest, QUEST_LIST_X, QUEST_LIST_Y + 0.172f);

    UI_WriteTextFrameSized(QUEST_DETAIL_X,
                           QUEST_DETAIL_Y,
                           QUEST_DETAIL_W,
                           0.022f,
                           UI_LevelStringSafe(quest->title),
                           MAKE(COLOR32, 252, 210, 18, 255),
                           FONT_JUSTIFYLEFT,
                           HUD_FONT_SIZE);
    UI_WriteTextFrameSized(QUEST_DETAIL_X,
                           QUEST_DETAIL_Y + 0.030f,
                           QUEST_DETAIL_W,
                           0.135f,
                           UI_LevelStringSafe(quest->description),
                           COLOR32_WHITE,
                           FONT_JUSTIFYLEFT,
                           HUD_SMALL_FONT_SIZE);
    UI_WriteQuestItems(quest, QUEST_DETAIL_X, QUEST_DETAIL_Y + 0.185f);
    UI_WriteCommandTextFrame(QUEST_X + QUEST_W - 0.095f,
                             QUEST_Y + QUEST_H - 0.036f,
                             0.070f,
                             0.020f,
                             "Close",
                             "hidequests",
                             MAKE(COLOR32, 252, 210, 18, 255),
                             FONT_JUSTIFYCENTER,
                             HUD_FONT_SIZE);

    gi.Write(PF_LONG, &(LONG){0});
    gi.Write(PF_SHORT, &(LONG){0});
    gi.unicast(ent);
}

void UI_ShowQuests(LPEDICT ent) {
    LPCQUEST fallback = NULL;

    FOR_EACH_LIST(QUEST, q, level.quests) {
        if (!q->discovered) {
            continue;
        }
        if (!fallback) {
            fallback = q;
        }
        if (q->required) {
            UI_ShowQuest(ent, q);
            return;
        }
    }
    if (fallback) {
        UI_ShowQuest(ent, fallback);
    }
}

void UI_HideQuests(LPEDICT ent) {
    UI_ClearLayer(ent, LAYER_QUESTDIALOG);
}
void UI_Init(void) {}
void UI_ShowInterface(LPEDICT ent, BOOL flag, FLOAT duration) {
    (void)duration;
    if (!ent || !ent->client) return;
    ent->client->ps.client_ui_state = flag ? CLIENT_UI_GAME : CLIENT_UI_CINEMATIC;
    if (flag)
        ent->client->ps.uiflags = 1 << LAYER_CINEMATIC;
    else
        ent->client->ps.uiflags = ~(1u << LAYER_CINEMATIC);
}
void UI_ShowMainMenu(LPEDICT ent) { (void)ent; }
void UI_ShowGameInterface(LPEDICT ent) {
    UI_WriteServerConsoleShell(ent);
    UI_WriteCinematicLayer(ent);  /* Match old G_ClientBegin: send cinematic layout once */
}
void UI_ShowText(LPEDICT ent, LPCVECTOR2 pos, LPCSTR text, FLOAT duration) {
    FLOAT x = pos ? pos->x : 0.0500f;
    FLOAT y = QUEST_MESSAGE_Y;
    LPCSTR message = NULL;

    (void)duration;
    if (!ent) {
        return;
    }
    if (x < 0.0f || x > UI_BASE_WIDTH) {
        x = 0.0500f;
    }
    gi.Write(PF_BYTE, &(LONG){svc_layout});
    gi.Write(PF_BYTE, &(LONG){LAYER_MESSAGE});
    ui_next_frame_number = 1;
    message = UI_FormatMessageText(UI_LevelStringSafe(text));
    UI_WriteTextAreaFrame(x,
                          y,
                          QUEST_MESSAGE_W,
                          QUEST_MESSAGE_H,
                          message,
                          COLOR32_WHITE,
                          HUD_FONT_SIZE,
                          0.0f);
    gi.Write(PF_LONG, &(LONG){0});
    gi.Write(PF_SHORT, &(LONG){0});
    gi.unicast(ent);
}

void UI_AddCancelButton(LPEDICT ent) {
    UI_SetCurrentClient(ent ? ent->client : NULL);
    UI_WriteStart(LAYER_COMMANDBAR);
    UI_AddCommandButton(STR_CmdCancel);
    gi.Write(PF_LONG, &(LONG){0});
    gi.Write(PF_SHORT, &(LONG){0});
    gi.unicast(ent);
    UI_SetCurrentClient(NULL);
}

void UI_AddCommandButton(LPCSTR code) {
    UI_AddCommandButtonExtended(code, false, 0);
}

void UI_AddCommandButtonExtended(LPCSTR code, BOOL research, DWORD level) {
    UI_WriteCommandButton(code, research, level);
}

void UI_ClearCreateGameSlots(void) {}
void UI_AddCreateGameSlot(DWORD slot, LPCSTR name, LPCSTR race, LPCSTR color, DWORD team) {
    (void)slot; (void)name; (void)race; (void)color; (void)team;
}

DWORD UI_LoadTexture(LPCSTR path, BOOL forcewrap) {
    (void)forcewrap;
    return gi.ImageIndex(path);
}

LPCSTR Theme_String(LPCSTR key, LPCSTR def) {
    LPCSTR value = NULL;
    if (key && !strstr(key, "\\") && game.config.theme) {
        value = FS_FindSheetCell(game.config.theme, "Default", key);
    }
    return value ? value : def;
}

FLOAT Theme_Float(LPCSTR key, LPCSTR def) {
    (void)key;
    return def ? atof(def) : 0.0f;
}

void UI_WriteStart(DWORD layer) {
    gi.Write(PF_BYTE, &(LONG){svc_layout});
    gi.Write(PF_BYTE, &(LONG){layer});
    ui_next_frame_number = 1;
}



void UI_WriteCinematicLayer(LPEDICT ent) {
    LPPLAYER ps;

    if (!ent || !ent->client) return;
    ps = &ent->client->ps;

    FLOAT const bot_h = 0.14f, top_h = 0.0275f;
    FLOAT const bot_y = UI_BASE_HEIGHT - bot_h;
    FLOAT const pp_x = 0.014f, pp_y = bot_y + 0.014f, pp_w = 0.116f, pp_h = 0.116f;
    FLOAT const pc_x = pp_x - 0.014f, pc_y = pp_y - 0.014f, pc_w = pp_w + 0.028f, pc_h = pp_h + 0.028f;
    FLOAT const t_x = pc_x + pc_w + 0.015f, t_y = pp_y + 0.005f;
    FLOAT const t_w = UI_BASE_WIDTH - t_x - 0.010f;

    gi.Write(PF_BYTE, &(LONG){svc_layout});
    gi.Write(PF_BYTE, &(LONG){LAYER_CINEMATIC});
    ui_next_frame_number = 1;

    /* Bottom border bar (edge-only, no bg — CinematicBorder provides the 9-slice decoration) */
    {
        uiFrame_t frame; uiBackdrop_t bd;
        memset(&frame, 0, sizeof(frame)); memset(&bd, 0, sizeof(bd));
        frame.flags.type = FT_BACKDROP; frame.color = COLOR32_WHITE;
        bd.EdgeFile = gi.ImageIndex(Theme_String("CinematicBorder", "CinematicBorder"));
        bd.CornerFlags = (1<<0)|(1<<2)|(1<<1); bd.CornerSize = 0.04f;
        bd.BackgroundSize = 0.256f; bd.BackgroundInsets[1] = 0.01f;
        bd.TileBackground = true; bd.BlendAll = true;
        UI_SetFrameRect(&frame, 0.0f, bot_y, UI_BASE_WIDTH, bot_h);
        UI_WriteProxyFrame(&frame, &bd, sizeof(bd));
    }

    /* Top border bar */
    {
        uiFrame_t frame; uiBackdrop_t bd;
        memset(&frame, 0, sizeof(frame)); memset(&bd, 0, sizeof(bd));
        frame.flags.type = FT_BACKDROP; frame.color = COLOR32_WHITE;
        bd.EdgeFile = gi.ImageIndex(Theme_String("CinematicBorder", "CinematicBorder"));
        bd.CornerFlags = (1<<6)|(1<<8)|(1<<7); bd.CornerSize = 0.04f;
        bd.BackgroundSize = 0.256f; bd.BackgroundInsets[2] = 0.01f;
        bd.TileBackground = true; bd.BlendAll = true;
        UI_SetFrameRect(&frame, 0.0f, 0.0f, UI_BASE_WIDTH, top_h);
        UI_WriteProxyFrame(&frame, &bd, sizeof(bd));
    }

    /* Portrait cover — backdrop border around the portrait (sent whenever portrait is sent) */
    if (ps->cinematic_portrait) {
        uiFrame_t f; uiBackdrop_t bd;
        memset(&f, 0, sizeof(f)); memset(&bd, 0, sizeof(bd));
        f.flags.type = FT_BACKDROP; f.color = COLOR32_WHITE;
        bd.Background = gi.ImageIndex("UI\\Widgets\\EscMenu\\Human\\blank-background.blp");
        bd.EdgeFile = gi.ImageIndex("UI\\Widgets\\EscMenu\\Human\\human-options-menu-border.blp");
        bd.CornerFlags = 0x1ff; bd.CornerSize = 0.048f; bd.BackgroundSize = 0.256f;
        FOR_LOOP(i, 4) bd.BackgroundInsets[i] = 0.01f;
        bd.TileBackground = true; bd.BlendAll = true;
        UI_SetFrameRect(&f, pc_x, pc_y, pc_w, pc_h);
        UI_WriteProxyFrame(&f, &bd, sizeof(bd));

    /* Portrait model */
        uiFrame_t frame;
        memset(&frame, 0, sizeof(frame));
        frame.flags.type = FT_PORTRAIT;
        frame.color = COLOR32_WHITE;
        frame.tex.index = ps->cinematic_portrait;
        frame.text = "Portrait";
        UI_SetFrameRect(&frame, pp_x, pp_y, pp_w, pp_h);
        UI_WriteProxyFrame(&frame, NULL, 0);
    }

    /* Speaker text — golden yellow (EscMenuTitleTextTemplate FontColor 0.99 0.827 0.0705), 15px */
    if (ps->texts[PLAYERTEXT_SPEAKER] && ps->texts[PLAYERTEXT_SPEAKER][0]) {
        uiFrame_t frame; uiLabel_t label;
        memset(&frame, 0, sizeof(frame)); memset(&label, 0, sizeof(label));
        frame.flags.type = FT_STRING;
        frame.color = MAKE(COLOR32, 252, 211, 18, 255);
        frame.stat = MAX_STATS + PLAYERTEXT_SPEAKER;
        label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", 15);
        label.textalignx = FONT_JUSTIFYLEFT; label.textaligny = FONT_JUSTIFYTOP;
        UI_SetFrameRect(&frame, t_x, t_y, t_w, 0.020f);
        UI_WriteProxyFrame(&frame, &label, sizeof(label));
    }

    /* Dialogue text — white, 15px */
    if (ps->texts[PLAYERTEXT_DIALOGUE] && ps->texts[PLAYERTEXT_DIALOGUE][0]) {
        uiFrame_t frame; uiTextArea_t textarea;
        memset(&frame, 0, sizeof(frame)); memset(&textarea, 0, sizeof(textarea));
        frame.flags.type = FT_TEXTAREA; frame.color = COLOR32_WHITE;
        frame.stat = MAX_STATS + PLAYERTEXT_DIALOGUE;
        textarea.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", 15);
        textarea.inset = 0.005f;
        UI_SetFrameRect(&frame, t_x, t_y + 0.024f, t_w, bot_h - 0.050f);
        UI_WriteProxyFrame(&frame, &textarea, sizeof(textarea));
    }

    gi.Write(PF_LONG, &(LONG){0});
    gi.Write(PF_SHORT, &(LONG){0});
    gi.unicast(ent);
}

static void UI_ClearCursorSplat(LPEDICT ent) {
    FLOAT radius = 0.0f;

    if (!ent || !ent->client) {
        return;
    }
    gi.Write(PF_BYTE, &(LONG){ svc_cursor_splat });
    gi.Write(PF_SHORT, &(LONG){ 0 });
    gi.Write(PF_FLOAT, &radius);
    gi.unicast(ent);
}
