#include "g_local.h"

static LONG multiboard_index(LPCMULTIBOARD board) {
    uintptr_t ptr = (uintptr_t)board, base = (uintptr_t)level.multiboards;
    size_t span = sizeof(level.multiboards);
    if (!board || ptr < base || ptr >= base + span ||
        (ptr - base) % sizeof(*board) != 0 || !board->inuse) return -1;
    return (LONG)((ptr - base) / sizeof(*board));
}

static LONG texttag_index(LPCTEXTTAG tag) {
    uintptr_t ptr = (uintptr_t)tag, base = (uintptr_t)level.texttags;
    size_t span = sizeof(level.texttags);
    if (!tag || ptr < base || ptr >= base + span ||
        (ptr - base) % sizeof(*tag) != 0 || !tag->inuse) return -1;
    return (LONG)((ptr - base) / sizeof(*tag));
}

/* Dirty/visibility bits address client slots, while JASS players use WC3 numbers. */
static DWORD multiboard_client_mask(DWORD player) {
    FOR_LOOP(i, MIN((DWORD)game.max_clients, (DWORD)MAX_CLIENTS))
        if (game.clients[i].ps.number == player) return 1u << i;
    return 0;
}

static DWORD multiboard_all_client_mask(void) {
    DWORD count = MIN((DWORD)game.max_clients, (DWORD)MAX_CLIENTS);
    return count ? (DWORD)((1ull << count) - 1ull) : 0;
}

static void multiboard_init_cell(struct gmultiboardcell_s *cell) {
    memset(cell, 0, sizeof(*cell));
    cell->show_value = cell->show_icon = true;
}

static void multiboard_grow_cells(LPMULTIBOARD board, DWORD old_rows, DWORD old_cols) {
    DWORD row, col;
    if (!board) return;
    for (row = 0; row < board->rows; row++)
        for (col = 0; col < board->cols; col++)
            if (row >= old_rows || col >= old_cols)
                multiboard_init_cell(&board->cells[row * MAX_MULTIBOARD_COLS + col]);
}

LPMULTIBOARD G_AllocMultiboard(void) {
    FOR_LOOP(i, MAX_MULTIBOARDS) if (!level.multiboards[i].inuse) {
        LPMULTIBOARD board = &level.multiboards[i];
        memset(board, 0, sizeof(*board));
        board->inuse = true;
        return board;
    }
    return NULL;
}

void G_FreeMultiboard(LPMULTIBOARD board) {
    LONG index = multiboard_index(board);
    if (index < 0) return;
    FOR_LOOP(i, MAX_MULTIBOARD_ITEMS) {
        LPMULTIBOARDITEM item = &level.multiboard_items[i];
        if (item->inuse && item->board == index) item->board = -1;
    }
    memset(board, 0, sizeof(*board));
}

void G_SetMultiboardDisplayed(LPMULTIBOARD board, LPPLAYER player, BOOL displayed) {
    DWORD mask;
    if (multiboard_index(board) < 0) return;
    mask = player ? multiboard_client_mask(PLAYER_NUM(player)) : multiboard_all_client_mask();
    if (displayed) board->displayed_clients |= mask; else board->displayed_clients &= ~mask;
    level.multiboard_dirty_clients |= mask;
}

BOOL G_IsMultiboardDisplayed(LPCMULTIBOARD board, LPCPLAYER player) {
    DWORD mask;
    if (!board || !board->inuse) return false;
    if (player) return board->displayed_clients & multiboard_client_mask(PLAYER_NUM(player));
    mask = multiboard_all_client_mask();
    return mask && (board->displayed_clients & mask) == mask;
}

void G_SetMultiboardMinimized(LPMULTIBOARD board, LPPLAYER player, BOOL minimized) {
    DWORD mask;
    if (multiboard_index(board) < 0) return;
    mask = player ? multiboard_client_mask(PLAYER_NUM(player)) : multiboard_all_client_mask();
    if (minimized) board->minimized_clients |= mask; else board->minimized_clients &= ~mask;
    level.multiboard_dirty_clients |= mask;
}

BOOL G_IsMultiboardMinimized(LPCMULTIBOARD board, LPCPLAYER player) {
    DWORD mask;
    if (!board || !board->inuse) return false;
    if (player) return board->minimized_clients & multiboard_client_mask(PLAYER_NUM(player));
    mask = multiboard_all_client_mask();
    return mask && (board->minimized_clients & mask) == mask;
}

