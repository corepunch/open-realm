#include "g_local.h"

static int32_t leaderboard_index(leaderboard_t const * board) {
    uintptr_t ptr = (uintptr_t)board;
    uintptr_t base = (uintptr_t)level.leaderboards;
    size_t span = sizeof(level.leaderboards);
    if (!board || ptr < base || ptr >= base + span ||
        (ptr - base) % sizeof(*board) != 0 || !board->inuse) return -1;
    return (int32_t)((ptr - base) / sizeof(*board));
}

/* Dirty bits address client slots, while leaderboard ownership uses WC3 player numbers. */
static uint32_t leaderboard_client_mask(uint32_t player) {
    FOR_LOOP(i, MIN((uint32_t)game.max_clients, (uint32_t)MAX_CLIENTS))
        if (game.clients[i].ps.number == player) return 1u << i;
    return 0;
}

leaderboard_t * G_AllocLeaderboard(void) {
    FOR_LOOP(i, MAX_LEADERBOARDS) if (!level.leaderboards[i].inuse) {
        leaderboard_t * board = &level.leaderboards[i];
        memset(board, 0, sizeof(*board));
        board->inuse = true;
        board->show_label = board->show_names = board->show_values = board->show_icons = true;
        board->size_by_item_count = -1;
        return board;
    }
    return NULL;
}

void G_FreeLeaderboard(leaderboard_t * board) {
    int32_t index = leaderboard_index(board);
    if (index < 0) return;
    FOR_LOOP(i, MAX_PLAYERS) if (level.player_leaderboards[i] == index) {
        level.player_leaderboards[i] = -1;
        level.leaderboard_dirty_clients |= leaderboard_client_mask(i);
    }
    memset(board, 0, sizeof(*board));
}

leaderboard_t * G_PlayerLeaderboard(uint32_t player) {
    int32_t index;
    if (player >= MAX_PLAYERS || (index = level.player_leaderboards[player]) < 0 || index >= MAX_LEADERBOARDS) return NULL;
    return level.leaderboards[index].inuse ? &level.leaderboards[index] : NULL;
}

void G_SetPlayerLeaderboard(uint32_t player, leaderboard_t * board) {
    int32_t index;
    if (player >= MAX_PLAYERS) return;
    index = board ? leaderboard_index(board) : -1;
    if (board && index < 0) return;
    level.player_leaderboards[player] = index;
    level.leaderboard_dirty_clients |= leaderboard_client_mask(player);
}

void G_SetLeaderboardDisplayed(leaderboard_t * board, player_t * player, bool displayed) {
    uint32_t mask;
    if (leaderboard_index(board) < 0) return;
    if (player) mask = leaderboard_client_mask(PLAYER_NUM(player));
    else { uint32_t count = MIN((uint32_t)game.max_clients, (uint32_t)MAX_CLIENTS); mask = count ? (uint32_t)((1ull << count) - 1ull) : 0; }
    if (displayed) board->displayed_clients |= mask; else board->displayed_clients &= ~mask;
    level.leaderboard_dirty_clients |= mask;
}

bool G_IsLeaderboardDisplayed(leaderboard_t const * board, player_t const * player) {
    if (!board || !board->inuse) return false;
    if (player) return board->displayed_clients & leaderboard_client_mask(PLAYER_NUM(player));
    uint32_t count = MIN((uint32_t)game.max_clients, (uint32_t)MAX_CLIENTS);
    uint32_t mask = count ? (uint32_t)((1ull << count) - 1ull) : 0;
    return mask && (board->displayed_clients & mask) == mask;
}

void G_MarkLeaderboardDirty(leaderboard_t const * board) {
    int32_t index = leaderboard_index(board);
    if (index < 0) return;
    FOR_LOOP(i, MIN((uint32_t)MAX_PLAYERS, (uint32_t)MAX_CLIENTS))
        if (level.player_leaderboards[i] == index) level.leaderboard_dirty_clients |= leaderboard_client_mask(i);
}

void G_UpdateLeaderboards(void) {
    uint32_t dirty = level.leaderboard_dirty_clients;
    if (!dirty) return;
    FOR_LOOP(i, MIN((uint32_t)game.max_clients, (uint32_t)MAX_CLIENTS)) {
        edict_t * ent;
        if (!(dirty & (1u << i))) continue;
        if (!game.clients[i].connected) { level.leaderboard_dirty_clients &= ~(1u << i); continue; }
        ent = G_GetPlayerEntityByNumber(game.clients[i].ps.number);
        if (ent && ent->client) UI_WriteLeaderboard(ent);
        level.leaderboard_dirty_clients &= ~(1u << i);
    }
}
