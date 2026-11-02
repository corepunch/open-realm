#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"
#include "jass/jass.h"
extern void reset_entities(void),setup_test_world(void);
bool run_test_jass(cstring_t);
slkTestData_t *parse_slk_string(cstring_t);
void free_slk_rows(slkTestData_t *);

typedef struct {
    mapInfo_t info;mapInfo_t const *old_info;
    slkTestData_t *rows[5],*old[5];
    uint8_t cells[64*64];
    edict_t *tree,*mine;
} entangleFixture_t;
static cstring_t const entangle_tables[]={"UnitBalance","UnitData","UnitAbilities","UnitUI","AbilityData"};
static void EntStep(void) {level.time+=FRAMETIME;globals.RunFrame();}
static void EntCall(cstring_t name) {jass_callbyname(level.vm,name,false);T_ASSERT(!jass_rterror_pending(level.vm));}
static void EntBegin(entangleFixture_t *f,float range,bool far,bool uprooted) {
    reset_entities();setup_test_world();memset(f,0,sizeof(*f));f->old_info=level.mapinfo;
    bool tft=atoi(gi.CvarString("fs_expansion","0"))!=0;
    char balance[2048],ability[4096];
    snprintf(balance,sizeof(balance),
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitBalanceID\"\nC;X2;K\"realHP\"\nC;X3;K\"spd\"\n"
        "C;X4;K\"collision\"\nC;X5;K\"bldtm\"\nC;X6;K\"%s\"\nC;X7;K\"preventPlace\"\n"
        "C;X1;Y2;K\"etol\"\nC;X2;K1300\nC;X3;K40\nC;X4;K144\nC;X6;K1\nC;X7;K\"unbuildable\"\n"
        "C;X1;Y3;K\"ngol\"\nC;X2;K150000\nC;X4;K50\nC;X6;K1\nC;X7;K\"unbuildable\"\n"
        "C;X1;Y4;K\"egol\"\nC;X2;K800\nC;X5;K120\nC;X6;K1\n"
        "C;X1;Y5;K\"hpea\"\nC;X2;K220\nC;X3;K270\nC;X4;K16\nE\n",tft ? "isbldg" : "unused");
    snprintf(ability,sizeof(ability),
        "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"Rng1\"\nC;X3;K\"%s\"\n"
        "C;X4;K\"Dur1\"\nC;X5;K\"HeroDur1\"\nC;X6;K\"%s\"\nC;X7;K\"%s\"\n"
        "C;X1;Y2;K\"Aent\"\nC;X2;K%g\nC;X3;K\"egol\"\n"
        "C;X1;Y3;K\"Aro1\"\nC;X4;K2.5\nC;X5;K2.5\n"
        "C;X1;Y4;K\"Agld\"\nC;X6;K12500\nC;X1;Y5;K\"Agl2\"\n"
        "C;X1;Y6;K\"Aegm\"\nC;X6;K10\nC;X7;K1\nE\n",
        tft ? "UnitID1" : "unused",tft ? "DataA1" : "Data11",tft ? "DataB1" : "Data12",range);
    cstring_t const text[]={balance,
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitID\"\nC;X2;K\"movetp\"\nC;X3;K\"pathTex\"\n"
        "C;X4;K\"turnRate\"\nC;X5;K\"race\"\nC;X6;K\"canBuildOn\"\nC;X7;K\"isBuildOn\"\nC;X8;K\"collision\"\nC;X9;K\"propWin\"\n"
        "C;X1;Y2;K\"etol\"\nC;X2;K\"foot\"\nC;X3;K\"PathTextures\\12x12TreeOfLife.tga\"\nC;X4;K0.4\nC;X5;K\"nightelf\"\nC;X8;K144\nC;X9;K60\n"
        "C;X1;Y3;K\"ngol\"\nC;X3;K\"PathTextures\\16x16Goldmine.tga\"\nC;X6;K1\nC;X8;K50\n"
        "C;X1;Y4;K\"egol\"\nC;X5;K\"nightelf\"\nC;X7;K1\n"
        "C;X1;Y5;K\"hpea\"\nC;X2;K\"foot\"\nC;X4;K0.6\nC;X5;K\"human\"\nC;X8;K16\nC;X9;K60\nE\n",
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitAbilID\"\nC;X2;K\"abilList\"\n"
        "C;X1;Y2;K\"etol\"\nC;X2;K\"Aent,Aro1,Argd\"\n"
        "C;X1;Y3;K\"ngol\"\nC;X2;K\"Agld\"\nC;X1;Y4;K\"egol\"\nC;X2;K\"Agl2,Aegm\"\nE\n",
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitUIID\"\nC;X2;K\"isbldg\"\nC;X3;K\"file\"\n"
        "C;X1;Y2;K\"etol\"\nC;X2;K1\nC;X3;K\"TestUI\\Models\\anim_pulse.mdx\"\n"
        "C;X1;Y3;K\"ngol\"\nC;X2;K1\nC;X3;K\"TestUI\\Models\\anim_pulse.mdx\"\n"
        "C;X1;Y4;K\"egol\"\nC;X2;K1\nC;X3;K\"TestUI\\Models\\anim_pulse.mdx\"\n"
        "C;X1;Y5;K\"hpea\"\nC;X2;K0\nC;X3;K\"TestUI\\Models\\anim_pulse.mdx\"\nE\n",ability};
    FOR_LOOP(i,5){f->rows[i]=parse_slk_string(text[i]);f->old[i]=G_SetSLKRows(entangle_tables[i],f->rows[i]);}
    f->info.fileFormat=tft ? 25 : 24;f->info.players[0].used=true;f->info.players[0].playerType=kPlayerTypeHuman;
    level.mapinfo=&f->info;game.clients[0].mapplayer=f->info.players;
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,f->cells);
    level.time=level.pathing_msec=level.pathing_phase=level.framenum=0;
    level.started=level.scriptsConfigured=level.scriptsStarted=false;
    level.pathing_due=level.pathing_owner_clock_valid=level.timer_clock_valid=false;
    level.pathing_clock=(wc3Clock_t){0,0,300};
    /* Map spawn reserves the destination ring before any removable actor. */
    G_InitWaypoints();
    char script[2600];snprintf(script,sizeof(script),
        "globals\nunit entTree\nunit entMine\nendglobals\n"
        "function entIssue takes nothing returns nothing\n"
        "call BJassAssert(IssueTargetOrder(entTree,\"entangle\",entMine),\"Entangle native admission\")\nendfunction\n"
        "function entUnroot takes nothing returns nothing\ncall BJassAssert(IssueImmediateOrder(entTree,\"unroot\"),\"Unroot before Entangle\")\nendfunction\n"
        "function entStop takes nothing returns nothing\ncall IssueImmediateOrder(entTree,\"stop\")\nendfunction\n"
        "function entRemoveMine takes nothing returns nothing\ncall RemoveUnit(entMine)\nendfunction\n"
        "function entRemoveAbility takes nothing returns nothing\ncall UnitRemoveAbility(entTree,'Aent')\nendfunction\n"
        "function entMove takes nothing returns nothing\ncall BJassAssert(IssuePointOrder(entTree,\"move\",512.0,768.0),\"replacement Move\")\nendfunction\n"
        "function entPause takes nothing returns nothing\ncall PauseUnit(entTree,true)\nendfunction\n"
        "function entUnpause takes nothing returns nothing\ncall PauseUnit(entTree,false)\nendfunction\n"
        "function main takes nothing returns nothing\nset entTree=CreateUnit(Player(0),'etol',704.0,1024.0,0.0)\n"
        "call SetUnitAcquireRange(entTree,0.0)\nset entMine=CreateUnit(Player(15),'ngol',%.1f,1024.0,0.0)\n"
        "call SetResourceAmount(entMine,5000)\nendfunction\n",far ? 1536.0 : 1216.0);
    T_ASSERT(run_test_jass(script));
    FILTER_EDICTS(e,e->inuse) {
        if(e->class_id==MAKEFOURCC('e','t','o','l'))f->tree=e;
        if(e->class_id==MAKEFOURCC('n','g','o','l'))f->mine=e;
    }
    T_NOT_NULL(f->tree);T_NOT_NULL(f->mine);T_FEQ(f->tree->collision,144,0);T_FEQ(f->mine->collision,50,0);
    T_NOT_NULL(f->tree->pathtex);T_NOT_NULL(f->mine->pathtex);
    G_FinishMovePathingInitialization();level.started=level.scriptsConfigured=level.scriptsStarted=true;
    if(uprooted) {
        EntCall("entUnroot");
        for(unsigned i=0;i<100 && S_AncientIsMorphing(f->tree);i++)EntStep();
        T_ASSERT(!S_AncientIsRooted(f->tree));T_EQ(f->tree->current_order_id,0);
    }
}
static edict_t *EntOverlay(entangleFixture_t *f) {
    FILTER_EDICTS(e,e->inuse && e->mineoverlay && e->mineoverlay->parent==f->mine)return e;
    return NULL;
}
static void EntEnd(entangleFixture_t *f) {
    level.started=false;reset_entities();setup_test_world();
    level.mapinfo=f->old_info;game.clients[0].mapplayer=f->old_info->players;
    FOR_LOOP(i,5){G_SetSLKRows(entangle_tables[i],f->old[i]);free_slk_rows(f->rows[i]);}
}
TEST(wc3_entangle_orders, public_admission_separates_placement_failure_from_order_rejection) {
    float const ranges[]={500,64,0};
    FOR_LOOP(setting,3)FOR_LOOP(mode,4) {
        entangleFixture_t f;EntBegin(&f,ranges[setting],mode&1,mode>=2);EntCall("entIssue");
        T_EQ(f.tree->current_order_id,setting==0 && mode!=0 ? G_OrderId("entangle") : 0);
        if(setting==0 && mode==0)T_NOT_NULL(EntOverlay(&f));else T_NULL(EntOverlay(&f));
        if(setting!=0)T_EQ(S_AncientIsRooted(f.tree),mode<2);
        EntEnd(&f);
    }
}
TEST(wc3_entangle_orders, retained_site_completes_through_root_and_physical_scheduler) {
    FOR_LOOP(mode,4) {
        entangleFixture_t f;EntBegin(&f,500,mode&1,mode>=2);EntCall("entIssue");
        for(unsigned i=0;i<650 && !EntOverlay(&f);i++)EntStep();
        T_NOT_NULL(EntOverlay(&f));T_ASSERT(S_AncientIsRooted(f.tree));T_EQ(f.tree->current_order_id,0);
        T_FEQ(f.tree->s.origin2.x,mode&1 ? 896 : 704,0.02f);
        T_FEQ(f.tree->s.origin2.y,mode&1 ? 960 : 1024,0.02f);
        EntEnd(&f);
    }
}

