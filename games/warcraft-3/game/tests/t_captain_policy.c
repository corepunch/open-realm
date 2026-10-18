#ifdef BZ_TESTS
#include "../g_local.h"
#include "../skills/s_skills.h"
#include "jass/jass.h"
#include "shared/test.h"
extern void reset_entities(void),setup_test_world(void);
extern bool run_test_jass(cstring_t);
extern slkTestData_t *parse_slk_string(char const *),*G_SetSLKRows(char const *,slkTestData_t *);
extern void free_slk_rows(slkTestData_t *);

static void CaptainPolicyWorld(void) {
    G_BotStop(0);reset_entities();setup_test_world();
    level.time=level.pathing_msec=level.pathing_phase=0;
    level.pathing_due=level.pathing_owner_clock_valid=level.timer_clock_valid=false;
    static uint8_t cells[64*64];memset(cells,0,sizeof(cells));
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
    level.pathing_clock=(wc3Clock_t){0,0,300};
    ((mapInfo_t *)level.mapinfo)->players[0].used=true;
    game.clients[0].mapplayer=level.mapinfo->players;
    game.clients[0].jass.controller=1;G_BotInitPlayers();
    G_BotCreateCaptains(&game.clients[0].ps);
    G_BotSetCaptainHome(&game.clients[0].ps,1,256,1024);
}

TEST(wc3_bot, captain_empty_go_home_places_actor_and_retains_request) {
    CaptainPolicyWorld();
    botCaptain_t *captain=level.bots[0].captains;
    edict_t *actor=captain->home_actor;
    S_PlaceUnitPosition(actor,&(vec2_t){1600,1024});
    T_EQ(wc3_float_bits(actor->s.origin2.x),wc3_float_bits(1600));
    g_edicts[0].client=game.clients;
    T_ASSERT(run_test_jass(
        "globals\ninteger notices=0\nendglobals\n"
        "function observed takes nothing returns nothing\nset notices=notices+1\nendfunction\n"
        "function verify takes nothing returns nothing\ncall BJassAssert(notices==0,\"Captain actor must not issue public Stop\")\nendfunction\n"
        "function main takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)\n"
        "call TriggerAddAction(t,function observed)\nendfunction\n"));
    G_BotCaptainGoHome(&game.clients[0].ps);
    jass_callbyname(level.vm,"verify",false);T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(wc3_float_bits(actor->s.origin2.x),wc3_float_bits(256));
    T_EQ(wc3_float_bits(actor->s.origin2.y),wc3_float_bits(1024));
    T_NOT_NULL(actor->goalentity);
    T_EQ(actor->current_order_id,G_OrderId("move"));
    T_EQ(wc3_float_bits(actor->unitinfo.MoveSpeed),0x461c3c00u);
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(500));
    G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}

TEST(wc3_bot, captain_empty_home_change_places_then_publishes_request) {
    CaptainPolicyWorld();
    botCaptain_t *captain=level.bots[0].captains;
    G_BotSetCaptainHome(&game.clients[0].ps,1,1600,1024);
    T_EQ(wc3_float_bits(captain->home_actor->s.origin2.x),wc3_float_bits(1600));
    T_NOT_NULL(captain->home_actor->goalentity);
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(1600));
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(500));
    G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}

TEST(wc3_bot, captain_occupied_home_and_campaign_removals_use_counts_not_life) {
    CaptainPolicyWorld();
    player_t *player=&game.clients[0].ps;
    bot_t *bot=level.bots;botCaptain_t *captain=bot->captains;
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_CAMPAIGN));
    edict_t *units[7];
    FOR_LOOP(i,7)units[i]=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){256+96*(i%4),256+96*(i/4)},0);
    T_ASSERT(G_BotAddAssault(player,7,MAKEFOURCC('h','f','o','o')));
    bot->flags|=BOT_GROUPS_FLEE;
    /* Advance the real retained owner before changing its authored home. */
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    FOR_LOOP(i,240) {level.time+=5;globals.RunFrame();}
    edict_t *actor=captain->home_actor;
    vec2_t prior=actor->s.origin2;
    FOR_LOOP(i,7)units[i]->health.value=1;
    T_EQ(captain->strength_count,7);
    G_BotSetCaptainHome(player,1,1600,1024);
    T_EQ(wc3_float_bits(actor->s.origin2.x),wc3_float_bits(prior.x));
    T_ASSERT(!G_BotCaptainRetreating(player));
    FOR_LOOP(i,4) {
        G_FreeEdict(units[i]);
        T_ASSERT(!G_BotCaptainRetreating(player));
    }
    G_FreeEdict(units[4]);
    T_EQ(G_BotCaptainGroupSize(player),2);
    T_ASSERT(G_BotCaptainRetreating(player));
    T_NOT_NULL(actor->goalentity);
    if(actor->goalentity)T_EQ(wc3_float_bits(actor->goalentity->s.origin2.x),wc3_float_bits(1600));
    T_EQ(wc3_float_bits(actor->unitinfo.MoveSpeed),0x43fa0000u);
    wc3Clock_t due=captain->update_due;
    uint32_t flags=captain->policy_flags;
    cstring_t file=Test_TempPath("wc3-captain-policy159.bin");
    T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));
    T_EQ(captain->policy_flags,flags);T_EQ(captain->strength_count,2);
    T_EQ(wc3_float_bits(captain->update_due.time),wc3_float_bits(due.time));
    T_EQ(captain->update_due.epoch,due.epoch);
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(1600));
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(500));
    T_ASSERT(G_BotCaptainRetreating(player));
    G_BotCaptainAttack(player,&(vec2_t){256,1024});
    T_ASSERT(!G_BotCaptainRetreating(player));
    T_EQ(captain->state,BOT_CAPTAIN_ACTIVE);
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(200));
    remove(file);
    G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}

TEST(wc3_bot, captain_attack_native_admits_actor_request) {
    CaptainPolicyWorld();
    edict_t *actor=level.bots[0].captains[0].home_actor;
    T_ASSERT(G_BotStart(&game.clients[0].ps,"test_captain_attack.ai",BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(level.bots[0].vm);
    T_NOT_NULL(actor->goalentity);
    if(actor->goalentity)T_EQ(wc3_float_bits(actor->goalentity->s.origin2.x),wc3_float_bits(1600));
    T_EQ(actor->current_order_id,G_OrderId("move"));
    G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}
TEST(wc3_bot, captain_attack_arrival_returns_home_without_retreat) {
    CaptainPolicyWorld();
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    botCaptain_t *captain=level.bots[0].captains;
    G_BotCaptainAttack(&game.clients[0].ps,&(vec2_t){1600,1024});
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(200));
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(1600));
    FOR_LOOP(i,500) {level.time+=5;globals.RunFrame();}
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(256));
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(500));
    T_EQ(wc3_float_bits(captain->home_actor->s.origin2.x),wc3_float_bits(256));
    T_ASSERT(!G_BotCaptainRetreating(&game.clients[0].ps));
    T_EQ(captain->state,BOT_CAPTAIN_ACTIVE);
    G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}

/* A new Captain point request moves the virtual target, but an existing
 * member Move to that identity remains the same request (native9d87d0).
 * Upgrades become visible only when a genuinely new member request starts. */
TEST(wc3_bot, captain_repeated_point_request_retains_member_range_after_upgrade_and_save) {
    CaptainPolicyWorld();
    float range=400,acquire=1000,radius=32;uint32_t enabled=1;
    unitModification_t mods[]={
        {.modID=MAKEFOURCC('u','a','1','r'),.type=mod_unreal,.data=&range},
        {.modID=MAKEFOURCC('u','a','e','n'),.type=mod_int,.data=&enabled},
        {.modID=MAKEFOURCC('u','a','c','q'),.type=mod_unreal,.data=&acquire},
        {.modID=MAKEFOURCC('u','c','o','l'),.type=mod_unreal,.data=&radius},
        {.modID=MAKEFOURCC('u','p','g','r'),.type=mod_string,.data="Rhri"},
        {.modID=MAKEFOURCC('u','a','1','d'),.type=mod_int,.data=&enabled}};
    unitData_t type={.originalUnitID=MAKEFOURCC('h','R','T','E'),.newUnitID=MAKEFOURCC('h','C','P','3'),
        .numbeOfModifications=6,.modifications=mods};
    mapInfo_t const *old_info=level.mapinfo;mapInfo_t info=*old_info;
    info.num_userCreatedUnits=1;info.userCreatedUnits=&type;
    level.mapinfo=&info;G_SetMapUnitOverrides(&info);
    slkTestData_t *rows=parse_slk_string("ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"upgradeid\"\nC;X2;K\"maxlevel\"\nC;X3;K\"effect1\"\nC;X4;K\"base1\"\nC;X5;K\"mod1\"\n"
        "C;Y2;X1;K\"Rhri\"\nC;X2;K1\nC;X3;K\"ratr\"\nC;X4;K200\nC;X5;K0\nE\n");
    slkTestData_t *old_rows=G_SetSLKRows("UpgradeData",rows);
    memset(game.clients[0].tech,0,sizeof(game.clients[0].tech));
    player_t *player=&game.clients[0].ps;
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_CAMPAIGN));
    edict_t *units[13];
    FOR_LOOP(i,13)units[i]=unit_create(0,type.newUnitID,
        &(vec2_t){256+80*(i%4),192+80*(i/4)},0);
    T_ASSERT(G_BotAddAssault(player,13,type.newUnitID));
    botCaptain_t *captain=level.bots[0].captains;
    edict_t *actor=captain->home_actor;
    uint32_t ids[13];float ranges[13];wc3Clock_t due[13];
    FOR_LOOP(i,13) {
        ids[i]=units[i]->movement.group_id;due[i]=units[i]->movement.captain_home.due;
        ranges[i]=0;
        FOR_EACH_ARRAY(moveGroup_t *,group,level.move_groups)
            if((*group)->inuse && (*group)->id==ids[i]) ranges[i]=(*group)->members[0].arrival_range;
        T_EQ(wc3_float_bits(ranges[i]),0x412b0000u);
    }
    G_SetPlayerTechResearched(&game.clients[0],MAKEFOURCC('R','h','r','i'),1);
    T_EQ(wc3_float_bits(S_AttackProfileRead(units[0],0)->range),wc3_float_bits(600));
    G_BotCaptainAttack(player,&(vec2_t){256,1900});
    FOR_LOOP(i,13) {
        T_EQ(units[i]->movement.group_id,ids[i]);
        T_EQ(units[i]->movement.captain_home.actor,actor);
        T_EQ(wc3_float_bits(units[i]->movement.captain_home.due.time),wc3_float_bits(due[i].time));
    }
    cstring_t file=Test_TempPath("wc3-captain-reissue193.bin");
    T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));
    G_BotCaptainAttack(player,&(vec2_t){1600,1900});
    FOR_LOOP(i,13) {
        T_EQ(units[i]->movement.group_id,ids[i]);
        FOR_EACH_ARRAY(moveGroup_t *,group,level.move_groups)
            if((*group)->inuse && (*group)->id==units[i]->movement.group_id)
                T_EQ(wc3_float_bits((*group)->members[0].arrival_range),wc3_float_bits(ranges[i]));
    }
    /* A public Stop ends the physical request. The next Captain request must
     * reacquire the changed defaults rather than retain the ended snapshot. */
    T_ASSERT(unit_issueimmediateorder(units[0],"stop"));
    G_BotCaptainAttack(player,&(vec2_t){512,1900});
    T_ASSERT(units[0]->movement.group_id!=ids[0]);
    bool replaced=false;
    FOR_EACH_ARRAY(moveGroup_t *,group,level.move_groups)
        if((*group)->inuse && (*group)->id==units[0]->movement.group_id) {
            replaced=true;
            T_EQ(wc3_float_bits((*group)->members[0].arrival_range),0x41670000u);
        }
    T_ASSERT(replaced);
    remove(file);
    G_BotStop(0);level.started=false;reset_entities();setup_test_world();
    G_SetMapUnitOverrides(NULL);level.mapinfo=old_info;
    G_SetSLKRows("UpgradeData",old_rows);free_slk_rows(rows);
}

static float CaptainPolicyArrival(edict_t const *unit) {
    FOR_EACH_ARRAY(moveGroup_t *,group,level.move_groups)
        if((*group)->inuse && (*group)->id==unit->movement.group_id)
            FOR_LOOP(i,(*group)->count)if((*group)->members[i].unit==unit)
                return (*group)->members[i].arrival_range;
    return -1;
}

/* Follow a real outer-circle departure, change a producer during the private
 * walk, save it, then let shared re-admission and another departure execute. */
TEST(wc3_bot, captain_range_producers_retain_walk_and_refresh_on_natural_departure) {
    CaptainPolicyWorld();
    float range=400,acquire=1000,radius=32;uint32_t enabled=1;
    unitModification_t mods[]={
        {.modID=MAKEFOURCC('u','a','1','r'),.type=mod_unreal,.data=&range},
        {.modID=MAKEFOURCC('u','a','e','n'),.type=mod_int,.data=&enabled},
        {.modID=MAKEFOURCC('u','a','c','q'),.type=mod_unreal,.data=&acquire},
        {.modID=MAKEFOURCC('u','c','o','l'),.type=mod_unreal,.data=&radius},
        {.modID=MAKEFOURCC('u','p','g','r'),.type=mod_string,.data="Rhri"},
        {.modID=MAKEFOURCC('u','a','1','d'),.type=mod_int,.data=&enabled},
        {.modID=MAKEFOURCC('u','a','1','w'),.type=mod_string,.data="missile"}};
    unitData_t type={.originalUnitID=MAKEFOURCC('h','R','T','E'),.newUnitID=MAKEFOURCC('h','C','D','3'),
        .numbeOfModifications=7,.modifications=mods};
    mapInfo_t const *old_info=level.mapinfo;mapInfo_t info=*old_info;
    info.num_userCreatedUnits=1;info.userCreatedUnits=&type;
    level.mapinfo=&info;G_SetMapUnitOverrides(&info);
    slkTestData_t *rows=parse_slk_string("ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"upgradeid\"\nC;X2;K\"maxlevel\"\nC;X3;K\"effect1\"\nC;X4;K\"base1\"\nC;X5;K\"mod1\"\n"
        "C;Y2;X1;K\"Rhri\"\nC;X2;K1\nC;X3;K\"ratr\"\nC;X4;K200\nC;X5;K0\nE\n");
    slkTestData_t *old_rows=G_SetSLKRows("UpgradeData",rows);
    memset(game.clients[0].tech,0,sizeof(game.clients[0].tech));
    player_t *player=&game.clients[0].ps;
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_CAMPAIGN));
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    edict_t *units[13];
    FOR_LOOP(i,13)units[i]=unit_create(0,type.newUnitID,&(vec2_t){256+80*(i%4),192+80*(i/4)},0);
    T_ASSERT(G_BotAddAssault(player,13,type.newUnitID));
    botCaptain_t *captain=level.bots[0].captains;
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    uint32_t fresh[]={0x404c0000,0x40240000,0x41260000,0x41670000};
    cstring_t file=Test_TempPath("wc3-captain-departure193.bin");
    FOR_LOOP(kind,4) {
        /* Each member returns through the actual one-second listener. */
        uint32_t deadline=level.time+20000;
        while(level.time<deadline && (captain->entered_members!=13 || units[kind]->movement.captain_home.active)) {
            level.time+=5;globals.RunFrame();
        }
        T_EQ(captain->entered_members,13);T_ASSERT(!units[kind]->movement.captain_home.active);
        S_SetUnitPosition(units[kind],&(vec2_t){1800,128});
        deadline=level.time+1100;
        while(level.time<deadline && !units[kind]->movement.captain_home.active) {level.time+=5;globals.RunFrame();}
        T_ASSERT(units[kind]->movement.captain_home.active);
        T_EQ(wc3_float_bits(CaptainPolicyArrival(units[kind])),0x412b0000u);
        uint32_t id=units[kind]->movement.group_id;
        if(kind==0)S_AttackAdjustPrevention(units[kind],2,false);
        if(kind==1)S_ApplyTimedLife(units[kind],MAKEFOURCC('B','T','L','F'),600);
        if(kind==2)T_ASSERT(G_ActorRemoveSkill(units[kind],MAKEFOURCC('A','a','t','k')));
        if(kind==3)G_SetPlayerTechResearched(&game.clients[0],MAKEFOURCC('R','h','r','i'),1);
        T_EQ(units[kind]->movement.group_id,id);
        T_EQ(wc3_float_bits(CaptainPolicyArrival(units[kind])),0x412b0000u);
        T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));
        T_EQ(units[kind]->movement.group_id,id);
        T_EQ(wc3_float_bits(CaptainPolicyArrival(units[kind])),0x412b0000u);
        deadline=level.time+20000;
        while(level.time<deadline && (captain->entered_members!=13 || units[kind]->movement.captain_home.active)) {
            level.time+=5;globals.RunFrame();
        }
        T_EQ(captain->entered_members,13);T_ASSERT(!units[kind]->movement.captain_home.active);
        S_SetUnitPosition(units[kind],&(vec2_t){1800,128});
        deadline=level.time+1100;
        while(level.time<deadline && !units[kind]->movement.captain_home.active) {level.time+=5;globals.RunFrame();}
        T_ASSERT(units[kind]->movement.captain_home.active);
        T_EQ(wc3_float_bits(CaptainPolicyArrival(units[kind])),fresh[kind]);
        T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));
        T_EQ(wc3_float_bits(CaptainPolicyArrival(units[kind])),fresh[kind]);
    }
    remove(file);G_BotStop(0);level.started=false;reset_entities();setup_test_world();
    G_SetMapUnitOverrides(NULL);level.mapinfo=old_info;
    G_SetSLKRows("UpgradeData",old_rows);free_slk_rows(rows);
}

static void CaptainSpeedRoster(edict_t **units) {
    CaptainPolicyWorld();
    player_t *player=&game.clients[0].ps;
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_CAMPAIGN));
    FOR_LOOP(i,6) units[i]=unit_create(0,MAKEFOURCC('h','f','o','o'),
        &(vec2_t){256+80*(i%4),192-80*(i/4)},0);
    T_ASSERT(G_BotAddAssault(player,6,MAKEFOURCC('h','f','o','o')));
}

TEST(wc3_bot, captain_nonhome_request_reduces_speed_until_range_entry) {
    edict_t *units[6];CaptainSpeedRoster(units);
    player_t *player=&game.clients[0].ps;
    botCaptain_t *captain=level.bots[0].captains;
    G_BotCaptainAttack(player,&(vec2_t){256,64});
    /* Two complete public retail repeats: six 270-speed members, no inner
     * range entries, retained request different from the authored home. */
    T_EQ(wc3_float_bits(captain->home_actor->unitinfo.MoveSpeed),0x43592c85u);
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    bool entered=false;
    FOR_LOOP(i,600) {
        level.time+=5;globals.RunFrame();
        entered=true;
        FOR_LOOP(j,6) if(!units[j]->movement.captain_home.entered) entered=false;
        if(entered) break;
    }
    T_ASSERT(entered);
    T_EQ(wc3_float_bits(captain->home_actor->unitinfo.MoveSpeed),0x43870000u);
    G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}

TEST(wc3_bot, captain_nonhome_speed_uses_live_minimum_and_retained_save_state) {
    edict_t *units[6];CaptainSpeedRoster(units);
    botCaptain_t *captain=level.bots[0].captains;
    S_SetUnitMoveSpeed(units[0],180);
    G_BotCaptainAttack(&game.clients[0].ps,&(vec2_t){256,64});
    T_EQ(wc3_float_bits(captain->home_actor->unitinfo.MoveSpeed),0x4310c858u);
    cstring_t file=Test_TempPath("wc3-captain-speed160.bin");
    T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));
    T_EQ(wc3_float_bits(captain->home_actor->unitinfo.MoveSpeed),0x4310c858u);
    S_CaptainPointMove(captain,&captain->goal,200);
    T_EQ(wc3_float_bits(captain->home_actor->unitinfo.MoveSpeed),0x4310c858u);
    G_BotCaptainAttack(&game.clients[0].ps,&captain->home);
    T_EQ(wc3_float_bits(captain->home_actor->unitinfo.MoveSpeed),wc3_float_bits(180));
    remove(file);
    G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}

