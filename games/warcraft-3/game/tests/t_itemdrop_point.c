#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"
#include "jass/jass.h"
#include "games/warcraft-3/common/wc3_pathing_arrival.h"
#include "fixtures/retail_itemdrop_motion.h"
extern void reset_entities(void),setup_test_world(void);
bool run_test_jass(cstring_t);
slkTestData_t *parse_slk_string(cstring_t);
void free_slk_rows(slkTestData_t *);

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

/* Extend the same VM, preserving the public carrier/item handles across saves. */
static edict_t *ItemGiveRecipient(itemDropFixture_t *f,vec2_t point) {
    Stb_IniCacheFree(&game.config.misc);
    T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.misc,"[Misc]\nDropItemRange=100\nGiveItemRange=150\nPawnItemRange=130\nPickupItemRange=150\n"));
    cstring_t program=
        "globals\nunit giveRecipient=null\nendglobals\n"
        "function giveInit takes nothing returns nothing\n"
        "set giveRecipient=CreateUnit(Player(0),'Hpal',%.2f,%.2f,0.0)\n"
        "call SetUnitAcquireRange(giveRecipient,0.0)\nendfunction\n"
        "function giveIssue takes nothing returns nothing\n"
        "call BJassAssert(UnitDropItemTarget(dropActor,dropItem,giveRecipient),\"target drop accepted\")\nendfunction\n"
        "function giveReject takes nothing returns nothing\n"
        "call BJassAssert(not UnitDropItemTarget(dropActor,dropItem,giveRecipient),\"disabled target drop rejected\")\nendfunction\n";
    char script[1536];snprintf(script,sizeof(script),program,point.x,point.y);
    T_ASSERT(jass_dobuffer(level.vm,script));
    ItemDropCall("giveInit");
    edict_t *target=NULL;
    FOR_LOOP(i,globals.num_edicts) {
        edict_t *ent=g_edicts+i;
        if(ent->inuse && ent!=f->actor && ent->class_id==MAKEFOURCC('H','p','a','l'))target=ent;
    }
    T_NOT_NULL(target);
    if(!target)return NULL;
    target->collision=32;gi.LinkEntity(target);
    T_FEQ(target->s.origin2.x,point.x,0);T_FEQ(target->s.origin2.y,point.y,0);
    T_EQ(G_InventoryCapacity(target),6);
    T_NULL(target->inventory[0]);
    G_FinishMovePathingInitialization();
    return target;
}

/* Use the same public Apit-only recipient as native V4, with authored31 hull. */
static edict_t *ItemPawnRecipient(itemDropFixture_t *f,uint32_t owner) {
    if(!ItemGiveRecipient(f,(vec2_t){1104,1056}))return NULL;
    char script[768];
    cstring_t program="function giveMakePawn takes nothing returns nothing\ncall RemoveUnit(giveRecipient)\n"
        "set giveRecipient=CreateUnit(Player(%u),'hfoo',1104.0,1056.0,0.0)\n"
        "call SetUnitAcquireRange(giveRecipient,0.0)\n"
        "call BJassAssert(UnitAddAbility(giveRecipient,'Apit'),\"Apit attached\")\n"
        "call SetItemPawnable(dropItem,true)\nendfunction\n";
    snprintf(script,sizeof(script),program,owner);
    T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("giveMakePawn");
    edict_t *target=NULL;
    FOR_LOOP(i,globals.num_edicts)if(g_edicts[i].inuse && g_edicts[i].class_id==MAKEFOURCC('h','f','o','o'))target=g_edicts+i;
    T_NOT_NULL(target);
    if(!target)return NULL;
    T_EQ(target->s.player,owner);T_ASSERT(G_ActorHasAbilityCode(target,MAKEFOURCC('A','p','i','t')));
    T_ASSERT(!G_UnitHasInventory(target));target->collision=31;gi.LinkEntity(target);
    G_FinishMovePathingInitialization();
    return target;
}

/* V1 scenes0..2:75 plus both32 hulls admits139 exactly; the next quarter moves. */
TEST(wc3_inventory_orders, give_public_boundary_and_saved_input) {
    FOR_LOOP(edge,3)FOR_LOOP(saved,2) {
        itemDropFixture_t f;
        vec2_t point={842.75f+edge*.25f,1056};
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=point});
        edict_t *target=ItemGiveRecipient(&f,point);
        if(!target){ItemDropEnd(&f);continue;}
        uint32_t slot=target-g_edicts;
        if(saved) {
            ItemDropRestore(&f,"wc3-give-before-admission.bin");
            target=g_edicts+slot;
        }
        ItemDropCall("giveIssue");
        if(edge<2) {
            T_EQ(f.actor->current_order_id,0);
            T_EQ(f.item->item->carrier,target);
            T_NULL(f.actor->inventory[0]);
            T_EQ(target->inventory[0],f.item);
            T_ASSERT(!G_UnitHasActiveOrder(f.actor));
            T_NULL(move_unit_group(f.actor));
        } else {
            T_EQ(f.actor->current_order_id,852001);
            T_EQ(f.item->item->carrier,f.actor);
            moveGroup_t const *group=move_unit_group(f.actor);T_NOT_NULL(group);
            if(group) {
                T_EQ(group->target,target);
                T_EQ(group->owner_ability,GetAbilityIndex(CAbilityInventory)+1);
                /* Literal native1710a0 publication:4.34375 fine units (0x408b0000). */
                T_EQ(wc3_float_bits(group->members[0].arrival_range),0x408b0000u);
                T_NOT_NULL(group->complete);
            }
        }
        ItemDropEnd(&f);
    }
}

/* V2a18..23: an immobile source attempts the half-range task, then tests full150. */
TEST(wc3_inventory_orders, give_immobile_uses_full_terminal_range) {
    float const points[]={882.75f,883,883.25f,957.75f,958,958.25f};
    FOR_LOOP(i,sizeof(points)/sizeof(*points)) {
        itemDropFixture_t f;
        vec2_t point={points[i],1024};
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_BUILDING_NEAR,.range=100,.point=point});
        edict_t *target=ItemGiveRecipient(&f,point);
        if(!target){ItemDropEnd(&f);continue;}
        ItemDropCall("giveIssue");
        T_EQ(f.actor->current_order_id,0);
        T_NULL(move_unit_group(f.actor));
        T_EQ(f.item->item->carrier,i<5 ? target : f.actor);
        T_EQ(target->inventory[0],i<5 ? f.item : NULL);
        T_EQ(f.actor->inventory[0],i<5 ? NULL : f.item);
        ItemDropEnd(&f);
    }
}

/* V2a24/25 reject before replacing the source's pause/death owner. */
TEST(wc3_inventory_orders, give_paused_and_dead_source_reject_without_replacement) {
    FOR_LOOP(dead,2) {
        itemDropFixture_t f;
        vec2_t point={1104,1056};
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=point});
        edict_t *target=ItemGiveRecipient(&f,point);
        if(!target){ItemDropEnd(&f);continue;}
        ItemDropCall(dead ? "dropKill" : "dropPause");
        ItemDropCall("giveReject");
        T_EQ(f.actor->current_order_id,dead ? 0 : 851973);
        T_EQ(f.item->item->carrier,f.actor);
        T_NULL(target->inventory[0]);
        T_NULL(move_unit_group(f.actor));
        ItemDropEnd(&f);
    }
}
/* V3 scenes0/39: Drop sees the source slot, Pickup sees the destination, and
 * a public Move issued by Drop owns the source after the native returns. */
TEST(wc3_inventory_orders, give_callbacks_are_synchronous_and_preserve_nested_move) {
    FOR_LOOP(nested,2) {
        itemDropFixture_t f;
        vec2_t point={843,1056};
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=point});
        edict_t *target=ItemGiveRecipient(&f,point);
        if(!target){ItemDropEnd(&f);continue;}
        char program[]=
            "globals\ninteger givePhase=0\nboolean giveNested=false\nendglobals\n"
            "function giveDropEvent takes nothing returns nothing\n"
            "call BJassAssert(givePhase==0,\"Drop is first\")\nset givePhase=1\n"
            "call BJassAssert(GetTriggerUnit()==dropActor,\"Drop source\")\n"
            "call BJassAssert(GetManipulatedItem()==dropItem,\"Drop item\")\n"
            "call BJassAssert(UnitItemInSlot(dropActor,0)==dropItem,\"Drop before detach\")\n"
            "call BJassAssert(UnitItemInSlot(giveRecipient,0)==null,\"Drop before attach\")\n"
            "call BJassAssert(GetUnitCurrentOrder(dropActor)==852001,\"Drop sees Inventory head\")\n"
            "if giveNested then\ncall IssuePointOrder(dropActor,\"move\",512.0,1056.0)\nendif\nendfunction\n"
            "function givePickupEvent takes nothing returns nothing\n"
            "call BJassAssert(givePhase==1,\"Pickup after Drop\")\nset givePhase=2\n"
            "call BJassAssert(GetTriggerUnit()==giveRecipient,\"Pickup recipient\")\n"
            "call BJassAssert(GetManipulatedItem()==dropItem,\"Pickup item\")\n"
            "call BJassAssert(UnitItemInSlot(dropActor,0)==null,\"Pickup after detach\")\n"
            "call BJassAssert(UnitItemInSlot(giveRecipient,0)==dropItem,\"Pickup after attach\")\n"
            "if giveNested then\ncall BJassAssert(GetUnitCurrentOrder(dropActor)==851986,\"Pickup sees nested Move\")\n"
            "else\ncall BJassAssert(GetUnitCurrentOrder(dropActor)==852001,\"Pickup sees Inventory head\")\nendif\nendfunction\n"
            "function giveListen takes nothing returns nothing\nlocal trigger d=CreateTrigger()\nlocal trigger p=CreateTrigger()\n"
            "call TriggerRegisterPlayerUnitEvent(d,Player(0),ConvertPlayerUnitEvent(48),null)\n"
            "call TriggerRegisterPlayerUnitEvent(p,Player(0),EVENT_PLAYER_UNIT_PICKUP_ITEM,null)\n"
            "call TriggerAddAction(d,function giveDropEvent)\ncall TriggerAddAction(p,function givePickupEvent)\nendfunction\n"
            "function giveNestedEnable takes nothing returns nothing\nset giveNested=true\nendfunction\n"
            "function giveCheckCallbacks takes nothing returns nothing\ncall BJassAssert(givePhase==2,\"callbacks before return\")\nendfunction\n";
        T_ASSERT(jass_dobuffer(level.vm,program));
        ItemDropCall("giveListen");
        if(nested)ItemDropCall("giveNestedEnable");
        ItemDropCall("giveIssue");
        ItemDropCall("giveCheckCallbacks");
        T_EQ(f.item->item->carrier,target);
        T_EQ(f.actor->current_order_id,nested ? 851986 : 0);
        if(nested) {
            T_NOT_NULL(move_unit_group(f.actor));
            float before=f.actor->s.origin2.x;
            FOR_LOOP(frame,20)ItemDropStep();
            T_ASSERT(f.actor->s.origin2.x<before);
        } else T_NULL(move_unit_group(f.actor));
        ItemDropEnd(&f);
    }
}