static void EntRestore(entangleFixture_t *f,cstring_t name) {
    uint32_t tree=f->tree-g_edicts,mine=f->mine-g_edicts;
    cstring_t path=Test_TempPath(name);T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);
    f->tree=g_edicts+tree;f->mine=g_edicts+mine;
}

/* Cold snapshots retain both the parent item-free target identity and Root's
 * physical callback. No saved process pointer or public reissue reconstructs it. */
TEST(wc3_entangle_orders, unroot_approach_and_root_callbacks_round_trip) {
    FOR_LOOP(setting,2)FOR_LOOP(stage,3) {
        entangleFixture_t f;EntBegin(&f,setting ? 333 : 500,true,false);EntCall("entIssue");
        entanglePhase_t const phase=stage==0 ? ENTANGLE_UNROOTING : stage==1 ? ENTANGLE_APPROACH : ENTANGLE_ROOTING;
        for(unsigned i=0;i<500 && f.tree->entangle && f.tree->entangle->phase!=phase;i++)EntStep();
        T_NOT_NULL(f.tree->entangle);
        if(f.tree->entangle) {
            T_EQ(f.tree->entangle->phase,phase);T_EQ(f.tree->entangle->mine,f.mine);
            T_EQ(f.tree->entangle->mine_spawn_time,f.mine->spawn_time);
        }
        EntRestore(&f,"wc3-entangle-phases.bin");
        T_EQ(f.tree->current_order_id,G_OrderId("entangle"));
        T_NOT_NULL(f.tree->entangle);T_EQ(f.tree->entangle->mine,f.mine);
        T_EQ(f.tree->entangle->phase,phase);
        if(stage!=1)T_EQ(f.tree->ancient_root->complete,S_EntangleRootComplete);
        else {
            moveGroup_t const *group=move_unit_group(f.tree);T_NOT_NULL(group);
            if(group){T_EQ(group->complete,S_EntangleApproachComplete);T_EQ(group->receiver,f.tree);}
        }
        for(unsigned i=0;i<650 && !EntOverlay(&f);i++)EntStep();
        T_NOT_NULL(EntOverlay(&f));T_EQ(f.tree->current_order_id,0);T_ASSERT(S_AncientIsRooted(f.tree));
        T_EQ(f.tree->entangle->phase,ENTANGLE_NONE);T_NULL(f.tree->entangle->mine);
        T_NULL(f.tree->ancient_root->complete);T_NULL(move_unit_group(f.tree));
        T_FEQ(f.tree->s.origin2.x,setting ? 1024 : 896,.02f);T_FEQ(f.tree->s.origin2.y,960,.02f);
        EntEnd(&f);
    }
}

