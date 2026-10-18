#ifdef BZ_TESTS
#include "shared/test.h"
#include "../skills/s_skills.h"

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void setup_test_world(void);
void CM_SetupTestWorldBounds(box2_t const *);
void reset_entities(void);
bool run_test_jass(cstring_t src);
void order_attack(edict_t *self, edict_t *target);
void T_Damage(edict_t *target, edict_t *attacker, int damage);
void G_RunEntities(void);
void monster_think(edict_t *self);
void SV_Physics_Toss(edict_t *ent);
void unit_build(edict_t *self, uint32_t class_id);
void attack_melee_cooldown(edict_t *self);
uint64_t G_TestAttackTargetPriority(edict_t const *,edict_t const *);
void ai_train_build(edict_t *self);
static slkTestData_t *building_install_repair_data(slkTestData_t **rows_out);
static void building_restore_repair_data(slkTestData_t *old, slkTestData_t *rows);

static bool lifecycle_stop_frame_seen, lifecycle_hold_frame_seen;
static bool lifecycle_move_frame_seen;
static uint32_t lifecycle_stop_frame_flags, lifecycle_hold_frame_flags, lifecycle_move_frame_flags;
static uint32_t lifecycle_stop_frame_texture, lifecycle_hold_frame_texture;

static void lifecycle_capture_command_frame(pfWriteType_t type, void const *value) {
    uiFrame_t const *frame;
    if (type != PF_UIFRAME || !value) return;
    frame = value;
    if (frame->flags.type != FT_COMMANDBUTTON) return;
    if (frame->onclick && !strcmp(frame->onclick, "button CmdStop")) {
        lifecycle_stop_frame_seen = true;
        lifecycle_stop_frame_flags = frame->flagsvalue;
        lifecycle_stop_frame_texture = frame->tex.index;
    } else if (frame->onclick && !strcmp(frame->onclick, "button CmdHoldPos")) {
        lifecycle_hold_frame_seen = true;
        lifecycle_hold_frame_flags = frame->flagsvalue;
        lifecycle_hold_frame_texture = frame->tex.index;
    } else if (frame->onclick && !strcmp(frame->onclick, "button CmdMove")) {
        lifecycle_move_frame_seen = true;
        lifecycle_move_frame_flags = frame->flagsvalue;
    }
}

/* Keep production order, acquisition, and death entry points active in this review fixture. */
static edict_t *review_order_unit_at(float x, float y, uint32_t owner) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), x, y);
    ((mapInfo_t *)level.mapinfo)->players[owner].playerType = kPlayerTypeHuman;
    ent->s.player = owner;
    ent->svflags |= SVF_MONSTER;
    ent->movetype = MOVETYPE_STEP;
    ent->stand = unit_stand;
    ent->die = unit_die;
    ent->unitinfo.MoveSpeed = 300;
    S_AttackProfileWrite(ent, 0)->type = ATK_NORMAL;
    S_AttackProfileWrite(ent, 0)->range = 30;
    S_AttackProfileWrite(ent, 0)->cooldown = 1;
    S_AttackProfileWrite(ent, 0)->damageBase = 10;
    S_AttackProfileWrite(ent, 0)->targetsAllowed = WC3_TARGET_FLAG_GROUND | WC3_TARGET_FLAG_STRUCTURE;
    ent->targtype = TARG_GROUND;
    ent->runtime.acquisition_range = 600;
    unit_stand(ent);
    gi.LinkEntity(ent);
    return ent;
}

static edict_t *review_order_unit(float x, uint32_t owner) { return review_order_unit_at(x, 0, owner); }
TEST(wc3_order_lifecycle, unused_queue_is_sparse_and_wrapped_entries_survive_save) {
    unsigned const pending_limit=MAX_UNIT_ORDER_QUEUE+1; /* No executing user head. */
    cstring_t file = Test_TempPath("wc3-sparse-order-ring.bin");
    reset_entities(); setup_test_world();
    edict_t *unit = review_order_unit(0, 0);
    T_NULL(unit->order_queue.entries);
    G_ClearUnitOrderQueue(unit);
    T_NULL(unit->order_queue.entries);
    FOR_LOOP(i, pending_limit) {
        vec2_t point = { (float)i, -(float)i };
        T_ASSERT(G_QueueUnitOrder(unit, "holdposition", UNIT_ORDER_TARGET_NONE,
                                 &point, NULL, 0, 0, i));
    }
    unitOrder_t *storage = unit->order_queue.entries;
    T_NOT_NULL(storage);
    T_ASSERT(!G_QueueUnitOrder(unit, "holdposition", UNIT_ORDER_TARGET_NONE, NULL, NULL, 0, 0, 100));
    T_ASSERT(G_UnitStartNextQueuedOrder(unit));
    T_EQ(unit->order_queue.head, 1); T_EQ(unit->order_queue.count, pending_limit - 1);
    vec2_t last = { 900, 800 };
    T_ASSERT(G_QueueUnitOrder(unit, "holdposition", UNIT_ORDER_TARGET_NONE, &last, NULL, 3, 7, 999));
    T_ASSERT(WriteGame(file));
    G_ClearUnitOrderQueue(unit);
    T_NULL(unit->order_queue.entries);
    T_EQ(unit->order_queue.count, 0);
    T_ASSERT(ReadGame(file));
    T_EQ(unit->order_queue.head, 1); T_EQ(unit->order_queue.count, pending_limit);
    FOR_LOOP(i, pending_limit) {
        unitOrder_t const *entry = unit->order_queue.entries + unit->order_queue.head;
        T_EQ(entry->order_id, i == pending_limit - 1 ? 999 : i + 1);
        T_STREQ(entry->order, "holdposition");
        T_FEQ(entry->point.x, i == pending_limit - 1 ? 900 : i + 1, 0);
        T_ASSERT(G_UnitStartNextQueuedOrder(unit));
    }
    T_EQ(unit->order_queue.head, 0); T_EQ(unit->order_queue.count, 0);
    T_NULL(unit->order_queue.entries);
    T_ASSERT(WriteGame(file));
    T_ASSERT(ReadGame(file));
    T_NULL(unit->order_queue.entries);
    G_FreeEdict(unit);
    T_NULL(unit->order_queue.entries);
    remove(file);
    reset_entities(); setup_test_world();
}

/* Retail693490 admits user_count <501. One active Move leaves500 pending
 * commands; the next Shift must preserve the current head and the full FIFO. */
TEST(wc3_order_lifecycle, queue198_retail_ceiling_and_saved_successors) {
    reset_entities(); setup_test_world();
    edict_t *unit=review_order_unit(0,0);
    T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){800,0},false,0,0));
    unsigned accepted=0;
    FOR_LOOP(i,500) accepted+=G_IssueUnitPointOrder(unit,"move",&(vec2_t){(float)i,64},true,0,0);
    T_EQ(accepted,500);T_EQ(unit->order_queue.count,500);
    if(accepted!=500) {G_ClearUnitOrderQueue(unit);reset_entities();setup_test_world();return;}
    umove_t const *active=unit->currentmove;
    T_ASSERT(!G_IssueUnitPointOrder(unit,"move",&(vec2_t){900,64},true,0,0));
    T_ASSERT(unit->currentmove==active);T_EQ(unit->order_queue.count,500);
    uint32_t number=unit->s.number;
    cstring_t file=Test_TempPath("wc3-queue198.bin");
    T_ASSERT(WriteGame(file));
    G_ClearUnitOrderQueue(unit);T_NULL(unit->order_queue.entries);
    T_ASSERT(ReadGame(file));unit=g_edicts+number;
    T_EQ(unit->order_queue.count,500);
    FOR_LOOP(i,500) {
        unitOrder_t const *entry=unit->order_queue.entries+unit->order_queue.head;
        T_STREQ(entry->order,"move");T_FEQ(entry->point.x,i,0);T_FEQ(entry->point.y,64,0);
        T_ASSERT(G_UnitStartNextQueuedOrder(unit));
    }
    T_EQ(unit->order_queue.count,0);T_NULL(unit->order_queue.entries);
    remove(file);reset_entities();setup_test_world();
}

TEST(wc3_order_lifecycle, queue198_wrapped_growth_preserves_commands_and_reset_releases_storage) {
    reset_entities();setup_test_world();
    edict_t *unit=review_order_unit(0,0);
    FOR_LOOP(i,17) T_ASSERT(G_QueueUnitOrder(unit,"holdposition",UNIT_ORDER_TARGET_NONE,
        &(vec2_t){i,-(float)i},NULL,0,0,i));
    FOR_LOOP(i,8) T_ASSERT(G_UnitStartNextQueuedOrder(unit));
    FOR_LOOP(i,8) T_ASSERT(G_QueueUnitOrder(unit,"holdposition",UNIT_ORDER_TARGET_NONE,
        &(vec2_t){i+17,-(float)(i+17)},NULL,0,0,i+17));
    T_EQ(unit->order_queue.head,8);T_EQ(unit->order_queue.count,17);
    T_EQ(unit->order_queue.capacity,UNIT_ORDER_INITIAL_CAPACITY);
    T_ASSERT(G_QueueUnitOrder(unit,"holdposition",UNIT_ORDER_TARGET_NONE,
        &(vec2_t){25,-25},NULL,0,0,25));
    T_ASSERT(unit->order_queue.capacity>UNIT_ORDER_INITIAL_CAPACITY);
    FOR_LOOP(i,3) T_ASSERT(G_UnitStartNextQueuedOrder(unit));
    cstring_t file=Test_TempPath("wc3-queue198-wrapped.bin");
    uint32_t number=unit->s.number;
    T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));unit=g_edicts+number;
    T_EQ(unit->order_queue.count,15);
    FOR_LOOP(i,15) {
        unitOrder_t const *order=unit->order_queue.entries+unit->order_queue.head;
        T_EQ(order->order_id,i+11);T_FEQ(order->point.x,i+11,0);T_FEQ(order->point.y,-(float)(i+11),0);
        T_ASSERT(G_UnitStartNextQueuedOrder(unit));
    }
    T_NULL(unit->order_queue.entries);T_EQ(unit->order_queue.capacity,0);
    FOR_LOOP(i,40) T_ASSERT(G_QueueUnitOrder(unit,"holdposition",UNIT_ORDER_TARGET_NONE,NULL,NULL,0,0,i));
    G_PoolsReset();T_NULL(unit->order_queue.entries);T_EQ(unit->order_queue.capacity,0);
    T_EQ(unit->order_queue.head,0);T_EQ(unit->order_queue.count,0);
    remove(file);reset_entities();setup_test_world();
}

TEST(wc3_order_lifecycle, queue198_suspended_user_head_counts_toward_501) {
    reset_entities();setup_test_world();
    edict_t *unit=review_order_unit(0,0);
    G_DeferFreeEdict(unit);T_ASSERT(G_IsDeferredFree(unit));
    unsigned accepted=0;
    FOR_LOOP(i,501)accepted+=G_IssueUnitPointOrder(unit,"move",&(vec2_t){200+i,64},true,0,0);
    T_EQ(accepted,501);T_EQ(unit->order_queue.count,501);
    T_ASSERT(!G_IssueUnitPointOrder(unit,"move",&(vec2_t){900,64},true,0,0));
    T_EQ(unit->current_order_id,G_OrderId("move"));
    G_TestFinishDeferredFrees();T_ASSERT(!unit->inuse);T_NULL(unit->order_queue.entries);
    reset_entities();setup_test_world();
}

static abilityProc_t queue198_move_proc;
static edict_t *queue198_other;
static bool queue198_growth_seen;
static intptr_t queue198_cancel_grows_ring(edict_t *unit,abilityMsg_t msg,abilityCall_t const *call) {
    if(msg==A_QUEUE_ORDER_CANCEL && !queue198_growth_seen) {
        queue198_growth_seen=true;
        uint32_t id=call->queued_order->order_id;
        vec2_t point=call->queued_order->point;
        T_ASSERT(G_QueueUnitOrder(unit,"move",UNIT_ORDER_TARGET_POINT,&(vec2_t){300,400},NULL,0,0,999));
        /* Growth returns the short bucket; this unit immediately reuses it.
         * The cancel handler still owns its original command through unwind. */
        T_ASSERT(G_QueueUnitOrder(queue198_other,"holdposition",UNIT_ORDER_TARGET_NONE,NULL,NULL,0,0,555));
        T_EQ(call->queued_order->order_id,id);
        T_STREQ(call->queued_order->order,"move");
        T_FEQ(call->queued_order->point.x,point.x,0);T_FEQ(call->queued_order->point.y,point.y,0);
    }
    return queue198_move_proc(unit,msg,call);
}

TEST(wc3_order_lifecycle, queue198_cancel_payload_survives_ring_growth_and_bucket_reuse) {
    reset_entities();setup_test_world();
    edict_t *unit=review_order_unit(0,0);
    queue198_other=review_order_unit(32,0);
    FOR_LOOP(i,17)T_ASSERT(G_QueueUnitOrder(unit,"move",UNIT_ORDER_TARGET_POINT,
        &(vec2_t){100+i,64},NULL,0,0,i+101));
    ability_t const *move=FindAbilityByClassname("Amov");T_NOT_NULL(move);if(!move)return;
    queue198_move_proc=move->proc;queue198_growth_seen=false;
    S_ReplaceAbilityProcedure(move,queue198_cancel_grows_ring);
    G_ClearUnitOrderQueue(unit);
    S_ReplaceAbilityProcedure(move,queue198_move_proc);
    T_ASSERT(queue198_growth_seen);T_NULL(unit->order_queue.entries);
    T_EQ(queue198_other->order_queue.count,1);
    G_ClearUnitOrderQueue(queue198_other);queue198_other=NULL;
    reset_entities();setup_test_world();
}

/* Original public owner transfers: d01a2 subscribers precede d01a5.
 * Acquisition is synchronous and retains the public head. An explicit Move
 * has disabled this Attack notification; pausing removes world eligibility. */
TEST(wc3_order_lifecycle, available260_owner_transfer_acquires_without_waiting_for_ai_poll) {
    FOR_LOOP(mode,9) {
        reset_entities();setup_test_world();
        level.timer_clock_valid=false;level.pathing_clock=(wc3Clock_t){8,0,300};
        edict_t *unit=review_order_unit(0,0),*target=review_order_unit(256,0);
        unit->s.model=target->s.model=1;unit->collision=target->collision=16;
        G_PublishMoveSpatialObject(unit);G_PublishMoveSpatialObject(target);
        unit->runtime.acquisition_range=300;
        if(mode==1)T_ASSERT(G_IssueUnitPointOrder(unit,"attack",&(vec2_t){900,0},false,0,0));
        if(mode==2)T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){900,0},false,0,0));
        if(mode==3)unit->paused=true;
        if(mode==4)G_SetPlayerAlliance(&game.clients[0].ps,&game.clients[1].ps,ALLIANCE_PASSIVE,true);
        if(mode==5){target->s.origin.x=600;gi.LinkEntity(target);G_PublishMoveSpatialObject(target);}
        if(mode==7)target->invulnerable=true;
        if(mode==8)target->health.value=0;
        uint32_t order=unit->current_order_id;
        if(mode==6)G_SetUnitPlayer(target,0);else G_SetUnitPlayer(target,1);
        T_EQ(unit->current_order_id,order);
        if(mode<2) {
            T_ASSERT(unit->attack_speed_cap.active);
            T_FEQ(unit->attack_speed_cap.deadline.time,11,0);
            T_EQ(unit->goalentity,target);
            if(mode==1)T_NOT_NULL(unit->movement.attackmove_waypoint);
        } else T_ASSERT(!unit->attack_speed_cap.active);
    }
    reset_entities();setup_test_world();
}

/* Explicit target ownership disables the native availability subscription.
 * A pending command cannot change the executing owner's policy. */
TEST(wc3_order_lifecycle, subscription262_explicit_attack_defers_availability_until_owner_release) {
    FOR_LOOP(mode,10) {
        reset_entities();setup_test_world();
        level.timer_clock_valid=false;level.pathing_clock=(wc3Clock_t){8,0,300};
        edict_t *unit=review_order_unit(0,0),*old=review_order_unit(300,1),*candidate=review_order_unit(100,0);
        unit->s.model=old->s.model=candidate->s.model=1;
        unit->collision=old->collision=candidate->collision=16;
        G_PublishMoveSpatialObject(unit);G_PublishMoveSpatialObject(old);G_PublishMoveSpatialObject(candidate);
        if(mode==2 || mode==6 || mode==9) {
            T_ASSERT(G_IssueUnitPointOrder(unit,"attack",&(vec2_t){900,0},false,0,0));
        } else if(mode!=3) {
            T_ASSERT(G_IssueUnitTargetOrder(unit,mode==1 ? "attackonce" : "attack",old,false,0));
        }
        if(mode==4)T_ASSERT(unit_issueimmediateorder(unit,"stop"));
        if(mode==5)T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){900,0},true,0,0));
        if(mode==6 || mode==9)T_ASSERT(G_IssueUnitTargetOrder(unit,"attack",old,true,0));
        if(mode==7)T_ASSERT(!G_IssueUnitTargetOrder(unit,"attack",NULL,false,0));
        if(mode==5 || mode==6 || mode==9)T_EQ(unit->order_queue.count,1);
        if(mode==9)unit_stand(unit); /* Activate the queued target owner. */
        if(mode==8) {
            uint32_t u=unit->s.number,c=candidate->s.number,o=old->s.number;
            cstring_t file=Test_TempPath("wc3-subscription262.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            unit=g_edicts+u;candidate=g_edicts+c;old=g_edicts+o;
        }
        uint32_t head=unit->current_order_id;
        T_ASSERT(!unit->attack_speed_cap.active);
        G_SetUnitPlayer(candidate,1);
        bool enabled=mode==2 || mode==3 || mode==4 || mode==6;
        T_EQ(unit->attack_speed_cap.active,enabled);
        T_EQ(unit->current_order_id,head);
        if(enabled)T_EQ(unit->attack_target,candidate);
        else T_EQ(unit->attack_target,old);
    }
    reset_entities();setup_test_world();
}

/* Original49d680/49e3a0: weapon relevance precedes range, retained in-range
 * targets precede distance, and equal ranks replace only for strict proximity. */
