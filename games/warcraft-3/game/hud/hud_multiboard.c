/* Server-authored multiboard / Team Resources presentation. */
#include "hud_local.h"

#define WC3_MB_WIDTH 0.275f
#define WC3_MB_ROW_HEIGHT 0.016f
#define WC3_MB_HEADER_HEIGHT 0.020f
#define WC3_MB_HEADER_PAD 0.006f
#define WC3_MB_PANEL_GAP 0.001f
#define WC3_MB_TOP 0.040f

static uint32_t MultiboardFont(void) {
    frameDef_t *title = hud.leaderboard.LeaderboardTitle;
    uint32_t font = title ? UI_LiveFont(title->Font.Index) : 0;
    return font ? font : gi.FontIndex("Fonts\\FRIZQT__.TTF", HUD_FONT_SIZE);
}

static color32_t MultiboardPlayerColor(uint32_t color) {
    static color32_t const colors[] = {
        { 255, 3, 3, 255 }, { 0, 66, 255, 255 }, { 28, 230, 185, 255 }, { 84, 0, 129, 255 },
        { 255, 252, 1, 255 }, { 254, 138, 14, 255 }, { 32, 192, 0, 255 }, { 229, 91, 176, 255 },
        { 149, 150, 151, 255 }, { 126, 191, 241, 255 }, { 16, 98, 70, 255 }, { 78, 42, 4, 255 },
        { 155, 0, 0, 255 }, { 0, 0, 195, 255 }, { 0, 234, 255, 255 }, { 190, 0, 254, 255 },
        { 235, 205, 135, 255 }, { 248, 164, 139, 255 }, { 191, 255, 128, 255 }, { 220, 185, 235, 255 },
        { 40, 40, 40, 255 }, { 235, 240, 255, 255 }, { 0, 120, 30, 255 }, { 164, 111, 51, 255 },
    };
    return colors[MIN(color, 23u)];
}

static void MultiboardIcon(uint32_t parent, float x, float y, cstring_t art) {
    uiFrame_t frame = { 0 };
    frame.flags.type = FT_TEXTURE;
    frame.flagsvalue |= UIFLAG_TEXTURE_OVERLAY;
    frame.parent = parent;
    frame.tex.index = UI_LoadTexture(art, true);
    UI_SetFrameRect(&frame, x, y, 0.013f, 0.013f);
    frame.points.x[FPP_MIN].relativeTo = UI_PARENT;
    frame.points.y[FPP_MIN].relativeTo = UI_PARENT;
    UI_WriteProxyFrame(&frame, NULL, 0);
}