/* V3 scene26: capacity changes after admission do not turn Give into Drop. */
TEST(wc3_inventory_orders, give_late_full_recipient_retains_item) {
    itemDropFixture_t f;
    vec2_t point={1104,1056};
    ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=point});
    edict_t *target=ItemGiveRecipient(&f,point);
    if(!target){ItemDropEnd(&f);return;}
    char program[]=
        "function giveFill takes nothing returns nothing\nlocal integer i=0\nloop\nexitwhen i==6\n"
        "call UnitAddItem(giveRecipient,CreateItem('ratf',1104.0,1056.0))\nset i=i+1\nendloop\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.vm,program));
    ItemDropCall("giveIssue");
    FOR_LOOP(frame,4)ItemDropStep();
    ItemDropCall("giveFill");
    T_NOT_NULL(target->inventory[5]);
    for(unsigned frame=0;frame<100 && G_UnitHasActiveOrder(f.actor);frame++)ItemDropStep();
    T_EQ(f.actor->current_order_id,0);
    T_NULL(move_unit_group(f.actor));
    T_EQ(f.item->item->carrier,f.actor);
    T_EQ(f.actor->inventory[0],f.item);
    T_ASSERT(!f.item->item->in_world);
    ItemDropEnd(&f);
}
/* V3 scene45: Apit alone admits Pawn; no shop merchandise UI is required. */
TEST(wc3_inventory_orders, pawn_target_owns_approach_and_authored_range) {
    itemDropFixture_t f;
    vec2_t point={1104,1056};
    ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=point});
    edict_t *target=ItemGiveRecipient(&f,point);
    if(!target){ItemDropEnd(&f);return;}
    char script[]=
        "function giveMakePawn takes nothing returns nothing\n"
        "call RemoveUnit(giveRecipient)\n"
        "set giveRecipient=CreateUnit(Player(0),'hfoo',1104.0,1056.0,0.0)\n"
        "call SetUnitAcquireRange(giveRecipient,0.0)\n"
        "call BJassAssert(UnitAddAbility(giveRecipient,'Apit'),\"Apit attached\")\n"
        "call SetItemPawnable(dropItem,true)\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("giveMakePawn");
    FOR_LOOP(i,globals.num_edicts)if(g_edicts[i].inuse && g_edicts[i].class_id==MAKEFOURCC('h','f','o','o'))target=g_edicts+i;
    T_EQ(target->class_id,MAKEFOURCC('h','f','o','o'));
    T_ASSERT(G_ActorHasAbilityCode(target,MAKEFOURCC('A','p','i','t')));
    T_ASSERT(!G_UnitHasInventory(target));
    target->collision=31;gi.LinkEntity(target);
    ItemData_t data=*f.item->data.ItemData;
    data.goldcost=800;data.lumbercost=0;f.item->data.ItemData=&data;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD]=0;
    ItemDropCall("giveIssue");
    T_EQ(f.actor->current_order_id,852001);
    T_EQ(f.actor->inventory[0],f.item);
    moveGroup_t const *group=move_unit_group(f.actor);T_NOT_NULL(group);
    /* Native Pawn approach:65 plus Hpal32 and Footman31 hulls =4 fine. */
    if(group)T_EQ(wc3_float_bits(group->members[0].arrival_range),0x40800000u);
    for(unsigned frame=0;frame<100 && f.actor->current_order_id;frame++)ItemDropStep();
    T_EQ(f.actor->current_order_id,0);
    T_NULL(f.actor->inventory[0]);T_NULL(move_unit_group(f.actor));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD],400);
    ItemDropEnd(&f);
}

/* V2a11: a noninventory unit takes the captured point/drop path immediately. */
TEST(wc3_inventory_orders, noninventory_target_uses_point_drop_fallback) {
    itemDropFixture_t f;
    vec2_t point={1104,1056};
    ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=point});
    edict_t *target=ItemGiveRecipient(&f,point);
    if(!target){ItemDropEnd(&f);return;}
    char script[]=
        "function giveMakeFootman takes nothing returns nothing\ncall RemoveUnit(giveRecipient)\n"
        "set giveRecipient=CreateUnit(Player(0),'hfoo',1104.0,1056.0,0.0)\n"
        "call SetUnitAcquireRange(giveRecipient,0.0)\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("giveMakeFootman");
    ItemDropCall("giveIssue");
    T_EQ(f.actor->current_order_id,852001);
    moveGroup_t const *group=move_unit_group(f.actor);T_NOT_NULL(group);
    if(group) {
        T_EQ(group->target,NULL);
        T_EQ(wc3_float_bits(group->members[0].arrival_range),0x40480000u);
    }
    for(unsigned frame=0;frame<100 && f.actor->current_order_id;frame++)ItemDropStep();
    T_EQ(f.actor->current_order_id,0);T_NULL(f.actor->inventory[0]);
    T_ASSERT(f.item->item->in_world);T_NULL(f.item->item->carrier);
    T_FEQ(f.item->s.origin2.x,1104,0);T_FEQ(f.item->s.origin2.y,1056,0);
    ItemDropEnd(&f);
}
/* A save must retain the live callback, both identities, full range and
 * half-range Move request, not merely reproduce admission after loading. */
TEST(wc3_inventory_orders, give_saved_approach_reaches_same_recipient) {
    itemDropFixture_t f;
    vec2_t point={1104,1056};
    ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=point});
    edict_t *target=ItemGiveRecipient(&f,point);
    if(!target){ItemDropEnd(&f);return;}
    uint32_t target_slot=target-g_edicts;
    ItemDropCall("giveIssue");
    FOR_LOOP(frame,4)ItemDropStep();
    T_NOT_NULL(move_unit_group(f.actor));
    ItemDropRestore(&f,"wc3-give-pending-approach.bin");target=g_edicts+target_slot;
    moveGroup_t const *group=move_unit_group(f.actor);T_NOT_NULL(group);
    if(group) {
        T_EQ(group->target,target);
        T_EQ(group->complete,S_InventoryTargetComplete);
        T_EQ(wc3_float_bits(group->members[0].arrival_range),0x408b0000u);
    }
    T_EQ(f.actor->item_drop,f.item);
    T_EQ(f.actor->item_drop_target_spawn_time,target->spawn_time);
    T_EQ(wc3_float_bits(f.actor->item_drop_range),0x43160000u);
    for(unsigned frame=0;frame<100 && f.actor->current_order_id;frame++)ItemDropStep();
    T_EQ(f.actor->current_order_id,0);T_NULL(move_unit_group(f.actor));
    T_NULL(f.actor->inventory[0]);T_EQ(target->inventory[0],f.item);
    T_EQ(f.item->item->carrier,target);T_NULL(f.actor->item_drop);
    ItemDropEnd(&f);
}
/* V2a8/9/14 and V3 37/38/51 all capture an unassociated point packet.
 * These cases must not retain the rejected widget as an observed target. */
TEST(wc3_inventory_orders, rejected_recipient_shapes_capture_point_packets) {
    char const *changes[]={
        "local integer i=0\nloop\nexitwhen i==6\ncall UnitAddItem(giveRecipient,CreateItem('ratf',1104.0,1056.0))\nset i=i+1\nendloop\n",
        "call SetUnitOwner(giveRecipient,Player(1),false)\n",
        "call RemoveUnit(giveRecipient)\nset giveRecipient=dropActor\n",
        "call KillUnit(giveRecipient)\n",
        "call ShowUnit(giveRecipient,false)\n"
    };
    FOR_LOOP(kind,sizeof(changes)/sizeof(*changes)) {
        itemDropFixture_t f;
        vec2_t point={1104,1056};
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=point});
        edict_t *target=ItemGiveRecipient(&f,point);
        if(!target){ItemDropEnd(&f);continue;}
        char program[1024];snprintf(program,sizeof(program),"function giveChange takes nothing returns nothing\n%sendfunction\n",changes[kind]);
        T_ASSERT(jass_dobuffer(level.vm,program));ItemDropCall("giveChange");
        ItemDropCall("giveIssue");
#ifdef WC3_SC2API
        unitOrder_t packet;T_ASSERT(G_GetActiveUnitOrder(f.actor,&packet));
        T_EQ(packet.target_type,UNIT_ORDER_TARGET_POINT_ITEM);

#endif
        moveGroup_t const *group=move_unit_group(f.actor);T_NOT_NULL(group);
        if(group)T_NULL(group->target);
        for(unsigned frame=0;frame<100 && f.actor->current_order_id;frame++)ItemDropStep();
        T_EQ(f.actor->current_order_id,0);T_NULL(f.actor->inventory[0]);
        T_ASSERT(f.item->item->in_world);T_NULL(f.item->item->carrier);
        T_FEQ(f.item->s.origin2.x,kind==2 ? 704 : 1104,0);
        T_FEQ(f.item->s.origin2.y,1056,0);
        ItemDropEnd(&f);
    }
}

/* V3 35/36 require a physical task even though half-range already succeeds. */
TEST(wc3_inventory_orders, give_behind_source_retains_facing_task) {
    FOR_LOOP(edge,2) {
        itemDropFixture_t f;
        vec2_t point={565.25f-edge*.25f,1056};
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=point});
        edict_t *target=ItemGiveRecipient(&f,point);
        if(!target){ItemDropEnd(&f);continue;}
        ItemDropCall("giveIssue");
        T_EQ(f.actor->current_order_id,852001);T_NOT_NULL(move_unit_group(f.actor));
        T_EQ(f.actor->inventory[0],f.item);T_NULL(target->inventory[0]);
        for(unsigned frame=0;frame<100 && f.actor->current_order_id;frame++)ItemDropStep();
        T_EQ(f.actor->current_order_id,0);T_EQ(target->inventory[0],f.item);
        T_NULL(f.actor->inventory[0]);T_NULL(move_unit_group(f.actor));
        ItemDropEnd(&f);
    }
}

