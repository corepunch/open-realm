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
    uiBackdrop_t backdrop = { 0 };
    uiLabel_t label = { 0 };
    LPCSTR black = "Textures\\Black32.blp";
    LPCSTR hp = Theme_String("SimpleHpBarConsoleSmall", "SimpleHpBarConsoleSmall");
    LPCSTR mana = Theme_String("SimpleManaBarConsoleSmall", "SimpleManaBarConsoleSmall");

    if (!ent || !ent->client || !ent->client->connected) return;
    UI_WriteStart(LAYER_WORLD_HOVER);

    frame.flags.type = FT_BACKDROP; frame.color = COLOR32_WHITE;
    backdrop.Background = gi.ImageIndex(Theme_String("ToolTipBackground", "ToolTipBackground"));
    backdrop.EdgeFile = gi.ImageIndex(Theme_String("ToolTipBorder", "ToolTipBorder"));
    backdrop.CornerFlags = 0x1ff; backdrop.CornerSize = 0.010f; backdrop.BackgroundSize = 0.036f;
    backdrop.BackgroundInsets[0] = backdrop.BackgroundInsets[1] = 0.0019f;
    backdrop.BackgroundInsets[2] = backdrop.BackgroundInsets[3] = 0.0019f;
    backdrop.TileBackground = true; backdrop.BlendAll = true;
    UI_SetFrameRect(&frame, -0.060f, -0.047f, 0.120f, 0.027f);
    UI_WriteProxyFrame(&frame, &backdrop, sizeof(backdrop));

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_STRING; frame.stat = UI_STAT_CONTEXT_NAME; frame.color = COLOR32_WHITE;
    label.font = gi.FontIndex(Theme_String("MasterFont", "Fonts\\FRIZQT__.TTF"), HUD_FONT_SIZE);
    label.textalignx = FONT_JUSTIFYCENTER; label.textaligny = FONT_JUSTIFYMIDDLE;
    UI_SetFrameRect(&frame, -0.058f, -0.045f, 0.116f, 0.023f);
    UI_WriteProxyFrame(&frame, &label, sizeof(label));

    UI_WriteHoverTexture(-0.0225f, -0.019f, 0.045f, 0.008f, black, UI_STAT_CONTEXT_MANA,
                         MAKE(COLOR32, 0, 0, 0, 220));
    UI_WriteHoverBar(-0.0215f, -0.018f, 0.043f, 0.006f, mana, UI_STAT_CONTEXT_MANA,
                     MAKE(COLOR32, 60, 90, 235, 255));
    UI_WriteHoverTexture(-0.0225f, -0.010f, 0.045f, 0.008f, black, 0, MAKE(COLOR32, 0, 0, 0, 220));
    UI_WriteHoverBar(-0.0215f, -0.009f, 0.043f, 0.006f, hp, UI_STAT_CONTEXT_HEALTH,
                     MAKE(COLOR32, 80, 200, 80, 255));
    UI_WriteEnd(ent);
}