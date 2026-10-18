#ifdef BZ_TESTS
#include "../g_local.h"
#include "../skills/s_skills.h"
#include "shared/test.h"
extern void reset_entities(void),setup_test_world(void);
extern bool run_test_jass(cstring_t);
extern edict_t *alloc_test_unit(uint32_t,float,float);
static edict_t *Target363Follower(float y) {
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','R','T','E'),128,y);
    unit->svflags=SVF_MONSTER;unit->movetype=MOVETYPE_STEP;
    unit->stand=unit_stand;unit->collision=31;unit_stand(unit);
    S_SetUnitMoveSpeed(unit,270);G_SetUnitPlayer(unit,2);
    return unit;
}

static edict_t *target363_early,*target363_attack,*target363_late;
static botCaptain_t *target363_captain;
static uint32_t target363_history[32],target363_history_count;
static void Target363Visit(targetSubscriberKind_t kind,void *owner,edict_t *target) {
    (void)target;
    uint32_t event=owner==target363_early && kind==TARGET_OWNER_MOVE ? 1 :
        owner==target363_captain && kind==TARGET_OWNER_CAPTAIN ? 2 :
        owner==target363_attack && kind==TARGET_OWNER_ATTACK ? 3 :
        owner==target363_late && kind==TARGET_OWNER_MOVE ? 4 : 0;
    if(event && target363_history_count<32)target363_history[target363_history_count++]=event;
}


/* Public TargetLost/owner transfer reaches Captain in the same registry as
 * retained Move and Attack. The original mixed-owner map removes/converts its
 * hostile source before the deferred unit destructor runs. */