/* V3 scene47: a stationary recipient's perimeter is valid at GiveItemRange0. */
TEST(wc3_inventory_orders, give_zero_range_to_stationary_inventory) {
    itemDropFixture_t f;
    vec2_t point={1088,1024};
    ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=point});
    edict_t *target=ItemGiveRecipient(&f,point);
    if(!target){ItemDropEnd(&f);return;}
    char program[]=
        "function giveFarm takes nothing returns nothing\ncall RemoveUnit(giveRecipient)\n"
        "set giveRecipient=CreateUnit(Player(0),'hhou',1088.0,1024.0,0.0)\n"
        "call BJassAssert(UnitAddAbility(giveRecipient,'AInv'),\"Farm inventory\")\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.vm,program));ItemDropCall("giveFarm");
    FOR_LOOP(i,globals.num_edicts)if(g_edicts[i].inuse && g_edicts[i].class_id==MAKEFOURCC('h','h','o','u'))target=g_edicts+i;
    T_EQ(target->class_id,MAKEFOURCC('h','h','o','u'));T_ASSERT(target->aiflags&AI_IMMOBILE);
    T_NOT_NULL(target->pathtex);T_FEQ(target->collision,72,0);
    Stb_IniCacheFree(&game.config.misc);
    T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.misc,"[Misc]\nDropItemRange=100\nGiveItemRange=0\nPawnItemRange=0\n"));
    G_FinishMovePathingInitialization();ItemDropCall("giveIssue");
    T_EQ(f.actor->current_order_id,852001);T_NOT_NULL(move_unit_group(f.actor));
    for(unsigned frame=0;frame<100 && f.actor->current_order_id;frame++)ItemDropStep();
    T_EQ(f.actor->current_order_id,0);T_NULL(move_unit_group(f.actor));
    T_EQ(target->inventory[0],f.item);T_EQ(f.item->item->carrier,target);T_NULL(f.actor->inventory[0]);
    ItemDropEnd(&f);
}

/* V3 target death and explicit Stop retire the handoff carrying its item. */
TEST(wc3_inventory_orders, give_target_death_and_stop_keep_source_item) {
    FOR_LOOP(stop,2) {
        itemDropFixture_t f;
        vec2_t point={1104,1056};
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=point});
        edict_t *target=ItemGiveRecipient(&f,point);
        if(!target){ItemDropEnd(&f);continue;}
        char program[]="function giveKillTarget takes nothing returns nothing\ncall KillUnit(giveRecipient)\nendfunction\n";
        T_ASSERT(jass_dobuffer(level.vm,program));ItemDropCall("giveIssue");
        FOR_LOOP(frame,4)ItemDropStep();
        ItemDropCall(stop ? "dropStop" : "giveKillTarget");
        for(unsigned frame=0;frame<100 && G_UnitHasActiveOrder(f.actor);frame++)ItemDropStep();
        T_EQ(f.actor->current_order_id,0);T_NULL(move_unit_group(f.actor));
        T_EQ(f.actor->inventory[0],f.item);T_EQ(f.item->item->carrier,f.actor);
        T_NULL(f.actor->item_drop);T_NULL(f.actor->goalentity);
        ItemDropEnd(&f);
    }
}
/* V2a14: item widgets are accepted through the captured point/drop fallback. */
TEST(wc3_inventory_orders, item_widget_target_uses_captured_point) {
    itemDropFixture_t f;
    vec2_t point={782,1056};
    ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=point});
    char program[]=
        "function giveToItem takes nothing returns nothing\nlocal item other=CreateItem('rde2',782.0,1056.0)\n"
        "call BJassAssert(UnitDropItemTarget(dropActor,dropItem,other),\"item-widget point fallback\")\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.vm,program));ItemDropCall("giveToItem");
    T_EQ(f.actor->current_order_id,852001);
    for(unsigned frame=0;frame<10 && f.actor->current_order_id;frame++)ItemDropStep();
    T_EQ(f.actor->current_order_id,0);T_NULL(f.actor->inventory[0]);
    T_ASSERT(f.item->item->in_world);T_NULL(f.item->item->carrier);
    /* Native V3 scene14: occupied item placement selects this adjacent cell. */
    T_FEQ(f.item->s.origin2.x,784,0);T_FEQ(f.item->s.origin2.y,1040,0);
    ItemDropEnd(&f);
}

/* V3 40/41: removal inside Drop does not invalidate the enclosing action's
 * recipient. RemoveItem itself publishes a nested Drop before deletion. */
TEST(wc3_inventory_orders, give_reentrant_source_and_item_removal) {
    FOR_LOOP(remove_item,2) {
        itemDropFixture_t f;
        vec2_t point={843,1056};
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=point});
        edict_t *target=ItemGiveRecipient(&f,point);
        if(!target){ItemDropEnd(&f);continue;}
        char program[4096];
        snprintf(program,sizeof(program),
            "globals\ninteger giveDrops=0\ninteger givePickups=0\nendglobals\n"
            "function giveRemoveDrop takes nothing returns nothing\nset giveDrops=giveDrops+1\n"
            "call BJassAssert(GetManipulatedItem()==dropItem,\"nested Drop retains item\")\n"
            "call BJassAssert(UnitItemInSlot(dropActor,0)==dropItem,\"nested Drop before detach\")\n"
            "if giveDrops==1 then\n%s\nendif\nendfunction\n"
            "function giveRemovePickup takes nothing returns nothing\nset givePickups=givePickups+1\n"
            "call BJassAssert(GetTriggerUnit()==giveRecipient,\"retained recipient callback\")\n%s\nendfunction\n"
            "function giveRemoveListen takes nothing returns nothing\nlocal trigger d=CreateTrigger()\nlocal trigger p=CreateTrigger()\n"
            "call TriggerRegisterPlayerUnitEvent(d,Player(0),ConvertPlayerUnitEvent(48),null)\n"
            "call TriggerRegisterPlayerUnitEvent(p,Player(0),EVENT_PLAYER_UNIT_PICKUP_ITEM,null)\n"
            "call TriggerAddAction(d,function giveRemoveDrop)\ncall TriggerAddAction(p,function giveRemovePickup)\nendfunction\n"
            "function giveRemoveCheck takes nothing returns nothing\n"
            "call BJassAssert(giveDrops==%u,\"all nested Drops before return\")\n"
            "call BJassAssert(givePickups==1,\"Pickup before return\")\nendfunction\n",
            remove_item ? "call RemoveItem(dropItem)" : "call RemoveUnit(dropActor)",
            remove_item ? "call BJassAssert(GetManipulatedItem()==null,\"removed item resolves null\")\ncall BJassAssert(UnitItemInSlot(giveRecipient,0)==null,\"no removed item attached\")" :
                "call BJassAssert(GetManipulatedItem()==dropItem,\"source removal retains item\")\ncall BJassAssert(UnitItemInSlot(giveRecipient,0)==dropItem,\"recipient owns item\")\ncall BJassAssert(GetUnitCurrentOrder(dropActor)==0,\"removed source head\")",
            remove_item ? 2u : 1u);
        T_ASSERT(jass_dobuffer(level.vm,program));ItemDropCall("giveRemoveListen");
        ItemDropCall("giveIssue");ItemDropCall("giveRemoveCheck");
        if(remove_item) {
            T_ASSERT(!G_IsItem(f.item));T_NULL(target->inventory[0]);
        } else {
            T_EQ(target->inventory[0],f.item);T_EQ(f.item->item->carrier,target);
            FOR_LOOP(frame,3)ItemDropStep();
            T_ASSERT(!f.actor->inuse);T_EQ(target->inventory[0],f.item);
        }
        ItemDropEnd(&f);
    }
}

/* V3 scene33 retains the action through PauseUnit; a cold snapshot must
 * preserve the same item, target and terminal range during suspension. */
TEST(wc3_inventory_orders, give_paused_approach_and_saved_resume) {
    FOR_LOOP(saved,2) {
        itemDropFixture_t f;
        vec2_t point={1104,1056};
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=point});
        edict_t *target=ItemGiveRecipient(&f,point);
        if(!target){ItemDropEnd(&f);continue;}
        uint32_t target_slot=target-g_edicts;
        ItemDropCall("giveIssue");FOR_LOOP(frame,4)ItemDropStep();
        ItemDropCall("dropPause");T_EQ(f.actor->current_order_id,851973);
        if(saved) {
            ItemDropRestore(&f,"wc3-give-paused.bin");target=g_edicts+target_slot;
        }
        vec2_t before=f.actor->s.origin2;
        FOR_LOOP(frame,5)ItemDropStep();
        T_FEQ(f.actor->s.origin2.x,before.x,0);T_FEQ(f.actor->s.origin2.y,before.y,0);
        T_EQ(f.actor->inventory[0],f.item);T_NULL(target->inventory[0]);
        ItemDropCall("dropUnpause");
        for(unsigned frame=0;frame<100 && !target->inventory[0];frame++)ItemDropStep();
        T_EQ(target->inventory[0],f.item);T_EQ(f.actor->current_order_id,0);
        T_NULL(f.actor->inventory[0]);T_NULL(move_unit_group(f.actor));
        ItemDropEnd(&f);
    }
}

/* A callback's successor may use the same recipient as its new Move goal.
 * Inventory leave must release its own binding without erasing that successor. */
TEST(wc3_inventory_orders, give_callback_follow_preserves_same_target) {
    itemDropFixture_t f;
    vec2_t point={843,1056};
    ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=point});
    edict_t *target=ItemGiveRecipient(&f,point);
    if(!target){ItemDropEnd(&f);return;}
    char program[]=
        "function giveFollowEvent takes nothing returns nothing\n"
        "call BJassAssert(IssueTargetOrder(dropActor,\"move\",giveRecipient),\"callback Follow admitted\")\nendfunction\n"
        "function giveFollowListen takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "call TriggerRegisterPlayerUnitEvent(t,Player(0),ConvertPlayerUnitEvent(48),null)\n"
        "call TriggerAddAction(t,function giveFollowEvent)\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.vm,program));ItemDropCall("giveFollowListen");
    ItemDropCall("giveIssue");
    T_EQ(target->inventory[0],f.item);T_NULL(f.actor->inventory[0]);
    T_EQ(f.actor->goalentity,target);T_EQ(f.actor->movement.follow_target,target);
    T_ASSERT(f.actor->currentmove && f.actor->currentmove->proc==CAbilityMove);
    ItemDropStep();
    T_EQ(f.actor->movement.follow_target,target);
    T_ASSERT(f.actor->currentmove && f.actor->currentmove->proc==CAbilityMove);
    ItemDropEnd(&f);
}

/* V3 49/50 reject item eligibility before replacing an existing user order. */
TEST(wc3_inventory_orders, give_invalid_item_preserves_existing_move) {
    FOR_LOOP(unequipped,2) {
        itemDropFixture_t f;
        vec2_t point={1104,1056};
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point=point});
        edict_t *target=ItemGiveRecipient(&f,point);
        if(!target){ItemDropEnd(&f);continue;}
        char program[]=
            "function givePriorMove takes nothing returns nothing\n"
            "call BJassAssert(IssuePointOrder(dropActor,\"move\",512.0,1056.0),\"prior Move\")\nendfunction\n";
        T_ASSERT(jass_dobuffer(level.vm,program));ItemDropCall("givePriorMove");
        T_EQ(f.actor->current_order_id,851986);
        moveGroup_t const *group=move_unit_group(f.actor);T_NOT_NULL(group);
        uint32_t id=group ? group->id : 0;
        ItemDropCall(unequipped ? "dropUnequip" : "dropLock");
        ItemDropCall("giveReject");
        T_EQ(f.actor->current_order_id,851986);T_EQ(f.actor->movement.group_id,id);
        T_EQ(f.actor->inventory[0],unequipped ? NULL : f.item);T_NULL(target->inventory[0]);
        ItemDropEnd(&f);
    }
}

