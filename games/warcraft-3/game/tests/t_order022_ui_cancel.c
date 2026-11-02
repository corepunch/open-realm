#ifdef BZ_TESTS
#include "shared/test.h"
#include "../skills/s_skills.h"

void reset_entities(void);
void setup_test_world(void);
void CM_SetupTestPathmap(uint32_t, uint32_t, uint8_t const *);
void CM_SetupTestWorldBounds(box2_t const *);
bool run_test_jass(cstring_t);

static abilityProc_t order022_cancel_proc;
static edict_t *order022_cancel_target,*order022_cancel_replacement;
static unsigned order022_cancel_mode;
static bool order022_cancel_seen;
static void (*order022_indicator_write)(pfWriteType_t,void const *);
static void (*order022_indicator_unicast)(edict_t *);
static unsigned order022_indicator_stage,order022_indicator_count;

/* Inspect production serialization without requiring an initialized server transport. */
static void order022_capture_indicator_write(pfWriteType_t type,void const *data) {
    if(!order022_indicator_stage)
        order022_indicator_stage=type==PF_BYTE && *(int32_t const *)data==svc_temp_entity ? 1 : 2;
    else if(order022_indicator_stage==1) {
        if(type==PF_BYTE && *(int32_t const *)data==TE_ENTITY_INDICATOR)order022_indicator_count++;
        order022_indicator_stage=2;
    }
}

/* Each unicast starts a new message, including unrelated command-panel packets. */
static void order022_capture_indicator_unicast(edict_t *client) {
    (void)client;
    order022_indicator_stage=0;
}

/* Real successor cancellation can retire/reuse either identity before the
 * new target packet is inserted. Allocation observes the real cooldown. */
static intptr_t order022_cancel_reuses_identity(edict_t *unit,abilityMsg_t msg,abilityCall_t const *call) {
    intptr_t result=order022_cancel_proc(unit,msg,call);
    if(msg==A_QUEUE_ORDER_CANCEL && !order022_cancel_seen) {
        order022_cancel_seen=true;
        edict_t *victim=order022_cancel_mode==1 ? unit : order022_cancel_target;
        uint32_t slot=victim->s.number,spawn=victim->spawn_time;
        G_FreeEdict(victim);
        if(order022_cancel_mode!=2) {
            level.time+=1001;
            order022_cancel_replacement=SP_SpawnAtLocation(MAKEFOURCC('h','f','o','o'),0,&(vec2_t){900,900});
            T_NOT_NULL(order022_cancel_replacement);
            if(order022_cancel_replacement) {
                T_EQ(order022_cancel_replacement->s.number,slot);
                T_NE(order022_cancel_replacement->spawn_time,spawn);
                order022_cancel_replacement->targtype=TARG_GROUND;
                if(order022_cancel_mode==1)
                    T_ASSERT(G_QueueUnitOrder(order022_cancel_replacement,"move",UNIT_ORDER_TARGET_POINT,&(vec2_t){777,777},NULL,0,0,0));
            }
        }
    }
    return result;
}

/* The packet owns its old target identity before cancelling successors;
 * after a subject reuse, the old admission must not append to its successor. */
