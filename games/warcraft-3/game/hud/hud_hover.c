/*
 * hud_hover.c — Server-authored world-hover nameplate and resource bars.
 *
 * Retail creates these frames at runtime rather than loading an FDF template.
 * The client only projects the hovered snapshot entity and evaluates bindings.
 */

#include "hud_local.h"

/* Write one texture inside a context-bound hover row. */
static void UI_WriteHoverTexture(uint32_t parent, float y, float w, float h, cstring_t art, uint32_t stat, color32_t color) {
    uiFrame_t frame = { 0 };

    frame.flags.type = FT_TEXTURE; frame.tex.index = gi.ImageIndex(art); frame.stat = stat; frame.color = color;
    frame.size.width = w; frame.size.height = h;
    UI_SetFramePoint(&frame.points.x[FPP_MID], FPP_MID, UI_PARENT, 0.0f, false);
    UI_SetFramePoint(&frame.points.y[FPP_MIN], FPP_MIN, UI_PARENT, y, true);
    UI_WriteProxyFrameToParent(&frame, NULL, 0, parent);
}

/* Write a fill bar whose fraction is resolved from the hovered snapshot entity. */
static void UI_WriteHoverBar(uint32_t parent, float y, float w, float h, cstring_t art, uint32_t stat, color32_t color) {
    uiFrame_t frame = { 0 };

    frame.flags.type = FT_SIMPLESTATUSBAR; frame.tex.index = gi.ImageIndex(art); frame.stat = stat; frame.color = color;
    frame.size.width = w; frame.size.height = h;
    UI_SetFramePoint(&frame.points.x[FPP_MID], FPP_MID, UI_PARENT, 0.0f, false);
    UI_SetFramePoint(&frame.points.y[FPP_MIN], FPP_MIN, UI_PARENT, y, true);
    UI_WriteProxyFrameToParent(&frame, NULL, 0, parent);
}

/* A context row contributes its full authored height only while the client has
 * that live hover capability. The shared layout solver collapses it otherwise. */
static uint32_t UI_WriteHoverRow(uint32_t stat, uint32_t below, float height) {
    uiFrame_t frame = { 0 };
    uint32_t const number = ui_next_frame_number;

    frame.flags.type = FT_FRAME; frame.stat = stat;
    frame.size.width = 0.045f; frame.size.height = height;
    UI_SetFramePoint(&frame.points.x[FPP_MID], FPP_MID, 0, 0.0f, false);
    UI_SetFramePoint(&frame.points.y[FPP_MAX], FPP_MIN, below, 0.0f, true);
    UI_WriteProxyFrame(&frame, NULL, 0);
    return number;
}

static cstring_t UI_HoverResourceLabel(void) {
    static bool warned;
    cstring_t label = UI_GetString("COLON_GOLD");

    if (label && strcmp(label, "COLON_GOLD")) return label;
    if (!warned) {
        fprintf(stderr, "UI_WC3: missing GlobalStrings entry COLON_GOLD; using Gold: fallback\n");
        warned = true;
    }
    return "Gold:";
}

/* Retail COccupUI is a cargo CStatBar. This hover stack places it below HP/MP;
 * capacity owns the slot count while filled and empty slots use separate art. */
static uint32_t UI_WriteHoverCargoBar(cstring_t filled_art, cstring_t empty_art) {
    uiFrame_t frame = { 0 };
    uint32_t const number = ui_next_frame_number;

    frame.flags.type = FT_SEGMENTED_STATUSBAR;
    frame.tex.index = gi.ImageIndex(filled_art);
    frame.tex.index2 = gi.ImageIndex(empty_art);
    frame.stat = ENT_CARGO;
    frame.color = MAKE(color32_t, 255, 204, 0, 255);
    frame.value = 0.001f; /* separation between capacity-sized segments */
    frame.size.width = 0.043f; frame.size.height = 0.004f;
    UI_SetFramePoint(&frame.points.x[FPP_MID], FPP_MID, 0, 0.0f, false);
    UI_SetFramePoint(&frame.points.y[FPP_MAX], FPP_MIN, 0, -0.002f, true);
    UI_WriteProxyFrame(&frame, NULL, 0);
    return number;
}

/* The server owns the complete widget; only its declared context changes at draw time. */
void UI_WriteHoverLayout(edict_t *ent) {
    uiFrame_t frame = { 0 };
    cstring_t black = "Textures\\Black32.blp";
    cstring_t hp = "SimpleHpBarConsoleSmall";
    cstring_t mana = "SimpleManaBarConsoleSmall";
    uint32_t cargo, mana_row, health_row;

    if (!ent || !ent->client || !ent->client->connected) return;
    UI_WriteStart(LAYER_WORLD_HOVER);

    /* Build bottom-up. Missing context rows collapse to zero client-side, so
     * every remaining element stays adjacent without a hover-time layout RPC. */
    cargo = UI_WriteHoverCargoBar(hp, black);
    mana_row = UI_WriteHoverRow(UI_STAT_CONTEXT_MANA, cargo, 0.009f);
    UI_WriteHoverTexture(mana_row, 0.0f, 0.045f, 0.008f, black, UI_STAT_CONTEXT_MANA,
                         MAKE(color32_t, 0, 0, 0, 220));
    UI_WriteHoverBar(mana_row, 0.001f, 0.043f, 0.006f, mana, UI_STAT_CONTEXT_MANA,
                     MAKE(color32_t, 60, 90, 235, 255));
    health_row = UI_WriteHoverRow(UI_STAT_CONTEXT_HEALTH, mana_row, 0.009f);
    UI_WriteHoverTexture(health_row, 0.0f, 0.045f, 0.008f, black, UI_STAT_CONTEXT_HEALTH,
                         MAKE(color32_t, 0, 0, 0, 220));
    UI_WriteHoverBar(health_row, 0.001f, 0.043f, 0.006f, hp, UI_STAT_CONTEXT_HEALTH,
                     MAKE(color32_t, 80, 200, 80, 255));

    frame.flags.type = FT_NAMETAG; frame.flagsvalue |= UIFLAG_SIZE_TO_CONTENT; frame.stat = UI_STAT_CONTEXT_NAME;
    frame.color = COLOR32_WHITE;
    frame.text = UI_HoverResourceLabel();
    uiNameTag_t data = MAKE(uiNameTag_t,
        .background = MAKE(uiBackdrop_t,
            .Background = gi.ImageIndex("ToolTipBackground"),
            .EdgeFile = gi.ImageIndex("ToolTipBorder"),
            .CornerFlags = 0x1ff,
            .CornerSize = 0.010f,
            .BackgroundSize = 0.036f,
            .BackgroundInsets = { 0.0019f, 0.0019f, 0.0019f, 0.0019f },
            .TileBackground = true,
            .BlendAll = true),
        .text = MAKE(uiLabel_t,
            .font = gi.FontIndex(Theme_String("MasterFont", "Fonts\\FRIZQT__.TTF"), HUD_FONT_SIZE),
            .textalignx = FONT_JUSTIFYCENTER,
            .textaligny = FONT_JUSTIFYMIDDLE),
        .padding_x = 0.008f,
        .padding_y = 0.006f);
    UI_SetFramePoint(&frame.points.x[FPP_MID], FPP_MID, 0, 0.0f, false);
    UI_SetFramePoint(&frame.points.y[FPP_MAX], FPP_MIN, health_row, 0.0f, true);
    UI_WriteProxyFrame(&frame, &data, sizeof(data));

    UI_WriteEnd(ent);
}
