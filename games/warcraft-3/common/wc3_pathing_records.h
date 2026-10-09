#ifndef BZ_WC3_PATHING_RECORDS_H
#define BZ_WC3_PATHING_RECORDS_H
#include "wc3_pathing_fine.h"

/*14d9e0/14e050: low24 link index, high8 record kind, stable pooled identity.
 * Rectangles are XY here; retail's stored rectangles are YX. Never resolve a
 * retained record through a reusable edict slot. See retail-pathfinding-storage. */
enum {
    WC3_RECORD_END=0x00ffffff,
    WC3_RECORD_REMOVE=0,
    WC3_RECORD_INSERT=0x01000000,
    WC3_RECORD_METADATA=0x02000000,
    WC3_RECORD_REGION=0x10000000,
    WC3_RECORD_GROWTH=0x20000,
    WC3_RECORD_OBJECT_BLOCK=64
};
typedef struct {uint32_t next,payload;} wc3SpatialRecord_t;
typedef struct wc3RecordObject_s {
    wc3FineBox_t box;
    uint32_t owner,stamp,refs,flags,free_next,category;
} wc3RecordObject_t;
typedef struct wc3SpatialRecords_s {
    uint32_t width,height,count,capacity,free_head,free_count,records,object_count,query;
    uint32_t *cells,*dirty,*objects;
    wc3SpatialRecord_t *links;
    wc3RecordObject_t **blocks;
    uint32_t block_count,block_capacity,raw_objects,free_object,live_objects;
    uint64_t *occupied;
    uint32_t occupied_stride;
} wc3SpatialRecords_t;

/* Storm never returns a recoverable allocation failure. The engine likewise
 * terminates before a caller can observe partial membership. */
static inline void *wc3_records_memory(void *old,size_t bytes) {
    void *p=realloc(old,bytes);
    if(!p)abort();
    return p;
}
static inline wc3RecordObject_t *wc3_records_object(wc3SpatialRecords_t const *map,uint32_t id) {
    return map->blocks[id/WC3_RECORD_OBJECT_BLOCK]+id%WC3_RECORD_OBJECT_BLOCK;
}
static inline wc3RecordObject_t *wc3_records_owned(wc3SpatialRecords_t const *map,uint32_t owner) {
    return owner<map->object_count && map->objects[owner]!=WC3_RECORD_END ?
        wc3_records_object(map,map->objects[owner]) : NULL;
}
static inline bool wc3_records_contains(wc3RecordObject_t const *object,wc3FinePoint_t point) {
    return object && object->stamp!=UINT32_MAX && point.x>=object->box.min.x && point.y>=object->box.min.y &&
        point.x<object->box.max.x && point.y<object->box.max.y;
}
static inline wc3FineBox_t wc3_records_clip(wc3SpatialRecords_t const *map,wc3FineBox_t box) {
    if(box.min.x<0)box.min.x=0;
    if(box.min.y<0)box.min.y=0;
    if(box.max.x>(int)map->width)box.max.x=map->width;
    if(box.max.y>(int)map->height)box.max.y=map->height;
    return box;
}
static inline void wc3_records_free(wc3SpatialRecords_t *map) {
    for(uint32_t i=0;i<map->block_count;i++)free(map->blocks[i]);
    free(map->blocks);free(map->cells);free(map->dirty);free(map->objects);free(map->links);free(map->occupied);
    *map=(wc3SpatialRecords_t){0};
}
/* Derived capacities survive a same-map reset, but no identity/link/stamp does. */
static inline void wc3_records_clear(wc3SpatialRecords_t *map) {
    if(map->cells)for(uint32_t i=0;i<map->width*map->height;i++)map->cells[i]=WC3_RECORD_END;
    if(map->dirty)memset(map->dirty,0,((size_t)map->width*map->height+31)/32*sizeof(*map->dirty));
    if(map->objects)for(uint32_t i=0;i<map->object_count;i++)map->objects[i]=WC3_RECORD_END;
    if(map->occupied)memset(map->occupied,0,(size_t)map->occupied_stride*map->height*sizeof(*map->occupied));
    map->count=map->free_count=map->records=map->query=map->raw_objects=map->live_objects=0;
    map->free_head=map->free_object=WC3_RECORD_END;
}
static inline bool wc3_records_init(wc3SpatialRecords_t *map,uint32_t width,uint32_t height,uint32_t owners) {
    if(!width || !height || width>INT32_MAX || height>INT32_MAX ||
        (uint64_t)width*height>UINT32_MAX || (uint64_t)width*height>SIZE_MAX/sizeof(uint32_t))return false;
    wc3SpatialRecords_t next={.width=width,.height=height,.object_count=owners};
    next.cells=wc3_records_memory(NULL,(size_t)width*height*sizeof(*next.cells));
    next.dirty=wc3_records_memory(NULL,((size_t)width*height+31)/32*sizeof(*next.dirty));
    if(owners)next.objects=wc3_records_memory(NULL,(size_t)owners*sizeof(*next.objects));
    wc3_records_clear(&next);wc3_records_free(map);*map=next;return true;
}
/* Optional derived raw-head summary. Metadata and dead records count too:
 * skipping such a cell would erase its observable predicate/cleanup stamp. */
