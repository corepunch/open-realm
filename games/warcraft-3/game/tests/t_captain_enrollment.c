#ifdef BZ_TESTS
#include "../g_local.h"
#include "../skills/s_skills.h"
#include "jass/jass.h"
#include "shared/test.h"

extern void reset_entities(void), setup_test_world(void);
extern bool run_test_jass(cstring_t);

static moveGroup_t *EnrollmentGroup(edict_t const *unit) {
    FOR_EACH_ARRAY(moveGroup_t *, group, level.move_groups)
        if ((*group)->inuse && (*group)->id==unit->movement.group_id) return *group;
    return NULL;
}

/* Retail first enrollment replaces a Move with another Move; only a duplicate
 * preserves the actual task. The first private range precedes buff publication. */
TEST(wc3_bot, temporary_captain_enrollment_replaces_once_and_disabled_policy_detaches) {
    G_BotStop(0);reset_entities();setup_test_world();
    static uint8_t cells[64*64];memset(cells,0,sizeof(cells));
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
    level.pathing_clock=(wc3Clock_t){0,0,300};
    mapInfo_t info={0};info.players[0].used=true;
    mapInfo_t const *old_info=level.mapinfo;level.mapinfo=&info;
    mapPlayer_t const *old_player=game.clients[0].mapplayer;
    uint32_t old_controller=game.clients[0].jass.controller;
    game.clients[0].mapplayer=info.players;game.clients[0].jass.controller=1;
    G_BotInitPlayers();G_BotCreateCaptains(&game.clients[0].ps);
    G_BotSetCaptainHome(&game.clients[0].ps,1,1600,1024);
    bot_t *bot=level.bots;
    T_ASSERT(bot->flags&BOT_GROUP_TIMED_LIFE);
    /* Keep exercising the public application after the missing default fails. */
    bot->flags|=BOT_GROUP_TIMED_LIFE;
    edict_t *home_actor=bot->captains[0].home_actor;
    T_ASSERT(G_BotStart(&game.clients[0].ps,"test_idle.ai",BOT_CAMPAIGN));
    T_EQ(bot->captains[0].home_actor,home_actor);
    T_ASSERT(bot->flags&BOT_GROUP_TIMED_LIFE);
    T_ASSERT(run_test_jass("globals\nunit first\nendglobals\n"
        "function main takes nothing returns nothing\n"
        "set first=CreateUnit(Player(0),'hfoo',256,256,0)\n"
        "call IssuePointOrder(first,\"move\",1600,256)\nendfunction\n"));
    edict_t *unit=NULL;
    FILTER_EDICTS(cur,cur->inuse && cur->class_id==MAKEFOURCC('h','f','o','o'))unit=cur;
    T_NOT_NULL(unit);if(!unit)return;
    uint32_t public_move=G_OrderId("move"),initial_group=unit->movement.group_id;
    float attack_range=0;bool armed=S_UnitAttackApproachRange(unit,&attack_range);
    float old_world_range=armed ? wc3_add(wc3_mul(attack_range,wc3_float(0x3f19999a)),70) : 300;
    S_ApplyTimedLife(unit,MAKEFOURCC('B','T','L','F'),30);
    T_EQ(G_BotCaptainGroupSize(&game.clients[0].ps),1);
    T_EQ(unit->current_order_id,public_move);
    T_ASSERT(unit->movement.group_id!=initial_group);
    T_EQ(unit->movement.captain_home.roster_actor,bot->captains[0].home_actor);
    moveGroup_t *group=EnrollmentGroup(unit);
    T_NOT_NULL(group);
    if(group)T_EQ(wc3_float_bits(group->members[0].arrival_range),wc3_float_bits(wc3_div(wc3_add(old_world_range,MAX(1,unit->collision)),32)));
    T_ASSERT(S_UnitHasTimedLife(unit));
    T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){1600,256},false,0,0));
    uint32_t retained_group=unit->movement.group_id;edict_t *retained_goal=unit->goalentity;
    S_ApplyTimedLife(unit,MAKEFOURCC('B','H','w','e'),30);
    T_EQ(G_BotCaptainGroupSize(&game.clients[0].ps),1);
    T_EQ(unit->movement.group_id,retained_group);T_EQ(unit->goalentity,retained_goal);
    bot->flags&=~BOT_GROUP_TIMED_LIFE;
    /* Toggling policy alone leaves membership until the next application. */
    T_EQ(G_BotCaptainGroupSize(&game.clients[0].ps),1);
    S_ApplyTimedLife(unit,MAKEFOURCC('B','T','L','F'),30);
    T_EQ(G_BotCaptainGroupSize(&game.clients[0].ps),0);
    T_EQ(unit->current_order_id,0);T_EQ(G_UnitQueuedOrderCount(unit),0);
    T_NULL(unit->movement.captain_home.roster_actor);T_NULL(unit->movement.captain_home.actor);
    G_BotStop(0);reset_entities();setup_test_world();level.mapinfo=old_info;
    game.clients[0].mapplayer=old_player;game.clients[0].jass.controller=old_controller;
}

