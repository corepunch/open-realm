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
typedef struct {float archer_x;bool range333,empty,retail_profile;cstring_t peer_type;} coupleInstantSetup_t;
static void CoupleInstantBeginProfile(coupleInstantFixture_t *f,coupleInstantSetup_t const *setup) {
    float archer_x=setup->archer_x;bool range333=setup->range333;cstring_t peer_type=setup->peer_type;
    reset_entities();setup_test_world();memset(f,0,sizeof(*f));f->old_info=level.mapinfo;
    bool tft=atoi(gi.CvarString("fs_expansion","0"))!=0;
    char ability[1800];snprintf(ability,sizeof(ability),
        "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
        "C;X4;K\"UnitID1\"\nC;X5;K\"Rng1\"\nC;X6;K\"levels\"\nC;X7;K\"targs\"\nC;X8;K\"%s\"\nC;X9;K\"Area1\"\n"
        "C;X1;Y2;K\"Aco2\"\nC;X2;K\"Acoi\"\nC;X3;K\"ehip\"\nC;X4;K\"ehpr\"\nC;X5;K%g\nC;X6;K1\nC;X7;K\"vuln,invu\"\nC;X8;K0\nC;X9;K900\n"
        "C;X1;Y3;K\"Aco3\"\nC;X2;K\"Acoi\"\nC;X3;K\"%s\"\nC;X4;K\"ehpr\"\nC;X5;K%g\nC;X6;K1\nC;X7;K\"vuln,invu\"\nC;X8;K1\nC;X9;K900\nE\n",
        tft ? "DataA1" : "Data11",tft ? "DataB1" : "Data12",range333 ? 333.0 : 99999.0,peer_type,range333 ? 333.0 : 0.0);
    cstring_t text[]={
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitBalanceID\"\nC;X2;K\"realHP\"\nC;X3;K\"spd\"\nC;X4;K\"collision\"\n"
        "C;X1;Y2;K\"earc\"\nC;X2;K245\nC;X3;K270\nC;X4;K32\n"
        "C;X1;Y3;K\"ehip\"\nC;X2;K525\nC;X3;K400\nC;X4;K8\n"
        "C;X1;Y4;K\"ehpr\"\nC;X2;K765\nC;X3;K400\nC;X4;K8\nE\n",
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitID\"\nC;X2;K\"movetp\"\nC;X3;K\"turnRate\"\nC;X4;K\"race\"\nC;X5;K\"collision\"\nC;X6;K\"propWin\"\nC;X7;K\"death\"\n"
        "C;X1;Y2;K\"earc\"\nC;X2;K\"foot\"\nC;X3;K0.6\nC;X4;K\"nightelf\"\nC;X5;K32\nC;X6;K60\nC;X7;K0.4\n"
        "C;X1;Y3;K\"ehip\"\nC;X2;K\"fly\"\nC;X3;K0.4\nC;X4;K\"nightelf\"\nC;X5;K8\nC;X6;K61\nC;X7;K1.2\n"
        "C;X1;Y4;K\"ehpr\"\nC;X2;K\"fly\"\nC;X3;K0.4\nC;X4;K\"nightelf\"\nC;X5;K8\nC;X6;K61\nE\n",
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitAbilID\"\nC;X2;K\"abilList\"\n"
        "C;X1;Y2;K\"earc\"\nC;X2;K\"Aco2\"\nC;X1;Y3;K\"ehip\"\nC;X2;K\"Aco3\"\nC;X1;Y4;K\"ehpr\"\nC;X2;K\"_\"\nE\n",
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitUIID\"\nC;X2;K\"file\"\n"
        "C;X1;Y2;K\"earc\"\nC;X2;K\"TestUI\\Models\\anim_pulse.mdx\"\n"
        "C;X1;Y3;K\"ehip\"\nC;X2;K\"TestUI\\Models\\anim_pulse.mdx\"\n"
        "C;X1;Y4;K\"ehpr\"\nC;X2;K\"TestUI\\Models\\anim_pulse.mdx\"\nE\n",ability};
    /* The stock poll fixture uses the captured retail profile; other tests
     * retain their deliberately unequal authored retirement deadlines. */
    char retail_balance[1024],retail_data[1800];
    if(setup->retail_profile) {
        snprintf(retail_balance,sizeof(retail_balance),"%s",text[0]);
        char *hp=strstr(retail_balance,"K245");if(hp)memcpy(hp,"K310",4);
        hp=strstr(retail_balance,"K765");if(hp)memcpy(hp,"K835",4);
        char *rider=strstr(retail_balance,"K\"ehpr\"");
        char *speed=rider ? strstr(rider,"K400") : NULL;if(speed)memcpy(speed,"K350",4);
        snprintf(retail_data,sizeof(retail_data),"%s",text[1]);
        char *death=strstr(retail_data,"X7;K0.4");if(death)memcpy(death,"X7;K3.0",7);
        death=strstr(retail_data,"X7;K1.2");if(death)memcpy(death,"X7;K3.0",7);
        text[0]=retail_balance;text[1]=retail_data;
    }
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
    if(setup->empty)snprintf(script,sizeof(script),
        "globals\nunit ciArcher\nunit ciHippo\nendglobals\n"
        "function main takes nothing returns nothing\ncall SetPlayerTechResearched(Player(0),'Reht',1)\nendfunction\n");
    T_ASSERT(run_test_jass(script));
    if(setup->empty){G_FinishMovePathingInitialization();level.started=level.scriptsConfigured=level.scriptsStarted=true;return;}
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
static void CoupleInstantBeginAuthored(coupleInstantFixture_t *f,float x,bool range333,cstring_t peer_type) {
    CoupleInstantBeginProfile(f,&(coupleInstantSetup_t){.archer_x=x,.range333=range333,.peer_type=peer_type});
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


/* Public680320 does not intercept an approaching owner. Its cancellation
 * constructs action0, not the ability-tagged tskO used by internal phases. */
TEST(wc3_couple_instant_orders, explicit_reissue_cancels_approach_peer_owner) {
    coupleInstantFixture_t f;CoupleInstantBegin(&f,704,false);
    T_ASSERT(CoupleInstantCompile(
        "globals\ninteger crStops=0\ninteger crEvents=0\nendglobals\n"
        "function crObserve takes nothing returns nothing\n"
        "if GetIssuedOrderId()==851972 and GetTriggerUnit()==ciArcher then\nset crStops=crStops+1\nset crEvents=crEvents*10+1\nendif\n"
        "if GetIssuedOrderId()==852507 and GetTriggerUnit()==ciHippo and GetOrderTargetUnit()==ciArcher then\nset crEvents=crEvents*10+2\nendif\nendfunction\n"
        "function crBegin takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "call TriggerRegisterUnitEvent(t,ciArcher,EVENT_UNIT_ISSUED_ORDER)\n"
        "call TriggerRegisterUnitEvent(t,ciHippo,EVENT_UNIT_ISSUED_ORDER)\n"
        "call TriggerRegisterUnitEvent(t,ciHippo,EVENT_UNIT_ISSUED_TARGET_ORDER)\n"
        "call TriggerAddAction(t,function crObserve)\n"
        "call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial pair accepted\")\nendfunction\n"
        "function crReissue takes nothing returns nothing\n"
        "set crStops=0\nset crEvents=0\n"
        "call BJassAssert(IssueTargetOrder(ciHippo,\"coupletarget\",ciArcher),\"explicit approach replacement accepted\")\n"
        "call SetUnitUserData(ciArcher,crStops)\ncall SetUnitUserData(ciHippo,crEvents)\nendfunction\n"));
    CoupleInstantCall("crBegin");
    FOR_LOOP(i,60)CoupleInstantStep();
    T_NOT_NULL(f.archer->couple_instant);T_NOT_NULL(f.hippo->couple_instant);
    T_ASSERT(f.archer->couple_instant->active && f.archer->couple_instant->waiting);
    T_ASSERT(f.hippo->couple_instant->active && !f.hippo->couple_instant->waiting);
    T_EQ(f.archer->current_order_id,852508);T_EQ(f.hippo->current_order_id,852507);
    T_NOT_NULL(CoupleInstantGroup(f.hippo));
    CoupleInstantCall("crReissue");
    T_ASSERT(f.archer->user_data > 0);
    T_EQ(f.hippo->user_data,12);
    T_EQ(f.archer->current_order_id,0);
    T_ASSERT(!f.archer->couple_instant->active);
    CoupleInstantEnd(&f);
}

/* Immediate852508 is not intercepted and retains ordinary peer inverse. */
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


/* Live211 repeated/control captures: waiting Archer rejects both public
 * reissues before events; existing heads and owner remain unchanged. */
TEST(wc3_couple_instant_orders, waiting_reissues_reject_and_preserve_owner) {
    for(int immediate=0;immediate<2;++immediate) {
    coupleInstantFixture_t f;CoupleInstantBegin(&f,704,false);
    T_ASSERT(CoupleInstantCompile(
        "globals\ninteger cwPeerStops=0\ninteger cwTargets=0\nendglobals\n"
        "function cwObserve takes nothing returns nothing\n"
        "if GetTriggerUnit()==ciHippo and GetIssuedOrderId()==851972 then\n"
        "set cwPeerStops=cwPeerStops+1\nendif\nendfunction\n"
        "function cwTarget takes nothing returns nothing\nset cwTargets=cwTargets+1\nendfunction\n"
        "function cwBegin takes nothing returns nothing\nlocal trigger t=CreateTrigger()\nlocal trigger u=CreateTrigger()\n"
        "call TriggerRegisterUnitEvent(u,ciArcher,EVENT_UNIT_ISSUED_TARGET_ORDER)\n"
        "call TriggerRegisterUnitEvent(u,ciArcher,EVENT_UNIT_ISSUED_ORDER)\n"
        "call TriggerRegisterUnitEvent(u,ciHippo,EVENT_UNIT_ISSUED_TARGET_ORDER)\n"
        "call TriggerRegisterUnitEvent(u,ciHippo,EVENT_UNIT_ISSUED_ORDER)\n"
        "call TriggerAddAction(u,function cwTarget)\n"
        "call TriggerRegisterUnitEvent(t,ciHippo,EVENT_UNIT_ISSUED_ORDER)\n"
        "call TriggerAddAction(t,function cwObserve)\n"
        "call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"initial pair accepted\")\nendfunction\n"
        "function cwReissue takes nothing returns nothing\n"
        "set cwPeerStops=0\nset cwTargets=0\n"
        "call BJassAssert(not IssueTargetOrder(ciArcher,\"coupletarget\",ciHippo),\"explicit wait replacement rejected\")\n"
        "call SetUnitUserData(ciHippo,cwPeerStops)\ncall SetUnitUserData(ciArcher,cwTargets)\nendfunction\n"
        "function cwImmediate takes nothing returns nothing\nset cwPeerStops=0\nset cwTargets=0\n"
        "call BJassAssert(not IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"immediate wait replacement rejected\")\n"
        "call SetUnitUserData(ciHippo,cwPeerStops)\ncall SetUnitUserData(ciArcher,cwTargets)\nendfunction\n"));
    CoupleInstantCall("cwBegin");
    FOR_LOOP(i,60)CoupleInstantStep();
    T_NOT_NULL(f.archer->couple_instant);T_NOT_NULL(f.hippo->couple_instant);
    T_ASSERT(f.archer->couple_instant->active && f.archer->couple_instant->waiting);
    T_ASSERT(f.hippo->couple_instant->active && !f.hippo->couple_instant->waiting);
    T_EQ(f.archer->current_order_id,852508);T_EQ(f.hippo->current_order_id,852507);
    T_NOT_NULL(CoupleInstantGroup(f.hippo));
    uint32_t const serial=f.archer->couple_instant->serial;
    uint32_t const revision=G_UnitMoveRevision(f.archer);
    CoupleInstantCall(immediate ? "cwImmediate" : "cwReissue");
    T_EQ(f.archer->user_data,0);
    T_EQ(f.archer->current_order_id,852508);
    T_EQ(f.archer->couple_instant->serial,serial);
    T_EQ(G_UnitMoveRevision(f.archer),revision);
    T_ASSERT(f.archer->couple_instant->active && f.archer->couple_instant->waiting);
    T_EQ(f.hippo->user_data,0);
    T_EQ(f.hippo->current_order_id,852507);
    T_ASSERT(f.hippo->couple_instant->active && !f.hippo->couple_instant->waiting);
    T_NOT_NULL(CoupleInstantGroup(f.hippo));
    CoupleInstantEnd(&f);
    }
}

/* Observe the real JASS spell callbacks through a test-local assertion wrapper.
 * This keeps phase delivery public while sampling the signed game-owned counter. */
extern jassModule_t jass_funcs[];
static jassCFunction_t ci_phase_assert_original;
static unsigned ci_phase_count;
static int ci_phase_events[32],ci_phase_busy[32];
static uint32_t CoupleInstantPhaseAssert(jass_t *j) {
    cstring_t message=jass_checkstring(j,2);
    if(message && !strncmp(message,"ciPhase",7)) {
        edict_t *unit=jass_getcontext(j)->unit;
        int const phase=atoi(message+7);
        T_NOT_NULL(unit);
        if(unit && ci_phase_count<(sizeof(ci_phase_events)/sizeof(ci_phase_events[0]))) {
            uint32_t const code=unit->class_id==MAKEFOURCC('e','a','r','c') ? MAKEFOURCC('A','c','o','2') : MAKEFOURCC('A','c','o','3');
            int busy=0;
            for(unitAbilityDisable_t *p=unit->ability_disables;p;p=p->next)
                if(p->code==code && !p->class_code)busy=p->busy_depth;
            ci_phase_events[ci_phase_count]=(code==MAKEFOURCC('A','c','o','2') ? 10 : 20)+phase;
            ci_phase_busy[ci_phase_count++]=busy;
            T_EQ(S_AbilityOrderBusy(unit,code),busy>0);
        } else T_ASSERT(false);
    }
    return ci_phase_assert_original(j);
}
static void CoupleInstantPhaseHook(bool enable) {
    for(jassModule_t *p=jass_funcs;p->name;p++)if(!strcmp(p->name,"BJassAssert")) {
        if(enable){ci_phase_assert_original=p->func;p->func=CoupleInstantPhaseAssert;ci_phase_count=0;}
        else p->func=ci_phase_assert_original;
        return;
    }
    T_ASSERT(false);
}
static void CoupleInstantPhaseInstall(bool interrupt) {
    char script[6500];size_t used=0;
    used+=snprintf(script+used,sizeof(script)-used,"globals\nboolean ciPhaseStop=%s\nendglobals\n",interrupt ? "true" : "false");
    /* Fixture common.j uses the RoC event IDs; JassUnitEventType maps them
     * to canonical engine IDs. It defines no EVENT_UNIT_SPELL_* globals. */
    unsigned const events[]={289,290,291,293,292};
    FOR_LOOP(i,5) {
        used+=snprintf(script+used,sizeof(script)-used,
            "function ciPhase%u takes nothing returns nothing\ncall BJassAssert(true,\"ciPhase%u\")\n",i+1,i+1);
        if(i==0)used+=snprintf(script+used,sizeof(script)-used,
            "if ciPhaseStop and GetTriggerUnit()==ciArcher then\nset ciPhaseStop=false\n"
            "call BJassAssert(IssueImmediateOrder(ciArcher,\"stop\"),\"channel Stop admitted\")\n"
            "call BJassAssert(GetSpellTargetUnit()==null,\"resumed CHANNEL target cleared\")\nendif\n");
        if(i==4)used+=snprintf(script+used,sizeof(script)-used,
            "call BJassAssert(GetSpellTargetUnit()==null,\"interrupted FINISH target cleared\")\n");
        used+=snprintf(script+used,sizeof(script)-used,"endfunction\n");
    }
    used+=snprintf(script+used,sizeof(script)-used,"function ciPhaseSetup takes nothing returns nothing\nlocal trigger t\n");
    FOR_LOOP(i,5)used+=snprintf(script+used,sizeof(script)-used,
        "set t=CreateTrigger()\ncall TriggerRegisterUnitEvent(t,ciArcher,ConvertUnitEvent(%u))\n"
        "call TriggerRegisterUnitEvent(t,ciHippo,ConvertUnitEvent(%u))\ncall TriggerAddAction(t,function ciPhase%u)\n",events[i],events[i],i+1);
    snprintf(script+used,sizeof(script)-used,
        "call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"phase initial admission\")\nendfunction\n"
        "function ciPhaseStopNow takes nothing returns nothing\ncall IssueImmediateOrder(ciArcher,\"stop\")\nendfunction\n");
    T_ASSERT(CoupleInstantCompile(script));CoupleInstantCall("ciPhaseSetup");
}
TEST(wc3_couple_instant_orders, native_channel_phase_order_and_busy_depth) {
    CoupleInstantPhaseHook(true);
    coupleInstantFixture_t f;CoupleInstantBegin(&f,704,false);CoupleInstantPhaseInstall(false);
    FOR_LOOP(i,900)CoupleInstantStep();
    int const events[]={11,12,13,21,22,23,14,24},busy[]={0,1,1,0,1,1,0,0};
    T_EQ(ci_phase_count,(sizeof(events)/sizeof(events[0])));
    FOR_LOOP(i,(sizeof(events)/sizeof(events[0]))){T_EQ(ci_phase_events[i],events[i]);T_EQ(ci_phase_busy[i],busy[i]);}
    T_EQ(CoupleInstantRiders(),1);
    CoupleInstantEnd(&f);CoupleInstantPhaseHook(false);
}
TEST(wc3_couple_instant_orders, waiting_channel_owner_survives_save_and_stop) {
    CoupleInstantPhaseHook(true);
    coupleInstantFixture_t f;CoupleInstantBegin(&f,704,false);CoupleInstantPhaseInstall(false);
    FOR_LOOP(i,60)CoupleInstantStep();
    T_EQ(ci_phase_count,3);T_ASSERT(f.archer->couple_instant->waiting);
    T_ASSERT(f.archer->couple_instant->channel_open);T_NOT_NULL(f.archer->couple_instant->channel_owner);
    T_ASSERT(S_AbilityOrderBusy(f.archer,MAKEFOURCC('A','c','o','2')));
    uint32_t const a=f.archer-g_edicts,h=f.hippo-g_edicts;
    cstring_t path=Test_TempPath("couple-instant227-channel.bin");
    T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));f.archer=g_edicts+a;f.hippo=g_edicts+h;
    T_ASSERT(f.archer->couple_instant->waiting);T_ASSERT(f.archer->couple_instant->channel_open);
    T_NOT_NULL(f.archer->couple_instant->channel_owner);
    T_ASSERT(S_AbilityOrderBusy(f.archer,MAKEFOURCC('A','c','o','2')));
    if(f.archer->couple_instant->channel_owner)T_EQ(f.archer->couple_instant->channel_owner->busy_depth,1);
    CoupleInstantCall("ciPhaseStopNow");
    T_EQ(ci_phase_count,4);T_EQ(ci_phase_events[3],14);T_EQ(ci_phase_busy[3],0);
    T_ASSERT(!S_AbilityOrderBusy(f.archer,MAKEFOURCC('A','c','o','2')));
    T_ASSERT(!f.archer->couple_instant->channel_open);T_EQ(CoupleInstantRiders(),0);
    CoupleInstantEnd(&f);CoupleInstantPhaseHook(false);
}
TEST(wc3_couple_instant_orders, native_channel_stop_ends_then_finishes_without_effect) {
    CoupleInstantPhaseHook(true);
    coupleInstantFixture_t f;CoupleInstantBegin(&f,704,false);CoupleInstantPhaseInstall(true);
    FOR_LOOP(i,900)CoupleInstantStep();
    T_EQ(ci_phase_count,3);
    int const events[]={11,14,15},busy[]={0,-1,0};
    FOR_LOOP(i,3){T_EQ(ci_phase_events[i],events[i]);T_EQ(ci_phase_busy[i],busy[i]);}
    T_EQ(f.archer->current_order_id,0);T_EQ(f.hippo->current_order_id,0);
    T_EQ(CoupleInstantRiders(),0);T_ASSERT(!S_AbilityOrderBusy(f.archer,MAKEFOURCC('A','c','o','2')));
    CoupleInstantEnd(&f);CoupleInstantPhaseHook(false);
}

