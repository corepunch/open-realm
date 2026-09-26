#include "g_local.h"

static int32_t multiboard_index(multiboard_t const * board) {
    uintptr_t ptr = (uintptr_t)board, base = (uintptr_t)level.multiboards;
    size_t span = sizeof(level.multiboards);
    if (!board || ptr < base || ptr >= base + span ||
        (ptr - base) % sizeof(*board) != 0 || !board->inuse) return -1;
    return (int32_t)((ptr - base) / sizeof(*board));
}

static int32_t texttag_index(texttag_t const * tag) {
    uintptr_t ptr = (uintptr_t)tag, base = (uintptr_t)level.texttags;
    size_t span = sizeof(level.texttags);
    if (!tag || ptr < base || ptr >= base + span ||
        (ptr - base) % sizeof(*tag) != 0 || !tag->inuse) return -1;
    return (int32_t)((ptr - base) / sizeof(*tag));
}

/* Dirty/visibility bits address client slots, while JASS players use WC3 numbers. */
static uint32_t multiboard_client_mask(uint32_t player) {
    FOR_LOOP(i, MIN((uint32_t)game.max_clients, (uint32_t)MAX_CLIENTS))
        if (game.clients[i].ps.number == player) return 1u << i;
    return 0;
}

static uint32_t multiboard_all_client_mask(void) {
    uint32_t count = MIN((uint32_t)game.max_clients, (uint32_t)MAX_CLIENTS);
    return count ? (uint32_t)((1ull << count) - 1ull) : 0;
}

static void multiboard_init_cell(struct gmultiboardcell_s *cell) {
    memset(cell, 0, sizeof(*cell));
    cell->show_value = cell->show_icon = true;
}

static void multiboard_grow_cells(multiboard_t * board, uint32_t old_rows, uint32_t old_cols) {
    uint32_t row, col;
    if (!board) return;
    for (row = 0; row < board->rows; row++)
        for (col = 0; col < board->cols; col++)
            if (row >= old_rows || col >= old_cols)
                multiboard_init_cell(&board->cells[row * MAX_MULTIBOARD_COLS + col]);
}

multiboard_t * G_AllocMultiboard(void) {
    FOR_LOOP(i, MAX_MULTIBOARDS) if (!level.multiboards[i].inuse) {
        multiboard_t * board = &level.multiboards[i];
        memset(board, 0, sizeof(*board));
        board->inuse = true;
        return board;
    }
    return NULL;
}

void G_FreeMultiboard(multiboard_t * board) {
    int32_t index = multiboard_index(board);
    if (index < 0) return;
    FOR_LOOP(i, MAX_MULTIBOARD_ITEMS) {
        multiboardItem_t * item = &level.multiboard_items[i];
        if (item->inuse && item->board == index) item->board = -1;
    }
    memset(board, 0, sizeof(*board));
}

void G_SetMultiboardDisplayed(multiboard_t * board, player_t * player, bool displayed) {
    uint32_t mask;
    if (multiboard_index(board) < 0) return;
    mask = player ? multiboard_client_mask(PLAYER_NUM(player)) : multiboard_all_client_mask();
    if (displayed) board->displayed_clients |= mask; else board->displayed_clients &= ~mask;
    level.multiboard_dirty_clients |= mask;
}

bool G_IsMultiboardDisplayed(multiboard_t const * board, player_t const * player) {
    uint32_t mask;
    if (!board || !board->inuse) return false;
    if (player) return board->displayed_clients & multiboard_client_mask(PLAYER_NUM(player));
    mask = multiboard_all_client_mask();
    return mask && (board->displayed_clients & mask) == mask;
}

void G_SetMultiboardMinimized(multiboard_t * board, player_t * player, bool minimized) {
    uint32_t mask;
    if (multiboard_index(board) < 0) return;
    mask = player ? multiboard_client_mask(PLAYER_NUM(player)) : multiboard_all_client_mask();
    if (minimized) board->minimized_clients |= mask; else board->minimized_clients &= ~mask;
    level.multiboard_dirty_clients |= mask;
}

bool G_IsMultiboardMinimized(multiboard_t const * board, player_t const * player) {
    uint32_t mask;
    if (!board || !board->inuse) return false;
    if (player) return board->minimized_clients & multiboard_client_mask(PLAYER_NUM(player));
    mask = multiboard_all_client_mask();
    return mask && (board->minimized_clients & mask) == mask;
}