TEST(wc3_entangle_orders, stopped_approach_releases_parent_and_saved_root_callback) {
    FOR_LOOP(saved,2) {
        entangleFixture_t f;EntBegin(&f,500,true,true);EntCall("entIssue");
        T_EQ(f.tree->entangle->phase,ENTANGLE_APPROACH);T_NOT_NULL(move_unit_group(f.tree));
        FOR_LOOP(i,3)EntStep();
        if(saved)EntRestore(&f,"wc3-entangle-stop.bin");
        EntCall("entStop");FOR_LOOP(i,80)EntStep();
        T_EQ(f.tree->current_order_id,0);T_NULL(EntOverlay(&f));T_NULL(move_unit_group(f.tree));
        T_EQ(f.tree->entangle->phase,ENTANGLE_NONE);T_NULL(f.tree->entangle->mine);
        T_NULL(f.tree->ancient_root->complete);T_ASSERT(!S_AncientIsRooted(f.tree));
        EntEnd(&f);
    }
}

TEST(wc3_entangle_orders, target_and_ability_loss_retire_each_retained_phase) {
    FOR_LOOP(stage,3)FOR_LOOP(ability_loss,2)FOR_LOOP(saved,2) {
        entangleFixture_t f;EntBegin(&f,500,true,false);EntCall("entIssue");
        entanglePhase_t phase=stage==0 ? ENTANGLE_UNROOTING : stage==1 ? ENTANGLE_APPROACH : ENTANGLE_ROOTING;
        for(unsigned i=0;i<500 && f.tree->entangle->phase!=phase;i++)EntStep();
        T_EQ(f.tree->entangle->phase,phase);
        if(saved)EntRestore(&f,"wc3-entangle-loss.bin");
        EntCall(ability_loss ? "entRemoveAbility" : "entRemoveMine");
        if(stage==1) {
            EntStep();T_EQ(f.tree->entangle->phase,ENTANGLE_APPROACH);
            T_EQ(f.tree->current_order_id,G_OrderId("entangle"));T_NOT_NULL(move_unit_group(f.tree));
        }
        FOR_LOOP(i,100)EntStep();
        T_EQ(f.tree->current_order_id,0);T_NULL(move_unit_group(f.tree));T_NULL(EntOverlay(&f));
        T_EQ(f.tree->entangle->phase,ENTANGLE_NONE);T_NULL(f.tree->entangle->mine);
        T_NULL(f.tree->ancient_root->complete);T_ASSERT(!f.tree->ancient_root->displace_occupants);
        EntEnd(&f);
    }
}

