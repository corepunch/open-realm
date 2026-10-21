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
    edict_t *worker,*hall;
} militiaOrderFixture_t;
static cstring_t const militia_order_tables[]={"UnitBalance","UnitData","UnitAbilities","UnitUI","AbilityData"};
static void MilitiaOrderStep(void) {level.time+=FRAMETIME;globals.RunFrame();}
static void MilitiaOrderCall(cstring_t name) {jass_callbyname(level.vm,name,false);T_ASSERT(!jass_rterror_pending(level.vm));}
static void MilitiaOrderBegin(militiaOrderFixture_t *f,bool alternate,vec2_t worker,vec2_t hall,float facing,float range) {
    reset_entities();setup_test_world();memset(f,0,sizeof(*f));f->old_info=level.mapinfo;
    bool tft=atoi(gi.CvarString("fs_expansion","0"))!=0;
    char balance[1500],ability[1800];
    snprintf(balance,sizeof(balance),
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitBalanceID\"\nC;X2;K\"realHP\"\nC;X3;K\"spd\"\n"
        "C;X4;K\"collision\"\nC;X5;K\"%s\"\n"
        "C;X1;Y2;K\"hpea\"\nC;X2;K220\nC;X3;K270\nC;X4;K16\n"
        "C;X1;Y3;K\"hmil\"\nC;X2;K220\nC;X3;K270\nC;X4;K16\n"
        "C;X1;Y4;K\"htow\"\nC;X2;K1500\nC;X4;K144\nC;X5;K1\nE\n",tft ? "isbldg" : "unused");
    snprintf(ability,sizeof(ability),
        "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
        "C;X4;K\"%s\"\nC;X5;K\"Rng1\"\nC;X6;K\"Dur1\"\nC;X7;K\"Area1\"\nC;X8;K\"levels\"\nC;X9;K\"Rng2\"\n"
        "C;X1;Y2;K\"Amil\"\nC;X2;K\"Amil\"\nC;X3;K\"hpea\"\nC;X4;K\"hmil\"\nC;X6;K60\n"
        "C;X1;Y3;K\"Amic\"\nC;X2;K\"Amic\"\nC;X5;K%g\nC;X7;K600\nC;X8;K2\nC;X9;K37\nE\n",
        tft ? "DataA1" : "Data11",tft ? "DataB1" : "Data12",range);
    cstring_t const text[]={balance,
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitID\"\nC;X2;K\"movetp\"\nC;X3;K\"turnRate\"\n"
        "C;X4;K\"race\"\nC;X5;K\"collision\"\nC;X6;K\"propWin\"\n"
        "C;X1;Y2;K\"hpea\"\nC;X2;K\"foot\"\nC;X3;K0.6\nC;X4;K\"human\"\nC;X5;K16\nC;X6;K60\n"
        "C;X1;Y3;K\"hmil\"\nC;X2;K\"foot\"\nC;X3;K0.6\nC;X4;K\"human\"\nC;X5;K16\nC;X6;K60\n"
        "C;X1;Y4;K\"htow\"\nC;X4;K\"human\"\nC;X5;K144\nE\n",
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitAbilID\"\nC;X2;K\"abilList\"\n"
        "C;X1;Y2;K\"hpea\"\nC;X2;K\"Amil\"\nC;X1;Y3;K\"hmil\"\nC;X2;K\"Amil\"\n"
        "C;X1;Y4;K\"htow\"\nC;X2;K\"Amic\"\nE\n",
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitUIID\"\nC;X2;K\"isbldg\"\nC;X3;K\"file\"\n"
        "C;X1;Y2;K\"hpea\"\nC;X2;K0\nC;X3;K\"TestUI\\Models\\anim_pulse.mdx\"\n"
        "C;X1;Y3;K\"hmil\"\nC;X2;K0\nC;X3;K\"TestUI\\Models\\anim_pulse.mdx\"\n"
        "C;X1;Y4;K\"htow\"\nC;X2;K1\nC;X3;K\"TestUI\\Models\\anim_pulse.mdx\"\nE\n",ability};
    FOR_LOOP(i,5){f->rows[i]=parse_slk_string(text[i]);f->old[i]=G_SetSLKRows(militia_order_tables[i],f->rows[i]);}
    f->info.fileFormat=tft ? 25 : 24;f->info.players[0].used=true;f->info.players[0].playerType=kPlayerTypeHuman;
    level.mapinfo=&f->info;game.clients[0].mapplayer=f->info.players;
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,f->cells);
    level.time=level.pathing_msec=level.pathing_phase=level.framenum=0;
    level.started=level.scriptsConfigured=level.scriptsStarted=false;
    level.pathing_due=level.pathing_owner_clock_valid=level.timer_clock_valid=false;
    level.pathing_clock=(wc3Clock_t){0,0,300};G_InitWaypoints();
    char script[1000];snprintf(script,sizeof(script),
        "globals\nunit milWorker\nunit milHall\nendglobals\n"
        "function main takes nothing returns nothing\nset milWorker=CreateUnit(Player(0),'%s',%g,%g,%g)\n"
        "call SetUnitAcquireRange(milWorker,0.0)\nset milHall=CreateUnit(Player(0),'htow',%g,%g,0.0)\nendfunction\n",
        alternate ? "hmil" : "hpea",worker.x,worker.y,facing,hall.x,hall.y);
    T_ASSERT(run_test_jass(script));
    FILTER_EDICTS(e,e->inuse) {
        if(e->class_id==MAKEFOURCC('h','p','e','a') || e->class_id==MAKEFOURCC('h','m','i','l'))f->worker=e;
        if(e->class_id==MAKEFOURCC('h','t','o','w'))f->hall=e;
    }
    T_NOT_NULL(f->worker);T_NOT_NULL(f->hall);
    G_FinishMovePathingInitialization();level.started=level.scriptsConfigured=level.scriptsStarted=true;
}
static void MilitiaOrderEnd(militiaOrderFixture_t *f) {
    level.started=false;reset_entities();setup_test_world();
    level.mapinfo=f->old_info;game.clients[0].mapplayer=f->old_info->players;
    FOR_LOOP(i,5){G_SetSLKRows(militia_order_tables[i],f->old[i]);free_slk_rows(f->rows[i]);}
}

