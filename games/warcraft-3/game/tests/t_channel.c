#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

edict_t *alloc_test_unit(uint32_t, float, float);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *);
void free_slk_rows(slkTestData_t *);

static void channel325_advance(float until) {
    abilityTimerRequest_t request;
    unsigned fired=0;
    while(S_NextAbilityPrimaryTimer(&request) && request.deadline.time<=until) {
        T_ASSERT(++fired<128);if(fired>=128)break;
        level.pathing_clock=request.deadline;
        abilityCall_t call={.primary_timer=&request};
        request.proc(NULL,A_PRIMARY_TIMER_FIRE,&call);
    }
    level.pathing_clock.time=until;
}

/* Native ANcl: DataE acquires before windup; DataA owns follow-through after
 * channel-work starts. Non-stock timing distinguishes both from animation. */
TEST(wc3_channel, separation325_public_cast_owns_windup_and_follow_through) {
    reset_entities(); setup_test_world();
    slkTestData_t *rows=parse_slk_string(
        "ID;PWXL;N;E\nB;X10;Y2;D0\n"
        "C;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"DataA1\"\n"
        "C;X4;K\"DataB1\"\nC;X5;K\"DataC1\"\nC;X6;K\"DataE1\"\n"
        "C;X7;K\"DataF1\"\nC;X8;K\"Cost1\"\nC;X9;K\"Cool1\"\nC;X10;K\"Cast1\"\n"
        "C;X1;Y2;K\"ANcl\"\nC;X2;K\"ANcl\"\nC;X3;K0.75\n"
        "C;X4;K0\nC;X5;K1\nC;X6;K1\nC;X7;K\"channel\"\n"
        "C;X8;K0\nC;X9;K0\nC;X10;K0\nE\n");
    slkTestData_t *old=G_SetSLKRows("AbilityData",rows);
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),0,0);
    UnitBalance_t balance=*unit->data.UnitBalance;balance.repulse=1;
    UnitWeapons_t weapons=*unit->data.UnitWeapons;
    weapons.castPoint=0.25f;weapons.castBackSwing=0.125f;
    unit->data.UnitBalance=&balance;unit->data.UnitWeapons=&weapons;
    unit->stand=unit_stand;unit->svflags|=SVF_MONSTER;
    level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
    T_ASSERT(G_ActorAddSkill(unit,MAKEFOURCC('A','N','c','l')));
    CAbilityMove(unit,A_UNIT_INIT,NULL);
    T_ASSERT(unit->movement.repulse.active);
    T_ASSERT(unit_issueimmediateorder(unit,"channel"));
    T_EQ(unit->current_order_id,852600);
    T_EQ(unit->movement.repulse.disable_depth,1);
    T_ASSERT(!S_SpellIsChanneling(unit));
    T_EQ(unit->ability_exclusive_depth,1);
    T_ASSERT(!G_IssueUnitPointOrder(unit,"move",&(vec2_t){128,0},false,unit->s.player,0));
    T_ASSERT(!unit_issueimmediateorder(unit,"channel"));
    channel325_advance(1.25f);
    T_ASSERT(S_SpellIsChanneling(unit));
    T_EQ(unit->movement.repulse.disable_depth,1);
    channel325_advance(1.875f);
    T_ASSERT(S_SpellIsChanneling(unit));
    channel325_advance(2);
    T_ASSERT(!S_SpellIsChanneling(unit));
    T_EQ(unit->movement.repulse.disable_depth,0);
    T_EQ(unit->ability_exclusive_depth,0);
    T_ASSERT(unit->movement.repulse.active);
    /* Retail positive Unit198 retains Stop behind the existing task. */
    level.pathing_clock.time=3;
    T_ASSERT(unit_issueimmediateorder(unit,"channel"));
    T_ASSERT(unit_issueimmediateorder(unit,"stop"));
    T_EQ(unit->current_order_id,852600);
    T_EQ(unit->movement.repulse.disable_depth,1);
    channel325_advance(3.25f);
    T_ASSERT(S_SpellIsChanneling(unit));
    channel325_advance(4);
    T_EQ(unit->movement.repulse.disable_depth,0);
    T_EQ(unit->current_order_id,0);
    T_EQ(G_UnitQueuedOrderCount(unit),0);
    /* Pause/unpause before the retained cast ends never constructs a hold. */
    level.pathing_clock.time=5;
    T_ASSERT(unit_issueimmediateorder(unit,"channel"));
    channel325_advance(5.25f);
    S_SetUnitPaused(unit,true);
    T_ASSERT(unit->paused);
    T_EQ(unit->current_order_id,852600);
    T_EQ(unit->movement.repulse.disable_depth,1);
    T_EQ(unit->ability_pause_depth,0);
    channel325_advance(5.5f);
    S_SetUnitPaused(unit,false);
    T_ASSERT(!unit->paused);
    T_ASSERT(S_SpellIsChanneling(unit));
    T_EQ(unit->movement.repulse.disable_depth,1);
    channel325_advance(6);
    T_EQ(unit->movement.repulse.disable_depth,0);
    T_EQ(unit->ability_exclusive_depth,0);
    T_EQ(unit->current_order_id,0);
    /* A pause that outlives the cast takes over at completion, including
     * after restoring the pending cast and pause from the save contract. */
    level.pathing_clock.time=7;
    T_ASSERT(unit_issueimmediateorder(unit,"channel"));
    channel325_advance(7.25f);
    S_SetUnitPaused(unit,true);
    unsigned slot=unit->s.number;
    cstring_t save=Test_TempPath("channel325-pending-pause.bin");
    T_ASSERT(WriteGame(save));T_ASSERT(ReadGame(save));remove(save);
    unit=g_edicts+slot;
    T_ASSERT(unit->paused);T_ASSERT(S_SpellIsChanneling(unit));
    T_EQ(unit->ability_exclusive_depth,1);T_EQ(unit->ability_pause_depth,0);
    channel325_advance(8);
    T_ASSERT(!S_SpellIsChanneling(unit));T_ASSERT(unit->paused);
    T_EQ(unit->ability_exclusive_depth,0);T_EQ(unit->ability_pause_depth,1);
    T_EQ(unit->current_order_id,851973);
    T_EQ(unit->movement.repulse.disable_depth,1);
    S_SetUnitPaused(unit,false);
    level.pathing_clock.time=8.1f;S_RunMoveTimers();
    T_EQ(unit->movement.repulse.disable_depth,0);
    T_EQ(unit->ability_pause_depth,0);
    reset_entities();G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
}
/* DataB/DataF belong to the actual alias, not the registry's static ANcl row. */
TEST(wc3_channel, separation325_authored_alias_order_and_target_shape) {
    for(unsigned type=0;type<7;type++) {
        reset_entities();setup_test_world();
        char data[2048];
        snprintf(data,sizeof(data),
            "ID;PWXL;N;E\nB;X10;Y2;D0\n"
            "C;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"DataA1\"\n"
            "C;X4;K\"DataB1\"\nC;X5;K\"DataE1\"\nC;X6;K\"DataF1\"\n"
            "C;X7;K\"targs1\"\nC;X8;K\"Rng1\"\nC;X9;K\"Cost1\"\nC;X10;K\"Cool1\"\n"
            "C;X1;Y2;K\"AC25\"\nC;X2;K\"ANcl\"\nC;X3;K0.75\n"
            "C;X4;K%u\nC;X5;K1\nC;X6;K\"%s\"\n"
            "C;X7;K\"air,ground,friend,enemy\"\nC;X8;K999\nC;X9;K0\nC;X10;K0\nE\n",type,type==4 ? "" : type==5 ? "invalid-order" : "thunderclap");
        slkTestData_t *rows=parse_slk_string(data),*old=G_SetSLKRows("AbilityData",rows);
        unitModification_t mod={.modID=MAKEFOURCC('N','c','l','6'),.type=mod_string,.level=1,.dataPointer=6,.data="berserk"};
        unitData_t authored={.originalUnitID=MAKEFOURCC('A','C','2','5'),.numbeOfModifications=1,.modifications=&mod};
        mapInfo_t info={.num_originalAbilities=1,.originalAbilities=&authored};
        if(type==6)G_SetMapAbilityOverrides(&info);
        edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),0,0);
        edict_t *target=alloc_test_unit(MAKEFOURCC('h','f','o','o'),32,0);
        unit->stand=unit_stand;unit->svflags|=SVF_MONSTER;target->svflags|=SVF_MONSTER;
        target->targtype=TARG_GROUND;
        T_ASSERT(G_ActorAddSkill(unit,MAKEFOURCC('A','C','2','5')));
        level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
        cstring_t order=type==6 ? "berserk" : (type==4 || type==5) ? "channel" : "thunderclap";
        bool accepted=(type==0 || type>=4) ? unit_issueimmediateorder(unit,order) :
            type==2 ? unit_issueorder(unit,order,&(vec2_t){32,0}) : unit_issuetargetorder(unit,order,target);
        T_ASSERT(accepted);
        T_EQ(unit->current_order_id,type==6 ? 852100 : (type==4 || type==5) ? 852600 : 852096);
        T_EQ(unit->movement.repulse.disable_depth,1);
        T_ASSERT(!unit_issuetargetorder(unit,"move",target));
        channel325_advance(2.5f);
        T_ASSERT(!S_SpellIsChanneling(unit));
        T_EQ(unit->movement.repulse.disable_depth,0);
        reset_entities();G_SetMapAbilityOverrides(NULL);G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    }
}