static void MultiboardButton(uint32_t parent, float x, float y, float w, float h,
                             cstring_t text) {
    uiFrame_t frame = { 0 };
    uiLabel_t label = { 0 };
    frame.flags.type = FT_STRING;
    frame.parent = parent;
    frame.text = text;
    frame.onclick = "team_resources_toggle";
    frame.color = COLOR32_WHITE;
    label.font = MultiboardFont();
    label.textalignx = FONT_JUSTIFYMIDDLE;
    label.textaligny = FONT_JUSTIFYMIDDLE;
    UI_SetFrameRect(&frame, x, y, w, h);
    frame.points.x[FPP_MIN].relativeTo = UI_PARENT;
    frame.points.y[FPP_MIN].relativeTo = UI_PARENT;
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

static void MultiboardText(uint32_t parent, float x, float y, float w, float h,
                           cstring_t value, color32_t color, uiFontJustificationH_t align) {
    uiFrame_t frame = { 0 };
    uiLabel_t label = { 0 };
    frame.flags.type = FT_STRING;
    frame.parent = parent;
    frame.text = value && *value ? value : " ";
    frame.color = color.a ? color : COLOR32_WHITE;
    label.font = MultiboardFont();
    label.textalignx = align;
    label.textaligny = FONT_JUSTIFYMIDDLE;
    UI_SetFrameRect(&frame, x, y, w, h);
    frame.points.x[FPP_MIN].relativeTo = UI_PARENT;
    frame.points.y[FPP_MIN].relativeTo = UI_PARENT;
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

static uint32_t MultiboardRoot(void) {
    uiFrame_t root = { 0 };
    uint32_t number = ui_next_frame_number;
    root.flags.type = FT_SIMPLEFRAME;
    root.flagsvalue |= UIFLAG_EXTEND_WIDESCREEN_X;
    UI_SetFrameRect(&root, 0, 0, UI_BASE_WIDTH, UI_BASE_HEIGHT);
    UI_WriteProxyFrame(&root, NULL, 0);
    return number;
}

static void MultiboardPanel(uint32_t parent, float x, float y, float w, float h) {
    frameDef_t const *style = hud.leaderboard.LeaderboardBackdrop;
    uiFrame_t frame = { 0 };
    uiBackdrop_t backdrop = { 0 };
    frame.flags.type = FT_BACKDROP;
    frame.parent = parent;
    frame.color = style && style->Color.a ? style->Color : COLOR32_WHITE;
    UI_SetFrameRect(&frame, x, y, w, h);
    frame.points.x[FPP_MIN].relativeTo = UI_PARENT;
    frame.points.y[FPP_MIN].relativeTo = UI_PARENT;
    if (style) {
        backdrop = MAKE(uiBackdrop_t,
            .CornerFlags = style->Backdrop.CornerFlags,
            .CornerSize = style->Backdrop.CornerSize,
            .BackgroundSize = style->Backdrop.BackgroundSize,
            .BackgroundInsets = {
                style->Backdrop.BackgroundInsets[0], style->Backdrop.BackgroundInsets[1],
                style->Backdrop.BackgroundInsets[2], style->Backdrop.BackgroundInsets[3],
            },
            .EdgeFile = UI_LiveImage(style->Backdrop.EdgeFile),
            .Background = UI_LiveImage(style->Backdrop.Background),
            .TileBackground = style->Backdrop.TileBackground,
            .Opaque = !style->Backdrop.BlendAll,
            .Mirrored = style->Backdrop.Mirrored,
        );
    }
    UI_WriteProxyFrame(&frame, &backdrop, sizeof(backdrop));
}

static uint32_t MultiboardTeamRows(uint32_t viewer) {
    uint32_t count = 0;
    FOR_LOOP(i, PLAYER_NEUTRAL_AGGRESSIVE) {
        if (!G_CanViewTeamResources(viewer, i)) continue;
        FOR_LOOP(slot, (uint32_t)game.max_clients) {
            if (game.clients[slot].ps.number == i) { count++; break; }
        }
    }
    return count;
}

void UI_WriteMultiboard(edict_t *ent) {
    gameClient_t *client;
    multiboard_t *board;
    uint32_t client_index, viewer, root, rows = 0, cols = 0;
    bool team = false, minimized = false;
    float width = WC3_MB_WIDTH, height, body_height, body_y, x, y;

    if (!ent || !(client = ent->client)) return;
    client_index = (uint32_t)(client - game.clients);
    viewer = client->ps.number;
    board = G_VisibleMultiboard(client_index);
    if (!board && !G_IsMultiboardSuppressed(&client->ps)) {
        rows = MultiboardTeamRows(viewer);
        team = rows > 0;
        cols = 4;
        minimized = (level.team_resources_collapsed_clients & (1u << client_index)) != 0;
    } else if (board) {
        rows = MIN(board->rows, (uint32_t)MAX_MULTIBOARD_ROWS);
        cols = MIN(board->cols, (uint32_t)MAX_MULTIBOARD_COLS);
        minimized = (board->minimized_clients & (1u << client_index)) != 0;
    }
    if (!team && !board) { UI_ClearLayer(ent, WC3_LAYER_MULTIBOARD); return; }

    body_height = team
        ? (minimized ? 0.0f : rows * WC3_MB_ROW_HEIGHT + 0.012f)
        : (minimized ? 0.0f : rows * WC3_MB_ROW_HEIGHT) + 0.006f;
    height = team ? WC3_MB_HEADER_HEIGHT + WC3_MB_HEADER_PAD +
                    (minimized ? 0.0f : WC3_MB_PANEL_GAP + body_height)
                  : WC3_MB_HEADER_HEIGHT + body_height;
    x = UI_BASE_WIDTH - WC3_MB_WIDTH - HUD_HERO_SHORTCUT_EDGE_X;
    /* Timer, leaderboard, title and resources form one top-right stack. */
    y = WC3_MB_TOP + UI_TimerDialogLeaderboardOffset(viewer)
        + UI_LeaderboardMultiboardOffset(viewer);
    UI_SetCurrentClient(client);
    UI_WriteStart(WC3_LAYER_MULTIBOARD);
    root = MultiboardRoot();
    if (team) {
        /* Keep the Team Resources heading in its own bordered strip directly
         * above the resource rows, matching the stock stacked HUD panels. */
        float const header_height = WC3_MB_HEADER_HEIGHT + WC3_MB_HEADER_PAD;
        float const header_square = header_height;
        float const title_width = width - header_square - WC3_MB_PANEL_GAP;
        float const square_x = x + title_width + WC3_MB_PANEL_GAP;
        body_y = y + header_height + WC3_MB_PANEL_GAP;
        if (!minimized) MultiboardPanel(root, x, body_y, width, body_height);
        MultiboardPanel(root, x, y, title_width, header_height);
        MultiboardPanel(root, square_x, y, header_square, header_height);
        MultiboardText(root, x + 0.007f, y + 0.003f,
                       title_width - 0.014f, WC3_MB_HEADER_HEIGHT,
                       "Team Resources", hud.leaderboard_default_title_color,
                       FONT_JUSTIFYLEFT);
        MultiboardButton(root, square_x, y + 0.003f, header_square,
                         WC3_MB_HEADER_HEIGHT, minimized ? "+" : "-");
    } else {
        body_y = y;
        MultiboardPanel(root, x, y, width, height);
        MultiboardText(root, x + 0.007f, y + height - WC3_MB_HEADER_HEIGHT,
                       width - 0.014f, WC3_MB_HEADER_HEIGHT, board->title,
                       hud.leaderboard_default_title_color, FONT_JUSTIFYLEFT);
    }
    if (!minimized) {
        if (team) {
            uint32_t row = 0;
            FOR_LOOP(i, PLAYER_NEUTRAL_AGGRESSIVE) {
                player_t *owner;
                gameClient_t *owner_client;
                char gold[32], lumber[32], food[32];
                int32_t food_used, food_cap;
                float yy;
                if (!G_CanViewTeamResources(viewer, i)) continue;
                owner_client = NULL;
                FOR_LOOP(slot, (uint32_t)game.max_clients)
                    if (game.clients[slot].ps.number == i) { owner_client = &game.clients[slot]; break; }
                if (!owner_client) continue;
                owner = &owner_client->ps;
                yy = body_y + 0.006f + row++ * WC3_MB_ROW_HEIGHT;
                snprintf(gold, sizeof(gold), "%ld", (long)owner->stats[PLAYERSTATE_RESOURCE_GOLD]);
                snprintf(lumber, sizeof(lumber), "%ld", (long)owner->stats[PLAYERSTATE_RESOURCE_LUMBER]);
                food_used = owner->stats[PLAYERSTATE_RESOURCE_FOOD_USED];
                food_cap = G_GetEffectiveFoodCap(owner_client);
                snprintf(food, sizeof(food), "%ld/%ld",
                         (long)food_used, (long)food_cap);
                MultiboardText(root, x + 0.007f, yy, 0.105f, WC3_MB_ROW_HEIGHT,
                               owner->name, MultiboardPlayerColor(owner->color), FONT_JUSTIFYLEFT);
                MultiboardIcon(root, x + 0.115f, yy + 0.0015f, "GoldIcon");
                MultiboardText(root, x + 0.129f, yy, 0.034f, WC3_MB_ROW_HEIGHT,
                               gold, hud.leaderboard_default_item_color, FONT_JUSTIFYRIGHT);
                MultiboardIcon(root, x + 0.165f, yy + 0.0015f, "LumberIcon");
                MultiboardText(root, x + 0.179f, yy, 0.033f, WC3_MB_ROW_HEIGHT,
                               lumber, hud.leaderboard_default_item_color, FONT_JUSTIFYRIGHT);
                MultiboardIcon(root, x + 0.214f, yy + 0.0015f, "SupplyIcon");
                MultiboardText(root, x + 0.228f, yy, 0.040f, WC3_MB_ROW_HEIGHT,
                               food, food_used >= food_cap
                                   ? MAKE(color32_t, 255, 64, 64, 255)
                                   : MAKE(color32_t, 96, 255, 96, 255),
                               FONT_JUSTIFYRIGHT);
            }
        } else if (cols) {
            FOR_LOOP(r, rows) FOR_LOOP(c, cols) {
                struct gmultiboardcell_s const *cell =
                    &board->cells[r * MAX_MULTIBOARD_COLS + c];
                float cw = (width - 0.014f) / cols;
                float xx = x + 0.007f + c * cw;
                float yy = y + height - WC3_MB_HEADER_HEIGHT - (r + 1) * WC3_MB_ROW_HEIGHT;
                if (cell->show_icon && cell->icon[0]) {
                    uiFrame_t icon = { 0 };
                    icon.flags.type = FT_TEXTURE;
                    icon.parent = root;
                    icon.tex.index = gi.ImageIndex(cell->icon);
                    UI_SetFrameRect(&icon, xx, yy + 0.001f, 0.012f, 0.012f);
                    icon.points.x[FPP_MIN].relativeTo = UI_PARENT;
                    icon.points.y[FPP_MIN].relativeTo = UI_PARENT;
                    UI_WriteProxyFrame(&icon, NULL, 0);
                    xx += 0.013f;
                    cw -= 0.013f;
                }
                if (cell->show_value)
                    MultiboardText(root, xx, yy, cw, WC3_MB_ROW_HEIGHT, cell->value,
                                   cell->value_color_set ? cell->value_color : COLOR32_WHITE,
                                   FONT_JUSTIFYLEFT);
            }
        }
    }
    UI_WriteEnd(ent);
}

void G_UpdateMultiboards(void) {
    uint32_t dirty = level.multiboard_dirty_clients;
    FOR_LOOP(i, MIN((uint32_t)game.max_clients, (uint32_t)MAX_CLIENTS)) {
        edict_t *ent;
        if (!(dirty & (1u << i))) continue;
        if (game.clients[i].connected) {
            ent = G_GetPlayerEntityByNumber(game.clients[i].ps.number);
            if (ent && ent->client) UI_WriteMultiboard(ent);
        }
        level.multiboard_dirty_clients &= ~(1u << i);
    }
}
