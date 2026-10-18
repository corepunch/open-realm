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

#endif