TEST(wc3_bot, captain_cargo_drop_roster_keeps_unreduced_speed) {
    edict_t *units[6];CaptainSpeedRoster(units);
    slkTestData_t *rows=parse_slk_string("ID;PWXL;N;E\nB;X3;Y2\nC;X1;Y1;K\"ID\"\nC;X2;K\"code\"\nC;X3;K\"levels\"\nC;X1;Y2;K\"Adro\"\nC;X2;K\"Adro\"\nC;X3;K1\nE\n");
    slkTestData_t *old=G_SetSLKRows("AbilityData",rows);
    FOR_LOOP(i,6) T_ASSERT(G_ActorAddSkill(units[i],MAKEFOURCC('A','d','r','o')));
    G_BotCaptainAttack(&game.clients[0].ps,&(vec2_t){256,64});
    T_EQ(wc3_float_bits(level.bots[0].captains[0].home_actor->unitinfo.MoveSpeed),0x43870000u);
    T_ASSERT(G_ActorRemoveSkill(units[0],MAKEFOURCC('A','d','r','o')));
    G_BotCaptainAttack(&game.clients[0].ps,&(vec2_t){256,64});
    T_EQ(wc3_float_bits(level.bots[0].captains[0].home_actor->unitinfo.MoveSpeed),0x43592c85u);
    G_BotStop(0);level.started=false;reset_entities();setup_test_world();
    G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
}

TEST(wc3_bot, captain_speed_ignores_removed_move_owner) {
    edict_t *units[6];CaptainSpeedRoster(units);
    S_SetUnitMoveSpeed(units[0],180);
    T_ASSERT(G_ActorRemoveSkill(units[0],MAKEFOURCC('A','m','o','v')));
    G_BotCaptainAttack(&game.clients[0].ps,&(vec2_t){256,64});
    T_EQ(wc3_float_bits(level.bots[0].captains[0].home_actor->unitinfo.MoveSpeed),0x43592c85u);
    T_ASSERT(G_ActorAddSkill(units[0],MAKEFOURCC('A','m','o','v')));
    G_BotCaptainAttack(&game.clients[0].ps,&(vec2_t){256,64});
    T_EQ(wc3_float_bits(level.bots[0].captains[0].home_actor->unitinfo.MoveSpeed),0x4310c858u);
    G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}

TEST(wc3_bot, captain_large_roster_preserves_admission_batches_and_cold_save) {
    uint32_t sizes[]={24,25};
    FOR_LOOP(case_index,sizeof(sizes)/sizeof(*sizes)) {
        uint32_t count=sizes[case_index];
        CaptainPolicyWorld();
        player_t *player=&game.clients[0].ps;
        botCaptain_t *captain=level.bots[0].captains;
        T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_CAMPAIGN));
        FOR_LOOP(i,count) unit_create(0,MAKEFOURCC('h','f','o','o'),
            &(vec2_t){128+96*(i%8),128+96*(i/8)},0);
        T_ASSERT(G_BotAddAssault(player,count,MAKEFOURCC('h','f','o','o')));
        T_EQ(ARRAY_COUNT(captain->units),count);
        bool admitted=true;
        FOR_LOOP(i,count) {
            edict_t *unit=captain->units[i];
            T_EQ(unit->movement.captain_home.roster_actor,captain->home_actor);
            T_EQ(unit->movement.captain_home.member_index,i);
            if(unit->movement.captain_home.roster_actor!=captain->home_actor) admitted=false;
        }
        /* The old thirteen-member fallback loses registration. Stop the red
         * run here instead of driving its inconsistent range-listener state. */
        if(!admitted) goto done;
        G_BotCaptainAttack(player,&(vec2_t){256,64});
        /* Completed retail repeats: the twenty-fifth member takes the factor
         * above one; there is no post-multiplier clamp back to the minimum. */
        T_EQ(wc3_float_bits(captain->home_actor->unitinfo.MoveSpeed),count==24 ? 0x43870000u : 0x438877a6u);
        cstring_t file=Test_TempPath("wc3-captain-large161.bin");
        T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));remove(file);
        T_EQ(ARRAY_COUNT(captain->units),count);
        T_ASSERT(S_ValidateCaptainHomeActors(false));
        T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
        level.started=level.scriptsConfigured=level.scriptsStarted=true;
        bool shared=false;
        FOR_LOOP(frame,600) {
            level.time+=5;globals.RunFrame();
            shared=true;
            FOR_LOOP(i,count) if(captain->units[i]->movement.captain_home.active) shared=false;
            if(shared)break;
        }
        T_ASSERT(shared);
        T_EQ(captain->entered_members,count);
        uint64_t shared_id=0;
        uint32_t batches=0,admitted_count=0;
        FOR_EACH_ARRAY(moveGroup_t *,item,level.move_groups) {
            moveGroup_t *group=*item;
            if(!group->inuse || !group->shared_id || group->target)continue;
            if(!shared_id)shared_id=group->shared_id;
            T_EQ(group->shared_id,shared_id);
            T_EQ(group->count,MIN(count-admitted_count,BZ_WC3_GROUP_ORDER_UNITS));
            admitted_count+=group->count;batches++;
        }
        T_EQ(batches,(count+BZ_WC3_GROUP_ORDER_UNITS-1)/BZ_WC3_GROUP_ORDER_UNITS);
        T_EQ(admitted_count,count);
        T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));remove(file);
        T_ASSERT(S_ValidateCaptainHomeActors(false));
        T_EQ(captain->entered_members,count);
        G_FreeEdict(captain->units[count/2]);
        T_EQ(ARRAY_COUNT(captain->units),count-1);
        T_EQ(captain->entered_members,count-1);
        T_ASSERT(S_ValidateCaptainHomeActors(false));
        G_BotCaptainAttack(player,&(vec2_t){1536,1024});
        FOR_LOOP(i,count-1)T_EQ(captain->units[i]->movement.captain_home.roster_actor,captain->home_actor);
done:
        G_BotStop(0);level.started=false;reset_entities();setup_test_world();
    }
}
/* Public member damage reaches the Captain before ordinary retaliation.
 * A mana-only melee roster has c0==0; combat contact sends its moving
 * virtual actor home without setting the removal-driven retreat bit. */
TEST(wc3_bot, captain358_mana_roster_combat_contact_goes_home_and_restores) {
    CaptainPolicyWorld();
    player_t *player=&game.clients[0].ps;
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_MELEE));
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    edict_t *units[6];
    FOR_LOOP(i,6) {
        units[i]=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){256+80*(i%4),192+80*(i/4)},0);
        units[i]->mana.value=100;units[i]->mana.max_value=200;
    }
    T_ASSERT(G_BotAddAssault(player,6,MAKEFOURCC('h','f','o','o')));
    botCaptain_t *captain=level.bots[0].captains;
    level.bots[0].flags|=BOT_GROUPS_FLEE;
    T_EQ(captain->strength_count,0);
    G_BotCaptainAttack(player,&(vec2_t){1900,1024});
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    uint32_t limit=level.time+10000;
    while(level.time<limit && S_CaptainNearHome(captain)) {level.time+=5;globals.RunFrame();}
    T_ASSERT(!S_CaptainNearHome(captain));
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(1900));
    T_ASSERT(captain->policy_flags&BOT_CAPTAIN_STRENGTH_READY);
    edict_t *source=unit_create(1,MAKEFOURCC('h','f','o','o'),&(vec2_t){1800,1800},0);
    cstring_t file=Test_TempPath("wc3-captain358-contact.bin");
    T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));
    T_EQ(captain->strength_count,0);
    T_Damage(units[0],source,1);
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(captain->home.x));
    T_EQ(wc3_float_bits(captain->goal.y),wc3_float_bits(captain->home.y));
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(500));
    T_ASSERT(!G_BotCaptainRetreating(player));
    T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(500));
    T_EQ(captain->strength_count,0);
    T_ASSERT(!G_BotCaptainRetreating(player));
    G_BotCaptainAttack(player,&(vec2_t){1600,1024});
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(200));
    /* An already removed contact cannot replace that new live actor request. */
    G_FreeEdict(source);
    G_BotCaptainCombatContact(units[1],source);
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(1600));
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(200));
    remove(file);G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}
/*9d0650 applies the predicate again at removal. A mana mutation can
 * therefore leave c0 negative; it is retained policy state, not a recount. */
TEST(wc3_bot, captain_contact_preserves_negative_mana_counter_through_save_and_readmission) {
    CaptainPolicyWorld();
    player_t *player=&game.clients[0].ps;
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_MELEE));
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    edict_t *units[6];
    FOR_LOOP(i,6) {
        units[i]=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){256+80*(i%4),192+80*(i/4)},0);
        units[i]->mana.value=100;units[i]->mana.max_value=200;
    }
    T_ASSERT(G_BotAddAssault(player,6,MAKEFOURCC('h','f','o','o')));
    botCaptain_t *captain=level.bots[0].captains;
    T_EQ(captain->strength_count,0);
    G_BotCaptainAttack(player,&(vec2_t){1900,1024});
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    uint32_t limit=level.time+10000;
    while(level.time<limit && S_CaptainNearHome(captain)) {level.time+=5;globals.RunFrame();}
    T_ASSERT(!S_CaptainNearHome(captain));
    units[0]->mana.value=0;
    G_FreeEdict(units[0]);
    T_EQ(ARRAY_COUNT(captain->units),5);
    T_EQ(captain->strength_count,-1);
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(1900));
    T_ASSERT(!G_BotCaptainRetreating(player));
    cstring_t file=Test_TempPath("wc3-captain-negative-mana.bin");
    T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));
    T_EQ(captain->strength_count,-1);
    level.bots[0].flags|=BOT_GROUPS_FLEE;
    edict_t *source=unit_create(1,MAKEFOURCC('h','f','o','o'),&(vec2_t){1800,1800},0);
    T_Damage(units[1],source,1);
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(captain->home.x));
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(500));
    T_ASSERT(!G_BotCaptainRetreating(player));
    T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));
    T_EQ(captain->strength_count,-1);
    edict_t *replacement=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){128,128},0);
    replacement->mana.value=0;
    T_ASSERT(G_BotAddAssault(player,6,MAKEFOURCC('h','f','o','o')));
    T_EQ(captain->strength_count,0);
    T_EQ(ARRAY_COUNT(captain->units),6);
    remove(file);G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}
/* Fresh Attack acquisition is independent of damage. A moving AI point
 * member must interrupt its physical request and notify the logical Captain. */
TEST(wc3_bot, captain_ai_point_acquisition_returns_home_before_any_damage) {
    CaptainPolicyWorld();
    player_t *player=&game.clients[0].ps;
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_MELEE));
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    float range=600,acquire=1000,radius=32;uint32_t enabled=1;
    unitModification_t mods[]={
        {.modID=MAKEFOURCC('u','a','1','r'),.type=mod_unreal,.data=&range},
        {.modID=MAKEFOURCC('u','a','e','n'),.type=mod_int,.data=&enabled},
        {.modID=MAKEFOURCC('u','a','c','q'),.type=mod_unreal,.data=&acquire},
        {.modID=MAKEFOURCC('u','c','o','l'),.type=mod_unreal,.data=&radius}};
    unitData_t type={.originalUnitID=MAKEFOURCC('h','R','T','E'),.newUnitID=MAKEFOURCC('h','C','A','5'),
        .numbeOfModifications=4,.modifications=mods};
    mapInfo_t const *old_info=level.mapinfo;mapInfo_t info=*old_info;
    info.num_userCreatedUnits=1;info.userCreatedUnits=&type;
    level.mapinfo=&info;G_SetMapUnitOverrides(&info);
    edict_t *units[6];
    FOR_LOOP(i,6) {
        units[i]=unit_create(0,type.newUnitID,&(vec2_t){256+80*(i%4),192+80*(i/4)},0);
        units[i]->mana.value=100;units[i]->mana.max_value=200;
        /* A non-stock windup keeps damage/help from masking the independent
         * acquisition producer during the observation window. */
        units[i]->runtime.acquisition_range=1000;
        units[i]->targtype=TARG_GROUND;
        unitAttack_t *profile=S_AttackProfileWrite(units[i],0);
        profile->damagePoint=3.25f;profile->cooldown=1.5f;
        profile->damageBase=17;profile->type=ATK_NORMAL;
        profile->range=600;profile->targetsAllowed=WC3_TARGET_FLAG_GROUND;
        profile->weapon=WPN_NORMAL;
        gi.LinkEntity(units[i]);
    }
    T_ASSERT(G_BotAddAssault(player,6,type.newUnitID));
    botCaptain_t *captain=level.bots[0].captains;
    level.bots[0].flags|=BOT_GROUPS_FLEE;
    G_BotCaptainAttack(player,&(vec2_t){1900,1024});
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    uint32_t limit=level.time+10000;
    while(level.time<limit && S_CaptainNearHome(captain)) {level.time+=5;globals.RunFrame();}
    T_ASSERT(!S_CaptainNearHome(captain));
    T_EQ(captain->strength_count,0);
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(200));
    T_EQ(units[0]->current_order_id,851988u);
    G_SetPlayerAlliance(player,&game.clients[1].ps,ALLIANCE_PASSIVE,false);
    G_SetPlayerAlliance(&game.clients[1].ps,player,ALLIANCE_PASSIVE,false);
    vec2_t point={units[0]->s.origin2.x+160,units[0]->s.origin2.y};
    edict_t *source=unit_create(1,MAKEFOURCC('h','f','o','o'),&point,0);
    S_SetUnitPaused(source,true);source->targtype=TARG_GROUND;gi.LinkEntity(source);
    T_ASSERT(S_AttackCanTarget(units[0],source));
    T_EQ(G_FindNearestEnemy(units[0],G_AcquisitionRange(units[0])),source);
    float health=source->health.value;
    FOR_LOOP(i,6)S_SetUnitPaused(units[i],true);
    limit=level.time+700;
    while(level.time<limit) {level.time+=5;globals.RunFrame();}
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(200));
    FOR_LOOP(i,6) {
        S_AcquireBuffAbilityDisables(units[i],false);
        S_SetUnitPaused(units[i],false);
    }
    T_ASSERT(S_AbilityExecutionDisabled(units[0],MAKEFOURCC('A','a','t','k')));
    limit=level.time+500;
    while(level.time<limit) {level.time+=5;globals.RunFrame();}
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(200));
    T_ASSERT(units[0]->attack_scan.active);
    wc3Clock_t deadline=units[0]->attack_scan.deadline;
    uint32_t sequence=units[0]->attack_scan.sequence;
    cstring_t file=Test_TempPath("wc3-captain-independent-acquire.bin");
    T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));
    T_ASSERT(S_AbilityExecutionDisabled(units[0],MAKEFOURCC('A','a','t','k')));
    T_ASSERT(units[0]->attack_scan.active);
    T_EQ(units[0]->attack_scan.sequence,sequence);
    T_EQ(wc3_float_bits(units[0]->attack_scan.deadline.time),wc3_float_bits(deadline.time));
    T_EQ(units[0]->attack_scan.deadline.epoch,deadline.epoch);
    FOR_LOOP(i,6)S_ReleaseBuffAbilityDisables(units[i],false);
    T_ASSERT(!S_AbilityExecutionDisabled(units[0],MAKEFOURCC('A','a','t','k')));
    limit=level.time+1200;
    while(level.time<limit && captain->request_range!=500) {level.time+=5;globals.RunFrame();}
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(captain->home.x));
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(500));
    T_ASSERT(!G_BotCaptainRetreating(player));
    bool began=false;FOR_LOOP(i,6)began|=units[i]->attack_speed_cap.active;
    T_ASSERT(began); /* Actor arrival alone cannot satisfy the acquisition producer. */
    /* Native1557 publishes Attack before Captain home500 reissues members.
     * The timer returns with no Attack target; later ally alerts are separate. */
    T_FEQ(source->health.value,health,0.0001f);
    remove(file);G_BotStop(0);level.started=false;
    G_SetMapUnitOverrides(NULL);level.mapinfo=old_info;reset_entities();setup_test_world();
}
/* The source engagement continuation has its own target and active route;
 * a policy0 refresh must not replace the CaptainAttack retained point. */
TEST(wc3_bot, captain_near_home_hostile_contact_retains_point_and_pursues_source) {
    CaptainPolicyWorld();
    player_t *player=&game.clients[0].ps;
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_MELEE));
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    UnitWeapons_t weapons={.attacksEnabled=1};
    edict_t *units[6];
    FOR_LOOP(i,6) {
        units[i]=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){256+80*(i%4),192+80*(i/4)},0);
        units[i]->mana.value=100;units[i]->mana.max_value=200;
        units[i]->data.UnitWeapons=&weapons;
        units[i]->runtime.acquisition_range=1000;units[i]->targtype=TARG_GROUND;
        unitAttack_t *profile=S_AttackProfileWrite(units[i],0);
        profile->type=ATK_NORMAL;profile->damageBase=17;
        profile->cooldown=1.5f;profile->damagePoint=3.25f;
        profile->range=600;profile->weapon=WPN_NORMAL;
        profile->targetsAllowed=WC3_TARGET_FLAG_GROUND;
        gi.LinkEntity(units[i]);
    }
    T_ASSERT(G_BotAddAssault(player,6,MAKEFOURCC('h','f','o','o')));
    botCaptain_t *captain=level.bots[0].captains;
    G_BotCaptainAttack(player,&captain->home);
    T_ASSERT(S_CaptainNearHome(captain));
    G_SetPlayerAlliance(player,&game.clients[1].ps,ALLIANCE_PASSIVE,false);
    G_SetPlayerAlliance(&game.clients[1].ps,player,ALLIANCE_PASSIVE,false);
    edict_t *source=unit_create(1,MAKEFOURCC('h','f','o','o'),&(vec2_t){1500,1024},0);
    source->targtype=TARG_GROUND;S_SetUnitPaused(source,true);gi.LinkEntity(source);
    T_ASSERT(S_AttackCanTarget(units[0],source));
    T_Damage(units[0],source,1);
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(captain->home.x));
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(200));
    T_ASSERT(!G_BotCaptainRetreating(player));
    T_NOT_NULL(captain->home_actor->goalentity);
    if(captain->home_actor->goalentity) {
        T_EQ(wc3_float_bits(captain->home_actor->goalentity->s.origin2.x),wc3_float_bits(source->s.origin2.x));
        T_EQ(wc3_float_bits(captain->home_actor->goalentity->s.origin2.y),wc3_float_bits(source->s.origin2.y));
    }
    T_EQ(wc3_float_bits(captain->home_actor->s.origin2.x),wc3_float_bits(captain->home.x));
    cstring_t file=Test_TempPath("wc3-captain-source-owner.bin");
    T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(captain->home.x));
    T_NOT_NULL(captain->home_actor->goalentity);
    if(captain->home_actor->goalentity)
        T_EQ(wc3_float_bits(captain->home_actor->goalentity->s.origin2.x),wc3_float_bits(source->s.origin2.x));
    T_EQ(captain->follow.unit,source);
    T_EQ(captain->follow.birth,source->spawn_time);
    G_FreeEdict(source);
    T_NULL(captain->follow.unit);
    T_NULL(captain->backup.unit);
    T_EQ(ARRAY_COUNT(captain->units),6u);
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(captain->home.x));
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(200));
    T_NOT_NULL(captain->home_actor->goalentity);
    if(captain->home_actor->goalentity)
        T_EQ(wc3_float_bits(captain->home_actor->goalentity->s.origin2.x),wc3_float_bits(captain->home.x));
    T_ASSERT(captain->policy_flags&BOT_CAPTAIN_REQUEST_RESTORED);
    T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));
    T_NULL(captain->follow.unit);
    T_ASSERT(captain->policy_flags&BOT_CAPTAIN_REQUEST_RESTORED);
    T_EQ(wc3_float_bits(captain->active_goal.x),wc3_float_bits(captain->home.x));
    remove(file);G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}
/* Native ClearCaptainTargets is a semantic identity removal, not a home order.
 * With no identities, two public observations retain every point/request word. */
TEST(wc3_bot, captain_clear368_without_targets_retains_nonhome_request) {
    CaptainPolicyWorld();
    player_t *player=&game.clients[0].ps;
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_MELEE));
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    edict_t *members[6];
    FOR_LOOP(i,6) {
        members[i]=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){256+80*(i%4),256+80*(i/4)},0);
        members[i]->mana.value=100;members[i]->mana.max_value=200;
    }
    T_ASSERT(G_BotAddAssault(player,6,MAKEFOURCC('h','f','o','o')));
    botCaptain_t *captain=level.bots[0].captains;
    vec2_t point={1800,700};G_BotCaptainAttack(player,&point);
    T_NULL(captain->target.unit);T_NULL(captain->follow.unit);T_NULL(captain->backup.unit);
    T_EQ(captain->state,BOT_CAPTAIN_ACTIVE);
    edict_t *actor=captain->home_actor,*goal=actor->goalentity;
    umove_t const *move=actor->currentmove;
    uint32_t flags=captain->policy_flags,order=actor->current_order_id;
    G_BotClearCaptainTargets(player);
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(point.x));
    T_EQ(wc3_float_bits(captain->goal.y),wc3_float_bits(point.y));
    T_EQ(wc3_float_bits(captain->active_goal.x),wc3_float_bits(point.x));
    T_EQ(wc3_float_bits(captain->active_goal.y),wc3_float_bits(point.y));
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(200));
    T_EQ(captain->state,BOT_CAPTAIN_ACTIVE);T_EQ(captain->policy_flags,flags);
    T_EQ(actor->goalentity,goal);T_EQ(actor->currentmove,move);T_EQ(actor->current_order_id,order);
    T_EQ(ARRAY_COUNT(captain->units),6u);
    G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}