/* Native target admission distinguishes pairing orders from the Hall's
 * immediate militia/militiaoff commands. These identifiers are not aliases. */
TEST(wc3_militia_orders, public_target_names_and_numeric_ids_select_pairing_orders) {
    FOR_LOOP(returning,2)FOR_LOOP(numeric,2)FOR_LOOP(correct,2) {
        militiaOrderFixture_t f;
        MilitiaOrderBegin(&f,returning!=0,(vec2_t){512,512},(vec2_t){1024,512},0,37);
        uint32_t original=f.worker->class_id;
        cstring_t name=correct ? (returning ? "militiaunconvert" : "militiaconvert") :
            (returning ? "militiaoff" : "militia");
        uint32_t id=correct ? (returning ? 852651 : 852071) : (returning ? 852073 : 852072);
        char call[160],script[500];
        if(numeric)snprintf(call,sizeof(call),"IssueTargetOrderById(milWorker,%u,milHall)",id);
        else snprintf(call,sizeof(call),"IssueTargetOrder(milWorker,\"%s\",milHall)",name);
        snprintf(script,sizeof(script),"function milIssue takes nothing returns nothing\n"
            "call BJassAssert(%s%s,\"Militia target order identity\")\nendfunction\n",correct ? "" : "not ",call);
        T_ASSERT(jass_dobuffer(level.vm,script));MilitiaOrderCall("milIssue");
        T_EQ(f.worker->class_id,original);
        T_EQ(f.worker->current_order_id,correct ? id : 0);
        MilitiaOrderEnd(&f);
    }
}

/* Corrected native V1 scenes2/5: range333 admits the near point, but a
 * facing-away worker retains its form and public target head until it turns. */
TEST(wc3_militia_orders, in_range_facing_away_retains_pairing_until_turn_completion) {
    militiaOrderFixture_t f;
    MilitiaOrderBegin(&f,false,(vec2_t){1152,1024},(vec2_t){1536,1024},180,333);
    char script[]="function milForward takes nothing returns nothing\n"
        "call BJassAssert(IssueTargetOrder(milWorker,\"militiaconvert\",milHall),\"forward pairing\")\nendfunction\n"
        "function milReverse takes nothing returns nothing\ncall SetUnitFacing(milWorker,180.0)\n"
        "call BJassAssert(IssueTargetOrder(milWorker,\"militiaunconvert\",milHall),\"reverse pairing\")\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.vm,script));
    FOR_LOOP(returning,2) {
        uint32_t before=returning ? MAKEFOURCC('h','m','i','l') : MAKEFOURCC('h','p','e','a');
        uint32_t after=returning ? MAKEFOURCC('h','p','e','a') : MAKEFOURCC('h','m','i','l');
        MilitiaOrderCall(returning ? "milReverse" : "milForward");
        T_EQ(f.worker->class_id,before);T_EQ(f.worker->current_order_id,returning ? 852651 : 852071);
        T_FEQ(f.worker->s.origin2.x,1152,.01f);T_FEQ(f.worker->s.origin2.y,1024,.01f);
        for(unsigned i=0;i<100 && f.worker->class_id!=after;i++)MilitiaOrderStep();
        T_EQ(f.worker->class_id,after);T_EQ(f.worker->current_order_id,0);
        T_FEQ(f.worker->s.origin2.x,1152,.01f);T_FEQ(f.worker->s.origin2.y,1024,.01f);
    }
    MilitiaOrderEnd(&f);
}


