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
} coupleInstantFixture_t;
static cstring_t const couple_instant_tables[]={"UnitBalance","UnitData","UnitAbilities","UnitUI","AbilityData"};
static void CoupleInstantBeginAuthored(coupleInstantFixture_t *f,float archer_x,bool range333,cstring_t peer_type) {
    reset_entities();setup_test_world();memset(f,0,sizeof(*f));f->old_info=level.mapinfo;
    bool tft=atoi(gi.CvarString("fs_expansion","0"))!=0;
    char ability[1800];snprintf(ability,sizeof(ability),
        "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
        "C;X4;K\"UnitID1\"\nC;X5;K\"Rng1\"\nC;X6;K\"levels\"\nC;X7;K\"targs\"\nC;X8;K\"%s\"\nC;X9;K\"Area1\"\n"
        "C;X1;Y2;K\"Aco2\"\nC;X2;K\"Acoi\"\nC;X3;K\"ehip\"\nC;X4;K\"ehpr\"\nC;X5;K%g\nC;X6;K1\nC;X7;K\"vuln,invu\"\nC;X8;K0\nC;X9;K900\n"
        "C;X1;Y3;K\"Aco3\"\nC;X2;K\"Acoi\"\nC;X3;K\"%s\"\nC;X4;K\"ehpr\"\nC;X5;K%g\nC;X6;K1\nC;X7;K\"vuln,invu\"\nC;X8;K1\nC;X9;K900\nE\n",
        tft ? "DataA1" : "Data11",tft ? "DataB1" : "Data12",range333 ? 333.0 : 99999.0,peer_type,range333 ? 333.0 : 0.0);
    cstring_t const text[]={
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitBalanceID\"\nC;X2;K\"realHP\"\nC;X3;K\"spd\"\nC;X4;K\"collision\"\n"
        "C;X1;Y2;K\"earc\"\nC;X2;K245\nC;X3;K270\nC;X4;K32\n"
        "C;X1;Y3;K\"ehip\"\nC;X2;K525\nC;X3;K400\nC;X4;K8\n"
        "C;X1;Y4;K\"ehpr\"\nC;X2;K765\nC;X3;K400\nC;X4;K8\nE\n",
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitID\"\nC;X2;K\"movetp\"\nC;X3;K\"turnRate\"\nC;X4;K\"race\"\nC;X5;K\"collision\"\nC;X6;K\"propWin\"\nC;X7;K\"death\"\n"
        "C;X1;Y2;K\"earc\"\nC;X2;K\"foot\"\nC;X3;K0.6\nC;X4;K\"nightelf\"\nC;X5;K32\nC;X6;K60\nC;X7;K0.4\n"
        "C;X1;Y3;K\"ehip\"\nC;X2;K\"fly\"\nC;X3;K0.6\nC;X4;K\"nightelf\"\nC;X5;K8\nC;X6;K60\nC;X7;K1.2\n"
        "C;X1;Y4;K\"ehpr\"\nC;X2;K\"fly\"\nC;X3;K0.6\nC;X4;K\"nightelf\"\nC;X5;K8\nC;X6;K60\nE\n",
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitAbilID\"\nC;X2;K\"abilList\"\n"
        "C;X1;Y2;K\"earc\"\nC;X2;K\"Aco2\"\nC;X1;Y3;K\"ehip\"\nC;X2;K\"Aco3\"\nC;X1;Y4;K\"ehpr\"\nC;X2;K\"_\"\nE\n",
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitUIID\"\nC;X2;K\"file\"\n"
        "C;X1;Y2;K\"earc\"\nC;X2;K\"TestUI\\Models\\anim_pulse.mdx\"\n"
        "C;X1;Y3;K\"ehip\"\nC;X2;K\"TestUI\\Models\\anim_pulse.mdx\"\n"
        "C;X1;Y4;K\"ehpr\"\nC;X2;K\"TestUI\\Models\\anim_pulse.mdx\"\nE\n",ability};
    FOR_LOOP(i,5){f->rows[i]=parse_slk_string(text[i]);f->old[i]=G_SetSLKRows(couple_instant_tables[i],f->rows[i]);}
    f->info.fileFormat=tft ? 25 : 24;f->info.players[0].used=true;f->info.players[0].playerType=kPlayerTypeHuman;
    level.mapinfo=&f->info;game.clients[0].mapplayer=f->info.players;
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,f->cells);
    level.time=level.pathing_msec=level.pathing_phase=level.framenum=0;
    level.started=level.scriptsConfigured=level.scriptsStarted=false;
    level.pathing_due=level.pathing_owner_clock_valid=level.timer_clock_valid=false;
    level.pathing_clock=(wc3Clock_t){0,0,300};G_InitWaypoints();
    char script[1600];snprintf(script,sizeof(script),
        "globals\nunit ciArcher\nunit ciHippo\nendglobals\n"
        "function main takes nothing returns nothing\n"
        "call SetPlayerTechResearched(Player(0),'Reht',1)\n"
        "set ciArcher=CreateUnit(Player(0),'earc',%g,1024.0,180.0)\n"
        "set ciHippo=CreateUnit(Player(0),'ehip',1536.0,1024.0,0.0)\n"
        "call SetUnitAcquireRange(ciArcher,0.0)\ncall SetUnitAcquireRange(ciHippo,0.0)\nendfunction\n",(double)archer_x);
    T_ASSERT(run_test_jass(script));
    FILTER_EDICTS(e,e->inuse){if(e->class_id==MAKEFOURCC('e','a','r','c'))f->archer=e;if(e->class_id==MAKEFOURCC('e','h','i','p'))f->hippo=e;}
    T_NOT_NULL(f->archer);T_NOT_NULL(f->hippo);
    T_EQ(S_SpellDataId(MAKEFOURCC('A','c','o','2'),1,1),MAKEFOURCC('e','h','i','p'));
    T_EQ(S_SpellUnitId(MAKEFOURCC('A','c','o','3'),1),MAKEFOURCC('e','h','p','r'));
    T_EQ(G_UnitAbilityClassLevel(f->archer,MAKEFOURCC('A','c','o','2')),1);
    T_EQ(G_UnitAbilityClassLevel(f->hippo,MAKEFOURCC('A','c','o','3')),1);
    T_EQ(S_SpellData(MAKEFOURCC('A','c','o','2'),1,2),0.0f);
    T_EQ(S_SpellData(MAKEFOURCC('A','c','o','3'),1,2),1.0f);
    T_ASSERT(G_UnitAbilityResearchAvailable(f->archer,MAKEFOURCC('A','c','o','2')));
    T_ASSERT(G_UnitAbilityResearchAvailable(f->hippo,MAKEFOURCC('A','c','o','3')));
    G_FinishMovePathingInitialization();level.started=level.scriptsConfigured=level.scriptsStarted=true;
}
static void CoupleInstantBegin(coupleInstantFixture_t *f,float x,bool range333) {CoupleInstantBeginAuthored(f,x,range333,"earc");}
static void CoupleInstantEnd(coupleInstantFixture_t *f) {
    level.started=false;reset_entities();setup_test_world();level.mapinfo=f->old_info;
    game.clients[0].mapplayer=f->old_info->players;
    FOR_LOOP(i,5){G_SetSLKRows(couple_instant_tables[i],f->old[i]);free_slk_rows(f->rows[i]);}
}


static void CoupleInstantStep(void) {level.time+=5;globals.RunFrame();}
static moveGroup_t *CoupleInstantGroup(edict_t const *unit) {
    FOR_LOOP(i,ARRAY_COUNT(level.move_groups)) {
        moveGroup_t *group=level.move_groups[i];
        if(group && group->inuse && group->id==unit->movement.group_id)return group;
    }
    return NULL;
}
static unsigned CoupleInstantRiders(void) {
    unsigned n=0;FILTER_EDICTS(e,e->inuse && !G_IsDeferredFree(e) && e->class_id==MAKEFOURCC('e','h','p','r'))n++;
    return n;
}
static bool CoupleInstantCompile(cstring_t source) {
    char *buffer=malloc(strlen(source)+1);strcpy(buffer,source);
    bool result=jass_dobuffer(level.vm,buffer);free(buffer);return result;
}
static void CoupleInstantCall(cstring_t name) {
    jass_callbyname(level.vm,name,false);
    if(jass_rterror_pending(level.vm))fprintf(stderr,"Acoi JASS runtime: %s\n",jass_rterror_message(level.vm));
    T_ASSERT(!jass_rterror_pending(level.vm));
}

/* Native stock-v3 and range333-v1: immediate actor owns852508, synthesized
 * target852508 follows, and only Archer automatic selection coordinates852507.
 * Explicit Archer852507 approaches/waits alone over the captured interval. */
TEST(wc3_couple_instant_orders, public_stock_and_authored_range_producers) {
    FOR_LOOP(range333,2)FOR_LOOP(scene,6) {
        bool reverse=scene==2 || scene==3 || scene==5,explicit_target=scene>=4;
        coupleInstantFixture_t f;CoupleInstantBegin(&f,scene==0 || scene==2 ? 1280 : 704,range333!=0);
        char script[5200];snprintf(script,sizeof(script),
            "globals\ninteger ciStage=0\nunit ciActor\nunit ciPeer\nboolean ciExplicit=false\nendglobals\n"
            "function ciImmediate takes nothing returns nothing\nif GetIssuedOrderId()==852508 then\n"
            "call BJassAssert(ciStage==0,\"immediate event first\")\n"
            "call BJassAssert(GetTriggerUnit()==ciActor,\"immediate actor\")\n"
            "call BJassAssert(GetUnitCurrentOrder(ciActor)==852508 and GetUnitCurrentOrder(ciPeer)==0,\"immediate heads\")\nset ciStage=1\nendif\nendfunction\n"
            "function ciTarget takes nothing returns nothing\nif GetIssuedOrderId()==852508 then\n"
            "call BJassAssert(ciStage==1,\"automatic target after immediate\")\n"
            "call BJassAssert(GetTriggerUnit()==ciActor and GetOrderTargetUnit()==ciPeer,\"automatic actor target\")\n"
            "call BJassAssert(GetUnitCurrentOrder(ciActor)==852508 and GetUnitCurrentOrder(ciPeer)==0,\"automatic target heads\")\nset ciStage=2\n"
            "elseif GetIssuedOrderId()==852507 then\ncall BJassAssert(ciStage==%u,\"explicit or reciprocal target frontier\")\nset ciStage=ciStage+1\n"
            "if ciExplicit then\ncall BJassAssert(GetTriggerUnit()==ciActor and GetOrderTargetUnit()==ciPeer,\"explicit actor target\")\n"
            "call BJassAssert(GetUnitCurrentOrder(ciActor)==852507 and GetUnitCurrentOrder(ciPeer)==0,\"explicit heads\")\n"
            "else\ncall BJassAssert(GetTriggerUnit()==ciPeer and GetOrderTargetUnit()==ciActor,\"reciprocal actor target\")\n"
            "call BJassAssert(GetUnitCurrentOrder(ciActor)==852508 and GetUnitCurrentOrder(ciPeer)==852507,\"reciprocal heads\")\nendif\nendif\nendfunction\n"
            "function ciIssue takes nothing returns nothing\nlocal trigger a=CreateTrigger()\nlocal trigger b=CreateTrigger()\nlocal boolean accepted=false\n"
            "set ciActor=%s\nset ciPeer=%s\nset ciExplicit=%s\n"
            "call TriggerRegisterUnitEvent(a,ciArcher,EVENT_UNIT_ISSUED_ORDER)\ncall TriggerRegisterUnitEvent(a,ciHippo,EVENT_UNIT_ISSUED_ORDER)\ncall TriggerAddAction(a,function ciImmediate)\n"
            "call TriggerRegisterUnitEvent(b,ciArcher,EVENT_UNIT_ISSUED_TARGET_ORDER)\ncall TriggerRegisterUnitEvent(b,ciHippo,EVENT_UNIT_ISSUED_TARGET_ORDER)\ncall TriggerAddAction(b,function ciTarget)\n"
            "set accepted=%s(%s,\"%s\"%s)\nif accepted then\ncall SetUnitUserData(ciArcher,1)\nendif\n"
            "if accepted then\ncall BJassAssert(ciStage==%u,\"complete producer event sequence\")\nendif\nendfunction\n",
            explicit_target ? 0u : 2u,reverse ? "ciHippo" : "ciArcher",reverse ? "ciArcher" : "ciHippo",explicit_target ? "true" : "false",explicit_target ? "IssueTargetOrder" : "IssueImmediateOrder",
            reverse ? "ciHippo" : "ciArcher",explicit_target ? "coupletarget" : "coupleinstant",
            explicit_target ? (reverse ? ",ciArcher" : ",ciHippo") : "",explicit_target ? 1u : reverse ? 2u : 3u);
        T_ASSERT(CoupleInstantCompile(script));CoupleInstantCall("ciIssue");T_EQ(f.archer->user_data,1);
        if(f.archer->user_data==1) {
            T_EQ(f.archer->current_order_id,reverse ? 0 : explicit_target ? 852507 : 852508);
            T_EQ(f.hippo->current_order_id,reverse ? (explicit_target ? 852507 : 852508) : explicit_target ? 0 : 852507);
            vec2_t before=f.archer->s.origin2;
            moveGroup_t *group=CoupleInstantGroup(reverse ? f.hippo : f.archer);
            T_NOT_NULL(group);
            if(group) {
                T_ASSERT(group->receiver!=NULL);T_EQ(group->target,reverse ? f.archer : f.hippo);
                bool in_range=range333 ? (scene==0 || scene==2) : !reverse;
                T_EQ((group->flags&0x200u)!=0,in_range);
                if(!in_range)T_FEQ(group->members[0].arrival_range,range333 ? 11.65625f : 1.25f,0);
            }
            FOR_LOOP(i,60)CoupleInstantStep();
            if(!reverse && range333 && scene!=0)T_ASSERT(Vector2_distance(&before,&f.archer->s.origin2)>0);
            else if(!range333 || reverse)T_FEQ(Vector2_distance(&before,&f.archer->s.origin2),0,0);
            FOR_LOOP(i,1140)CoupleInstantStep();
            T_EQ(CoupleInstantRiders(),scene==4 ? 0 : 1);
            if(scene==4) {T_EQ(f.archer->current_order_id,852507);T_EQ(f.hippo->current_order_id,0);}
        }
        CoupleInstantEnd(&f);
    }
}