TEST(wc3_entangle_orders, replacement_move_survives_cancelled_point_receiver) {
    FOR_LOOP(saved,2) {
        entangleFixture_t f;EntBegin(&f,500,true,true);EntCall("entIssue");
        FOR_LOOP(i,3)EntStep();
        if(saved)EntRestore(&f,"wc3-entangle-replace.bin");
        EntCall("entMove");
        T_EQ(f.tree->current_order_id,G_OrderId("move"));T_NULL(EntOverlay(&f));
        T_EQ(f.tree->entangle->phase,ENTANGLE_NONE);T_NULL(f.tree->entangle->mine);
        T_NOT_NULL(f.tree->goalentity);
        for(unsigned i=0;i<100 && f.tree->s.origin2.x>=700;i++)EntStep();
        T_EQ(f.tree->current_order_id,G_OrderId("move"));T_ASSERT(f.tree->s.origin2.x<704);
        EntEnd(&f);
    }
}

TEST(wc3_entangle_orders, paused_approach_retains_parent_until_public_resume) {
    FOR_LOOP(saved,2) {
        entangleFixture_t f;EntBegin(&f,500,true,true);EntCall("entIssue");
        FOR_LOOP(i,3)EntStep();
        EntCall("entPause");
        vec2_t point=f.tree->s.origin2;
        if(saved)EntRestore(&f,"wc3-entangle-pause.bin");
        FOR_LOOP(i,40)EntStep();
        T_FEQ(f.tree->s.origin2.x,point.x,0);T_FEQ(f.tree->s.origin2.y,point.y,0);T_NULL(EntOverlay(&f));
        EntCall("entUnpause");
        for(unsigned i=0;i<650 && !EntOverlay(&f);i++)EntStep();
        T_NOT_NULL(EntOverlay(&f));T_EQ(f.tree->current_order_id,0);T_NULL(move_unit_group(f.tree));
        EntEnd(&f);
    }
}