TEST(wc3_bot, target363_public_source_change_restores_captain_synchronously) {
    FOR_LOOP(convert,3) FOR_LOOP(saved,2) {
        G_BotStop(0);reset_entities();setup_test_world();
        level.time=level.pathing_msec=level.pathing_phase=0;
        level.pathing_due=level.pathing_owner_clock_valid=level.timer_clock_valid=false;
        static uint8_t cells[64*64];memset(cells,0,sizeof(cells));
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
        level.pathing_clock=(wc3Clock_t){0,0,300};
        ((mapInfo_t *)level.mapinfo)->players[0].used=true;
        FOR_LOOP(p,3) {
            ((mapInfo_t *)level.mapinfo)->players[p].used=true;
            ((mapInfo_t *)level.mapinfo)->players[p].playerType=kPlayerTypeHuman;
            game.clients[p].mapplayer=level.mapinfo->players+p;
        }
        game.clients[0].mapplayer=level.mapinfo->players;
        game.clients[0].jass.controller=1;G_BotInitPlayers();
        G_BotCreateCaptains(&game.clients[0].ps);
        G_BotSetCaptainHome(&game.clients[0].ps,1,256,1024);
        player_t *player=&game.clients[0].ps;
        T_ASSERT(G_BotStart(player,"test_idle.ai",BOT_MELEE));
        T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
        UnitWeapons_t weapons={.attacksEnabled=1};
        edict_t *members[6];
        FOR_LOOP(i,6) {
            edict_t *unit=members[i]=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){256+80*(i%4),192+80*(i/4)},0);
            unit->mana.value=100;unit->mana.max_value=200;unit->data.UnitWeapons=&weapons;
            unit->runtime.acquisition_range=1000;unit->targtype=TARG_GROUND;
            unitAttack_t *profile=S_AttackProfileWrite(unit,0);
            profile->type=ATK_NORMAL;profile->damageBase=17;profile->cooldown=1.5f;
            profile->damagePoint=3.25f;profile->range=600;profile->weapon=WPN_NORMAL;
            profile->targetsAllowed=WC3_TARGET_FLAG_GROUND;gi.LinkEntity(unit);
        }
        T_ASSERT(G_BotAddAssault(player,6,MAKEFOURCC('h','f','o','o')));
        botCaptain_t *captain=level.bots[0].captains;
        G_BotCaptainAttack(player,&captain->home);
        G_SetPlayerAlliance(player,&game.clients[1].ps,ALLIANCE_PASSIVE,false);
        G_SetPlayerAlliance(&game.clients[1].ps,player,ALLIANCE_PASSIVE,false);
        edict_t *source=unit_create(1,MAKEFOURCC('h','f','o','o'),&(vec2_t){1500,1024},0);
        source->targtype=TARG_GROUND;S_SetUnitPaused(source,true);gi.LinkEntity(source);
        target363_early=Target363Follower(512);
        T_ASSERT(unit_issuetargetorder(target363_early,"move",source));
        T_Damage(members[0],source,1);
        target363_captain=captain;
        target363_attack=unit_create(2,MAKEFOURCC('h','f','o','o'),&(vec2_t){128,640},0);
        target363_attack->data.UnitWeapons=&weapons;target363_attack->targtype=TARG_GROUND;
        *S_AttackProfileWrite(target363_attack,0)=*S_AttackProfileRead(members[0],0);
        T_ASSERT(unit_issuetargetorder(target363_attack,"attack",source));
        target363_late=Target363Follower(768);
        T_ASSERT(unit_issuetargetorder(target363_late,"move",source));
        T_EQ(captain->follow.unit,source);
        T_NOT_NULL(captain->home_actor->goalentity);
        T_EQ(wc3_float_bits(captain->active_goal.x),wc3_float_bits(1500));
        cstring_t file=Test_TempPath("wc3-target363-source.bin");
        if(saved) {
            T_ASSERT(WriteGame(file));G_BotStop(0);T_ASSERT(ReadGame(file));
            FOR_LOOP(i,6)members[i]->data.UnitWeapons=&weapons;
            target363_attack->data.UnitWeapons=&weapons;
        }
        target363_history_count=0;
        if(convert<2)target363_test_visit=Target363Visit;
        if(convert==1)G_SetUnitPlayer(source,2);
        else if(convert==2) {G_SetHealth(source,0);unit_die(source,NULL);}
        else G_DeferFreeEdict(source);
        target363_test_visit=NULL;
        if(convert<2) {
            T_EQ(target363_history_count,4u);
            FOR_LOOP(i,MIN(4,target363_history_count))T_EQ(target363_history[i],i+1);
        }
        /* Both public APIs publish the source mutation synchronously. No
         * timer, end-of-frame cleanup, or G_FreeEdict is needed to recover. */
        T_NULL(captain->follow.unit);T_NULL(captain->backup.unit);
        T_EQ(ARRAY_COUNT(captain->units),6u);
        if(convert==2) {
            /* Dead-source home/retreat choice also consumes live engagement
             * policy. This fixture checks event ownership, not that policy. */
            T_NE(wc3_float_bits(captain->active_goal.x),wc3_float_bits(source->s.origin2.x));
        } else {
            T_ASSERT(captain->policy_flags&BOT_CAPTAIN_REQUEST_RESTORED);
            T_EQ(wc3_float_bits(captain->active_goal.x),wc3_float_bits(captain->home.x));
            T_NOT_NULL(captain->home_actor->goalentity);
            if(captain->home_actor->goalentity)
                T_EQ(wc3_float_bits(captain->home_actor->goalentity->s.origin2.x),wc3_float_bits(captain->home.x));
        }
        remove(file);G_BotStop(0);level.started=false;reset_entities();setup_test_world();
    }
}

/* The original0725d0 key is (event,callback), not callback alone. An owner
 * adding a second event after another owner must gain a different position
 * in that event while duplicate updates preserve its earlier first-event rank. */