/* Integration coverage of208 alias+owner reservation-count eligibility.
 * This is not a native public chronology or402060 priority witness. */
TEST(wc3_couple_instant_orders, standalone_target_reservation_gates_fresh_source) {
    FOR_LOOP(approaching,2)FOR_LOOP(saved,2) {
        coupleInstantFixture_t f;CoupleInstantBegin(&f,704,approaching!=0);
        T_ASSERT(CoupleInstantCompile(
            "globals\nunit ceA2\nunit ceH2\ninteger ceEvents=0\nunit ceChosen=null\nendglobals\n"
            "function ceObserve takes nothing returns nothing\n"
            "set ceEvents=ceEvents+1\n"
            "if GetIssuedOrderId()==852508 then\nset ceChosen=GetOrderTargetUnit()\nendif\nendfunction\n"
            "function ceReserve takes nothing returns nothing\n"
            "call SetUnitFacing(ciArcher,0.0)\n"
            "call BJassAssert(IssueTargetOrder(ciArcher,\"coupletarget\",ciHippo),\"standalone A1 reservation accepted\")\n"
            "call BJassAssert(GetUnitCurrentOrder(ciHippo)==0,\"H1 receives no reciprocal order\")\nendfunction\n"
            "function ceRejectAndChoose takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "set ceA2=CreateUnit(Player(0),'earc',704.0,1216.0,0.0)\n"
            "call SetUnitAcquireRange(ceA2,0.0)\n"
            "call BJassAssert(GetUnitX(ceA2)==704.0 and GetUnitY(ceA2)==1216.0,\"A2 area fixture pose\")\n"
            "call BJassAssert(GetUnitX(ciHippo)==1536.0 and GetUnitY(ciHippo)==1024.0,\"H1 area fixture pose\")\n"
            "call TriggerRegisterUnitEvent(t,ceA2,EVENT_UNIT_ISSUED_ORDER)\n"
            "call TriggerRegisterUnitEvent(t,ceA2,EVENT_UNIT_ISSUED_POINT_ORDER)\n"
            "call TriggerRegisterUnitEvent(t,ceA2,EVENT_UNIT_ISSUED_TARGET_ORDER)\n"
            "call TriggerAddAction(t,function ceObserve)\n"
            "call BJassAssert(IssuePointOrder(ceA2,\"move\",512.0,1216.0),\"A2 prior Move accepted\")\n"
            "set ceEvents=0\n"
            "call BJassAssert(not IssueTargetOrder(ceA2,\"coupletarget\",ciHippo),\"same-alias reservation rejects fresh explicit source\")\n"
            "call BJassAssert(ceEvents==0 and GetUnitCurrentOrder(ceA2)==851986,\"explicit rejection preserves Move without issued event\")\n"
            "call BJassAssert(not IssueImmediateOrder(ceA2,\"coupleinstant\"),\"only reserved candidate rejects automatic source\")\n"
            "call BJassAssert(ceEvents==0 and GetUnitCurrentOrder(ceA2)==851986,\"automatic rejection preserves Move without issued event\")\n"
            "set ceH2=CreateUnit(Player(0),'ehip',1536.0,1536.0,0.0)\n"
            "call SetUnitAcquireRange(ceH2,0.0)\n"
            "call BJassAssert(GetUnitX(ceH2)==1536.0 and GetUnitY(ceH2)==1536.0,\"H2 area fixture pose\")\n"
            "set ceChosen=null\n"
            "call BJassAssert(IssueImmediateOrder(ceA2,\"coupleinstant\"),\"free farther H2 permits automatic source\")\n"
            "call BJassAssert(ceChosen==ceH2,\"eligible H2 selected instead of reserved H1\")\nendfunction\n"
            "function ceRelease takes nothing returns nothing\n"
            "call BJassAssert(IssueImmediateOrder(ceA2,\"stop\"),\"release A2 chosen pair\")\n"
            "call BJassAssert(IssueImmediateOrder(ciArcher,\"stop\"),\"release A1 reservation\")\n"
            "call BJassAssert(IssueTargetOrder(ceA2,\"coupletarget\",ciHippo),\"fresh source accepts H1 after reservation release\")\nendfunction\n"));
        CoupleInstantCall("ceReserve");
        FOR_LOOP(i,40)CoupleInstantStep();
        T_NOT_NULL(f.archer->couple_instant);
        if(f.archer->couple_instant) {
            T_ASSERT(f.archer->couple_instant->active);
            T_EQ(f.archer->couple_instant->waiting,!approaching);
        }
        T_EQ(f.hippo->current_order_id,0);
        T_ASSERT(!f.hippo->couple_instant || !f.hippo->couple_instant->active);
        if(saved) {
            uint32_t const a=f.archer-g_edicts,h=f.hippo-g_edicts;
            uint32_t const a_birth=f.archer->spawn_time,h_birth=f.hippo->spawn_time;
            cstring_t path=Test_TempPath("couple-instant228-held-reservation.bin");
            T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));
            f.archer=g_edicts+a;f.hippo=g_edicts+h;
            T_EQ(f.archer->spawn_time,a_birth);T_EQ(f.hippo->spawn_time,h_birth);
            T_ASSERT(f.archer->couple_instant && f.archer->couple_instant->active);
            if(f.archer->couple_instant)T_EQ(f.archer->couple_instant->waiting,!approaching);
        }
        CoupleInstantCall("ceRejectAndChoose");
        CoupleInstantCall("ceRelease");
        CoupleInstantEnd(&f);
    }
}