static void CoupleInstantNoopWrite(pfWriteType_t type,void const *data) {(void)type;(void)data;}
static void CoupleInstantNoopUnicast(edict_t *client) {(void)client;}
TEST(wc3_couple_instant_orders, stock_command_button_produces_immediate_pairing) {
    FOR_LOOP(reverse,2) {
        coupleInstantFixture_t f;CoupleInstantBegin(&f,704,false);
        edict_t *caster=reverse ? f.hippo : f.archer,*clent=g_edicts;gameClient_t *client=game.clients;
        clent->client=client;G_SelectEntity(client,caster);
        uint32_t code=reverse ? MAKEFOURCC('A','c','o','3') : MAKEFOURCC('A','c','o','2');
        client->menu.ability_code=code;abilityitem_t item=S_AbilityItem(code);abilityCall_t call={.client=clent,.item=&item};
        void (*old_write)(pfWriteType_t,void const *)=gi.Write;void (*old_unicast)(edict_t *)=gi.unicast;
        gi.Write=CoupleInstantNoopWrite;gi.unicast=CoupleInstantNoopUnicast;
        T_ASSERT(S_AbilityMessage(caster,A_COMMAND,&call));
        gi.Write=old_write;gi.unicast=old_unicast;
        T_EQ(caster->current_order_id,852508);
        T_EQ((reverse ? f.archer : f.hippo)->current_order_id,reverse ? 0 : 852507);
        FOR_LOOP(i,1200)CoupleInstantStep();T_EQ(CoupleInstantRiders(),1);
        CoupleInstantEnd(&f);
    }
}
/* Draft additions for t_couple_instant_orders.c; uses its fixture and public
 * dispatch. No fixed retirement deadline is inferred from retail samples. */
static void CoupleInstantSaveProjection(coupleInstantFixture_t const *f,uint32_t row[9]) {
    row[0]=f->archer->current_order_id;row[1]=f->hippo->current_order_id;
    row[2]=wc3_float_bits(f->archer->s.origin2.x);
    row[3]=wc3_float_bits(f->archer->s.origin2.y);
    row[4]=wc3_float_bits(f->hippo->s.origin2.x);
    row[5]=wc3_float_bits(f->hippo->s.origin2.y);
    row[6]=CoupleInstantRiders();
    row[7]=f->archer->couple_instant && f->archer->couple_instant->waiting;
    row[8]=f->archer->couple_instant ? f->archer->couple_instant->ability : 0;
}

TEST(wc3_couple_instant_orders, direct_archer_approach_and_wait_resume_without_peer_order) {
    FOR_LOOP(save_wait,2) {
        coupleInstantFixture_t f;CoupleInstantBegin(&f,704,true);
        char const script[]=
            "globals\ninteger ciPeerOrders=0\nendglobals\n"
            "function ciPeerObserved takes nothing returns nothing\n"
            "if GetIssuedOrderId()==852507 or GetIssuedOrderId()==852508 then\n"
            "set ciPeerOrders=ciPeerOrders+1\nendif\nendfunction\n"
            "function ciSaveIssue takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call TriggerRegisterUnitEvent(t,ciHippo,EVENT_UNIT_ISSUED_TARGET_ORDER)\n"
            "call TriggerAddAction(t,function ciPeerObserved)\n"
            "call BJassAssert(IssueTargetOrder(ciArcher,\"coupletarget\",ciHippo),\"direct Archer admission\")\nendfunction\n"
            "function ciSaveCheck takes nothing returns nothing\n"
            "call BJassAssert(ciPeerOrders==0,\"restore must not synthesize reciprocal order\")\n"
            "call BJassAssert(GetUnitCurrentOrder(ciArcher)==852507,\"retained direct order\")\nendfunction\n";
        T_ASSERT(CoupleInstantCompile(script));CoupleInstantCall("ciSaveIssue");
        unsigned const advance=save_wait ? 1200 : 20;
        FOR_LOOP(i,advance)CoupleInstantStep();
        T_NOT_NULL(f.archer->couple_instant);
        if(!f.archer->couple_instant) {CoupleInstantEnd(&f);continue;}
        T_EQ(f.archer->couple_instant->waiting,save_wait!=0);
        T_EQ(f.archer->current_order_id,852507);T_EQ(f.hippo->current_order_id,0);
        uint32_t const a=f.archer-g_edicts,h=f.hippo-g_edicts;
        uint32_t const a_birth=f.archer->spawn_time,h_birth=f.hippo->spawn_time;
        cstring_t path=Test_TempPath("couple-instant225-direct.bin");
        T_ASSERT(WriteGame(path));
        uint32_t expected[800][9];
        FOR_LOOP(i,800) {CoupleInstantStep();CoupleInstantSaveProjection(&f,expected[i]);}
        T_EQ(CoupleInstantRiders(),0);CoupleInstantCall("ciSaveCheck");
        T_ASSERT(f.archer->couple_instant && f.archer->couple_instant->waiting);
        T_ASSERT(f.archer->s.origin2.x>704);T_FEQ(f.hippo->s.origin2.x,1536,0.001f);
        T_ASSERT(ReadGame(path));f.archer=g_edicts+a;f.hippo=g_edicts+h;
        T_EQ(f.archer->spawn_time,a_birth);T_EQ(f.hippo->spawn_time,h_birth);
        T_NOT_NULL(f.archer->couple_instant);
        if(f.archer->couple_instant) {
            T_EQ(f.archer->couple_instant->partner,f.hippo);
            T_EQ(f.archer->couple_instant->partner_spawn_time,h_birth);
            T_EQ(f.archer->couple_instant->ability,MAKEFOURCC('A','c','o','2'));
            T_EQ(f.archer->couple_instant->waiting,save_wait!=0);
        }
        FOR_LOOP(i,800) {
            uint32_t actual[9];CoupleInstantStep();CoupleInstantSaveProjection(&f,actual);
            FOR_LOOP(j,9)T_EQ(actual[j],expected[i][j]);
        }
        CoupleInstantCall("ciSaveCheck");remove(path);CoupleInstantEnd(&f);
    }
}

/* EXTERNAL DRAFT: append inside t_couple_instant_orders.c's BZ_TESTS block.
 * Uses its existing fixture/helpers. Not compiled or executed yet.
 * Native public contracts: lifecycle-v1 and callback-v1. These tests do not
 * assert native physical trajectories or an exact storage release deadline.
 * Native probe ticks are 20 ms; fixture steps are 5 ms. The 300/2000 ms
 * windows below reproduce captured 15/100-tick intervals. The fixture must
 * establish the same precondition before interpreting an inverse failure.
 */