/* Public Clear removes a living source semantically and restores the retained
 * request. It must not broadcast actual source retirement to unrelated units. */
TEST(wc3_bot, captain_clear368_follow_inverse_and_saved_point) {
    CaptainPolicyWorld();
    player_t *player=&game.clients[0].ps;
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_MELEE));
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    UnitWeapons_t weapons={.attacksEnabled=1};
    edict_t *units[6];
    FOR_LOOP(i,6) {
        units[i]=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){256+80*(i%4),192+80*(i/4)},0);
        units[i]->mana.value=100;units[i]->mana.max_value=200;
        units[i]->data.UnitWeapons=&weapons;
        units[i]->runtime.acquisition_range=1000;units[i]->targtype=TARG_GROUND;
        unitAttack_t *profile=S_AttackProfileWrite(units[i],0);
        profile->type=ATK_NORMAL;profile->damageBase=17;
        profile->cooldown=1.5f;profile->damagePoint=3.25f;
        profile->range=600;profile->weapon=WPN_NORMAL;
        profile->targetsAllowed=WC3_TARGET_FLAG_GROUND;
        gi.LinkEntity(units[i]);
    }
    T_ASSERT(G_BotAddAssault(player,6,MAKEFOURCC('h','f','o','o')));
    botCaptain_t *captain=level.bots[0].captains;
    G_BotCaptainAttack(player,&captain->home);
    T_ASSERT(S_CaptainNearHome(captain));
    G_SetPlayerAlliance(player,&game.clients[1].ps,ALLIANCE_PASSIVE,false);
    G_SetPlayerAlliance(&game.clients[1].ps,player,ALLIANCE_PASSIVE,false);
    edict_t *source=unit_create(1,MAKEFOURCC('h','f','o','o'),&(vec2_t){1500,1024},0);
    source->targtype=TARG_GROUND;S_SetUnitPaused(source,true);gi.LinkEntity(source);
    T_ASSERT(S_AttackCanTarget(units[0],source));
    T_Damage(units[0],source,1);
    T_EQ(captain->follow.unit,source);
    T_EQ(captain->follow.birth,source->spawn_time);
    edict_t *outsider=unit_create(2,MAKEFOURCC('h','f','o','o'),&(vec2_t){1800,1024},0);
    outsider->data.UnitWeapons=&weapons;outsider->targtype=TARG_GROUND;
    *S_AttackProfileWrite(outsider,0)=*S_AttackProfileRead(units[0],0);
    T_ASSERT(unit_issuetargetorder(outsider,"attack",source));
    T_EQ(outsider->attack_target,source);
    vec2_t retained=captain->goal;
    float range=captain->request_range,life=source->health.value;
    G_BotClearCaptainTargets(player);
    T_NULL(captain->follow.unit);T_NULL(captain->backup.unit);T_NULL(captain->target.unit);
    T_EQ(ARRAY_COUNT(captain->units),6u);
    T_ASSERT(source->inuse);T_EQ(wc3_float_bits(source->health.value),wc3_float_bits(life));
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(retained.x));
    T_EQ(wc3_float_bits(captain->goal.y),wc3_float_bits(retained.y));
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(range));
    T_EQ(wc3_float_bits(captain->active_goal.x),wc3_float_bits(retained.x));
    T_EQ(wc3_float_bits(captain->active_goal.y),wc3_float_bits(retained.y));
    T_ASSERT(captain->policy_flags&BOT_CAPTAIN_REQUEST_RESTORED);
    T_ASSERT(!(captain->policy_flags&BOT_CAPTAIN_RETREAT_FLAG));
    T_ASSERT(units[0]->attack_target!=source);
    T_EQ(outsider->attack_target,source);
    cstring_t file=Test_TempPath("wc3-clear368-source.bin");
    T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));
    T_NULL(captain->follow.unit);T_NULL(captain->backup.unit);T_NULL(captain->target.unit);
    T_ASSERT(captain->policy_flags&BOT_CAPTAIN_REQUEST_RESTORED);
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(retained.x));
    T_EQ(wc3_float_bits(captain->active_goal.y),wc3_float_bits(retained.y));
    remove(file);G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}

/* Native1357 Begin retains the current point and waits for d01c2. Clear in
 * the same callback cancels only this selection owner, leaving periodic work. */
TEST(wc3_bot, captain_selection369_immediate_clear_preserves_pending_point) {
    CaptainPolicyWorld();
    FOR_LOOP(p,2) {
        ((mapInfo_t *)level.mapinfo)->players[p].used=true;
        ((mapInfo_t *)level.mapinfo)->players[p].playerType=kPlayerTypeHuman;
        game.clients[p].mapplayer=level.mapinfo->players+p;
    }
    player_t *player=&game.clients[0].ps;
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_CAMPAIGN));
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    edict_t *members[6];
    FOR_LOOP(i,6) {
        members[i]=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){256+80*(i%4),256+80*(i/4)},0);
        members[i]->mana.value=100;members[i]->mana.max_value=200;
    }
    T_ASSERT(G_BotAddAssault(player,6,MAKEFOURCC('h','f','o','o')));
    botCaptain_t *captain=level.bots[0].captains;
    edict_t *target=unit_create(1,MAKEFOURCC('h','f','o','o'),&(vec2_t){1600,1024},0);
    target->targtype=TARG_GROUND;S_SetUnitPaused(target,true);gi.LinkEntity(target);
    uint32_t flags=player->rdflags;player->rdflags|=RDF_NOFOG;
    T_ASSERT(target->inuse && target->health.value>0);
    T_ASSERT(G_FowPlayerCanHoverEntity(0,target));
    vec2_t point={256,1600};G_BotCaptainAttack(player,&point);
    wc3Clock_t periodic=captain->update_due;
    G_BotCaptainVsPlayer(player,&game.clients[1].ps);
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(point.x));
    T_EQ(wc3_float_bits(captain->goal.y),wc3_float_bits(point.y));
    T_EQ(captain->state,BOT_CAPTAIN_ACTIVE);T_NULL(captain->target.unit);
    FOR_LOOP(i,6)T_ASSERT(members[i]->attack_target!=target);
    G_BotClearCaptainTargets(player);
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    FOR_LOOP(i,24) {level.time+=5;globals.RunFrame();}
    T_NULL(captain->target.unit);T_EQ(captain->state,BOT_CAPTAIN_ACTIVE);
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(point.x));
    T_EQ(wc3_float_bits(captain->goal.y),wc3_float_bits(point.y));
    T_EQ(wc3_float_bits(captain->update_due.time),wc3_float_bits(periodic.time));
    T_EQ(captain->update_due.epoch,periodic.epoch);
    player->rdflags=flags;G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}

/* Reachable native bucket2 becomes primary/state3 only after the independently
 * scheduled .1-second owner fires; publication is a Captain point request. */
TEST(wc3_bot, captain_selection369_reachable_target_is_deferred_primary) {
    CaptainPolicyWorld();
    FOR_LOOP(p,2) {
        ((mapInfo_t *)level.mapinfo)->players[p].used=true;
        ((mapInfo_t *)level.mapinfo)->players[p].playerType=kPlayerTypeHuman;
        game.clients[p].mapplayer=level.mapinfo->players+p;
    }
    player_t *player=&game.clients[0].ps;
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_CAMPAIGN));
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    edict_t *members[6];
    FOR_LOOP(i,6) {
        members[i]=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){256+80*(i%4),256+80*(i/4)},0);
        members[i]->mana.value=100;members[i]->mana.max_value=200;
    }
    T_ASSERT(G_BotAddAssault(player,6,MAKEFOURCC('h','f','o','o')));
    botCaptain_t *captain=level.bots[0].captains;
    edict_t *target=unit_create(1,MAKEFOURCC('h','f','o','o'),&(vec2_t){1600,1024},0);
    target->targtype=TARG_GROUND;S_SetUnitPaused(target,true);gi.LinkEntity(target);
    uint32_t flags=player->rdflags;player->rdflags|=RDF_NOFOG;
    T_ASSERT(target->inuse && target->health.value>0);
    T_ASSERT(G_FowPlayerCanHoverEntity(0,target));
    vec2_t point={256,1600};G_BotCaptainAttack(player,&point);
    G_BotCaptainVsPlayer(player,&game.clients[1].ps);
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(point.x));T_NULL(captain->target.unit);
    T_ASSERT(captain->selection.request.active);
    T_EQ(ARRAY_COUNT(captain->selection.pending),1);
    T_EQ(captain->selection.pending[0].unit,target);
    abilityPrimaryTimer_t pending=captain->selection.request;
    uint64_t class_order=target->class_seq;
    G_SetUnitPlayer(target,2);G_SetUnitPlayer(target,1);
    T_EQ(target->class_seq,class_order);T_ASSERT(target->own_seq>class_order);
    cstring_t file=Test_TempPath("wc3-captain-selection369.bin");
    T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));
    T_ASSERT(captain->selection.request.active);
    T_EQ(captain->selection.request.sequence,pending.sequence);
    T_EQ(wc3_float_bits(captain->selection.request.deadline.time),wc3_float_bits(pending.deadline.time));
    T_EQ(captain->selection.request.deadline.epoch,pending.deadline.epoch);
    T_EQ(ARRAY_COUNT(captain->selection.pending),1);
    T_EQ(captain->selection.pending[0].unit,target);T_EQ(target->class_seq,class_order);
    remove(file);
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    FOR_LOOP(i,24) {level.time+=5;globals.RunFrame();}
    T_EQ(captain->target.unit,target);T_EQ(captain->target.birth,target->spawn_time);
    /* Native state3 represents a selected primary; retreat remains flags.bit2. */
    T_EQ(captain->state,(botCaptainState_t)3);T_ASSERT(!G_BotCaptainRetreating(player));
    T_ASSERT(captain->policy_flags&4);
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(target->s.origin2.x));
    T_EQ(wc3_float_bits(captain->goal.y),wc3_float_bits(target->s.origin2.y));
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(200));
    FOR_LOOP(i,6)T_ASSERT(members[i]->attack_target!=target);
    player->rdflags=flags;G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}


/* Repeated307-marker native RemoveUnit/control: primary inverse rearms at1424,
 * retains selected state3, then the empty query expires independently at1427. */
TEST(wc3_bot, captain_selection369_selected_primary_loss_rearms_query) {
    CaptainPolicyWorld();
    FOR_LOOP(p,2) {((mapInfo_t *)level.mapinfo)->players[p].used=true;
        ((mapInfo_t *)level.mapinfo)->players[p].playerType=kPlayerTypeHuman;
        game.clients[p].mapplayer=level.mapinfo->players+p;}
    player_t *player=&game.clients[0].ps;
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_CAMPAIGN));
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    FOR_LOOP(i,6) {edict_t *u=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){256+80*(i%4),256+80*(i/4)},0);u->mana.value=100;u->mana.max_value=200;}
    T_ASSERT(G_BotAddAssault(player,6,MAKEFOURCC('h','f','o','o')));
    botCaptain_t *captain=level.bots[0].captains;
    edict_t *target=unit_create(1,MAKEFOURCC('h','f','o','o'),&(vec2_t){1600,1024},0);
    S_SetUnitPaused(target,true);gi.LinkEntity(target);
    G_BotCaptainAttack(player,&(vec2_t){256,1600});
    G_BotCaptainVsPlayer(player,&game.clients[1].ps);
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    FOR_LOOP(i,24){level.time+=5;globals.RunFrame();}
    T_EQ(captain->target.unit,target);T_EQ(captain->state,BOT_CAPTAIN_TARGETED);
    T_ASSERT(!captain->selection.request.active);
    uint32_t previous=captain->selection.request.sequence;
    wc3Clock_t periodic=captain->update_due,now=G_TimerQueryClock(NULL);
    G_FreeEdict(target);
    T_NULL(captain->target.unit);T_EQ(captain->state,BOT_CAPTAIN_TARGETED);
    T_ASSERT(captain->selection.request.active);
    T_ASSERT(captain->selection.request.sequence>previous);
    T_EQ(wc3_float_bits(captain->selection.request.deadline.time),wc3_float_bits(wc3_add(now.time,wc3_float(0x3dcccccd))));
    T_EQ(captain->selection.player,1);T_ASSERT(captain->policy_flags&4);
    T_ASSERT(captain->policy_flags&BOT_CAPTAIN_REQUEST_RESTORED);
    T_EQ(wc3_float_bits(captain->update_due.time),wc3_float_bits(periodic.time));T_EQ(captain->update_due.epoch,periodic.epoch);
    G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}

/* Native repeated SuicidePlayer selects a deferred structure primary through
 * the same .1-second selection owner, with policy bit80 and no member orders.
 * The external immutable78838 baseline records exactly four behavior failures
 * in both editions; this ordinary fixture preserves the public producer. */
TEST(wc3_bot, captain_strategic373_suicide_selects_deferred_structure_primary) {
    CaptainPolicyWorld();
    FOR_LOOP(p,2) {((mapInfo_t *)level.mapinfo)->players[p].used=true;
        ((mapInfo_t *)level.mapinfo)->players[p].playerType=kPlayerTypeHuman;
        game.clients[p].mapplayer=level.mapinfo->players+p;}
    player_t *player=&game.clients[0].ps;
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_MELEE));
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    edict_t *members[6];
    FOR_LOOP(i,6) {members[i]=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){256+80*(i%4),256+80*(i/4)},0);members[i]->mana.value=100;members[i]->mana.max_value=200;}
    T_ASSERT(G_BotAddAssault(player,6,MAKEFOURCC('h','f','o','o')));
    botCaptain_t *captain=level.bots[0].captains;
    edict_t *ordinary=unit_create(1,MAKEFOURCC('h','f','o','o'),&(vec2_t){704,512},0);
    edict_t *building=unit_create(1,MAKEFOURCC('h','b','a','r'),&(vec2_t){800,512},0);
    S_SetUnitPaused(ordinary,true);S_SetUnitPaused(building,true);gi.LinkEntity(ordinary);gi.LinkEntity(building);
    T_ASSERT(G_UnitIsStructure(building));T_ASSERT(!G_UnitIsStructure(ordinary));
    uint32_t visibility=player->rdflags;player->rdflags|=RDF_NOFOG;
    G_BotCaptainAttack(player,&(vec2_t){256,0});
    T_ASSERT(G_BotSuicidePlayer(player,1,false));
    T_ASSERT(captain->selection.request.active);T_ASSERT(captain->policy_flags&0x80);
    T_NULL(captain->target.unit);T_EQ(captain->goal.x,256);T_EQ(captain->goal.y,0);
    FOR_LOOP(i,6)T_ASSERT(members[i]->attack_target!=ordinary && members[i]->attack_target!=building);
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    FOR_LOOP(i,50){level.time+=5;globals.RunFrame();}
    T_EQ(captain->target.unit,building);T_EQ(captain->state,BOT_CAPTAIN_TARGETED);
    T_ASSERT(!G_BotCaptainRetreating(player));T_ASSERT(!captain->selection.request.active);
    player->rdflags=visibility;G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}

/* Added only after the ordinary Suicide producer baseline is recorded.
 * All inputs and complete outputs come from two retail observations/control. */
#include "../../common/wc3_ai_regions.h"
#include "retail_captain369_regions.h"
TEST(wc3_bot, captain_regions373_native_grid_graph_rng_and_destination) {
    wc3AiRegions_t *regions=gi.MemAlloc(sizeof(*regions));
    uint8_t classes[6800],grid[65536];uint32_t count=0;
    for(uint32_t i=0;i<sizeof(captain369_class_runs)/sizeof(uint32_t);i+=2) {
        memset(classes+count,captain369_class_runs[i+1],captain369_class_runs[i]);
        count+=captain369_class_runs[i];
    }
    T_EQ(count,6800);
    T_ASSERT(wc3_ai_regions_build(regions,100,68,classes));
    float origin[2]={-7168,-3072};
    for(uint32_t i=0;i<6;i+=2) {
        float point[2]={wc3_float(captain369_structure_positions[i]),wc3_float(captain369_structure_positions[i+1])};
        wc3_ai_regions_mark(regions,point,origin,4);
    }
    wc3_ai_regions_expand_marks(regions);
    T_EQ(regions->count,67);
    T_EQ(memcmp(regions->nodes,captain369_node_words,sizeof(captain369_node_words)),0);
    count=0;
    for(uint32_t i=0;i<sizeof(captain369_grid_runs)/sizeof(uint32_t);i+=2) {
        memset(grid+count,captain369_grid_runs[i+1],captain369_grid_runs[i]);
        count+=captain369_grid_runs[i];
    }
    T_EQ(count,65536);T_EQ(memcmp(regions->grid,grid,sizeof(grid)),0);
    float actor[2]={wc3_float(captain369_actor_position[0]),wc3_float(captain369_actor_position[1])};
    uint32_t nearest=wc3_ai_region_nearest(regions,actor,origin,4);T_EQ(nearest,6);
    float point[2]={wc3_float(captain369_initial_point[0]),wc3_float(captain369_initial_point[1])};
    wc3_ai_regions_filter(regions,nearest,point,origin,4);
    T_EQ(regions->eligible_count,2);T_EQ(regions->near_count,2);
    T_EQ(regions->eligible[0],3);T_EQ(regions->eligible[1],8);
    T_EQ(regions->near[0],3);T_EQ(regions->near[1],8);
    T_EQ(memcmp(regions->nodes,captain369_filtered_words,sizeof(captain369_filtered_words)),0);
    wc3Random_t random={2851754017u,1215875280u};
    T_EQ(wc3_random_range(&random,3),0);
    T_EQ(random.sum,809192828u);T_EQ(random.index,1147973812u);
    T_ASSERT(wc3_ai_regions_roll(regions,&random,origin,4,point));
    T_EQ(random.sum,2186210856u);T_EQ(random.index,1080072344u);
    T_EQ(wc3_float_bits(point[0]),3306160128u);T_EQ(wc3_float_bits(point[1]),1132462080u);
    gi.MemFree(regions);
}

/* Replay the captured selection-publication boundary through the production
 * timer, cached hierarchy and structure query. This is not a full-map journey:
 * candidate ranking is restored at its captured bucket boundary, while all
 * region inputs, actual structure/Captain positions and RNG state are native. */
static void CaptainRegions373PublicationReplay(bool hidden_sources) {
    CaptainPolicyWorld();
    static uint8_t terrain[384*256];memset(terrain,0,sizeof(terrain));
    CM_SetupTestWorldBounds(&(box2_t){{-7168,-3072},{5120,5120}});
    CM_SetupTestPathmap(384,256,terrain);
    FOR_LOOP(p,2) {((mapInfo_t *)level.mapinfo)->players[p].used=true;
        ((mapInfo_t *)level.mapinfo)->players[p].playerType=kPlayerTypeHuman;
        game.clients[p].mapplayer=level.mapinfo->players+p;}
    player_t *player=&game.clients[0].ps;
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_MELEE));
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    uint32_t types[3]={MAKEFOURCC('u','g','o','l'),MAKEFOURCC('h','b','a','r'),MAKEFOURCC('h','t','o','w')};
    edict_t *structures[3];
    FOR_LOOP(i,3) {
        vec2_t position={wc3_float(captain369_structure_positions[i*2]),wc3_float(captain369_structure_positions[i*2+1])};
        structures[i]=unit_create(1,types[i],&position,0);
        S_SetUnitPaused(structures[i],true);S_PlaceUnitPosition(structures[i],&position);
        T_ASSERT(G_UnitIsStructure(structures[i]));
        if(hidden_sources){G_SetEntityHidden(structures[i],true);T_ASSERT(structures[i]->s.renderfx&RF_HIDDEN);}
    }
    botCaptain_t *captain=level.bots[0].captains;
    vec2_t actor={wc3_float(captain369_actor_position[0]),wc3_float(captain369_actor_position[1])};
    S_PlaceUnitPosition(captain->home_actor,&actor);
    vec2_t actual;S_UnitPredictedWorldPosition(captain->home_actor,&actual);
    T_EQ(wc3_float_bits(actual.x),captain369_actor_position[0]);
    T_EQ(wc3_float_bits(actual.y),captain369_actor_position[1]);
    T_ASSERT(level.bots[0].flags&BOT_RANDOM_PATHS);
    T_ASSERT(G_BotSuicidePlayer(player,1,false));
    T_ASSERT(captain->selection.request.active);
    ARRAY_COUNT(captain->selection.pending)=0;captain->selection.pending_first=0;
    captain->selection.best[1]=(botCaptainTarget_t){structures[1],structures[1]->spawn_time};
    captain->selection.scores[1]=0;
    uint32_t size=G_GetMoveAdaptiveStateSize();uint8_t *cache=gi.MemAlloc(size);
    T_ASSERT(G_GetMoveAdaptiveState(cache,size));
    point2_t first=G_GetMoveAdaptiveMapSize(0),second=G_GetMoveAdaptiveMapSize(1);
    T_EQ(second.x,100);T_EQ(second.y,68);
    uint32_t offset=first.x*first.y*4,count=0;
    for(uint32_t i=0;i<sizeof(captain369_class_runs)/sizeof(uint32_t);i+=2) {
        memset(cache+offset+count,captain369_class_runs[i+1],captain369_class_runs[i]);
        count+=captain369_class_runs[i];
    }
    T_EQ(count,6800);T_ASSERT(G_SetMoveAdaptiveState(cache,size));gi.MemFree(cache);
    level.purpose_random[13]=(wc3Random_t){2851754017u,1215875280u};
    T_ASSERT(CAbilityCaptainSelection(NULL,A_PRIMARY_TIMER_FIRE,NULL));
    T_EQ(captain->target.unit,structures[1]);T_EQ(captain->state,BOT_CAPTAIN_TARGETED);
    /* Generic WidgetQuery excludes world-retired sources (Unit5c bit8).
     * With no source marks the consumer retains the building point and spends
     * no second random draw. Ranking still consumes its first purpose13 draw. */
    T_EQ(wc3_float_bits(captain->goal.x),hidden_sources?3301965824u:3306160128u);
    T_EQ(wc3_float_bits(captain->goal.y),hidden_sources?3292528640u:1132462080u);
    T_EQ(level.purpose_random[13].sum,hidden_sources?809192828u:2186210856u);
    T_EQ(level.purpose_random[13].index,hidden_sources?1147973812u:1080072344u);
    G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}

