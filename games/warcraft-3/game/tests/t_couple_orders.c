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
    slkTestData_t *rows[5],*old[5];uint8_t cells[64*64];
    edict_t *archer,*hippo;
} coupleOrderFixture_t;
static cstring_t const couple_order_tables[]={"UnitBalance","UnitData","UnitAbilities","UnitUI","AbilityData"};
static void CoupleOrderBeginEx(coupleOrderFixture_t *f,bool near,bool aliases) {
    reset_entities();setup_test_world();memset(f,0,sizeof(*f));f->old_info=level.mapinfo;
    bool tft=atoi(gi.CvarString("fs_expansion","0"))!=0;
    char ability[1600];snprintf(ability,sizeof(ability),
        "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
        "C;X4;K\"UnitID1\"\nC;X5;K\"Rng1\"\nC;X6;K\"levels\"\nC;X7;K\"targs\"\n"
        "C;X1;Y2;K\"%s\"\nC;X2;K\"Acoa\"\nC;X3;K\"ehip\"\nC;X4;K\"ehpr\"\nC;X5;K333\nC;X6;K1\nC;X7;K\"_\"\n"
        "C;X1;Y3;K\"%s\"\nC;X2;K\"Acoh\"\nC;X3;K\"earc\"\nC;X4;K\"ehpr\"\nC;X5;K333\nC;X6;K1\nC;X7;K\"_\"\nE\n",
        tft ? "DataA1" : "Data11",aliases ? "Zcoa" : "Acoa",aliases ? "Zcoh" : "Acoh");
    cstring_t const text[]={
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitBalanceID\"\nC;X2;K\"realHP\"\nC;X3;K\"spd\"\nC;X4;K\"collision\"\n"
        "C;X1;Y2;K\"earc\"\nC;X2;K245\nC;X3;K270\nC;X4;K32\n"
        "C;X1;Y3;K\"ehip\"\nC;X2;K525\nC;X3;K400\nC;X4;K8\n"
        "C;X1;Y4;K\"ehpr\"\nC;X2;K765\nC;X3;K400\nC;X4;K8\nE\n",
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitID\"\nC;X2;K\"movetp\"\nC;X3;K\"turnRate\"\nC;X4;K\"race\"\nC;X5;K\"collision\"\nC;X6;K\"propWin\"\n"
        "C;X1;Y2;K\"earc\"\nC;X2;K\"foot\"\nC;X3;K0.6\nC;X4;K\"nightelf\"\nC;X5;K32\nC;X6;K60\n"
        "C;X1;Y3;K\"ehip\"\nC;X2;K\"fly\"\nC;X3;K0.6\nC;X4;K\"nightelf\"\nC;X5;K8\nC;X6;K60\n"
        "C;X1;Y4;K\"ehpr\"\nC;X2;K\"fly\"\nC;X3;K0.6\nC;X4;K\"nightelf\"\nC;X5;K8\nC;X6;K60\nE\n",
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitAbilID\"\nC;X2;K\"abilList\"\n"
        "C;X1;Y2;K\"earc\"\nC;X2;K\"_\"\nC;X1;Y3;K\"ehip\"\nC;X2;K\"_\"\nC;X1;Y4;K\"ehpr\"\nC;X2;K\"_\"\nE\n",
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitUIID\"\nC;X2;K\"file\"\n"
        "C;X1;Y2;K\"earc\"\nC;X2;K\"TestUI\\Models\\anim_pulse.mdx\"\n"
        "C;X1;Y3;K\"ehip\"\nC;X2;K\"TestUI\\Models\\anim_pulse.mdx\"\n"
        "C;X1;Y4;K\"ehpr\"\nC;X2;K\"TestUI\\Models\\anim_pulse.mdx\"\nE\n",ability};
    FOR_LOOP(i,5){f->rows[i]=parse_slk_string(text[i]);f->old[i]=G_SetSLKRows(couple_order_tables[i],f->rows[i]);}
    f->info.fileFormat=tft ? 25 : 24;f->info.players[0].used=true;f->info.players[0].playerType=kPlayerTypeHuman;
    level.mapinfo=&f->info;game.clients[0].mapplayer=f->info.players;
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,f->cells);
    level.time=level.pathing_msec=level.pathing_phase=level.framenum=0;
    level.started=level.scriptsConfigured=level.scriptsStarted=false;
    level.pathing_due=level.pathing_owner_clock_valid=level.timer_clock_valid=false;
    level.pathing_clock=(wc3Clock_t){0,0,300};G_InitWaypoints();
    char script[1600];snprintf(script,sizeof(script),
        "globals\nunit cpArcher\nunit cpHippo\nendglobals\n"
        "function main takes nothing returns nothing\n"
        "call SetPlayerTechResearched(Player(0),'Reht',1)\n"
        "set cpArcher=CreateUnit(Player(0),'earc',%g,1024.0,180.0)\n"
        "set cpHippo=CreateUnit(Player(0),'ehip',1536.0,1024.0,0.0)\n"
        "call UnitAddAbility(cpArcher,'%s')\ncall UnitAddAbility(cpHippo,'%s')\n"
        "call SetUnitAcquireRange(cpArcher,0.0)\ncall SetUnitAcquireRange(cpHippo,0.0)\nendfunction\n",near ? 1280.0 : 704.0,aliases ? "Zcoa" : "Acoa",aliases ? "Zcoh" : "Acoh");
    T_ASSERT(run_test_jass(script));
    FILTER_EDICTS(e,e->inuse){if(e->class_id==MAKEFOURCC('e','a','r','c'))f->archer=e;if(e->class_id==MAKEFOURCC('e','h','i','p'))f->hippo=e;}
    T_NOT_NULL(f->archer);T_NOT_NULL(f->hippo);
    T_EQ(S_SpellDataId(aliases ? MAKEFOURCC('Z','c','o','a') : MAKEFOURCC('A','c','o','a'),1,1),MAKEFOURCC('e','h','i','p'));
    T_EQ(S_SpellUnitId(aliases ? MAKEFOURCC('Z','c','o','h') : MAKEFOURCC('A','c','o','h'),1),MAKEFOURCC('e','h','p','r'));
    G_FinishMovePathingInitialization();level.started=level.scriptsConfigured=level.scriptsStarted=true;
}
static void CoupleOrderBegin(coupleOrderFixture_t *f,bool near) {CoupleOrderBeginEx(f,near,false);}
static void CoupleOrderEnd(coupleOrderFixture_t *f) {
    level.started=false;reset_entities();setup_test_world();level.mapinfo=f->old_info;
    game.clients[0].mapplayer=f->old_info->players;
    FOR_LOOP(i,5){G_SetSLKRows(couple_order_tables[i],f->old[i]);free_slk_rows(f->rows[i]);}
}