bool run_test_jass(cstring_t);
extern void (*test_preload_marker)(cstring_t);
static unsigned channel325_callbacks;
static void channel325_callback_marker(cstring_t text) {
    if(!strcmp(text,"C325 callback"))channel325_callbacks++;
}
TEST(wc3_channel, separation325_nested_channel_orders_preserve_native_ownership) {
    for(unsigned action=0;action<3;action++) {
    reset_entities();setup_test_world();
    slkTestData_t *rows=parse_slk_string(
        "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\n"
        "C;X3;K\"DataA1\"\nC;X4;K\"DataE1\"\nC;X5;K\"DataF1\"\n"
        "C;X1;Y2;K\"ANcl\"\nC;X2;K\"ANcl\"\nC;X3;K0.75\nC;X4;K1\nC;X5;K\"channel\"\n"
        "C;X1;Y3;K\"AC26\"\nC;X2;K\"ANcl\"\nC;X3;K0.75\nC;X4;K1\nC;X5;K\"thunderclap\"\nE\n");
    slkTestData_t *old=G_SetSLKRows("AbilityData",rows);
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),0,0);
    unit->stand=unit_stand;unit->svflags|=SVF_MONSTER;
    UnitWeapons_t weapons=*unit->data.UnitWeapons;weapons.castPoint=.25f;
    unit->data.UnitWeapons=&weapons;
    T_ASSERT(G_ActorAddSkill(unit,MAKEFOURCC('A','N','c','l')));
    if(action==1)T_ASSERT(G_ActorAddSkill(unit,MAKEFOURCC('A','C','2','6')));
    channel325_callbacks=0;test_preload_marker=channel325_callback_marker;
    char source[2048];
    snprintf(source,sizeof(source),
        "globals\nunit caster=null\ntrigger t=null\nboolean fired=false\nendglobals\n"
        "function cb takes nothing returns nothing\ncall Preload(\"C325 callback\")\n"
        "if not fired then\nset fired=true\ncall IssueImmediateOrder(caster,\"%s\")\nendif\nendfunction\n"
        "function main takes nothing returns nothing\nlocal group g=CreateGroup()\n"
        "call GroupEnumUnitsOfPlayer(g,Player(0),null)\nset caster=FirstOfGroup(g)\ncall DestroyGroup(g)\n"
        "set t=CreateTrigger()\ncall TriggerRegisterPlayerUnitEvent(t,Player(0),ConvertPlayerUnitEvent(272),null)\n"
        "call TriggerAddAction(t,function cb)\nendfunction\n"
        "function cleanup takes nothing returns nothing\ncall DestroyTrigger(t)\nendfunction\n",
        action==0 ? "channel" : action==1 ? "thunderclap" : "stop");
    T_ASSERT(run_test_jass(source));
    level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
    T_ASSERT(unit_issueimmediateorder(unit,"channel"));
    T_EQ(channel325_callbacks,action==1 ? 2 : 1);
    if(action)T_EQ(unit->current_order_id,0);
    T_EQ(unit->movement.repulse.disable_depth,1);
    channel325_advance(1.25f);T_EQ(S_SpellIsChanneling(unit),action==0);
    channel325_advance(2);T_ASSERT(!S_SpellIsChanneling(unit));
    T_EQ(unit->movement.repulse.disable_depth,action==0 ? 0 : 1);
    T_ASSERT(!jass_rterror_pending(level.vm));jass_callbyname(level.vm,"cleanup",false);
    test_preload_marker=NULL;reset_entities();G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    }
}