TEST(wc3_bot, captain_regions373_native_cache_rng_reaches_publication) {
    CaptainRegions373PublicationReplay(false);
}
TEST(wc3_bot, captain_regions373_hidden_structures_do_not_mark_destinations) {
    CaptainRegions373PublicationReplay(true);
}

/* Proper pre-port external query/ownership regressions use447 ABI guards;
 * normal compiled versions retain public ShowUnit and actual Wind Walk cast. */
TEST(wc3_bot, captain_strategic373_query_omits_script_hidden_structures) {
 CaptainPolicyWorld();
 T_ASSERT(G_BotStart(&game.clients[0].ps,"test_idle.ai",BOT_MELEE));
 FOR_LOOP(i,3){edict_t *u=unit_create(1,MAKEFOURCC('h','b','a','r'),&(vec2_t){704+128*i,512},0);T_ASSERT(G_UnitIsStructure(u));G_SetEntityHidden(u,true);T_ASSERT(u->s.renderfx&RF_HIDDEN);}
 T_ASSERT(G_BotSuicidePlayer(&game.clients[0].ps,1,false));botCaptain_t *captain=level.bots[0].captains;
 T_ASSERT(captain->selection.request.active);T_EQ(ARRAY_COUNT(captain->selection.pending),0);
 G_BotStop(0);reset_entities();setup_test_world();
}

static void Captain373QueryCount(uint32_t expected) {
 player_t *player=&game.clients[0].ps;G_BotCreateCaptains(player);
 T_ASSERT(G_BotSuicidePlayer(player,1,false));T_ASSERT(level.bots[0].captains[0].selection.request.active);
 T_EQ(ARRAY_COUNT(level.bots[0].captains[0].selection.pending),expected);
}
static void Captain373Call(cstring_t name) {jass_callbyname(level.vm,name,true);jass_runevents(level.vm);}
TEST(wc3_bot, captain_strategic373_show_invisibility_inverse_and_saved_query) {
 CaptainPolicyWorld();
 const char slk[]="ID;PWXL;N;EBB;Y2;X9\nC;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\nC;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Dur1\"\nC;Y1;X7;K\"HeroDur1\"\nC;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"DataC1\"\nC;Y2;X1;K\"AOwk\"\nC;Y2;X2;K\"AOwk\"\nC;Y2;X3;K\"1\"\nC;Y2;X4;K\"75\"\nC;Y2;X5;K\"5\"\nC;Y2;X6;K\"20\"\nC;Y2;X7;K\"20\"\nC;Y2;X8;K\"0.1\"\nC;Y2;X9;K\"40\"\nE\n";
 slkTestData_t *rows=parse_slk_string(slk),*old=G_SetSLKRows("AbilityData",rows);
 T_ASSERT(G_BotStart(&game.clients[0].ps,"test_idle.ai",BOT_MELEE));
 T_ASSERT(run_test_jass(
 "globals\nunit captain373Target=null\nendglobals\n"
 "function hideTarget takes nothing returns nothing\ncall ShowUnit(captain373Target,false)\nendfunction\n"
 "function showTarget takes nothing returns nothing\ncall ShowUnit(captain373Target,true)\nendfunction\n"
 "function clearInvisibility takes nothing returns nothing\ncall UnitRemoveAbility(captain373Target,'BOwk')\nendfunction\n"
 "function castTarget takes nothing returns nothing\ncall BJassAssert(IssueImmediateOrder(captain373Target,\"windwalk\"),\"windwalk accepted\")\nendfunction\n"
 "function main takes nothing returns nothing\nset captain373Target=CreateUnit(Player(1),'Hpal',704.0,512.0,0.0)\n"
 "call UnitAddAbility(captain373Target,'AOwk')\ncall SetUnitState(captain373Target,UNIT_STATE_MANA,100.0)\nendfunction\n"));
 edict_t *hero=NULL;FILTER_EDICTS(unit,unit->inuse&&unit->class_id==MAKEFOURCC('H','p','a','l')&&unit->s.player==1)hero=unit;
 T_NOT_NULL(hero);if(!hero)return;
 level.started=level.scriptsConfigured=level.scriptsStarted=true;FOR_LOOP(i,200){level.time+=5;globals.RunFrame();}
 T_EQ(G_UnitAbilityLevel(hero,MAKEFOURCC('A','O','w','k')),1);Captain373Call("castTarget");FOR_LOOP(i,400){level.time+=5;globals.RunFrame();}
 T_ASSERT(S_UnitHasTemporaryInvisibility(hero,NULL));T_ASSERT(hero->s.renderfx&RF_HIDDEN);
 Captain373QueryCount(1);
 Captain373Call("hideTarget");T_ASSERT(S_UnitHasTemporaryInvisibility(hero,NULL));T_ASSERT(hero->s.renderfx&RF_HIDDEN);T_EQ(S_MoveTargetStatus(level.bots[0].captains[0].home_actor,hero),MOVE_TARGET_HIDDEN);T_ASSERT(S_UnitIsHiddenFromPlayer(hero,1));Captain373QueryCount(0);
 cstring_t file=Test_TempPath("captain373-invisible-hidden.bin");T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));
 T_ASSERT(S_UnitHasTemporaryInvisibility(hero,NULL));T_ASSERT(hero->s.renderfx&RF_HIDDEN);T_EQ(S_MoveTargetStatus(level.bots[0].captains[0].home_actor,hero),MOVE_TARGET_HIDDEN);Captain373QueryCount(0);
 Captain373Call("showTarget");T_ASSERT(S_UnitHasTemporaryInvisibility(hero,NULL));T_ASSERT(hero->s.renderfx&RF_HIDDEN);Captain373QueryCount(1);
 Captain373Call("hideTarget");Captain373Call("clearInvisibility");T_ASSERT(!S_UnitHasTemporaryInvisibility(hero,NULL));T_ASSERT(hero->s.renderfx&RF_HIDDEN);T_EQ(S_MoveTargetStatus(level.bots[0].captains[0].home_actor,hero),MOVE_TARGET_HIDDEN);Captain373QueryCount(0);
 Captain373Call("showTarget");T_ASSERT(!(hero->s.renderfx&RF_HIDDEN));Captain373QueryCount(1);
 G_SetSLKRows("AbilityData",old);free_slk_rows(rows);remove(file);G_BotStop(0);level.started=false;reset_entities();setup_test_world();
}

/* External draft. Append to t_captain_policy.c only in an isolated reviewed
 * snapshot. No Captain/Town identities or target subscriptions are seeded.
 * Native positive family: 610 public markers. Clear family: 611 markers. */
static void Captain376Step(unsigned milliseconds) {
    for(unsigned elapsed=0;elapsed<milliseconds;elapsed+=5) {
        level.time+=5;globals.RunFrame();
    }
}

static wc3Clock_t Captain376World(void) {
    G_BotStop(0);reset_entities();setup_test_world();
    level.time=level.pathing_msec=level.pathing_phase=0;
    level.pathing_due=level.pathing_owner_clock_valid=level.timer_clock_valid=false;
    static uint8_t cells[64*64];memset(cells,0,sizeof(cells));
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
    level.pathing_clock=(wc3Clock_t){0,0,300};
    ((mapInfo_t *)level.mapinfo)->players[0].used=true;
    game.clients[0].mapplayer=level.mapinfo->players;
    game.clients[0].jass.controller=1;
    wc3Clock_t town_constructed=G_TimerQueryClock(NULL);
    G_BotInitPlayers(); /* Actual Town creation, with no intervening frame. */
    return town_constructed;
}

/* The native old-B owner callback remains registered after Home clears its
 * role and a later Captain periodic owner retains C. Observe the concrete
 * registry visit without modifying its normal callback or manufacturing roles. */
static botCaptain_t *captain376_visit_owner;
static edict_t *captain376_visit_source;
static uint32_t captain376_old_source_visits,captain376_old_town_visits;
static uint32_t captain376_old_visit_order[2],captain376_old_visit_count;
static void Captain376RetainedVisit(targetSubscriberKind_t kind,void *owner,edict_t *source) {
    if(source!=captain376_visit_source)return;
    /* Kind4 is the forthcoming concrete Town owner; the regression compiles
     * against the unchanged earlier production enum to record a real red. */
    bool town=(uint32_t)kind==4 && owner==level.bots;
    bool captain=kind==TARGET_OWNER_CAPTAIN && owner==captain376_visit_owner;
    if(town)captain376_old_town_visits++;
    if(captain)captain376_old_source_visits++;
    if(town || captain) {
        if(captain376_old_visit_count<2)captain376_old_visit_order[captain376_old_visit_count]=kind;
        captain376_old_visit_count++;
    }
}

/* A singleton requested-player candidate makes the recovered four-pass
 * selector deterministic without seeding its selected identity or RNG. */
static edict_t const *captain376_point_source;
static abilityProc_t captain376_point_owner;
static uint32_t captain376_point_birth,captain376_point_range,captain376_point_calls;
static void Captain376PointApproach(edict_t const *unit,float range,abilityProc_t owner) {
    if((owner!=CAbilityAttack && owner!=CAbilityMove) || unit->s.player!=0)return;
    captain376_point_owner=owner;captain376_point_source=unit;captain376_point_birth=unit->spawn_time;
    captain376_point_range=wc3_float_bits(range);captain376_point_calls++;
}

static void Captain376AssertSuicidePoint(botCaptain_t *captain,edict_t **members,edict_t *target) {
    edict_t *selected=NULL;
    FOR_LOOP(i,6)if(members[i]->movement.captain_home.roster_actor!=captain->home_actor) {
        T_NULL(selected);selected=members[i];
    }
    T_NOT_NULL(selected);if(!selected)return;
    T_EQ(target->s.player,1);T_ASSERT(target->inuse && !M_IsDead(target));
    T_EQ(selected->current_order_id,851983u);
    T_NOT_NULL(selected->currentmove);
    if(selected->currentmove)T_EQ(selected->currentmove->proc,CAbilityAttack);
    T_NOT_NULL(selected->movement.attackmove_waypoint);
    if(selected->movement.attackmove_waypoint) {
        T_EQ(wc3_float_bits(selected->movement.attackmove_waypoint->s.origin2.x),wc3_float_bits(target->s.origin2.x));
        T_EQ(wc3_float_bits(selected->movement.attackmove_waypoint->s.origin2.y),wc3_float_bits(target->s.origin2.y));
    }
    moveGroup_t *group=move_unit_group(selected);T_NOT_NULL(group);
    if(group) {
        T_EQ(group->count,1u);T_EQ(group->receiver,selected);
        T_EQ(group->receiver_spawn,selected->spawn_time);
        T_EQ(group->owner_ability,GetAbilityIndex(CAbilityAttack)+1);
        T_EQ(group->complete,S_AttackMoveComplete);T_NULL(group->target);
        /* Raw d016b range is world-space zero; the physical group retains
         * its independently recovered minimum fine-cell arrival gate. */
        T_ASSERT(captain376_point_calls>0);T_EQ(captain376_point_source,selected);
        T_EQ(captain376_point_birth,selected->spawn_time);T_EQ(captain376_point_range,0u);T_EQ(captain376_point_owner,CAbilityAttack);
        T_EQ(wc3_float_bits(group->members[0].arrival_range),0x3efae148u);
    }
}

/* Native O routes d000f through Move when the Attack object is absent. */
static void Captain376AssertSuicideMove(botCaptain_t *captain,edict_t **members,edict_t *target) {
    edict_t *selected=NULL;
    FOR_LOOP(i,6)if(members[i]->movement.captain_home.roster_actor!=captain->home_actor){T_NULL(selected);selected=members[i];}
    T_NOT_NULL(selected);if(!selected)return;
    T_ASSERT(!G_ActorHasAbilityCode(selected,MAKEFOURCC('A','a','t','k')));
    T_EQ(selected->current_order_id,851983u);T_NOT_NULL(selected->currentmove);
    if(selected->currentmove)T_EQ(selected->currentmove->proc,CAbilityMove);
    T_NULL(selected->movement.attackmove_waypoint);T_NOT_NULL(selected->goalentity);
    if(selected->goalentity){T_EQ(wc3_float_bits(selected->goalentity->s.origin2.x),wc3_float_bits(target->s.origin2.x));T_EQ(wc3_float_bits(selected->goalentity->s.origin2.y),wc3_float_bits(target->s.origin2.y));}
    moveGroup_t *group=move_unit_group(selected);T_NOT_NULL(group);
    if(group){
        T_EQ(group->count,1u);T_EQ(group->members[0].unit,selected);
        T_NULL(group->receiver);T_EQ(group->owner_ability,0u);T_NULL(group->complete);T_NULL(group->target);
        T_ASSERT(captain376_point_calls>0);T_EQ(captain376_point_source,selected);T_EQ(captain376_point_birth,selected->spawn_time);
        T_EQ(captain376_point_owner,CAbilityMove);T_EQ(captain376_point_range,0u);
        T_EQ(wc3_float_bits(group->members[0].arrival_range),0x3efae148u);
    }
}