/* Explicitly added legacy/custom Acoa/Acoh, not patched stock Aco2/Aco3.
 * Native176: both actors retain target tasks, even within authored333 range;
 * the reciprocal public head is852141, not the unrelated coupletarget order. */
TEST(wc3_couple_orders, public_pairing_installs_reciprocal_approach_before_merge) {
    FOR_LOOP(reverse,2)FOR_LOOP(near,2) {
        coupleOrderFixture_t f;CoupleOrderBegin(&f,near!=0);
        char script[1100];snprintf(script,sizeof(script),
            "function cpIssue takes nothing returns nothing\n"
            "call BJassAssert(IssueTargetOrder(%s,\"%s\",%s),\"pair admitted\")\n"
            "call BJassAssert(GetUnitCurrentOrder(cpArcher)==%u,\"Archer head\")\n"
            "call BJassAssert(GetUnitCurrentOrder(cpHippo)==%u,\"Hippogryph head\")\n"
            "call BJassAssert(GetUnitTypeId(cpArcher)=='earc' and GetUnitTypeId(cpHippo)=='ehip',\"both inputs retained\")\nendfunction\n",
            reverse ? "cpHippo" : "cpArcher",reverse ? "loadarcher" : "mounthippogryph",
            reverse ? "cpArcher" : "cpHippo",reverse ? 852141u : 852143u,reverse ? 852142u : 852141u);
        T_ASSERT(jass_dobuffer(level.vm,script));jass_callbyname(level.vm,"cpIssue",false);T_ASSERT(!jass_rterror_pending(level.vm));
        vec2_t a=f.archer->s.origin2,h=f.hippo->s.origin2;bool archer_moved=false,hippo_moved=false;
        for(unsigned i=0;i<150 && f.archer->inuse && f.hippo->inuse;i++) {
            level.time+=FRAMETIME;globals.RunFrame();
            if(f.archer->inuse && !G_IsDeferredFree(f.archer))archer_moved|=Vector2_distance(&a,&f.archer->s.origin2)>1;
            if(f.hippo->inuse && !G_IsDeferredFree(f.hippo))hippo_moved|=Vector2_distance(&h,&f.hippo->s.origin2)>1;
        }
        T_ASSERT(archer_moved);T_ASSERT(hippo_moved);
        unsigned riders=0;FILTER_EDICTS(e,e->inuse && !G_IsDeferredFree(e) && e->class_id==MAKEFOURCC('e','h','p','r'))riders++;
        T_EQ(riders,1);T_ASSERT(!f.archer->inuse || G_IsDeferredFree(f.archer));T_ASSERT(!f.hippo->inuse || G_IsDeferredFree(f.hippo));
        CoupleOrderEnd(&f);
    }
}