/* V4 0..7 distinguish the two directional permissions. The setter/getter
 * preconditions are public; symmetric alliances cannot expose these inverses. */
TEST(wc3_inventory_orders, give_and_pawn_use_their_native_alliance_direction) {
    FOR_LOOP(pawn,2)FOR_LOOP(bits,4) {
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point={1104,1056}});
        edict_t *target=pawn ? ItemPawnRecipient(&f,1) : ItemGiveRecipient(&f,(vec2_t){1104,1056});
        if(!target){ItemDropEnd(&f);continue;}
        ItemData_t data=*f.item->data.ItemData;data.goldcost=800;data.lumbercost=0;
        f.item->data.ItemData=&data;game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD]=0;
        char script[1024];
        cstring_t format="function giveDiplomacy takes nothing returns nothing\n"
            "call SetUnitOwner(giveRecipient,Player(1),false)\n"
            "call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,%s)\n"
            "call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,%s)\n"
            "call SetPlayerAlliance(Player(0),Player(1),ConvertAllianceType(4),%s)\n"
            "call SetPlayerAlliance(Player(1),Player(0),ConvertAllianceType(4),%s)\nendfunction\n";
        snprintf(script,sizeof(script),format,!pawn || (bits&1) ? "true" : "false",!pawn || (bits&2) ? "true" : "false",!pawn && (bits&1) ? "true" : "false",!pawn && (bits&2) ? "true" : "false");
        T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("giveDiplomacy");
        T_EQ(target->s.player,1);
        T_EQ(!!(level.alliances[0][1]&(1u<<ALLIANCE_PASSIVE)),!pawn || !!(bits&1));
        T_EQ(!!(level.alliances[1][0]&(1u<<ALLIANCE_PASSIVE)),!pawn || !!(bits&2));
        T_EQ(!!(level.alliances[0][1]&(1u<<ALLIANCE_SHARED_SPELLS)),!pawn && !!(bits&1));
        T_EQ(!!(level.alliances[1][0]&(1u<<ALLIANCE_SHARED_SPELLS)),!pawn && !!(bits&2));
        bool accepted_target=(bits&(pawn ? 2u : 1u))!=0;
        ItemDropCall("giveIssue");
        moveGroup_t const *group=move_unit_group(f.actor);T_NOT_NULL(group);
        if(group)T_EQ(group->target,accepted_target ? target : NULL);
        for(unsigned frame=0;frame<100 && f.actor->current_order_id;frame++)ItemDropStep();
        T_EQ(f.actor->current_order_id,0);
        T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD],pawn && accepted_target ? 400 : 0);
        if(pawn && accepted_target)T_ASSERT(!G_IsItem(f.item));
        else if(accepted_target)T_EQ(target->inventory[0],f.item);
        else {
            T_ASSERT(G_IsItem(f.item));
            if(G_IsItem(f.item)) {
                T_ASSERT(f.item->item->in_world);T_NULL(f.item->item->carrier);
                T_FEQ(f.item->s.origin2.x,1104,0);T_FEQ(f.item->s.origin2.y,1056,0);
            }
        }
        ItemDropEnd(&f);
    }
}

/* V4 8..11: unavailable Apit at admission takes point fallback; losing
 * availability during the Pawn approach instead retires with the item carried. */
TEST(wc3_inventory_orders, pawn_availability_before_and_after_admission) {
    FOR_LOOP(mode,4) {
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point={1104,1056}});
        edict_t *target=ItemPawnRecipient(&f,0);
        if(!target){ItemDropEnd(&f);continue;}
        ItemData_t data=*f.item->data.ItemData;data.goldcost=800;data.lumbercost=0;f.item->data.ItemData=&data;
        game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD]=0;
        char script[]="function givePauseShop takes nothing returns nothing\ncall PauseUnit(giveRecipient,true)\nendfunction\n"
            "function giveDisableShop takes nothing returns nothing\ncall SetPlayerAbilityAvailable(Player(0),'Apit',false)\nendfunction\n"
            "function giveRemoveShop takes nothing returns nothing\ncall UnitRemoveAbility(giveRecipient,'Apit')\nendfunction\n";
        T_ASSERT(jass_dobuffer(level.vm,script));
        if(mode<2)ItemDropCall(mode ? "giveDisableShop" : "givePauseShop");
        ItemDropCall("giveIssue");
        if(mode>=2) {
            FOR_LOOP(frame,4)ItemDropStep();
            ItemDropCall(mode==2 ? "givePauseShop" : "giveRemoveShop");
        }
        for(unsigned frame=0;frame<100 && f.actor->current_order_id;frame++)ItemDropStep();
        T_EQ(f.actor->current_order_id,0);T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD],0);
        T_ASSERT(G_IsItem(f.item));
        if(G_IsItem(f.item)) {
            T_EQ(f.item->item->in_world,mode<2);
            T_EQ(f.item->item->carrier,mode<2 ? NULL : f.actor);
        }
        T_EQ(f.actor->inventory[0],mode<2 ? NULL : f.item);
        /* A player-wide disable belongs to the player registry, outside edict reset. */
        G_SetPlayerAbilityAvailable(game.clients,MAKEFOURCC('A','p','i','t'),true);
        ItemDropEnd(&f);
    }
}

/* V4 12..14: source detachment precedes the second recipient admission.
 * Full/lost Inventory leaves a hidden, unowned item; a removed recipient
 * first receives it, then publishes Drop during its deferred destruction. */
TEST(wc3_inventory_orders, give_recipient_mutation_during_drop) {
    FOR_LOOP(mode,3) {
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point={843,1056}});
        edict_t *target=ItemGiveRecipient(&f,(vec2_t){843,1056});
        if(!target){ItemDropEnd(&f);continue;}
        uint32_t target_slot=target-g_edicts;
        cstring_t format="globals\ninteger mutationDrops=0\ninteger mutationPickups=0\nendglobals\n"
            "function giveMutateDrop takes nothing returns nothing\nlocal integer i=0\n"
            "set mutationDrops=mutationDrops+1\n"
            "if GetTriggerUnit()==dropActor then\n"
            "call BJassAssert(UnitItemInSlot(dropActor,0)==dropItem,\"source Drop before detach\")\n%s\n"
            "else\ncall BJassAssert(GetTriggerUnit()==giveRecipient,\"retiring recipient Drop\")\n"
            "call BJassAssert(UnitItemInSlot(giveRecipient,0)==null,\"retirement detaches before Drop\")\nendif\nendfunction\n"
            "function giveMutatePickup takes nothing returns nothing\nif GetManipulatedItem()==dropItem then\n"
            "set mutationPickups=mutationPickups+1\nendif\nendfunction\n"
            "function giveMutateListen takes nothing returns nothing\nlocal trigger d=CreateTrigger()\nlocal trigger p=CreateTrigger()\n"
            "call TriggerRegisterPlayerUnitEvent(d,Player(0),ConvertPlayerUnitEvent(48),null)\n"
            "call TriggerRegisterPlayerUnitEvent(p,Player(0),EVENT_PLAYER_UNIT_PICKUP_ITEM,null)\n"
            "call TriggerAddAction(d,function giveMutateDrop)\ncall TriggerAddAction(p,function giveMutatePickup)\nendfunction\n"
            "function giveMutateCheck takes nothing returns nothing\n"
            "call BJassAssert(mutationDrops==%u,\"complete Drop phases\")\n"
            "call BJassAssert(mutationPickups==%u,\"complete gift Pickup phases\")\nendfunction\n";
        cstring_t mutation=mode==0 ? "loop\nexitwhen i==6\ncall UnitAddItem(giveRecipient,CreateItem('ratf',843.0,1056.0))\nset i=i+1\nendloop" : mode==1 ? "call RemoveUnit(giveRecipient)" : "call UnitRemoveAbility(giveRecipient,'AInv')";
        char script[3072];snprintf(script,sizeof(script),format,mutation,mode==1 ? 2u : 1u,mode==1 ? 1u : 0u);
        T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("giveMutateListen");ItemDropCall("giveIssue");
        T_NULL(f.actor->inventory[0]);T_EQ(f.actor->current_order_id,0);
        if(mode==1) {
            T_EQ(target->inventory[0],f.item);FOR_LOOP(frame,3)ItemDropStep();
            T_ASSERT(!G_IsItem(f.item));
        } else {
            T_ASSERT(G_IsItem(f.item));T_NULL(f.item->item->carrier);
            T_EQ(f.item->item->inventory_slot,-1);T_ASSERT(!f.item->item->in_world);
            T_ASSERT(f.item->s.renderfx&RF_HIDDEN);T_ASSERT(f.item->svflags&SVF_NOCLIENT);
            ItemDropRestore(&f,"wc3-give-detached.bin");target=g_edicts+target_slot;
            T_NULL(f.item->item->carrier);T_ASSERT(!f.item->item->in_world);
            T_FEQ(f.item->s.origin2.x,704,0);T_FEQ(f.item->s.origin2.y,1056,0);
        }
        ItemDropCall("giveMutateCheck");ItemDropEnd(&f);
    }
}

/*202520 admits SoldItem only for Sell/Pawn. A real earlier purchase must not
 * leak its item through the following Pawn transaction's Drop callback. */