static cstring_t const couple_instant_inverse_script =
"globals\n"
"integer cxScene=0\n"
"integer cxMode=0\n"
"integer cxN=0\n"
"integer cxDeaths=0\n"
"boolean cxNested=false\n"
"boolean cxIntervening=false\n"
"integer array cxOrder\n"
"integer array cxActor\n"
"integer array cxTarget\n"
"integer array cxKind\n"
"integer array cxHeadA\n"
"integer array cxHeadH\n"
"real cxAX=0\n"
"real cxAY=0\n"
"real cxHX=0\n"
"real cxHY=0\n"
"real cxLife=0\n"
"endglobals\n"
"function cxRole takes unit u returns integer\n"
"if u==ciArcher then\n"
"return 1\n"
"elseif u==ciHippo then\n"
"return 2\n"
"endif\n"
"return 0\n"
"endfunction\n"
"function cxRiders takes nothing returns integer\n"
"local group g=CreateGroup()\n"
"local unit u\n"
"local integer n=0\n"
"call GroupEnumUnitsOfPlayer(g,Player(0),null)\n"
"loop\n"
"set u=FirstOfGroup(g)\n"
"exitwhen u==null\n"
"if GetUnitTypeId(u)=='ehpr' then\n"
"set n=n+1\n"
"endif\n"
"call GroupRemoveUnit(g,u)\n"
"endloop\n"
"call DestroyGroup(g)\n"
"return n\n"
"endfunction\n"
"function cxEvent takes integer kind returns nothing\n"
"local unit actor=GetTriggerUnit()\n"
"local integer oid=GetIssuedOrderId()\n"
"local integer role=cxRole(actor)\n"
"set cxKind[cxN]=kind\n"
"set cxHeadA[cxN]=GetUnitCurrentOrder(ciArcher)\n"
"set cxHeadH[cxN]=GetUnitCurrentOrder(ciHippo)\n"
"set cxOrder[cxN]=oid\n"
"set cxActor[cxN]=role\n"
"set cxTarget[cxN]=0\n"
"if kind==2 then\n"
"set cxTarget[cxN]=cxRole(GetOrderTargetUnit())\n"
"endif\n"
"set cxN=cxN+1\n"
"if cxMode==1 and not cxNested and ((cxScene==5 and kind==1 and oid==852508 and role==1) or (cxScene==1 and kind==2 and oid==852508 and role==1) or (cxScene==2 and kind==2 and oid==852507 and role==2) or (cxScene==3 and cxIntervening and kind==1 and oid==851972 and role==2) or (cxScene==4 and cxIntervening and kind==1 and oid==851972 and role==1)) then\n"
"set cxNested=true\n"
"if cxScene==2 then\n"
"call BJassAssert(IssuePointOrder(ciHippo,\"move\",1792,768),\"nested Hippo Move admitted\")\n"
"else\n"
"call BJassAssert(IssuePointOrder(ciArcher,\"move\",512,768),\"nested Archer Move admitted\")\n"
"endif\n"
"call BJassAssert(GetTriggerUnit()==actor and GetIssuedOrderId()==oid,\"outer event actor/order response restored\")\n"
"endif\n"
"endfunction\n"
"function cxImmediate takes nothing returns nothing\n"
"call cxEvent(1)\n"
"endfunction\n"
"function cxTargetEvent takes nothing returns nothing\n"
"call cxEvent(2)\n"
"endfunction\n"
"function cxPoint takes nothing returns nothing\n"
"call cxEvent(3)\n"
"endfunction\n"
"function cxDeath takes nothing returns nothing\n"
"call BJassAssert(GetUnitTypeId(ciArcher)=='earc' and GetUnitTypeId(ciHippo)=='ehip',\"original types retained during merge deaths\")\n"
"call BJassAssert(cxRiders()==1,\"result published before original deaths\")\n"
"call BJassAssert(GetWidgetLife(ciHippo)==0.0,\"Hippo life zero first\")\n"
"if cxDeaths==0 then\n"
"call BJassAssert(GetTriggerUnit()==ciHippo and GetWidgetLife(ciArcher)==cxLife,\"Hippo death while Archer remains alive\")\n"
"else\n"
"call BJassAssert(cxDeaths==1 and GetTriggerUnit()==ciArcher and GetWidgetLife(ciArcher)==0.0,\"Archer death second\")\n"
"endif\n"
"set cxDeaths=cxDeaths+1\n"
"endfunction\n"
"function cxInstall takes nothing returns nothing\n"
"local trigger a=CreateTrigger()\n"
"local trigger b=CreateTrigger()\n"
"local trigger c=CreateTrigger()\n"
"local trigger d=CreateTrigger()\n"
"call TriggerRegisterUnitEvent(a,ciArcher,EVENT_UNIT_ISSUED_ORDER)\n"
"call TriggerRegisterUnitEvent(a,ciHippo,EVENT_UNIT_ISSUED_ORDER)\n"
"call TriggerAddAction(a,function cxImmediate)\n"
"call TriggerRegisterUnitEvent(b,ciArcher,EVENT_UNIT_ISSUED_TARGET_ORDER)\n"
"call TriggerRegisterUnitEvent(b,ciHippo,EVENT_UNIT_ISSUED_TARGET_ORDER)\n"
"call TriggerAddAction(b,function cxTargetEvent)\n"
"call TriggerRegisterUnitEvent(c,ciArcher,EVENT_UNIT_ISSUED_POINT_ORDER)\n"
"call TriggerRegisterUnitEvent(c,ciHippo,EVENT_UNIT_ISSUED_POINT_ORDER)\n"
"call TriggerAddAction(c,function cxPoint)\n"
"call TriggerRegisterUnitEvent(d,ciArcher,EVENT_UNIT_DEATH)\n"
"call TriggerRegisterUnitEvent(d,ciHippo,EVENT_UNIT_DEATH)\n"
"call TriggerAddAction(d,function cxDeath)\n"
"set cxLife=GetWidgetLife(ciArcher)\n"
"endfunction\n"
"function cxPrecondition takes nothing returns nothing\n"
"call BJassAssert(GetOwningPlayer(ciArcher)==Player(0) and GetOwningPlayer(ciHippo)==Player(0),\"both original owners before intervention\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==852508 and GetUnitCurrentOrder(ciHippo)==852507,\"both live pair heads before intervention\")\n"
"call BJassAssert(GetUnitTypeId(ciArcher)=='earc' and GetUnitTypeId(ciHippo)=='ehip' and GetWidgetLife(ciArcher)>0 and GetWidgetLife(ciHippo)>0,\"live originals before intervention\")\n"
"call BJassAssert(GetUnitAbilityLevel(ciArcher,'Aco2')==1 and GetUnitAbilityLevel(ciHippo,'Aco3')==1 and GetPlayerTechCount(Player(0),'Reht',true)==1,\"natural available pair\")\n"
"call BJassAssert(not IsUnitPaused(ciArcher) and not IsUnitPaused(ciHippo) and cxRiders()==0,\"unpaused before merge\")\n"
"set cxAX=GetUnitX(ciArcher)\n"
"set cxAY=GetUnitY(ciArcher)\n"
"set cxHX=GetUnitX(ciHippo)\n"
"set cxHY=GetUnitY(ciHippo)\n"
"endfunction\n";

