#ifdef BZ_TESTS
#include "shared/test.h"
#include "../skills/s_skills.h"

/* ORDER-02.2 producer contracts: retail-order022-live-1.27.json.gz. */
edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void setup_test_world(void);
void CM_SetupTestWorldBounds(box2_t const *);
void CM_SetupTestPathmap(uint32_t width, uint32_t height, uint8_t const *cells);
void reset_entities(void);
bool run_test_jass(cstring_t src);
static edict_t *review_order_unit(float x, uint32_t owner);
static slkTestData_t *building_install_repair_data(slkTestData_t **rows_out);
static void building_restore_repair_data(slkTestData_t *old, slkTestData_t *rows);

enum { ORDER022_STOP=851972, ORDER022_MOVE=851986, ORDER022_REPAIR=852024 };

static edict_t *order022_setup(void) {
    reset_entities();setup_test_world();
    uint8_t cells[128*128]={0};CM_SetupTestPathmap(128,128,cells);
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{4096,4096}});
    FOR_LOOP(i,2) {
        edict_t *player=alloc_test_unit(0,0,0);
        player->client=game.clients+i;player->client->ps.number=i;
    }
    edict_t *unit=review_order_unit(512,0);unit->s.origin2.y=512;gi.LinkEntity(unit);
    return unit;
}

/* 698ce0 admits d0004 through 680320(mode1, dispatch1) while the old owner is
 * still published, for idle, walking, attacking and Shift-queued units alike.
 * The FIFO retires with that public Stop; the owner event follows it. */
TEST(wc3_order022, owner_change_admits_public_stop_before_owner_event) {
    FOR_LOOP(scene,4) {
        edict_t *unit=order022_setup();
        /* Fixture common.j omits CHANGE_OWNER and keeps pre-mouse ids: 270 maps to 273. */
        T_ASSERT(run_test_jass("globals\ninteger stops=0\ninteger stopplayer=-1\ninteger stopowner=-1\n"
            "integer owners=0\ninteger previous=-1\nendglobals\n"
            "function issued takes nothing returns nothing\n"
            "if GetIssuedOrderId()==851972 then\nset stops=stops+1\n"
            "set stopplayer=GetPlayerId(GetTriggerPlayer())\nset stopowner=GetPlayerId(GetOwningPlayer(GetTriggerUnit()))\n"
            "call BJassAssert(owners==0,\"Stop precedes the owner event\")\nendif\nendfunction\n"
            "function owned takes nothing returns nothing\nset owners=owners+1\n"
            "set previous=GetPlayerId(GetChangingUnitPrevOwner())\nendfunction\n"
            "function verify takes nothing returns nothing\n"
            "call BJassAssert(stops==1 and stopplayer==0 and stopowner==0,\"public Stop from the old owner\")\n"
            "call BJassAssert(owners==1 and previous==0,\"one owner event\")\nendfunction\n"
            "function main takes nothing returns nothing\nlocal integer i=0\nlocal trigger t\nloop\nexitwhen i==2\n"
            "set t=CreateTrigger()\ncall TriggerRegisterPlayerUnitEvent(t,Player(i),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)\n"
            "call TriggerAddAction(t,function issued)\n"
            "set t=CreateTrigger()\ncall TriggerRegisterPlayerUnitEvent(t,Player(i),ConvertPlayerUnitEvent(270),null)\n"
            "call TriggerAddAction(t,function owned)\nset i=i+1\nendloop\nendfunction\n"));
        if(scene==1)T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){1800,512},false,0,0));
        if(scene==2) {
            edict_t *target=review_order_unit(576,2);target->s.origin2.y=512;gi.LinkEntity(target);
            T_ASSERT(G_IssueUnitTargetOrder(unit,"attack",target,false,0));
            T_EQ(unit->current_order_id,851983);
        }
        if(scene==3) {
            T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){1277,512},false,0,0));
            T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){1188,512},true,0,0));
            T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){1366,512},true,0,0));
            T_EQ(unit->order_queue.count,2);
        }
        G_SetUnitPlayer(unit,1);
        /* The engine queues CHANGE_OWNER; the synchronous Stop still precedes it. */
        G_RunEvents();jass_runnewevents(level.vm);
        jass_callbyname(level.vm,"verify",false);T_ASSERT(!jass_rterror_pending(level.vm));
        T_EQ(unit->s.player,1);T_EQ(unit->current_order_id,0);T_EQ(unit->order_queue.count,0);
        T_ASSERT(!unit_is_walking(unit));
    }
    reset_entities();setup_test_world();
}

