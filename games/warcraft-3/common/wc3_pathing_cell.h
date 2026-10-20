#ifndef BZ_WC3_PATHING_CELL_H
#define BZ_WC3_PATHING_CELL_H
#include "wc3_pathing_records.h"

/*1489a0/148e90/148ad0/149170 share encounter semantics, but have distinct
 * eligibility rules. The hierarchy budget counts all49 raw links, including
 * metadata and removals. The other consumers have no traversal limit. */
typedef enum {WC3_CELL_FINE,WC3_CELL_HIERARCHY,WC3_CELL_COLLECT,WC3_CELL_UNION} wc3CellMode_t;
enum {WC3_CELL_HIERARCHY_LINKS=49}; /*148e90 increments before cmp32h/jge. */
typedef struct {
    wc3CellMode_t mode;
    uint32_t mask,target;
    bool endpoint;
    void *data;
    wc3FineObject_t (*describe)(void *,wc3RecordObject_t const *);
    void (*emit)(void *,uint32_t); /* END is terrain/bounds, never an identity. */
} wc3CellQuery_t;
typedef struct {uint32_t value,blocker;bool target_seen;} wc3CellResult_t;

static inline wc3CellResult_t wc3_records_cell(wc3SpatialRecords_t *map,wc3FinePoint_t pos,
        uint32_t terrain,wc3CellQuery_t const *query) {
    wc3CellResult_t result={.value=query->mode==WC3_CELL_UNION ? terrain&0xff000000 : 1,.blocker=WC3_RECORD_END};
    if(pos.x<0 || pos.y<0 || (uint32_t)pos.x>=map->width || (uint32_t)pos.y>=map->height ||
        (terrain&query->mask&0xff000000)) {
        result.value=query->mode==WC3_CELL_UNION ? UINT32_MAX : 0;
        if(query->mode==WC3_CELL_COLLECT)query->emit(query->data,WC3_RECORD_END);
        return result;
    }
    uint32_t id=map->cells[(uint32_t)pos.y*map->width+pos.x]&WC3_RECORD_END;
    if(id==WC3_RECORD_END)return result;
    uint32_t stamp=++map->query,count=0;
    while(id!=WC3_RECORD_END) {
        if(query->mode==WC3_CELL_HIERARCHY && ++count>WC3_CELL_HIERARCHY_LINKS)return result;
        wc3SpatialRecord_t link=map->links[id];uint32_t kind=link.next&~WC3_RECORD_END;
        id=link.next&WC3_RECORD_END;
        if(kind==WC3_RECORD_METADATA)continue;
        wc3RecordObject_t *object=wc3_records_object(map,link.payload);
        if(object->stamp==UINT32_MAX || object->stamp==stamp)continue;
        wc3FineObject_t shape=query->describe ? query->describe(query->data,object) :
            (wc3FineObject_t){object->category,object->flags,true};
        if(!(shape.mask&WC3_RECORD_INSERT) ||
            (query->mode==WC3_CELL_HIERARCHY && !(shape.flags&WC3_RECORD_REGION)))continue;
        object->stamp=stamp;
        if(kind!=WC3_RECORD_INSERT)continue;
        if(query->mode==WC3_CELL_FINE && link.payload==query->target)result.target_seen=true;
        if(shape.flags&0x8fffffff)continue;
        if(query->mode==WC3_CELL_FINE && !query->endpoint && (shape.flags&0x60000000))continue;
        if(query->mode==WC3_CELL_UNION) {result.value|=shape.mask&0xffffff;continue;}
        if(!(shape.mask&query->mask&0xffffff))continue;
        if(query->mode==WC3_CELL_COLLECT) {query->emit(query->data,link.payload);continue;}
        result.value=0;result.blocker=link.payload;return result;
    }
    return result;
}
#endif