static cstring_t const couple_instant_lifecycle_cases[] = {
"function cxStart takes nothing returns nothing\n"
"set cxScene=0\n"
"set cxMode=0\n"
"call cxInstall()\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial immediate accepted\")\n"
"endfunction\n"
"function cxIntervene takes nothing returns nothing\n"
"call cxPrecondition()\n"
"set cxIntervening=true\n"
"\n"
"endfunction\n"
"function cxResume takes nothing returns nothing\n"
"endfunction\n"
"function cxVerify takes nothing returns nothing\n"
"call BJassAssert(cxN==4,\"exact event count\")\n"
"call BJassAssert(cxKind[0]==1 and cxOrder[0]==852508 and cxActor[0]==1 and cxTarget[0]==0,\"event 0 kind order actor target\")\n"
"call BJassAssert(cxHeadA[0]==852508 and cxHeadH[0]==0,\"event 0 public heads\")\n"
"call BJassAssert(cxKind[1]==2 and cxOrder[1]==852508 and cxActor[1]==1 and cxTarget[1]==2,\"event 1 kind order actor target\")\n"
"call BJassAssert(cxHeadA[1]==852508 and cxHeadH[1]==0,\"event 1 public heads\")\n"
"call BJassAssert(cxKind[2]==2 and cxOrder[2]==852507 and cxActor[2]==2 and cxTarget[2]==1,\"event 2 kind order actor target\")\n"
"call BJassAssert(cxHeadA[2]==852508 and cxHeadH[2]==852507,\"event 2 public heads\")\n"
"call BJassAssert(cxKind[3]==1 and cxOrder[3]==851972 and cxActor[3]==1 and cxTarget[3]==0,\"event 3 kind order actor target\")\n"
"call BJassAssert(cxHeadA[3]==851972 and cxHeadH[3]==852507,\"event 3 public heads\")\n"
"call BJassAssert(cxRiders()==1 and cxDeaths==2,\"bounded merge and death count\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==0 and GetUnitCurrentOrder(ciHippo)==0,\"settled heads\")\n"
"endfunction\n",
"function cxStart takes nothing returns nothing\n"
"set cxScene=1\n"
"set cxMode=0\n"
"call cxInstall()\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial immediate accepted\")\n"
"endfunction\n"
"function cxIntervene takes nothing returns nothing\n"
"call cxPrecondition()\n"
"set cxIntervening=true\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"stop\"),\"Stop A\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==0 and GetUnitCurrentOrder(ciHippo)==0,\"both inverse heads clear\")\n"
"endfunction\n"
"function cxResume takes nothing returns nothing\n"
"endfunction\n"
"function cxVerify takes nothing returns nothing\n"
"call BJassAssert(cxN==6,\"exact event count\")\n"
"call BJassAssert(cxKind[0]==1 and cxOrder[0]==852508 and cxActor[0]==1 and cxTarget[0]==0,\"event 0 kind order actor target\")\n"
"call BJassAssert(cxHeadA[0]==852508 and cxHeadH[0]==0,\"event 0 public heads\")\n"
"call BJassAssert(cxKind[1]==2 and cxOrder[1]==852508 and cxActor[1]==1 and cxTarget[1]==2,\"event 1 kind order actor target\")\n"
"call BJassAssert(cxHeadA[1]==852508 and cxHeadH[1]==0,\"event 1 public heads\")\n"
"call BJassAssert(cxKind[2]==2 and cxOrder[2]==852507 and cxActor[2]==2 and cxTarget[2]==1,\"event 2 kind order actor target\")\n"
"call BJassAssert(cxHeadA[2]==852508 and cxHeadH[2]==852507,\"event 2 public heads\")\n"
"call BJassAssert(cxKind[3]==1 and cxOrder[3]==851972 and cxActor[3]==1 and cxTarget[3]==0,\"event 3 kind order actor target\")\n"
"call BJassAssert(cxHeadA[3]==851972 and cxHeadH[3]==852507,\"event 3 public heads\")\n"
"call BJassAssert(cxKind[4]==1 and cxOrder[4]==851972 and cxActor[4]==2 and cxTarget[4]==0,\"event 4 kind order actor target\")\n"
"call BJassAssert(cxHeadA[4]==0 and cxHeadH[4]==851972,\"event 4 public heads\")\n"
"call BJassAssert(cxKind[5]==1 and cxOrder[5]==851972 and cxActor[5]==1 and cxTarget[5]==0,\"event 5 kind order actor target\")\n"
"call BJassAssert(cxHeadA[5]==851972 and cxHeadH[5]==0,\"event 5 public heads\")\n"
"call BJassAssert(cxRiders()==0 and cxDeaths==0,\"bounded merge and death count\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==0 and GetUnitCurrentOrder(ciHippo)==0,\"settled heads\")\n"
"call BJassAssert(GetUnitX(ciArcher)==cxAX and GetUnitY(ciArcher)==cxAY and GetUnitX(ciHippo)==cxHX and GetUnitY(ciHippo)==cxHY,\"Stop preserves intervention XY\")\n"
"endfunction\n",
"function cxStart takes nothing returns nothing\n"
"set cxScene=2\n"
"set cxMode=0\n"
"call cxInstall()\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial immediate accepted\")\n"
"endfunction\n"
"function cxIntervene takes nothing returns nothing\n"
"call cxPrecondition()\n"
"set cxIntervening=true\n"
"call BJassAssert(IssueImmediateOrder(ciHippo,\"stop\"),\"Stop H\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==0 and GetUnitCurrentOrder(ciHippo)==0,\"both inverse heads clear\")\n"
"endfunction\n"
"function cxResume takes nothing returns nothing\n"
"endfunction\n"
"function cxVerify takes nothing returns nothing\n"
"call BJassAssert(cxN==5,\"exact event count\")\n"
"call BJassAssert(cxKind[0]==1 and cxOrder[0]==852508 and cxActor[0]==1 and cxTarget[0]==0,\"event 0 kind order actor target\")\n"
"call BJassAssert(cxHeadA[0]==852508 and cxHeadH[0]==0,\"event 0 public heads\")\n"
"call BJassAssert(cxKind[1]==2 and cxOrder[1]==852508 and cxActor[1]==1 and cxTarget[1]==2,\"event 1 kind order actor target\")\n"
"call BJassAssert(cxHeadA[1]==852508 and cxHeadH[1]==0,\"event 1 public heads\")\n"
"call BJassAssert(cxKind[2]==2 and cxOrder[2]==852507 and cxActor[2]==2 and cxTarget[2]==1,\"event 2 kind order actor target\")\n"
"call BJassAssert(cxHeadA[2]==852508 and cxHeadH[2]==852507,\"event 2 public heads\")\n"
"call BJassAssert(cxKind[3]==1 and cxOrder[3]==851972 and cxActor[3]==1 and cxTarget[3]==0,\"event 3 kind order actor target\")\n"
"call BJassAssert(cxHeadA[3]==851972 and cxHeadH[3]==852507,\"event 3 public heads\")\n"
"call BJassAssert(cxKind[4]==1 and cxOrder[4]==851972 and cxActor[4]==2 and cxTarget[4]==0,\"event 4 kind order actor target\")\n"
"call BJassAssert(cxHeadA[4]==0 and cxHeadH[4]==851972,\"event 4 public heads\")\n"
"call BJassAssert(cxRiders()==0 and cxDeaths==0,\"bounded merge and death count\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==0 and GetUnitCurrentOrder(ciHippo)==0,\"settled heads\")\n"
"call BJassAssert(GetUnitX(ciArcher)==cxAX and GetUnitY(ciArcher)==cxAY and GetUnitX(ciHippo)==cxHX and GetUnitY(ciHippo)==cxHY,\"Stop preserves intervention XY\")\n"
"endfunction\n",
"function cxStart takes nothing returns nothing\n"
"set cxScene=3\n"
"set cxMode=0\n"
"call cxInstall()\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial immediate accepted\")\n"
"endfunction\n"
"function cxIntervene takes nothing returns nothing\n"
"call cxPrecondition()\n"
"set cxIntervening=true\n"
"call BJassAssert(IssuePointOrder(ciArcher,\"move\",512,768),\"Move A\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==851986 and GetUnitCurrentOrder(ciHippo)==0,\"role-specific retained head\")\n"
"endfunction\n"
"function cxResume takes nothing returns nothing\n"
"endfunction\n"
"function cxVerify takes nothing returns nothing\n"
"call BJassAssert(cxN==6,\"exact event count\")\n"
"call BJassAssert(cxKind[0]==1 and cxOrder[0]==852508 and cxActor[0]==1 and cxTarget[0]==0,\"event 0 kind order actor target\")\n"
"call BJassAssert(cxHeadA[0]==852508 and cxHeadH[0]==0,\"event 0 public heads\")\n"
"call BJassAssert(cxKind[1]==2 and cxOrder[1]==852508 and cxActor[1]==1 and cxTarget[1]==2,\"event 1 kind order actor target\")\n"
"call BJassAssert(cxHeadA[1]==852508 and cxHeadH[1]==0,\"event 1 public heads\")\n"
"call BJassAssert(cxKind[2]==2 and cxOrder[2]==852507 and cxActor[2]==2 and cxTarget[2]==1,\"event 2 kind order actor target\")\n"
"call BJassAssert(cxHeadA[2]==852508 and cxHeadH[2]==852507,\"event 2 public heads\")\n"
"call BJassAssert(cxKind[3]==1 and cxOrder[3]==851972 and cxActor[3]==1 and cxTarget[3]==0,\"event 3 kind order actor target\")\n"
"call BJassAssert(cxHeadA[3]==851972 and cxHeadH[3]==852507,\"event 3 public heads\")\n"
"call BJassAssert(cxKind[4]==1 and cxOrder[4]==851972 and cxActor[4]==2 and cxTarget[4]==0,\"event 4 kind order actor target\")\n"
"call BJassAssert(cxHeadA[4]==0 and cxHeadH[4]==851972,\"event 4 public heads\")\n"
"call BJassAssert(cxKind[5]==3 and cxOrder[5]==851986 and cxActor[5]==1 and cxTarget[5]==0,\"event 5 kind order actor target\")\n"
"call BJassAssert(cxHeadA[5]==851986 and cxHeadH[5]==0,\"event 5 public heads\")\n"
"call BJassAssert(cxRiders()==0 and cxDeaths==0,\"bounded merge and death count\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==0 and GetUnitCurrentOrder(ciHippo)==0,\"settled heads\")\n"
"call BJassAssert(GetUnitX(ciArcher)<cxAX,\"surviving physical movement\")\n"
"endfunction\n",
"function cxStart takes nothing returns nothing\n"
"set cxScene=4\n"
"set cxMode=0\n"
"call cxInstall()\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial immediate accepted\")\n"
"endfunction\n"
"function cxIntervene takes nothing returns nothing\n"
"call cxPrecondition()\n"
"set cxIntervening=true\n"
"call BJassAssert(IssuePointOrder(ciHippo,\"move\",1792,768),\"Move H\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==0 and GetUnitCurrentOrder(ciHippo)==851986,\"role-specific retained head\")\n"
"endfunction\n"
"function cxResume takes nothing returns nothing\n"
"endfunction\n"
"function cxVerify takes nothing returns nothing\n"
"call BJassAssert(cxN==5,\"exact event count\")\n"
"call BJassAssert(cxKind[0]==1 and cxOrder[0]==852508 and cxActor[0]==1 and cxTarget[0]==0,\"event 0 kind order actor target\")\n"
"call BJassAssert(cxHeadA[0]==852508 and cxHeadH[0]==0,\"event 0 public heads\")\n"
"call BJassAssert(cxKind[1]==2 and cxOrder[1]==852508 and cxActor[1]==1 and cxTarget[1]==2,\"event 1 kind order actor target\")\n"
"call BJassAssert(cxHeadA[1]==852508 and cxHeadH[1]==0,\"event 1 public heads\")\n"
"call BJassAssert(cxKind[2]==2 and cxOrder[2]==852507 and cxActor[2]==2 and cxTarget[2]==1,\"event 2 kind order actor target\")\n"
"call BJassAssert(cxHeadA[2]==852508 and cxHeadH[2]==852507,\"event 2 public heads\")\n"
"call BJassAssert(cxKind[3]==1 and cxOrder[3]==851972 and cxActor[3]==1 and cxTarget[3]==0,\"event 3 kind order actor target\")\n"
"call BJassAssert(cxHeadA[3]==851972 and cxHeadH[3]==852507,\"event 3 public heads\")\n"
"call BJassAssert(cxKind[4]==3 and cxOrder[4]==851986 and cxActor[4]==2 and cxTarget[4]==0,\"event 4 kind order actor target\")\n"
"call BJassAssert(cxHeadA[4]==0 and cxHeadH[4]==851986,\"event 4 public heads\")\n"
"call BJassAssert(cxRiders()==0 and cxDeaths==0,\"bounded merge and death count\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==0 and GetUnitCurrentOrder(ciHippo)==0,\"settled heads\")\n"
"call BJassAssert(GetUnitX(ciHippo)>cxHX,\"surviving physical movement\")\n"
"endfunction\n",
"function cxStart takes nothing returns nothing\n"
"set cxScene=5\n"
"set cxMode=0\n"
"call cxInstall()\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial immediate accepted\")\n"
"endfunction\n"
"function cxIntervene takes nothing returns nothing\n"
"call cxPrecondition()\n"
"set cxIntervening=true\n"
"call RemoveUnit(ciArcher)\n"
"endfunction\n"
"function cxResume takes nothing returns nothing\n"
"endfunction\n"
"function cxVerify takes nothing returns nothing\n"
"call BJassAssert(cxN==5,\"exact event count\")\n"
"call BJassAssert(cxKind[0]==1 and cxOrder[0]==852508 and cxActor[0]==1 and cxTarget[0]==0,\"event 0 kind order actor target\")\n"
"call BJassAssert(cxHeadA[0]==852508 and cxHeadH[0]==0,\"event 0 public heads\")\n"
"call BJassAssert(cxKind[1]==2 and cxOrder[1]==852508 and cxActor[1]==1 and cxTarget[1]==2,\"event 1 kind order actor target\")\n"
"call BJassAssert(cxHeadA[1]==852508 and cxHeadH[1]==0,\"event 1 public heads\")\n"
"call BJassAssert(cxKind[2]==2 and cxOrder[2]==852507 and cxActor[2]==2 and cxTarget[2]==1,\"event 2 kind order actor target\")\n"
"call BJassAssert(cxHeadA[2]==852508 and cxHeadH[2]==852507,\"event 2 public heads\")\n"
"call BJassAssert(cxKind[3]==1 and cxOrder[3]==851972 and cxActor[3]==1 and cxTarget[3]==0,\"event 3 kind order actor target\")\n"
"call BJassAssert(cxHeadA[3]==851972 and cxHeadH[3]==852507,\"event 3 public heads\")\n"
"call BJassAssert(cxKind[4]==2 and cxOrder[4]==852507 and cxActor[4]==2 and cxTarget[4]==1,\"event 4 kind order actor target\")\n"
"call BJassAssert(cxHeadA[4]==0 and cxHeadH[4]==852507,\"event 4 public heads\")\n"
"call BJassAssert(cxRiders()==0 and cxDeaths==0,\"bounded merge and death count\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==0 and GetUnitCurrentOrder(ciHippo)==0,\"settled heads\")\n"
"call BJassAssert(GetUnitTypeId(ciArcher)==0 and GetUnitTypeId(ciHippo)=='ehip',\"removed actor identity only\")\n"
"endfunction\n",
"function cxStart takes nothing returns nothing\n"
"set cxScene=6\n"
"set cxMode=0\n"
"call cxInstall()\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial immediate accepted\")\n"
"endfunction\n"
"function cxIntervene takes nothing returns nothing\n"
"call cxPrecondition()\n"
"set cxIntervening=true\n"
"call RemoveUnit(ciHippo)\n"
"endfunction\n"
"function cxResume takes nothing returns nothing\n"
"endfunction\n"
"function cxVerify takes nothing returns nothing\n"
"call BJassAssert(cxN==4,\"exact event count\")\n"
"call BJassAssert(cxKind[0]==1 and cxOrder[0]==852508 and cxActor[0]==1 and cxTarget[0]==0,\"event 0 kind order actor target\")\n"
"call BJassAssert(cxHeadA[0]==852508 and cxHeadH[0]==0,\"event 0 public heads\")\n"
"call BJassAssert(cxKind[1]==2 and cxOrder[1]==852508 and cxActor[1]==1 and cxTarget[1]==2,\"event 1 kind order actor target\")\n"
"call BJassAssert(cxHeadA[1]==852508 and cxHeadH[1]==0,\"event 1 public heads\")\n"
"call BJassAssert(cxKind[2]==2 and cxOrder[2]==852507 and cxActor[2]==2 and cxTarget[2]==1,\"event 2 kind order actor target\")\n"
"call BJassAssert(cxHeadA[2]==852508 and cxHeadH[2]==852507,\"event 2 public heads\")\n"
"call BJassAssert(cxKind[3]==1 and cxOrder[3]==851972 and cxActor[3]==1 and cxTarget[3]==0,\"event 3 kind order actor target\")\n"
"call BJassAssert(cxHeadA[3]==851972 and cxHeadH[3]==852507,\"event 3 public heads\")\n"
"call BJassAssert(cxRiders()==0 and cxDeaths==0,\"bounded merge and death count\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==0 and GetUnitCurrentOrder(ciHippo)==0,\"settled heads\")\n"
"call BJassAssert(GetUnitTypeId(ciHippo)==0 and GetUnitTypeId(ciArcher)=='earc',\"removed actor identity only\")\n"
"endfunction\n",
"function cxStart takes nothing returns nothing\n"
"set cxScene=7\n"
"set cxMode=0\n"
"call cxInstall()\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial immediate accepted\")\n"
"endfunction\n"
"function cxIntervene takes nothing returns nothing\n"
"call cxPrecondition()\n"
"set cxIntervening=true\n"
"call PauseUnit(ciArcher,true)\n"
"call BJassAssert(IsUnitPaused(ciArcher) and not IsUnitPaused(ciHippo) and GetUnitCurrentOrder(ciArcher)==851973 and GetUnitCurrentOrder(ciHippo)==0,\"pause inverse and flags\")\n"
"set cxAX=GetUnitX(ciArcher)\n"
"set cxAY=GetUnitY(ciArcher)\n"
"set cxHX=GetUnitX(ciHippo)\n"
"set cxHY=GetUnitY(ciHippo)\n"
"endfunction\n"
"function cxResume takes nothing returns nothing\n"
"call BJassAssert(GetUnitX(ciArcher)==cxAX and GetUnitY(ciArcher)==cxAY and IsUnitPaused(ciArcher) and GetUnitCurrentOrder(ciArcher)==851973,\"paused pose and head retained\")\n"
"call PauseUnit(ciArcher,false)\n"
"call BJassAssert(not IsUnitPaused(ciArcher),\"resume flag\")\n"
"endfunction\n"
"function cxVerify takes nothing returns nothing\n"
"call BJassAssert(cxN==6,\"exact event count\")\n"
"call BJassAssert(cxKind[0]==1 and cxOrder[0]==852508 and cxActor[0]==1 and cxTarget[0]==0,\"event 0 kind order actor target\")\n"
"call BJassAssert(cxHeadA[0]==852508 and cxHeadH[0]==0,\"event 0 public heads\")\n"
"call BJassAssert(cxKind[1]==2 and cxOrder[1]==852508 and cxActor[1]==1 and cxTarget[1]==2,\"event 1 kind order actor target\")\n"
"call BJassAssert(cxHeadA[1]==852508 and cxHeadH[1]==0,\"event 1 public heads\")\n"
"call BJassAssert(cxKind[2]==2 and cxOrder[2]==852507 and cxActor[2]==2 and cxTarget[2]==1,\"event 2 kind order actor target\")\n"
"call BJassAssert(cxHeadA[2]==852508 and cxHeadH[2]==852507,\"event 2 public heads\")\n"
"call BJassAssert(cxKind[3]==1 and cxOrder[3]==851972 and cxActor[3]==1 and cxTarget[3]==0,\"event 3 kind order actor target\")\n"
"call BJassAssert(cxHeadA[3]==851972 and cxHeadH[3]==852507,\"event 3 public heads\")\n"
"call BJassAssert(cxKind[4]==1 and cxOrder[4]==851972 and cxActor[4]==2 and cxTarget[4]==0,\"event 4 kind order actor target\")\n"
"call BJassAssert(cxHeadA[4]==0 and cxHeadH[4]==851972,\"event 4 public heads\")\n"
"call BJassAssert(cxKind[5]==3 and cxOrder[5]==851973 and cxActor[5]==1 and cxTarget[5]==0,\"event 5 kind order actor target\")\n"
"call BJassAssert(cxHeadA[5]==851973 and cxHeadH[5]==0,\"event 5 public heads\")\n"
"call BJassAssert(cxRiders()==0 and cxDeaths==0,\"bounded merge and death count\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==0 and GetUnitCurrentOrder(ciHippo)==0,\"settled heads\")\n"
"endfunction\n",
"function cxStart takes nothing returns nothing\n"
"set cxScene=8\n"
"set cxMode=0\n"
"call cxInstall()\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial immediate accepted\")\n"
"endfunction\n"
"function cxIntervene takes nothing returns nothing\n"
"call cxPrecondition()\n"
"set cxIntervening=true\n"
"call PauseUnit(ciHippo,true)\n"
"call BJassAssert(IsUnitPaused(ciHippo) and not IsUnitPaused(ciArcher) and GetUnitCurrentOrder(ciHippo)==851973 and GetUnitCurrentOrder(ciArcher)==0,\"pause inverse and flags\")\n"
"set cxAX=GetUnitX(ciArcher)\n"
"set cxAY=GetUnitY(ciArcher)\n"
"set cxHX=GetUnitX(ciHippo)\n"
"set cxHY=GetUnitY(ciHippo)\n"
"endfunction\n"
"function cxResume takes nothing returns nothing\n"
"call BJassAssert(GetUnitX(ciHippo)==cxHX and GetUnitY(ciHippo)==cxHY and IsUnitPaused(ciHippo) and GetUnitCurrentOrder(ciHippo)==851973,\"paused pose and head retained\")\n"
"call PauseUnit(ciHippo,false)\n"
"call BJassAssert(not IsUnitPaused(ciHippo),\"resume flag\")\n"
"endfunction\n"
"function cxVerify takes nothing returns nothing\n"
"call BJassAssert(cxN==6,\"exact event count\")\n"
"call BJassAssert(cxKind[0]==1 and cxOrder[0]==852508 and cxActor[0]==1 and cxTarget[0]==0,\"event 0 kind order actor target\")\n"
"call BJassAssert(cxHeadA[0]==852508 and cxHeadH[0]==0,\"event 0 public heads\")\n"
"call BJassAssert(cxKind[1]==2 and cxOrder[1]==852508 and cxActor[1]==1 and cxTarget[1]==2,\"event 1 kind order actor target\")\n"
"call BJassAssert(cxHeadA[1]==852508 and cxHeadH[1]==0,\"event 1 public heads\")\n"
"call BJassAssert(cxKind[2]==2 and cxOrder[2]==852507 and cxActor[2]==2 and cxTarget[2]==1,\"event 2 kind order actor target\")\n"
"call BJassAssert(cxHeadA[2]==852508 and cxHeadH[2]==852507,\"event 2 public heads\")\n"
"call BJassAssert(cxKind[3]==1 and cxOrder[3]==851972 and cxActor[3]==1 and cxTarget[3]==0,\"event 3 kind order actor target\")\n"
"call BJassAssert(cxHeadA[3]==851972 and cxHeadH[3]==852507,\"event 3 public heads\")\n"
"call BJassAssert(cxKind[4]==3 and cxOrder[4]==851973 and cxActor[4]==2 and cxTarget[4]==0,\"event 4 kind order actor target\")\n"
"call BJassAssert(cxHeadA[4]==0 and cxHeadH[4]==851973,\"event 4 public heads\")\n"
"call BJassAssert(cxKind[5]==2 and cxOrder[5]==852507 and cxActor[5]==2 and cxTarget[5]==1,\"event 5 kind order actor target\")\n"
"call BJassAssert(cxHeadA[5]==0 and cxHeadH[5]==852507,\"event 5 public heads\")\n"
"call BJassAssert(cxRiders()==1 and cxDeaths==2,\"bounded merge and death count\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==0 and GetUnitCurrentOrder(ciHippo)==0,\"settled heads\")\n"
"endfunction\n",
"function cxStart takes nothing returns nothing\n"
"set cxScene=9\n"
"set cxMode=0\n"
"call cxInstall()\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial immediate accepted\")\n"
"endfunction\n"
"function cxIntervene takes nothing returns nothing\n"
"call cxPrecondition()\n"
"set cxIntervening=true\n"
"call BJassAssert(UnitRemoveAbility(ciArcher,'Aco2'),\"detach Aco2\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==0 and GetUnitCurrentOrder(ciHippo)==0,\"both inverse heads clear\")\n"
"call BJassAssert(GetUnitAbilityLevel(ciArcher,'Aco2')==0 and GetUnitAbilityLevel(ciHippo,'Aco3')==1,\"exact removed ability\")\n"
"endfunction\n"
"function cxResume takes nothing returns nothing\n"
"endfunction\n"
"function cxVerify takes nothing returns nothing\n"
"call BJassAssert(cxN==4,\"exact event count\")\n"
"call BJassAssert(cxKind[0]==1 and cxOrder[0]==852508 and cxActor[0]==1 and cxTarget[0]==0,\"event 0 kind order actor target\")\n"
"call BJassAssert(cxHeadA[0]==852508 and cxHeadH[0]==0,\"event 0 public heads\")\n"
"call BJassAssert(cxKind[1]==2 and cxOrder[1]==852508 and cxActor[1]==1 and cxTarget[1]==2,\"event 1 kind order actor target\")\n"
"call BJassAssert(cxHeadA[1]==852508 and cxHeadH[1]==0,\"event 1 public heads\")\n"
"call BJassAssert(cxKind[2]==2 and cxOrder[2]==852507 and cxActor[2]==2 and cxTarget[2]==1,\"event 2 kind order actor target\")\n"
"call BJassAssert(cxHeadA[2]==852508 and cxHeadH[2]==852507,\"event 2 public heads\")\n"
"call BJassAssert(cxKind[3]==1 and cxOrder[3]==851972 and cxActor[3]==2 and cxTarget[3]==0,\"event 3 kind order actor target\")\n"
"call BJassAssert(cxHeadA[3]==852508 and cxHeadH[3]==851972,\"event 3 public heads\")\n"
"call BJassAssert(cxRiders()==0 and cxDeaths==0,\"bounded merge and death count\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==0 and GetUnitCurrentOrder(ciHippo)==0,\"settled heads\")\n"
"endfunction\n",
"function cxStart takes nothing returns nothing\n"
"set cxScene=10\n"
"set cxMode=0\n"
"call cxInstall()\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial immediate accepted\")\n"
"endfunction\n"
"function cxIntervene takes nothing returns nothing\n"
"call cxPrecondition()\n"
"set cxIntervening=true\n"
"call BJassAssert(UnitRemoveAbility(ciHippo,'Aco3'),\"detach Aco3\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==0 and GetUnitCurrentOrder(ciHippo)==852507,\"role-specific retained head\")\n"
"call BJassAssert(GetUnitAbilityLevel(ciArcher,'Aco2')==1 and GetUnitAbilityLevel(ciHippo,'Aco3')==0,\"exact removed ability\")\n"
"endfunction\n"
"function cxResume takes nothing returns nothing\n"
"endfunction\n"
"function cxVerify takes nothing returns nothing\n"
"call BJassAssert(cxN==4,\"exact event count\")\n"
"call BJassAssert(cxKind[0]==1 and cxOrder[0]==852508 and cxActor[0]==1 and cxTarget[0]==0,\"event 0 kind order actor target\")\n"
"call BJassAssert(cxHeadA[0]==852508 and cxHeadH[0]==0,\"event 0 public heads\")\n"
"call BJassAssert(cxKind[1]==2 and cxOrder[1]==852508 and cxActor[1]==1 and cxTarget[1]==2,\"event 1 kind order actor target\")\n"
"call BJassAssert(cxHeadA[1]==852508 and cxHeadH[1]==0,\"event 1 public heads\")\n"
"call BJassAssert(cxKind[2]==2 and cxOrder[2]==852507 and cxActor[2]==2 and cxTarget[2]==1,\"event 2 kind order actor target\")\n"
"call BJassAssert(cxHeadA[2]==852508 and cxHeadH[2]==852507,\"event 2 public heads\")\n"
"call BJassAssert(cxKind[3]==1 and cxOrder[3]==851972 and cxActor[3]==1 and cxTarget[3]==0,\"event 3 kind order actor target\")\n"
"call BJassAssert(cxHeadA[3]==851972 and cxHeadH[3]==852507,\"event 3 public heads\")\n"
"call BJassAssert(cxRiders()==0 and cxDeaths==0,\"bounded merge and death count\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==0 and GetUnitCurrentOrder(ciHippo)==0,\"settled heads\")\n"
"call BJassAssert(GetUnitX(ciHippo)<cxHX,\"surviving physical movement\")\n"
"endfunction\n"
};