static void Captain376PublicTownClear(bool cold,unsigned inverse) {
    wc3Clock_t town_constructed=Captain376World();
    slkTestData_t *query_data=NULL,*old_query_data=NULL;
    /* The minimal MPQ omits UnitWeapons entirely. Restore the literal retail
     * Footman row before construction so Attack owns a real acquisition timer.
     * Intrinsic Move/Attack are derived from these real authored fields. */
    slkTestData_t *data=parse_slk_string(
        "ID;PWXL;N;E\n"
        "C;X1;Y1;K\"unitID\"\n"
        "C;X2;Y1;K\"sort\"\n"
        "C;X3;Y1;K\"comment(s)\"\n"
        "C;X4;Y1;K\"race\"\n"
        "C;X5;Y1;K\"prio\"\n"
        "C;X6;Y1;K\"threat\"\n"
        "C;X7;Y1;K\"valid\"\n"
        "C;X8;Y1;K\"deathType\"\n"
        "C;X9;Y1;K\"death\"\n"
        "C;X10;Y1;K\"canSleep\"\n"
        "C;X11;Y1;K\"cargoSize\"\n"
        "C;X12;Y1;K\"movetp\"\n"
        "C;X13;Y1;K\"moveHeight\"\n"
        "C;X14;Y1;K\"moveFloor\"\n"
        "C;X15;Y1;K\"turnRate\"\n"
        "C;X16;Y1;K\"propWin\"\n"
        "C;X17;Y1;K\"orientInterp\"\n"
        "C;X18;Y1;K\"formation\"\n"
        "C;X19;Y1;K\"targType\"\n"
        "C;X20;Y1;K\"pathTex\"\n"
        "C;X21;Y1;K\"fatLOS\"\n"
        "C;X22;Y1;K\"points\"\n"
        "C;X23;Y1;K\"buffType\"\n"
        "C;X24;Y1;K\"buffRadius\"\n"
        "C;X25;Y1;K\"nameCount\"\n"
        "C;X26;Y1;K\"canFlee\"\n"
        "C;X27;Y1;K\"requireWaterRadius\"\n"
        "C;X28;Y1;K\"isBuildOn\"\n"
        "C;X29;Y1;K\"canBuildOn\"\n"
        "C;X30;Y1;K\"InBeta\"\n"
        "C;X31;Y1;K\"version\"\n"
        "C;X1;Y2;K\"hfoo\"\n"
        "C;X2;Y2;K\"a2\"\n"
        "C;X3;Y2;K\"Footman\"\n"
        "C;X4;Y2;K\"human\"\n"
        "C;X5;Y2;K6\n"
        "C;X6;Y2;K1\n"
        "C;X7;Y2;K1\n"
        "C;X8;Y2;K3\n"
        "C;X9;Y2;K3.04\n"
        "C;X10;Y2;K0\n"
        "C;X11;Y2;K1\n"
        "C;X12;Y2;K\"foot\"\n"
        "C;X13;Y2;K0\n"
        "C;X14;Y2;K0\n"
        "C;X15;Y2;K0.6\n"
        "C;X16;Y2;K60\n"
        "C;X17;Y2;K0\n"
        "C;X18;Y2;K0\n"
        "C;X19;Y2;K\"ground\"\n"
        "C;X20;Y2;K\"_\"\n"
        "C;X21;Y2;K0\n"
        "C;X22;Y2;K100\n"
        "C;X23;Y2;K\"_\"\n"
        "C;X24;Y2;K\"-\"\n"
        "C;X25;Y2;K\"-\"\n"
        "C;X26;Y2;K1\n"
        "C;X27;Y2;K0\n"
        "C;X28;Y2;K0\n"
        "C;X29;Y2;K0\n"
        "C;X30;Y2;K1\n"
        "C;X31;Y2;K0\n"
        "C;X1;Y3;K\"htow\"\n"
        "C;X2;Y3;K\"a3\"\n"
        "C;X3;Y3;K\"TownHall\"\n"
        "C;X4;Y3;K\"human\"\n"
        "C;X5;Y3;K6\n"
        "C;X6;Y3;K1\n"
        "C;X7;Y3;K1\n"
        "C;X8;Y3;K2\n"
        "C;X9;Y3;K3.17\n"
        "C;X10;Y3;K0\n"
        "C;X11;Y3;K\"-\"\n"
        "C;X12;Y3;K\"_\"\n"
        "C;X13;Y3;K0\n"
        "C;X14;Y3;K0\n"
        "C;X15;Y3;K\"-\"\n"
        "C;X16;Y3;K60\n"
        "C;X17;Y3;K0\n"
        "C;X18;Y3;K0\n"
        "C;X19;Y3;K\"structure\"\n"
        "C;X20;Y3;K\"PathTextures\\16x16Simple.tga\"\n"
        "C;X21;Y3;K0\n"
        "C;X22;Y3;K100\n"
        "C;X23;Y3;K\"townhall\"\n"
        "C;X24;Y3;K8\n"
        "C;X25;Y3;K\"-\"\n"
        "C;X26;Y3;K1\n"
        "C;X27;Y3;K0\n"
        "C;X28;Y3;K0\n"
        "C;X29;Y3;K0\n"
        "C;X30;Y3;K1\n"
        "C;X31;Y3;K0\n"
        "C;X1;Y4;K\"Hpal\"\nC;X12;Y4;K\"foot\"\nC;X15;Y4;K0.5\nC;X16;Y4;K60\nC;X19;Y4;K\"ground\"\nC;X20;Y4;K\"_\"\n"
        "E\n");
    slkTestData_t *old_data=G_SetSLKRows("UnitData",data);
    slkTestData_t *weapons=parse_slk_string(
        "ID;PWXL;N;E\n"
        "C;X1;Y1;K\"unitWeapID\"\n"
        "C;X2;Y1;K\"sortWeap\"\n"
        "C;X3;Y1;K\"sort2\"\n"
        "C;X4;Y1;K\"comment(s)\"\n"
        "C;X5;Y1;K\"weapsOn\"\n"
        "C;X6;Y1;K\"acquire\"\n"
        "C;X7;Y1;K\"minRange\"\n"
        "C;X8;Y1;K\"castpt\"\n"
        "C;X9;Y1;K\"castbsw\"\n"
        "C;X10;Y1;K\"launchX\"\n"
        "C;X11;Y1;K\"launchY\"\n"
        "C;X12;Y1;K\"launchZ\"\n"
        "C;X13;Y1;K\"launchSwimZ\"\n"
        "C;X14;Y1;K\"impactZ\"\n"
        "C;X15;Y1;K\"impactSwimZ\"\n"
        "C;X16;Y1;K\"weapType1\"\n"
        "C;X17;Y1;K\"targs1\"\n"
        "C;X18;Y1;K\"showUI1\"\n"
        "C;X19;Y1;K\"rangeN1\"\n"
        "C;X20;Y1;K\"RngTst\"\n"
        "C;X21;Y1;K\"RngBuff1\"\n"
        "C;X22;Y1;K\"atkType1\"\n"
        "C;X23;Y1;K\"weapTp1\"\n"
        "C;X24;Y1;K\"cool1\"\n"
        "C;X25;Y1;K\"mincool1\"\n"
        "C;X26;Y1;K\"dice1\"\n"
        "C;X27;Y1;K\"sides1\"\n"
        "C;X28;Y1;K\"dmgplus1\"\n"
        "C;X29;Y1;K\"dmgUp1\"\n"
        "C;X30;Y1;K\"mindmg1\"\n"
        "C;X31;Y1;K\"avgdmg1\"\n"
        "C;X32;Y1;K\"maxdmg1\"\n"
        "C;X33;Y1;K\"dmgpt1\"\n"
        "C;X34;Y1;K\"backSw1\"\n"
        "C;X35;Y1;K\"Farea1\"\n"
        "C;X36;Y1;K\"Harea1\"\n"
        "C;X37;Y1;K\"Qarea1\"\n"
        "C;X38;Y1;K\"Hfact1\"\n"
        "C;X39;Y1;K\"Qfact1\"\n"
        "C;X40;Y1;K\"splashTargs1\"\n"
        "C;X41;Y1;K\"targCount1\"\n"
        "C;X42;Y1;K\"damageLoss1\"\n"
        "C;X43;Y1;K\"spillDist1\"\n"
        "C;X44;Y1;K\"spillRadius1\"\n"
        "C;X45;Y1;K\"DmgUpg\"\n"
        "C;X46;Y1;K\"dmod1\"\n"
        "C;X47;Y1;K\"DPS\"\n"
        "C;X48;Y1;K\"weapType2\"\n"
        "C;X49;Y1;K\"targs2\"\n"
        "C;X50;Y1;K\"showUI2\"\n"
        "C;X51;Y1;K\"rangeN2\"\n"
        "C;X52;Y1;K\"RngTst2\"\n"
        "C;X53;Y1;K\"RngBuff2\"\n"
        "C;X54;Y1;K\"atkType2\"\n"
        "C;X55;Y1;K\"weapTp2\"\n"
        "C;X56;Y1;K\"cool2\"\n"
        "C;X57;Y1;K\"mincool2\"\n"
        "C;X58;Y1;K\"dice2\"\n"
        "C;X59;Y1;K\"sides2\"\n"
        "C;X60;Y1;K\"dmgplus2\"\n"
        "C;X61;Y1;K\"dmgUp2\"\n"
        "C;X62;Y1;K\"mindmg2\"\n"
        "C;X63;Y1;K\"avgdmg2\"\n"
        "C;X64;Y1;K\"maxdmg2\"\n"
        "C;X65;Y1;K\"dmgpt2\"\n"
        "C;X66;Y1;K\"backSw2\"\n"
        "C;X67;Y1;K\"Farea2\"\n"
        "C;X68;Y1;K\"Harea2\"\n"
        "C;X69;Y1;K\"Qarea2\"\n"
        "C;X70;Y1;K\"Hfact2\"\n"
        "C;X71;Y1;K\"Qfact2\"\n"
        "C;X72;Y1;K\"splashTargs2\"\n"
        "C;X73;Y1;K\"targCount2\"\n"
        "C;X74;Y1;K\"damageLoss2\"\n"
        "C;X75;Y1;K\"spillDist2\"\n"
        "C;X76;Y1;K\"spillRadius2\"\n"
        "C;X77;Y1;K\"InBeta\"\n"
        "C;X1;Y2;K\"hfoo\"\n"
        "C;X2;Y2;K\"a2\"\n"
        "C;X3;Y2;K\"me1\"\n"
        "C;X4;Y2;K\"Footman\"\n"
        "C;X5;Y2;K1\n"
        "C;X6;Y2;K500\n"
        "C;X7;Y2;K\"-\"\n"
        "C;X8;Y2;K0.3\n"
        "C;X9;Y2;K0.51\n"
        "C;X10;Y2;K0\n"
        "C;X11;Y2;K0\n"
        "C;X12;Y2;K60\n"
        "C;X13;Y2;K0\n"
        "C;X14;Y2;K60\n"
        "C;X15;Y2;K0\n"
        "C;X16;Y2;K\"MetalMediumSlice\"\n"
        "C;X17;Y2;K\"ground,structure,debris,item,ward\"\n"
        "C;X18;Y2;K1\n"
        "C;X19;Y2;K90\n"
        "C;X20;Y2;K\"-\"\n"
        "C;X21;Y2;K250\n"
        "C;X22;Y2;K\"normal\"\n"
        "C;X23;Y2;K\"normal\"\n"
        "C;X24;Y2;K1.35\n"
        "C;X25;Y2;K\"-\"\n"
        "C;X26;Y2;K1\n"
        "C;X27;Y2;K2\n"
        "C;X28;Y2;K11\n"
        "C;X29;Y2;K\"-\"\n"
        "C;X30;Y2;K12\n"
        "C;X31;Y2;K12.5\n"
        "C;X32;Y2;K13\n"
        "C;X33;Y2;K0.5\n"
        "C;X34;Y2;K0.5\n"
        "C;X35;Y2;K\" - \"\n"
        "C;X36;Y2;K\" - \"\n"
        "C;X37;Y2;K\" - \"\n"
        "C;X38;Y2;K\"-\"\n"
        "C;X39;Y2;K\"-\"\n"
        "C;X40;Y2;K\"_\"\n"
        "C;X41;Y2;K1\n"
        "C;X42;Y2;K0\n"
        "C;X43;Y2;K0\n"
        "C;X44;Y2;K0\n"
        "C;X45;Y2;K\"-\"\n"
        "C;X46;Y2;K\"-\"\n"
        "C;X47;Y2;K9.25925925925926\n"
        "C;X48;Y2;K\"_\"\n"
        "C;X49;Y2;K\"_\"\n"
        "C;X50;Y2;K1\n"
        "C;X51;Y2;K\"-\"\n"
        "C;X52;Y2;K\"-\"\n"
        "C;X53;Y2;K\"-\"\n"
        "C;X54;Y2;K\"normal\"\n"
        "C;X55;Y2;K\"_\"\n"
        "C;X56;Y2;K\"-\"\n"
        "C;X57;Y2;K\"-\"\n"
        "C;X58;Y2;K\"-\"\n"
        "C;X59;Y2;K\"-\"\n"
        "C;X60;Y2;K\"-\"\n"
        "C;X61;Y2;K\"-\"\n"
        "C;X62;Y2;K\" - \"\n"
        "C;X63;Y2;K\"-\"\n"
        "C;X64;Y2;K\" - \"\n"
        "C;X65;Y2;K\"-\"\n"
        "C;X66;Y2;K\"-\"\n"
        "C;X67;Y2;K\"-\"\n"
        "C;X68;Y2;K\"-\"\n"
        "C;X69;Y2;K\"-\"\n"
        "C;X70;Y2;K\"-\"\n"
        "C;X71;Y2;K\"-\"\n"
        "C;X72;Y2;K\"_\"\n"
        "C;X73;Y2;K1\n"
        "C;X74;Y2;K0\n"
        "C;X75;Y2;K0\n"
        "C;X76;Y2;K0\n"
        "C;X77;Y2;K1\n"
        "E\n");
    slkTestData_t *old_weapons=G_SetSLKRows("UnitWeapons",weapons);
    FOR_LOOP(p,4) {
        ((mapInfo_t *)level.mapinfo)->players[p].used=true;
        ((mapInfo_t *)level.mapinfo)->players[p].playerType=kPlayerTypeHuman;
        game.clients[p].mapplayer=level.mapinfo->players+p;
    }
    player_t *player=&game.clients[0].ps;
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_MELEE));
    T_ASSERT(run_test_jass(
        "globals\nunit preferred376Town\nunit preferred376Source\nunit preferred376CandidateUnit\ninteger preferred376Reaction=0\nendglobals\n"
        "function preferred376Attack takes nothing returns nothing\n"
        "set preferred376Source=CreateUnit(Player(1),'hfoo',1100.,700.,0.)\n"
        "call SetUnitUserData(preferred376Source,375)\n"
        "if IssueTargetOrder(preferred376Source,\"attack\",preferred376Town) then\n"
        "call SetUnitUserData(preferred376Source,376)\nendif\n"
        "if UnitDamageTarget(preferred376Source,preferred376Town,1.,true,false,ATTACK_TYPE_NORMAL,DAMAGE_TYPE_NORMAL,WEAPON_TYPE_WHOKNOWS) then\n"
        "call SetUnitUserData(preferred376Town,378)\nendif\nendfunction\n"
        "function preferred376Candidate takes nothing returns nothing\n"
        "set preferred376CandidateUnit=CreateUnit(Player(1),'hfoo',200.0,1800.0,0.0)\n"
        "call PauseUnit(preferred376CandidateUnit,true)\n"
        "call SetUnitUserData(preferred376CandidateUnit,379)\n"
        "endfunction\n"
        "function preferred376DamageCandidate takes nothing returns nothing\n"
        "if UnitDamageTarget(preferred376CandidateUnit,preferred376Town,1.0,true,false,ATTACK_TYPE_NORMAL,DAMAGE_TYPE_NORMAL,WEAPON_TYPE_WHOKNOWS) then\n"
        "call SetUnitUserData(preferred376CandidateUnit,380)\n"
        "endif\n"
        "endfunction\n"
        "function preferred376MoveCandidate takes nothing returns nothing\n"
        "call SetUnitPosition(preferred376CandidateUnit,100.0,1800.0)\n"
        "endfunction\n"
        "function preferred376DamageB takes nothing returns nothing\n"
        "if UnitDamageTarget(preferred376Source,preferred376Town,1.0,true,false,ATTACK_TYPE_NORMAL,DAMAGE_TYPE_NORMAL,WEAPON_TYPE_WHOKNOWS) then\n"
        "call SetUnitUserData(preferred376Source,381)\n"
        "endif\n"
        "endfunction\n"
        "function preferred376FindQuery takes nothing returns nothing\n"
        "if GetUnitUserData(GetEnumUnit())==382 then\nset preferred376CandidateUnit=GetEnumUnit()\nendif\nendfunction\n"
        "function preferred376QueryWindWalk takes nothing returns nothing\nlocal group g=CreateGroup()\n"
        "call GroupEnumUnitsOfPlayer(g,Player(1),null)\ncall ForGroup(g,function preferred376FindQuery)\ncall DestroyGroup(g)\n"
        "call UnitAddAbility(preferred376CandidateUnit,'AOwk')\ncall SetUnitState(preferred376CandidateUnit,UNIT_STATE_MANA,100.)\n"
        "if IssueImmediateOrder(preferred376CandidateUnit,\"windwalk\") then\ncall SetUnitUserData(preferred376CandidateUnit,383)\nendif\nendfunction\n"
        "function preferred376HideQuery takes nothing returns nothing\ncall ShowUnit(preferred376CandidateUnit,false)\nendfunction\n"
        "function preferred376RemoveAttackEach takes nothing returns nothing\n"
        "if GetUnitTypeId(GetEnumUnit())=='hfoo' then\ncall UnitRemoveAbility(GetEnumUnit(),'Aatk')\nendif\nendfunction\n"
        "function preferred376RemoveQueryAttack takes nothing returns nothing\nlocal group g=CreateGroup()\n"
        "call GroupEnumUnitsOfPlayer(g,Player(0),null)\ncall ForGroup(g,function preferred376RemoveAttackEach)\ncall DestroyGroup(g)\nendfunction\n"
        "function preferred376Issued takes nothing returns nothing\n"
        "if (preferred376Reaction==3 and GetIssuedOrderId()==851982) or (preferred376Reaction==4 and GetIssuedOrderId()==851983) then\n"
        "if IssuePointOrder(GetTriggerUnit(),\"move\",1600.,1700.) then\ncall SetUnitUserData(preferred376Town,911+preferred376Reaction)\n"
        "else\ncall SetUnitUserData(preferred376Town,1911+preferred376Reaction)\nendif\nreturn\nendif\n"
        "if (preferred376Reaction==1 and GetIssuedOrderId()==851982) or (preferred376Reaction==2 and GetIssuedOrderId()==851983) then\n"
        "call SetUnitUserData(preferred376Town,911+preferred376Reaction)\n"
        "call RemoveUnit(GetTriggerUnit())\nreturn\nendif\n"
        "if GetIssuedOrderId()==851982 or GetIssuedOrderId()==851972 then\n"
        "call SetUnitUserData(GetTriggerUnit(),GetUnitUserData(GetTriggerUnit())*10+GetIssuedOrderId()-851970)\nendif\nendfunction\n"
        "function preferred376ObserveSuicideOrders takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)\n"
        "call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)\n"
        "call TriggerAddAction(t,function preferred376Issued)\nendfunction\n"
        "function preferred376RemoveImmediate takes nothing returns nothing\nset preferred376Reaction=1\ncall preferred376ObserveSuicideOrders()\nendfunction\n"
        "function preferred376RemovePoint takes nothing returns nothing\nset preferred376Reaction=2\ncall preferred376ObserveSuicideOrders()\nendfunction\n"
        "function preferred376ReplaceImmediate takes nothing returns nothing\nset preferred376Reaction=3\ncall preferred376ObserveSuicideOrders()\nendfunction\n"
        "function preferred376ReplacePoint takes nothing returns nothing\nset preferred376Reaction=4\ncall preferred376ObserveSuicideOrders()\nendfunction\n"
        "function preferred376Allied takes nothing returns nothing\nlocal integer i=0\nloop\nexitwhen i==16\n"
        "call SetPlayerAlliance(Player(0),Player(i),ALLIANCE_PASSIVE,true)\nset i=i+1\nendloop\nendfunction\n"
        "function main takes nothing returns nothing\n"
        "set preferred376Town=CreateUnit(Player(0),'htow',1400.,700.,0.)\n"
        "endfunction\n"));
    char mode_source[]=
        "function preferred376Campaign takes nothing returns nothing\ncall SetCampaignAI()\nendfunction\n"
        "function preferred376Melee takes nothing returns nothing\ncall SetMeleeAI()\nendfunction\n"
        "function preferred376SuicideTown takes nothing returns nothing\ncall SuicideUnitEx(1,'htow',1)\nendfunction\n"
        "function preferred376SuicideMember takes nothing returns nothing\ncall SuicideUnitEx(1,'hfoo',1)\nendfunction\n"
        "function preferred376UnpauseEach takes nothing returns nothing\n"
        "if GetUnitTypeId(GetEnumUnit())=='hfoo' then\ncall PauseUnit(GetEnumUnit(),false)\nendif\nendfunction\n"
        "function preferred376SuicideNoTarget takes nothing returns nothing\ncall SuicideUnitEx(1,'hfoo',15)\nendfunction\n"
        "function preferred376StopEach takes nothing returns nothing\n"
        "if GetUnitTypeId(GetEnumUnit())=='hfoo' then\n"
        "call SetUnitUserData(GetEnumUnit(),375)\n"
        "if IssueImmediateOrder(GetEnumUnit(),\"stop\") then\ncall SetUnitUserData(GetEnumUnit(),376)\nendif\nendif\nendfunction\n"
        "function preferred376PrepareMembers takes nothing returns nothing\n"
        "local group g=CreateGroup()\ncall GroupEnumUnitsOfPlayer(g,GetAiPlayer(),null)\n"
        "call ForGroup(g,function preferred376UnpauseEach)\ncall DestroyGroup(g)\nendfunction\n"
        "function preferred376StopMembers takes nothing returns nothing\n"
        "local group g=CreateGroup()\ncall GroupEnumUnitsOfPlayer(g,GetAiPlayer(),null)\n"
        "call ForGroup(g,function preferred376StopEach)\ncall DestroyGroup(g)\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.bots[0].vm,mode_source));
    /* Retain separate Town and Captain constructor phases through real API. */
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    Captain376Step(250);G_BotCreateCaptains(player);
    G_BotSetCaptainHome(player,1,256,1024);
    edict_t *members[6];
    FOR_LOOP(i,6) {
        members[i]=unit_create(0,MAKEFOURCC('h','f','o','o'),
            &(vec2_t){256+80*(i%4),256+80*(i/4)},0);
        T_ASSERT(members[i] && members[i]->inuse);
        T_ASSERT(G_ActorHasAbilityCode(members[i],MAKEFOURCC('A','m','o','v')));
        T_ASSERT(G_ActorHasAbilityCode(members[i],MAKEFOURCC('A','a','t','k')));
        T_ASSERT(members[i]->attack_scan.active);T_EQ(members[i]->targtype,TARG_GROUND);
        T_EQ(wc3_float_bits(S_AttackProfileRead(members[i],0)->range),wc3_float_bits(90));
        T_EQ(wc3_float_bits(members[i]->runtime.acquisition_range),wc3_float_bits(500));
        members[i]->mana.value=100;members[i]->mana.max_value=200;
    }
    T_ASSERT(G_BotAddAssault(player,6,MAKEFOURCC('h','f','o','o')));
    botCaptain_t *captain=level.bots[0].captains;
    edict_t *selected=unit_create(1,(inverse>=15 && inverse<=17) ? MAKEFOURCC('H','p','a','l') : MAKEFOURCC('h','f','o','o'),&(vec2_t){1400,1500},0);
    T_ASSERT(selected && selected->inuse);S_SetUnitPaused(selected,true);
    player->rdflags|=RDF_NOFOG;
    G_BotCaptainVsPlayer(player,&game.clients[1].ps);
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    for(unsigned elapsed=0;elapsed<10000 && captain->follow.unit!=selected;elapsed+=5)
        Captain376Step(5);
    /* These setup assertions must pass before the preferred-primary behavior
     * is classified. The public selection/periodic owners produce both roles. */
    T_EQ(captain->target.unit,selected);T_EQ(captain->follow.unit,selected);
    FOR_LOOP(i,6)S_SetUnitPaused(members[i],true);
    jass_callbyname(level.vm,"preferred376Attack",false);
    edict_t *preferred=NULL,*town=NULL;
    FILTER_EDICTS(e,e->inuse) {
        if(e->user_data==375 || e->user_data==376)preferred=e;
        if(e->user_data==378)town=e;
    }
    T_ASSERT(preferred && town);
    if(!preferred || !town) {G_BotStop(0);level.started=false;reset_entities();
    G_SetSLKRows("UnitData",old_data);free_slk_rows(data);
    G_SetSLKRows("UnitWeapons",old_weapons);free_slk_rows(weapons);
    setup_test_world();return;}
    T_EQ(preferred->user_data,376); /* Independent accepted attack marker. */
    T_EQ(town->user_data,378); /* Independent accepted damage marker. */
    T_ASSERT(preferred->health.value>0 && town->health.value>0);
    T_ASSERT(town->aiflags&AI_TOWN_OWNED);
    Captain376Step(1500); /* Exercise real weapon/help delivery and its latch. */
    T_EQ(captain->target.unit,selected);T_EQ(captain->follow.unit,selected);
    /* Missing Town publication is a behavior failure, separate from setup. */
    T_EQ(level.bots[0].captains[BOT_CAPTAIN_DEFENSE].follow.unit,preferred);
    if(inverse==1 || inverse==2) {
        jass_t *vm=level.bots[0].vm;
        if(inverse==17) {
            jass_callbyname(level.vm,"preferred376RemoveQueryAttack",false);
            FOR_LOOP(i,6)T_ASSERT(!G_ActorHasAbilityCode(members[i],MAKEFOURCC('A','a','t','k')));
        }
        if(inverse==12 || inverse==13)jass_callbyname(level.vm,inverse==12 ? "preferred376RemoveImmediate" : "preferred376RemovePoint",false);
        if(inverse==18 || inverse==19)jass_callbyname(level.vm,inverse==18 ? "preferred376ReplaceImmediate" : "preferred376ReplacePoint",false);
        jasscoroutine_t *command=jass_startcoroutinebynameforplayer(vm,"preferred376Campaign",player);
        T_NOT_NULL(command);if(command)jass_resume(vm,command);
        T_EQ(level.bots[0].mode,BOT_CAMPAIGN);
        if(inverse==2) {
            command=jass_startcoroutinebynameforplayer(vm,"preferred376Melee",player);
            T_NOT_NULL(command);if(command)jass_resume(vm,command);
            T_EQ(level.bots[0].mode,BOT_MELEE);
        }
    } else if(inverse==3) {
        G_SetUnitPlayer(preferred,3);T_EQ(preferred->s.player,3);
        T_ASSERT(preferred->health.value>0);
        T_NULL(level.bots[0].captains[BOT_CAPTAIN_DEFENSE].follow.unit);
    } else if(inverse==4) {
        S_SetUnitPaused(preferred,true);
        botCaptain_t *defense=level.bots[0].captains+BOT_CAPTAIN_DEFENSE;
        wc3Clock_t now=G_TimerQueryClock(NULL);
        T_EQ(now.epoch,town_constructed.epoch);
        /* Native Town initializer passes Math_IntegerTwo. Derive the next
         * deadline from the captured constructor frontier, not global zero. */
        float period=2;
        float cycle=floorf(wc3_sub(now.time,town_constructed.time)/period)+1;
        float town_due=wc3_add(town_constructed.time,wc3_mul(cycle,period));
        while(G_TimerQueryClock(NULL).time<wc3_sub(town_due,.05f))Captain376Step(5);
        now=G_TimerQueryClock(NULL);
        wc3Clock_t captain_due=defense->update_due;
        T_ASSERT(now.time<town_due);
        T_EQ(captain_due.epoch,now.epoch);T_ASSERT(captain_due.time>town_due);
        T_EQ(preferred->s.player,1);T_ASSERT(preferred->health.value>0);
        G_BotSetCaptainHome(player,2,100,1800);
        T_NULL(defense->follow.unit);
        while(G_TimerQueryClock(NULL).time<town_due)Captain376Step(5);
        now=G_TimerQueryClock(NULL);
        T_ASSERT(now.time>=town_due && now.time<captain_due.time);
        T_NULL(defense->follow.unit);
        T_EQ(preferred->s.player,1);T_ASSERT(preferred->health.value>0);
    }
    edict_t *replacement=NULL;
    if(inverse>=5 && inverse<=7) {
        S_SetUnitPaused(preferred,true);T_ASSERT(G_UnitHasWorldPresence(preferred));
        jass_callbyname(level.vm,"preferred376Candidate",false);
        FILTER_EDICTS(e,e->inuse && e->user_data==379)replacement=e;
        T_NOT_NULL(replacement);
        if(!replacement)goto preferred376_cleanup;
        T_EQ(replacement->s.player,1);T_ASSERT(replacement->health.value>0);
        wc3Clock_t now=G_TimerQueryClock(NULL);
        T_EQ(now.epoch,town_constructed.epoch);
        float cycle=floorf(wc3_sub(now.time,town_constructed.time)/2)+1;
        float town_due=wc3_add(town_constructed.time,wc3_mul(cycle,2));
        if(town->combat_help.active) {
            T_EQ(town->combat_help.deadline.epoch,now.epoch);
            if(town->combat_help.deadline.time>=wc3_add(town_due,.05f))town_due=wc3_add(town_due,2);
        }
        while(G_TimerQueryClock(NULL).time<wc3_add(town_due,.05f))Captain376Step(5);
        T_ASSERT(!town->combat_help.active);
        if(inverse==5) {
            jass_callbyname(level.vm,"preferred376DamageB",false);
            T_EQ(preferred->user_data,381);T_ASSERT(town->combat_help.active);
            Captain376Step(600); /* After the real .5-second help suppression,
                                  before the following two-second Town owner. */
            T_ASSERT(!town->combat_help.active);
            T_ASSERT(G_TimerQueryClock(NULL).time<wc3_add(town_due,2));
        } else if(inverse==7) {
            T_ASSERT(G_TimerQueryClock(NULL).time<level.bots[0].captains[BOT_CAPTAIN_DEFENSE].update_due.time);
            jass_callbyname(level.vm,"preferred376MoveCandidate",false);
            G_BotSetCaptainHome(player,2,100,1800);
            T_NULL(level.bots[0].captains[BOT_CAPTAIN_DEFENSE].follow.unit);
        }
        jass_callbyname(level.vm,"preferred376DamageCandidate",false);
        T_EQ(replacement->user_data,380);T_ASSERT(town->health.value>0);
        if(inverse==7) {
            /* Retail accepts the new request during damage, then publishes
             * follow on the next independent Captain periodic owner visit. */
            botCaptain_t *defense=level.bots[0].captains+BOT_CAPTAIN_DEFENSE;
            wc3Clock_t next=defense->update_due;
            T_EQ(next.epoch,G_TimerQueryClock(NULL).epoch);
            T_ASSERT(next.time>G_TimerQueryClock(NULL).time);
            T_ASSERT(next.time<wc3_add(town_due,2));
            while(G_TimerQueryClock(NULL).time<next.time)Captain376Step(5);
            T_ASSERT(preferred->health.value>0 && replacement->health.value>0);
            T_EQ(defense->follow.unit,replacement);
            T_NULL(defense->target.unit);T_NULL(defense->backup.unit);
            T_EQ(ARRAY_COUNT(defense->units),0u);
            if(cold) {
                uint32_t ai=selected-g_edicts,bi=preferred-g_edicts,ci=replacement-g_edicts;
                cstring_t file=Test_TempPath("wc3-captain-preferred376-registration.bin");
                T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));remove(file);
                selected=g_edicts+ai;preferred=g_edicts+bi;replacement=g_edicts+ci;
                captain=level.bots[0].captains;defense=captain+BOT_CAPTAIN_DEFENSE;
                T_EQ(defense->follow.unit,replacement);
                T_ASSERT(preferred->inuse && preferred->health.value>0);
                T_EQ(preferred->s.player,1);
            }
            captain376_visit_owner=defense;captain376_visit_source=preferred;
            captain376_old_source_visits=captain376_old_town_visits=captain376_old_visit_count=0;
            memset(captain376_old_visit_order,0,sizeof(captain376_old_visit_order));
            target363_test_visit=Captain376RetainedVisit;
            G_SetUnitPlayer(preferred,3);target363_test_visit=NULL;
            T_EQ(preferred->s.player,3);T_EQ(captain376_old_source_visits,1u);
            T_EQ(captain376_old_town_visits,1u);T_EQ(captain376_old_visit_count,2u);
            T_EQ(captain376_old_visit_order[0],4u);
            T_EQ(captain376_old_visit_order[1],(uint32_t)TARGET_OWNER_CAPTAIN);
            T_ASSERT(G_ValidateTargetSubscriptions());
            captain376_visit_owner=NULL;captain376_visit_source=NULL;
            T_EQ(level.bots[0].captains[BOT_CAPTAIN_DEFENSE].follow.unit,replacement);
        } else T_EQ(level.bots[0].captains[BOT_CAPTAIN_DEFENSE].follow.unit,preferred);
    }
    if(inverse>=8) {
        /* Native SuicideUnitEx operates on the Town-owned pool; it does
         * not enroll the selected unit into the assault Captain. */
        S_SetUnitPaused(preferred,true);
        G_SetUnitPlayer(preferred,3);T_EQ(preferred->s.player,3);
        T_NULL(level.bots[0].captains[BOT_CAPTAIN_DEFENSE].follow.unit);
        T_EQ(ARRAY_COUNT(captain->units),6u);
        jass_t *vm=level.bots[0].vm;
        if(inverse>=15 && inverse<=17) {
            char const data[]="ID;PWXL;N;E\nC;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\nC;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Dur1\"\nC;Y1;X7;K\"HeroDur1\"\nC;Y1;X8;K\"DataA1\"\nC;Y2;X1;K\"AOwk\"\nC;Y2;X2;K\"AOwk\"\nC;Y2;X3;K1\nC;Y2;X4;K75\nC;Y2;X5;K5\nC;Y2;X6;K20\nC;Y2;X7;K20\nC;Y2;X8;K0.1\nE\n";
            query_data=parse_slk_string(data);old_query_data=G_SetSLKRows("AbilityData",query_data);
            selected->user_data=382;
            S_SetUnitPaused(selected,false);Captain376Step(20);
            T_ASSERT(!selected->paused);T_EQ(selected->targtype,TARG_GROUND);
            T_ASSERT(selected->mana.max_value>=100);
            jass_callbyname(level.vm,"preferred376QueryWindWalk",false);
            T_EQ(selected->user_data,383);
            Captain376Step(200);S_SetUnitPaused(selected,true);
            T_ASSERT(S_UnitHasTemporaryInvisibility(selected,NULL));
            T_ASSERT(S_UnitIsInvisibleToPlayer(selected,0));
            T_ASSERT(G_UnitHasWorldPresence(selected));
            if(inverse==16) {
                jass_callbyname(level.vm,"preferred376HideQuery",false);
                T_ASSERT(!G_UnitHasWorldPresence(selected));
                T_ASSERT(S_UnitHasTemporaryInvisibility(selected,NULL));
                jass_callbyname(level.vm,"preferred376Allied",false);
                FOR_LOOP(i,MAX_PLAYERS)T_ASSERT(G_PlayerTreatsPlayerAsAlly(0,i));
            }
        }
        if(inverse==9 || (inverse>=11 && inverse<=13) || inverse>=15) {
            jasscoroutine_t *prepare=jass_startcoroutinebynameforplayer(vm,"preferred376PrepareMembers",player);
            T_NOT_NULL(prepare);if(prepare)jass_resume(vm,prepare);
            /* Retail D unpauses before the immediate Stop/selection frontier.
             * The engine's existing Pause owner releases physical ownership at
             * two scheduled 5 ms boundaries; no Captain periodic may intervene. */
            wc3Clock_t captain_due=captain->update_due;
            T_ASSERT(wc3_add(G_TimerQueryClock(NULL).time,.02f)<captain_due.time);
            Captain376Step(20);
            T_EQ(captain->update_due.time,captain_due.time);
            jasscoroutine_t *stop=jass_startcoroutinebynameforplayer(vm,"preferred376StopMembers",player);
            T_NOT_NULL(stop);if(stop)jass_resume(vm,stop);
            FOR_LOOP(i,6) {
                T_EQ(members[i]->user_data,376);
                T_ASSERT(!members[i]->paused);
                T_ASSERT(!G_UnitHasActiveOrder(members[i]));
                T_EQ(G_UnitQueuedOrderCount(members[i]),0u);
            }
            T_EQ(ARRAY_COUNT(captain->units),6u);
        }
        uint32_t ids[6],queues[6],groups[6];umove_t const *moves[6];
        FOR_LOOP(i,6) {
            ids[i]=members[i]->current_order_id;queues[i]=G_UnitQueuedOrderCount(members[i]);
            groups[i]=members[i]->movement.group_id;moves[i]=members[i]->currentmove;
        }
        if(inverse==11) {
            jass_callbyname(level.vm,"preferred376Allied",false);
            FOR_LOOP(i,MAX_PLAYERS)T_ASSERT(G_PlayerTreatsPlayerAsAlly(0,i));
            jass_callbyname(level.vm,"preferred376ObserveSuicideOrders",false);
        }
        if(inverse==17) {
            jass_callbyname(level.vm,"preferred376RemoveQueryAttack",false);
            FOR_LOOP(i,6)T_ASSERT(!G_ActorHasAbilityCode(members[i],MAKEFOURCC('A','a','t','k')));
        }
        if(inverse==12 || inverse==13)jass_callbyname(level.vm,inverse==12 ? "preferred376RemoveImmediate" : "preferred376RemovePoint",false);
        if(inverse==18 || inverse==19)jass_callbyname(level.vm,inverse==18 ? "preferred376ReplaceImmediate" : "preferred376ReplacePoint",false);
        captain376_point_source=NULL;captain376_point_calls=0;
        move_test_point_approach=Captain376PointApproach;
        jasscoroutine_t *command=jass_startcoroutinebynameforplayer(vm,
            (inverse==8 || inverse==14) ? "preferred376SuicideTown" : inverse==11 ? "preferred376SuicideNoTarget" : "preferred376SuicideMember",player);
        T_NOT_NULL(command);if(command)jass_resume(vm,command);
        move_test_point_approach=NULL;
        T_ASSERT(town->inuse && town->health.value>0 && G_UnitHasWorldPresence(town));
        if(inverse==10) {
            /* The accepted diagnostic B shows action8/held members are
             * ineligible: no d000e admission, role or queue mutation. */
            T_EQ(ARRAY_COUNT(captain->units),6u);
            FOR_LOOP(i,6) {
                T_ASSERT(members[i]->paused);
                T_EQ(members[i]->current_order_id,ids[i]);
                T_EQ(G_UnitQueuedOrderCount(members[i]),queues[i]);
                T_EQ(members[i]->movement.group_id,groups[i]);
                T_EQ(members[i]->currentmove,moves[i]);
            }
        } else {
            T_EQ(ARRAY_COUNT(captain->units),(inverse==9 || (inverse>=11 && inverse<=13) || inverse>=15) ? 5u : 6u);
            uint32_t detached=0;
            FOR_LOOP(i,6)if(members[i]->movement.captain_home.roster_actor!=captain->home_actor)detached++;
            T_EQ(detached,(inverse==9 || (inverse>=11 && inverse<=13) || inverse>=15) ? 1u : 0u);
        }
        if(inverse==18 || inverse==19) {
            T_EQ(town->user_data,inverse==18 ? 914 : 915);
            edict_t *detached=NULL;
            FOR_LOOP(i,6)if(members[i]->movement.captain_home.roster_actor!=captain->home_actor)detached=members[i];
            T_NOT_NULL(detached);
            if(detached) {
                T_ASSERT(detached->inuse && !G_IsDeferredFree(detached));
                T_EQ(detached->current_order_id,851986u);
                T_NOT_NULL(detached->currentmove);
                if(detached->currentmove)T_EQ(detached->currentmove->proc,CAbilityMove);
                T_EQ(G_UnitQueuedOrderCount(detached),0u);
                T_NULL(detached->movement.attackmove_waypoint);
                vec2_t point={0,0};T_ASSERT(G_GetIssuedOrderPoint(detached,&point));
                T_EQ(wc3_float_bits(point.x),wc3_float_bits(1600.0f));
                T_EQ(wc3_float_bits(point.y),wc3_float_bits(1700.0f));
                moveGroup_t *group=move_unit_group(detached);T_NOT_NULL(group);
                if(group) {
                    vec2_t expected=move_point_fine(&(vec2_t){1600,1700});
                    T_EQ(group->count,1u);T_NULL(group->target);
                    T_EQ(wc3_float_bits(group->goal.x),wc3_float_bits(expected.x));
                    T_EQ(wc3_float_bits(group->goal.y),wc3_float_bits(expected.y));
                }
            }
            T_ASSERT(G_ValidateTargetSubscriptions());
            goto preferred376_cleanup;
        }
        if(inverse==12 || inverse==13) {
            T_EQ(town->user_data,inverse==12 ? 912 : 913);
            edict_t *detached=NULL;
            FOR_LOOP(i,6)if(members[i]->movement.captain_home.roster_actor!=captain->home_actor)detached=members[i];
            T_NOT_NULL(detached);
            if(detached) {
                T_ASSERT(detached->inuse && G_IsDeferredFree(detached));
                T_EQ(detached->current_order_id,0u);T_EQ(detached->movement.group_id,0u);
                T_NULL(detached->currentmove);T_EQ(G_UnitQueuedOrderCount(detached),0u);
                uint32_t slot=detached-g_edicts,birth=detached->spawn_time;
                cstring_t pending_file=Test_TempPath("wc3-captain376-removed-pending.bin");
                T_ASSERT(WriteGame(pending_file));T_ASSERT(ReadGame(pending_file));remove(pending_file);
                detached=g_edicts+slot;captain=level.bots[0].captains;
                T_ASSERT(detached->inuse && detached->spawn_time==birth && G_IsDeferredFree(detached));
                T_EQ(detached->current_order_id,0u);T_EQ(detached->movement.group_id,0u);
                T_NULL(detached->currentmove);T_EQ(G_UnitQueuedOrderCount(detached),0u);
                Captain376Step(5); /* Actual deferred storage owner releases the cache. */
                T_ASSERT(!detached->inuse);T_NULL(detached->movement.attackmove_waypoint);
            }
            T_ASSERT(G_ValidateTargetSubscriptions());
            goto preferred376_cleanup;
        }
        if(inverse==16) {
            edict_t *detached=NULL;
            FOR_LOOP(i,6)if(members[i]->movement.captain_home.roster_actor!=captain->home_actor)detached=members[i];
            T_NOT_NULL(detached);
            if(detached) {
                T_EQ(detached->current_order_id,0u);T_EQ(G_UnitQueuedOrderCount(detached),0u);
                T_EQ(detached->movement.group_id,0u);T_NULL(detached->movement.attackmove_waypoint);
            }
            goto preferred376_cleanup;
        }
        if(inverse==11) {
            edict_t *detached=NULL;
            FOR_LOOP(i,6)if(members[i]->movement.captain_home.roster_actor!=captain->home_actor)detached=members[i];
            T_NOT_NULL(detached);
            if(detached) {
                /* Initial Stop acceptance marker376, then d000e event12,
                 * then ordinary Stop event2: no synthetic notification. */
                T_EQ(detached->user_data,37722);
                T_EQ(detached->current_order_id,0u);T_ASSERT(!G_UnitHasActiveOrder(detached));
                T_EQ(G_UnitQueuedOrderCount(detached),0u);T_EQ(detached->movement.group_id,0u);
                T_NULL(detached->movement.attackmove_waypoint);
            }
            goto preferred376_cleanup;
        }
        if(inverse==9 || inverse==15)Captain376AssertSuicidePoint(captain,members,selected);
        if(inverse==17)Captain376AssertSuicideMove(captain,members,selected);
        if(cold) {
            uint32_t a=selected-g_edicts,b=preferred-g_edicts,t=town-g_edicts,mi[6];
            FOR_LOOP(i,6)mi[i]=members[i]-g_edicts;
            cstring_t file=Test_TempPath("wc3-captain376-suicide.bin");
            T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));remove(file);
            selected=g_edicts+a;preferred=g_edicts+b;town=g_edicts+t;captain=level.bots[0].captains;
            FOR_LOOP(i,6)members[i]=g_edicts+mi[i];
            T_EQ(ARRAY_COUNT(captain->units),(inverse==9 || (inverse>=11 && inverse<=13) || inverse>=15) ? 5u : 6u);
            T_ASSERT(town->inuse && town->health.value>0);
            if(inverse==9 || inverse==15)Captain376AssertSuicidePoint(captain,members,selected);
        if(inverse==17)Captain376AssertSuicideMove(captain,members,selected);
        }
        if(inverse==8 || inverse==14) {
            /* New damage is real, hostile and past both suppression and
             * the next independent Town rearm; the helper's AI-record bit
             * must exclude the recipient before consuming that latch. */
            G_SetUnitPlayer(preferred,1);T_EQ(preferred->s.player,1);
            wc3Clock_t now=G_TimerQueryClock(NULL);
            T_EQ(now.epoch,town_constructed.epoch);
            float cycle=floorf(wc3_sub(now.time,town_constructed.time)/2)+1;
            float due=wc3_add(town_constructed.time,wc3_mul(cycle,2));
            if(town->combat_help.active && town->combat_help.deadline.time>=wc3_add(due,.05f))due=wc3_add(due,2);
            while(G_TimerQueryClock(NULL).time<wc3_add(due,.05f))Captain376Step(5);
            T_ASSERT(!town->combat_help.active);
            float health=town->health.value;
            jass_callbyname(level.vm,"preferred376DamageB",false);
            T_EQ(preferred->user_data,381);T_ASSERT(town->health.value<health);
            wc3Clock_t next=level.bots[0].captains[BOT_CAPTAIN_DEFENSE].update_due;
            T_EQ(next.epoch,G_TimerQueryClock(NULL).epoch);
            while(G_TimerQueryClock(NULL).time<next.time)Captain376Step(5);
            T_NULL(level.bots[0].captains[BOT_CAPTAIN_DEFENSE].follow.unit);
            if(inverse==14) {
                /* Keep the preceding positive Suicide exclusion intact. The
                 * public owner round trip retires the actual owned AI record,
                 * not the edict. Returning creates a fresh default record. */
                T_EQ(town->ai_town_player,1u);
                uint32_t const birth=town->spawn_time;
                G_SetUnitPlayer(town,3);
                T_EQ(town->s.player,3u);T_EQ(town->ai_town_player,0u);
                G_SetUnitPlayer(town,0);
                T_EQ(town->s.player,0u);T_EQ(town->ai_town_player,1u);
                T_EQ(town->spawn_time,birth);T_ASSERT(town->health.value>0);
                T_ASSERT(G_ValidateTargetSubscriptions());
                now=G_TimerQueryClock(NULL);
                T_EQ(now.epoch,town_constructed.epoch);
                cycle=floorf(wc3_sub(now.time,town_constructed.time)/2)+1;
                due=wc3_add(town_constructed.time,wc3_mul(cycle,2));
                if(town->combat_help.active && town->combat_help.deadline.time>=wc3_add(due,.05f))due=wc3_add(due,2);
                while(G_TimerQueryClock(NULL).time<wc3_add(due,.05f))Captain376Step(5);
                T_ASSERT(!town->combat_help.active);
                health=town->health.value;
                jass_callbyname(level.vm,"preferred376DamageB",false);
                T_EQ(preferred->user_data,381);T_ASSERT(town->health.value<health);
                next=level.bots[0].captains[BOT_CAPTAIN_DEFENSE].update_due;
                T_EQ(next.epoch,G_TimerQueryClock(NULL).epoch);
                while(G_TimerQueryClock(NULL).time<next.time)Captain376Step(5);
                T_EQ(level.bots[0].captains[BOT_CAPTAIN_DEFENSE].follow.unit,preferred);
            }
        }
    }
    if(cold && inverse<7) {
        uint32_t selected_index=selected-g_edicts,preferred_index=preferred-g_edicts;
        cstring_t file=Test_TempPath("wc3-captain-preferred376.bin");
        T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));remove(file);
        captain=level.bots[0].captains;
        selected=g_edicts+selected_index;preferred=g_edicts+preferred_index;
        T_EQ(captain->target.unit,selected);T_EQ(captain->follow.unit,selected);
        T_ASSERT(preferred->inuse && preferred->health.value>0);
    }
    vec2_t point=inverse==7 ? replacement->s.origin2 :
        (inverse==1 || inverse==3 || inverse==4 || (inverse>=8 && inverse<=13) || inverse>=15) ? selected->s.origin2 : preferred->s.origin2;
    T_ASSERT(wc3_float_bits(preferred->s.origin2.x)!=wc3_float_bits(selected->s.origin2.x) ||
             wc3_float_bits(preferred->s.origin2.y)!=wc3_float_bits(selected->s.origin2.y));
    uint32_t count=ARRAY_COUNT(captain->units);T_EQ(count,(inverse==9 || (inverse>=11 && inverse<=13) || inverse>=15) ? 5u : 6u);
    G_BotClearCaptainTargets(player);
    T_NULL(captain->target.unit);T_NULL(captain->follow.unit);T_NULL(captain->backup.unit);
    T_EQ(ARRAY_COUNT(captain->units),count);
    T_EQ(wc3_float_bits(captain->goal.x),wc3_float_bits(point.x));
    T_EQ(wc3_float_bits(captain->goal.y),wc3_float_bits(point.y));
    T_EQ(wc3_float_bits(captain->active_goal.x),wc3_float_bits(point.x));
    T_EQ(wc3_float_bits(captain->active_goal.y),wc3_float_bits(point.y));
    T_EQ(wc3_float_bits(captain->request_range),wc3_float_bits(200));