static void CoupleOrderNoopWrite(pfWriteType_t type,void const *data) {(void)type;(void)data;}
static void CoupleOrderNoopUnicast(edict_t *client) {(void)client;}

/* Exercise the real command procedure, configured aliases and controller
 * authority through the installed target-selection callback. */
TEST(wc3_couple_orders, command_target_selection_retains_both_near_approaches) {
    FOR_LOOP(aliases,2)FOR_LOOP(reverse,2) {
        coupleOrderFixture_t f;CoupleOrderBeginEx(&f,true,aliases!=0);
        edict_t *caster=reverse ? f.hippo : f.archer,*partner=reverse ? f.archer : f.hippo;
        edict_t *clent=g_edicts;gameClient_t *client=game.clients;clent->client=client;
        uint32_t code=reverse ? MAKEFOURCC('A','c','o','h') : MAKEFOURCC('A','c','o','a');
        if(aliases)code=reverse ? MAKEFOURCC('Z','c','o','h') : MAKEFOURCC('Z','c','o','a');
        G_SelectEntity(client,caster);client->menu.ability_code=code;
        abilityitem_t item=S_AbilityItem(code);abilityCall_t call={.client=clent,.item=&item};
        void (*old_write)(pfWriteType_t,void const *)=gi.Write;
        void (*old_unicast)(edict_t *)=gi.unicast;
        gi.Write=CoupleOrderNoopWrite;gi.unicast=CoupleOrderNoopUnicast;
        T_ASSERT(S_AbilityMessage(caster,A_COMMAND,&call));T_NOT_NULL(client->menu.on_entity_selected);
        caster->s.player=1;
        T_ASSERT(!client->menu.on_entity_selected(clent,partner));
        caster->s.player=0;
        bool accepted=client->menu.on_entity_selected && client->menu.on_entity_selected(clent,partner);
        gi.Write=old_write;gi.unicast=old_unicast;
        T_ASSERT(accepted);T_EQ(partner->current_order_id,852141);
        T_ASSERT(f.archer->inuse && !G_IsDeferredFree(f.archer));
        T_ASSERT(f.hippo->inuse && !G_IsDeferredFree(f.hippo));
        vec2_t a=f.archer->s.origin2,h=f.hippo->s.origin2;bool archer_moved=false,hippo_moved=false;
        for(unsigned i=0;i<150 && f.archer->inuse && f.hippo->inuse;i++) {
            level.time+=FRAMETIME;globals.RunFrame();
            if(f.archer->inuse && !G_IsDeferredFree(f.archer))archer_moved|=Vector2_distance(&a,&f.archer->s.origin2)>1;
            if(f.hippo->inuse && !G_IsDeferredFree(f.hippo))hippo_moved|=Vector2_distance(&h,&f.hippo->s.origin2)>1;
        }
        T_ASSERT(archer_moved);T_ASSERT(hippo_moved);
        unsigned riders=0;FILTER_EDICTS(e,e->inuse && !G_IsDeferredFree(e) && e->class_id==MAKEFOURCC('e','h','p','r'))riders++;
        T_EQ(riders,1);
        CoupleOrderEnd(&f);
    }
}

static void CoupleOrderStep(void) {level.time+=5;globals.RunFrame();}
static void CoupleOrderCall(cstring_t name) {jass_callbyname(level.vm,name,false);T_ASSERT(!jass_rterror_pending(level.vm));}
static void CoupleOrderRestore(coupleOrderFixture_t *f) {
    uint32_t a=f->archer-g_edicts,h=f->hippo-g_edicts;
    cstring_t path=Test_TempPath("couple224.bin");
    T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);
    f->archer=g_edicts+a;f->hippo=g_edicts+h;
}
static unsigned CoupleOrderRiders(void) {
    unsigned count=0;FILTER_EDICTS(e,e->inuse && !G_IsDeferredFree(e) && e->class_id==MAKEFOURCC('e','h','p','r'))count++;
    return count;
}

