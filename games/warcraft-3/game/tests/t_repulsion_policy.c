#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "retail_repulsion_policy.h"

edict_t *alloc_test_unit(uint32_t, float, float);
void reset_entities(void);
void setup_test_world(void);
bool run_test_jass(cstring_t);
slkTestData_t *parse_slk_string(char const *);
void free_slk_rows(slkTestData_t *);

/* Borrow authored rows for the fixture lifetime; execute Move's real initial
 * policy publication and query callback, including its pose/RNG arithmetic. */
static edict_t *policy_unit(UnitBalance_t const *balance, UnitData_t const *data, unsigned owner, float x, float y) {
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), x, y);
    unit->data.UnitBalance = balance; unit->data.UnitData = data;
    unit->s.player = owner; unit->collision = 8; unit->svflags |= SVF_MONSTER;
    unit->s.model = 1;
    unit->movetype = MOVETYPE_STEP; unit->stand = unit_stand;
    if (S_UnitMovementType(data) == UNIT_MOVE_FLY) unit->aiflags |= AI_FLYING;
    CAbilityMove(unit, A_UNIT_INIT, NULL);
    unit_stand(unit); gi.LinkEntity(unit);
    return unit;
}

static unsigned policy_removal_notifications;

static intptr_t policy_removal_observer(edict_t *ent, abilityMsg_t msg, abilityCall_t const *call) {
    if (msg != A_TARGET_REMOVED) return false;
    edict_t *target = call->removed_target;
    policy_removal_notifications++;
    T_ASSERT(target->inuse); T_ASSERT(G_IsDeferredFree(target));
    T_ASSERT(!target->movement.repulse.active);
    T_NULL(move_repulse_links[target-g_edicts]);
    T_ASSERT(ent->movement.repulse.active);
    return true;
}

/* Public RemoveUnit must retire separation before notifying surviving owners.
 * Owner/type/pause refreshes cannot recreate it while its identity is pending. */