static edict_t *target363_kernel_units[128],*target363_kernel_target;
static uint32_t target363_kernel_size,target363_kernel_order[512],target363_kernel_count;
static bool target363_kernel_nested;
static edict_t *Target363KernelMover(uint32_t index) {
    edict_t *unit=Target363Follower(128+(index/16)*128);
    S_PlaceUnitPosition(unit,&(vec2_t){128+(index%16)*96,128+(index/16)*128});
    T_ASSERT(unit_issuetargetorder(unit,"move",target363_kernel_target));
    return unit;
}
static void Target363KernelWorld(void) {
    G_BotStop(0);reset_entities();setup_test_world();
    static uint8_t cells[64*64];memset(cells,0,sizeof(cells));
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
    FOR_LOOP(p,3) {
        ((mapInfo_t *)level.mapinfo)->players[p].used=true;
        ((mapInfo_t *)level.mapinfo)->players[p].playerType=kPlayerTypeHuman;
        game.clients[p].ps.rdflags|=RDF_NOFOG|RDF_NOFOGMASK;
    }
    target363_kernel_target=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){1800,1800},0);
    target363_kernel_size=target363_kernel_count=0;target363_kernel_nested=false;
    FOR_LOOP(i,2)target363_kernel_units[target363_kernel_size++]=Target363KernelMover(i);
}
static void Target363KernelVisit(targetSubscriberKind_t kind,void *owner,edict_t *target) {
    if(kind!=TARGET_OWNER_MOVE || target!=target363_kernel_target)return;
    FOR_LOOP(i,target363_kernel_size)if(owner==target363_kernel_units[i]) {
        T_ASSERT(target363_kernel_count<512);
        if(target363_kernel_count<512)target363_kernel_order[target363_kernel_count++]=i;
        break;
    }
    if(!target363_kernel_nested)return;
    target363_kernel_nested=false;
    T_ASSERT(unit_issueimmediateorder(target363_kernel_units[1],"stop"));
    T_ASSERT(unit_issuetargetorder(target363_kernel_units[1],"move",target));
    /* Force both node-pool and hash growth while the outer snapshot is live.
     * Retired slots may be reused; their old registration identity may not. */
    FOR_LOOP(i,100) {
        uint32_t index=target363_kernel_size;
        target363_kernel_units[target363_kernel_size++]=Target363KernelMover(index);
    }
    S_UnitTargetLost(target);
}
TEST(wc3_movement, target363_distinct_event_ranks_and_duplicate_updates_survive_save) {
    Target363KernelWorld();
    edict_t *first=target363_kernel_units[0],*target=target363_kernel_target;
    uint32_t owner=(uint32_t)(first-g_edicts)+1,other=(uint32_t)(target-g_edicts)+1;
    uint32_t bucket=target_subscription_find(TARGET_OWNER_MOVE,owner,other,2,false);
    T_ASSERT(bucket!=UINT32_MAX && target_subscription_hash[bucket] && target_subscription_hash[bucket]!=UINT32_MAX);
    if(bucket==UINT32_MAX || !target_subscription_hash[bucket] || target_subscription_hash[bucket]==UINT32_MAX)return;
    target_subscription_remove(target_subscription_hash[bucket]);
    targetSubscriptionRecord_t record={.target=other,.owner=owner,.target_birth=target->spawn_time,
        .owner_birth=first->spawn_time,.kind=TARGET_OWNER_MOVE,.mask=1};
    target_subscription_insert(&record); /* Duplicate first-event update stays in place. */
    record.mask=2;target_subscription_insert(&record); /* Newly added second event appends. */
    cstring_t file=Test_TempPath("wc3-target363-event-ranks.bin");
    T_ASSERT(WriteGame(file));
    target363_test_visit=Target363KernelVisit;
    S_UnitTargetOwnerChanged(target);target363_test_visit=NULL;
    T_EQ(target363_kernel_count,2u);
    T_EQ(target363_kernel_order[0],1u);T_EQ(target363_kernel_order[1],0u);
    T_ASSERT(ReadGame(file));target363_kernel_count=0;
    target363_test_visit=Target363KernelVisit;
    S_UnitTargetLost(target);target363_test_visit=NULL;
    T_EQ(target363_kernel_count,2u);
    T_EQ(target363_kernel_order[0],0u);T_EQ(target363_kernel_order[1],1u);
    T_ASSERT(G_ValidateTargetSubscriptions());
    remove(file);reset_entities();setup_test_world();
}
TEST(wc3_movement, target363_nested_growth_cancels_reused_outer_registration) {
    Target363KernelWorld();target363_kernel_nested=true;
    target363_test_visit=Target363KernelVisit;
    S_UnitTargetLost(target363_kernel_target);target363_test_visit=NULL;
    T_EQ(target363_kernel_size,102u);T_EQ(target363_kernel_count,103u);
    T_EQ(target363_kernel_order[0],0u);
    FOR_LOOP(i,102)T_EQ(target363_kernel_order[i+1],i);
    T_ASSERT(G_ValidateTargetSubscriptions());
    cstring_t file=Test_TempPath("wc3-target363-grown-registry.bin");
    T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
    T_ASSERT(G_ValidateTargetSubscriptions());
    reset_entities();setup_test_world();
}
TEST(wc3_save, target363_rejects_duplicate_event_unknown_event_and_stale_incarnation) {
    FOR_LOOP(which,4) {
        Target363KernelWorld();
        FILE *file=tmpfile();T_NOT_NULL(file);if(!file)return;
        T_ASSERT(G_WriteTargetSubscriptions(file));rewind(file);
        uint64_t sequence;uint32_t count;
        T_EQ(fread(&sequence,sizeof(sequence),1,file),1u);T_EQ(fread(&count,sizeof(count),1,file),1u);
        T_ASSERT(count>=2);if(count<2) {fclose(file);return;}
        targetSubscriptionRecord_t *records=gi.MemAlloc(count*sizeof(*records));
        T_EQ(fread(records,sizeof(*records),count,file),count);
        if(which==0) {uint64_t next=records[1].sequence;records[1]=records[0];records[1].sequence=next;}
        else if(which==1)records[0].mask=8;
        else if(which==2)records[0].owner_birth++;
        else records[0].target_birth++;
        rewind(file);T_EQ(fwrite(&sequence,sizeof(sequence),1,file),1u);T_EQ(fwrite(&count,sizeof(count),1,file),1u);
        T_EQ(fwrite(records,sizeof(*records),count,file),count);gi.MemFree(records);rewind(file);
        T_ASSERT(!G_ReadTargetSubscriptions(file));fclose(file);
        reset_entities();setup_test_world();
    }
}

