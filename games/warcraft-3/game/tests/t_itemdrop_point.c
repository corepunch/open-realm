#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"
#include "jass/jass.h"
#include "games/warcraft-3/common/wc3_pathing_arrival.h"
#include "fixtures/retail_itemdrop_motion.h"
extern void reset_entities(void),setup_test_world(void);
bool run_test_jass(cstring_t);

/* Retail public UnitDropItemPoint (native2188b0) admits Inventory order852001.
 * Producer54efe0 prepends an ordinary d016b point task at the requested point
 * with authored Misc.DropItemRange, then action d017c. Both Move arrival and
 * can't-path run56a670, which drops at the exact point only when05b440 (the
 * predicted source plus source collision) is in range. Scenes and final words
 * follow tools/ghidra/fixtures/research/itemdrop captures (Hpal at704,1056). */
typedef enum {
    DROP_INSIDE, DROP_EQUAL, DROP_OUTSIDE, DROP_FAR, DROP_OCCUPIED, DROP_ENCLOSED, DROP_STOP, DROP_ZERO, DROP_UNDROPPABLE_ZERO, DROP_UNDROPPABLE_FAR, DROP_REMOVE, DROP_UNEQUIP, DROP_BUILDING_NEAR, DROP_BUILDING_FAR, DROP_SCENES
} itemDropScene_t;
typedef struct {
    itemDropScene_t scene;
    float range;
    vec2_t point;
} itemDropParams_t;
typedef struct {
    mapInfo_t info;
    mapInfo_t const *old_info;
    stbIniCache_t old_misc;
    uint8_t cells[64*64];
    edict_t *actor,*item;
} itemDropFixture_t;