TEST(wc3_repulsion_policy, removal_retires_membership_before_callbacks_and_survives_save) {
    FOR_LOOP(removed, 3) {
        reset_entities(); setup_test_world();
        mapInfo_t const *saved=level.mapinfo; mapInfo_t info=*saved;
        int enabled=2,selector=17,category=17,rank=17;
        unitModification_t mods[]={
            {.modID=MAKEFOURCC('u','r','p','o'),.type=mod_int,.data=&enabled},
            {.modID=MAKEFOURCC('u','r','p','p'),.type=mod_int,.data=&selector},
            {.modID=MAKEFOURCC('u','r','p','g'),.type=mod_int,.data=&category},
            {.modID=MAKEFOURCC('u','r','p','r'),.type=mod_int,.data=&rank}};
        unitData_t custom={.originalUnitID=MAKEFOURCC('h','f','o','o'),.newUnitID=MAKEFOURCC('h','R','E','M'),
            .numbeOfModifications=4,.modifications=mods};
        info.num_userCreatedUnits=1; info.userCreatedUnits=&custom; level.mapinfo=&info; G_SetMapUnitOverrides(&info);
        T_ASSERT(run_test_jass("globals\nunit a\nunit b\nunit c\nunit target\nendglobals\n"
            "function main takes nothing returns nothing\n"
            "set a=CreateUnit(Player(0),'hREM',304,304,0)\n"
            "set b=CreateUnit(Player(0),'hREM',432,304,0)\n"
            "set c=CreateUnit(Player(0),'hREM',560,304,0)\nendfunction\n"
            "function removeA takes nothing returns nothing\nset target=a\ncall RemoveUnit(target)\nendfunction\n"
            "function removeB takes nothing returns nothing\nset target=b\ncall RemoveUnit(target)\nendfunction\n"
            "function removeC takes nothing returns nothing\nset target=c\ncall RemoveUnit(target)\nendfunction\n"
            "function refresh takes nothing returns nothing\n"
            "call SetUnitOwner(target,Player(1),false)\n"
            "call PauseUnit(target,true)\ncall PauseUnit(target,false)\nendfunction\n"
            "function removeAgain takes nothing returns nothing\ncall RemoveUnit(target)\nendfunction\n"));
        edict_t *units[3] = {0}; unsigned count = 0;
        FILTER_EDICTS(ent,ent->inuse && ent->class_id==custom.newUnitID) {
            if (count < 3) units[count] = ent;
            count++;
        }
        T_EQ(count,3);
        if (count != 3) {
            reset_entities(); G_SetMapUnitOverrides(NULL); level.mapinfo=saved;
            break;
        }
        FOR_LOOP(i,3) T_ASSERT(units[i]->movement.repulse.active);
        edict_t *target=units[removed], *observer=units[(removed+1)%3];
        edict_t *survivors[2]; unsigned n=0;
        for (unsigned i=3; i-- > 0;) if (i != removed) survivors[n++]=units[i];
        umove_t watching={.animation="stand",.proc=policy_removal_observer};
        M_SetMove(observer,&watching); policy_removal_notifications=0;
        unsigned visits=move_test_repulse_unlink_visits;
        wc3Random_t random=level.pathing_random;
        cstring_t functions[]={"removeA","removeB","removeC"};
        jass_callbyname(level.vm,functions[removed],false);
        T_ASSERT(!jass_rterror_pending(level.vm)); T_EQ(policy_removal_notifications,1);
        T_ASSERT(G_IsDeferredFree(target)); T_ASSERT(!target->movement.repulse.active);
        T_EQ(move_test_repulse_unlink_visits-visits,1);
        T_EQ(memcmp(&level.pathing_random,&random,sizeof(random)),0);
        T_EQ(level.repulse_head,survivors[0]);
        T_EQ(survivors[0]->movement.repulse.next,survivors[1]);
        T_NULL(survivors[1]->movement.repulse.next);
        jass_callbyname(level.vm,"refresh",false);
        T_ASSERT(!jass_rterror_pending(level.vm));
        T_ASSERT(!target->movement.repulse.active); T_NULL(move_repulse_links[target-g_edicts]);
        /* A rebind notification at the same real lifetime barrier is also ineligible. */
        S_UnitAbilityEvent(target,A_UNIT_TYPE_CHANGED);
        T_ASSERT(!target->movement.repulse.active);
        jass_callbyname(level.vm,"removeAgain",false);
        T_ASSERT(!jass_rterror_pending(level.vm)); T_EQ(policy_removal_notifications,1);
        T_EQ(level.repulse_head,survivors[0]);
        T_EQ(survivors[0]->movement.repulse.next,survivors[1]);
        unit_stand(observer); /* Restore a registered task before serialization. */
        unsigned slot=target->s.number;
        cstring_t file=Test_TempPath("wc3-removal-repulsion-policy.bin");
        T_ASSERT(WriteGame(file)); T_ASSERT(ReadGame(file)); remove(file);
        target=g_edicts+slot;
        T_ASSERT(G_IsDeferredFree(target)); T_ASSERT(!target->movement.repulse.active);
        T_EQ(level.repulse_head,survivors[0]);
        T_EQ(survivors[0]->movement.repulse.next,survivors[1]);
        wc3Clock_t due; uint32_t sequence;
        T_ASSERT(G_NextUnitRelease(&due,&sequence));
        level.pathing_clock=due; G_RunDeferredFrees();
        T_ASSERT(!target->inuse); T_ASSERT(!G_IsDeferredFree(target));
        T_EQ(level.repulse_head,survivors[0]);
        T_EQ(survivors[0]->movement.repulse.next,survivors[1]);
        T_NULL(survivors[1]->movement.repulse.next);
        reset_entities(); G_SetMapUnitOverrides(NULL); level.mapinfo=saved;
    }
    reset_entities(); setup_test_world();
}

/* Native66fc50 tests suppression, not whether the authored Move ability exists.
 * A stationary repulsor remains a candidate and owns its own update state. */
