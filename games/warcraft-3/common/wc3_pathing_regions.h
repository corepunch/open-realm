#ifndef BZ_WC3_PATHING_REGIONS_H
#define BZ_WC3_PATHING_REGIONS_H
#include "wc3_pathing_records.h"
#include "wc3_pathing_widget.h"

/*064460: the collection owns distinct identities, not a merged category.
 * Ordinary objects and region objects share the stable64-object pool. */
typedef struct wc3RegionCollection_s {uint32_t count,objects[4];} wc3RegionCollection_t;
static inline void wc3_regions_resize(wc3SpatialRecords_t *map,wc3RegionCollection_t *collection,
        uint32_t owner,uint32_t count) {
    static uint8_t const categories[]={0xc2,0x10,8,4};
    assert(count<=4);
    while(collection->count>count)wc3_records_retire(map,collection->objects[--collection->count]);
    while(collection->count<count) {
        uint32_t slot=collection->count++,id=wc3_records_create(map,owner,WC3_RECORD_REGION);
        wc3_records_object(map,id)->category=WC3_RECORD_INSERT|categories[slot];collection->objects[slot]=id;
    }
}
static inline void wc3_regions_cell(wc3SpatialRecords_t *map,wc3RegionCollection_t const *collection,
        uint32_t cell,uint8_t pixel,uint32_t kind) {
    for(uint32_t i=0;i<collection->count;i++) {
        uint32_t id=collection->objects[i];
        if(wc3_records_object(map,id)->category&pixel)wc3_records_prepend(map,cell,id,kind);
    }
}
/*22e9c0/063e50: row-major callback order, software scalar increments, then
 * clamp every sample to the world before converting. Edge samples may alias;
 * their duplicate references survive until ordered compaction. */
static inline void wc3_regions_raster(wc3SpatialRecords_t *map,wc3RegionCollection_t const *collection,
        uint32_t width,uint32_t height,uint8_t const *pixels,float const center[2],
        float const minimum[2],float const maximum[2],unsigned turn,uint32_t kind) {
    if(!width || !height)return;
    float hx=wc3_mul(wc3_float(wc3_from_int(width-1)),16),hy=wc3_mul(wc3_float(wc3_from_int(height-1)),16);
    float start[2],dx[2]={0},dy[2]={0};
    switch(turn&3) {
    case 0:start[0]=wc3_sub(center[0],hx);start[1]=wc3_sub(center[1],hy);dx[0]=32;dy[1]=32;break;
    case 1:start[0]=wc3_add(center[0],hy);start[1]=wc3_sub(center[1],hx);dx[1]=32;dy[0]=-32;break;
    case 2:start[0]=wc3_add(center[0],hx);start[1]=wc3_add(center[1],hy);dx[0]=-32;dy[1]=-32;break;
    default:start[0]=wc3_sub(center[0],hy);start[1]=wc3_add(center[1],hx);dx[1]=-32;dy[0]=32;break;
    }
    float point[2]={start[0],start[1]};
    for(uint32_t y=0;y<height;y++) {
        for(uint32_t x=0;x<width;x++) {
            uint8_t pixel=pixels[y*width+x];
            if(pixel) {
                float px=wc3_widget_clamp_axis(point[0],minimum[0],maximum[0]);
                float py=wc3_widget_clamp_axis(point[1],minimum[1],maximum[1]);
                uint32_t fx=wc3_int_bits(wc3_floor_bits(wc3_float_bits(wc3_div(wc3_sub(px,minimum[0]),32))));
                uint32_t fy=wc3_int_bits(wc3_floor_bits(wc3_float_bits(wc3_div(wc3_sub(py,minimum[1]),32))));
                assert(fx<map->width && fy<map->height);
                wc3_regions_cell(map,collection,fy*map->width+fx,pixel,kind);
            }
            point[0]=wc3_add(point[0],dx[0]);point[1]=wc3_add(point[1],dx[1]);
        }
        point[0]=wc3_add(point[0],dy[0]);point[1]=wc3_add(point[1],dy[1]);
        if(!(turn&1))point[0]=start[0];else point[1]=start[1];
    }
}
#endif