/* V4 scenes0..2: friendly construction occupants remain displaceable; the
 * hostile unit rejects the same root site and changes the captured point. */
TEST(wc3_entangle_orders, authored_range_and_occupant_relationship_choose_captured_root_site) {
    FOR_LOOP(mode,6) {
        unsigned owner=mode%3;
        entangleFixture_t f;EntBegin(&f,333,mode<3,mode>=3);
        if(owner) {
            char script[512];snprintf(script,sizeof(script),"function entBlock takes nothing returns nothing\n"
                "local unit u=CreateUnit(Player(%u),'hpea',1024.0,960.0,0.0)\n"
                "%s\nendfunction\n",owner-1,mode>=3 ? "call SetUnitX(u,704.0)\ncall SetUnitY(u,1024.0)" : "");
            T_ASSERT(jass_dobuffer(level.vm,script));EntCall("entBlock");
        }
        EntCall("entIssue");T_NOT_NULL(f.tree->entangle);
        vec2_t const sites[]={{1024,960},{1024,1024},{1088,1216},{704,1024},{768,832},{768,768}};
        vec2_t expected=sites[mode];
        if(f.tree->entangle) {
            T_FEQ(f.tree->entangle->destination.x,expected.x,0);T_FEQ(f.tree->entangle->destination.y,expected.y,0);
        }
        for(unsigned i=0;i<900 && !EntOverlay(&f);i++)EntStep();
        T_NOT_NULL(EntOverlay(&f));T_FEQ(f.tree->s.origin2.x,expected.x,.02f);T_FEQ(f.tree->s.origin2.y,expected.y,.02f);
        EntEnd(&f);
    }
}

