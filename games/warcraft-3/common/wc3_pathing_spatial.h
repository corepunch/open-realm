#ifndef BZ_WC3_PATHING_SPATIAL_H
#define BZ_WC3_PATHING_SPATIAL_H

#include "wc3_pathing_fine.h"

/* Active ordinary fine links after resolving the newest kind0/1 record.
 * Retail14e770 leaves intersection cells untouched. One publication rank per
 * rectangle update preserves the resulting per-cell order; records within an
 * update belong to the same object. Native allocator/stamp storage is separate.
 * Fine footprint classes0..3 cover at most4x4 cells (1492b0/16ee80). */
typedef struct wc3SpatialActive_s {
    wc3FineBox_t box;
    uint64_t ranks[16];
} wc3SpatialActive_t;

static inline uint64_t wc3_spatial_rank(wc3SpatialActive_t const *object, wc3FinePoint_t p) {
    if (p.x<object->box.min.x || p.y<object->box.min.y ||
        p.x>=object->box.max.x || p.y>=object->box.max.y) return 0;
    return object->ranks[(p.y-object->box.min.y)*4+p.x-object->box.min.x];
}

/* Erased cells disappear, entered cells prepend, retained cells keep history.
 * This is the observable chain left by14e770/14e050, not an entity birth sort. */
static inline bool wc3_spatial_update(wc3SpatialActive_t *object, wc3FineBox_t box, uint64_t *serial) {
    int64_t width=(int64_t)box.max.x-box.min.x, height=(int64_t)box.max.y-box.min.y;
    if (width<0 || height<0 || width>4 || height>4 || *serial==UINT64_MAX) return false;
    if (!memcmp(&object->box,&box,sizeof(box))) return true;
    wc3SpatialActive_t next={.box=box};
    uint64_t rank=++*serial;
    for(int y=box.min.y;y<box.max.y;y++) for(int x=box.min.x;x<box.max.x;x++) {
        uint64_t old=wc3_spatial_rank(object,(wc3FinePoint_t){x,y});
        next.ranks[(y-box.min.y)*4+x-box.min.x]=old ? old : rank;
    }
    *object=next;
    return true;
}

static inline bool wc3_spatial_valid(wc3SpatialActive_t const *object, uint64_t serial) {
    int64_t width=(int64_t)object->box.max.x-object->box.min.x;
    int64_t height=(int64_t)object->box.max.y-object->box.min.y;
    if(width<0 || height<0 || width>4 || height>4) return false;
    for(int y=0;y<4;y++) for(int x=0;x<4;x++) {
        uint64_t rank=object->ranks[y*4+x];
        if(x<width && y<height ? !rank || rank>serial : rank!=0) return false;
    }
    return true;
}

#endif
