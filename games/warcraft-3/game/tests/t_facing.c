#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "retail_facing207.h"
#include "retail_facing_visual207.h"

static edict_t *facing207_units[5];
static unsigned facing207_cursor;
static bool facing207_mismatch;

static void facing207_before(moveGroup_t const *group) {
    if(!group->turning || facing207_mismatch)return;
    T_ASSERT(facing207_cursor<(sizeof(facing207_owner)/sizeof(*facing207_owner)));
    if(facing207_cursor>=(sizeof(facing207_owner)/sizeof(*facing207_owner)))return;
    edict_t const *unit=group->members[0].unit;
    unsigned role=5;FOR_LOOP(i,5)if(unit==facing207_units[i])role=i;
    moveGroupMember_t const *member=group->members;
    uint32_t actual[]={level.pathing_counter,role,group->flags,group->age,wc3_float_bits(group->turn_rate),
        wc3_float_bits(group->point.x),wc3_float_bits(group->point.y),
        wc3_float_bits(group->route.group_goal.x),wc3_float_bits(group->route.group_goal.y),
        group->route.group_count,group->route.group_index,
        wc3_float_bits(unit->movement.fine_pose.x),wc3_float_bits(unit->movement.fine_pose.y),
        wc3_float_bits(wc3_div(unit->movement.velocity.x,32)),wc3_float_bits(wc3_div(unit->movement.velocity.y,32)),
        wc3_float_bits(unit->s.angle),wc3_float_bits(member->destination.x),wc3_float_bits(member->destination.y),
        wc3_float_bits(wc3_div(member->speed,32)),wc3_float_bits(member->heading),member->flags};
    FOR_LOOP(i,(sizeof(actual)/sizeof(*actual))) {
        T_EQ(actual[i],facing207_owner[facing207_cursor][i]);
        if(actual[i]!=facing207_owner[facing207_cursor][i]) {
            fprintf(stderr,"facing207 row=%u counter=%u field=%u actual=%08x expected=%08x\n",
                facing207_cursor,level.pathing_counter,i,actual[i],facing207_owner[facing207_cursor][i]);
            facing207_mismatch=true;
        }
    }
}

static void facing207_after(edict_t *unit) {
    moveGroup_t const *group=move_unit_group(unit);
    if(!group || !group->turning || facing207_mismatch)return;
    uint32_t actual[]={wc3_float_bits(unit->movement.fine_pose.x),wc3_float_bits(unit->movement.fine_pose.y),
        wc3_float_bits(wc3_div(unit->movement.velocity.x,32)),wc3_float_bits(wc3_div(unit->movement.velocity.y,32)),
        wc3_float_bits(unit->s.angle),group->members[0].arrived};
    FOR_LOOP(i,(sizeof(actual)/sizeof(*actual))) {
        T_EQ(actual[i],facing207_owner[facing207_cursor][21+i]);
        if(actual[i]!=facing207_owner[facing207_cursor][21+i]) {
            fprintf(stderr,"facing207 row=%u after=%u actual=%08x expected=%08x\n",
                facing207_cursor,i,actual[i],facing207_owner[facing207_cursor][21+i]);
            facing207_mismatch=true;
        }
    }
    facing207_cursor++;
}

static unsigned facing207_visual_cursor[5];
static uint32_t const (*const facing207_visual_rows[5])[5]={facing207_visual0,facing207_visual1,
    facing207_visual2,facing207_visual3,facing207_visual4};
static unsigned const facing207_visual_count[5]={sizeof(facing207_visual0)/sizeof(*facing207_visual0),
    sizeof(facing207_visual1)/sizeof(*facing207_visual1),sizeof(facing207_visual2)/sizeof(*facing207_visual2),
    sizeof(facing207_visual3)/sizeof(*facing207_visual3),sizeof(facing207_visual4)/sizeof(*facing207_visual4)};