/* Native9cf680 calls9d4a50(unit,1,0,1,1): roster membership retains
 * death/owner subscriptions; only a separate target role owns TargetLost. */
static botCaptain_t *Target378CaptainWorld(void) {
    Target363KernelWorld();
    player_t *player=&game.clients[0].ps;
    G_BotCreateCaptains(player);G_BotSetCaptainHome(player,1,256,1024);
    T_ASSERT(G_BotAddAssault(player,1,target363_kernel_target->class_id));
    botCaptain_t *captain=level.bots[0].captains+BOT_CAPTAIN_ATTACK;
    T_EQ(ARRAY_COUNT(captain->units),1u);
    T_EQ(captain->units[0],target363_kernel_target);
    T_EQ(target363_kernel_target->movement.captain_home.roster_actor,captain->home_actor);
    return captain;
}
static uint64_t Target378Rank(botCaptain_t *captain,edict_t *target,uint32_t mask) {
    uint32_t pos=target_subscription_find(TARGET_OWNER_CAPTAIN,target_subscription_captain(captain),
        target_subscription_unit(target),mask,false);
    uint32_t id=pos==UINT32_MAX ? 0 : target_subscription_hash[pos];
    return id && id!=UINT32_MAX ? target_subscriptions[id-1].saved.sequence : 0;
}
TEST(wc3_movement,target378_roster_lost_preserves_membership_death_and_owner_withdraw) {
    FOR_LOOP(event,4)FOR_LOOP(saved,2) {
        botCaptain_t *captain=Target378CaptainWorld();edict_t *target=target363_kernel_target;
        T_EQ(Target378Rank(captain,target,1),0u);
        T_ASSERT(Target378Rank(captain,target,4));T_ASSERT(Target378Rank(captain,target,2));
        cstring_t file=Test_TempPath("wc3-target378-roster.bin");
        if(saved){T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));}
        if(event==0)S_UnitTargetLost(target);
        else if(event==1){G_SetHealth(target,0);unit_die(target,NULL);}
        else if(event==2)G_SetUnitPlayer(target,1);
        else G_DeferFreeEdict(target);
        T_EQ(ARRAY_COUNT(captain->units),event==0 || event==3 ? 1u : 0u);
        if(event==0 || event==3)T_EQ(target->movement.captain_home.roster_actor,captain->home_actor);
        if(event==3){G_TestFinishDeferredFrees();T_EQ(ARRAY_COUNT(captain->units),0u);}
        if(event==0){T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));T_EQ(ARRAY_COUNT(captain->units),1u);}
        T_ASSERT(G_ValidateTargetSubscriptions());remove(file);
        G_BotStop(0);reset_entities();setup_test_world();
    }
}
TEST(wc3_movement,target378_overlapping_roles_preserve_event_ranks_and_save) {
    botCaptain_t *captain=Target378CaptainWorld();edict_t *target=target363_kernel_target;
    uint64_t death=Target378Rank(captain,target,4),owner=Target378Rank(captain,target,2);
    T_EQ(Target378Rank(captain,target,1),0u);
    edict_t *late=Target363KernelMover(2);
    uint32_t pos=target_subscription_find(TARGET_OWNER_MOVE,target_subscription_unit(late),
        target_subscription_unit(target),1,false);
    T_ASSERT(pos!=UINT32_MAX && target_subscription_hash[pos] && target_subscription_hash[pos]!=UINT32_MAX);
    uint64_t late_rank=target_subscriptions[target_subscription_hash[pos]-1].saved.sequence;
    captain->follow=(botCaptainTarget_t){target,target->spawn_time};G_RefreshCaptainTargetSubscriptions(captain);
    uint64_t lost=Target378Rank(captain,target,1);
    T_ASSERT(lost>late_rank);T_EQ(Target378Rank(captain,target,4),death);T_EQ(Target378Rank(captain,target,2),owner);
    cstring_t file=Test_TempPath("wc3-target378-union.bin");
    T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));
    T_EQ(Target378Rank(captain,target,1),lost);T_EQ(Target378Rank(captain,target,4),death);T_EQ(Target378Rank(captain,target,2),owner);
    captain->follow=(botCaptainTarget_t){0};G_RefreshCaptainTargetSubscriptions(captain);
    T_EQ(Target378Rank(captain,target,1),0u);T_EQ(Target378Rank(captain,target,4),death);T_EQ(Target378Rank(captain,target,2),owner);
    T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));
    captain->target=(botCaptainTarget_t){target,target->spawn_time};G_RefreshCaptainTargetSubscriptions(captain);
    T_ASSERT(Target378Rank(captain,target,1)>lost);T_EQ(Target378Rank(captain,target,4),death);T_EQ(Target378Rank(captain,target,2),owner);
    T_ASSERT(G_ValidateTargetSubscriptions());remove(file);G_BotStop(0);reset_entities();setup_test_world();
}
TEST(wc3_movement,target378_late_target_role_joins_lost_fifo_after_existing_moves) {
    FOR_LOOP(saved,2) {
        botCaptain_t *captain=Target378CaptainWorld();edict_t *target=target363_kernel_target;
        target363_early=target363_kernel_units[0];target363_late=Target363KernelMover(2);
        target363_attack=NULL;target363_captain=captain;
        captain->follow=(botCaptainTarget_t){target,target->spawn_time};G_RefreshCaptainTargetSubscriptions(captain);
        cstring_t file=Test_TempPath("wc3-target378-fifo.bin");
        if(saved){T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));}
        target363_history_count=0;target363_test_visit=Target363Visit;
        S_UnitTargetLost(target);target363_test_visit=NULL;
        T_EQ(target363_history_count,3u);
        uint32_t expected[]={1,4,2};FOR_LOOP(i,MIN(3,target363_history_count))T_EQ(target363_history[i],expected[i]);
        T_EQ(ARRAY_COUNT(captain->units),0u);T_NULL(captain->follow.unit);
        T_ASSERT(G_ValidateTargetSubscriptions());remove(file);G_BotStop(0);reset_entities();setup_test_world();
    }
}

