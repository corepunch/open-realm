/* Diagnostic bridge to production cell traversal; no duplicate predicate. */
#include "../../games/warcraft-3/common/wc3_pathing_cell.h"
typedef struct {uint32_t const *attributes;uint32_t *out,count;} cellProbe_t;
static void cell_emit(void *data,uint32_t id) {
    cellProbe_t *probe=data;
    if(probe->count==32)return;
    probe->out[probe->count++]=id!=WC3_RECORD_END && probe->attributes[id*4+3] ? id : UINT32_MAX;
}
void cell_query(uint32_t mode,uint32_t mask,uint32_t endpoint,uint32_t target,
        uint32_t const *kinds,uint32_t const *identities,uint32_t count,
        uint32_t const *attributes,uint32_t objects,uint32_t terrain,uint32_t out[40]) {
    assert(objects<=4); /* Four identity fixtures;32 separately capped tokens. */
    wc3SpatialRecords_t map={0};wc3_records_init(&map,1,1,objects);
    for(uint32_t i=0;i<objects;i++) {
        uint32_t id=wc3_records_create(&map,i,attributes[i*4+2]);
        wc3RecordObject_t *object=wc3_records_object(&map,id);
        object->category=attributes[i*4];object->stamp=attributes[i*4+1];
    }
    for(uint32_t i=count;i>0;i--)wc3_records_prepend(&map,0,identities[i-1],kinds[i-1]<<24);
    map.query=1000;memset(out,0,40*sizeof(*out));
    cellProbe_t probe={attributes,out+8,0};
    wc3CellQuery_t query={.mode=mode,.mask=mask,.endpoint=endpoint,.target=target,.data=&probe,.emit=cell_emit};
    wc3CellResult_t result=wc3_records_cell(&map,(wc3FinePoint_t){0,0},terrain,&query);
    out[0]=result.value;out[1]=result.target_seen;out[2]=probe.count;out[3]=map.query;
    for(uint32_t i=0;i<objects;i++)out[4+i]=wc3_records_object(&map,i)->stamp;
    wc3_records_free(&map);
}
