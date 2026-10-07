/* Public timed-life factory witnesses: eight classes and unknown-ID fallback. */
#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"
#include "jass/jass.h"
#include "retail_timed_life_156.h"

void reset_entities(void);
void setup_test_world(void);
bool run_test_jass(cstring_t src);
extern void (*test_preload_marker)(cstring_t);
edict_t *alloc_test_unit(uint32_t,float,float);

static unsigned timedlife_marker_156;
static void TimedLifeMarker156(cstring_t text) {
    if(strncmp(text,"RSG ",4))return;
    /* Engine rawcodes are byte-packed identifiers; retail JASS prints their
     * big-endian integer spelling. Normalize reporting only, keeping every
     * captured class-level, health, food and timeline assertion unchanged. */
    char normalized[512];
    cstring_t buff=strstr(text," buff=");
    if(buff) {
        char *end;
        uint32_t code=strtoul(buff+6,&end,10);
        uint32_t retail=((code&0xff)<<24)|((code&0xff00)<<8)|((code>>8)&0xff00)|(code>>24);
        snprintf(normalized,sizeof(normalized),"%.*s buff=%u%s",(int)(buff-text),text,retail,end);
        text=normalized;
    }
    T_ASSERT(timedlife_marker_156<sizeof(timedlife_markers_156)/sizeof(*timedlife_markers_156));
    if(timedlife_marker_156>=sizeof(timedlife_markers_156)/sizeof(*timedlife_markers_156))return;
    cstring_t expected=timedlife_markers_156[timedlife_marker_156++];
    T_STREQ(text,expected);
    if(strcmp(text,expected))fprintf(stderr,"Timed-life marker%u: %s != %s\n",timedlife_marker_156-1,text,expected);
}

TEST(wc3_movement, public_timed_life_matches_completed_retail_markers_and_saved_pause) {
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    G_BotStop(0);reset_entities();setup_test_world();
    float old_decay=game.constants.decayTime,old_bones=game.constants.boneDecayTime;
    game.constants.decayTime=2;game.constants.boneDecayTime=88;
    uint32_t food=2,health=420,death_type=3;
    unitModification_t mods[]={
        {.modID=MAKEFOURCC('u','f','o','o'),.type=mod_int,.data=&food},
        {.modID=MAKEFOURCC('u','h','p','m'),.type=mod_int,.data=&health},
        {.modID=MAKEFOURCC('u','d','e','a'),.type=mod_int,.data=&death_type},
        {.modID=MAKEFOURCC('u','m','d','l'),.type=mod_string,.data="TestUI\\Models\\unit_death.mdx"}};
    unitData_t type={.originalUnitID=MAKEFOURCC('h','R','T','E'),
        .newUnitID=MAKEFOURCC('h','R','A','0'),.numbeOfModifications=4,.modifications=mods};
    mapInfo_t info={.num_userCreatedUnits=1,.userCreatedUnits=&type};
    mapInfo_t const *old_info=level.mapinfo;level.mapinfo=&info;G_SetMapUnitOverrides(&info);
    CM_SetupTestWorldBounds(&(box2_t){{-2048,-2048},{2048,2048}});
    level.pathing_clock=(wc3Clock_t){0,0,300};level.time=level.pathing_msec=level.pathing_phase=0;
    level.timer_clock_valid=false;timedlife_marker_156=0;test_preload_marker=TimedLifeMarker156;
    T_ASSERT(run_test_jass(timedlife_script_156));
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    while(level.time<6050) {
        level.time+=FRAMETIME;globals.RunFrame();
        if(level.time==700) {
            T_ASSERT(WriteGame("/tmp/wc3-timedlife156-pause.bin"));
            T_ASSERT(ReadGame("/tmp/wc3-timedlife156-pause.bin"));remove("/tmp/wc3-timedlife156-pause.bin");
        }
    }
    T_EQ(timedlife_marker_156,sizeof(timedlife_markers_156)/sizeof(*timedlife_markers_156));
    T_ASSERT(!jass_rterror_pending(level.vm));
    if(jass_rterror_pending(level.vm))fprintf(stderr,"Timed-life capture: %s\n",jass_rterror_message(level.vm));
    test_preload_marker=NULL;
    game.constants.decayTime=old_decay;game.constants.boneDecayTime=old_bones;
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    G_BotStop(0);level.started=false;reset_entities();setup_test_world();G_SetMapUnitOverrides(NULL);level.mapinfo=old_info;
}

