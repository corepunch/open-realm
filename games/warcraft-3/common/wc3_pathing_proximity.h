#ifndef BZ_WC3_PATHING_PROXIMITY_H
#define BZ_WC3_PATHING_PROXIMITY_H
#include "wc3_pathing_fine.h"
#include "wc3_math.h"

/* Effective ordinary memberships after retail's kind0/1 resolution. Keep
 * retained links in place, prepend entrants and discard removals immediately.
 * This represents gameplay enumeration, not native allocation/stamp counters. */
typedef struct { uint32_t next,previous,owner_next,cell,owner; } wc3ProximityLink_t;
typedef struct { wc3FineBox_t box; uint32_t head; uint64_t seen; bool active; } wc3ProximityObject_t;
typedef struct {
    uint32_t width,height,count,capacity,free_head,free_count,object_count;
    uint32_t *cells;
    wc3ProximityLink_t *links;
    wc3ProximityObject_t *objects;
    uint64_t query;
} wc3ProximityMap_t;

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

static inline void wc3_proximity_free(wc3ProximityMap_t *map) {
    free(map->cells);free(map->links);free(map->objects);*map=(wc3ProximityMap_t){0};
}

static inline bool wc3_proximity_init(wc3ProximityMap_t *map,uint32_t width,uint32_t height,uint32_t objects) {
    if(!width || !height || (uint64_t)width*height>SIZE_MAX/sizeof(uint32_t))return false;
    wc3ProximityMap_t next={.width=width,.height=height,.object_count=objects};
    next.cells=calloc((size_t)width*height,sizeof(*next.cells));
    next.objects=calloc(objects,sizeof(*next.objects));
    if(!next.cells || !next.objects) {wc3_proximity_free(&next);return false;}
    wc3_proximity_free(map);*map=next;return true;
}

static inline bool wc3_proximity_reserve(wc3ProximityMap_t *map,uint32_t count) {
    if(count<=map->capacity)return true;
    uint32_t capacity=map->capacity ? map->capacity : 256;
    while(capacity<count) {if(capacity>UINT32_MAX/2)return false;capacity*=2;}
    if((uint64_t)capacity+1>SIZE_MAX/sizeof(*map->links))return false;
    void *links=realloc(map->links,((size_t)capacity+1)*sizeof(*map->links));
    if(!links)return false;
    map->links=links;map->capacity=capacity;return true;
}

static inline wc3FineBox_t wc3_proximity_clip(wc3ProximityMap_t const *map,wc3FineBox_t box) {
    if(box.min.x<0)box.min.x=0;
    if(box.min.y<0)box.min.y=0;
    if(box.max.x>(int)map->width)box.max.x=map->width;
    if(box.max.y>(int)map->height)box.max.y=map->height;
    return box;
}

static inline bool wc3_proximity_contains(wc3FineBox_t box,int x,int y) {
    return x>=box.min.x && x<box.max.x && y>=box.min.y && y<box.max.y;
}

static inline bool wc3_proximity_update(wc3ProximityMap_t *map,uint32_t owner,wc3FineBox_t box,bool active) {
    if(owner>=map->object_count || box.min.x>box.max.x || box.min.y>box.max.y)return false;
    wc3ProximityObject_t *object=map->objects+owner;
    if(object->active==active && !memcmp(&object->box,&box,sizeof(box)))return true;
    wc3FineBox_t clipped=wc3_proximity_clip(map,box),old=object->box;
    uint64_t needed=0;
    if(active)for(int y=clipped.min.y;y<clipped.max.y;y++)for(int x=clipped.min.x;x<clipped.max.x;x++)
        if(!object->active || !wc3_proximity_contains(old,x,y))needed++;
    /* Reserve before unlinking: no successful update can publish half a box. */
    uint64_t extra=needed>map->free_count ? needed-map->free_count : 0;
    if(extra>UINT32_MAX-map->count || !wc3_proximity_reserve(map,map->count+extra))return false;
    for(uint32_t *at=&object->head;*at;) {
        uint32_t id=*at;wc3ProximityLink_t *link=map->links+id;
        int x=link->cell%map->width,y=link->cell/map->width;
        if(active && wc3_proximity_contains(box,x,y)) {at=&link->owner_next;continue;}
        *at=link->owner_next;
        if(link->previous)map->links[link->previous].next=link->next;
        else map->cells[link->cell]=link->next;
        if(link->next)map->links[link->next].previous=link->previous;
        link->next=map->free_head;map->free_head=id;map->free_count++;
    }
    if(active)for(int y=clipped.min.y;y<clipped.max.y;y++)for(int x=clipped.min.x;x<clipped.max.x;x++) {
        if(object->active && wc3_proximity_contains(old,x,y))continue;
        uint32_t id;
        if(map->free_head) {id=map->free_head;map->free_head=map->links[id].next;map->free_count--;}
        else id=++map->count;
        uint32_t cell=(uint32_t)y*map->width+x,*head=map->cells+cell;
        map->links[id]=(wc3ProximityLink_t){.next=*head,.owner_next=object->head,.cell=cell,.owner=owner};
        if(*head)map->links[*head].previous=id;
        *head=object->head=id;
    }
    object->box=box;object->active=active;return true;
}

static inline void wc3_proximity_query(wc3ProximityMap_t *map,wc3FineBox_t box,uint32_t source,
    void (*visit)(void *,uint32_t),void *data) {
    box=wc3_proximity_clip(map,box);
    if(box.min.x>=box.max.x || box.min.y>=box.max.y)return;
    if(++map->query==0) {for(uint32_t i=0;i<map->object_count;i++)map->objects[i].seen=0;map->query=1;}
    if(source<map->object_count)map->objects[source].seen=map->query;
    for(int y=box.min.y;y<box.max.y;y++)for(int x=box.min.x;x<box.max.x;x++)
        for(uint32_t at=map->cells[(uint32_t)y*map->width+x];at;at=map->links[at].next) {
            uint32_t owner=map->links[at].owner;
            wc3ProximityObject_t *object=map->objects+owner;
            if(object->seen==map->query)continue;
            object->seen=map->query;visit(data,owner);
        }
}
#endif
