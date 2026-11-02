/* Diagnostic bridge to the exact production record implementation. */
#include "../../games/warcraft-3/common/wc3_pathing_records.h"

wc3SpatialRecords_t *records_create(uint32_t width,uint32_t height,uint32_t owners) {
    wc3SpatialRecords_t *map=calloc(1,sizeof(*map));
    if(!map || !wc3_records_init(map,width,height,owners))abort();
    return map;
}
void records_free(wc3SpatialRecords_t *map) {wc3_records_free(map);free(map);}
uint32_t records_object(wc3SpatialRecords_t *map,uint32_t owner,uint32_t flags) {
    uint32_t id=wc3_records_create(map,owner,flags);map->objects[owner]=id;return id;
}
void records_update(wc3SpatialRecords_t *map,uint32_t id,int y0,int x0,int y1,int x1) {
    wc3_records_update(map,id,(wc3FineBox_t){{x0,y0},{x1,y1}});
}
void records_retire(wc3SpatialRecords_t *map,uint32_t id) {wc3_records_retire(map,id);}
void records_metadata(wc3SpatialRecords_t *map,uint32_t x,uint32_t y,uint32_t payload) {
    wc3_records_prepend(map,y*map->width+x,payload,WC3_RECORD_METADATA);
}
void records_compact(wc3SpatialRecords_t *map,uint32_t all) {wc3_records_compact(map,all);}
void records_stamp(wc3SpatialRecords_t *map,uint32_t stamp) {map->query=stamp;}
void records_object_stamp(wc3SpatialRecords_t *map,uint32_t id,uint32_t stamp) {wc3_records_object(map,id)->stamp=stamp;}
void records_fields(wc3SpatialRecords_t const *map,uint32_t out[9]) {
    uint32_t fields[]={map->count,map->capacity,map->records,map->free_head,map->free_count,
        map->query,map->raw_objects,map->live_objects,map->block_count};memcpy(out,fields,sizeof(fields));
}
uint32_t records_cell(wc3SpatialRecords_t const *map,uint32_t cell) {return map->cells[cell]&WC3_RECORD_END;}
uint32_t records_dirty(wc3SpatialRecords_t const *map,uint32_t cell) {return (map->dirty[cell/32]>>(cell%32))&1;}
void records_link(wc3SpatialRecords_t const *map,uint32_t id,uint32_t out[3]) {
    wc3SpatialRecord_t link=map->links[id];out[0]=link.next&WC3_RECORD_END;
    out[1]=link.next>>24;out[2]=link.payload;
}
void records_object_fields(wc3SpatialRecords_t const *map,uint32_t id,uint32_t out[7]) {
    wc3RecordObject_t const *o=wc3_records_object(map,id);
    uint32_t fields[]={o->refs,o->stamp,o->flags,o->box.min.y,o->box.min.x,o->box.max.y,o->box.max.x};
    memcpy(out,fields,sizeof(fields));
}
typedef struct {uint32_t *out,count;} recordsQuery_t;
static void records_visit(void *data,uint32_t owner) {
    recordsQuery_t *query=data;query->out[query->count++]=owner;
}
uint32_t records_query(wc3SpatialRecords_t *map,int y0,int x0,int y1,int x1,uint32_t *out) {
    recordsQuery_t query={.out=out};wc3_records_query(map,(wc3FineBox_t){{x0,y0},{x1,y1}},UINT32_MAX,records_visit,&query);
    return query.count;
}