/* Public JASS actors use the captured centers; tests never load retail assets. */
static void ItemDropBegin(itemDropFixture_t *f,itemDropParams_t cfg) {
    itemDropScene_t scene=cfg.scene;float range=cfg.range;vec2_t point=cfg.point;
    bool building=scene==DROP_BUILDING_NEAR || scene==DROP_BUILDING_FAR;
    reset_entities();setup_test_world();memset(f,0,sizeof(*f));
    f->old_info=level.mapinfo;f->old_misc=game.config.misc;game.config.misc=(stbIniCache_t){0};
    char misc[64];snprintf(misc,sizeof(misc),"[Misc]\nDropItemRange=%g\n",range);
    T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.misc,misc));
    f->info.players[0].used=true;f->info.players[0].playerType=kPlayerTypeHuman;
    level.mapinfo=&f->info;game.clients[0].mapplayer=f->info.players;
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,f->cells);
    level.time=level.pathing_msec=level.pathing_phase=level.framenum=0;
    level.started=level.scriptsConfigured=level.scriptsStarted=false;
    level.pathing_due=level.pathing_owner_clock_valid=level.timer_clock_valid=false;
    level.pathing_clock=(wc3Clock_t){0,0,300};
    /* Retail CreateUnit snapped the probe Farms to these footprint centers. */
    /* Supply those captured centers; CreateUnit placement is a separate producer contract. */
    static char const *farms[DROP_SCENES]={
        [DROP_OCCUPIED]="call CreateUnit(Player(0),'hhou',1216.0,1024.0,270.0)\n",
        [DROP_ENCLOSED]="call CreateUnit(Player(0),'hhou',1344.0,896.0,270.0)\n"
            "call CreateUnit(Player(0),'hhou',1472.0,896.0,270.0)\ncall CreateUnit(Player(0),'hhou',1600.0,896.0,270.0)\n"
            "call CreateUnit(Player(0),'hhou',1344.0,1024.0,270.0)\ncall CreateUnit(Player(0),'hhou',1600.0,1024.0,270.0)\n"
            "call CreateUnit(Player(0),'hhou',1344.0,1152.0,270.0)\ncall CreateUnit(Player(0),'hhou',1472.0,1152.0,270.0)\n"
            "call CreateUnit(Player(0),'hhou',1600.0,1152.0,270.0)\n",
    };
    char script[8192];
    cstring_t program=
        "globals\nunit dropActor\nitem dropItem\ninteger dropEvents=0\nendglobals\n"
        "function dropIssue takes nothing returns nothing\n"
        "call BJassAssert(UnitDropItemPoint(dropActor,dropItem,%.2f,%.2f),\"public point drop\")\nendfunction\n"
        "function dropLock takes nothing returns nothing\ncall SetItemDroppable(dropItem,false)\nendfunction\n"
        "function dropReject takes nothing returns nothing\n"
        "call BJassAssert(not UnitDropItemPoint(dropActor,dropItem,%.2f,%.2f),\"undroppable rejects\")\nendfunction\n"
        "function dropStop takes nothing returns nothing\n"
        "call BJassAssert(IssueImmediateOrder(dropActor,\"stop\"),\"public Stop\")\nendfunction\n"
        "function dropRemove takes nothing returns nothing\ncall RemoveItem(dropItem)\nset dropItem=null\nendfunction\n"
        "function dropUnequip takes nothing returns nothing\ncall UnitRemoveItem(dropActor,dropItem)\nendfunction\n"
        "function dropNew takes nothing returns nothing\nset dropItem=CreateItem('ratf',704.0,1056.0)\nendfunction\n"
        "function dropKill takes nothing returns nothing\ncall KillUnit(dropActor)\nendfunction\n"
        "function dropPause takes nothing returns nothing\ncall PauseUnit(dropActor,true)\nendfunction\n"
        "function dropUnpause takes nothing returns nothing\ncall PauseUnit(dropActor,false)\nendfunction\n"
        "function dropPickup takes nothing returns nothing\n"
        "call BJassAssert(UnitAddItem(dropActor,dropItem),\"replacement pickup\")\nendfunction\n"
        "function dropEvent takes nothing returns nothing\nset dropEvents=dropEvents+1\nendfunction\n"
        "function dropListen takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)\n"
        "call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)\n"
        "call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)\n"
        "call TriggerAddAction(t,function dropEvent)\nendfunction\n"
        "function dropNoEvents takes nothing returns nothing\n"
        "call BJassAssert(dropEvents==0,\"specialized item packet emits no issued event\")\nendfunction\n"
        "function dropEventControls takes nothing returns nothing\n"
        "local unit friend=CreateUnit(Player(0),'hfoo',1500.0,1056.0,0.0)\nset dropEvents=0\n"
        "call BJassAssert(IssuePointOrder(dropActor,\"move\",512.0,1056.0),\"point control accepted\")\n"
        "call BJassAssert(dropEvents==1,\"point control notifies\")\nset dropEvents=0\n"
        "call BJassAssert(IssueTargetOrder(dropActor,\"move\",friend),\"target control accepted\")\n"
        "call BJassAssert(dropEvents==1,\"target control notifies\")\nset dropEvents=0\n"
        "call BJassAssert(IssueImmediateOrder(dropActor,\"stop\"),\"immediate control accepted\")\n"
        "call BJassAssert(dropEvents==1,\"immediate control notifies\")\n"
        "call RemoveUnit(friend)\nendfunction\n"
        "function main takes nothing returns nothing\n%s"
        "set dropActor=CreateUnit(Player(0),'%s',704.0,%.2f,0.0)\n%s"
        "call SetUnitAcquireRange(dropActor,0.0)\ncall SetUnitMoveSpeed(dropActor,270.0)\n"
        "set dropItem=CreateItem('ratf',704.0,1056.0)\n"
        "call BJassAssert(UnitAddItem(dropActor,dropItem),\"public pickup\")\n"
        "call BJassAssert(UnitItemInSlot(dropActor,0)==dropItem,\"carried identity\")\nendfunction\n";
    cstring_t extra=farms[scene] ? farms[scene] : "",add=building ? "call UnitAddAbility(dropActor,'AInv')\n" : "";
    snprintf(script,sizeof(script),program,point.x,point.y,point.x,point.y,extra,building ? "hhou" : "Hpal",building ? 1024.0 : 1056.0,add);
    T_ASSERT(run_test_jass(script));
    FOR_LOOP(i,globals.num_edicts) {
        edict_t *e=g_edicts+i;
        if(!e->inuse)continue;
        if(e->class_id==(building ? MAKEFOURCC('h','h','o','u') : MAKEFOURCC('H','p','a','l')))f->actor=e;
        else if(G_IsItem(e) && e->class_id==MAKEFOURCC('r','a','t','f'))f->item=e;
    }
    T_NOT_NULL(f->actor);T_NOT_NULL(f->item);
    if(!f->actor || !f->item)return;
    /* Retail UnitBalance/UnitData Hpal collision32; the shared fixture row omits it. */
    if(!building)f->actor->collision=32;
    gi.LinkEntity(f->actor);
    T_ASSERT(G_ActorHasAbilityCode(f->actor,MAKEFOURCC('A','I','n','v')));
    T_EQ(S_UnitCanTranslate(f->actor),!building);
    if(building) {
        T_ASSERT(f->actor->aiflags&AI_IMMOBILE);
        T_FEQ(f->actor->collision,72,0);
        T_NOT_NULL(f->actor->pathtex);
    } else T_FEQ(S_UnitMoveSpeed(f->actor),270,0);
    T_EQ(f->item->item->carrier,f->actor);T_ASSERT(!f->item->item->in_world);
    G_FinishMovePathingInitialization();level.started=level.scriptsConfigured=level.scriptsStarted=true;
}