TEST(wc3_inventory_orders, pawn_callback_getters_after_previous_sale) {
    itemDropFixture_t f;
    ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point={1104,1056}});
    edict_t *target=ItemPawnRecipient(&f,PLAYER_NEUTRAL_PASSIVE);
    if(!target){ItemDropEnd(&f);return;}
    ItemData_t data=*f.item->data.ItemData;data.goldcost=800;data.lumbercost=0;f.item->data.ItemData=&data;
    UnitProfile_t profile=*target->data.UnitProfile;profile.sellItems="spro";target->data.UnitProfile=&profile;
    target->stock=G_AllocStock();T_NOT_NULL(target->stock);
    if(!target->stock){ItemDropEnd(&f);return;}
    target->stock->item_slots=11;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD]=2000;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER]=2000;
    edict_t *old_sold=eventsolditem;
    /* The replaced fixture Hero must finish deferred removal before patron
     * acquisition; this test needs a real sale to the active source. */
    ItemDropStep();T_EQ(G_FindShopPatron(game.clients,target),f.actor);
    T_ASSERT(G_ShopPurchaseItem(G_GetPlayerEntityByNumber(0),target,MAKEFOURCC('s','p','r','o')));
    T_NOT_NULL(f.actor->inventory[1]);T_EQ(eventsolditem,f.actor->inventory[1]);
    uint32_t gold=game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD];
    char script[3072];
    cstring_t program="globals\ninteger pawnPhases=0\nendglobals\n"
        "function pawnReadEvent takes nothing returns nothing\n"
        "call BJassAssert(pawnPhases==0,\"Pawn before Drop\")\nset pawnPhases=1\n"
        "call BJassAssert(GetSoldItem()==dropItem,\"Pawn sold item belongs to callback\")\n"
        "call BJassAssert(GetManipulatedItem()==null,\"Pawn has no manipulated item\")\n"
        "call BJassAssert(GetTriggerUnit()==dropActor,\"Pawn source\")\n"
        "call BJassAssert(UnitItemInSlot(dropActor,0)==dropItem,\"Pawn before detach\")\n"
        "call BJassAssert(GetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD)==%u,\"Pawn before payment\")\nendfunction\n"
        "function pawnDropReadEvent takes nothing returns nothing\n"
        "call BJassAssert(pawnPhases==1,\"Drop after Pawn\")\nset pawnPhases=2\n"
        "call BJassAssert(GetSoldItem()==null,\"prior sale cannot leak into Drop\")\n"
        "call BJassAssert(GetManipulatedItem()==dropItem,\"Drop gift\")\n"
        "call BJassAssert(UnitItemInSlot(dropActor,0)==dropItem,\"Drop before detach\")\n"
        "call BJassAssert(GetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD)==%u,\"Drop before payment\")\nendfunction\n"
        "function pawnReadListen takes nothing returns nothing\nlocal trigger p=CreateTrigger()\nlocal trigger d=CreateTrigger()\n"
        "call TriggerRegisterPlayerUnitEvent(p,Player(0),ConvertPlayerUnitEvent(277),null)\n"
        "call TriggerRegisterPlayerUnitEvent(d,Player(0),ConvertPlayerUnitEvent(48),null)\n"
        "call TriggerAddAction(p,function pawnReadEvent)\ncall TriggerAddAction(d,function pawnDropReadEvent)\nendfunction\n"
        "function pawnReadCheck takes nothing returns nothing\ncall BJassAssert(pawnPhases==2,\"both Pawn callback phases\")\nendfunction\n";
    snprintf(script,sizeof(script),program,gold,gold);
    T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("pawnReadListen");ItemDropCall("giveIssue");
    for(unsigned frame=0;frame<100 && f.actor->current_order_id;frame++)ItemDropStep();
    ItemDropCall("pawnReadCheck");
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD],gold+400);
    T_NULL(f.actor->inventory[0]);T_NOT_NULL(f.actor->inventory[1]);
    eventsolditem=old_sold;ItemDropEnd(&f);
}

/* V4 recovery: both public APIs recover the detached hidden item after a
 * full-recipient or lost-Inventory callback, including a cold snapshot. */
TEST(wc3_inventory_orders, detached_gift_scripted_recovery) {
    FOR_LOOP(lost_inventory,2)FOR_LOOP(position,2) {
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point={843,1056}});
        if(!ItemGiveRecipient(&f,(vec2_t){843,1056})){ItemDropEnd(&f);continue;}
        cstring_t format="function recoverGiftDrop takes nothing returns nothing\nlocal integer i=0\n%s\nendfunction\n"
            "function recoverGiftListen takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call TriggerRegisterPlayerUnitEvent(t,Player(0),ConvertPlayerUnitEvent(48),null)\n"
            "call TriggerAddAction(t,function recoverGiftDrop)\nendfunction\n"
            "function recoverGiftAdd takes nothing returns nothing\n"
            "call BJassAssert(UnitAddItem(dropActor,dropItem),\"recover detached gift\")\nendfunction\n"
            "function recoverGiftPosition takes nothing returns nothing\ncall SetItemPosition(dropItem,900.0,1200.0)\n"
            "call BJassAssert(IsItemVisible(dropItem),\"position republishes detached item\")\nendfunction\n";
        cstring_t mutation=lost_inventory ? "call UnitRemoveAbility(giveRecipient,'AInv')" :
            "loop\nexitwhen i==6\ncall UnitAddItem(giveRecipient,CreateItem('ratf',843.0,1056.0))\nset i=i+1\nendloop";
        char script[2048];snprintf(script,sizeof(script),format,mutation);
        T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("recoverGiftListen");ItemDropCall("giveIssue");
        T_NULL(f.actor->inventory[0]);T_NULL(f.item->item->carrier);T_ASSERT(!f.item->item->in_world);
        ItemDropRestore(&f,"wc3-detached-gift-recover.bin");
        ItemDropCall(position ? "recoverGiftPosition" : "recoverGiftAdd");
        if(position) {
            T_NULL(f.item->item->carrier);T_ASSERT(f.item->item->in_world);
            T_ASSERT(!(f.item->s.renderfx&RF_HIDDEN));T_ASSERT(!(f.item->svflags&SVF_NOCLIENT));
            T_FEQ(f.item->s.origin2.x,900,0);T_FEQ(f.item->s.origin2.y,1200,0);
        } else {
            T_EQ(f.actor->inventory[0],f.item);T_EQ(f.item->item->carrier,f.actor);
            T_ASSERT(!f.item->item->in_world);
        }
        ItemDropEnd(&f);
    }
}

/* V4 script: visible/hidden world insertion and carried/undroppable transfer
 * all finish their appropriate callbacks before public UnitAddItem returns. */
TEST(wc3_inventory_orders, scripted_add_callbacks_are_synchronous) {
    FOR_LOOP(mode,4) {
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point={843,1056}});
        edict_t *target=ItemGiveRecipient(&f,(vec2_t){843,1056});
        if(!target){ItemDropEnd(&f);continue;}
        bool transfer=(mode&1)!=0;
        if(!transfer){ItemDropCall("dropUnequip");ItemDropStep();}
        char script[3072];
        cstring_t format="globals\ninteger scriptedDrops=0\ninteger scriptedPickups=0\nendglobals\n"
            "function scriptedDropEvent takes nothing returns nothing\nset scriptedDrops=scriptedDrops+1\n"
            "call BJassAssert(UnitItemInSlot(dropActor,0)==dropItem,\"scripted Drop before detach\")\nendfunction\n"
            "function scriptedPickupEvent takes nothing returns nothing\nset scriptedPickups=scriptedPickups+1\n"
            "call BJassAssert(GetManipulatedItem()==dropItem,\"scripted Pickup item\")\n"
            "call BJassAssert(UnitItemInSlot(%s,0)==dropItem,\"scripted Pickup after attach\")\nendfunction\n"
            "function scriptedAddListen takes nothing returns nothing\nlocal trigger d=CreateTrigger()\nlocal trigger p=CreateTrigger()\n"
            "call TriggerRegisterPlayerUnitEvent(d,Player(0),ConvertPlayerUnitEvent(48),null)\n"
            "call TriggerRegisterPlayerUnitEvent(p,Player(0),EVENT_PLAYER_UNIT_PICKUP_ITEM,null)\n"
            "call TriggerAddAction(d,function scriptedDropEvent)\ncall TriggerAddAction(p,function scriptedPickupEvent)\nendfunction\n"
            "function scriptedAddIssue takes nothing returns nothing\n%s\n"
            "call BJassAssert(UnitAddItem(%s,dropItem),\"scripted item accepted\")\n"
            "call BJassAssert(scriptedDrops==%u,\"scripted Drop before return\")\n"
            "call BJassAssert(scriptedPickups==1,\"scripted Pickup before return\")\nendfunction\n";
        snprintf(script,sizeof(script),format,transfer ? "giveRecipient" : "dropActor",mode==2 ? "call SetItemVisible(dropItem,false)" : mode==3 ? "call SetItemDroppable(dropItem,false)" : "",transfer ? "giveRecipient" : "dropActor",transfer ? 1u : 0u);
        T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("scriptedAddListen");ItemDropCall("scriptedAddIssue");
        T_EQ(f.item->item->carrier,transfer ? target : f.actor);
        T_EQ((transfer ? target : f.actor)->inventory[0],f.item);
        ItemDropEnd(&f);
    }
}
/* V5 scene8: the Inventory completion must retain the Move authored inside
 * its synchronous Drop callback, including a saved physical approach. */
TEST(wc3_inventory_orders, point_drop_callback_retains_successor_move) {
    FOR_LOOP(reload,2) {
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point={900,1056}});
        char script[]="globals\ninteger pointDrops=0\nendglobals\n"
            "function pointDropMove takes nothing returns nothing\nset pointDrops=pointDrops+1\n"
            "call BJassAssert(GetManipulatedItem()==dropItem,\"Drop callback item\")\n"
            "call BJassAssert(UnitItemInSlot(dropActor,0)==dropItem,\"Drop callback before detach\")\n"
            "call BJassAssert(IssuePointOrder(dropActor,\"move\",512.0,1056.0),\"nested point Move\")\nendfunction\n"
            "function pointDropListen takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call TriggerRegisterPlayerUnitEvent(t,Player(0),ConvertPlayerUnitEvent(48),null)\n"
            "call TriggerAddAction(t,function pointDropMove)\nendfunction\n"
            "function pointDropCheck takes nothing returns nothing\ncall BJassAssert(pointDrops==1,\"one synchronous Drop\")\nendfunction\n";
        T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("pointDropListen");ItemDropCall("dropIssue");
        if(reload)ItemDropRestore(&f,"wc3-point-drop-successor.bin");
        for(unsigned frame=0;frame<100 && f.item->item->carrier;frame++)ItemDropStep();
        ItemDropCall("pointDropCheck");T_ASSERT(f.item->item->in_world);T_NULL(f.actor->inventory[0]);
        T_FEQ(f.item->s.origin2.x,900,0);T_FEQ(f.item->s.origin2.y,1056,0);
        T_EQ(f.actor->current_order_id,851986);T_NOT_NULL(move_unit_group(f.actor));
        float const x=f.actor->s.origin2.x;
        FOR_LOOP(frame,10)ItemDropStep();
        T_ASSERT(f.actor->s.origin2.x<x);
        ItemDropEnd(&f);
    }
}