static inline void wc3_records_occupancy(wc3SpatialRecords_t *map) {
    if(map->occupied)return;
    map->occupied_stride=(map->width+63)/64;
    map->occupied=wc3_records_memory(NULL,(size_t)map->occupied_stride*map->height*sizeof(*map->occupied));
    memset(map->occupied,0,(size_t)map->occupied_stride*map->height*sizeof(*map->occupied));
    for(uint32_t cell=0;cell<map->width*map->height;cell++)
        if((map->cells[cell]&WC3_RECORD_END)!=WC3_RECORD_END)
            map->occupied[(cell/map->width)*map->occupied_stride+(cell%map->width)/64]|=UINT64_C(1)<<(cell%map->width%64);
}
static inline uint32_t wc3_records_create(wc3SpatialRecords_t *map,uint32_t owner,uint32_t flags) {
    uint32_t id;
    if(map->free_object!=WC3_RECORD_END) {
        id=map->free_object;map->free_object=wc3_records_object(map,id)->free_next;
    } else {
        id=map->raw_objects++;
        uint32_t block=id/WC3_RECORD_OBJECT_BLOCK;
        if(block==map->block_count) {
            if(block==map->block_capacity) {
                map->block_capacity=map->block_capacity ? map->block_capacity*2 : 4;
                map->blocks=wc3_records_memory(map->blocks,(size_t)map->block_capacity*sizeof(*map->blocks));
            }
            map->blocks[block]=wc3_records_memory(NULL,WC3_RECORD_OBJECT_BLOCK*sizeof(wc3RecordObject_t));
            map->block_count++;
        }
    }
    *wc3_records_object(map,id)=(wc3RecordObject_t){.box={{-1,-1},{-1,-1}},.owner=owner,.flags=flags,.category=WC3_RECORD_INSERT};
    map->live_objects++;return id;
}
static inline bool wc3_records_release(wc3SpatialRecords_t *map,uint32_t id) {
    wc3RecordObject_t *object=wc3_records_object(map,id);
    if(object->stamp!=UINT32_MAX)return true;
    if(!object->refs) {
        object->free_next=map->free_object;map->free_object=id;map->live_objects--;
    }
    return false;
}
static inline uint32_t wc3_records_prepend(wc3SpatialRecords_t *map,uint32_t cell,uint32_t payload,uint32_t kind) {
    uint32_t id;
    if(map->free_head!=WC3_RECORD_END) {
        id=map->free_head;map->free_head=map->links[id].next&WC3_RECORD_END;map->free_count--;
    } else {
        if(map->count==WC3_RECORD_END)abort();
        if(map->count==map->capacity) {
            map->capacity+=WC3_RECORD_GROWTH;
            map->links=wc3_records_memory(map->links,(size_t)map->capacity*sizeof(*map->links));
        }
        id=map->count++;
    }
    map->links[id]=(wc3SpatialRecord_t){kind|(map->cells[cell]&WC3_RECORD_END),payload};
    map->cells[cell]=(map->cells[cell]&~WC3_RECORD_END)|id;map->records++;
    if(map->occupied)map->occupied[(cell/map->width)*map->occupied_stride+(cell%map->width)/64]|=UINT64_C(1)<<(cell%map->width%64);
    if(kind==WC3_RECORD_REMOVE || kind==WC3_RECORD_METADATA)map->dirty[cell/32]|=1u<<(cell%32);
    if(kind!=WC3_RECORD_METADATA)wc3_records_object(map,payload)->refs++;
    return id;
}
static inline void wc3_records_emit(wc3SpatialRecords_t *map,wc3FineBox_t box,uint32_t id,uint32_t kind) {
    box=wc3_records_clip(map,box);
    for(int y=box.min.y;y<box.max.y;y++)for(int x=box.min.x;x<box.max.x;x++)
        wc3_records_prepend(map,(uint32_t)y*map->width+x,id,kind);
}
/*147af0/14d890: only a metadata head can be overwritten. A buried metadata
 * record remains retained; prepend a new one and mark the cell dirty. */