TEST(wc3_movement, timed_life_records_grow_independently_of_script_timer_capacity) {
    reset_entities();setup_test_world();
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    level.pathing_clock=(wc3Clock_t){0,0,300};level.time=level.pathing_msec=level.pathing_phase=0;
    edict_t *units[4096];
    FOR_LOOP(i,4096) {
        units[i]=alloc_test_unit(MAKEFOURCC('h','R','T','E'),0,0);
        units[i]->die=unit_die;
        S_ApplyTimedLife(units[i],MAKEFOURCC('B','T','L','F'),.02f);
    }
    FOR_LOOP(i,64)S_ApplyTimedLife(units[0],MAKEFOURCC('B','T','L','F'),2);
    T_EQ(level.num_timers,0);
    T_ASSERT(WriteGame("/tmp/wc3-timedlife156-growth.bin"));
    T_ASSERT(ReadGame("/tmp/wc3-timedlife156-growth.bin"));remove("/tmp/wc3-timedlife156-growth.bin");
    G_FreeEdict(units[1]);edict_t *replacement=alloc_test_unit(MAKEFOURCC('h','R','T','E'),0,0);
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    while(level.time<50){level.time+=5;globals.RunFrame();}
    FOR_LOOP(i,4096)if(i!=1){T_ASSERT(M_IsDead(units[i]));T_EQ(S_TimedLifeLevel(units[i],MAKEFOURCC('B','T','L','F')),0);}
    T_ASSERT(!M_IsDead(replacement));
    level.started=false;reset_entities();setup_test_world();
}

