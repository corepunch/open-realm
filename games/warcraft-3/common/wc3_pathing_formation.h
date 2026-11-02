#ifndef WC3_PATHING_FORMATION_H
#define WC3_PATHING_FORMATION_H

#include "wc3_math.h"

#define WC3_FORMATION_MEMBERS 12 // members; original temporary rank bucket capacity; bounds verified layouts

typedef struct {
    float position[2], radius, offset[2];
    unsigned rank;
} wc3FormationMember_t;

typedef struct {
    wc3FormationMember_t *members;
    unsigned count;
    float heading;
} wc3Formation_t;

typedef struct {
    unsigned count, members[WC3_FORMATION_MEMBERS], capacity;
    float diameter;
} wc3FormationBucket_t;

/* Original16a5b0: predicted fine positions in group order, rank buckets,
 * capacity tables, strict selection sorts, common row width, mean and rotation.
 * The original temporary bucket has twelve member slots. Larger caller
 * domains require separate proof; reject them before writing any outputs. */
static inline bool wc3_formation_layout_gap(wc3Formation_t *f, float rank_gap) {
    static unsigned char const capacity[2][2][13] = {
        {{0,1,2,3,2,3,3,3,3,3,4,4,4}, {0,1,2,3,2,3,3,3,3,3,3,3,3}},
        {{0,1,2,3,4,3,3,4,4,4,4,4,4}, {0,1,2,3,3,3,3,3,3,3,3,3,3}}
    };
    wc3FormationBucket_t buckets[16] = {0};
    float projected[WC3_FORMATION_MEMBERS][2];
    float sine, cosine, width = 0, row_x = 0;
    unsigned ranks = 0;
    if (!f || f->count > WC3_FORMATION_MEMBERS || (f->count && !f->members)) return false;
    if (f->count == 1) {
        f->members[0].offset[0] = f->members[0].offset[1] = 0;
        return true;
    }
    if (!f->count) return true;
    wc3_sincos(wc3_float(wc3_float_bits(f->heading) ^ 0x80000000u), &sine, &cosine);
    for (unsigned i = 0; i < f->count; i++) {
        wc3FormationMember_t const *m = f->members + i;
        wc3FormationBucket_t *b = buckets + (m->rank & 15);
        if (!b->count) ranks++;
        b->members[b->count++] = i;
        projected[i][0] = wc3_sub(wc3_mul(m->position[0], cosine), wc3_mul(m->position[1], sine));
        projected[i][1] = wc3_add(wc3_mul(m->position[0], sine), wc3_mul(m->position[1], cosine));
    }
    for (unsigned rank = 0; rank < 16; rank++) {
        wc3FormationBucket_t *b = buckets + rank;
        float radius = 0;
        if (!b->count) continue;
        for (unsigned i = 0; i < b->count; i++) {
            float r = f->members[b->members[i]].radius;
            if (r > radius) radius = r;
        }
        b->diameter = wc3_add(radius, radius);
        b->capacity = capacity[ranks != 1][radius >= 1.5f][b->count];
        if (b->capacity > b->count) b->capacity = b->count;
        float k = wc3_float(wc3_from_int(b->capacity));
        float w = wc3_add(wc3_mul(b->diameter, k),
            wc3_mul(wc3_add(b->diameter, 2), wc3_sub(k, 1)));
        if (w > width) width = w;
    }
    for (unsigned rank = 0; rank < 16; rank++) {
        wc3FormationBucket_t *b = buckets + rank;
        if (!b->count) continue;
        /* Strict minimum X to each tail: descending projected X, including
         * the original backwards scan and observable tie permutations. */
        for (unsigned tail = b->count - 1; tail; tail--) {
            unsigned selected = tail;
            for (unsigned j = tail; j-- > 0;)
                if (projected[b->members[j]][0] < projected[b->members[selected]][0]) selected = j;
            unsigned tmp = b->members[tail];
            b->members[tail] = b->members[selected]; b->members[selected] = tmp;
        }
        for (unsigned start = 0; start < b->count;) {
            unsigned n = b->capacity < b->count - start ? b->capacity : b->count - start;
            /* Strict maximum Y to the row tail: ascending Y. */
            for (unsigned tail = start + n - 1; tail > start; tail--) {
                unsigned selected = tail;
                for (unsigned j = tail; j-- > start;)
                    if (projected[b->members[selected]][1] < projected[b->members[j]][1]) selected = j;
                unsigned tmp = b->members[tail];
                b->members[tail] = b->members[selected]; b->members[selected] = tmp;
            }
            float step = n == 1 ? 0 : wc3_div(wc3_sub(width, b->diameter), wc3_float(wc3_from_int(n - 1)));
            for (unsigned j = 0; j < n; j++) {
                wc3FormationMember_t *m = f->members + b->members[start + j];
                m->offset[0] = row_x;
                m->offset[1] = n == 1 ? wc3_mul(width, .5f) :
                    wc3_add(wc3_mul(b->diameter, .5f), wc3_mul(step, wc3_float(wc3_from_int(j))));
            }
            start += n;
            if (start < b->count) row_x = wc3_sub(row_x, wc3_add(b->diameter, 2.5f));
        }
        row_x = wc3_sub(row_x, rank_gap);
    }
    float mean[2] = {0};
    for (unsigned i = 0; i < f->count; i++)
        for (unsigned k = 0; k < 2; k++) mean[k] = wc3_add(mean[k], f->members[i].offset[k]);
    float reciprocal = wc3_recip(wc3_float(wc3_from_int(f->count)));
    for (unsigned k = 0; k < 2; k++) mean[k] = wc3_mul(mean[k], reciprocal);
    wc3_sincos(f->heading, &sine, &cosine);
    for (unsigned i = 0; i < f->count; i++) {
        wc3FormationMember_t *m = f->members + i;
        float x = wc3_sub(m->offset[0], mean[0]), y = wc3_sub(m->offset[1], mean[1]);
        m->offset[0] = wc3_sub(wc3_mul(x, cosine), wc3_mul(y, sine));
        m->offset[1] = wc3_add(wc3_mul(x, sine), wc3_mul(y, cosine));
    }
    return true;
}

/* DLL initializer004180 parses "5.5" through070de0, producing40b00001.
 * The supplied-scalar oracle separately injects exact IEEE5.5 into this global. */
static inline bool wc3_formation_layout(wc3Formation_t *f) {
    return wc3_formation_layout_gap(f,wc3_float(0x40b00001));
}

#endif