TEST(wc3_order_lifecycle, ranking264_available_target_competes_with_retained_combat) {
    FOR_LOOP(mode,8) {
        reset_entities();setup_test_world();
        level.timer_clock_valid=false;level.pathing_clock=(wc3Clock_t){8,0,300};
        level.show_map_cheat=true; /* Original probe disables fog and mask. */
        float oldx=588,newx=128;
        if(mode==1){oldx=128;newx=96;}
        if(mode==2)newx=388;
        if(mode==3){oldx=388;newx=588;}
        if(mode==4)newx=oldx;
        edict_t *unit=review_order_unit(0,0),*old=review_order_unit(oldx,1),*candidate=review_order_unit(newx,0);
        unit->s.model=old->s.model=candidate->s.model=1;
        unit->collision=old->collision=candidate->collision=31;
        unit->invulnerable=true;unit->runtime.acquisition_range=700;
        unit->defense_type=old->defense_type=candidate->defense_type=2;
        S_AttackProfileWrite(unit,0)->range=90;
        if(mode==5)T_ASSERT(G_ActorRemoveSkill(candidate,MAKEFOURCC('A','a','t','k')));
        if(mode==6)T_ASSERT(G_ActorRemoveSkill(candidate,MAKEFOURCC('A','m','o','v')));
        G_PublishMoveSpatialObject(unit);G_PublishMoveSpatialObject(old);G_PublishMoveSpatialObject(candidate);
        T_ASSERT(G_IssueUnitPointOrder(unit,"attack",&(vec2_t){1500,0},false,0,0));
        order_attack(unit,old);T_EQ(unit->attack_target,old);
        T_ASSERT(!unit->attack_acquisition_suppressed);
        if(mode==7) {
            uint32_t u=unit->s.number,o=old->s.number,c=candidate->s.number;
            UnitWeapons_t const *weapons=unit->data.UnitWeapons;
            cstring_t file=Test_TempPath("wc3-ranking264.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            unit=g_edicts+u;old=g_edicts+o;candidate=g_edicts+c;
            /* This synthetic archive has no UnitWeapons.slk. Rebind the same
             * authored fixture row; runtime weapon overrides came from save. */
            unit->data.UnitWeapons=old->data.UnitWeapons=candidate->data.UnitWeapons=weapons;
        }
        uint64_t incoming=G_TestAttackTargetPriority(unit,candidate),retained=G_TestAttackTargetPriority(unit,old);
        T_EQ(incoming>>32,mode==5 ? 0x0ba00000u : 0x0be00000u);
        T_EQ((uint32_t)incoming,mode==2 || mode==3 || mode==4 ? 0x60000000u : mode==6 ? 0x44000000u : 0x64000000u);
        T_EQ(retained>>32,0x0be00000u);
        T_EQ((uint32_t)retained,mode==1 ? 0x65000000u : 0x60000000u);
        uint32_t head=unit->current_order_id;
        edict_t *waypoint=unit->movement.attackmove_waypoint;
        wc3Random_t random=level.pathing_random;
        G_SetUnitPlayer(candidate,1);
        bool replace=mode==0 || mode==2 || mode==7;
        T_EQ(unit->attack_target,replace ? candidate : old);
        T_EQ(unit->goalentity,replace ? candidate : old);
        T_EQ(unit->current_order_id,head);
        T_EQ(unit->movement.attackmove_waypoint,waypoint);
        T_ASSERT(unit->attack_speed_cap.active);
        T_EQ(level.pathing_random.sum,random.sum);T_EQ(level.pathing_random.index,random.index);
    }
    reset_entities();setup_test_world();
}

/* Original49d8e2 queries the attacking owner's level, not the candidate's.
 * Cross the level7 boundary independently on each side, for Hero and ordinary
 * units, and keep player-controlled owners outside the neutral rule. */
TEST(wc3_order_lifecycle, group338_neutral_priority_uses_attacker_level) {
    FOR_LOOP(human,2)FOR_LOOP(hero,2)FOR_LOOP(source_high,2)FOR_LOOP(target_high,2) {
        reset_entities();setup_test_world();
        edict_t *unit=review_order_unit(0,human ? 0 : PLAYER_NEUTRAL_AGGRESSIVE);
        edict_t *target=review_order_unit(300,1);
        UnitBalance_t source_balance=*unit->data.UnitBalance,target_balance=*target->data.UnitBalance;
        source_balance.level=source_high ? 7 : 6;
        source_balance.strength=hero ? 20 : 0;
        source_balance.agility=source_balance.intelligence=0;
        target_balance.level=target_high ? 7 : 1;
        target_balance.strength=target_balance.agility=target_balance.intelligence=0;
        unit->data.UnitBalance=&source_balance;target->data.UnitBalance=&target_balance;
        unit->hero.level=source_balance.level;
        uint64_t key=G_TestAttackTargetPriority(unit,target);
        T_ASSERT(key!=0);
        T_EQ((uint32_t)(key>>32)&0x00200000u,human || source_high ? 0x00200000u : 0u);
        unit->data.UnitBalance=G_UnitBalance(unit->class_id);
        target->data.UnitBalance=G_UnitBalance(target->class_id);
    }
    reset_entities();setup_test_world();
}

/* 49d8b2 asks whether the victim grants Shared Spells to the observer.
 * Passive alliance and the reverse spell grant must not substitute for it. */
TEST(wc3_order_lifecycle, group338_defending_priority_uses_victim_spell_grant) {
    FOR_LOOP(passive,2)FOR_LOOP(forward,2)FOR_LOOP(reverse,2) {
        reset_entities();setup_test_world();
        edict_t *unit=review_order_unit(0,0),*candidate=review_order_unit(300,1);
        edict_t *victim=review_order_unit(400,2);
        G_SetPlayerAlliance(&game.clients[0].ps,&game.clients[2].ps,ALLIANCE_PASSIVE,passive);
        G_SetPlayerAlliance(&game.clients[2].ps,&game.clients[0].ps,ALLIANCE_SHARED_SPELLS,forward);
        G_SetPlayerAlliance(&game.clients[0].ps,&game.clients[2].ps,ALLIANCE_SHARED_SPELLS,reverse);
        order_attack(candidate,victim);
        T_EQ(candidate->attack_target,victim);
        uint64_t key=G_TestAttackTargetPriority(unit,candidate);
        T_ASSERT(key!=0);
        T_EQ((uint32_t)key&0x00400000u,forward ? 0x00400000u : 0u);
    }
    reset_entities();setup_test_world();
}

/*49d8bd uses the retained candidate's authored nonzero minimum-range flag.
 * It is independent of the observer's range and of current in-range stickiness. */
TEST(wc3_order_lifecycle, group355_retained_minimum_range_priority_and_public_replacement) {
    float const minima[]={0,120,600,-24,-0.0f};
    FOR_LOOP(saved,2)FOR_LOOP(mode,4)FOR_LOOP(kind,sizeof(minima)/sizeof(*minima)) {
        reset_entities();setup_test_world();level.time=1000;level.show_map_cheat=true;
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{4096,4096}});
        edict_t *unit=review_order_unit(512,0),*retained=review_order_unit(1500,1),*incoming=review_order_unit(1400,0);
        unit->s.model=retained->s.model=incoming->s.model=1;
        unit->collision=retained->collision=incoming->collision=31;
        unit->defense_type=retained->defense_type=incoming->defense_type=2;
        unit->invulnerable=true;unit->runtime.acquisition_range=1100;
        UnitWeapons_t const *authored=retained->data.UnitWeapons;
        UnitWeapons_t weapons=*authored;weapons.minimumAttackRange=minima[kind];weapons.attacksEnabled=1;
        retained->data.UnitWeapons=&weapons;
        S_AttackProfileWrite(unit,0)->range=400;
        T_ASSERT(unit_issueimmediateorder(retained,"holdposition"));
        T_ASSERT(unit_issueimmediateorder(incoming,"holdposition"));
        if(mode==1)S_SetUnitPaused(retained,true);
        if(mode==2)T_ASSERT(G_ActorRemoveSkill(retained,MAKEFOURCC('A','a','t','k')));
        G_PublishMoveSpatialObject(unit);G_PublishMoveSpatialObject(retained);G_PublishMoveSpatialObject(incoming);
        T_ASSERT(G_IssueUnitPointOrder(unit,"attack",&(vec2_t){1800,0},false,0,0));
        order_attack(unit,retained);T_EQ(unit->attack_target,retained);
        if(mode==3) {
            /* The same minimum-range candidate has no retained-target bonus
             * when another actor is asking the question. */
            edict_t *observer=review_order_unit(512,2);observer->invulnerable=true;
            T_EQ((uint32_t)G_TestAttackTargetPriority(observer,retained)&0x00400000u,0u);
        }
        if(saved) {
            UnitWeapons_t const *source=unit->data.UnitWeapons,*next=incoming->data.UnitWeapons;
            cstring_t file=Test_TempPath("wc3-group355-retained-minimum.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            unit->data.UnitWeapons=source;incoming->data.UnitWeapons=next;retained->data.UnitWeapons=&weapons;
            T_EQ(unit->attack_target,retained);
        }
        bool defending=minima[kind]!=0 && mode!=1 && mode!=2;
        uint64_t key=G_TestAttackTargetPriority(unit,retained);
        T_EQ((uint32_t)key&0x00400000u,defending ? 0x00400000u : 0u);
        T_EQ((uint32_t)key&0x05000000u,0u); /* Both candidates are out of range. */
        uint32_t head=unit->current_order_id;
        G_SetUnitPlayer(incoming,1);
        T_EQ(unit->attack_target,defending ? retained : incoming);
        T_EQ(unit->current_order_id,head);
        /* Stop removes the retained identity; the authored minimum alone
         * must not confer defending priority on later independent queries. */
        T_ASSERT(unit_issueimmediateorder(unit,"stop"));
        T_EQ((uint32_t)G_TestAttackTargetPriority(unit,retained)&0x00400000u,0u);
        retained->data.UnitWeapons=authored;
    }
    reset_entities();setup_test_world();
}

/* Retail359: Freeze's Stun owner disables attached abilities; its true v188
 * never bypasses the candidate's disabled Attack. Test the real hit path. */
TEST(wc3_order_lifecycle, group359_freeze_hit_disables_orders_priority_and_saved_inverse) {
    FOR_LOOP(saved,2)FOR_LOOP(timed,2)FOR_LOOP(custom,2) {
        reset_entities();setup_test_world();level.time=1000;level.show_map_cheat=true;
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{4096,4096}});
        char slk[1800];snprintf(slk,sizeof(slk),
            "ID;PWXL;N;EBB;Y2;X6\nC;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
            "C;Y1;X3;K\"Dur1\"\nC;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"BuffID1\"\nC;Y1;X6;K\"DataA1\"\n"
            "C;Y2;X1;K\"AIzb\"\nC;Y2;X2;K\"AIzb\"\nC;Y2;X3;K\"%s\"\nC;Y2;X4;K\"%s\"\n"
            "C;Y2;X5;K\"%s\"\nC;Y2;X6;K\"9\"\nE\n",timed ? "0.375" : "0",timed ? "0.375" : "0",custom ? "Bz59" : "Bfre");
        slkTestData_t *rows=parse_slk_string(slk),*old=G_SetSLKRows("AbilityData",rows);
        edict_t *observer=review_order_unit(512,0),*target=review_order_unit(1500,1),*caster=review_order_unit(1700,2);
        observer->invulnerable=true;target->health.value=target->health.max_value=10000;
        observer->defense_type=target->defense_type=2;
        T_ASSERT(G_ActorAddSkill(caster,MAKEFOURCC('A','I','z','b')));
        if(timed)T_ASSERT(unit_issueorder(target,"move",&(vec2_t){1500,2500}));
        else T_ASSERT(unit_issuetargetorder(target,"attack",caster));
        T_ASSERT(target->current_order_id!=0);
        uint64_t before=G_TestAttackTargetPriority(observer,target);
        T_ASSERT(before!=0);T_ASSERT(((uint32_t)(before>>32)&0x00400000u)!=0);
        S_ResolveAttackHit(caster,target,10);
        uint32_t buff=custom ? MAKEFOURCC('B','z','5','9') : MAKEFOURCC('B','f','r','e');
        T_ASSERT(S_UnitHasStatus(target,buff));
        T_ASSERT(S_UnitOrderDisabled(target,"move"));T_ASSERT(S_UnitOrderDisabled(target,"attack"));
        T_EQ(target->current_order_id,0);
        T_ASSERT(!unit_issueorder(target,"move",&(vec2_t){1500,2500}));
        T_EQ(target->current_order_id,0);
        uint64_t frozen=G_TestAttackTargetPriority(observer,target);
        T_EQ((uint32_t)(frozen>>32)&0x00400000u,0u);
        T_EQ((uint32_t)frozen&0x20000000u,0u);T_EQ((uint32_t)frozen&0x00400000u,0u);
        if(custom) {
            G_FreeEdict(caster);T_ASSERT(!caster->inuse);
            T_ASSERT(S_UnitHasStatus(target,buff));
            T_EQ(G_TestAttackTargetPriority(observer,target),frozen);
        }
        if(saved) {
            cstring_t file=Test_TempPath("wc3-group359-freeze.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            reattach_test_weapons(observer);reattach_test_weapons(target);if(caster->inuse)reattach_test_weapons(caster);
            T_ASSERT(S_UnitOrderDisabled(target,"move"));
            T_EQ(G_TestAttackTargetPriority(observer,target),frozen);
        }
        /* Script pause is an independent owner: buff release cannot resume it. */
        S_SetUnitPaused(target,true);
        if(timed) {level.time+=376;unit_updatestatuses(target);}
        else T_EQ(unit_removebuffs(target,false,true,false,false,false,true,false),1u);
        T_ASSERT(!S_UnitHasStatus(target,buff));T_ASSERT(S_UnitOrderDisabled(target,"move"));
        S_SetUnitPaused(target,false);
        T_ASSERT(!S_UnitOrderDisabled(target,"move"));
        /* Pause owns its own resumed head; compare restored eligibility,
         * independently of the Attack head that Freeze correctly retired. */
        uint64_t restored=G_TestAttackTargetPriority(observer,target);
        T_EQ((uint32_t)(restored>>32)&0x00400000u,(uint32_t)(before>>32)&0x00400000u);
        T_EQ((uint32_t)restored&0x20000000u,(uint32_t)before&0x20000000u);
        G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    }
    reset_entities();setup_test_world();
}

/* Afrz is a passive structure-hit owner, independently disabling execution
 * (Unit1b8) while Freeze above disables both execution and new orders (1c4). */
TEST(wc3_order_lifecycle, group359_freezing_breath_hit_keeps_distinct_disable_owner) {
    FOR_LOOP(saved,2) {
        reset_entities();setup_test_world();level.time=1000;level.show_map_cheat=true;
        slkTestData_t *rows=parse_slk_string(
            "ID;PWXL;N;EBB;Y2;X7\nC;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
            "C;Y1;X3;K\"Dur1\"\nC;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"BuffID1\"\n"
            "C;Y1;X6;K\"targs1\"\nC;Y1;X7;K\"Area1\"\n"
            "C;Y2;X1;K\"Afrz\"\nC;Y2;X2;K\"Afrz\"\nC;Y2;X3;K\"5\"\n"
            "C;Y2;X4;K\"5\"\nC;Y2;X5;K\"Bfrz\"\n"
            "C;Y2;X6;K\"structure,enemy\"\nC;Y2;X7;K\"100\"\nE\n"),*old=G_SetSLKRows("AbilityData",rows);
        edict_t *caster=review_order_unit(1700,2),*target=review_order_unit(1500,1),*observer=review_order_unit(512,0);
        target->targtype=TARG_STRUCTURE;target->aiflags|=AI_IMMOBILE;
        target->s.flags|=EF_BUILDING;target->runtime.flags|=UNIT_BALANCE_BUILDING;
        target->health.value=target->health.max_value=10000;observer->invulnerable=true;
        T_ASSERT(G_ActorAddSkill(caster,MAKEFOURCC('A','f','r','z')));
        abilityitem_t item=S_AbilityItem(MAKEFOURCC('A','f','r','z'));
        T_ASSERT((item.ability->flags&AB_PASSIVE)!=0);T_ASSERT((item.ability->flags&AB_SPELL)==0);
        /* Native hit handlers58ff90/590110 reject the source's positive3c
         * before creating an effect. Exercise that exact weapon-hit owner. */
        S_AcquireBuffAbilityDisables(caster,false);
        T_ASSERT(S_AbilityExecutionDisabled(caster,MAKEFOURCC('A','f','r','z')));
        S_FreezingBreathOnHit(caster,target);
        T_ASSERT(!S_UnitHasStatus(target,MAKEFOURCC('B','f','r','z')));
        unit_removebuffs(target,false,true,false,false,false,true,false);
        S_ReleaseBuffAbilityDisables(caster,false);
        gameClient_t *owner=G_GetPlayerClientByNumber(caster->s.player);
        T_NOT_NULL(owner);owner->ps.number=caster->s.player;
        G_SetPlayerAbilityAvailable(owner,MAKEFOURCC('A','f','r','z'),false);
        S_FreezingBreathOnHit(caster,target);
        T_ASSERT(!S_UnitHasStatus(target,MAKEFOURCC('B','f','r','z')));
        unit_removebuffs(target,false,true,false,false,false,true,false);
        G_SetPlayerAbilityAvailable(owner,MAKEFOURCC('A','f','r','z'),true);
        uint32_t tech=MAKEFOURCC('R','u','f','b');
        int32_t old_tech=G_GetPlayerTechResearchedLevel(owner,tech);
        unitModification_t requirement={.modID=MAKEFOURCC('a','r','e','q'),.type=mod_string,.data="Rufb"};
        unitData_t original={.originalUnitID=MAKEFOURCC('A','f','r','z'),.numbeOfModifications=1,.modifications=&requirement};
        mapInfo_t authored={.num_originalAbilities=1,.originalAbilities=&original};
        G_SetMapAbilityOverrides(&authored);G_SetPlayerTechResearched(owner,tech,0);
        T_ASSERT(!G_AbilityRequirementsSatisfied(caster,MAKEFOURCC('A','f','r','z')));
        S_FreezingBreathOnHit(caster,target);
        T_ASSERT(!S_UnitHasStatus(target,MAKEFOURCC('B','f','r','z')));
        unit_removebuffs(target,false,true,false,false,false,true,false);
        G_SetPlayerTechResearched(owner,tech,1);
        T_ASSERT(G_AbilityRequirementsSatisfied(caster,MAKEFOURCC('A','f','r','z')));
        G_SetMapAbilityOverrides(level.mapinfo);
        S_ResolveAttackHit(caster,target,10);
        T_ASSERT(S_UnitHasStatus(target,MAKEFOURCC('B','f','r','z')));
        bool disabled=false;for(unitAbilityDisable_t const *a=target->ability_disables;a;a=a->next)
            if(a->code==MAKEFOURCC('A','a','t','k')) {T_EQ(a->order_depth,0);disabled=a->depth==1;}
        T_ASSERT(disabled);
        uint64_t frozen=G_TestAttackTargetPriority(observer,target);
        T_EQ((uint32_t)(frozen>>32)&0x00400000u,0u);
        if(saved) {
            cstring_t file=Test_TempPath("wc3-group359-breath.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            reattach_test_weapons(observer);reattach_test_weapons(target);if(caster->inuse)reattach_test_weapons(caster);
            T_ASSERT(S_UnitHasStatus(target,MAKEFOURCC('B','f','r','z')));
            T_EQ(G_TestAttackTargetPriority(observer,target),frozen);
        }
        T_EQ(unit_removebuffs(target,false,true,false,false,false,true,false),1u);
        T_ASSERT(!S_UnitHasStatus(target,MAKEFOURCC('B','f','r','z')));
        T_ASSERT(((uint32_t)(G_TestAttackTargetPriority(observer,target)>>32)&0x00400000u)!=0);
        G_SetPlayerTechResearched(G_GetPlayerClientByNumber(caster->s.player),tech,old_tech);
        G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    }
    reset_entities();setup_test_world();
}

/* A lethal weapon hit must keep the corpse's completed death ownership. */
TEST(wc3_order_lifecycle, group359_lethal_freeze_hit_keeps_death_cleanup) {
    reset_entities();setup_test_world();level.time=1000;
    slkTestData_t *rows=parse_slk_string(
        "ID;PWXL;N;EBB;Y2;X5\nC;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"Dur1\"\nC;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AIzb\"\nC;Y2;X2;K\"AIzb\"\nC;Y2;X3;K\"0\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"Bfre\"\nE\n"),*old=G_SetSLKRows("AbilityData",rows);
    edict_t *caster=review_order_unit(1700,2),*target=review_order_unit(1500,1);
    target->health.value=1;target->defense_type=2;
    T_ASSERT(G_ActorAddSkill(caster,MAKEFOURCC('A','I','z','b')));
    S_ResolveAttackHit(caster,target,10000);
    T_ASSERT(M_IsDead(target));
    T_ASSERT(!S_UnitHasStatus(target,MAKEFOURCC('B','f','r','e')));
    T_EQ(target->ability_disable_depth,0);
    T_EQ(target->ability_exclusive_depth,0);
    G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    reset_entities();setup_test_world();
}

/* Group354 retail: MagicLeashTarget v188 supplies low00400000 while
 * its Attack remains enabled. Pause/Thunder Bolt do not supply that override.
 * Custom BuffID must keep the same gameplay owner and cold-save inverse. */
TEST(wc3_order_lifecycle, group354_shackles_defending_priority_survives_save_and_inverse) {
    cstring_t slk =
        "ID;PWXL;N;EBB;Y3;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\nC;Y1;X4;K\"Rng1\"\n"
        "C;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\nC;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"BuffID1\"\n"
        "C;Y2;X1;K\"Amls\"\nC;Y2;X2;K\"Amls\"\nC;Y2;X3;K\"air,enemy\"\nC;Y2;X4;K\"800\"\n"
        "C;Y2;X5;K\"4.125\"\nC;Y2;X6;K\"4.125\"\nC;Y2;X7;K\"7\"\nC;Y2;X8;K\"Bmlc,Bmlt\"\n"
        "C;Y3;X1;K\"A354\"\nC;Y3;X2;K\"Amls\"\nC;Y3;X3;K\"air,enemy\"\nC;Y3;X4;K\"800\"\n"
        "C;Y3;X5;K\"4.125\"\nC;Y3;X6;K\"4.125\"\nC;Y3;X7;K\"7\"\nC;Y3;X8;K\"Bmlc,B354\"\nE\n";
    slkTestData_t *rows=parse_slk_string(slk),*old=G_SetSLKRows("AbilityData",rows);
    FOR_LOOP(custom,2)FOR_LOOP(saved,2)FOR_LOOP(mode,4) {
        reset_entities();setup_test_world();level.time=1000;level.show_map_cheat=true;
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{4096,4096}});
        edict_t *unit=review_order_unit(512,0),*target=review_order_unit(640,1),*caster=review_order_unit(864,2);
        unit->invulnerable=true;target->health.value=target->health.max_value=1000;
        unit->s.model=target->s.model=caster->s.model=1;
        unit->collision=target->collision=31;target->targtype=TARG_AIR;
        unit->defense_type=target->defense_type=2;
        S_AttackProfileWrite(unit,0)->range=400;
        S_AttackProfileWrite(unit,0)->targetsAllowed|=WC3_TARGET_FLAG_AIR;
        UnitAbilities_t abilities={.abilList=custom ? "A354" : "Amls"};caster->data.UnitAbilities=&abilities;
        uint32_t code=custom ? MAKEFOURCC('A','3','5','4') : MAKEFOURCC('A','m','l','s');
        uint32_t buff=custom ? MAKEFOURCC('B','3','5','4') : MAKEFOURCC('B','m','l','t');
        uint64_t before=G_TestAttackTargetPriority(unit,target);
        T_EQ((uint32_t)before,0x64000000u);T_EQ((uint32_t)(before>>32),0x0be00000u);
        T_ASSERT(S_CastUnitTargetSpell(caster,code,target));
        T_EQ(G_UnitStatusLevel(target,buff),1);
        T_Damage(target,caster,1);T_NULL(target->attack_target);
        T_ASSERT(!S_HumanCanAttack(target));T_FEQ(S_HumanMoveFactor(target),0,0);
        T_EQ((uint32_t)G_TestAttackTargetPriority(unit,target),0x64400000u);
        T_EQ((uint32_t)(G_TestAttackTargetPriority(unit,target)>>32),0x0be00000u);
        if(saved) {
            UnitWeapons_t const *weapons[3]={unit->data.UnitWeapons,target->data.UnitWeapons,caster->data.UnitWeapons};
            cstring_t file=Test_TempPath("wc3-group354-shackles.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            unit->data.UnitWeapons=weapons[0];target->data.UnitWeapons=weapons[1];caster->data.UnitWeapons=weapons[2];
            caster->data.UnitAbilities=&abilities;
            T_EQ((uint32_t)G_TestAttackTargetPriority(unit,target),0x64400000u);
            T_ASSERT(!S_HumanCanAttack(target));T_FEQ(S_HumanMoveFactor(target),0,0);
        }
        if(mode==0) {
            edict_t *retained=review_order_unit(1200,1);
            retained->collision=31;retained->defense_type=2;retained->s.model=1;
            ((mapInfo_t *)level.mapinfo)->players[3].playerType=kPlayerTypeHuman;
            G_PublishMoveSpatialObject(unit);G_PublishMoveSpatialObject(retained);
            unit->runtime.acquisition_range=1100;
            target->s.origin2.x=1400;gi.LinkEntity(target);G_PublishMoveSpatialObject(target);
            T_ASSERT(G_IssueUnitPointOrder(unit,"attack",&(vec2_t){1800,0},false,0,0));
            order_attack(unit,retained);T_EQ(unit->attack_target,retained);
            uint32_t head=unit->current_order_id;
            T_EQ((uint32_t)G_TestAttackTargetPriority(unit,target),0x60400000u);
            G_SetUnitPlayer(target,3);
            T_EQ(unit->attack_target,target);T_EQ(unit->current_order_id,head);
            T_ASSERT(unit->attack_speed_cap.active);
            T_ASSERT(unit_issueimmediateorder(unit,"stop"));
            target->s.origin2.x=640;gi.LinkEntity(target);G_PublishMoveSpatialObject(target);
        }
        if(mode==1) {
            T_ASSERT(G_ActorRemoveSkill(target,MAKEFOURCC('A','a','t','k')));
            T_EQ((uint32_t)G_TestAttackTargetPriority(unit,target)&0x00400000u,0u);
        }
        if(mode==2) {
            S_SetUnitPaused(target,true);
            T_EQ((uint32_t)G_TestAttackTargetPriority(unit,target)&0x00400000u,0u);
            S_SetUnitPaused(target,false);
        }
        if(mode==3) {
            T_NOT_NULL(unit_addtimedstatus(target,"Bclf",1,8));
            T_EQ((uint32_t)G_TestAttackTargetPriority(unit,target)&0x00400000u,0u);
            unit_removestatus(target,unit_findstatus(target,MAKEFOURCC('B','c','l','f')),STATUS_REMOVE_SCRIPT);
        }
        unit_removestatus(target,unit_findstatus(target,buff),STATUS_REMOVE_SCRIPT);
        T_ASSERT(S_HumanCanAttack(target));T_FEQ(S_HumanMoveFactor(target),1,0);
        if(mode!=1)T_EQ(G_TestAttackTargetPriority(unit,target),before);
        /* Ordinary pause-stun must not substitute for MagicLeashTarget. */
        T_NOT_NULL(unit_addtimedstatus(target,"Bstu",1,1));
        T_EQ((uint32_t)G_TestAttackTargetPriority(unit,target)&0x00400000u,0u);
    }
    reset_entities();setup_test_world();G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
}