void G_MarkMultiboardDirty(multiboard_t const * board) {
    if (multiboard_index(board) < 0) return;
    level.multiboard_dirty_clients |= board->displayed_clients ? board->displayed_clients : multiboard_all_client_mask();
}

void G_MultiboardSetRowCount(multiboard_t * board, int32_t count) {
    uint32_t old_rows, old_cols;
    if (multiboard_index(board) < 0) return;
    if (count < 0) count = 0;
    if ((uint32_t)count > MAX_MULTIBOARD_ROWS) count = MAX_MULTIBOARD_ROWS;
    old_rows = board->rows;
    old_cols = board->cols;
    board->rows = (uint32_t)count;
    multiboard_grow_cells(board, old_rows, old_cols);
    G_MarkMultiboardDirty(board);
}

void G_MultiboardSetColumnCount(multiboard_t * board, int32_t count) {
    uint32_t old_rows, old_cols;
    if (multiboard_index(board) < 0) return;
    if (count < 0) count = 0;
    if ((uint32_t)count > MAX_MULTIBOARD_COLS) count = MAX_MULTIBOARD_COLS;
    old_rows = board->rows;
    old_cols = board->cols;
    board->cols = (uint32_t)count;
    multiboard_grow_cells(board, old_rows, old_cols);
    G_MarkMultiboardDirty(board);
}

struct gmultiboardcell_s *G_MultiboardCell(multiboard_t * board, int32_t row, int32_t col) {
    if (multiboard_index(board) < 0 || row < 0 || col < 0) return NULL;
    if ((uint32_t)row >= board->rows || (uint32_t)col >= board->cols) return NULL;
    return &board->cells[(uint32_t)row * MAX_MULTIBOARD_COLS + (uint32_t)col];
}

multiboardItem_t * G_MultiboardGetItem(multiboard_t * board, int32_t row, int32_t col) {
    int32_t board_id = multiboard_index(board);
    if (board_id < 0 || !G_MultiboardCell(board, row, col)) return NULL;
    FOR_LOOP(i, MAX_MULTIBOARD_ITEMS) if (!level.multiboard_items[i].inuse) {
        multiboardItem_t * item = &level.multiboard_items[i];
        memset(item, 0, sizeof(*item));
        item->inuse = true;
        item->refs = 1;
        item->board = board_id;
        item->row = row;
        item->col = col;
        return item;
    }
    return NULL;
}

void G_MultiboardReleaseItem(multiboardItem_t * item) {
    uintptr_t ptr = (uintptr_t)item, base = (uintptr_t)level.multiboard_items;
    size_t span = sizeof(level.multiboard_items);
    if (!item || ptr < base || ptr >= base + span ||
        (ptr - base) % sizeof(*item) != 0 || !item->inuse) return;
    if (item->refs > 1) { item->refs--; return; }
    memset(item, 0, sizeof(*item));
}

multiboard_t * G_MultiboardItemBoard(multiboardItem_t const * item) {
    if (!item || !item->inuse || item->board < 0 || item->board >= MAX_MULTIBOARDS) return NULL;
    return level.multiboards[item->board].inuse ? &level.multiboards[item->board] : NULL;
}

texttag_t * G_AllocTextTag(void) {
    FOR_LOOP(i, MAX_TEXTTAGS) if (!level.texttags[i].inuse) {
        texttag_t * tag = &level.texttags[i];
        memset(tag, 0, sizeof(*tag));
        tag->inuse = true;
        tag->visible_clients = multiboard_all_client_mask();
        tag->permanent = true;
        tag->color = MAKE(color32_t, 255, 255, 255, 255);
        tag->height = 0.024f;
        return tag;
    }
    return NULL;
}

void G_FreeTextTag(texttag_t * tag) {
    if (texttag_index(tag) < 0) return;
    memset(tag, 0, sizeof(*tag));
}

void G_SetTextTagVisible(texttag_t * tag, player_t * player, bool visible) {
    uint32_t mask;
    if (texttag_index(tag) < 0) return;
    mask = player ? multiboard_client_mask(PLAYER_NUM(player)) : multiboard_all_client_mask();
    if (visible) tag->visible_clients |= mask; else tag->visible_clients &= ~mask;
}

bool G_IsTextTagVisible(texttag_t const * tag, player_t const * player) {
    uint32_t mask;
    if (!tag || !tag->inuse) return false;
    if (player) return tag->visible_clients & multiboard_client_mask(PLAYER_NUM(player));
    mask = multiboard_all_client_mask();
    return mask && (tag->visible_clients & mask) == mask;
}