TEST(wc3_couple_orders, lifecycle_and_saved_pause_replay_preserve_actor_roles) {
    FOR_LOOP(saved,2)FOR_LOOP(mode,11) {
        coupleOrderFixture_t f;CoupleOrderBegin(&f,false);
        cstring_t actions[]={"","call IssueImmediateOrder(cpArcher,\"stop\")","call IssueImmediateOrder(cpHippo,\"stop\")",
            "call IssuePointOrder(cpArcher,\"move\",512.0,768.0)","call IssuePointOrder(cpHippo,\"move\",1536.0,768.0)",
            "call RemoveUnit(cpArcher)","call RemoveUnit(cpHippo)","call PauseUnit(cpArcher,true)","call PauseUnit(cpHippo,true)",
            "call IssueImmediateOrder(cpArcher,\"stop\")","call IssueImmediateOrder(cpHippo,\"stop\")"};
        char script[1500];snprintf(script,sizeof(script),
            "function cpIssue takes nothing returns nothing\ncall BJassAssert(IssueTargetOrder(cpArcher,\"mounthippogryph\",cpHippo),\"lifecycle coupling\")\nendfunction\n"
            "function cpInterrupt takes nothing returns nothing\n%s\nendfunction\n"
            "function cpResume takes nothing returns nothing\ncall PauseUnit(%s,false)\nendfunction\n",actions[mode],mode==7 ? "cpArcher" : "cpHippo");
        T_ASSERT(jass_dobuffer(level.vm,script));CoupleOrderCall("cpIssue");
        if(mode>=9) {
            unsigned i=0;
            while(i++<1500 && f.archer->inuse && f.hippo->inuse &&
                  !(f.archer->couple->ready != f.hippo->couple->ready))CoupleOrderStep();
            T_ASSERT(f.archer->inuse && f.hippo->inuse && f.archer->couple->ready != f.hippo->couple->ready);
        } else {FOR_LOOP(i,60)CoupleOrderStep();}
        vec2_t a=f.archer->s.origin2,h=f.hippo->s.origin2;
        if(saved && mode>=9)CoupleOrderRestore(&f);
        CoupleOrderCall("cpInterrupt");
        if(mode==1 || mode==2 || mode>=9) {T_EQ(f.archer->current_order_id,0);T_EQ(f.hippo->current_order_id,0);}
        if(mode==3) {T_EQ(f.archer->current_order_id,851986);T_EQ(f.hippo->current_order_id,0);}
        if(mode==4) {T_EQ(f.archer->current_order_id,0);T_EQ(f.hippo->current_order_id,851986);}
        if(mode==5)T_EQ(f.hippo->current_order_id,0);
        if(mode==6)T_EQ(f.archer->current_order_id,0);
        if(mode==7) {T_EQ(f.archer->current_order_id,851973);T_EQ(f.hippo->current_order_id,0);}
        if(mode==8) {T_EQ(f.archer->current_order_id,0);T_EQ(f.hippo->current_order_id,851973);}
        if(mode==5 || mode==6)CoupleOrderStep();
        if(saved && mode<9)CoupleOrderRestore(&f);
        if(mode==7 || mode==8) {
            FOR_LOOP(i,200)CoupleOrderStep();
            T_FEQ(f.archer->s.origin2.x,a.x,.01f);T_FEQ(f.archer->s.origin2.y,a.y,.01f);
            T_FEQ(f.hippo->s.origin2.x,h.x,.01f);T_FEQ(f.hippo->s.origin2.y,h.y,.01f);
            CoupleOrderCall("cpResume");
        }
        FOR_LOOP(i,2500)CoupleOrderStep();
        T_EQ(CoupleOrderRiders(),mode==0 || mode==7 ? 1 : 0);
        if(mode==3)T_ASSERT(f.archer->s.origin2.x<600 && f.archer->s.origin2.y<850);
        if(mode==4)T_ASSERT(f.hippo->s.origin2.y<850);
        if(mode==8) {
            T_EQ(f.archer->current_order_id,0);T_EQ(f.hippo->current_order_id,852141);
            T_ASSERT(f.hippo->couple && f.hippo->couple->ready);
        }
        CoupleOrderEnd(&f);
    }
}