/* Repeated Group338 scene0/3, at normal and hard map difficulty: Town AI
 * compares the same candidates even before any AI script starts. */
TEST(wc3_order_lifecycle, group338_town_priority_replaces_without_script_vm) {
    FOR_LOOP(saved,2)FOR_LOOP(hard,2)FOR_LOOP(town,2)FOR_LOOP(house,2) {
        reset_entities();setup_test_world();level.show_map_cheat=true;
        level.setup.difficulty=hard ? 2 : 1;
        edict_t *unit=review_order_unit(0,0),*old=review_order_unit(588,1);
        edict_t *candidate=review_order_unit(128,0);
        unit->s.model=old->s.model=candidate->s.model=1;
        unit->collision=old->collision=candidate->collision=31;
        unit->invulnerable=true;unit->runtime.acquisition_range=1100;
        S_AttackProfileWrite(unit,0)->range=90;
        unit->defense_type=old->defense_type=candidate->defense_type=2;
        candidate->health.value=old->health.value=420;
        if(town){level.ai_owned_players|=1;G_BotUnitReady(unit);}
        T_EQ(!!(unit->aiflags&AI_TOWN_OWNED),town);
        T_NULL(level.bots[0].vm);
        if(house) {
            T_ASSERT(G_ActorRemoveSkill(candidate,MAKEFOURCC('A','m','o','v')));
            T_ASSERT(G_ActorRemoveSkill(candidate,MAKEFOURCC('A','a','t','k')));
            candidate->class_id=MAKEFOURCC('h','h','o','u');candidate->defense_type=3;
        }
        G_PublishMoveSpatialObject(unit);G_PublishMoveSpatialObject(old);G_PublishMoveSpatialObject(candidate);
        T_ASSERT(G_IssueUnitPointOrder(unit,"attack",&(vec2_t){1800,0},false,0,0));
        order_attack(unit,old);
        UnitWeapons_t const *weapons=unit->data.UnitWeapons;
        if(saved) {
            uint32_t u=unit->s.number,o=old->s.number,c=candidate->s.number;
            cstring_t path=Test_TempPath("wc3-group338-town.bin");
            T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);
            unit=g_edicts+u;old=g_edicts+o;candidate=g_edicts+c;
            unit->data.UnitWeapons=old->data.UnitWeapons=candidate->data.UnitWeapons=weapons;
        }
        uint64_t key=G_TestAttackTargetPriority(unit,candidate);
        uint32_t high=house ? 0x0ba00000u : 0x0be00000u;
        uint32_t low=house ? 0x04000000u : 0x64000000u;
        if(town){high|=hard ? 0xf0005000u : 0xa0005000u;if(house)low|=hard ? 0x02010000u : 0x02000000u;}
        T_EQ((uint32_t)(key>>32),high);T_EQ((uint32_t)key,low);
        uint32_t head=unit->current_order_id;edict_t *waypoint=unit->movement.attackmove_waypoint;
        G_SetUnitPlayer(candidate,1);
        T_EQ(unit->attack_target,house ? old : candidate);
        T_EQ(unit->current_order_id,head);T_EQ(unit->movement.attackmove_waypoint,waypoint);
        T_ASSERT(unit->attack_speed_cap.active);
    }
    reset_entities();setup_test_world();
}

/* Original public Roots cast: Move query false, virtual18c true, Attack
 * prevention one, BEer present and current Move speed zero. The priority
 * remains mobile while translation and attacks are disabled. */