/* V6/V7: native placement publishes the occupant Move before Unroot. The
 * half-second Root retries cannot consume that independent public head. */
TEST(wc3_entangle_orders, friendly_occupant_move_precedes_unroot_and_slow_clearance_fails_root) {
    FOR_LOOP(slow,2) {
        entangleFixture_t f;EntBegin(&f,333,true,false);
        char script[512];snprintf(script,sizeof(script),"globals\nunit entWorker\nendglobals\n"
            "function entWorkerCreate takes nothing returns nothing\n"
            "set entWorker=CreateUnit(Player(0),'hpea',1024.0,960.0,0.0)\n"
            "call SetUnitAcquireRange(entWorker,0.0)\n"
            "call SetUnitMoveSpeed(entWorker,%u.0)\nendfunction\n",slow ? 1 : 270);
        T_ASSERT(jass_dobuffer(level.vm,script));EntCall("entWorkerCreate");
        edict_t *worker=NULL;FILTER_EDICTS(e,e->inuse && e->class_id==MAKEFOURCC('h','p','e','a'))worker=e;
        T_NOT_NULL(worker);EntCall("entIssue");
        T_EQ(worker->current_order_id,G_OrderId("move"));
        T_EQ(f.tree->current_order_id,G_OrderId("entangle"));
        for(unsigned i=0;i<900 && f.tree->current_order_id;i++)EntStep();
        if(slow) {
            T_NULL(EntOverlay(&f));T_EQ(f.tree->current_order_id,0);
            T_ASSERT(!S_AncientIsRooted(f.tree));T_EQ(worker->current_order_id,G_OrderId("move"));
        } else T_NOT_NULL(EntOverlay(&f));
        EntEnd(&f);
    }
}

TEST(wc3_entangle_orders, pending_root_retry_round_trips_and_stop_releases_its_timer) {
    FOR_LOOP(stop,2) {
        entangleFixture_t f;EntBegin(&f,333,true,false);
        char slow_script[]="globals\nunit entWorker\nendglobals\n"
            "function entSlow takes nothing returns nothing\n"
            "set entWorker=CreateUnit(Player(0),'hpea',1024.0,960.0,0.0)\n"
            "call SetUnitAcquireRange(entWorker,0.0)\ncall SetUnitMoveSpeed(entWorker,1.0)\nendfunction\n";
        T_ASSERT(jass_dobuffer(level.vm,slow_script));
        EntCall("entSlow");EntCall("entIssue");
        for(unsigned i=0;i<900 && !f.tree->ancient_root->retry.active;i++)EntStep();
        T_ASSERT(f.tree->ancient_root->retry.active);T_EQ(f.tree->ancient_root->placement_retries,1);
        abilityPrimaryTimer_t before=f.tree->ancient_root->retry;
        EntRestore(&f,"wc3-entangle-root-retry.bin");
        T_ASSERT(f.tree->ancient_root->retry.active);T_EQ(f.tree->ancient_root->placement_retries,1);
        T_FEQ(f.tree->ancient_root->retry.deadline.time,before.deadline.time,0);
        T_EQ(f.tree->ancient_root->retry.sequence,before.sequence);
        if(stop) {
            EntCall("entStop");T_ASSERT(!f.tree->ancient_root->retry.active);
            T_NULL(f.tree->ancient_root->complete);
        }
        unsigned high=f.tree->ancient_root->placement_retries;
        for(unsigned i=0;i<65;i++) {EntStep();high=MAX(high,f.tree->ancient_root->placement_retries);}
        if(!stop)T_EQ(high,10);
        T_ASSERT(!f.tree->ancient_root->retry.active);T_EQ(f.tree->current_order_id,0);
        T_NULL(EntOverlay(&f));T_ASSERT(!S_AncientIsRooted(f.tree));
        EntEnd(&f);
    }
}