preferred376_cleanup:
    move_test_point_approach=NULL;
    G_BotStop(0);level.started=false;reset_entities();
    if(query_data){G_SetSLKRows("AbilityData",old_query_data);free_slk_rows(query_data);}
    G_SetSLKRows("UnitData",old_data);free_slk_rows(data);
    G_SetSLKRows("UnitWeapons",old_weapons);free_slk_rows(weapons);
    setup_test_world();
}

TEST(wc3_bot, captain_preferred376_public_town_attack_changes_clear_goal) {
    Captain376PublicTownClear(false,0);
}
TEST(wc3_bot, captain_preferred376_public_town_attack_saved_clear_goal) {
    Captain376PublicTownClear(true,0);
}

TEST(wc3_bot, captain_preferred376_campaign_clear_uses_selected) {Captain376PublicTownClear(false,1);}
TEST(wc3_bot, captain_preferred376_melee_inverse_restores_town_goal) {Captain376PublicTownClear(false,2);}
TEST(wc3_bot, captain_preferred376_living_owner_withdrawal_restores_selected) {Captain376PublicTownClear(false,3);}
TEST(wc3_bot, captain_preferred376_defense_home_maintenance_restores_selected) {Captain376PublicTownClear(false,4);}

TEST(wc3_bot, captain_preferred376_consumed_latch_rejects_new_source) {Captain376PublicTownClear(false,5);}
TEST(wc3_bot, captain_preferred376_farther_source_does_not_replace) {Captain376PublicTownClear(false,6);}
TEST(wc3_bot, captain_preferred376_closer_source_replaces_and_ignores_old_owner) {Captain376PublicTownClear(false,7);}