/* V5 scene4: Drop changes the owner before the Pawn payment phase. */
TEST(wc3_inventory_orders, pawn_payment_uses_owner_after_drop_callback) {
    itemDropFixture_t f;
    ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point={800,1056}});
    if(!ItemPawnRecipient(&f,0)){ItemDropEnd(&f);return;}
    ItemData_t data=*f.item->data.ItemData;data.goldcost=800;data.lumbercost=0;f.item->data.ItemData=&data;
    uint32_t const gold0=game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD],gold1=game.clients[1].ps.stats[PLAYERSTATE_RESOURCE_GOLD];
    char script[]="function pawnChangeOwner takes nothing returns nothing\n"
        "call BJassAssert(GetManipulatedItem()==dropItem,\"Pawn Drop identity\")\n"
        "call SetUnitOwner(dropActor,Player(1),false)\nendfunction\n"
        "function pawnOwnerListen takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "call TriggerRegisterPlayerUnitEvent(t,Player(0),ConvertPlayerUnitEvent(48),null)\n"
        "call TriggerAddAction(t,function pawnChangeOwner)\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("pawnOwnerListen");ItemDropCall("giveIssue");
    for(unsigned frame=0;frame<100 && G_IsItem(f.item);frame++)ItemDropStep();
    T_EQ(f.actor->s.player,1);T_ASSERT(!G_IsItem(f.item));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD],gold0);
    T_EQ(game.clients[1].ps.stats[PLAYERSTATE_RESOURCE_GOLD],gold1+400);
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD]=gold0;game.clients[1].ps.stats[PLAYERSTATE_RESOURCE_GOLD]=gold1;
    ItemDropEnd(&f);
}

/* V3 scene32: removing the source Inventory releases items after clearing
 * their slots, while the already-owned physical task runs to completion. */
TEST(wc3_inventory_orders, source_inventory_removal_releases_item_and_retains_approach) {
    FOR_LOOP(reload,2) {
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point={1104,1056}});
        if(!ItemGiveRecipient(&f,(vec2_t){1104,1056})){ItemDropEnd(&f);continue;}
        char script[]="globals\ninteger inventoryReleaseDrops=0\nendglobals\n"
            "function inventoryReleaseEvent takes nothing returns nothing\nset inventoryReleaseDrops=inventoryReleaseDrops+1\n"
            "call BJassAssert(GetManipulatedItem()==dropItem,\"Inventory release item\")\n"
            "call BJassAssert(UnitItemInSlot(dropActor,0)==null,\"Inventory release clears slot before Drop\")\nendfunction\n"
            "function inventoryReleaseListen takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call TriggerRegisterPlayerUnitEvent(t,Player(0),ConvertPlayerUnitEvent(48),null)\n"
            "call TriggerAddAction(t,function inventoryReleaseEvent)\nendfunction\n"
            "function inventoryReleaseIssue takes nothing returns nothing\n"
            "call BJassAssert(UnitRemoveAbility(dropActor,'AInv'),\"remove source Inventory\")\n"
            "call BJassAssert(inventoryReleaseDrops==1,\"one synchronous Inventory release\")\nendfunction\n";
        T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("inventoryReleaseListen");ItemDropCall("giveIssue");
        FOR_LOOP(frame,8)ItemDropStep();
        if(reload)ItemDropRestore(&f,"wc3-inventory-before-release.bin");
        vec2_t const point=f.actor->s.origin2;
        ItemDropCall("inventoryReleaseIssue");
        T_EQ(G_InventoryCapacity(f.actor),0);T_NULL(f.actor->inventory[0]);T_NULL(f.item->item->carrier);
        T_ASSERT(f.item->item->in_world);T_ASSERT(!(f.item->s.renderfx&RF_HIDDEN));
        T_FEQ(f.item->s.origin2.x,point.x,0);T_FEQ(f.item->s.origin2.y,point.y,0);
        T_EQ(f.actor->current_order_id,852001);T_NOT_NULL(move_unit_group(f.actor));
        ItemDropRestore(&f,"wc3-inventory-after-release.bin");
        for(unsigned frame=0;frame<100 && f.actor->current_order_id;frame++)ItemDropStep();
        T_EQ(f.actor->current_order_id,0);T_NULL(move_unit_group(f.actor));
        T_NULL(f.item->item->carrier);T_ASSERT(f.item->item->in_world);
        T_FEQ(f.item->s.origin2.x,point.x,0);T_FEQ(f.item->s.origin2.y,point.y,0);
        ItemDropEnd(&f);
    }
}

/* A synchronous script callback can delete a quest powerup or rune. The
 * remaining consume/use stages must not dereference the retired item state. */
TEST(wc3_inventory_orders, scripted_powerup_callback_removal) {
    FOR_LOOP(mode,3) {
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point={900,1056}});
        ItemDropCall("dropUnequip");ItemDropStep();
        ItemData_t data=*f.item->data.ItemData;data.powerup=true;data.usable=true;
        data.abilList=mode ? "AIgo" : "_";f.item->data.ItemData=&data;
        if(mode)T_NOT_NULL(S_ItemPowerup(f.actor,f.item).ability);
        char script[1536];
        cstring_t format="function powerupRemoveEvent takes nothing returns nothing\ncall RemoveItem(GetManipulatedItem())\nendfunction\n"
            "function powerupRemoveListen takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call TriggerRegisterPlayerUnitEvent(t,Player(0),ConvertPlayerUnitEvent(%u),null)\n"
            "call TriggerAddAction(t,function powerupRemoveEvent)\nendfunction\n"
            "function powerupRemoveIssue takes nothing returns nothing\n"
            "call BJassAssert(UnitAddItem(dropActor,dropItem),\"scripted powerup accepted\")\n"
            "call BJassAssert(GetItemTypeId(dropItem)==0,\"powerup callback removed item\")\nendfunction\n";
        snprintf(script,sizeof(script),format,mode==2 ? 50u : 49u);
        T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("powerupRemoveListen");ItemDropCall("powerupRemoveIssue");
        T_ASSERT(!G_IsItem(f.item));T_NULL(f.actor->inventory[0]);
        ItemDropEnd(&f);
    }
}

/* V6 scenes0..7: mutation may originate in Pawn or Drop. Deleting the item
 * or removing the source does not cancel payment; Move remains the owner. */
TEST(wc3_inventory_orders, pawn_callback_mutation_matrix) {
    FOR_LOOP(phase,2)FOR_LOOP(mode,4) {
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point={800,1056}});
        /* Real map startup reserves destinations before any removed actor
         * leaves reusable edict holes; this fixture must do the same. */
        G_InitWaypoints();
        if(!ItemPawnRecipient(&f,0)){ItemDropEnd(&f);continue;}
        ItemData_t data=*f.item->data.ItemData;data.goldcost=800;data.lumbercost=0;f.item->data.ItemData=&data;
        uint32_t const gold0=game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD],gold1=game.clients[1].ps.stats[PLAYERSTATE_RESOURCE_GOLD];
        cstring_t mutations[]={"call SetUnitOwner(dropActor,Player(1),false)","call RemoveItem(dropItem)","call RemoveUnit(dropActor)",
            "call BJassAssert(IssuePointOrder(dropActor,\"move\",512.0,1056.0),\"nested Pawn Move\")"};
        char script[2048];
        cstring_t format="globals\nboolean pawnMutated=false\nendglobals\n"
            "function pawnMutateEvent takes nothing returns nothing\nif pawnMutated then\nreturn\nendif\nset pawnMutated=true\n%s\nendfunction\n"
            "function pawnMutateListen takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call TriggerRegisterPlayerUnitEvent(t,Player(0),ConvertPlayerUnitEvent(%u),null)\n"
            "call TriggerAddAction(t,function pawnMutateEvent)\nendfunction\n"
            "function pawnMutateCheck takes nothing returns nothing\ncall BJassAssert(pawnMutated,\"Pawn mutation executed\")\nendfunction\n";
        snprintf(script,sizeof(script),format,mutations[mode],phase ? 48u : 277u);
        T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("pawnMutateListen");ItemDropCall("giveIssue");
        for(unsigned frame=0;frame<100 && G_IsItem(f.item);frame++)ItemDropStep();
        ItemDropCall("pawnMutateCheck");T_ASSERT(!G_IsItem(f.item));T_NULL(f.actor->inventory[0]);
        T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD],gold0+(mode ? 400 : 0));
        T_EQ(game.clients[1].ps.stats[PLAYERSTATE_RESOURCE_GOLD],gold1+(mode ? 0 : 400));
        if(mode==3)T_EQ(f.actor->current_order_id,851986);
        if(mode==2)T_ASSERT(!f.actor->inuse || G_IsDeferredFree(f.actor));
        game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD]=gold0;game.clients[1].ps.stats[PLAYERSTATE_RESOURCE_GOLD]=gold1;
        ItemDropEnd(&f);
    }
}

/* This fixture selects legacy or lettered DataA-D names. Its custom
 * Inventory uses the shared DataE1 drop flag, absent from legacy Data11..14. */
static slkTestData_t *ItemDropMorphRows(bool legacy,slkTestData_t **rows) {
    char text[1536];
    cstring_t format="ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\n"
        "C;X3;K\"%s\"\nC;X4;K\"%s\"\nC;X5;K\"%s\"\nC;X6;K\"%s\"\nC;X7;K\"%s\"\n"
        "C;X8;K\"UnitID1\"\nC;X9;K\"Dur1\"\nC;X10;K\"HeroDur1\"\nC;X11;K\"BuffID1\"\n"
        "C;X1;Y2;K\"AInv\"\nC;X2;K\"AInv\"\nC;X3;K6\nC;X4;K0\nC;X5;K1\nC;X6;K1\nC;X7;K1\n"
        "C;X1;Y3;K\"AEme\"\nC;X2;K\"AEme\"\nC;X3;K\"Hpal\"\nC;X4;K1\nC;X8;K\"hfoo\"\n"
        "C;X9;K0.375\nC;X10;K60\nC;X11;K\"BEme\"\nE\n";
    snprintf(text,sizeof(text),format,legacy ? "Data11" : "DataA1",legacy ? "Data12" : "DataB1",legacy ? "Data13" : "DataC1",legacy ? "Data14" : "DataD1","DataE1");
    *rows=parse_slk_string(text);T_NOT_NULL(*rows);
    /* parse_slk_string borrows text; normalize the table before it expires. */
    return G_SetSLKRows("AbilityData",*rows);
}

/* V6 scenes9..11 admit Give while Metamorphosis retains its task, then
 * invalidate the recipient before54efe0. This remains a target-bound Drop. */
