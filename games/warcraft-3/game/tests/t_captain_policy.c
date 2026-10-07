#ifdef BZ_TESTS
#include "../g_local.h"
#include "../skills/s_skills.h"
#include "shared/test.h"
extern void reset_entities(void),setup_test_world(void);
extern bool run_test_jass(cstring_t);
extern slkTestData_t *parse_slk_string(char const *),*G_SetSLKRows(char const *,slkTestData_t *);
extern void free_slk_rows(slkTestData_t *);

static void CaptainPolicyWorld(void) {
    G_BotStop(0);reset_entities();setup_test_world();
    level.time=0;level.pathing_phase=0;level.timer_clock_valid=false;
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
    S_SetUnitPosition(actor,&(vec2_t){1600,1024});
    T_EQ(wc3_float_bits(actor->s.origin2.x),wc3_float_bits(1600));
    G_BotCaptainGoHome(&game.clients[0].ps);
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
    cstring_t file="/tmp/wc3-captain-policy159.bin";
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
    cstring_t file="/tmp/wc3-captain-speed160.bin";
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
#endif