TEST(wc3_order022_ui, fallback_cancel_preserves_target_identity_and_rejects_subject_reuse) {
    order022_indicator_write=gi.Write;order022_indicator_unicast=gi.unicast;
    gi.Write=order022_capture_indicator_write;gi.unicast=order022_capture_indicator_unicast;
    FOR_LOOP(input,5)FOR_LOOP(saved,2)FOR_LOOP(mode,3) {
        bool const point=input==1 || input==2,smart=input>=3;
        if(point && mode!=1)continue;
        reset_entities();setup_test_world();
        uint8_t cells[128*128]={0};CM_SetupTestPathmap(128,128,cells);
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{4096,4096}});
        level.time=level.pathing_msec=1000;level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
        /* Map startup reserves the contiguous waypoint ring before actors. */
        G_InitWaypoints();
        FOR_LOOP(i,2) {
            ((mapInfo_t *)level.mapinfo)->players[i].playerType=kPlayerTypeHuman;
            game.clients[i].ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED]=0;
            game.clients[i].ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP]=100;
        }
        edict_t *clent=g_edicts;
        clent->inuse=true;clent->client=game.clients;clent->client->connected=true;clent->client->ps.number=0;
        edict_t *unit=SP_SpawnAtLocation(MAKEFOURCC('h','f','o','o'),0,&(vec2_t){512,600});
        edict_t *target=SP_SpawnAtLocation(MAKEFOURCC('h','f','o','o'),1,&(vec2_t){1777,512});
        edict_t *peer=input==2 || input==4 ? SP_SpawnAtLocation(MAKEFOURCC('h','f','o','o'),0,&(vec2_t){512,420}) : NULL;
        T_NOT_NULL(unit);T_NOT_NULL(target);
        if(!unit || !target)goto restore_network;
        /* Minimal UnitData fixture omits targetType; this arena uses ground Footmen. */
        unit->targtype=target->targtype=TARG_GROUND;
        UnitWeapons_t weapons={.attacksEnabled=3,.castPoint=.3f};unit->data.UnitWeapons=&weapons;
        S_AttackProfileWrite(unit,0)->type=ATK_NORMAL;
        S_AttackProfileWrite(unit,0)->range=30;
        S_AttackProfileWrite(unit,0)->targetsAllowed=WC3_TARGET_FLAG_GROUND;
        unit->runtime.acquisition_range=0;
        target->runtime.acquisition_range=0;
        if(peer) {
            peer->targtype=TARG_GROUND;peer->runtime.acquisition_range=0;peer->think=monster_think;
            T_ASSERT(peer->data.UnitBalance->speed>0);T_ASSERT(peer->unitinfo.MoveSpeed>0);
            peer->data.UnitWeapons=&weapons;
            *S_AttackProfileWrite(peer,0)=*S_AttackProfileRead(unit,0);
            T_ASSERT(S_AttackCanTarget(peer,target));
        }
        slkTestData_t *rows=parse_slk_string(
            "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"DataA1\"\n"
            "C;X4;K\"DataB1\"\nC;X5;K\"DataC1\"\nC;X6;K\"DataE1\"\nC;X7;K\"DataF1\"\n"
            "C;X8;K\"Cast1\"\nC;X1;Y2;K\"AQ22\"\nC;X2;K\"ANcl\"\nC;X3;K0.8\nC;X4;K0\n"
            "C;X5;K1\nC;X6;K0\nC;X7;K\"channel\"\nC;X8;K0.6\nE\n");
        slkTestData_t *old=G_SetSLKRows("AbilityData",rows);
        T_ASSERT(G_ActorAddSkill(unit,MAKEFOURCC('A','Q','2','2')));
        T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
        level.started=level.scriptsStarted=true;
        T_ASSERT(unit_issueimmediateorder(unit,"channel"));T_EQ(unit->current_order_id,852600);
        T_ASSERT(S_PlayerOrderCastProtected(unit));T_ASSERT(S_AttackCanTarget(unit,target));
        T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){1200,512},true,0,0));
        uint32_t uslot=unit->s.number,tslot=target->s.number,tspawn=target->spawn_time;
        uint32_t pslot=peer ? peer->s.number : 0;
        G_SetEntitySelectionMask(unit,1);
        if(peer)G_SetEntitySelectionMask(peer,1);
        if(!smart) {
            cstring_t button[]={"button",point ? "CmdMove" : "CmdAttack"};G_ClientCommand(clent,2,button);
            if(point)T_EQ(clent->client->menu.on_location_selected,move_selectlocation);
            else T_EQ(clent->client->menu.on_entity_selected,attack_menu_selecttarget);
        }
        ability_t const *move=FindAbilityByClassname("Amov");T_NOT_NULL(move);
        if(!move)goto restore_network;
        order022_cancel_proc=move->proc;order022_cancel_target=target;
        order022_cancel_mode=mode;order022_cancel_seen=false;order022_cancel_replacement=NULL;
        S_ReplaceAbilityProcedure(move,order022_cancel_reuses_identity);
        char number[16];snprintf(number,sizeof(number),"%u",tslot);
        cstring_t select[]={smart ? "smart" : "select",number},location[]={"point","1600","512"};
        order022_indicator_count=order022_indicator_stage=0;
        G_SendWidgetIndicator(target,(color32_t){255,255,255,255},&clent->client->ps);
        T_EQ(order022_indicator_count,1);
        order022_indicator_count=0;
        if(point)G_ClientCommand(clent,3,location);
        else G_ClientCommand(clent,2,select);
        S_ReplaceAbilityProcedure(move,order022_cancel_proc);
        T_ASSERT(order022_cancel_seen);
        if(smart)T_EQ(order022_indicator_count,mode==1 && peer ? 1 : 0);
        unit=g_edicts+uslot;
        if(mode==1) {
            T_EQ(unit,order022_cancel_replacement);T_EQ(unit->current_order_id,0);T_EQ(unit->order_queue.count,1);
            if(unit->order_queue.count) {
                unitOrder_t const *packet=unit->order_queue.entries+unit->order_queue.head;
                T_STREQ(packet->order,"move");T_FEQ(packet->point.x,777,0);
            }
        } else {
            T_EQ(unit->current_order_id,852600);T_EQ(unit->order_queue.count,1);
            if(unit->order_queue.count) {
                unitOrder_t const *packet=unit->order_queue.entries+unit->order_queue.head;
                T_EQ(packet->target_type,UNIT_ORDER_TARGET_ENTITY);T_EQ(packet->target_number,tslot);
                T_EQ(packet->target_spawn_time,tspawn);T_FEQ(packet->point.x,1777,0);T_FEQ(packet->point.y,512,0);
            }
        }
        if(peer) {
            if(point) {
                T_EQ(peer->current_order_id,851986);
                moveGroup_t *group=move_unit_group(peer);T_NOT_NULL(group);
                if(group) {
                    T_EQ(group->count,1);T_EQ(group->members[0].unit,peer);
                    T_EQ(group->request_id,peer->movement.previous_request_id);
                }
            } else {
                T_EQ(peer->current_order_id,851971);
                if(mode!=1) {
                    T_NULL(peer->attack_target);T_NULL(peer->movement.follow_target);
                    T_NOT_NULL(peer->goalentity);
                    if(peer->goalentity)T_FEQ(peer->goalentity->s.origin2.x,1777,0);
                }
            }
        }
        if(saved) {
            cstring_t file=Test_TempPath("order022-fallback-cancel.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            unit=g_edicts+uslot;unit->data.UnitWeapons=&weapons;
            if(pslot)peer=g_edicts+pslot;
            T_EQ(unit->order_queue.count,1);
            if(peer) {
                if(point)T_NOT_NULL(move_unit_group(peer));
                T_EQ(peer->current_order_id,point ? 851986 : 851971);
            }
            if(mode!=1 && unit->order_queue.count)T_EQ(unit->order_queue.entries[unit->order_queue.head].target_spawn_time,tspawn);
        }
        if(peer && point) {
            for(unsigned frame=0;frame<300 && peer->current_order_id;frame++) {
                level.time+=FRAMETIME;globals.RunFrame();
            }
            T_EQ(peer->current_order_id,0);T_NULL(move_unit_group(peer));
            T_ASSERT(peer->s.origin2.x>1400);
        }
        if(mode!=1) {
            for(unsigned frame=0;frame<50 && unit->current_order_id==852600;frame++) {
                level.time+=FRAMETIME;globals.RunFrame();
            }
            T_EQ(unit->current_order_id,smart ? 851971 : 851983);T_EQ(unit->order_queue.count,0);
            T_NULL(unit->attack_target);
            edict_t *waypoint=smart ? unit->goalentity : unit->movement.attackmove_waypoint;
            T_NOT_NULL(waypoint);
            if(waypoint)T_FEQ(waypoint->s.origin2.x,1777,0);
        }
        reset_entities();setup_test_world();G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    }