/* Restore shared fixture state so later suites do not inherit the probe world. */
static void ItemDropEnd(itemDropFixture_t *f) {
    level.started=false;reset_entities();setup_test_world();
    level.mapinfo=f->old_info;game.clients[0].mapplayer=f->old_info->players;
    Stb_IniCacheFree(&game.config.misc);game.config.misc=f->old_misc;
}

static void ItemDropCall(cstring_t name) {jass_callbyname(level.vm,name,false);T_ASSERT(!jass_rterror_pending(level.vm));}
static void ItemDropStep(void) {level.time+=FRAMETIME;globals.RunFrame();}
/* Save with the physical callback live and resolve both restored edict addresses. */
static void ItemDropRestore(itemDropFixture_t *f,cstring_t name) {
    uint32_t actor=f->actor-g_edicts,item=f->item-g_edicts;
    cstring_t path=Test_TempPath(name);T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);
    f->actor=g_edicts+actor;f->item=g_edicts+item;
}

/* The accepted public order owns a synchronous Inventory point task. */
static void ItemDropAdmitted(itemDropFixture_t *f,float range,vec2_t point) {
    T_EQ(f->actor->current_order_id,G_OrderId("dropitem"));
    T_EQ(f->item->item->carrier,f->actor);T_ASSERT(!f->item->item->in_world);
    T_EQ(f->actor->item_drop,f->item);
    T_EQ(f->actor->item_drop_spawn_time,f->item->spawn_time);
    moveGroup_t const *group=move_unit_group(f->actor);T_NOT_NULL(group);
    if(!group)return;
    T_EQ(group->receiver,f->actor);T_EQ(group->owner_ability,GetAbilityIndex(CAbilityInventory)+1);
    T_NOT_NULL(group->complete);T_NULL(group->target);
    T_EQ(wc3_float_bits(group->members[0].arrival_range),wc3_float_bits(wc3_point_arrival_range(range)));
    T_FEQ(group->goal.x,wc3_div(point.x,32),0);T_FEQ(group->goal.y,wc3_div(point.y,32),0);
    T_NOT_NULL(f->actor->goalentity);
    if(f->actor->goalentity){T_FEQ(f->actor->goalentity->s.origin2.x,point.x,0);T_FEQ(f->actor->goalentity->s.origin2.y,point.y,0);}
}

TEST(wc3_items, itemdrop_public_stop_retains_carried_item_through_save) {
    FOR_LOOP(setting,2)FOR_LOOP(saved,2) {
        float range=setting ? 64 : 100;
        itemDropFixture_t f;
        vec2_t point={1004+range,1056};
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_STOP,.range=range,.point=point});
        ItemDropCall("dropIssue");
        ItemDropAdmitted(&f,range,point);
        FOR_LOOP(i,4)ItemDropStep();
        T_EQ(f.actor->current_order_id,G_OrderId("dropitem"));
        T_ASSERT(f.actor->s.origin2.x>704);
        if(saved)ItemDropRestore(&f,"wc3-itemdrop-stop.bin");
        ItemDropCall("dropStop");
        FOR_LOOP(i,20)ItemDropStep();
        T_EQ(f.actor->current_order_id,0);
        T_NULL(move_unit_group(f.actor));
        T_NULL(f.actor->item_drop);
        T_EQ(f.actor->inventory[0],f.item);
        T_EQ(f.item->item->carrier,f.actor);
        T_ASSERT(!f.item->item->in_world);
        ItemDropEnd(&f);
    }
}

/* These legacy cases use the public native and actual frame scheduler.
 * Calling the behavior think directly does not execute its physical owner. */