/* Same-owner calls bypass admission. An inactive unit takes698d94 instead:
 * cancel successors without inventing a public Stop or replacing its head. */
TEST(wc3_order022, same_owner_retains_work_and_inactive_transfer_cuts_only_successors) {
    FOR_LOOP(mode,3) {
        edict_t *unit=order022_setup();
        T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){1800,512},false,0,0));
        if(mode==2)G_DeferFreeEdict(unit);
        FOR_LOOP(i,3)T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){1200+64*i,512},true,0,0));
        T_EQ(unit->order_queue.count,3);
        if(mode==1)G_SetHealth(unit,0);
        T_ASSERT(run_test_jass("globals\ninteger stops=0\nendglobals\n"
            "function seen takes nothing returns nothing\n"
            "if GetIssuedOrderId()==851972 then\nset stops=stops+1\nendif\nendfunction\n"
            "function verify takes nothing returns nothing\n"
            "call BJassAssert(stops==0,\"no Stop for same-owner or inactive transfer\")\nendfunction\n"
            "function main takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)\n"
            "call TriggerAddAction(t,function seen)\nendfunction\n"));
        uint32_t head=unit->current_order_id;
        unitOrder_t retained=unit->order_queue.entries[unit->order_queue.head];
        G_SetUnitPlayer(unit,mode ? 1 : 0);
        jass_callbyname(level.vm,"verify",false);T_ASSERT(!jass_rterror_pending(level.vm));
        T_EQ(unit->s.player,mode ? 1 : 0);T_EQ(unit->current_order_id,head);
        T_EQ(unit->order_queue.count,mode==0 ? 3 : mode==1 ? 0 : 1);
        if(mode==2) {
            T_ASSERT(G_IsDeferredFree(unit));
            T_EQ(unit->order_queue.entries[unit->order_queue.head].point.x,retained.point.x);
            T_EQ(unit->order_queue.entries[unit->order_queue.head].order_id,retained.order_id);
            unsigned slot=unit->s.number;
            cstring_t file=Test_TempPath("order022-removed.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);unit=g_edicts+slot;
            T_ASSERT(G_IsDeferredFree(unit));T_EQ(unit->order_queue.count,1);
            G_TestFinishDeferredFrees();T_ASSERT(!unit->inuse);
        }
    }
    reset_entities();setup_test_world();
}

/* 680320 mode1: Repair's 4371b0 interception keeps the working head and task,
 * cancels every pending Shift successor through 673e80 and publishes no issued
 * event. The un-intercepted control activates its Move after completion. */
TEST(wc3_order022, repair_interception_cancels_successors_without_issued_event) {
    slkTestData_t *rows,*old=building_install_repair_data(&rows);
    FOR_LOOP(sample,6) {
        unsigned mode=sample%3;
        edict_t *worker=order022_setup();
        edict_t *target=alloc_test_unit(MAKEFOURCC('h','b','a','r'),576,512);
        UnitAbilities_t abilities={.abilList="Arep"};
        worker->data.UnitAbilities=&abilities;
        target->health.value=target->health.max_value-100;gi.LinkEntity(target);
        game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD]=game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER]=1000;
        T_ASSERT(G_IssueUnitTargetOrder(worker,"repair",target,false,0));
        /* The adjacent worker publishes its retained target and starts work at admission. */
        T_EQ(worker->currentmove,&repair_move_work);
        umove_t const *work=worker->currentmove;
        T_ASSERT(G_IssueUnitPointOrder(worker,"move",&(vec2_t){1277,512},true,0,0));
        T_EQ(worker->order_queue.count,1);
        T_ASSERT(run_test_jass("globals\ninteger issued=0\nendglobals\n"
            "function seen takes nothing returns nothing\nset issued=issued+1\nendfunction\n"
            "function verify takes nothing returns nothing\ncall BJassAssert(issued==0,\"interception publishes no order\")\nendfunction\n"
            "function main takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)\n"
            "call TriggerAddAction(t,function seen)\nendfunction\n"));
        if(sample>=3) {
            unsigned worker_slot=worker->s.number,target_slot=target->s.number;
            cstring_t file=Test_TempPath("order022-repair.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            worker=g_edicts+worker_slot;target=g_edicts+target_slot;
            worker->data.UnitAbilities=&abilities;
            T_EQ(worker->currentmove,work);T_EQ(worker->order_queue.count,1);
        }
        if(mode<2)T_ASSERT(G_IssueUnitTargetOrder(worker,mode ? "smart" : "repair",target,false,0));
        jass_callbyname(level.vm,"verify",false);T_ASSERT(!jass_rterror_pending(level.vm));
        T_EQ(worker->current_order_id,ORDER022_REPAIR);T_EQ(worker->currentmove,work);
        T_EQ(worker->order_queue.count,mode<2 ? 0 : 1);
        target->health.value=target->health.max_value;
        worker->think=monster_think;
        level.started=level.scriptsStarted=true;
        for(int frame=0;frame<8 && worker->current_order_id==ORDER022_REPAIR;frame++) {
            level.time+=FRAMETIME;globals.RunFrame();
        }
        T_EQ(worker->current_order_id,mode<2 ? 0 : ORDER022_MOVE);
    }
    reset_entities();setup_test_world();
    building_restore_repair_data(old,rows);
}
/* Core Stop must not erase Train's separate queue before owner-change cleanup
 * can cancel/refund it. Exercise a real paid training admission and its save. */
