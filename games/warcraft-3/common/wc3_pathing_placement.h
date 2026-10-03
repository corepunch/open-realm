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
#endif
