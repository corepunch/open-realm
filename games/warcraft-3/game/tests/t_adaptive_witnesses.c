#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "games/warcraft-3/common/wc3_pathing_adaptive.h"
#include "games/warcraft-3/common/wc3_pathing_gate.h"

typedef struct { char const *name; uint32_t fine, count, gates, gate_count; } retailAccMap_t;
typedef struct { uint32_t id, x, y, width, height, dx, dy, active; } retailAccGate_t;
typedef struct {
    uint32_t map, lane, size, warp, budget, pose[4];
    uint32_t result, work, nodes, warps, words, count;
} retailAccCase_t;
typedef struct { uint32_t row, first, count; } retailAccDetail_t;
typedef struct { uint32_t row, cost; } retailAccGoal_t;
typedef struct { uint32_t map, size, budget, result, work, nodes, nearest, distance, first[2]; } retailAccBudget_t;
typedef struct { uint8_t classes[4], markers[4], parent[4], unmarked[4]; } retailAccParent_t;
#include "retail_adaptive_witnesses.h"

/* Constructor-padded map dimensions for the handoff's64x64 fine storage.
 * These supplied inputs are distinct from the retail producer evidence. */
typedef struct {
    wc3AccSearch_t search;
    uint8_t fine[64*64], classes[41*41+20*20+10*10+5*5], markers[41*41];
    int indices[41*41+20*20+10*10+5*5];
    wc3AccGate_t gates[BZ_WC3_GATE_RECORDS];
} accWitness_t;

static void acc_witness_map(accWitness_t *data, uint32_t index, uint32_t lane) {
    retailAccMap_t const *map=retail_acc_maps+index;
    uint8_t const flags[]={2,0x80,0x40,4}, masks[]={6,0x80,0x40,4};
    memset(data->fine,0,sizeof(data->fine)); memset(data->markers,0,sizeof(data->markers));
    memset(data->gates,0,sizeof(data->gates));
    FOR_LOOP(i,map->count) data->fine[retail_acc_fine_cells[map->fine+i]]=flags[lane/2];
    FOR_LOOP(i,map->gate_count) {
        retailAccGate_t const *g=retail_acc_gates+map->gates+i;
        wc3_gate_stamp(data->markers,41,41,2*g->x,2*g->y,
            2*(g->x+g->width-1),2*(g->y+g->height-1),(uint8_t)g->id);
        data->gates[g->id]=(wc3AccGate_t){g->active,{(int)g->dx,(int)g->dy}};
    }
    uint32_t offset=0;
    FOR_LOOP(level,4) {
        uint32_t side=41u>>level;
        wc3AccMap_t *current=data->search.maps+level;
        *current=(wc3AccMap_t){side,side,data->classes+offset,data->indices+offset};
        memset(current->indices,0,side*side*sizeof(int));
        FOR_LOOP(y,side) FOR_LOOP(x,side) {
            uint8_t value;
            if (!level) {
                unsigned blocked=0;
                FOR_LOOP(dy,2) FOR_LOOP(dx,2) {
                    uint32_t fx=2*x+dx,fy=2*y+dy;
                    blocked+=fx>=64 || fy>=64 || (data->fine[fy*64+fx]&masks[lane/2])!=0;
                }
                value=blocked==4 ? 1 : blocked ? 2 : 0;
            } else {
                wc3AccMap_t const *child=data->search.maps+level-1;
                value=wc3_gate_parent(child->classes,level==1?data->markers:NULL,
                    child->width,child->height,2*x,2*y);
            }
            data->classes[offset+y*side+x]=value;
        }
        offset+=side*side;
    }
    data->search.markers=data->markers; data->search.gates=data->gates;
}

static uint32_t acc_witness_request(accWitness_t *data, retailAccCase_t const *row) {
    static wc3FineVector_t points[BZ_WC3_ACC_ROUTE_NODES];
    acc_witness_map(data,row->map,row->lane); data->search.warp=row->warp!=0;
    wc3AccRequest_t request={{wc3_float(row->pose[0]),wc3_float(row->pose[1])},
        {wc3_float(row->pose[2]),wc3_float(row->pose[3])},1u<<row->size,row->budget};
    uint32_t result=wc3_acc_route(&data->search,&request,points),count=result&0x7fffffffu;
    if (!(count==row->count && (!(result&0x80000000u))==row->result &&
        data->search.work.pops==row->work && data->search.work.count==row->nodes &&
        data->search.warps==row->warps))
        fprintf(stderr,"Adaptive witness %s lane%u size%u budget%u warp%u\n",
            retail_acc_maps[row->map].name,row->lane,row->size,row->budget,row->warp);
    T_EQ(!(result&0x80000000u),row->result); T_EQ(count,row->count);
    T_EQ(data->search.work.pops,row->work); T_EQ(data->search.work.count,row->nodes);
    T_EQ(data->search.warps,row->warps);
    if (count==row->count) FOR_LOOP(i,count) {
        T_EQ(wc3_float_bits(points[i].x),retail_acc_route_words[row->words+2*i]);
        T_EQ(wc3_float_bits(points[i].y),retail_acc_route_words[row->words+2*i+1]);
    }
    return result;
}

/* 3,288 producer requests cover463 reachable branch outcomes and276 level/
 * clamp transitions.336 ordinary/transposed cost requests extend that matrix.
 * Exercise both literal resets and the production epoch/alias optimization. */
