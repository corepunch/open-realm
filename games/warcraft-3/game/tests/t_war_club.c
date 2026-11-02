/* Mountain Giant War Club: entity-targeted tree consumption, reversible
 * alternate weapon and committed-swing limits. */
#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"

void reset_entities(void);
void setup_test_world(void);
edict_t *alloc_test_unit(uint32_t class_id, float x, float y);

#define TEST_GRAB_TREE MAKEFOURCC('A','g','r','a')

static UnitAbilities_t war_club_abilities = { .abilList = "Agra" };
static UnitWeapons_t war_club_weapons = { .attacksEnabled = 1 };

static edict_t *make_war_club_giant(void) {
    edict_t *giant = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    giant->data.UnitAbilities = &war_club_abilities;
    giant->data.UnitWeapons = &war_club_weapons;
    giant->svflags |= SVF_MONSTER;
    giant->collision = 48.0f;
    giant->unit_state->attack1.type = ATK_NORMAL;
    giant->unit_state->attack1.range = 128.0f;
    giant->unit_state->attack2.type = ATK_SIEGE;
    giant->unit_state->attack2.range = 250.0f;
    giant->unit_state->attack1.targetsAllowed = 0xffffffffu;
    giant->unit_state->attack2.targetsAllowed = 0xffffffffu;
    return giant;
}

static edict_t *make_war_club_tree(void) {
    edict_t *tree = G_Spawn();
    tree->class_id = MAKEFOURCC('B','0','0','X');
    tree->s.class_id = tree->class_id;
    G_BindEntityData(tree);
    tree->targtype = TARG_TREE;
    tree->health.max_value = tree->health.value = 100.0f;
    tree->s.model = 1;
    tree->s.scale = 1.0f;
    tree->s.origin2 = (vec2_t){64, 0};
    tree->s.origin = (vec3_t){64, 0, 0};
    tree->collision = 16;
    G_AddDestructable(tree);
    return tree;
}

static bool grab_tree_message(edict_t *giant, edict_t *tree, abilityMsg_t msg) {
    abilityitem_t item = S_AbilityItem(TEST_GRAB_TREE);
    spellTarget_t target = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = tree);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item, .target = &target);
    return S_AbilityMessage(giant, msg, &call) != 0;
}

TEST(wc3_war_club, command_cursor_selects_tree_without_generic_unit_targeting) {
    const char ability_slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Rng1\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
        "C;Y2;X1;K\"Agra\"\nC;Y2;X2;K\"Agra\"\nC;Y2;X3;K1\n"
        "C;Y2;X4;K256\nC;Y2;X5;K0\nC;Y2;X6;K0\nE\n";
    slkTestData_t *rows, *old;
    edict_t *giant, *tree, *approach = NULL, *clent = &g_edicts[0];
    gameClient_t *client = &game.clients[0];
    char entity_number[16];
    cstring_t button[] = { "button", "Agra" };
    cstring_t select[] = { "select", entity_number };

    reset_entities(); setup_test_world();
    rows = parse_slk_string(ability_slk); old = G_SetSLKRows("AbilityData", rows);
    giant = make_war_club_giant();
    giant->s.player = client->ps.number = 0;
    giant->stand = unit_stand;
    giant->think = monster_think;
    giant->movetype = MOVETYPE_STEP;
    giant->health.value = giant->health.max_value = 500;
    giant->mana.value = giant->mana.max_value = 100;
    giant->unitinfo.MoveSpeed = 270.0f;
    giant->collision = 48.0f;
    clent->client = client; clent->inuse = true;
    client->connected = true;
    G_SelectEntity(client, giant);
    tree = make_war_club_tree();
    tree->s.origin2.x = tree->s.origin.x = 600.0f;
    tree->collision = 16.0f;
    gi.LinkEntity(giant);
    gi.LinkEntity(tree);

    G_ClientCommand(clent, 2, button);
    T_ASSERT(S_SpellEntityTargetMode(clent));
    T_NOT_NULL(client->menu.on_entity_selected);
    T_NULL(client->menu.on_location_selected);
    snprintf(entity_number, sizeof(entity_number), "%u", (unsigned)tree->s.number);
    G_ClientCommand(clent, 2, select);
    FILTER_EDICTS(ent, ent->inuse && ent->think == S_SpellTargetApproachThink) {
        approach = ent;
        break;
    }
    T_NOT_NULL(approach);
    FOR_LOOP(frame, 300) {
        level.time += FRAMETIME;
        level.framenum++;
        G_RunEntities();
        /* A normal Move can settle at a destructable's collision edge and
         * switch to Stand before the target-approach thinker runs again. */
        if (frame == 5) {
            giant->s.origin2 = approach->s.origin2;
            giant->s.origin.x = approach->s.origin2.x;
            giant->s.origin.y = approach->s.origin2.y;
            giant->goalentity = approach;
            unit_stand(giant);
        }
    }

    T_ASSERT(G_GetDestructable(tree)->dead);
    T_ASSERT(S_WarClubActive(giant));
    T_EQ(giant->unit_state->war_club_attacks_remaining, 15u);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_war_club, grab_consumes_only_a_living_tree_and_switches_attack_mask) {
    edict_t *giant, *tree;
    reset_entities(); setup_test_world();
    giant = make_war_club_giant();
    tree = make_war_club_tree();

    T_ASSERT(S_UnitAttackSlotEnabled(giant, 0));
    T_ASSERT(!S_UnitAttackSlotEnabled(giant, 1));
    T_ASSERT(grab_tree_message(giant, tree, A_VALIDATE));
    T_ASSERT(grab_tree_message(giant, tree, A_EXECUTE));
    T_ASSERT(G_GetDestructable(tree)->dead);
    T_FEQ(tree->health.value, 0.0f, 0.001f);
    T_ASSERT(S_WarClubActive(giant));
    T_EQ(giant->unit_state->war_club_ability, TEST_GRAB_TREE);
    T_EQ(giant->unit_state->war_club_attacks_remaining, 15u);
    T_ASSERT(!S_UnitAttackSlotEnabled(giant, 0));
    T_ASSERT(S_UnitAttackSlotEnabled(giant, 1));
    T_ASSERT(!grab_tree_message(giant, tree, A_VALIDATE));
    T_ASSERT(!grab_tree_message(giant, tree, A_EXECUTE));
}

