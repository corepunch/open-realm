/* Controlled composition uses the production record/region/cell algorithms. */
#include "../../games/warcraft-3/common/wc3_pathing_regions.h"
#include "../../games/warcraft-3/common/wc3_pathing_cell.h"
#include "../../games/warcraft-3/common/wc3_pathing_segment.h"
typedef struct {wc3SpatialRecords_t map;wc3RegionCollection_t regions;uint32_t unit,mask,exclude,count,*out;bool endpoint;} regionProbe_t;
void *region_create(void) {
    regionProbe_t *scene=calloc(1,sizeof(*scene));assert(scene);
    wc3_records_init(&scene->map,16,16,1);scene->unit=wc3_records_create(&scene->map,0,0);
    wc3_records_object(&scene->map,scene->unit)->category=0x010000ca;
    wc3_regions_resize(&scene->map,&scene->regions,1,3);return scene;
}
void region_free(void *data) {regionProbe_t *scene=data;wc3_records_free(&scene->map);free(scene);}
void region_action(void *data,uint32_t action) {
    regionProbe_t *scene=data;wc3SpatialRecords_t *map=&scene->map;
    if(action==1 || action==4) {
        int cell=action==1 ? 7 : 12;
        wc3_records_update(map,scene->unit,(wc3FineBox_t){{cell-1,cell-1},{cell+1,cell+1}});
    } else if(action==2 || action==6) {
        uint8_t pixels[9];memset(pixels,0xda,sizeof(pixels));
        wc3_regions_raster(map,&scene->regions,3,3,pixels,(float[]){208,208},(float[]){0,0},(float[]){512,512},0,
            action==2 ? WC3_RECORD_INSERT : WC3_RECORD_REMOVE);
    } else if(action==3)wc3_records_retire(map,scene->unit);
    else if(action==5)wc3_regions_resize(map,&scene->regions,1,0);
    else wc3_records_compact(map,action==8);
}
/* Stable normalized identities: A=0, B.c2=1, B.10=2, B.08=3. */
uint32_t region_state(void *data,uint32_t *out) {
    regionProbe_t *scene=data;wc3SpatialRecords_t *map=&scene->map;uint32_t n=0;
    out[n++]=map->records;out[n++]=map->count;out[n++]=map->free_count;
    out[n++]=(map->dirty[(6*16+6)/32]>>((6*16+6)%32))&1;
    out[n++]=map->query;
    for(uint32_t i=0;i<4;i++) {
        wc3RecordObject_t const *object=wc3_records_object(map,i);
        out[n++]=object->category;out[n++]=object->stamp;out[n++]=object->refs;out[n++]=object->flags;
    }
    uint32_t count_at=n++;out[count_at]=0;
    for(uint32_t link=map->cells[6*16+6]&WC3_RECORD_END;link!=WC3_RECORD_END;link=map->links[link].next&WC3_RECORD_END) {
        out[n++]=map->links[link].next>>24;out[n++]=map->links[link].payload;out[count_at]++;
    }
    return n;
}
static wc3FineObject_t region_describe(void *data,wc3RecordObject_t const *object) {
    regionProbe_t *scene=data;
    return (wc3FineObject_t){object->category,object->flags|((scene->exclude && object->owner==0) ? 1 : 0),true};
}
static void region_emit(void *data,uint32_t id) {
    regionProbe_t *scene=data;if(scene->count<32)scene->out[scene->count++]=id==0 ? 0 : UINT32_MAX;
}
static bool region_cell(void const *data,wc3FinePoint_t pos) {
    regionProbe_t *scene=(regionProbe_t *)data;
    wc3CellQuery_t query={.mode=WC3_CELL_FINE,.mask=scene->mask,.target=WC3_RECORD_END,
        .endpoint=scene->endpoint,.describe=region_describe,.data=scene};
    return wc3_records_cell(&scene->map,pos,0,&query).value;
}
uint32_t region_query(void *data,uint32_t op,uint32_t mask,uint32_t cls,uint32_t endpoint,uint32_t exclude,uint32_t out[40]) {
    regionProbe_t *scene=data;scene->mask=mask;scene->endpoint=endpoint;scene->exclude=exclude;
    wc3FineSegment_t query={.cls=cls,.cell=region_cell,.data=scene};
    if(op==0 || op==4)return region_cell(scene,(wc3FinePoint_t){6,6});
    if(op==1) {
        /*148d00 raw perimeter bits at7,6, offset1,width3; no early return. */
        wc3FinePoint_t pos={6,5};static int const directions[4][2]={{1,0},{0,1},{-1,0},{0,-1}};
        uint32_t blocked=0;
        for(unsigned edge=0,bit=0;edge<4;edge++)for(unsigned i=0;i<2;i++,bit++) {
            if(!region_cell(scene,pos))blocked|=1u<<bit;
            pos.x+=directions[edge][0];pos.y+=directions[edge][1];
        }
        return blocked;
    }
    if(op==2 || op==3)return wc3_segment_foot(&query,(wc3FinePoint_t){6,6},0);
    wc3CellQuery_t cell={.mode=op==5 ? WC3_CELL_COLLECT : WC3_CELL_UNION,.mask=mask,
        .target=WC3_RECORD_END,.describe=region_describe,.emit=region_emit,.data=scene};
    scene->out=out;scene->count=0;
    wc3CellResult_t result=wc3_records_cell(&scene->map,(wc3FinePoint_t){6,6},0,&cell);
    return op==5 ? scene->count : result.value;
}
void region_hierarchy(void *data,uint32_t out[8]) {
    regionProbe_t *scene=data;
    static uint32_t const masks[]={6,0x80,0x40,4};
    static int const corners[4][2]={{0,0},{1,0},{1,1},{0,1}};
    /* Rig dimensions: base(16+16)/2+1=17, parents8/4/2. */
    uint8_t base[4][17*17]={0};
    for(unsigned y=0;y<17;y++)for(unsigned x=0;x<17;x++)for(unsigned lane=0;lane<4;lane++) {
        unsigned blocked=0;
        for(unsigned corner=0;corner<4;corner++) {
            wc3CellQuery_t query={.mode=WC3_CELL_HIERARCHY,.mask=masks[lane]|masks[lane]<<24,.target=WC3_RECORD_END};
            blocked+=!wc3_records_cell(&scene->map,(wc3FinePoint_t){2*x+corners[corner][0],2*y+corners[corner][1]},0,&query).value;
        }
        base[lane][y*17+x]=blocked==4 ? 1 : blocked ? 2 : 0;
    }
    for(unsigned lane=0;lane<4;lane++) {
        out[lane]=base[lane][3*17+3];unsigned value=base[lane][2*17+2];
        for(unsigned y=2;y<4;y++)for(unsigned x=2;x<4;x++)if(base[lane][y*17+x]!=value)value=2;
        out[4+lane]=value;
    }
}