TEST(wc3_adaptive_witnesses, complete_routes_and_identity_epochs) {
    FOR_LOOP(reuse,2) {
        accWitness_t data={0}; data.search.reuse_indices=reuse!=0;
        FOR_LOOP(i,sizeof(retail_acc_cases)/sizeof(*retail_acc_cases))
            acc_witness_request(&data,retail_acc_cases+i);
        wc3_acc_free(&data.search);
    }
}

/* Full original tables retain first-lookup representatives, parent order,
 * reopen states, costs and source/incoming markers for the ten rare outcomes. */
TEST(wc3_adaptive_witnesses, rare_branches_keep_complete_node_state) {
    FOR_LOOP(reuse,2) {
        accWitness_t data={0}; data.search.reuse_indices=reuse!=0;
        FOR_LOOP(c,sizeof(retail_acc_details)/sizeof(*retail_acc_details)) {
            retailAccDetail_t const *detail=retail_acc_details+c;
            acc_witness_request(&data,retail_acc_cases+detail->row);
            T_EQ(data.search.work.count,detail->count);
            if(data.search.work.count==detail->count) FOR_LOOP(i,detail->count) {
                wc3FineNode_t const *node=data.search.work.nodes+i;
                int32_t const actual[]={node->pos.x,node->pos.y,data.search.levels[i],
                    node->g,node->h,node->parent,node->state,data.search.source_ids[i],data.search.gate_ids[i]};
                FOR_LOOP(word,9) T_EQ(actual[word],retail_acc_node_words[(detail->first+i)*9+word]);
            }
        }
        wc3_acc_free(&data.search);
    }
}

TEST(wc3_adaptive_witnesses, integer_costs_and_budget_nearest) {
    for(uint32_t a=0;a<=520;a++) for(uint32_t b=0;b<=a;b++) {
        uint32_t n=576u*(a*a+b*b),root=(uint32_t)sqrt((double)n);
        T_EQ(wc3_acc_cost((wc3FinePoint_t){0,0},(wc3FinePoint_t){a,b}),root);
    }
    accWitness_t data={0}; data.search.reuse_indices=true;
    FOR_LOOP(i,sizeof(retail_acc_goals)/sizeof(*retail_acc_goals)) {
        retailAccGoal_t const *goal=retail_acc_goals+i;
        retailAccCase_t const *row=retail_acc_cases+goal->row;
        acc_witness_request(&data,row);
        int at=wc3_acc_find(&data.search,0,data.search.goal);
        T_ASSERT(at>=0);
        if(at>=0) T_EQ(data.search.work.nodes[at].g,goal->cost);
    }
    FOR_LOOP(i,sizeof(retail_acc_budgets)/sizeof(*retail_acc_budgets)) {
        retailAccBudget_t const *row=retail_acc_budgets+i;
        acc_witness_map(&data,row->map,0); data.search.warp=false;
        wc3AccRequest_t request={{4.25f,4.75f},{27.25f,27.75f},1u<<row->size,row->budget};
        static wc3FineVector_t points[BZ_WC3_ACC_ROUTE_NODES];
        uint32_t result=wc3_acc_route(&data.search,&request,points);
        T_EQ(!(result&0x80000000u),row->result); T_EQ(data.search.work.pops,row->work);
        T_EQ(data.search.work.count,row->nodes); T_EQ(data.search.work.nearest,row->nearest);
        T_EQ(data.search.work.dist2,row->distance);
        T_EQ(wc3_float_bits(points[0].x),row->first[0]);
        T_EQ(wc3_float_bits(points[0].y),row->first[1]);
    }
    wc3_acc_free(&data.search);
}

TEST(wc3_adaptive_witnesses, marker_parents_match_original_reducer) {
    FOR_LOOP(i,sizeof(retail_acc_parents)/sizeof(*retail_acc_parents)) {
        retailAccParent_t const *row=retail_acc_parents+i;
        FOR_LOOP(lane,4) {
            uint8_t children[4];
            FOR_LOOP(c,4) children[c]=(row->classes[c]>>(6-2*lane))&3;
            T_EQ(wc3_gate_parent(children,row->markers,2,2,0,0),row->parent[lane]);
            T_EQ(wc3_gate_parent(children,NULL,2,2,0,0),row->unmarked[lane]);
        }
    }
    /* Original marker9 at base13,13 forces a mixed clear ancestor at every
     * level. Erasure leaves the clear descendants and restores all parents. */
    accWitness_t data={0}; acc_witness_map(&data,0,0);
    memset(data.markers,0,sizeof(data.markers));
    FOR_LOOP(marked,2) {
        memset(data.classes,0,sizeof(data.classes));
        wc3_gate_stamp(data.markers,41,41,26,26,26,26,marked?0:9);
        T_EQ(data.markers[13*41+13],marked?0:9);
        FOR_LOOP(level,3) {
            wc3AccMap_t const *child=data.search.maps+level,*parent=data.search.maps+level+1;
            FOR_LOOP(y,parent->height) FOR_LOOP(x,parent->width)
                ((uint8_t *)parent->classes)[y*parent->width+x]=wc3_gate_parent(child->classes,
                    level?NULL:data.markers,child->width,child->height,2*x,2*y);
            unsigned at=13u>>(level+1);
            T_EQ(parent->classes[at*parent->width+at],marked?0:2);
            FOR_LOOP(cell,parent->width*parent->height) T_ASSERT(parent->classes[cell]<3);
        }
    }
}
#endif