TEST(wc3_movement, public_timed_life_factory_publishes_temporary_captain_ranges) {
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    G_BotStop(0);reset_entities();setup_test_world();
    uint32_t food=3,health=123;float radius=31;
    unitModification_t mods[]={
        {.modID=MAKEFOURCC('u','f','o','o'),.type=mod_int,.data=&food},
        {.modID=MAKEFOURCC('u','h','p','m'),.type=mod_int,.data=&health},
        {.modID=MAKEFOURCC('u','c','o','l'),.type=mod_unreal,.data=&radius}
    };
    unitData_t type={.originalUnitID=MAKEFOURCC('h','R','T','E'),
        .newUnitID=MAKEFOURCC('h','T','L','0'),.numbeOfModifications=3,.modifications=mods};
    mapInfo_t info={.num_userCreatedUnits=1,.userCreatedUnits=&type};
    info.players[0].playerType=kPlayerTypeHuman;
    mapInfo_t const *old_info=level.mapinfo;level.mapinfo=&info;G_SetMapUnitOverrides(&info);
    static uint8_t cells[128*128];memset(cells,0,sizeof(cells));
    box2_t bounds={{-2048,-2048},{2048,2048}};
    CM_SetupTestWorldBounds(&bounds);CM_SetupTestPathmap(128,128,cells);
    level.pathing_clock=(wc3Clock_t){0,0,300};level.time=level.pathing_msec=0;
    char const *buffs[]={"BTLF","BUan","BFig","BEfn","Bhwd","Bplg","Brai","BHwe","ZZZZ"};
    char script[5000];int length=snprintf(script,sizeof(script),
        "globals\nunit array roster\nendglobals\nfunction main takes nothing returns nothing\n");
    FOR_LOOP(i,9)length+=snprintf(script+length,sizeof(script)-length,
        "set roster[%u]=CreateUnit(Player(0),'hTL0',%d,-976,90)\n"
        "call UnitApplyTimedLife(roster[%u],'%s',30.0)\n",
        i,-1536+(int)i*128,i,buffs[i]);
    length+=snprintf(script+length,sizeof(script)-length,
        "call UnitApplyTimedLife(null,'BTLF',1.0)\n"
        "call BJassAssert(IsUnitType(roster[0],UNIT_TYPE_SUMMONED),\"timed life must publish summoned\")\nendfunction\n");
    T_ASSERT(length>0 && length<sizeof(script));T_ASSERT(run_test_jass(script));
    edict_t *units[9]={0};unsigned count=0;
    FILTER_EDICTS(unit,unit->inuse && unit->class_id==type.newUnitID)if(count<9)units[count++]=unit;
    T_EQ(count,9);
    botCaptain_t captain={.home={-1024,1024},.home_set=true,.units=units,.units_count=9,.created={0,0,300}};
    S_SetCaptainHomeActor(&captain,0,BOT_CAPTAIN_ATTACK+1);
    FOR_LOOP(i,9) {
        T_NOT_NULL(units[i]);if(!units[i])continue;
        T_EQ(G_UnitAbilityLevel(units[i],FS_SLKKey(i==8 ? "BTLF" : buffs[i])),1);
        if(i>0 && i<8)T_EQ(G_UnitAbilityLevel(units[i],FS_SLKKey("BTLF")),0);
        T_NOT_NULL(units[i]->food);if(units[i]->food)T_EQ(units[i]->food->used,0);
        T_ASSERT(S_IssueCaptainHomeMove(units[i],&captain));
        moveGroup_t const *group=move_find_group(units[i]->movement.group_id);
        T_NOT_NULL(group);if(group)T_EQ(wc3_float_bits(group->members[0].arrival_range),0x40220000);
    }
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED],0);
    T_ASSERT(WriteGame("/tmp/wc3-timedlife156.bin"));T_ASSERT(ReadGame("/tmp/wc3-timedlife156.bin"));
    FOR_LOOP(i,9) {
        T_EQ(G_UnitAbilityLevel(units[i],FS_SLKKey(i==8 ? "BTLF" : buffs[i])),1);
        moveGroup_t const *group=move_find_group(units[i]->movement.group_id);
        T_NOT_NULL(group);if(group)T_EQ(wc3_float_bits(group->members[0].arrival_range),0x40220000);
    }
    remove("/tmp/wc3-timedlife156.bin");
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    G_BotStop(0);reset_entities();setup_test_world();G_SetMapUnitOverrides(NULL);level.mapinfo=old_info;
}
TEST(wc3_movement, public_timed_life_duplicates_pause_removal_and_permanent_markers) {
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    G_BotStop(0);reset_entities();setup_test_world();
    level.pathing_clock=(wc3Clock_t){0,0,300};level.time=level.pathing_msec=0;
    level.timer_clock_valid=false;level.started=level.scriptsConfigured=true;level.scriptsStarted=false;
    static char const script[]=
        "native CreateTimer takes nothing returns timer\n"
        "native TimerStart takes timer t, real timeout, boolean periodic, code handler returns nothing\n"
        "native PauseTimer takes timer t returns nothing\n"
        "globals\nunit array u\ninteger tick=0\ntimer timer1=null\nendglobals\n"
        "function Sample takes nothing returns nothing\n"
        "set tick=tick+1\n"
        "if tick==1 then\ncall BJassAssert(GetWidgetLife(u[2])==0.0,\"short life expires\")\n"
        "elseif tick==5 then\ncall UnitApplyTimedLife(u[3],'BTLF',3.0)\n"
        "call UnitPauseTimedLife(u[4],true)\n"
        "call BJassAssert(UnitRemoveAbility(u[5],'BTLF'),\"remove timed life\")\n"
        "call BJassAssert(GetWidgetLife(u[5])==0.0,\"removal kills immediately\")\n"
        "call UnitApplyTimedLife(u[6],'BHwe',3.0)\n"
        "elseif tick==11 then\ncall BJassAssert(GetWidgetLife(u[3])==0.0,\"reapply retains first deadline\")\n"
        "call BJassAssert(GetWidgetLife(u[6])==0.0,\"different classes retain first deadline\")\n"
        "call BJassAssert(GetWidgetLife(u[4])>0.0,\"paused lifetime stays alive\")\n"
        "elseif tick==15 then\ncall UnitPauseTimedLife(u[4],false)\n"
        "elseif tick==20 then\ncall BJassAssert(GetWidgetLife(u[4])>0.0,\"truncated repeat precedes expiry\")\n"
        "elseif tick==21 then\ncall BJassAssert(GetWidgetLife(u[4])==0.0,\"resumed scalar expiry\")\n"
        "call BJassAssert(GetUnitAbilityLevel(u[4],'BTLF')==0,\"death clears timed life\")\n"
        "call BJassAssert(IsUnitType(u[4],UNIT_TYPE_SUMMONED),\"summoned flag survives death\")\n"
        "call BJassAssert(GetWidgetLife(u[0])>0.0 and GetWidgetLife(u[1])>0.0,\"nonpositive lifetimes permanent\")\n"
        "call SetUnitUserData(u[0],21)\ncall PauseTimer(timer1)\nendif\nendfunction\n"
        "function main takes nothing returns nothing\nlocal integer i=0\n"
        "loop\nexitwhen i==7\nset u[i]=CreateUnit(Player(0),'hRTE',I2R(i)*128.0,0.0,0.0)\n"
        "set i=i+1\nendloop\n"
        "call UnitApplyTimedLife(u[0],'BTLF',0.0)\ncall UnitApplyTimedLife(u[1],'BTLF',-1.0)\n"
        "call UnitApplyTimedLife(u[2],'BTLF',0.01)\ncall UnitApplyTimedLife(u[3],'BTLF',1.0)\n"
        "call UnitApplyTimedLife(u[4],'BTLF',1.0)\ncall UnitApplyTimedLife(u[5],'BTLF',1.0)\n"
        "call UnitApplyTimedLife(u[6],'BUan',1.0)\nset timer1=CreateTimer()\n"
        "call TimerStart(timer1,0.1,true,function Sample)\nendfunction\n";
    T_ASSERT(run_test_jass(script));
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    while(level.time<2150){level.time+=5;globals.RunFrame();}
    T_ASSERT(!jass_rterror_pending(level.vm));
    if(jass_rterror_pending(level.vm))fprintf(stderr,"Timed-life callback: %s\n",jass_rterror_message(level.vm));
    edict_t *first=NULL;
    FILTER_EDICTS(unit,unit->inuse && unit->class_id==MAKEFOURCC('h','R','T','E')) {
        if(!first)first=unit;
    }
    T_NOT_NULL(first);if(first)T_EQ(first->user_data,21);
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}
#endif
