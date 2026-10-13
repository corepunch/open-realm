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

#endif
