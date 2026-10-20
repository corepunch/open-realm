#ifdef BZ_TESTS
#include "shared/test.h"
#include "../g_local.h"
void reset_entities(void);
void setup_test_world(void);
void CM_SetupTestPathmap(uint32_t,uint32_t,uint8_t const *);
void CM_SetupTestWorldBounds(box2_t const *);
bool run_test_jass(cstring_t);
#include "fixtures/retail_mask362.h"
static edict_t *mask362_unit;
static uint32_t const (*mask362_rows)[23];
static uint32_t mask362_cursor,mask362_count;
static bool mask362_mismatch;
static void mask362_before(moveGroup_t const *group) {
    if(mask362_mismatch || group->count!=1 || group->members[0].unit!=mask362_unit) return;
    T_ASSERT(mask362_cursor<mask362_count);
    if(mask362_cursor>=mask362_count)return;
    uint32_t const *row=mask362_rows[mask362_cursor];
    moveFineRoute_t const *path=&mask362_unit->movement.fine_route;
    /* Original amphibious mover radius and captured reach, approach and Follow. */
    T_EQ(wc3_float_bits(wc3_div(S_MoveCollisionRadius(mask362_unit),32)),0x3fc00000u);
    T_EQ(wc3_float_bits(group->members[0].arrival_range),0x413d8000u);
    T_EQ(M_UnitStaticPathingFlags(mask362_unit),0x80);
    T_EQ(wc3_float_bits(mask362_unit->data.UnitData->turnRate),0x3f000000u);
    uint32_t actual[]={level.pathing_counter,group->target_refresh,group->unseen_counter,
        wc3_float_bits(mask362_unit->movement.fine_pose.x),wc3_float_bits(mask362_unit->movement.fine_pose.y),
        mask362_unit->movement.fine_request_time,path->adaptive_admission.time,
        wc3_float_bits(path->adaptive_goal.x),wc3_float_bits(path->adaptive_goal.y),
        wc3_float_bits(group->route.group_goal.x),wc3_float_bits(group->route.group_goal.y),
        group->route.adaptive_admission.time,group->route.group_admission.time,0,group->route.group_count,
        UINT32_MAX,group->route.group_index,path->count,path->adaptive_count,path->index,path->adaptive_index,
        mask362_unit->movement.wait_delay,mask362_unit->movement.retry_count};
    FOR_LOOP(i,23) {
        /* Newly allocated, unpublished route storage has no coordinate identity. */
        if((i==7 || i==8) && !path->adaptive_points)continue;
        if((i==9 || i==10) && !group->initialized)continue;
        if(i==16 && !group->route.group_count)continue;
        if(i==19 && !path->count)continue;
        if(i==20 && !path->adaptive_count)continue;
        T_EQ(actual[i],row[i]);
        if(actual[i]!=row[i]) {
            fprintf(stderr,"mask362 row=%u field=%u counter=%u actual=%08x expected=%08x\n",
                mask362_cursor,i,level.pathing_counter,actual[i],row[i]);
            mask362_mismatch=true;
        }
    }
    mask362_cursor++;
}
static void mask362_journey(void) {
    reset_entities();setup_test_world();
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    float radius=48,target_radius=31,speed=270,turn=.5f,old_min=game.constants.minUnitSpeed,old_max=game.constants.maxUnitSpeed;
    float old_follow=game.constants.followRange;
    game.constants.minUnitSpeed=150;game.constants.maxUnitSpeed=400;game.constants.followRange=300;
    char amphibious[]="amph";
    unitModification_t mods[]={{.modID=MAKEFOURCC('u','c','o','l'),.type=mod_real,.data=&radius},
        {.modID=MAKEFOURCC('u','m','v','s'),.type=mod_real,.data=&speed},{.modID=MAKEFOURCC('u','m','v','t'),.type=mod_string,.data=amphibious},
        {.modID=MAKEFOURCC('u','m','v','r'),.type=mod_real,.data=&turn}};
    unitModification_t target_mods[]={{.modID=MAKEFOURCC('u','c','o','l'),.type=mod_real,.data=&target_radius},
        {.modID=MAKEFOURCC('u','m','v','s'),.type=mod_real,.data=&speed}};
    unitData_t custom[2]={{.originalUnitID=MAKEFOURCC('h','R','T','E'),.newUnitID=MAKEFOURCC('h','M','6','2'),
        .numbeOfModifications=4,.modifications=mods},
        {.originalUnitID=MAKEFOURCC('h','R','T','E'),.newUnitID=MAKEFOURCC('h','T','6','2'),.numbeOfModifications=2,.modifications=target_mods}};
    mapInfo_t info={.num_userCreatedUnits=2,.userCreatedUnits=custom};mapInfo_t const *oldinfo=level.mapinfo;
    level.mapinfo=&info;G_SetMapUnitOverrides(&info);
    uint8_t cells[64*64];memset(cells,0x40,sizeof(cells));
    FOR_LOOP(y,50)FOR_LOOP(x,4)cells[y*64+30+x]=0xca;
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
    level.waypoints=(typeof(level.waypoints)){0};level.pathing_clock=(wc3Clock_t){0,0,300};
    level.time=level.pathing_msec=0;level.pathing_phase=0;level.pathing_due=false;level.pathing_counter=1024;
    int start=1220;
    char script[5000];snprintf(script,sizeof(script),
        "globals\nunit a\nunit b\ninteger tick=0\ninteger dir=1\nendglobals\n"
        "function ModuloInteger takes integer dividend,integer divisor returns integer\n"
        "local integer modulus=dividend-(dividend/divisor)*divisor\n"
        "if modulus<0 then\nset modulus=modulus+divisor\nendif\nreturn modulus\nendfunction\n"
        "function on_tick takes nothing returns nothing\nset tick=tick+1\n"
        "if tick==%d then\nset a=CreateUnit(Player(0),'hM62',480,288,0)\n"
        "set b=CreateUnit(Player(0),'hT62',1568,288,90)\ncall SetUnitMoveSpeed(b,150)\n%s\nendif\n"
        "if tick==%d then\ncall IssueTargetOrder(a,\"%s\",b)\nendif\n"
        "%s\n"
        "if tick==%d then\ncall IssueImmediateOrder(a,\"stop\")\ncall IssueImmediateOrder(b,\"stop\")\nendif\n"
        "endfunction\nfunction main takes nothing returns nothing\n"
        "call FogEnable(false)\ncall FogMaskEnable(false)\n"
        "call TimerStart(CreateTimer(),0.1,true,function on_tick)\nendfunction\n",
        start,"call IssuePointOrder(b,\"move\",GetUnitX(b),1312)",start+5,"smart",
        "if tick==1290 then\ncall IssuePointOrder(b,\"move\",GetUnitX(b),288)\nendif\n"
            "if tick==1360 then\ncall IssuePointOrder(b,\"move\",GetUnitX(b),1312)\nendif\n",start+190);
    T_ASSERT(run_test_jass(script));
    mask362_rows=mask362_amph;
    mask362_count=sizeof(mask362_amph)/sizeof(*mask362_amph);
    mask362_unit=NULL;mask362_cursor=0;mask362_mismatch=false;move_test_group_begin=mask362_before;
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    cstring_t file=Test_TempPath("wc3-mask362-delay.bin");uint32_t saved_cursor=0;
    FOR_LOOP(pass,2) {
        if(pass) {if(!saved_cursor)break;T_ASSERT(ReadGame(file));mask362_cursor=saved_cursor;}
        while(level.time<(start+191)*100 && !mask362_mismatch && mask362_cursor<mask362_count) {
            level.time+=5;globals.RunFrame();
            if(!mask362_unit)FILTER_EDICTS(ent,ent->inuse && ent->class_id==custom[0].newUnitID) {mask362_unit=ent;break;}
            if(!pass && !saved_cursor && level.pathing_counter==5350) {
                T_ASSERT(WriteGame(file));saved_cursor=mask362_cursor;
            }
        }
        T_EQ(mask362_cursor,mask362_count);
        if(mask362_mismatch)break;
        move_test_group_begin=NULL;
        while(level.time<(start+191)*100) {level.time+=5;globals.RunFrame();}
        T_EQ(mask362_unit->current_order_id,0);T_EQ(mask362_unit->movement.group_id,0);
        move_test_group_begin=mask362_before;
    }
    remove(file);move_test_group_begin=NULL;mask362_unit=NULL;level.started=false;
    reset_entities();G_SetMapUnitOverrides(NULL);level.mapinfo=oldinfo;
    game.constants.minUnitSpeed=old_min;game.constants.maxUnitSpeed=old_max;game.constants.followRange=old_follow;
}

TEST(wc3_movement, mask362_amphibious_complete_target_journey_and_save) {mask362_journey();}
#endif
