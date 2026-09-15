#include "g_local.h"

static LONG leaderboard_index(LPCLEADERBOARD board) {
    uintptr_t ptr = (uintptr_t)board;
    uintptr_t base = (uintptr_t)level.leaderboards;
    size_t span = sizeof(level.leaderboards);
    if (!board || ptr < base || ptr >= base + span ||
        (ptr - base) % sizeof(*board) != 0 || !board->inuse) return -1;
    return (LONG)((ptr - base) / sizeof(*board));
}

LPLEADERBOARD G_AllocLeaderboard(void) {
    FOR_LOOP(i, MAX_LEADERBOARDS) if (!level.leaderboards[i].inuse) {
        LPLEADERBOARD board = &level.leaderboards[i];
        memset(board, 0, sizeof(*board));
        board->inuse = true;
        board->show_label = board->show_names = board->show_values = board->show_icons = true;
        board->size_by_item_count = -1;
        return board;
    }
    return NULL;
}

void G_FreeLeaderboard(LPLEADERBOARD board) {
    LONG index = leaderboard_index(board);
    if (index < 0) return;
    FOR_LOOP(i, MAX_PLAYERS) if (level.player_leaderboards[i] == index) {
        level.player_leaderboards[i] = -1;
        if (i < MAX_CLIENTS) level.leaderboard_dirty_clients |= 1u << i;
    }
    memset(board, 0, sizeof(*board));
}

LPLEADERBOARD G_PlayerLeaderboard(DWORD player) {
    LONG index;
    if (player >= MAX_PLAYERS || (index = level.player_leaderboards[player]) < 0 || index >= MAX_LEADERBOARDS) return NULL;
    return level.leaderboards[index].inuse ? &level.leaderboards[index] : NULL;
}

void G_SetPlayerLeaderboard(DWORD player, LPLEADERBOARD board) {
    LONG index;
    if (player >= MAX_PLAYERS) return;
    index = board ? leaderboard_index(board) : -1;
    if (board && index < 0) return;
    level.player_leaderboards[player] = index;
    if (player < MAX_CLIENTS) level.leaderboard_dirty_clients |= 1u << player;
}

void G_SetLeaderboardDisplayed(LPLEADERBOARD board, LPPLAYER player, BOOL displayed) {
    DWORD mask;
    if (leaderboard_index(board) < 0) return;
    if (player) { DWORD n = PLAYER_NUM(player); if (n >= MAX_CLIENTS) return; mask = 1u << n; }
    else { DWORD count = MIN((DWORD)game.max_clients, (DWORD)MAX_CLIENTS); mask = count ? (DWORD)((1ull << count) - 1ull) : 0; }
    if (displayed) board->displayed_clients |= mask; else board->displayed_clients &= ~mask;
    level.leaderboard_dirty_clients |= mask;
}

BOOL G_IsLeaderboardDisplayed(LPCLEADERBOARD board, LPCPLAYER player) {
    if (!board || !board->inuse) return false;
    if (player) { DWORD n = PLAYER_NUM(player); return n < MAX_CLIENTS && (board->displayed_clients & (1u << n)); }
    DWORD count = MIN((DWORD)game.max_clients, (DWORD)MAX_CLIENTS);
    DWORD mask = count ? (DWORD)((1ull << count) - 1ull) : 0;
    return mask && (board->displayed_clients & mask) == mask;
}

void G_MarkLeaderboardDirty(LPCLEADERBOARD board) {
    LONG index = leaderboard_index(board);
    if (index < 0) return;
    FOR_LOOP(i, MIN((DWORD)MAX_PLAYERS, (DWORD)MAX_CLIENTS))
        if (level.player_leaderboards[i] == index) level.leaderboard_dirty_clients |= 1u << i;
}

void G_UpdateLeaderboards(void) {
    DWORD dirty = level.leaderboard_dirty_clients;
    if (!dirty) return;
    FOR_LOOP(i, MIN((DWORD)game.max_clients, (DWORD)MAX_CLIENTS)) {
        LPEDICT ent;
        if (!(dirty & (1u << i))) continue;
        if (!game.clients[i].connected) { level.leaderboard_dirty_clients &= ~(1u << i); continue; }
        ent = G_GetPlayerEntityByNumber(i);
        if (ent && ent->client) UI_WriteLeaderboard(ent);
        level.leaderboard_dirty_clients &= ~(1u << i);
    }
}
