#ifdef BZ_TESTS
#include "../g_local.h"
#include "../skills/s_skills.h"
#include "shared/test.h"
extern void reset_entities(void),setup_test_world(void);
extern slkTestData_t *parse_slk_string(char const *);
extern void free_slk_rows(slkTestData_t *);

/* Actual49af00 publishes the selected weapon range for far point Ground,
 * and FLT_MAX for an already reachable point, before public admission returns.
 * Save before any think can manufacture or complete the physical owner. */
TEST(wc3_movement, target371_ground_point_admits_selected_or_in_range_physical_owner_before_tick) {
    /* UnitWeapons is deliberately sparse in the engine test archive. Supply
     * authored rows so cold load rebinds the same enabled/minimum-range data. */
    slkTestData_t *rows=parse_slk_string("ID;PWXL;N;E\nB;Y3;X3;D0\n"
        "C;Y1;X1;K\"unitWeaponID\"\nC;X2;K\"weapsOn\"\nC;X3;K\"minRange\"\n"
        "C;Y2;X1;K\"hfoo\"\nC;X2;K1\nC;X3;K0\n"
        "C;Y3;X1;K\"hmtm\"\nC;X2;K1\nC;X3;K250\nE\n");
    slkTestData_t *old=G_SetSLKRows("UnitWeapons",rows);
    static uint32_t const points[]={0x44c00000u,0x44080000u,0x44e80000u,0x44080000u,
        0x441e4000u,0x441e4045u,0x441e4046u,0x44468000u,0x4446801du,0x4446801eu};
    static uint32_t const ranges[]={0x40340000u,0x7cffffffu,0x420fc000u,0,
        0x7cffffffu,0x7cffffffu,0x40340000u,0,0,0x7cffffffu};
    FOR_LOOP(scene,10)FOR_LOOP(saved,2) {
        reset_entities();setup_test_world();
        static uint8_t cells[64*64];memset(cells,0,sizeof(cells));
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
        level.time=level.pathing_msec=level.pathing_phase=0;
        level.pathing_due=level.pathing_owner_clock_valid=level.timer_clock_valid=false;
        level.pathing_clock=(wc3Clock_t){0,0,300};
        ((mapInfo_t *)level.mapinfo)->players[0].used=true;
        ((mapInfo_t *)level.mapinfo)->players[0].playerType=kPlayerTypeHuman;
        game.clients[0].mapplayer=level.mapinfo->players;
        bool mortar=scene==2 || scene==3 || scene>=7;
        uint32_t code=mortar?MAKEFOURCC('h','m','t','m'):MAKEFOURCC('h','f','o','o');
        edict_t *unit=unit_create(0,code,&(vec2_t){512,1024},180);
        unit->health.value=unit->health.max_value=mortar?360:420;
        unit->collision=mortar?32:31;
        /* The sparse archive also omits Mortar's UnitData body row. Publish
         * the ordinary ground-unit lifecycle used by these native sources. */
        unit->svflags|=SVF_MONSTER;unit->movetype=MOVETYPE_STEP;
        unit->think=monster_think;unit->stand=unit_stand;unit->targtype=TARG_GROUND;
        S_SetUnitMoveSpeed(unit,270);
        unitAttack_t *weapon=S_AttackProfileWrite(unit,0);
        weapon->type=ATK_NORMAL;weapon->weapon=mortar?WPN_ARTILLERY:WPN_NORMAL;
        weapon->range=mortar?1150:90;weapon->targetsAllowed=WC3_TARGET_FLAG_GROUND;
        T_ASSERT(S_SpellIsAliveTarget(unit));T_ASSERT(S_UnitCanTranslate(unit));
        T_ASSERT(S_UnitAttackSlotEnabled(unit,0));
        T_NE(S_AttackProfileRead(unit,0)->type,ATK_NONE);
        T_FEQ(unit->data.UnitWeapons->minimumAttackRange,mortar?250:0,0);
        unit_stand(unit);gi.LinkEntity(unit);
        vec2_t point={wc3_float(points[scene]),1024};
        uint32_t range=ranges[scene];
        bool accepted=G_IssueUnitPointOrder(unit,"attackground",&point,false,0,0);
        if(!range) {
            /* Actual point validator rejects Mortar's minimum range before
             * task creation; it does not admit a retreating ground order. */
            T_ASSERT(!accepted);T_EQ(unit->current_order_id,0);
            T_NULL(move_unit_group(unit));T_NULL(unit->goalentity);
            T_FEQ(unit->s.origin.x,512,0);T_FEQ(unit->s.origin.y,1024,0);
            if(saved) {
                cstring_t path=Test_TempPath("wc3-target371-ground-rejected.bin");
                T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);
                T_EQ(unit->current_order_id,0);T_NULL(move_unit_group(unit));
            }
            continue;
        }
        T_ASSERT(accepted);
        T_EQ(unit->current_order_id,G_OrderId("attackground"));
        T_FEQ(unit->s.origin.x,512,0);T_FEQ(unit->s.origin.y,1024,0);
        T_NOT_NULL(unit->goalentity);
        if(unit->goalentity) {
            T_FEQ(unit->goalentity->s.origin2.x,point.x,0);
            T_FEQ(unit->goalentity->s.origin2.y,point.y,0);
        }
        moveGroup_t *group=move_unit_group(unit);T_NOT_NULL(group);
        if(group) {
            T_EQ(group->receiver,unit);T_ASSERT(group->owner_ability!=0);
            /* Ground is a separate flat engine command adapter; the saved
             * physical owner must agree with that installed command. */
            if(group->owner_ability)T_EQ(GetAbilityByIndex(group->owner_ability-1)->proc,unit->currentmove->proc);
            T_NOT_NULL(group->complete);T_NULL(group->target);
            T_EQ(wc3_float_bits(group->members[0].arrival_range),range);
            T_FEQ(group->goal.x,point.x/32,0);T_FEQ(group->goal.y,32,0);
        }
        if(saved) {
            cstring_t path=Test_TempPath("wc3-target371-ground-immediate.bin");
            T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);
            T_EQ(unit->current_order_id,G_OrderId("attackground"));
            group=move_unit_group(unit);T_NOT_NULL(group);
            if(group) {
                T_EQ(group->receiver,unit);T_NOT_NULL(group->complete);
                T_EQ(wc3_float_bits(group->members[0].arrival_range),range);
                T_FEQ(group->goal.x,point.x/32,0);T_FEQ(group->goal.y,32,0);
            }
        }
        T_ASSERT(unit_issueimmediateorder(unit,"stop"));
        T_EQ(unit->current_order_id,0);T_NULL(move_unit_group(unit));
    }
    reset_entities();setup_test_world();
    G_SetSLKRows("UnitWeapons",old);free_slk_rows(rows);
}