static void facing207_visual(edict_t *unit,float facing,float speed) {
    unsigned role=5;FOR_LOOP(i,5)if(unit==facing207_units[i])role=i;
    T_ASSERT(role<5);if(role>=5)return;
    unsigned index=facing207_visual_cursor[role]++;
    T_ASSERT(index<facing207_visual_count[role]);if(index>=facing207_visual_count[role])return;
    uint32_t actual[]={wc3_float_bits(unit->s.angle),wc3_float_bits(facing),wc3_float_bits(speed),
        wc3_float_bits(unit->movement.visual_facing),wc3_float_bits(unit->movement.visual_speed)};
    FOR_LOOP(i,5) {
        T_EQ(actual[i],facing207_visual_rows[role][index][i]);
        if(actual[i]!=facing207_visual_rows[role][index][i]) {
            fprintf(stderr,"facing207 visual role=%u row=%u field=%u actual=%08x expected=%08x\n",
                role,index,i,actual[i],facing207_visual_rows[role][index][i]);facing207_mismatch=true;
        }
    }
}

TEST(wc3_facing, public_timed_cohorts_match_original_and_saved_suffix) {
    reset_entities();setup_test_world();
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    float radius=31,speed=270,old_min=game.constants.minUnitSpeed,old_max=game.constants.maxUnitSpeed;
    game.constants.minUnitSpeed=150;game.constants.maxUnitSpeed=400;
    unitModification_t mods[]={{.modID=MAKEFOURCC('u','c','o','l'),.type=mod_real,.data=&radius},
        {.modID=MAKEFOURCC('u','m','v','s'),.type=mod_real,.data=&speed}};
    unitData_t custom={.originalUnitID=MAKEFOURCC('h','R','T','E'),.newUnitID=MAKEFOURCC('h','F','0','0'),
        .numbeOfModifications=2,.modifications=mods};
    mapInfo_t info={.num_userCreatedUnits=1,.userCreatedUnits=&custom};mapInfo_t const *oldinfo=level.mapinfo;
    level.mapinfo=&info;G_SetMapUnitOverrides(&info);
    uint8_t cells[64*64]={0};CM_SetupTestPathmap(64,64,cells);
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});
    level.waypoints=(typeof(level.waypoints)){0};level.pathing_clock=(wc3Clock_t){0,0,300};
    level.time=level.pathing_msec=level.pathing_phase=0;level.pathing_due=false;level.pathing_counter=1024;
    T_ASSERT(run_test_jass("globals\nunit array u\ninteger tick=0\nendglobals\n"
        "function on_tick takes nothing returns nothing\nset tick=tick+1\n"
        "if tick==3 then\ncall PauseUnit(u[4],true)\nendif\n"
        "if tick==4 then\ncall IssuePointOrder(u[1],\"move\",1536,512)\nendif\n"
        "if tick==5 then\ncall SetUnitFacingTimed(u[0],90,1.2)\ncall SetUnitFacingTimed(u[1],90,1.2)\n"
        "call SetUnitFacingTimed(u[2],180,0.1)\ncall SetUnitFacingTimed(u[3],45,0.3001)\n"
        "call SetUnitFacingTimed(u[4],90,1.2)\nendif\n"
        "if tick==10 then\ncall PauseUnit(u[4],false)\nendif\n"
        "if tick==15 then\ncall SetUnitFacingTimed(u[2],270,0.100001)\nendif\n"
        "if tick==25 then\ncall SetUnitFacingTimed(u[0],270,0.6)\nendif\n"
        "if tick==28 then\ncall IssuePointOrder(u[0],\"move\",1536,256)\nendif\n"
        "if tick==35 then\ncall SetUnitFacingTimed(u[1],180,0)\nendif\nendfunction\n"
        "function main takes nothing returns nothing\nlocal integer i=0\n"
        "call FogEnable(false)\ncall FogMaskEnable(false)\nloop\nexitwhen i==5\n"
        "set u[i]=CreateUnit(Player(0),'hF00',512,256+256*i,0)\ncall SetUnitAcquireRange(u[i],0)\n"
        "set i=i+1\nendloop\ncall SetUnitFacing(u[3],315)\n"
        "call TimerStart(CreateTimer(),0.1,true,function on_tick)\nendfunction\n"));
    unsigned n=0;FILTER_EDICTS(ent,ent->inuse && ent->class_id==custom.newUnitID)if(n<5)facing207_units[n++]=ent;
    T_EQ(n,5);facing207_cursor=0;facing207_mismatch=false;memset(facing207_visual_cursor,0,sizeof(facing207_visual_cursor));
    move_test_group_begin=facing207_before;move_test_motion_commit=facing207_after;move_test_visual_commit=facing207_visual;
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    cstring_t file=Test_TempPath("wc3-facing207.bin");unsigned saved=0,saved_visual[5]={0};
    FOR_LOOP(pass,2) {
        if(pass && saved) {T_ASSERT(ReadGame(file));facing207_cursor=saved;memcpy(facing207_visual_cursor,saved_visual,sizeof(saved_visual));}
        while(level.time<8000 && !facing207_mismatch && level.time<8000) {
            level.time+=5;globals.RunFrame();
            if(!pass && !saved && facing207_cursor>=20) {saved=facing207_cursor;memcpy(saved_visual,facing207_visual_cursor,sizeof(saved_visual));T_ASSERT(WriteGame(file));}
        }
        T_EQ(facing207_cursor,(sizeof(facing207_owner)/sizeof(*facing207_owner)));
        FOR_LOOP(i,5)T_EQ(facing207_visual_cursor[i],facing207_visual_count[i]);
        if(facing207_mismatch)break;
    }
    move_test_group_begin=NULL;move_test_motion_commit=NULL;move_test_visual_commit=NULL;level.started=false;remove(file);
    reset_entities();setup_test_world();G_SetMapUnitOverrides(NULL);level.mapinfo=oldinfo;
    game.constants.minUnitSpeed=old_min;game.constants.maxUnitSpeed=old_max;
}