TEST(wc3_bot, captain_preferred376_saved_replacement_retains_old_registration) {Captain376PublicTownClear(true,7);}

TEST(wc3_bot,captain_preferred376_suicide_town_excludes_help) {Captain376PublicTownClear(false,8);}
TEST(wc3_bot,captain_preferred376_saved_suicide_town_excludes_help) {Captain376PublicTownClear(true,8);}
TEST(wc3_bot,captain_preferred376_suicide_detaches_one_idle_member) {Captain376PublicTownClear(false,9);}
TEST(wc3_bot,captain_preferred376_saved_suicide_detaches_one_idle_member) {Captain376PublicTownClear(true,9);}
TEST(wc3_bot,captain_preferred376_suicide_rejects_paused_members_without_orders) {Captain376PublicTownClear(false,10);}

TEST(wc3_bot,captain_preferred376_suicide_no_target_publishes_ordinary_stop) {Captain376PublicTownClear(false,11);}

TEST(wc3_bot,captain_preferred376_suicide_immediate_callback_removes_source) {Captain376PublicTownClear(false,12);}
TEST(wc3_bot,captain_preferred376_suicide_point_callback_removes_source) {Captain376PublicTownClear(false,13);}

TEST(wc3_bot,captain_preferred376_suicide_record_owner_roundtrip_restores_help) {Captain376PublicTownClear(false,14);}
TEST(wc3_bot,captain_preferred376_saved_suicide_record_owner_roundtrip_restores_help) {Captain376PublicTownClear(true,14);}

TEST(wc3_bot,captain_preferred376_suicide_query_selects_undetected_windwalk) {Captain376PublicTownClear(false,15);}
TEST(wc3_bot,captain_preferred376_suicide_query_excludes_script_hidden_windwalk) {Captain376PublicTownClear(false,16);}
TEST(wc3_bot,captain_preferred376_suicide_query_without_attack_owner_selects_windwalk) {Captain376PublicTownClear(false,17);}

TEST(wc3_bot,captain_preferred376_suicide_immediate_callback_move_wins) {Captain376PublicTownClear(false,18);}
TEST(wc3_bot,captain_preferred376_suicide_point_callback_move_wins) {Captain376PublicTownClear(false,19);}

/* Public worker admission cases: native Peasant gold approach is action 5;
 * native Acolyte haunted-mine approach is action 6. Minimal MPQ data is
 * authored before actual CreateUnit; no current move/AI state is seeded. */
static void Captain376WorkerSuicide(unsigned policy) {
    Captain376World();
    slkTestData_t *rows[6],*old[6];
    rows[0]=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"unitID\"\nC;X2;Y1;K\"movetp\"\nC;X3;Y1;K\"targType\"\nC;X4;Y1;K\"turnRate\"\nC;X5;Y1;K\"propWin\"\nC;X6;Y1;K\"valid\"\nC;X7;Y1;K\"deathType\"\nC;X1;Y2;K\"hpea\"\nC;X2;Y2;K\"foot\"\nC;X3;Y2;K\"ground\"\nC;X4;Y2;K0.6\nC;X5;Y2;K60\nC;X6;Y2;K1\nC;X7;Y2;K3\nC;X1;Y3;K\"uaco\"\nC;X2;Y3;K\"foot\"\nC;X3;Y3;K\"ground\"\nC;X4;Y3;K0.6\nC;X5;Y3;K60\nC;X6;Y3;K1\nC;X7;Y3;K3\nC;X1;Y4;K\"hfoo\"\nC;X2;Y4;K\"foot\"\nC;X3;Y4;K\"ground\"\nC;X4;Y4;K0.6\nC;X5;Y4;K60\nC;X6;Y4;K1\nC;X7;Y4;K3\nC;X1;Y5;K\"ngol\"\nC;X2;Y5;K\"none\"\nC;X3;Y5;K\"structure\"\nC;X4;Y5;K0.6\nC;X5;Y5;K60\nC;X6;Y5;K1\nC;X7;Y5;K3\nC;X1;Y6;K\"ugol\"\nC;X2;Y6;K\"none\"\nC;X3;Y6;K\"structure\"\nC;X4;Y6;K0.6\nC;X5;Y6;K60\nC;X6;Y6;K1\nC;X7;Y6;K3\nE\n");old[0]=G_SetSLKRows("UnitData",rows[0]);
    rows[1]=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"unitBalanceID\"\nC;X2;Y1;K\"realHP\"\nC;X3;Y1;K\"spd\"\nC;X4;Y1;K\"type\"\nC;X1;Y2;K\"hpea\"\nC;X2;Y2;K250\nC;X3;Y2;K270\nC;X4;Y2;K\"peon\"\nC;X1;Y3;K\"uaco\"\nC;X2;Y3;K220\nC;X3;Y3;K220\nC;X4;Y3;K\"peon\"\nC;X1;Y4;K\"hfoo\"\nC;X2;Y4;K420\nC;X3;Y4;K270\nC;X4;Y4;K\"\"\nC;X1;Y5;K\"ngol\"\nC;X2;Y5;K12500\nC;X3;Y5;K0\nC;X4;Y5;K\"mechanical\"\nC;X1;Y6;K\"ugol\"\nC;X2;Y6;K950\nC;X3;Y6;K0\nC;X4;Y6;K\"mechanical\"\nE\n");old[1]=G_SetSLKRows("UnitBalance",rows[1]);
    rows[2]=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"unitWeapID\"\nC;X2;Y1;K\"weapsOn\"\nC;X3;Y1;K\"acquire\"\nC;X4;Y1;K\"weapType1\"\nC;X5;Y1;K\"weapTp1\"\nC;X6;Y1;K\"atkType1\"\nC;X7;Y1;K\"targs1\"\nC;X8;Y1;K\"rangeN1\"\nC;X9;Y1;K\"dmgpt1\"\nC;X10;Y1;K\"cool1\"\nC;X11;Y1;K\"dice1\"\nC;X12;Y1;K\"sides1\"\nC;X13;Y1;K\"dmgplus1\"\nC;X1;Y2;K\"hpea\"\nC;X2;Y2;K1\nC;X3;Y2;K500\nC;X4;Y2;K\"normal\"\nC;X5;Y2;K\"normal\"\nC;X6;Y2;K\"normal\"\nC;X7;Y2;K\"ground,structure,debris,item,ward\"\nC;X8;Y2;K100\nC;X9;Y2;K0.3\nC;X10;Y2;K2\nC;X11;Y2;K1\nC;X12;Y2;K2\nC;X13;Y2;K5\nC;X1;Y3;K\"uaco\"\nC;X2;Y3;K1\nC;X3;Y3;K500\nC;X4;Y3;K\"normal\"\nC;X5;Y3;K\"normal\"\nC;X6;Y3;K\"normal\"\nC;X7;Y3;K\"ground,structure,debris,item,ward\"\nC;X8;Y3;K100\nC;X9;Y3;K0.3\nC;X10;Y3;K2\nC;X11;Y3;K1\nC;X12;Y3;K2\nC;X13;Y3;K5\nC;X1;Y4;K\"hfoo\"\nC;X2;Y4;K1\nC;X3;Y4;K500\nC;X4;Y4;K\"normal\"\nC;X5;Y4;K\"normal\"\nC;X6;Y4;K\"normal\"\nC;X7;Y4;K\"ground,structure,debris,item,ward\"\nC;X8;Y4;K100\nC;X9;Y4;K0.3\nC;X10;Y4;K2\nC;X11;Y4;K1\nC;X12;Y4;K2\nC;X13;Y4;K5\nE\n");old[2]=G_SetSLKRows("UnitWeapons",rows[2]);
    rows[3]=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"unitAbilID\"\nC;X2;Y1;K\"abilList\"\nC;X1;Y2;K\"hpea\"\nC;X2;Y2;K\"Ahar\"\nC;X1;Y3;K\"uaco\"\nC;X2;Y3;K\"Aaha\"\nC;X1;Y4;K\"hfoo\"\nC;X2;Y4;K\"\"\nC;X1;Y5;K\"ngol\"\nC;X2;Y5;K\"Agld\"\nC;X1;Y6;K\"ugol\"\nC;X2;Y6;K\"Abgm\"\nE\n");old[3]=G_SetSLKRows("UnitAbilities",rows[3]);
    rows[4]=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"unitUIID\"\nC;X2;Y1;K\"file\"\nC;X3;Y1;K\"modelScale\"\nC;X4;Y1;K\"scale\"\nC;X1;Y2;K\"hpea\"\nC;X2;Y2;K\"Units\\\\Creeps\\\\Medivh\\\\Medivh.mdx\"\nC;X3;Y2;K1\nC;X4;Y2;K1\nC;X1;Y3;K\"uaco\"\nC;X2;Y3;K\"Units\\\\Creeps\\\\Medivh\\\\Medivh.mdx\"\nC;X3;Y3;K1\nC;X4;Y3;K1\nC;X1;Y4;K\"hfoo\"\nC;X2;Y4;K\"Units\\\\Creeps\\\\Medivh\\\\Medivh.mdx\"\nC;X3;Y4;K1\nC;X4;Y4;K1\nC;X1;Y5;K\"ngol\"\nC;X2;Y5;K\"Units\\\\Creeps\\\\Medivh\\\\Medivh.mdx\"\nC;X3;Y5;K1\nC;X4;Y5;K1\nC;X1;Y6;K\"ugol\"\nC;X2;Y6;K\"Units\\\\Creeps\\\\Medivh\\\\Medivh.mdx\"\nC;X3;Y6;K1\nC;X4;Y6;K1\nE\n");old[4]=G_SetSLKRows("UnitUI",rows[4]);
    rows[5]=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;Y1;K\"code\"\nC;X3;Y1;K\"levels\"\nC;X4;Y1;K\"Rng1\"\nC;X5;Y1;K\"DataA1\"\nC;X6;Y1;K\"DataB1\"\nC;X7;Y1;K\"DataC1\"\nC;X8;Y1;K\"DataD1\"\nC;X9;Y1;K\"Dur1\"\nC;X1;Y2;K\"Ahar\"\nC;X2;Y2;K\"Ahar\"\nC;X3;Y2;K1\nC;X4;Y2;K64\nC;X5;Y2;K10\nC;X6;Y2;K1\nC;X7;Y2;K0\nC;X8;Y2;K0\nC;X9;Y2;K1\nC;X1;Y3;K\"Aaha\"\nC;X2;Y3;K\"Aaha\"\nC;X3;Y3;K1\nC;X4;Y3;K64\nC;X5;Y3;K0\nC;X6;Y3;K0\nC;X7;Y3;K0\nC;X8;Y3;K0\nC;X9;Y3;K0\nC;X1;Y4;K\"Agld\"\nC;X2;Y4;K\"Agld\"\nC;X3;Y4;K1\nC;X4;Y4;K0\nC;X5;Y4;K12500\nC;X6;Y4;K1\nC;X7;Y4;K5\nC;X8;Y4;K0\nC;X9;Y4;K0\nC;X1;Y5;K\"Abgm\"\nC;X2;Y5;K\"Abgm\"\nC;X3;Y5;K1\nC;X4;Y5;K0\nC;X5;Y5;K10\nC;X6;Y5;K1\nC;X7;Y5;K5\nC;X8;Y5;K200\nC;X9;Y5;K0\nE\n");old[5]=G_SetSLKRows("AbilityData",rows[5]);
    player_t *player=&game.clients[0].ps;
    ((mapInfo_t *)level.mapinfo)->players[1].used=true;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType=kPlayerTypeHuman;
    game.clients[1].mapplayer=level.mapinfo->players+1;
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_MELEE));
    char script[2048];
    int n=snprintf(script,sizeof(script),
        "globals\nunit worker376Source\nunit worker376Mine\nunit worker376Enemy\nendglobals\n"
        "function main takes nothing returns nothing\n"
        "set worker376Mine=CreateUnit(Player(15),'ngol',1024.,1024.,0.)\n"
        "call SetResourceAmount(worker376Mine,10000)\n%s"
        "set worker376Enemy=CreateUnit(Player(1),'hfoo',1600.,1400.,0.)\ncall PauseUnit(worker376Enemy,true)\n"
        "set worker376Source=CreateUnit(Player(0),'%s',512.,1024.,0.)\n"
        "call SetUnitUserData(worker376Source,375)\n"
        "if IssueTargetOrder(worker376Source,\"harvest\",worker376Mine) then\ncall SetUnitUserData(worker376Source,376)\nendif\nendfunction\n",
        policy==1 ? "set worker376Mine=CreateBlightedGoldmine(Player(0),1024.,1024.,0.)\n" : "",
        policy==1 ? "uaco" : "hpea");
    T_ASSERT(n>0 && (size_t)n<sizeof(script));T_ASSERT(run_test_jass(script));
    edict_t *worker=NULL,*enemy=NULL;
    FILTER_EDICTS(unit,unit->inuse) {
        if(unit->user_data==375 || unit->user_data==376)worker=unit;
        if(unit->s.player==1 && unit->class_id==MAKEFOURCC('h','f','o','o'))enemy=unit;
    }
    T_NOT_NULL(worker);T_NOT_NULL(enemy);if(!worker || !enemy)goto cleanup376worker;
    T_ASSERT(worker->health.max_value>0 && worker->health.value>0);T_EQ(worker->targtype,TARG_GROUND);
    T_EQ(worker->user_data,376);T_EQ(worker->ai_town_player,1u);
    T_ASSERT(G_ActorHasAbilityCode(worker,MAKEFOURCC('A','m','o','v')));
    T_ASSERT(G_ActorHasAbilityCode(worker,MAKEFOURCC('A','a','t','k')));
    T_NOT_NULL(worker->currentmove);
    if(worker->currentmove)T_EQ(worker->currentmove->proc,policy==1 ? CAbilityAcolyteHarvest : CAbilityGoldMine);
    T_EQ(ARRAY_COUNT(level.bots[0].captains[0].units),0u);
    uint32_t old_order=worker->current_order_id;
    if(policy==2){S_SetUnitPaused(worker,true);T_ASSERT(worker->paused);T_EQ(worker->movement.pause_phase,UNIT_PAUSE_SUSPENDED);T_ASSERT(S_UnitOrderExecutionHeld(worker));old_order=worker->current_order_id;}
    char command[160];
    n=snprintf(command,sizeof(command),"function worker376Suicide takes nothing returns nothing\ncall SuicideUnitEx(1,'%s',1)\nendfunction\n",policy==1 ? "uaco" : "hpea");
    T_ASSERT(n>0 && (size_t)n<sizeof(command));T_ASSERT(jass_dobuffer(level.bots[0].vm,command));
    jasscoroutine_t *call=jass_startcoroutinebynameforplayer(level.bots[0].vm,"worker376Suicide",player);
    T_NOT_NULL(call);if(call)jass_resume(level.bots[0].vm,call);
    T_EQ(ARRAY_COUNT(level.bots[0].captains[0].units),0u);
    if(policy==2) {
        T_ASSERT(worker->paused);T_EQ(worker->current_order_id,old_order);
    } else {
        T_EQ(worker->current_order_id,851983u);T_NOT_NULL(worker->currentmove);
        if(worker->currentmove)T_EQ(worker->currentmove->proc,CAbilityAttack);
        T_NOT_NULL(worker->movement.attackmove_waypoint);
        if(worker->movement.attackmove_waypoint) {
            T_EQ(wc3_float_bits(worker->movement.attackmove_waypoint->s.origin2.x),wc3_float_bits(enemy->s.origin2.x));
            T_EQ(wc3_float_bits(worker->movement.attackmove_waypoint->s.origin2.y),wc3_float_bits(enemy->s.origin2.y));
        }
        moveGroup_t *group=move_unit_group(worker);T_NOT_NULL(group);
        if(group){T_EQ(group->count,1u);T_EQ(group->receiver,worker);T_EQ(group->owner_ability,GetAbilityIndex(CAbilityAttack)+1);}
    }
cleanup376worker:
    G_BotStop(0);level.started=false;reset_entities();
    G_SetSLKRows("UnitData",old[0]);free_slk_rows(rows[0]);
    G_SetSLKRows("UnitBalance",old[1]);free_slk_rows(rows[1]);
    G_SetSLKRows("UnitWeapons",old[2]);free_slk_rows(rows[2]);
    G_SetSLKRows("UnitAbilities",old[3]);free_slk_rows(rows[3]);
    G_SetSLKRows("UnitUI",old[4]);free_slk_rows(rows[4]);
    G_SetSLKRows("AbilityData",old[5]);free_slk_rows(rows[5]);
    setup_test_world();
}
TEST(wc3_bot,captain_preferred376_suicide_worker_gold_approach) {Captain376WorkerSuicide(0);}
TEST(wc3_bot,captain_preferred376_suicide_worker_acolyte_approach) {Captain376WorkerSuicide(1);}
TEST(wc3_bot,captain_preferred376_suicide_worker_pause_rejects_admission) {Captain376WorkerSuicide(2);}

