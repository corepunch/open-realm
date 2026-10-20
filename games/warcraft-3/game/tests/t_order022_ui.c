#ifdef BZ_TESTS
#include "shared/test.h"
#include "../skills/s_skills.h"

static edict_t *review_order_unit_at(float, float, uint32_t);

/* These headless command arenas have no server transport; simulation still
 * executes the production command/presentation producers. Packet assertions
 * live in the cancellation arena's serialization observer. */
static void order022_ui_discard_write(pfWriteType_t type,void const *data) { (void)type; (void)data; }
static void order022_ui_discard_unicast(edict_t *client) { (void)client; }

/* Advance through the real scheduler, including natural spell completion. */
static void order022_ui_frame(void) {
    level.time += FRAMETIME;
    globals.RunFrame();
}

/* Real UI point/button producers, contrasted with public JASS replacement.
 * Stock-shaped AQ22 matches the native arena; the shorter authored timing
 * guards against treating the captured six/eight seconds as engine constants. */
static void order022_ui_matrix(unsigned shape) {
    void (*write)(pfWriteType_t,void const *)=gi.Write;
    void (*unicast)(edict_t *)=gi.unicast;
    gi.Write=order022_ui_discard_write;
    gi.unicast=order022_ui_discard_unicast;
    for(unsigned fast=0;fast<2;fast++)for(unsigned saved=0;saved<2;saved++)for(unsigned scene=0;scene<(shape ? 9 : 12);scene++) {
        reset_entities();setup_test_world();
        uint8_t cells[128*128]={0};CM_SetupTestPathmap(128,128,cells);
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{4096,4096}});
        char data[2048];
        snprintf(data,sizeof(data),
            "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\n"
            "C;X3;K\"DataA1\"\nC;X4;K\"DataB1\"\nC;X5;K\"DataC1\"\n"
            "C;X6;K\"DataE1\"\nC;X7;K\"DataF1\"\nC;X8;K\"Cost1\"\n"
            "C;X9;K\"Cool1\"\nC;X10;K\"Cast1\"\n"
            "C;X1;Y2;K\"AQ22\"\nC;X2;K\"ANcl\"\nC;X3;K%g\n"
            "C;X4;K0\nC;X5;K1\nC;X6;K0\nC;X7;K\"channel\"\n"
            "C;X8;K0\nC;X9;K0\nC;X10;K%g\nE\n",fast ? .8 : 8.,fast ? .6 : 6.);
        slkTestData_t *rows=parse_slk_string(data),*old=G_SetSLKRows("AbilityData",rows);
        edict_t *clent=g_edicts;
        clent->inuse=true;clent->client=game.clients;clent->client->connected=true;clent->client->ps.number=0;
        edict_t *caster=review_order_unit_at(512,600,0),*peer=review_order_unit_at(512,420,0);
        caster->user_data=1;peer->user_data=2;
        edict_t *target=(shape==2 || shape==3) ? review_order_unit_at(1777,512,1) : NULL;
        char target_number[16];snprintf(target_number,sizeof(target_number),"%u",target ? target->s.number : 0);
        caster->think=peer->think=monster_think;
        UnitWeapons_t weapons=*caster->data.UnitWeapons;weapons.castPoint=.3f;
        caster->data.UnitWeapons=peer->data.UnitWeapons=&weapons;
        T_ASSERT(G_ActorAddSkill(caster,MAKEFOURCC('A','Q','2','2')));
        if(scene==7)T_ASSERT(G_ActorAddSkill(peer,MAKEFOURCC('A','Q','2','2')));
        bool mixed=scene==3 || scene==5 || scene==6 || scene==7 || scene==10;
        if(scene==10)peer->aiflags|=AI_IMMOBILE;
        G_SetEntitySelectionMask(caster,1);G_SetEntitySelectionMask(peer,mixed ? 1 : 0);
        T_ASSERT(run_test_jass(
            "globals\nunit caster=null\ninteger points=0\ninteger ends=0\ninteger finishes=0\nendglobals\n"
            "function point takes nothing returns nothing\n"
            "if GetUnitUserData(GetTriggerUnit())==1 then\nset points=points+1\nendif\nendfunction\n"
            "function ended takes nothing returns nothing\n"
            "if GetUnitUserData(GetTriggerUnit())==1 then\nset ends=ends+1\nendif\nendfunction\n"
            "function finished takes nothing returns nothing\n"
            "if GetUnitUserData(GetTriggerUnit())==1 then\nset finishes=finishes+1\nendif\nendfunction\n"
            "function waiting takes nothing returns nothing\n"
            "call BJassAssert(points==0 and ends==0 and finishes==0,\"retained spell publishes no premature point/end\")\nendfunction\n"
            "function complete takes nothing returns nothing\n"
            "call BJassAssert(points==1 and ends==1 and finishes==1,\"natural finish dispatches pending Move once\")\nendfunction\n"
            "function scriptMove takes nothing returns nothing\ncall IssuePointOrder(caster,\"move\",1277,512)\nendfunction\n"
            "function main takes nothing returns nothing\nlocal trigger t\nlocal group g=CreateGroup()\nlocal unit u\n"
            "call GroupEnumUnitsOfPlayer(g,Player(0),null)\nloop\nset u=FirstOfGroup(g)\nexitwhen u==null\n"
            "if GetUnitUserData(u)==1 then\nset caster=u\nendif\ncall GroupRemoveUnit(g,u)\nendloop\ncall DestroyGroup(g)\n"
            "set t=CreateTrigger()\ncall TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)\n"
            "call TriggerAddAction(t,function point)\n"
            "set t=CreateTrigger()\ncall TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)\n"
            "call TriggerAddAction(t,function point)\n"
            "set t=CreateTrigger()\ncall TriggerRegisterPlayerUnitEvent(t,Player(0),ConvertPlayerUnitEvent(276),null)\n"
            "call TriggerAddAction(t,function ended)\n"
            "set t=CreateTrigger()\ncall TriggerRegisterPlayerUnitEvent(t,Player(0),ConvertPlayerUnitEvent(275),null)\n"
            "call TriggerAddAction(t,function finished)\nendfunction\n"));
        level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
        level.time=level.pathing_msec=1000;level.started=level.scriptsStarted=true;
        T_ASSERT(unit_issueimmediateorder(caster,"channel"));
        if(scene==7)T_ASSERT(unit_issueimmediateorder(peer,"channel"));
        T_EQ(caster->current_order_id,852600);T_ASSERT(!S_SpellIsChanneling(caster));
        if(target) {T_ASSERT(S_AttackCanTarget(caster,target));T_ASSERT(S_AttackCanTarget(peer,target));}
        bool working=(scene>=4 && scene<=7) || scene==10;
        if(working) {
            for(unsigned frame=0;frame<100 && !S_SpellIsChanneling(caster);frame++)order022_ui_frame();
            T_ASSERT(S_SpellIsChanneling(caster));
        }
        T_EQ(S_PlayerOrderCastProtected(caster),!working);
        T_EQ(S_PlayerOrderWorking(caster),working);
        T_EQ(G_PlayerSelectionAllWorking(clent->client),working && (scene==4 || scene==7));
        if(scene==9) {
            T_ASSERT(G_IssueUnitPointOrder(caster,"move",&(vec2_t){1100,512},true,0,0));
            T_ASSERT(G_IssueUnitPointOrder(caster,"move",&(vec2_t){1200,512},true,0,0));
            T_EQ(caster->order_queue.count,2);
        }
        if(shape==1 || shape==2) {
            cstring_t button[]={"button","CmdAttack"};
            G_ClientCommand(clent,2,button);
            if(shape==2)T_NOT_NULL(clent->client->menu.on_entity_selected);
            else T_NOT_NULL(clent->client->menu.on_location_selected);
        } else if(!shape)clent->client->menu.on_location_selected=move_selectlocation;
        else Get_Commands_f(clent);
        clent->client->menu.supports_order_queue=true;
        cstring_t move[]={"point","1277","512","queue"};
        cstring_t stop[]={"button","CmdStop"},hold[]={"button","CmdHoldPos"};
        if(scene==1)G_ClientCommand(clent,2,stop);
        else if(scene==6)G_ClientCommand(clent,2,hold);
        else if(scene==8)jass_callbyname(level.vm,"scriptMove",false);
        else if(shape==2 || shape==3) {
            cstring_t select[]={shape==2 ? "select" : "smart",target_number,"queue"};
            G_ClientCommand(clent,scene==2 ? 3 : 2,select);
        } else if(shape==4) {
            cstring_t smart[]={"smartpoint","1277","512","queue"};
            G_ClientCommand(clent,scene==2 ? 4 : 3,smart);
        } else G_ClientCommand(clent,scene==2 ? 4 : 3,move);
        if(scene==11) {
            /* A completed point click exits target mode. Re-enter Move through
             * its command button before submitting another targeted point. */
            cstring_t button[]={"button","CmdMove"};
            G_ClientCommand(clent,2,button);
            T_EQ(clent->client->menu.on_location_selected,move_selectlocation);
            cstring_t replacement[]={"point","1377","512"};
            G_ClientCommand(clent,3,replacement);
            T_EQ(caster->order_queue.entries[caster->order_queue.head].point.x,1377);
        }
        bool retained=scene==0 || scene==2 || scene==3 || scene==5 || scene>=9;
        uint32_t issued_head=shape==3 && scene!=8 ? 851971 : shape>=1 && shape<=2 && scene!=8 ? 851983 : 851986;
        T_EQ(caster->current_order_id,retained ? 852600 : (scene==1 || scene==6) ? 0 : issued_head);
        T_EQ(caster->order_queue.count,retained ? 1 : 0);
        if(retained) {
            T_EQ(S_SpellIsChanneling(caster),working);
            jass_callbyname(level.vm,"waiting",false);T_ASSERT(!jass_rterror_pending(level.vm));
        } else T_ASSERT(!S_SpellIsChanneling(caster));
        if(mixed && scene!=6 && scene!=10)T_EQ(peer->current_order_id,issued_head);
        if(scene==10)T_EQ(peer->current_order_id,0);
        if(scene==6) {T_ASSERT(caster->movement.holding_position);T_ASSERT(peer->movement.holding_position);}
        if(saved) {
            unsigned caster_slot=caster->s.number,peer_slot=peer->s.number;
            cstring_t file=Test_TempPath("order022-ui.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            caster=g_edicts+caster_slot;peer=g_edicts+peer_slot;
            caster->data.UnitWeapons=peer->data.UnitWeapons=&weapons;
            T_EQ(caster->order_queue.count,retained ? 1 : 0);
            T_EQ(caster->current_order_id,retained ? 852600 : (scene==1 || scene==6) ? 0 : issued_head);
            if(retained) {T_EQ(S_PlayerOrderCastProtected(caster),!working);T_EQ(S_PlayerOrderWorking(caster),working);}
        }
        if(retained) {
            for(unsigned frame=0;frame<180 && caster->current_order_id==852600;frame++)order022_ui_frame();
            T_ASSERT(caster->current_order_id!=852600);T_EQ(caster->order_queue.count,0);
            jass_callbyname(level.vm,"complete",false);T_ASSERT(!jass_rterror_pending(level.vm));
        }
        reset_entities();setup_test_world();G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    }
    gi.Write=write;
    gi.unicast=unicast;
}
TEST(wc3_order022_ui, selected_move_preserves_windup_and_mixed_channel_work) {order022_ui_matrix(0);}
TEST(wc3_order022_ui, selected_attack_point_preserves_windup_and_mixed_channel_work) {order022_ui_matrix(1);}
TEST(wc3_order022_ui, selected_attack_target_preserves_windup_and_mixed_channel_work) {order022_ui_matrix(2);}
TEST(wc3_order022_ui, selected_smart_target_preserves_windup_and_mixed_channel_work) {order022_ui_matrix(3);}
TEST(wc3_order022_ui, selected_smart_point_preserves_windup_and_mixed_channel_work) {order022_ui_matrix(4);}
/* Native Smart head capture distinguishes public851971 from the physical
 * Attack owner, including replacement of an ordered spell before EFFECT. */