TEST(wc3_repulsion_policy, zero_authored_speed_keeps_repulsor_through_public_lifecycle) {
    reset_entities();setup_test_world();
    mapInfo_t const *saved=level.mapinfo;mapInfo_t info=*saved;
    int speed=0,enabled=1;
    unitModification_t mods[]={
        {.modID=MAKEFOURCC('u','m','v','s'),.type=mod_int,.data=&speed},
        {.modID=MAKEFOURCC('u','r','p','o'),.type=mod_int,.data=&enabled}};
    unitData_t custom={.originalUnitID=MAKEFOURCC('h','f','o','o'),.newUnitID=MAKEFOURCC('h','S','I','0'),
        .numbeOfModifications=2,.modifications=mods};
    info.num_userCreatedUnits=1;info.userCreatedUnits=&custom;level.mapinfo=&info;G_SetMapUnitOverrides(&info);
    T_ASSERT(run_test_jass("globals\nunit u\nendglobals\nfunction main takes nothing returns nothing\n"
        "set u=CreateUnit(Player(0),'hSI0',304,304,0)\nendfunction\n"
        "function transfer takes nothing returns nothing\ncall SetUnitOwner(u,Player(1),false)\nendfunction\n"
        "function freeze takes nothing returns nothing\ncall PauseUnit(u,true)\nendfunction\n"
        "function resume takes nothing returns nothing\ncall PauseUnit(u,false)\nendfunction\n"));
    edict_t *unit=NULL;
    FILTER_EDICTS(ent,ent->inuse && ent->class_id==custom.newUnitID)unit=ent;
    T_NOT_NULL(unit);
    if(unit) {
        T_ASSERT(M_UnitMoveDisabled(unit));T_ASSERT(unit->movement.repulse.active);
        T_EQ(level.repulse_head,unit);T_EQ(unit->movement.repulse.state.packed,0);
        jass_callbyname(level.vm,"transfer",false);T_ASSERT(!jass_rterror_pending(level.vm));
        T_ASSERT(unit->movement.repulse.active);T_EQ(unit->movement.repulse.state.packed,0x01000000u);
        jass_callbyname(level.vm,"freeze",false);T_ASSERT(!jass_rterror_pending(level.vm));
        T_ASSERT(!unit->movement.repulse.active);T_NULL(level.repulse_head);
        jass_callbyname(level.vm,"resume",false);T_ASSERT(!jass_rterror_pending(level.vm));
        T_ASSERT(unit->movement.repulse.active);T_EQ(level.repulse_head,unit);
        T_EQ(unit->movement.repulse.state.packed,0x01000000u);
        unsigned number=unit->s.number;cstring_t file=Test_TempPath("wc3-immobile-repulsor.bin");
        T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);unit=g_edicts+number;
        T_ASSERT(M_UnitMoveDisabled(unit));T_ASSERT(unit->movement.repulse.active);
        T_EQ(level.repulse_head,unit);T_EQ(unit->movement.repulse.state.packed,0x01000000u);
    }
    reset_entities();G_SetMapUnitOverrides(NULL);level.mapinfo=saved;setup_test_world();
}

TEST(wc3_repulsion_policy, live_matrix_checks_eligibility_before_displacement) {
    FOR_LOOP(row, sizeof(retail_repulsion_policy)/sizeof(*retail_repulsion_policy)) {
        reset_entities(); setup_test_world();
        UnitBalance_t balance[2] = {{0}}; UnitData_t data[2] = {{0}}; edict_t *units[2];
        FOR_LOOP(i, 2) {
            typeof(retail_repulsion_policy[0].units[0]) const *input = &retail_repulsion_policy[row].units[i];
            balance[i] = (UnitBalance_t){.speed=270,.repulse=input->enabled, .repulseParam=input->selector,
                .repulseGroup=input->group, .repulsePrio=input->rank};
            data[i].moveTypeName = input->type;
            units[i] = policy_unit(balance+i, data+i, input->owner, 304+i*8, 304);
            units[i]->collision = input->collision; gi.LinkEntity(units[i]);
            T_EQ(units[i]->movement.repulse.active, input->enabled != 0);
        }
        FOR_LOOP(i, 2) {
            wc3GridPose_t source; unit_predicted_pose(units[i], &source);
            moveRepulseQuery_t query = {.self=units[i],
                .pair={.source={source.grid[0],source.grid[1]}, .config=wc3_repulse_config(0), .random=&level.pathing_random},
                .category=(units[i]->movement.repulse.state.packed>>20)&255,
                .rank=units[i]->movement.repulse.state.packed>>28};
            repulse_query = &query;
            if (units[i]->movement.repulse.active) move_repulse_candidate(units[1-i]);
            repulse_query = NULL;
            wc3Repulse_t const *state = &units[i]->movement.repulse.state;
            bool contributed = state->vector[0] != 0 || state->vector[1] != 0;
            T_EQ(contributed, retail_repulsion_policy[row].eligible[i]);
        }
    }
    reset_entities(); setup_test_world();
}