TEST(wc3_inventory_orders, queued_give_invalid_recipient_keeps_target_drop) {
    FOR_LOOP(schema,2)FOR_LOOP(mode,3)FOR_LOOP(saved,3) {
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point={1104,1056}});
        edict_t *target=ItemGiveRecipient(&f,(vec2_t){1104,1056});
        if(!target){ItemDropEnd(&f);continue;}
        slkTestData_t *rows,*old=ItemDropMorphRows(schema,&rows);T_NOT_NULL(old);
        /* Native Edem/Edmm both own Inventory. Keep that contract in the
         * synthetic Hpal/Footman pair instead of inheriting gated Aihn. */
        char invtext[]="ID;PWXL;N;E\nC;X1;Y1;K\"unitAbilID\"\nC;X2;K\"abilList\"\n"
            "C;X1;Y2;K\"Hpal\"\nC;X2;K\"AInv\"\nC;X1;Y3;K\"hfoo\"\nC;X2;K\"AInv\"\nE\n";
        slkTestData_t *invrows=parse_slk_string(invtext),*oldinv=G_SetSLKRows("UnitAbilities",invrows);
        T_NOT_NULL(invrows);T_NOT_NULL(oldinv);
        T_EQ(G_InventoryCapacity(f.actor),6);T_ASSERT(G_InventoryCanDropItems(f.actor));
        T_FEQ(G_AbilityLevel(FS_SLKKey("AEme"),1)->dur,.375f,0);
        char script[]="native UnitMakeAbilityPermanent takes unit u, boolean flag, integer code returns boolean\n"
            "function queuedGiftMorph takes nothing returns nothing\n"
            "call BJassAssert(UnitAddAbility(dropActor,'AEme'),\"attach pending Morph\")\n"
            "call BJassAssert(UnitMakeAbilityPermanent(dropActor,true,'AEme'),\"retain Morph through type change\")\n"
            "call BJassAssert(IssueImmediateOrder(dropActor,\"metamorphosis\"),\"public pending Morph\")\nendfunction\n"
            "function queuedGiftFull takes nothing returns nothing\nlocal integer i=0\nloop\nexitwhen i==6\n"
            "call BJassAssert(UnitAddItem(giveRecipient,CreateItem('ratf',1104.0,1056.0)),\"fill queued recipient\")\n"
            "set i=i+1\nendloop\nendfunction\n"
            "function queuedGiftDead takes nothing returns nothing\ncall KillUnit(giveRecipient)\nendfunction\n"
            "function queuedGiftNoInventory takes nothing returns nothing\ncall UnitRemoveAbility(giveRecipient,'AInv')\nendfunction\n";
        T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("queuedGiftMorph");
        T_ASSERT(f.actor->morph && f.actor->morph->transition.active);T_ASSERT(S_UnitOrderExecutionHeld(f.actor));
        if(!f.actor->morph || !f.actor->morph->transition.active) {
            ItemDropEnd(&f);G_SetSLKRows("AbilityData",old);G_SetSLKRows("UnitAbilities",oldinv);free_slk_rows(rows);free_slk_rows(invrows);continue;
        }
        ItemDropCall("giveIssue");T_EQ(f.actor->current_order_id,852180);T_EQ(f.actor->order_queue.count,1);
        if(f.actor->order_queue.count)T_EQ(f.actor->order_queue.entries[f.actor->order_queue.head].target_type,UNIT_ORDER_TARGET_ENTITY_ITEM);
        ItemDropCall(mode==0 ? "queuedGiftFull" : mode==1 ? "queuedGiftDead" : "queuedGiftNoInventory");
        uint32_t const number=target-g_edicts;
        if(saved==1){ItemDropRestore(&f,"wc3-queued-gift-before-activation.bin");target=g_edicts+number;}
        for(unsigned frame=0;frame<100 && f.actor->current_order_id!=852001;frame++)ItemDropStep();
        T_EQ(f.actor->current_order_id,852001);T_EQ(f.actor->order_queue.count,0);
        T_EQ(G_InventoryCapacity(f.actor),6);T_ASSERT(G_InventoryCanDropItems(f.actor));
        moveGroup_t const *group=move_unit_group(f.actor);T_NOT_NULL(group);
        if(group) {
            T_EQ(group->target,target);T_EQ(group->owner_ability,GetAbilityIndex(CAbilityInventory)+1);
            T_EQ(group->target_life,MOVE_TARGET_RETAIN_DEAD);
            /* Zero authored source hull uses physical minimum1: (50+1+32)/32. */
            T_FEQ(f.actor->collision,0,0);T_FEQ(target->collision,32,0);
            T_EQ(wc3_float_bits(group->members[0].arrival_range),0x40260000u);
        }
        T_FEQ(f.actor->item_drop_range,100,0);T_EQ(f.actor->item_drop_target_spawn_time,target->spawn_time);
        if(saved==2){ItemDropRestore(&f,"wc3-queued-gift-physical.bin");target=g_edicts+number;}
        for(unsigned frame=0;frame<150 && f.actor->current_order_id;frame++)ItemDropStep();
        T_EQ(f.actor->current_order_id,0);T_NULL(move_unit_group(f.actor));
        T_NULL(f.item->item->carrier);T_ASSERT(f.item->item->in_world);T_NULL(f.actor->inventory[0]);
        T_FEQ(f.item->s.origin2.x,1104,0);T_FEQ(f.item->s.origin2.y,1056,0);
        ItemDropEnd(&f);G_SetSLKRows("AbilityData",old);G_SetSLKRows("UnitAbilities",oldinv);free_slk_rows(rows);free_slk_rows(invrows);
    }
}

/* Ordinary pickup permission is separate from forced insertion/receipt.
 * Normalize while the borrowed SLK source is still in scope. */
static slkTestData_t *ItemDropGetRows(bool legacy,bool can_get,slkTestData_t **rows) {
    char text[1024];
    cstring_t format="ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\n"
        "C;X3;K\"%s\"\nC;X4;K\"%s\"\nC;X5;K\"%s\"\nC;X6;K\"%s\"\nC;X7;K\"DataE1\"\n"
        "C;X1;Y2;K\"AInv\"\nC;X2;K\"AInv\"\nC;X3;K6\nC;X4;K0\nC;X5;K1\nC;X6;K%u\nC;X7;K1\nE\n";
    snprintf(text,sizeof(text),format,legacy ? "Data11" : "DataA1",legacy ? "Data12" : "DataB1",legacy ? "Data13" : "DataC1",legacy ? "Data14" : "DataD1",can_get ? 1u : 0u);
    *rows=parse_slk_string(text);T_NOT_NULL(*rows);
    return G_SetSLKRows("AbilityData",*rows);
}

/* V7 toggles only inv4: Give and UnitAddItem accept with CanGetItems=false,
 * while public Smart rejects. Native getters independently confirm the flag. */
TEST(wc3_inventory_orders, give_and_script_bypass_ordinary_pickup_permission) {
    FOR_LOOP(schema,2)FOR_LOOP(can_get,2)FOR_LOOP(mode,3)FOR_LOOP(saved,2) {
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point={843,1056}});
        edict_t *target=ItemGiveRecipient(&f,(vec2_t){843,1056});
        if(!target){ItemDropEnd(&f);continue;}
        if(mode) {
            ItemDropCall("dropUnequip");
            char position[]="function canGetPosition takes nothing returns nothing\ncall SetItemPosition(dropItem,900.0,1056.0)\nendfunction\n";
            T_ASSERT(jass_dobuffer(level.vm,position));ItemDropCall("canGetPosition");
        }
        slkTestData_t *rows,*old=ItemDropGetRows(schema,can_get,&rows);T_NOT_NULL(old);
        T_EQ(G_InventoryCapacity(target),6);T_EQ(G_InventoryCanGetItems(target),can_get!=0);
        char script[1024];
        cstring_t format="function canGetScript takes nothing returns nothing\n"
            "call BJassAssert(UnitAddItem(giveRecipient,dropItem),\"forced item insertion\")\nendfunction\n"
            "function canGetSmart takes nothing returns nothing\n"
            "call BJassAssert(%sIssueTargetOrder(giveRecipient,\"smart\",dropItem),\"ordinary pickup permission\")\nendfunction\n";
        snprintf(script,sizeof(script),format,can_get ? "" : "not ");T_ASSERT(jass_dobuffer(level.vm,script));
        uint32_t const number=target-g_edicts;
        if(saved){ItemDropRestore(&f,"wc3-inventory-canget-producer.bin");target=g_edicts+number;}
        ItemDropCall(mode==0 ? "giveIssue" : mode==1 ? "canGetScript" : "canGetSmart");
        bool const receive=mode!=2 || can_get;
        if(mode==0 || mode==1)T_EQ(target->inventory[0],f.item);
        /* Smart publishes its head before the retained pickup task completes.
         * Advance the real scheduler until attachment. */
        for(unsigned frame=0;frame<100 && mode==2 && can_get && f.item->item->carrier!=target;frame++)ItemDropStep();
        T_EQ(target->current_order_id,0);
        T_EQ(f.item->item->carrier,receive ? target : NULL);T_EQ(target->inventory[0],receive ? f.item : NULL);
        T_EQ(f.item->item->in_world,!receive);
        ItemDropEnd(&f);G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    }
}

/* Native55bb10 gives ordinary Smart pickup its own target task, using the
 * authored PickupItemRange. The retained item survives both save boundaries. */
TEST(wc3_inventory_orders, pickup_authored_range_owns_target_task_through_save) {
    float const ranges[]={150,64,0};
    FOR_LOOP(setting,3)FOR_LOOP(saved,3) {
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point={1104,1056}});
        ItemDropCall("dropUnequip");
        char misc[96];snprintf(misc,sizeof(misc),"[Misc]\nPickupItemRange=%g\nDropItemRange=100\n",ranges[setting]);
        Stb_IniCacheFree(&game.config.misc);T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.misc,misc));
        char script[]="function pickupPlace takes nothing returns nothing\n"
            "call SetItemPosition(dropItem,1104.0,1056.0)\nendfunction\n"
            "function pickupIssue takes nothing returns nothing\n"
            "call BJassAssert(IssueTargetOrder(dropActor,\"smart\",dropItem),\"public pickup admitted\")\nendfunction\n";
        T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("pickupPlace");
        if(saved==1)ItemDropRestore(&f,"wc3-pickup-before-admission.bin");
        ItemDropCall("pickupIssue");
        T_EQ(f.actor->current_order_id,G_OrderId("smart"));T_EQ(f.actor->goalentity,f.item);
        T_NULL(f.item->item->carrier);T_ASSERT(f.item->item->in_world);
        moveGroup_t const *group=move_unit_group(f.actor);T_NOT_NULL(group);
        if(group) {
            T_EQ(group->target,f.item);T_EQ(group->owner_ability,GetAbilityIndex(CAbilityInventory)+1);
            T_EQ(group->receiver,f.actor);T_NOT_NULL(group->complete);
        }
        if(saved==2)ItemDropRestore(&f,"wc3-pickup-physical.bin");
        for(unsigned frame=0;frame<150 && f.actor->current_order_id==G_OrderId("smart");frame++)ItemDropStep();
        T_EQ(f.actor->current_order_id,0);T_NULL(move_unit_group(f.actor));
        /* Native zero-range far pickup reaches the raw target perimeter,
         * then rejects collection outside the independent radial predicate. */
        T_EQ(f.item->item->carrier,setting==2 ? NULL : f.actor);
        T_EQ(f.actor->inventory[0],setting==2 ? NULL : f.item);
        T_EQ(f.item->item->in_world,setting==2);
        ItemDropEnd(&f);
    }
}

/* V8b2..4 use Hpal32 plus the item's minimum1 at range+33 and its adjacent
 * quarter-unit inputs. Mobile pickup always admits an approach before receipt. */
