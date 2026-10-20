#ifdef BZ_TESTS
#include "shared/test.h"
#include "../g_local.h"
#include "fixtures/retail_crowd361.h"

edict_t *alloc_test_unit(uint32_t class_id,float x,float y);
void reset_entities(void);
void setup_test_world(void);
void CM_SetupTestPathmap(uint32_t,uint32_t,uint8_t const *);
void CM_SetupTestWorldBounds(box2_t const *);
bool run_test_jass(cstring_t);

static edict_t *crowd361_units[36];
static unsigned crowd361_cursor;
static bool crowd361_mismatch;

static void crowd361_before(moveGroup_t const *group) {
    if(crowd361_mismatch || group->count!=1)return;
    unsigned actor=0;
    while(actor<36 && crowd361_units[actor]!=group->members[0].unit)actor++;
    if(actor==36)return;
    T_ASSERT(crowd361_cursor<sizeof(crowd361_rows)/sizeof(*crowd361_rows));
    if(crowd361_cursor>=sizeof(crowd361_rows)/sizeof(*crowd361_rows))return;
    uint32_t const *expected=crowd361_rows[crowd361_cursor];
    edict_t *unit=crowd361_units[actor];
    moveFineRoute_t const *path=&unit->movement.fine_route;
    uint32_t actual[]={level.pathing_counter,actor,group->target_refresh,group->unseen_counter,
        wc3_float_bits(unit->movement.fine_pose.x),wc3_float_bits(unit->movement.fine_pose.y),
        unit->movement.fine_request_time,path->adaptive_admission.time,
        wc3_float_bits(path->adaptive_goal.x),wc3_float_bits(path->adaptive_goal.y),
        wc3_float_bits(group->route.group_goal.x),wc3_float_bits(group->route.group_goal.y),
        group->route.adaptive_admission.time,group->route.group_admission.time,
        0,group->route.group_count,UINT32_MAX,group->route.group_index,
        path->count,path->adaptive_count,path->index,path->adaptive_index};
    FOR_LOOP(i,22) {
        if((i==8 || i==9) && !path->adaptive_points)continue;
        if((i==10 || i==11) && !group->initialized)continue;
        if(i==17 && !group->route.group_count)continue;
        if(i==20 && !path->count)continue;
        if(i==21 && !path->adaptive_count)continue;
        T_EQ(actual[i],expected[i]);
        if(actual[i]!=expected[i]) {
            fprintf(stderr,"crowd361 row=%u actor=%u counter=%u field=%u actual=%08x expected=%08x\n",
                crowd361_cursor,actor,level.pathing_counter,i,actual[i],expected[i]);
            crowd361_mismatch=true;
        }
    }
    crowd361_cursor++;
}