static inline void wc3_records_node(wc3SpatialRecords_t *map,wc3FinePoint_t pos,uint16_t stamp,uint16_t node) {
    uint32_t cell=(uint32_t)pos.y*map->width+pos.x,id=map->cells[cell]&WC3_RECORD_END;
    uint32_t payload=stamp|((uint32_t)node<<16);
    if(id!=WC3_RECORD_END && (map->links[id].next&~WC3_RECORD_END)==WC3_RECORD_METADATA)
        map->links[id].payload=payload;
    else wc3_records_prepend(map,cell,payload,WC3_RECORD_METADATA);
}
/*1d4ae0/14e770: highX,lowX,highY,lowY strips; row-major inside each.
 * This is not equivalent to sorting all changed cells when slot identity matters. */
static inline void wc3_records_difference(wc3SpatialRecords_t *map,wc3FineBox_t box,wc3FineBox_t intersection,uint32_t id,uint32_t kind) {
    if(intersection.min.x>=intersection.max.x || intersection.min.y>=intersection.max.y) {
        wc3_records_emit(map,box,id,kind);return;
    }
    if(intersection.max.x<box.max.x)wc3_records_emit(map,(wc3FineBox_t){{intersection.max.x,intersection.min.y},{box.max.x,intersection.max.y}},id,kind);
    if(box.min.x<intersection.min.x)wc3_records_emit(map,(wc3FineBox_t){{box.min.x,intersection.min.y},{intersection.min.x,intersection.max.y}},id,kind);
    if(intersection.max.y<box.max.y)wc3_records_emit(map,(wc3FineBox_t){{box.min.x,intersection.max.y},box.max},id,kind);
    if(box.min.y<intersection.min.y)wc3_records_emit(map,(wc3FineBox_t){box.min,{box.max.x,intersection.min.y}},id,kind);
}
static inline void wc3_records_update(wc3SpatialRecords_t *map,uint32_t id,wc3FineBox_t box) {
    wc3RecordObject_t *object=wc3_records_object(map,id);
    wc3FineBox_t old=object->box;
    if(!memcmp(&old,&box,sizeof(box)))return;
    wc3FineBox_t intersection={{old.min.x>box.min.x ? old.min.x : box.min.x,old.min.y>box.min.y ? old.min.y : box.min.y},
        {old.max.x<box.max.x ? old.max.x : box.max.x,old.max.y<box.max.y ? old.max.y : box.max.y}};
    wc3_records_difference(map,old,intersection,id,WC3_RECORD_REMOVE);
    wc3_records_difference(map,box,intersection,id,WC3_RECORD_INSERT);
    object->box=box;
}
static inline void wc3_records_retire(wc3SpatialRecords_t *map,uint32_t id) {
    wc3RecordObject_t *object=wc3_records_object(map,id);
    if(!(object->flags&WC3_RECORD_REGION))wc3_records_emit(map,object->box,id,WC3_RECORD_REMOVE);
    object->stamp=UINT32_MAX;wc3_records_release(map,id);
}
static inline void wc3_records_compact_cell(wc3SpatialRecords_t *map,uint32_t cell) {
    uint32_t *previous=map->cells+cell,id=*previous&WC3_RECORD_END;
    if(id==WC3_RECORD_END)return;
    uint32_t stamp=++map->query;
    while(id!=WC3_RECORD_END) {
        wc3SpatialRecord_t *link=map->links+id;
        uint32_t next=link->next&WC3_RECORD_END,kind=link->next&~WC3_RECORD_END;
        wc3RecordObject_t *object=kind!=WC3_RECORD_METADATA ? wc3_records_object(map,link->payload) : NULL;
        if(kind==WC3_RECORD_INSERT && object->stamp!=UINT32_MAX && object->stamp!=stamp) {
            previous=&link->next;
        } else {
            *previous=(*previous&~WC3_RECORD_END)|next;
            link->next=(link->next&~WC3_RECORD_END)|map->free_head;map->free_head=id;
            map->free_count++;map->records--;
            if(object)object->refs--;
        }
        if(object && wc3_records_release(map,link->payload))object->stamp=stamp;
        id=next;
    }
    if(map->occupied && (map->cells[cell]&WC3_RECORD_END)==WC3_RECORD_END)
        map->occupied[(cell/map->width)*map->occupied_stride+(cell%map->width)/64]&=~(UINT64_C(1)<<(cell%map->width%64));
}
static inline void wc3_records_compact(wc3SpatialRecords_t *map,bool all) {
    /* Native repair deliberately retains object stamps. Labelled forced-jump
     * tests cover both the one-query alias and unlink-on-repair hazard. */
    if(map->query>INT32_MAX)map->query=0;
    if(all) {
        for(uint32_t cell=0;cell<map->width*map->height;cell++)wc3_records_compact_cell(map,cell);
    } else for(uint32_t word=0;word<(map->width*map->height+31)/32;word++) {
        uint32_t bits=map->dirty[word];
        while(bits) {unsigned bit=__builtin_ctz(bits);bits&=bits-1;wc3_records_compact_cell(map,word*32+bit);}
        map->dirty[word]=0;
    }
}
/*170c00/170b30: query stamp once, cell stamp for each nonempty raw cell.
 * Removal records suppress older insertions only within their cell. An accepted
 * or rejected ordinary insertion receives the query stamp before the next cell. */