TEST(wc3_facing, timed_native_retains_pose_and_authored_turn_at_admission) {
    reset_entities();setup_test_world();
    T_ASSERT(run_test_jass("globals\nunit u\nendglobals\n"
        "function main takes nothing returns nothing\n"
        "set u=CreateUnit(Player(0),'hfoo',512,256,0)\nendfunction\n"
        "function face takes nothing returns nothing\n"
        "call SetUnitFacingTimed(u,90,1.2)\nendfunction\n"));
    edict_t *unit=NULL;
    FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','f','o','o')) {unit=ent;break;}
    T_NOT_NULL(unit);if(!unit)return;
    float facing=unit->s.angle,turn=unit_turnspeed(unit);
    vec2_t origin=unit->s.origin2;
    jass_callbyname(level.vm,"face",false);
    T_EQ(wc3_float_bits(unit->s.angle),wc3_float_bits(facing));
    T_EQ(unit->s.origin2.x,origin.x);T_EQ(unit->s.origin2.y,origin.y);
    T_EQ(unit_turnspeed(unit),turn);T_EQ(unit->current_order_id,0);
    T_ASSERT(!jass_rterror_pending(level.vm));
    reset_entities();setup_test_world();
}

TEST(wc3_facing, paused_idle_head_resumes_without_canceling_angular_owner) {
    reset_entities();setup_test_world();
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    level.pathing_clock=(wc3Clock_t){0,0,300};level.time=level.pathing_msec=level.pathing_phase=0;
    level.pathing_due=false;level.pathing_counter=1024;
    T_ASSERT(run_test_jass("globals\nunit u\nendglobals\n"
        "function main takes nothing returns nothing\n"
        "set u=CreateUnit(Player(0),'hfoo',512,256,0)\ncall PauseUnit(u,true)\n"
        "call SetUnitFacingTimed(u,90,1.2)\nendfunction\n"
        "function resume takes nothing returns nothing\ncall PauseUnit(u,false)\nendfunction\n"));
    edict_t *unit=NULL;FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','f','o','o')) {unit=ent;break;}
    T_NOT_NULL(unit);if(!unit)return;
    T_EQ(unit->current_order_id,MOVE_ORDER_SUSPENDED);
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    FOR_LOOP(i,20) {level.time+=5;globals.RunFrame();}
    uint32_t id=unit->movement.group_id;
    T_ASSERT(id);T_ASSERT(unit->s.angle>0);T_EQ(unit->current_order_id,MOVE_ORDER_SUSPENDED);
    jass_callbyname(level.vm,"resume",false);
    T_EQ(unit->current_order_id,MOVE_ORDER_SUSPENDED);
    level.time+=5;globals.RunFrame();T_EQ(unit->current_order_id,MOVE_ORDER_SUSPENDED);
    level.time+=5;globals.RunFrame();T_EQ(unit->current_order_id,0);
    T_EQ(unit->movement.group_id,id);T_ASSERT(move_unit_group(unit)->turning);
    level.started=false;reset_entities();setup_test_world();
}
#endif
