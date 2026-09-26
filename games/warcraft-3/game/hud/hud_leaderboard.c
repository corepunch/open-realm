#include "hud_local.h"

#define LEADERBOARD_EDGE_INSET     0.006f
#define LEADERBOARD_TOP_PAD        0.004f
#define LEADERBOARD_BOTTOM_PAD     0.004f
#define LEADERBOARD_TITLE_GAP      0.002f
#define LEADERBOARD_TEXT_HEIGHT    0.012f
#define BZ_WC3_LEADERBOARD_MIN_CONTENT_WIDTH 0.020f // normalized UI units; keeps an empty board readable
#define BZ_WC3_LEADERBOARD_COLUMN_GAP     "    " // spaces; separates measured label and value columns

typedef struct {
    uint32_t parent;
    float y;
    float h;
    cstring_t text;
    color32_t color;
    uiFontJustificationH_t align;
    bool right_anchored;
} leaderboardTextParams_t;

static char leaderboard_measure_text[(MAX_TRIGSTR_LENGTH + 48) * (MAX_LEADERBOARD_ITEMS + 1)];

static void LeaderboardItemText(leaderboard_t const * board, struct gleaderboarditem_s const *item,
                                string_t out, size_t out_size) {
    player_t * player = item->player >= 0 ? G_GetPlayerByNumber((uint32_t)item->player) : NULL;
    cstring_t name = board->show_names && player && player->name ? player->name : "";
    cstring_t label = item->show_label ? item->label : "";
    if (*name && *label) snprintf(out, out_size, "%s - %s", name, label);
    else snprintf(out, out_size, "%s%s", name, label);
}

static void WriteLeaderboardText(leaderboardTextParams_t const *params) {
    uiFrame_t frame;
    uiLabel_t label;
    if (!params) return;
    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    frame.flags.type = FT_STRING;
    frame.parent = params->parent;
    frame.text = params->text;
    frame.color = params->color.a ? params->color : COLOR32_WHITE;
    label.font = hud.leaderboard.LeaderboardTitle
        ? UI_LiveFont(hud.leaderboard.LeaderboardTitle->Font.Index)
        : gi.FontIndex("Fonts\\FRIZQT__.TTF", HUD_FONT_SIZE);
    label.textalignx = params->align;
    label.textaligny = FONT_JUSTIFYMIDDLE;
    frame.size.height = params->h;
    UI_SetFramePoint(&frame.points.x[params->right_anchored ? FPP_MAX : FPP_MIN],
                     params->right_anchored ? FPP_MAX : FPP_MIN,
                     UI_PARENT, 0.0f, false);
    UI_SetFramePoint(&frame.points.y[FPP_MIN], FPP_MIN, UI_PARENT, params->y, true);
    frame.points.y[FPP_MIN].relativeTo = UI_PARENT;
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

static void ResetFramePoints(frameDef_t * frame) {
    if (!frame) return;
    memset(&frame->Points, 0, sizeof(frame->Points));
    frame->AnyPointsSet = false;
}

static void LeaderboardAppendMeasureLine(string_t out, size_t out_size, cstring_t left, cstring_t right) {
    size_t used;

    if (!out || out_size == 0) return;
    used = strlen(out);
    if (used && used + 1 < out_size) out[used++] = '\n', out[used] = '\0';
    if (left && *left) strlcat(out, left, out_size);
    if (right && *right) {
        strlcat(out, BZ_WC3_LEADERBOARD_COLUMN_GAP, out_size);
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

void UI_WriteLeaderboard(edict_t * ent) {
    leaderboard_t * board;
    frameDef_t * root, *backdrop, *title, *container;
    float row_height, title_height, total_height, list_top, top_y;
    bool has_title;
    uint32_t player, rows, visible_rows, parent, measure_font;
    uiSizeToTextParams_t size_params;

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

    rows = board->size_by_item_count >= 0 ? (uint32_t)board->size_by_item_count : board->item_count;
    rows = MAX(1u, MIN(rows, (uint32_t)MAX_LEADERBOARD_ITEMS));
    visible_rows = board->item_count ? MAX(1u, MIN(board->item_count, rows)) : 0;
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

    UI_SetHidden(container, visible_rows == 0);
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
    size_params = (uiSizeToTextParams_t){
        .frame = root, .parent = &hud.leaderboard_anchor,
        .measure_text = leaderboard_measure_text, .font = measure_font,
        .padding_x = LEADERBOARD_EDGE_INSET,
        .min_width = BZ_WC3_LEADERBOARD_MIN_CONTENT_WIDTH + 2.0f * LEADERBOARD_EDGE_INSET,
    };
    UI_WriteFrameWithChildrenSizedToText(&size_params);
    parent = UI_GetWrittenFrameNumber(container);
    FOR_LOOP(i, MIN(board->item_count, rows)) {
        struct gleaderboarditem_s const *item = &board->items[i];
        char text[MAX_TRIGSTR_LENGTH], number[32];
        color32_t label_color = item->label_color_set ? item->label_color : hud.leaderboard_default_item_color;
        color32_t value_color = item->value_color_set ? item->value_color :
            (board->value_color_set ? board->value_color : hud.leaderboard_default_item_color);
        leaderboardTextParams_t label_params = {
            .parent = parent, .y = (float)i * row_height, .h = row_height,
            .text = NULL, .color = label_color,
            .align = FONT_JUSTIFYLEFT, .right_anchored = false,
        };
        leaderboardTextParams_t value_params = label_params;
        LeaderboardItemText(board, item, text, sizeof(text));
        snprintf(number, sizeof(number), "%ld", (long)item->value);
        label_params.text = text[0] ? text : " ";
        value_params.text = item->show_value && board->show_values ? number : " ";
        value_params.color = value_color;
        value_params.align = FONT_JUSTIFYRIGHT;
        value_params.right_anchored = true;
        WriteLeaderboardText(&label_params);
        WriteLeaderboardText(&value_params);
    }
    UI_WriteEnd(ent);
    UI_SetCurrentClient(NULL);
}
