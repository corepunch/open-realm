/* Server-authored multiboard / Team Resources presentation. The shared layout
 * is deliberately conservative until original stock TeamResources FDF is known. */
#include "hud_local.h"

#define WC3_MB_WIDTH 0.275f
#define WC3_MB_ROW_HEIGHT 0.016f
#define WC3_MB_HEADER_HEIGHT 0.020f
#define WC3_MB_TOP 0.040f

static void MultiboardText(uint32_t parent, float x, float y, float w, float h,
                           cstring_t value, color32_t color, uiFontJustificationH_t align) {
    uiFrame_t frame = { 0 };
    uiLabel_t label = { 0 };
    frame.flags.type = FT_STRING;
    frame.parent = parent;
    frame.text = value && *value ? value : " ";
    frame.color = color.a ? color : COLOR32_WHITE;
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", HUD_SMALL_FONT_SIZE);
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
    uiFrame_t frame = { 0 };
    frame.flags.type = FT_TEXTURE;
    frame.parent = parent;
    frame.tex.index = gi.ImageIndex("Textures\\Black32.blp");
    frame.color = MAKE(color32_t, 0, 0, 0, 175);
    UI_SetFrameRect(&frame, x, y, w, h);
    frame.points.x[FPP_MIN].relativeTo = UI_PARENT;
    frame.points.y[FPP_MIN].relativeTo = UI_PARENT;
    UI_WriteProxyFrame(&frame, NULL, 0);
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
    float width = WC3_MB_WIDTH, height, x, y;

    if (!ent || !(client = ent->client)) return;
    client_index = (uint32_t)(client - game.clients);
    viewer = client->ps.number;
    board = G_VisibleMultiboard(client_index);
    if (!board && !G_IsMultiboardSuppressed(&client->ps)) {
        rows = MultiboardTeamRows(viewer);
        team = rows > 0;
        cols = 4;
    } else if (board) {
        rows = MIN(board->rows, (uint32_t)MAX_MULTIBOARD_ROWS);
        cols = MIN(board->cols, (uint32_t)MAX_MULTIBOARD_COLS);
        minimized = (board->minimized_clients & (1u << client_index)) != 0;
    }
    if (!team && !board) { UI_ClearLayer(ent, WC3_LAYER_MULTIBOARD); return; }

    height = WC3_MB_HEADER_HEIGHT + (minimized ? 0 : rows * WC3_MB_ROW_HEIGHT) + 0.006f;
    x = UI_BASE_WIDTH - WC3_MB_WIDTH - HUD_HERO_SHORTCUT_EDGE_X;
    y = UI_BASE_HEIGHT - WC3_MB_TOP - height - UI_TimerDialogLeaderboardOffset(viewer);
    UI_SetCurrentClient(client);
    UI_WriteStart(WC3_LAYER_MULTIBOARD);
    root = MultiboardRoot();
    MultiboardPanel(root, x, y, width, height);
    MultiboardText(root, x + 0.007f, y + height - WC3_MB_HEADER_HEIGHT,
                   width - 0.014f, WC3_MB_HEADER_HEIGHT,
                   team ? "Team Resources" : board->title, COLOR32_WHITE, FONT_JUSTIFYLEFT);
    if (!minimized) {
        if (team) {
            uint32_t row = 0;
            FOR_LOOP(i, PLAYER_NEUTRAL_AGGRESSIVE) {
                player_t *owner;
                gameClient_t *owner_client;
                char gold[32], lumber[32], food[32];
                float yy;
                if (!G_CanViewTeamResources(viewer, i)) continue;
                owner_client = NULL;
                FOR_LOOP(slot, (uint32_t)game.max_clients)
                    if (game.clients[slot].ps.number == i) { owner_client = &game.clients[slot]; break; }
                if (!owner_client) continue;
                owner = &owner_client->ps;
                yy = y + height - WC3_MB_HEADER_HEIGHT - (++row) * WC3_MB_ROW_HEIGHT;
                snprintf(gold, sizeof(gold), "%ld", (long)owner->stats[PLAYERSTATE_RESOURCE_GOLD]);
                snprintf(lumber, sizeof(lumber), "%ld", (long)owner->stats[PLAYERSTATE_RESOURCE_LUMBER]);
                snprintf(food, sizeof(food), "%ld/%ld",
                         (long)owner->stats[PLAYERSTATE_RESOURCE_FOOD_USED],
                         (long)G_GetEffectiveFoodCap(owner_client));
                MultiboardText(root, x + 0.007f, yy, 0.105f, WC3_MB_ROW_HEIGHT,
                               owner->name, COLOR32_WHITE, FONT_JUSTIFYLEFT);
                MultiboardText(root, x + 0.115f, yy, 0.048f, WC3_MB_ROW_HEIGHT,
                               gold, COLOR32_WHITE, FONT_JUSTIFYRIGHT);
                MultiboardText(root, x + 0.165f, yy, 0.047f, WC3_MB_ROW_HEIGHT,
                               lumber, COLOR32_WHITE, FONT_JUSTIFYRIGHT);
                MultiboardText(root, x + 0.214f, yy, 0.054f, WC3_MB_ROW_HEIGHT,
                               food, COLOR32_WHITE, FONT_JUSTIFYRIGHT);
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