static void MilitiaOrderRestore(militiaOrderFixture_t *f) {
    uint32_t worker=f->worker-g_edicts,hall=f->hall-g_edicts;
    cstring_t path=Test_TempPath("wc3-militia-pairing.bin");
    T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);
    f->worker=g_edicts+worker;f->hall=g_edicts+hall;
}

TEST(wc3_militia_orders, aligned_near_pairing_finishes_synchronously) {
    FOR_LOOP(returning,2) {
        militiaOrderFixture_t f;
        MilitiaOrderBegin(&f,returning!=0,(vec2_t){1152,1024},(vec2_t){1536,1024},0,333);
        char script[400];snprintf(script,sizeof(script),"function milIssue takes nothing returns nothing\n"
            "call BJassAssert(IssueTargetOrder(milWorker,\"%s\",milHall),\"aligned pairing\")\nendfunction\n",
            returning ? "militiaunconvert" : "militiaconvert");
        T_ASSERT(jass_dobuffer(level.vm,script));MilitiaOrderCall("milIssue");
        T_EQ(f.worker->class_id,returning ? MAKEFOURCC('h','p','e','a') : MAKEFOURCC('h','m','i','l'));
        T_EQ(f.worker->current_order_id,0);
        MilitiaOrderEnd(&f);
    }
}

/* Native lifecycle V1: physical approach survives either pairing ability's
 * removal, while removing the Hall ends it before further translation. */
TEST(wc3_militia_orders, public_interruptions_and_saved_approaches_keep_distinct_ownership) {
    FOR_LOOP(saved,2)FOR_LOOP(mode,7) {
        militiaOrderFixture_t f;
        MilitiaOrderBegin(&f,false,(vec2_t){704,1024},(vec2_t){1536,1024},0,333);
        cstring_t actions[]={"", "call IssueImmediateOrder(milWorker,\"stop\")",
            "call IssuePointOrder(milWorker,\"move\",512.0,768.0)","call RemoveUnit(milHall)",
            "call UnitRemoveAbility(milHall,'Amic')","call UnitRemoveAbility(milWorker,'Amil')",
            "call PauseUnit(milWorker,true)"};
        char script[1200];snprintf(script,sizeof(script),
            "function milIssue takes nothing returns nothing\n"
            "call BJassAssert(IssueTargetOrder(milWorker,\"militiaconvert\",milHall),\"lifecycle pairing\")\nendfunction\n"
            "function milInterrupt takes nothing returns nothing\n%s\nendfunction\n"
            "function milResume takes nothing returns nothing\ncall PauseUnit(milWorker,false)\nendfunction\n",actions[mode]);
        T_ASSERT(jass_dobuffer(level.vm,script));MilitiaOrderCall("milIssue");
        FOR_LOOP(i,3)MilitiaOrderStep();
        T_EQ(f.worker->current_order_id,852071);T_EQ(f.worker->class_id,MAKEFOURCC('h','p','e','a'));
        vec2_t interrupted=f.worker->s.origin2;
        MilitiaOrderCall("milInterrupt");
        if(mode==3 || mode==4 || mode==5)T_EQ(f.worker->current_order_id,852071);
        /* Removal is reclaimed by the scheduler before a snapshot. */
        if(mode==3)MilitiaOrderStep();
        if(saved)MilitiaOrderRestore(&f);
        if(mode==6) {
            FOR_LOOP(i,20)MilitiaOrderStep();
            T_FEQ(f.worker->s.origin2.x,interrupted.x,.01f);T_FEQ(f.worker->s.origin2.y,interrupted.y,.01f);
            T_EQ(f.worker->class_id,MAKEFOURCC('h','p','e','a'));MilitiaOrderCall("milResume");
        }
        FOR_LOOP(i,150)MilitiaOrderStep();
        T_EQ(f.worker->class_id,(mode==0 || mode==6) ? MAKEFOURCC('h','m','i','l') : MAKEFOURCC('h','p','e','a'));
        T_EQ(f.worker->current_order_id,0);
        if(mode==1 || mode==3) {
            T_FEQ(f.worker->s.origin2.x,interrupted.x,.1f);T_FEQ(f.worker->s.origin2.y,interrupted.y,.1f);
        }
        if(mode==2)T_ASSERT(f.worker->s.origin2.x<600 && f.worker->s.origin2.y<850);
        if(mode==4 || mode==5)T_ASSERT(f.worker->s.origin2.x>interrupted.x+32);
        T_ASSERT(!f.worker->militia || !f.worker->militia->partner);
        MilitiaOrderEnd(&f);
    }
}