TEST(wc3_items, point_drop_waits_for_simulation_tick) {
    itemDropFixture_t f;
    vec2_t point={803.75f,1056};
    ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_INSIDE,.range=100,.point=point});
    ItemDropCall("dropIssue");
    ItemDropAdmitted(&f,100,point);
    ItemDropStep();
    T_NULL(f.actor->inventory[0]);
    T_ASSERT(f.item->item->in_world);
    T_FEQ(f.item->s.origin2.x,point.x,0);
    T_FEQ(f.item->s.origin2.y,point.y,0);
    T_NULL(f.actor->item_drop);
    T_NULL(f.actor->goalentity);
    T_EQ(f.actor->current_order_id,0);
    ItemDropEnd(&f);
}

TEST(wc3_items, distant_point_drop_moves_before_releasing_item) {
    itemDropFixture_t f;
    vec2_t point={1104,1056};
    ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=point});
    ItemDropCall("dropIssue");
    ItemDropStep();
    T_EQ(f.actor->inventory[0],f.item);
    T_ASSERT(!f.item->item->in_world);
    T_ASSERT(f.actor->s.origin2.x>704);
    T_EQ(f.actor->item_drop,f.item);
    T_NOT_NULL(f.actor->goalentity);
    T_NOT_NULL(move_unit_group(f.actor));
    ItemDropEnd(&f);
}

TEST(wc3_items, point_drop_revalidates_carried_item) {
    itemDropFixture_t f;
    vec2_t point={1104,1056};
    ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=point});
    ItemDropCall("dropIssue");
    ItemDropStep();
    ItemDropCall("dropUnequip");
    vec2_t detached=f.item->s.origin2;
    T_NULL(f.actor->inventory[0]);
    T_EQ(f.actor->current_order_id,G_OrderId("dropitem"));
    T_NOT_NULL(move_unit_group(f.actor));
    FOR_LOOP(frame,100)ItemDropStep();
    T_NULL(f.actor->item_drop);
    T_NULL(f.actor->goalentity);
    T_NULL(move_unit_group(f.actor));
    T_EQ(f.actor->current_order_id,0);
    T_STREQ(f.actor->currentmove->animation,"stand");
    T_NULL(f.actor->inventory[0]);
    T_NULL(f.item->item->carrier);
    T_ASSERT(f.item->item->in_world);
    T_FEQ(f.item->s.origin2.x,detached.x,0);
    T_FEQ(f.item->s.origin2.y,detached.y,0);
    T_ASSERT(f.actor->s.origin2.x>1000);
    ItemDropEnd(&f);
}

/* Scenes8/9 reject before publishing a head or a physical point task. */
TEST(wc3_items, itemdrop_undroppable_zero_and_far_reject_without_movement) {
    FOR_LOOP(setting,2)FOR_LOOP(far,2) {
        itemDropFixture_t f;
        float range=setting ? 64 : 100;
        vec2_t point={far ? 1004+range : 704,1056};
        ItemDropBegin(&f,(itemDropParams_t){.scene=far ? DROP_UNDROPPABLE_FAR : DROP_UNDROPPABLE_ZERO,.range=range,.point=point});
        ItemDropCall("dropLock");
        ItemDropCall("dropReject");
        FOR_LOOP(frame,50)ItemDropStep();
        T_EQ(f.actor->current_order_id,0);
        T_NULL(move_unit_group(f.actor));
        T_NULL(f.actor->item_drop);
        T_EQ(f.actor->inventory[0],f.item);
        T_EQ(f.item->item->carrier,f.actor);
        T_ASSERT(!f.item->item->in_world);
        T_FEQ(f.actor->s.origin2.x,704,0);
        T_FEQ(f.actor->s.origin2.y,1056,0);
        ItemDropEnd(&f);
    }
}

/* Retail obstacle scenes require live authored Farm footprints, including
 * after restoration. A visual building alone does not block Move pathing. */
TEST(wc3_items, itemdrop_farm_fixture_retains_authored_obstacles_through_save) {
    FOR_LOOP(scene,2)FOR_LOOP(saved,2) {
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=scene ? DROP_ENCLOSED : DROP_OCCUPIED,.range=100,.point=(vec2_t){1216,1024}});
        if(saved)ItemDropRestore(&f,"wc3-itemdrop-farms.bin");
        unsigned farms=0;
        FOR_LOOP(i,globals.num_edicts) {
            edict_t *farm=g_edicts+i;
            if(!farm->inuse || farm->class_id!=MAKEFOURCC('h','h','o','u'))continue;
            farms++;
            T_ASSERT(!M_IsDead(farm));
            T_ASSERT(farm->targtype&TARG_STRUCTURE);
            T_ASSERT(farm->s.flags&EF_BUILDING);
            T_ASSERT(farm->aiflags&AI_IMMOBILE);
            T_FEQ(farm->collision,72,0);
            T_NOT_NULL(farm->pathtex);
            pathAccelParams_t blocked={.from=&farm->s.origin2,.radius=f.actor->collision,
                .blocked_flags=M_UnitStaticPathingFlags(f.actor)};
            T_ASSERT(!G_MovePathPointIsPathable(&blocked));
            if(farm->pathtex) {
                T_EQ(farm->pathtex->width,4);
                T_EQ(farm->pathtex->height,4);
            }
        }
        T_EQ(farms,scene ? 8 : 1);
        ItemDropEnd(&f);
    }
}


