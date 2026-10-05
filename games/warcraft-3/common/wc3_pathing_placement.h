#ifndef BZ_WC3_PATHING_PLACEMENT_H
#define BZ_WC3_PATHING_PLACEMENT_H

#include "wc3_pathing_segment.h"

typedef struct {
    float point[2];
    uint32_t limit;
    wc3FineSegment_t footprint;
    bool (*admit)(void const *data, float const *point);
    bool integer_result;
} wc3FinePlacement_t;

/* Original14b580 tests footprint cells before the caller's integer-point
 * callback. The half-cell output adjustment happens only after acceptance. */
static bool wc3_placement_candidate(wc3FinePlacement_t const *query, wc3FinePoint_t cell) {
    if (!wc3_segment_foot(&query->footprint, cell, 0)) return false;
    float point[2] = {wc3_float(wc3_from_int(cell.x)), wc3_float(wc3_from_int(cell.y))};
    return !query->admit || query->admit(query->footprint.data, point);
}

/* The public point case of14a1e0, scan policy2: preserve a legal requested
 * scalar pair; otherwise expand the floor rectangle and walk each ring in
 * bottom/right/top/left order. Limit includes the initial point attempt. */
static bool wc3_fine_place(wc3FinePlacement_t const *query, float *out) {
    wc3FinePoint_t cell = {(int32_t)wc3_int_bits(wc3_floor_bits(wc3_float_bits(query->point[0]))),
                          (int32_t)wc3_int_bits(wc3_floor_bits(wc3_float_bits(query->point[1])))};
    out[0] = query->point[0]; out[1] = query->point[1];
    if (!query->limit) return false;
    if (wc3_segment_foot(&query->footprint, cell, 0) &&
        (!query->admit || query->admit(query->footprint.data, query->point))) return true;
    wc3FineBox_t ring = {cell, {cell.x + 1, cell.y + 1}};
    for (uint32_t left = query->limit - 1; left; left--) {
        ring.min.x--; ring.min.y--; ring.max.x++; ring.max.y++;
        cell.y = ring.min.y;
        for (cell.x = ring.min.x + 1; cell.x < ring.max.x; cell.x++)
            if (wc3_placement_candidate(query, cell)) goto found;
        cell.x = ring.max.x - 1;
        for (cell.y = ring.min.y + 1; cell.y < ring.max.y; cell.y++)
            if (wc3_placement_candidate(query, cell)) goto found;
        cell.y = ring.max.y - 1;
        for (cell.x = ring.max.x - 2; cell.x >= ring.min.x; cell.x--)
            if (wc3_placement_candidate(query, cell)) goto found;
        cell.x = ring.min.x;
        for (cell.y = ring.max.y - 2; cell.y >= ring.min.y; cell.y--)
            if (wc3_placement_candidate(query, cell)) goto found;
    }
    return false;
found:
    out[0] = wc3_float(wc3_from_int(cell.x)); out[1] = wc3_float(wc3_from_int(cell.y));
    if (!query->integer_result) { out[0] = wc3_add(out[0], .5f); out[1] = wc3_add(out[1], .5f); }
    return true;
}
/* A pure admission query can reject many centers with one blocking witness.
 * Rows and columns retain the original ring order while skipping those centers.
 * A witness is a half-open rectangle whose EVERY cell rejects this query; the
 * cell reader publishes it only on failure. No witness means reject this one
 * candidate (for example a failed terrain-level admission). This index lives
 * for one synchronous request, so later publication/eligibility edits cannot
 * leave stale blocked cells in another unit's placement. */
typedef struct {
    uint64_t rows[64], columns[64];
    wc3FinePoint_t origin;
    uint32_t extent;
} wc3PlacementIndex_t;