TEST(wc3_militia_orders, facing_only_continuation_round_trips) {
    FOR_LOOP(returning,2) {
        militiaOrderFixture_t f;
        MilitiaOrderBegin(&f,returning!=0,(vec2_t){1152,1024},(vec2_t){1536,1024},180,333);
        char script[400];snprintf(script,sizeof(script),"function milIssue takes nothing returns nothing\n"
            "call BJassAssert(IssueTargetOrder(milWorker,\"%s\",milHall),\"saved turn pairing\")\nendfunction\n",
            returning ? "militiaunconvert" : "militiaconvert");
        T_ASSERT(jass_dobuffer(level.vm,script));MilitiaOrderCall("milIssue");
        T_EQ(f.worker->current_order_id,returning ? 852651 : 852071);
        MilitiaOrderRestore(&f);
        for(unsigned i=0;i<100 && f.worker->current_order_id;i++)MilitiaOrderStep();
        T_EQ(f.worker->class_id,returning ? MAKEFOURCC('h','p','e','a') : MAKEFOURCC('h','m','i','l'));
        T_EQ(f.worker->current_order_id,0);T_FEQ(f.worker->s.origin2.x,1152,.01f);T_FEQ(f.worker->s.origin2.y,1024,.01f);
        MilitiaOrderEnd(&f);
    }
}


/* 62e8f0 captures the Hall's current rank for physical approach; 64b280
 * resolves it again at the terminal instead of reusing the captured range. */
TEST(wc3_militia_orders, live_hall_rank_changes_terminal_range_without_replacing_approach) {
    FOR_LOOP(mode,3) {
        militiaOrderFixture_t f;
        MilitiaOrderBegin(&f,false,(vec2_t){704,1024},(vec2_t){1536,1024},0,333);
        char script[1000];snprintf(script,sizeof(script),
            "function milIssue takes nothing returns nothing\n"
            "call SetUnitAbilityLevel(milHall,'Amic',%u)\n"
            "call BJassAssert(IssueTargetOrder(milWorker,\"militiaconvert\",milHall),\"ranked pairing\")\nendfunction\n"
            "function milChangeRank takes nothing returns nothing\n"
            "call SetUnitAbilityLevel(milHall,'Amic',%u)\nendfunction\n",mode==0 ? 1 : 2,mode==1 ? 1 : 2);
        T_ASSERT(jass_dobuffer(level.vm,script));MilitiaOrderCall("milIssue");
        FOR_LOOP(i,3)MilitiaOrderStep();MilitiaOrderCall("milChangeRank");
        T_EQ(f.worker->current_order_id,852071);
        for(unsigned i=0;i<150 && f.worker->current_order_id;i++)MilitiaOrderStep();
        T_EQ(f.worker->class_id,mode==0 ? MAKEFOURCC('h','p','e','a') : MAKEFOURCC('h','m','i','l'));
        T_EQ(f.worker->current_order_id,0);
        MilitiaOrderEnd(&f);
    }
}

/* The issued event runs after admission, before construction. Rejection must
 * retire that packet, while an event-installed successor keeps ownership. */
