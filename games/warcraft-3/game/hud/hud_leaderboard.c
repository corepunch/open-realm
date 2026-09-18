#include "hud_local.h"

#define LEADERBOARD_EDGE_INSET     0.006f
#define LEADERBOARD_TOP_PAD        0.004f
#define LEADERBOARD_BOTTOM_PAD     0.004f
#define LEADERBOARD_TITLE_GAP      0.002f
#define LEADERBOARD_TEXT_HEIGHT    0.012f
#define LEADERBOARD_MIN_CONTENT_WIDTH 0.020f
#define LEADERBOARD_COLUMN_GAP     "    "

static char leaderboard_measure_text[(MAX_TRIGSTR_LENGTH + 48) * (MAX_LEADERBOARD_ITEMS + 1)];

static void LeaderboardItemText(LPCLEADERBOARD board, struct gleaderboarditem_s const *item,
                                LPSTR out, size_t out_size) {
    LPPLAYER player = item->player >= 0 ? G_GetPlayerByNumber((DWORD)item->player) : NULL;
    LPCSTR name = board->show_names && player && player->name ? player->name : "";
    LPCSTR label = item->show_label ? item->label : "";
    if (*name && *label) snprintf(out, out_size, "%s - %s", name, label);
    else snprintf(out, out_size, "%s%s", name, label);
}