TEST(wc3_order_lifecycle, group338_roots_keeps_mobile_priority_until_removed) {
    FOR_LOOP(saved,2)FOR_LOOP(town,2) {
        reset_entities();setup_test_world();level.show_map_cheat=true;
        level.setup.difficulty=2;
        edict_t *unit=review_order_unit(0,0),*target=review_order_unit(300,1);
        unit->invulnerable=true;unit->runtime.acquisition_range=1100;
        unit->collision=target->collision=31;
        unit->unitinfo.MoveSpeed=target->unitinfo.MoveSpeed=270;
        unit->defense_type=target->defense_type=2;
        target->health.value=420;S_AttackProfileWrite(unit,0)->range=90;
        if(town){level.ai_owned_players|=1;G_BotUnitReady(unit);}
        uint64_t before=G_TestAttackTargetPriority(unit,target);
        status_application_t roots={.buff="BEer",.level=1,.duration=8,
            .state_mask=WC3_STATUS_STATE_ROOTED,.source=unit};
        T_NOT_NULL(unit_applystatus(target,&roots));
        if(saved) {
            UnitWeapons_t const *source_weapons=unit->data.UnitWeapons,*target_weapons=target->data.UnitWeapons;
            cstring_t file=Test_TempPath("wc3-group338-roots.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            unit->data.UnitWeapons=source_weapons;target->data.UnitWeapons=target_weapons;
        }
        T_ASSERT(!S_UnitCanTranslate(target));
        T_FEQ(S_UnitMoveSpeed(target),0,0);
        uint64_t key=G_TestAttackTargetPriority(unit,target);
        T_EQ((uint32_t)key,town ? 0x62000000u : 0x60000000u);
        T_EQ((uint32_t)(key>>32),town ? 0x7ba01000u : 0x0ba00000u);
        unit_removestatus(target,unit_findstatus(target,MAKEFOURCC('B','E','e','r')),STATUS_REMOVE_SCRIPT);
        T_ASSERT(S_UnitCanTranslate(target));
        T_FEQ(S_UnitMoveSpeed(target),270,0);
        T_EQ(G_TestAttackTargetPriority(unit,target),before);
    }
    reset_entities();setup_test_world();
}

TEST(wc3_order_lifecycle, group339_sleep_admission_uses_acquiring_town_policy) {
    FOR_LOOP(saved,2)FOR_LOOP(town,2) {
        reset_entities();setup_test_world();level.show_map_cheat=true;
        edict_t *unit=review_order_unit(0,0),*old=review_order_unit(588,1);
        edict_t *target=review_order_unit(300,0);
        unit->s.model=old->s.model=target->s.model=1;
        unit->invulnerable=true;unit->runtime.acquisition_range=1100;
        unit->collision=old->collision=target->collision=31;
        target->health.value=old->health.value=420;
        if(town){level.ai_owned_players|=1;G_BotUnitReady(unit);}
        G_PublishMoveSpatialObject(unit);G_PublishMoveSpatialObject(old);G_PublishMoveSpatialObject(target);
        T_ASSERT(G_IssueUnitPointOrder(unit,"attack",&(vec2_t){1800,0},false,0,0));
        order_attack(unit,old);
        status_application_t sleep={.buff="BUsl",.level=1,.duration=30,.source=old};
        T_NOT_NULL(unit_applystatus(target,&sleep));
        if(saved) {
            UnitWeapons_t const *weapons=unit->data.UnitWeapons;
            cstring_t file=Test_TempPath("wc3-group339-sleep.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            unit->data.UnitWeapons=old->data.UnitWeapons=target->data.UnitWeapons=weapons;
        }
        T_ASSERT(!unit->attack_speed_cap.active);
        uint32_t head=unit->current_order_id;edict_t *waypoint=unit->movement.attackmove_waypoint;
        G_SetUnitPlayer(target,1);
        T_ASSERT(unit_hasstatusstate(target,WC3_STATUS_STATE_SLEEPING));
        T_ASSERT(S_AttackCanTarget(unit,target));
        T_EQ(S_AttackCanAutoAcquire(unit,target),town);
        T_EQ(unit->attack_speed_cap.active,town);
        T_EQ(unit->attack_target,old);
        T_EQ(unit->current_order_id,head);T_EQ(unit->movement.attackmove_waypoint,waypoint);
        unit_removestatus(target,unit_findstatus(target,MAKEFOURCC('B','U','s','l')),STATUS_REMOVE_SCRIPT);
        T_ASSERT(S_AttackCanAutoAcquire(unit,target));
        G_SetUnitPlayer(target,0);G_SetUnitPlayer(target,1);
        T_EQ(unit->attack_target,target);T_ASSERT(unit->attack_speed_cap.active);
    }
    reset_entities();setup_test_world();
}

TEST(wc3_order_lifecycle, group339_town_can_scan_naturally_sleeping_creep) {
    FOR_LOOP(saved,2)FOR_LOOP(town,2) {
        reset_entities();setup_test_world();level.show_map_cheat=true;
        edict_t *unit=review_order_unit(0,0),*target=review_order_unit(300,PLAYER_NEUTRAL_AGGRESSIVE);
        unit->s.model=target->s.model=1;unit->invulnerable=true;
        if(town){level.ai_owned_players|=1;G_BotUnitReady(unit);}
        G_UnitSetCanSleep(target,true);
        G_SetTimeOfDay(game.constants.duskTimeGameHours);G_UpdateTimeOfDay();
        ai_stand(target);T_ASSERT(G_UnitIsSleeping(target));
        gi.LinkEntity(unit);gi.LinkEntity(target);
        if(saved) {
            UnitWeapons_t const *weapons=unit->data.UnitWeapons;
            cstring_t file=Test_TempPath("wc3-group339-natural-sleep.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            unit->data.UnitWeapons=target->data.UnitWeapons=weapons;
        }
        T_ASSERT(G_UnitIsSleeping(target));
        T_EQ(S_AttackCanAutoAcquire(unit,target),town);
        T_EQ(G_FindNearestEnemy(unit,1100),town ? target : NULL);
        G_UnitWakeUp(target);T_ASSERT(!G_UnitIsSleeping(target));
        T_ASSERT(S_AttackCanAutoAcquire(unit,target));
        T_EQ(G_FindNearestEnemy(unit,1100),target);
    }
    reset_entities();setup_test_world();G_SetTimeOfDay(12);G_UpdateTimeOfDay();
}

TEST(wc3_order_lifecycle, group339_ensnare_priority_preserves_mobile_class) {
    cstring_t const buffs[]={"Bens","Bena","Beng","Bwea","Bweb"};
    FOR_LOOP(saved,2)FOR_LOOP(town,2)FOR_LOOP(kind,sizeof(buffs)/sizeof(*buffs)) {
        reset_entities();setup_test_world();level.show_map_cheat=true;level.setup.difficulty=2;
        edict_t *unit=review_order_unit(0,0),*target=review_order_unit(300,1);
        unit->invulnerable=true;unit->runtime.acquisition_range=1100;
        unit->collision=target->collision=31;
        unit->unitinfo.MoveSpeed=target->unitinfo.MoveSpeed=270;
        unit->defense_type=target->defense_type=2;
        target->health.value=420;S_AttackProfileWrite(unit,0)->range=90;
        if(town){level.ai_owned_players|=1;G_BotUnitReady(unit);}
        uint64_t before=G_TestAttackTargetPriority(unit,target);
        status_application_t bind={.buff=buffs[kind],.level=1,.duration=12,.source=unit};
        T_NOT_NULL(unit_applystatus(target,&bind));
        if(saved) {
            UnitWeapons_t const *weapons=unit->data.UnitWeapons;
            cstring_t file=Test_TempPath("wc3-group339-ensnare.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            unit->data.UnitWeapons=target->data.UnitWeapons=weapons;
        }
        T_ASSERT(!S_UnitCanTranslate(target));T_FEQ(S_UnitMoveSpeed(target),0,0);
        uint64_t key=G_TestAttackTargetPriority(unit,target);
        T_EQ((uint32_t)key,town ? 0x62000000u : 0x60000000u);
        T_EQ((uint32_t)(key>>32),town ? 0xfbe00000u : 0x0be00000u);
        T_ASSERT(G_ActorRemoveSkill(target,MAKEFOURCC('A','m','o','v')));
        T_EQ((uint32_t)G_TestAttackTargetPriority(unit,target)&0x20000000u,0);
        T_ASSERT(G_ActorAddSkill(target,MAKEFOURCC('A','m','o','v')));
        unit_removestatus(target,unit_findstatus(target,MAKEFOURCC(buffs[kind][0],buffs[kind][1],buffs[kind][2],buffs[kind][3])),STATUS_REMOVE_SCRIPT);
        T_ASSERT(S_UnitCanTranslate(target));T_FEQ(S_UnitMoveSpeed(target),270,0);
        T_EQ(G_TestAttackTargetPriority(unit,target),before);
    }
    reset_entities();setup_test_world();
}

#include "fixtures/retail_group338_town.h"
TEST(wc3_order_lifecycle, group338_town_matches_original_candidate_priority_words) {
    typeof(game.constants) constants=game.constants;
    FOR_LOOP(i,sizeof(group338_town_rows)/sizeof(*group338_town_rows)) {
        typeof(*group338_town_rows) *row=group338_town_rows+i;
        reset_entities();setup_test_world();level.setup.difficulty=2;
        edict_t *unit=review_order_unit(0,0),*target=review_order_unit(row->in_range ? 64 : 600,1);
        unit->invulnerable=true;unit->runtime.acquisition_range=1100;
        unit->collision=target->collision=16;S_AttackProfileWrite(unit,0)->range=90;
        unit->unitinfo.MoveSpeed=wc3_float(row->source_speed);
        target->unitinfo.MoveSpeed=wc3_float(row->target_speed);
        UnitBalance_t balance=*target->data.UnitBalance;
        balance.type=row->worker ? "peon" : "";target->data.UnitBalance=&balance;
        target->class_id=MAKEFOURCC(row->rawcode>>24,row->rawcode>>16,
                                  row->rawcode>>8,row->rawcode);
        target->defense_type=row->defense;
        target->health.value=wc3_float(row->life);target->mana.value=wc3_float(row->mana);
        if(row->summoned)target->aiflags|=AI_SUMMONED;
        if(row->illusion)target->aiflags|=AI_ILLUSION;
        if(!row->mobile)T_ASSERT(G_ActorRemoveSkill(target,MAKEFOURCC('A','m','o','v')));
        if(!row->armed)T_ASSERT(G_ActorRemoveSkill(target,MAKEFOURCC('A','a','t','k')));
        game.constants.combatConstantsLoaded=true;
        game.constants.damageBonus[ATK_NORMAL][row->defense]=wc3_float(row->multiplier);
        level.ai_owned_players|=1;G_BotUnitReady(unit);level.bots[0].mode=BOT_CAMPAIGN;
        /* Town admission may acquire this nearby enemy immediately. The
         * captured first rank evaluates an alternative to a different victim. */
        edict_t *old=review_order_unit(1800,1);order_attack(unit,old);
        G_PublishMoveSpatialObject(unit);G_PublishMoveSpatialObject(target);
        uint64_t key=G_TestAttackTargetPriority(unit,target);
        if((uint32_t)key!=row->low || (uint32_t)(key>>32)!=row->high)
            fprintf(stderr,"Town338 row %u: %08x:%08x expected %08x:%08x\n",i,
                (uint32_t)(key>>32),(uint32_t)key,row->high,row->low);
        T_EQ((uint32_t)key,row->low);T_EQ((uint32_t)(key>>32),row->high);
        target->data.UnitBalance=G_UnitBalance(target->class_id);
    }
    game.constants=constants;
    reset_entities();setup_test_world();
}

#include "fixtures/retail_group338_thresholds.h"
TEST(wc3_order_lifecycle, group338_town_original_scalar_boundaries) {
    typeof(game.constants) constants=game.constants;
    FOR_LOOP(i,sizeof(group338_health)/sizeof(*group338_health)) {
        reset_entities();setup_test_world();level.setup.difficulty=1;
        edict_t *unit=review_order_unit(0,0),*target=review_order_unit(1000,1);
        unit->invulnerable=true;target->health.value=wc3_float(group338_health[i].word);
        level.ai_owned_players|=1;G_BotUnitReady(unit);level.bots[0].mode=BOT_CAMPAIGN;
        uint64_t key=G_TestAttackTargetPriority(unit,target);
        T_EQ((uint32_t)(key>>32)&0x206u,group338_health[i].high);
    }
    FOR_LOOP(i,sizeof(group338_multipliers)/sizeof(*group338_multipliers)) {
        typeof(*group338_multipliers) *row=group338_multipliers+i;
        reset_entities();setup_test_world();level.setup.difficulty=0;
        edict_t *unit=review_order_unit(0,0),*target=review_order_unit(1000,1);
        unit->invulnerable=true;target->defense_type=2;
        if(!row->mobile)T_ASSERT(G_ActorRemoveSkill(target,MAKEFOURCC('A','m','o','v')));
        game.constants.combatConstantsLoaded=true;
        game.constants.damageBonus[ATK_NORMAL][2]=wc3_float(row->word);
        level.ai_owned_players|=1;G_BotUnitReady(unit);level.bots[0].mode=BOT_MELEE;
        uint64_t key=G_TestAttackTargetPriority(unit,target);
        T_EQ((uint32_t)(key>>32)&0x60d09u,row->high);
        T_EQ((uint32_t)key&0x80000000u,row->low);
    }
    game.constants=constants;
    reset_entities();setup_test_world();
}

/* Native Group338 scenes78..83: nearby friendlies lower the artillery
 * base from7 to2 to0, then veto acquisition. A missing retained target does
 * not bypass that veto. Exercise the owner-change notification itself. */
TEST(wc3_order_lifecycle, group338_artillery_counts_friendly_splash_before_replacement) {
    FOR_LOOP(saved,2)FOR_LOOP(retained,2)FOR_LOOP(friends,6) {
        reset_entities();setup_test_world();level.show_map_cheat=true;
        edict_t *unit=review_order_unit(0,0),*candidate=review_order_unit(488,0);
        edict_t *old=retained ? review_order_unit(1600,1) : NULL;
        unit->invulnerable=true;unit->runtime.acquisition_range=1100;
        unit->s.model=candidate->s.model=1;if(old)old->s.model=1;
        UnitWeapons_t const *authored=unit->data.UnitWeapons;
        UnitWeapons_t weapons=*authored;
        weapons.attack1.areaTargets=WC3_TARGET_FLAG_GROUND;
        unit->data.UnitWeapons=&weapons;
        unitAttack_t *weapon=S_AttackProfileWrite(unit,0);
        weapon->weapon=WPN_ARTILLERY;weapon->range=1200;weapon->areaSmall=250;
        candidate->collision=unit->collision=31;
        candidate->defense_type=2;
        FOR_LOOP(i,friends) {
            edict_t *friend=review_order_unit_at(488+i*8,48,0);
            friend->collision=31;friend->s.model=1;G_PublishMoveSpatialObject(friend);
        }
        G_PublishMoveSpatialObject(unit);G_PublishMoveSpatialObject(candidate);
        T_ASSERT(G_IssueUnitPointOrder(unit,"attack",&(vec2_t){1800,0},false,0,0));
        if(old){old->collision=31;G_PublishMoveSpatialObject(old);order_attack(unit,old);}
        if(saved) {
            uint32_t u=unit->s.number,c=candidate->s.number,o=old ? old->s.number : 0;
            cstring_t path=Test_TempPath("wc3-group338-artillery.bin");
            T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);
            unit=g_edicts+u;candidate=g_edicts+c;old=retained ? g_edicts+o : NULL;
            /* Rebind this authored test row after the cold load; runtime
             * weapon radius/range are restored by the save itself. */
            unit->data.UnitWeapons=&weapons;candidate->data.UnitWeapons=authored;
            if(old)old->data.UnitWeapons=authored;
        }
        uint32_t head=unit->current_order_id;edict_t *waypoint=unit->movement.attackmove_waypoint;
        G_SetUnitPlayer(candidate,1);
        uint64_t key=G_TestAttackTargetPriority(unit,candidate);
        uint32_t base[]={0x03800000,0x01000000,0};
        if(friends>=3)T_EQ(key,0);
        else T_EQ((uint32_t)(key>>32),base[friends]|0x08600000u);
        bool replace=friends<3 && (!retained || friends==0);
        T_EQ(unit->attack_target,replace ? candidate : old);
        T_EQ(unit->current_order_id,head);T_EQ(unit->movement.attackmove_waypoint,waypoint);
        T_ASSERT(unit->attack_speed_cap.active);
        unit->data.UnitWeapons=authored;
    }
    reset_entities();setup_test_world();
}

#include "fixtures/retail_ranking264_distance.h"
TEST(wc3_order_lifecycle, ranking264_ties_predict_observer_world_and_keep_target_committed) {
    reset_entities();setup_test_world();
    edict_t *unit=review_order_unit(0,0),*target=review_order_unit(0,1);
    FOR_LOOP(i,sizeof(ranking264_distances)/sizeof(*ranking264_distances)) {
        typeof(*ranking264_distances) *row=ranking264_distances+i;
        vec2_t origin={wc3_float(row->origin[0]),wc3_float(row->origin[1])};
        CM_SetupTestWorldBounds(&(box2_t){.min=origin,.max={origin.x+4096,origin.y+4096}});
        edict_t *units[]={unit,target};
        FOR_LOOP(j,2) {
            edict_t *e=units[j];uint32_t const *fine=j ? row->target : row->source;
            e->movement.fine_pose=(vec2_t){wc3_float(fine[0]),wc3_float(fine[1])};
            e->s.origin2=(vec2_t){wc3_world_coordinate(e->movement.fine_pose.x,origin.x,32),
                                  wc3_world_coordinate(e->movement.fine_pose.y,origin.y,32)};
            e->movement.pose_world=e->s.origin2;e->movement.pose_valid=true;
            e->movement.velocity=j ? (vec2_t){32000,-31968} :
                (vec2_t){wc3_mul(wc3_float(row->velocity[0]),32),wc3_mul(wc3_float(row->velocity[1]),32)};
            e->movement.clock_valid=true;e->movement.pose_clock=(wc3Clock_t){wc3_float(row->old),0,8};
        }
        level.pathing_clock=(wc3Clock_t){wc3_float(row->now),row->epoch,8};
        vec2_t position=unit->s.origin2,fine=target->movement.fine_pose;
        wc3Clock_t clock=unit->movement.pose_clock;wc3Random_t random=level.pathing_random;
        T_EQ(wc3_float_bits(S_UnitCommittedTargetDistanceSquared(unit,target)),row->distance);
        T_ASSERT(!memcmp(&unit->s.origin2,&position,sizeof(position)));
        T_ASSERT(!memcmp(&target->movement.fine_pose,&fine,sizeof(fine)));
        T_ASSERT(!memcmp(&unit->movement.pose_clock,&clock,sizeof(clock)));
        T_ASSERT(!memcmp(&level.pathing_random,&random,sizeof(random)));
    }
    reset_entities();setup_test_world();
}

static unsigned bridge265_completions;
static void bridge265_complete(edict_t *receiver,edict_t *unit,bool arrived) {
    (void)receiver;(void)unit;(void)arrived;
    bridge265_completions++;
}

TEST(wc3_order_lifecycle, bridge265_retired_receiver_cannot_receive_completion) {
    FOR_LOOP(mode,6) {
        reset_entities();setup_test_world();
        edict_t *unit=review_order_unit(64,0),*target=review_order_unit(900,1);
        edict_t *receiver=mode>=4 ? review_order_unit(128,0) : unit;
        bridge265_completions=0;
        T_ASSERT(S_BeginUnitTargetApproach(unit,target,32,receiver,bridge265_complete));
        T_NOT_NULL(S_UnitTargetApproachReceiver(unit));
        if(mode==1)unit->spawn_time++; /* Same storage, different identity. */
        if(mode>=2)G_DeferFreeEdict(receiver);
        if(mode==5) {
            unsigned visits=0;
            while(unit->movement.group_id && visits++<600) {
                level.pathing_clock.time=wc3_add(level.pathing_clock.time,.03f);level.pathing_counter++;
                level.scheduled_think=true;S_BeginAbilityOwnerUpdates();S_RunAbilityOwnerUpdates();level.scheduled_think=false;
            }
            T_ASSERT(visits<600);T_EQ(unit->movement.group_id,0);
        } else if(mode<2 || mode==4)unit_stand(unit);
        T_EQ(bridge265_completions,mode==0 ? 1u : 0u);
        if(mode==3) {
            G_DeferFreeEdict(unit);unit_stand(unit);
            T_EQ(bridge265_completions,0);
        }
        T_NULL(S_UnitTargetApproachReceiver(unit));
    }
    reset_entities();setup_test_world();
}

TEST(wc3_order_lifecycle, swing199_explicit_weapons_publish_exemption_before_damage) {
    /* Explicit producers do not pass through automatic acquisition or an
     * attacked/ally-help notification. Chase alone must not release the cap. */
    FOR_LOOP(mode,3) {
        reset_entities();setup_test_world();
        level.timer_clock_valid=false;level.pathing_clock=(wc3Clock_t){8,0,300};
        edict_t *unit=review_order_unit(0,0),*target=review_order_unit(300,1);
        unitAttack_t *profile=S_AttackProfileWrite(unit,0);
        profile->weapon=mode==0 ? WPN_NORMAL : mode==1 ? WPN_MISSILE : WPN_ARTILLERY;
        profile->range=30;profile->damagePoint=.3f;
        if(mode==2)T_ASSERT(G_IssueUnitPointOrder(unit,"attackground",&target->s.origin2,false,0,0));
        else T_ASSERT(G_IssueUnitTargetOrder(unit,mode==1 ? "attackonce" : "attack",target,false,0));
        T_ASSERT(!unit->attack_speed_cap.active);
        /* Walk transitions to the weapon windup at the true range gate. */
        unit->s.origin.x=280;gi.LinkEntity(unit);
        unit->currentmove->think(unit);
        T_ASSERT(unit->attack_speed_cap.active);
        T_FEQ(unit->attack_speed_cap.deadline.time,11,0);
        T_ASSERT(unit->wait>0);T_FEQ(target->health.value,target->health.max_value,0);
        uint32_t sequence=unit->attack_speed_cap.sequence;
        T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){600,64},false,0,0));
        T_ASSERT(unit->attack_speed_cap.active);T_EQ(unit->attack_speed_cap.sequence,sequence);
        uint32_t number=unit->s.number;
        cstring_t file=Test_TempPath("wc3-swing199.bin");
        T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);unit=g_edicts+number;
        T_ASSERT(unit->attack_speed_cap.active);T_FEQ(unit->attack_speed_cap.deadline.time,11,0);
        T_EQ(unit->attack_speed_cap.sequence,sequence);
    }
    reset_entities();setup_test_world();
}

TEST(wc3_order_lifecycle, swing199_repeated_windups_use_the_existing_exact_rearm_gate) {
    reset_entities();setup_test_world();level.timer_clock_valid=false;
    edict_t *unit=review_order_unit(0,0),*target=review_order_unit(20,1);
    S_AttackProfileWrite(unit,0)->damagePoint=.3f;
    uint32_t sequence=0;
    FOR_LOOP(i,3) {
        level.pathing_clock=(wc3Clock_t){8+i*.25f,0,300};
        /* Each admitted explicit head reaches a ready weapon windup. */
        unit->attack_cooldown_active=false;unit->wait=0;
        T_ASSERT(G_IssueUnitTargetOrder(unit,"attack",target,false,0));
        unit->currentmove->think(unit);
        T_ASSERT(unit->attack_speed_cap.active);
        if(i==1)T_EQ(unit->attack_speed_cap.sequence,sequence);
        else T_ASSERT(unit->attack_speed_cap.sequence>sequence);
        T_FEQ(unit->attack_speed_cap.deadline.time,i==2 ? 11.5f : 11,0);
        sequence=unit->attack_speed_cap.sequence;
    }
    reset_entities();setup_test_world();
}

/* Cold allocation, ordered cancellation and final dispatch must return storage
 * without retaining a bucket on every unit that once received Shift orders. */
TEST(wc3_order_lifecycle, pool192_empty_queues_release_storage_and_reuse_lifo) {
    enum { COUNT = 129 };
    edict_t *units[COUNT];
    unitOrder_t *slots[COUNT];
    reset_entities(); setup_test_world();
    FOR_LOOP(i, COUNT) {
        units[i] = review_order_unit(0, 0);
        T_NULL(units[i]->order_queue.entries);
        T_ASSERT(G_QueueUnitOrder(units[i], "holdposition", UNIT_ORDER_TARGET_NONE, NULL, NULL, 0, 0, 0));
        slots[i] = units[i]->order_queue.entries;
        T_NOT_NULL(slots[i]);
        FOR_LOOP(j, i) T_NE(slots[i], slots[j]);
    }
    FOR_LOOP(i, COUNT) {
        T_ASSERT(unit_issueimmediateorder(units[i], "stop"));
        T_NULL(units[i]->order_queue.entries);
        T_EQ(units[i]->order_queue.count, 0);
        T_EQ(units[i]->order_queue.head, 0);
    }
    FOR_LOOP(i, COUNT) {
        T_ASSERT(G_QueueUnitOrder(units[i], "holdposition", UNIT_ORDER_TARGET_NONE, NULL, NULL, 0, 0, 0));
        T_EQ(units[i]->order_queue.entries, slots[COUNT - 1 - i]);
    }
    FOR_LOOP(i, COUNT) {
        T_ASSERT(G_UnitStartNextQueuedOrder(units[i]));
        T_NULL(units[i]->order_queue.entries);
        T_ASSERT(units[i]->movement.holding_position);
        T_ASSERT(!G_UnitStartNextQueuedOrder(units[i]));
        G_ClearUnitOrderQueue(units[i]);
        T_NULL(units[i]->order_queue.entries);
    }
    reset_entities(); setup_test_world();
}

TEST(wc3_order_lifecycle, pool192_lost_target_fallback_and_replacement_release_storage) {
    reset_entities(); setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *target = review_order_unit(256, 1);
    vec2_t point = {512, 0};
    T_ASSERT(G_QueueUnitOrder(unit, "attack", UNIT_ORDER_TARGET_ENTITY, NULL, target, 0, 0, 0));
    T_NOT_NULL(unit->order_queue.entries);
    target->spawn_time++;
    /* Payoff263:67abe0's unresolved target follows the point branch. Attack
     * Move survives a lost identity; only its queue backing is reclaimed. */
    T_ASSERT(G_UnitStartNextQueuedOrder(unit));
    T_NOT_NULL(unit->movement.attackmove_waypoint);
    if(unit->movement.attackmove_waypoint)T_FEQ(unit->movement.attackmove_waypoint->s.origin2.x,256,0);
    T_NULL(unit->order_queue.entries);
    T_ASSERT(G_QueueUnitOrder(unit, "holdposition", UNIT_ORDER_TARGET_NONE, NULL, NULL, 0, 0, 0));
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &point, false, 0, 0));
    T_NULL(unit->order_queue.entries);
    T_EQ(unit->current_order_id, G_OrderId("move"));
    reset_entities(); setup_test_world();
}

TEST(wc3_order_lifecycle, hold_position_does_not_chase_acquired_enemy) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(300, 1);
    T_ASSERT(S_HoldPosition(unit));
    level.time = 300 - (uint32_t)(unit - g_edicts) % 300;
    unit->currentmove->think(unit);
    T_ASSERT(unit->goalentity == enemy);
    T_ASSERT(unit->movement.holding_position);
    unit->currentmove->think(unit);
    T_FEQ(unit->s.origin.x, 0, 0.001f);
}

TEST(wc3_order_lifecycle, hold_attacks_in_range_then_stays_when_enemy_leaves) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(20, 1);
    T_ASSERT(S_HoldPosition(unit));
    level.time = 300 - (uint32_t)(unit - g_edicts) % 300;
    unit->currentmove->think(unit);
    unit->currentmove->think(unit);
    T_STREQ(unit->currentmove->animation, "attack");
    enemy->s.origin.x = 300;
    attack_melee_cooldown(unit);
    unit->currentmove->think(unit);
    unit->currentmove->think(unit);
    T_FEQ(unit->s.origin.x, 0, 0.001f);
    T_ASSERT(unit->movement.holding_position);
    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_order_lifecycle, delayed_kill_preserves_new_move_order) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(300, 1);
    vec2_t point = {600, 0};
    T_ASSERT(unit_issuetargetorder(unit, "attack", enemy));
    edict_t *missile = G_Spawn();
    missile->owner = unit;
    S_SetMoveGoal(missile, &missile->goalentity, enemy);
    missile->velocity = 10000;
    missile->damage = 10000;
    T_ASSERT(unit_issueorder(unit, "move", &point));
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &MAKE(vec2_t, .x = 800), true, 0, 0));
    umove_t const *move = unit->currentmove;
    /* A projectile resolves damage after its owner has already accepted Move. */
    SV_Physics_Toss(missile);
    T_ASSERT(M_IsDead(enemy));
    T_ASSERT(unit->currentmove == move);
    T_EQ(G_UnitQueuedOrderCount(unit), 1);
}

TEST(wc3_order_lifecycle, explicit_attack_replaces_persistent_follow) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *ally = review_order_unit(500, 0);
    edict_t *enemy = review_order_unit(300, 1);
    T_ASSERT(unit_issuetargetorder(unit, "move", ally));
    T_ASSERT(unit->movement.follow_target == ally);
    G_SetHealth(enemy, 0);
    /* Native207160 accepts this as a point Attack Move snapshot. */
    T_ASSERT(unit_issuetargetorder(unit, "attack", enemy));
    T_NULL(unit->movement.follow_target);
    T_NOT_NULL(unit->movement.attackmove_waypoint);
    T_FEQ(unit->goalentity->s.origin2.x, enemy->s.origin2.x, 0);
    G_SetHealth(enemy, enemy->health.max_value);
    T_ASSERT(unit_issuetargetorder(unit, "attack", enemy));
    T_Damage(enemy, unit, (int)enemy->health.value);
    T_NULL(unit->movement.follow_target);
}

