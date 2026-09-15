#include "hud_local.h"

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

void UI_LoadHudLeaderboards(void) {
    if (!LeaderBoard_Load(&hud.leaderboard)) return;
    if (hud.leaderboard.LeaderboardTitle) {
        hud.leaderboard_default_title_color = hud.leaderboard.LeaderboardTitle->Font.Color;
        hud.leaderboard_default_item_color = hud.leaderboard.LeaderboardTitle->Font.Color;
    }
}

void UI_WriteLeaderboard(LPEDICT ent) {
    LPLEADERBOARD board;
    LPFRAMEDEF container;
    FLOAT row_height, width;
    DWORD player, rows, parent;

    if (!ent || !ent->client) return;
    player = ent->client->ps.number;
    board = G_PlayerLeaderboard(player);
    if (!board || !(board->displayed_clients & (1u << player)) || !hud.leaderboard.Leaderboard ||
        !(container = hud.leaderboard.LeaderboardListContainer)) {
        UI_ClearLayer(ent, LAYER_LEADERBOARD);
        return;
    }

    UI_SetHidden(hud.leaderboard.Leaderboard, false);
    if (hud.leaderboard.LeaderboardBackdrop)
        UI_SetHidden(hud.leaderboard.LeaderboardBackdrop, false);
    UI_SetHidden(container, false);
    if (hud.leaderboard.LeaderboardTitle) {
        UI_SetHidden(hud.leaderboard.LeaderboardTitle, !board->show_label);
        UI_SetText(hud.leaderboard.LeaderboardTitle, "%s", board->label[0] ? board->label : " ");
        hud.leaderboard.LeaderboardTitle->Font.Color = board->label_color_set
            ? board->label_color : hud.leaderboard_default_title_color;
    }

    rows = board->size_by_item_count >= 0 ? (DWORD)board->size_by_item_count : board->item_count;
    rows = MAX(1u, MIN(rows, (DWORD)MAX_LEADERBOARD_ITEMS));
    row_height = container->Height > 0.0f ? container->Height / rows :
        (hud.leaderboard.LeaderboardTitle && hud.leaderboard.LeaderboardTitle->Font.Size > 0.0f
            ? hud.leaderboard.LeaderboardTitle->Font.Size * 1.25f : 0.012f);
    width = container->Width > 0.0f ? container->Width : 0.16f;

    UI_SetCurrentClient(ent->client);
    UI_WriteStart(LAYER_LEADERBOARD);
    UI_WriteFrameWithChildren(hud.leaderboard.Leaderboard, NULL);
    parent = UI_GetWrittenFrameNumber(container);
    FOR_LOOP(i, MIN(board->item_count, rows)) {
        struct gleaderboarditem_s const *item = &board->items[i];
        char text[MAX_TRIGSTR_LENGTH], number[32];
        COLOR32 label_color = item->label_color_set ? item->label_color : hud.leaderboard_default_item_color;
        COLOR32 value_color = item->value_color_set ? item->value_color :
            (board->value_color_set ? board->value_color : hud.leaderboard_default_item_color);
        LeaderboardItemText(board, item, text, sizeof(text));
        snprintf(number, sizeof(number), "%ld", (long)item->value);
        WriteLeaderboardText(parent, 0.0f, (FLOAT)i * row_height, width * 0.74f, row_height,
                             text[0] ? text : " ", label_color, FONT_JUSTIFYLEFT);
        WriteLeaderboardText(parent, width * 0.76f, (FLOAT)i * row_height, width * 0.24f, row_height,
                             item->show_value && board->show_values ? number : " ", value_color, FONT_JUSTIFYRIGHT);
    }
    UI_WriteEnd(ent);
    UI_SetCurrentClient(NULL);
}