/* Slot reuse is an engine lifetime regression, not a claimed native public
 * RemoveUnit timeline. Drive admission through JASS after actual allocator reuse. */
TEST(wc3_couple_instant_orders, reservation_target_slot_reuse_does_not_retain_old_claim) {
    coupleInstantFixture_t f;CoupleInstantBegin(&f,704,false);
    T_ASSERT(CoupleInstantCompile(
        "globals\nunit crFresh\nendglobals\n"
        "function crReserve takes nothing returns nothing\n"
        "set crFresh=CreateUnit(Player(0),'earc',704.0,1216.0,0.0)\n"
        "call SetUnitAcquireRange(crFresh,0.0)\n"
        "call SetUnitFacing(ciArcher,0.0)\n"
        "call BJassAssert(IssueTargetOrder(ciArcher,\"coupletarget\",ciHippo),\"old incarnation reserved\")\nendfunction\n"
        "function crRecreate takes nothing returns nothing\n"
        "set ciHippo=CreateUnit(Player(0),'ehip',1536.0,1024.0,0.0)\n"
        "call SetUnitAcquireRange(ciHippo,0.0)\n"
        "call BJassAssert(IssueTargetOrder(crFresh,\"coupletarget\",ciHippo),\"new incarnation has no stale reservation\")\n"
        "call IssueImmediateOrder(ciArcher,\"stop\")\n"
        "call BJassAssert(not IssueTargetOrder(ciArcher,\"coupletarget\",ciHippo),\"old source cleanup cannot release fresh claim\")\n"
        "call IssueImmediateOrder(crFresh,\"stop\")\n"
        "call BJassAssert(IssueTargetOrder(ciArcher,\"coupletarget\",ciHippo),\"fresh claim releases normally\")\nendfunction\n"));
    CoupleInstantCall("crReserve");FOR_LOOP(i,40)CoupleInstantStep();
    T_ASSERT(f.archer->couple_instant && f.archer->couple_instant->waiting);
    edict_t *old=f.hippo;uint32_t const incarnation=old->spawn_time;
    G_FreeEdict(old);level.time+=1001;
    CoupleInstantCall("crRecreate");
    T_ASSERT(old->inuse);T_EQ(old->class_id,MAKEFOURCC('e','h','i','p'));
    T_NE(old->spawn_time,incarnation);
    T_NOT_NULL(f.archer->couple_instant);
    if(f.archer->couple_instant) {
        T_EQ(f.archer->couple_instant->partner,old);
        T_EQ(f.archer->couple_instant->partner_spawn_time,old->spawn_time);
    }
    CoupleInstantEnd(&f);
}