TEST(wc3_entangle_orders, occupant_point_callbacks_can_replace_or_remove_parent) {
    FOR_LOOP(mode,3) {
        entangleFixture_t f;EntBegin(&f,333,true,false);
        char script[1400];snprintf(script,sizeof(script),"globals\nunit entWorker\nendglobals\n"
            "function entOccupant takes nothing returns nothing\n"
            "set entWorker=CreateUnit(Player(0),'hpea',1024.0,960.0,0.0)\n"
            "call SetUnitAcquireRange(entWorker,0.0)\ncall SetUnitMoveSpeed(entWorker,1.0)\nendfunction\n"
            "function entMutateParent takes nothing returns nothing\n%s\nendfunction\n"
            "function entObserveOccupant takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call TriggerRegisterUnitEvent(t,entWorker,EVENT_UNIT_ISSUED_POINT_ORDER)\n"
            "call TriggerAddAction(t,function entMutateParent)\nendfunction\n"
            "function entStopOccupant takes nothing returns nothing\ncall IssueImmediateOrder(entWorker,\"stop\")\nendfunction\n",
            mode==1 ? "call RemoveUnit(entTree)" : "call IssueImmediateOrder(entTree,\"stop\")");
        T_ASSERT(jass_dobuffer(level.vm,script));EntCall("entOccupant");
        if(mode<2)EntCall("entObserveOccupant");
        EntCall("entIssue");
        if(mode==2) {
            for(unsigned i=0;i<900 && !f.tree->ancient_root->retry.active;i++)EntStep();
            T_ASSERT(f.tree->ancient_root->retry.active);
            EntCall("entObserveOccupant");EntCall("entStopOccupant");
            for(unsigned i=0;i<10 && f.tree->current_order_id;i++)EntStep();
        }
        if(mode==1)T_ASSERT(!f.tree->inuse || G_IsDeferredFree(f.tree));
        else {
            T_EQ(f.tree->current_order_id,0);
            T_ASSERT(!f.tree->entangle || f.tree->entangle->phase==ENTANGLE_NONE);
            T_ASSERT(!f.tree->ancient_root || !f.tree->ancient_root->retry.active);
        }
        T_NULL(EntOverlay(&f));
        EntEnd(&f);
    }
}

TEST(wc3_entangle_orders, adjacent_authored_ranges_choose_direct_or_retained_root_site) {
    float const ranges[]={317.75f,318,318.25f};
    FOR_LOOP(setting,3)FOR_LOOP(uprooted,2) {
        entangleFixture_t f;EntBegin(&f,ranges[setting],false,uprooted!=0);EntCall("entIssue");
        bool direct=setting!=0 && !uprooted;
        T_EQ(f.tree->current_order_id,direct ? 0 : G_OrderId("entangle"));
        if(direct)T_NOT_NULL(EntOverlay(&f));else T_NULL(EntOverlay(&f));
        for(unsigned i=0;i<650 && !EntOverlay(&f);i++)EntStep();
        T_NOT_NULL(EntOverlay(&f));T_EQ(f.tree->current_order_id,0);T_ASSERT(S_AncientIsRooted(f.tree));
        T_FEQ(f.tree->s.origin2.x,setting ? 704 : 768,.02f);
        T_FEQ(f.tree->s.origin2.y,setting ? 1024 : 960,.02f);
        EntEnd(&f);
    }
}