/* Replay original owner clocks and compare retained mover words. This
 * isolates timing from the public frame fixture's zero-clock approximation. */
TEST(wc3_items, itemdrop_original_clock_mover_writes_match_retail) {
    FOR_LOOP(c,sizeof(itemdrop_motion_cases)/sizeof(*itemdrop_motion_cases))FOR_LOOP(saved,2) {
        itemDropMotionCase_t const *sample=itemdrop_motion_cases+c;
        itemDropFixture_t f;
        vec2_t point={wc3_float(sample->point[0]),wc3_float(sample->point[1])};
        ItemDropBegin(&f,(itemDropParams_t){.scene=sample->scene,.range=sample->setting ? 64 : 100,.point=point});
        level.pathing_clock=(wc3Clock_t){wc3_float(sample->clock[0]),sample->clock[1],wc3_float(sample->clock[2])};
        level.pathing_counter=sample->counter;
        f.actor->movement.pose_clock=level.pathing_clock;
        ItemDropCall("dropIssue");
        ItemDropAdmitted(&f,sample->setting ? 64 : 100,point);
        if(saved)ItemDropRestore(&f,"wc3-itemdrop-original-clock.bin");
        bool mismatch=false;
        FOR_LOOP(i,sample->count) {
            itemDropMotionRow_t const *row=itemdrop_motion_rows+sample->first+i;
            level.pathing_clock=(wc3Clock_t){wc3_float(row->time),row->epoch,wc3_float(row->span)};
            level.pathing_counter=row->counter-1;
            level.scheduled_think=true;
            S_BeginAbilityOwnerUpdates();
            S_RunAbilityOwnerUpdates();
            level.scheduled_think=false;
            uint32_t actual[]={wc3_float_bits(f.actor->movement.pose_clock.time),f.actor->movement.pose_clock.epoch,
                wc3_float_bits(f.actor->movement.fine_pose.x),wc3_float_bits(f.actor->movement.fine_pose.y),
                wc3_float_bits(wc3_mul(f.actor->movement.velocity.x,1/32.f)),
                wc3_float_bits(wc3_mul(f.actor->movement.velocity.y,1/32.f)),
                wc3_float_bits(wc3_div(S_UnitMoveSpeed(f.actor),32)),wc3_float_bits(f.actor->s.angle)};
            FOR_LOOP(k,8) {
                T_EQ(actual[k],row->pose[k]);
                if(actual[k]!=row->pose[k]) {
                    fprintf(stderr,"itemdrop setting=%u scene=%u saved=%u row=%u field=%u actual=%08x retail=%08x\n",sample->setting,sample->scene,saved,i,k,actual[k],row->pose[k]);

                    mismatch=true;
                }
            }
            if(mismatch)break;
            if(saved && i==sample->count/2)ItemDropRestore(&f,"wc3-itemdrop-original-clock-walking.bin");
        }
        T_ASSERT(!mismatch);
        T_EQ(f.actor->current_order_id,0);
        T_NULL(move_unit_group(f.actor));
        T_NULL(f.actor->item_drop);
        T_EQ(f.item->item->in_world,sample->dropped);
        T_EQ(f.item->item->carrier,sample->dropped ? NULL : f.actor);
        T_EQ(f.actor->inventory[0],sample->dropped ? NULL : f.item);
        if(sample->dropped) {
            T_EQ(wc3_float_bits(f.item->s.origin2.x),sample->item[0]);
            T_EQ(wc3_float_bits(f.item->s.origin2.y),sample->item[1]);
        }
        ItemDropEnd(&f);
    }
}

/* Native V4 adds AInv to a Farm before the successful scripted pickup.
 * Runtime attachment must feed capacity/permissions just like authored rows. */