TEST(wc3_war_club, committed_attacks_exhaust_club_then_restore_attack_one) {
    edict_t *giant;
    reset_entities(); setup_test_world();
    giant = make_war_club_giant();
    S_WarClubEquip(giant, TEST_GRAB_TREE);
    T_ASSERT(S_WarClubActive(giant));
    FOR_LOOP(i, 14) S_WarClubConsumeAttack(giant);
    T_EQ(giant->unit_state->war_club_attacks_remaining, 1u);
    T_ASSERT(S_UnitAttackSlotEnabled(giant, 1));
    S_WarClubConsumeAttack(giant);
    T_ASSERT(!S_WarClubActive(giant));
    T_EQ(giant->unit_state->war_club_ability, 0u);
    T_ASSERT(S_UnitAttackSlotEnabled(giant, 0));
    T_ASSERT(!S_UnitAttackSlotEnabled(giant, 1));
    S_WarClubConsumeAttack(giant);
    T_EQ(giant->unit_state->war_club_attacks_remaining, 0u);
}

TEST(wc3_war_club, invalid_targets_are_not_consumed) {
    edict_t *giant, *tree;
    reset_entities(); setup_test_world();
    giant = make_war_club_giant();
    tree = make_war_club_tree();
    tree->targtype = TARG_DEBRIS;
    T_ASSERT(!grab_tree_message(giant, tree, A_VALIDATE));
    T_ASSERT(!grab_tree_message(giant, tree, A_EXECUTE));
    T_ASSERT(!G_GetDestructable(tree)->dead);
    tree->targtype = TARG_TREE;
    G_SetUnitFlag(tree, UNIT_FLAG_INVULNERABLE, true);
    T_ASSERT(!grab_tree_message(giant, tree, A_VALIDATE));
    G_SetUnitFlag(tree, UNIT_FLAG_INVULNERABLE, false);
    T_ASSERT(grab_tree_message(giant, tree, A_VALIDATE));
}

TEST(wc3_war_club, another_giant_cannot_grab_a_consumed_tree) {
    edict_t *first, *second, *tree;
    reset_entities(); setup_test_world();
    first = make_war_club_giant();
    second = make_war_club_giant();
    tree = make_war_club_tree();
    T_ASSERT(grab_tree_message(first, tree, A_EXECUTE));
    T_ASSERT(!grab_tree_message(second, tree, A_EXECUTE));
    T_ASSERT(!S_WarClubActive(second));
}

TEST(wc3_war_club, death_clears_the_alternate_weapon) {
    edict_t *giant;
    abilityitem_t item = S_AbilityItem(TEST_GRAB_TREE);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    reset_entities(); setup_test_world();
    giant = make_war_club_giant();
    S_WarClubEquip(giant, TEST_GRAB_TREE);
    S_AbilityMessage(giant, A_DEATH, &call);
    T_ASSERT(!S_WarClubActive(giant));
    T_ASSERT(S_UnitAttackSlotEnabled(giant, 0));
}
#endif
