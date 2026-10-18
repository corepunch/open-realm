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

#endif
