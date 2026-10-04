#ifndef BZ_WC3_PATHING_GATE_H
#define BZ_WC3_PATHING_GATE_H
#include <stdint.h>

#define BZ_WC3_GATE_RECORDS 256 // original CPaWarp backing; zero is reserved

/* Original04e510: first free availability byte from1 through255, else0.
 * The engine rebuilds these bytes from live ability-owned IDs, so save/load
 * and map teardown do not retain a second allocation registry. */
static inline uint32_t wc3_gate_allocate(uint8_t used[BZ_WC3_GATE_RECORDS]) {
    for (uint32_t id=1; id<BZ_WC3_GATE_RECORDS; id++)
        if (!used[id]) { used[id]=1; return id; }
    return 0;
}
/* Original15c030 visits inclusive floor-scaled ends after the fine clip. */
static inline void wc3_gate_stamp(uint8_t *markers,uint32_t width,uint32_t height,
        int minx,int miny,int maxx,int maxy,uint8_t id) {
    uint32_t right=(uint32_t)(maxx/2)+1,bottom=(uint32_t)(maxy/2)+1;
    if(right>width)right=width;
    if(bottom>height)bottom=height;
    for(uint32_t y=(uint32_t)(miny/2);y<bottom;y++)
        for(uint32_t x=(uint32_t)(minx/2);x<right;x++)markers[y*width+x]=id;
}

/* Original15d1c0 only tests special bytes in clear children. A blocked child
 * retains class1 even when it carries a marker; a clear marker forces mixed. */
static inline uint8_t wc3_gate_parent(uint8_t const *classes,uint8_t const *markers,
        uint32_t width,uint32_t height,uint32_t x,uint32_t y) {
    unsigned blocked=0;
    for(unsigned dy=0;dy<2;dy++)for(unsigned dx=0;dx<2;dx++) {
        uint32_t cx=x+dx,cy=y+dy;
        if(cx>=width || cy>=height){blocked++;continue;}
        uint32_t cell=cy*width+cx;
        if(classes[cell]==1){blocked++;continue;}
        if(classes[cell]==2 || (markers && markers[cell]))return 2;
    }
    return blocked==4?1:blocked?2:0;
}

#endif