static void WriteLeaderboardText(DWORD parent, FLOAT y, FLOAT h,
                                 LPCSTR text, COLOR32 color, uiFontJustificationH_t align,
                                 BOOL right_anchored) {
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
    frame.size.height = h;
    UI_SetFramePoint(&frame.points.x[right_anchored ? FPP_MAX : FPP_MIN],
                     right_anchored ? FPP_MAX : FPP_MIN,
                     UI_PARENT, 0.0f, false);
    UI_SetFramePoint(&frame.points.y[FPP_MIN], FPP_MIN, UI_PARENT, y, true);
    frame.points.y[FPP_MIN].relativeTo = UI_PARENT;
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

static void ResetFramePoints(LPFRAMEDEF frame) {
    if (!frame) return;
    memset(&frame->Points, 0, sizeof(frame->Points));
    frame->AnyPointsSet = false;
}

static void LeaderboardAppendMeasureLine(LPSTR out, size_t out_size, LPCSTR left, LPCSTR right) {
    size_t used;

    if (!out || out_size == 0) return;
    used = strlen(out);
    if (used && used + 1 < out_size) out[used++] = '\n', out[used] = '\0';
    if (left && *left) strlcat(out, left, out_size);
    if (right && *right) {
        strlcat(out, LEADERBOARD_COLUMN_GAP, out_size);
        strlcat(out, right, out_size);
    }
    if (!out[used]) strlcat(out, " ", out_size);
}

void UI_LoadHudLeaderboards(void) {
    if (!LeaderBoard_Load(&hud.leaderboard)) {
        fprintf(stderr, "WC3 HUD: missing LeaderBoard.fdf\n");
        return;
    }
#ifdef WC3_DEBUG_HUMAN06
    if (WC3_HUMAN06_DEBUG_ENABLED())
        fprintf(stderr, "Human06Diag leaderboard hud_load root=%p backdrop=%p title=%p container=%p\n",
                (void *)hud.leaderboard.Leaderboard, (void *)hud.leaderboard.LeaderboardBackdrop,
                (void *)hud.leaderboard.LeaderboardTitle, (void *)hud.leaderboard.LeaderboardListContainer);
#endif

    /* Use the same full-screen widescreen anchor and edge offsets as the
     * TimerDialog so both HUD types start at the same top-right position. */
    memset(&hud.leaderboard_anchor, 0, sizeof(hud.leaderboard_anchor));
    hud.leaderboard_anchor.Type = FT_SIMPLEFRAME;
    hud.leaderboard_anchor.ui_flags |= UIFLAG_EXTEND_WIDESCREEN_X;
    UI_SetSize(&hud.leaderboard_anchor, UI_BASE_WIDTH, UI_BASE_HEIGHT);
    UI_SetPoint(&hud.leaderboard_anchor,
                FRAMEPOINT_TOPLEFT, NULL, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);

    if (hud.leaderboard.Leaderboard) {
        ResetFramePoints(hud.leaderboard.Leaderboard);
        UI_SetPoint(hud.leaderboard.Leaderboard,
                    FRAMEPOINT_TOPRIGHT, &hud.leaderboard_anchor, FRAMEPOINT_TOPRIGHT,
                    -HUD_HERO_SHORTCUT_EDGE_X, -HUD_HERO_SHORTCUT_TOP_Y);
    }

    if (hud.leaderboard.LeaderboardTitle) {
        hud.leaderboard_default_title_color = hud.leaderboard.LeaderboardTitle->Font.Color;
        hud.leaderboard_default_item_color = hud.leaderboard.LeaderboardTitle->Font.Color;
    }
}

void UI_WriteLeaderboard(LPEDICT ent) {
    LPLEADERBOARD board;
    LPFRAMEDEF root, backdrop, title, container;
    FLOAT row_height, title_height, total_height, list_top, top_y;
    BOOL has_title;
    DWORD player, rows, visible_rows, parent, measure_font;

    if (!ent || !ent->client) return;
    player = ent->client->ps.number;
    board = G_PlayerLeaderboard(player);
    root = hud.leaderboard.Leaderboard;
    backdrop = hud.leaderboard.LeaderboardBackdrop;
    title = hud.leaderboard.LeaderboardTitle;
    container = hud.leaderboard.LeaderboardListContainer;
    if (!board || !G_IsLeaderboardDisplayed(board, &ent->client->ps) || !root || !container) {
#ifdef WC3_DEBUG_HUMAN06
        if (WC3_HUMAN06_DEBUG_ENABLED())
            fprintf(stderr, "Human06Diag leaderboard skip client=%u board=%p displayed=%d root=%p container=%p\n",
                    player, (void *)board, board ? (int)G_IsLeaderboardDisplayed(board, &ent->client->ps) : 0,
                    (void *)root, (void *)container);
#endif
        UI_ClearLayer(ent, WC3_LAYER_LEADERBOARD);
        return;
    }

    /* Leaderboard-only HUDs keep the stock top position. When this client has
     * a visible timer dialog, place the board below that dialog instead of
     * letting the two server-authored layers overlap. */
    top_y = HUD_HERO_SHORTCUT_TOP_Y + UI_TimerDialogLeaderboardOffset(player);
    UI_SetPoint(root, FRAMEPOINT_TOPRIGHT, &hud.leaderboard_anchor, FRAMEPOINT_TOPRIGHT,
                -HUD_HERO_SHORTCUT_EDGE_X, -top_y);
#ifdef WC3_DEBUG_HUMAN06
    if (WC3_HUMAN06_DEBUG_ENABLED())
        fprintf(stderr, "Human06Diag leaderboard draw client=%u board=%p items=%u label=\"%s\" displayed=0x%08x\n",
                player, (void *)board, board->item_count, board->label, board->displayed_clients);
#endif

    rows = board->size_by_item_count >= 0 ? (DWORD)board->size_by_item_count : board->item_count;
    rows = MAX(1u, MIN(rows, (DWORD)MAX_LEADERBOARD_ITEMS));
    visible_rows = MAX(1u, MIN(board->item_count, rows));
    has_title = board->show_label && board->label[0];

    /* The stock list container reserves substantially more vertical space than
     * a campaign counter needs. Size the visible board to its actual rows so a
     * one-line objective is only one text row tall instead of several blanks. */
    row_height = title && title->Font.Size > 0.0f
        ? MAX(LEADERBOARD_TEXT_HEIGHT, title->Font.Size * 1.25f)
        : LEADERBOARD_TEXT_HEIGHT;
    title_height = has_title ? row_height : 0.0f;
    list_top = LEADERBOARD_TOP_PAD + title_height + (title_height > 0.0f ? LEADERBOARD_TITLE_GAP : 0.0f);
    total_height = list_top + visible_rows * row_height + LEADERBOARD_BOTTOM_PAD;

    UI_SetHidden(root, false);
    root->Height = total_height;

    if (backdrop) {
        UI_SetHidden(backdrop, false);
        ResetFramePoints(backdrop);
        UI_SetPoint(backdrop, FRAMEPOINT_TOPLEFT, root, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
        UI_SetPoint(backdrop, FRAMEPOINT_BOTTOMRIGHT, root, FRAMEPOINT_BOTTOMRIGHT, 0.0f, 0.0f);
    }

    if (title) {
        UI_SetHidden(title, !has_title);
        UI_SetText(title, "%s", board->label[0] ? board->label : " ");
        title->Font.Color = board->label_color_set
            ? board->label_color : hud.leaderboard_default_title_color;
        if (has_title) {
            ResetFramePoints(title);
            UI_SetPoint(title, FRAMEPOINT_TOPLEFT, root, FRAMEPOINT_TOPLEFT,
                        LEADERBOARD_EDGE_INSET, -LEADERBOARD_TOP_PAD);
            UI_SetPoint(title, FRAMEPOINT_TOPRIGHT, root, FRAMEPOINT_TOPRIGHT,
                        -LEADERBOARD_EDGE_INSET, -LEADERBOARD_TOP_PAD);
            title->Height = title_height;
        }
    }

    UI_SetHidden(container, false);
    ResetFramePoints(container);
    UI_SetPoint(container, FRAMEPOINT_TOPLEFT, root, FRAMEPOINT_TOPLEFT,
                LEADERBOARD_EDGE_INSET, -list_top);
    UI_SetPoint(container, FRAMEPOINT_TOPRIGHT, root, FRAMEPOINT_TOPRIGHT,
                -LEADERBOARD_EDGE_INSET, -list_top);
    container->Height = visible_rows * row_height;

    leaderboard_measure_text[0] = '\0';
    if (has_title)
        LeaderboardAppendMeasureLine(leaderboard_measure_text, sizeof(leaderboard_measure_text),
                                     board->label[0] ? board->label : " ", NULL);
    FOR_LOOP(i, MIN(board->item_count, rows)) {
        struct gleaderboarditem_s const *item = &board->items[i];
        char text[MAX_TRIGSTR_LENGTH], number[32];
        LeaderboardItemText(board, item, text, sizeof(text));
        snprintf(number, sizeof(number), "%ld", (long)item->value);
        LeaderboardAppendMeasureLine(leaderboard_measure_text, sizeof(leaderboard_measure_text),
                                     text[0] ? text : " ",
                                     item->show_value && board->show_values ? number : NULL);
    }
    measure_font = title ? UI_LiveFont(title->Font.Index) : 0;
    if (!measure_font) measure_font = gi.FontIndex("Fonts\\FRIZQT__.TTF", HUD_FONT_SIZE);

    UI_SetCurrentClient(ent->client);
    UI_WriteStart(WC3_LAYER_LEADERBOARD);
    UI_WriteFrame(&hud.leaderboard_anchor);
    UI_WriteFrameWithChildrenSizedToText(root, &hud.leaderboard_anchor,
                                         leaderboard_measure_text,
                                         measure_font,
                                         LEADERBOARD_EDGE_INSET,
                                         LEADERBOARD_MIN_CONTENT_WIDTH + 2.0f * LEADERBOARD_EDGE_INSET);
    parent = UI_GetWrittenFrameNumber(container);
    FOR_LOOP(i, MIN(board->item_count, rows)) {
        struct gleaderboarditem_s const *item = &board->items[i];
        char text[MAX_TRIGSTR_LENGTH], number[32];
        COLOR32 label_color = item->label_color_set ? item->label_color : hud.leaderboard_default_item_color;
        COLOR32 value_color = item->value_color_set ? item->value_color :
            (board->value_color_set ? board->value_color : hud.leaderboard_default_item_color);
        LeaderboardItemText(board, item, text, sizeof(text));
        snprintf(number, sizeof(number), "%ld", (long)item->value);
        WriteLeaderboardText(parent, (FLOAT)i * row_height, row_height,
                             text[0] ? text : " ", label_color, FONT_JUSTIFYLEFT, false);
        WriteLeaderboardText(parent, (FLOAT)i * row_height, row_height,
                             item->show_value && board->show_values ? number : " ",
                             value_color, FONT_JUSTIFYRIGHT, true);
    }
    UI_WriteEnd(ent);
    UI_SetCurrentClient(NULL);
}