/* AA retail matrix: the public classification mutation controls the actual
 * SuicideUnitEx target query. A singleton requested-player candidate excludes
 * random selection; outgoing alliances exclude all fallback candidates. */
static void Captain376SuicideClass(unsigned policy) {
    Captain376World();
    slkTestData_t *rows[5],*old[5];
    rows[0]=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"unitID\"\nC;X2;Y1;K\"movetp\"\nC;X3;Y1;K\"targType\"\nC;X4;Y1;K\"turnRate\"\nC;X5;Y1;K\"propWin\"\nC;X6;Y1;K\"valid\"\nC;X7;Y1;K\"deathType\"\nC;X1;Y2;K\"hfoo\"\nC;X2;Y2;K\"foot\"\nC;X3;Y2;K\"ground\"\nC;X4;Y2;K0.6\nC;X5;Y2;K60\nC;X6;Y2;K1\nC;X7;Y2;K3\nC;X1;Y3;K\"Hpal\"\nC;X2;Y3;K\"foot\"\nC;X3;Y3;K\"ground\"\nC;X4;Y3;K0.6\nC;X5;Y3;K60\nC;X6;Y3;K1\nC;X7;Y3;K3\nE\n");old[0]=G_SetSLKRows("UnitData",rows[0]);
    rows[1]=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"unitBalanceID\"\nC;X2;Y1;K\"realHP\"\nC;X3;Y1;K\"spd\"\nC;X4;Y1;K\"type\"\nC;X1;Y2;K\"hfoo\"\nC;X2;Y2;K420\nC;X3;Y2;K270\nC;X4;Y2;K\"\"\nC;X1;Y3;K\"Hpal\"\nC;X2;Y3;K1000\nC;X3;Y3;K270\nC;X4;Y3;K\"\"\nE\n");old[1]=G_SetSLKRows("UnitBalance",rows[1]);
    rows[2]=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"unitWeapID\"\nC;X2;Y1;K\"weapsOn\"\nC;X3;Y1;K\"acquire\"\nC;X4;Y1;K\"weapType1\"\nC;X5;Y1;K\"weapTp1\"\nC;X6;Y1;K\"atkType1\"\nC;X7;Y1;K\"targs1\"\nC;X8;Y1;K\"rangeN1\"\nC;X9;Y1;K\"dmgpt1\"\nC;X10;Y1;K\"cool1\"\nC;X11;Y1;K\"dice1\"\nC;X12;Y1;K\"sides1\"\nC;X13;Y1;K\"dmgplus1\"\nC;X1;Y2;K\"hfoo\"\nC;X2;Y2;K1\nC;X3;Y2;K500\nC;X4;Y2;K\"normal\"\nC;X5;Y2;K\"normal\"\nC;X6;Y2;K\"normal\"\nC;X7;Y2;K\"ground,organic,nonsapper,nonancient\"\nC;X8;Y2;K100\nC;X9;Y2;K0.3\nC;X10;Y2;K2\nC;X11;Y2;K1\nC;X12;Y2;K2\nC;X13;Y2;K5\nE\n");old[2]=G_SetSLKRows("UnitWeapons",rows[2]);
    rows[3]=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"unitAbilID\"\nC;X2;Y1;K\"abilList\"\nC;X1;Y2;K\"hfoo\"\nC;X2;Y2;K\"\"\nC;X1;Y3;K\"Hpal\"\nC;X2;Y3;K\"\"\nE\n");old[3]=G_SetSLKRows("UnitAbilities",rows[3]);
    rows[4]=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"unitUIID\"\nC;X2;Y1;K\"file\"\nC;X3;Y1;K\"modelScale\"\nC;X4;Y1;K\"scale\"\nC;X1;Y2;K\"hfoo\"\nC;X2;Y2;K\"Units\\\\Creeps\\\\Medivh\\\\Medivh.mdx\"\nC;X3;Y2;K1\nC;X4;Y2;K1\nC;X1;Y3;K\"Hpal\"\nC;X2;Y3;K\"Units\\\\Creeps\\\\Medivh\\\\Medivh.mdx\"\nC;X3;Y3;K1\nC;X4;Y3;K1\nE\n");old[4]=G_SetSLKRows("UnitUI",rows[4]);
    player_t *player=&game.clients[0].ps;
    ((mapInfo_t *)level.mapinfo)->players[1].used=true;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType=kPlayerTypeHuman;
    game.clients[1].mapplayer=level.mapinfo->players+1;
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_MELEE));
    unsigned type=(unsigned[]){15,17,19}[policy/2];bool remove=(policy&1)!=0;
    char script[2048];
    int n=snprintf(script,sizeof(script),
        "globals\nunit class376Source\nunit class376Target\nendglobals\n"
        "function main takes nothing returns nothing\nlocal integer i=0\n"
        "loop\nexitwhen i==16\ncall SetPlayerAlliance(Player(0),Player(i),ALLIANCE_PASSIVE,true)\nset i=i+1\nendloop\n"
        "set class376Target=CreateUnit(Player(1),'Hpal',1600.,1400.,0.)\ncall PauseUnit(class376Target,true)\n"
        "set class376Source=CreateUnit(Player(0),'hfoo',512.,1024.,0.)\n"
        "if IssueImmediateOrder(class376Source,\"stop\") then\ncall SetUnitUserData(class376Source,376)\nendif\n"
        "if UnitAddType(class376Target,ConvertUnitType(%u)) then\ncall SetUnitUserData(class376Target,384)\nendif\n%s"
        "endfunction\n",type,remove ? (type==15 ? "if UnitRemoveType(class376Target,ConvertUnitType(15)) then\ncall SetUnitUserData(class376Target,385)\nendif\n" : type==17 ? "if UnitRemoveType(class376Target,ConvertUnitType(17)) then\ncall SetUnitUserData(class376Target,385)\nendif\n" : "if UnitRemoveType(class376Target,ConvertUnitType(19)) then\ncall SetUnitUserData(class376Target,385)\nendif\n") : "");
    T_ASSERT(n>0 && (size_t)n<sizeof(script));T_ASSERT(run_test_jass(script));
    edict_t *source=NULL,*target=NULL;
    FILTER_EDICTS(unit,unit->inuse) {
        if(unit->class_id==MAKEFOURCC('h','f','o','o'))source=unit;
        if(unit->class_id==MAKEFOURCC('H','p','a','l'))target=unit;
    }
    T_NOT_NULL(source);T_NOT_NULL(target);if(!source || !target)goto cleanup376class;
    T_EQ(source->user_data,376);T_EQ(source->ai_town_player,1u);
    T_ASSERT(source->health.value>0 && target->health.value>0);
    T_ASSERT(G_ActorHasAbilityCode(source,MAKEFOURCC('A','m','o','v')));
    T_ASSERT(G_ActorHasAbilityCode(source,MAKEFOURCC('A','a','t','k')));
    T_EQ(S_AttackProfileRead(source,0)->targetsAllowed,0x54000002u);
    T_ASSERT(!G_UnitHasActiveOrder(source));T_EQ(G_UnitQueuedOrderCount(source),0u);
    T_ASSERT(G_UnitHasWorldPresence(target));T_EQ(target->targtype,TARG_GROUND);
    T_EQ(target->user_data,remove ? 385 : 384);
    FOR_LOOP(i,MAX_PLAYERS)T_ASSERT(G_PlayerTreatsPlayerAsAlly(0,i));
    char command[]="function class376Suicide takes nothing returns nothing\ncall SuicideUnitEx(1,'hfoo',1)\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.bots[0].vm,command));
    jasscoroutine_t *call=jass_startcoroutinebynameforplayer(level.bots[0].vm,"class376Suicide",player);
    T_NOT_NULL(call);if(call)jass_resume(level.bots[0].vm,call);
    T_EQ(ARRAY_COUNT(level.bots[0].captains[0].units),0u);
    if(remove) {
        T_EQ(source->current_order_id,851983u);T_NOT_NULL(source->movement.attackmove_waypoint);
        if(source->movement.attackmove_waypoint){T_EQ(wc3_float_bits(source->movement.attackmove_waypoint->s.origin2.x),wc3_float_bits(target->s.origin2.x));T_EQ(wc3_float_bits(source->movement.attackmove_waypoint->s.origin2.y),wc3_float_bits(target->s.origin2.y));}
        moveGroup_t *group=move_unit_group(source);T_NOT_NULL(group);
        if(group){T_EQ(group->count,1u);T_EQ(group->receiver,source);T_EQ(group->owner_ability,GetAbilityIndex(CAbilityAttack)+1);}
    } else {T_EQ(source->current_order_id,0u);T_EQ(G_UnitQueuedOrderCount(source),0u);T_EQ(source->movement.group_id,0u);T_NULL(source->movement.attackmove_waypoint);}
    T_ASSERT(G_ValidateTargetSubscriptions());
cleanup376class:
    G_BotStop(0);level.started=false;reset_entities();
    G_SetSLKRows("UnitData",old[0]);free_slk_rows(rows[0]);
    G_SetSLKRows("UnitBalance",old[1]);free_slk_rows(rows[1]);
    G_SetSLKRows("UnitWeapons",old[2]);free_slk_rows(rows[2]);
    G_SetSLKRows("UnitAbilities",old[3]);free_slk_rows(rows[3]);
    G_SetSLKRows("UnitUI",old[4]);free_slk_rows(rows[4]);
    setup_test_world();
}
TEST(wc3_bot,captain_preferred376_suicide_class_mechanical_excludes) {Captain376SuicideClass(0);}
TEST(wc3_bot,captain_preferred376_suicide_class_mechanical_remove_restores) {Captain376SuicideClass(1);}
TEST(wc3_bot,captain_preferred376_suicide_class_sapper_excludes) {Captain376SuicideClass(2);}
TEST(wc3_bot,captain_preferred376_suicide_class_sapper_remove_restores) {Captain376SuicideClass(3);}
TEST(wc3_bot,captain_preferred376_suicide_class_ancient_excludes) {Captain376SuicideClass(4);}
TEST(wc3_bot,captain_preferred376_suicide_class_ancient_remove_restores) {Captain376SuicideClass(5);}

/* Retail S: a real Freezing Breath hit raises execution 3c while leaving
 * admission 40 unchanged. The immobile source still admits d000e and d000f. */
TEST(wc3_bot,captain_preferred376_suicide_query_execution_disabled_tower) {
    Captain376World();slkTestData_t *rows[6],*old[6];
    rows[0]=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"unitID\"\nC;X2;Y1;K\"movetp\"\nC;X3;Y1;K\"targType\"\nC;X4;Y1;K\"turnRate\"\nC;X5;Y1;K\"propWin\"\nC;X6;Y1;K\"valid\"\nC;X7;Y1;K\"deathType\"\nC;X1;Y2;K\"hfoo\"\nC;X2;Y2;K\"foot\"\nC;X3;Y2;K\"ground\"\nC;X4;Y2;K0.6\nC;X5;Y2;K60\nC;X6;Y2;K1\nC;X7;Y2;K3\nC;X1;Y3;K\"Hpal\"\nC;X2;Y3;K\"foot\"\nC;X3;Y3;K\"ground\"\nC;X4;Y3;K0.6\nC;X5;Y3;K60\nC;X6;Y3;K1\nC;X7;Y3;K3\nC;X1;Y4;K\"hgtw\"\nC;X2;Y4;K\"none\"\nC;X3;Y4;K\"structure\"\nC;X4;Y4;K0.6\nC;X5;Y4;K60\nC;X6;Y4;K1\nC;X7;Y4;K3\nE\n");old[0]=G_SetSLKRows("UnitData",rows[0]);
    rows[1]=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"unitBalanceID\"\nC;X2;Y1;K\"realHP\"\nC;X3;Y1;K\"spd\"\nC;X4;Y1;K\"isbldg\"\nC;X1;Y2;K\"hfoo\"\nC;X2;Y2;K420\nC;X3;Y2;K270\nC;X4;Y2;K0\nC;X1;Y3;K\"Hpal\"\nC;X2;Y3;K1000\nC;X3;Y3;K270\nC;X4;Y3;K0\nC;X1;Y4;K\"hgtw\"\nC;X2;Y4;K1000\nC;X3;Y4;K0\nC;X4;Y4;K1\nE\n");old[1]=G_SetSLKRows("UnitBalance",rows[1]);
    rows[2]=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"unitWeapID\"\nC;X2;Y1;K\"weapsOn\"\nC;X3;Y1;K\"acquire\"\nC;X4;Y1;K\"weapType1\"\nC;X5;Y1;K\"weapTp1\"\nC;X6;Y1;K\"atkType1\"\nC;X7;Y1;K\"targs1\"\nC;X8;Y1;K\"rangeN1\"\nC;X9;Y1;K\"dmgpt1\"\nC;X10;Y1;K\"cool1\"\nC;X11;Y1;K\"dice1\"\nC;X12;Y1;K\"sides1\"\nC;X13;Y1;K\"dmgplus1\"\nC;X1;Y2;K\"hfoo\"\nC;X2;Y2;K1\nC;X3;Y2;K500\nC;X4;Y2;K\"normal\"\nC;X5;Y2;K\"normal\"\nC;X6;Y2;K\"normal\"\nC;X7;Y2;K\"ground,air,structure\"\nC;X8;Y2;K100\nC;X9;Y2;K0.3\nC;X10;Y2;K2\nC;X11;Y2;K1\nC;X12;Y2;K2\nC;X13;Y2;K5\nC;X1;Y3;K\"hgtw\"\nC;X2;Y3;K1\nC;X3;Y3;K500\nC;X4;Y3;K\"normal\"\nC;X5;Y3;K\"normal\"\nC;X6;Y3;K\"normal\"\nC;X7;Y3;K\"ground,air,structure\"\nC;X8;Y3;K100\nC;X9;Y3;K0.3\nC;X10;Y3;K2\nC;X11;Y3;K1\nC;X12;Y3;K2\nC;X13;Y3;K5\nE\n");old[2]=G_SetSLKRows("UnitWeapons",rows[2]);
    rows[3]=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"unitAbilID\"\nC;X2;Y1;K\"abilList\"\nC;X1;Y2;K\"hfoo\"\nC;X2;Y2;K\"Afrz\"\nC;X1;Y3;K\"Hpal\"\nC;X2;Y3;K\"\"\nC;X1;Y4;K\"hgtw\"\nC;X2;Y4;K\"\"\nE\n");old[3]=G_SetSLKRows("UnitAbilities",rows[3]);
    rows[4]=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"unitUIID\"\nC;X2;Y1;K\"file\"\nC;X3;Y1;K\"modelScale\"\nC;X4;Y1;K\"scale\"\nC;X1;Y2;K\"hfoo\"\nC;X2;Y2;K\"Units\\\\Creeps\\\\Medivh\\\\Medivh.mdx\"\nC;X3;Y2;K1\nC;X4;Y2;K1\nC;X1;Y3;K\"Hpal\"\nC;X2;Y3;K\"Units\\\\Creeps\\\\Medivh\\\\Medivh.mdx\"\nC;X3;Y3;K1\nC;X4;Y3;K1\nC;X1;Y4;K\"hgtw\"\nC;X2;Y4;K\"Units\\\\Creeps\\\\Medivh\\\\Medivh.mdx\"\nC;X3;Y4;K1\nC;X4;Y4;K1\nE\n");old[4]=G_SetSLKRows("UnitUI",rows[4]);
    rows[5]=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;Y1;K\"code\"\nC;X3;Y1;K\"levels\"\nC;X4;Y1;K\"Dur1\"\nC;X5;Y1;K\"HeroDur1\"\nC;X6;Y1;K\"BuffID1\"\nC;X7;Y1;K\"targs1\"\nC;X8;Y1;K\"Area1\"\nC;X1;Y2;K\"Afrz\"\nC;X2;Y2;K\"Afrz\"\nC;X3;Y2;K1\nC;X4;Y2;K5\nC;X5;Y2;K5\nC;X6;Y2;K\"Bfrz\"\nC;X7;Y2;K\"structure,enemy\"\nC;X8;Y2;K100\nE\n");old[5]=G_SetSLKRows("AbilityData",rows[5]);
    player_t *player=&game.clients[0].ps;
    FOR_LOOP(i,3){((mapInfo_t *)level.mapinfo)->players[i].used=true;((mapInfo_t *)level.mapinfo)->players[i].playerType=kPlayerTypeHuman;game.clients[i].mapplayer=level.mapinfo->players+i;}
    T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_MELEE));
    T_ASSERT(run_test_jass(
        "globals\nunit disabled376Source\nendglobals\n"
        "function disabled376Issued takes nothing returns nothing\n"
        "if GetIssuedOrderId()==851982 or GetIssuedOrderId()==851983 then\n"
        "call SetUnitUserData(GetTriggerUnit(),GetUnitUserData(GetTriggerUnit())*10+GetIssuedOrderId()-851970)\nendif\nendfunction\n"
        "function disabled376Observe takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)\n"
        "call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)\n"
        "call TriggerAddAction(t,function disabled376Issued)\nendfunction\n"
        "function main takes nothing returns nothing\n"
        "set disabled376Source=CreateUnit(Player(0),'hgtw',512.,1024.,0.)\n"
        "call SetUnitUserData(CreateUnit(Player(1),'Hpal',1600.,1400.,0.),381)\n"
        "call SetUnitUserData(CreateUnit(Player(2),'hfoo',800.,1024.,0.),382)\nendfunction\n"));
    edict_t *source=NULL,*target=NULL,*caster=NULL;
    FILTER_EDICTS(unit,unit->inuse){if(unit->class_id==MAKEFOURCC('h','g','t','w'))source=unit;if(unit->user_data==381)target=unit;if(unit->user_data==382)caster=unit;}
    T_NOT_NULL(source);T_NOT_NULL(target);T_NOT_NULL(caster);if(!source || !target || !caster)goto cleanup376disabled;
    T_EQ(source->ai_town_player,1u);T_ASSERT(G_UnitIsStructure(source));T_ASSERT(source->aiflags&AI_IMMOBILE);
    T_ASSERT(G_ActorHasAbilityCode(source,MAKEFOURCC('A','a','t','k')));
    T_ASSERT(G_ActorHasAbilityCode(caster,MAKEFOURCC('A','f','r','z')));
    S_ResolveAttackHit(caster,source,1);
    T_ASSERT(S_UnitHasStatus(source,MAKEFOURCC('B','f','r','z')));
    T_ASSERT(S_AbilityExecutionDisabled(source,MAKEFOURCC('A','a','t','k')));
    bool counted=false;for(unitAbilityDisable_t const *a=source->ability_disables;a;a=a->next)if(a->code==MAKEFOURCC('A','a','t','k')){counted=true;T_ASSERT(a->depth>0);T_EQ(a->order_depth,0);}
    T_ASSERT(counted);
    char stop_script[]="function disabled376Stop takes nothing returns nothing\nif IssueImmediateOrder(disabled376Source,\"stop\") then\ncall SetUnitUserData(disabled376Source,376)\nendif\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.vm,stop_script));
    jass_callbyname(level.vm,"disabled376Stop",false);T_EQ(source->user_data,376);T_ASSERT(!G_UnitHasActiveOrder(source));
    jass_callbyname(level.vm,"disabled376Observe",false);
    char command[]="function disabled376Suicide takes nothing returns nothing\ncall SuicideUnitEx(1,'hgtw',1)\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.bots[0].vm,command));
    jasscoroutine_t *call=jass_startcoroutinebynameforplayer(level.bots[0].vm,"disabled376Suicide",player);T_NOT_NULL(call);if(call)jass_resume(level.bots[0].vm,call);
    T_EQ(source->user_data,37733); /* 376 -> 3772 -> 37733 through real events. */
    T_EQ(ARRAY_COUNT(level.bots[0].captains[0].units),0u);
    T_NULL(move_unit_group(source)); /* Immobile d000f is not a moving owner. */
    T_ASSERT(S_AbilityExecutionDisabled(source,MAKEFOURCC('A','a','t','k')));
cleanup376disabled:
    G_BotStop(0);level.started=false;reset_entities();
    G_SetSLKRows("UnitData",old[0]);free_slk_rows(rows[0]);
    G_SetSLKRows("UnitBalance",old[1]);free_slk_rows(rows[1]);
    G_SetSLKRows("UnitWeapons",old[2]);free_slk_rows(rows[2]);
    G_SetSLKRows("UnitAbilities",old[3]);free_slk_rows(rows[3]);
    G_SetSLKRows("UnitUI",old[4]);free_slk_rows(rows[4]);
    G_SetSLKRows("AbilityData",old[5]);free_slk_rows(rows[5]);
    setup_test_world();
}

#endif