TEST(wc3_order_lifecycle, queued_attack_preserves_follow_until_follow_completes) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *ally = review_order_unit(500, 0);
    edict_t *enemy = review_order_unit(300, 1);
    T_ASSERT(unit_issuetargetorder(unit, "move", ally));
    T_ASSERT(G_IssueUnitTargetOrder(unit, "attack", enemy, true, 0));
    T_ASSERT(unit->movement.follow_target == ally);
    T_ASSERT(unit->goalentity == ally);
    T_EQ(G_UnitQueuedOrderCount(unit), 1);
    G_SetHealth(ally, 0);
    unit->currentmove->think(unit);
    T_ASSERT(unit->goalentity == enemy);
    T_NULL(unit->movement.follow_target);
    T_EQ(G_UnitQueuedOrderCount(unit), 0);
    T_Damage(enemy, unit, (int)enemy->health.value);
    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_order_lifecycle, auto_attack_resumes_patrol_but_smart_attack_replaces_it) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *first = review_order_unit(300, 1);
    edict_t *second = review_order_unit(400, 1);
    order_patrol(unit, Waypoint_add(&MAKE(vec2_t, .x = 600)));
    edict_t *patrol = unit->movement.patrol_target;
    order_attack(unit, first);
    T_Damage(first, unit, (int)first->health.value);
    T_ASSERT(unit->goalentity == patrol);
    /* Retail resumes the same leg as an ordinary point task beneath851991. */
    T_ASSERT(move_is_active_order_walk(unit));
    T_EQ(unit->current_order_id, WC3_ORDER_ID_PATROL_TWO_POINTS);
    T_ASSERT(unit_issuetargetorder(unit, "smart", second));
    T_ASSERT(unit->goalentity == second);
    T_ASSERT(unit->currentmove->proc == CAbilityAttack);
    T_Damage(second, unit, (int)second->health.value);
    T_NULL(unit->movement.patrol_a);
    T_NULL(unit->movement.patrol_b);
    T_NULL(unit->movement.patrol_target);
    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_order_lifecycle, patrol_head_differs_from_issued_command_through_combat) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(30, 1);
    vec2_t const endpoint = {600, 0};
    T_EQ(G_OrderId("patrol"), 851990);
    T_ASSERT(G_IssueUnitPointOrder(unit, "patrol", &endpoint, false, 0, 0));
    T_EQ(G_GetIssuedOrderId(unit), 851990);
    T_EQ(unit->current_order_id, 851991);
    edict_t *patrol = unit->movement.patrol_target;
    order_attack(unit, enemy);
    T_EQ(unit->current_order_id, 851991);
    T_ASSERT(!G_IssueUnitTargetOrder(unit, "repair", enemy, false, 0));
    T_EQ(unit->current_order_id, 851991);
    T_ASSERT(unit->currentmove->proc == CAbilityAttack);
    T_Damage(enemy, unit, (int)enemy->health.value);
    T_EQ(unit->current_order_id, 851991);
    T_ASSERT(unit->goalentity == patrol);
    T_ASSERT(move_is_active_order_walk(unit));
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &(vec2_t){768, 0}, false, 0, 0));
    T_EQ(unit->current_order_id, 851986);
    T_NULL(unit->movement.patrol_a);
}

TEST(wc3_order_lifecycle, animationless_melee_kill_preserves_resumed_follow) {
    setup_test_world();
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    edict_t *unit = review_order_unit(0, 0), *ally = review_order_unit(500, 0);
    edict_t *enemy = review_order_unit(20, 1);
    T_ASSERT(unit_issuetargetorder(unit, "move", ally));
    S_AttackProfileWrite(unit, 0)->damagePoint = (float)FRAMETIME / 1000.0f;
    G_SetHealth(enemy, 1);
    order_attack(unit, enemy);
    unit->currentmove->think(unit);
    unit->currentmove->think(unit);
    T_ASSERT(M_IsDead(enemy));
    /* The committed hit retains Follow through its independent swing wait;
     * no animation-end callback is required to resume it. */
    T_EQ(unit->movement.follow_target, ally);
    T_ASSERT(unit->attack_swing.active);
    T_NULL(unit->goalentity);
    level.started = level.scriptsConfigured = level.scriptsStarted = true;
    FOR_LOOP(i, 8) { level.time += FRAMETIME; globals.RunFrame(); }
    T_ASSERT(unit->goalentity == ally);
    T_ASSERT(unit->currentmove->proc == CAbilityMove);
}

TEST(wc3_order_lifecycle, finishing_repair_preserves_production_queue) {
    setup_test_world();
    slkTestData_t *rows, *old = building_install_repair_data(&rows);
    edict_t *worker = review_order_unit(0, 0);
    edict_t *building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    UnitAbilities_t abilities = { .abilList = "Arep" };
    worker->data.UnitAbilities = &abilities;
    building->stand = unit_stand;
    building->svflags |= SVF_MONSTER;
    UnitBalance_t balance = *building->data.UnitBalance;
    balance.reptm = 10;
    building->data.UnitBalance = &balance;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 1000;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 1000;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    unit_build(building, MAKEFOURCC('h','f','o','o'));
    edict_t *queued = building->build;
    T_NOT_NULL(queued);
    UnitBalance_t trainee = *queued->data.UnitBalance;
    trainee.buildTime = 20;
    queued->data.UnitBalance = &trainee;
    queued->health.max_value = 420;
    queued->health.value = 0;
    queued->stand = unit_stand;
    building->health.value = building->health.max_value - 1.0f;
    T_ASSERT(S_OrderRepair(worker, building, 0));
    worker->currentmove->think(worker);
    building_restore_repair_data(old, rows);
    T_FEQ(building->health.value, building->health.max_value, 0.0001f);
    T_ASSERT(building->build == queued);
    T_ASSERT(building->currentmove->proc == CAbilityTrain);
    float progress = queued->health.value;
    ai_train_build(building);
    T_ASSERT(queued->health.value > progress);
}

TEST(wc3_order_lifecycle, stop_auto_acquires_and_chases_after_returning_to_idle) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(300, 1);
    order_stop(unit);
    level.time = 300 - (uint32_t)(unit - g_edicts) % 300;
    unit->currentmove->think(unit);
    T_ASSERT(unit->goalentity == enemy);
    T_ASSERT(!unit->movement.holding_position);
    T_ASSERT(unit->currentmove->proc == CAbilityAttack);
}

TEST(wc3_order_lifecycle, queued_stop_preserves_later_fifo_work) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0);
    vec2_t first = {300, 0}, second = {600, 0};

    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &first, false, 0, 0.0f));
    T_ASSERT(G_QueueUnitOrder(unit, "stop", UNIT_ORDER_TARGET_NONE, NULL, NULL, 0, 0.0f, 0));
    T_ASSERT(G_QueueUnitOrder(unit, "move", UNIT_ORDER_TARGET_POINT, &second, NULL, 0, 0.0f, 0));
    T_EQ(G_UnitQueuedOrderCount(unit), 2);

    unit_stand(unit);
    T_EQ(G_UnitQueuedOrderCount(unit), 1);
    T_ASSERT(!unit->movement.holding_position);
    T_ASSERT(unit->currentmove->think == ai_stand);

    unit->currentmove->think(unit);
    T_EQ(G_UnitQueuedOrderCount(unit), 0);
    T_ASSERT(unit->currentmove->proc == CAbilityMove);
}

TEST(wc3_order_lifecycle, queued_hold_preserves_later_fifo_work) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0);
    vec2_t first = {300, 0}, second = {600, 0};

    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &first, false, 0, 0.0f));
    T_ASSERT(G_QueueUnitOrder(unit, "holdposition", UNIT_ORDER_TARGET_NONE, NULL, NULL, 0, 0.0f, 0));
    T_ASSERT(G_QueueUnitOrder(unit, "move", UNIT_ORDER_TARGET_POINT, &second, NULL, 0, 0.0f, 0));
    T_EQ(G_UnitQueuedOrderCount(unit), 2);

    unit_stand(unit);
    T_EQ(G_UnitQueuedOrderCount(unit), 1);
    T_ASSERT(unit->movement.holding_position);

    unit->currentmove->think(unit);
    T_EQ(G_UnitQueuedOrderCount(unit), 0);
    T_ASSERT(!unit->movement.holding_position);
    T_ASSERT(unit->currentmove->proc == CAbilityMove);
}

TEST(wc3_order_lifecycle, hold_position_interrupts_active_channel) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0);
    if (!unit->channel) unit->channel = G_AllocChannel();
    assert(unit->channel);
    unit->channel->code = MAKEFOURCC('A','x','x','x');

    T_ASSERT(S_HoldPosition(unit));
    T_ASSERT(!unit->channel || unit->channel->code == 0);
    T_ASSERT(unit->movement.holding_position);
}

TEST(wc3_order_lifecycle, hold_position_owns_common_stand_transition) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0);

    T_ASSERT(S_HoldPosition(unit));
    T_ASSERT(unit->currentmove == &holdpos_move_stand);
    unit_stand(unit);
    T_ASSERT(unit->currentmove == &holdpos_move_stand);
}

TEST(wc3_order_lifecycle, stop_and_hold_buttons_expose_engaged_state) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0);
    gameCommandButton_t stop, hold;

    T_ASSERT(G_BuildCommandButton(unit, STR_CmdStop, false, 0, &stop));
    T_ASSERT(G_BuildCommandButton(unit, STR_CmdHoldPos, false, 0, &hold));
    T_ASSERT(stop.queueable);
    T_ASSERT(hold.queueable);
    T_EQ(stop.engaged, 1);
    T_EQ(hold.engaged, 0);

    T_ASSERT(S_HoldPosition(unit));
    T_ASSERT(G_BuildCommandButton(unit, STR_CmdStop, false, 0, &stop));
    T_ASSERT(G_BuildCommandButton(unit, STR_CmdHoldPos, false, 0, &hold));
    T_EQ(stop.engaged, 0);
    T_EQ(hold.engaged, 1);

    {
        edict_t *enemy = review_order_unit(20, 1);
        level.time = 300 - (uint32_t)(unit - g_edicts) % 300;
        unit->currentmove->think(unit);
        T_ASSERT(unit->goalentity == enemy);
        T_ASSERT(G_BuildCommandButton(unit, STR_CmdHoldPos, false, 0, &hold));
        T_EQ(hold.engaged, 1);
        S_SetMoveGoal(unit, &unit->goalentity, NULL);
        unit_stand(unit);
        T_ASSERT(G_BuildCommandButton(unit, STR_CmdStop, false, 0, &stop));
        T_ASSERT(G_BuildCommandButton(unit, STR_CmdHoldPos, false, 0, &hold));
        T_EQ(stop.engaged, 0);
        T_EQ(hold.engaged, 1);
    }

    order_stop(unit);
    T_ASSERT(G_BuildCommandButton(unit, STR_CmdStop, false, 0, &stop));
    T_ASSERT(G_BuildCommandButton(unit, STR_CmdHoldPos, false, 0, &hold));
    T_EQ(stop.engaged, 1);
    T_EQ(hold.engaged, 0);
}

TEST(wc3_order_lifecycle, hold_button_command_refresh_publishes_new_engaged_state) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    edict_t *clent, *unit;
    gameCommandButton_t hold;
    cstring_t command[] = { "button", "CmdHoldPos" };

    setup_test_world();
    clent = &g_edicts[0];
    clent->inuse = true;
    clent->client->connected = true;
    clent->client->ps.number = 0;
    unit = review_order_unit(0, 0);
    G_SelectEntity(clent->client, unit);

    lifecycle_stop_frame_seen = lifecycle_hold_frame_seen = lifecycle_move_frame_seen = false;
    lifecycle_stop_frame_flags = lifecycle_hold_frame_flags = 0;
    lifecycle_move_frame_flags = 0;
    lifecycle_stop_frame_texture = lifecycle_hold_frame_texture = 0;
    gi.Write = lifecycle_capture_command_frame;
    G_ClientCommand(clent, 2, command);
    gi.Write = old_write;

    T_ASSERT(unit->movement.holding_position);
    T_ASSERT(G_BuildCommandButton(unit, STR_CmdHoldPos, false, 0, &hold));
    T_EQ(hold.engaged, 1);
    T_ASSERT(lifecycle_stop_frame_seen);
    T_ASSERT(lifecycle_hold_frame_seen);
    T_ASSERT(lifecycle_move_frame_seen);
    T_ASSERT(lifecycle_stop_frame_texture != 0);
    T_ASSERT(lifecycle_hold_frame_texture != 0);
    T_ASSERT(!(lifecycle_stop_frame_flags & UIFLAG_ABILITY_ENGAGED));
    T_ASSERT(lifecycle_hold_frame_flags & UIFLAG_ABILITY_ENGAGED);
    T_ASSERT(lifecycle_stop_frame_flags & UIFLAG_ORDER_QUEUEABLE);
    T_ASSERT(lifecycle_hold_frame_flags & UIFLAG_ORDER_QUEUEABLE);
    T_ASSERT(!(lifecycle_move_frame_flags & UIFLAG_ORDER_QUEUEABLE));
}
/* A right-click move and the later return to idle change Stop's glow without a command-card click. */
TEST(wc3_order_lifecycle, move_and_idle_transitions_refresh_stop_glow) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    edict_t *clent, *unit;
    vec2_t point = {500, 0};

    setup_test_world();
    clent = &g_edicts[0];
    clent->inuse = true;
    clent->client->connected = true;
    clent->client->ps.number = 0;
    unit = review_order_unit(0, 0);
    G_SelectEntity(clent->client, unit);
    order_stop(unit);
    clent->client->commands_dirty = false;

    T_ASSERT(G_IssueUnitPointOrder(unit, "smart", &point, false, 0, 0.0f));
    T_ASSERT(clent->client->commands_dirty);
    lifecycle_stop_frame_seen = false;
    lifecycle_stop_frame_flags = 0;
    gi.Write = lifecycle_capture_command_frame;
    Get_Commands_f(clent);
    gi.Write = old_write;
    T_ASSERT(lifecycle_stop_frame_seen);
    T_ASSERT(!(lifecycle_stop_frame_flags & UIFLAG_ABILITY_ENGAGED));

    clent->client->commands_dirty = false;
    unit_stand(unit);
    T_ASSERT(clent->client->commands_dirty);
    lifecycle_stop_frame_seen = false;
    gi.Write = lifecycle_capture_command_frame;
    Get_Commands_f(clent);
    gi.Write = old_write;
    T_ASSERT(lifecycle_stop_frame_seen);
    T_ASSERT(lifecycle_stop_frame_flags & UIFLAG_ABILITY_ENGAGED);
}

TEST(wc3_order_lifecycle, stop_records_guard_position_and_returns_after_auto_combat) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(300, 1);

    order_stop(unit);
    T_ASSERT(unit->movement.guard_state == GUARD_IDLE);
    T_FEQ(unit->movement.guard_position.x, 0, 0.001f);
    T_FEQ(unit->movement.guard_position.y, 0, 0.001f);

    level.time = 300 - (uint32_t)(unit - g_edicts) % 300;
    unit->currentmove->think(unit);
    T_ASSERT(unit->goalentity == enemy);
    T_ASSERT(unit->movement.guard_state == GUARD_COMBAT);

    unit->s.origin2.x = unit->s.origin.x = 180;
    gi.LinkEntity(unit);
    T_Damage(enemy, unit, (int)enemy->health.value);

    T_ASSERT(unit->movement.guard_state == GUARD_RETURNING);
    T_ASSERT(unit->currentmove->proc == CAbilityMove);
    T_NOT_NULL(unit->goalentity);
    T_FEQ(unit->goalentity->s.origin2.x, 0, 0.001f);
    T_FEQ(unit->goalentity->s.origin2.y, 0, 0.001f);
}

TEST(wc3_order_lifecycle, internal_stop_cleanup_does_not_create_guard_position) {
    setup_test_world();
    edict_t *unit = review_order_unit(96, 0);

    order_stop_cleanup(unit);

    T_ASSERT(unit->movement.guard_state == GUARD_NONE);
}

TEST(wc3_order_lifecycle, guard_return_completion_restores_stopped_idle) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(300, 1);

    order_stop(unit);
    level.time = 300 - (uint32_t)(unit - g_edicts) % 300;
    unit->currentmove->think(unit);
    unit->s.origin2.x = unit->s.origin.x = 120;
    gi.LinkEntity(unit);
    T_Damage(enemy, unit, (int)enemy->health.value);
    T_ASSERT(unit->movement.guard_state == GUARD_RETURNING);

    unit->s.origin2 = unit->movement.guard_position;
    unit->s.origin.x = unit->s.origin2.x;
    unit->s.origin.y = unit->s.origin2.y;
    gi.LinkEntity(unit);
    unit->currentmove->think(unit);

    T_ASSERT(unit->movement.guard_state == GUARD_IDLE);
    T_ASSERT(unit->currentmove->think == ai_stand);
}

TEST(wc3_order_lifecycle, explicit_move_clears_old_stop_guard) {
    setup_test_world();
    edict_t *unit = review_order_unit(40, 0);
    vec2_t point = {500, 0};

    order_stop(unit);
    T_ASSERT(unit->movement.guard_state == GUARD_IDLE);
    T_FEQ(unit->movement.guard_position.x, 40, 0.001f);

    T_ASSERT(unit_issueorder(unit, "move", &point));
    T_ASSERT(unit->movement.guard_state == GUARD_NONE);
}

TEST(wc3_order_lifecycle, second_stop_refreshes_guard_position) {
    setup_test_world();
    edict_t *unit = review_order_unit(20, 0);

    order_stop(unit);
    T_FEQ(unit->movement.guard_position.x, 20, 0.001f);

    unit->s.origin2.x = unit->s.origin.x = 240;
    gi.LinkEntity(unit);
    order_stop(unit);

    T_ASSERT(unit->movement.guard_state == GUARD_IDLE);
    T_FEQ(unit->movement.guard_position.x, 240, 0.001f);
}

TEST(wc3_order_lifecycle, queued_player_order_outranks_guard_return) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(300, 1);
    vec2_t point = {700, 0};

    order_stop(unit);
    level.time = 300 - (uint32_t)(unit - g_edicts) % 300;
    unit->currentmove->think(unit);
    T_ASSERT(unit->movement.guard_state == GUARD_COMBAT);
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &point, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(unit), 1);

    unit->s.origin2.x = unit->s.origin.x = 160;
    gi.LinkEntity(unit);
    T_Damage(enemy, unit, (int)enemy->health.value);

    T_ASSERT(unit->movement.guard_state == GUARD_NONE);
    T_EQ(G_UnitQueuedOrderCount(unit), 0);
    T_ASSERT(unit->currentmove->proc == CAbilityMove);
}