TEST(wc3_order022, owner_stop_keeps_production_queue_reachable_for_old_owner_cleanup) {
    FOR_LOOP(saved,2) {
        order022_setup();
        gameClient_t *old=game.clients,*next=game.clients+1;
        old->ps.stats[PLAYERSTATE_RESOURCE_GOLD]=old->ps.stats[PLAYERSTATE_RESOURCE_LUMBER]=1000;
        old->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP]=100;
        next->ps.stats[PLAYERSTATE_RESOURCE_GOLD]=123;next->ps.stats[PLAYERSTATE_RESOURCE_LUMBER]=456;
        edict_t *producer=alloc_test_unit(MAKEFOURCC('h','b','a','r'),800,512);
        UnitProfile_t profile={.trains="hpea"};
        UnitBalance_t balance=*producer->data.UnitBalance;balance.isBuilding=true;
        producer->data.UnitProfile=&profile;producer->data.UnitBalance=&balance;
        producer->stand=unit_stand;producer->s.player=0;
        T_ASSERT(SP_TrainUnit(producer,MAKEFOURCC('h','p','e','a')));
        edict_t *trainee=producer->build;
        T_NOT_NULL(trainee);
        if(!trainee) {reset_entities();setup_test_world();return;}
        T_ASSERT(trainee->training);T_ASSERT(trainee->inuse);
        T_ASSERT(old->ps.stats[PLAYERSTATE_RESOURCE_GOLD]<1000);
        if(saved) {
            unsigned producer_slot=producer->s.number,trainee_slot=trainee->s.number;
            cstring_t file=Test_TempPath("order022-production.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            producer=g_edicts+producer_slot;trainee=g_edicts+trainee_slot;
            producer->data.UnitProfile=&profile;producer->data.UnitBalance=&balance;
            old=game.clients;next=game.clients+1;
            T_EQ(producer->build,trainee);
        }
        G_SetUnitPlayer(producer,1);
        T_EQ(producer->s.player,1);T_NULL(producer->build);
        T_ASSERT(!trainee->inuse);
        T_EQ(old->ps.stats[PLAYERSTATE_RESOURCE_GOLD],1000);
        T_EQ(old->ps.stats[PLAYERSTATE_RESOURCE_LUMBER],1000);
        T_EQ(next->ps.stats[PLAYERSTATE_RESOURCE_GOLD],123);
        T_EQ(next->ps.stats[PLAYERSTATE_RESOURCE_LUMBER],456);
        T_EQ(old->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED],0);
    }
    reset_entities();setup_test_world();
}

