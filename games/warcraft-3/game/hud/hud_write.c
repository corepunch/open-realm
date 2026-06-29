/*
 * hud_write.c — Frame-write primitives, theme lookup, text formatting.
 *
 * Low-level helpers that build uiFrame_t structs and serialize them
 * to the network via gi.Write.  All HUD panels use these to emit frames.
 */

#include "hud_local.h"

DWORD ui_next_frame_number;
LPGAMECLIENT ui_current_client;

LPCSTR UI_LevelStringSafe(LPCSTR text) {
    if (!text || !*text) {
        return " ";
    }
    return G_LevelString(text);
}

void UI_SetCurrentClient(LPGAMECLIENT client) {
    ui_current_client = client;
}

void UI_SetFramePoint(uiFramePoint_t *point, uiFramePointPos_t target, DWORD relative, FLOAT offset, BOOL y_axis) {
    point->used = 1;
    point->targetPos = target;
    point->relativeTo = (BYTE)relative;
    point->offset = (SHORT)((y_axis ? -offset : offset) * UI_FRAMEPOINT_SCALE);
}

void UI_SetFrameRect(LPUIFRAME frame, FLOAT x, FLOAT y, FLOAT w, FLOAT h) {
    UI_SetFramePoint(&frame->points.x[FPP_MIN], FPP_MIN, 0, x, false);
    UI_SetFramePoint(&frame->points.y[FPP_MIN], FPP_MIN, 0, y, true);
    frame->size.width = w;
    frame->size.height = h;
}

void UI_WriteProxyFrame(LPUIFRAME frame, HANDLE data, DWORD data_size) {
    frame->number = ui_next_frame_number++;
    frame->color = frame->color.a ? frame->color : COLOR32_WHITE;
    if (!frame->tex.coord[1] && !frame->tex.coord[3]) {
        frame->tex.coord[1] = 0xff;
        frame->tex.coord[3] = 0xff;
    }
    frame->buffer.data = data;
    frame->buffer.size = data_size;
    gi.Write(PF_UIFRAME, frame);
}

void UI_WriteProxyFrameToParent(LPUIFRAME frame, HANDLE data, DWORD data_size, DWORD parent) {
    frame->parent = parent;
    UI_WriteProxyFrame(frame, data, data_size);
}

void UI_SetFramePointRelative(uiFramePoint_t *point, uiFramePointPos_t target, DWORD relative, FLOAT offset, BOOL y_axis) {
    point->used = 1;
    point->targetPos = target;
    point->relativeTo = (BYTE)relative;
    point->offset = (SHORT)((y_axis ? -offset : offset) * UI_FRAMEPOINT_SCALE);
}

void UI_WriteTextFrame(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR text, COLOR32 color,
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

void UI_WriteTextureFrame(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR art) {
    uiFrame_t frame;

    if (!art || !*art) {
        return;
    }
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_TEXTURE;
    frame.color = COLOR32_WHITE;
    frame.tex.index = gi.ImageIndex(art);
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

void UI_WriteTextFrameSized(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR text, COLOR32 color,
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

void UI_WriteCommandTextFrame(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR text, LPCSTR command,
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

void UI_WriteBackdropFrame(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR background, LPCSTR edge) {
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

void UI_WriteTextAreaFrame(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR text, COLOR32 color,
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

void UI_WriteTooltipFrame(void) {
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

void UI_AppendMessageText(LPSTR out, DWORD out_size, LPCSTR text) {
    if (!out || out_size == 0 || !text) {
        return;
    }
    strncat(out, text, out_size - strlen(out) - 1);
}

LPCSTR UI_FormatMessageText(LPCSTR text) {
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
    UI_ResetFrameWriteList();
    gi.Write(PF_BYTE, &(LONG){svc_layout});
    gi.Write(PF_BYTE, &(LONG){layer});
    ui_next_frame_number = 1;
}

void UI_WriteEnd(LPEDICT ent) {
    gi.Write(PF_LONG, &(LONG){0});   /* bits=0 */
    gi.Write(PF_SHORT, &(LONG){0});  /* number=0  — MSG_ReadEntityBits reads LONG+SHORT */
    gi.unicast(ent);
}