TEST(wc3_bot, captain_logical_roster_and_policy_survive_cold_save_load) {
    G_BotStop(0);reset_entities();setup_test_world();
    static uint8_t cells[64*64];memset(cells,0,sizeof(cells));
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
    level.pathing_clock=(wc3Clock_t){.25f,0,300};
    G_BotCreateCaptains(&game.clients[0].ps);
    G_BotSetCaptainHome(&game.clients[0].ps,1,1600,1024);
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n"
        "call CreateUnit(Player(0),'hfoo',256,256,0)\n"
        "call CreateUnit(Player(0),'hfoo',512,256,0)\nendfunction\n"));
    T_ASSERT(G_BotAddAssault(&game.clients[0].ps,2,MAKEFOURCC('h','f','o','o')));
    botCaptain_t *captain=level.bots[0].captains;
    edict_t *first=captain->units[0],*second=captain->units[1],*actor=captain->home_actor;
    level.bots[0].flags=BOT_GROUP_TIMED_LIFE;
    cstring_t file="/tmp/wc3-captain-logical158.bin";
    T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));remove(file);
    T_EQ(level.bots[0].flags,BOT_GROUP_TIMED_LIFE);
    T_EQ(ARRAY_COUNT(captain->units),2);
    if(ARRAY_COUNT(captain->units)==2) {T_EQ(captain->units[0],first);T_EQ(captain->units[1],second);}
    T_EQ(captain->home_actor,actor);T_ASSERT(captain->home_set);
    T_EQ(wc3_float_bits(captain->home.x),wc3_float_bits(1600));
    T_EQ(wc3_float_bits(captain->created.time),wc3_float_bits(.25f));
    G_BotStop(0);reset_entities();setup_test_world();
}

TEST(wc3_bot, temporary_captain_disconnected_admission_uses_point_and_no_issued_event) {
    G_BotStop(0);reset_entities();setup_test_world();
    static uint8_t cells[64*64];memset(cells,0,sizeof(cells));
    FOR_LOOP(y,64) cells[y*64+32]=CM_PATHING_UNWALKABLE;
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
    level.pathing_clock=(wc3Clock_t){0,0,300};
    ((mapInfo_t *)level.mapinfo)->players[0].used=true;
    game.clients[0].mapplayer=level.mapinfo->players;
    game.clients[0].jass.controller=1;G_BotInitPlayers();
    G_BotCreateCaptains(&game.clients[0].ps);G_BotSetCaptainHome(&game.clients[0].ps,1,1600,1024);
    /* Native058900 supplies the committed fallback, whereas0594f0
     * predicts the source of a reachability query. Separate those poses. */
    edict_t *actor=level.bots[0].captains[0].home_actor;
    actor->movement.clock_valid=true;
    actor->movement.pose_clock=level.pathing_clock;
    actor->movement.velocity=(vec2_t){1,0};
    level.pathing_clock.time=.25f;
    T_ASSERT(run_test_jass("globals\nunit mover\ninteger issued=0\nendglobals\n"
        "function observe takes nothing returns nothing\nset issued=issued+1\nendfunction\n"
        "function count_issued takes nothing returns integer\nreturn issued\nendfunction\n"
        "function main takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "set mover=CreateUnit(Player(0),'hfoo',256,256,0)\n"
        "call TriggerRegisterUnitEvent(t,mover,EVENT_UNIT_ISSUED_POINT_ORDER)\n"
        "call TriggerAddAction(t,function observe)\n"
        "call IssuePointOrder(mover,\"move\",512,256)\n"
        "call UnitApplyTimedLife(mover,'BTLF',30)\nendfunction\n"));
    edict_t *unit=NULL;
    FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','f','o','o'))unit=ent;
    T_NOT_NULL(unit);if(!unit)return;
    G_RunEvents();jass_runevents(level.vm);
    int32_t issued=0;
    T_ASSERT(jass_evaluateplayerinteger(level.vm,jass_functionbyname(level.vm,"count_issued"),&game.clients[0].ps,&issued));
    T_EQ(issued,1);
    T_EQ(G_BotCaptainGroupSize(&game.clients[0].ps),1);
    T_EQ(unit->movement.captain_home.roster_actor,level.bots[0].captains[0].home_actor);
    T_ASSERT(!unit->movement.captain_home.active);
    T_NOT_NULL(unit->goalentity);
    if(unit->goalentity) {
        T_EQ(wc3_float_bits(unit->goalentity->s.origin2.x),wc3_float_bits(1600));
        T_EQ(wc3_float_bits(unit->goalentity->s.origin2.y),wc3_float_bits(1024));
    }
    T_EQ(unit->current_order_id,G_OrderId("move"));
    G_BotStop(0);reset_entities();setup_test_world();
}
#endif
