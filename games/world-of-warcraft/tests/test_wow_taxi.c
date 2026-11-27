#include "test.h"
#include "game/g_taxi.h"
#include <string.h>

TEST(wow_taxi, routes_discovered_directed_paths_by_fare) {
    wowTaxiNode_t nodes[]={{1,0,0,0,0,"One"},{2,0,0,0,0,"Two"},{3,0,0,0,0,"Three"}};
    wowTaxiPath_t paths[]={{10,1,2,3,0,2},{11,2,3,4,2,2},{12,1,3,20,4,2}};
    wowTaxiPoint_t points[]={{10,0,0,0,0,0,0,0},{10,1,0,1,0,0,0,0},
        {11,0,0,0,0,0,0,0},{11,1,0,1,0,0,0,0},
        {12,0,0,0,0,0,0,0},{12,1,0,1,0,0,0,0}};
    wowTaxiGraph_t graph={nodes,3,paths,3,points,6};
    wowTaxiRoute_t route; uint8_t known[1]={14};
    T_ASSERT(WowTaxi_Route(&graph,1,3,known,sizeof(known),&route));
    T_EQ(route.price,7); T_EQ(route.count,2); T_EQ(route.paths[0],10); T_EQ(route.paths[1],11);
    T_ASSERT(!WowTaxi_Route(&graph,3,1,known,sizeof(known),&route));
    known[0] &= ~(1u << 2);
    T_ASSERT(WowTaxi_Route(&graph,1,3,known,sizeof(known),&route)); T_EQ(route.price,20);
    known[0] &= ~(1u << 3); T_ASSERT(!WowTaxi_Route(&graph,1,3,known,sizeof(known),&route));
}
TEST(wow_taxi, malformed_dbc_is_rejected) {
    wowTaxiGraph_t graph={0}; uint8_t bad[24]={0};
    T_ASSERT(!WowTaxi_Load(&graph,bad,sizeof(bad),bad,sizeof(bad),bad,sizeof(bad)));
    T_EQ(graph.node_count,0); WowTaxi_Free(&graph);
}
static void put32(uint8_t *p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static void header(uint8_t *p, uint32_t rows, uint32_t fields, uint32_t strings) {
    memcpy(p,"WDBC",4); put32(p+4,rows); put32(p+8,fields); put32(p+12,fields*4); put32(p+16,strings);
}
TEST(wow_taxi, loads_and_orders_authored_points) {
    uint8_t nodes[20+2*24+5]={0}, paths[20+16+1]={0}, points[20+2*36+1]={0};
    wowTaxiGraph_t graph={0}; wowTaxiRoute_t route; uint8_t known[1]={6};
    header(nodes,2,6,5); header(paths,1,4,1); header(points,2,9,1);
    put32(nodes+20,1); put32(nodes+40,1); put32(nodes+44,2); put32(nodes+64,3);
    memcpy(nodes+68,"\0A\0B",5);
    put32(paths+20,7); put32(paths+24,1); put32(paths+28,2); put32(paths+32,5);
    put32(points+24,7); put32(points+28,1);
    put32(points+60,7); put32(points+64,0);
    T_ASSERT(WowTaxi_Load(&graph,nodes,sizeof(nodes),paths,sizeof(paths),points,sizeof(points)));
    T_EQ(graph.node_count,2); T_EQ(graph.point_count,2); T_EQ(graph.points[0].index,0);
    T_STREQ(graph.nodes[0].name,"A");
    T_ASSERT(WowTaxi_Route(&graph,1,2,known,sizeof(known),&route)); T_EQ(route.price,5);
    WowTaxi_Free(&graph);
}
TEST(wow_taxi, rejects_cross_map_and_incomplete_paths) {
    wowTaxiNode_t nodes[]={{1,0,0,0,0,"One"},{2,0,0,0,0,"Two"}};
    wowTaxiPath_t paths[]={{10,1,2,3,0,2}};
    wowTaxiPoint_t points[]={{10,0,0,0,0,0,0,0},{10,1,1,1,0,0,0,0}};
    wowTaxiGraph_t graph={nodes,2,paths,1,points,2};
    wowTaxiRoute_t route; uint8_t known[]={6};
    T_ASSERT(!WowTaxi_Route(&graph,1,2,known,sizeof(known),&route));
    points[1].map=0; graph.point_count=1;
    T_ASSERT(!WowTaxi_Route(&graph,1,2,known,sizeof(known),&route));
}
TEST(wow_taxi, rejects_truncated_and_overflowed_envelopes) {
    uint8_t dbc[24]={0}; wowTaxiGraph_t graph={0};
    header(dbc,0xffffffffu,9,0xffffffffu);
    T_ASSERT(!WowTaxi_Load(&graph,dbc,sizeof(dbc),dbc,sizeof(dbc),dbc,sizeof(dbc)));
    header(dbc,1,9,0);
    T_ASSERT(!WowTaxi_Load(&graph,dbc,sizeof(dbc),dbc,sizeof(dbc),dbc,sizeof(dbc)));
    WowTaxi_Free(&graph);
}