TEST(wc3_inventory_orders, pickup_range_boundary_and_adjacent_inputs_keep_physical_owner) {
    float const ranges[]={150,64,0},offsets[]={-.25f,0,.25f};
    FOR_LOOP(setting,3)FOR_LOOP(side,3)FOR_LOOP(saved,2) {
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point={1104,1056}});
        ItemDropCall("dropUnequip");
        char misc[64];snprintf(misc,sizeof(misc),"[Misc]\nPickupItemRange=%g\n",ranges[setting]);
        Stb_IniCacheFree(&game.config.misc);T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.misc,misc));
        char script[512];snprintf(script,sizeof(script),"function pickupBoundary takes nothing returns nothing\n"
            "call SetItemPosition(dropItem,%.2f,1056.0)\n"
            "call BJassAssert(IssueTargetOrder(dropActor,\"smart\",dropItem),\"pickup boundary admitted\")\nendfunction\n",
            704+ranges[setting]+33+offsets[side]);
        T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("pickupBoundary");
        T_EQ(f.actor->current_order_id,851971);T_NULL(f.item->item->carrier);
        T_ASSERT(S_UnitTargetChaseActive(f.actor,CAbilityInventory));
        if(saved)ItemDropRestore(&f,"wc3-pickup-boundary.bin");
        for(unsigned frame=0;frame<150 && f.actor->current_order_id;frame++)ItemDropStep();
        T_EQ(f.actor->current_order_id,0);T_NULL(move_unit_group(f.actor));
        T_EQ(f.item->item->carrier,f.actor);T_EQ(f.actor->inventory[0],f.item);
        ItemDropEnd(&f);
    }
}

/*55bb10 captures approach range, but56b360 rereads live Misc at completion.
 * Widening/narrowing the terminal must not rewrite the admitted physical task. */
TEST(wc3_inventory_orders, pickup_terminal_rereads_range_without_rebinding_approach) {
    FOR_LOOP(narrow,2)FOR_LOOP(saved,2) {
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point={1104,1056}});
        ItemDropCall("dropUnequip");
        char misc[64];snprintf(misc,sizeof(misc),"[Misc]\nPickupItemRange=%u\n",narrow ? 150 : 64);
        Stb_IniCacheFree(&game.config.misc);T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.misc,misc));
        char script[]="function pickupRangeIssue takes nothing returns nothing\n"
            "call SetItemPosition(dropItem,1104.0,1056.0)\n"
            "call BJassAssert(IssueTargetOrder(dropActor,\"smart\",dropItem),\"pickup range admitted\")\nendfunction\n";
        T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("pickupRangeIssue");ItemDropStep();
        moveGroup_t const *group=move_unit_group(f.actor);T_NOT_NULL(group);
        float const approach=group ? group->members[0].arrival_range : 0;
        Stb_IniCacheFree(&game.config.misc);
        T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.misc,narrow ? "[Misc]\nPickupItemRange=0\n" : "[Misc]\nPickupItemRange=150\n"));
        if(saved)ItemDropRestore(&f,"wc3-pickup-live-range.bin");
        group=move_unit_group(f.actor);T_NOT_NULL(group);
        if(group)T_EQ(wc3_float_bits(group->members[0].arrival_range),wc3_float_bits(approach));
        T_FEQ(f.actor->item_drop_range,narrow ? 150 : 64,0);
        for(unsigned frame=0;frame<150 && f.actor->current_order_id;frame++)ItemDropStep();
        T_EQ(f.actor->current_order_id,0);T_NULL(move_unit_group(f.actor));
        T_EQ(f.item->item->carrier,narrow ? NULL : f.actor);
        T_EQ(f.item->item->in_world,narrow);
        ItemDropEnd(&f);
    }
}

/* V8 scenes5..15: constructor admission and terminal collection are separate.
 * Hiding, inventory loss and another carrier do not cancel admitted routing. */
TEST(wc3_inventory_orders, pickup_terminal_mutation_and_callback_matrix) {
    FOR_LOOP(mode,7)FOR_LOOP(saved,2) {
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point={1104,1056}});
        ItemDropCall("dropUnequip");
        Stb_IniCacheFree(&game.config.misc);
        T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.misc,"[Misc]\nPickupItemRange=150\nDropItemRange=100\n"));
        char script[2048];
        cstring_t program="function pickupEvent takes nothing returns nothing\n"
            "if GetManipulatedItem()==dropItem then\n"
            "call BJassAssert(GetUnitCurrentOrder(dropActor)==851971,\"pickup callback retains Smart head\")\n%s\nendif\nendfunction\n"
            "function pickupInit takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call TriggerRegisterUnitEvent(t,dropActor,ConvertUnitEvent(86))\n"
            "call TriggerAddAction(t,function pickupEvent)\ncall SetItemPosition(dropItem,1104.0,1056.0)\nendfunction\n"
            "function pickupIssue takes nothing returns nothing\n"
            "call BJassAssert(IssueTargetOrder(dropActor,\"smart\",dropItem),\"pickup admitted\")\nendfunction\n"
            "function pickupMutate takes nothing returns nothing\n%s\nendfunction\n";
        /* Unit pickup event86 is distinct from player-unit pickup event48. */
        cstring_t action=mode==4 ? "call BJassAssert(IssuePointOrder(dropActor,\"move\",512.0,1056.0),\"pickup callback Move\")" :
                         mode==5 ? "call RemoveItem(dropItem)" : "";
        cstring_t mutations[]={"call SetItemVisible(dropItem,false)",
            "call BJassAssert(UnitRemoveAbility(dropActor,'AInv'),\"Inventory removed\")",
            "call UnitAddItem(dropActor,CreateItem('ratf',704.0,1056.0))\n"
            "call UnitAddItem(dropActor,CreateItem('ratf',704.0,1056.0))\n"
            "call UnitAddItem(dropActor,CreateItem('ratf',704.0,1056.0))\n"
            "call UnitAddItem(dropActor,CreateItem('ratf',704.0,1056.0))\n"
            "call UnitAddItem(dropActor,CreateItem('ratf',704.0,1056.0))\n"
            "call UnitAddItem(dropActor,CreateItem('ratf',704.0,1056.0))",
            "call UnitAddItem(CreateUnit(Player(0),'Hpal',1500.0,1056.0,0.0),dropItem)","","",
            "call BJassAssert(IssueImmediateOrder(dropActor,\"stop\"),\"pickup Stop\")"};
        snprintf(script,sizeof(script),program,action,mutations[mode]);
        T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("pickupInit");ItemDropCall("pickupIssue");
        ItemDropStep();ItemDropCall("pickupMutate");
        uint32_t const itemnum=f.item-g_edicts;
        if(saved)ItemDropRestore(&f,"wc3-pickup-terminal.bin");
        for(unsigned frame=0;frame<150 && f.actor->current_order_id==851971;frame++)ItemDropStep();
        T_NE(f.actor->current_order_id,851971);T_ASSERT(!S_UnitTargetChaseActive(f.actor,CAbilityInventory));
        if(mode==0 || mode==4) {
            T_EQ(f.item->item->carrier,f.actor);T_EQ(f.actor->inventory[0],f.item);
            if(mode==4)T_EQ(f.actor->current_order_id,G_OrderId("move"));
        } else if(mode==5)T_ASSERT(!G_IsItem(g_edicts+itemnum));
        else {
            T_NE(f.item->item->carrier,f.actor);
            if(mode!=6)T_ASSERT(f.actor->s.origin2.x>850);
            if(mode!=3)T_ASSERT(f.item->item->in_world);
        }
        ItemDropEnd(&f);
    }
}

/* V8b5/6: even an immobile Inventory admits Smart and attempts its terminal
 * immediately; the authored range decides collection independently of movement. */
TEST(wc3_inventory_orders, pickup_immobile_attempts_authored_terminal_synchronously) {
    float const ranges[]={150,64,0};
    FOR_LOOP(setting,3)FOR_LOOP(far,2) {
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_BUILDING_NEAR,.range=100,.point={804,1024}});
        ItemDropCall("dropUnequip");
        char misc[96];snprintf(misc,sizeof(misc),"[Misc]\nPickupItemRange=%g\n",ranges[setting]);
        Stb_IniCacheFree(&game.config.misc);T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.misc,misc));
        char script[512];snprintf(script,sizeof(script),"function pickupFarm takes nothing returns nothing\n"
            "call SetItemPosition(dropItem,%.1f,1024.0)\n"
            "call BJassAssert(IssueTargetOrder(dropActor,\"smart\",dropItem),\"immobile pickup admitted\")\nendfunction\n",far ? 1104.0 : 804.0);
        T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("pickupFarm");
        T_EQ(f.actor->current_order_id,0);T_NULL(move_unit_group(f.actor));
        T_EQ(f.item->item->carrier,!far && setting<2 ? f.actor : NULL);
        T_EQ(f.item->item->in_world,far || setting==2);
        ItemDropEnd(&f);
    }
}

/* Pause reconstructs the pickup producer; removal retires the retained item
 * identity without assigning a replacement item at the same storage slot. */
TEST(wc3_inventory_orders, pickup_pause_and_item_removal_release_physical_owner) {
    FOR_LOOP(remove_item,2)FOR_LOOP(saved,2) {
        itemDropFixture_t f;
        ItemDropBegin(&f,(itemDropParams_t){.scene=DROP_FAR,.range=100,.point={1104,1056}});
        ItemDropCall("dropUnequip");
        Stb_IniCacheFree(&game.config.misc);
        T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.misc,"[Misc]\nPickupItemRange=150\n"));
        char script[]="function pickupBegin takes nothing returns nothing\n"
            "call SetItemPosition(dropItem,1104.0,1056.0)\n"
            "call BJassAssert(IssueTargetOrder(dropActor,\"smart\",dropItem),\"pickup admitted\")\nendfunction\n";
        T_ASSERT(jass_dobuffer(level.vm,script));ItemDropCall("pickupBegin");ItemDropStep();
        if(saved)ItemDropRestore(&f,"wc3-pickup-lifetime.bin");
        if(remove_item) {
            ItemDropCall("dropRemove");
            FOR_LOOP(frame,4)ItemDropStep();
            T_EQ(f.actor->current_order_id,0);T_NULL(move_unit_group(f.actor));
        } else {
            ItemDropCall("dropPause");vec2_t const position=f.actor->s.origin2;
            FOR_LOOP(frame,4)ItemDropStep();
            T_FEQ(f.actor->s.origin2.x,position.x,0);T_NULL(f.item->item->carrier);
            ItemDropCall("dropUnpause");
            for(unsigned frame=0;frame<150 && f.item->item->carrier!=f.actor;frame++)ItemDropStep();
            T_EQ(f.actor->current_order_id,0);T_NULL(move_unit_group(f.actor));T_EQ(f.item->item->carrier,f.actor);
        }
        ItemDropEnd(&f);
    }
}

#endif
