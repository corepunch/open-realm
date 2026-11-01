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
typedef struct {float archer_x;bool range333,empty,retail_profile,preparation_profile,repulsion_profile;cstring_t peer_type;} coupleInstantSetup_t;
static void CoupleInstantBeginProfile(coupleInstantFixture_t *f,coupleInstantSetup_t const *setup) {
    float archer_x=setup->archer_x;bool range333=setup->range333;cstring_t peer_type=setup->peer_type;
    reset_entities();setup_test_world();memset(f,0,sizeof(*f));f->old_info=level.mapinfo;
    bool tft=atoi(gi.CvarString("fs_expansion","0"))!=0;
    char ability[4096];snprintf(ability,sizeof(ability),
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
    char repulsion_balance[2048];
    if(setup->repulsion_profile) {
        snprintf(repulsion_balance,sizeof(repulsion_balance),"%.*s%s",(int)strlen(text[0])-2,text[0],
            "C;Y1;X5;K\"repulse\"\nC;X6;K\"repulseParam\"\nC;X7;K\"repulseGroup\"\nC;X8;K\"repulsePrio\"\n"
            "C;Y2;X5;K0\nC;Y3;X5;K1\nC;Y4;X5;K1\nE\n");
        text[0]=repulsion_balance;
    }
    char preparation_balance[2048],preparation_data[2048],preparation_abilities[1024],preparation_ui[1024];
    if(setup->preparation_profile) {
        /* Authored native243 ehip mana capacity, separate Inner Fire and stock
         * mana-potion alias. Keep this profile isolated from prior captures. */
        snprintf(preparation_balance,sizeof(preparation_balance),"%.*s%s",(int)strlen(text[0])-2,text[0],
            "C;Y1;X5;K\"manaN\"\nC;X6;K\"realM\"\nC;X7;K\"mana0\"\n"
            "C;Y3;X5;K200\nC;X6;K200\nC;X7;K0\n"
            "C;Y5;X1;K\"hfoo\"\nC;X2;K420\nC;X3;K270\nC;X4;K31\n"
            "C;Y1;X8;K\"repulse\"\nC;X9;K\"repulseParam\"\nC;X10;K\"repulseGroup\"\nC;X11;K\"repulsePrio\"\n"
            "C;Y2;X8;K0\nC;Y3;X8;K1\nC;Y4;X8;K1\n"
            "C;Y5;X8;K2\nC;X9;K17\nC;X10;K17\nC;X11;K17\nE\n");
        snprintf(preparation_data,sizeof(preparation_data),"%.*s%s",(int)strlen(text[1])-2,text[1],
            "C;Y5;X1;K\"hfoo\"\nC;X2;K\"foot\"\nC;X3;K0.6\nC;X4;K\"human\"\nC;X5;K31\nC;X6;K60\n"
            "C;Y1;X8;K\"targType\"\nC;X9;K\"castpt\"\nC;X10;K\"castbsw\"\nC;Y3;X9;K0.3\nC;X10;K0.51\nC;Y2;X8;K\"ground\"\nC;Y3;X8;K\"air\"\nC;Y4;X8;K\"air\"\nC;Y5;X8;K\"ground\"\nE\n");
        snprintf(preparation_abilities,sizeof(preparation_abilities),"%.*s%s",(int)strlen(text[2])-2,text[2],
            "C;Y5;X1;K\"hfoo\"\nC;X2;K\"Amov\"\nE\n");
        snprintf(preparation_ui,sizeof(preparation_ui),"%.*s%s",(int)strlen(text[3])-2,text[3],
            "C;Y5;X1;K\"hfoo\"\nC;X2;K\"TestUI\\Models\\anim_pulse.mdx\"\nE\n");
        size_t used=strlen(ability)-2;
        snprintf(ability+used,sizeof(ability)-used,
            "C;Y1;X10;K\"Cost1\"\nC;X11;K\"Cast1\"\nC;X12;K\"Cool1\"\nC;X13;K\"Dur1\"\nC;X14;K\"item\"\nC;X15;K\"checkDep\"\nC;X16;K\"%s\"\nC;X17;K\"%s\"\nC;X18;K\"%s\"\nC;X19;K\"BuffID1\"\n"
            "C;Y4;X1;K\"Xinf\"\nC;X2;K\"Ainf\"\nC;X3;K0.1\nC;X5;K99999\nC;X6;K1\nC;X7;K\"air,ground,friend,neutral,self\"\nC;X8;K5\nC;X10;K50\nC;X11;K0\nC;X12;K0\nC;X13;K60\nC;X16;K500\nC;X19;K\"Binf\"\n"
            "C;Y5;X1;K\"AIm1\"\nC;X2;K\"AIma\"\nC;X3;K150\nC;X5;K100\nC;X6;K1\nC;X7;K\"_\"\nC;X10;K0\nC;X11;K0\nC;X12;K20\nC;X14;K1\nC;X15;K0\n"
            "C;Y6;X1;K\"AInv\"\nC;X2;K\"AInv\"\nC;X3;K6\nC;X6;K1\nC;X7;K\"_\"\nC;X8;K0\nC;X16;K1\nC;X17;K1\nC;X18;K1\nE\n",
            tft ? "DataC1" : "Data13",tft ? "DataD1" : "Data14",tft ? "DataE1" : "Data15");
        text[0]=preparation_balance;text[1]=preparation_data;text[2]=preparation_abilities;text[3]=preparation_ui;
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
        FOR_LOOP(i,1200) {CoupleInstantStep();}
        T_EQ(CoupleInstantRiders(),1);
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
    FOR_LOOP(i,1200) {CoupleInstantStep();}
        T_EQ(f.archer->current_order_id,852507);T_EQ(CoupleInstantRiders(),0);
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
        if(scene) {CoupleInstantCall("cpSetScene");}
        CoupleInstantCall("cpStartClock");
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
        FOR_LOOP(i,1120) {CoupleInstantStep();}
        T_EQ(couple_poll_trace.count,total);
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
        if(mode==1){T_EQ(f.archer->current_order_id,851986);FOR_LOOP(i,60) {CoupleInstantStep();}
        T_ASSERT(f.archer->s.origin2.x<704);}
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
    FOR_LOOP(i,13936) {CoupleInstantStep();}
        T_EQ(couple_boundary_spawns,9);
    T_ASSERT(!jass_rterror_pending(level.vm));CouplePollCheckNative193();
    unsigned total=couple_poll_trace.count;
    couplePollRow_t *expected=malloc(sizeof(couple_poll_trace.rows));memcpy(expected,couple_poll_trace.rows,sizeof(couple_poll_trace.rows));
    T_ASSERT(ReadGame(path));couple_poll_trace.count=prefix;couple_poll_trace.scene=scene;couple_poll_trace.print=false;
    memcpy(couple_poll_trace.roles,roles,sizeof(roles));couple_boundary_spawns=1;
    FOR_LOOP(i,13936) {CoupleInstantStep();}
        T_EQ(couple_boundary_spawns,9);T_EQ(couple_poll_trace.count,total);
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
    FOR_LOOP(i,9136) {CoupleInstantStep();}
        T_EQ(couple_boundary_spawns,6);
    T_ASSERT(!jass_rterror_pending(level.vm));CouplePollCheckNativeStock193();
    unsigned total=couple_poll_trace.count;
    couplePollRow_t *expected=malloc(sizeof(couple_poll_trace.rows));memcpy(expected,couple_poll_trace.rows,sizeof(couple_poll_trace.rows));
    T_ASSERT(ReadGame(path));couple_poll_trace.count=prefix;couple_poll_trace.scene=scene;couple_poll_trace.print=false;
    memcpy(couple_poll_trace.roles,roles,sizeof(roles));couple_boundary_spawns=1;
    FOR_LOOP(i,9136) {CoupleInstantStep();}
        T_EQ(couple_boundary_spawns,6);T_EQ(couple_poll_trace.count,total);
    FOR_LOOP(i,total-prefix) {
        T_EQ(couple_poll_trace.rows[prefix+i].scene,expected[prefix+i].scene);
        FOR_LOOP(j,20)T_EQ(couple_poll_trace.rows[prefix+i].words[j],expected[prefix+i].words[j]);
    }
    T_ASSERT(!jass_rterror_pending(level.vm));free(expected);remove(path);
    move_test_group_arrival=NULL;move_test_group_commit=NULL;move_test_group_committed=NULL;
    CoupleInstantEnd(&f);
    for(jassModule_t *p=jass_funcs;p->name;p++)if(!strcmp(p->name,"BJassAssert")){p->func=couple_boundary_assert_original;break;}
}

/* Native233 ready chronology: exact public actor/target/head tuples. Callback
 * replacement/removal below are engine ownership controls, not retail captures. */
static jassCFunction_t couple_ready_assert_original;
static char couple_ready_rows[2048];
static unsigned couple_ready_frontiers;
static int couple_ready_mode;
static cstring_t couple_ready_save;
static uint32_t CoupleReadyAssert(jass_t *j) {
    cstring_t message=jass_checkstring(j,2);
    if(message && !strncmp(message,"ready-row:",10)) {
        unsigned kind,id,actor,target,archer_head,hippo_head;
        int fields=sscanf(message+10,"%u:%u:%u:%u:%u:%u;",&kind,&id,&actor,&target,&archer_head,&hippo_head);
        T_EQ(fields,6);
        if(fields==6) {
            /* Engine/JASS rawcode literals use MAKEFOURCC's little-endian
             * integer representation; retail logs print big-endian rawcodes.
             * Canonicalize only typed spell IDs. Order IDs/heads are numeric
             * and remain exact. This does not assert raw integer ABI parity. */
            if(kind>=2)id=((id&0xffu)<<24)|((id&0xff00u)<<8)|((id>>8)&0xff00u)|((id>>24)&0xffu);
            size_t used=strlen(couple_ready_rows);
            int written=snprintf(couple_ready_rows+used,sizeof(couple_ready_rows)-used,
                "%u:%u:%u:%u:%u:%u;",kind,id,actor,target,archer_head,hippo_head);
            T_ASSERT(written>=0 && (size_t)written<sizeof(couple_ready_rows)-used);
        }
    } else if(message && !strcmp(message,"ready-frontier")) {
        couple_ready_frontiers++;
        T_ASSERT(S_TargetApproachPublicationPending());
        if(couple_ready_mode) {
            T_ASSERT(!WriteGame(couple_ready_save));T_ASSERT(!ReadGame(couple_ready_save));
        }
    }
    return couple_ready_assert_original(j);
}
static void CoupleReadyBegin(coupleInstantFixture_t *f,int mode) {
    for(jassModule_t *p=jass_funcs;p->name;p++)if(!strcmp(p->name,"BJassAssert")) {
        couple_ready_assert_original=p->func;p->func=CoupleReadyAssert;break;
    }
    couple_ready_rows[0]=0;couple_ready_frontiers=0;couple_ready_mode=mode;
    CoupleInstantBeginProfile(f,&(coupleInstantSetup_t){.range333=true,.empty=true,.retail_profile=true,.peer_type="earc"});
    T_ASSERT(CoupleInstantCompile(
        "globals\ninteger crMode=0\nendglobals\n"
        "function crRole takes unit u returns integer\nif u==ciArcher then\nreturn 1\nelseif u==ciHippo then\nreturn 2\nendif\nreturn 0\nendfunction\n"
        "function crRow takes integer kind,integer id,unit target returns nothing\n"
        "call BJassAssert(true,\"ready-row:\"+I2S(kind)+\":\"+I2S(id)+\":\"+I2S(crRole(GetTriggerUnit()))+\":\"+I2S(crRole(target))+\":\"+I2S(GetUnitCurrentOrder(ciArcher))+\":\"+I2S(GetUnitCurrentOrder(ciHippo))+\";\")\nendfunction\n"
        "function crImmediate takes nothing returns nothing\ncall crRow(0,GetIssuedOrderId(),null)\nendfunction\n"
        "function crTarget takes nothing returns nothing\ncall crRow(1,GetIssuedOrderId(),GetOrderTargetUnit())\n"
        "if GetIssuedOrderId()==852508 then\ncall BJassAssert(true,\"ready-frontier\")\n"
        "if crMode==1 then\ncall IssuePointOrder(ciHippo,\"move\",1792.0,1024.0)\nelseif crMode==2 then\ncall RemoveUnit(ciHippo)\nendif\nendif\nendfunction\n"
        "function crChannel takes nothing returns nothing\ncall crRow(2,GetSpellAbilityId(),GetSpellTargetUnit())\nendfunction\n"
        "function crCast takes nothing returns nothing\ncall crRow(3,GetSpellAbilityId(),GetSpellTargetUnit())\nendfunction\n"
        "function crEffect takes nothing returns nothing\ncall crRow(4,GetSpellAbilityId(),GetSpellTargetUnit())\nendfunction\n"
        "function crEnd takes nothing returns nothing\ncall crRow(5,GetSpellAbilityId(),GetSpellTargetUnit())\nendfunction\n"
        "function crFinish takes nothing returns nothing\ncall crRow(6,GetSpellAbilityId(),GetSpellTargetUnit())\nendfunction\n"
        "function crInstall takes nothing returns nothing\nlocal trigger t\n"
        "set ciArcher=CreateUnit(Player(0),'earc',1280.0,1024.0,0.0)\nset ciHippo=CreateUnit(Player(0),'ehip',1536.0,1024.0,180.0)\n"
        "call SetUnitAcquireRange(ciArcher,0.0)\ncall SetUnitAcquireRange(ciHippo,0.0)\n"
        "set t=CreateTrigger()\ncall TriggerRegisterUnitEvent(t,ciArcher,EVENT_UNIT_ISSUED_ORDER)\ncall TriggerRegisterUnitEvent(t,ciHippo,EVENT_UNIT_ISSUED_ORDER)\ncall TriggerAddAction(t,function crImmediate)\n"
        "set t=CreateTrigger()\ncall TriggerRegisterUnitEvent(t,ciArcher,EVENT_UNIT_ISSUED_TARGET_ORDER)\ncall TriggerRegisterUnitEvent(t,ciHippo,EVENT_UNIT_ISSUED_TARGET_ORDER)\ncall TriggerAddAction(t,function crTarget)\n"
        "endfunction\n"));
    CoupleInstantCall("crInstall");
    unsigned const events[]={289,290,291,293,292};
    cstring_t const handlers[]={"crChannel","crCast","crEffect","crEnd","crFinish"};
    char script[2600];size_t used=snprintf(script,sizeof(script),"function crPhases takes nothing returns nothing\nlocal trigger t\n");
    FOR_LOOP(i,5)used+=snprintf(script+used,sizeof(script)-used,
        "set t=CreateTrigger()\ncall TriggerRegisterUnitEvent(t,ciArcher,ConvertUnitEvent(%u))\ncall TriggerRegisterUnitEvent(t,ciHippo,ConvertUnitEvent(%u))\ncall TriggerAddAction(t,function %s)\n",events[i],events[i],handlers[i]);
    snprintf(script+used,sizeof(script)-used,"set crMode=%d\nendfunction\n",mode);
    T_ASSERT(CoupleInstantCompile(script));CoupleInstantCall("crPhases");
    FILTER_EDICTS(e,e->inuse) {
        if(e->class_id==MAKEFOURCC('e','a','r','c'))f->archer=e;
        if(e->class_id==MAKEFOURCC('e','h','i','p'))f->hippo=e;
    }
    T_NOT_NULL(f->archer);T_NOT_NULL(f->hippo);
    memset(&couple_poll_trace,0,sizeof(couple_poll_trace));couple_poll_trace.roles[0]=f->archer;couple_poll_trace.roles[1]=f->hippo;
    move_test_group_arrival=CouplePollArrival;move_test_group_committed=CouplePollCommitted;
}
static void CoupleReadyIssue(unsigned scene) {
    cstring_t const orders[]={"IssueTargetOrder(ciArcher,\"coupletarget\",ciHippo)","IssueTargetOrder(ciHippo,\"coupletarget\",ciArcher)","IssueImmediateOrder(ciArcher,\"coupleinstant\")","IssueImmediateOrder(ciHippo,\"coupleinstant\")"};
    char script[420];snprintf(script,sizeof(script),"function crIssue takes nothing returns nothing\ncall BJassAssert(%s,\"ready order accepted\")\nendfunction\n",orders[scene]);
    T_ASSERT(CoupleInstantCompile(script));CoupleInstantCall("crIssue");
}
static void CoupleReadyEnd(coupleInstantFixture_t *f) {
    move_test_group_arrival=NULL;move_test_group_committed=NULL;
    CoupleInstantEnd(f);
    for(jassModule_t *p=jass_funcs;p->name;p++)if(!strcmp(p->name,"BJassAssert")){p->func=couple_ready_assert_original;break;}
}
TEST(wc3_couple_instant_orders, native_ready_producer_public_phase_chronology) {
    cstring_t const expected[]={
        "1:852507:1:2:852507:0;2:1097035570:1:2:852507:0;3:1097035570:1:2:852507:0;4:1097035570:1:2:852507:0;",
        "1:852507:2:1:0:852507;2:1097035571:2:1:0:852507;3:1097035571:2:1:0:852507;4:1097035571:2:1:0:852507;5:1097035571:2:1:0:852507;",
        "0:852508:1:0:852508:0;1:852508:1:2:852508:0;1:852507:2:1:852508:852507;2:1097035571:2:1:852508:852507;3:1097035571:2:1:852508:852507;4:1097035571:2:1:852508:852507;0:851972:1:0:851972:852507;5:1097035571:2:1:0:852507;",
        "0:852508:2:0:0:852508;1:852508:2:1:0:852508;2:1097035571:2:1:0:852508;3:1097035571:2:1:0:852508;4:1097035571:2:1:0:852508;5:1097035571:2:1:0:852508;",
    };
    FOR_LOOP(scene,4) {
        coupleInstantFixture_t f;CoupleReadyBegin(&f,0);CoupleReadyIssue(scene);
        T_ASSERT(!strcmp(couple_ready_rows,expected[scene]));
        T_EQ(couple_ready_frontiers,scene>=2 ? 1 : 0);T_ASSERT(!S_TargetApproachPublicationPending());
        T_EQ(couple_poll_trace.count,0);T_ASSERT(CoupleInstantGroup(f.archer)==NULL);T_ASSERT(CoupleInstantGroup(f.hippo)==NULL);
        T_EQ(CoupleInstantRiders(),scene==0 ? 0 : 1);
        if(scene==0) {T_ASSERT(f.archer->couple_instant->waiting);T_EQ(f.archer->current_order_id,852507);}
        else {T_FEQ(f.archer->health.value,0,0);T_FEQ(f.hippo->health.value,0,0);}
        FOR_LOOP(i,20) {CoupleInstantStep();}
        T_EQ(couple_poll_trace.count,0);
        CoupleReadyEnd(&f);
    }
}
TEST(wc3_couple_instant_orders, ready_publication_callback_preserves_replacement_and_blocks_partial_save) {
    for(int mode=1;mode<=2;mode++) {
        coupleInstantFixture_t f;CoupleReadyBegin(&f,mode);
        couple_ready_save=Test_TempPath("couple-ready230-publication.bin");T_ASSERT(WriteGame(couple_ready_save));
        CoupleReadyIssue(3);T_EQ(couple_ready_frontiers,1);T_ASSERT(!S_TargetApproachPublicationPending());
        T_EQ(CoupleInstantRiders(),0);T_ASSERT(!strstr(couple_ready_rows,"2:1097035571:"));
        if(mode==1) {
            T_EQ(f.hippo->current_order_id,851986);T_ASSERT(!f.hippo->couple_instant->ready_pending);
            FOR_LOOP(i,120) {CoupleInstantStep();}
        T_ASSERT(f.hippo->s.origin2.x>1536);
        } else T_ASSERT(!f.hippo->inuse || G_IsDeferredFree(f.hippo));
        T_ASSERT(WriteGame(couple_ready_save));T_ASSERT(ReadGame(couple_ready_save));
        remove(couple_ready_save);CoupleReadyEnd(&f);
    }
}

#include "fixtures/retail_couple_instant233_boundaries.h"
static jassCFunction_t couple_adjacent_assert_original;
static unsigned couple_adjacent_events,couple_adjacent_spawns,couple_adjacent_returns;
static bool couple_adjacent_complete;
static uint32_t CoupleAdjacentAssert(jass_t *j) {
    cstring_t message=jass_checkstring(j,2);unsigned tick,scene;
    char label[40];
    if(message && sscanf(message,"CPR tick=%u scene=%u label=%39s",&tick,&scene,label)==3 &&
       scene==11 && !strcmp(label,"complete"))couple_adjacent_complete=true;
    if(message && sscanf(message,"CPR tick=%u scene=%u label=%39s",&tick,&scene,label)==3 && scene<11) {
        if(!strcmp(label,"spawn-a")) {
            couple_poll_trace.scene=scene;couple_adjacent_spawns++;
            memset(couple_poll_trace.roles,0,sizeof(couple_poll_trace.roles));
            FILTER_EDICTS(e,e->inuse && !G_IsDeferredFree(e) && !M_IsDead(e)) {
                if(e->class_id==MAKEFOURCC('e','a','r','c'))couple_poll_trace.roles[0]=e;
                if(e->class_id==MAKEFOURCC('e','h','i','p'))couple_poll_trace.roles[1]=e;
            }
            T_NOT_NULL(couple_poll_trace.roles[0]);T_NOT_NULL(couple_poll_trace.roles[1]);
            if(scene>=7) {
                uint32_t const headings[]={0x3e4cccda,0x3e4cccdb,0x3e4cccdc,0x3e4ccccd};
                T_EQ(wc3_float_bits(couple_poll_trace.roles[0]->s.angle),headings[scene-7]);
            }
        } else if(!strcmp(label,"order-return-setup")) {
            T_NOT_NULL(strstr(message,"accepted=1"));couple_adjacent_returns++;
            bool task=scene==4 || scene==8 || scene==9;
            edict_t *actor=couple_poll_trace.roles[scene>=4 && scene<=6 ? 1 : 0];
            moveGroup_t *group=CoupleInstantGroup(actor);
            if(task) {
                T_NOT_NULL(group);
                if(group)T_EQ(wc3_float_bits(group->members[0].arrival_range),scene==4 ? 0x413a7fd3u : 0x7cffffffu);
            } else {
                T_ASSERT(CoupleInstantGroup(couple_poll_trace.roles[0])==NULL);
                T_ASSERT(CoupleInstantGroup(couple_poll_trace.roles[1])==NULL);
            }
        } else if(!strncmp(label,"issued-",7) || !strncmp(label,"spell-",6)) {
            unsigned id=0,actor=0,target=0,ah=0,hh=0;char const *fields=strstr(message," oid=");
            bool spell=!strncmp(label,"spell-",6);
            if(spell)fields=strstr(message," ability=");
            T_NOT_NULL(fields);
            int n=fields ? sscanf(strchr(fields,'=')+1,"%u actor=%u target=%u ah=%u hh=%u",&id,&actor,&target,&ah,&hh) : 0;
            T_EQ(n,5);
            unsigned kind=!strcmp(label,"issued-immediate") ? 0 : !strcmp(label,"issued-target") ? 1 :
                !strcmp(label,"spell-channel") ? 2 : !strcmp(label,"spell-cast") ? 3 :
                !strcmp(label,"spell-effect") ? 4 : !strcmp(label,"spell-endcast") ? 5 : 6;
            if(spell)id=((id&0xffu)<<24)|((id&0xff00u)<<8)|((id>>8)&0xff00u)|((id>>24)&0xffu);
            unsigned count=sizeof(couple_native_public233)/sizeof(couple_native_public233[0]);
            T_ASSERT(couple_adjacent_events<count);
            if(n==5 && couple_adjacent_events<count) {
                coupleNativePublic233_t const *e=&couple_native_public233[couple_adjacent_events++];
                T_EQ(scene,e->scene);T_EQ(tick,e->tick);T_EQ(kind,e->kind);T_EQ(id,e->id);
                T_EQ(actor,e->actor);T_EQ(target,e->target);T_EQ(ah,e->archer_head);T_EQ(hh,e->hippo_head);
                T_EQ(wc3_float_bits(level.pathing_clock.time),e->clock_word);
                if(couple_poll_trace.print)fprintf(stderr,"ACOIPUBLIC %u %u %u %u %u %u %u %u %u\n",
                    scene,tick,kind,id,actor,target,ah,hh,wc3_float_bits(level.pathing_clock.time));
            }
        }
    }
    return couple_adjacent_assert_original(j);
}
static void CouplePollCheckNative233(void) {
    unsigned at=0,counts[11]={0};
    unsigned const arrival[]={2,3,4,5,6,7,8,18,10,11},committed[]={2,13,14,15,16,7};
    unsigned count=sizeof(couple_native_polls233)/sizeof(couple_native_polls233[0]);T_EQ(count,12);
    FOR_LOOP(i,couple_poll_trace.count) {
        couplePollRow_t const *row=&couple_poll_trace.rows[i];if(row->words[0]==1)continue;
        T_ASSERT(at<count);if(at>=count)break;
        coupleNativePoll233_t const *e=&couple_native_polls233[at++];
        T_EQ(row->scene,e->scene);T_EQ(row->words[0],e->phase);T_EQ(row->words[1],e->role);
        unsigned const *indices=e->phase==0 ? arrival : committed;
        FOR_LOOP(j,e->count)T_EQ(row->words[indices[j]],e->words[j]);
        if(row->words[0]==0 && row->scene<11)counts[row->scene]++;
    }
    T_EQ(at,count);
    FOR_LOOP(i,11)T_EQ(counts[i],i==4 || i==8 || i==9 ? 2 : 0);
}
/* Floating timer deadlines accumulate; 88 nominal seconds need not include
 * callback4399. Stop on the probe's own completion marker, with a hard bound,
 * preserving every captured absolute clock rather than shifting expectations. */
static unsigned CoupleAdjacentRunToComplete(void) {
    unsigned steps=0;
    while(!couple_adjacent_complete && steps<17200) {CoupleInstantStep();steps++;}
    T_ASSERT(couple_adjacent_complete);
    return steps;
}
TEST(wc3_couple_instant_orders, native_adjacent_range_and_public_facing_boundaries) {
    for(jassModule_t *p=jass_funcs;p->name;p++)if(!strcmp(p->name,"BJassAssert")) {
        couple_adjacent_assert_original=p->func;p->func=CoupleAdjacentAssert;break;
    }
    coupleInstantFixture_t f;
    CoupleInstantBeginProfile(&f,&(coupleInstantSetup_t){.range333=true,.empty=true,.retail_profile=true,.peer_type="earc"});
    /* Parsed w3a-style binary floats, not decimal SLK approximations. The
     * three aliases inherit Aco3 and override only its authored range. */
    float ranges[3];unitModification_t mods[3];unitData_t aliases[3];
    FOR_LOOP(i,3) {
        ranges[i]=wc3_float(0x43a67fd3u+i);
        mods[i]=(unitModification_t){.modID=MAKEFOURCC('a','r','a','n'),.type=mod_unreal,.level=1,.data=&ranges[i]};
        aliases[i]=(unitData_t){.originalUnitID=MAKEFOURCC('A','c','o','3'),.newUnitID=MAKEFOURCC('X','c','3','1'+i),.numbeOfModifications=1,.modifications=&mods[i]};
    }
    f.info.num_userCreatedAbilities=3;f.info.userCreatedAbilities=aliases;G_SetMapAbilityOverrides(&f.info);
    FOR_LOOP(i,3)T_EQ(wc3_float_bits(S_SpellRange(aliases[i].newUnitID,1)),0x43a67fd3u+i);
    memset(&couple_poll_trace,0,sizeof(couple_poll_trace));couple_poll_trace.print=true;
    couple_adjacent_events=couple_adjacent_spawns=couple_adjacent_returns=0;couple_adjacent_complete=false;
    /* Native233 public probe. Preload transport becomes a test observer;
     * absent Blizzard ModuloInteger is expressed with integer arithmetic;
     * Xc31/32/33 are typed tags (engine and retail rawcode integers differ).
     * Fixture common.j uses pre-mouse player spell IDs272..276; retail233
     * uses275..279. JassUnitEventType adds3 at registration for this common.j.
     * Public I2R/division facing expressions and all timer ages are intact. */
    T_ASSERT(CoupleInstantCompile(
        "globals\n"
        " timer cpTimer=null\n"
        " unit cpArcher=null\n"
        " unit cpHippo=null\n"
        " integer cpTick=0\n"
        " integer cpScene=0\n"
        " integer cpAlias='Aco3'\n"
        " integer cpRangeBits=1134985216\n"
        " integer cpFacingBits=0\n"
        " boolean cpNested=false\n"
        "endglobals\n"
        "function CpBool takes boolean b returns integer\n"
        " if b then\n"
        "  return 1\n"
        " endif\n"
        " return 0\n"
        "endfunction\n"
        "function CpEmit takes string value returns nothing\n"
        " call BJassAssert(true,value)\n"
        "endfunction\n"
        "function CpMark takes string label,boolean accepted returns nothing\n"
        " local group g=CreateGroup()\n"
        " local unit u=null\n"
        " local integer riders=0\n"
        " local real rx=0.0\n"
        " local real ry=0.0\n"
        " call GroupEnumUnitsOfPlayer(g,Player(0),null)\n"
        " loop\n"
        "  set u=FirstOfGroup(g)\n"
        "  exitwhen u==null\n"
        "  call GroupRemoveUnit(g,u)\n"
        "  if GetUnitTypeId(u)=='ehpr' then\n"
        "   set riders=riders+1\n"
        "   set rx=GetUnitX(u)\n"
        "   set ry=GetUnitY(u)\n"
        "  endif\n"
        " endloop\n"
        " call DestroyGroup(g)\n"
        " call CpEmit(\"CPR tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=\"+label+\"-a ah=\"+I2S(GetUnitCurrentOrder(cpArcher))+\" ax=\"+R2S(GetUnitX(cpArcher))+\" ay=\"+R2S(GetUnitY(cpArcher))+\" af=\"+R2S(GetUnitFacing(cpArcher))+\" al=\"+R2S(GetWidgetLife(cpArcher)))\n"
        " call CpEmit(\"CPR tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=\"+label+\"-h hh=\"+I2S(GetUnitCurrentOrder(cpHippo))+\" hx=\"+R2S(GetUnitX(cpHippo))+\" hy=\"+R2S(GetUnitY(cpHippo))+\" hf=\"+R2S(GetUnitFacing(cpHippo))+\" hl=\"+R2S(GetWidgetLife(cpHippo)))\n"
        " call CpEmit(\"CPR tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=\"+label+\"-setup accepted=\"+I2S(CpBool(accepted))+\" reht=\"+I2S(GetPlayerTechCount(Player(0),'Reht',true))+\" at=\"+I2S(GetUnitTypeId(cpArcher))+\" aa=\"+I2S(GetUnitAbilityLevel(cpArcher,'Aco2'))+\" ht=\"+I2S(GetUnitTypeId(cpHippo))+\" ha=\"+I2S(GetUnitAbilityLevel(cpHippo,cpAlias))+\" pa=\"+I2S(CpBool(IsUnitPaused(cpArcher)))+\" ph=\"+I2S(CpBool(IsUnitPaused(cpHippo))))\n"
        " call CpEmit(\"CPR tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=\"+label+\"-result riders=\"+I2S(riders)+\" rx=\"+R2S(rx)+\" ry=\"+R2S(ry))\n"
        "\n"
        "endfunction\n"
        "function CpEvent takes string kind returns nothing\n"
        " local unit actor=GetTriggerUnit()\n"
        " local integer role=0\n"
        " local integer target=0\n"
        " local integer oid=GetIssuedOrderId()\n"
        " if actor==cpArcher then\n"
        "  set role=1\n"
        " elseif actor==cpHippo then\n"
        "  set role=2\n"
        " else\n"
        "  return\n"
        " endif\n"
        " if GetOrderTargetUnit()==cpArcher then\n"
        "  set target=1\n"
        " elseif GetOrderTargetUnit()==cpHippo then\n"
        "  set target=2\n"
        " endif\n"
        " call CpEmit(\"CPR tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=issued-\"+kind+\" oid=\"+I2S(oid)+\" actor=\"+I2S(role)+\" target=\"+I2S(target)+\" ah=\"+I2S(GetUnitCurrentOrder(cpArcher))+\" hh=\"+I2S(GetUnitCurrentOrder(cpHippo)))\n"
        " set actor=null\n"
        "endfunction\n"
        "function CpTargetEvent takes nothing returns nothing\n"
        " call CpEvent(\"target\")\n"
        "endfunction\n"
        "function CpPointEvent takes nothing returns nothing\n"
        " call CpEvent(\"point\")\n"
        "endfunction\n"
        "function CpImmediateEvent takes nothing returns nothing\n"
        " call CpEvent(\"immediate\")\n"
        "endfunction\n"
        "function CpSpellEvent takes string kind returns nothing\n"
        " local unit actor=GetTriggerUnit()\n"
        " local unit target=GetSpellTargetUnit()\n"
        " local integer role=0\n"
        " local integer tr=0\n"
        " if actor==cpArcher then\n"
        "  set role=1\n"
        " elseif actor==cpHippo then\n"
        "  set role=2\n"
        " else\n"
        "  return\n"
        " endif\n"
        " if target==cpArcher then\n"
        "  set tr=1\n"
        " elseif target==cpHippo then\n"
        "  set tr=2\n"
        " endif\n"
        " call CpEmit(\"CPR tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=spell-\"+kind+\" ability=\"+I2S(GetSpellAbilityId())+\" actor=\"+I2S(role)+\" target=\"+I2S(tr)+\" ah=\"+I2S(GetUnitCurrentOrder(cpArcher))+\" hh=\"+I2S(GetUnitCurrentOrder(cpHippo)))\n"
        " set actor=null\n"
        " set target=null\n"
        "endfunction\n"
        "function CpSpellChannel takes nothing returns nothing\n"
        " call CpSpellEvent(\"channel\")\n"
        "endfunction\n"
        "function CpSpellCast takes nothing returns nothing\n"
        " call CpSpellEvent(\"cast\")\n"
        "endfunction\n"
        "function CpSpellEffect takes nothing returns nothing\n"
        " call CpSpellEvent(\"effect\")\n"
        "endfunction\n"
        "function CpSpellEnd takes nothing returns nothing\n"
        " call CpSpellEvent(\"endcast\")\n"
        "endfunction\n"
        "function CpSpellFinish takes nothing returns nothing\n"
        " call CpSpellEvent(\"finish\")\n"
        "endfunction\n"
        "function CpTick takes nothing returns nothing\n"
        " local integer age=cpTick-(cpTick/400)*400\n"
        " local real ax=1280.0\n"
        " local real af=0.0\n"
        " local group g=null\n"
        " local unit u=null\n"
        " local boolean removed=false\n"
        " local boolean added=false\n"
        " if age==0 then\n"
        "  set cpAlias='Aco3'\n"
        "  set cpRangeBits=1134985216\n"
        "  set cpFacingBits=0\n"
        "  if cpScene>=4 and cpScene<=6 then\n"
        "   set ax=1163.0\n"
        "   if cpScene==4 then\n"
        "set cpAlias='Xc31'\n"
        "elseif cpScene==5 then\n"
        "set cpAlias='Xc32'\n"
        "else\n"
        "set cpAlias='Xc33'\n"
        "endif\n"
        "   set cpRangeBits=1134985171+cpScene-4\n"
        "  endif\n"
        "  if cpScene==7 then\n"
        "   set cpFacingBits=1094146240\n"
        "   set af=I2R(12015808)/1048576.0\n"
        "  elseif cpScene==8 then\n"
        "   set cpFacingBits=1094146241\n"
        "   set af=I2R(12015809)/1048576.0\n"
        "  elseif cpScene==9 then\n"
        "   set cpFacingBits=1094146242\n"
        "   set af=I2R(12015810)/1048576.0\n"
        "  elseif cpScene==10 then\n"
        "   set cpFacingBits=1094146229\n"
        "   set af=I2R(12015797)/1048576.0\n"
        "  endif\n"
        "  set cpArcher=CreateUnit(Player(0),'earc',ax,1024.0,af)\n"
        "  set cpHippo=CreateUnit(Player(0),'ehip',1536.0,1024.0,180.0)\n"
        "  call SetUnitAcquireRange(cpArcher,0.0)\n"
        "  call SetUnitAcquireRange(cpHippo,0.0)\n"
        "  if cpScene>=4 and cpScene<=6 then\n"
        "   set removed=UnitRemoveAbility(cpHippo,'Aco3')\n"
        "   set added=UnitAddAbility(cpHippo,cpAlias)\n"
        "  endif\n"
        "  call CpEmit(\"CPR tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=alias alias=\"+I2S(cpAlias)+\" removed=\"+I2S(CpBool(removed))+\" added=\"+I2S(CpBool(added))+\" level=\"+I2S(GetUnitAbilityLevel(cpHippo,cpAlias)))\n"
        "  call CpEmit(\"CPR tick=\"+I2S(cpTick)+\" scene=\"+I2S(cpScene)+\" label=inputs rangeBits=\"+I2S(cpRangeBits)+\" facingBits=\"+I2S(cpFacingBits)+\" arange=333 area=900 ab=0 hb=1\")\n"
        "  call CpMark(\"spawn\",true)\n"
        " elseif age==100 then\n"
        "  call CpMark(\"before\",true)\n"
        "  if cpScene==2 then\n"
        "   call CpMark(\"order-return\",IssueImmediateOrder(cpArcher,\"coupleinstant\"))\n"
        "  elseif cpScene==3 then\n"
        "   call CpMark(\"order-return\",IssueImmediateOrder(cpHippo,\"coupleinstant\"))\n"
        "  elseif cpScene==1 or (cpScene>=4 and cpScene<=6) then\n"
        "   call CpMark(\"order-return\",IssueTargetOrder(cpHippo,\"coupletarget\",cpArcher))\n"
        "  else\n"
        "   call CpMark(\"order-return\",IssueTargetOrder(cpArcher,\"coupletarget\",cpHippo))\n"
        "  endif\n"
        " elseif age==398 then\n"
        "  call CpMark(\"settled\",true)\n"
        " elseif age==399 then\n"
        "  set g=CreateGroup()\n"
        "  call GroupEnumUnitsOfPlayer(g,Player(0),null)\n"
        "  loop\n"
        "   set u=FirstOfGroup(g)\n"
        "   exitwhen u==null\n"
        "   call GroupRemoveUnit(g,u)\n"
        "   call RemoveUnit(u)\n"
        "  endloop\n"
        "  call DestroyGroup(g)\n"
        "  set cpArcher=null\n"
        "  set cpHippo=null\n"
        "  set cpScene=cpScene+1\n"
        "  if cpScene==11 then\n"
        "   call CpEmit(\"CPR tick=\"+I2S(cpTick)+\" scene=11 label=complete\")\n"
        "   call PauseTimer(cpTimer)\n"
        "   return\n"
        "  endif\n"
        " elseif age>100 and (age<=115 or age-(age/10)*10==0) then\n"
        "  call CpMark(\"sample\",true)\n"
        " endif\n"
        " set cpTick=cpTick+1\n"
        "endfunction\n"
        "function PathProbeInit takes nothing returns nothing\n"
        " local trigger et=CreateTrigger()\n"
        " local trigger ep=CreateTrigger()\n"
        " local trigger ei=CreateTrigger()\n"
        " local trigger sc=CreateTrigger()\n"
        " local trigger ss=CreateTrigger()\n"
        " local trigger se=CreateTrigger()\n"
        " local trigger sd=CreateTrigger()\n"
        " local trigger sf=CreateTrigger()\n"
        " call TriggerRegisterPlayerUnitEvent(et,Player(0),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)\n"
        " call TriggerRegisterPlayerUnitEvent(ep,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)\n"
        " call TriggerRegisterPlayerUnitEvent(ei,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)\n"
        " call TriggerAddAction(et,function CpTargetEvent)\n"
        " call TriggerAddAction(ep,function CpPointEvent)\n"
        " call TriggerAddAction(ei,function CpImmediateEvent)\n"
        " call TriggerRegisterPlayerUnitEvent(sc,Player(0),ConvertPlayerUnitEvent(272),null)\n"
        " call TriggerRegisterPlayerUnitEvent(ss,Player(0),ConvertPlayerUnitEvent(273),null)\n"
        " call TriggerRegisterPlayerUnitEvent(se,Player(0),ConvertPlayerUnitEvent(274),null)\n"
        " call TriggerRegisterPlayerUnitEvent(sd,Player(0),ConvertPlayerUnitEvent(276),null)\n"
        " call TriggerRegisterPlayerUnitEvent(sf,Player(0),ConvertPlayerUnitEvent(275),null)\n"
        " call TriggerAddAction(sc,function CpSpellChannel)\n"
        " call TriggerAddAction(ss,function CpSpellCast)\n"
        " call TriggerAddAction(se,function CpSpellEffect)\n"
        " call TriggerAddAction(sd,function CpSpellEnd)\n"
        " call TriggerAddAction(sf,function CpSpellFinish)\n"
        " call FogEnable(false)\n"
        " call FogMaskEnable(false)\n"
        " call SetPlayerTechResearched(Player(0),'Reht',1)\n"
        " call CpEmit(\"CPR tick=0 scene=-1 label=start\")\n"
        " set cpTimer=CreateTimer()\n"
        " call TimerStart(cpTimer,0.02,true,function CpTick)\n"
        "endfunction\n"
    ));
    move_test_group_arrival=CouplePollArrival;move_test_group_committed=CouplePollCommitted;
    CoupleInstantCall("PathProbeInit");
    FOR_LOOP(i,464) {CoupleInstantStep();}
    unsigned prefix=couple_poll_trace.count,event_prefix=couple_adjacent_events;
    edict_t *roles[4];memcpy(roles,couple_poll_trace.roles,sizeof(roles));
    cstring_t path=Test_TempPath("couple-instant233-adjacent.bin");T_ASSERT(WriteGame(path));
    unsigned suffix_steps=CoupleAdjacentRunToComplete();
    T_EQ(couple_adjacent_spawns,11);T_EQ(couple_adjacent_returns,11);
    T_EQ(couple_adjacent_events,sizeof(couple_native_public233)/sizeof(couple_native_public233[0]));
    T_ASSERT(!jass_rterror_pending(level.vm));CouplePollCheckNative233();
    unsigned total=couple_poll_trace.count;
    couplePollRow_t expected[32];T_ASSERT(total<=32);memcpy(expected,couple_poll_trace.rows,MIN(total,32)*sizeof(*expected));
    T_ASSERT(ReadGame(path));couple_poll_trace.count=prefix;couple_poll_trace.scene=0;couple_poll_trace.print=false;
    memcpy(couple_poll_trace.roles,roles,sizeof(roles));couple_adjacent_events=event_prefix;couple_adjacent_spawns=1;couple_adjacent_returns=1;
    couple_adjacent_complete=false;T_EQ(CoupleAdjacentRunToComplete(),suffix_steps);
    T_EQ(couple_adjacent_events,sizeof(couple_native_public233)/sizeof(couple_native_public233[0]));T_EQ(couple_poll_trace.count,total);
    FOR_LOOP(i,MIN(total,32)-prefix) {
        T_EQ(couple_poll_trace.rows[prefix+i].scene,expected[prefix+i].scene);
        FOR_LOOP(j,20)T_EQ(couple_poll_trace.rows[prefix+i].words[j],expected[prefix+i].words[j]);
    }
    T_ASSERT(!jass_rterror_pending(level.vm));remove(path);
    move_test_group_arrival=NULL;move_test_group_committed=NULL;
    G_SetMapAbilityOverrides(NULL);CoupleInstantEnd(&f);
    for(jassModule_t *p=jass_funcs;p->name;p++)if(!strcmp(p->name,"BJassAssert")){p->func=couple_adjacent_assert_original;break;}
}


#include "fixtures/retail_couple_instant243_preparation.h"
#include "fixtures/retail_couple_instant244_context.h"
static jassCFunction_t couple_preparation_assert_original;
static handle_t (*couple_preparation_read_original)(cstring_t,uint32_t *);
static unsigned couple_preparation_rows,couple_preparation_base_rows,couple_preparation_contexts;
static uint32_t couple_preparation_phase_ability;
static bool couple_preparation_complete;
static unsigned couple_preparation_mode,couple_preparation_item_endcasts,couple_preparation_barriers;
static edict_t *couple_preparation_potion;
static cstring_t couple_preparation_save;
static uint64_t couple_preparation_save_hash;
static uint64_t CouplePreparationFileHash(cstring_t path) {
    FILE *file=fopen(path,"rb");T_NOT_NULL(file);if(!file)return 0;
    uint64_t hash=UINT64_C(14695981039346656037);int byte;
    while((byte=fgetc(file))!=EOF)hash=(hash^(uint8_t)byte)*UINT64_C(1099511628211);
    T_ASSERT(!ferror(file));fclose(file);return hash;
}
static char couple_preparation_script[24000];
static void CouplePreparationReplace(cstring_t from,cstring_t to) {
    char *p=couple_preparation_script;size_t a=strlen(from),b=strlen(to);
    while((p=strstr(p,from))) {
        T_ASSERT(strlen(couple_preparation_script)+b-a<sizeof(couple_preparation_script));
        memmove(p+b,p+a,strlen(p+a)+1);memcpy(p,to,b);p+=b;
    }
}
static handle_t CouplePreparationRead(cstring_t path,uint32_t *size) {
    if(!strcmp(path,"Scripts\\target243.ai")) {
        cstring_t source="function main takes nothing returns nothing\n"
            "call BJassAssert(true,\"CPF tick=0 scene=0 label=ai-thread-enter\")\n"
            "call SetCampaignAI()\ncall CreateCaptains()\ncall SetCaptainHome(1,1536.0,1024.0)\n"
            "loop\ncall Sleep(1.0)\nendloop\nendfunction\n";
        *size=strlen(source);char *data=gi.MemAlloc(*size+1);memcpy(data,source,*size+1);return data;
    }
    return couple_preparation_read_original(path,size);
}
#include "fixtures/retail_couple_instant249_ownership.h"
static struct {
    edict_t *source;
    unsigned expected,next_context,issued_moves;
    bool returned,channel,endcast,settled,intervened;
    vec2_t held;
} couple_ownership;
static void CoupleOwnershipCanonicalize(char *text,cstring_t key) {
    char *p=text;
    while((p=strstr(p,key))) {
        p+=strlen(key);char *end;uint32_t raw=(uint32_t)strtoul(p,&end,10);
        uint32_t native=((raw&255u)<<24)|((raw&0xff00u)<<8)|((raw>>8)&0xff00u)|(raw>>24);
        char value[32];snprintf(value,sizeof(value),"%u",native);
        size_t n=strlen(value);memmove(p+n,end,strlen(end)+1);memcpy(p,value,n);p+=n;
    }
}
static uint32_t CoupleOwnershipAssert(jass_t *j,cstring_t message) {
    unsigned tick,scene;if(sscanf(message,"CPF tick=%u scene=%u",&tick,&scene)!=2)return couple_preparation_assert_original(j);
    if(strstr(message,"label=start ")) {
        FILTER_EDICTS(e,e->inuse && e->user_data==5)couple_ownership.source=e;
        T_NOT_NULL(couple_ownership.source);
    }
    if(scene!=1)return couple_preparation_assert_original(j);
    if(couple_ownership.intervened) {
        if(strstr(message,"label=issued oid=851986 actor=5 "))couple_ownership.issued_moves++;
        return couple_preparation_assert_original(j);
    }
    char canonical[512];snprintf(canonical,sizeof(canonical),"%s",message);
    CoupleOwnershipCanonicalize(canonical," ability=");CoupleOwnershipCanonicalize(canonical," type=");CoupleOwnershipCanonicalize(canonical," item=");
    unsigned n=sizeof(couple_native_public249)/sizeof(couple_native_public249[0]);
    T_ASSERT(couple_ownership.expected<n);
    if(couple_ownership.expected<n) {
        cstring_t expected=couple_native_public249[couple_ownership.expected++];
        if(strcmp(canonical,expected))fprintf(stderr,"ACOIPREP249 expected:%s actual:%s\n",expected,canonical);
        T_ASSERT(!strcmp(canonical,expected));
    }
    if(strstr(canonical,"label=context ")) {
        unsigned count=sizeof(couple_native_context249)/sizeof(couple_native_context249[0]);
        T_ASSERT(couple_ownership.next_context<count);
        if(couple_ownership.next_context<count) {
            coupleNativeContext249_t const *context=&couple_native_context249[couple_ownership.next_context++];
            T_EQ(context->scene,1);T_EQ(context->tick,tick);
            T_EQ(wc3_float_bits(level.pathing_clock.time),context->clock_word);
        }
    }
    if(strstr(canonical,"label=issued oid=851986 actor=5 "))couple_ownership.issued_moves++;
    if(strstr(canonical,"label=outer-return ")) {
        T_NOT_NULL(strstr(canonical,"accepted=1"));couple_ownership.returned=true;
        T_EQ(couple_ownership.source->current_order_id,851986);
        couple_ownership.held=couple_ownership.source->s.origin2;
    }
    if(strstr(canonical,"label=spell event=272 actor=5 ability=1483304550"))couple_ownership.channel=true;
    if(strstr(canonical,"label=spell event=276 actor=5 ability=1483304550"))couple_ownership.endcast=true;
    if(tick==800 && strstr(canonical,"label=item "))couple_ownership.settled=true;
    return couple_preparation_assert_original(j);
}
static void CoupleOwnershipStep(void) {
    CoupleInstantStep();
    if(couple_ownership.returned && !couple_ownership.endcast && !couple_ownership.intervened) {
        edict_t *source=couple_ownership.source;
        T_EQ(source->current_order_id,851986);
        T_EQ(wc3_float_bits(source->s.origin2.x),wc3_float_bits(couple_ownership.held.x));
        T_EQ(wc3_float_bits(source->s.origin2.y),wc3_float_bits(couple_ownership.held.y));
    }
}
static void CoupleOwnershipCheck(void) {
    unsigned n=sizeof(couple_native_public249)/sizeof(couple_native_public249[0]);
    T_ASSERT(couple_ownership.settled);T_ASSERT(couple_ownership.channel);T_ASSERT(couple_ownership.endcast);
    T_ASSERT(couple_ownership.expected<n);
    if(couple_ownership.expected<n)T_ASSERT(strstr(couple_native_public249[couple_ownership.expected],"scene=1 ")==NULL);
    T_EQ(couple_ownership.issued_moves,1);
    T_EQ(couple_ownership.next_context,20);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_FEQ(couple_ownership.source->mana.value,100,0);
    T_ASSERT(couple_ownership.source->s.origin2.x>couple_ownership.held.x);
    T_ASSERT(!S_SpellPreparationPending());
}

static uint32_t CouplePreparationAssert(jass_t *j) {
    cstring_t message=jass_checkstring(j,2);
    if(couple_preparation_mode==7 && message && !strcmp(message,"engine direct preparation cast")) {
        edict_t *target=NULL;
        FILTER_EDICTS(e,e->inuse && e->user_data==7)target=e;
        T_NOT_NULL(target);T_NOT_NULL(couple_ownership.source);
        T_ASSERT(S_CastUnitTargetSpell(couple_ownership.source,MAKEFOURCC('X','i','n','f'),target));
        couple_ownership.returned=true;
        return couple_preparation_assert_original(j);
    }
    if(couple_preparation_mode>=3 && message && !strncmp(message,"CPF ",4))return CoupleOwnershipAssert(j,message);
    if(message && !strncmp(message,"CPF ",4)) {
        unsigned observed_tick,observed_scene;
        if(sscanf(message,"CPF tick=%u scene=%u",&observed_tick,&observed_scene)==2)
            couple_poll_trace.scene=observed_scene;
        /* Initial three markers precede retail PreloadGenClear. They establish
         * real public setup; the completed preload below owns all92 rows. */
        if(strstr(message,"label=start ")) {
            memset(&couple_poll_trace,0,sizeof(couple_poll_trace));
            FILTER_EDICTS(e,e->inuse && e->user_data>=1 && e->user_data<=4) {
                couple_poll_trace.roles[e->user_data-1]=e;
                T_ASSERT(e->aiflags&AI_TOWN_OWNED);
            }
            FOR_LOOP(i,4) {T_NOT_NULL(couple_poll_trace.roles[i]);}
            if(couple_poll_trace.roles[3]) {
                T_FEQ(couple_poll_trace.roles[3]->mana.max_value,200,0);
                T_FEQ(couple_poll_trace.roles[3]->mana.value,0,0);
                couple_preparation_potion=G_InventoryItem(couple_poll_trace.roles[3],0);
                T_NOT_NULL(couple_preparation_potion);
            }
        }
        if(strstr(message,"tick=0 ") && !strstr(message,"label=ai-thread-enter"))return couple_preparation_assert_original(j);
        if(couple_ownership.intervened) {
        if(strstr(message,"label=issued oid=851986 actor=5 "))couple_ownership.issued_moves++;
        return couple_preparation_assert_original(j);
    }
    char canonical[512];snprintf(canonical,sizeof(canonical),"%s",message);
        char *id=strstr(canonical," ability=");
        if(id) {
            uint32_t raw=(uint32_t)strtoul(id+9,NULL,10);
            uint32_t native=((raw&255u)<<24)|((raw&0xff00u)<<8)|((raw>>8)&0xff00u)|(raw>>24);
            snprintf(id+9,sizeof(canonical)-(size_t)(id+9-canonical),"%u",native);
            couple_preparation_phase_ability=native;
        }
        if(couple_preparation_mode==2 && strstr(message,"label=nested-before ")) {
            edict_t *source=couple_poll_trace.roles[3];uint32_t spawn=source->spawn_time;
            T_ASSERT(S_SpellPreparationPending());T_ASSERT(S_SpellPreparationBypassesTarget(source));
            T_ASSERT(!WriteGame(couple_preparation_save));T_ASSERT(!ReadGame(couple_preparation_save));
            T_EQ(CouplePreparationFileHash(couple_preparation_save),couple_preparation_save_hash);
            T_ASSERT(source->inuse);T_EQ(source->spawn_time,spawn);
            T_ASSERT(S_SpellPreparationBypassesTarget(source));couple_preparation_barriers++;
        }
        if(couple_preparation_mode==1) {
            /* Engine success control from the recovered AIma effect/charge
             * lifecycle, not a new exact native243 chronology claim. */
            if(strstr(canonical,"label=spell event=276 actor=4 ability=1095331121")) {
                edict_t *source=couple_poll_trace.roles[3];
                T_ASSERT(S_SpellPreparationPending());
                T_FEQ(source->mana.value,150,0);
                item_t const *potion=G_ItemState(couple_preparation_potion);
                T_NOT_NULL(potion);if(potion)T_EQ(potion->charges,1);
                couple_preparation_item_endcasts++;
            }
            if(strstr(message,"label=outer-return "))T_NOT_NULL(strstr(message,"accepted=1"));
            if(strstr(message,"label=complete "))couple_preparation_complete=true;
            return couple_preparation_assert_original(j);
        }
        if(strstr(message,"label=spellcontext ")) {
            unsigned tick,scene,event,actor,target;float x,y;
            int parsed=sscanf(message,"CPF tick=%u scene=%u label=spellcontext event=%u actor=%u target=%u x=%f y=%f",&tick,&scene,&event,&actor,&target,&x,&y);
            unsigned n=sizeof(couple_native_context244)/sizeof(couple_native_context244[0]);
            T_EQ(parsed,7);T_ASSERT(couple_preparation_contexts<n);
            if(parsed==7 && couple_preparation_contexts<n) {
                coupleNativeContext244_t const *e=&couple_native_context244[couple_preparation_contexts];
                T_EQ(tick,e->tick);T_EQ(event,e->event);T_EQ(actor,e->actor);T_EQ(target,e->target);
                T_EQ(couple_preparation_phase_ability,e->ability);
                T_EQ(wc3_float_bits(x),e->x_word);T_EQ(wc3_float_bits(y),e->y_word);
                T_EQ(wc3_float_bits(level.pathing_clock.time),e->clock_word);
            }
            couple_preparation_contexts++;
        }
        unsigned count=sizeof(couple_native_public244)/sizeof(couple_native_public244[0]);
        T_ASSERT(couple_preparation_rows<count);
        if(couple_preparation_rows<count) {
            cstring_t expected=couple_native_public244[couple_preparation_rows];
            if(strcmp(canonical,expected))fprintf(stderr,"ACOIPREP243 row%u expected:%s actual:%s\n",couple_preparation_rows,expected,canonical);
            T_ASSERT(!strcmp(canonical,expected));
        }
        couple_preparation_rows++;
        if(!strstr(message,"label=spellcontext ")) {
            unsigned base_count=sizeof(couple_native_public243)/sizeof(couple_native_public243[0]);
            T_ASSERT(couple_preparation_base_rows<base_count);
            if(couple_preparation_base_rows<base_count)T_ASSERT(!strcmp(canonical,couple_native_public243[couple_preparation_base_rows]));
            couple_preparation_base_rows++;
        }
        if(strstr(message,"label=complete "))couple_preparation_complete=true;
    }
    return couple_preparation_assert_original(j);
}
static void CouplePreparationRun(unsigned mode) {
    couple_preparation_mode=mode;couple_preparation_item_endcasts=couple_preparation_barriers=0;
    couple_preparation_potion=NULL;couple_preparation_save=NULL;
    /* Native bindings are cached when the map VM loads common.j. */
    for(jassModule_t *p=jass_funcs;p->name;p++)if(!strcmp(p->name,"BJassAssert")) {
        couple_preparation_assert_original=p->func;p->func=CouplePreparationAssert;break;
    }
    coupleInstantFixture_t f;
    CoupleInstantBeginProfile(&f,&(coupleInstantSetup_t){.empty=true,.retail_profile=true,.preparation_profile=true,.peer_type="earc"});
    slkTestData_t *items=parse_slk_string("ID;PWXL;N;E\nC;Y1;X1;K\"itemID\"\nC;X2;K\"file\"\nC;X3;K\"abilList\"\nC;X4;K\"uses\"\nC;X5;K\"usable\"\nC;X6;K\"perishable\"\nC;X7;K\"droppable\"\nC;X8;K\"cooldownID\"\nC;X9;K\"HP\"\n"
        "C;Y2;X1;K\"pman\"\nC;X2;K\"TestUI\\Models\\anim_pulse.mdx\"\nC;X3;K\"AIm1\"\nC;X4;K1\nC;X5;K1\nC;X6;K1\nC;X7;K1\nC;X8;K\"AIma\"\nC;X9;K75\nE\n");
    slkTestData_t *old_items=G_SetSLKRows("ItemData",items);
    int old_controller=game.clients[1].jass.controller;
    bool old_removed=game.clients[1].jass.removed;
    typeof(game.clients[1].mapplayer) old_player=game.clients[1].mapplayer;
    f.info.players[1].used=true;f.info.players[1].playerType=kPlayerTypeComputer;
    game.clients[1].mapplayer=f.info.players+1;game.clients[1].jass.controller=1;game.clients[1].jass.removed=false;
    G_BotInitPlayers();
    if(mode>=3) {
        /* Native249 inherits17x17 W3E vertices at0/0 and64x64 clear WPM.
         * Preserve2048 bounds even for its deliberately out-of-bounds spawns. */
        memset(&couple_ownership,0,sizeof(couple_ownership));
        couple_ownership.intervened=mode==7;
        while(!strstr(couple_native_public249[couple_ownership.expected],"scene=1 "))couple_ownership.expected++;
        while(couple_native_context249[couple_ownership.next_context].scene!=1)couple_ownership.next_context++;
    }
    couple_preparation_rows=couple_preparation_base_rows=couple_preparation_contexts=0;couple_preparation_phase_ability=0;couple_preparation_complete=false;
    couple_preparation_read_original=gi.ReadFile;gi.ReadFile=CouplePreparationRead;
    /* GetTriggerEventId is not implemented by this host. Separate typed event
     * callbacks observe the exact native roles without changing game behavior. */
    T_ASSERT(CoupleInstantCompile(
        "function CouplePreparationObserve takes string value returns nothing\ncall BJassAssert(true,value)\nendfunction\n"
        "function CpModulo takes integer a,integer b returns integer\nreturn a-(a/b)*b\nendfunction\n"
        "function CpRegister takes playerunitevent e,code f returns nothing\nlocal trigger t=CreateTrigger()\n"
        "call TriggerRegisterPlayerUnitEvent(t,Player(1),e,null)\ncall TriggerAddAction(t,f)\nendfunction\n"));
    snprintf(couple_preparation_script,sizeof(couple_preparation_script),"%s",mode>=3 ? couple_native_probe249 : couple_native_probe244);
    CouplePreparationReplace("ModuloInteger(","CpModulo(");
    /* The focused common.j omits these enum initializers. */
    CouplePreparationReplace("UNIT_STATE_MAX_MANA","ConvertUnitState(3)");
    CouplePreparationReplace("UNIT_STATE_MANA","ConvertUnitState(2)");
    CouplePreparationReplace("function CpSpell takes nothing returns nothing",mode>=3 ? "function CpSpell takes integer observedEvent returns nothing" : "function CpSpell takes integer observedEvent returns nothing\nif cpScene==1 and GetSpellAbilityId()=='Aco3' then\ncall BJassAssert(GetSpellTargetUnit()==null,\"forced channel has no unit target\")\nendif");
    CouplePreparationReplace("GetHandleId(GetTriggerEventId())","observedEvent");
    CouplePreparationReplace("function PathProbeInit takes nothing returns nothing",
        "function CpChannel takes nothing returns nothing\ncall CpSpell(272)\nendfunction\n"
        "function CpCast takes nothing returns nothing\ncall CpSpell(273)\nendfunction\n"
        "function CpEffect takes nothing returns nothing\ncall CpSpell(274)\nendfunction\n"
        "function CpFinish takes nothing returns nothing\ncall CpSpell(275)\nendfunction\n"
        "function CpEnd takes nothing returns nothing\ncall CpSpell(276)\nendfunction\n"
        "function PathProbeInit takes nothing returns nothing");
    CouplePreparationReplace("call TriggerRegisterPlayerUnitEvent(ts,Player(1),EVENT_PLAYER_UNIT_SPELL_CHANNEL,null)","call CpRegister(ConvertPlayerUnitEvent(272),function CpChannel)");
    CouplePreparationReplace("call TriggerRegisterPlayerUnitEvent(ts,Player(1),EVENT_PLAYER_UNIT_SPELL_CAST,null)","call CpRegister(ConvertPlayerUnitEvent(273),function CpCast)");
    CouplePreparationReplace("call TriggerRegisterPlayerUnitEvent(ts,Player(1),EVENT_PLAYER_UNIT_SPELL_EFFECT,null)","call CpRegister(ConvertPlayerUnitEvent(274),function CpEffect)");
    CouplePreparationReplace("call TriggerRegisterPlayerUnitEvent(ts,Player(1),EVENT_PLAYER_UNIT_SPELL_ENDCAST,null)","call CpRegister(ConvertPlayerUnitEvent(276),function CpEnd)");
    CouplePreparationReplace("call TriggerRegisterPlayerUnitEvent(ts,Player(1),EVENT_PLAYER_UNIT_SPELL_FINISH,null)","call CpRegister(ConvertPlayerUnitEvent(275),function CpFinish)");
    CouplePreparationReplace("call TriggerAddAction(ts,function CpSpell)","");
    if(mode>=3)CouplePreparationReplace("EVENT_PLAYER_UNIT_USE_ITEM","ConvertPlayerUnitEvent(50)");
    if(mode==1)CouplePreparationReplace("and not cpNested then","and false then");
    if(mode==7)CouplePreparationReplace(
        "set accepted=IssueTargetOrder(cpHippo[cpScene],\"innerfire\",cpBuff[cpScene])",
        "if cpScene==1 then\ncall BJassAssert(true,\"engine direct preparation cast\")\nset accepted=true\nelse\n"
        "set accepted=IssueTargetOrder(cpHippo[cpScene],\"innerfire\",cpBuff[cpScene])\nendif");
    T_ASSERT(CoupleInstantCompile(couple_preparation_script));CoupleInstantCall("PathProbeInit");
    if(mode==2) {
        couple_preparation_save=Test_TempPath("preparation-pending231.bin");
        T_ASSERT(WriteGame(couple_preparation_save));
        couple_preparation_save_hash=CouplePreparationFileHash(couple_preparation_save);
    }
    if(mode>=3) {
        unsigned steps=0;
        while(!couple_ownership.returned && steps++<2400)CoupleOwnershipStep();
        T_ASSERT(couple_ownership.returned);
        edict_t *source=couple_ownership.source;
        if(mode==7) {
            /* Direct AI/autocast entry keeps its existing immediate semantics.
             * Its preparation callback still creates a real public Move. */
            T_EQ(couple_ownership.issued_moves,1);
            T_EQ(source->current_order_id,G_OrderId("move"));
            T_FEQ(source->mana.value,100,0);
            T_ASSERT(!source->channel || !source->channel->resume_move.order_id);
            T_ASSERT(!S_SpellPreparationPending());
            cstring_t path=Test_TempPath("preparation-direct-move231.bin");
            vec2_t before=source->s.origin2;
            T_ASSERT(WriteGame(path));S_ClearMoveGroups();
            bool restored=ReadGame(path);T_ASSERT(restored);
            if(restored) {
                FOR_LOOP(frame,200) {CoupleInstantStep();}
                T_ASSERT(source->s.origin2.x>before.x);
                T_EQ(couple_ownership.issued_moves,1);
            }
            remove(path);goto preparation_cleanup;
        }
        if(mode==4 || mode==6) {
            while(!couple_ownership.channel && steps++<2500)CoupleOwnershipStep();
            T_ASSERT(couple_ownership.channel);T_ASSERT(!couple_ownership.endcast);
        } else {
            edict_t *receiver=S_UnitTargetApproachReceiver(source);
            T_NOT_NULL(receiver);
            if(receiver) {T_NOT_NULL(receiver->channel);if(receiver->channel)T_EQ(receiver->channel->resume_move.order_id,851986);}
            T_ASSERT(!couple_ownership.channel);
        }
        cstring_t path=Test_TempPath(mode==4 ? "preparation-move-cast231.bin" : "preparation-move-facing231.bin");
        T_ASSERT(WriteGame(path));
        typeof(couple_ownership) checkpoint=couple_ownership;
        unsigned saved_steps=steps;
        if(mode>=5) {
            /* Engine-only corruption and later-order tests, not additional
             * native249 callback chronology claims. Serialize real structs
             * through WriteGame; do not guess binary field offsets. */
            cstring_t invalid=Test_TempPath("preparation-invalid-resume231.bin");
            if(mode==5) {
                edict_t *receiver=S_UnitTargetApproachReceiver(source);
                T_NOT_NULL(receiver);
                if(receiver && receiver->channel)receiver->channel->resume_move.point.x=NAN;
            } else source->current_order_id=G_OrderId("stop");
            T_ASSERT(WriteGame(invalid));
            couple_ownership.intervened=true;
            S_ClearMoveGroups();T_ASSERT(!ReadGame(invalid));
            T_ASSERT(ReadGame(path));couple_ownership=checkpoint;
            remove(invalid);
            T_ASSERT(CoupleInstantCompile(
                "function CpLaterStop takes nothing returns nothing\n"
                "call BJassAssert(IssueImmediateOrder(cpHippo[1],\"stop\"),\"later Stop accepted\")\nendfunction\n"
                "function CpLaterMove takes nothing returns nothing\n"
                "call BJassAssert(IssuePointOrder(cpHippo[1],\"move\",1024.0,1536.0),\"later Move accepted\")\nendfunction\n"));
            /* Compile before the checkpoint used for repeated reloads. */
            T_ASSERT(WriteGame(path));
            FOR_LOOP(replacement,2) {
                couple_ownership.intervened=true;
                S_ClearMoveGroups();T_ASSERT(ReadGame(path));couple_ownership=checkpoint;
                couple_ownership.intervened=true;
                CoupleInstantCall(replacement ? "CpLaterMove" : "CpLaterStop");
                unsigned issued=checkpoint.issued_moves+(replacement ? 1 : 0);
                T_EQ(couple_ownership.issued_moves,issued);
                T_ASSERT(S_UnitTargetApproachReceiver(source)==NULL);
                vec2_t stopped=source->s.origin2;
                cstring_t replaced=Test_TempPath("preparation-replaced231.bin");
                T_ASSERT(WriteGame(replaced));
                typeof(couple_ownership) replaced_checkpoint=couple_ownership;
                vec2_t final_point={0};uint32_t final_order=0;
                FOR_LOOP(replay,2) {
                    if(replay) {
                        S_ClearMoveGroups();T_ASSERT(ReadGame(replaced));
                        couple_ownership=replaced_checkpoint;
                    }
                    FOR_LOOP(frame,400) {
                        CoupleInstantStep();
                        T_EQ(couple_ownership.issued_moves,issued);
                        if(source->current_order_id==G_OrderId("move") && source->goalentity &&
                           (source->goalentity->svflags&SVF_MOVE_WAYPOINT))
                            T_EQ(wc3_float_bits(source->goalentity->s.origin2.x),wc3_float_bits(1024.0f));
                    }
                    if(replacement)T_ASSERT(source->s.origin2.x<stopped.x-128);
                    else {T_EQ(source->current_order_id,0);T_FEQ(source->s.origin2.x,stopped.x,0);T_FEQ(source->s.origin2.y,stopped.y,0);}
                    T_ASSERT(!jass_rterror_pending(level.vm));
                    if(!replay){final_point=source->s.origin2;final_order=source->current_order_id;}
                    else {T_EQ(wc3_float_bits(source->s.origin2.x),wc3_float_bits(final_point.x));T_EQ(wc3_float_bits(source->s.origin2.y),wc3_float_bits(final_point.y));T_EQ(source->current_order_id,final_order);}
                }
                remove(replaced);
            }
            remove(path);goto preparation_cleanup;
        }
        while(!couple_ownership.settled && steps++<7000)CoupleOwnershipStep();
        CoupleOwnershipCheck();
        vec2_t final=source->s.origin2;uint32_t final_head=source->current_order_id;
        unsigned final_expected=couple_ownership.expected,final_steps=steps;
        S_ClearMoveGroups();T_ASSERT(ReadGame(path));couple_ownership=checkpoint;steps=saved_steps;
        while(!couple_ownership.settled && steps++<7000)CoupleOwnershipStep();
        CoupleOwnershipCheck();T_EQ(steps,final_steps);T_EQ(couple_ownership.expected,final_expected);
        T_EQ(wc3_float_bits(source->s.origin2.x),wc3_float_bits(final.x));
        T_EQ(wc3_float_bits(source->s.origin2.y),wc3_float_bits(final.y));T_EQ(source->current_order_id,final_head);
        remove(path);goto preparation_cleanup;
    }
    move_test_group_arrival=CouplePollArrival;move_test_group_committed=CouplePollCommitted;
    unsigned steps=0;while(!couple_preparation_complete && steps<3400){CoupleInstantStep();steps++;}
    T_ASSERT(couple_preparation_complete);T_ASSERT(!jass_rterror_pending(level.vm));
    if(mode==1) {
        T_EQ(couple_preparation_item_endcasts,1);
        T_ASSERT(CoupleInstantCompile("function CpSuccessCheck takes nothing returns nothing\n"
            "call BJassAssert(GetUnitState(cpHippo[1],ConvertUnitState(2))==100.0,\"preparation150 minus spell50\")\n"
            "call BJassAssert(GetItemCharges(cpPotion)==0,\"successful preparation consumes charge\")\n"
            "call BJassAssert(UnitItemInSlot(cpHippo[1],0)==null,\"last charge retires item after ENDCAST\")\nendfunction\n"));
        CoupleInstantCall("CpSuccessCheck");
    } else {
        T_EQ(couple_preparation_rows,sizeof(couple_native_public244)/sizeof(couple_native_public244[0]));
        T_EQ(couple_preparation_base_rows,sizeof(couple_native_public243)/sizeof(couple_native_public243[0]));
        T_EQ(couple_preparation_contexts,sizeof(couple_native_context244)/sizeof(couple_native_context244[0]));
    }
    T_ASSERT(!S_SpellPreparationPending());
    T_ASSERT(!S_SpellPreparationBypassesTarget(couple_poll_trace.roles[3]));
    if(mode==2) {
        T_EQ(couple_preparation_barriers,1);
        T_ASSERT(WriteGame(couple_preparation_save));
        S_ClearMoveGroups();T_ASSERT(ReadGame(couple_preparation_save));
        T_ASSERT(!S_SpellPreparationPending());remove(couple_preparation_save);
    }
    unsigned ordinary=0,nested=0;
    FOR_LOOP(i,couple_poll_trace.count)if(couple_poll_trace.rows[i].words[0]==0) {
        if(couple_poll_trace.rows[i].words[1]==1 && couple_poll_trace.rows[i].scene==0)ordinary++;
        if(couple_poll_trace.rows[i].words[1]==3 && couple_poll_trace.rows[i].scene==1)nested++;
    }
    /* Native scene0 has22 total polls: the first is a different Town mover
     * at tick1/fine(48,32), before either Acoi producer. The ordinary Hippo
     * at fine(48,16) owns exactly21, matching these21 primary clock words.
     * Role3 scene0 background ownership is outside the forced scene1 count.
     * All recorded rows remain intact. This is not a full numerical oracle. */
    T_EQ(ordinary,21);T_EQ(nested,0);
    T_ASSERT(!S_TargetApproachPublicationPending());
    move_test_group_arrival=NULL;move_test_group_committed=NULL;
preparation_cleanup:
    gi.ReadFile=couple_preparation_read_original;G_BotStop(1);
    for(jassModule_t *p=jass_funcs;p->name;p++)if(!strcmp(p->name,"BJassAssert")){p->func=couple_preparation_assert_original;break;}
    CoupleInstantEnd(&f);G_SetSLKRows("ItemData",old_items);free_slk_rows(items);
    game.clients[1].jass.controller=old_controller;game.clients[1].jass.removed=old_removed;game.clients[1].mapplayer=old_player;
}

TEST(wc3_couple_instant_orders, native_preparatory_item_callback_forced_entry) {
    CouplePreparationRun(0);
}
TEST(wc3_couple_instant_orders, preparatory_item_success_commits_after_endcast) {
    CouplePreparationRun(1);
}
TEST(wc3_couple_instant_orders, preparatory_callback_blocks_partial_save_then_unwinds) {
    CouplePreparationRun(2);
}

/* Native249 scene1 is the public oracle. Both snapshots replay its complete
 * remaining marker sequence with actual primary clocks; movement assertions
 * additionally guard the engine-owned private continuation. This is a
 * path-cleared ReadGame suffix, not a full server-map reload. */
TEST(wc3_couple_instant_orders, native_preparatory_move_continuation_saved_facing_and_cast) {
    CouplePreparationRun(3);CouplePreparationRun(4);
}

TEST(wc3_couple_instant_orders, preparatory_move_later_orders_and_invalid_saved_owner) {
    CouplePreparationRun(5);CouplePreparationRun(6);
}

TEST(wc3_couple_instant_orders, direct_preparatory_cast_does_not_orphan_move_staging) {
    CouplePreparationRun(7);
}

/* Append inside t_couple_instant_orders.c's test conditional. Native251 full
 * public replay; no sampled position injection and no shortened timer phases. */
#include "fixtures/retail_couple_instant251_cantpath.h"
#include "fixtures/retail_couple_instant258_rng.h"
static jassCFunction_t couple_cantpath_assert_original;
static struct { unsigned prefix, rows, events, accepted; bool complete; } couple_cantpath;
static void CoupleCantPathSeparationTrace(edict_t *unit,moveRepulseTrace_t const *before) {
    if(!getenv("WC3_COUPLE_SEPARATION_TRACE"))return;
    fprintf(stderr,"CSEP %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n",level.pathing_counter,wc3_float_bits(level.pathing_clock.time),unit->class_id,unit->user_data,wc3_float_bits(before->point.x),wc3_float_bits(before->point.y),wc3_float_bits(before->state.vector[0]),wc3_float_bits(before->state.vector[1]),before->state.packed,wc3_float_bits(unit->movement.fine_pose.x),wc3_float_bits(unit->movement.fine_pose.y),wc3_float_bits(unit->movement.repulse.state.vector[0]),wc3_float_bits(unit->movement.repulse.state.vector[1]),unit->movement.repulse.state.packed,before->owner.sum,before->owner.index);
}

static char couple_cantpath_script[16000];
static void CoupleCantPathReplace(cstring_t from,cstring_t to) {
    char *p=couple_cantpath_script;size_t a=strlen(from),b=strlen(to);
    while((p=strstr(p,from))) {
        size_t needed=strlen(couple_cantpath_script)-a+b+1;
        T_ASSERT(needed<=sizeof(couple_cantpath_script));
        if(needed>sizeof(couple_cantpath_script))return;
        memmove(p+b,p+a,strlen(p+a)+1);memcpy(p,to,b);p+=b;
    }
}
static void CoupleCantPathFourCC(char *text,cstring_t key) {
    char *p=strstr(text,key);if(!p)return;
    p+=strlen(key);char *end;uint32_t raw=(uint32_t)strtoul(p,&end,10);
    uint32_t native=((raw&255u)<<24)|((raw&0xff00u)<<8)|((raw>>8)&0xff00u)|(raw>>24);
    char value[32];snprintf(value,sizeof(value),"%u",native);
    size_t n=strlen(value);memmove(p+n,end,strlen(end)+1);memcpy(p,value,n);
}
static uint32_t CoupleCantPathAssert(jass_t *j) {
    cstring_t message=jass_checkstring(j,2);
    if(message && !strncmp(message,"CPF ",4)) {
        char canonical[512];snprintf(canonical,sizeof(canonical),"%s",message);
        /* Only typed unit/ability FourCCs differ in host integer encoding. */
        CoupleCantPathFourCC(canonical," type=");
        CoupleCantPathFourCC(canonical," ability=");
        unsigned tick=0,scene=0;T_EQ(sscanf(canonical,"CPF tick=%u scene=%u",&tick,&scene),2);
        couple_poll_trace.scene=scene;
        if(strstr(canonical,"label=setup ")) {
            memset(couple_poll_trace.roles,0,sizeof(couple_poll_trace.roles));
            FILTER_EDICTS(e,e->inuse && e->user_data>=1 && e->user_data<=3)
                couple_poll_trace.roles[e->user_data-1]=e;
            FOR_LOOP(role,3)T_NOT_NULL(couple_poll_trace.roles[role]);
        }
        cstring_t expected=NULL;
        if(tick==0) {
            unsigned n=sizeof(couple_native_prefix251)/sizeof(couple_native_prefix251[0]);
            T_ASSERT(couple_cantpath.prefix<n);
            if(couple_cantpath.prefix<n)expected=couple_native_prefix251[couple_cantpath.prefix++];
        } else {
            unsigned n=sizeof(couple_native_public251)/sizeof(couple_native_public251[0]);
            T_ASSERT(couple_cantpath.rows<n);
            if(couple_cantpath.rows<n)expected=couple_native_public251[couple_cantpath.rows++];
        }
        if(expected) {
            if(strcmp(canonical,expected))fprintf(stderr,"ACOICANTPATH251 expected:%s actual:%s\n",expected,canonical);
            T_ASSERT(!strcmp(canonical,expected));
        }
        unsigned code=0,actor=0,target=0,ability=0,kind=0;bool event=false;
        char const *label=strstr(canonical,"label=");
        if(label && sscanf(label,"label=issued id=%u actor=%u target=%u",&code,&actor,&target)==3)event=true;
        else if(label && sscanf(label,"label=spell event=%u actor=%u ability=%u",&code,&actor,&ability)==3){event=true;kind=1;}
        if(event) {
            unsigned n=sizeof(couple_native_events251)/sizeof(couple_native_events251[0]);
            T_ASSERT(couple_cantpath.events<n);
            if(couple_cantpath.events<n) {
                coupleNativeEvent251_t const *e=&couple_native_events251[couple_cantpath.events++];
                T_EQ(scene,e->scene);T_EQ(tick,e->tick);T_EQ(kind,e->kind);T_EQ(code,e->code);
                T_EQ(actor,e->actor);T_EQ(target,e->target);T_EQ(ability,e->ability);
                T_EQ(wc3_float_bits(level.pathing_clock.time),e->clock_word);
            }
        }
        if(strstr(canonical,"-return accepted=")) {
            T_ASSERT(strstr(canonical,"accepted=1")!=NULL);couple_cantpath.accepted++;
        }
        if(strstr(canonical,"label=complete "))couple_cantpath.complete=true;
    }
    return couple_cantpath_assert_original(j);
}
static void CoupleCantPathCheckPolls(void) {
    unsigned const arrival[]={2,3,4,5,6,7,8,18,10,11};
    unsigned const committed[]={2,13,14,15,16,7};
    unsigned count=sizeof(couple_native_polls251)/sizeof(couple_native_polls251[0]);
    T_EQ(count,160);T_EQ(couple_poll_trace.count,count);
    FOR_LOOP(i,MIN(count,couple_poll_trace.count)) {
        couplePollRow_t const *actual=&couple_poll_trace.rows[i];
        coupleNativePoll251_t const *expected=&couple_native_polls251[i];
        T_EQ(actual->scene,expected->scene);T_EQ(actual->words[0],expected->phase);
        T_EQ(actual->words[1],expected->role);
        unsigned n=expected->phase==0 ? 10 : 6;
        unsigned const *indices=expected->phase==0 ? arrival : committed;
        T_EQ(expected->count,n);
        FOR_LOOP(j,n)T_EQ(actual->words[indices[j]],expected->words[j]);
    }
}
static void CoupleCantPathRun(bool replay) {
    memset(&couple_cantpath,0,sizeof(couple_cantpath));
    for(jassModule_t *p=jass_funcs;p->name;p++)if(!strcmp(p->name,"BJassAssert")) {
        couple_cantpath_assert_original=p->func;p->func=CoupleCantPathAssert;break;
    }
    coupleInstantFixture_t f;
    CoupleInstantBeginProfile(&f,&(coupleInstantSetup_t){.empty=true,.retail_profile=true,.repulsion_profile=true,.range333=true,.peer_type="earc"});
    /* Native flat17x17 W3E,64x64 WPM: full-height NOFLY+ground wall.
     * The ground Archer and flying Hippo must both meet a true route barrier. */
    memset(f.cells,0x40,sizeof(f.cells));
    for(unsigned y=0;y<64;y++)for(unsigned x=20;x<=23;x++)f.cells[y*64+x]=0xce;
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});
    CM_SetupTestPathmap(64,64,f.cells);G_InitWaypoints();G_FinishMovePathingInitialization();
    float ranges[2]={37,37};unitModification_t mods[2];unitData_t originals[2];
    memset(mods,0,sizeof(mods));memset(originals,0,sizeof(originals));
    for(unsigned i=0;i<2;i++) {
        mods[i]=(unitModification_t){.modID=MAKEFOURCC('a','r','a','n'),.type=mod_unreal,.level=1,.data=&ranges[i]};
        originals[i]=(unitData_t){.originalUnitID=i?MAKEFOURCC('A','c','o','3'):MAKEFOURCC('A','c','o','2'),.numbeOfModifications=1,.modifications=&mods[i]};
    }
    f.info.num_originalAbilities=2;f.info.originalAbilities=originals;G_SetMapAbilityOverrides(&f.info);
    /* Native258 observes map reseeding and twelve startup owner draws before
     * the first scene. Reproduce those inputs, never inject sampled poses. */
    wc3_random_seed(&level.pathing_random,couple_native_owner_seed258);
    FOR_LOOP(startup_draw,couple_native_startup_draws258)wc3_random_next(&level.pathing_random);
    T_EQ(level.pathing_random.sum,couple_native_initial_rng258[0]);T_EQ(level.pathing_random.index,couple_native_initial_rng258[1]);
    level.repulse_phase=couple_native_constructor_phase258;
    T_ASSERT(CoupleInstantCompile(
        "function CoupleCantPathObserve takes string value returns nothing\ncall BJassAssert(true,value)\nendfunction\n"
        "function CoupleCantPathPrimeGroups takes nothing returns nothing\n"
        "local group g=CreateGroup()\ncall DestroyGroup(g)\nset g=null\nendfunction\n"
        "function CpModulo takes integer a,integer b returns integer\nreturn a-(a/b)*b\nendfunction\n"
        "function CpRegister takes playerunitevent e,code f returns nothing\nlocal trigger t=CreateTrigger()\n"
        "call TriggerRegisterPlayerUnitEvent(t,Player(0),e,null)\ncall TriggerAddAction(t,f)\nendfunction\n"));
    snprintf(couple_cantpath_script,sizeof(couple_cantpath_script),"%s",couple_native_probe251);
    CoupleCantPathReplace("ModuloInteger(","CpModulo(");
    CoupleCantPathReplace("function CpSpell takes nothing returns nothing","function CpSpell takes integer observedEvent returns nothing");
    CoupleCantPathReplace("GetHandleId(GetTriggerEventId())","observedEvent");
    CoupleCantPathReplace("function PathProbeInit takes nothing returns nothing",
        "function CpChannel takes nothing returns nothing\ncall CpSpell(272)\nendfunction\n"
        "function CpCast takes nothing returns nothing\ncall CpSpell(273)\nendfunction\n"
        "function CpEffect takes nothing returns nothing\ncall CpSpell(274)\nendfunction\n"
        "function CpFinish takes nothing returns nothing\ncall CpSpell(275)\nendfunction\n"
        "function CpEnd takes nothing returns nothing\ncall CpSpell(276)\nendfunction\n"
        "function PathProbeInit takes nothing returns nothing");
    CoupleCantPathReplace("call TriggerRegisterPlayerUnitEvent(ts,Player(0),EVENT_PLAYER_UNIT_SPELL_CHANNEL,null)","call CpRegister(ConvertPlayerUnitEvent(272),function CpChannel)");
    CoupleCantPathReplace("call TriggerRegisterPlayerUnitEvent(ts,Player(0),EVENT_PLAYER_UNIT_SPELL_CAST,null)","call CpRegister(ConvertPlayerUnitEvent(273),function CpCast)");
    CoupleCantPathReplace("call TriggerRegisterPlayerUnitEvent(ts,Player(0),EVENT_PLAYER_UNIT_SPELL_EFFECT,null)","call CpRegister(ConvertPlayerUnitEvent(274),function CpEffect)");
    CoupleCantPathReplace("call TriggerRegisterPlayerUnitEvent(ts,Player(0),EVENT_PLAYER_UNIT_SPELL_ENDCAST,null)","call CpRegister(ConvertPlayerUnitEvent(276),function CpEnd)");
    CoupleCantPathReplace("call TriggerRegisterPlayerUnitEvent(ts,Player(0),EVENT_PLAYER_UNIT_SPELL_FINISH,null)","call CpRegister(ConvertPlayerUnitEvent(275),function CpFinish)");
    CoupleCantPathReplace("call TriggerAddAction(ts,function CpSpell)","");
    memset(&couple_poll_trace,0,sizeof(couple_poll_trace));
    couple_poll_trace.print=getenv("WC3_COUPLE_POLL_TRACE")!=NULL;
    move_test_group_arrival=CouplePollArrival;move_test_group_committed=CouplePollCommitted;
    move_test_repulse=CoupleCantPathSeparationTrace;
    T_ASSERT(CoupleInstantCompile(couple_cantpath_script));
    /* The native cleanup later allocates a group. Prime that public registry
     * before the early snapshot so path-cleared ReadGame restores the same
     * high-water baseline; no save-format or registry guard is bypassed. */
    CoupleInstantCall("CoupleCantPathPrimeGroups");CoupleInstantCall("PathProbeInit");
    unsigned steps=0;
    if(replay) {
        edict_t *retired=NULL;uint32_t retired_spawn=0;
        vec2_t death_pose={0};bool displaced=false;
        /* Observe after complete frames: no snapshot is made from a native
         * event callback or while the separation owner is visiting a node. */
        while(!displaced && !couple_cantpath.complete && steps++<12500) {
            CoupleInstantStep();
            if(!retired) {
                FILTER_EDICTS(e,e->inuse && e->class_id==MAKEFOURCC('e','h','i','p') &&
                    e->health.value==0 && e->couple_instant && e->couple_instant->retiring &&
                    e->movement.repulse.active && !e->movement.group_id) {
                    retired=e;retired_spawn=e->spawn_time;death_pose=e->s.origin2;break;
                }
            } else if(retired->inuse && retired->spawn_time==retired_spawn) {
                wc3Repulse_t const *state=&retired->movement.repulse.state;
                displaced=(state->vector[0]!=0 || state->vector[1]!=0) &&
                    (retired->s.origin2.x!=death_pose.x || retired->s.origin2.y!=death_pose.y);
            } else {T_ASSERT(false);break;}
        }
        T_ASSERT(displaced);T_NOT_NULL(retired);
        if(displaced && retired) {
            T_EQ(retired->health.value,0);T_ASSERT(retired->movement.repulse.active);
            T_EQ(retired->movement.group_id,0);T_ASSERT(!G_UnitHasWorldPresence(retired));
            wc3Repulse_t saved_repulse=retired->movement.repulse.state;
            vec2_t saved_pose=retired->s.origin2,saved_fine=retired->movement.fine_pose;
            wc3Random_t saved_random=level.pathing_random;uint32_t saved_phase=level.repulse_phase;
            unsigned links[MAX_ENTITIES],link_count=0;
            for(edict_t *e=level.repulse_head;e && link_count<MAX_ENTITIES;e=e->movement.repulse.next)
                links[link_count++]=(unsigned)(e-g_edicts);
            T_ASSERT(link_count<MAX_ENTITIES);
            typeof(couple_cantpath) checkpoint=couple_cantpath;
            typeof(couple_poll_trace) *poll_checkpoint=malloc(sizeof(couple_poll_trace));
            T_NOT_NULL(poll_checkpoint);if(poll_checkpoint)*poll_checkpoint=couple_poll_trace;
            unsigned saved_steps=steps;
            cstring_t path=Test_TempPath("couple-retired-separation231.bin");
            T_ASSERT(WriteGame(path));
            while(!couple_cantpath.complete && steps++<12500)CoupleInstantStep();
            T_ASSERT(couple_cantpath.complete);T_ASSERT(!jass_rterror_pending(level.vm));
            T_EQ(couple_cantpath.rows,478);T_EQ(couple_cantpath.events,38);T_EQ(couple_cantpath.accepted,8);
            unsigned final_steps=steps;CoupleCantPathCheckPolls();
            S_ClearMoveGroups();T_ASSERT(ReadGame(path));
            couple_cantpath=checkpoint;steps=saved_steps;
            if(poll_checkpoint)couple_poll_trace=*poll_checkpoint;
            T_ASSERT(retired->inuse);T_EQ(retired->spawn_time,retired_spawn);
            T_EQ(retired->health.value,0);T_EQ(retired->movement.group_id,0);
            T_ASSERT(retired->couple_instant && retired->couple_instant->retiring);
            T_ASSERT(retired->movement.repulse.active);T_ASSERT(!G_UnitHasWorldPresence(retired));
            T_EQ(wc3_float_bits(retired->s.origin2.x),wc3_float_bits(saved_pose.x));
            T_EQ(wc3_float_bits(retired->s.origin2.y),wc3_float_bits(saved_pose.y));
            T_EQ(wc3_float_bits(retired->movement.fine_pose.x),wc3_float_bits(saved_fine.x));
            T_EQ(wc3_float_bits(retired->movement.fine_pose.y),wc3_float_bits(saved_fine.y));
            T_EQ(wc3_float_bits(retired->movement.repulse.state.vector[0]),wc3_float_bits(saved_repulse.vector[0]));
            T_EQ(wc3_float_bits(retired->movement.repulse.state.vector[1]),wc3_float_bits(saved_repulse.vector[1]));
            T_EQ(retired->movement.repulse.state.packed,saved_repulse.packed);
            T_EQ(level.pathing_random.sum,saved_random.sum);T_EQ(level.pathing_random.index,saved_random.index);
            T_EQ(level.repulse_phase,saved_phase);
            unsigned restored_links=0;
            for(edict_t *e=level.repulse_head;e && restored_links<MAX_ENTITIES;e=e->movement.repulse.next) {
                T_ASSERT(restored_links<link_count);
                if(restored_links<link_count)T_EQ((unsigned)(e-g_edicts),links[restored_links]);
                restored_links++;
            }
            T_EQ(restored_links,link_count);
            while(!couple_cantpath.complete && steps++<12500)CoupleInstantStep();
            T_EQ(steps,final_steps);free(poll_checkpoint);remove(path);
        }
    } else while(!couple_cantpath.complete && steps++<12500)CoupleInstantStep();
    T_ASSERT(couple_cantpath.complete);T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(couple_cantpath.prefix,3);T_EQ(couple_cantpath.rows,478);T_EQ(couple_cantpath.events,38);T_EQ(couple_cantpath.accepted,8);
    CoupleCantPathCheckPolls();
    move_test_group_arrival=NULL;move_test_group_committed=NULL;move_test_repulse=NULL;
    G_SetMapAbilityOverrides(NULL);CoupleInstantEnd(&f);
    for(jassModule_t *p=jass_funcs;p->name;p++)if(!strcmp(p->name,"BJassAssert")){p->func=couple_cantpath_assert_original;break;}
}

