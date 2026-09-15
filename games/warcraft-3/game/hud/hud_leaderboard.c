#include "hud_local.h"

#define LEADERBOARD_FALLBACK_WIDTH 0.180f
#define LEADERBOARD_EDGE_INSET     0.006f
#define LEADERBOARD_TOP_PAD        0.004f
#define LEADERBOARD_BOTTOM_PAD     0.004f
#define LEADERBOARD_TITLE_GAP      0.002f
#define LEADERBOARD_TEXT_HEIGHT    0.012f

static void LeaderboardItemText(LPCLEADERBOARD board, struct gleaderboarditem_s const *item,
                                LPSTR out, size_t out_size) {
    LPPLAYER player = item->player >= 0 ? G_GetPlayerByNumber((DWORD)item->player) : NULL;
    LPCSTR name = board->show_names && player && player->name ? player->name : "";
    LPCSTR label = item->show_label ? item->label : "";
    if (*name && *label) snprintf(out, out_size, "%s - %s", name, label);
    else snprintf(out, out_size, "%s%s", name, label);
}

static void WriteLeaderboardText(DWORD parent, FLOAT x, FLOAT y, FLOAT w, FLOAT h,
                                 LPCSTR text, COLOR32 color, uiFontJustificationH_t align) {
    uiFrame_t frame;
    uiLabel_t label;
    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    frame.flags.type = FT_STRING;
    frame.parent = parent;
    frame.text = text;
    frame.color = color.a ? color : COLOR32_WHITE;
    label.font = hud.leaderboard.LeaderboardTitle
        ? UI_LiveFont(hud.leaderboard.LeaderboardTitle->Font.Index)
        : gi.FontIndex("Fonts\\FRIZQT__.TTF", HUD_FONT_SIZE);
    label.textalignx = align;
    label.textaligny = FONT_JUSTIFYMIDDLE;
    UI_SetFrameRect(&frame, x, y, w, h);
    frame.points.x[FPP_MIN].relativeTo = UI_PARENT;
    frame.points.y[FPP_MIN].relativeTo = UI_PARENT;
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

static void ResetFramePoints(LPFRAMEDEF frame) {
    if (!frame) return;
    memset(&frame->Points, 0, sizeof(frame->Points));
    frame->AnyPointsSet = false;
}

void UI_LoadHudLeaderboards(void) {
    if (!LeaderBoard_Load(&hud.leaderboard)) {
        fprintf(stderr, "WC3 HUD: missing LeaderBoard.fdf\n");
        return;
    }

    /* Use the same full-screen widescreen anchor and edge offsets as the
     * TimerDialog so both HUD types start at the same top-right position. */
    memset(&hud.leaderboard_anchor, 0, sizeof(hud.leaderboard_anchor));
    hud.leaderboard_anchor.Type = FT_SIMPLEFRAME;
    hud.leaderboard_anchor.ui_flags |= UIFLAG_EXTEND_WIDESCREEN_X;
    UI_SetSize(&hud.leaderboard_anchor, UI_BASE_WIDTH, UI_BASE_HEIGHT);
    UI_SetPoint(&hud.leaderboard_anchor,
                FRAMEPOINT_TOPLEFT, NULL, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);

    if (hud.leaderboard.Leaderboard) {
        hud.leaderboard_width = hud.leaderboard.Leaderboard->Width > 0.0f
            ? hud.leaderboard.Leaderboard->Width : LEADERBOARD_FALLBACK_WIDTH;
        ResetFramePoints(hud.leaderboard.Leaderboard);
        UI_SetPoint(hud.leaderboard.Leaderboard,
                    FRAMEPOINT_TOPRIGHT, &hud.leaderboard_anchor, FRAMEPOINT_TOPRIGHT,
                    -HUD_HERO_SHORTCUT_EDGE_X, -HUD_HERO_SHORTCUT_TOP_Y);
    } else {
        hud.leaderboard_width = LEADERBOARD_FALLBACK_WIDTH;
    }

    if (hud.leaderboard.LeaderboardTitle) {
        hud.leaderboard_default_title_color = hud.leaderboard.LeaderboardTitle->Font.Color;
        hud.leaderboard_default_item_color = hud.leaderboard.LeaderboardTitle->Font.Color;
    }
}

void UI_WriteLeaderboard(LPEDICT ent) {
    LPLEADERBOARD board;
    LPFRAMEDEF root, backdrop, title, container;
    FLOAT row_height, title_height, content_width, total_height, list_top;
    DWORD player, rows, visible_rows, parent;

    if (!ent || !ent->client) return;
    player = ent->client->ps.number;
    board = G_PlayerLeaderboard(player);
    root = hud.leaderboard.Leaderboard;
    backdrop = hud.leaderboard.LeaderboardBackdrop;
    title = hud.leaderboard.LeaderboardTitle;
    container = hud.leaderboard.LeaderboardListContainer;
    if (!board || !(board->displayed_clients & (1u << player)) || !root || !container) {
        UI_ClearLayer(ent, WC3_LAYER_LEADERBOARD);
        return;
    }

    rows = board->size_by_item_count >= 0 ? (DWORD)board->size_by_item_count : board->item_count;
    rows = MAX(1u, MIN(rows, (DWORD)MAX_LEADERBOARD_ITEMS));
    visible_rows = MAX(1u, MIN(board->item_count, rows));

    /* The stock list container reserves substantially more vertical space than
     * a campaign counter needs. Size the visible board to its actual rows so a
     * one-line objective is only one text row tall instead of several blanks. */
    row_height = title && title->Font.Size > 0.0f
        ? MAX(LEADERBOARD_TEXT_HEIGHT, title->Font.Size * 1.25f)
        : LEADERBOARD_TEXT_HEIGHT;
    title_height = board->show_label ? row_height : 0.0f;
    content_width = MAX(0.02f, hud.leaderboard_width - 2.0f * LEADERBOARD_EDGE_INSET);
    list_top = LEADERBOARD_TOP_PAD + title_height + (title_height > 0.0f ? LEADERBOARD_TITLE_GAP : 0.0f);
    total_height = list_top + visible_rows * row_height + LEADERBOARD_BOTTOM_PAD;

    UI_SetHidden(root, false);
    UI_SetSize(root, hud.leaderboard_width, total_height);

    if (backdrop) {
        UI_SetHidden(backdrop, false);
        UI_SetSize(backdrop, hud.leaderboard_width, total_height);
        ResetFramePoints(backdrop);
        UI_SetPoint(backdrop, FRAMEPOINT_TOPLEFT, root, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
        UI_SetPoint(backdrop, FRAMEPOINT_BOTTOMRIGHT, root, FRAMEPOINT_BOTTOMRIGHT, 0.0f, 0.0f);
    }

    if (title) {
        UI_SetHidden(title, !board->show_label);
        UI_SetText(title, "%s", board->label[0] ? board->label : " ");
        title->Font.Color = board->label_color_set
            ? board->label_color : hud.leaderboard_default_title_color;
        if (board->show_label) {
            UI_SetSize(title, content_width, title_height);
            ResetFramePoints(title);
            UI_SetPoint(title, FRAMEPOINT_TOPLEFT, root, FRAMEPOINT_TOPLEFT,
                        LEADERBOARD_EDGE_INSET, -LEADERBOARD_TOP_PAD);
        }
    }

    UI_SetHidden(container, false);
    UI_SetSize(container, content_width, visible_rows * row_height);
    ResetFramePoints(container);
    UI_SetPoint(container, FRAMEPOINT_TOPLEFT, root, FRAMEPOINT_TOPLEFT,
                LEADERBOARD_EDGE_INSET, -list_top);

    UI_SetCurrentClient(ent->client);
    UI_WriteStart(WC3_LAYER_LEADERBOARD);
    UI_WriteFrame(&hud.leaderboard_anchor);
    UI_WriteFrameWithChildren(root, &hud.leaderboard_anchor);
    parent = UI_GetWrittenFrameNumber(container);
    FOR_LOOP(i, MIN(board->item_count, rows)) {
        struct gleaderboarditem_s const *item = &board->items[i];
        char text[MAX_TRIGSTR_LENGTH], number[32];
        COLOR32 label_color = item->label_color_set ? item->label_color : hud.leaderboard_default_item_color;
        COLOR32 value_color = item->value_color_set ? item->value_color :
            (board->value_color_set ? board->value_color : hud.leaderboard_default_item_color);
        LeaderboardItemText(board, item, text, sizeof(text));
        snprintf(number, sizeof(number), "%ld", (long)item->value);
        WriteLeaderboardText(parent, 0.0f, (FLOAT)i * row_height, content_width * 0.74f, row_height,
                             text[0] ? text : " ", label_color, FONT_JUSTIFYLEFT);
        WriteLeaderboardText(parent, content_width * 0.76f, (FLOAT)i * row_height,
                             content_width * 0.24f, row_height,
                             item->show_value && board->show_values ? number : " ",
                             value_color, FONT_JUSTIFYRIGHT);
    }
    UI_WriteEnd(ent);
    UI_SetCurrentClient(NULL);
}