TEST(wc3_couple_instant_orders, native_lifecycle_public_contract) {
    FOR_LOOP(scene,sizeof(couple_instant_lifecycle_cases)/sizeof(*couple_instant_lifecycle_cases)) {
        coupleInstantFixture_t f;CoupleInstantBegin(&f,704,false);
        T_ASSERT(CoupleInstantCompile(couple_instant_inverse_script));
        T_ASSERT(CoupleInstantCompile(couple_instant_lifecycle_cases[scene]));
        CoupleInstantCall("cxStart");
        FOR_LOOP(i,60)CoupleInstantStep(); /* 300 ms: 15 native ticks, age 100 -> 115 */
        CoupleInstantCall("cxIntervene");
        FOR_LOOP(i,400)CoupleInstantStep(); /* 2000 ms: 100 native ticks, age 115 -> 215 */
        CoupleInstantCall("cxResume");
        FOR_LOOP(i,732)CoupleInstantStep(); /* 3660 ms: age 215 -> 398; no trajectory parity claim */
        CoupleInstantCall("cxVerify");
        CoupleInstantEnd(&f);
    }
}

static cstring_t const couple_instant_callbacks_cases[] = {
"function cxStart takes nothing returns nothing\n"
"set cxScene=0\n"
"set cxMode=1\n"
"call cxInstall()\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial immediate accepted\")\n"
"endfunction\n"
"function cxIntervene takes nothing returns nothing\n"
"set cxIntervening=true\n"
"endfunction\n"
"function cxResume takes nothing returns nothing\n"
"endfunction\n"
"function cxVerify takes nothing returns nothing\n"
"call BJassAssert(cxN==4,\"exact event count\")\n"
"call BJassAssert(cxKind[0]==1 and cxOrder[0]==852508 and cxActor[0]==1 and cxTarget[0]==0,\"event 0 kind order actor target\")\n"
"call BJassAssert(cxHeadA[0]==852508 and cxHeadH[0]==0,\"event 0 public heads\")\n"
"call BJassAssert(cxKind[1]==2 and cxOrder[1]==852508 and cxActor[1]==1 and cxTarget[1]==2,\"event 1 kind order actor target\")\n"
"call BJassAssert(cxHeadA[1]==852508 and cxHeadH[1]==0,\"event 1 public heads\")\n"
"call BJassAssert(cxKind[2]==2 and cxOrder[2]==852507 and cxActor[2]==2 and cxTarget[2]==1,\"event 2 kind order actor target\")\n"
"call BJassAssert(cxHeadA[2]==852508 and cxHeadH[2]==852507,\"event 2 public heads\")\n"
"call BJassAssert(cxKind[3]==1 and cxOrder[3]==851972 and cxActor[3]==1 and cxTarget[3]==0,\"event 3 kind order actor target\")\n"
"call BJassAssert(cxHeadA[3]==851972 and cxHeadH[3]==852507,\"event 3 public heads\")\n"
"call BJassAssert(cxRiders()==1 and cxDeaths==2,\"bounded merge and death count\")\n"
"call BJassAssert(not cxNested,\"exact nested callback exercised\")\n"
"endfunction\n",
"function cxStart takes nothing returns nothing\n"
"set cxScene=1\n"
"set cxMode=1\n"
"call cxInstall()\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial immediate accepted\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==851986 and GetUnitCurrentOrder(ciHippo)==852507,\"callback return ownership\")\n"
"endfunction\n"
"function cxIntervene takes nothing returns nothing\n"
"set cxIntervening=true\n"
"endfunction\n"
"function cxResume takes nothing returns nothing\n"
"endfunction\n"
"function cxVerify takes nothing returns nothing\n"
"call BJassAssert(cxN==4,\"exact event count\")\n"
"call BJassAssert(cxKind[0]==1 and cxOrder[0]==852508 and cxActor[0]==1 and cxTarget[0]==0,\"event 0 kind order actor target\")\n"
"call BJassAssert(cxHeadA[0]==852508 and cxHeadH[0]==0,\"event 0 public heads\")\n"
"call BJassAssert(cxKind[1]==2 and cxOrder[1]==852508 and cxActor[1]==1 and cxTarget[1]==2,\"event 1 kind order actor target\")\n"
"call BJassAssert(cxHeadA[1]==852508 and cxHeadH[1]==0,\"event 1 public heads\")\n"
"call BJassAssert(cxKind[2]==3 and cxOrder[2]==851986 and cxActor[2]==1 and cxTarget[2]==0,\"event 2 kind order actor target\")\n"
"call BJassAssert(cxHeadA[2]==851986 and cxHeadH[2]==0,\"event 2 public heads\")\n"
"call BJassAssert(cxKind[3]==2 and cxOrder[3]==852507 and cxActor[3]==2 and cxTarget[3]==1,\"event 3 kind order actor target\")\n"
"call BJassAssert(cxHeadA[3]==851986 and cxHeadH[3]==852507,\"event 3 public heads\")\n"
"call BJassAssert(cxRiders()==1 and cxDeaths==2,\"bounded merge and death count\")\n"
"call BJassAssert(cxNested,\"exact nested callback exercised\")\n"
"endfunction\n",
"function cxStart takes nothing returns nothing\n"
"set cxScene=2\n"
"set cxMode=1\n"
"call cxInstall()\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial immediate accepted\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==852508 and GetUnitCurrentOrder(ciHippo)==851986,\"callback return ownership\")\n"
"endfunction\n"
"function cxIntervene takes nothing returns nothing\n"
"set cxIntervening=true\n"
"endfunction\n"
"function cxResume takes nothing returns nothing\n"
"endfunction\n"
"function cxVerify takes nothing returns nothing\n"
"call BJassAssert(cxN==4,\"exact event count\")\n"
"call BJassAssert(cxKind[0]==1 and cxOrder[0]==852508 and cxActor[0]==1 and cxTarget[0]==0,\"event 0 kind order actor target\")\n"
"call BJassAssert(cxHeadA[0]==852508 and cxHeadH[0]==0,\"event 0 public heads\")\n"
"call BJassAssert(cxKind[1]==2 and cxOrder[1]==852508 and cxActor[1]==1 and cxTarget[1]==2,\"event 1 kind order actor target\")\n"
"call BJassAssert(cxHeadA[1]==852508 and cxHeadH[1]==0,\"event 1 public heads\")\n"
"call BJassAssert(cxKind[2]==2 and cxOrder[2]==852507 and cxActor[2]==2 and cxTarget[2]==1,\"event 2 kind order actor target\")\n"
"call BJassAssert(cxHeadA[2]==852508 and cxHeadH[2]==852507,\"event 2 public heads\")\n"
"call BJassAssert(cxKind[3]==3 and cxOrder[3]==851986 and cxActor[3]==2 and cxTarget[3]==0,\"event 3 kind order actor target\")\n"
"call BJassAssert(cxHeadA[3]==852508 and cxHeadH[3]==851986,\"event 3 public heads\")\n"
"call BJassAssert(cxRiders()==0 and cxDeaths==0,\"bounded merge and death count\")\n"
"call BJassAssert(cxNested,\"exact nested callback exercised\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==852508 and GetUnitCurrentOrder(ciHippo)==0 and GetUnitX(ciHippo)>1700,\"lone ready Archer and completed peer Move\")\n"
"endfunction\n",
"function cxStart takes nothing returns nothing\n"
"set cxScene=3\n"
"set cxMode=1\n"
"call cxInstall()\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial immediate accepted\")\n"
"endfunction\n"
"function cxIntervene takes nothing returns nothing\n"
"call cxPrecondition()\n"
"set cxIntervening=true\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"stop\"),\"outer Stop\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==0 and GetUnitCurrentOrder(ciHippo)==0,\"outer Stop nested Move precedence\")\n"
"endfunction\n"
"function cxResume takes nothing returns nothing\n"
"endfunction\n"
"function cxVerify takes nothing returns nothing\n"
"call BJassAssert(cxN==7,\"exact event count\")\n"
"call BJassAssert(cxKind[0]==1 and cxOrder[0]==852508 and cxActor[0]==1 and cxTarget[0]==0,\"event 0 kind order actor target\")\n"
"call BJassAssert(cxHeadA[0]==852508 and cxHeadH[0]==0,\"event 0 public heads\")\n"
"call BJassAssert(cxKind[1]==2 and cxOrder[1]==852508 and cxActor[1]==1 and cxTarget[1]==2,\"event 1 kind order actor target\")\n"
"call BJassAssert(cxHeadA[1]==852508 and cxHeadH[1]==0,\"event 1 public heads\")\n"
"call BJassAssert(cxKind[2]==2 and cxOrder[2]==852507 and cxActor[2]==2 and cxTarget[2]==1,\"event 2 kind order actor target\")\n"
"call BJassAssert(cxHeadA[2]==852508 and cxHeadH[2]==852507,\"event 2 public heads\")\n"
"call BJassAssert(cxKind[3]==1 and cxOrder[3]==851972 and cxActor[3]==1 and cxTarget[3]==0,\"event 3 kind order actor target\")\n"
"call BJassAssert(cxHeadA[3]==851972 and cxHeadH[3]==852507,\"event 3 public heads\")\n"
"call BJassAssert(cxKind[4]==1 and cxOrder[4]==851972 and cxActor[4]==2 and cxTarget[4]==0,\"event 4 kind order actor target\")\n"
"call BJassAssert(cxHeadA[4]==0 and cxHeadH[4]==851972,\"event 4 public heads\")\n"
"call BJassAssert(cxKind[5]==3 and cxOrder[5]==851986 and cxActor[5]==1 and cxTarget[5]==0,\"event 5 kind order actor target\")\n"
"call BJassAssert(cxHeadA[5]==851986 and cxHeadH[5]==851972,\"event 5 public heads\")\n"
"call BJassAssert(cxKind[6]==1 and cxOrder[6]==851972 and cxActor[6]==1 and cxTarget[6]==0,\"event 6 kind order actor target\")\n"
"call BJassAssert(cxHeadA[6]==851972 and cxHeadH[6]==0,\"event 6 public heads\")\n"
"call BJassAssert(cxRiders()==0 and cxDeaths==0,\"bounded merge and death count\")\n"
"call BJassAssert(cxNested,\"exact nested callback exercised\")\n"
"call BJassAssert(GetUnitX(ciArcher)==cxAX and GetUnitY(ciArcher)==cxAY,\"outer source Stop suppresses nested travel\")\n"
"endfunction\n",
"function cxStart takes nothing returns nothing\n"
"set cxScene=4\n"
"set cxMode=1\n"
"call cxInstall()\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial immediate accepted\")\n"
"endfunction\n"
"function cxIntervene takes nothing returns nothing\n"
"call cxPrecondition()\n"
"set cxIntervening=true\n"
"call BJassAssert(IssueImmediateOrder(ciHippo,\"stop\"),\"outer Stop\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==851986 and GetUnitCurrentOrder(ciHippo)==0,\"outer Stop nested Move precedence\")\n"
"endfunction\n"
"function cxResume takes nothing returns nothing\n"
"endfunction\n"
"function cxVerify takes nothing returns nothing\n"
"call BJassAssert(cxN==6,\"exact event count\")\n"
"call BJassAssert(cxKind[0]==1 and cxOrder[0]==852508 and cxActor[0]==1 and cxTarget[0]==0,\"event 0 kind order actor target\")\n"
"call BJassAssert(cxHeadA[0]==852508 and cxHeadH[0]==0,\"event 0 public heads\")\n"
"call BJassAssert(cxKind[1]==2 and cxOrder[1]==852508 and cxActor[1]==1 and cxTarget[1]==2,\"event 1 kind order actor target\")\n"
"call BJassAssert(cxHeadA[1]==852508 and cxHeadH[1]==0,\"event 1 public heads\")\n"
"call BJassAssert(cxKind[2]==2 and cxOrder[2]==852507 and cxActor[2]==2 and cxTarget[2]==1,\"event 2 kind order actor target\")\n"
"call BJassAssert(cxHeadA[2]==852508 and cxHeadH[2]==852507,\"event 2 public heads\")\n"
"call BJassAssert(cxKind[3]==1 and cxOrder[3]==851972 and cxActor[3]==1 and cxTarget[3]==0,\"event 3 kind order actor target\")\n"
"call BJassAssert(cxHeadA[3]==851972 and cxHeadH[3]==852507,\"event 3 public heads\")\n"
"call BJassAssert(cxKind[4]==3 and cxOrder[4]==851986 and cxActor[4]==1 and cxTarget[4]==0,\"event 4 kind order actor target\")\n"
"call BJassAssert(cxHeadA[4]==851986 and cxHeadH[4]==852507,\"event 4 public heads\")\n"
"call BJassAssert(cxKind[5]==1 and cxOrder[5]==851972 and cxActor[5]==2 and cxTarget[5]==0,\"event 5 kind order actor target\")\n"
"call BJassAssert(cxHeadA[5]==851986 and cxHeadH[5]==851972,\"event 5 public heads\")\n"
"call BJassAssert(cxRiders()==0 and cxDeaths==0,\"bounded merge and death count\")\n"
"call BJassAssert(cxNested,\"exact nested callback exercised\")\n"
"call BJassAssert(GetUnitX(ciArcher)<704,\"nested source Move travels\")\n"
"endfunction\n",
"function cxStart takes nothing returns nothing\n"
"set cxScene=5\n"
"set cxMode=1\n"
"call cxInstall()\n"
"call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial immediate accepted\")\n"
"call BJassAssert(GetUnitCurrentOrder(ciArcher)==851986 and GetUnitCurrentOrder(ciHippo)==0,\"callback return ownership\")\n"
"endfunction\n"
"function cxIntervene takes nothing returns nothing\n"
"set cxIntervening=true\n"
"endfunction\n"
"function cxResume takes nothing returns nothing\n"
"endfunction\n"
"function cxVerify takes nothing returns nothing\n"
"call BJassAssert(cxN==2,\"exact event count\")\n"
"call BJassAssert(cxKind[0]==1 and cxOrder[0]==852508 and cxActor[0]==1 and cxTarget[0]==0,\"event 0 kind order actor target\")\n"
"call BJassAssert(cxHeadA[0]==852508 and cxHeadH[0]==0,\"event 0 public heads\")\n"
"call BJassAssert(cxKind[1]==3 and cxOrder[1]==851986 and cxActor[1]==1 and cxTarget[1]==0,\"event 1 kind order actor target\")\n"
"call BJassAssert(cxHeadA[1]==851986 and cxHeadH[1]==0,\"event 1 public heads\")\n"
"call BJassAssert(cxRiders()==0 and cxDeaths==0,\"bounded merge and death count\")\n"
"call BJassAssert(cxNested,\"exact nested callback exercised\")\n"
"call BJassAssert(GetUnitX(ciArcher)<704,\"nested source Move travels\")\n"
"call BJassAssert(GetUnitX(ciHippo)==1536.0 and GetUnitY(ciHippo)==1024.0,\"immediate replacement suppresses pairing\")\n"
"endfunction\n"
};