/* Actual public214 range333 scene streams: compare the complete ordered native
 * arrival/post-commit projection, then every engine row in the cold-save suffix. */
#include "fixtures/retail_couple_instant214_polls.h"
typedef struct {uint32_t words[20];unsigned scene;} couplePollRow_t;
static struct {edict_t *roles[4];couplePollRow_t rows[1024];unsigned count,scene;bool print;} couple_poll_trace;
static int CouplePollRole(edict_t const *unit) {
    FOR_LOOP(i,4)if(couple_poll_trace.roles[i]==unit)return i;
    return -1;
}
static void CouplePollRoute(edict_t const *unit,moveGroup_t const *group,unsigned phase) {
    if(!couple_poll_trace.print || couple_poll_trace.scene!=1 || CouplePollRole(unit)!=3)return;
    moveFineRoute_t const *route=&unit->movement.fine_route;
    uint32_t words[]={phase,wc3_float_bits(level.pathing_clock.time),group->flags,
        wc3_float_bits(group->goal.x),wc3_float_bits(group->goal.y),wc3_float_bits(group->point.x),wc3_float_bits(group->point.y),
        group->target_refresh,group->route.group_admission.time,group->route.group_index,
        route->count,route->index,route->adaptive_count,route->adaptive_index,
        wc3_float_bits(route->adaptive_goal.x),wc3_float_bits(route->adaptive_goal.y),route->adaptive_admission.time,
        unit->movement.fine_request_time,unit->movement.wait_delay,unit->movement.retry_count,
        unit->movement.path.valid,wc3_float_bits(unit->movement.heading)};
    fprintf(stderr,"ACOIROUTE");FOR_LOOP(i,sizeof(words)/sizeof(words[0]))fprintf(stderr," %u",words[i]);
    fprintf(stderr," fine");FOR_LOOP(i,route->count)fprintf(stderr," %u %u",wc3_float_bits(route->points[i].x),wc3_float_bits(route->points[i].y));
    fprintf(stderr," adaptive");FOR_LOOP(i,route->adaptive_count)fprintf(stderr," %u %u",wc3_float_bits(route->adaptive_points[i].x),wc3_float_bits(route->adaptive_points[i].y));fprintf(stderr,"\n");
}
static void CouplePollTurn(edict_t *unit,float target,wc3Motion_t const *motion) {
    if(!couple_poll_trace.print || couple_poll_trace.scene!=1 || CouplePollRole(unit)!=3)return;
    fprintf(stderr,"ACOITURN %u %u %u %u %u %u\n",wc3_float_bits(level.pathing_clock.time),wc3_float_bits(target),
        wc3_float_bits(motion->heading),wc3_float_bits(motion->error),wc3_float_bits(motion->turn),wc3_float_bits(motion->window));
}
static void CouplePollRecord(moveGroup_t const *group,moveGroupMember_t const *member,unsigned commit,float speed,wc3Arrival_t const *arrival) {
    int role=CouplePollRole(member->unit);if(role<0)return;
    if(!member->unit->couple_instant || !member->unit->couple_instant->active)return;
    T_ASSERT(couple_poll_trace.count<1024);if(couple_poll_trace.count>=1024)return;
    edict_t *unit=member->unit;wc3GridPose_t pose;unit_predicted_pose(unit,&pose);
    couplePollRow_t row={{commit,role,wc3_float_bits(level.pathing_clock.time),
        wc3_float_bits(pose.grid[0]),wc3_float_bits(pose.grid[1]),
        wc3_float_bits(member->destination.x),wc3_float_bits(member->destination.y),
        wc3_float_bits(commit==2?unit->s.angle:commit?member->heading:move_member_heading(unit)),
        wc3_float_bits(member->arrival_range),member->forced_arrival || (group->flags&0x200),
        member->in_range,member->arrived,wc3_float_bits(speed),
        wc3_float_bits(unit->movement.fine_pose.x),wc3_float_bits(unit->movement.fine_pose.y),
        wc3_float_bits(wc3_mul(unit->movement.velocity.x,1.0f/32)),
        wc3_float_bits(wc3_mul(unit->movement.velocity.y,1.0f/32)),unit->current_order_id}};
    if(arrival){row.words[3]=wc3_float_bits(arrival->source[0]);row.words[4]=wc3_float_bits(arrival->source[1]);
        row.words[5]=wc3_float_bits(arrival->target[0]);row.words[6]=wc3_float_bits(arrival->target[1]);
        row.words[7]=wc3_float_bits(arrival->heading);row.words[8]=wc3_float_bits(arrival->range);
        row.words[9]=arrival->flags;row.words[18]=wc3_float_bits(arrival->error);row.words[19]=wc3_float_bits(arrival->distance);}
    row.scene=couple_poll_trace.scene;
    CouplePollRoute(unit,group,commit);
    couple_poll_trace.rows[couple_poll_trace.count++]=row;
    if(couple_poll_trace.print){fprintf(stderr,"ACOIPOLL scene=%u",couple_poll_trace.scene);FOR_LOOP(i,20)fprintf(stderr," %u",row.words[i]);fprintf(stderr,"\n");}
}
static void CouplePollArrival(moveGroup_t const *group,moveGroupMember_t const *member,wc3Arrival_t const *arrival) {
    CouplePollRecord(group,member,false,0,arrival);
}
static void CouplePollCommit(moveGroup_t const *group,moveGroupMember_t const *member,moveGroupCommitTrace_t const *trace) {
    CouplePollRecord(group,member,true,trace->speed,NULL);
}
static void CouplePollCommitted(moveGroup_t const *group,moveGroupMember_t const *member) {
    CouplePollRecord(group,member,2,0,NULL);
}
static void CouplePollCheckNative214(unsigned scene) {
    unsigned expected_index=0,compared=0,expected_count=0;
    unsigned const arrival_indices[]={2,3,4,5,6,7,8,18,10,11};
    unsigned const committed_indices[]={2,13,14,15,16,7};
    unsigned const native_count=sizeof(couple_native_polls214)/sizeof(couple_native_polls214[0]);
    T_EQ(native_count,400);
    FOR_LOOP(i,native_count)if(couple_native_polls214[i].scene==scene)expected_count++;
    FOR_LOOP(i,couple_poll_trace.count) {
        couplePollRow_t const *row=&couple_poll_trace.rows[i];
        if(row->words[0]==1)continue; /* Native schema samples phases0 and2 only. */
        while(expected_index<native_count && couple_native_polls214[expected_index].scene!=scene)expected_index++;
        T_ASSERT(expected_index<native_count);
        if(expected_index>=native_count)break;
        coupleNativePoll214_t const *expected=&couple_native_polls214[expected_index++];
        T_EQ(row->words[0],expected->phase);T_EQ(row->words[1],expected->role);
        unsigned const count=row->words[0]==0 ? 10 : 6;
        unsigned const *indices=row->words[0]==0 ? arrival_indices : committed_indices;
        T_EQ(expected->count,count);
        FOR_LOOP(j,count)T_EQ(row->words[indices[j]],expected->words[j]);
        compared++;
    }
    T_EQ(compared,expected_count);
}
TEST(wc3_couple_instant_orders, complete_public_reservation_poll_stream_and_saved_suffix) {
    FOR_LOOP(scene,2){
        coupleInstantFixture_t f;CoupleInstantBegin(&f,704,true);
        T_ASSERT(CoupleInstantCompile(
            "globals\nunit cpA2\nunit cpH2\ninteger cpScene=0\ninteger cpTick=0\ntimer cpPollTimer=null\nendglobals\n"
            "function cpSpawn takes nothing returns nothing\n"
            "set cpA2=CreateUnit(Player(0),'earc',704.0,1216.0,0.0)\ncall SetUnitAcquireRange(cpA2,0.0)\n"
            "if cpScene==1 then\nset cpH2=CreateUnit(Player(0),'ehip',1536.0,1536.0,180.0)\ncall SetUnitAcquireRange(cpH2,0.0)\nendif\nendfunction\n"
            "function cpIssue takes nothing returns nothing\ncall BJassAssert(IssueTargetOrder(ciArcher,\"coupletarget\",ciHippo),\"initial reservation\")\nendfunction\n"
            "function cpCompete takes nothing returns nothing\n"
            "call BJassAssert(IssuePointOrder(cpA2,\"move\",512.0,1216.0),\"prior Move\")\n"
            "call BJassAssert(not IssueTargetOrder(cpA2,\"coupletarget\",ciHippo),\"occupied target\")\n"
            "call BJassAssert(IssueImmediateOrder(cpA2,\"coupleinstant\")== (cpScene==1),\"automatic selection\")\n"
            "if cpScene==0 then\ncall BJassAssert(IssueImmediateOrder(ciArcher,\"stop\"),\"release\")\n"
            "call BJassAssert(IssueTargetOrder(cpA2,\"coupletarget\",ciHippo),\"retry\")\nendif\nendfunction\n"
            "function cpClock takes nothing returns nothing\n"
            "if cpTick==cpScene*400 then\ncall cpSpawn()\n"
            "elseif cpTick==cpScene*400+100 then\ncall cpIssue()\n"
            "elseif cpTick==cpScene*400+115 then\ncall cpCompete()\n"
            "elseif cpTick==cpScene*400+398 then\ncall PauseTimer(cpPollTimer)\nendif\n"
            "set cpTick=cpTick+1\nendfunction\n"
            "function cpStartClock takes nothing returns nothing\nset cpPollTimer=CreateTimer()\n"
            "call TimerStart(cpPollTimer,0.02,true,function cpClock)\nendfunction\n"));
        if(scene)T_ASSERT(CoupleInstantCompile("function cpSetScene takes nothing returns nothing\nset cpScene=1\nendfunction\n"));
        if(scene)CoupleInstantCall("cpSetScene");CoupleInstantCall("cpStartClock");
        /* Execute the same periodic JASS callback before any equal-deadline
         * owner tick, rather than issuing after RunFrame already drained it. */
        FOR_LOOP(i,4+scene*1600)CoupleInstantStep();
        memset(&couple_poll_trace,0,sizeof(couple_poll_trace));couple_poll_trace.scene=scene;couple_poll_trace.print=false;
        couple_poll_trace.roles[0]=f.archer;couple_poll_trace.roles[1]=f.hippo;
        FILTER_EDICTS(e,e->inuse){if(e!=f.archer && e->class_id==MAKEFOURCC('e','a','r','c'))couple_poll_trace.roles[2]=e;if(e!=f.hippo && e->class_id==MAKEFOURCC('e','h','i','p'))couple_poll_trace.roles[3]=e;}
        move_test_group_arrival=CouplePollArrival;move_test_group_commit=CouplePollCommit;move_test_group_committed=CouplePollCommitted;move_test_turn_request=CouplePollTurn;
        FOR_LOOP(i,400+60)CoupleInstantStep();
        FOR_LOOP(i,20)CoupleInstantStep();
        cstring_t path=Test_TempPath("couple-instant221-polls.bin");T_ASSERT(WriteGame(path));
        unsigned const prefix=couple_poll_trace.count;
        FOR_LOOP(i,1120)CoupleInstantStep();
        unsigned const total=couple_poll_trace.count;T_ASSERT(total>prefix);
        CouplePollCheckNative214(scene);
        couplePollRow_t *expected=malloc(sizeof(couple_poll_trace.rows));memcpy(expected,couple_poll_trace.rows,sizeof(couple_poll_trace.rows));
        T_ASSERT(ReadGame(path));couple_poll_trace.count=prefix;couple_poll_trace.print=false;
        FOR_LOOP(i,1120)CoupleInstantStep();T_EQ(couple_poll_trace.count,total);
        FOR_LOOP(i,total-prefix)FOR_LOOP(j,20)T_EQ(couple_poll_trace.rows[prefix+i].words[j],expected[prefix+i].words[j]);
        free(expected);remove(path);move_test_group_arrival=NULL;move_test_group_commit=NULL;move_test_group_committed=NULL;move_test_turn_request=NULL;CoupleInstantEnd(&f);
    }
}