TEST(wc3_movement, crowd361_natural_contention_replays_every_owner_and_delayed_save) {
    reset_entities();setup_test_world();
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    float radius=31,speed=270;
    float old_min=game.constants.minUnitSpeed,old_max=game.constants.maxUnitSpeed;
    float old_follow=game.constants.followRange;
    game.constants.minUnitSpeed=150;game.constants.maxUnitSpeed=400;game.constants.followRange=300;
    unitModification_t mods[]={{.modID=MAKEFOURCC('u','c','o','l'),.type=mod_real,.data=&radius},
        {.modID=MAKEFOURCC('u','m','v','s'),.type=mod_real,.data=&speed}};
    unitData_t custom[2]={
        {.originalUnitID=MAKEFOURCC('h','R','T','E'),.newUnitID=MAKEFOURCC('h','C','6','1'),.numbeOfModifications=2,.modifications=mods},
        {.originalUnitID=MAKEFOURCC('h','R','T','E'),.newUnitID=MAKEFOURCC('h','T','6','1'),.numbeOfModifications=2,.modifications=mods}};
    mapInfo_t info={.num_userCreatedUnits=2,.userCreatedUnits=custom};
    mapInfo_t const *old_info=level.mapinfo;level.mapinfo=&info;G_SetMapUnitOverrides(&info);
    uint8_t cells[64*64]={0};FOR_LOOP(y,50)FOR_LOOP(x,4)cells[y*64+30+x]=2;
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
    level.waypoints=(typeof(level.waypoints)){0};level.pathing_clock=(wc3Clock_t){0,0,300};
    level.time=level.pathing_msec=level.pathing_phase=0;level.pathing_due=false;level.pathing_counter=1024;
    T_ASSERT(run_test_jass("globals\nunit array cohort\nunit target\ninteger tick=0\nendglobals\n"
        "function on_tick takes nothing returns nothing\nlocal integer i=0\nset tick=tick+1\n"
        "if tick==20 then\nset target=CreateUnit(Player(0),'hT61',1568,288,90)\n"
        "loop\nexitwhen i==36\nset cohort[i]=CreateUnit(Player(0),'hC61',160+I2R(i-(i/6)*6)*64,160+I2R(i/6)*64,0)\nset i=i+1\nendloop\n"
        "call SetUnitMoveSpeed(target,150)\ncall IssuePointOrder(target,\"move\",GetUnitX(target),1312)\nendif\n"
        "if tick==25 then\nloop\nexitwhen i==36\ncall IssueTargetOrder(cohort[i],\"smart\",target)\nset i=i+1\nendloop\nendif\n"
        "if tick==90 then\ncall IssuePointOrder(target,\"move\",GetUnitX(target),288)\nendif\n"
        "if tick==160 then\ncall IssuePointOrder(target,\"move\",GetUnitX(target),1312)\nendif\n"
        "if tick==210 then\ncall IssueImmediateOrder(cohort[0],\"stop\")\ncall IssueImmediateOrder(target,\"stop\")\nendif\n"
        "if tick==215 then\ncall RemoveUnit(cohort[0])\ncall RemoveUnit(target)\n"
        "loop\nexitwhen i==36\ncall RemoveUnit(cohort[i])\nset i=i+1\nendloop\nendif\n"
        "endfunction\nfunction main takes nothing returns nothing\n"
        "call FogEnable(false)\ncall FogMaskEnable(false)\ncall SetTimeOfDayScale(0)\n"
        "call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)\n"
        "call TimerStart(CreateTimer(),0.1,true,function on_tick)\nendfunction\n"));
    memset(crowd361_units,0,sizeof(crowd361_units));crowd361_cursor=0;crowd361_mismatch=false;
    move_test_group_begin=crowd361_before;
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    cstring_t file=Test_TempPath("wc3-crowd361-contention.bin");unsigned saved_cursor=0;
    FOR_LOOP(pass,2) {
        if(pass){if(!saved_cursor)break;T_ASSERT(ReadGame(file));crowd361_cursor=saved_cursor;}
        while(level.time<21600 && !crowd361_mismatch && crowd361_cursor<sizeof(crowd361_rows)/sizeof(*crowd361_rows)) {
            level.time+=5;globals.RunFrame();
            if(!crowd361_units[0]) {
                unsigned count=0;
                FILTER_EDICTS(unit,unit->inuse && unit->class_id==custom[0].newUnitID) {
                    if(count<36)crowd361_units[count++]=unit;
                }
                if(count)T_EQ(count,36);
            }
            if(!pass && !saved_cursor && level.pathing_counter==1142) {
                T_ASSERT(WriteGame(file));saved_cursor=crowd361_cursor;
            }
        }
        T_EQ(crowd361_cursor,sizeof(crowd361_rows)/sizeof(*crowd361_rows));
        if(crowd361_mismatch)break;
        move_test_group_begin=NULL;
        while(level.time<21600){level.time+=5;globals.RunFrame();}
        FILTER_EDICTS(unit,unit->inuse) {
            T_NE(unit->class_id,custom[0].newUnitID);T_NE(unit->class_id,custom[1].newUnitID);
        }
        move_test_group_begin=crowd361_before;
    }
    remove(file);move_test_group_begin=NULL;memset(crowd361_units,0,sizeof(crowd361_units));level.started=false;
    reset_entities();G_SetMapUnitOverrides(NULL);level.mapinfo=old_info;
    game.constants.minUnitSpeed=old_min;game.constants.maxUnitSpeed=old_max;game.constants.followRange=old_follow;
}
#endif
