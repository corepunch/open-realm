#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "retail_repulsion_policy.h"

edict_t *alloc_test_unit(uint32_t, float, float);
void reset_entities(void);
void setup_test_world(void);
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
    cstring_t file = "/tmp/wc3-repulsion-channel-policy.bin";
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