static jassCFunction_t couple_publication_assert_original;
static cstring_t couple_publication_save;
static unsigned couple_publication_observed;
static uint32_t CouplePublicationAssert(jass_t *j) {
    cstring_t message=jass_checkstring(j,2);
    if(message && !strcmp(message,"publication pending")) {
        edict_t *unit=jass_getcontext(j)->unit;
        moveGroup_t *pending=unit ? CoupleInstantGroup(unit) : NULL;
        T_NOT_NULL(pending);T_ASSERT(S_TargetApproachPublicationPending());
        if(pending) {
            T_ASSERT(pending->publication_pending);
            /* A derived-list rebuild must not make the provisional owner runnable. */
            move_group_order_valid=false;move_prepare_group_order();
            for(moveGroup_t *group=move_group_head;group;group=group->older)T_ASSERT(group!=pending);
        }
        T_ASSERT(!WriteGame(couple_publication_save));T_ASSERT(!ReadGame(couple_publication_save));
        couple_publication_observed++;
    }
    return couple_publication_assert_original(j);
}
TEST(wc3_couple_instant_orders, provisional_target_publication_preserves_callback_successors) {
    FOR_LOOP(mode,3) {
        /* jassFunc_t captures nativefunc while the fixture VM is compiled. */
        for(jassModule_t *p=jass_funcs;p->name;p++)if(!strcmp(p->name,"BJassAssert")) {
            couple_publication_assert_original=p->func;p->func=CouplePublicationAssert;break;
        }
        coupleInstantFixture_t f;CoupleInstantBegin(&f,704,true);
        char script[2400];snprintf(script,sizeof(script),
            "globals\nboolean cpPublicationSeen=false\nendglobals\n"
            "function cpPublicationCallback takes nothing returns nothing\n"
            "if GetIssuedOrderId()==852508 and not cpPublicationSeen then\nset cpPublicationSeen=true\n"
            "call BJassAssert(true,\"publication pending\")\n%s\nendif\nendfunction\n"
            "function cpPublicationInstall takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "call TriggerRegisterUnitEvent(t,ciArcher,EVENT_UNIT_ISSUED_TARGET_ORDER)\n"
            "call TriggerAddAction(t,function cpPublicationCallback)\nendfunction\n"
            "function cpPublicationIssue takes nothing returns nothing\n"
            "call BJassAssert(IssueImmediateOrder(ciArcher,\"coupleinstant\"),\"automatic admission\")\nendfunction\n",
            mode==1 ? "call BJassAssert(IssuePointOrder(ciArcher,\"move\",512.0,768.0),\"callback Move\")" :
            mode==2 ? "call RemoveUnit(ciArcher)" : "");
        T_ASSERT(CoupleInstantCompile(script));CoupleInstantCall("cpPublicationInstall");
        couple_publication_save=Test_TempPath("couple-instant229-publication.bin");
        T_ASSERT(WriteGame(couple_publication_save));couple_publication_observed=0;
        CoupleInstantCall("cpPublicationIssue");
        for(jassModule_t *p=jass_funcs;p->name;p++)if(!strcmp(p->name,"BJassAssert")) {p->func=couple_publication_assert_original;break;}
        T_EQ(couple_publication_observed,1);T_ASSERT(!S_TargetApproachPublicationPending());
        FOR_LOOP(i,ARRAY_COUNT(level.move_groups))if(level.move_groups[i]->inuse)T_ASSERT(!level.move_groups[i]->publication_pending);
        if(mode==1){T_EQ(f.archer->current_order_id,851986);FOR_LOOP(i,60)CoupleInstantStep();T_ASSERT(f.archer->s.origin2.x<704);}
        if(mode==2)T_ASSERT(!f.archer->inuse || G_IsDeferredFree(f.archer));
        T_ASSERT(WriteGame(couple_publication_save));T_ASSERT(ReadGame(couple_publication_save));
        T_ASSERT(!S_TargetApproachPublicationPending());remove(couple_publication_save);CoupleInstantEnd(&f);
    }
}