TEST(wc3_order_lifecycle, repair_family_heads_survive_approach_and_complete_at_work) {
    static cstring_t const names[] = {"repair", "renew", "restoration"};
    static cstring_t const codes[] = {"Arep", "Aren", "Arst"};
    static uint32_t const ids[] = {852024, 852161, 852202};
    slkTestData_t *rows, *old = building_install_repair_data(&rows);
    FOR_LOOP(i, 3) {
        reset_entities(); setup_test_world();
        edict_t *worker = review_order_unit(0, 0);
        edict_t *target = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 512, 0);
        UnitAbilities_t abilities = {.abilList = codes[i]};
        worker->data.UnitAbilities = &abilities;
        target->health.value = target->health.max_value - 100;
        gi.LinkEntity(target);
        T_EQ(G_OrderId(names[i]), ids[i]);
        T_ASSERT(G_IssueUnitTargetOrder(worker, names[i], target, false, 0));
        T_EQ(worker->current_order_id, ids[i]);
        T_EQ(worker->build, target);
        target->health.value = target->health.max_value;
        /* Full health during approach is completion at contact, not target loss. */
        worker->currentmove->think(worker);
        T_EQ(worker->current_order_id, ids[i]);
        T_EQ(worker->build, target);
        worker->s.origin2 = target->s.origin2; gi.LinkEntity(worker);
        worker->currentmove->think(worker);
        worker->currentmove->think(worker);
        T_EQ(worker->current_order_id, 0); T_NULL(worker->build);
        target->health.value -= 100;
        T_ASSERT(G_IssueUnitTargetOrder(worker, "smart", target, false, 0));
        T_EQ(worker->current_order_id, 851971);
        T_ASSERT(G_IssueUnitPointOrder(worker, "move", &(vec2_t){768,0}, true, 0, 0));
        umove_t const *work = worker->currentmove;
        worker->buildwork->gold_accum = 0.25f;
        T_ASSERT(G_IssueUnitTargetOrder(worker, names[i], target, false, 0));
        T_EQ(worker->current_order_id, 851971); T_EQ(worker->currentmove, work);
        T_EQ(worker->order_queue.count, 1); T_FEQ(worker->buildwork->gold_accum, 0.25f, 0);
        /* An unknown family must not retag or replace an accepted Smart repair. */
        T_ASSERT(!G_IssueUnitTargetOrder(worker, names[(i + 1) % 3], target, false, 0));
        T_EQ(worker->current_order_id, 851971);
        T_EQ(worker->order_queue.count, 1);
        T_ASSERT(unit_issueimmediateorder(worker, "stop"));
        T_EQ(worker->current_order_id, 0); T_NULL(worker->build);
        T_ASSERT(S_OrderRepair(worker, target, 0));
        T_EQ(worker->current_order_id, 0); /* Internal construction work has no public head. */
        unit_issueimmediateorder(worker, "stop");
    }
    reset_entities(); setup_test_world();
    building_restore_repair_data(old, rows);
}

TEST(wc3_order_lifecycle, repair_autocast_orders_validate_direction_before_interrupting) {
    static cstring_t const codes[] = {"Arep", "Aren", "Arst"};
    static cstring_t const on[] = {"repairon", "renewon", "restorationon"};
    static cstring_t const off[] = {"repairoff", "renewoff", "restorationoff"};
    slkTestData_t *rows, *old = building_install_repair_data(&rows);
    FOR_LOOP(i, 3) {
        reset_entities(); setup_test_world();
        edict_t *worker = review_order_unit(0, 0);
        UnitAbilities_t abilities = {.abilList = codes[i]};
        worker->data.UnitAbilities = &abilities;
        vec2_t goal = {512, 0}, pending = {768, 0};
        T_ASSERT(!unit_issueimmediateorder(worker, off[i]));
        T_ASSERT(G_IssueUnitPointOrder(worker, "move", &goal, false, 0, 0));
        T_ASSERT(G_IssueUnitPointOrder(worker, "move", &pending, true, 0, 0));
        T_ASSERT(unit_issueimmediateorder(worker, on[i]));
        T_EQ(worker->current_order_id, 0); T_EQ(G_UnitQueuedOrderCount(worker), 0);
        T_ASSERT(worker->aiflags & AI_AUTOCAST_REPAIR);
        T_ASSERT(G_IssueUnitPointOrder(worker, "move", &goal, false, 0, 0));
        T_ASSERT(G_IssueUnitPointOrder(worker, "move", &pending, true, 0, 0));
        edict_t *destination = worker->goalentity; umove_t const *move = worker->currentmove;
        T_ASSERT(!unit_issueimmediateorder(worker, on[i]));
        T_EQ(worker->current_order_id, 851986); T_EQ(worker->goalentity, destination);
        T_EQ(worker->currentmove, move); T_EQ(G_UnitQueuedOrderCount(worker), 1);
        G_SetPlayerAbilityAvailable(&game.clients[0], FS_SLKKey(codes[i]), false);
        T_ASSERT(!unit_issueimmediateorder(worker, off[i]));
        T_EQ(worker->current_order_id, 851986); T_EQ(G_UnitQueuedOrderCount(worker), 1);
        G_SetPlayerAbilityAvailable(&game.clients[0], FS_SLKKey(codes[i]), true);
        T_ASSERT(unit_issueimmediateorder(worker, off[i]));
        T_EQ(worker->current_order_id, 0); T_EQ(G_UnitQueuedOrderCount(worker), 0);
        T_ASSERT(!(worker->aiflags & AI_AUTOCAST_REPAIR));
        T_ASSERT(!unit_issueimmediateorder(worker, off[i]));
    }
    reset_entities(); setup_test_world();
    building_restore_repair_data(old, rows);
}

TEST(wc3_order_lifecycle, repair_removed_target_cannot_become_a_reused_building) {
    slkTestData_t *rows, *old = building_install_repair_data(&rows);
    reset_entities(); setup_test_world();
    edict_t *worker = review_order_unit(0, 0);
    UnitAbilities_t abilities = {.abilList = "Arep"};
    worker->data.UnitAbilities = &abilities;
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 512, 0);
    target->spawn_time = level.time;
    target->health.value -= 100;
    T_ASSERT(G_IssueUnitTargetOrder(worker, "repair", target, false, 0));
    G_FreeEdict(target);
    T_EQ(worker->current_order_id, 852024);
    level.time += 1001;
    edict_t *replacement = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    replacement->spawn_time = level.time;
    T_EQ(replacement, target); replacement->health.value -= 100;
    float health = replacement->health.value;
    worker->currentmove->think(worker);
    T_EQ(worker->current_order_id, 0); T_NULL(worker->build);
    T_FEQ(replacement->health.value, health, 0);
    T_ASSERT(G_IssueUnitTargetOrder(worker, "repair", replacement, false, 0));
    G_DeferFreeEdict(replacement);
    T_EQ(worker->current_order_id, 852024); T_NULL(worker->build);
    T_ASSERT(G_IssueUnitPointOrder(worker, "move", &(vec2_t){512,0}, false, 0, 0));
    T_EQ(worker->current_order_id, 851986); T_NOT_NULL(worker->goalentity);
    reset_entities(); setup_test_world();
    building_restore_repair_data(old, rows);
}

TEST(wc3_order_lifecycle, queued_repair_family_activates_after_move_in_server_frames) {
    static cstring_t const names[] = {"repair", "renew", "restoration"};
    static cstring_t const codes[] = {"Arep", "Aren", "Arst"};
    static uint32_t const ids[] = {852024, 852161, 852202};
    slkTestData_t *rows, *old = building_install_repair_data(&rows);
    FOR_LOOP(i, 3) {
        reset_entities(); setup_test_world();
        T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
        edict_t *worker = review_order_unit(0, 0);
        edict_t *target = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 512, 0);
        UnitAbilities_t abilities = {.abilList = codes[i]};
        worker->data.UnitAbilities = &abilities;
        worker->think = monster_think;
        target->health.value -= 100;
        gi.LinkEntity(target);
        level.started = level.scriptsConfigured = level.scriptsStarted = true;
        game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 10000;
        game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 10000;
        T_ASSERT(G_IssueUnitPointOrder(worker, "move", &(vec2_t){128,0}, false, 0, 0));
        T_ASSERT(G_IssueUnitTargetOrder(worker, names[i], target, true, 0));
        T_EQ(worker->current_order_id, 851986); T_EQ(worker->order_queue.count, 1);
        for (int frame = 0; frame < 120 && worker->order_queue.count; frame++) {
            level.time += FRAMETIME; globals.RunFrame();
        }
        T_EQ(worker->order_queue.count, 0); T_EQ(worker->current_order_id, ids[i]);
        T_EQ(worker->build, target);
        target->health.value = target->health.max_value;
        for (int frame = 0; frame < 120 && worker->current_order_id; frame++) {
            level.time += FRAMETIME; globals.RunFrame();
        }
        T_EQ(worker->current_order_id, 0); T_NULL(worker->build);
        T_EQ(worker->order_queue.count, 0);
    }
    reset_entities(); setup_test_world();
    building_restore_repair_data(old, rows);
}

TEST(wc3_order_lifecycle, public_move_follow_ignores_automatic_combat) {
    reset_entities(); setup_test_world();
    edict_t *subject = review_order_unit(0, 0);
    edict_t *target = review_order_unit(512, 0);
    edict_t *enemy = review_order_unit(48, 1);
    T_ASSERT(G_IssueUnitTargetOrder(subject, "move", target, false, 0));
    level.time = 0; G_BeginAcquisitionFrame();
    G_AcquisitionEntityLinked(enemy);
    T_EQ(G_FindNearestEnemy(subject, G_AcquisitionRange(subject)), enemy);
    subject->currentmove->think(subject);
    T_EQ(subject->goalentity, target);
    T_NE(subject->goalentity, enemy);
    T_EQ(subject->current_order_id, 851986);
    T_ASSERT(subject->currentmove->proc == CAbilityMove);
    reset_entities(); setup_test_world();
}

TEST(wc3_order_lifecycle, follow_combat_target_loss_preserves_head_until_enemy_loss) {
    reset_entities(); setup_test_world();
    edict_t *subject = review_order_unit(0, 0);
    edict_t *target = review_order_unit(512, 0);
    edict_t *enemy = review_order_unit(48, 1);
    T_ASSERT(G_IssueUnitTargetOrder(subject, "smart", target, false, 0));
    T_ASSERT(G_IssueUnitPointOrder(subject, "move", &(vec2_t){1024, 0}, true, 0, 0));
    order_attack(subject, enemy);
    T_EQ(subject->current_order_id, 851971);
    G_DeferFreeEdict(target);
    T_NULL(subject->movement.follow_target);
    T_EQ(subject->goalentity, enemy);
    T_EQ(subject->current_order_id, 851971);
    T_EQ(subject->order_queue.count, 1);
    unit_die(enemy, NULL);
    T_EQ(subject->current_order_id, 851986);
    T_EQ(subject->order_queue.count, 0);
    T_NE(subject->goalentity, enemy);
    G_TestFinishDeferredFrees();
    reset_entities(); setup_test_world();
}

TEST(wc3_order_lifecycle, follow_incarnation_and_combat_removal_survive_save_before_drain) {
    cstring_t file = Test_TempPath("wc3-follow-retirement117.bin");
    /* alloc_test_unit supplies transient weapon rows; restore must resolve a
     * real authored row instead. The fixture MPQ has no UnitWeapons.slk. */
    slkTestData_t *weapons = parse_slk_string(
        "ID;PWXL;N;E\nC;Y1;X1;K\"unitWeaponID\"\nC;Y1;X2;K\"weapsOn\"\n"
        "C;Y2;X1;K\"hfoo\"\nC;Y2;X2;K3\nE\n");
    slkTestData_t *old_weapons = G_SetSLKRows("UnitWeapons", weapons);
    reset_entities(); setup_test_world();
    edict_t *subject = review_order_unit(0, 0);
    edict_t *target = review_order_unit(512, 0);
    edict_t *enemy = review_order_unit(48, 1);
    target->spawn_time = 4242;
    game.clients[0].ps.rdflags |= RDF_NOFOG;
    T_ASSERT(G_IssueUnitTargetOrder(subject, "smart", target, false, 0));
    T_EQ(subject->movement.follow_target_spawn_time, 4242);
    T_ASSERT(WriteGame(file));
    subject->movement.follow_target_spawn_time = 1;
    T_ASSERT(ReadGame(file));
    T_EQ(subject->movement.follow_target_spawn_time, 4242);
    T_ASSERT(S_AttackCanTarget(subject, enemy));
    order_attack(subject, enemy);
    T_EQ(subject->current_order_id, 851971);
    T_EQ(subject->goalentity, enemy);
    G_DeferFreeEdict(target);
    T_ASSERT(WriteGame(file));
    T_ASSERT(ReadGame(file));
    T_EQ(subject->current_order_id, 851971);
    T_NULL(subject->movement.follow_target);
    T_ASSERT(G_IsDeferredFree(target));
    G_TestFinishDeferredFrees();
    T_ASSERT(!target->inuse);
    unit_die(enemy, NULL);
    T_EQ(subject->current_order_id, 0);
    remove(file);
    reset_entities(); setup_test_world();
    G_SetSLKRows("UnitWeapons", old_weapons);
    free_slk_rows(weapons);
}

TEST(wc3_order_lifecycle, follow_damage_callback_nested_replacement_survives_owner_exit) {
    for (int smart = 0; smart < 2; smart++) {
        char script[4096];
        reset_entities(); setup_test_world();
        snprintf(script, sizeof(script),
            "globals\nunit subject\nunit target\nunit enemy\ntrigger nested\n"
            "integer removedHead=0\ninteger replacementHead=0\nendglobals\n"
            "function child takes nothing returns nothing\n"
            "call IssuePointOrder(subject, \"move\", 1024.0, 0.0)\n"
            "call RemoveUnit(enemy)\nendfunction\n"
            "function damaged takes nothing returns nothing\n"
            "call RemoveUnit(target)\nset removedHead=GetUnitCurrentOrder(subject)\n"
            "call TriggerExecute(nested)\nset replacementHead=GetUnitCurrentOrder(subject)\nendfunction\n"
            "function verify takes nothing returns nothing\n"
            "call BJassAssert(removedHead==%d, \"Follow loss head\")\n"
            "call BJassAssert(replacementHead==851986, \"nested Move owns head\")\n"
            "call BJassAssert(GetUnitCurrentOrder(subject)==851986, \"old owner cannot complete replacement\")\n"
            "endfunction\nfunction main takes nothing returns nothing\n"
            "local trigger damage=CreateTrigger()\n"
            "set subject=CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
            "set target=CreateUnit(Player(0), 'hfoo', 512.0, 0.0, 0.0)\n"
            "set enemy=CreateUnit(Player(1), 'hfoo', 48.0, 0.0, 0.0)\n"
            "call SetUnitUserData(subject, 900)\ncall SetUnitUserData(enemy, 901)\n"
            "call IssueTargetOrder(subject, \"%s\", target)\n"
            "set nested=CreateTrigger()\ncall TriggerAddAction(nested, function child)\n"
            "call TriggerRegisterUnitEvent(damage, %s, EVENT_UNIT_DAMAGED)\n"
            "call TriggerAddAction(damage, function damaged)\nendfunction\n",
            smart ? 851971 : 0, smart ? "smart" : "move", smart ? "enemy" : "subject");
        T_ASSERT(run_test_jass(script));
        edict_t *subject = NULL, *enemy = NULL;
        FILTER_EDICTS(ent, ent->inuse) {
            if (ent->user_data == 900) subject = ent;
            if (ent->user_data == 901) enemy = ent;
        }
        T_NOT_NULL(subject); T_NOT_NULL(enemy);
        if (subject && enemy) {
            if (smart) order_attack(subject, enemy);
            T_Damage(smart ? enemy : subject, smart ? subject : enemy, 1);
            level.started = level.scriptsConfigured = level.scriptsStarted = true;
            level.time += FRAMETIME; globals.RunFrame();
            jass_callbyname(level.vm, "verify", true);
            T_ASSERT(!jass_rterror_pending(level.vm));
            T_EQ(subject->current_order_id, 851986);
            T_NULL(subject->movement.follow_target);
            T_NE(subject->goalentity, enemy);
            T_ASSERT(!enemy->inuse);
        }
    }
    reset_entities(); setup_test_world();
}

TEST(wc3_order_lifecycle, smart_combat_enemy_death_resumes_healthy_follow_parent) {
    reset_entities(); setup_test_world();
    edict_t *subject = review_order_unit(0, 0);
    edict_t *target = review_order_unit(512, 0);
    edict_t *enemy = review_order_unit(48, 1);
    T_ASSERT(G_IssueUnitTargetOrder(subject, "smart", target, false, 0));
    order_attack(subject, enemy);
    unit_die(enemy, NULL);
    T_EQ(subject->current_order_id, 851971);
    T_EQ(subject->movement.follow_target, target);
    T_EQ(subject->goalentity, target);
    T_ASSERT(subject->currentmove->proc == CAbilityMove);
    reset_entities(); setup_test_world();
}

TEST(wc3_order_lifecycle, retired_follow_subject_reuse_does_not_inherit_old_callbacks) {
    for (int smart = 0; smart < 2; smart++) {
        reset_entities(); setup_test_world();
        T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
        edict_t *subject = review_order_unit(0, 0);
        edict_t *target = review_order_unit(512, 0);
        subject->spawn_time = 10;
        T_ASSERT(G_IssueUnitTargetOrder(subject, smart ? "smart" : "move", target, false, 0));
        T_ASSERT(G_IssueUnitPointOrder(subject, "move", &(vec2_t){1024, 0}, true, 0, 0));
        G_DeferFreeEdict(subject); G_TestFinishDeferredFrees();
        level.time += 1001;
        edict_t *replacement = review_order_unit(128, 0);
        replacement->spawn_time = level.time;
        T_EQ(replacement, subject);
        T_NULL(replacement->movement.follow_target);
        T_EQ(replacement->movement.follow_target_spawn_time, 0);
        T_EQ(replacement->current_order_id, 0);
        T_EQ(replacement->order_queue.count, 0);
        T_ASSERT(G_IssueUnitPointOrder(replacement, "move", &(vec2_t){1024, 0}, false, 0, 0));
        G_DeferFreeEdict(target);
        T_EQ(replacement->current_order_id, 851986);
        replacement->think = monster_think;
        level.started = level.scriptsConfigured = level.scriptsStarted = true;
        FOR_LOOP(frame, 4) { level.time += FRAMETIME; globals.RunFrame(); }
        T_EQ(replacement->current_order_id, 851986);
        T_ASSERT(replacement->s.origin2.x > 128);
    }
    reset_entities(); setup_test_world();
}

TEST(wc3_order_lifecycle, follow_direct_free_cannot_adopt_reused_target) {
    for (int smart = 0; smart < 2; smart++) {
        reset_entities(); setup_test_world();
        edict_t *subject = review_order_unit(0, 0);
        edict_t *target = review_order_unit(512, 0);
        target->spawn_time = level.time;
        T_ASSERT(G_IssueUnitTargetOrder(subject, smart ? "smart" : "move", target, false, 0));
        G_FreeEdict(target);
        level.time += 1001;
        edict_t *replacement = review_order_unit(128, 0);
        replacement->spawn_time = level.time;
        T_EQ(replacement, target);
        subject->currentmove->think(subject);
        T_NULL(subject->movement.follow_target);
        T_EQ(subject->current_order_id, 0);
        T_NE(subject->goalentity, replacement);
    }
    reset_entities(); setup_test_world();
}

TEST(wc3_order_lifecycle, guard254_timer_ownership_survives_move_save_and_rebase) {
    reset_entities();setup_test_world();
    float old_distance=game.constants.guardDistance,old_return=game.constants.guardReturnTime;
    game.constants.guardDistance=700;game.constants.guardReturnTime=7;
    level.timer_clock_valid=false;level.pathing_clock=(wc3Clock_t){8,0,300};
    edict_t *unit=review_order_unit(0,PLAYER_NEUTRAL_PASSIVE);
    T_ASSERT(unit->attack_guard.initialized);T_ASSERT(unit->attack_guard.timer.active);
    T_ASSERT(!unit->attack_guard.returning);T_FEQ(unit->attack_guard.timer.deadline.time,10,0);
    unit->s.origin2.x=900;unit->movement.pose_valid=false;
    unit_stand(unit);
    T_ASSERT(unit->attack_guard.returning);T_FEQ(unit->attack_guard.timer.deadline.time,15,0);
    uint32_t serial=unit->attack_guard.timer.sequence,number=unit->s.number;
    T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){1200,0},false,unit->s.player,0));
    T_EQ(unit->attack_guard.timer.sequence,serial);T_FEQ(unit->attack_guard.point.x,0,0);
    cstring_t file=Test_TempPath("wc3-guard254.bin");
    T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);unit=g_edicts+number;
    T_EQ(unit->attack_guard.timer.sequence,serial);T_ASSERT(unit->attack_guard.returning);
    T_FEQ(unit->attack_guard.timer.deadline.time,15,0);T_FEQ(unit->attack_guard.range,700,0);
    abilityCall_t call={.clock_span=300};CAbilityAttack(NULL,A_PRIMARY_TIMER_REBASE,&call);
    T_EQ(unit->attack_guard.timer.deadline.epoch,1);T_FEQ(unit->attack_guard.timer.deadline.time,-285,0);
    G_SetUnitPlayer(unit,0);
    T_ASSERT(!unit->attack_guard.timer.active);T_ASSERT(!unit->attack_guard.initialized);
    game.constants.guardDistance=old_distance;game.constants.guardReturnTime=old_return;
    reset_entities();setup_test_world();
}