TEST(wc3_couple_instant_orders, native_cannot_path_releases_pair_and_allows_fresh_retry) {
    CoupleCantPathRun(false);
}
TEST(wc3_couple_instant_orders, native_retired_separation_saved_suffix) {
    CoupleCantPathRun(true);
}

/* Engine-only reentrancy guards at the native251 terminal-failure frontier.
 * No claim that these extra callback mutations were executed in retail. */
TEST(wc3_couple_instant_orders, cannot_path_inverse_preserves_callback_move_and_removal) {
    FOR_LOOP(removing,2) {
        coupleInstantFixture_t f;
        CoupleInstantBeginProfile(&f,&(coupleInstantSetup_t){.empty=true,.retail_profile=true,.repulsion_profile=true,.range333=true,.peer_type="earc"});
        memset(f.cells,0x40,sizeof(f.cells));
        for(unsigned y=0;y<64;y++)for(unsigned x=20;x<=23;x++)f.cells[y*64+x]=0xce;
        CM_SetupTestPathmap(64,64,f.cells);G_InitWaypoints();G_FinishMovePathingInitialization();
        float ranges[2]={37,37};unitModification_t mods[2];unitData_t originals[2];
        memset(mods,0,sizeof(mods));memset(originals,0,sizeof(originals));
        FOR_LOOP(i,2) {
            mods[i]=(unitModification_t){.modID=MAKEFOURCC('a','r','a','n'),.type=mod_unreal,.level=1,.data=&ranges[i]};
            originals[i]=(unitData_t){.originalUnitID=i?MAKEFOURCC('A','c','o','3'):MAKEFOURCC('A','c','o','2'),.numbeOfModifications=1,.modifications=&mods[i]};
        }
        f.info.num_originalAbilities=2;f.info.originalAbilities=originals;G_SetMapAbilityOverrides(&f.info);
        char script[5000];snprintf(script,sizeof(script),
            "globals\nunit cgA=null\nunit cgH=null\nboolean cgFired=false\ninteger cgMoves=0\nendglobals\n"
            "function CgEvent takes nothing returns nothing\n"
            "if GetTriggerUnit()==cgA and GetIssuedOrderId()==851986 then\nset cgMoves=cgMoves+1\nendif\n"
            "if GetTriggerUnit()==cgH and GetIssuedOrderId()==851972 and not cgFired then\n"
            "set cgFired=true\ncall BJassAssert(GetUnitCurrentOrder(cgA)==852508,\"failed owner head survives peer inverse\")\n"
            "%s\nendif\nendfunction\n"
            "function CgInit takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
            "set cgA=CreateUnit(Player(0),'earc',512.0,512.0,0.0)\nset cgH=CreateUnit(Player(0),'ehip',1024.0,512.0,180.0)\n"
            "call SetUnitUserData(cgA,41)\ncall SetUnitUserData(cgH,42)\n"
            "call SetUnitAcquireRange(cgA,0.0)\ncall SetUnitAcquireRange(cgH,0.0)\n"
            "call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)\n"
            "call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)\n"
            "call TriggerAddAction(t,function CgEvent)\n"
            "call BJassAssert(IssueImmediateOrder(cgA,\"coupleinstant\"),\"wall pair admitted\")\nendfunction\n"
            "function CgCheck takes nothing returns nothing\ncall BJassAssert(cgFired,\"actual wall failure invoked peer Stop\")\n"
            "call BJassAssert(cgMoves==%u,\"callback Move is not republished\")\n"
            "%s\nendfunction\n",
            removing ? "call RemoveUnit(cgA)" : "call BJassAssert(IssuePointOrder(cgA,\"move\",256.0,512.0),\"inverse callback Move admitted\")",
            removing ? 0u : 1u,
            removing ? "call BJassAssert(GetUnitTypeId(cgA)==0,\"removed actor eventually released\")" :
                "call BJassAssert(GetUnitX(cgA)<384.0,\"callback successor physically continues\")");
        T_ASSERT(CoupleInstantCompile(script));CoupleInstantCall("CgInit");
        edict_t *source=NULL,*peer=NULL;
        FILTER_EDICTS(e,e->inuse && e->user_data==41)source=e;
        FILTER_EDICTS(e,e->inuse && e->user_data==42)peer=e;
        T_NOT_NULL(source);T_NOT_NULL(peer);
        uint32_t spawn=source->spawn_time;unsigned steps=0;
        while(source->inuse && source->spawn_time==spawn && source->couple_instant && source->couple_instant->active && steps++<600)CoupleInstantStep();
        T_ASSERT(steps<600);T_ASSERT(!peer->couple_instant || !peer->couple_instant->active);
        if(!removing) {
            T_EQ(source->current_order_id,G_OrderId("move"));
            T_NOT_NULL(source->goalentity);
            if(source->goalentity)T_EQ(wc3_float_bits(source->goalentity->s.origin2.x),wc3_float_bits(256.0f));
            cstring_t path=Test_TempPath("cantpath-callback-move231.bin");
            T_ASSERT(WriteGame(path));
            FOR_LOOP(frame,400) {CoupleInstantStep();}
            CoupleInstantCall("CgCheck");vec2_t final=source->s.origin2;
            S_ClearMoveGroups();T_ASSERT(ReadGame(path));
            FOR_LOOP(frame,400) {CoupleInstantStep();}
            CoupleInstantCall("CgCheck");
            T_EQ(wc3_float_bits(source->s.origin2.x),wc3_float_bits(final.x));
            T_EQ(wc3_float_bits(source->s.origin2.y),wc3_float_bits(final.y));remove(path);
        } else {
            FOR_LOOP(frame,400) {CoupleInstantStep();}
            CoupleInstantCall("CgCheck");
        }
        T_ASSERT(!jass_rterror_pending(level.vm));
        G_SetMapAbilityOverrides(NULL);CoupleInstantEnd(&f);
    }
}

#endif