#include "fixtures/retail_couple_instant193_polls.h"
static jassCFunction_t couple_boundary_assert_original;
static unsigned couple_boundary_spawns;
static uint32_t CoupleBoundaryAssert(jass_t *j) {
    cstring_t message=jass_checkstring(j,2);
    if(message && !strncmp(message,"boundary scene ",15)) {
        couple_poll_trace.scene=atoi(message+15);
        memset(couple_poll_trace.roles,0,sizeof(couple_poll_trace.roles));
        FILTER_EDICTS(e,e->inuse && !G_IsDeferredFree(e) && !M_IsDead(e)) {
            if(e->class_id==MAKEFOURCC('e','a','r','c'))couple_poll_trace.roles[0]=e;
            if(e->class_id==MAKEFOURCC('e','h','i','p'))couple_poll_trace.roles[1]=e;
        }
        T_NOT_NULL(couple_poll_trace.roles[0]);T_NOT_NULL(couple_poll_trace.roles[1]);
        couple_boundary_spawns++;
    }
    return couple_boundary_assert_original(j);
}
static void CouplePollCheckNative193(void) {
    unsigned at=0;
    unsigned const arrival[]={2,3,4,5,6,7,8,18,10,11},committed[]={2,13,14,15,16,7};
    unsigned const count=sizeof(couple_native_polls193)/sizeof(couple_native_polls193[0]);
    T_EQ(count,532);
    FOR_LOOP(i,couple_poll_trace.count) {
        couplePollRow_t const *row=&couple_poll_trace.rows[i];if(row->words[0]==1)continue;
        T_ASSERT(at<count);if(at>=count)break;
        coupleNativePoll193_t const *expected=&couple_native_polls193[at++];
        T_EQ(row->scene,expected->scene);T_EQ(row->words[0],expected->phase);T_EQ(row->words[1],expected->role);
        unsigned const *indices=row->words[0]==0 ? arrival : committed;
        unsigned words=row->words[0]==0 ? 10 : 6;T_EQ(words,expected->count);
        FOR_LOOP(j,words)T_EQ(row->words[indices[j]],expected->words[j]);
    }
    T_EQ(at,count);
}
TEST(wc3_couple_instant_orders, continuous_native_range_boundary_polls_and_saved_suffix) {
    for(jassModule_t *p=jass_funcs;p->name;p++)if(!strcmp(p->name,"BJassAssert")) {
        couple_boundary_assert_original=p->func;p->func=CoupleBoundaryAssert;break;
    }
    coupleInstantFixture_t f;
    CoupleInstantBeginProfile(&f,&(coupleInstantSetup_t){.range333=true,.empty=true,.peer_type="earc"});
    memset(&couple_poll_trace,0,sizeof(couple_poll_trace));couple_poll_trace.print=true;couple_boundary_spawns=0;
    T_ASSERT(CoupleInstantCompile(
        "globals\ninteger cbTick=0\ninteger cbScene=0\ntimer cbTimer=null\nendglobals\n"
        "function cbClock takes nothing returns nothing\nlocal integer age=cbTick-(cbTick/400)*400\n"
        "local real ax=704.0\nlocal group g=null\nlocal unit u=null\nlocal boolean accepted=false\n"
        "if age==0 then\n"
        "if cbScene==4 or cbScene==5 then\nset ax=1280.0\nelseif cbScene==6 then\nset ax=1163.0\n"
        "elseif cbScene==7 then\nset ax=1162.75\nelseif cbScene==8 then\nset ax=1163.25\nendif\n"
        "set ciArcher=CreateUnit(Player(0),'earc',ax,1024.0,180.0)\n"
        "set ciHippo=CreateUnit(Player(0),'ehip',1536.0,1024.0,0.0)\n"
        "call SetUnitAcquireRange(ciArcher,0.0)\ncall SetUnitAcquireRange(ciHippo,0.0)\n"
        "call BJassAssert(GetUnitAbilityLevel(ciArcher,'Aco2')==1 and GetUnitAbilityLevel(ciHippo,'Aco3')==1,\"native configured aliases\")\n"
        /* Native CpMark creates/enumerates/destroys a rider-query group at
         * spawn, before the save frontier. Preserve that registry lifecycle. */
        "set g=CreateGroup()\ncall GroupEnumUnitsOfPlayer(g,Player(0),null)\ncall DestroyGroup(g)\nset g=null\n"
        "call BJassAssert(true,\"boundary scene \"+I2S(cbScene))\n"
        "elseif age==100 then\n"
        "if cbScene==0 then\nset accepted=IssueImmediateOrder(ciArcher,\"coupleinstant\")\n"
        "elseif cbScene==1 then\nset accepted=IssueImmediateOrder(ciHippo,\"coupleinstant\")\n"
        "elseif cbScene==2 or cbScene==4 then\nset accepted=IssueTargetOrder(ciArcher,\"coupletarget\",ciHippo)\n"
        "else\nset accepted=IssueTargetOrder(ciHippo,\"coupletarget\",ciArcher)\nendif\n"
        "call BJassAssert(accepted,\"native producer accepted\")\n"
        "elseif age==399 then\nset g=CreateGroup()\ncall GroupEnumUnitsOfPlayer(g,Player(0),null)\n"
        "loop\nset u=FirstOfGroup(g)\nexitwhen u==null\ncall GroupRemoveUnit(g,u)\ncall RemoveUnit(u)\nendloop\n"
        "call DestroyGroup(g)\nset ciArcher=null\nset ciHippo=null\nset cbScene=cbScene+1\n"
        "if cbScene==9 then\ncall PauseTimer(cbTimer)\nreturn\nendif\nendif\nset cbTick=cbTick+1\nendfunction\n"
        "function cbStart takes nothing returns nothing\nset cbTimer=CreateTimer()\ncall TimerStart(cbTimer,0.02,true,function cbClock)\nendfunction\n"));
    move_test_group_arrival=CouplePollArrival;move_test_group_commit=CouplePollCommit;move_test_group_committed=CouplePollCommitted;
    CoupleInstantCall("cbStart");FOR_LOOP(i,464)CoupleInstantStep();
    T_EQ(couple_boundary_spawns,1);T_ASSERT(couple_poll_trace.count>0);
    unsigned prefix=couple_poll_trace.count,scene=couple_poll_trace.scene;
    edict_t *roles[4];memcpy(roles,couple_poll_trace.roles,sizeof(roles));
    cstring_t path=Test_TempPath("couple-instant229-boundary-polls.bin");T_ASSERT(WriteGame(path));
    FOR_LOOP(i,13936)CoupleInstantStep();T_EQ(couple_boundary_spawns,9);
    T_ASSERT(!jass_rterror_pending(level.vm));CouplePollCheckNative193();
    unsigned total=couple_poll_trace.count;
    couplePollRow_t *expected=malloc(sizeof(couple_poll_trace.rows));memcpy(expected,couple_poll_trace.rows,sizeof(couple_poll_trace.rows));
    T_ASSERT(ReadGame(path));couple_poll_trace.count=prefix;couple_poll_trace.scene=scene;couple_poll_trace.print=false;
    memcpy(couple_poll_trace.roles,roles,sizeof(roles));couple_boundary_spawns=1;
    FOR_LOOP(i,13936)CoupleInstantStep();T_EQ(couple_boundary_spawns,9);T_EQ(couple_poll_trace.count,total);
    FOR_LOOP(i,total-prefix) {
        T_EQ(couple_poll_trace.rows[prefix+i].scene,expected[prefix+i].scene);
        FOR_LOOP(j,20)T_EQ(couple_poll_trace.rows[prefix+i].words[j],expected[prefix+i].words[j]);
    }
    T_ASSERT(!jass_rterror_pending(level.vm));free(expected);remove(path);
    move_test_group_arrival=NULL;move_test_group_commit=NULL;move_test_group_committed=NULL;
    CoupleInstantEnd(&f);
    for(jassModule_t *p=jass_funcs;p->name;p++)if(!strcmp(p->name,"BJassAssert")){p->func=couple_boundary_assert_original;break;}
}

