/*
 * hud_hover.c — Server-authored world-hover nameplate and resource bars.
 *
 * Retail creates these frames at runtime rather than loading an FDF template.
 * The client only projects the hovered snapshot entity and evaluates bindings.
 */

#include "hud_local.h"

/* Write one texture relative to the projected entity-context root. */
static void UI_WriteHoverTexture(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR art, DWORD stat, COLOR32 color) {
    uiFrame_t frame = { 0 };

    frame.flags.type = FT_TEXTURE; frame.tex.index = gi.ImageIndex(art); frame.stat = stat; frame.color = color;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

/* Write a fill bar whose fraction is resolved from the hovered snapshot entity. */
static void UI_WriteHoverBar(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR art, DWORD stat, COLOR32 color) {
    uiFrame_t frame = { 0 };

    frame.flags.type = FT_SIMPLESTATUSBAR; frame.tex.index = gi.ImageIndex(art); frame.stat = stat; frame.color = color;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

/* The server owns the complete widget; only its declared context changes at draw time. */
void UI_WriteHoverLayout(LPEDICT ent) {
    uiFrame_t frame = { 0 };
    LPCSTR black = "Textures\\Black32.blp";
    LPCSTR hp = Theme_String("SimpleHpBarConsoleSmall", "SimpleHpBarConsoleSmall");
    LPCSTR mana = Theme_String("SimpleManaBarConsoleSmall", "SimpleManaBarConsoleSmall");
    FLOAT const bar_y = -0.010f;

    if (!ent || !ent->client || !ent->client->connected) return;
    UI_WriteStart(LAYER_WORLD_HOVER);

    frame.flags.type = FT_NAMETAG; frame.flagsvalue |= UIFLAG_SIZE_TO_CONTENT; frame.stat = UI_STAT_CONTEXT_NAME;
    frame.color = COLOR32_WHITE;
    uiNameTag_t data = { 0 };
    data.background.Background = gi.ImageIndex(Theme_String("ToolTipBackground", "ToolTipBackground"));
    data.background.EdgeFile = gi.ImageIndex(Theme_String("ToolTipBorder", "ToolTipBorder"));
    data.background.CornerFlags = 0x1ff; data.background.CornerSize = 0.010f; data.background.BackgroundSize = 0.036f;
    data.background.BackgroundInsets[0] = data.background.BackgroundInsets[1] = 0.0019f;
    data.background.BackgroundInsets[2] = data.background.BackgroundInsets[3] = 0.0019f;
    data.background.TileBackground = true; data.background.BlendAll = true;
    data.text.font = gi.FontIndex(Theme_String("MasterFont", "Fonts\\FRIZQT__.TTF"), HUD_FONT_SIZE);
    data.text.textalignx = FONT_JUSTIFYCENTER; data.text.textaligny = FONT_JUSTIFYMIDDLE;
    data.padding_x = 0.008f; data.padding_y = 0.006f;
    UI_SetFramePoint(&frame.points.x[FPP_MID], FPP_MID, 0, 0.0f, false);
    UI_SetFramePoint(&frame.points.y[FPP_MIN], FPP_MIN, 0, -0.047f, true);
    UI_WriteProxyFrame(&frame, &data, sizeof(data));

    if (ent->mana.value > 0.0f) {
        UI_WriteHoverTexture(-0.0225f, -0.019f, 0.045f, 0.008f, black, UI_STAT_CONTEXT_MANA, MAKE(COLOR32, 0, 0, 0, 220));
        UI_WriteHoverBar(-0.0215f, -0.018f, 0.043f, 0.006f, mana, UI_STAT_CONTEXT_MANA, MAKE(COLOR32, 60, 90, 235, 255));
    }
    UI_WriteHoverTexture(-0.0225f, bar_y, 0.045f, 0.008f, black, 0, MAKE(COLOR32, 0, 0, 0, 220));
    UI_WriteHoverBar(-0.0215f, bar_y + 0.001f, 0.043f, 0.006f, hp, UI_STAT_CONTEXT_HEALTH, MAKE(COLOR32, 80, 200, 80, 255));
    UI_WriteEnd(ent);
}
