#include "games/warcraft-3/common/wc3_pathing_spatial.h"

static wc3SpatialActive_t spatial_objects[3];
static uint64_t spatial_serial;

void pathing_spatial_clear(void) {
    memset(spatial_objects,0,sizeof(spatial_objects)); spatial_serial=0;
}

int pathing_spatial_update(unsigned object, int const rectangle[4]) {
    assert(object<3);
    return wc3_spatial_update(spatial_objects+object,
        (wc3FineBox_t){{rectangle[1],rectangle[0]},{rectangle[3],rectangle[2]}},&spatial_serial);
}

unsigned pathing_spatial_cell(int x, int y, unsigned output[3]) {
    uint64_t ranks[3]; unsigned count=0;
    for(unsigned i=0;i<3;i++) {
        uint64_t rank=wc3_spatial_rank(spatial_objects+i,(wc3FinePoint_t){x,y});
        if(!rank)continue;
        unsigned at=count++;
        while(at && ranks[at-1]<rank){ranks[at]=ranks[at-1];output[at]=output[at-1];at--;}
        ranks[at]=rank;output[at]=i;
    }
    return count;
}