/* Native679bc0 commits dead/life state, then publishes651010 lost and
 *650d80 death, before673610 cancels the old public queue/physical task.
 * This observes that registry boundary only. Public script death event
 * ordering is deliberately left to the separately classified event bridge. */
static edict_t *target378_death_source,*target378_death_follower;
static botCaptain_t *target378_death_captain;
static uint32_t target378_death_order,target378_death_group,target378_death_visits;
static void *target378_death_move;
static void Target378DeathVisit(targetSubscriberKind_t kind,void *owner,edict_t *target) {
    if(target!=target378_death_source)return;
    bool lost=kind==TARGET_OWNER_MOVE && owner==target378_death_follower;
    bool death=kind==TARGET_OWNER_CAPTAIN && owner==target378_death_captain;
    if(!lost && !death)return;
    T_EQ(target378_death_visits,lost ? 0u : 1u);target378_death_visits++;
    T_ASSERT(target->svflags&SVF_DEADMONSTER);
    T_ASSERT(M_IsDead(target));T_FEQ(target->health.value,0,0);
    T_EQ(target->current_order_id,target378_death_order);
    T_EQ(target->order_queue.count,1u);
    if(target->order_queue.count) {
        unitOrder_t const *next=target->order_queue.entries+target->order_queue.head;
        T_STREQ(next->order,"holdposition");T_EQ(next->order_id,G_OrderId("holdposition"));
    }
    T_EQ((void *)target->currentmove,target378_death_move);
    T_EQ(target->movement.group_id,target378_death_group);
    for(unitAbilityDisable_t *state=target->ability_disables;state;state=state->next)
        T_ASSERT(!state->retire_owned);
    T_EQ(ARRAY_COUNT(target378_death_captain->units),1u);
}
TEST(wc3_movement,target378_death_registry_precedes_old_order_and_physical_cancellation) {
    FOR_LOOP(saved,2) {
        target378_death_captain=Target378CaptainWorld();
        target378_death_source=target363_kernel_target;
        target378_death_follower=target363_kernel_units[0];
        /* Use one independent Move subscriber so its lost callback cannot
         * change the dying source's own public/physical task. */
        T_ASSERT(unit_issueimmediateorder(target363_kernel_units[1],"stop"));
        T_ASSERT(unit_issueorder(target378_death_source,"move",&(vec2_t){1400,1400}));
        T_ASSERT(G_QueueUnitOrder(target378_death_source,"holdposition",UNIT_ORDER_TARGET_NONE,
                                 NULL,NULL,0,0,G_OrderId("holdposition")));
        cstring_t file=Test_TempPath("wc3-target378-early-death.bin");
        if(saved){T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));}
        target378_death_order=target378_death_source->current_order_id;
        target378_death_move=(void *)target378_death_source->currentmove;
        target378_death_group=target378_death_source->movement.group_id;
        T_EQ(target378_death_order,G_OrderId("move"));
        T_NOT_NULL(target378_death_move);T_ASSERT(target378_death_group!=0);
        T_EQ(target378_death_source->order_queue.count,1u);
        target378_death_visits=0;target363_test_visit=Target378DeathVisit;
        G_SetHealth(target378_death_source,0);unit_die(target378_death_source,NULL);
        target363_test_visit=NULL;
        T_EQ(target378_death_visits,2u);
        T_EQ(ARRAY_COUNT(target378_death_captain->units),0u);
        T_EQ(target378_death_source->current_order_id,0u);
        T_EQ(target378_death_source->order_queue.count,0u);
        T_EQ(target378_death_source->movement.group_id,0u);
        T_ASSERT(G_ValidateTargetSubscriptions());remove(file);
        G_BotStop(0);reset_entities();setup_test_world();
    }
}

#endif