/* Native stock etrp has no physical Move request. An unreachable far point
 * completes the accepted command synchronously; the near point keeps its head. */
TEST(wc3_movement, target371_ground_immobile_near_holds_far_completes_during_admission) {
    slkTestData_t *rows=parse_slk_string("ID;PWXL;N;E\nB;Y2;X3;D0\n"
        "C;Y1;X1;K\"unitWeaponID\"\nC;X2;K\"weapsOn\"\nC;X3;K\"minRange\"\n"
        "C;Y2;X1;K\"etrp\"\nC;X2;K1\nC;X3;K0\nE\n");
    slkTestData_t *old=G_SetSLKRows("UnitWeapons",rows);
    FOR_LOOP(far,2)FOR_LOOP(saved,2) {
        reset_entities();setup_test_world();
        /* The default test world ends at1024 and would clamp native1056.
         * Match the captured post-placement geometry inside a full world. */
        static uint8_t cells[64*64];memset(cells,0,sizeof(cells));
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
        ((mapInfo_t *)level.mapinfo)->players[0].used=true;
        ((mapInfo_t *)level.mapinfo)->players[0].playerType=kPlayerTypeHuman;
        game.clients[0].mapplayer=level.mapinfo->players;
        edict_t *unit=unit_create(0,MAKEFOURCC('e','t','r','p'),&(vec2_t){544,1056},180);
        unit->health.value=unit->health.max_value=600;unit->collision=32;
        unit->svflags|=SVF_MONSTER;unit->aiflags|=AI_IMMOBILE;
        unit->movetype=MOVETYPE_NONE;unit->stand=unit_stand;unit->think=monster_think;
        unit->targtype=TARG_GROUND;unit_stand(unit);gi.LinkEntity(unit);
        unitAttack_t *weapon=S_AttackProfileWrite(unit,0);
        weapon->type=ATK_NORMAL;weapon->weapon=WPN_NORMAL;weapon->range=128;
        weapon->targetsAllowed=WC3_TARGET_FLAG_GROUND;
        T_ASSERT(S_SpellIsAliveTarget(unit));T_ASSERT(!S_UnitCanTranslate(unit));
        T_ASSERT(S_UnitAttackSlotEnabled(unit,0));
        T_FEQ(unit->s.origin2.x,544,0);T_FEQ(unit->s.origin2.y,1056,0);
        vec2_t point={far?1536:544,1024};
        T_ASSERT(G_IssueUnitPointOrder(unit,"attackground",&point,false,0,0));
        T_NULL(move_unit_group(unit));
        T_EQ(unit->current_order_id,far?0:G_OrderId("attackground"));
        T_FEQ(unit->s.origin2.x,544,0);T_FEQ(unit->s.origin2.y,1056,0);
        if(far)T_NULL(unit->goalentity);
        if(saved) {
            cstring_t file=Test_TempPath("target371-ground-immobile.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            T_NULL(move_unit_group(unit));
            T_EQ(unit->current_order_id,far?0:G_OrderId("attackground"));
            T_FEQ(unit->s.origin2.x,544,0);T_FEQ(unit->s.origin2.y,1056,0);
        }
        T_ASSERT(unit_issueimmediateorder(unit,"stop"));
        T_EQ(unit->current_order_id,0);T_NULL(move_unit_group(unit));
    }
    reset_entities();setup_test_world();G_SetSLKRows("UnitWeapons",old);free_slk_rows(rows);
}
static void ground371_failure_tick(edict_t *unit) {
    level.time+=30;wc3_clock_advance(&level.pathing_clock,.03f,0);level.pathing_counter++;
    if(unit->currentmove && unit->currentmove->think)unit->currentmove->think(unit);
    M_RunScheduledThinks();CM_ProcessPathJobs(8192);M_SamplePoses();
}
/* Human603110 flags20 retains Ground's public head. Computer flags22 runs
 * transport-null/AI immediate and publishes Attack-to-guard synchronously. */
TEST(wc3_movement,target371_ground_blocked_human_retains_head_computer_returns_to_guard_with_save) {
    slkTestData_t *rows=parse_slk_string("ID;PWXL;N;E\nB;Y2;X3;D0\n"
        "C;Y1;X1;K\"unitWeaponID\"\nC;X2;K\"weapsOn\"\nC;X3;K\"minRange\"\n"
        "C;Y2;X1;K\"hfoo\"\nC;X2;K1\nC;X3;K0\nE\n");
    slkTestData_t *old=G_SetSLKRows("UnitWeapons",rows);
    /* Stock hfoo has both intrinsic movement and attack. Author them in SLK
     * so cold restoration rebinds the same real ability ownership. */
    slkTestData_t *abilities=parse_slk_string("ID;PWXL;N;E\nB;Y2;X2;D0\n"
        "C;Y1;X1;K\"unitAbilID\"\nC;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"hfoo\"\nC;X2;K\"Amov,Aatk\"\nE\n");
    slkTestData_t *old_abilities=G_SetSLKRows("UnitAbilities",abilities);
    int controllers[2]={game.clients[0].jass.controller,game.clients[1].jass.controller};
    typeof(game.clients[0].mapplayer) players[2]={game.clients[0].mapplayer,game.clients[1].mapplayer};
    FOR_LOOP(computer,2)FOR_LOOP(saved,2) {
        G_BotStop(0);G_BotStop(1);reset_entities();setup_test_world();
        static uint8_t cells[64*64];memset(cells,0,sizeof(cells));
        for(unsigned y=30;y<=34;y++)for(unsigned x=29;x<=33;x++)cells[y*64+x]=2;
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
        FOR_LOOP(p,2) {
            ((mapInfo_t *)level.mapinfo)->players[p].used=true;
            ((mapInfo_t *)level.mapinfo)->players[p].playerType=p?kPlayerTypeComputer:kPlayerTypeHuman;
            game.clients[p].mapplayer=level.mapinfo->players+p;
            game.clients[p].jass.controller=p;game.clients[p].jass.removed=false;
        }
        G_BotInitPlayers();
        edict_t *unit=unit_create(computer,MAKEFOURCC('h','f','o','o'),&(vec2_t){272,304},90);
        unit->health.value=unit->health.max_value=420;unit->collision=31;
        S_SetUnitMoveSpeed(unit,270);unit->unitinfo.AcquireRange=0;
        unitAttack_t *weapon=S_AttackProfileWrite(unit,0);
        weapon->type=ATK_NORMAL;weapon->weapon=WPN_NORMAL;weapon->range=90;
        weapon->targetsAllowed=WC3_TARGET_FLAG_GROUND;gi.LinkEntity(unit);
        T_ASSERT(G_ActorHasAbilityCode(unit,MAKEFOURCC('A','m','o','v')));
        T_ASSERT(G_ActorHasAbilityCode(unit,MAKEFOURCC('A','a','t','k')));
        T_ASSERT(S_SpellIsAliveTarget(unit));T_ASSERT(S_UnitCanTranslate(unit));
        T_ASSERT(S_UnitAttackSlotEnabled(unit,0));
        T_EQ(!!(unit->aiflags&AI_TOWN_OWNED),computer);
        if(computer)T_NOT_NULL(G_BotAssignedGuard(unit));
        vec2_t goal={1008,1040};
        T_ASSERT(G_IssueUnitPointOrder(unit,"attackground",&goal,false,computer,0));
        T_EQ(unit->current_order_id,G_OrderId("attackground"));
        uint32_t number=unit-g_edicts;
        if(saved) {
            cstring_t file=Test_TempPath("ground371-before-failure.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);unit=g_edicts+number;
        }
        moveGroup_t *first=move_unit_group(unit);T_NOT_NULL(first);
        uint64_t sequence=first?first->sequence:0;bool finished=false;
        FOR_LOOP(frame,600) {
            ground371_failure_tick(unit);
            moveGroup_t *group=move_unit_group(unit);
            if(!group || group->sequence!=sequence) {finished=true;break;}
        }
        T_ASSERT(finished);
        uint32_t order=G_OrderId(computer?"attack":"attackground");
        vec2_t expected=computer?(vec2_t){272,304}:goal;
        T_EQ(unit->current_order_id,order);T_NOT_NULL(unit->goalentity);
        if(unit->goalentity) {
            T_FEQ(unit->goalentity->s.origin2.x,expected.x,0);
            T_FEQ(unit->goalentity->s.origin2.y,expected.y,0);
        }
        if(!computer)T_NULL(move_unit_group(unit));
        if(saved) {
            cstring_t file=Test_TempPath("ground371-after-failure.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);unit=g_edicts+number;
            T_EQ(unit->current_order_id,order);T_NOT_NULL(unit->goalentity);
            if(unit->goalentity) {
                T_FEQ(unit->goalentity->s.origin2.x,expected.x,0);
                T_FEQ(unit->goalentity->s.origin2.y,expected.y,0);
            }
        }
        if(!computer) {
            vec2_t stopped=unit->s.origin2;
            FOR_LOOP(frame,16)ground371_failure_tick(unit);
            T_EQ(unit->current_order_id,order);T_NULL(move_unit_group(unit));
            T_FEQ(unit->s.origin2.x,stopped.x,0);T_FEQ(unit->s.origin2.y,stopped.y,0);
        }
        T_ASSERT(unit_issueimmediateorder(unit,"stop"));T_EQ(unit->current_order_id,0);
    }
    G_BotStop(0);G_BotStop(1);reset_entities();setup_test_world();
    FOR_LOOP(p,2){game.clients[p].jass.controller=controllers[p];game.clients[p].mapplayer=players[p];}
    G_SetSLKRows("UnitAbilities",old_abilities);free_slk_rows(abilities);
    G_SetSLKRows("UnitWeapons",old);free_slk_rows(rows);
}
static edict_t *ground371_idle_source;
static struct {
    uint32_t count,ids[8],heads[8],pending[8];
    int32_t depth[8];
    bool physical[8];
} ground371_idle_events;
static void ground371_idle_event(cstring_t marker) {
    if(strncmp(marker,"AI371 ",6) || !ground371_idle_source)return;
    uint32_t at=ground371_idle_events.count++;
    if(at>=8)return;
    edict_t *unit=ground371_idle_source;
    ground371_idle_events.ids[at]=G_GetIssuedOrderId(unit);
    ground371_idle_events.heads[at]=unit->current_order_id;
    ground371_idle_events.depth[at]=unit->movement.repulse.disable_depth;
    ground371_idle_events.pending[at]=G_UnitQueuedOrderCount(unit);
    ground371_idle_events.physical[at]=move_unit_group(unit)!=NULL;
}

/* Native constructor resume851974 may synchronously relocate an idle source.
 * d0194 holds guard admission under198; both issued events occur at depth0,
 * before the guard's physical task, with a following851974 still pending. */
TEST(wc3_movement,target371_ground_idle_ai_immediate_frontier_and_saved_successor) {
    slkTestData_t *abilities=parse_slk_string("ID;PWXL;N;E\nB;Y2;X2;D0\n"
        "C;Y1;X1;K\"unitAbilID\"\nC;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"hfoo\"\nC;X2;K\"Amov,Aatk\"\nE\n");
    slkTestData_t *old_abilities=G_SetSLKRows("UnitAbilities",abilities);
    int controller=game.clients[1].jass.controller;
    typeof(game.clients[1].mapplayer) player=game.clients[1].mapplayer;
    FOR_LOOP(saved,2) {
        G_BotStop(1);reset_entities();setup_test_world();
        static uint8_t cells[64*64];memset(cells,0,sizeof(cells));
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
        ((mapInfo_t *)level.mapinfo)->players[1].used=true;
        ((mapInfo_t *)level.mapinfo)->players[1].playerType=kPlayerTypeComputer;
        game.clients[1].mapplayer=level.mapinfo->players+1;
        game.clients[1].jass.controller=1;game.clients[1].jass.removed=false;
        G_BotInitPlayers();
        edict_t *unit=unit_create(1,MAKEFOURCC('h','f','o','o'),&(vec2_t){272,304},90);
        unit->health.value=unit->health.max_value=420;unit->collision=31;
        S_SetUnitMoveSpeed(unit,270);unit->unitinfo.AcquireRange=0;
        T_ASSERT(S_SpellIsAliveTarget(unit));T_ASSERT(S_UnitCanTranslate(unit));
        T_ASSERT(G_ActorHasAbilityCode(unit,MAKEFOURCC('A','m','o','v')));
        T_ASSERT(G_ActorHasAbilityCode(unit,MAKEFOURCC('A','a','t','k')));
        botGuardPost_t *post=G_BotAssignedGuard(unit);T_NOT_NULL(post);
        if(post){T_FEQ(post->origin.x,272,0);T_FEQ(post->origin.y,304,0);}
        S_SetUnitPosition(unit,&(vec2_t){997,936});
        T_EQ(unit->current_order_id,0);T_NULL(move_unit_group(unit));
        T_FEQ(unit->s.origin2.x,997,0);T_FEQ(unit->s.origin2.y,936,0);
        uint32_t number=unit-g_edicts;
        if(saved) {
            cstring_t file=Test_TempPath("ground371-idle-ai-before.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);unit=g_edicts+number;
        }
        T_ASSERT(run_test_jass("function ai371_event takes nothing returns nothing\n"
            "call Preload(\"AI371 \"+I2S(GetIssuedOrderId()))\nendfunction\n"
            "function main takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call TriggerRegisterPlayerUnitEvent(t,Player(1),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)\n"
            "call TriggerRegisterPlayerUnitEvent(t,Player(1),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)\n"
            "call TriggerAddAction(t,function ai371_event)\nendfunction\n"));
        memset(&ground371_idle_events,0,sizeof(ground371_idle_events));
        ground371_idle_source=unit;test_preload_marker=ground371_idle_event;
        G_BotDispatchAIStop(unit);
        test_preload_marker=NULL;ground371_idle_source=NULL;
        T_EQ(ground371_idle_events.count,2);
        T_EQ(ground371_idle_events.ids[0],851974);T_EQ(ground371_idle_events.ids[1],851983);
        T_EQ(ground371_idle_events.heads[0],851974);T_EQ(ground371_idle_events.heads[1],851983);
        T_EQ(ground371_idle_events.depth[0],0);T_EQ(ground371_idle_events.depth[1],0);
        T_ASSERT(!ground371_idle_events.physical[0]);T_ASSERT(!ground371_idle_events.physical[1]);
        T_EQ(ground371_idle_events.pending[1],1);
        T_EQ(unit->movement.repulse.disable_depth,0);
        T_EQ(unit->current_order_id,G_OrderId("attack"));T_NOT_NULL(unit->goalentity);
        if(unit->goalentity){T_FEQ(unit->goalentity->s.origin2.x,272,0);T_FEQ(unit->goalentity->s.origin2.y,304,0);}
        T_EQ(G_UnitQueuedOrderCount(unit),1);
        if(unit->order_queue.count) {
            unitOrder_t const *pending=unit->order_queue.entries+unit->order_queue.head;
            T_EQ(pending->target_type,UNIT_ORDER_TARGET_NONE);T_EQ(pending->order_id,851974);
        }
        if(saved) {
            cstring_t file=Test_TempPath("ground371-idle-ai-after.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);unit=g_edicts+number;
            T_EQ(unit->current_order_id,G_OrderId("attack"));T_EQ(G_UnitQueuedOrderCount(unit),1);
            if(unit->order_queue.count)T_EQ(unit->order_queue.entries[unit->order_queue.head].order_id,851974);
        }
        T_ASSERT(unit_issueimmediateorder(unit,"stop"));T_EQ(unit->current_order_id,0);
    }
    test_preload_marker=NULL;ground371_idle_source=NULL;G_BotStop(1);reset_entities();setup_test_world();
    game.clients[1].jass.controller=controller;game.clients[1].mapplayer=player;
    G_SetSLKRows("UnitAbilities",old_abilities);free_slk_rows(abilities);
}
static struct {
    uint32_t count,ids[8],heads[8],pending[8];
    int32_t depth[8];
    bool physical[8],guard[8];
    vec2_t guard_origin[8];
} ground371_birth_events;

static void ground371_birth_event(cstring_t marker) {
    if(strncmp(marker,"AI371 BIRTH EVENT ",18))return;
    edict_t *source=NULL;
    /* CreateUnit has not returned its global handle during these callbacks. */
    FILTER_EDICTS(unit,unit->inuse && unit->s.player==1 && unit->class_id==MAKEFOURCC('h','f','o','o')) {
        source=unit;break;
    }
    uint32_t at=ground371_birth_events.count++;
    if(!source || at>=8)return;
    ground371_birth_events.ids[at]=G_GetIssuedOrderId(source);
    ground371_birth_events.heads[at]=source->current_order_id;
    ground371_birth_events.pending[at]=G_UnitQueuedOrderCount(source);
    ground371_birth_events.depth[at]=source->movement.repulse.disable_depth;
    ground371_birth_events.physical[at]=move_unit_group(source)!=NULL;
    botGuardPost_t *post=G_BotAssignedGuard(source);
    ground371_birth_events.guard[at]=post!=NULL;
    if(post)ground371_birth_events.guard_origin[at]=post->origin;
}

/* Native birth: Stop precedes initial guard enrollment, then append activates
 * AIImmediate. Its public callback relocates the newborn before guard return;
 * CreateUnit returns only after Guard Attack has replaced that immediate head. */
TEST(wc3_movement,target371_ground_public_birth_stop_enroll_ai_callback_frontier) {
    slkTestData_t *abilities=parse_slk_string("ID;PWXL;N;E\nB;Y2;X2;D0\n"
        "C;Y1;X1;K\"unitAbilID\"\nC;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"hfoo\"\nC;X2;K\"Amov,Aatk\"\nE\n");
    slkTestData_t *old_abilities=G_SetSLKRows("UnitAbilities",abilities);
    int controller=game.clients[1].jass.controller;
    bool removed=game.clients[1].jass.removed;
    typeof(game.clients[1].mapplayer) player=game.clients[1].mapplayer;
    bool used=level.mapinfo->players[1].used;
    typeof(level.mapinfo->players[1].playerType) type=level.mapinfo->players[1].playerType;
    G_BotStop(1);reset_entities();setup_test_world();
    static uint8_t cells[64*64];memset(cells,0,sizeof(cells));
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
    ((mapInfo_t *)level.mapinfo)->players[1].used=true;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType=kPlayerTypeComputer;
    game.clients[1].mapplayer=level.mapinfo->players+1;
    game.clients[1].jass.controller=1;game.clients[1].jass.removed=false;
    G_BotInitPlayers();
    memset(&ground371_birth_events,0,sizeof(ground371_birth_events));
    test_preload_marker=ground371_birth_event;
    bool ran=run_test_jass("globals\nunit birth371=null\nboolean moved371=false\nendglobals\n"
        "function birth371_event takes nothing returns nothing\nlocal unit u=GetTriggerUnit()\n"
        "call Preload(\"AI371 BIRTH EVENT \"+I2S(GetIssuedOrderId()))\n"
        "if GetIssuedOrderId()==851974 and not moved371 then\nset moved371=true\n"
        "call SetUnitX(u,997.0)\ncall SetUnitY(u,936.0)\nendif\nendfunction\n"
        "function main takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "call TriggerRegisterPlayerUnitEvent(t,Player(1),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)\n"
        "call TriggerRegisterPlayerUnitEvent(t,Player(1),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)\n"
        "call TriggerAddAction(t,function birth371_event)\n"
        "set birth371=CreateUnit(Player(1),'hfoo',272.0,304.0,90.0)\nendfunction\n");
    test_preload_marker=NULL;T_ASSERT(ran);
    edict_t *source=NULL;
    FILTER_EDICTS(unit,unit->inuse && unit->s.player==1 && unit->class_id==MAKEFOURCC('h','f','o','o')) {
        source=unit;break;
    }
    T_NOT_NULL(source);
    if(source) {
        T_ASSERT(S_SpellIsAliveTarget(source));T_ASSERT(S_UnitCanTranslate(source));
        T_ASSERT(G_ActorHasAbilityCode(source,MAKEFOURCC('A','m','o','v')));
        T_ASSERT(G_ActorHasAbilityCode(source,MAKEFOURCC('A','a','t','k')));
        T_EQ(ground371_birth_events.count,3);
        uint32_t const expected[]={851972,851974,851983};
        FOR_LOOP(i,3) {
            T_EQ(ground371_birth_events.ids[i],expected[i]);
            T_EQ(ground371_birth_events.heads[i],expected[i]);
            T_EQ(ground371_birth_events.depth[i],0);
            T_ASSERT(!ground371_birth_events.physical[i]);
            T_EQ(ground371_birth_events.guard[i],i!=0);
            if(i) {
                T_FEQ(ground371_birth_events.guard_origin[i].x,272,0);
                T_FEQ(ground371_birth_events.guard_origin[i].y,304,0);
            }
        }
        T_EQ(ground371_birth_events.pending[2],1);
        T_FEQ(source->s.origin2.x,997,0);T_FEQ(source->s.origin2.y,936,0);
        T_EQ(source->current_order_id,G_OrderId("attack"));
        T_EQ(source->movement.repulse.disable_depth,0);
        T_NOT_NULL(move_unit_group(source));T_NOT_NULL(source->goalentity);
        if(source->goalentity){T_FEQ(source->goalentity->s.origin2.x,272,0);T_FEQ(source->goalentity->s.origin2.y,304,0);}
        T_EQ(G_UnitQueuedOrderCount(source),1);
        if(source->order_queue.count) {
            unitOrder_t const *next=source->order_queue.entries+source->order_queue.head;
            T_EQ(next->target_type,UNIT_ORDER_TARGET_NONE);T_EQ(next->order_id,851974);
        }
    }
    G_BotStop(1);reset_entities();setup_test_world();
    game.clients[1].jass.controller=controller;game.clients[1].jass.removed=removed;game.clients[1].mapplayer=player;
    ((mapInfo_t *)level.mapinfo)->players[1].used=used;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType=type;
    G_BotInitPlayers();
    G_SetSLKRows("UnitAbilities",old_abilities);free_slk_rows(abilities);
}
/*693490 permits at most500 successors beside an executing user head. The
 * internal AI append shares that admission rule rather than using the extra
 * physical storage slot reserved for a suspended head. */
TEST(wc3_movement,target371_ground_private_ai_append_obeys_user_queue_bound_and_save) {
    slkTestData_t *abilities=parse_slk_string("ID;PWXL;N;E\nB;Y2;X2;D0\n"
        "C;Y1;X1;K\"unitAbilID\"\nC;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"hfoo\"\nC;X2;K\"Amov,Aatk\"\nE\n");
    slkTestData_t *old_abilities=G_SetSLKRows("UnitAbilities",abilities);
    FOR_LOOP(full,2) {
        reset_entities();setup_test_world();
        static uint8_t cells[64*64];memset(cells,0,sizeof(cells));
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
        edict_t *unit=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){272,304},90);
        unit->health.value=unit->health.max_value=420;unit->unitinfo.AcquireRange=0;
        S_SetUnitMoveSpeed(unit,270);
        vec2_t point={1800,1800};
        T_ASSERT(unit_issueorder(unit,"move",&point));T_ASSERT(G_UnitHasActiveOrder(unit));
        T_EQ(unit->current_order_id,G_OrderId("move"));
        uint32_t seed=MAX_UNIT_ORDER_QUEUE-1+full,accepted=0;
        FOR_LOOP(i,seed)if(G_QueueUnitOrder(unit,"move",UNIT_ORDER_TARGET_POINT,&point,NULL,0,0,0))accepted++;
        T_EQ(accepted,seed);T_EQ(G_UnitQueuedOrderCount(unit),seed);
        G_BotAppendAIStop(unit);
        T_EQ(G_UnitQueuedOrderCount(unit),MAX_UNIT_ORDER_QUEUE);
        T_EQ(unit->current_order_id,G_OrderId("move"));
        unitOrder_t const *tail=unit->order_queue.entries+
            (unit->order_queue.head+unit->order_queue.count-1)%unit->order_queue.capacity;
        T_EQ(tail->order_id,full ? G_OrderId("move") : 851974);
        T_EQ(tail->target_type,full ? UNIT_ORDER_TARGET_POINT : UNIT_ORDER_TARGET_NONE);
        if(!full)T_ASSERT(G_IsAIImmediateOrder(tail));
        T_ASSERT(!G_QueueUnitOrder(unit,"move",UNIT_ORDER_TARGET_POINT,&point,NULL,0,0,0));
        G_BotAppendAIStop(unit);T_EQ(G_UnitQueuedOrderCount(unit),MAX_UNIT_ORDER_QUEUE);
        uint32_t number=unit-g_edicts;
        cstring_t file=Test_TempPath("ground371-ai-queue-capacity.bin");
        T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);unit=g_edicts+number;
        T_EQ(unit->current_order_id,G_OrderId("move"));
        T_EQ(G_UnitQueuedOrderCount(unit),MAX_UNIT_ORDER_QUEUE);
        tail=unit->order_queue.entries+
            (unit->order_queue.head+unit->order_queue.count-1)%unit->order_queue.capacity;
        T_EQ(tail->order_id,full ? G_OrderId("move") : 851974);
        if(!full)T_ASSERT(G_IsAIImmediateOrder(tail));
    }
    reset_entities();setup_test_world();
    G_SetSLKRows("UnitAbilities",old_abilities);free_slk_rows(abilities);
}
#endif