TEST(wc3_couple_orders, reciprocal_and_inverse_stop_callbacks_preserve_nested_move) {
    FOR_LOOP(inverse,2) {
        coupleOrderFixture_t f;CoupleOrderBegin(&f,false);
        char script[2000];snprintf(script,sizeof(script),
            "function cpReplace takes nothing returns nothing\n"
            "if GetIssuedOrderId()==%u then\ncall IssuePointOrder(cpHippo,\"move\",1536.0,768.0)\nendif\nendfunction\n"
            "function cpObserve takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call TriggerRegisterUnitEvent(t,cpHippo,%s)\ncall TriggerAddAction(t,function cpReplace)\nendfunction\n"
            "function cpIssue takes nothing returns nothing\ncall BJassAssert(IssueTargetOrder(cpArcher,\"mounthippogryph\",cpHippo),\"callback pairing\")\nendfunction\n"
            "function cpStop takes nothing returns nothing\ncall IssueImmediateOrder(cpArcher,\"stop\")\nendfunction\n",
            inverse ? 851972u : 852141u,inverse ? "EVENT_UNIT_ISSUED_ORDER" : "EVENT_UNIT_ISSUED_TARGET_ORDER");
        T_ASSERT(jass_dobuffer(level.vm,script));CoupleOrderCall("cpObserve");CoupleOrderCall("cpIssue");
        if(inverse) {FOR_LOOP(i,60)CoupleOrderStep();CoupleOrderCall("cpStop");}
        T_EQ(f.archer->current_order_id,inverse ? 0 : 852143);T_EQ(f.hippo->current_order_id,851986);
        FOR_LOOP(i,2500)CoupleOrderStep();
        T_EQ(CoupleOrderRiders(),0);T_EQ(f.hippo->current_order_id,0);T_ASSERT(f.hippo->s.origin2.y<850);
        T_EQ(f.archer->current_order_id,inverse ? 0 : 852143);
        if(!inverse)T_ASSERT(f.archer->couple && f.archer->couple->ready);
        CoupleOrderEnd(&f);
    }
}

TEST(wc3_couple_orders, cold_round_trip_matches_continuation) {
    coupleOrderFixture_t f;CoupleOrderBegin(&f,false);
    char script[]="function cpIssue takes nothing returns nothing\ncall BJassAssert(IssueTargetOrder(cpArcher,\"mounthippogryph\",cpHippo),\"cold pairing\")\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.vm,script));CoupleOrderCall("cpIssue");FOR_LOOP(i,60)CoupleOrderStep();
    cstring_t path=Test_TempPath("couple224-cold.bin");T_ASSERT(WriteGame(path));
    uint32_t state[600][7];
    FOR_LOOP(i,600) {
        CoupleOrderStep();
        state[i][0]=f.archer->current_order_id;state[i][1]=f.hippo->current_order_id;
        state[i][2]=wc3_float_bits(f.archer->s.origin2.x);state[i][3]=wc3_float_bits(f.hippo->s.origin2.x);
        state[i][4]=f.archer->couple ? f.archer->couple->ready : 0;state[i][5]=f.hippo->couple ? f.hippo->couple->ready : 0;
        state[i][6]=CoupleOrderRiders();
    }
    T_EQ(CoupleOrderRiders(),1);T_ASSERT(ReadGame(path));
    FOR_LOOP(i,600) {
        CoupleOrderStep();
        T_EQ(f.archer->current_order_id,state[i][0]);T_EQ(f.hippo->current_order_id,state[i][1]);
        T_EQ(wc3_float_bits(f.archer->s.origin2.x),state[i][2]);T_EQ(wc3_float_bits(f.hippo->s.origin2.x),state[i][3]);
        T_EQ(f.archer->couple ? f.archer->couple->ready : 0,state[i][4]);T_EQ(f.hippo->couple ? f.hippo->couple->ready : 0,state[i][5]);
        T_EQ(CoupleOrderRiders(),state[i][6]);
    }
    remove(path);CoupleOrderEnd(&f);
}