/* Native scenes6/7/8 and16/17/18 release active Channel ownership before
 * death/removal. Removal then owns its separate final-retirement hold. */
TEST(wc3_channel, separation325_retirement_releases_only_cast_contribution) {
    for(unsigned enabled=0;enabled<2;enabled++)for(unsigned action=0;action<3;action++) {
        reset_entities();setup_test_world();
        char data[1024];snprintf(data,sizeof(data),
            "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\n"
            "C;X3;K\"DataA1\"\nC;X4;K\"DataE1\"\nC;X5;K\"DataF1\"\n"
            "C;X1;Y2;K\"ANcl\"\nC;X2;K\"ANcl\"\nC;X3;K0.75\nC;X4;K%u\nC;X5;K\"channel\"\nE\n",enabled);
        slkTestData_t *rows=parse_slk_string(data),*old=G_SetSLKRows("AbilityData",rows);
        edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),0,0);
        unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
        UnitWeapons_t weapons=*unit->data.UnitWeapons;weapons.castPoint=.25f;unit->data.UnitWeapons=&weapons;
        T_ASSERT(G_ActorAddSkill(unit,MAKEFOURCC('A','N','c','l')));
        level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
        T_ASSERT(unit_issueimmediateorder(unit,"channel"));
        channel325_advance(1.25f);T_ASSERT(S_SpellIsChanneling(unit));
        T_EQ(unit->movement.repulse.disable_depth,enabled);
        if(action==0)unit_die(unit,NULL);
        else if(action==1)T_ASSERT(G_ActorRemoveSkill(unit,MAKEFOURCC('A','N','c','l')));
        else G_FreeEdict(unit);
        T_ASSERT(!S_SpellIsChanneling(unit));
        T_EQ(unit->ability_exclusive_depth,0);
        if(action!=2)T_EQ(unit->movement.repulse.disable_depth,0);
        channel325_advance(2);T_EQ(unit->ability_exclusive_depth,0);
        reset_entities();G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    }
}

#endif