TEST(wc3_repulsion_policy, channels_retire_and_recreate_membership_before_callbacks) {
    reset_entities(); setup_test_world();
    UnitBalance_t balance = {.speed=270,.repulse=1, .repulseParam=17, .repulseGroup=17, .repulsePrio=17};
    UnitData_t data = {.moveTypeName="foot"};
    edict_t *unit = policy_unit(&balance, &data, 0, 304, 304);
    T_EQ(unit->movement.repulse.state.packed, 0x10110000u);
    spell_begin_channel(unit, MAKEFOURCC('A','H','b','z'));
    T_ASSERT(!unit->movement.repulse.active); T_NULL(level.repulse_head);
    move_repulse_init(unit); /* Owner/type refresh cannot bypass a live channel. */
    T_ASSERT(!unit->movement.repulse.active);
    S_SpellCancelChannel(unit);
    T_ASSERT(unit->movement.repulse.active); T_EQ(level.repulse_head, unit);
    T_EQ(unit->movement.repulse.state.packed, 0x10110000u);
    S_SetUnitPaused(unit, true);
    spell_begin_channel(unit, MAKEFOURCC('A','H','b','z'));
    S_SpellCancelChannel(unit);
    T_ASSERT(!unit->movement.repulse.active);
    S_SetUnitPaused(unit, false);
    T_ASSERT(unit->movement.repulse.active);
    reset_entities(); setup_test_world();
}

TEST(wc3_repulsion_policy, saved_channel_restores_disabled_membership_and_resumes) {
    reset_entities(); setup_test_world();
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','g','r','y'), 304, 304);
    unit->stand=unit_stand; unit->s.model=1; unit->collision=8; unit->svflags|=SVF_MONSTER;
    CAbilityMove(unit, A_UNIT_INIT, NULL); unit_stand(unit);
    T_ASSERT(unit->movement.repulse.active);
    spell_begin_channel(unit, MAKEFOURCC('A','H','b','z'));
    T_ASSERT(!unit->movement.repulse.active);
    cstring_t file = Test_TempPath("wc3-repulsion-channel-policy.bin");
    T_ASSERT(WriteGame(file)); T_ASSERT(ReadGame(file));
    T_ASSERT(!unit->movement.repulse.active); T_NULL(level.repulse_head);
    S_SpellCancelChannel(unit);
    T_ASSERT(unit->movement.repulse.active); T_EQ(level.repulse_head, unit);
    T_EQ(unit->movement.repulse.state.packed, 0);
    remove(file); reset_entities(); setup_test_world();
}

TEST(wc3_repulsion_policy, rebound_type_replaces_policy_and_pending_displacement) {
    reset_entities(); setup_test_world();
    UnitBalance_t old = {.speed=270,.repulse=1}, replacement = {.speed=270,.repulse=2, .repulseParam=4, .repulseGroup=3, .repulsePrio=2};
    UnitData_t data = {.moveTypeName="foot"};
    edict_t *unit = policy_unit(&old, &data, 2, 304, 304);
    unit->movement.repulse.state.vector[0] = .3f;
    S_UnitAbilityEvent(unit, A_UNIT_TYPE_CHANGING);
    unit->data.UnitBalance = &replacement;
    S_UnitAbilityEvent(unit, A_UNIT_TYPE_CHANGED);
    T_EQ(unit->movement.repulse.state.packed, 0x22340000u);
    T_EQ(unit->movement.repulse.state.vector[0], 0);
    replacement.repulse = 0;
    S_UnitAbilityEvent(unit, A_UNIT_TYPE_CHANGED);
    T_ASSERT(!unit->movement.repulse.active); T_NULL(level.repulse_head);
    reset_entities(); setup_test_world();
}

TEST(wc3_repulsion_policy, authored_selector_keeps_integer_low_bits) {
    reset_entities(); setup_test_world();
    slkTestData_t *rows = parse_slk_string("ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"unitBalanceID\"\nC;Y1;X2;K\"repulse\"\n"
        "C;Y1;X3;K\"repulseParam\"\nC;Y1;X4;K\"repulseGroup\"\nC;Y1;X5;K\"repulsePrio\"\nC;Y1;X6;K\"spd\"\n"
        "C;Y2;X1;K\"hfoo\"\nC;Y2;X2;K\"2\"\nC;Y2;X3;K\"16777217\"\n"
        "C;Y2;X4;K\"17\"\nC;Y2;X5;K\"17\"\nC;Y2;X6;K\"270\"\nE\n");
    slkTestData_t *old = G_SetSLKRows("UnitBalance", rows);
    UnitData_t data = {.moveTypeName="foot"};
    edict_t *unit = policy_unit(G_UnitBalance(MAKEFOURCC('h','f','o','o')), &data, 0, 304, 304);
    T_EQ(unit->movement.repulse.state.packed, 0x10110000u);
    G_SetSLKRows("UnitBalance", old); free_slk_rows(rows);
    reset_entities(); setup_test_world();
}
#endif