#include "fixtures/retail_couple_instant_stock193_polls.h"
static void CouplePollCheckNativeStock193(void) {
    unsigned at=0;
    unsigned const arrival[]={2,3,4,5,6,7,8,18,10,11},committed[]={2,13,14,15,16,7};
    unsigned const count=sizeof(couple_native_stock_polls193)/sizeof(couple_native_stock_polls193[0]);
    T_EQ(count,540);
    FOR_LOOP(i,couple_poll_trace.count) {
        couplePollRow_t const *row=&couple_poll_trace.rows[i];if(row->words[0]==1)continue;
        T_ASSERT(at<count);if(at>=count)break;
        coupleNativeStockPoll193_t const *expected=&couple_native_stock_polls193[at++];
        T_EQ(row->scene,expected->scene);T_EQ(row->words[0],expected->phase);T_EQ(row->words[1],expected->role);
        unsigned const *indices=row->words[0]==0 ? arrival : committed;
        unsigned words=row->words[0]==0 ? 10 : 6;T_EQ(words,expected->count);
        FOR_LOOP(j,words)T_EQ(row->words[indices[j]],expected->words[j]);
    }
    T_EQ(at,count);
}
TEST(wc3_couple_instant_orders, continuous_native_stock_polls_and_saved_suffix) {
    for(jassModule_t *p=jass_funcs;p->name;p++)if(!strcmp(p->name,"BJassAssert")) {
        couple_boundary_assert_original=p->func;p->func=CoupleBoundaryAssert;break;
    }
    coupleInstantFixture_t f;
    CoupleInstantBeginProfile(&f,&(coupleInstantSetup_t){.range333=false,.empty=true,.retail_profile=true,.peer_type="earc"});
    memset(&couple_poll_trace,0,sizeof(couple_poll_trace));couple_poll_trace.print=true;couple_boundary_spawns=0;
    T_ASSERT(CoupleInstantCompile(
        "globals\ninteger cbTick=0\ninteger cbScene=0\ntimer cbTimer=null\nendglobals\n"
        "function cbClock takes nothing returns nothing\nlocal integer age=cbTick-(cbTick/400)*400\n"
        "local real ax=704.0\nlocal group g=null\nlocal unit u=null\nlocal boolean accepted=false\n"
        "if age==0 then\n"
        "if cbScene==0 or cbScene==2 then\nset ax=1280.0\nendif\n"
        "set ciArcher=CreateUnit(Player(0),'earc',ax,1024.0,180.0)\n"
        "set ciHippo=CreateUnit(Player(0),'ehip',1536.0,1024.0,0.0)\n"
        "call SetUnitAcquireRange(ciArcher,0.0)\ncall SetUnitAcquireRange(ciHippo,0.0)\n"
        "call BJassAssert(GetUnitAbilityLevel(ciArcher,'Aco2')==1 and GetUnitAbilityLevel(ciHippo,'Aco3')==1,\"native configured aliases\")\n"
        /* Native CpMark creates/enumerates/destroys a rider-query group at
         * spawn, before the save frontier. Preserve that registry lifecycle. */
        "set g=CreateGroup()\ncall GroupEnumUnitsOfPlayer(g,Player(0),null)\ncall DestroyGroup(g)\nset g=null\n"
        "call BJassAssert(true,\"boundary scene \"+I2S(cbScene))\n"
        "elseif age==100 then\n"
        "if cbScene==0 or cbScene==1 then\nset accepted=IssueImmediateOrder(ciArcher,\"coupleinstant\")\n"
        "elseif cbScene==2 or cbScene==3 then\nset accepted=IssueImmediateOrder(ciHippo,\"coupleinstant\")\n"
        "elseif cbScene==4 then\nset accepted=IssueTargetOrder(ciArcher,\"coupletarget\",ciHippo)\n"
        "else\nset accepted=IssueTargetOrder(ciHippo,\"coupletarget\",ciArcher)\nendif\n"
        "call BJassAssert(accepted,\"native producer accepted\")\n"
        "elseif age==399 then\nset g=CreateGroup()\ncall GroupEnumUnitsOfPlayer(g,Player(0),null)\n"
        "loop\nset u=FirstOfGroup(g)\nexitwhen u==null\ncall GroupRemoveUnit(g,u)\ncall RemoveUnit(u)\nendloop\n"
        "call DestroyGroup(g)\nset ciArcher=null\nset ciHippo=null\nset cbScene=cbScene+1\n"
        "if cbScene==6 then\ncall PauseTimer(cbTimer)\nreturn\nendif\nendif\nset cbTick=cbTick+1\nendfunction\n"
        "function cbStart takes nothing returns nothing\nset cbTimer=CreateTimer()\ncall TimerStart(cbTimer,0.02,true,function cbClock)\nendfunction\n"));
    move_test_group_arrival=CouplePollArrival;move_test_group_commit=CouplePollCommit;move_test_group_committed=CouplePollCommitted;
    CoupleInstantCall("cbStart");FOR_LOOP(i,464)CoupleInstantStep();
    T_EQ(couple_boundary_spawns,1);T_ASSERT(couple_poll_trace.count>0);
    unsigned prefix=couple_poll_trace.count,scene=couple_poll_trace.scene;
    edict_t *roles[4];memcpy(roles,couple_poll_trace.roles,sizeof(roles));
    cstring_t path=Test_TempPath("couple-instant232-stock-polls.bin");T_ASSERT(WriteGame(path));
    FOR_LOOP(i,9136)CoupleInstantStep();T_EQ(couple_boundary_spawns,6);
    T_ASSERT(!jass_rterror_pending(level.vm));CouplePollCheckNativeStock193();
    unsigned total=couple_poll_trace.count;
    couplePollRow_t *expected=malloc(sizeof(couple_poll_trace.rows));memcpy(expected,couple_poll_trace.rows,sizeof(couple_poll_trace.rows));
    T_ASSERT(ReadGame(path));couple_poll_trace.count=prefix;couple_poll_trace.scene=scene;couple_poll_trace.print=false;
    memcpy(couple_poll_trace.roles,roles,sizeof(roles));couple_boundary_spawns=1;
    FOR_LOOP(i,9136)CoupleInstantStep();T_EQ(couple_boundary_spawns,6);T_EQ(couple_poll_trace.count,total);
    FOR_LOOP(i,total-prefix) {
        T_EQ(couple_poll_trace.rows[prefix+i].scene,expected[prefix+i].scene);
        FOR_LOOP(j,20)T_EQ(couple_poll_trace.rows[prefix+i].words[j],expected[prefix+i].words[j]);
    }
    T_ASSERT(!jass_rterror_pending(level.vm));free(expected);remove(path);
    move_test_group_arrival=NULL;move_test_group_commit=NULL;move_test_group_committed=NULL;
    CoupleInstantEnd(&f);
    for(jassModule_t *p=jass_funcs;p->name;p++)if(!strcmp(p->name,"BJassAssert")){p->func=couple_boundary_assert_original;break;}
}

#endif