TEST(wc3_couple_orders, reissued_pairing_retires_only_old_partner) {
    FOR_LOOP(other,2) {
        coupleOrderFixture_t f;CoupleOrderBegin(&f,false);
        char script[1400];snprintf(script,sizeof(script),
            "function cpIssue takes nothing returns nothing\ncall BJassAssert(IssueTargetOrder(cpArcher,\"mounthippogryph\",cpHippo),\"initial pair\")\nendfunction\n"
            "function cpAgain takes nothing returns nothing\nlocal unit target=cpHippo\n%s\n"
            "call BJassAssert(IssueTargetOrder(cpArcher,\"mounthippogryph\",target),\"reissued pair\")\nendfunction\n",
            other ? "set target=CreateUnit(Player(0),'ehip',1536.0,1536.0,0.0)\ncall UnitAddAbility(target,'Acoh')\ncall SetUnitAcquireRange(target,0.0)" : "");
        T_ASSERT(jass_dobuffer(level.vm,script));CoupleOrderCall("cpIssue");FOR_LOOP(i,60)CoupleOrderStep();CoupleOrderCall("cpAgain");
        T_EQ(f.archer->current_order_id,852143);T_ASSERT(f.archer->couple && f.archer->couple->active);
        T_EQ(f.archer->couple->partner->current_order_id,852141);
        T_EQ(f.hippo->current_order_id,other ? 0 : 852141);
        FOR_LOOP(i,2500)CoupleOrderStep();T_EQ(CoupleOrderRiders(),1);
        CoupleOrderEnd(&f);
    }
}

TEST(wc3_couple_orders, queued_pairing_and_invalid_partner_preserve_fifo) {
    FOR_LOOP(invalid,2) {
        coupleOrderFixture_t f;CoupleOrderBegin(&f,false);
        vec2_t first={704,1152},last={512,768};
        T_ASSERT(G_IssueUnitPointOrder(f.archer,"move",&first,false,0,0));
        T_ASSERT(G_IssueUnitTargetOrder(f.archer,"mounthippogryph",f.hippo,true,0));
        if(invalid)T_ASSERT(G_IssueUnitPointOrder(f.archer,"move",&last,true,0,0));
        T_EQ(f.archer->current_order_id,851986);T_EQ(f.hippo->current_order_id,0);
        if(invalid) {
            char script[]="function cpRemove takes nothing returns nothing\ncall RemoveUnit(cpHippo)\nendfunction\n";
            T_ASSERT(jass_dobuffer(level.vm,script));CoupleOrderCall("cpRemove");
        }
        FOR_LOOP(i,2500)CoupleOrderStep();T_EQ(CoupleOrderRiders(),invalid ? 0 : 1);
        if(invalid) {T_EQ(f.archer->current_order_id,0);T_ASSERT(f.archer->s.origin2.x<600 && f.archer->s.origin2.y<850);}
        CoupleOrderEnd(&f);
    }
}

TEST(wc3_couple_orders, allied_owner_rejected_and_single_configured_alias_retained) {
    FOR_LOOP(allied,2) {
        coupleOrderFixture_t f;CoupleOrderBeginEx(&f,false,true);
        f.info.players[1].used=true;f.info.players[1].playerType=kPlayerTypeHuman;
        char script[1200];snprintf(script,sizeof(script),
            "function cpIssue takes nothing returns nothing\n%s\n"
            "call BJassAssert(%sIssueTargetOrder(cpArcher,\"mounthippogryph\",cpHippo),\"configured pair admission\")\nendfunction\n",
            allied ? "call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,true)\ncall SetUnitOwner(cpHippo,Player(1),false)" : "",allied ? "not " : "");
        T_ASSERT(jass_dobuffer(level.vm,script));CoupleOrderCall("cpIssue");
        if(!allied) {
            T_EQ(f.archer->couple->ability,MAKEFOURCC('Z','c','o','a'));T_EQ(f.hippo->couple->ability,MAKEFOURCC('Z','c','o','h'));
            CoupleOrderRestore(&f);
        } else {T_ASSERT(S_SpellIsFriend(f.archer,f.hippo));T_EQ(f.archer->current_order_id,0);}
        FOR_LOOP(i,2500)CoupleOrderStep();T_EQ(CoupleOrderRiders(),allied ? 0 : 1);
        CoupleOrderEnd(&f);
    }
}