TEST(wc3_couple_instant_orders, native_callbacks_public_contract) {
    FOR_LOOP(scene,sizeof(couple_instant_callbacks_cases)/sizeof(*couple_instant_callbacks_cases)) {
        coupleInstantFixture_t f;CoupleInstantBegin(&f,704,false);
        T_ASSERT(CoupleInstantCompile(couple_instant_inverse_script));
        T_ASSERT(CoupleInstantCompile(couple_instant_callbacks_cases[scene]));
        CoupleInstantCall("cxStart");
        FOR_LOOP(i,60)CoupleInstantStep(); /* 300 ms: 15 native ticks, age 100 -> 115 */
        CoupleInstantCall("cxIntervene");
        FOR_LOOP(i,400)CoupleInstantStep(); /* 2000 ms: 100 native ticks, age 115 -> 215 */
        CoupleInstantCall("cxResume");
        FOR_LOOP(i,732)CoupleInstantStep(); /* 3660 ms: age 215 -> 398; no trajectory parity claim */
        CoupleInstantCall("cxVerify");
        CoupleInstantEnd(&f);
    }
}

/* Append to t_couple_instant_orders.c. Fixture must author UnitData.death
 * Archer=0.4, Hippo=1.2 seconds. Windows avoid exact scheduler quantization;
 * loading at0.9s must preserve Hippo's remaining deadline, not restart1.2s. */
