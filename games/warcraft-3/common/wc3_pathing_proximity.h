#ifndef BZ_WC3_PATHING_PROXIMITY_H
#define BZ_WC3_PATHING_PROXIMITY_H
#include "wc3_pathing_fine.h"
#include "wc3_math.h"

#include "wc3_pathing_records.h"
typedef wc3SpatialRecords_t wc3ProximityMap_t;

/*1604d0/170960: native fine coordinates, independent radius, inverse scale
 * 1/8 from15ab60. Software scalar operations precede each floor. */
static inline wc3FineBox_t wc3_proximity_bounds(float const point[2],float radius) {
    wc3FineBox_t box;
    for(unsigned axis=0;axis<2;axis++) {
        int lo=(int32_t)wc3_int_bits(wc3_floor_bits(wc3_float_bits(wc3_mul(wc3_sub(point[axis],radius),.125f))));
        int hi=(int32_t)wc3_int_bits(wc3_floor_bits(wc3_float_bits(wc3_mul(wc3_add(point[axis],radius),.125f))));
        if(axis) {box.min.y=lo;box.max.y=hi<INT32_MAX ? hi+1 : hi;}
        else {box.min.x=lo;box.max.x=hi<INT32_MAX ? hi+1 : hi;}
    }
    return box;
}

static inline void wc3_proximity_free(wc3ProximityMap_t *map) {wc3_records_free(map);}
static inline bool wc3_proximity_init(wc3ProximityMap_t *map,uint32_t width,uint32_t height,uint32_t owners) {
    return wc3_records_init(map,width,height,owners);
}
static inline bool wc3_proximity_update(wc3ProximityMap_t *map,uint32_t owner,wc3FineBox_t box,bool active) {
    if(owner>=map->object_count || box.min.x>box.max.x || box.min.y>box.max.y)return false;
    uint32_t id=map->objects[owner];
    if(!active) {
        if(id!=WC3_RECORD_END) {wc3_records_retire(map,id);map->objects[owner]=WC3_RECORD_END;}
    } else {
        if(id==WC3_RECORD_END)id=map->objects[owner]=wc3_records_create(map,owner,0);
        wc3_records_update(map,id,box);
    }
    return true;
}
static inline void wc3_proximity_query(wc3ProximityMap_t *map,wc3FineBox_t box,uint32_t source,
    void (*visit)(void *,uint32_t),void *data) {wc3_records_query(map,box,source,visit,data);}
#endif