TEST(wc3_militia_orders, issued_callback_rejection_and_replacement_retire_only_own_packet) {
    FOR_LOOP(mode,3) {
        militiaOrderFixture_t f;
        MilitiaOrderBegin(&f,false,(vec2_t){704,1024},(vec2_t){1536,1024},0,333);
        vec2_t initial={704,1536};
        T_ASSERT(G_IssueUnitPointOrder(f.worker,"move",&initial,false,0,0));
        cstring_t actions[]={"call SetUnitOwner(milHall,Player(1),false)","call RemoveUnit(milHall)",
            "call IssuePointOrder(milWorker,\"move\",512.0,768.0)"};
        char script[1500];snprintf(script,sizeof(script),
            "function milMutate takes nothing returns nothing\n%s\nendfunction\n"
            "function milIssue takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call TriggerRegisterUnitEvent(t,milWorker,EVENT_UNIT_ISSUED_TARGET_ORDER)\n"
            "call TriggerAddAction(t,function milMutate)\n"
            "call BJassAssert(IssueTargetOrder(milWorker,\"militiaconvert\",milHall),\"admitted before callback\")\nendfunction\n",actions[mode]);
        T_ASSERT(jass_dobuffer(level.vm,script));MilitiaOrderCall("milIssue");
        T_EQ(f.worker->current_order_id,mode==2 ? 851986 : 0);
        T_ASSERT(f.worker->currentmove && f.worker->currentmove->think);
        FOR_LOOP(i,100)MilitiaOrderStep();
        T_EQ(f.worker->current_order_id,0);T_EQ(f.worker->class_id,MAKEFOURCC('h','p','e','a'));
        if(mode==2)T_ASSERT(f.worker->s.origin2.x<600 && f.worker->s.origin2.y<850);
        MilitiaOrderEnd(&f);
    }
}

TEST(wc3_militia_orders, queued_rejection_advances_to_successor_move) {
    militiaOrderFixture_t f;
    MilitiaOrderBegin(&f,false,(vec2_t){704,1024},(vec2_t){1536,1024},0,333);
    vec2_t first={704,1152},last={512,768};
    T_ASSERT(G_IssueUnitPointOrder(f.worker,"move",&first,false,0,0));
    T_ASSERT(G_IssueUnitTargetOrder(f.worker,"militiaconvert",f.hall,true,0));
    T_ASSERT(G_IssueUnitPointOrder(f.worker,"move",&last,true,0,0));
    char script[]="function milInvalidate takes nothing returns nothing\ncall SetUnitOwner(milHall,Player(1),false)\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.vm,script));MilitiaOrderCall("milInvalidate");
    FOR_LOOP(i,200)MilitiaOrderStep();
    T_EQ(f.worker->current_order_id,0);T_EQ(f.worker->class_id,MAKEFOURCC('h','p','e','a'));
    T_ASSERT(f.worker->s.origin2.x<600 && f.worker->s.origin2.y<850);
    MilitiaOrderEnd(&f);
}

TEST(wc3_militia_orders, replacement_pairing_owns_the_new_hall) {
    militiaOrderFixture_t f;
    MilitiaOrderBegin(&f,false,(vec2_t){704,1024},(vec2_t){1536,1024},0,333);
    char script[]=
        "function milIssue takes nothing returns nothing\ncall IssueTargetOrder(milWorker,\"militiaconvert\",milHall)\nendfunction\n"
        "function milReplace takes nothing returns nothing\nlocal unit other=CreateUnit(Player(0),'htow',704.0,1792.0,0.0)\n"
        "call BJassAssert(IssueTargetOrder(milWorker,\"militiaconvert\",other),\"replacement Hall\")\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.vm,script));MilitiaOrderCall("milIssue");FOR_LOOP(i,3)MilitiaOrderStep();MilitiaOrderCall("milReplace");
    T_NOT_NULL(f.worker->militia);T_NOT_NULL(f.worker->militia->partner);
    T_ASSERT(f.worker->militia->partner!=f.hall);T_EQ(f.worker->current_order_id,852071);
    FOR_LOOP(i,150)MilitiaOrderStep();
    T_EQ(f.worker->class_id,MAKEFOURCC('h','m','i','l'));T_EQ(f.worker->current_order_id,0);
    T_ASSERT(f.worker->s.origin2.y>1200);
    MilitiaOrderEnd(&f);
}


/* The native perimeter alternative is independent of configured radial range. */
TEST(wc3_militia_orders, fine_perimeter_can_admit_outside_authored_range) {
    militiaOrderFixture_t f;
    MilitiaOrderBegin(&f,false,(vec2_t){1480,1024},(vec2_t){1536,1024},0,1);
    f.hall->collision=16;
    gi.LinkEntity(f.hall);
    T_ASSERT(!S_UnitTargetInMoveRange(f.worker,f.hall,1));
    T_ASSERT(S_UnitTargetOnPerimeter(f.worker,f.hall));
    char script[]="function milIssue takes nothing returns nothing\n"
        "call BJassAssert(IssueTargetOrder(milWorker,\"militiaconvert\",milHall),\"perimeter pairing\")\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.vm,script));MilitiaOrderCall("milIssue");
    T_EQ(f.worker->class_id,MAKEFOURCC('h','m','i','l'));T_EQ(f.worker->current_order_id,0);
    MilitiaOrderEnd(&f);
}

#endif