TEST(wc3_couple_instant_orders, authored_retirement_deadlines_survive_load) {
    coupleInstantFixture_t f;CoupleInstantBegin(&f,704,false);
    char const script[]=
        "function ciRetirementIssue takes nothing returns nothing\n"
        "call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"retirement pairing admitted\")\nendfunction\n"
        "function ciRetirementBothPending takes nothing returns nothing\n"
        "call BJassAssert(GetUnitTypeId(ciArcher)=='earc' and GetUnitTypeId(ciHippo)=='ehip',\"original handles retained at merge\")\n"
        "call BJassAssert(GetWidgetLife(ciArcher)==0.0 and GetWidgetLife(ciHippo)==0.0,\"both originals consumed\")\nendfunction\n"
        "function ciRetirementHippoPending takes nothing returns nothing\n"
        "call BJassAssert(GetUnitTypeId(ciArcher)==0,\"short authored death has elapsed\")\n"
        "call BJassAssert(GetUnitTypeId(ciHippo)=='ehip' and GetWidgetLife(ciHippo)==0.0,\"long authored death still pending\")\nendfunction\n"
        "function ciRetirementComplete takes nothing returns nothing\n"
        "call BJassAssert(GetUnitTypeId(ciArcher)==0 and GetUnitTypeId(ciHippo)==0,\"both authored deaths elapsed\")\nendfunction\n";
    T_FEQ(f.archer->data.UnitData->death,0.4f,0.00001f);
    T_FEQ(f.hippo->data.UnitData->death,1.2f,0.00001f);
    T_ASSERT(CoupleInstantCompile(script));CoupleInstantCall("ciRetirementIssue");
    for(unsigned i=0;i<2000 && !CoupleInstantRiders();i++)CoupleInstantStep();
    T_EQ(CoupleInstantRiders(),1);
    CoupleInstantCall("ciRetirementBothPending");
    T_ASSERT(!G_UnitHasWorldPresence(f.archer));T_ASSERT(!G_UnitHasWorldPresence(f.hippo));
    FOR_LOOP(i,180)CoupleInstantStep(); /*0.9s: Archer released, Hippo pending.*/
    CoupleInstantCall("ciRetirementHippoPending");
    cstring_t path=Test_TempPath("couple-instant225-retirement.bin");
    T_ASSERT(WriteGame(path));
    FOR_LOOP(i,100)CoupleInstantStep(); /*1.4s from merge.*/
    CoupleInstantCall("ciRetirementComplete");T_EQ(CoupleInstantRiders(),1);
    T_ASSERT(ReadGame(path));
    CoupleInstantCall("ciRetirementHippoPending");
    FOR_LOOP(i,100)CoupleInstantStep();
    CoupleInstantCall("ciRetirementComplete");T_EQ(CoupleInstantRiders(),1);
    remove(path);CoupleInstantEnd(&f);
}


TEST(wc3_couple_instant_orders, authored_range_radius_boundary) {
    float positions[]={1163.0f,1162.75f,1163.25f};
    FOR_LOOP(scene,3) {
        coupleInstantFixture_t f;CoupleInstantBegin(&f,positions[scene],true);
        T_ASSERT(CoupleInstantCompile("function ciBoundary takes nothing returns nothing\ncall BJassAssert(IssueTargetOrder(ciHippo,\"coupletarget\",ciArcher),\"boundary admitted\")\nendfunction\n"));
        CoupleInstantCall("ciBoundary");
        T_EQ(f.hippo->current_order_id,852507);T_EQ(f.archer->current_order_id,0);
        moveGroup_t *group=CoupleInstantGroup(f.hippo);T_NOT_NULL(group);
        if(group) {
            T_ASSERT(group->receiver!=NULL);T_EQ(group->target,f.archer);
            T_EQ((group->flags&0x200u)!=0,scene!=1);
            if(scene==1)T_FEQ(group->members[0].arrival_range,11.65625f,0);
        }
        CoupleInstantEnd(&f);
    }
}


TEST(wc3_couple_instant_orders, explicit_source_uses_own_partner_type_only) {
    coupleInstantFixture_t f;CoupleInstantBeginAuthored(&f,704,true,"hfoo");
    T_EQ(S_SpellDataId(MAKEFOURCC('A','c','o','3'),1,1),MAKEFOURCC('h','f','o','o'));
    T_ASSERT(CoupleInstantCompile("function ciAsymmetric takes nothing returns nothing\ncall BJassAssert(IssueTargetOrder(ciArcher,\"coupletarget\",ciHippo),\"source own DataA admits asymmetric peer\")\nendfunction\n"));
    CoupleInstantCall("ciAsymmetric");T_EQ(f.archer->current_order_id,852507);T_EQ(f.hippo->current_order_id,0);
    FOR_LOOP(i,1200)CoupleInstantStep();T_EQ(f.archer->current_order_id,852507);T_EQ(CoupleInstantRiders(),0);
    CoupleInstantEnd(&f);
}


/* Accepted area-selection-v2: query radius adds only the candidate radius,
 * independently of spell Rng. Outside requests reject before publication. */
TEST(wc3_couple_instant_orders, automatic_area_candidate_radius_admission) {
    float offsets[]={0.0f,0.25f,-0.25f};
    FOR_LOOP(reverse,2)FOR_LOOP(edge,3) {
        float distance=(reverse ? 932.0f : 908.0f)+offsets[edge];
        coupleInstantFixture_t f;CoupleInstantBegin(&f,1536.0f-distance,false);
        T_FEQ(Vector2_distance(&f.archer->s.origin2,&f.hippo->s.origin2),distance,0);
        char script[2600];snprintf(script,sizeof(script),
            "globals\ninteger ciAreaEvents=0\nendglobals\n"
            "function ciAreaEvent takes nothing returns nothing\nset ciAreaEvents=ciAreaEvents+1\nendfunction\n"
            "function ciAreaIssue takes nothing returns nothing\nlocal trigger t=CreateTrigger()\nlocal boolean accepted=false\n"
            "call TriggerRegisterUnitEvent(t,ciArcher,EVENT_UNIT_ISSUED_ORDER)\n"
            "call TriggerRegisterUnitEvent(t,ciHippo,EVENT_UNIT_ISSUED_ORDER)\n"
            "call TriggerRegisterUnitEvent(t,ciArcher,EVENT_UNIT_ISSUED_TARGET_ORDER)\n"
            "call TriggerRegisterUnitEvent(t,ciHippo,EVENT_UNIT_ISSUED_TARGET_ORDER)\n"
            "call TriggerAddAction(t,function ciAreaEvent)\n"
            "set accepted=IssueImmediateOrder(%s,\"coupleinstant\")\n"
            "call BJassAssert(accepted==%s,\"Area plus candidate radius admission\")\n"
            "call BJassAssert(ciAreaEvents==%u,\"Area rejection precedes all issued events\")\n"
            "call BJassAssert(GetUnitCurrentOrder(ciArcher)==%u and GetUnitCurrentOrder(ciHippo)==%u,\"Area request public heads\")\nendfunction\n",
            reverse ? "ciHippo" : "ciArcher",edge==1 ? "false" : "true",
            edge==1 ? 0u : reverse ? 2u : 3u,
            edge==1 || reverse ? 0u : 852508u,edge==1 ? 0u : reverse ? 852508u : 852507u);
        T_ASSERT(CoupleInstantCompile(script));CoupleInstantCall("ciAreaIssue");
        if(edge==1) {
            T_ASSERT(!f.archer->couple_instant || !f.archer->couple_instant->active);
            T_ASSERT(!f.hippo->couple_instant || !f.hippo->couple_instant->active);
            T_EQ(CoupleInstantRiders(),0);
        }
        CoupleInstantEnd(&f);
    }
}

/* Captured equal-distance east/north traversal selects north regardless of
 * creation order. This is a concrete geometry witness, not a global tie rule. */