TEST(wc3_items, itemdrop_runtime_added_inventory_exposes_capacity_and_permissions) {
    reset_entities();setup_test_world();level.started=false;
    cstring_t script=
        "globals\nunit addedCarrier\nendglobals\n"
        "function main takes nothing returns nothing\n"
        "set addedCarrier=CreateUnit(Player(0),'hhou',704.0,1024.0,0.0)\n"
        "call BJassAssert(UnitAddAbility(addedCarrier,'AInv'),\"inventory factory accepted\")\n"
        "call BJassAssert(GetUnitAbilityLevel(addedCarrier,'AInv')==1,\"runtime attachment exists\")\n"
        "call BJassAssert(UnitInventorySize(addedCarrier)==6,\"runtime inventory capacity\")\nendfunction\n";
    T_ASSERT(run_test_jass(script));
    edict_t *carrier=NULL;
    FOR_LOOP(i,globals.num_edicts) {
        edict_t *unit=g_edicts+i;
        if(unit->inuse && unit->class_id==MAKEFOURCC('h','h','o','u'))carrier=unit;
    }
    T_NOT_NULL(carrier);
    if(carrier) {
        T_ASSERT(G_ActorHasAbilityCode(carrier,MAKEFOURCC('A','I','n','v')));
        T_EQ(G_InventoryCapacity(carrier),6);
        T_ASSERT(G_InventoryCanUseItems(carrier));
        T_ASSERT(G_InventoryCanGetItems(carrier));
        T_ASSERT(G_InventoryCanDropItems(carrier));
    }
    reset_entities();setup_test_world();
}

/* The public validator rejects a dead Hero while its inventory remains owned. */
TEST(wc3_items, itemdrop_dead_source_rejects_without_releasing_retained_inventory) {
    itemDropFixture_t f;
    ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=(vec2_t){1104,1056}});
    ItemDropCall("dropKill");
    T_ASSERT(M_IsDead(f.actor));
    T_EQ(f.actor->inventory[0],f.item);
    ItemDropCall("dropReject");
    T_EQ(f.actor->current_order_id,0);
    T_NULL(move_unit_group(f.actor));
    T_NULL(f.actor->item_drop);
    T_EQ(f.actor->inventory[0],f.item);
    T_EQ(f.item->item->carrier,f.actor);
    T_ASSERT(!f.item->item->in_world);
    ItemDropEnd(&f);
}

/* Original V4 scenes6/7 accept an immobile carrier and finish synchronously.
 * Only the near point passes the terminal range predicate. */
TEST(wc3_items, itemdrop_immobile_source_completes_synchronously_without_move_owner) {
    FOR_LOOP(far,2)FOR_LOOP(saved,3) {
        itemDropFixture_t f;
        vec2_t point=far ? (vec2_t){1104,1056} : (vec2_t){803.75f,1024};
        ItemDropBegin(&f,(itemDropParams_t){.scene=far ? DROP_BUILDING_FAR : DROP_BUILDING_NEAR,.range=100,.point=point});
        T_FEQ(f.actor->s.origin2.x,704,0);
        T_FEQ(f.actor->s.origin2.y,1024,0);
        if(saved==1)ItemDropRestore(&f,"wc3-itemdrop-immobile-before.bin");
        ItemDropCall("dropIssue");
        if(saved==2)ItemDropRestore(&f,"wc3-itemdrop-immobile-after.bin");
        T_EQ(f.actor->current_order_id,0);
        T_NULL(move_unit_group(f.actor));
        T_NULL(f.actor->item_drop);
        T_NULL(f.actor->goalentity);
        T_EQ(f.item->item->in_world,!far);
        T_EQ(f.item->item->carrier,far ? f.actor : NULL);
        T_EQ(f.actor->inventory[0],far ? f.item : NULL);
        if(!far) {
            T_FEQ(f.item->s.origin2.x,point.x,0);
            T_FEQ(f.item->s.origin2.y,point.y,0);
        }
        FOR_LOOP(frame,20)ItemDropStep();
        T_EQ(f.actor->current_order_id,0);
        T_NULL(move_unit_group(f.actor));
        T_FEQ(f.actor->s.origin2.x,704,0);
        T_FEQ(f.actor->s.origin2.y,1024,0);
        ItemDropEnd(&f);
    }
}

/* Original ort2 activation skips the ordinary order subtype notifications.
 * Positive controls establish that all three trigger registrations work. */
TEST(wc3_items, itemdrop_specialized_packet_keeps_issued_events_silent) {
    itemDropFixture_t f;
    ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=(vec2_t){1104,1056}});
    ItemDropCall("dropListen");
    ItemDropCall("dropIssue");
    ItemDropAdmitted(&f,100,(vec2_t){1104,1056});
    ItemDropCall("dropNoEvents");
    FOR_LOOP(frame,3)ItemDropStep();
    ItemDropCall("dropNoEvents");
    ItemDropCall("dropEventControls");
    ItemDropEnd(&f);
}