static void wc3_placement_reject(wc3PlacementIndex_t *index, unsigned cls,
                                  wc3FinePoint_t cell, wc3FineBox_t witness) {
    int size = (int)cls + 1;
    int64_t x0 = cell.x, x1 = x0 + 1, y0 = cell.y, y1 = y0 + 1;
    if (witness.max.x > witness.min.x && witness.max.y > witness.min.y) {
        x0 = (int64_t)witness.min.x + size / 2 - size + 1;
        y0 = (int64_t)witness.min.y + size / 2 - size + 1;
        x1 = (int64_t)witness.max.x + size / 2;
        y1 = (int64_t)witness.max.y + size / 2;
    }
    x0 -= index->origin.x; x1 -= index->origin.x;
    y0 -= index->origin.y; y1 -= index->origin.y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > index->extent) x1 = index->extent;
    if (y1 > index->extent) y1 = index->extent;
    if (x0 >= x1 || y0 >= y1) return;
    uint64_t xs = (UINT64_MAX << x0) & (UINT64_MAX >> (64 - x1));
    uint64_t ys = (UINT64_MAX << y0) & (UINT64_MAX >> (64 - y1));
    for (int y = (int)y0; y < y1; y++) index->rows[y] |= xs;
    for (int x = (int)x0; x < x1; x++) index->columns[x] |= ys;
}

static bool wc3_placement_index_strip(wc3FinePlacement_t const *query, wc3PlacementIndex_t *index,
        unsigned fixed, unsigned low, unsigned high, bool vertical, bool reverse,
        wc3FineBox_t *witness, wc3FinePoint_t *out) {
    uint64_t range = (UINT64_MAX << low) & (UINT64_MAX >> (63 - high));
    uint64_t const *rejected = vertical ? index->columns + fixed : index->rows + fixed;
    while (range & ~*rejected) {
        uint64_t remaining = range & ~*rejected;
        unsigned at = reverse ? 63u - __builtin_clzll(remaining) : __builtin_ctzll(remaining);
        wc3FinePoint_t cell = {index->origin.x + (int)(vertical ? fixed : at),
                              index->origin.y + (int)(vertical ? at : fixed)};
        *witness = (wc3FineBox_t){0};
        if (wc3_placement_candidate(query, cell)) { *out = cell; return true; }
        wc3_placement_reject(index, query->footprint.cls, cell, *witness);
    }
    return false;
}

/* Game-owned placement/Stop admission is pure. Portal and other side-effecting
 * callbacks continue through wc3_fine_place. The point attempt remains first,
 * with no index initialization on the common already-legal path. */
static inline bool wc3_fine_place_indexed(wc3FinePlacement_t const *query, float *out, wc3FineBox_t *witness) {
    assert(query->limit <= 32 && query->footprint.cls < 4);
    wc3FinePoint_t cell = {(int32_t)wc3_int_bits(wc3_floor_bits(wc3_float_bits(query->point[0]))),
                          (int32_t)wc3_int_bits(wc3_floor_bits(wc3_float_bits(query->point[1])))};
    out[0] = query->point[0]; out[1] = query->point[1];
    if (!query->limit) return false;
    *witness = (wc3FineBox_t){0};
    if (wc3_segment_foot(&query->footprint, cell, 0) &&
        (!query->admit || query->admit(query->footprint.data, query->point))) return true;
    unsigned radius = query->limit - 1;
    wc3PlacementIndex_t index = {.origin = {cell.x - (int)radius, cell.y - (int)radius},
                                 .extent = radius * 2 + 1};
    wc3_placement_reject(&index, query->footprint.cls, cell, *witness);
    for (unsigned r = 1; r <= radius; r++) {
        unsigned low = radius - r, high = radius + r;
        if (wc3_placement_index_strip(query, &index, low, low + 1, high, false, false, witness, &cell) ||
            wc3_placement_index_strip(query, &index, high, low + 1, high, true, false, witness, &cell) ||
            wc3_placement_index_strip(query, &index, high, low, high - 1, false, true, witness, &cell) ||
            wc3_placement_index_strip(query, &index, low, low, high - 1, true, true, witness, &cell)) {
            out[0] = wc3_float(wc3_from_int(cell.x)); out[1] = wc3_float(wc3_from_int(cell.y));
            if (!query->integer_result) { out[0] = wc3_add(out[0], .5f); out[1] = wc3_add(out[1], .5f); }
            return true;
        }
    }
    return false;
}
#endif