TEST(wc3_couple_instant_orders, automatic_nearest_and_spatial_tie_selection) {
    FOR_LOOP(tie,2)FOR_LOOP(reverse_creation,2) {
        coupleInstantFixture_t f;CoupleInstantBegin(&f,512,false);
        char script[3300];snprintf(script,sizeof(script),
            "globals\nunit ciChoiceTwo\nunit ciChoiceThree\ninteger ciChoices=0\nendglobals\n"
            "function ciChoiceEvent takes nothing returns nothing\n"
            "if GetTriggerUnit()==ciArcher and GetIssuedOrderId()==852508 then\n"
            "call BJassAssert(GetOrderTargetUnit()==%s,\"nearest or spatial tie candidate\")\n"
            "set ciChoices=ciChoices+1\nendif\nendfunction\n"
            "function ciChoose takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call RemoveUnit(ciHippo)\ncall SetUnitY(ciArcher,512.0)\n"
            "%s\n%s\n"
            "call SetUnitAcquireRange(ciChoiceTwo,0.0)\ncall SetUnitAcquireRange(ciChoiceThree,0.0)\n"
            "call BJassAssert(GetUnitX(ciArcher)==512.0 and GetUnitY(ciArcher)==512.0,\"selection source pose\")\n"
            "call BJassAssert(GetUnitX(ciChoiceTwo)==1012.0 and GetUnitY(ciChoiceTwo)==512.0,\"selection east pose\")\n"
            "call BJassAssert(GetUnitX(ciChoiceThree)==%s and GetUnitY(ciChoiceThree)==%s,\"selection other pose\")\n"
            "call TriggerRegisterUnitEvent(t,ciArcher,EVENT_UNIT_ISSUED_TARGET_ORDER)\n"
            "call TriggerAddAction(t,function ciChoiceEvent)\n"
            "call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"candidate selection admitted\")\n"
            "call BJassAssert(ciChoices==1,\"one automatic target publication\")\n"
            "call BJassAssert(GetUnitCurrentOrder(%s)==852507 and GetUnitCurrentOrder(%s)==0,\"only chosen peer receives reciprocal\")\nendfunction\n",
            tie ? "ciChoiceThree" : "ciChoiceTwo",
            reverse_creation ? (tie ? "set ciChoiceThree=CreateUnit(Player(0),'ehip',512.0,1012.0,0.0)" : "set ciChoiceThree=CreateUnit(Player(0),'ehip',1212.0,512.0,0.0)") : "set ciChoiceTwo=CreateUnit(Player(0),'ehip',1012.0,512.0,0.0)",
            reverse_creation ? "set ciChoiceTwo=CreateUnit(Player(0),'ehip',1012.0,512.0,0.0)" : (tie ? "set ciChoiceThree=CreateUnit(Player(0),'ehip',512.0,1012.0,0.0)" : "set ciChoiceThree=CreateUnit(Player(0),'ehip',1212.0,512.0,0.0)"),
            tie ? "512.0" : "1212.0",tie ? "1012.0" : "512.0",
            tie ? "ciChoiceThree" : "ciChoiceTwo",tie ? "ciChoiceTwo" : "ciChoiceThree");
        T_ASSERT(CoupleInstantCompile(script));CoupleInstantCall("ciChoose");
        CoupleInstantEnd(&f);
    }
}

/* Append inside existing t_couple_instant_orders.c BZ_TESTS section.
 * Integration regression, not a new native buff-family parity claim.
 * Real Bloodlust owner supplies policy and movement inverse; BTLF is the
 * existing excluded lifetime owner. No protected-metadata override. */
TEST(wc3_couple_instant_orders, merge_removes_applied_bloodlust_preserves_timed_life) {
    coupleInstantFixture_t f;CoupleInstantBegin(&f,704,false);
    edict_t *inputs[]={f.archer,f.hippo};
    FOR_LOOP(i,2) {
        edict_t *unit=inputs[i];
        heroabilitystatus_t *buff=unit_applystatus(unit,&(status_application_t){
            .buff="Bblo",.level=1,.duration=30.0f,
            .data=MAKEFOURCC('A','b','l','o'),
            .source_ability=MAKEFOURCC('A','b','l','o')});
        T_NOT_NULL(buff);
        if(buff) {
            /* Install a real applying-owner contribution. AbilityData in this
             * focused fixture contains only Aco2/Aco3; no synthetic Ablo row. */
            /* Application already installs the absent authored DataB as a
             * zero contribution. Replace that retained contribution explicitly. */
            S_UpdateMoveSpeedContribution(unit,buff,0.25f);
            T_EQ(buff->move_speed_applied,1);
            T_FEQ(unit->movement.speed_factor,1.25f,0.00001f);
        }
        T_NOT_NULL(S_SpellApplyTimedLife(unit,1,30.0f));
        T_EQ(G_UnitStatusClassLevel(unit,MAKEFOURCC('B','b','l','o')),1);
        T_EQ(G_UnitStatusClassLevel(unit,MAKEFOURCC('B','T','L','F')),1);
    }
    T_ASSERT(CoupleInstantCompile(
        "function ciBuffMerge takes nothing returns nothing\n"
        "call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"buff-bearing pairing admitted\")\n"
        "endfunction\n"));
    CoupleInstantCall("ciBuffMerge");
    for(unsigned i=0;i<2000 && !CoupleInstantRiders();i++)CoupleInstantStep();
    T_EQ(CoupleInstantRiders(),1);
    FOR_LOOP(i,2) {
        edict_t *unit=inputs[i];
        /* First rider publication precedes either authored .4/1.2s release. */
        T_ASSERT(unit->inuse);
        T_EQ(unit->class_id,i ? MAKEFOURCC('e','h','i','p') : MAKEFOURCC('e','a','r','c'));
        T_EQ(G_UnitStatusClassLevel(unit,MAKEFOURCC('B','b','l','o')),0);
        T_FEQ(unit->movement.speed_factor,1.0f,0.00001f);
        T_EQ(G_UnitStatusClassLevel(unit,MAKEFOURCC('B','T','L','F')),1);
    }
    CoupleInstantEnd(&f);
}


/* Original 6f4ef810 executes common approach cleanup but suppresses the peer
 * inverse for a replacement COrder whose ID equals Acoi v348 (852507).
 * This covers an approaching actor; the separate wait inverse has no exemption. */
TEST(wc3_couple_instant_orders, explicit_reissue_keeps_approach_peer_owner) {
    coupleInstantFixture_t f;CoupleInstantBegin(&f,704,false);
    T_ASSERT(CoupleInstantCompile(
        "globals\ninteger crStops=0\nendglobals\n"
        "function crObserve takes nothing returns nothing\n"
        "if GetIssuedOrderId()==851972 then\nset crStops=crStops+1\nendif\nendfunction\n"
        "function crBegin takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "call TriggerRegisterUnitEvent(t,ciArcher,EVENT_UNIT_ISSUED_ORDER)\n"
        "call TriggerRegisterUnitEvent(t,ciHippo,EVENT_UNIT_ISSUED_ORDER)\n"
        "call TriggerAddAction(t,function crObserve)\n"
        "call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial pair accepted\")\nendfunction\n"
        "function crReissue takes nothing returns nothing\n"
        "set crStops=0\n"
        "call BJassAssert(IssueTargetOrder(ciHippo,\"coupletarget\",ciArcher),\"explicit approach replacement accepted\")\n"
        "call SetUnitUserData(ciArcher,crStops)\nendfunction\n"));
    CoupleInstantCall("crBegin");
    FOR_LOOP(i,60)CoupleInstantStep();
    T_NOT_NULL(f.archer->couple_instant);T_NOT_NULL(f.hippo->couple_instant);
    T_ASSERT(f.archer->couple_instant->active && f.archer->couple_instant->waiting);
    T_ASSERT(f.hippo->couple_instant->active && !f.hippo->couple_instant->waiting);
    T_EQ(f.archer->current_order_id,852508);T_EQ(f.hippo->current_order_id,852507);
    T_NOT_NULL(CoupleInstantGroup(f.hippo));
    CoupleInstantCall("crReissue");
    T_EQ(f.archer->user_data,0);
    T_EQ(f.archer->current_order_id,852508);T_EQ(f.hippo->current_order_id,852507);
    T_ASSERT(f.archer->couple_instant->active && f.archer->couple_instant->waiting);
    T_ASSERT(f.hippo->couple_instant->active && !f.hippo->couple_instant->waiting);
    T_NOT_NULL(CoupleInstantGroup(f.hippo));
    CoupleInstantEnd(&f);
}

/* Integration positive control for the original4ef810 replacement predicate.
 * 852508 is not exempt: require observable peer Stop, without assigning a
 * whole-game native event sequence, exact count, or replacement outcome. */
TEST(wc3_couple_instant_orders, immediate_reissue_keeps_approach_peer_inverse) {
    coupleInstantFixture_t f;CoupleInstantBegin(&f,704,false);
    T_ASSERT(CoupleInstantCompile(
        "globals\ninteger cpPeerStops=0\nendglobals\n"
        "function cpObserve takes nothing returns nothing\n"
        "if GetTriggerUnit()==ciArcher and GetIssuedOrderId()==851972 then\n"
        "set cpPeerStops=cpPeerStops+1\nendif\nendfunction\n"
        "function cpBegin takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "call TriggerRegisterUnitEvent(t,ciArcher,EVENT_UNIT_ISSUED_ORDER)\n"
        "call TriggerAddAction(t,function cpObserve)\n"
        "call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial pair accepted\")\nendfunction\n"
        "function cpReissue takes nothing returns nothing\n"
        "set cpPeerStops=0\n"
        "call BJassAssert(IssueImmediateOrder(ciHippo,\"coupleinstant\"),\"immediate approach replacement accepted\")\n"
        "call SetUnitUserData(ciArcher,cpPeerStops)\nendfunction\n"));
    CoupleInstantCall("cpBegin");
    FOR_LOOP(i,60)CoupleInstantStep();
    T_NOT_NULL(f.archer->couple_instant);T_NOT_NULL(f.hippo->couple_instant);
    T_ASSERT(f.archer->couple_instant->active && f.archer->couple_instant->waiting);
    T_ASSERT(f.hippo->couple_instant->active && !f.hippo->couple_instant->waiting);
    T_EQ(f.archer->current_order_id,852508);T_EQ(f.hippo->current_order_id,852507);
    T_NOT_NULL(CoupleInstantGroup(f.hippo));
    CoupleInstantCall("cpReissue");
    T_ASSERT(f.archer->user_data > 0);
    CoupleInstantEnd(&f);
}


/* Original 4ef810 -> 438fd0 -> 439160 checks incoming 852507 before
 * invoking the waiting owner's v388/4cc890, so its peer inverse is skipped.
 * This is an owner-preservation check, not an exact public event chronology. */
TEST(wc3_couple_instant_orders, explicit_wait_reissue_preserves_peer) {
    coupleInstantFixture_t f;CoupleInstantBegin(&f,704,false);
    T_ASSERT(CoupleInstantCompile(
        "globals\ninteger cwPeerStops=0\nendglobals\n"
        "function cwObserve takes nothing returns nothing\n"
        "if GetTriggerUnit()==ciHippo and GetIssuedOrderId()==851972 then\n"
        "set cwPeerStops=cwPeerStops+1\nendif\nendfunction\n"
        "function cwBegin takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "call TriggerRegisterUnitEvent(t,ciHippo,EVENT_UNIT_ISSUED_ORDER)\n"
        "call TriggerAddAction(t,function cwObserve)\n"
        "call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial pair accepted\")\nendfunction\n"
        "function cwReissue takes nothing returns nothing\n"
        "set cwPeerStops=0\n"
        "call BJassAssert(IssueTargetOrder(ciArcher,\"coupletarget\",ciHippo),\"explicit wait replacement accepted\")\n"
        "call SetUnitUserData(ciHippo,cwPeerStops)\nendfunction\n"));
    CoupleInstantCall("cwBegin");
    FOR_LOOP(i,60)CoupleInstantStep();
    T_NOT_NULL(f.archer->couple_instant);T_NOT_NULL(f.hippo->couple_instant);
    T_ASSERT(f.archer->couple_instant->active && f.archer->couple_instant->waiting);
    T_ASSERT(f.hippo->couple_instant->active && !f.hippo->couple_instant->waiting);
    T_EQ(f.archer->current_order_id,852508);T_EQ(f.hippo->current_order_id,852507);
    T_NOT_NULL(CoupleInstantGroup(f.hippo));
    CoupleInstantCall("cwReissue");
    T_EQ(f.hippo->user_data,0);
    T_EQ(f.hippo->current_order_id,852507);
    T_ASSERT(f.hippo->couple_instant->active && !f.hippo->couple_instant->waiting);
    T_NOT_NULL(CoupleInstantGroup(f.hippo));
    CoupleInstantEnd(&f);
}

#endif