void G_MarkMultiboardDirty(LPCMULTIBOARD board) {
    if (multiboard_index(board) < 0) return;
    level.multiboard_dirty_clients |= board->displayed_clients ? board->displayed_clients : multiboard_all_client_mask();
}

void G_MultiboardSetRowCount(LPMULTIBOARD board, LONG count) {
    DWORD old_rows, old_cols;
    if (multiboard_index(board) < 0) return;
    if (count < 0) count = 0;
    if ((DWORD)count > MAX_MULTIBOARD_ROWS) count = MAX_MULTIBOARD_ROWS;
    old_rows = board->rows;
    old_cols = board->cols;
    board->rows = (DWORD)count;
    multiboard_grow_cells(board, old_rows, old_cols);
    G_MarkMultiboardDirty(board);
}

void G_MultiboardSetColumnCount(LPMULTIBOARD board, LONG count) {
    DWORD old_rows, old_cols;
    if (multiboard_index(board) < 0) return;
    if (count < 0) count = 0;
    if ((DWORD)count > MAX_MULTIBOARD_COLS) count = MAX_MULTIBOARD_COLS;
    old_rows = board->rows;
    old_cols = board->cols;
    board->cols = (DWORD)count;
    multiboard_grow_cells(board, old_rows, old_cols);
    G_MarkMultiboardDirty(board);
}

struct gmultiboardcell_s *G_MultiboardCell(LPMULTIBOARD board, LONG row, LONG col) {
    if (multiboard_index(board) < 0 || row < 0 || col < 0) return NULL;
    if ((DWORD)row >= board->rows || (DWORD)col >= board->cols) return NULL;
    return &board->cells[(DWORD)row * MAX_MULTIBOARD_COLS + (DWORD)col];
}

LPMULTIBOARDITEM G_MultiboardGetItem(LPMULTIBOARD board, LONG row, LONG col) {
    LONG board_id = multiboard_index(board);
    if (board_id < 0 || !G_MultiboardCell(board, row, col)) return NULL;
    FOR_LOOP(i, MAX_MULTIBOARD_ITEMS) if (!level.multiboard_items[i].inuse) {
        LPMULTIBOARDITEM item = &level.multiboard_items[i];
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

void G_MultiboardReleaseItem(LPMULTIBOARDITEM item) {
    uintptr_t ptr = (uintptr_t)item, base = (uintptr_t)level.multiboard_items;
    size_t span = sizeof(level.multiboard_items);
    if (!item || ptr < base || ptr >= base + span ||
        (ptr - base) % sizeof(*item) != 0 || !item->inuse) return;
    if (item->refs > 1) { item->refs--; return; }
    memset(item, 0, sizeof(*item));
}

LPMULTIBOARD G_MultiboardItemBoard(LPCMULTIBOARDITEM item) {
    if (!item || !item->inuse || item->board < 0 || item->board >= MAX_MULTIBOARDS) return NULL;
    return level.multiboards[item->board].inuse ? &level.multiboards[item->board] : NULL;
}

LPTEXTTAG G_AllocTextTag(void) {
    FOR_LOOP(i, MAX_TEXTTAGS) if (!level.texttags[i].inuse) {
        LPTEXTTAG tag = &level.texttags[i];
        memset(tag, 0, sizeof(*tag));
        tag->inuse = true;
        tag->visible_clients = multiboard_all_client_mask();
        tag->permanent = true;
        tag->color = MAKE(COLOR32, 255, 255, 255, 255);
        tag->height = 0.024f;
        return tag;
    }
    return NULL;
}

void G_FreeTextTag(LPTEXTTAG tag) {
    if (texttag_index(tag) < 0) return;
    memset(tag, 0, sizeof(*tag));
}

void G_SetTextTagVisible(LPTEXTTAG tag, LPPLAYER player, BOOL visible) {
    DWORD mask;
    if (texttag_index(tag) < 0) return;
    mask = player ? multiboard_client_mask(PLAYER_NUM(player)) : multiboard_all_client_mask();
    if (visible) tag->visible_clients |= mask; else tag->visible_clients &= ~mask;
}

BOOL G_IsTextTagVisible(LPCTEXTTAG tag, LPCPLAYER player) {
    DWORD mask;
    if (!tag || !tag->inuse) return false;
    if (player) return tag->visible_clients & multiboard_client_mask(PLAYER_NUM(player));
    mask = multiboard_all_client_mask();
    return mask && (tag->visible_clients & mask) == mask;
}