restore_network:
    gi.Write=order022_indicator_write;gi.unicast=order022_indicator_unicast;
}
/* Native selected-target captures retain the common target for both target
 * notifications, even when the first callback removes it and creates another. */
TEST(wc3_order022_ui, selected_target_notifications_retain_original_target_after_callback_remove) {
    order022_indicator_write=gi.Write;order022_indicator_unicast=gi.unicast;
    gi.Write=order022_capture_indicator_write;gi.unicast=order022_capture_indicator_unicast;
    FOR_LOOP(attack,2) {
        reset_entities();setup_test_world();
        uint8_t cells[128*128]={0};CM_SetupTestPathmap(128,128,cells);
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{4096,4096}});
        level.time=level.pathing_msec=1000;level.pathing_clock=(wc3Clock_t){.time=1,.span=300};G_InitWaypoints();
        slkTestData_t *rows=parse_slk_string(
            "ID;PWXL;N;E\nC;X1;Y1;K\"unitID\"\nC;X2;K\"targType\"\nC;X3;K\"movetp\"\n"
            "C;X1;Y2;K\"hfoo\"\nC;X2;K\"ground\"\nC;X3;K\"foot\"\nE\n");
        slkTestData_t *old=G_SetSLKRows("UnitData",rows);
        FOR_LOOP(i,2)((mapInfo_t *)level.mapinfo)->players[i].playerType=kPlayerTypeHuman;
        edict_t *clent=g_edicts;
        clent->inuse=true;clent->client=game.clients;clent->client->connected=true;clent->client->ps.number=0;
        edict_t *first=SP_SpawnAtLocation(MAKEFOURCC('h','f','o','o'),0,&(vec2_t){512,600});
        edict_t *peer=SP_SpawnAtLocation(MAKEFOURCC('h','f','o','o'),0,&(vec2_t){512,420});
        edict_t *target=SP_SpawnAtLocation(MAKEFOURCC('h','f','o','o'),1,&(vec2_t){1277.12f,511.37f});
        T_NOT_NULL(first);T_NOT_NULL(peer);T_NOT_NULL(target);
        if(!first || !peer || !target)goto restore_network;
        first->user_data=1;peer->user_data=3;target->user_data=2;
        UnitWeapons_t weapons={.attacksEnabled=3};first->data.UnitWeapons=peer->data.UnitWeapons=&weapons;
        edict_t *units[]={first,peer};
        FOR_LOOP(i,2) {
            S_AttackProfileWrite(units[i],0)->type=ATK_NORMAL;
            S_AttackProfileWrite(units[i],0)->range=30;
            S_AttackProfileWrite(units[i],0)->targetsAllowed=WC3_TARGET_FLAG_GROUND;
            units[i]->runtime.acquisition_range=0;
            T_ASSERT(S_AttackCanTarget(units[i],target));G_SetEntitySelectionMask(units[i],1);
        }
        T_ASSERT(run_test_jass(
            "globals\nunit first=null\nunit target=null\nunit replacement=null\ninteger targets=0\ninteger points=0\ninteger changed=0\nendglobals\n"
            "function notified takes nothing returns nothing\nlocal integer who=GetUnitUserData(GetTriggerUnit())\n"
            "if who==1 or who==3 then\nset targets=targets+1\n"
            "call BJassAssert(GetUnitUserData(GetOrderTargetUnit())==2,\"selected packet retains the old target\")\n"
            "if who==1 then\ncall BJassAssert(targets==1,\"first receiver precedes peer\")\n"
            "call RemoveUnit(target)\nset replacement=CreateUnit(Player(1),'hfoo',900,900,0)\n"
            "call SetUnitUserData(replacement,4)\nset changed=changed+1\n"
            "else\ncall BJassAssert(changed==1,\"peer notification follows removal callback\")\n"
            "call BJassAssert(GetUnitCurrentOrder(first)==GetIssuedOrderId(),\"first head survives through peer notification\")\nendif\nendif\nendfunction\n"
            "function pointed takes nothing returns nothing\nset points=points+1\nendfunction\n"
            "function verify takes nothing returns nothing\ncall BJassAssert(targets==2 and points==0 and changed==1,\"two original-target notifications\")\nendfunction\n"
            "function main takes nothing returns nothing\nlocal group g=CreateGroup()\nlocal unit u\nlocal trigger t=CreateTrigger()\n"
            "call GroupEnumUnitsOfPlayer(g,Player(0),null)\nloop\nset u=FirstOfGroup(g)\nexitwhen u==null\n"
            "if GetUnitUserData(u)==1 then\nset first=u\nendif\ncall GroupRemoveUnit(g,u)\nendloop\n"
            "call GroupEnumUnitsOfPlayer(g,Player(1),null)\nset target=FirstOfGroup(g)\ncall DestroyGroup(g)\n"
            "call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)\ncall TriggerAddAction(t,function notified)\n"
            "set t=CreateTrigger()\ncall TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)\ncall TriggerAddAction(t,function pointed)\nendfunction\n"));
        level.started=level.scriptsStarted=true;
        Get_Commands_f(clent);
        if(attack) {
            cstring_t button[]={"button","CmdAttack"};G_ClientCommand(clent,2,button);
            T_EQ(clent->client->menu.on_entity_selected,attack_menu_selecttarget);
        }
        char number[16];snprintf(number,sizeof(number),"%u",target->s.number);
        cstring_t command[]={attack ? "select" : "smart",number};G_ClientCommand(clent,2,command);
        G_RunEvents();jass_runnewevents(level.vm);
        jass_callbyname(level.vm,"verify",false);T_ASSERT(!jass_rterror_pending(level.vm));
        T_ASSERT(G_IsDeferredFree(target));T_NE(target->s.number,0);
        reset_entities();setup_test_world();G_SetSLKRows("UnitData",old);free_slk_rows(rows);
    }
restore_network:
    gi.Write=order022_indicator_write;gi.unicast=order022_indicator_unicast;
}
#endif