TEST(wc3_entangle_orders, occupant_root_admission_callback_preserves_replacement_entangle) {
    entangleFixture_t f;EntBegin(&f,333,true,false);
    char script[]="globals\nunit entWorker\nboolean entReplaced=false\nendglobals\n"
        "function entOccupant takes nothing returns nothing\n"
        "set entWorker=CreateUnit(Player(0),'hpea',1024.0,960.0,0.0)\n"
        "call SetUnitAcquireRange(entWorker,0.0)\ncall SetUnitMoveSpeed(entWorker,1.0)\nendfunction\n"
        "function entReplaceParent takes nothing returns nothing\n"
        "call DisableTrigger(GetTriggeringTrigger())\nset entReplaced=true\n"
        "call BJassAssert(IssueTargetOrder(entTree,\"entangle\",entMine),\"replacement Entangle admission\")\n"
        "call SetUnitMoveSpeed(entWorker,270.0)\nendfunction\n"
        "function entObserveOccupant takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "call TriggerRegisterUnitEvent(t,entWorker,EVENT_UNIT_ISSUED_POINT_ORDER)\n"
        "call TriggerAddAction(t,function entReplaceParent)\n"
        "call IssueImmediateOrder(entWorker,\"stop\")\nendfunction\n"
        "function entCheckReplacement takes nothing returns nothing\n"
        "call BJassAssert(entReplaced,\"Root admission occupant callback fired\")\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.vm,script));EntCall("entOccupant");EntCall("entIssue");
    EntCall("entObserveOccupant");
    for(unsigned i=0;i<900 && !EntOverlay(&f);i++)EntStep();
    EntCall("entCheckReplacement");
    T_NOT_NULL(EntOverlay(&f));T_ASSERT(S_AncientIsRooted(f.tree));
    T_EQ(f.tree->current_order_id,0);
    EntEnd(&f);
}

/* Admission precedes issued-target callbacks. A removed mine rejects only
 * this constructor; it must not retain its public head or erase a successor. */
TEST(wc3_entangle_orders, issued_target_mine_removal_retires_packet_and_preserves_successor) {
    FOR_LOOP(replace,2) {
        entangleFixture_t f;EntBegin(&f,333,true,true);EntCall("entMove");
        char script[1200];snprintf(script,sizeof(script),
            "function entInvalidate takes nothing returns nothing\n"
            "call RemoveUnit(entMine)\n%s\nendfunction\n"
            "function entObserve takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call TriggerRegisterUnitEvent(t,entTree,EVENT_UNIT_ISSUED_TARGET_ORDER)\n"
            "call TriggerAddAction(t,function entInvalidate)\nendfunction\n",
            replace ? "call BJassAssert(IssuePointOrder(entTree,\"move\",512.0,768.0),\"callback successor\")" : "");
        T_ASSERT(jass_dobuffer(level.vm,script));EntCall("entObserve");EntCall("entIssue");
        T_EQ(f.tree->current_order_id,replace ? G_OrderId("move") : 0);
        T_ASSERT(f.tree->currentmove && f.tree->currentmove->think);
        FOR_LOOP(i,200)EntStep();
        T_EQ(f.tree->current_order_id,0);T_NULL(EntOverlay(&f));
        T_ASSERT(!f.tree->entangle || !f.tree->entangle->phase);
        if(replace)T_ASSERT(f.tree->s.origin2.x<600 && f.tree->s.origin2.y<850);
        EntEnd(&f);
    }
}

TEST(wc3_entangle_orders, queued_removed_mine_advances_to_following_move) {
    entangleFixture_t f;EntBegin(&f,333,true,true);
    vec2_t first={704,1152},last={512,768};
    T_ASSERT(G_IssueUnitPointOrder(f.tree,"move",&first,false,0,0));
    T_ASSERT(G_IssueUnitTargetOrder(f.tree,"entangle",f.mine,true,0));
    T_ASSERT(G_IssueUnitPointOrder(f.tree,"move",&last,true,0,0));
    EntCall("entRemoveMine");
    FOR_LOOP(i,250)EntStep();
    T_EQ(f.tree->current_order_id,0);T_NULL(EntOverlay(&f));
    T_ASSERT(f.tree->s.origin2.x<600 && f.tree->s.origin2.y<850);
    T_ASSERT(!f.tree->entangle || !f.tree->entangle->phase);
    EntEnd(&f);
}

#endif