/*206b90 uses common mode1 replacement, which cancels the active user head
 * and its successors before appending the specialized point/item packet. */
TEST(wc3_items, itemdrop_replacement_cancels_active_and_queued_move_through_save) {
    FOR_LOOP(setting,2)FOR_LOOP(saved,2) {
        itemDropFixture_t f;
        float range=setting ? 64 : 100;
        vec2_t point={1004+range,1056};
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=range,.point=point});
        T_ASSERT(G_IssueUnitPointOrder(f.actor,"move",&(vec2_t){1900,1056},false,0,0));
        T_ASSERT(G_IssueUnitPointOrder(f.actor,"move",&(vec2_t){512,1056},true,0,0));
        T_ASSERT(G_IssueUnitPointOrder(f.actor,"move",&(vec2_t){600,1056},true,0,0));
        T_EQ(G_UnitQueuedOrderCount(f.actor),2);
        if(saved)ItemDropRestore(&f,"wc3-itemdrop-old-queue.bin");
        ItemDropCall("dropIssue");
        ItemDropAdmitted(&f,range,point);
        T_EQ(G_UnitQueuedOrderCount(f.actor),0);
        if(saved)ItemDropRestore(&f,"wc3-itemdrop-new-head.bin");
        FOR_LOOP(frame,100)ItemDropStep();
        T_EQ(G_UnitQueuedOrderCount(f.actor),0);
        T_EQ(f.actor->current_order_id,0);
        T_NULL(move_unit_group(f.actor));
        T_ASSERT(f.item->item->in_world);
        T_ASSERT(f.actor->s.origin2.x>1000);
        T_FEQ(f.item->s.origin2.x,point.x,0);
        T_FEQ(f.item->s.origin2.y,point.y,0);
        ItemDropEnd(&f);
    }
}

/* Original admission scene10 rejects while scripted Pause owns851973. */
TEST(wc3_items, itemdrop_paused_source_rejects_without_replacing_suspension) {
    FOR_LOOP(setting,2)FOR_LOOP(saved,2) {
        itemDropFixture_t f;
        float range=setting ? 64 : 100;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=range,.point=(vec2_t){1004+range,1056}});
        ItemDropCall("dropPause");
        T_EQ(f.actor->current_order_id,851973);
        if(saved)ItemDropRestore(&f,"wc3-itemdrop-paused.bin");
        ItemDropCall("dropReject");
        T_EQ(f.actor->current_order_id,851973);
        T_NULL(move_unit_group(f.actor));
        T_NULL(f.actor->item_drop);
        T_EQ(f.actor->inventory[0],f.item);
        ItemDropCall("dropUnpause");
        FOR_LOOP(frame,50)ItemDropStep();
        T_EQ(f.actor->current_order_id,0);
        T_NULL(move_unit_group(f.actor));
        T_EQ(f.actor->inventory[0],f.item);
        T_EQ(f.item->item->carrier,f.actor);
        T_ASSERT(!f.item->item->in_world);
        T_FEQ(f.actor->s.origin2.x,704,0);
        T_FEQ(f.actor->s.origin2.y,1056,0);
        ItemDropEnd(&f);
    }
}

/* Reattaching the original item preserves its identity and terminal action. */
TEST(wc3_items, itemdrop_reattached_original_item_still_completes_through_save) {
    FOR_LOOP(setting,2)FOR_LOOP(saved,2) {
        itemDropFixture_t f;
        float range=setting ? 64 : 100;
        vec2_t point={1004+range,1056};
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=range,.point=point});
        uint32_t spawn=f.item->spawn_time;
        ItemDropCall("dropIssue");
        FOR_LOOP(frame,4)ItemDropStep();
        ItemDropCall("dropUnequip");
        ItemDropCall("dropPickup");
        T_EQ(f.item->spawn_time,spawn);
        ItemDropAdmitted(&f,range,point);
        if(saved)ItemDropRestore(&f,"wc3-itemdrop-reattached.bin");
        for(unsigned frame=0;frame<100 && f.actor->current_order_id==G_OrderId("dropitem");frame++)ItemDropStep();
        T_EQ(f.actor->current_order_id,0);
        T_NULL(move_unit_group(f.actor));
        T_NULL(f.actor->inventory[0]);
        T_NULL(f.item->item->carrier);
        T_ASSERT(f.item->item->in_world);
        T_FEQ(f.item->s.origin2.x,point.x,0);
        T_FEQ(f.item->s.origin2.y,point.y,0);
        ItemDropEnd(&f);
    }
}