static inline void wc3_records_query_ordered(wc3SpatialRecords_t *map,wc3FineBox_t box,uint32_t source,
    void (*visit)(void *,uint32_t),void *data,bool columns_first) {
    box=wc3_records_clip(map,box);
    if(box.min.x>=box.max.x || box.min.y>=box.max.y)return;
    uint32_t query=++map->query;
    wc3RecordObject_t *exclude=wc3_records_owned(map,source);
    if(exclude)exclude->stamp=query;
    int outer_min=columns_first ? box.min.x : box.min.y,outer_max=columns_first ? box.max.x : box.max.y;
    int inner_min=columns_first ? box.min.y : box.min.x,inner_max=columns_first ? box.max.y : box.max.x;
    for(int outer=outer_min;outer<outer_max;outer++)for(int inner=inner_min;inner<inner_max;inner++) {
        int x=columns_first ? outer : inner,y=columns_first ? inner : outer;
        uint32_t id=map->cells[(uint32_t)y*map->width+x]&WC3_RECORD_END;
        if(id==WC3_RECORD_END)continue;
        uint32_t stamp=++map->query;
        while(id!=WC3_RECORD_END) {
            wc3SpatialRecord_t link=map->links[id];uint32_t kind=link.next&~WC3_RECORD_END;
            if(kind!=WC3_RECORD_METADATA) {
                wc3RecordObject_t *object=wc3_records_object(map,link.payload);
                if(object->stamp!=UINT32_MAX && object->stamp!=query && object->stamp!=stamp) {
                    if(kind==WC3_RECORD_INSERT) {visit(data,object->owner);object->stamp=query;}
                    else object->stamp=stamp;
                }
            }
            id=link.next&WC3_RECORD_END;
        }
    }
}
static inline void wc3_records_query(wc3SpatialRecords_t *map,wc3FineBox_t box,uint32_t source,
    void (*visit)(void *,uint32_t),void *data) {
    wc3_records_query_ordered(map,box,source,visit,data,false);
}
#endif
