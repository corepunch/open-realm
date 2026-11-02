/* Literal routes copied from unchanged retail fixtures; no expected-policy model. */
typedef struct { uint32_t kind,budget,pops,count; bool complete; uint32_t const (*points)[2]; } baseline203Route_t;
static uint32_t const baseline203_detour_points0[][2]={
    {0x41000000u,0x40800000u},
    {0x40f00000u,0x40600000u},
    {0x40f00000u,0x40200000u},
    {0x40f00000u,0x3fc00000u},
    {0x40d00000u,0x3fc00000u},
    {0x40b00000u,0x3fc00000u},
    {0x40b00000u,0x40200000u},
    {0x40b00000u,0x40600000u},
    {0x40800000u,0x40800000u},
};
static baseline203Route_t const baseline203_detour[]={
    {0,700,UINT32_MAX,9,true,baseline203_detour_points0},
};
static uint32_t const baseline203_blocked_points0[][2]={
    {0x42a70000u,0x42360000u},
    {0x42a78000u,0x42330000u},
    {0x42a38000u,0x42230000u},
    {0x42a38000u,0x42130000u},
    {0x42a38000u,0x42030000u},
};
static uint32_t const baseline203_blocked_points1[][2]={
    {0x42a78000u,0x42330000u},
    {0x42a38000u,0x42230000u},
    {0x42a38000u,0x42130000u},
    {0x42a38000u,0x42030000u},
};
static uint32_t const baseline203_blocked_points2[][2]={
    {0x43278000u,0x42b30000u},
    {0x43278000u,0x42b10000u},
    {0x43268000u,0x42af0000u},
    {0x43258000u,0x42ad0000u},
    {0x43248000u,0x42ab0000u},
    {0x43238000u,0x42a90000u},
    {0x43238000u,0x42a70000u},
    {0x43238000u,0x42a50000u},
    {0x43238000u,0x42a30000u},
    {0x43238000u,0x42a10000u},
    {0x43238000u,0x429f0000u},
    {0x43238000u,0x429d0000u},
    {0x43238000u,0x429b0000u},
    {0x43238000u,0x42990000u},
    {0x43238000u,0x42970000u},
    {0x43238000u,0x42950000u},
    {0x43238000u,0x42930000u},
    {0x43238000u,0x42910000u},
    {0x43238000u,0x428f0000u},
    {0x43238000u,0x428d0000u},
    {0x43238000u,0x428b0000u},
    {0x43238000u,0x42890000u},
    {0x43238000u,0x42870000u},
    {0x43238000u,0x42850000u},
    {0x43238000u,0x42830000u},
};
static uint32_t const baseline203_blocked_points3[][2]={
    {0x42a70000u,0x42360000u},
    {0x42a76d45u,0x423224a3u},
};
static uint32_t const baseline203_blocked_points4[][2]={
    {0x43238000u,0x42b10000u},
    {0x43248000u,0x42b10000u},
    {0x43258000u,0x42b10000u},
    {0x43268000u,0x42b10000u},
    {0x43278000u,0x42b10000u},
    {0x43276d45u,0x42b224a3u},
};
static uint32_t const baseline203_blocked_points5[][2]={
    {0x4323dfcbu,0x42b11410u},
};
static baseline203Route_t const baseline203_blocked[]={
    {1,5000,130,5,false,baseline203_blocked_points0},
    {1,400,4,4,true,baseline203_blocked_points1},
    {0,700,103,25,true,baseline203_blocked_points2},
    {1,400,133,2,false,baseline203_blocked_points3},
    {0,700,701,6,false,baseline203_blocked_points4},
    {0,700,701,1,false,baseline203_blocked_points5},
};
static uint32_t const baseline203_disconnected_points0[][2]={
    {0x41da0000u,0x41de0000u},
    {0xc7fa0001u,0x3f800000u},
    {0x40f80000u,0x410c0000u},
    {0x40980000u,0x40980000u},
};
static uint32_t const baseline203_disconnected_points1[][2]={
    {0x41d5b603u,0x41e2321du},
    {0xc7fa0001u,0x3f800000u},
    {0x40f80000u,0x410c0000u},
    {0x40880000u,0x40980000u},
};
static uint32_t const baseline203_disconnected_points2[][2]={
    {0x41780000u,0x418c0000u},
    {0x41680000u,0x41840000u},
    {0x41680000u,0x41780000u},
    {0x41580000u,0x41680000u},
    {0x41480000u,0x41580000u},
    {0x41380000u,0x41480000u},
    {0x41280000u,0x41380000u},
    {0x41180000u,0x41280000u},
    {0x41080000u,0x41180000u},
};
static uint32_t const baseline203_disconnected_points3[][2]={
    {0x41de49fcu,0x41d9cde2u},
    {0xc7fa0001u,0x3f800000u},
    {0x40f80000u,0x410c0000u},
    {0x40980000u,0x40980000u},
};
static uint32_t const baseline203_disconnected_points4[][2]={
    {0x41780000u,0x418c0000u},
    {0x41680000u,0x41840000u},
    {0x41680000u,0x41780000u},
    {0x41680000u,0x41680000u},
    {0x41580000u,0x41580000u},
    {0x41480000u,0x41480000u},
    {0x41380000u,0x41380000u},
    {0x41280000u,0x41280000u},
    {0x41180000u,0x41180000u},
};
static uint32_t const baseline203_disconnected_points5[][2]={
    {0x41780000u,0x418c0000u},
    {0x41680000u,0x41840000u},
    {0x41680000u,0x41780000u},
    {0x41580000u,0x41680000u},
    {0x41480000u,0x41580000u},
    {0x41380000u,0x41480000u},
    {0x41280000u,0x41380000u},
    {0x41180000u,0x41280000u},
    {0x41080000u,0x41180000u},
};
static uint32_t const baseline203_disconnected_points6[][2]={
    {0x41da0000u,0x41de0000u},
    {0xc7fa0001u,0x3f800000u},
    {0x40f80000u,0x410c0000u},
    {0x40980000u,0x40980000u},
};
static uint32_t const baseline203_disconnected_points7[][2]={
    {0x41d5b603u,0x41e2321du},
    {0xc7fa0001u,0x3f800000u},
    {0x40f80000u,0x410c0000u},
    {0x40880000u,0x40980000u},
};
static uint32_t const baseline203_disconnected_points8[][2]={
    {0x41780000u,0x418c0000u},
    {0x41680000u,0x41840000u},
    {0x41680000u,0x41780000u},
    {0x41580000u,0x41680000u},
    {0x41480000u,0x41580000u},
    {0x41380000u,0x41480000u},
    {0x41280000u,0x41380000u},
    {0x41180000u,0x41280000u},
    {0x41080000u,0x41180000u},
};
static uint32_t const baseline203_disconnected_points9[][2]={
    {0x41de49fcu,0x41d9cde2u},
    {0xc7fa0001u,0x3f800000u},
    {0x40f80000u,0x410c0000u},
    {0x40980000u,0x40980000u},
};
static uint32_t const baseline203_disconnected_points10[][2]={
    {0x41780000u,0x418c0000u},
    {0x41680000u,0x41840000u},
    {0x41680000u,0x41780000u},
    {0x41680000u,0x41680000u},
    {0x41580000u,0x41580000u},
    {0x41480000u,0x41480000u},
    {0x41380000u,0x41380000u},
    {0x41280000u,0x41280000u},
    {0x41180000u,0x41180000u},
};
static uint32_t const baseline203_disconnected_points11[][2]={
    {0x41780000u,0x418c0000u},
    {0x41680000u,0x41840000u},
    {0x41680000u,0x41780000u},
    {0x41580000u,0x41680000u},
    {0x41480000u,0x41580000u},
    {0x41380000u,0x41480000u},
    {0x41280000u,0x41380000u},
    {0x41180000u,0x41280000u},
    {0x41080000u,0x41180000u},
};
static uint32_t const baseline203_disconnected_points12[][2]={
    {0x41f40000u,0x42320000u},
    {0x41f40000u,0x422e0000u},
    {0x41f40000u,0x422a0000u},
    {0x41f40000u,0x42260000u},
    {0x41f40000u,0x42220000u},
    {0x41f40000u,0x421e0000u},
    {0x41f40000u,0x421a0000u},
    {0x41f40000u,0x42160000u},
    {0x41f40000u,0x42120000u},
    {0x41f40000u,0x420e0000u},
    {0x41f40000u,0x420a0000u},
    {0x41f40000u,0x42060000u},
    {0x41f40000u,0x42020000u},
    {0x41ec0000u,0x41fc0000u},
    {0x41e40000u,0x41f40000u},
    {0x41dc0000u,0x41ec0000u},
    {0x41d40000u,0x41e40000u},
    {0x41cc0000u,0x41dc0000u},
    {0x41c40000u,0x41d40000u},
    {0x41bc0000u,0x41cc0000u},
    {0x41b40000u,0x41c40000u},
    {0x41ac0000u,0x41bc0000u},
    {0x41a40000u,0x41b40000u},
    {0x419c0000u,0x41ac0000u},
    {0x41940000u,0x41a40000u},
    {0x418c0000u,0x419c0000u},
    {0x41840000u,0x41940000u},
    {0x41780000u,0x418c0000u},
    {0x416f7c27u,0x418644b8u},
};
static uint32_t const baseline203_disconnected_points13[][2]={
    {0x41f40000u,0x42360000u},
    {0x41f40000u,0x42320000u},
    {0x41f40000u,0x422e0000u},
    {0x41f40000u,0x422a0000u},
    {0x41f40000u,0x42260000u},
    {0x41f40000u,0x42220000u},
    {0x41f40000u,0x421e0000u},
    {0x41f40000u,0x421a0000u},
    {0x41f40000u,0x42160000u},
    {0x41f40000u,0x42120000u},
    {0x41f40000u,0x420e0000u},
    {0x41f40000u,0x420a0000u},
    {0x41f40000u,0x42060000u},
    {0x41f40000u,0x42020000u},
    {0x41ec0000u,0x41fc0000u},
    {0x41e40000u,0x41f40000u},
    {0x41e40000u,0x41ec0000u},
    {0x41dc0000u,0x41e40000u},
    {0x41d40000u,0x41dc0000u},
    {0x41cc0000u,0x41d40000u},
    {0x41c40000u,0x41cc0000u},
    {0x41bc0000u,0x41c40000u},
    {0x41b40000u,0x41bc0000u},
    {0x41ac0000u,0x41b40000u},
    {0x41a40000u,0x41ac0000u},
    {0x419c0000u,0x41a40000u},
    {0x41940000u,0x419c0000u},
    {0x418c0000u,0x41940000u},
    {0x41840000u,0x418c0000u},
    {0x41700056u,0x41876df3u},
};
static uint32_t const baseline203_disconnected_points14[][2]={
    {0x41f40000u,0x425a0000u},
    {0x41f40000u,0x42560000u},
    {0x41f40000u,0x42520000u},
    {0x41f40000u,0x424e0000u},
    {0x41f40000u,0x424a0000u},
    {0x41f40000u,0x42460000u},
    {0x41f40000u,0x42420000u},
    {0x41f40000u,0x423e0000u},
    {0x41f40000u,0x423a0000u},
    {0x41f40000u,0x42360000u},
    {0x41f27726u,0x42309fc9u},
};
static uint32_t const baseline203_disconnected_points15[][2]={
    {0x41f40000u,0x42620000u},
    {0x41f40000u,0x425e0000u},
    {0x41f40000u,0x425a0000u},
    {0x41f40000u,0x42560000u},
    {0x41f40000u,0x42520000u},
    {0x41f40000u,0x424e0000u},
    {0x41f40000u,0x424a0000u},
    {0x41f40000u,0x42460000u},
    {0x41f40000u,0x42420000u},
    {0x41f40000u,0x423e0000u},
    {0x41f40000u,0x423a0000u},
    {0x41f271ddu,0x423477f7u},
};
static uint32_t const baseline203_disconnected_points16[][2]={
    {0x41f3fec3u,0x4258fb78u},
};
static uint32_t const baseline203_disconnected_points17[][2]={
    {0x41f40000u,0x42620000u},
    {0x41ec0000u,0x425e0000u},
    {0x41ec0000u,0x425a0000u},
    {0x41ec0000u,0x42560000u},
    {0x41f3ee07u,0x4254beccu},
};
static uint32_t const baseline203_disconnected_points18[][2]={
    {0x41f3fec3u,0x4258fb78u},
};
static uint32_t const baseline203_disconnected_points19[][2]={
    {0x41f3fec3u,0x4258fb78u},
};
static uint32_t const baseline203_disconnected_points20[][2]={
    {0x41f2309du,0x4260d2b5u},
};
static uint32_t const baseline203_disconnected_points21[][2]={
    {0x41f3fec3u,0x4258fb78u},
};
static uint32_t const baseline203_disconnected_points22[][2]={
    {0x41f2309du,0x4260d2b5u},
};
static uint32_t const baseline203_disconnected_points23[][2]={
    {0x41f3fec3u,0x4258fb78u},
};
static uint32_t const baseline203_disconnected_points24[][2]={
    {0x41f2309du,0x4260d2b5u},
};
static uint32_t const baseline203_disconnected_points25[][2]={
    {0x41f3fec3u,0x4258fb78u},
};
static uint32_t const baseline203_disconnected_points26[][2]={
    {0x41f2309du,0x4260d2b5u},
};
static uint32_t const baseline203_disconnected_points27[][2]={
    {0x41f3fec3u,0x4258fb78u},
};
static uint32_t const baseline203_disconnected_points28[][2]={
    {0x41f2309du,0x4260d2b5u},
};
static uint32_t const baseline203_disconnected_points29[][2]={
    {0x41f2309du,0x4260d2b5u},
};
static baseline203Route_t const baseline203_disconnected[]={
    {1,5000,3,4,true,baseline203_disconnected_points0},
    {1,400,3,4,true,baseline203_disconnected_points1},
    {0,700,15,9,true,baseline203_disconnected_points2},
    {1,400,3,4,true,baseline203_disconnected_points3},
    {0,700,19,9,true,baseline203_disconnected_points4},
    {0,700,15,9,true,baseline203_disconnected_points5},
    {1,5000,3,4,true,baseline203_disconnected_points6},
    {1,400,3,4,true,baseline203_disconnected_points7},
    {0,700,15,9,true,baseline203_disconnected_points8},
    {1,400,3,4,true,baseline203_disconnected_points9},
    {0,700,19,9,true,baseline203_disconnected_points10},
    {0,700,15,9,true,baseline203_disconnected_points11},
    {0,700,701,29,false,baseline203_disconnected_points12},
    {0,700,701,30,false,baseline203_disconnected_points13},
    {0,700,701,11,false,baseline203_disconnected_points14},
    {0,700,701,12,false,baseline203_disconnected_points15},
    {0,700,701,1,false,baseline203_disconnected_points16},
    {0,700,701,5,false,baseline203_disconnected_points17},
    {0,700,701,1,false,baseline203_disconnected_points18},
    {0,700,701,1,false,baseline203_disconnected_points19},
    {0,700,701,1,false,baseline203_disconnected_points20},
    {0,700,701,1,false,baseline203_disconnected_points21},
    {0,700,701,1,false,baseline203_disconnected_points22},
    {0,700,701,1,false,baseline203_disconnected_points23},
    {0,700,701,1,false,baseline203_disconnected_points24},
    {0,700,701,1,false,baseline203_disconnected_points25},
    {0,700,701,1,false,baseline203_disconnected_points26},
    {0,700,701,1,false,baseline203_disconnected_points27},
    {0,700,701,1,false,baseline203_disconnected_points28},
    {0,700,701,1,false,baseline203_disconnected_points29},
};
static uint32_t const baseline203_blocked_retries[][8]={
    {0x4323dfcbu,0x42b11410u,0x43270000u,0x42b60000u,0x00000000u,0x00000001u,0x00000001u,0x00000001u},
    {0x4323dfcbu,0x42b11410u,0x43270000u,0x42b60000u,0x00000001u,0x00000001u,0x00000004u,0x00000001u},
};