TEST(wc3_order022, owner_stop_keeps_revive_queue_reachable_for_old_owner_refund) {
    order022_setup();
    stbIniCache_t misc=game.config.misc;game.config.misc=(stbIniCache_t){0};
    T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.misc,"[Misc]\nReviveBaseFactor=0.4\nReviveLevelFactor=0.2\n"
        "ReviveBaseLumberFactor=0.25\nReviveLumberLevelFactor=0.1\nReviveMaxFactor=1\n"
        "ReviveTimeFactor=0.3\nReviveMaxTimeFactor=0.5\n"));
    gameClient_t *old=game.clients,*next=game.clients+1;
    old->ps.stats[PLAYERSTATE_RESOURCE_GOLD]=old->ps.stats[PLAYERSTATE_RESOURCE_LUMBER]=1000;
    next->ps.stats[PLAYERSTATE_RESOURCE_GOLD]=123;next->ps.stats[PLAYERSTATE_RESOURCE_LUMBER]=456;
    edict_t *producer=alloc_test_unit(MAKEFOURCC('h','b','a','r'),800,512);
    edict_t *hero=alloc_test_unit(MAKEFOURCC('H','p','a','l'),1000,512);
    UnitProfile_t profile={.revive="1"};producer->data.UnitProfile=&profile;
    UnitBalance_t balance=*hero->data.UnitBalance;
    balance.strength=1;balance.goldCost=120;balance.lumberCost=80;balance.buildTime=10;
    hero->data.UnitBalance=&balance;hero->hero.level=1;
    G_SetHealth(hero,0);hero->svflags|=SVF_DEADMONSTER;
    hero->revival=G_AllocRevival();hero->revival->awaiting=true;
    producer->stand=unit_stand;
    T_ASSERT(G_QueueHeroRevive(producer,hero));T_EQ(producer->build,hero);
    T_ASSERT(hero->revival->reviving);T_EQ(hero->revival->producer,producer);
    T_EQ(old->ps.stats[PLAYERSTATE_RESOURCE_GOLD],952);T_EQ(old->ps.stats[PLAYERSTATE_RESOURCE_LUMBER],980);
    G_SetUnitPlayer(producer,1);
    T_EQ(producer->s.player,1);T_NULL(producer->build);
    T_ASSERT(hero->inuse);T_ASSERT(hero->revival->awaiting);T_ASSERT(!hero->revival->reviving);
    T_NULL(hero->revival->producer);T_EQ(hero->revival->gold,0);T_EQ(hero->revival->lumber,0);
    T_EQ(old->ps.stats[PLAYERSTATE_RESOURCE_GOLD],1000);T_EQ(old->ps.stats[PLAYERSTATE_RESOURCE_LUMBER],1000);
    T_EQ(next->ps.stats[PLAYERSTATE_RESOURCE_GOLD],123);T_EQ(next->ps.stats[PLAYERSTATE_RESOURCE_LUMBER],456);
    reset_entities();setup_test_world();Stb_IniCacheFree(&game.config.misc);game.config.misc=misc;
}

/* Training owns the transition from queue-next link to an independent unit.
 * Keep the second trainee reachable while the first loses that link, including
 * a save between the two completions through the real frame scheduler. */
TEST(wc3_order022, training_completion_detaches_child_link_and_schedules_saved_successor) {
    FOR_LOOP(saved,2) {
        order022_setup();
        /* reset_entities clears units, but intentionally retains player stats. */
        game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED]=0;
        game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD]=1000;
        game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER]=1000;
        game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP]=100;
        edict_t *producer=alloc_test_unit(MAKEFOURCC('h','b','a','r'),800,512);
        UnitProfile_t profile={.trains="hpea"};
        UnitBalance_t producer_balance=*producer->data.UnitBalance;producer_balance.isBuilding=true;
        producer->data.UnitProfile=&profile;producer->data.UnitBalance=&producer_balance;
        producer->stand=unit_stand;producer->think=monster_think;
        T_ASSERT(SP_TrainUnit(producer,MAKEFOURCC('h','p','e','a')));
        T_ASSERT(SP_TrainUnit(producer,MAKEFOURCC('h','p','e','a')));
        edict_t *first=producer->build,*second=first ? first->build : NULL;
        T_NOT_NULL(first);T_NOT_NULL(second);
        if(!first || !second) {reset_entities();setup_test_world();return;}
        UnitBalance_t balance=*first->data.UnitBalance;balance.buildTime=1;
        first->data.UnitBalance=second->data.UnitBalance=&balance;
        T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
        level.started=level.scriptsStarted=true;
        for(unsigned frame=0;frame<30 && first->training;frame++) {
            level.time+=FRAMETIME;globals.RunFrame();
        }
        T_ASSERT(first->inuse);T_ASSERT(!first->training);T_NULL(first->build);
        T_EQ(producer->build,second);T_ASSERT(second->training);
        if(saved) {
            unsigned pslot=producer->s.number,fslot=first->s.number,sslot=second->s.number;
            cstring_t file=Test_TempPath("order022-complete-training.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            producer=g_edicts+pslot;first=g_edicts+fslot;second=g_edicts+sslot;
            producer->data.UnitProfile=&profile;producer->data.UnitBalance=&producer_balance;
            first->data.UnitBalance=second->data.UnitBalance=&balance;
            T_NULL(first->build);T_EQ(producer->build,second);
        }
        for(unsigned frame=0;frame<30 && second->training;frame++) {
            level.time+=FRAMETIME;globals.RunFrame();
        }
        T_ASSERT(first->inuse);T_ASSERT(second->inuse);
        T_ASSERT(!first->training);T_ASSERT(!second->training);
        T_NULL(first->build);T_NULL(second->build);T_NULL(producer->build);
        T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED],2*balance.foodUsed);
    }
    reset_entities();setup_test_world();
}

#endif