/* Native55b420 resolves the retained AgentIdentity pair. Removing an item
 * and recycling its edict must not redirect the terminal action to a new one. */
TEST(wc3_items, itemdrop_recycled_carried_item_is_not_released_by_old_point_task) {
    FOR_LOOP(setting,2)FOR_LOOP(saved,4) {
        itemDropFixture_t f;
        float range=setting ? 64 : 100;
        vec2_t point={1900,1056};
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=range,.point=point});
        uint32_t old=f.item->spawn_time;
        ItemDropCall("dropIssue");
        ItemDropAdmitted(&f,range,point);
        if(saved==1)ItemDropRestore(&f,"wc3-itemdrop-reuse-admitted.bin");
        ItemDropStep();
        ItemDropCall("dropRemove");
        T_ASSERT(!f.item->inuse);
        if(saved==2)ItemDropRestore(&f,"wc3-itemdrop-reuse-removed.bin");
        FOR_LOOP(frame,11)ItemDropStep();
        T_EQ(f.actor->current_order_id,G_OrderId("dropitem"));
        /* Use the actual allocator after its one-second reuse cooldown. */
        for(unsigned count=0;count<64 && !f.item->inuse;count++)ItemDropCall("dropNew");
        T_ASSERT(G_IsItem(f.item));
        if(!G_IsItem(f.item)) {
            ItemDropEnd(&f);
            continue;
        }
        T_NE(f.item->spawn_time,old);
        ItemDropCall("dropPickup");
        T_EQ(f.actor->inventory[0],f.item);
        T_EQ(f.item->item->carrier,f.actor);
        if(saved==3)ItemDropRestore(&f,"wc3-itemdrop-reuse-replacement.bin");
        for(unsigned frame=0;frame<100 && f.actor->current_order_id==G_OrderId("dropitem");frame++)ItemDropStep();
        T_EQ(f.actor->current_order_id,0);
        T_NULL(move_unit_group(f.actor));
        T_NULL(f.actor->item_drop);
        T_EQ(f.actor->item_drop_spawn_time,0);
        T_ASSERT(f.actor->s.origin2.x>1750);
        T_EQ(f.actor->inventory[0],f.item);
        T_EQ(f.item->item->carrier,f.actor);
        T_ASSERT(!f.item->item->in_world);
        ItemDropEnd(&f);
    }
}

/* All six V2 runs keep852001 after item loss and finish the retained point
 * journey. Neither loss of ownership nor RemoveItem cancels the Move task. */
TEST(wc3_items, itemdrop_item_loss_retains_point_owner_through_save_and_arrival) {
    FOR_LOOP(setting,2)FOR_LOOP(removed,2)FOR_LOOP(saved,2) {
        float range=setting ? 64 : 100;
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=range,.point=(vec2_t){1004+range,1056}});
        if(!f.actor || !f.item) {
            ItemDropEnd(&f);
            continue;
        }
        ItemDropCall("dropIssue");
        ItemDropAdmitted(&f,range,(vec2_t){1004+range,1056});
        FOR_LOOP(frame,4)ItemDropStep();
        T_EQ(f.actor->current_order_id,G_OrderId("dropitem"));
        ItemDropCall(removed ? "dropRemove" : "dropUnequip");
        T_EQ(f.actor->current_order_id,G_OrderId("dropitem"));
        T_NULL(f.actor->inventory[0]);
        if(saved) {
            ItemDropRestore(&f,"wc3-itemdrop-item-loss.bin");
            T_EQ(f.actor->current_order_id,G_OrderId("dropitem"));
        }
        ItemDropStep();
        T_EQ(f.actor->current_order_id,G_OrderId("dropitem"));
        T_NOT_NULL(move_unit_group(f.actor));
        for(unsigned frame=0;frame<100 && f.actor->current_order_id==G_OrderId("dropitem");frame++)ItemDropStep();
        T_EQ(f.actor->current_order_id,0);
        T_NULL(move_unit_group(f.actor));
        T_NULL(f.actor->item_drop);
        T_ASSERT(f.actor->s.origin2.x>1000);
        T_NULL(f.actor->inventory[0]);
        ItemDropEnd(&f);
    }
}
#endif