TEST(wc3_order_lifecycle, guard254_death_and_removal_unlink_pending_return) {
    FOR_LOOP(mode,2) {
        reset_entities();setup_test_world();
        level.timer_clock_valid=false;level.pathing_clock=(wc3Clock_t){8,0,300};
        edict_t *unit=review_order_unit(0,PLAYER_NEUTRAL_PASSIVE);
        T_ASSERT(unit->attack_guard.timer.active);
        if(mode)G_FreeEdict(unit);else unit_die(unit,NULL);
        T_ASSERT(!unit->attack_guard.timer.active);
    }
    reset_entities();setup_test_world();
}

TEST(wc3_order_lifecycle, guard255_periodic_poll_retains_serial_and_catches_up_in_one_drain) {
    reset_entities();setup_test_world();
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    level.timer_clock_valid=false;level.pathing_clock=(wc3Clock_t){8,0,300};
    edict_t *units[]={review_order_unit(0,PLAYER_NEUTRAL_PASSIVE),
        review_order_unit(128,PLAYER_NEUTRAL_PASSIVE)};
    uint32_t serials[]={units[0]->attack_guard.timer.sequence,units[1]->attack_guard.timer.sequence};
    T_ASSERT(serials[0]<serials[1]);
    level.pathing_clock.time=14;
    bool scheduled=level.scheduled_frame;level.scheduled_frame=true;G_RunTimers();level.scheduled_frame=scheduled;
    FOR_LOOP(i,2) {
        T_ASSERT(units[i]->attack_guard.timer.active);T_ASSERT(!units[i]->attack_guard.returning);
        T_EQ(units[i]->attack_guard.timer.sequence,serials[i]);
        T_FEQ(units[i]->attack_guard.timer.deadline.time,16,0);
    }
    cstring_t file=Test_TempPath("wc3-guard255-periodic.bin");
    T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
    level.pathing_clock.time=16;level.timer_clock_valid=false;
    level.scheduled_frame=true;G_RunTimers();level.scheduled_frame=scheduled;
    FOR_LOOP(i,2) {
        T_EQ(units[i]->attack_guard.timer.sequence,serials[i]);
        T_FEQ(units[i]->attack_guard.timer.deadline.time,18,0);
    }
    reset_entities();setup_test_world();
}

/*67abe0 publishes the new user head before its internal task. Busy693490
 * only retains the packet; native Shift captures are in Queue263. */
static unsigned queue263_points;
static uint32_t queue263_head;
static vec2_t queue263_point;
void G_TestIssuedPointObserver(void (*observer)(edict_t *));
static void queue263_point_event(edict_t *unit) {
    queue263_points++;
    queue263_head=unit->current_order_id;
    T_ASSERT(G_GetIssuedOrderPoint(unit,&queue263_point));
}
static edict_t *queue263_setup(void) {
    reset_entities();setup_test_world();
    uint8_t cells[128*128]={0};CM_SetupTestPathmap(128,128,cells);
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{4096,4096}});
    edict_t *client=alloc_test_unit(0,0,0);
    client->client=game.clients;client->client->ps.number=0;
    edict_t *unit=review_order_unit(512,0);unit->s.origin2.y=512;gi.LinkEntity(unit);
    T_ASSERT(unit_issueorder(unit,"move",&(vec2_t){512,1800}));
    queue263_points=queue263_head=0;
    G_TestIssuedPointObserver(queue263_point_event);
    return unit;
}
static void queue263_close(void) {
    G_TestIssuedPointObserver(NULL);reset_entities();setup_test_world();
}
TEST(wc3_order_lifecycle, queue263_generic_point_event_belongs_to_activation_and_save) {
    FOR_LOOP(saved,2) {
        edict_t *unit=queue263_setup();
        vec2_t point={1024,512};
        T_ASSERT(G_IssueUnitPointOrder(unit,"attack",&point,true,0,0));
        T_EQ(queue263_points,0);T_EQ(unit->current_order_id,G_OrderId("move"));
        T_EQ(unit->order_queue.count,1);
        if(saved) {
            cstring_t file=Test_TempPath("wc3-queue263-point.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
        }
        unit_stand(unit);
        T_EQ(queue263_points,1);T_EQ(queue263_head,G_OrderId("attack"));
        T_FEQ(queue263_point.x,point.x,0);T_FEQ(queue263_point.y,point.y,0);
        T_EQ(unit->current_order_id,G_OrderId("attack"));
        T_EQ(unit->order_queue.count,0);T_NULL(unit->order_queue.entries);
        T_NOT_NULL(unit->movement.attackmove_waypoint);
        queue263_close();
    }
}
TEST(wc3_order_lifecycle, queue263_removed_attack_target_uses_retained_point_and_generation) {
    FOR_LOOP(reuse,2)FOR_LOOP(saved,2) {
        edict_t *unit=queue263_setup(),*target=review_order_unit(1024,1);
        target->s.origin2.y=512;gi.LinkEntity(target);
        target->spawn_time=777;
        T_ASSERT(G_IssueUnitTargetOrder(unit,"attack",target,true,0));
        T_EQ(queue263_points,0);T_EQ(unit->order_queue.count,1);
        if(reuse)target->spawn_time++;else G_DeferFreeEdict(target);
        if(saved) {
            cstring_t file=Test_TempPath("wc3-queue263-target.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
        }
        unit_stand(unit);
        T_EQ(queue263_points,1);T_EQ(queue263_head,G_OrderId("attack"));
        T_FEQ(queue263_point.x,1024,0);T_FEQ(queue263_point.y,512,0);
        T_EQ(unit->current_order_id,G_OrderId("attack"));
        T_NULL(unit->movement.follow_target);T_NOT_NULL(unit->movement.attackmove_waypoint);
        T_EQ(unit->order_queue.count,0);T_NULL(unit->order_queue.entries);
        queue263_close();
    }
}
TEST(wc3_order_lifecycle, queue263_empty_and_canceled_queue_do_not_publish_pending_orders) {
    edict_t *unit=queue263_setup();
    T_ASSERT(!G_UnitStartNextQueuedOrder(unit));T_EQ(queue263_points,0);
    T_EQ(unit->order_queue.count,0);T_NULL(unit->order_queue.entries);
    T_ASSERT(G_IssueUnitPointOrder(unit,"attack",&(vec2_t){1024,512},true,0,0));
    T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){1536,512},true,0,0));
    T_EQ(queue263_points,0);T_EQ(unit->order_queue.count,2);
    order_stop(unit);
    T_EQ(queue263_points,0);T_EQ(unit->order_queue.count,0);T_NULL(unit->order_queue.entries);
    T_ASSERT(!G_UnitStartNextQueuedOrder(unit));T_EQ(queue263_points,0);
    queue263_close();
}

TEST(wc3_order_lifecycle, queue263_live_target_event_waits_for_activation) {
    edict_t *unit=queue263_setup(),*target=review_order_unit(1024,1);
    target->user_data=264;
    T_ASSERT(run_test_jass("globals\ntrigger listener\ninteger visits=0\nendglobals\n"
        "function issued takes nothing returns nothing\nset visits=visits+1\n"
        "call BJassAssert(GetUnitCurrentOrder(GetTriggerUnit())==OrderId(\"attack\"),\"new target head before task\")\n"
        "call BJassAssert(GetUnitUserData(GetOrderTargetUnit())==264,\"retained target payload\")\nendfunction\n"
        "function zero takes nothing returns nothing\ncall BJassAssert(visits==0,\"no issued event at append\")\nendfunction\n"
        "function one takes nothing returns nothing\ncall BJassAssert(visits==1,\"one activation event\")\nendfunction\n"
        "function close takes nothing returns nothing\ncall DestroyTrigger(listener)\nendfunction\n"
        "function main takes nothing returns nothing\nset listener=CreateTrigger()\n"
        "call TriggerRegisterPlayerUnitEvent(listener,Player(0),ConvertPlayerUnitEvent(40),null)\n"
        "call TriggerAddAction(listener,function issued)\nendfunction\n"));
    T_ASSERT(G_IssueUnitTargetOrder(unit,"attack",target,true,0));
    jass_callbyname(level.vm,"zero",false);
    unit_stand(unit);jass_callbyname(level.vm,"one",false);
    T_EQ(unit->current_order_id,G_OrderId("attack"));T_EQ(unit->goalentity,target);
    T_EQ(queue263_points,0);T_EQ(unit->order_queue.count,0);T_NULL(unit->order_queue.entries);
    jass_callbyname(level.vm,"close",false);queue263_close();
}
TEST(wc3_order_lifecycle, queue263_callback_replacement_stop_and_removal_keep_task_ownership) {
    FOR_LOOP(mode,3) {
        edict_t *unit=queue263_setup();char script[3072];
        snprintf(script,sizeof(script),
            "globals\ntrigger listener\nboolean entered=false\ninteger visits=0\nendglobals\n"
            "function issued takes nothing returns nothing\nset visits=visits+1\n"
            "if not entered then\nset entered=true\n"
            "call BJassAssert(GetUnitCurrentOrder(GetTriggerUnit())==OrderId(\"attack\"),\"outer head before task\")\n"
            "%s\nendif\nendfunction\n"
            "function verify takes nothing returns nothing\ncall BJassAssert(entered and visits==%u,\"activation callback count\")\nendfunction\n"
            "function close takes nothing returns nothing\ncall DestroyTrigger(listener)\nendfunction\n"
            "function main takes nothing returns nothing\nset listener=CreateTrigger()\n"
            "call TriggerRegisterPlayerUnitEvent(listener,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)\n"
            "call TriggerAddAction(listener,function issued)\nendfunction\n",
            mode==0 ? "call IssuePointOrder(GetTriggerUnit(),\"move\",2000.0,1200.0)" :
            mode==1 ? "call IssueImmediateOrder(GetTriggerUnit(),\"stop\")" : "call RemoveUnit(GetTriggerUnit())",
            mode==0 ? 2u : 1u);
        T_ASSERT(run_test_jass(script));
        T_ASSERT(G_IssueUnitPointOrder(unit,"attack",&(vec2_t){1024,512},true,0,0));
        T_EQ(queue263_points,0);
        unit_stand(unit);jass_callbyname(level.vm,"verify",false);
        if(mode==0) {
            T_EQ(unit->current_order_id,G_OrderId("move"));T_NULL(unit->movement.attackmove_waypoint);
            T_NOT_NULL(unit->goalentity);
            if(unit->goalentity)T_FEQ(unit->goalentity->s.origin2.x,2000,0);
        } else if(mode==1) {
            T_EQ(unit->current_order_id,0);T_NOT_NULL(unit->movement.attackmove_waypoint);
            T_NOT_NULL(unit->goalentity);
        } else {T_ASSERT(G_IsDeferredFree(unit));T_NULL(unit->movement.attackmove_waypoint);}
        T_EQ(unit->order_queue.count,0);T_NULL(unit->order_queue.entries);
        jass_callbyname(level.vm,"close",false);queue263_close();
    }
}
TEST(wc3_order_lifecycle, queue263_rejected_successor_publishes_then_advances_without_leaking_storage) {
    FOR_LOOP(saved,2) {
        edict_t *unit=queue263_setup();
        /* A retired Repair target cannot produce a repair task. The next
         * user head still activates synchronously through the common loop. */
        edict_t *target=review_order_unit(1024,0);
        T_ASSERT(G_QueueUnitOrder(unit,"repair",UNIT_ORDER_TARGET_ENTITY,NULL,target,0,0,0));
        T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){1536,512},true,0,0));
        G_DeferFreeEdict(target);
        if(saved) {
            cstring_t file=Test_TempPath("wc3-queue263-rejected.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
        }
        T_EQ(queue263_points,0);unit_stand(unit);
        T_EQ(queue263_points,2);T_EQ(queue263_head,G_OrderId("move"));
        T_EQ(unit->current_order_id,G_OrderId("move"));T_NOT_NULL(unit->goalentity);
        T_EQ(unit->order_queue.count,0);T_NULL(unit->order_queue.entries);
        queue263_close();
    }
}

TEST(wc3_order_lifecycle, queue263_direct_append_resolves_optional_user_order_id) {
    edict_t *unit=queue263_setup();
    unitOrder_t queued={.target_type=UNIT_ORDER_TARGET_POINT,.point={1024,512}};
    strlcpy(queued.order,"move",sizeof(queued.order));
    T_ASSERT(G_AppendUnitOrder(unit,&queued));T_EQ(queue263_points,0);
    unit_stand(unit);T_EQ(queue263_points,1);T_EQ(queue263_head,G_OrderId("move"));
    T_EQ(unit->current_order_id,G_OrderId("move"));T_NULL(unit->order_queue.entries);
    queue263_close();
}


/* Native49e277/49e2b7: missing Move does not admit targets merely inside
 * acquisition range. A zero-speed Move ability still owns eligibility. */
/* Rejected alternatives survive current-target loss and bypass the ordinary
 * scan radius, but remain weak identities and obey cleanup/eligibility. */
TEST(wc3_order_lifecycle, group341_rejected_alternative_lifetime_and_saved_fallback) {
    FOR_LOOP(saved,2)FOR_LOOP(mode,14) {
        reset_entities();setup_test_world();level.show_map_cheat=true;
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{4096,4096}});
        edict_t *unit=review_order_unit(512,0),*old=review_order_unit(576,1),*target=review_order_unit(640,0);
        unit->spawn_time=11;old->spawn_time=12;target->spawn_time=13;
        unit->s.model=old->s.model=target->s.model=1;
        unit->collision=old->collision=target->collision=31;
        unit->invulnerable=true;unit->runtime.acquisition_range=1100;
        S_AttackProfileWrite(unit,0)->range=90;
        old->health.value=target->health.value=420;
        old->defense_type=target->defense_type=unit->defense_type=2;
        G_PublishMoveSpatialObject(unit);G_PublishMoveSpatialObject(old);G_PublishMoveSpatialObject(target);
        T_ASSERT(G_IssueUnitPointOrder(unit,"attack",&(vec2_t){1800,0},false,0,0));
        order_attack(unit,old);T_EQ(unit->attack_target,old);
        G_SetUnitPlayer(target,1);
        T_EQ(unit->attack_target,old);T_EQ(unit->attack_alternative,target);
        T_EQ(unit->attack_alternative_spawn_time,target->spawn_time);
        if(saved) {
            UnitWeapons_t const *weapons=unit->data.UnitWeapons;
            cstring_t file=Test_TempPath("wc3-group341-alternative.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            unit->data.UnitWeapons=old->data.UnitWeapons=target->data.UnitWeapons=weapons;
            T_EQ(unit->attack_alternative,target);
            T_EQ(unit->attack_alternative_spawn_time,target->spawn_time);
        }
        unit->runtime.acquisition_range=1;
        if(mode==1)unit_die(old,NULL);
        if(mode==2 || mode>=12)G_DeferFreeEdict(old);
        if(mode==3)G_SetUnitPlayer(target,0);
        if(mode==4)T_ASSERT(unit_issueimmediateorder(unit,"stop"));
        if(mode==5)T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){1800,0},false,0,0));
        if(mode==6 || mode==8)G_FreeEdict(target);
        if(mode==7)S_SetUnitPaused(unit,true);
        if(mode==8) {
            uint32_t incarnation=13;
            level.time+=1001;
            edict_t *replacement=review_order_unit(640,1);
            replacement->spawn_time=level.time;
            T_EQ(replacement,target);T_NE(replacement->spawn_time,incarnation);
        }
        if(mode==9)T_ASSERT(G_ActorRemoveSkill(unit,MAKEFOURCC('A','m','o','v')));
        if(mode==10) {target->s.origin2.x=3000;gi.LinkEntity(target);G_PublishMoveSpatialObject(target);}
        if(mode==11)target->invulnerable=true;
        bool cleared=mode==4 || mode==5 || mode==7;
        T_EQ(unit->attack_alternative,cleared ? NULL : target);
        if(mode==1 || mode==2 || mode>=12) {
            T_NULL(unit->attack_target);
            T_EQ(G_FindNearestEnemy(unit,1),target);
            for(int i=0;i<16 && !unit->attack_target;i++) {
                level.time+=FRAMETIME;
                if(unit->currentmove && unit->currentmove->think)unit->currentmove->think(unit);
            }
            T_EQ(unit->attack_target,target);
            T_EQ(unit->attack_alternative,target);
            if(mode>=12) {
                G_SetUnitPlayer(target,mode==12 ? 0 : 2);
                T_NULL(unit->attack_alternative);
                T_EQ(unit->attack_alternative_spawn_time,0);
                if(mode==12)T_NULL(unit->attack_target);
            }
        } else {
            /* Exercise fallback eligibility after retiring the current target;
             * a live current target would mask every guard below. */
            if(mode==3 || mode==6 || mode>=8)G_DeferFreeEdict(old);
            T_NULL(G_FindNearestEnemy(unit,1));
        }
    }
    reset_entities();setup_test_world();
}

/* Public24-scene native matrix: availability tests minimum range only for
 * airborne candidates. The explicit task validator has a separate policy. */
TEST(wc3_order_lifecycle, group340_available_minimum_range_precedes_exemption) {
    FOR_LOOP(saved,2)FOR_LOOP(disabled,2)FOR_LOOP(scene,24) {
        reset_entities();setup_test_world();level.show_map_cheat=true;
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{4096,4096}});
        level.timer_clock_valid=false;level.pathing_clock=(wc3Clock_t){8,0,300};
        edict_t *unit=review_order_unit(512,0),*target=review_order_unit(scene>=12 ? 800 : 640,0);
        bool flying=(scene/3)%2,immobile=(scene/6)%2;
        unit->s.model=target->s.model=1;unit->collision=target->collision=31;
        unit->runtime.acquisition_range=1100;unit->invulnerable=true;
        UnitWeapons_t const *authored=unit->data.UnitWeapons;
        UnitWeapons_t weapons=*authored;
        weapons.minimumAttackRange=scene%3==0 ? 120 : scene%3==1 ? 0 : 600;
        weapons.attacksEnabled=1;unit->data.UnitWeapons=&weapons;
        S_AttackProfileWrite(unit,0)->range=500;
        S_AttackProfileWrite(unit,0)->targetsAllowed=WC3_TARGET_FLAG_GROUND|WC3_TARGET_FLAG_AIR;
        if(flying){target->aiflags|=AI_FLYING;target->targtype=TARG_AIR;}
        if(immobile) {
            T_ASSERT(G_ActorRemoveSkill(unit,MAKEFOURCC('A','m','o','v')));
            if(disabled)unit->aiflags|=AI_IMMOBILE;
        }
        G_PublishMoveSpatialObject(unit);G_PublishMoveSpatialObject(target);
        if(saved) {
            uint32_t u=unit->s.number,t=target->s.number;
            cstring_t file=Test_TempPath("wc3-group340-minimum.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            unit=g_edicts+u;target=g_edicts+t;
            unit->data.UnitWeapons=&weapons;target->data.UnitWeapons=authored;
        }
        uint32_t head=unit->current_order_id;
        T_ASSERT(!unit->attack_speed_cap.active);T_NULL(unit->attack_target);
        G_SetUnitPlayer(target,1);
        bool accepted=scene!=3 && scene!=9;
        T_EQ(unit->attack_speed_cap.active,accepted);
        T_EQ(unit->attack_target,accepted ? target : NULL);
        T_EQ(unit->current_order_id,head);
        if(!accepted) {
            weapons.minimumAttackRange=0;
            G_SetUnitPlayer(target,0);G_SetUnitPlayer(target,1);
            T_ASSERT(unit->attack_speed_cap.active);T_EQ(unit->attack_target,target);
        }
        unit->data.UnitWeapons=authored;
    }
    reset_entities();setup_test_world();
}