TEST(wc3_order022_ui, smart_enemy_public_head_owns_notification_and_save) {
    void (*write)(pfWriteType_t,void const *)=gi.Write;
    void (*unicast)(edict_t *)=gi.unicast;
    gi.Write=order022_ui_discard_write;
    gi.unicast=order022_ui_discard_unicast;
    FOR_LOOP(cast,2) {
        reset_entities();setup_test_world();
        uint8_t cells[128*128]={0};CM_SetupTestPathmap(128,128,cells);
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{4096,4096}});
        slkTestData_t *rows=parse_slk_string(
            "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"DataA1\"\n"
            "C;X4;K\"DataB1\"\nC;X5;K\"DataC1\"\nC;X6;K\"DataE1\"\nC;X7;K\"DataF1\"\n"
            "C;X8;K\"Cast1\"\nC;X1;Y2;K\"AQ22\"\nC;X2;K\"ANcl\"\nC;X3;K8\nC;X4;K0\n"
            "C;X5;K1\nC;X6;K0\nC;X7;K\"channel\"\nC;X8;K6\nE\n");
        slkTestData_t *old=G_SetSLKRows("AbilityData",rows);
        edict_t *unit=review_order_unit_at(512,600,0),*enemy=review_order_unit_at(1777,512,1);
        unit->user_data=1;enemy->user_data=2;unit->think=monster_think;
        UnitWeapons_t weapons=*unit->data.UnitWeapons;weapons.castPoint=.3f;
        unit->data.UnitWeapons=&weapons;
        T_ASSERT(G_ActorAddSkill(unit,MAKEFOURCC('A','Q','2','2')));
        T_ASSERT(S_AttackCanTarget(unit,enemy));
        T_ASSERT(run_test_jass(
            "globals\nunit subject=null\nunit target=null\ninteger issued=0\nendglobals\n"
            "function notified takes nothing returns nothing\n"
            "if GetUnitUserData(GetTriggerUnit())==1 then\nset issued=issued+1\n"
            "call BJassAssert(GetIssuedOrderId()==851971 and GetUnitCurrentOrder(GetTriggerUnit())==851971,\"Smart head precedes its target notification\")\n"
            "call BJassAssert(GetOrderTargetUnit()==target,\"Smart retains the original target\")\nendif\nendfunction\n"
            "function issue takes nothing returns nothing\ncall BJassAssert(IssueTargetOrder(subject,\"smart\",target),\"Smart accepted\")\nendfunction\n"
            "function verify takes nothing returns nothing\ncall BJassAssert(issued==1,\"one Smart target event\")\nendfunction\n"
            "function main takes nothing returns nothing\nlocal group g=CreateGroup()\nlocal unit u\nlocal trigger t=CreateTrigger()\n"
            "call GroupEnumUnitsOfPlayer(g,Player(0),null)\nloop\nset u=FirstOfGroup(g)\nexitwhen u==null\n"
            "if GetUnitUserData(u)==1 then\nset subject=u\nendif\ncall GroupRemoveUnit(g,u)\nendloop\n"
            "call GroupEnumUnitsOfPlayer(g,Player(1),null)\nset target=FirstOfGroup(g)\ncall DestroyGroup(g)\n"
            "call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)\n"
            "call TriggerAddAction(t,function notified)\nendfunction\n"));
        level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
        level.time=level.pathing_msec=1000;level.started=level.scriptsStarted=true;
        if(cast) {T_ASSERT(unit_issueimmediateorder(unit,"channel"));T_EQ(unit->current_order_id,852600);}
        jass_callbyname(level.vm,"issue",false);T_ASSERT(!jass_rterror_pending(level.vm));
        T_EQ(unit->current_order_id,851971);T_EQ(unit->currentmove->proc,CAbilityAttack);
        T_ASSERT(!S_PlayerOrderCastProtected(unit));T_EQ(unit->order_queue.count,0);
        jass_callbyname(level.vm,"verify",false);T_ASSERT(!jass_rterror_pending(level.vm));
        unsigned slot=unit->s.number;
        cstring_t file=Test_TempPath("order022-smart-head.bin");
        T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
        unit=g_edicts+slot;unit->data.UnitWeapons=&weapons;
        T_EQ(unit->current_order_id,851971);T_EQ(unit->currentmove->proc,CAbilityAttack);
        order022_ui_frame();T_EQ(unit->current_order_id,851971);
        reset_entities();setup_test_world();G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    }
    gi.Write=write;
    gi.unicast=unicast;
}
#endif
