#ifndef BZ_WC3_PATHING_SEGMENT_H
#define BZ_WC3_PATHING_SEGMENT_H

#include "wc3_pathing_fine.h"
#include "wc3_math.h"
#include "shared/types/vector2.h"

typedef struct {
    float start[2], direction[2], length;
    unsigned cls;
    bool (*cell)(void const *data, wc3FinePoint_t pos);
    void const *data;
} wc3FineSegment_t;
typedef struct { wc3FinePoint_t pos; unsigned count; bool vertical; } wc3FineStrip_t;
/* One point representation from reconstruction through owned consumption.
 * Units remain native fine/accelerator coordinates until world publication. */
typedef vec2_t wc3FineVector_t;
typedef struct { wc3FineVector_t const *points; uint32_t index; } wc3FineRoute_t;

/* 168280 leaves vectors of length <=1 unchanged. Software sqrt/reciprocal
 * affect the last integer sample even for cardinal distances. */
static inline float wc3_segment_normalize(float direction[2]) {
    float length = wc3_sqrt(wc3_add(wc3_mul(direction[0], direction[0]),
                                   wc3_mul(direction[1], direction[1])));
    if (length > 1.f) {
        float scale = wc3_recip(length);
        direction[0] = wc3_mul(direction[0], scale);
        direction[1] = wc3_mul(direction[1], scale);
    }
    return length;
}

static inline bool wc3_segment_strip(wc3FineSegment_t const *query, wc3FineStrip_t strip) {
    for (unsigned i = 0; i < strip.count; i++) {
        if (!query->cell(query->data, strip.pos)) return false;
        if (strip.vertical) strip.pos.y++; else strip.pos.x++;
    }
    return true;
}

/* 149440/149630/149970/149cc0: entering strips, with the predecessor
 * side strips for diagonal transitions. Class0 has a distinct cell order. */
static inline bool wc3_segment_foot(wc3FineSegment_t const *query, wc3FinePoint_t pos, unsigned code) {
    unsigned size = query->cls + 1;
    wc3FineBox_t box = wc3_fine_cover(query->cls, pos);
    int x0 = box.min.x, y0 = box.min.y, x1 = box.max.x - 1, y1 = box.max.y - 1;
    if (!query->cls) {
        if (!query->cell(query->data, pos)) return false;
        if (code != 3 && code != 6 && code != 9 && code != 12) return true;
        int sx = code == 3 || code == 6 ? -1 : 1;
        int sy = code == 3 || code == 9 ? 1 : -1;
        return query->cell(query->data, (wc3FinePoint_t){pos.x + sx, pos.y}) &&
               query->cell(query->data, (wc3FinePoint_t){pos.x, pos.y + sy});
    }
    switch (code) {
    case 1: return wc3_segment_strip(query, (wc3FineStrip_t){ {x0, y0}, size, false });
    case 2: return wc3_segment_strip(query, (wc3FineStrip_t){ {x1, y0}, size, true });
    case 4: return wc3_segment_strip(query, (wc3FineStrip_t){ {x0, y1}, size, false });
    case 8: return wc3_segment_strip(query, (wc3FineStrip_t){ {x0, y0}, size, true });
    case 3: case 6: case 9: case 12: {
        bool east = code == 3 || code == 6, north = code == 3 || code == 9;
        wc3FineStrip_t row = { {east ? x0 - 1 : x0, north ? y0 : y1}, size + 1, false };
        wc3FineStrip_t col = { {east ? x1 : x0, north ? y0 + 1 : y0 - 1}, size, true };
        return wc3_segment_strip(query, row) && wc3_segment_strip(query, col);
    }
    default:
        for (unsigned i = 0; i < size; i++)
            if (!wc3_segment_strip(query, (wc3FineStrip_t){ {x0, y0 + (int)i}, size, false })) return false;
        return true;
    }
}

/*148d00 samples one clockwise perimeter, starting at its northwest corner.
 * Every cell is observed, even after an obstruction. The four neighbor builders
 * consume overlapping bit ranges from that word, without repeating queries.
 * Current-cell interiors remain unchecked, allowing overlap escape. */
static inline uint8_t wc3_fine_cell_edges(wc3FineSegment_t const *query, wc3FinePoint_t pos) {
    static uint32_t const masks[4][8] = {
        {0x83,2,0x0e,0x80,8,0xe0,0x20,0x38},
        {0xc07,6,0x3e,0xc00,0x30,0xf80,0x180,0x1f0},
        {0xe00f,0x0e,0xfe,0xe000,0xe0,0xfe00,0xe00,0xfe0},
        {0xf001f,0x1e,0x3fe,0xf0000,0x3c0,0xff800,0x7800,0x7fc0}
    };
    assert(query->cls < 4);
    unsigned side=query->cls+2, offset=1+(query->cls+1)/2;
    wc3FinePoint_t cell={pos.x-(int)offset,pos.y-(int)offset};
    static wc3FinePoint_t const directions[]={{1,0},{0,1},{-1,0},{0,-1}};
    uint32_t clear=0;
    for(unsigned edge=0,bit=0;edge<4;edge++) {
        for(unsigned i=0;i<side;i++,bit++) {
            if(query->cell(query->data,cell))clear|=1u<<bit;
            cell.x+=directions[edge].x;cell.y+=directions[edge].y;
        }
    }
    uint8_t result = 0;
    for(unsigned i=0;i<8;i++)
        if((clear&masks[query->cls][i])==masks[query->cls][i])result|=1u<<i;
    return result;
}

/* 168d30: previous starts at0,0; neither endpoint is checked here. This
 * helper tests supplied fine-cell scalars; callers own admission/eligibility. */
static inline wc3FinePoint_t wc3_segment_point(wc3FineSegment_t const *query, float step) {
    float x = wc3_add(query->start[0], wc3_mul(step, query->direction[0]));
    float y = wc3_add(query->start[1], wc3_mul(step, query->direction[1]));
    return (wc3FinePoint_t){ (int32_t)wc3_int_bits(wc3_floor_bits(wc3_float_bits(x))),
                             (int32_t)wc3_int_bits(wc3_floor_bits(wc3_float_bits(y))) };
}

static inline bool wc3_segment_test(wc3FineSegment_t const *query) {
    wc3FinePoint_t previous = {0, 0};
    assert(query->cls < 4);
    for (float step = 1.f; step < query->length; step = wc3_add(step, 1.f)) {
        wc3FinePoint_t pos = wc3_segment_point(query, step);
        if (pos.x == previous.x && pos.y == previous.y) continue;
        unsigned code = (pos.x < previous.x ? 8u : pos.x > previous.x ? 2u : 0u) |
                        (pos.y < previous.y ? 1u : pos.y > previous.y ? 4u : 0u);
        if (!wc3_segment_foot(query, pos, code)) return false;
        previous = pos;
    }
    return true;
}

/* 167bf0 starts with the next route point unchecked, then tests successively
 * farther candidates. The first rejection stops skipping. Points are stored
 * destination-first; index is the current route index, at least one. */
static inline uint32_t wc3_segment_waypoint(wc3FineSegment_t const *query, wc3FineRoute_t route) {
    assert(route.index > 0);
    uint32_t chosen = route.index - 1;
    while (chosen > 0) {
        wc3FineSegment_t candidate = *query;
        candidate.direction[0] = wc3_sub(route.points[chosen - 1].x, query->start[0]);
        candidate.direction[1] = wc3_sub(route.points[chosen - 1].y, query->start[1]);
        candidate.length = wc3_segment_normalize(candidate.direction);
        if (!wc3_segment_test(&candidate)) break;
        chosen--;
    }
    return chosen;
}

#endif
