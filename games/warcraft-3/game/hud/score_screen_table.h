/* The retail ScoreScreen.fdf defines static columns but no player rows.
 * Page values are projected from an immutable outgoing-match snapshot.
 * Keep unavailable counters unavailable instead of inventing retail values. */
#ifndef WC3_SCORE_SCREEN_TABLE_H
#define WC3_SCORE_SCREEN_TABLE_H

#include "../g_local.h"

static inline uint32_t WC3_ScoreColumns(uint32_t tab) {
    return tab == 0 ? 4 : tab == 1 ? 2 : 5;
}

static inline uint32_t WC3_ScoreMaxScroll(uint32_t count) {
    return count > WC3_SCORE_VISIBLE_ROWS ? count - WC3_SCORE_VISIBLE_ROWS : 0;
}

static inline uint32_t WC3_ScoreScrollStart(uint32_t count, float value) {
    uint32_t max_scroll = WC3_ScoreMaxScroll(count);
    if (!isfinite(value) || value <= 0.0f) return 0;
    if (value >= 1.0f) return max_scroll;
    return (uint32_t)lroundf(value * (float)max_scroll);
}

/* Return false when the score foundation does not yet collect this retail
 * column. A dash is more truthful than a manufactured zero. The existing
 * technology percentage and aggregate weights are still tagged GUESS in
 * g_score.c; these projections do not independently verify them. */
static inline bool WC3_ScoreCellValue(wc3ScorePlayerSnapshot_t const *p,
                                     uint32_t tab, uint32_t col, int32_t *out,
                                     bool *percent) {
    static int8_t const indexes[4][5] = {
        { WC3_PLAYER_SCORE_UNIT_TOTAL, WC3_PLAYER_SCORE_HERO_TOTAL,
          WC3_PLAYER_SCORE_RESOURCE_TOTAL, WC3_PLAYER_SCORE_TOTAL, -1 },
        { WC3_PLAYER_SCORE_UNITS_TRAINED, WC3_PLAYER_SCORE_UNITS_KILLED,
          -1, -1, -1 },
        { -1, WC3_PLAYER_SCORE_HEROES_KILLED, WC3_PLAYER_SCORE_ITEMS_GAINED,
          WC3_PLAYER_SCORE_MERCS_HIRED, -1 },
        { WC3_PLAYER_SCORE_GOLD_MINED_TOTAL, WC3_PLAYER_SCORE_LUMBER_TOTAL,
          -1, WC3_PLAYER_SCORE_TECH_PERCENT, WC3_PLAYER_SCORE_GOLD_LOST_UPKEEP },
    };
    int32_t index;
    if (!p || !out || tab >= 4 || col >= WC3_ScoreColumns(tab)) return false;
    index = indexes[tab][col];
    if (index < 0) return false;
    *out = p->values[index];
    if (percent) *percent = tab == 3 && col == 3;
    return true;
}

/* Interactions are controller policy, NOT confirmed retail ordering semantics.
 * GUESS: default descending numeric columns, repeat click reverses direction;
 * unknown/dash values always sort after known values. Teams groups by numeric
 * team id before sorting within each team, with the snapshot order as a stable
 * tie break. Never mutate the outgoing immutable snapshot. */
typedef struct wc3ScoreView_s {
    uint8_t column;     /* 0..4, or WC3_SCORE_NO_SORT */
    bool descending;
    bool teams;
    bool ally_colors;
} wc3ScoreView_t;
#define WC3_SCORE_NO_SORT UINT8_MAX

static inline void WC3_ScoreSortColumn(wc3ScoreView_t *view, uint32_t tab, uint32_t col) {
    if (!view || tab >= 4 || col >= WC3_ScoreColumns(tab)) return;
    if (view->column == col) view->descending = !view->descending;
    else { view->column = (uint8_t)col; view->descending = true; }
}

static inline int WC3_ScoreCompare(wc3ScoreSnapshot_t const *snapshot,
                                   uint32_t tab, wc3ScoreView_t const *view,
                                   uint32_t a, uint32_t b) {
    wc3ScorePlayerSnapshot_t const *pa = &snapshot->players[a], *pb = &snapshot->players[b];
    int32_t va = 0, vb = 0;
    bool ha, hb;
    if (view->teams && pa->team != pb->team) return pa->team < pb->team ? -1 : 1;
    if (view->column >= WC3_ScoreColumns(tab)) return 0;
    ha = WC3_ScoreCellValue(pa, tab, view->column, &va, NULL);
    hb = WC3_ScoreCellValue(pb, tab, view->column, &vb, NULL);
    if (ha != hb) return ha ? -1 : 1; /* unknown values stay last */
    if (!ha || va == vb) return 0;
    if (view->descending) return va > vb ? -1 : 1;
    return va < vb ? -1 : 1;
}

static inline void WC3_ScoreOrderedPlayers(wc3ScoreSnapshot_t const *snapshot,
                                            uint32_t tab, wc3ScoreView_t const *view,
                                            uint8_t order[MAX_PLAYERS]) {
    uint32_t const count = snapshot ? MIN(snapshot->count, (uint32_t)MAX_PLAYERS) : 0;
    if (!order) return;
    FOR_LOOP(i, count) order[i] = (uint8_t)i;
    if (!view || tab >= 4) return;
    /* Insertion sort is bounded by MAX_PLAYERS and stable on equal values. */
    for (uint32_t i = 1; i < count; ++i) {
        uint8_t const current = order[i];
        uint32_t j = i;
        while (j && WC3_ScoreCompare(snapshot, tab, view, current, order[j - 1]) < 0) {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = current;
    }
}

static inline uint32_t WC3_ScoreTeamColor(wc3ScoreSnapshot_t const *snapshot,
                                          wc3ScorePlayerSnapshot_t const *player) {
    if (!player || !snapshot) return 0;
    /* GUESS: ally-colour mode uses the lowest numbered teammate's team colour.
     * This is an approximation, not retail's local alliance-colour palette. */
    uint32_t color = player->color;
    uint32_t number = UINT32_MAX;
    FOR_LOOP(i, MIN(snapshot->count, (uint32_t)MAX_PLAYERS)) {
        wc3ScorePlayerSnapshot_t const *p = &snapshot->players[i];
        if (p->team == player->team && p->number < number) {
            number = p->number;
            color = p->color;
        }
    }
    return color;
}

#endif