TEST(wc3_couple_orders, source_target_callback_removal_retires_constructor) {
    FOR_LOOP(source,2) {
        coupleOrderFixture_t f;CoupleOrderBegin(&f,false);
        char script[1200];snprintf(script,sizeof(script),
            "function cpRemove takes nothing returns nothing\ncall RemoveUnit(%s)\nendfunction\n"
            "function cpIssue takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call TriggerRegisterUnitEvent(t,cpArcher,EVENT_UNIT_ISSUED_TARGET_ORDER)\ncall TriggerAddAction(t,function cpRemove)\n"
            "call BJassAssert(IssueTargetOrder(cpArcher,\"mounthippogryph\",cpHippo),\"admitted before removal\")\nendfunction\n",source ? "cpArcher" : "cpHippo");
        T_ASSERT(jass_dobuffer(level.vm,script));CoupleOrderCall("cpIssue");
        if(!source) {T_EQ(f.archer->current_order_id,0);T_ASSERT(f.archer->currentmove && f.archer->currentmove->think);}
        FOR_LOOP(i,60)CoupleOrderStep();T_EQ(CoupleOrderRiders(),0);
        T_ASSERT(source ? !f.archer->inuse : !f.hippo->inuse);
        CoupleOrderEnd(&f);
    }
}

TEST(wc3_couple_orders, reciprocal_callback_revalidates_ability_and_owner) {
    FOR_LOOP(owner,4) {
        coupleOrderFixture_t f;CoupleOrderBeginEx(&f,false,owner==3);
        char script[1500];snprintf(script,sizeof(script),
            "function cpInvalidate takes nothing returns nothing\nif GetIssuedOrderId()==852141 then\n%s\nendif\nendfunction\n"
            "function cpIssue takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call TriggerRegisterUnitEvent(t,cpHippo,EVENT_UNIT_ISSUED_TARGET_ORDER)\ncall TriggerAddAction(t,function cpInvalidate)\n"
            "call BJassAssert(IssueTargetOrder(cpArcher,\"mounthippogryph\",cpHippo),\"initiator retained\")\nendfunction\n",
            owner==1 ? "call SetUnitOwner(cpHippo,Player(1),false)" : owner==2 ? "call UnitRemoveAbility(cpArcher,'Acoa')" : owner==3 ? "call UnitRemoveAbility(cpHippo,'Zcoh')" : "call UnitRemoveAbility(cpHippo,'Acoh')");
        T_ASSERT(jass_dobuffer(level.vm,script));CoupleOrderCall("cpIssue");
        T_EQ(f.hippo->current_order_id,0);T_ASSERT(!f.hippo->couple || !f.hippo->couple->active);
        FOR_LOOP(i,2500)CoupleOrderStep();T_EQ(CoupleOrderRiders(),0);
        CoupleOrderEnd(&f);
    }
}

TEST(wc3_couple_orders, lone_ready_wait_round_trips_without_reissuing_peer) {
    coupleOrderFixture_t f;CoupleOrderBegin(&f,false);
    char script[]="function cpReplace takes nothing returns nothing\nif GetIssuedOrderId()==852141 then\ncall IssuePointOrder(cpHippo,\"move\",1536.0,768.0)\nendif\nendfunction\n"
        "function cpIssue takes nothing returns nothing\nlocal trigger t=CreateTrigger()\ncall TriggerRegisterUnitEvent(t,cpHippo,EVENT_UNIT_ISSUED_TARGET_ORDER)\n"
        "call TriggerAddAction(t,function cpReplace)\ncall IssueTargetOrder(cpArcher,\"mounthippogryph\",cpHippo)\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.vm,script));CoupleOrderCall("cpIssue");
    FOR_LOOP(i,2500)CoupleOrderStep();T_ASSERT(f.archer->couple && f.archer->couple->ready);
    T_EQ(f.archer->current_order_id,852143);T_EQ(f.hippo->current_order_id,0);
    CoupleOrderRestore(&f);FOR_LOOP(i,500)CoupleOrderStep();
    T_EQ(f.archer->current_order_id,852143);T_EQ(f.hippo->current_order_id,0);T_EQ(CoupleOrderRiders(),0);
    T_ASSERT(f.archer->couple && f.archer->couple->ready);CoupleOrderEnd(&f);
}