TEST(wc3_order_lifecycle, guard270_available_target_requires_reachable_weapon_range) {
    FOR_LOOP(mode,11) {
        reset_entities();setup_test_world();level.show_map_cheat=true;
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{4096,4096}});
        level.timer_clock_valid=false;level.pathing_clock=(wc3Clock_t){8,0,300};
        edict_t *unit=review_order_unit(0,0),*candidate=review_order_unit(mode==1 || mode==5 ? 128 : mode==10 ? 80 : 300,0);
        unit->s.model=candidate->s.model=1;unit->collision=candidate->collision=31;
        unit->runtime.acquisition_range=400;unit->invulnerable=true;
        S_AttackProfileWrite(unit,0)->range=90;
        if(mode!=2 && mode!=3)
            T_ASSERT(G_ActorRemoveSkill(unit,MAKEFOURCC('A','m','o','v')));
        if(mode==3)unit->unitinfo.MoveSpeed=0; /* Not an absent/disabled Move. */
        if(mode==4)S_AttackProfileWrite(unit,0)->range=300; /* Authored override. */
        UnitWeapons_t const *authored=unit->data.UnitWeapons;
        UnitWeapons_t weapons=*authored;
        if(mode==5 || mode>=8) {
            weapons.attacksEnabled=mode==9 ? 1 : 3;unit->data.UnitWeapons=&weapons;
            S_AttackProfileWrite(unit,1)->type=ATK_NORMAL;
            S_AttackProfileWrite(unit,1)->targetsAllowed=WC3_TARGET_FLAG_AIR;
            S_AttackProfileWrite(unit,1)->range=500;
        }
        if(mode==10)S_AttackProfileWrite(unit,0)->range=10;
        if(mode==5) {
            candidate->movement.fine_pose=(vec2_t){4,0};
            candidate->movement.pose_world=candidate->s.origin2;
            candidate->movement.pose_valid=candidate->movement.clock_valid=true;
            candidate->movement.pose_clock=(wc3Clock_t){7.5f,0,300};
            candidate->movement.velocity=(vec2_t){400,0}; /* Leaves predicted weapon reach. */
        }
        G_PublishMoveSpatialObject(unit);G_PublishMoveSpatialObject(candidate);
        if(mode==6) {
            uint32_t u=unit->s.number,c=candidate->s.number;
            UnitWeapons_t const *weapons=unit->data.UnitWeapons;
            cstring_t file=Test_TempPath("wc3-guard270.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            unit=g_edicts+u;candidate=g_edicts+c;
            unit->data.UnitWeapons=candidate->data.UnitWeapons=weapons;
        }
        if(mode==7)T_ASSERT(G_ActorAddSkill(unit,MAKEFOURCC('A','m','o','v')));
        uint32_t head=unit->current_order_id;wc3Random_t random=level.pathing_random;
        T_ASSERT(!unit->attack_speed_cap.active);T_NULL(unit->attack_target);
        G_SetUnitPlayer(candidate,1);
        bool accepted=mode==1 || mode==2 || mode==3 || mode==4 || mode==7 || mode==10;
        T_EQ(unit->attack_speed_cap.active,accepted);
        T_EQ(unit->attack_target,accepted ? candidate : NULL);
        T_EQ(unit->current_order_id,head);
        T_EQ(level.pathing_random.sum,random.sum);T_EQ(level.pathing_random.index,random.index);
    }
    reset_entities();setup_test_world();
}
TEST(wc3_order_lifecycle, neutral_creep_guard_keeps_spawn_anchor_independent_of_stop) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    T_ASSERT(creep->movement.creep_guard_enabled);
    T_FEQ(creep->movement.creep_guard_origin.x, 128, 0.01f);
    creep->s.origin2.x = 256;
    G_SetUnitGuardPosition(creep);
    T_FEQ(creep->movement.creep_guard_origin.x, 128, 0.01f);
    T_FEQ(creep->movement.guard_position.x, 256, 0.01f);
}

TEST(wc3_order_lifecycle, explicit_order_drops_only_creep_auto_combat) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    G_CreepGuardAutoCombat(creep);
    T_ASSERT(creep->movement.creep_guard_phase == CREEP_GUARD_COMBAT);
    G_CreepGuardExplicitOrder(creep);
    T_ASSERT(creep->movement.creep_guard_phase != CREEP_GUARD_COMBAT);
    T_ASSERT(creep->movement.creep_guard_enabled);
    T_FEQ(creep->movement.creep_guard_origin.x, 128, 0.01f);
}

TEST(wc3_order_lifecycle, internal_order_notification_does_not_cancel_creep_guard) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    G_CreepGuardAutoCombat(creep);
    S_UnitAbilityOrderAccepted(creep, "attack");
    T_ASSERT(creep->movement.creep_guard_phase == CREEP_GUARD_COMBAT);
}

TEST(wc3_order_lifecycle, creep_guard_native_disable_keeps_anchor_and_explicit_order) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    G_CreepGuardAutoCombat(creep);
    G_CreepGuardSetEnabled(creep, false);
    T_ASSERT(!creep->movement.creep_guard_enabled);
    T_ASSERT(creep->movement.creep_guard_phase != CREEP_GUARD_COMBAT);
    T_ASSERT(creep->movement.creep_guard_phase != CREEP_GUARD_RETURNING);
    T_FEQ(creep->movement.creep_guard_origin.x, 128, 0.01f);
    creep->s.origin2.x = 350;
    G_CreepGuardSetEnabled(creep, true);
    T_ASSERT(creep->movement.creep_guard_enabled);
    T_ASSERT(creep->movement.creep_guard_phase != CREEP_GUARD_COMBAT);
    T_FEQ(creep->movement.creep_guard_origin.x, 128, 0.01f);
}

/* Disabling an active autonomous return must stop the Move, but it must not
 * assign a new player Stop guard position or move the original creep anchor. */
TEST(wc3_order_lifecycle, disable_creep_guard_cancels_active_return_move) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    G_SetUnitGuardPosition(creep);
    creep->s.origin2.x = creep->s.origin.x = 900;
    gi.LinkEntity(creep);
    G_CreepGuardAutoCombat(creep);
    T_ASSERT(G_CreepGuardIssueReturn(creep, &creep->movement.creep_guard_origin));
    T_EQ(creep->movement.creep_guard_phase, CREEP_GUARD_RETURNING);
    T_ASSERT(creep->currentmove && creep->currentmove->proc == CAbilityMove);

    G_CreepGuardSetEnabled(creep, false);
    T_ASSERT(!creep->movement.creep_guard_enabled);
    T_EQ(creep->movement.creep_guard_phase, CREEP_GUARD_IDLE);
    T_ASSERT(creep->currentmove && creep->currentmove->proc != CAbilityMove);
    T_FEQ(creep->movement.creep_guard_origin.x, 128, 0.001f);
    T_FEQ(creep->movement.guard_position.x, 128, 0.001f);
}

/* Disabling future automatic guarding must not interrupt a script-owned Move. */
TEST(wc3_order_lifecycle, disable_creep_guard_preserves_explicit_move) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    vec2_t const destination = { 400, 0 };
    G_CreepGuardInit(creep);
    T_ASSERT(G_IssueUnitPointOrder(creep, "move", &destination, false, creep->s.player, 0.0f));
    T_ASSERT(creep->currentmove && creep->currentmove->proc == CAbilityMove);
    edict_t *goal = creep->goalentity;
    G_CreepGuardSetEnabled(creep, false);
    T_ASSERT(!creep->movement.creep_guard_enabled);
    T_ASSERT(creep->currentmove && creep->currentmove->proc == CAbilityMove);
    T_ASSERT(creep->goalentity == goal);
}

/* Exercise the production post-mitigation damage path: a lethal blow must
 * alert a surviving idle camp member before the victim is torn down. */
TEST(wc3_order_lifecycle, lethal_creep_hit_alerts_surviving_camp_member) {
    setup_test_world();
    edict_t *victim = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    edict_t *ally = review_order_unit(192, PLAYER_NEUTRAL_AGGRESSIVE);
    edict_t *attacker = review_order_unit(240, 0);
    G_CreepGuardInit(victim);
    G_CreepGuardInit(ally);
    T_ASSERT(ally->movement.creep_guard_phase != CREEP_GUARD_COMBAT);
    T_Damage(victim, attacker, (int)victim->health.value + 100);
    T_ASSERT(M_IsDead(victim));
    T_ASSERT(ally->movement.creep_guard_phase == CREEP_GUARD_COMBAT);
    T_ASSERT(ally->goalentity == attacker);
    T_ASSERT(ally->currentmove && ally->currentmove->proc == CAbilityAttack);
}

/* Explicit immediate orders must cancel return ownership without changing its
 * independently stored original anchor. */
TEST(wc3_order_lifecycle, creep_guard_stop_and_hold_cancel_return) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    creep->s.origin2.x = 900;
    creep->movement.creep_guard_phase = CREEP_GUARD_RETURNING;
    T_ASSERT(unit_issueimmediateorder(creep, "stop"));
    T_EQ(creep->movement.creep_guard_phase, CREEP_GUARD_IDLE);
    T_FEQ(creep->movement.creep_guard_origin.x, 128, 0.01f);
    creep->movement.creep_guard_phase = CREEP_GUARD_RETURNING;
    T_ASSERT(unit_issueimmediateorder(creep, "holdposition"));
    T_EQ(creep->movement.creep_guard_phase, CREEP_GUARD_IDLE);
}

TEST(wc3_order_lifecycle, creep_guard_explicit_point_move_cancels_return) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    vec2_t destination = { 220, 0 };
    G_CreepGuardInit(creep);
    creep->movement.creep_guard_phase = CREEP_GUARD_RETURNING;
    T_ASSERT(G_IssueUnitPointOrder(creep, "move", &destination, false, creep->s.player, 0.0f));
    T_EQ(creep->movement.creep_guard_phase, CREEP_GUARD_IDLE);
    T_ASSERT(creep->currentmove && creep->currentmove->proc == CAbilityMove);
}

/* Ownership transfer aborts an AI return without recording a new Stop
 * guard anchor at the creep's displaced position. */
TEST(wc3_order_lifecycle, creep_guard_owner_transfer_preserves_stop_anchor) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    G_SetUnitGuardPosition(creep);
    creep->s.origin2.x = creep->s.origin.x = 900;
    gi.LinkEntity(creep);
    G_CreepGuardAutoCombat(creep);
    T_ASSERT(G_CreepGuardIssueReturn(creep, &creep->movement.creep_guard_origin));
    T_ASSERT(creep->currentmove && creep->currentmove->proc == CAbilityMove);

    G_SetUnitPlayer(creep, 0);
    T_ASSERT(!creep->movement.creep_guard_enabled);
    T_EQ(creep->movement.creep_guard_phase, CREEP_GUARD_IDLE);
    T_ASSERT(creep->currentmove && creep->currentmove->proc != CAbilityMove);
    T_FEQ(creep->movement.guard_position.x, 128, 0.001f);
}

TEST(wc3_order_lifecycle, creep_guard_owner_transfer_reconciles_policy) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    creep->movement.creep_guard_phase = CREEP_GUARD_RETURNING;
    G_SetUnitPlayer(creep, 0);
    T_ASSERT(!creep->movement.creep_guard_enabled);
    T_EQ(creep->movement.creep_guard_phase, CREEP_GUARD_IDLE);
    creep->s.origin2.x = 320;
    G_SetUnitPlayer(creep, PLAYER_NEUTRAL_AGGRESSIVE);
    T_ASSERT(creep->movement.creep_guard_enabled);
    T_EQ(creep->movement.creep_guard_phase, CREEP_GUARD_IDLE);
    T_FEQ(creep->movement.creep_guard_origin.x, 320, 0.01f);
}

/* Creep guard scheduler fixtures: a map script so globals.RunFrame pumps the
 * real server frame, and creeps the scheduler drives itself. */
static void creep_guard_setup_frame_world(void) {
    reset_entities();
    setup_test_world();
    level.time = 1000;
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
}

static void creep_guard_run_frames(uint32_t frames) {
    bool const started = level.started, scripts = level.scriptsStarted;
    level.started = level.scriptsStarted = true;
    FOR_LOOP(i, frames) { level.time += FRAMETIME; globals.RunFrame(); }
    level.started = started; level.scriptsStarted = scripts;
}

/* Anchor the creep at (anchor_x, 0), then teleport it to (x, y) so the leash sees distance. */
static edict_t *scheduled_creep(float anchor_x, float x, float y) {
    edict_t *creep = review_order_unit(anchor_x, PLAYER_NEUTRAL_AGGRESSIVE);
    creep->think = monster_think;
    creep->collision = 16.0f;
    creep->s.model = 1; /* IS_HOLLOW() ignores model-less edicts for collision */
    G_CreepGuardInit(creep);
    creep->s.origin2 = (vec2_t){ x, y };
    creep->s.origin.x = x;
    creep->s.origin.y = y;
    gi.LinkEntity(creep);
    return creep;
}

static bool creep_guard_attacking(edict_t const *creep) {
    return creep->currentmove && creep->currentmove->proc == CAbilityAttack;
}

static float creep_guard_home_distance(edict_t const *creep) {
    return Vector2_distance(&creep->s.origin2, &creep->movement.creep_guard_origin);
}

/* Two creeps walk home to the same anchor. B reaches it first; A's return
 * Move completes beside the occupied anchor and counts as home. An
 * enemy stands in acquisition range of that spot: a returning creep must not
 * acquire it, and the near-anchor stop must count as home. */
TEST(wc3_order_lifecycle, returning_creep_ignores_enemy_and_settles_beside_occupied_anchor) {
    creep_guard_setup_frame_world();
    edict_t *b = scheduled_creep(128, 400, 0);
    edict_t *a = scheduled_creep(128, 900, 0);
    edict_t *enemy = review_order_unit_at(160, 400, 0);
    int frame;
    S_AttackProfileWrite(b, 0)->cooldown = 0; /* B has no weapon, so it stays parked on the anchor */
    G_CreepGuardAutoCombat(b);
    T_ASSERT(G_CreepGuardIssueReturn(b, &b->movement.creep_guard_origin));
    G_CreepGuardAutoCombat(a);
    T_ASSERT(G_CreepGuardIssueReturn(a, &a->movement.creep_guard_origin));
    T_EQ(a->movement.creep_guard_phase, CREEP_GUARD_RETURNING);
    T_ASSERT(a->currentmove && a->currentmove->proc == CAbilityMove);
    for (frame = 0; frame < 400 && a->movement.creep_guard_phase == CREEP_GUARD_RETURNING; frame++) {
        creep_guard_run_frames(1);
        if (a->movement.creep_guard_phase == CREEP_GUARD_RETURNING) T_ASSERT(!creep_guard_attacking(a));
    }
    T_EQ(a->movement.creep_guard_phase, CREEP_GUARD_IDLE);
    T_EQ(b->movement.creep_guard_phase, CREEP_GUARD_IDLE);
    /* Retail point Moves stop inside their arrival band rather than snapping
     * onto the anchor; A's completed Move ends beside the occupied anchor. */
    T_ASSERT(creep_guard_home_distance(b) <= move_settle_distance(b));
    T_ASSERT(creep_guard_home_distance(a) > 4.0f);
    T_ASSERT(a->s.origin2.x > b->s.origin2.x);
    T_ASSERT(creep_guard_home_distance(a) < game.constants.guardDistance);
    T_ASSERT(!creep_guard_attacking(a));
    T_ASSERT(enemy->inuse);
}

/* An Ensnared returning creep stands in acquisition range of an enemy. Retail
 * returning creeps ignore enemies; the root only delays the return, and once it
 * expires the creep walks home and goes idle with no attack order. */
TEST(wc3_order_lifecycle, rooted_returning_creep_ignores_enemy_then_walks_home) {
    creep_guard_setup_frame_world();
    edict_t *creep = scheduled_creep(-800, 100, 0);
    edict_t *enemy = review_order_unit_at(100, 300, 0);
    /* Exercise real Stand loops: an unloaded model skips animation end
     * callbacks and concealed a return timer restarted by every idle loop. */
    creep->s.model = G_RegisterModel("TestUI\\Models\\anim_pulse.mdx");
    unit_stand(creep);
    T_ASSERT(creep->animation && creep->animation->interval[1] > creep->animation->interval[0]);
    int frame;
    G_CreepGuardAutoCombat(creep);
    T_ASSERT(G_CreepGuardIssueReturn(creep, &creep->movement.creep_guard_origin));
    T_EQ(creep->movement.creep_guard_phase, CREEP_GUARD_RETURNING);
    T_ASSERT(unit_addtimedstatus(creep, "BEer", 1, 2.0f));
    for (frame = 0; frame < 10; frame++) {
        creep_guard_run_frames(1);
        T_EQ(creep->movement.creep_guard_phase, CREEP_GUARD_RETURNING);
        T_ASSERT(!creep_guard_attacking(creep));
    }
    T_ASSERT(creep->currentmove && creep->currentmove->think == ai_stand);
    T_FEQ(creep->s.origin2.x, 100, 0.001f);
    for (frame = 0; frame < 400 && creep->movement.creep_guard_phase == CREEP_GUARD_RETURNING; frame++) {
        creep_guard_run_frames(1);
        if (creep->movement.creep_guard_phase == CREEP_GUARD_RETURNING) T_ASSERT(!creep_guard_attacking(creep));
    }
    T_EQ(creep->movement.creep_guard_phase, CREEP_GUARD_IDLE);
    T_ASSERT(creep_guard_home_distance(creep) <= move_settle_distance(creep));
    T_ASSERT(!creep_guard_attacking(creep));
    T_ASSERT(enemy->inuse);
}

/* Soft leash through the real scheduler: between GuardDistance and
 * MaxGuardDistance the creep keeps fighting until GuardReturnTime passes
 * without a hit, then walks home. */
TEST(wc3_order_lifecycle, creep_guard_soft_leash_times_out_through_scheduler) {
    creep_guard_setup_frame_world();
    edict_t *creep = scheduled_creep(-800, -100, 0);
    edict_t *enemy = review_order_unit(-70, 0);
    uint32_t combat_ms;
    int frame;
    for (frame = 0; frame < 4 && creep->movement.creep_guard_phase != CREEP_GUARD_COMBAT; frame++)
        creep_guard_run_frames(1);
    T_EQ(creep->movement.creep_guard_phase, CREEP_GUARD_COMBAT);
    T_ASSERT(creep_guard_attacking(creep) && creep->goalentity == enemy);
    combat_ms = level.time;
    for (frame = 0; frame < 80 && creep->movement.creep_guard_phase == CREEP_GUARD_COMBAT; frame++)
        creep_guard_run_frames(1);
    T_EQ(creep->movement.creep_guard_phase, CREEP_GUARD_RETURNING);
    T_ASSERT(level.time - combat_ms >= 5000u);
    T_ASSERT(creep->currentmove && creep->currentmove->proc == CAbilityMove);
    for (frame = 0; frame < 400 && creep->movement.creep_guard_phase == CREEP_GUARD_RETURNING; frame++)
        creep_guard_run_frames(1);
    T_EQ(creep->movement.creep_guard_phase, CREEP_GUARD_IDLE);
    T_ASSERT(creep_guard_home_distance(creep) <= move_settle_distance(creep));
}

#endif