TEST(wc3_couple_orders, last_ready_result_uses_hippogryph_pose) {
    coupleOrderFixture_t f;CoupleOrderBegin(&f,false);
    bool tft=atoi(gi.CvarString("fs_expansion","0"))!=0;
    char text[1200];snprintf(text,sizeof(text),
        "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\nC;X4;K\"UnitID1\"\nC;X5;K\"Rng1\"\n"
        "C;X1;Y2;K\"Acoa\"\nC;X2;K\"Acoa\"\nC;X3;K\"ehip\"\nC;X4;K\"ehpr\"\nC;X5;K333\n"
        "C;X1;Y3;K\"Acoh\"\nC;X2;K\"Acoh\"\nC;X3;K\"earc\"\nC;X4;K\"earc\"\nC;X5;K333\nE\n",tft ? "DataA1" : "Data11");
    slkTestData_t *rows=parse_slk_string(text),*old=G_SetSLKRows("AbilityData",rows);
    char script[]="function cpIssue takes nothing returns nothing\ncall IssueTargetOrder(cpArcher,\"mounthippogryph\",cpHippo)\nendfunction\n";
    T_ASSERT(jass_dobuffer(level.vm,script));CoupleOrderCall("cpIssue");
    for(unsigned i=0;i<2500 && f.hippo->inuse && !(f.hippo->couple && f.hippo->couple->ready);i++)CoupleOrderStep();
    T_ASSERT(f.hippo->inuse && f.hippo->couple && f.hippo->couple->ready);
    T_ASSERT(f.archer->inuse && f.archer->couple && !f.archer->couple->ready);
    CoupleOrderRestore(&f);
    T_ASSERT(f.hippo->couple->ready && !f.archer->couple->ready);
    f.hippo->s.angle=0.77f;
    FOR_LOOP(i,1000)CoupleOrderStep();
    T_EQ(CoupleOrderRiders(),1);
    FILTER_EDICTS(e,e->inuse && e->class_id==MAKEFOURCC('e','h','p','r'))T_ASSERT(fabsf(e->s.angle-0.77f)<0.001f);
    G_SetSLKRows("AbilityData",old);free_slk_rows(rows);CoupleOrderEnd(&f);
}

TEST(wc3_couple_orders, inverse_stop_callback_original_actor_is_outer_owned) {
    FOR_LOOP(remove,2) {
        coupleOrderFixture_t f;CoupleOrderBegin(&f,false);
        char script[2500];snprintf(script,sizeof(script),
            "globals\nboolean cpPeerStopped=false\nendglobals\n"
            "function cpOriginalEvent takes nothing returns nothing\nif GetIssuedOrderId()==851972 then\ncall BJassAssert(cpPeerStopped,\"peer Stop before original Stop\")\nendif\nendfunction\n"
            "function cpReplace takes nothing returns nothing\nif GetIssuedOrderId()==851972 then\nset cpPeerStopped=true\n%s\nendif\nendfunction\n"
            "function cpIssue takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call TriggerRegisterUnitEvent(t,cpHippo,EVENT_UNIT_ISSUED_ORDER)\ncall TriggerAddAction(t,function cpReplace)\n"
            "set t=CreateTrigger()\ncall TriggerRegisterUnitEvent(t,cpArcher,EVENT_UNIT_ISSUED_ORDER)\ncall TriggerAddAction(t,function cpOriginalEvent)\n"
            "call IssueTargetOrder(cpArcher,\"mounthippogryph\",cpHippo)\nendfunction\n"
            "function cpStop takes nothing returns nothing\ncall IssueImmediateOrder(cpArcher,\"stop\")\nendfunction\n",
            remove ? "call RemoveUnit(cpArcher)" : "call IssuePointOrder(cpArcher,\"move\",704.0,768.0)");
        T_ASSERT(jass_dobuffer(level.vm,script));CoupleOrderCall("cpIssue");
        FOR_LOOP(i,60)CoupleOrderStep();vec2_t before=f.archer->s.origin2;
        CoupleOrderCall("cpStop");
        T_EQ(f.hippo->current_order_id,0);
        if(!remove)T_EQ(f.archer->current_order_id,0);
        FOR_LOOP(i,500)CoupleOrderStep();T_EQ(CoupleOrderRiders(),0);
        if(!remove) {T_EQ(f.archer->current_order_id,0);T_ASSERT(Vector2_distance(&before,&f.archer->s.origin2)<0.01f);}
        /* Native185's removal attempt retained the actor. This branch checks
         * bounded engine lifetime safety without asserting a retail removal. */
        T_ASSERT(!f.hippo->couple || !f.hippo->couple->active);
        CoupleOrderEnd(&f);
    }
}

#endif
