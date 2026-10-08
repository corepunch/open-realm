#ifdef BZ_TESTS
/*
 * test_movement.c — Unit movement and pathfinding tests.
 *
 * Tests cover the complete move-order pipeline:
 *
 *  order_move / ai_walk integration
 *    - order_move wires up goalentity and switches to "walk" animation
 *    - unit advances toward goal each frame  (via currentmove->think)
 *    - unit transitions to "stand" once it reaches the goal
 *    - unit_movedistance matches speed × 10 / FRAMETIME
 *
 *  Waypoint helpers
 *    - Waypoint_add places a waypoint at the requested 2-D location
 *
 *  Goal-distance helper
 *    - M_DistanceToGoal returns the 2-D Euclidean distance to goalentity
 *
 * All tests use the test harness mock gi; no actual map or MPQ is needed.
 * Units are given collision = 0 for these movement tests so they don't
 * interact with each other; collision behaviour is covered in
 * test_collision.c.
 */

#include <math.h>
#include "test.h"
#include "../g_local.h"
#include "games/warcraft-3/common/terrain.h"

/* Helpers defined in t_utils.c */
edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
void CM_SetupTestPathmap(uint32_t width, uint32_t height, uint8_t const *cells);
void CM_SetupTestWorldBounds(box2_t const *bounds);
void CM_ProcessPathJobs(uint32_t work_budget);
uint32_t CM_RequestHeatmapForRadius(edict_t *goalentity, float radius);
extern void ai_train_build(edict_t *ent);



/* NAVI_THRESHOLD is the distance below which ai_walk uses direct
 * vector math rather than the heatmap flow field.  It is defined in
 * g_ai.c; the test helpers that place waypoints reference it. */

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

/* Create a unit at (x, y) with the lifecycle callbacks and zero collision
 * (movement tests don't want unintended push-apart).  Resets entity pool
 * so each test starts from a clean slate. */
static edict_t *make_moving_unit(float x, float y) {
    static UnitData_t movement_unit_data;
    reset_entities();
    setup_test_world();
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), x, y);
    movement_unit_data = *ent->data.UnitData;
    /* Synthetic rows need a permissive default; individual PropWindow tests
     * override it with the authored boundary under test. */
    movement_unit_data.propWin = 360.0f;
    ent->data.UnitData = &movement_unit_data;
    ent->unitinfo.PropWindow = DEG2RAD(movement_unit_data.propWin);
    ent->movetype  = MOVETYPE_STEP;
    ent->stand     = unit_stand;
    ent->birth     = unit_birth;
    ent->die       = unit_die;
    ent->collision = 0.0f;
    ent->health.value     = 250.0f;
    ent->health.max_value = 250.0f;
    unit_stand(ent);
    return ent;
}

/* Harvest damage now uses the authoritative destructable lifecycle, so test
 * trees must carry the initialization normally supplied by SP_SpawnDestructable. */
static edict_t *make_harvest_tree(float x, float y, float life) {
    edict_t *tree = alloc_test_unit(MAKEFOURCC('L','T','l','t'), x, y);
    SP_monster_tree(tree);
    if (!tree->destructable) tree->destructable = G_AllocDestructable();
    assert(tree->destructable);
    tree->destructable->item_table = (uint32_t)-1;
    tree->targtype = TARG_TREE;
    tree->health.value = tree->health.max_value = life;
    return tree;
}

static UnitAbilities_t const harvest_abilities = { .abilList = "Ahar" };
static UnitAbilities_t const ghoul_harvest_abilities = { .abilList = "Ahrl" };
static UnitAbilities_t const wisp_harvest_abilities = { .abilList = "Awha" };
static UnitProfile_t const wisp_rally_producer_profile = { .trains = "ewsp" };
static UnitAbilities_t const return_gold_lumber_abilities = { .abilList = "Argl" };
static UnitAbilities_t const return_lumber_abilities = { .abilList = "Arlm" };

static void make_live_dropoff(edict_t *building, UnitAbilities_t const *abilities) {
    building->data.UnitAbilities = abilities;
    building->health.value = building->health.max_value = 1000.0f;
}

/* Command-integration tests exercise server selection/target-mode state, not
 * svc_layout transport. Keep their HUD refreshes inside the game-test boundary. */
static void movement_noop_write(pfWriteType_t type, void const *value) { (void)type; (void)value; }
static void movement_noop_unicast(edict_t *ent) { (void)ent; }

typedef struct {
    uint32_t count;
    pfWriteType_t type[4];
    int32_t value[4];
    edict_t *recipient;
} smartIndicatorCapture_t;

static smartIndicatorCapture_t smart_indicator_capture;

static void movement_capture_indicator_write(pfWriteType_t type, void const *value) {
    uint32_t slot = smart_indicator_capture.count++;
    if (slot >= 4) return;
    smart_indicator_capture.type[slot] = type;
    if (value) smart_indicator_capture.value[slot] = *(int32_t const *)value;
}

static void movement_capture_indicator_unicast(edict_t *ent) {
    if (!smart_indicator_capture.recipient) smart_indicator_capture.recipient = ent;
}

slkTestData_t *parse_slk_string(char const *slk_text);
void free_slk_rows(slkTestData_t *rows);


extern float HARVEST_GOLD_CAPACITY;
extern float HARVEST_TREE_DAMAGE;
extern float HARVEST_LUMBER_CAPACITY;
extern float HARVEST_RANGE;
extern float HARVEST_COOLDOWN;
extern float HARVEST_SEARCH_RANGE;
extern void harvest_cooldown(edict_t *);
bool harvest_menu_selecttarget(edict_t *clent, edict_t *target);

static const char slk_wisp_harvest_test_data[] =
    "ID;PWXL;N;E\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"DataA1\"\n"
    "C;Y1;X4;K\"Rng1\"\n"
    "C;Y1;X5;K\"Dur1\"\n"
    "C;Y2;X1;K\"Awha\"\n"
    "C;Y2;X2;K\"Awha\"\n"
    "C;Y2;X3;K9\n"
    "C;Y2;X4;K500\n"
    "C;Y2;X5;K1.0\n"
    "E\n";

/* ROC/TFT stock Awha values from ability_audit -raw Awha. Keep a stock-shaped
 * fixture beside the non-stock DataA case above. */
static const char slk_wisp_harvest_stock_data[] =
    "ID;PWXL;N;E\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
    "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"Rng1\"\n"
    "C;Y1;X7;K\"Dur1\"\n"
    "C;Y2;X1;K\"Awha\"\nC;Y2;X2;K\"Awha\"\n"
    "C;Y2;X3;K5\nC;Y2;X4;K5\nC;Y2;X5;K150\n"
    "C;Y2;X6;K900\nC;Y2;X7;K8\nE\n";

static const char slk_ghoul_harvest_test_data[] =
    "ID;PWXL;N;E\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"DataA1\"\n"
    "C;Y1;X4;K\"DataB1\"\n"
    "C;Y1;X5;K\"Rng1\"\n"
    "C;Y1;X6;K\"Dur1\"\n"
    "C;Y2;X1;K\"Ahrl\"\n"
    "C;Y2;X2;K\"Ahrl\"\n"
    "C;Y2;X3;K3\n"
    "C;Y2;X4;K5\n"
    "C;Y2;X5;K128\n"
    "C;Y2;X6;K0.25\n"
    "E\n";

static slkTestData_t *install_ghoul_harvest_test_data(slkTestData_t **rows_out) {
    slkTestData_t *rows = parse_slk_string(slk_ghoul_harvest_test_data);
    *rows_out = rows;
    return G_SetSLKRows("AbilityData", rows);
}

TEST(wc3_movement, harvest_command_button_toggles_to_return_resources_ui) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    gameCommandButton_t button;

    worker->data.UnitAbilities = &harvest_abilities;

    T_ASSERT(G_BuildCommandButton(worker, "Ahar", false, 0, &button));
    T_STREQ(button.command, "Ahar");
    T_STREQ(button.art, "TestUI\\Textures\\gather.blp");
    T_STREQ(button.tooltip, "Gather");
    T_STREQ(button.ubertip, "Gather resources from a Gold Mine or tree.");
    T_EQ(button.hotkey, 'G');
    T_EQ(button.x, 0);
    T_EQ(button.y, 1);

    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 1);
    T_ASSERT(G_BuildCommandButton(worker, "Ahar", false, 0, &button));
    T_STREQ(button.command, "Ahar");
    T_STREQ(button.art, "TestUI\\Textures\\return-resources.blp");
    T_STREQ(button.tooltip, "Return Resources");
    T_STREQ(button.ubertip, "Return carried resources to a compatible drop-off.");
    T_EQ(button.hotkey, 'R');
    T_EQ(button.x, 3);
    T_EQ(button.y, 2);

    S_SetCarriedResource(worker, RETURN_RESOURCE_GOLD, 7);
    T_ASSERT(G_BuildCommandButton(worker, "Ahar", false, 0, &button));
    T_STREQ(button.tooltip, "Return Resources");

    S_SetCarriedResource(worker, RETURN_RESOURCE_GOLD, 0);
    T_ASSERT(G_BuildCommandButton(worker, "Ahar", false, 0, &button));
    T_STREQ(button.tooltip, "Gather");
}

TEST(wc3_movement, ghoul_ahrl_smart_uses_lumber_only_harvest_data) {
    slkTestData_t *rows, *old_abilities;
    edict_t *worker, *tree;
    float saved_range = HARVEST_RANGE;
    float saved_damage = HARVEST_TREE_DAMAGE;
    float saved_capacity = HARVEST_LUMBER_CAPACITY;

    worker = make_moving_unit(0.0f, 0.0f);
    old_abilities = install_ghoul_harvest_test_data(&rows);
    worker->data.UnitAbilities = &ghoul_harvest_abilities;
    worker->unitinfo.MoveSpeed = 100.0f;
    tree = make_harvest_tree(64.0f, 0.0f, 100.0f);

    /* Deliberately make the legacy globals incompatible with this order. The
     * Ahrl row must supply capacity/range/damage for this worker instead. */
    HARVEST_RANGE = 1.0f;
    HARVEST_TREE_DAMAGE = 1.0f;
    HARVEST_LUMBER_CAPACITY = 1.0f;
    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 2);

    T_ASSERT(S_HarvestCanLumber(worker));
    T_ASSERT(!S_HarvestCanGold(worker));
    T_ASSERT(unit_issuetargetorder(worker, "smart", tree));
    T_ASSERT(worker->goalentity == tree);
    T_STREQ(worker->currentmove->animation, "walk");

    /* Ahrl Rng1=128 means the 64-unit target is already in chop range even
     * though the legacy global above is only 1. */
    worker->currentmove->think(worker);
    T_STREQ(worker->currentmove->animation, "attack");
    worker->wait = FRAMETIME / 1000.0f;
    worker->currentmove->think(worker);
    T_FEQ(tree->health.value, 97.0f, 0.001f);
    T_EQ(worker->harvested_lumber, 5);

    HARVEST_RANGE = saved_range;
    HARVEST_TREE_DAMAGE = saved_damage;
    HARVEST_LUMBER_CAPACITY = saved_capacity;
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* The lumber target click starts harvest directly; it must still retire a Stop guard point. */
TEST(wc3_movement, lumber_target_click_clears_stop_guard) {
    slkTestData_t *rows, *old_abilities;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client;
    edict_t *worker, *tree;
    abilityCall_t call;

    worker = make_moving_unit(0.0f, 0.0f);
    client = &game.clients[0];
    clent->client = client;
    old_abilities = install_ghoul_harvest_test_data(&rows);
    worker->data.UnitAbilities = &ghoul_harvest_abilities;
    worker->s.player = client->ps.number;
    tree = make_harvest_tree(96.0f, 0.0f, 100.0f);
    G_SelectEntity(client, worker);
    order_stop(worker);
    T_ASSERT(worker->movement.guard_state == GUARD_IDLE);

    call = MAKE(abilityCall_t, .client = clent);
    T_ASSERT(CAbilityHarvestLumber(worker, A_COMMAND, &call));
    T_ASSERT(client->menu.on_entity_selected(clent, tree));

    T_ASSERT(worker->goalentity == tree);
    T_ASSERT(worker->movement.guard_state == GUARD_NONE);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, ghoul_ahrl_command_targets_tree_and_autoharvests_lumber) {
    slkTestData_t *rows, *old_abilities;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client;
    edict_t *worker, *tree;
    abilityCall_t call;

    worker = make_moving_unit(0.0f, 0.0f);
    client = &game.clients[0];
    clent->client = client;
    old_abilities = install_ghoul_harvest_test_data(&rows);
    worker->data.UnitAbilities = &ghoul_harvest_abilities;
    worker->s.player = client->ps.number;
    tree = make_harvest_tree(96.0f, 0.0f, 100.0f);
    G_SelectEntity(client, worker);

    call = MAKE(abilityCall_t, .client = clent);
    T_ASSERT(CAbilityHarvestLumber(worker, A_COMMAND, &call));
    T_NOT_NULL(client->menu.on_entity_selected);
    T_ASSERT(client->menu.on_entity_selected(clent, tree));
    T_ASSERT(worker->goalentity == tree);

    unit_stand(worker);
    T_ASSERT(unit_issueimmediateorder(worker, "autoharvestlumber"));
    T_ASSERT(worker->goalentity == tree);
    T_ASSERT(!unit_issueimmediateorder(worker, "autoharvestgold"));

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, runtime_added_call_to_arms_exposes_on_and_off_buttons) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    gameCommandButton_t buttons[16];
    uint8_t count;
    bool found_on = false, found_off = false;

    T_ASSERT(G_ActorAddSkill(worker, MAKEFOURCC('A','m','i','c')));
    count = G_GetCommandButtons(worker, buttons, (uint8_t)(sizeof(buttons) / sizeof(buttons[0])));
    FOR_LOOP(i, count) {
        if (!strcmp(buttons[i].command, "Amic")) {
            found_on = true;
            T_STREQ(buttons[i].tooltip, "Call to Arms");
            T_EQ(buttons[i].x, 1);
            T_EQ(buttons[i].y, 1);
        } else if (!strcmp(buttons[i].command, "Amic:off")) {
            found_off = true;
            T_STREQ(buttons[i].tooltip, "Back to Work");
            T_EQ(buttons[i].x, 2);
            T_EQ(buttons[i].y, 1);
        }
    }
    T_ASSERT(found_on);
    T_ASSERT(found_off);

    T_ASSERT(G_ActorRemoveSkill(worker, MAKEFOURCC('A','m','i','c')));
    count = G_GetCommandButtons(worker, buttons, (uint8_t)(sizeof(buttons) / sizeof(buttons[0])));
    FOR_LOOP(i, count) {
        T_ASSERT(strcmp(buttons[i].command, "Amic") != 0);
        T_ASSERT(strcmp(buttons[i].command, "Amic:off") != 0);
    }
}

TEST(wc3_movement, missing_melee_amic_recovers_only_first_tier_one_hall) {
    static UnitAbilities_t const townhall_abilities = {
        .id = MAKEFOURCC('h','t','o','w'),
        .abilList = "",
    };
    edict_t *first = make_moving_unit(0.0f, 0.0f);
    edict_t *second = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 512.0f, 0.0f);

    first->class_id = first->s.class_id = MAKEFOURCC('h','t','o','w');
    first->data.UnitAbilities = &townhall_abilities;
    first->svflags |= SVF_MONSTER;
    first->s.player = 0;
    first->spawn_time = 100;

    second->data.UnitAbilities = &townhall_abilities;
    second->svflags |= SVF_MONSTER;
    second->s.player = 0;
    second->spawn_time = 200;

    T_ASSERT(S_MilitiaEnsureHallAbility(first));
    T_ASSERT(G_ActorHasSkill(first, "Amic"));
    T_ASSERT(!S_MilitiaEnsureHallAbility(second));
    T_ASSERT(!G_ActorHasSkill(second, "Amic"));
}

TEST(wc3_movement, carried_resource_toggle_invalidates_selected_command_card) {
    gameClient_t *client = &game.clients[0];
    edict_t *worker = make_moving_unit(0.0f, 0.0f);

    worker->s.player = client->ps.number;
    G_SelectEntity(client, worker);
    client->commands_dirty = false;

    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 1);
    T_ASSERT(client->commands_dirty);

    client->commands_dirty = false;
    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 2);
    T_ASSERT(!client->commands_dirty);

    S_SetCarriedResource(worker, RETURN_RESOURCE_GOLD, 7);
    T_ASSERT(!client->commands_dirty);

    S_SetCarriedResource(worker, RETURN_RESOURCE_GOLD, 0);
    T_ASSERT(client->commands_dirty);
}

static const char slk_goldmine_test_data[] =
    "ID;PWXL;N;E\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"Data11\"\n"
    "C;Y1;X4;K\"Data12\"\n"
    "C;Y1;X5;K\"Data13\"\n"
    "C;Y2;X1;K\"Agld\"\n"
    "C;Y2;X2;K\"Agld\"\n"
    "C;Y2;X3;K12500\n"
    "C;Y2;X4;K1\n"
    "C;Y2;X5;K1\n"
    "C;Y3;X1;K\"A001\"\n"
    "C;Y3;X2;K\"Agld\"\n"
    "C;Y3;X3;K100\n"
    "C;Y3;X4;K0.01\n"
    "C;Y3;X5;K1\n"
    "C;Y4;X1;K\"A002\"\n"
    "C;Y4;X2;K\"Agld\"\n"
    "C;Y4;X3;K200\n"
    "C;Y4;X4;K2\n"
    "C;Y4;X5;K2\n"
    "E\n";

static UnitAbilities_t const test_goldmine_stock = { .abilList = "Agld" };
static UnitAbilities_t const test_goldmine_cap1 = { .abilList = "A001" };
static UnitAbilities_t const test_goldmine_cap2 = { .abilList = "A002" };

static const char slk_racial_goldmine_test_data[] =
    "ID;PWXL;N;E\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"Rng1\"\n"
    "C;Y1;X4;K\"DataA1\"\n"
    "C;Y1;X5;K\"DataB1\"\n"
    "C;Y1;X6;K\"DataC1\"\n"
    "C;Y1;X7;K\"DataD1\"\n"
    "C;Y1;X8;K\"UnitID1\"\n"
    "C;Y1;X9;K\"Rng2\"\n"
    "C;Y1;X10;K\"Dur1\"\n"
    "C;Y1;X11;K\"DataA1\"\n"
    "C;Y1;X12;K\"DataB1\"\n"
    "C;Y1;X13;K\"UnitID1\"\n"
    "C;Y1;X14;K\"isbldg\"\n"
    "C;Y1;X15;K\"alias\"\n"
    "C;Y1;X16;K\"Dur1\"\n"
    "C;Y1;X17;K\"HeroDur1\"\n"
    "C;Y2;X1;K\"Agld\"\n"
    "C;Y2;X2;K\"Agld\"\n"
    "C;Y3;X1;K\"Aaha\"\n"
    "C;Y3;X2;K\"Aaha\"\n"
    "C;Y3;X3;K64\n"
    "C;Y4;X1;K\"Abgm\"\n"
    "C;Y4;X2;K\"Abgm\"\n"
    "C;Y4;X4;K10\n"
    "C;Y4;X5;K1\n"
    "C;Y4;X6;K5\n"
    "C;Y4;X7;K200\n"
    "C;Y5;X1;K\"Aegm\"\n"
    "C;Y5;X2;K\"Aegm\"\n"
    "C;Y5;X4;K10\n"
    "C;Y5;X5;K1\n"
    "C;Y6;X1;K\"Aenc\"\n"
    "C;Y6;X2;K\"Aenc\"\n"
    "C;Y6;X4;K5\n"
    "C;Y7;X1;K\"Agl2\"\n"
    "C;Y7;X2;K\"Agl2\"\n"
    "C;Y8;X1;K\"Aent\"\n"
    "C;Y8;X2;K\"Aent\"\n"
    "C;Y8;X3;K64\n"
    "C;Y8;X13;K\"hbar\"\n"
    "C;Y8;X14;K1\n"
    "C;Y8;X15;K\"Aent\"\n"
    "C;Y9;X1;K\"Aro1\"\n"
    "C;Y9;X2;K\"Aro1\"\n"
    "C;Y9;X16;K1\n"
    "C;Y9;X17;K1\n"
    "E\n";

static UnitAbilities_t const test_haunted_mine = { .abilList = "Abgm" };
static UnitAbilities_t const test_acolyte_harvest = { .abilList = "Aaha" };
static UnitAbilities_t const test_entangled_mine = { .abilList = "Aegm,Aenc" };
static UnitAbilities_t const test_entangle_caster = { .abilList = "Aent,Aro1" };

static slkTestData_t *install_racial_goldmine_test_data(slkTestData_t **rows_out) {
    slkTestData_t *rows = parse_slk_string(slk_racial_goldmine_test_data);
    *rows_out = rows;
    return G_SetSLKRows("AbilityData", rows);
}

static uint32_t count_haunted_ring_effects(edict_t const *mine) {
    uint32_t count = 0;
    FILTER_EDICTS(effect, effect->inuse && effect->owner == mine &&
                  effect->summon_ability == MAKEFOURCC('A','b','g','m') &&
                  effect->resources > 0 && (effect->s.flags & EF_NOT_SELECTABLE)) {
        count++;
    }
    return count;
}

static edict_t *haunted_ring_effect_slot(edict_t *mine, uint32_t slot) {
    FILTER_EDICTS(effect, effect->inuse && effect->owner == mine &&
                  effect->summon_ability == MAKEFOURCC('A','b','g','m') &&
                  effect->resources == slot + 1 && (effect->s.flags & EF_NOT_SELECTABLE)) {
        return effect;
    }
    return NULL;
}

static slkTestData_t *install_goldmine_test_data(slkTestData_t **rows_out) {
    slkTestData_t *rows = parse_slk_string(slk_goldmine_test_data);
    *rows_out = rows;
    return G_SetSLKRows("AbilityData", rows);
}

static void setup_test_goldmine(edict_t *mine, UnitAbilities_t const *abilities, uint32_t resources) {
    mine->data.UnitAbilities = abilities;
    mine->resources = resources;
    mine->health.value = mine->health.max_value = 1000.0f;
}

static pathTex_t *movement_make_goldmine_pathtex(void) {
    enum { W = 16, H = 16 };
    pathTex_t *tex = gi.MemAlloc(sizeof(*tex) + W * H * sizeof(color32_t));
    T_ASSERT(tex != NULL);
    tex->width = W;
    tex->height = H;
    FOR_LOOP(i, W * H)
        tex->map[i] = (color32_t){ 0, 0, 0, 255 };
    for (int y = 4; y < 12; y++) {
        for (int x = 4; x < 12; x++)
            tex->map[x + y * W].b = 255;
    }
    return tex;
}

static edict_t *add_gold_worker(float x, float y) {
    edict_t *worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), x, y);
    worker->movetype = MOVETYPE_STEP;
    worker->stand = unit_stand;
    worker->die = unit_die;
    worker->collision = 16.0f;
    worker->health.value = worker->health.max_value = 250.0f;
    worker->unitinfo.MoveSpeed = 100.0f;
    unit_stand(worker);
    return worker;
}

TEST(wc3_movement, explicit_gold_harvest_retires_stop_guard_position) {
    edict_t *worker, *mine;
    slkTestData_t *rows, *old_abilities;

    setup_test_world();
    worker = add_gold_worker(0, 0);
    mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 320, 0);
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    worker->data.UnitAbilities = &harvest_abilities;
    order_stop(worker);
    T_ASSERT(worker->movement.guard_state != GUARD_NONE);

    old_abilities = install_goldmine_test_data(&rows);
    T_ASSERT(G_IssueUnitTargetOrder(worker, "harvest", mine, false, 0));
    T_ASSERT(worker->movement.guard_state == GUARD_NONE);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

static bool tree_died;
static uint32_t tree_pained;
static void test_tree_die(edict_t *tree, edict_t *attacker) { (void)tree; (void)attacker; tree_died = true; }
static void test_tree_pain(edict_t *tree) { (void)tree; tree_pained++; }

typedef struct {
    gameMsg_t msg[32];
    uint32_t count;
} msgTrace_t;

static void trace_message(gameMsg_t const *msg, void *ctx) {
    msgTrace_t *trace = ctx;
    if (trace->count < sizeof(trace->msg) / sizeof(trace->msg[0]))
        trace->msg[trace->count++] = *msg;
}

/* Worker resource movement mirrors CBehaviorHarvest's
 * disableCollision=true contract for unit targets.  A live Peasant directly
 * in the mine lane must therefore not deflect or stop the approaching miner;
 * static pathing remains enabled separately. */
TEST(wc3_movement, worker_resource_gold_approach_ignores_live_units) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *blocker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 35.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 400.0f, 0.0f);
    vec2_t const origin = worker->s.origin2;
    slkTestData_t *rows, *old_abilities;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.angle = 0.0f;
    blocker->collision = 16.0f;
    blocker->s.model = 1;
    blocker->movetype = MOVETYPE_NONE;
    mine->collision = 128.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(blocker);
    gi.LinkEntity(mine);
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    old_abilities = install_goldmine_test_data(&rows);
    harvest_gold_start(worker, mine);
    worker->currentmove->think(worker);

    T_ASSERT(worker->s.origin2.x > origin.x);
    T_ASSERT(Vector2_distance(&worker->s.origin2, &blocker->s.origin2) <
             worker->collision + blocker->collision);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Resource movement must route static geometry with the worker's real collision radius.
 * A Farm-sized obstacle across the direct mine lane reproduces the failure
 * where the old point-sized field chose cells a Peasant could not physically
 * traverse.  The bounded per-mover accelerator should immediately own a
 * collision-sized detour while the shared field is rebuilt. */
TEST(wc3_movement, worker_resource_static_detour_uses_worker_radius) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(-320.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 320.0f, 0.0f);
    slkTestData_t *rows, *old_abilities;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.angle = 0.0f;
    mine->collision = 128.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    old_abilities = install_goldmine_test_data(&rows);
    harvest_gold_start(worker, mine);

    /* Begin on an open lane, then rebuild the static map with a 4x4 block
     * centred ahead of the already-moving worker, matching construction start. */
    worker->currentmove->think(worker);
    T_ASSERT(worker->s.origin2.x > -320.0f);
    for (int y = 30; y <= 33; y++)
        for (int x = 30; x <= 33; x++)
            pathmap[x + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));
    worker->currentmove->think(worker);

    T_ASSERT(worker->movement.path.valid);
    T_FEQ(worker->movement.path.radius, worker->collision, 0.001f);
    T_ASSERT(fabsf(worker->movement.path.waypoint.y) >= CM_PathCellWorldSize());

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Returning gold should target the nearest legal edge of a blocked drop-off,
 * not the arbitrary pathable cell chosen around its centre.  Keep a Farm-sized
 * obstacle in the lane so this also proves the mover-owned detour is aimed at
 * that near-side edge rather than at the Town Hall centre/far side. */
TEST(wc3_movement, worker_resource_gold_return_targets_near_side_edge) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(-320.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -500.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 320.0f, 0.0f);
    pathTex_t *hall_pathtex = movement_make_goldmine_pathtex();

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->harvested_gold = 10;
    worker->s.renderfx |= RF_HAS_GOLD;
    worker->secondarygoal = mine;
    hall->collision = 64.0f;
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    hall->pathtex = hall_pathtex;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(hall);

    /* Hall authored footprint: centre cell is x=42/y=32 in these bounds and
     * movement_make_goldmine_pathtex() blocks local cells 4..11. */
    for (int y = 28; y < 36; y++)
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    /* Farm-sized obstacle between the mine side and the Hall's left edge. */
    for (int y = 30; y <= 33; y++)
        for (int x = 30; x <= 33; x++)
            pathmap[x + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(harvest_gold_return_to(worker, hall));
    worker->currentmove->think(worker);

    T_ASSERT(worker->movement.path.valid);
    T_FEQ(worker->movement.path.radius, worker->collision, 0.001f);
    T_ASSERT(worker->movement.path.target.x < hall->s.origin2.x);
    T_ASSERT(worker->movement.path.target.x > worker->s.origin2.x);

    hall->pathtex = NULL;
    gi.MemFree(hall_pathtex);
}

/* Lumber Return Resources uses the same collision contract but a separate
 * behavior.  A Lumber Mill to the right must likewise keep the route endpoint
 * on its left/near edge, even when the worker has to detour around new static
 * construction on the way there. */
TEST(wc3_movement, worker_resource_lumber_return_targets_near_side_edge) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(-320.0f, 0.0f);
    edict_t *mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 320.0f, 0.0f);
    pathTex_t *mill_pathtex = movement_make_goldmine_pathtex();

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 10);
    mill->collision = 64.0f;
    mill->s.model = 1;
    mill->s.player = worker->s.player;
    mill->pathtex = mill_pathtex;
    make_live_dropoff(mill, &return_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(mill);

    for (int y = 28; y < 36; y++)
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    for (int y = 30; y <= 33; y++)
        for (int x = 30; x <= 33; x++)
            pathmap[x + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(harvest_lumber_return_to(worker, mill));
    worker->currentmove->think(worker);

    T_ASSERT(worker->movement.path.valid);
    T_FEQ(worker->movement.path.radius, worker->collision, 0.001f);
    T_ASSERT(worker->movement.path.target.x < mill->s.origin2.x);
    T_ASSERT(worker->movement.path.target.x > worker->s.origin2.x);

    mill->pathtex = NULL;
    gi.MemFree(mill_pathtex);
}

/* Collision-sized static routing is cell-centred and therefore can stop just
 * outside the continuous footprint+step deposit test.  Once resource routing reaches
 * the innermost legal near-side endpoint, Return Resources must accept that
 * route end instead of repeatedly steering back across it. */
TEST(wc3_movement, worker_resource_gold_deposits_at_near_side_route_endpoint) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(-320.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 320.0f, 0.0f);
    pathTex_t *hall_pathtex = movement_make_goldmine_pathtex();
    uint32_t const old_gold = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD];
    vec2_t approach;
    float route_band;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->harvested_gold = 10;
    worker->s.renderfx |= RF_HAS_GOLD;
    hall->collision = 64.0f;
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    hall->pathtex = hall_pathtex;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(hall);

    for (int y = 28; y < 36; y++)
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    route_band = worker->collision + CM_PathCellWorldSize() * 1.41421356237f;
    T_ASSERT(CM_FindInnerApproachPointToFootprintForRadius(
        hall, &worker->s.origin2, route_band, worker->collision, &approach));
    T_ASSERT(CM_DistanceToPathingFootprint(hall, &approach) >
             worker->collision + unit_movedistance(worker));
    worker->s.origin2 = approach;
    gi.LinkEntity(worker);

    T_ASSERT(harvest_gold_return_to(worker, hall));
    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold + 10);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));

    hall->pathtex = NULL;
    gi.MemFree(hall_pathtex);
}

TEST(wc3_movement, worker_resource_lumber_deposits_at_near_side_route_endpoint) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(-320.0f, 0.0f);
    edict_t *mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 320.0f, 0.0f);
    pathTex_t *mill_pathtex = movement_make_goldmine_pathtex();
    uint32_t const old_lumber = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER];
    vec2_t approach;
    float route_band;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 10);
    mill->collision = 64.0f;
    mill->s.model = 1;
    mill->s.player = worker->s.player;
    mill->pathtex = mill_pathtex;
    make_live_dropoff(mill, &return_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(mill);

    for (int y = 28; y < 36; y++)
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    route_band = worker->collision + CM_PathCellWorldSize() * 1.41421356237f;
    T_ASSERT(CM_FindInnerApproachPointToFootprintForRadius(
        mill, &worker->s.origin2, route_band, worker->collision, &approach));
    T_ASSERT(CM_DistanceToPathingFootprint(mill, &approach) >
             worker->collision + unit_movedistance(worker));
    worker->s.origin2 = approach;
    gi.LinkEntity(worker);

    T_ASSERT(harvest_lumber_return_to(worker, mill));
    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], old_lumber + 10);
    T_EQ(worker->harvested_lumber, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));

    mill->pathtex = NULL;
    gi.MemFree(mill_pathtex);
}

/* Destructables are the opposite branch in Warsmash: Harvest resets the same
 * generic mover with collision enabled.  A tree approach may route/slide around
 * another unit, but must never commit a step through its collision circle. */
TEST(wc3_movement, worker_resource_tree_approach_keeps_live_unit_collision) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    float const saved_range = HARVEST_RANGE;
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *blocker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 35.0f, 0.0f);
    edict_t *tree = make_harvest_tree(400.0f, 0.0f, 100.0f);

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.angle = 0.0f;
    blocker->collision = 16.0f;
    blocker->s.model = 1;
    blocker->movetype = MOVETYPE_NONE;
    gi.LinkEntity(worker);
    gi.LinkEntity(blocker);
    gi.LinkEntity(tree);
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    HARVEST_RANGE = 64.0f;
    harvest_start(worker, tree);
    worker->currentmove->think(worker);

    T_ASSERT(Vector2_distance(&worker->s.origin2, &blocker->s.origin2) >=
             worker->collision + blocker->collision);
    HARVEST_RANGE = saved_range;
}

/* CBehaviorReturnResources always disables live-unit collision in Warsmash,
 * independent of whether the carried resource is gold or lumber. */
TEST(wc3_movement, worker_resource_lumber_return_ignores_live_units) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *blocker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 35.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 400.0f, 0.0f);

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.angle = 0.0f;
    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 10);
    blocker->collision = 16.0f;
    blocker->s.model = 1;
    blocker->movetype = MOVETYPE_NONE;
    hall->collision = 64.0f;
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(blocker);
    gi.LinkEntity(hall);
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(harvest_lumber_return_to(worker, hall));
    worker->currentmove->think(worker);

    T_ASSERT(Vector2_distance(&worker->s.origin2, &blocker->s.origin2) <
             worker->collision + blocker->collision);
}

/* Gold workers enter at the mine boundary; the mine's collision footprint must
 * not strand them just outside the older fixed interaction radius. */
TEST(wc3_movement, gold_worker_enters_large_mine_footprint) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 400.0f, 0.0f);
    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 100.0f;
    mine->collision = 128.0f; /* 8 blocked cells across in ROC 16x16Goldmine.tga. */
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    harvest_gold_start(worker, mine);

    FOR_LOOP(i, 40) {
        worker->currentmove->think(worker);
        if (worker->s.renderfx & RF_HIDDEN) break;
    }

    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
    T_EQ(mine->peonsinside, 1);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}


/* A worker at the last legal cell beside an authored mine footprint must be
 * admitted when one movement step reaches the footprint.  Keep this fixture at
 * the interaction boundary so it tests mine-entry semantics independently of
 * global route-cache/build-budget state left by earlier pathfinding tests. */
TEST(wc3_movement, gold_worker_enters_mine_with_blocked_pathing_footprint) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(158.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 320.0f, 0.0f);
    pathTex_t *mine_pathtex = movement_make_goldmine_pathtex();

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    mine->collision = 128.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    mine->pathtex = mine_pathtex;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);

    /* Mirror the mine path texture's central 8x8 no-walk cells into the
     * static test map.  The entity carries the same authored pathtex so
     * interaction distance and movement pathing describe one footprint. */
    for (int y = 28; y < 36; y++) {
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(CM_PointIsPathableForRadius(&worker->s.origin2, worker->collision));
    T_ASSERT(!CM_PointIsPathableForRadius(&mine->s.origin2, 0.0f));
    T_ASSERT(CM_DistanceToPathingFootprint(mine, &worker->s.origin2) <=
             worker->collision + unit_movedistance(worker));

    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    harvest_gold_start(worker, mine);
    worker->currentmove->think(worker);

    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
    T_EQ(mine->peonsinside, 1);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
    gi.MemFree(mine_pathtex);
}

/* Resource-building legs ignore live units, so the old Human02 crowd-settle
 * shortcut is no longer part of mine entry. Static pathing remains authoritative:
 * a worker that cannot get its real collision radius within the authored mine
 * interaction boundary must keep the Harvest order alive rather than entering
 * through a blocked edge. */
TEST(wc3_movement, gold_worker_static_blocked_edge_does_not_fake_mine_entry) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(151.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 320.0f, 0.0f);
    pathTex_t *mine_pathtex = movement_make_goldmine_pathtex();
    float footprint;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    mine->collision = 128.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    mine->pathtex = mine_pathtex;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);

    for (int y = 28; y < 36; y++) {
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    footprint = CM_DistanceToPathingFootprint(mine, &worker->s.origin2);
    T_ASSERT(footprint > worker->collision + unit_movedistance(worker));

    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    harvest_gold_start(worker, mine);

    FOR_LOOP(i, 20) {
        worker->currentmove->think(worker);
        CM_ProcessPathJobs(65536);
        if (worker->s.renderfx & RF_HIDDEN)
            break;
    }

    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_EQ(mine->peonsinside, 0);
    T_ASSERT(worker->goalentity == mine);
    T_STREQ(worker->currentmove->animation, "walk");
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
    gi.MemFree(mine_pathtex);
}

/* The mine pathing footprint is square/texture-authored, while mine->collision
 * is only a scalar approximation.  At a footprint corner the worker can be one
 * legal movement step from the no-walk cells while its centre distance is still
 * greater than worker+mine collision+step.  Mine entry must use the authored
 * footprint so routing cannot strand a diagonally approaching worker. */
TEST(wc3_movement, gold_worker_enters_at_pathing_footprint_corner) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(170.0f, 170.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 320.0f, 320.0f);
    pathTex_t *mine_pathtex = movement_make_goldmine_pathtex();

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    mine->collision = 128.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    mine->pathtex = mine_pathtex;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);

    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    /* Centre-circle entry is deliberately still false at this corner.
     * Check the fixture geometry directly: harvest_gold_start() has not yet
     * assigned worker->goalentity, so M_DistanceToGoal() is not valid here. */
    T_ASSERT(Vector2_distance(&worker->s.origin2, &mine->s.origin2) >
             worker->collision + mine->collision + unit_movedistance(worker));
    T_ASSERT(CM_DistanceToPathingFootprint(mine, &worker->s.origin2) <=
             worker->collision + unit_movedistance(worker));

    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    harvest_gold_start(worker, mine);
    worker->currentmove->think(worker);

    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
    T_EQ(mine->peonsinside, 1);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
    gi.MemFree(mine_pathtex);
}

/* A final chop equal to the remaining life must run the tree's death callback,
 * which owns its fall animation and pathing removal. */
TEST(wc3_movement, lumber_final_chop_fells_tree) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 10.0f);
    worker->attack1.damagePoint = 0.01f;
    msgTrace_t trace = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace));
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 10.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f;
    harvest_start(worker, tree);

    worker->currentmove->think(worker);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);
    G_UnsubscribeMessage(trace_message, &trace);

    T_FEQ(tree->health.value, 0.0f, 0.01f);
    T_FEQ(worker->harvested_lumber, 10.0f, 0.01f);
    T_ASSERT(tree->svflags & SVF_DEADMONSTER);
    T_STREQ(tree->currentmove->animation, "death");
    T_EQ(trace.count, 4);
    T_EQ(trace.msg[0].type, GAME_MSG_HARVEST_MOVE_LUMBER);
    T_EQ(trace.msg[1].type, GAME_MSG_HARVEST_START_CHOP);
    T_EQ(trace.msg[2].type, GAME_MSG_HARVEST_CHOP);
    T_EQ(trace.msg[3].type, GAME_MSG_HARVEST_TREE_FELLED);
    FOR_LOOP(i, trace.count) {
        T_EQ(trace.msg[i].actor, worker->s.number);
        T_EQ(trace.msg[i].target, tree->s.number);
    }
}

/* Non-lethal chops damage but do not fell a living tree. */
TEST(wc3_movement, lumber_nonlethal_chop_keeps_tree_standing) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 11.0f);
    tree->pain = test_tree_pain;
    tree->die = test_tree_die;
    tree_died = false;
    tree_pained = 0;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 10.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f;
    harvest_start(worker, tree);

    worker->currentmove->think(worker);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);

    T_ASSERT(!tree_died);
    T_EQ(tree_pained, 1);
    T_FEQ(tree->health.value, 1.0f, 0.01f);
}

/* A resumable route miss has not chosen a heading yet.  Harvest used to call
 * unit_moveindirection anyway, which committed a step along the worker's stale
 * facing while flow_generation=0/direct=false.  Hold the order and position
 * until CM_ProcessPathJobs completes the requested field. */
TEST(wc3_movement, lumber_pending_flow_does_not_move_on_stale_heading) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(-320.0f, 0.0f);
    edict_t *tree = make_harvest_tree(320.0f, 0.0f, 500.0f);
    vec2_t const origin = worker->s.origin2;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.angle = 1.5707963f; /* stale north-facing movement is legal */
    tree->collision = 0.0f;

    /* A full-height wall blocks the direct approach so the first Harvest tick
     * must request a resumable collision-sized field.  Do not process the job:
     * this test covers the pending state itself, not route completion. */
    for (int y = 0; y < CELLS; y++)
        pathmap[32 + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    HARVEST_RANGE = 64.0f;
    HARVEST_SEARCH_RANGE = 1000.0f;
    harvest_start(worker, tree);
    worker->currentmove->think(worker);

    T_EQ(worker->movement.flow_generation, 0);
    T_ASSERT(!worker->movement.flow_direct);
    T_FEQ(worker->s.origin2.x, origin.x, 0.01f);
    T_FEQ(worker->s.origin2.y, origin.y, 0.01f);
    T_ASSERT(worker->goalentity == tree);
}

/* Same-tree workers keep the same chop target.  A worker directly behind
 * another Peasant may queue for a tick while the front worker advances, but it
 * must not be assigned a persistent angular harvest slot. */
TEST(wc3_movement, lumber_same_tree_workers_preserve_direct_order) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *first = make_moving_unit(-400.0f, 0.0f);
    edict_t *second = add_gold_worker(-365.0f, 0.0f);
    edict_t *tree = make_harvest_tree(0.0f, 0.0f, 500.0f);
    vec2_t const first_origin = first->s.origin2;
    vec2_t const second_origin = second->s.origin2;

    first->collision = second->collision = 16.0f;
    first->unitinfo.MoveSpeed = second->unitinfo.MoveSpeed = 190.0f;
    first->s.model = second->s.model = 1;
    tree->collision = 0.0f;
    gi.LinkEntity(first);
    gi.LinkEntity(second);
    gi.LinkEntity(tree);
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    HARVEST_RANGE = 116.0f;
    HARVEST_SEARCH_RANGE = 1000.0f;
    harvest_start(first, tree);
    harvest_start(second, tree);
    /* If the rear worker is processed first it may queue for the occupied
     * direct step.  Once the front worker advances, the rear worker must resume
     * the same direct route on its next think rather than keeping a side lane. */
    first->currentmove->think(first);
    T_FEQ(first->s.origin2.x, first_origin.x, 0.01f);
    second->currentmove->think(second);
    first->currentmove->think(first);
    T_ASSERT(first->goalentity == tree);
    T_ASSERT(second->goalentity == tree);
    T_ASSERT(first->movement.flow_direct);
    T_ASSERT(second->movement.flow_direct);
    T_ASSERT(first->s.origin2.x > first_origin.x);
    T_ASSERT(first->s.origin2.x < second->s.origin2.x);
    T_ASSERT(fabsf(first->s.origin2.y - first_origin.y) < 2.0f);
    T_ASSERT(second->s.origin2.x > second_origin.x);
    T_ASSERT(fabsf(second->s.origin2.y - second_origin.y) < 2.0f);
}

/* A Peasant already chopping the shared tree is a permanent live-unit blocker
 * for the direct radial approach.  Generic left/right slide selection can make
 * following workers fight over that same line indefinitely.  Harvest uses the
 * resource-worker crowd policy: wait briefly behind same-stream traffic, then
 * take a deterministic bounded pass while preserving live-unit collision. */
TEST(wc3_movement, lumber_same_tree_worker_routes_around_chopper) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *chopper = make_moving_unit(-60.0f, 0.0f);
    edict_t *follower = add_gold_worker(-95.0f, 0.0f);
    edict_t *tree = make_harvest_tree(0.0f, 0.0f, 500.0f);
    float const saved_range = HARVEST_RANGE;
    bool follower_started_chopping = false;

    chopper->collision = follower->collision = 16.0f;
    chopper->unitinfo.MoveSpeed = follower->unitinfo.MoveSpeed = 190.0f;
    chopper->attack1.damagePoint = follower->attack1.damagePoint = 0.01f;
    chopper->s.model = follower->s.model = 1;
    tree->collision = 0.0f;
    gi.LinkEntity(chopper);
    gi.LinkEntity(follower);
    gi.LinkEntity(tree);
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    HARVEST_RANGE = 64.0f;
    harvest_start(chopper, tree);
    harvest_start(follower, tree);

    /* The front worker is already inside chop range and therefore remains a
     * stationary live collision circle while its attack animation is active. */
    chopper->currentmove->think(chopper);
    T_STREQ(chopper->currentmove->animation, "attack");

    FOR_LOOP(i, 24) {
        follower->currentmove->think(follower);
        if (follower->currentmove && !strcmp(follower->currentmove->animation, "attack")) {
            follower_started_chopping = true;
            break;
        }
    }

    T_ASSERT(follower_started_chopping);
    T_ASSERT(follower->goalentity == tree);
    T_ASSERT(Vector2_distance(&follower->s.origin2, &tree->s.origin2) <= HARVEST_RANGE);
    T_ASSERT(Vector2_distance(&follower->s.origin2, &chopper->s.origin2) >=
             follower->collision + chopper->collision - 0.5f);
    T_ASSERT(fabsf(follower->s.origin2.y) <= follower->collision * 6.0f + 0.5f);

    HARVEST_RANGE = saved_range;
}

/* A nearby static detour uses the bounded per-mover accelerator immediately;
 * it must not wait for the destination field to cover the whole pathmap. */
TEST(wc3_movement, nearby_move_starts_on_accelerated_waypoint) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *unit = make_moving_unit(320.0f, 0.0f);
    vec2_t const origin = unit->s.origin2;
    vec2_t dest = {-320.0f, 0.0f};

    FOR_LOOP(y, CELLS)
        pathmap[32 + y * CELLS] = 0x02;
    for (int y = 39; y <= 41; y++)
        pathmap[32 + y * CELLS] = 0;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    unit->collision = 16.0f;
    unit->unitinfo.MoveSpeed = 190.0f;
    unit->s.angle = 0.0f;
    order_move(unit, Waypoint_add(&dest));
    unit->currentmove->think(unit);

    T_EQ(unit->movement.flow_generation, 0);
    T_ASSERT(!unit->movement.flow_direct);
    T_ASSERT(unit->movement.path.valid);
    T_ASSERT(CM_LineIsWalkableForRadius(&origin, &unit->movement.path.waypoint, unit->collision));
    T_ASSERT(Vector2_distance(&unit->s.origin2, &origin) > 0.001f);
    T_FEQ(unit->s.origin.x, unit->s.origin2.x, 0.001f);
    T_FEQ(unit->s.origin.y, unit->s.origin2.y, 0.001f);
    T_STREQ(unit->currentmove->animation, "walk");
}

/* A turn-lagged facing may still be collision-free while pointing away from
 * the route heading.  Movement must use the resolved heading in that case so
 * a short scripted move cannot step past its marker. */
TEST(wc3_movement, turn_lag_does_not_step_away_from_route_heading) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    vec2_t const dest = {-32.0f, 64.0f};
    float before, after;

    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {-1024.0f, -1024.0f}, .max = {1024.0f, 1024.0f}));
    unit->unitinfo.MoveSpeed = 190.0f;
    unit->s.angle = 0.0f;
    order_move(unit, Waypoint_add(&dest));
    before = Vector2_distance(&unit->s.origin2, &dest);
    unit->currentmove->think(unit);
    after = Vector2_distance(&unit->s.origin2, &dest);

    T_ASSERT(after < before);
    T_FEQ(unit->s.origin.x, unit->s.origin2.x, 0.001f);
    T_FEQ(unit->s.origin.y, unit->s.origin2.y, 0.001f);
}

TEST(wc3_movement, turn_lag_facing_must_agree_with_resolved_route_heading) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *unit = make_moving_unit(320.0f, 0.0f);
    vec2_t dest = {-320.0f, 0.0f};

    FOR_LOOP(y, CELLS) pathmap[32 + y * CELLS] = 0x02;
    for (int y = 39; y <= 41; y++) pathmap[32 + y * CELLS] = 0;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f}, .max = {1024.0f, 1024.0f}));
    unit->unitinfo.MoveSpeed = 190.0f;
    unit->s.angle = 0.0f;
    order_move(unit, Waypoint_add(&dest));
    unit->currentmove->think(unit);

    T_ASSERT(Vector2_dot(&(vec2_t){cosf(unit->s.angle), sinf(unit->s.angle)},
                         &(vec2_t){cosf(unit->movement.heading), sinf(unit->movement.heading)}) < 0.0f);
    T_ASSERT(unit->s.origin2.x < 320.0f);
    T_FEQ(unit->s.origin.x, unit->s.origin2.x, 0.001f);
}

/* Retail WC3 does not leave a worker orbiting an unreachable tree buried in a
 * forest.  The clicked tree remains authoritative while a route exists; once
 * the collision-sized flow field reaches its closest legal approach point and
 * that point is still outside chop range, Harvest selects a reachable edge
 * tree and begins chopping it. */
TEST(wc3_movement, lumber_unreachable_clicked_tree_retargets_reachable_edge_tree) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(0.0f, -320.0f);
    edict_t *edge = make_harvest_tree(0.0f, -96.0f, 500.0f);
    edict_t *interior = make_harvest_tree(0.0f, 0.0f, 500.0f);

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->attack1.damagePoint = 0.01f;
    edge->collision = interior->collision = 0.0f;

    /* Seven blocked rows/columns model a dense forest around the clicked
     * interior tree.  With a 16u worker radius the closest legal route goal is
     * outside the forest, still >64u from the interior target but within 64u of
     * the southern edge tree. */
    for (int y = 29; y <= 35; y++) {
        for (int x = 29; x <= 35; x++)
            pathmap[x + y * CELLS] = 0x02;
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    HARVEST_RANGE = 64.0f;
    HARVEST_SEARCH_RANGE = 1000.0f;
    HARVEST_TREE_DAMAGE = 1.0f;
    harvest_start(worker, interior);

    FOR_LOOP(i, 200) {
        worker->currentmove->think(worker);
        CM_ProcessPathJobs(65536);
        if (worker->goalentity == edge &&
            worker->currentmove &&
            !strcmp(worker->currentmove->animation, "attack"))
            break;
    }

    T_ASSERT(worker->goalentity == edge);
    T_ASSERT(worker->secondarygoal == edge);
    T_NOT_NULL(worker->currentmove);
    T_STREQ(worker->currentmove->animation, "attack");
    T_ASSERT(Vector2_distance(&worker->s.origin2, &edge->s.origin2) <= HARVEST_RANGE);
}

TEST(wc3_movement, lumber_tree_dying_during_approach_retargets_immediately) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *dead = make_harvest_tree(400.0f, 0.0f, 100.0f);
    edict_t *live = make_harvest_tree(100.0f, 0.0f, 100.0f);

    HARVEST_RANGE = 64.0f;
    HARVEST_SEARCH_RANGE = 1000.0f;
    harvest_start(worker, dead);
    dead->health.value = 0.0f;
    dead->svflags |= SVF_DEADMONSTER;

    worker->currentmove->think(worker);

    T_ASSERT(worker->goalentity == live);
    T_ASSERT(worker->secondarygoal == live);
}

/* Ahar slots 1=1 (damage/lumber per swing), 2=10 (capacity): 10 swings are
 * needed per trip. Drives the full cooldown+swing cycle. */
TEST(wc3_movement, lumber_worker_takes_ten_swings_per_trip) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 500.0f);
    worker->attack1.damagePoint = 0.01f;
    tree->pain = test_tree_pain; tree->die = test_tree_die;
    tree_pained = 0; tree_died = false;
    HARVEST_RANGE = 64.0f; HARVEST_TREE_DAMAGE = 1.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f; HARVEST_COOLDOWN = 0.01f;
    harvest_start(worker, tree);
    worker->currentmove->think(worker); /* ai_walktree → harvest_swing (within range) */
    /* Drive the first chop. */
    worker->wait = 0.01f;
    worker->currentmove->think(worker); /* ai_chop: lumber=1, tree-=1 */
    /* Cycle through cooldown+swing until capacity fills; expect exactly 9 more chops. */
    FOR_LOOP(i, 15) {
        if (worker->harvested_lumber >= HARVEST_LUMBER_CAPACITY) break;
        harvest_cooldown(worker);           /* anim end: <cap → cooldown state */
        worker->wait = 0.01f;
        worker->currentmove->think(worker); /* ai_cooldown → harvest_swing */
        worker->wait = 0.01f;
        worker->currentmove->think(worker); /* ai_chop */
    }
    T_EQ(tree_pained, 10);
    T_FEQ(worker->harvested_lumber, 10.0f, 0.01f);
    T_FEQ(tree->health.value, 490.0f, 0.01f);
    T_ASSERT(!tree_died);
}

/* A custom/non-even capacity must clamp the final successful chop instead of
 * allowing the worker to carry more lumber than the Harvest capacity. */
TEST(wc3_movement, lumber_final_chop_clamps_to_capacity) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    worker->attack1.damagePoint = 0.01f;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 10.0f;
    HARVEST_LUMBER_CAPACITY = 25.0f;
    HARVEST_COOLDOWN = 0.01f;
    harvest_start(worker, tree);
    worker->currentmove->think(worker);

    FOR_LOOP(i, 3) {
        worker->wait = 0.01f;
        worker->currentmove->think(worker);
        if (i < 2) {
            harvest_cooldown(worker);
            worker->wait = 0.01f;
            worker->currentmove->think(worker);
        }
    }

    T_EQ(worker->harvested_lumber, 25);
    T_FEQ(tree->health.value, 70.0f, 0.01f);
    T_ASSERT(worker->s.renderfx & RF_HAS_LUMBER);
}

/* Harvest only awards carried lumber when the tree can actually take the
 * chop. Invulnerable destructibles reject G_DestructableApplyDamage. */
TEST(wc3_movement, lumber_invulnerable_tree_does_not_award_lumber) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    worker->attack1.damagePoint = 0.01f;
    tree->invulnerable = true;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 10.0f;
    HARVEST_LUMBER_CAPACITY = 25.0f;
    harvest_start(worker, tree);
    worker->currentmove->think(worker);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);

    T_EQ(worker->harvested_lumber, 0);
    T_FEQ(tree->health.value, 100.0f, 0.01f);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
}

/* A worker has one carried-resource presentation.  Starting to collect lumber
 * after gold must replace the gold bag rather than leaving both carry flags set. */
TEST(wc3_movement, lumber_chop_replaces_gold_carry_state) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    worker->attack1.damagePoint = 0.01f;
    worker->harvested_gold = 7;
    worker->s.renderfx |= RF_HAS_GOLD;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 1.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f;

    harvest_start(worker, tree);
    worker->currentmove->think(worker);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);

    T_EQ(worker->harvested_gold, 0);
    T_EQ(worker->harvested_lumber, 1);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));
    T_ASSERT(worker->s.renderfx & RF_HAS_LUMBER);
}

/* Smart-clicking a tree after an interrupted partial lumber trip resumes the
 * same trip and preserves the amount already gathered. */
TEST(wc3_movement, lumber_smart_click_resumes_partial_trip) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    worker->data.UnitAbilities = &harvest_abilities;
    worker->attack1.damagePoint = 0.01f;
    worker->harvested_lumber = 3;
    worker->s.renderfx |= RF_HAS_LUMBER;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 1.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f;

    T_ASSERT(unit_issuetargetorder(worker, "smart", tree));
    T_EQ(worker->harvested_lumber, 3);
    worker->currentmove->think(worker);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);

    T_EQ(worker->harvested_lumber, 4);
    T_ASSERT(worker->s.renderfx & RF_HAS_LUMBER);
}

/* Switching from lumber to gold keeps the lumber carry while travelling and
 * mining.  The first actual gold pickup replaces it atomically. */
TEST(wc3_movement, lumber_smart_click_gold_mine_switches_on_gold_pickup) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);

    worker->data.UnitAbilities = &harvest_abilities;
    worker->harvested_lumber = 5;
    worker->s.renderfx |= RF_HAS_LUMBER;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    HARVEST_GOLD_CAPACITY = 10.0f;

    T_ASSERT(unit_issuetargetorder(worker, "smart", mine));
    T_EQ(worker->harvested_lumber, 5);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(worker->s.renderfx & RF_HAS_LUMBER);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));

    harvestgold_minegold(worker);
    harvestgold_walkback(worker);

    T_EQ(worker->harvested_lumber, 0);
    T_EQ(worker->harvested_gold, 10);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(worker->s.renderfx & RF_HAS_GOLD);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Switching from gold to lumber similarly keeps the gold while approaching
 * the tree.  Only a successful chop replaces the carried gold with lumber. */
TEST(wc3_movement, gold_smart_click_tree_switches_on_successful_chop) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    worker->data.UnitAbilities = &harvest_abilities;
    worker->attack1.damagePoint = 0.01f;
    worker->harvested_gold = 7;
    worker->s.renderfx |= RF_HAS_GOLD;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 1.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f;

    T_ASSERT(unit_issuetargetorder(worker, "smart", tree));
    T_EQ(worker->harvested_gold, 7);
    T_ASSERT(worker->s.renderfx & RF_HAS_GOLD);
    worker->currentmove->think(worker);
    T_EQ(worker->harvested_gold, 7);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);

    T_EQ(worker->harvested_gold, 0);
    T_EQ(worker->harvested_lumber, 1);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));
    T_ASSERT(worker->s.renderfx & RF_HAS_LUMBER);
}

/* A worker already carrying gold honors the clicked mine first.  Reaching the
 * mine redirects the existing load to the nearest gold drop-off without
 * entering/mining, then deposit resumes the originally clicked mine. */
TEST(wc3_movement, gold_smart_click_gold_mine_visits_mine_then_returns_and_resumes) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    uint32_t const old_gold = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD];

    worker->data.UnitAbilities = &harvest_abilities;
    worker->harvested_gold = 7;
    worker->s.renderfx |= RF_HAS_GOLD;
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);

    T_ASSERT(unit_issuetargetorder(worker, "smart", mine));
    T_ASSERT(worker->goalentity == mine);
    T_ASSERT(worker->secondarygoal == mine);
    T_EQ(worker->harvested_gold, 7);
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold);

    /* Reaching the clicked mine redirects the existing load without entering
     * the mine or collecting any additional gold. */
    worker->currentmove->think(worker);
    T_ASSERT(worker->goalentity == hall);
    T_ASSERT(worker->secondarygoal == mine);
    T_EQ(worker->harvested_gold, 7);
    T_EQ(mine->peonsinside, 0);
    T_ASSERT(!S_GoldMineWorkerIsInside(worker));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold);

    /* The return completes immediately in this fixture because the hall is at
     * the worker position, then the original clicked mine becomes the goal. */
    worker->currentmove->think(worker);
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold + 7);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(worker->goalentity == mine);
    T_ASSERT(worker->secondarygoal == mine);
    T_STREQ(worker->currentmove->animation, "walk");
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Resumable routing returns generation 0 until its shared flow job completes.
 * The bounded mover-owned accelerator is intentionally best-effort: a longer
 * detour may exceed its immediate work budget. Three miners must then hold their
 * starting positions instead of walking along stale facing, and resume once the
 * shared collision-sized field becomes available. */
TEST(wc3_movement, gold_three_workers_hold_while_shared_route_is_pending) {
    enum { CELLS = 64, WORKERS = 3 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *mine;
    edict_t *workers[WORKERS];
    vec2_t origin[WORKERS];
    slkTestData_t *rows, *old_abilities;

    /* make_moving_unit() resets the shared entity array for isolated tests.
     * This test needs three workers and their mine alive at the same time, so
     * create the common world once and initialize each worker in-place. */
    reset_entities();
    setup_test_world();
    mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -320.0f, 0.0f);
    workers[0] = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 320.0f, -64.0f);
    workers[1] = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 352.0f,   0.0f);
    workers[2] = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 384.0f,  64.0f);

    /* Block the direct westward line but leave a reachable opening north of
     * the workers so the shared mine route requires a resumable flow field. */
    FOR_LOOP(y, CELLS)
        pathmap[32 + y * CELLS] = 0x02;
    pathmap[32 + 40 * CELLS] = 0;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    old_abilities = install_goldmine_test_data(&rows);
    mine->collision = 128.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(mine);

    FOR_LOOP(i, WORKERS) {
        workers[i]->movetype = MOVETYPE_STEP;
        workers[i]->stand = unit_stand;
        workers[i]->birth = unit_birth;
        workers[i]->die = unit_die;
        workers[i]->collision = 16.0f;
        workers[i]->health.value = workers[i]->health.max_value = 250.0f;
        workers[i]->unitinfo.MoveSpeed = 190.0f;
        workers[i]->s.angle = 0.0f; /* stale facing points east, away from mine */
        unit_stand(workers[i]);
        origin[i] = workers[i]->s.origin2;
        harvest_gold_start(workers[i], mine);
    }

    FOR_LOOP(i, WORKERS) {
        workers[i]->currentmove->think(workers[i]);
        T_FEQ(Vector2_distance(&workers[i]->s.origin2, &origin[i]), 0.0f, 0.001f);
        T_EQ(workers[i]->movement.flow_generation, 0);
        T_ASSERT(!workers[i]->movement.flow_direct);
        T_ASSERT(!workers[i]->movement.path.valid);
    }

    CM_ProcessPathJobs(65536);
    FOR_LOOP(i, WORKERS) {
        workers[i]->currentmove->think(workers[i]);
        T_ASSERT(workers[i]->movement.flow_generation != 0);
        T_ASSERT(!workers[i]->movement.path.valid);
        T_ASSERT(Vector2_distance(&workers[i]->s.origin2, &origin[i]) > 0.001f);
    }

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* A Town Hall is a blocked footprint, not a reachable centre point.  The
 * interaction walker should first take a collision-sized edge lane instead of
 * waiting for a point-flow toward the blocked centre.  This reproduces the
 * Human02 return stall where a Peasant could sit more than 100 units from the
 * footprint until another worker vacated the shared centre-directed lane. */
TEST(wc3_movement, gold_return_prefers_direct_footprint_edge_lane) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -400.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 320.0f, 0.0f);
    pathTex_t *hall_pathtex = movement_make_goldmine_pathtex();
    vec2_t const origin = worker->s.origin2;
    float const before = 192.0f;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->harvested_gold = 10;
    worker->s.renderfx |= RF_HAS_GOLD;
    worker->secondarygoal = mine;
    hall->collision = 64.0f;
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    hall->pathtex = hall_pathtex;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(hall);

    /* 320 world units maps to cell 42 in this fixture.  Mirror the 8x8
     * no-walk centre of movement_make_goldmine_pathtex(). */
    for (int y = 28; y < 36; y++) {
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(harvest_gold_return_to(worker, hall));
    T_FEQ(CM_DistanceToPathingFootprint(hall, &worker->s.origin2), before, 0.01f);
    worker->currentmove->think(worker);

    T_ASSERT(worker->movement.flow_direct);
    T_ASSERT(worker->s.origin2.x > origin.x);
    T_ASSERT(CM_DistanceToPathingFootprint(hall, &worker->s.origin2) < before);
    gi.MemFree(hall_pathtex);
}

/* Local collision can move a returner away from the edge lane that was nearest
 * on the previous think.  Re-select from the current position: retaining one
 * lane for the whole return leg makes packed Peasants steer back across the
 * Town Hall footprint and oscillate around one another. */
TEST(wc3_movement, gold_return_reselects_footprint_edge_after_displacement) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -400.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 320.0f, 0.0f);
    pathTex_t *hall_pathtex = movement_make_goldmine_pathtex();
    vec2_t const displaced = { 640.0f, 160.0f };
    vec2_t expected, expected_dir, actual_dir;
    float step, route_band;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->harvested_gold = 10;
    worker->s.renderfx |= RF_HAS_GOLD;
    worker->secondarygoal = mine;
    hall->collision = 64.0f;
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    hall->pathtex = hall_pathtex;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(hall);

    for (int y = 28; y < 36; y++) {
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(harvest_gold_return_to(worker, hall));
    worker->currentmove->think(worker);

    /* Simulate collision avoidance having displaced this worker to the other
     * side of the drop-off without restarting the Harvest order. */
    worker->s.origin2 = displaced;
    gi.LinkEntity(worker);
    step = unit_movedistance(worker);
    route_band = worker->collision + step +
                 CM_PathCellWorldSize() * 1.41421356237f;
    T_ASSERT(CM_FindApproachPointToFootprintForRadius(
        hall, &worker->s.origin2, route_band, worker->collision, &expected));
    T_ASSERT(CM_LineIsWalkableForRadius(
        &worker->s.origin2, &expected, worker->collision));
    expected_dir = Vector2_sub(&expected, &worker->s.origin2);
    Vector2_normalize(&expected_dir);

    worker->currentmove->think(worker);
    actual_dir = MAKE(vec2_t, cosf(worker->movement.heading),
                               sinf(worker->movement.heading));
    T_ASSERT(Vector2_dot(&expected_dir, &actual_dir) > 0.99f);
    gi.MemFree(hall_pathtex);
}

/* Gold return can miss the shared cache independently of mine approach. The
 * bounded mover route is best-effort; when this long detour exceeds that local
 * accelerator, Return Resources must hold rather than use stale facing, then
 * resume from the shared collision-sized field when its job completes. */
TEST(wc3_movement, gold_return_holds_while_shared_route_is_pending) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(320.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 500.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), -320.0f, 0.0f);
    vec2_t origin;

    FOR_LOOP(y, CELLS)
        pathmap[32 + y * CELLS] = 0x02;
    pathmap[32 + 40 * CELLS] = 0;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.angle = 0.0f; /* stale facing points east, away from hall */
    worker->harvested_gold = 10;
    worker->s.renderfx |= RF_HAS_GOLD;
    worker->secondarygoal = mine;
    hall->collision = 64.0f;
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(hall);

    T_ASSERT(harvest_gold_return_to(worker, hall));
    origin = worker->s.origin2;
    worker->currentmove->think(worker);

    T_FEQ(Vector2_distance(&worker->s.origin2, &origin), 0.0f, 0.001f);
    T_EQ(worker->movement.flow_generation, 0);
    T_ASSERT(!worker->movement.flow_direct);
    T_ASSERT(!worker->movement.path.valid);

    CM_ProcessPathJobs(65536);
    worker->currentmove->think(worker);
    T_ASSERT(worker->movement.flow_generation != 0);
    T_ASSERT(!worker->movement.path.valid);
    T_ASSERT(Vector2_distance(&worker->s.origin2, &origin) > 0.001f);
}

/* Right-click is also the cancel gesture for an active targeted command.
 * Leaving Harvest target mode armed lets the next left-click on an idle worker
 * be consumed as the old target click, so the previous worker group stays
 * selected and a following lumber Smart order retasks that entire group. */
TEST(wc3_movement, harvest_target_mode_right_click_cancel_prevents_stale_group_retask) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *miner1, *miner2, *idle, *tree;
    char tree_number[16];
    cstring_t cancel_command[] = { "smartpoint", "256", "256" };
    cstring_t harvest_command[] = { "smart", tree_number };

    setup_test_world();
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    miner1 = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    miner2 = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 32.0f, 0.0f);
    idle = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 0.0f);
    tree = make_harvest_tree(160.0f, 0.0f, 100.0f);
    miner1->data.UnitAbilities = miner2->data.UnitAbilities = idle->data.UnitAbilities = &harvest_abilities;
    G_SelectEntity(client, miner1);
    G_SelectEntity(client, miner2);
    client->menu.on_entity_selected = harvest_menu_selecttarget;

    G_ClientCommand(clent, 3, cancel_command);

    T_NULL(client->menu.on_entity_selected);
    T_NULL(client->menu.on_location_selected);
    T_NULL(miner1->goalentity);
    T_NULL(miner2->goalentity);

    /* Selection UI rebuilds the portrait/info panel, which is outside this
     * movement test fixture. Once target mode is proven cleared, update the
     * selected set directly and verify the next Smart order cannot reach the
     * old miner group through a stale callback. */
    G_DeselectEntity(client, miner1);
    G_DeselectEntity(client, miner2);
    G_SelectEntity(client, idle);
    T_ASSERT(!G_IsEntitySelected(client, miner1));
    T_ASSERT(!G_IsEntitySelected(client, miner2));
    T_ASSERT(G_IsEntitySelected(client, idle));

    snprintf(tree_number, sizeof(tree_number), "%u", (unsigned)tree->s.number);
    G_ClientCommand(clent, 2, harvest_command);
    T_ASSERT(idle->goalentity == tree);
    T_ASSERT(idle->secondarygoal == tree);
    T_NULL(miner1->goalentity);
    T_NULL(miner2->goalentity);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

/* Entity Smart/right-click uses the same cancel contract as ground Smart. */
TEST(wc3_movement, harvest_target_mode_right_click_entity_cancels_without_order) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *worker, *tree;
    char tree_number[16];
    cstring_t command[] = { "smart", tree_number };

    setup_test_world();
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    tree = make_harvest_tree(160.0f, 0.0f, 100.0f);
    worker->data.UnitAbilities = &harvest_abilities;
    G_SelectEntity(client, worker);
    client->menu.on_entity_selected = harvest_menu_selecttarget;
    snprintf(tree_number, sizeof(tree_number), "%u", (unsigned)tree->s.number);

    G_ClientCommand(clent, 2, command);

    T_NULL(client->menu.on_entity_selected);
    T_NULL(client->menu.on_location_selected);
    T_NULL(worker->goalentity);
    T_NULL(worker->secondarygoal);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

static edict_t *make_smart_destructable(float x, float y,
                                        DestructableData_t const *data,
                                        TARGTYPE targtype) {
    edict_t *dest = G_Spawn();
    dest->class_id = MAKEFOURCC('L','T','0','5');
    dest->data.DestructableData = data;
    if (!dest->destructable) dest->destructable = G_AllocDestructable();
    assert(dest->destructable);
    dest->destructable->placement_solid = true;
    dest->health.value = dest->health.max_value = 500.0f;
    dest->targtype = targtype;
    dest->s.origin2 = (vec2_t){ x, y };
    dest->s.origin.x = x;
    dest->s.origin.y = y;
    return dest;
}

TEST(wc3_movement, smart_unit_target_sends_classic_relationship_indicator) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *unit, *target;
    char target_number[16];
    cstring_t command[] = { "smart", target_number };

    setup_test_world();
    memset(&smart_indicator_capture, 0, sizeof(smart_indicator_capture));
    gi.Write = movement_capture_indicator_write;
    gi.unicast = movement_capture_indicator_unicast;
    G_SetClientConnected(clent, true);
    unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128.0f, 0.0f);
    unit->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
    unit->s.player = target->s.player = client->ps.number;
    unit->movetype = MOVETYPE_STEP; unit->stand = unit_stand; unit_stand(unit);
    G_SelectEntity(client, unit);
    snprintf(target_number, sizeof(target_number), "%u", (unsigned)target->s.number);

    G_ClientCommand(clent, 2, command);

    T_ASSERT(smart_indicator_capture.count >= 4);
    T_EQ(smart_indicator_capture.type[0], PF_BYTE);
    T_EQ(smart_indicator_capture.value[0], svc_temp_entity);
    T_EQ(smart_indicator_capture.type[1], PF_BYTE);
    T_EQ(smart_indicator_capture.value[1], TE_ENTITY_INDICATOR);
    T_EQ(smart_indicator_capture.type[2], PF_LONG);
    T_EQ(smart_indicator_capture.value[2], (int32_t)target->s.number);
    T_EQ(smart_indicator_capture.type[3], PF_LONG);
    T_EQ((uint32_t)smart_indicator_capture.value[3], 0xff00ff00u);
    T_EQ(smart_indicator_capture.recipient, clent);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_movement, smart_without_accepted_unit_target_sends_no_indicator) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent = &g_edicts[0];
    edict_t *target;
    char target_number[16];
    cstring_t command[] = { "smart", target_number };

    setup_test_world();
    memset(&smart_indicator_capture, 0, sizeof(smart_indicator_capture));
    gi.Write = movement_capture_indicator_write;
    gi.unicast = movement_capture_indicator_unicast;
    G_SetClientConnected(clent, true);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128.0f, 0.0f);
    target->svflags |= SVF_MONSTER; target->s.player = 1;
    snprintf(target_number, sizeof(target_number), "%u", (unsigned)target->s.number);

    G_ClientCommand(clent, 2, command);

    T_EQ(smart_indicator_capture.count, 0);
    T_NULL(smart_indicator_capture.recipient);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

/* Entity picking wins over the terrain trace on a bridge. Preserve that
 * traced point so rejected bridge Smart attack semantics can still become the
 * same formation-aware ground move as an ordinary SmartPoint click. */
TEST(wc3_movement, smart_walkable_bridge_falls_back_to_clicked_ground_point) {
    static DestructableData_t const bridge_data = {
        .file = "Doodads/Terrain/WoodBridgeLarge45/WoodBridgeLarge45.mdx",
        .walkable = true,
    };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *worker, *bridge;
    char bridge_number[16];
    cstring_t command[] = { "smart", bridge_number, "192", "64" };

    setup_test_world();
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    worker->collision = 0.0f;
    worker->stand = unit_stand;
    unit_stand(worker);
    bridge = make_smart_destructable(256.0f, 64.0f, &bridge_data, TARG_BRIDGE);
    G_SelectEntity(client, worker);
    snprintf(bridge_number, sizeof(bridge_number), "%u", (unsigned)bridge->s.number);

    G_ClientCommand(clent, 4, command);

    T_NOT_NULL(worker->goalentity);
    T_FEQ(worker->goalentity->s.origin2.x, 192.0f, 0.01f);
    T_FEQ(worker->goalentity->s.origin2.y, 64.0f, 0.01f);
    T_ASSERT(worker->goalentity != bridge);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_movement, smart_nonwalkable_destructable_does_not_fall_back_to_move) {
    static DestructableData_t const wall_data = {
        .file = "Doodads/TestWall.mdx",
        .walkable = false,
    };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *worker, *wall;
    char wall_number[16];
    cstring_t command[] = { "smart", wall_number, "192", "64" };

    setup_test_world();
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    worker->stand = unit_stand;
    unit_stand(worker);
    wall = make_smart_destructable(256.0f, 64.0f, &wall_data, TARG_WALL);
    G_SelectEntity(client, worker);
    snprintf(wall_number, sizeof(wall_number), "%u", (unsigned)wall->s.number);

    G_ClientCommand(clent, 4, command);

    T_NULL(worker->goalentity);
    T_EQ(G_UnitQueuedOrderCount(worker), 0);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_movement, smart_attackable_wall_targets_gate) {
    static DestructableData_t const gate_data = {
        .file = "Doodads/TestGate.mdx",
        .walkable = false,
    };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *attacker, *gate;
    char gate_number[16];
    cstring_t command[] = { "smart", gate_number };

    setup_test_world();
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    attacker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    attacker->stand = unit_stand;
    attacker->attack1.type = ATK_NORMAL;
    attacker->attack1.targetsAllowed = WC3_TARGET_FLAG_WALL;
    unit_stand(attacker);
    gate = make_smart_destructable(256.0f, 64.0f, &gate_data, TARG_WALL);
    G_SelectEntity(client, attacker);
    snprintf(gate_number, sizeof(gate_number), "%u", (unsigned)gate->s.number);

    G_ClientCommand(clent, 2, command);

    T_ASSERT(attacker->goalentity == gate);
    T_EQ(G_UnitQueuedOrderCount(attacker), 0);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_movement, attacking_unreachable_gate_keeps_gate_at_authored_position) {
    enum { CELLS = 16 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    static DestructableData_t const gate_data = {
        .file = "Doodads/TestGate.mdx",
        .walkable = false,
    };
    edict_t *attacker = make_moving_unit(80.0f, 80.0f);
    edict_t *gate = make_smart_destructable(432.0f, 240.0f, &gate_data, TARG_WALL);
    vec2_t const authored_gate_position = gate->s.origin2;
    bool fallback_reached = false;

    for (int y = 0; y < CELLS; ++y)
        pathmap[y * CELLS + 7] = CM_PATHING_UNWALKABLE;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {0.0f, 0.0f}, .max = {512.0f, 512.0f}));
    attacker->collision = 16.0f;
    attacker->unitinfo.MoveSpeed = 80.0f;
    attacker->attack1.type = ATK_NORMAL;
    attacker->attack1.targetsAllowed = WC3_TARGET_FLAG_WALL;
    attacker->attack1.range = 64.0f;
    gi.LinkEntity(attacker);

    T_ASSERT(S_OrderAttack(attacker, gate));
    FOR_LOOP(frame, 200) {
        attacker->currentmove->think(attacker);
        CM_ProcessPathJobs(4096);
        if (attacker->movement.flow_fallback_state == MOVE_FALLBACK_APPLIED) {
            fallback_reached = true;
            break;
        }
    }

    T_ASSERT(fallback_reached);
    FOR_LOOP(frame, 60) {
        attacker->currentmove->think(attacker);
        CM_ProcessPathJobs(4096);
    }
    /* The attacker starts off the gate's row, so only steering reaches the
     * approach point in front of the wall (column 7 spans x 224..256). */
    T_ASSERT(Vector2_distance(&attacker->s.origin2, &attacker->movement.flow_fallback_approach) < 16.0f);
    T_ASSERT(attacker->s.origin2.x < 224.0f - attacker->collision);
    T_FEQ(attacker->movement.flow_fallback_approach.y, authored_gate_position.y, 16.0f);
    T_FEQ(gate->s.origin2.x, authored_gate_position.x, 0.01f);
    T_FEQ(gate->s.origin2.y, authored_gate_position.y, 0.01f);
    T_ASSERT(attacker->goalentity == gate);
}

TEST(wc3_movement, shift_smart_walkable_bridge_queues_clicked_ground_point) {
    static DestructableData_t const bridge_data = {
        .file = "Doodads/Terrain/WoodBridgeLarge45/WoodBridgeLarge45.mdx",
        .walkable = true,
    };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *worker, *bridge;
    vec2_t first = { 64.0f, 0.0f };
    char bridge_number[16];
    cstring_t command[] = { "smart", bridge_number, "192", "64", "queue" };

    setup_test_world();
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    worker->collision = 0.0f;
    worker->stand = unit_stand;
    unit_stand(worker);
    bridge = make_smart_destructable(256.0f, 64.0f, &bridge_data, TARG_BRIDGE);
    G_SelectEntity(client, worker);
    T_ASSERT(G_IssueUnitPointOrder(worker, "move", &first, false,
                                   client->ps.number, 0.0f));
    snprintf(bridge_number, sizeof(bridge_number), "%u", (unsigned)bridge->s.number);

    G_ClientCommand(clent, 5, command);

    T_EQ(G_UnitQueuedOrderCount(worker), 1);
    T_EQ(worker->order_queue.entries[worker->order_queue.head].target_type,
         UNIT_ORDER_TARGET_POINT);
    T_STREQ(worker->order_queue.entries[worker->order_queue.head].order, "move");
    T_FEQ(worker->order_queue.entries[worker->order_queue.head].point.x, 192.0f, 0.01f);
    T_FEQ(worker->order_queue.entries[worker->order_queue.head].point.y, 64.0f, 0.01f);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_movement, smart_walkable_debris_keeps_entity_attack_precedence) {
    static DestructableData_t const debris_data = {
        .file = "Doodads/TestDebris.mdx",
        .walkable = true,
    };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *unit, *debris;
    char debris_number[16];
    cstring_t command[] = { "smart", debris_number, "192", "64" };

    setup_test_world();
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    unit->stand = unit_stand;
    unit_stand(unit);
    unit->attack1.type = ATK_NORMAL;
    unit->attack1.targetsAllowed = 256u; /* debris */
    debris = make_smart_destructable(256.0f, 64.0f, &debris_data, TARG_DEBRIS);
    G_SelectEntity(client, unit);
    snprintf(debris_number, sizeof(debris_number), "%u", (unsigned)debris->s.number);

    G_ClientCommand(clent, 4, command);

    T_ASSERT(unit->goalentity == debris);
    T_EQ(G_UnitQueuedOrderCount(unit), 0);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

/* Reissuing Harvest while already full remembers the requested tree but begins
 * return immediately, so no extra over-capacity chop can occur. */
TEST(wc3_movement, lumber_full_worker_returns_before_new_chop) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(100.0f, 0.0f, 100.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 300.0f, 0.0f);

    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;
    HARVEST_LUMBER_CAPACITY = 10.0f;

    harvest_start(worker, tree);

    T_ASSERT(worker->secondarygoal == tree);
    T_ASSERT(worker->goalentity == hall);
    T_STREQ(worker->currentmove->animation, "walk");
    T_EQ(worker->harvested_lumber, 10);
}

/* The capacity-filling chop must fell the tree before return starts.  After
 * depositing, the worker must reject that dead tree and select the next one. */
TEST(wc3_movement, lumber_lethal_trip_fells_then_selects_next_tree) {
    reset_entities();
    setup_test_world();
    edict_t *worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    worker->movetype = MOVETYPE_STEP; worker->stand = unit_stand; worker->die = unit_die;
    worker->collision = 0.0f; worker->attack1.damagePoint = 0.01f;
    edict_t *tree1 = make_harvest_tree(20.0f, 0.0f, 10.0f);
    tree1->s.model = G_RegisterModel("Doodads\\Terrain\\LordaeronTree\\LordaeronTree0.mdx");
    edict_t *tree2 = make_harvest_tree(30.0f, 0.0f, 500.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    HARVEST_RANGE = 64.0f; HARVEST_TREE_DAMAGE = 1.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f; HARVEST_COOLDOWN = 0.01f; HARVEST_SEARCH_RANGE = 1000.0f;
    msgTrace_t trace = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace));
    harvest_start(worker, tree1);
    worker->currentmove->think(worker); /* enter the first swing */
    FOR_LOOP(i, 10) {
        worker->wait = 0.01f;
        worker->currentmove->think(worker); /* chop */
        harvest_cooldown(worker);           /* cooldown, or return on chop ten */
        if (i < 9) {
            worker->wait = 0.01f;
            worker->currentmove->think(worker); /* start the next swing */
        }
    }
    worker->currentmove->think(worker); /* deposit and select tree2 */
    worker->currentmove->think(worker); /* begin chopping tree2 */
    G_UnsubscribeMessage(trace_message, &trace);

    T_FEQ(tree1->health.value, 0.0f, 0.01f);
    T_ASSERT(tree1->svflags & SVF_DEADMONSTER);
    T_STREQ(tree1->currentmove->animation, "death");
    if (tree1->animation) {
        T_STREQ(tree1->animation->name, "death");
        T_EQ(tree1->s.frame, tree1->animation->interval[0]);
    } else {
        T_EQ(tree1->s.frame, 0);
    }
    T_ASSERT(worker->goalentity == tree2);
    T_ASSERT(worker->secondarygoal == tree2);
    T_EQ(trace.count, 17);
    T_EQ(trace.msg[12].type, GAME_MSG_HARVEST_TREE_FELLED);
    T_EQ(trace.msg[12].target, tree1->s.number);
    T_EQ(trace.msg[13].type, GAME_MSG_HARVEST_RETURN_LUMBER);
    T_EQ(trace.msg[13].target, hall->s.number);
    T_EQ(trace.msg[14].type, GAME_MSG_HARVEST_DEPOSIT_LUMBER);
    T_EQ(trace.msg[15].type, GAME_MSG_HARVEST_RESUME_LUMBER);
    T_EQ(trace.msg[15].target, tree2->s.number);
    T_EQ(trace.msg[16].type, GAME_MSG_HARVEST_START_CHOP);
    T_EQ(trace.msg[16].target, tree2->s.number);
}

/* With no live tree left, depositing lumber ends in stand and emits no false
 * resume transition naming the felled tree. */
TEST(wc3_movement, lumber_deposit_without_live_tree_stops) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 1.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);
    worker->attack1.damagePoint = 0.01f;
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    HARVEST_RANGE = 64.0f; HARVEST_TREE_DAMAGE = 1.0f; HARVEST_LUMBER_CAPACITY = 1.0f;
    msgTrace_t trace = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace));
    harvest_start(worker, tree);
    worker->currentmove->think(worker);
    worker->wait = 0.01f; worker->currentmove->think(worker);
    harvest_cooldown(worker);
    worker->currentmove->think(worker);
    G_UnsubscribeMessage(trace_message, &trace);

    T_ASSERT(worker->goalentity == NULL);
    T_ASSERT(worker->secondarygoal == NULL);
    T_STREQ(worker->currentmove->animation, "stand");
    T_EQ(trace.count, 6);
    T_EQ(trace.msg[4].type, GAME_MSG_HARVEST_RETURN_LUMBER);
    T_EQ(trace.msg[5].type, GAME_MSG_HARVEST_DEPOSIT_LUMBER);
}

/* A manual return may carry lumber without a remembered tree target. */
TEST(wc3_movement, lumber_manual_return_without_tree_stops) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    worker->harvested_lumber = 1;
    worker->s.renderfx |= RF_HAS_LUMBER;
    harvest_walkback(worker);
    worker->currentmove->think(worker);

    T_ASSERT(worker->goalentity == NULL);
    T_ASSERT(worker->secondarygoal == NULL);
    T_EQ(worker->harvested_lumber, 0);
    T_STREQ(worker->currentmove->animation, "stand");
}

/* If the remembered tree dies while the worker is away, replacement-tree
 * selection is centered on that forest rather than the return building. */
TEST(wc3_movement, lumber_dead_previous_tree_searches_near_old_tree) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *old_tree = make_harvest_tree(-400.0f, 0.0f, 100.0f);
    edict_t *forest_tree = make_harvest_tree(-450.0f, 0.0f, 100.0f);
    edict_t *dropoff_tree = make_harvest_tree(50.0f, 0.0f, 100.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);

    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    old_tree->health.value = 0.0f;
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;
    worker->secondarygoal = old_tree;
    HARVEST_SEARCH_RANGE = 1000.0f;

    harvest_walkback(worker);
    worker->currentmove->think(worker);

    T_ASSERT(worker->goalentity == forest_tree);
    T_ASSERT(worker->secondarygoal == forest_tree);
    T_ASSERT(worker->goalentity != dropoff_tree);
    T_EQ(worker->harvested_lumber, 0);
}

/* Wisps do not use the normal chop/carry/drop-off loop. Awha attaches to a
 * tree, leaves the tree intact, and credits authored DataA lumber each Duration
 * interval directly to the owning player. */
TEST(wc3_movement, wisp_harvest_persists_and_credits_periodic_lumber) {
    slkTestData_t *rows = parse_slk_string(slk_wisp_harvest_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    float const old_range = HARVEST_RANGE;
    edict_t * wisp = make_moving_unit(0.0f, 0.0f);
    edict_t * tree = make_harvest_tree(20.0f, 0.0f, 100.0f);
    uint32_t const old_lumber = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER];

    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    HARVEST_RANGE = 128.0f;
    wisp->s.player = 0;
    gi.LinkEntity(wisp);
    gi.LinkEntity(tree);

    T_ASSERT(unit_issuetargetorder(wisp, "smart", tree));
    T_ASSERT(wisp->goalentity == tree);
    wisp->currentmove->think(wisp); /* attach to tree */
    T_ASSERT(wisp->inuse && !M_IsDead(wisp));
    T_ASSERT(wisp->goalentity == tree);
    T_STREQ(wisp->currentmove->animation, "stand lumber");
    T_FEQ(tree->health.value, 100.0f, 0.001f);

    wisp->wait = FRAMETIME / 2000.0f;
    wisp->currentmove->think(wisp); /* first periodic credit */
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], old_lumber + 9);
    T_ASSERT(wisp->inuse && !M_IsDead(wisp));
    T_EQ(wisp->harvested_lumber, 0);
    T_FEQ(tree->health.value, 100.0f, 0.001f);
    T_FEQ(wisp->wait, 1.0f, 0.001f);

    HARVEST_RANGE = old_range;
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_movement, autoharvestlumber_starts_wisp_harvest) {
    slkTestData_t *rows, *old;
    float const old_range = HARVEST_RANGE;
    edict_t *wisp, *tree;

    reset_entities();
    setup_test_world();
    rows = parse_slk_string(slk_wisp_harvest_test_data);
    old = G_SetSLKRows("AbilityData", rows);
    wisp = make_moving_unit(0.0f, 0.0f);
    tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    wisp->s.player = 0;
    HARVEST_RANGE = 128.0f;

    T_ASSERT(unit_issueimmediateorder(wisp, "autoharvestlumber"));
    T_EQ(wisp->goalentity, tree);
    T_ASSERT(wisp->currentmove && wisp->currentmove->proc == CAbilityWispHarvest);

    HARVEST_RANGE = old_range;
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_movement, wisp_harvest_uses_stock_range_and_duration) {
    slkTestData_t *rows = parse_slk_string(slk_wisp_harvest_stock_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    float const old_range = HARVEST_RANGE;
    float const old_search_range = HARVEST_SEARCH_RANGE;
    edict_t *wisp = make_moving_unit(0.0f, 0.0f);
    edict_t *first_tree = make_harvest_tree(32.0f, 0.0f, 100.0f);
    edict_t *next_tree = make_harvest_tree(700.0f, 0.0f, 100.0f);

    /* Make the old general Harvest search radius too short to find next_tree. */
    HARVEST_RANGE = 128.0f;
    HARVEST_SEARCH_RANGE = 100.0f;
    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    wisp->s.player = 0;
    T_ASSERT(unit_issuetargetorder(wisp, "smart", first_tree));
    wisp->currentmove->think(wisp);
    T_ASSERT(wisp->goalentity == first_tree);
    T_FEQ(wisp->wait, 8.0f, 0.001f);

    G_SetHealth(first_tree, 0.0f);
    wisp->currentmove->think(wisp);
    T_ASSERT(wisp->goalentity == next_tree);

    HARVEST_RANGE = old_range;
    HARVEST_SEARCH_RANGE = old_search_range;
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_movement, wisp_harvest_reads_roc_data_columns) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Data11\"\n"
        "C;Y1;X4;K\"Rng1\"\nC;Y1;X5;K\"Dur1\"\n"
        "C;Y2;X1;K\"Awha\"\nC;Y2;X2;K\"Awha\"\nC;Y2;X3;K9\n"
        "C;Y2;X4;K500\nC;Y2;X5;K1\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    float const old_range = HARVEST_RANGE;
    edict_t *wisp = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 100.0f);
    uint32_t const old_lumber = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER];

    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    HARVEST_RANGE = 128.0f;
    wisp->s.player = 0;
    T_ASSERT(unit_issuetargetorder(wisp, "smart", tree));
    wisp->currentmove->think(wisp);
    wisp->wait = FRAMETIME / 2000.0f;
    wisp->currentmove->think(wisp);
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], old_lumber + 9);

    HARVEST_RANGE = old_range;
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_movement, wisp_harvest_rejects_missing_ability_data) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y2;X1;K\"Afoo\"\nC;Y2;X2;K\"Afoo\"\nC;Y2;X3;K7\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *wisp = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    T_ASSERT(!unit_issuetargetorder(wisp, "smart", tree));
    T_ASSERT(!wisp->goalentity);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_movement, wisp_harvest_rejects_missing_duration) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"Rng1\"\n"
        "C;Y2;X1;K\"Awha\"\nC;Y2;X2;K\"Awha\"\nC;Y2;X3;K9\nC;Y2;X4;K500\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *wisp = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    T_ASSERT(!unit_issuetargetorder(wisp, "smart", tree));
    T_ASSERT(!wisp->goalentity);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_movement, wisp_harvest_rejects_missing_range) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"Dur1\"\n"
        "C;Y2;X1;K\"Awha\"\nC;Y2;X2;K\"Awha\"\nC;Y2;X3;K9\nC;Y2;X4;K1\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *wisp = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    T_ASSERT(!unit_issuetargetorder(wisp, "smart", tree));
    T_ASSERT(!wisp->goalentity);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_movement, wisp_harvest_move_leave_releases_its_tree_effect) {
    slkTestData_t *rows = parse_slk_string(slk_wisp_harvest_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    edict_t *wisp = make_moving_unit(0.0f, 0.0f);
    edict_t *effect = alloc_test_unit(MAKEFOURCC('e','f','f','t'), 0.0f, 0.0f);

    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    T_ASSERT(unit_issuetargetorder(wisp, "smart", make_harvest_tree(20.0f, 0.0f, 100.0f)));
    T_ASSERT(wisp->currentmove && wisp->currentmove->proc == CAbilityWispHarvest);
    effect->owner = wisp;
    effect->summon_ability = MAKEFOURCC('A','w','h','a');
    effect->s.flags |= EF_NOT_SELECTABLE;
    unit_stand(wisp);
    T_ASSERT(!effect->inuse);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Warsmash reserves an actively harvested tree to one Wisp. If two Wisps were
 * ordered to the same tree, the later arrival should acquire the nearest free
 * live tree instead of stacking on the occupied target. */
TEST(wc3_movement, wisp_harvest_retargets_when_clicked_tree_is_owned) {
    slkTestData_t *rows = parse_slk_string(slk_wisp_harvest_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    float const old_range = HARVEST_RANGE;
    edict_t * first = make_moving_unit(0.0f, 0.0f);
    edict_t * second = alloc_test_unit(MAKEFOURCC('e','w','s','p'), 0.0f, 20.0f);
    edict_t * tree1 = make_harvest_tree(20.0f, 0.0f, 100.0f);
    edict_t * tree2 = make_harvest_tree(80.0f, 0.0f, 100.0f);

    first->data.UnitAbilities = second->data.UnitAbilities = &wisp_harvest_abilities;
    HARVEST_RANGE = 128.0f;
    first->s.player = second->s.player = 0;
    second->movetype = MOVETYPE_STEP;
    second->stand = unit_stand;
    second->collision = 0.0f;
    second->health.value = second->health.max_value = 120.0f;
    gi.LinkEntity(first); gi.LinkEntity(second); gi.LinkEntity(tree1); gi.LinkEntity(tree2);

    wisp_harvest_start(first, tree1);
    first->currentmove->think(first); /* tree1 becomes actively owned */
    T_ASSERT(first->goalentity == tree1);
    T_STREQ(first->currentmove->animation, "stand lumber");

    wisp_harvest_start(second, tree1);
    second->currentmove->think(second); /* collision-free reservation retarget */
    T_ASSERT(second->goalentity == tree2);
    T_ASSERT(second->goalentity != tree1);
    T_ASSERT(second->inuse && !M_IsDead(second));

    HARVEST_RANGE = old_range;
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Smart-targeting a compatible drop-off while carrying lumber honors the
 * building the player clicked instead of silently choosing another nearer one. */
TEST(wc3_movement, lumber_smart_click_returns_to_clicked_dropoff) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 100.0f, 0.0f);
    edict_t *mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 500.0f, 0.0f);

    worker->data.UnitAbilities = &harvest_abilities;
    hall->s.player = mill->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    make_live_dropoff(mill, &return_lumber_abilities);
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;

    T_ASSERT(unit_issuetargetorder(worker, "smart", mill));
    T_ASSERT(worker->goalentity == mill);
    T_EQ(worker->harvested_lumber, 10);
}

/* A large Town Hall footprint can block the next step before the old +5u
 * lumber deposit tolerance is reached. Deposit at contact plus one simulation
 * step so the worker does not get stuck against the building pathing map. */
TEST(wc3_movement, lumber_return_deposits_at_next_step_contact) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(-400.0f, 0.0f, 100.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 220.0f, 0.0f);
    uint32_t const old_lumber = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER];

    worker->collision = 16.0f; worker->unitinfo.MoveSpeed = 190.0f;
    hall->collision = 192.0f; hall->s.model = 1; hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(worker); gi.LinkEntity(tree); gi.LinkEntity(hall);
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;
    worker->secondarygoal = tree;

    harvest_walkback(worker);
    T_ASSERT(M_DistanceToGoal(worker) > worker->collision + hall->collision + 5.0f);
    T_ASSERT(M_DistanceToGoal(worker) <= worker->collision + hall->collision + unit_movedistance(worker));
    worker->s.renderfx |= RF_HAS_GOLD; /* stale opposite carry tag must not survive deposit */
    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], old_lumber + 10);
    T_EQ(worker->harvested_lumber, 0);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));
    T_ASSERT(worker->goalentity == tree);
}


/* Returning lumber to a building with authored blocking pathing uses the same
 * generic point-route contract as mine entry.  Routing may approach the blocked
 * center, but the resource behavior owns the contact+step completion boundary. */
/* Lumber return uses the authored no-walk footprint as the physical deposit
 * boundary, matching gold return.  A drop-off can have a scalar collision
 * circle smaller than its pathing texture; in that case the worker must not
 * wait for or route toward the blocked model centre after it has already
 * reached the building footprint. */
TEST(wc3_movement, lumber_return_deposits_at_dropoff_footprint_corner) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(170.0f, 170.0f);
    edict_t *tree = make_harvest_tree(-400.0f, 0.0f, 100.0f);
    edict_t *mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 320.0f, 320.0f);
    pathTex_t *mill_pathtex = movement_make_goldmine_pathtex();
    uint32_t const old_lumber = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER];

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    mill->collision = 64.0f; /* deliberately smaller than authored footprint */
    mill->s.model = 1;
    mill->s.player = worker->s.player;
    mill->pathtex = mill_pathtex;
    make_live_dropoff(mill, &return_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(tree);
    gi.LinkEntity(mill);

    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;
    worker->secondarygoal = tree;
    harvest_walkback(worker);

    T_ASSERT(worker->goalentity == mill);
    T_ASSERT(M_DistanceToGoal(worker) >
             worker->collision + mill->collision + unit_movedistance(worker));
    T_ASSERT(CM_DistanceToPathingFootprint(mill, &worker->s.origin2) <=
             worker->collision + unit_movedistance(worker));

    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], old_lumber + 10);
    T_EQ(worker->harvested_lumber, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(worker->goalentity == tree);
    gi.MemFree(mill_pathtex);
}

TEST(wc3_movement, lumber_return_reaches_blocked_townhall_footprint) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(-400.0f, 0.0f, 100.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 320.0f, 0.0f);
    uint32_t const old_lumber = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER];

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    hall->collision = 192.0f;
    hall->s.model = 1;
    hall->movetype = MOVETYPE_NONE;
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(tree);
    gi.LinkEntity(hall);

    /* 12x12 Town Hall footprint centered on world (320,0). */
    for (int y = 26; y < 38; y++) {
        for (int x = 36; x < 48; x++)
            pathmap[x + y * CELLS] = 0x02;
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;
    worker->secondarygoal = tree;
    harvest_walkback(worker);

    FOR_LOOP(i, 80) {
        worker->currentmove->think(worker);
        CM_ProcessPathJobs(65536);
        if (!worker->harvested_lumber) break;
    }

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], old_lumber + 10);
    T_EQ(worker->harvested_lumber, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(worker->goalentity == tree);
}

/* The old training helper checked only dynamic circles and could choose a
 * point inside the producer's baked pathing footprint. This reproduces the
 * Human02 trained-Peasant regression observed while validating resource return. */
TEST(wc3_movement, trained_unit_exit_skips_blocked_producer_footprint) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    enum { FOOT_W = 16, FOOT_H = 16 };
    size_t const pathtex_size = sizeof(pathTex_t) + FOOT_W * FOOT_H * sizeof(color32_t);
    pathTex_t *pathtex;
    edict_t *producer = make_moving_unit(0.0f, 0.0f);
    edict_t *trained = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    vec2_t exit;
    float angle;

    producer->class_id = MAKEFOURCC('h','t','o','w');
    producer->movetype = MOVETYPE_NONE;
    producer->collision = 192.0f;
    trained->collision = 16.0f;

    /* 16x16 no-walk cells centered on the producer model a large authored
     * building footprint. WPM bit 1 is the no-walk flag. */
    for (int y = 24; y < 40; y++) {
        for (int x = 24; x < 40; x++) {
            pathmap[x + y * CELLS] = 0x02;
        }
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    pathtex = gi.MemAlloc(pathtex_size);
    T_NOT_NULL(pathtex);
    memset(pathtex, 0, pathtex_size);
    pathtex->width = FOOT_W;
    pathtex->height = FOOT_H;
    FOR_LOOP(i, FOOT_W * FOOT_H) pathtex->map[i].b = 0xff;
    producer->pathtex = pathtex;

    T_ASSERT(SP_FindUnitExitPosition(producer, trained, &exit, &angle));
    T_ASSERT(CM_PointIsPathableForRadius(&exit, trained->collision));
    T_ASSERT(Vector2_distance(&producer->s.origin2, &exit) > 256.0f);

    producer->pathtex = NULL;
    gi.MemFree(pathtex);
}

/* Dynamic unit circles are also part of legal exit placement. The first
 * deterministic candidate is occupied, so the trained unit must pick another. */
TEST(wc3_movement, trained_unit_exit_skips_dynamic_blocker) {
    edict_t *producer = make_moving_unit(0.0f, 0.0f);
    edict_t *trained = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    edict_t *blocker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -64.0f, -64.0f);
    vec2_t exit;
    float angle;

    producer->movetype = MOVETYPE_NONE;
    trained->collision = 16.0f;
    blocker->movetype = MOVETYPE_STEP;
    blocker->collision = 16.0f;
    blocker->s.model = 1;

    T_ASSERT(SP_FindUnitExitPosition(producer, trained, &exit, &angle));
    T_ASSERT(Vector2_distance(&blocker->s.origin2, &exit) >=
             trained->collision + blocker->collision);
}

/* Completing the head of a multi-unit queue must preserve the next link.
 * unit_stand() clears the completed unit's build pointer, which is also the
 * queue link while that unit is waiting behind the producer. */
TEST(wc3_movement, trained_unit_completion_preserves_remaining_queue) {
    edict_t *producer = make_moving_unit(0.0f, 0.0f);
    edict_t *first = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    edict_t *second = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    UnitBalance_t balance = { .buildTime = 1, .foodUsed = 2, .foodMade = 4 };
    gameClient_t *client = &game.clients[0];

    producer->class_id = MAKEFOURCC('h','t','o','w');
    producer->movetype = MOVETYPE_NONE;
    producer->s.player = first->s.player = second->s.player = client->ps.number;
    first->stand = second->stand = unit_stand;
    first->data.UnitBalance = second->data.UnitBalance = &balance;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    first->health.max_value = second->health.max_value = 100.0f;
    first->health.value = 100.0f;
    second->health.value = 0.0f;
    first->training = second->training = true;
    first->s.renderfx |= RF_HIDDEN;
    second->s.renderfx |= RF_HIDDEN;
    first->build = second;
    producer->build = first;

    ai_train_build(producer);

    T_ASSERT(producer->build == second);
    T_NULL(first->build);
    T_ASSERT(!first->training);
    T_ASSERT(!(first->s.renderfx & RF_HIDDEN));
    T_ASSERT(second->training);
    T_ASSERT(second->s.renderfx & RF_HIDDEN);
    T_FEQ(second->health.value, 0.0f, 0.01f);
    T_EQ(first->food->used, 2);
    T_EQ(first->food->made, 4);
    T_EQ(second->food->used, 2);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 4);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 104);
}

/* A completed unit must remain hidden and queued when no legal exit exists;
 * revealing it on blocked pathing recreates the permanent stuck-unit bug. */
TEST(wc3_movement, trained_unit_waits_when_no_exit_position_exists) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS];
    edict_t *producer = make_moving_unit(0.0f, 0.0f);
    edict_t *trained = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);

    memset(pathmap, 0x02, sizeof(pathmap));
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    producer->class_id = MAKEFOURCC('h','t','o','w');
    producer->movetype = MOVETYPE_NONE;
    producer->build = trained;
    UnitBalance_t balance = { .buildTime = 1 };
    trained->data.UnitBalance = &balance;
    trained->collision = 16.0f;
    trained->health.max_value = 100.0f;
    trained->health.value = 100.0f;
    trained->s.renderfx |= RF_HIDDEN;

    ai_train_build(producer);

    T_ASSERT(producer->build == trained);
    T_ASSERT(trained->s.renderfx & RF_HIDDEN);
    T_FEQ(trained->s.origin2.x, 0.0f, 0.01f);
    T_FEQ(trained->s.origin2.y, 0.0f, 0.01f);
}

/* Lumber return is ability-driven and chooses the nearest compatible
 * same-owner building rather than preferring a Town Hall class. */
TEST(wc3_movement, lumber_return_prefers_nearer_lumber_mill) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 500.0f, 0.0f);
    edict_t *mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 100.0f, 0.0f);
    hall->s.player = mill->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    make_live_dropoff(mill, &return_lumber_abilities);
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;

    harvest_walkback(worker);

    T_ASSERT(worker->goalentity == mill);
    T_FEQ(worker->harvested_lumber, 10.0f, 0.01f);
}

/* Return Resources is unavailable while a structure is under construction.
 * An unfinished War Mill must not become a lumber drop-off merely because its
 * Arlm ability is already present in unit data. */
TEST(wc3_movement, lumber_return_skips_unfinished_lumber_mill) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('o','g','r','e'), 500.0f, 0.0f);
    edict_t *mill = alloc_test_unit(MAKEFOURCC('o','w','a','r'), 100.0f, 0.0f);
    hall->s.player = mill->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    make_live_dropoff(mill, &return_lumber_abilities);
    if (!mill->construction) mill->construction = G_AllocConstruction();
    assert(mill->construction);
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;

    T_ASSERT(!S_CanReturnResourceAt(worker, mill, RETURN_RESOURCE_LUMBER));
    harvest_walkback(worker);

    T_ASSERT(worker->goalentity == hall);
    T_FEQ(worker->harvested_lumber, 10.0f, 0.01f);

    G_FreeConstruction(mill);
    T_ASSERT(S_CanReturnResourceAt(worker, mill, RETURN_RESOURCE_LUMBER));
}

/* The same construction gate applies to gold.  A Town Hall whose Argl data is
 * already loaded must not receive carried gold until construction completes. */
TEST(wc3_movement, gold_return_skips_unfinished_town_hall) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *complete_hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 500.0f, 0.0f);
    edict_t *unfinished_hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 100.0f, 0.0f);
    complete_hall->s.player = unfinished_hall->s.player = worker->s.player;
    make_live_dropoff(complete_hall, &return_gold_lumber_abilities);
    make_live_dropoff(unfinished_hall, &return_gold_lumber_abilities);
    if (!unfinished_hall->construction) unfinished_hall->construction = G_AllocConstruction();
    assert(unfinished_hall->construction);
    S_SetCarriedResource(worker, RETURN_RESOURCE_GOLD, 10);

    T_ASSERT(!S_CanReturnResourceAt(worker, unfinished_hall, RETURN_RESOURCE_GOLD));
    T_ASSERT(S_FindNearestResourceDropoff(worker, RETURN_RESOURCE_GOLD) == complete_hall);
    T_ASSERT(harvest_gold_return_to(worker, S_FindNearestResourceDropoff(worker, RETURN_RESOURCE_GOLD)));
    T_ASSERT(worker->goalentity == complete_hall);
    T_EQ(worker->harvested_gold, 10);
    T_ASSERT(worker->s.renderfx & RF_HAS_GOLD);

    G_FreeConstruction(unfinished_hall);
    T_ASSERT(S_CanReturnResourceAt(worker, unfinished_hall, RETURN_RESOURCE_GOLD));
    T_ASSERT(S_FindNearestResourceDropoff(worker, RETURN_RESOURCE_GOLD) == unfinished_hall);
}

/* If the chosen Lumber Mill dies during the trip, retain the carried lumber
 * and redirect to the nearest remaining compatible return building. */
TEST(wc3_movement, lumber_return_retargets_after_lumber_mill_dies) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 500.0f, 0.0f);
    edict_t *mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 100.0f, 0.0f);
    hall->s.player = mill->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    make_live_dropoff(mill, &return_lumber_abilities);
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;

    harvest_walkback(worker);
    T_ASSERT(worker->goalentity == mill);
    mill->health.value = 0;
    worker->currentmove->think(worker);

    T_ASSERT(worker->goalentity == hall);
    T_FEQ(worker->harvested_lumber, 10.0f, 0.01f);
    T_ASSERT(worker->s.renderfx & RF_HAS_LUMBER);
}

/* The complete gold loop enters, exits carrying gold, deposits it, and resumes mining. */
TEST(wc3_movement, gold_worker_deposits_and_resumes_mining) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 400.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);
    worker->collision = 16.0f; worker->unitinfo.MoveSpeed = 100.0f;
    mine->collision = 128.0f; mine->s.model = 1;
    hall->collision = 64.0f; hall->s.model = 1;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker); gi.LinkEntity(mine); gi.LinkEntity(hall);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    HARVEST_GOLD_CAPACITY = 10.0f;
    uint32_t const old_gold = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD];
    msgTrace_t trace = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace));
    harvest_gold_start(worker, mine);

    FOR_LOOP(i, 100) {
        worker->currentmove->think(worker);
        if (game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD] > old_gold) break;
    }
    G_UnsubscribeMessage(trace_message, &trace);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold + 10);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_ASSERT(worker->secondarygoal == mine);
    T_EQ(trace.count, 5);
    T_EQ(trace.msg[0].type, GAME_MSG_HARVEST_MOVE_GOLD);
    T_EQ(trace.msg[1].type, GAME_MSG_HARVEST_ENTER_MINE);
    T_EQ(trace.msg[2].type, GAME_MSG_HARVEST_RETURN_GOLD);
    T_EQ(trace.msg[3].type, GAME_MSG_HARVEST_DEPOSIT_GOLD);
    T_EQ(trace.msg[4].type, GAME_MSG_HARVEST_RESUME_GOLD);
    FOR_LOOP(i, trace.count)
        T_EQ(trace.msg[i].actor, worker->s.number);
    T_EQ(trace.msg[0].target, mine->s.number);
    T_EQ(trace.msg[1].target, mine->s.number);
    T_EQ(trace.msg[2].target, hall->s.number);
    T_EQ(trace.msg[3].target, hall->s.number);
    T_EQ(trace.msg[4].target, mine->s.number);
    T_EQ(mine->resources, 90);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Gold pickup replaces a prior lumber carry state.  RF_HAS_LUMBER used to
 * survive here, and the renderer checks lumber before gold, so the Peasant
 * continued to display the lumber-carry model while actually carrying gold. */
TEST(wc3_movement, gold_pickup_replaces_lumber_carry_state) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);

    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(mine);
    HARVEST_GOLD_CAPACITY = 10.0f;
    worker->harvested_lumber = 5;
    worker->s.renderfx |= RF_HAS_LUMBER;
    worker->goalentity = worker->secondarygoal = mine;

    harvestgold_minegold(worker);
    harvestgold_walkback(worker);

    T_EQ(worker->harvested_lumber, 0);
    T_EQ(worker->harvested_gold, 10);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(worker->s.renderfx & RF_HAS_GOLD);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* A large Town Hall footprint can block the next step before the old +5u
 * deposit tolerance is reached. The interaction must complete at contact plus
 * one simulation step, just like entering a gold mine. */
TEST(wc3_movement, gold_return_deposits_at_next_step_contact) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -400.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 220.0f, 0.0f);
    uint32_t const old_gold = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD];

    worker->collision = 16.0f; worker->unitinfo.MoveSpeed = 190.0f;
    mine->collision = 128.0f; mine->s.model = 1;
    hall->collision = 192.0f; hall->s.model = 1; hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker); gi.LinkEntity(mine); gi.LinkEntity(hall);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    HARVEST_GOLD_CAPACITY = 10.0f;
    worker->goalentity = mine; worker->secondarygoal = mine;
    harvestgold_minegold(worker);
    harvestgold_walkback(worker);
    T_ASSERT(M_DistanceToGoal(worker) > worker->collision + hall->collision + 5.0f);
    T_ASSERT(M_DistanceToGoal(worker) <= worker->collision + hall->collision + unit_movedistance(worker));
    worker->s.renderfx |= RF_HAS_LUMBER; /* stale opposite carry tag must not survive deposit */
    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold + 10);
    T_EQ(worker->harvested_lumber, 0);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));
    T_ASSERT(worker->goalentity == mine);
    T_EQ(mine->resources, 90);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Return-to-building range must use the authored footprint, not only the
 * building's scalar collision circle.  At a Town Hall corner the Peasant can
 * be one legal step from the no-walk cells while centre distance is still well
 * outside collision+step; gold must deposit at that footprint edge. */
TEST(wc3_movement, gold_return_deposits_at_townhall_footprint_corner) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(170.0f, 170.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -400.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 320.0f, 320.0f);
    pathTex_t *hall_pathtex = movement_make_goldmine_pathtex();
    uint32_t const old_gold = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD];

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    mine->collision = 128.0f;
    mine->s.model = 1;
    hall->collision = 64.0f; /* deliberately smaller than its authored footprint */
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    hall->pathtex = hall_pathtex;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);
    gi.LinkEntity(hall);

    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    HARVEST_GOLD_CAPACITY = 10.0f;
    worker->goalentity = worker->secondarygoal = mine;
    harvestgold_minegold(worker);
    harvestgold_walkback(worker);

    T_ASSERT(worker->goalentity == hall);
    T_ASSERT(M_DistanceToGoal(worker) >
             worker->collision + hall->collision + unit_movedistance(worker));
    T_ASSERT(CM_DistanceToPathingFootprint(hall, &worker->s.origin2) <=
             worker->collision + unit_movedistance(worker));

    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold + 10);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));
    T_ASSERT(worker->goalentity == mine);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
    gi.MemFree(hall_pathtex);
}

/* A lumber-only return ability is incompatible with carried gold even when it
 * is closer than a gold+lumber return building. */
TEST(wc3_movement, gold_return_rejects_nearer_lumber_only_dropoff) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -400.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 500.0f, 0.0f);
    edict_t *mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 100.0f, 0.0f);
    hall->s.player = mill->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    make_live_dropoff(mill, &return_lumber_abilities);
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker); gi.LinkEntity(mine); gi.LinkEntity(hall); gi.LinkEntity(mill);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    HARVEST_GOLD_CAPACITY = 10.0f;
    worker->goalentity = worker->secondarygoal = mine;
    harvestgold_minegold(worker); /* registers worker in mine */

    harvestgold_walkback(worker);

    T_ASSERT(worker->goalentity == hall);
    T_FEQ(worker->harvested_gold, 10.0f, 0.01f);
    T_ASSERT(worker->s.renderfx & RF_HAS_GOLD);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Unsubscription is part of the callback lifetime contract. */
TEST(wc3_movement, gameplay_message_unsubscribe_stops_delivery) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    msgTrace_t trace = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace));
    G_PublishMessage(worker, GAME_MSG_HARVEST_MOVE_GOLD, worker);
    G_UnsubscribeMessage(trace_message, &trace);
    G_PublishMessage(worker, GAME_MSG_HARVEST_ENTER_MINE, worker);
    T_EQ(trace.count, 1);
}

/* Duplicate subscriptions are idempotent, and exhaustion is explicit. */
TEST(wc3_movement, gameplay_message_subscription_capacity_is_bounded) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    msgTrace_t trace[MAX_MESSAGE_SUBSCRIBERS + 1] = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace[0]));
    T_ASSERT(G_SubscribeMessage(trace_message, &trace[0]));
    FOR_LOOP(i, MAX_MESSAGE_SUBSCRIBERS - 1)
        T_ASSERT(G_SubscribeMessage(trace_message, &trace[i + 1]));
    T_ASSERT(!G_SubscribeMessage(trace_message, &trace[MAX_MESSAGE_SUBSCRIBERS]));
    G_PublishMessage(worker, GAME_MSG_HARVEST_MOVE_GOLD, worker);
    FOR_LOOP(i, MAX_MESSAGE_SUBSCRIBERS) {
        T_EQ(trace[i].count, 1);
        G_UnsubscribeMessage(trace_message, &trace[i]);
    }
}

/* -----------------------------------------------------------------------
 * order_move tests
 * --------------------------------------------------------------------- */

TEST(wc3_movement, order_move_sets_goalentity) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    edict_t *wp   = alloc_test_unit(0, 30.0f, 0.0f); /* reuse edict as waypoint */
    order_move(unit, wp);
    T_ASSERT(unit->goalentity == wp);
}

TEST(wc3_movement, order_move_sets_walk_animation) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    edict_t *wp   = alloc_test_unit(0, 30.0f, 0.0f);
    order_move(unit, wp);
    T_NOT_NULL(unit->currentmove);
    T_STREQ(unit->currentmove->animation, "walk");
}

/* -----------------------------------------------------------------------
 * Waypoint_add tests
 * --------------------------------------------------------------------- */

TEST(wc3_movement, waypoint_add_sets_origin) {
    vec2_t dest = {128.0f, 256.0f};
    edict_t *wp = Waypoint_add(&dest);
    T_NOT_NULL(wp);
    T_FEQ(wp->s.origin.x, 128.0f, 0.01f);
    T_FEQ(wp->s.origin.y, 256.0f, 0.01f);
}

/* -----------------------------------------------------------------------
 * unit_movedistance tests
 * --------------------------------------------------------------------- */

TEST(wc3_movement, unit_movedistance_matches_formula) {
    /* unit_movedistance = 10 * speed / FRAMETIME */
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    float expected = 10.0f * G_UnitBalance(MAKEFOURCC('h','p','e','a'))->speed / (float)FRAMETIME;
    T_FEQ(unit_movedistance(unit), expected, 0.01f);
}

TEST(wc3_movement, unit_movedistance_uses_scripted_move_speed) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    unit->unitinfo.MoveSpeed = 300.0f;

    float expected = 10.0f * 300.0f / (float)FRAMETIME;
    T_FEQ(unit_movedistance(unit), expected, 0.01f);
}

/* -----------------------------------------------------------------------
 * M_DistanceToGoal tests
 * --------------------------------------------------------------------- */

TEST(wc3_movement, distance_to_goal_along_x_axis) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    edict_t *wp   = alloc_test_unit(0, 100.0f, 0.0f);
    unit->goalentity = wp;
    T_FEQ(M_DistanceToGoal(unit), 100.0f, 0.01f);
}

TEST(wc3_movement, distance_to_goal_diagonal) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    edict_t *wp   = alloc_test_unit(0, 30.0f, 40.0f); /* 3-4-5 right triangle → 50 */
    unit->goalentity = wp;
    T_FEQ(M_DistanceToGoal(unit), 50.0f, 0.1f);
}

TEST(wc3_movement, distance_to_goal_zero_when_at_goal) {
    edict_t *unit = make_moving_unit(10.0f, 10.0f);
    edict_t *wp   = alloc_test_unit(0, 10.0f, 10.0f);
    unit->goalentity = wp;
    T_FEQ(M_DistanceToGoal(unit), 0.0f, 0.01f);
}

/* Human01 LT05 is a walkable 32x32 destructable above river terrain; ground snapping must retain its deck Z. */
TEST(wc3_movement, ground_unit_stands_on_walkable_bridge_surface) {
    static DestructableData_t const bridge_data = { .walkable = true };
    struct { uint16_t width, height; color32_t map[4]; } bridge_path = { .width = 2, .height = 2 };
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    edict_t *bridge = G_Spawn();
    float const terrain = CM_GetHeightAtPoint(0.0f, 0.0f);

    bridge->class_id = MAKEFOURCC('L', 'T', '0', '5');
    bridge->data.DestructableData = &bridge_data;
    if (!bridge->destructable) bridge->destructable = G_AllocDestructable();
    assert(bridge->destructable);
    bridge->destructable->placement_solid = true;
    bridge->destructable->occluder_height = 32.0f; /* does not offset non-elevator bridges */
    bridge->pathtex = (pathTex_t *)&bridge_path;
    bridge->s.origin = MAKE(vec3_t, 0.0f, 0.0f, terrain + 64.0f);
    G_RegisterGroundSurface(bridge);
    T_ASSERT(bridge->s.flags & EF_GROUND_SURFACE);
    M_CheckGround(unit);
    T_FEQ(unit->s.origin.z, terrain + 64.0f, 0.01f);
    T_FEQ(unit->s.ground_offset, unit->unitinfo.FlyHeight, 0.01f);

    unit->s.origin.x = CM_PathCellWorldSize() * 2.0f;
    M_CheckGround(unit);
    T_FEQ(unit->s.origin.z, CM_GetHeightAtPoint(unit->s.origin.x, unit->s.origin.y), 0.01f);
}

TEST(wc3_movement, elevator_level_controls_ground_unit_height_and_clears_on_exit) {
    static DestructableData_t const elevator_data = { .walkable = true };
    struct { uint16_t width, height; color32_t map[4]; } path = { .width = 2, .height = 2 };
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    edict_t *deck = G_Spawn();
    float const terrain = CM_GetHeightAtPoint(0.0f, 0.0f);

    deck->class_id = MAKEFOURCC('D', 'T', 'r', 'x');
    deck->data.DestructableData = &elevator_data;
    deck->destructable = G_AllocDestructable();
    deck->destructable->placement_solid = true;
    deck->pathtex = (pathTex_t *)&path;
    deck->s.origin = MAKE(vec3_t, 0.0f, 0.0f, terrain);
    G_RegisterGroundSurface(deck);
    FOR_LOOP(level_index, 3) {
        deck->destructable->occluder_height = level_index * 128.0f;
        M_CheckGround(unit);
        T_FEQ(unit->s.origin.z, terrain + level_index * 128.0f, 0.01f);
    }
    unit->collision = 12.0f;
    unit->s.radius = 100.0f; /* selection radius must not extend physical support */
    unit->s.origin.x = CM_PathCellWorldSize() + 8.0f;
    M_CheckGround(unit);
    T_FEQ(unit->s.origin.z, terrain + 256.0f, 0.01f);
    unit->s.origin.x = CM_PathCellWorldSize() + 16.0f;
    M_CheckGround(unit);
    T_FEQ(unit->s.origin.z, CM_GetHeightAtPoint(unit->s.origin.x, unit->s.origin.y), 0.01f);
}

TEST(wc3_movement, rectangular_bridge_support_bounds_follow_quarter_turns) {
    static DestructableData_t const bridge_data = { .walkable = true };
    struct { uint16_t width, height; color32_t map[15]; } bridge_path = { .width = 5, .height = 3 };

    FOR_LOOP(angle, 4) {
        bool const vertical = !(angle & 1);
        float cell, width;
        edict_t *unit = make_moving_unit(0.0f, 0.0f);
        edict_t *bridge = G_Spawn();

        cell = CM_PathCellWorldSize();
        width = (vertical ? 3.0f : 5.0f) * cell;

        bridge->class_id = MAKEFOURCC('Y', 'T', '2', '0');
        bridge->data.DestructableData = &bridge_data;
        if (!bridge->destructable) bridge->destructable = G_AllocDestructable();
        assert(bridge->destructable);
        bridge->destructable->placement_solid = true;
        bridge->pathtex = (pathTex_t *)&bridge_path;
        bridge->s.origin = MAKE(vec3_t, 0.0f, 0.0f, 100.0f);
        bridge->targtype = TARG_BRIDGE;
        bridge->s.angle = angle * (float)M_PI / 2.0f;
        G_RegisterGroundSurface(bridge);

        unit->s.origin.x = width * 0.5f - 1.0f;
        M_CheckGround(unit);
        T_FEQ(unit->s.origin.z, 100.0f, 0.01f);
        unit->s.origin.x = width * 0.5f + 1.0f;
        M_CheckGround(unit);
        T_FEQ(unit->s.origin.z, CM_GetHeightAtPoint(unit->s.origin.x, unit->s.origin.y), 0.01f);
    }
}


TEST(wc3_movement, ground_surface_flag_clears_when_unregistered) {
    static DestructableData_t const bridge_data = { .walkable = true };
    edict_t *bridge = G_Spawn();

    bridge->class_id = MAKEFOURCC('L', 'T', '0', '5');
    bridge->data.DestructableData = &bridge_data;
    if (!bridge->destructable) bridge->destructable = G_AllocDestructable();
    assert(bridge->destructable);
    bridge->destructable->placement_solid = true;

    G_RegisterGroundSurface(bridge);
    T_ASSERT(bridge->s.flags & EF_GROUND_SURFACE);

    G_UnregisterGroundSurface(bridge);
    T_ASSERT(!(bridge->s.flags & EF_GROUND_SURFACE));
}

/* -0.75 tiles is a non-stock Water.slk height (-96 units) that keeps the encoded level exact. */
static void set_uniform_test_water_height(float height) {
    war3mapVertex_t *vertices = (war3mapVertex_t *)world.map->vertices;
    uint16_t encoded;
    CM_W3SetWaterHeight(-0.75f);
    encoded = (uint16_t)(0x2000 + (height + 0.75f * TILE_SIZE) * 4.0f);
    uint32_t const count = world.map->width * world.map->height;
    FOR_LOOP(i, count) vertices[i].waterlevel = encoded;
}

TEST(wc3_movement, snapshot_marks_authored_water_selection_circle_only_on_water_support) {
    static UnitUI_t water_circle_ui = { .selectionCircleOnWater = true };
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    entityState_t snapshot;
    float water_z, ground_z;

    T_NOT_NULL(globals.CustomizeEntity);
    if (!globals.CustomizeEntity) return;

    unit->data.UnitUI = &water_circle_ui;
    unit->s.ground_offset = 5.0f;
    set_uniform_test_water_height(64.0f);
    water_z = CM_GetWaterHeightAtPoint(0.0f, 0.0f);
    unit->s.origin.z = water_z + unit->s.ground_offset;
    snapshot = unit->s;
    globals.CustomizeEntity(0, unit, &snapshot);
    T_ASSERT(snapshot.flags & EF_SELECTION_CIRCLE_ON_WATER);

    /* The same authored unit on land must return to ordinary terrain rings. */
    ground_z = CM_GetHeightAtPoint(0.0f, 0.0f);
    unit->s.origin.z = ground_z + unit->s.ground_offset;
    snapshot = unit->s;
    snapshot.flags |= EF_SELECTION_CIRCLE_ON_WATER;
    globals.CustomizeEntity(0, unit, &snapshot);
    T_ASSERT(!(snapshot.flags & EF_SELECTION_CIRCLE_ON_WATER));

    /* Water support alone does not opt a unit in when the object data says no. */
    water_circle_ui.selectionCircleOnWater = false;
    unit->s.origin.z = water_z + unit->s.ground_offset;
    snapshot = unit->s;
    globals.CustomizeEntity(0, unit, &snapshot);
    T_ASSERT(!(snapshot.flags & EF_SELECTION_CIRCLE_ON_WATER));
    water_circle_ui.selectionCircleOnWater = true;
}

TEST(wc3_movement, fly_height_is_added_to_support_surface) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    float const terrain = CM_GetHeightAtPoint(0.0f, 0.0f);

    unit->unitinfo.FlyHeight = 300.0f;
    M_CheckGround(unit);

    T_FEQ(unit->s.origin.z, terrain + 300.0f, 0.01f);
    T_FEQ(unit->s.ground_offset, 300.0f, 0.01f);
}

TEST(wc3_movement, flyer_uses_water_surface_before_fly_height) {
    static UnitData_t const fly_data = { .moveTypeName = "fly" };
    edict_t *unit = make_moving_unit(0.0f, 0.0f);

    unit->data.UnitData = &fly_data;
    unit->unitinfo.FlyHeight = 300.0f;
    set_uniform_test_water_height(64.0f);
    M_CheckGround(unit);

    T_FEQ(unit->s.origin.z, 364.0f, 0.01f);
}

TEST(wc3_movement, float_unit_uses_water_surface_and_ignores_bridge) {
    static UnitData_t const float_data = { .moveTypeName = "float" };
    static DestructableData_t const bridge_data = { .walkable = true };
    struct { uint16_t width, height; color32_t map[4]; } bridge_path = { .width = 2, .height = 2 };
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    edict_t *bridge = G_Spawn();

    unit->data.UnitData = &float_data;
    set_uniform_test_water_height(32.0f);
    bridge->data.DestructableData = &bridge_data;
    if (!bridge->destructable) bridge->destructable = G_AllocDestructable();
    assert(bridge->destructable);
    bridge->destructable->placement_solid = true;
    bridge->pathtex = (pathTex_t *)&bridge_path;
    bridge->s.origin = MAKE(vec3_t, 0.0f, 0.0f, 96.0f);
    G_RegisterGroundSurface(bridge);
    M_CheckGround(unit);

    T_FEQ(unit->s.origin.z, 32.0f, 0.01f);
}

TEST(wc3_movement, tileset_water_slk_height_places_the_water_surface) {
    cstring_t slk =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"waterID\"\nC;Y1;X2;K\"height\"\n"
        "C;Y2;X1;K\"OSha\"\nC;Y2;X2;K-1.5\n"
        "C;Y3;X1;K\"LSha\"\nC;Y3;X2;K-0.7\nE\n";
    slkTestData_t *rows = parse_slk_string(slk);
    slkTestData_t *saved = G_SetSLKRows("WaterData", rows);
    mapInfo_t info = { .mainGroundType = 'O' };
    war3mapVertex_t *vertices;
    float raw_level;

    make_moving_unit(0.0f, 0.0f);
    vertices = (war3mapVertex_t *)world.map->vertices;
    FOR_LOOP(i, world.map->width * world.map->height) vertices[i].waterlevel = 0x2000 + 4 * 256;
    raw_level = 256.0f;

    /* Outland's Abyss surface sits 1.5 tiles below the authored W3E level; Lordaeron's 0.7. */
    G_ApplyTilesetWaterHeight(&info);
    T_FEQ(CM_W3WaterHeight(), -1.5f, 0.0001f);
    T_FEQ(CM_GetWaterHeightAtPoint(0.0f, 0.0f), raw_level - 192.0f, 0.01f);
    info.mainGroundType = 'L';
    G_ApplyTilesetWaterHeight(&info);
    T_FEQ(CM_GetWaterHeightAtPoint(0.0f, 0.0f), raw_level - 89.6f, 0.01f);
    info.mainGroundType = 'Q'; /* No row: reported, and the raw W3E level is used. */
    G_ApplyTilesetWaterHeight(&info);
    T_FEQ(CM_GetWaterHeightAtPoint(0.0f, 0.0f), raw_level, 0.01f);

    G_SetSLKRows("WaterData", saved); free_slk_rows(rows);
}

/* WPM water stays unwalkable; only the explicitly passable bridge lane may connect its banks. */
TEST(wc3_movement, water_is_blocked_except_at_authored_bridge_lane) {
    uint8_t pathmap[15] = { 0 };
    vec2_t const from = { 0.5f, 1.5f }, target = { 4.5f, 1.5f };

    pathmap[2] = pathmap[12] = 2;
    setup_test_pathmap(5, 3, pathmap);
    T_ASSERT(CM_LineIsWalkable(&from, &target));
    pathmap[7] = 2;
    setup_test_pathmap(5, 3, pathmap);
    T_ASSERT(!CM_LineIsWalkable(&from, &target));
}

/* -----------------------------------------------------------------------
 * ai_walk / movement frame tests
 *
 * ai_walk is static inside s_move.c; it is accessed via the think
 * function-pointer stored in move_move_walk.  After calling order_move
 * we invoke ent->currentmove->think() to simulate one game frame.
 * ===================================================================== */

TEST(wc3_movement, unit_moves_closer_to_goal_after_one_frame) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    /* Place waypoint within NAVI_THRESHOLD so direct vector math is used
     * and we don't need the heatmap mock to return a meaningful direction. */
    vec2_t dest = {40.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);
    T_NOT_NULL(unit->currentmove);
    T_NOT_NULL(unit->currentmove->think);

    float dist_before = M_DistanceToGoal(unit);
    unit->currentmove->think(unit);
    float dist_after = M_DistanceToGoal(unit);

    T_ASSERT(dist_after < dist_before);
}

TEST(wc3_movement, unit_reaches_goal_and_transitions_to_stand) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    /* Distance = 40, move_distance ≈ 27.  After two frames the unit
     * should have arrived (40 - 27 = 13 < 27) and called stand(). */
    vec2_t dest = {40.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    /* Run up to 10 frames — should arrive well within that. */
    for (int i = 0; i < 10; i++) {
        if (!unit->currentmove || !unit->currentmove->think) break;
        if (strcmp(unit->currentmove->animation, "walk") != 0) break;
        unit->currentmove->think(unit);
    }

    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_movement, unit_position_changes_after_move_frame) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    vec2_t dest = {40.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    float x0 = unit->s.origin2.x;
    unit->currentmove->think(unit);

    /* Unit must have moved in the X direction. */
    T_ASSERT(unit->s.origin2.x > x0);
}

/* Route generation must expand obstacles by the mover radius, matching the
 * move-time collision test.  The point route hugs this wall too closely; a
 * Peasant-sized route has room to detour above it and reach the destination. */
TEST(wc3_movement, move_order_detours_with_unit_collision_radius) {
    enum { CELLS = 16 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *unit = make_moving_unit(80.0f, 240.0f);
    vec2_t dest = {432.0f, 240.0f};

    for (int y = 3; y <= 12; y++)
        pathmap[y * CELLS + 7] = 2;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {0.0f, 0.0f}, .max = {512.0f, 512.0f}));
    unit->collision = 16.0f;
    unit->unitinfo.MoveSpeed = 80.0f;
    gi.LinkEntity(unit);
    order_move(unit, Waypoint_add(&dest));

    for (int frame = 0; frame < 200 && unit->currentmove->think; frame++) {
        unit->currentmove->think(unit);
        CM_ProcessPathJobs(4096);
        T_ASSERT(CM_PointIsPathableForRadius(&unit->s.origin2, unit->collision));
    }

    T_STREQ(unit->currentmove->animation, "stand");
    T_FEQ(Vector2_distance(&unit->s.origin2, &dest), 0.0f, 0.01f);
}

/* A click in another static connected component cannot be reached.  Retail
 * movement still advances as far as collision permits, then settles at the
 * closest boundary instead of freezing at the order origin or walking forever. */
TEST(wc3_movement, unreachable_move_settles_at_closest_boundary) {
    enum { CELLS = 16 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *unit = make_moving_unit(80.0f, 240.0f);
    vec2_t const start = unit->s.origin2;
    vec2_t dest = {432.0f, 240.0f};

    for (int y = 0; y < CELLS; y++)
        pathmap[y * CELLS + 7] = 2;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {0.0f, 0.0f}, .max = {512.0f, 512.0f}));
    unit->collision = 16.0f;
    unit->unitinfo.MoveSpeed = 80.0f;
    gi.LinkEntity(unit);
    order_move(unit, Waypoint_add(&dest));

    for (int frame = 0; frame < 200 && unit->currentmove->think; frame++) {
        unit->currentmove->think(unit);
        CM_ProcessPathJobs(4096);
        T_ASSERT(CM_PointIsPathableForRadius(&unit->s.origin2, unit->collision));
    }

    T_STREQ(unit->currentmove->animation, "stand");
    T_ASSERT(unit->s.origin2.x > start.x);
    T_ASSERT(unit->s.origin2.x < 7.0f * 32.0f);
    T_ASSERT(Vector2_distance(&unit->s.origin2, &dest) < Vector2_distance(&start, &dest));
}

/* Immobile is a single movement/facing contract, not just a command-menu filter. */
TEST(wc3_movement, immobile_unit_neither_moves_nor_rotates) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    edict_t *wp = alloc_test_unit(0, 100.0f, 100.0f);
    vec2_t const origin = unit->s.origin2;
    float const angle = unit->s.angle;
    unit->aiflags |= AI_IMMOBILE;
    unit->goalentity = wp;

    unit_changeangle(unit);
    unit_moveindirection(unit);

    T_FEQ(unit->s.origin2.x, origin.x, 0.01f);
    T_FEQ(unit->s.origin2.y, origin.y, 0.01f);
    T_FEQ(unit->s.angle, angle, 0.01f);
}

TEST(wc3_movement, propwin_turns_in_place_until_inside_authored_window) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    UnitData_t data = *unit->data.UnitData;
    vec2_t const target = {-100.0f, 0.0f};
    vec2_t const origin = unit->s.origin2;

    data.turnRate = 0.1f;
    data.propWin = 10.0f;
    unit->data.UnitData = &data;
    unit->unitinfo.PropWindow = DEG2RAD(data.propWin);
    unit->unitinfo.MoveSpeed = 100.0f;
    unit->s.angle = 0.0f;

    unit_changeangle_towards_point(unit, &target);
    unit_moveindirection(unit);

    T_FEQ(unit->s.origin2.x, origin.x, 0.001f);
    T_FEQ(unit->s.origin2.y, origin.y, 0.001f);
    T_ASSERT(fabsf(unit->s.angle) > 0.01f);
}

TEST(wc3_movement, propwin_turning_keeps_stand_animation_advancing) {
    static animation_t const stand = { .name = "Stand", .interval = { 10, 5000 } };
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    vec2_t target = {-100.0f, 0.0f};
    UnitData_t data = *unit->data.UnitData;
    data.turnRate = 0.1f;
    data.propWin = 10.0f;
    unit->data.UnitData = &data;
    unit->unitinfo.PropWindow = DEG2RAD(data.propWin);
    unit->unitinfo.MoveSpeed = 100.0f;
    unit->goalentity = alloc_test_unit(0, target.x, target.y);
    unit->s.angle = 0.0f;
    unit_issueorder(unit, "move", &target);
    unit->animation = &stand;
    strlcpy(unit->animation_request, "stand", sizeof(unit->animation_request));
    unit->s.frame = stand.interval[0];

    monster_think(unit);
    T_STREQ(unit->animation_request, "stand");
    uint32_t const stand_frame = unit->s.frame;
    monster_think(unit);
    T_STREQ(unit->animation_request, "stand");
    T_ASSERT(unit->s.frame > stand_frame);
}

TEST(wc3_movement, propwin_large_window_allows_translation_while_turning) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    UnitData_t data = *unit->data.UnitData;
    vec2_t const target = {-100.0f, 0.0f};
    vec2_t const origin = unit->s.origin2;

    data.turnRate = 0.1f;
    data.propWin = 180.0f;
    unit->data.UnitData = &data;
    unit->unitinfo.PropWindow = DEG2RAD(data.propWin);
    unit->unitinfo.MoveSpeed = 100.0f;
    unit->s.angle = 0.0f;

    unit_changeangle_towards_point(unit, &target);
    unit_moveindirection(unit);

    T_ASSERT(Vector2_distance(&unit->s.origin2, &origin) > 0.001f);
}

TEST(wc3_movement, propwin_uses_mutable_runtime_unit_value) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    UnitData_t data = *unit->data.UnitData;
    vec2_t const target = {-100.0f, 0.0f};
    vec2_t const origin = unit->s.origin2;

    data.turnRate = 0.1f;
    data.propWin = 180.0f;
    unit->data.UnitData = &data;
    unit->unitinfo.PropWindow = DEG2RAD(10.0f);
    unit->unitinfo.MoveSpeed = 100.0f;
    unit->s.angle = 0.0f;

    unit_changeangle_towards_point(unit, &target);
    unit_moveindirection(unit);

    T_FEQ(unit->s.origin2.x, origin.x, 0.001f);
    T_FEQ(unit->s.origin2.y, origin.y, 0.001f);
}

TEST(wc3_movement, zero_propwin_blocks_translation) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    UnitData_t data = *unit->data.UnitData;
    vec2_t const target = {-100.0f, 0.0f};
    vec2_t const origin = unit->s.origin2;

    unit->data.UnitData = &data;
    unit->unitinfo.PropWindow = 0.0f;
    unit->unitinfo.MoveSpeed = 100.0f;
    unit->s.angle = 0.0f;
    unit_changeangle_towards_point(unit, &target);
    unit_moveindirection(unit);
    T_FEQ(unit->s.origin2.x, origin.x, 0.001f);
    T_FEQ(unit->s.origin2.y, origin.y, 0.001f);
}

TEST(wc3_movement, immobile_unit_rejects_ground_move_order) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    vec2_t dest = {100.0f, 0.0f};
    unit->aiflags |= AI_IMMOBILE;

    T_ASSERT(!unit_issueorder(unit, "move", &dest));
    T_NULL(unit->goalentity);
    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_movement, unit_does_not_overshoot_goal) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    vec2_t dest = {40.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    /* Run frames until the unit stands. */
    for (int i = 0; i < 20; i++) {
        if (!unit->currentmove || !unit->currentmove->think) break;
        if (strcmp(unit->currentmove->animation, "walk") != 0) break;
        unit->currentmove->think(unit);
    }

    /* After reaching the goal the unit should be exactly at the waypoint,
     * which keeps scripted cutscene units from visibly stopping short. */
    float dist = M_DistanceToGoal(unit);
    T_FEQ(dist, 0.0f, 0.01f);
}

TEST(wc3_movement, group_move_assigns_distinct_reserved_destinations) {
    reset_entities();
    edict_t *clent = alloc_test_unit(0, 0.0f, 0.0f);
    clent->client = &game.clients[0];

    edict_t *a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    edict_t *b = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 20.0f, 0.0f);
    edict_t *c = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 20.0f);
    edict_t *units[] = { a, b, c };

    FOR_LOOP(i, 3) {
        units[i]->collision = 16.0f;
        units[i]->selected = 1 << clent->client->ps.number;
        units[i]->stand = unit_stand;
        unit_stand(units[i]);
    }

    vec2_t dest = {100.0f, 100.0f};
    T_ASSERT(move_selectlocation(clent, &dest));

    T_NOT_NULL(a->goalentity);
    T_NOT_NULL(b->goalentity);
    T_NOT_NULL(c->goalentity);
    T_NOT_NULL(a->goalentity->secondarygoal);
    T_ASSERT(a->goalentity->secondarygoal == b->goalentity->secondarygoal);
    T_ASSERT(a->goalentity->secondarygoal == c->goalentity->secondarygoal);
    T_ASSERT(Vector2_distance(&a->goalentity->s.origin2, &b->goalentity->s.origin2) >= 32.0f);
    T_ASSERT(Vector2_distance(&a->goalentity->s.origin2, &c->goalentity->s.origin2) >= 32.0f);
    T_ASSERT(Vector2_distance(&b->goalentity->s.origin2, &c->goalentity->s.origin2) >= 32.0f);
}

TEST(wc3_movement, group_move_ignores_selected_buildings) {
    reset_entities();
    edict_t *clent = alloc_test_unit(0, 0.0f, 0.0f);
    clent->client = &game.clients[0];

    edict_t *building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    edict_t *peasant = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 20.0f, 0.0f);

    building->collision = 64.0f;
    building->aiflags |= AI_IMMOBILE;
    building->selected = 1 << clent->client->ps.number;
    building->stand = unit_stand;
    unit_stand(building);

    peasant->collision = 16.0f;
    peasant->selected = 1 << clent->client->ps.number;
    peasant->stand = unit_stand;
    unit_stand(peasant);

    vec2_t dest = {100.0f, 100.0f};
    T_ASSERT(move_selectlocation(clent, &dest));

    T_NULL(building->goalentity);
    T_NOT_NULL(peasant->goalentity);
}

/* A mixed-speed group travels at its slowest member's speed so it stays
 * together instead of stringing out (WC3 group movement). */
TEST(wc3_movement, group_move_travels_at_slowest_member_speed) {
    reset_entities();
    edict_t *clent = alloc_test_unit(0, 0.0f, 0.0f);
    clent->client = &game.clients[0];

    edict_t *fast = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    edict_t *slow = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 20.0f, 0.0f);
    fast->unitinfo.MoveSpeed = 300.0f;
    slow->unitinfo.MoveSpeed = 100.0f;
    edict_t *units[] = { fast, slow };
    FOR_LOOP(i, 2) {
        units[i]->collision = 16.0f;
        units[i]->selected = 1 << clent->client->ps.number;
        units[i]->stand = unit_stand;
        unit_stand(units[i]);
    }

    vec2_t dest = {400.0f, 0.0f};
    T_ASSERT(move_selectlocation(clent, &dest));

    /* Both units adopt the slowest member's speed for the group move... */
    T_FEQ(fast->movement.group_speed, 100.0f, 0.01f);
    T_FEQ(slow->movement.group_speed, 100.0f, 0.01f);
    /* ...so the fast unit's per-frame travel is capped to the slow speed. */
    T_FEQ(unit_movedistance(fast), 10.0f * 100.0f / (float)FRAMETIME, 0.01f);
}

/* A lone unit keeps its own speed (no group cap). */
TEST(wc3_movement, single_unit_move_keeps_own_speed) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    unit->unitinfo.MoveSpeed = 300.0f;
    vec2_t dest = {200.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    T_FEQ(unit->movement.group_speed, 0.0f, 0.01f);
    T_FEQ(unit_movedistance(unit), 10.0f * 300.0f / (float)FRAMETIME, 0.01f);
}

TEST(wc3_movement, plain_move_uses_collision_sized_static_route) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *unit = make_moving_unit(-320.0f, 0.0f);
    vec2_t dest = {320.0f, 0.0f};

    unit->collision = 16.0f; /* one 32u path-cell radius in this fixture */
    unit->unitinfo.MoveSpeed = 190.0f;

    /* A one-cell opening is traversable by a point route but not by this
     * mover's 3x3 collision footprint.  Plain move must use the latter. */
    for (int y = 0; y < CELLS; y++)
        pathmap[32 + y * CELLS] = 0x02;
    pathmap[32 + 32 * CELLS] = 0;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(unit_issueorder(unit, "move", &dest));
    unit->currentmove->think(unit); /* queues the resumable radius field */
    CM_ProcessPathJobs(65536);
    unit->currentmove->think(unit); /* completed field retargets the private waypoint */

    T_STREQ(unit->currentmove->animation, "walk");
    T_ASSERT(!unit->movement.flow_unreachable);
    T_ASSERT(unit->goalentity->s.origin2.x > unit->s.origin2.x);
    T_ASSERT(unit->goalentity->s.origin2.x < 0.0f);
}

TEST(wc3_movement, blocked_move_keeps_order_alive_away_from_goal) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    vec2_t origin = unit->s.origin2;
    vec2_t dest = {400.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    /* Budget exceeds MOVE_BLOCKED_FRAMES.  A distant plain move must remain
     * active: another unit may be the temporary blocker and retail keeps the
     * right-click order alive until the route can make progress. */
    for (int i = 0; i < 30; i++) {
        if (!unit->currentmove || strcmp(unit->currentmove->animation, "walk") != 0) {
            break;
        }
        unit->currentmove->think(unit);
        unit->s.origin2 = origin;
        unit->s.origin.x = origin.x;
        unit->s.origin.y = origin.y;
        unit->bounds.min.x = unit->s.origin2.x - unit->collision;
    unit->bounds.min.y = unit->s.origin2.y - unit->collision;
    unit->bounds.max.x = unit->s.origin2.x + unit->collision;
    unit->bounds.max.y = unit->s.origin2.y + unit->collision;
    }

    T_STREQ(unit->currentmove->animation, "walk");
}

TEST(wc3_movement, attack_chase_progresses_with_captured_tower_corridor) {
    enum { PATH_W = 352, PATH_H = 160 };
    static uint8_t pathmap[PATH_W * PATH_H];
    vec2_t const start = { 4652.822f, -1692.506f };
    vec2_t const tower_pos = { 4736.0f, -1792.0f };
    vec2_t const target_pos = { -512.0f, -4352.0f };
    box2_t const bounds = { .min = {-6144.0f, -4608.0f}, .max = {5120.0f, 512.0f} };
    edict_t *attacker, *tower, *corridor_tower, *target;
    float start_distance;

    memset(pathmap, 0, sizeof(pathmap));
    /* The dump's 0x42 tower cells occupy world cells x=146..149, y=-55..-57.
     * These are the observed static cells; all other cells are deliberately open. */
    for (int y = -57; y <= -55; y++)
        for (int x = 146; x <= 149; x++)
            pathmap[(x + 192) + (y + 144) * PATH_W] = 0x42;
    /* The follow-up route trace exposed another tower directly ahead of the
     * unit: about 256 units along the goal line and 20 units off its center. */
    for (int y = -57; y <= -54; y++)
        for (int x = 136; x <= 140; x++)
            pathmap[(x + 192) + (y + 144) * PATH_W] = 0x42;

    attacker = make_moving_unit(start.x, start.y);
    attacker->class_id = MAKEFOURCC('h','f','o','o');
    attacker->s.player = 0;
    attacker->collision = 31.0f;
    attacker->attack1.type = ATK_NORMAL;
    attacker->attack1.range = 100.0f;
    attacker->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;

    tower = alloc_test_unit(MAKEFOURCC('h','c','t','w'), tower_pos.x, tower_pos.y);
    tower->s.player = 0;
    tower->collision = 64.0f;
    tower->aiflags |= AI_IMMOBILE;
    tower->runtime.flags |= UNIT_BALANCE_BUILDING;
    tower->bounds = (box2_t){
        .min = {tower_pos.x - tower->collision, tower_pos.y - tower->collision},
        .max = {tower_pos.x + tower->collision, tower_pos.y + tower->collision},
    };
    gi.LinkEntity(tower);

    corridor_tower = alloc_test_unit(MAKEFOURCC('n','d','g','t'), 4416.0f, -1792.0f);
    corridor_tower->s.player = 7;
    corridor_tower->collision = 64.0f;
    corridor_tower->aiflags |= AI_IMMOBILE;
    corridor_tower->runtime.flags |= UNIT_BALANCE_BUILDING;
    corridor_tower->bounds = (box2_t){
        .min = {corridor_tower->s.origin2.x - corridor_tower->collision,
                corridor_tower->s.origin2.y - corridor_tower->collision},
        .max = {corridor_tower->s.origin2.x + corridor_tower->collision,
                corridor_tower->s.origin2.y + corridor_tower->collision},
    };
    gi.LinkEntity(corridor_tower);

    target = alloc_test_unit(MAKEFOURCC('U','k','t','l'), target_pos.x, target_pos.y);
    target->s.player = 6;
    target->targtype = TARG_GROUND;
    target->collision = 32.0f;
    target->health.value = target->health.max_value = 1000.0f;
    gi.LinkEntity(target);

    CM_SetupTestPathmap(PATH_W, PATH_H, pathmap);
    CM_SetupTestWorldBounds(&bounds);
    T_ASSERT(S_OrderAttack(attacker, target));
    T_ASSERT(attacker->currentmove && attacker->currentmove->proc == CAbilityAttack);
    start_distance = Vector2_distance(&attacker->s.origin2, &target->s.origin2);

    for (int i = 0; i < 240; i++) {
        CM_ProcessPathJobs(4096);
        if (attacker->currentmove && attacker->currentmove->think)
            attacker->currentmove->think(attacker);
    }

    T_ASSERT(Vector2_distance(&attacker->s.origin2, &target->s.origin2) < start_distance - 1000.0f);
    T_ASSERT(attacker->goalentity == target);
    T_ASSERT(Vector2_distance(&attacker->s.origin2, &target->s.origin2) <=
        attacker->attack1.range + attacker->collision + target->collision + 16.0f);
}

TEST(wc3_movement, attack_chase_waits_through_competing_route_jobs_then_resumes) {
    enum { PATH_W = 256, PATH_H = 256, COMPETING_JOBS = 24 };
    static uint8_t pathmap[PATH_W * PATH_H];
    box2_t const bounds = { .min = {-4096.0f, -4096.0f}, .max = {4096.0f, 4096.0f} };
    edict_t *attacker, *target, *other_goals[COMPETING_JOBS];
    cmPathJobStatus_t path_job;
    vec2_t const start = {-1500.0f, 0.0f};
    vec2_t const target_pos = {1500.0f, 0.0f};
    vec2_t const start_origin = start;
    float start_distance;
    bool resumed = false;
    int next_competing_job = 1;

    memset(pathmap, 0, sizeof(pathmap));
    /* A tower footprint makes the attack target's center route require the
     * shared flow field, matching the long-range CmdAttack trace. */
    for (int y = 126; y <= 132; y++)
        for (int x = 126; x <= 130; x++)
            pathmap[x + y * PATH_W] = 0x42;
    attacker = make_moving_unit(start.x, start.y);
    attacker->s.player = 0;
    attacker->collision = 31.0f;
    attacker->unitinfo.MoveSpeed = 300.0f;
    attacker->attack1.type = ATK_NORMAL;
    attacker->attack1.range = 100.0f;
    attacker->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;

    target = alloc_test_unit(MAKEFOURCC('h','c','t','w'), target_pos.x, target_pos.y);
    target->s.player = 1;
    target->targtype = TARG_GROUND;
    target->collision = 64.0f;
    target->runtime.flags |= UNIT_BALANCE_BUILDING;
    target->health.value = target->health.max_value = 1000.0f;
    target->bounds = (box2_t){
        .min = {target_pos.x - target->collision, target_pos.y - target->collision},
        .max = {target_pos.x + target->collision, target_pos.y + target->collision},
    };
    gi.LinkEntity(target);

    /* Create unrelated long-route destinations that keep competing for the
     * shared field while the attacker's route request waits. */
    for (int i = 0; i < COMPETING_JOBS; i++) {
        float const x = -3000.0f + (float)(i % 6) * 1000.0f;
        float const y = 3000.0f - (float)(i / 6) * 1000.0f;
        other_goals[i] = alloc_test_unit(MAKEFOURCC('h','p','e','a'), x, y);
    }
    CM_SetupTestPathmap(PATH_W, PATH_H, pathmap);
    CM_SetupTestWorldBounds(&bounds);

    T_ASSERT(S_OrderAttack(attacker, target));
    T_ASSERT(attacker->currentmove && attacker->currentmove->proc == CAbilityAttack);
    start_distance = Vector2_distance(&attacker->s.origin2, &target->s.origin2);
    /* Establish a valid route and move several steps before invalidating the
     * shared field. The retained direction is then tested during contention. */
    for (int frame = 0; frame < 8; frame++) {
        CM_ProcessPathJobs(UINT_MAX);
        attacker->currentmove->think(attacker);
        level.time += FRAMETIME;
    }
    T_ASSERT(attacker->movement.route_resume_valid);
    T_ASSERT(Vector2_distance(&attacker->s.origin2, &start_origin) > 0.01f);

    /* Replacing the pathmap invalidates cached shared fields while keeping the
     * mover's goal and recent validated direction intact. */
    memset(pathmap, 0, sizeof(pathmap));
    for (int y = 126; y <= 132; y++)
        for (int x = 126; x <= 130; x++)
            pathmap[x + y * PATH_W] = 0x42;
    CM_SetupTestPathmap(PATH_W, PATH_H, pathmap);

    /* A lower-numbered mover starts first, matching production entity order. */
    T_EQ(CM_RequestHeatmapForRadius(other_goals[0], 0.0f), 0);
    T_ASSERT(attacker->currentmove && attacker->currentmove->proc == CAbilityAttack);
    /* Later movers keep submitting distinct requests before this attacker is
     * thought. FIFO service must preserve the attacker's earlier place in line. */
    for (int frame = 0; frame < 12 && !resumed; frame++) {
        if (next_competing_job < COMPETING_JOBS) {
            T_EQ(CM_RequestHeatmapForRadius(other_goals[next_competing_job], 0.0f), 0);
            next_competing_job++;
        }
        T_ASSERT(attacker->currentmove && attacker->currentmove->think);
        attacker->currentmove->think(attacker);
        T_ASSERT(attacker->goalentity == target);
        T_ASSERT(attacker->currentmove->proc == CAbilityAttack);
        resumed = Vector2_distance(&attacker->s.origin2, &start_origin) > 0.01f;
        if (!resumed) {
            T_ASSERT(attacker->movement.route_resume_active || attacker->movement.path_wait_active);
            T_FEQ(attacker->s.origin2.x, start_origin.x, 0.01f);
            T_FEQ(attacker->s.origin2.y, start_origin.y, 0.01f);
        }
        CM_ProcessPathJobs(32768);
        level.time += FRAMETIME;
    }

    CM_GetPathJobStatus(&path_job);
    T_ASSERT(resumed);
    T_ASSERT(next_competing_job < COMPETING_JOBS);
    T_ASSERT(path_job.active || path_job.pending_jobs > 0);
    T_ASSERT(attacker->goalentity == target);
    T_ASSERT(Vector2_distance(&attacker->s.origin2, &target->s.origin2) < start_distance);
}

TEST(wc3_movement, near_goal_jitter_settles_to_stand) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    vec2_t dest = {100.0f, 0.0f};
    /* Keep the fixture inside the settle band but beyond arrival tolerance for
     * both ROC and TFT, whose archive-backed Peasant move speeds differ. */
    unit->s.origin2.x = dest.x - unit_movedistance(unit) - 6.0f;
    unit->s.origin.x = unit->s.origin2.x;
    gi.LinkEntity(unit);
    vec2_t jitter = unit->s.origin2;
    unit_issueorder(unit, "move", &dest);

    for (int i = 0; i < 10; i++) {
        if (!unit->currentmove || strcmp(unit->currentmove->animation, "walk") != 0) {
            break;
        }
        unit->currentmove->think(unit);
        unit->s.origin2 = jitter;
        unit->s.origin.x = jitter.x;
        unit->s.origin.y = jitter.y;
        unit->bounds.min.x = unit->s.origin2.x - unit->collision;
    unit->bounds.min.y = unit->s.origin2.y - unit->collision;
    unit->bounds.max.x = unit->s.origin2.x + unit->collision;
    unit->bounds.max.y = unit->s.origin2.y + unit->collision;
    }

    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_movement, unit_stops_when_goal_is_occupied) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    edict_t *blocker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100.0f, 0.0f);
    vec2_t dest = {100.0f, 0.0f};

    unit->collision = 16.0f;
    blocker->collision = 16.0f;
    blocker->s.model = 1;            /* non-hollow so it is a collision obstacle */
    blocker->stand = unit_stand;
    blocker->movetype = MOVETYPE_NONE;
    unit_stand(blocker);
    /* Collision is assigned after allocation, so link both fixtures with their final radii. */
    gi.LinkEntity(unit);
    gi.LinkEntity(blocker);

    unit_issueorder(unit, "move", &dest);

    /* Move-time collision blocks the unit short of the occupied goal (it never
     * steps into the blocker), then the blocked-frame accumulator settles it to
     * stand.  No post-move solver is involved any more.  Track the closest the
     * unit ever comes to the goal: it should reach right up against the blocker
     * (just outside the combined collision radius) but never inside it. */
    float min_goal_dist = M_DistanceToGoal(unit);
    for (int i = 0; i < 40; i++) {
        if (!unit->currentmove || strcmp(unit->currentmove->animation, "walk") != 0) {
            break;
        }
        unit->currentmove->think(unit);
        float d = M_DistanceToGoal(unit);
        if (d < min_goal_dist) min_goal_dist = d;
    }

    float combined = unit->collision + blocker->collision;
    T_STREQ(unit->currentmove->animation, "stand");/* settled, didn't walk forever */
    T_ASSERT(min_goal_dist >= combined - 1.0f);                    /* never penetrated the blocker */
    T_ASSERT(min_goal_dist <= combined + unit_movedistance(unit)); /* but reached right up to it */
}

/* Without a town hall the worker exits the mine carrying gold but has nowhere
 * to go: it stops in stand state.  The RETURN_GOLD, DEPOSIT_GOLD, and
 * RESUME_GOLD messages must NOT be published. */
TEST(wc3_movement, gold_worker_stops_when_no_townhall) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *mine   = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 400.0f, 0.0f);
    worker->collision = 16.0f; worker->unitinfo.MoveSpeed = 100.0f;
    mine->collision = 128.0f; mine->s.model = 1; mine->movetype = MOVETYPE_NONE;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(mine);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    HARVEST_GOLD_CAPACITY = 10.0f;

    msgTrace_t trace = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace));
    harvest_gold_start(worker, mine);

    /* Drive until harvested_gold is set (harvestgold_walkback fired). */
    FOR_LOOP(i, 100) {
        worker->currentmove->think(worker);
        if (worker->harvested_gold > 0) break;
    }
    G_UnsubscribeMessage(trace_message, &trace);

    T_ASSERT(worker->harvested_gold > 0);           /* gold carried, not deposited */
    T_ASSERT(worker->s.renderfx & RF_HAS_GOLD);     /* visual bag still on worker */
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));    /* not inside mine */
    T_STREQ(worker->currentmove->animation, "stand");
    /* Only MOVE_GOLD and ENTER_MINE — no return/deposit/resume. */
    T_EQ((int)trace.count, 2);
    T_EQ(trace.msg[0].type, GAME_MSG_HARVEST_MOVE_GOLD);
    T_EQ(trace.msg[1].type, GAME_MSG_HARVEST_ENTER_MINE);
    T_EQ(mine->resources, 90);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* A second worker ordered to mine when the mine is already at capacity waits
 * outside.  When the first worker exits, it wakes the second, which enters
 * immediately without a new walk order from the player. */
TEST(wc3_movement, gold_mine_queues_second_worker_when_at_capacity) {
    edict_t *worker1 = make_moving_unit(0.0f, 0.0f);
    edict_t *mine    = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 400.0f, 0.0f);
    worker1->collision = 16.0f; worker1->unitinfo.MoveSpeed = 100.0f;
    mine->collision = 128.0f; mine->s.model = 1; mine->movetype = MOVETYPE_NONE;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(mine);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    HARVEST_GOLD_CAPACITY = 10.0f;

    /* Worker1 walks to and enters the mine. */
    harvest_gold_start(worker1, mine);
    FOR_LOOP(i, 60) {
        worker1->currentmove->think(worker1);
        if (worker1->s.renderfx & RF_HIDDEN) break;
    }
    T_ASSERT(worker1->s.renderfx & RF_HIDDEN);
    T_EQ((int)mine->peonsinside, 1);

    /* Worker2: wire and place at the mine entrance so it reaches immediately. */
    edict_t *worker2 = alloc_test_unit(MAKEFOURCC('h','p','e','a'),
                                      mine->s.origin2.x - mine->collision - 16.0f,
                                      mine->s.origin2.y);
    worker2->movetype = MOVETYPE_STEP;
    worker2->stand    = unit_stand;
    worker2->die      = unit_die;
    worker2->collision = 16.0f;
    worker2->health.value = worker2->health.max_value = 250.0f;
    worker2->unitinfo.MoveSpeed = 100.0f;
    unit_stand(worker2);
    gi.LinkEntity(worker2);

    harvest_gold_start(worker2, mine);
    worker2->currentmove->think(worker2); /* immediately at mine — enters wait state */

    T_ASSERT(!(worker2->s.renderfx & RF_HIDDEN));   /* waiting outside */
    T_EQ((int)mine->peonsinside, 1);                /* still only worker1 */
    T_STREQ(worker2->currentmove->animation, "stand");

    /* Worker1 exits; harvestgold_walkback wakes worker2 in the same call. */
    worker1->currentmove->think(worker1);
    T_ASSERT(worker2->s.renderfx & RF_HIDDEN);   /* worker2 now inside */
    T_EQ((int)mine->peonsinside, 1);             /* worker1 left (−1) worker2 entered (+1) */
    T_ASSERT(!(worker1->s.renderfx & RF_HIDDEN));/* worker1 exited */
    T_ASSERT(worker1->s.renderfx & RF_HAS_GOLD); /* worker1 carrying gold */
    T_EQ(mine->resources, 90);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Stock Agld has one internal mining slot. Six assigned workers may all keep
 * Harvest orders, but only one may ever be registered/hidden inside. */
TEST(wc3_movement, gold_mine_stock_capacity_never_exceeds_one_with_six_workers) {
    reset_entities();
    setup_test_world();
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    setup_test_goldmine(mine, &test_goldmine_stock, 12500);
    HARVEST_GOLD_CAPACITY = 10.0f;

    T_EQ(S_GoldMineCapacity(mine), 1);
    FOR_LOOP(i, 6) {
        edict_t *worker = add_gold_worker(150.0f + (float)i, 0.0f);
        worker->goalentity = worker->secondarygoal = mine;
        harvestgold_minegold(worker);
        T_ASSERT(mine->peonsinside <= 1);
        if (i == 0) {
            T_ASSERT(S_GoldMineWorkerIsInside(worker));
            T_ASSERT(worker->s.renderfx & RF_HIDDEN);
        } else {
            T_ASSERT(!S_GoldMineWorkerIsInside(worker));
            T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
            T_STREQ(worker->currentmove->animation, "stand");
        }
    }
    T_EQ(mine->peonsinside, 1);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Capacity, duration, and initial gold come from the specific Agld-derived
 * ability on each mine rather than process-wide globals. */
TEST(wc3_movement, gold_mines_keep_independent_custom_capacity_duration_and_gold) {
    reset_entities();
    setup_test_world();
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    edict_t *mine1 = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    edict_t *mine2 = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 500.0f, 0.0f);
    setup_test_goldmine(mine1, &test_goldmine_cap1, 0);
    setup_test_goldmine(mine2, &test_goldmine_cap2, 0);
    S_GoldMineInitUnit(mine1);
    S_GoldMineInitUnit(mine2);

    T_EQ(mine1->resources, 100);
    T_EQ(mine2->resources, 200);
    T_EQ(S_GoldMineCapacity(mine1), 1);
    T_EQ(S_GoldMineCapacity(mine2), 2);
    T_FEQ(S_GoldMineMiningDuration(mine1), 0.01f, 0.001f);
    T_FEQ(S_GoldMineMiningDuration(mine2), 2.0f, 0.001f);

    edict_t *a = add_gold_worker(0.0f, 0.0f);
    edict_t *b = add_gold_worker(0.0f, 0.0f);
    edict_t *c = add_gold_worker(500.0f, 0.0f);
    edict_t *d = add_gold_worker(500.0f, 0.0f);
    edict_t *e = add_gold_worker(500.0f, 0.0f);
    a->goalentity = b->goalentity = mine1;
    c->goalentity = d->goalentity = e->goalentity = mine2;
    harvestgold_minegold(a);
    harvestgold_minegold(b);
    harvestgold_minegold(c);
    harvestgold_minegold(d);
    harvestgold_minegold(e);

    T_EQ(mine1->peonsinside, 1);
    T_EQ(mine2->peonsinside, 2);
    T_FEQ(c->wait, 2.0f, 0.001f);
    T_ASSERT(!S_GoldMineWorkerIsInside(b));
    T_ASSERT(!S_GoldMineWorkerIsInside(e));

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Inside membership is authoritative: duplicate entry cannot increment the
 * mine twice, ordinary orders are rejected, and exit restores protection and
 * unregisters exactly once. */
TEST(wc3_movement, gold_miner_inside_is_non_orderable_and_unregisters_once) {
    reset_entities();
    setup_test_world();
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    edict_t *worker = add_gold_worker(0.0f, 0.0f);
    vec2_t point = { 100.0f, 100.0f };
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    worker->goalentity = worker->secondarygoal = mine;
    HARVEST_GOLD_CAPACITY = 10.0f;

    harvestgold_minegold(worker);
    T_EQ(mine->peonsinside, 1);
    T_ASSERT(strstr(mine->animation_props, "work") != NULL);
    T_ASSERT(worker->invulnerable);
    T_ASSERT(S_GoldMineWorkerIsInside(worker));
    harvestgold_minegold(worker);
    T_EQ(mine->peonsinside, 1);
    T_ASSERT(!unit_issueimmediateorder(worker, "stop"));
    T_ASSERT(!unit_issueorder(worker, "move", &point));
    T_ASSERT(!unit_issuetargetorder(worker, "attack", mine));
    T_EQ(mine->peonsinside, 1);

    harvestgold_walkback(worker);
    T_EQ(mine->peonsinside, 0);
    T_ASSERT(strstr(mine->animation_props, "work") == NULL);
    T_ASSERT(!S_GoldMineWorkerIsInside(worker));
    T_ASSERT(!worker->invulnerable);
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_EQ(worker->harvested_gold, 10);
    harvestgold_walkback(worker); /* cannot unregister/decrement twice */
    T_EQ(mine->peonsinside, 0);

    edict_t *removed = add_gold_worker(0.0f, 0.0f);
    removed->goalentity = removed->secondarygoal = mine;
    harvestgold_minegold(removed);
    T_EQ(mine->peonsinside, 1);
    G_FreeEdict(removed);
    T_EQ(mine->peonsinside, 0);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* The final trip is clamped to remaining mine gold. Draining the mine to zero
 * depletes it and prevents an already-waiting worker from entering. */
TEST(wc3_movement, gold_mine_partial_final_trip_depletes_and_rejects_waiter) {
    reset_entities();
    setup_test_world();
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    edict_t *miner = add_gold_worker(0.0f, 0.0f);
    edict_t *waiter = add_gold_worker(0.0f, 0.0f);
    setup_test_goldmine(mine, &test_goldmine_cap1, 6);
    HARVEST_GOLD_CAPACITY = 10.0f;
    miner->goalentity = miner->secondarygoal = mine;
    waiter->goalentity = waiter->secondarygoal = mine;

    harvestgold_minegold(miner);
    harvestgold_minegold(waiter);
    T_EQ(mine->peonsinside, 1);
    T_STREQ(waiter->currentmove->animation, "stand");

    harvestgold_walkback(miner);
    T_EQ(miner->harvested_gold, 6);
    T_EQ(mine->resources, 0);
    T_EQ(mine->peonsinside, 0);
    T_ASSERT(M_IsDead(mine));
    T_ASSERT(!S_GoldMineWorkerIsInside(waiter));
    T_ASSERT(!(waiter->s.renderfx & RF_HIDDEN));
    T_STREQ(waiter->currentmove->animation, "stand");

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Haunted mining keeps the underlying Agld unit as the sole resource pool.
 * Acolytes take deterministic external ring slots and the mine grants income
 * directly according to Warsmash's integer worker-count interval scaling. */
TEST(wc3_movement, haunted_mine_uses_acolyte_ring_slots_and_parent_gold) {
    slkTestData_t *rows, *old_abilities;
    gameClient_t *client;
    edict_t *parent, *haunted, *first, *second;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    client = &game.clients[0];
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    haunted = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    first = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    second = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    setup_test_goldmine(parent, &test_goldmine_stock, 100);
    haunted->data.UnitAbilities = &test_haunted_mine;
    haunted->health.value = haunted->health.max_value = 1000.0f;
    first->data.UnitAbilities = second->data.UnitAbilities = &test_acolyte_harvest;
    haunted->s.player = first->s.player = second->s.player = client->ps.number;
    first->stand = second->stand = unit_stand;
    first->collision = second->collision = 16.0f;
    first->unitinfo.MoveSpeed = second->unitinfo.MoveSpeed = 100.0f;
    unit_stand(first); unit_stand(second);

    T_ASSERT(S_MineOverlayBind(haunted, parent));
    T_ASSERT(parent->s.renderfx & RF_HIDDEN);
    T_ASSERT(parent->paused);
    T_ASSERT(S_AcolyteHarvestOrder(first, haunted));
    T_ASSERT(S_AcolyteHarvestOrder(second, haunted));
    first->currentmove->think(first);
    second->currentmove->think(second);
    T_ASSERT(S_AcolyteHarvestIsActive(first));
    T_ASSERT(S_AcolyteHarvestIsActive(second));
    T_ASSERT(first->acolyte_mine->slot != second->acolyte_mine->slot);
    T_ASSERT(!(first->s.renderfx & RF_HIDDEN));
    T_ASSERT(!(second->s.renderfx & RF_HIDDEN));
    T_STREQ(first->currentmove->animation, "stand work");

    /* Five authored slots with two active Acolytes use integer multiplier 2. */
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    level.time = 1999;
    blight_mine_think(haunted);
    T_EQ(count_haunted_ring_effects(haunted), 5);
    FOR_LOOP(slot, 5) {
        edict_t *effect = haunted_ring_effect_slot(haunted, slot);
        float const angle = (float)(M_PI / 2.0 + (M_PI * 2.0 / 5.0) * slot);
        T_NOT_NULL(effect);
        T_FEQ(effect->s.angle, angle, 0.001f);
    }
    T_EQ(parent->resources, 100);
    level.time = 2000;
    blight_mine_think(haunted);
    T_EQ(parent->resources, 90);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 10);

    unit_stand(first);
    T_ASSERT(!S_AcolyteHarvestIsActive(first));
    T_ASSERT(S_AcolyteHarvestIsActive(second));
    /* Actual destruction releases the overlay relationship before the death
     * animation; the original mine must immediately become usable again. The
     * parent is restored by its bound identity, not by reclassifying abilities
     * during teardown. */
    parent->data.UnitAbilities = NULL;
    unit_die(haunted, NULL);
    T_ASSERT(M_IsDead(haunted));
    T_ASSERT(!S_AcolyteHarvestIsActive(second));
    T_ASSERT(!(parent->s.renderfx & RF_HIDDEN));
    T_ASSERT(!parent->paused);
    T_EQ(parent->resources, 90);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* UndeadX04's retail cleanup issues autoharvestgold to its newly created
 * Acolytes after replacing the neutral mine with a Haunted Mine. */
TEST(wc3_movement, autoharvestgold_assigns_acolyte_to_nearest_haunted_mine) {
    slkTestData_t *rows, *old_abilities;
    edict_t *parent, *haunted, *acolyte;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    haunted = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    acolyte = alloc_test_unit(MAKEFOURCC('u','a','c','o'), 180.0f, 0.0f);
    setup_test_goldmine(parent, &test_goldmine_stock, 4500);
    haunted->data.UnitAbilities = &test_haunted_mine;
    haunted->s.player = acolyte->s.player = 0;
    acolyte->data.UnitAbilities = &test_acolyte_harvest;
    acolyte->stand = unit_stand;
    acolyte->collision = 16.0f;
    acolyte->unitinfo.MoveSpeed = 220.0f;
    unit_stand(acolyte);
    T_ASSERT(S_MineOverlayBind(haunted, parent));

    T_ASSERT(unit_issueimmediateorder(acolyte, "autoharvestgold"));
    T_EQ(acolyte->goalentity, haunted);
    T_ASSERT(acolyte->currentmove && acolyte->currentmove->proc == CAbilityAcolyteHarvest);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Map-loaded overlays must bind to the neutral mine at the same authored location. */
TEST(wc3_movement, preplaced_haunted_mine_binds_to_neutral_parent) {
    slkTestData_t *rows, *old_abilities;
    edict_t *parent, *haunted;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 128.0f, 128.0f);
    haunted = alloc_test_unit(MAKEFOURCC('u','g','o','l'), 128.0f, 128.0f);
    parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    haunted->s.player = 0;
    setup_test_goldmine(parent, &test_goldmine_stock, 4500);
    haunted->data.UnitAbilities = &test_haunted_mine;
    haunted->health.value = haunted->health.max_value = 1000.0f;

    S_MineOverlayBindPreplaced();
    T_EQ(haunted->mineoverlay->parent, parent);
    T_ASSERT(parent->s.renderfx & RF_HIDDEN);
    T_ASSERT(parent->paused);
    T_EQ(parent->resources, 4500);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Script-created Haunted Mines must return a live bound overlay and preserve parent gold. */
TEST(wc3_movement, scripted_haunted_mine_creation_binds_parent) {
    slkTestData_t *rows, *old_abilities;
    edict_t *parent, *haunted;
    vec2_t point = { 256.0f, 256.0f };

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), point.x, point.y);
    parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    setup_test_goldmine(parent, &test_goldmine_stock, 3200);

    haunted = S_CreateBlightedGoldmine(0, &point, 90.0f);
    T_NOT_NULL(haunted);
    T_EQ(haunted->mineoverlay->parent, parent);
    T_FEQ(haunted->s.angle, 90.0f, 0.001f);
    T_EQ(parent->resources, 3200);
    T_ASSERT(parent->s.renderfx & RF_HIDDEN);
    T_ASSERT(parent->paused);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Restoration spawns must initialize gameplay data without replaying Birth presentation. */
TEST(wc3_movement, no_birth_spawn_skips_birth_callback) {
    edict_t *unit;

    reset_entities();
    setup_test_world();
    unit = SP_SpawnAtLocationNoBirth(MAKEFOURCC('u','g','o','l'), 0, &MAKE(vec2_t, 0, 0));
    T_NOT_NULL(unit);
    T_ASSERT(unit->birth != NULL);
    T_ASSERT(unit->currentmove == NULL || strcmp(unit->currentmove->animation, "birth"));
}

/* Entangled gold income reuses generic cargo occupancy. The periodic slot
 * cursor advances before testing occupancy, skips empty slots, and depletion
 * kills the overlay, unloads Wisps, and restores the original mine. */
TEST(wc3_movement, entangled_mine_round_robin_income_depletes_parent_and_unloads_wisps) {
    slkTestData_t *rows, *old_abilities;
    gameClient_t *client;
    edict_t *parent, *mine, *first, *second;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    client = &game.clients[0];
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    mine = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    first = alloc_test_unit(MAKEFOURCC('e','w','s','p'), 0.0f, 0.0f);
    second = alloc_test_unit(MAKEFOURCC('e','w','s','p'), 0.0f, 0.0f);
    setup_test_goldmine(parent, &test_goldmine_stock, 25);
    mine->data.UnitAbilities = &test_entangled_mine;
    mine->think = monster_think;
    mine->health.value = mine->health.max_value = 1000.0f;
    mine->s.player = first->s.player = second->s.player = client->ps.number;
    first->stand = second->stand = unit_stand;
    first->s.renderfx |= RF_HIDDEN; second->s.renderfx |= RF_HIDDEN;
    first->paused = second->paused = true;
    if (!mine->cargo) mine->cargo = G_AllocCargo();
    assert(mine->cargo);
    mine->cargo->units[0] = first; mine->cargo->units[1] = second; mine->cargo->count = 2;

    T_ASSERT(S_MineOverlayBind(mine, parent));
    S_CargoInitUnit(mine);
    T_ASSERT(strstr(mine->animation_props, "second") != NULL);
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;

    level.time = 0;
    G_RunEntity(mine); /* index 1: occupied */
    T_EQ(parent->resources, 15);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 10);
    T_EQ(mine->mineoverlay->active_interval_index, 1);
    level.time = 1000; G_RunEntity(mine); /* index 2: empty */
    level.time = 2000; G_RunEntity(mine); /* index 3: empty */
    level.time = 3000; G_RunEntity(mine); /* index 4: empty */
    T_EQ(parent->resources, 15);
    level.time = 4000; G_RunEntity(mine); /* index 0: occupied */
    T_EQ(parent->resources, 5);
    level.time = 5000; G_RunEntity(mine); /* index 1: final 5 */

    T_EQ(parent->resources, 0);
    T_ASSERT(M_IsDead(mine));
    T_ASSERT(!mine->cargo || mine->cargo->count == 0);
    T_ASSERT(!(first->s.renderfx & RF_HIDDEN));
    T_ASSERT(!(second->s.renderfx & RF_HIDDEN));
    T_ASSERT(!first->paused && !second->paused);
    T_ASSERT(!(parent->s.renderfx & RF_HIDDEN));
    T_ASSERT(!parent->paused);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}


/* A successful Entangle owns a caster-local hidden/permanent command state for
 * exactly the live overlay generation.  The helper deliberately derives this
 * from saved overlay state rather than the optional CasterArt effect. */
TEST(wc3_movement, entangle_command_hidden_tracks_live_overlay_caster_generation) {
    edict_t * caster, *overlay, *parent;
    uint32_t const ability = MAKEFOURCC('A','e','n','t');

    reset_entities();
    setup_test_world();
    caster = alloc_test_unit(MAKEFOURCC('e','t','o','l'), 0.0f, 0.0f);
    overlay = alloc_test_unit(MAKEFOURCC('e','g','o','l'), 64.0f, 0.0f);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 64.0f, 0.0f);
    if (!overlay->mineoverlay) overlay->mineoverlay = G_AllocMineOverlay();
    assert(overlay->mineoverlay);
    overlay->mineoverlay->parent = parent;
    overlay->mineoverlay->parent_spawn_time = parent->spawn_time;
    overlay->mineoverlay->caster = caster;
    overlay->mineoverlay->caster_spawn_time = caster->spawn_time;
    overlay->mineoverlay->entangle_ability = ability;

    T_ASSERT(S_EntangleCommandHidden(caster, ability));
    overlay->mineoverlay->caster_spawn_time++;
    T_ASSERT(!S_EntangleCommandHidden(caster, ability));
}

static edict_t *movement_find_entangle_overlay(edict_t *caster, edict_t *parent) {
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *overlay = globals.edicts + i;
        if (overlay->inuse && overlay->mineoverlay && overlay->mineoverlay->parent == parent &&
            overlay->mineoverlay->caster == caster &&
            overlay->mineoverlay->entangle_ability == MAKEFOURCC('A','e','n','t'))
            return overlay;
    }
    return NULL;
}

static bool movement_issue_entangle_command(edict_t *clent, gameClient_t *client,
                                            edict_t *caster, edict_t *parent) {
    abilityCall_t call = MAKE(abilityCall_t, .client = clent);
    if (!CAbilityEntangle(caster, A_COMMAND, &call) || !client->menu.on_entity_selected)
        return false;
    return client->menu.on_entity_selected(clent, parent);
}

static void movement_prepare_rooted_entangle_caster(edict_t *caster, uint32_t player) {
    uint32_t const entangle = MAKEFOURCC('A','e','n','t');
    uint32_t const root = MAKEFOURCC('A','r','o','1');
    caster->s.player = player;
    caster->data.UnitAbilities = &test_entangle_caster;
    G_ActorAddSkill(caster, entangle);
    G_ActorAddSkill(caster, root);
    if (!caster->ancient_root) caster->ancient_root = G_AllocAncientRoot();
    assert(caster->ancient_root);
    caster->ancient_root->ability = root;
    caster->ancient_root->mode = ANCIENT_ROOTED;
    caster->s.flags |= EF_BUILDING;
    caster->aiflags |= AI_IMMOBILE;
    caster->runtime.flags |= UNIT_BALANCE_BUILDING;
}

TEST(wc3_movement, entangleinstant_target_order_creates_completed_overlay) {
    edict_t *caster, *parent, *overlay;
    slkTestData_t *rows, *old_abilities;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    caster = alloc_test_unit(MAKEFOURCC('e','T','S','T'), 0.0f, 0.0f);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 32.0f, 0.0f);
    movement_prepare_rooted_entangle_caster(caster, 0);
    parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    setup_test_goldmine(parent, &test_goldmine_stock, 5000);

    T_ASSERT(unit_issuetargetorder(caster, "entangleinstant", parent));
    overlay = movement_find_entangle_overlay(caster, parent);
    T_NOT_NULL(overlay);
    T_ASSERT(overlay && !overlay->construction);
    T_ASSERT(overlay && overlay->build != overlay);
    T_ASSERT(parent->s.renderfx & RF_HIDDEN);
    T_ASSERT(parent->paused);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, queued_entangleinstant_dispatches_when_previous_order_finishes) {
    edict_t *caster, *parent, *overlay;
    slkTestData_t *rows, *old_abilities;
    umove_t active_order = { .animation = "walk", .proc = CAbilityMove };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    caster = alloc_test_unit(MAKEFOURCC('e','T','S','T'), 0.0f, 0.0f);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 32.0f, 0.0f);
    movement_prepare_rooted_entangle_caster(caster, 0);
    parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    setup_test_goldmine(parent, &test_goldmine_stock, 5000);
    caster->currentmove = &active_order;

    T_ASSERT(G_IssueUnitTargetOrder(caster, "entangleinstant", parent, true, 0));
    T_EQ(G_UnitQueuedOrderCount(caster), 1);
    T_NULL(movement_find_entangle_overlay(caster, parent));

    caster->currentmove = NULL;
    T_ASSERT(G_UnitStartNextQueuedOrder(caster));
    overlay = movement_find_entangle_overlay(caster, parent);
    T_NOT_NULL(overlay);
    T_ASSERT(overlay && !overlay->construction);
    T_EQ(G_UnitQueuedOrderCount(caster), 0);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, entangle_range_uses_goldmine_footprint) {
    enum { W = 8, H = 8 };
    size_t const pathtex_size = sizeof(pathTex_t) + W * H * sizeof(color32_t);
    edict_t *caster, *parent, *overlay;
    pathTex_t *pathtex;
    slkTestData_t *rows, *old_abilities;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    caster = alloc_test_unit(MAKEFOURCC('e','T','S','T'), 200.0f, 0.0f);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    movement_prepare_rooted_entangle_caster(caster, 0);
    caster->collision = 16.0f;
    parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    parent->s.flags |= EF_BUILDING;
    parent->runtime.flags |= UNIT_BALANCE_BUILDING;
    setup_test_goldmine(parent, &test_goldmine_stock, 5000);
    pathtex = gi.MemAlloc(pathtex_size);
    T_NOT_NULL(pathtex);
    memset(pathtex, 0, pathtex_size);
    pathtex->width = W;
    pathtex->height = H;
    FOR_LOOP(i, W * H) pathtex->map[i].b = 0xff;
    parent->pathtex = pathtex;

    /* Aent's fixture range is 64. The centres are 200 apart, but the caster
     * is within its collision radius plus 64 of the authored mine footprint. */
    T_ASSERT(unit_issuetargetorder(caster, "entangleinstant", parent));
    overlay = movement_find_entangle_overlay(caster, parent);
    T_NOT_NULL(overlay);

    parent->pathtex = NULL;
    gi.MemFree(pathtex);
    gi.Write = old_write;
    gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, auto_entangle_nearby_starts_normal_construction) {
    edict_t *caster, *parent, *overlay;
    slkTestData_t *rows, *old_abilities;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    caster = alloc_test_unit(MAKEFOURCC('e','T','S','T'), 0.0f, 0.0f);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 32.0f, 0.0f);
    movement_prepare_rooted_entangle_caster(caster, 0);
    parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    setup_test_goldmine(parent, &test_goldmine_stock, 5000);

    T_ASSERT(S_AutoEntangleNearby(caster, false));
    overlay = movement_find_entangle_overlay(caster, parent);
    T_NOT_NULL(overlay);
    T_ASSERT(overlay && overlay->construction);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, root_completion_auto_entangles_nearest_mine_once) {
    slkTestData_t *gold_rows, *old_gold;
    edict_t *caster, *parent, *far_parent, *overlay;
    uint32_t end_time;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities();
    setup_test_world();
    old_gold = install_racial_goldmine_test_data(&gold_rows);
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    caster = alloc_test_unit(MAKEFOURCC('e','T','S','T'), 0.0f, 0.0f);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 32.0f, 0.0f);
    far_parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 48.0f, 0.0f);
    movement_prepare_rooted_entangle_caster(caster, 0);
    if (!caster->ancient_root) caster->ancient_root = G_AllocAncientRoot();
    assert(caster->ancient_root);
    caster->ancient_root->mode = ANCIENT_UPROOTED;
    caster->s.flags &= ~EF_BUILDING;
    caster->aiflags &= ~AI_IMMOBILE;
    caster->runtime.flags &= ~UNIT_BALANCE_BUILDING;
    parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    setup_test_goldmine(parent, &test_goldmine_stock, 5000);
    far_parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    setup_test_goldmine(far_parent, &test_goldmine_stock, 5000);

    S_AncientBeginMorph(caster, true);
    end_time = caster->ancient_root->transition_end_time;
    level.time = end_time - 1;
    S_RunAbilityUpdates(caster);
    T_EQ(caster->ancient_root->mode, ANCIENT_ROOTING);
    T_NULL(movement_find_entangle_overlay(caster, parent));

    level.time = end_time;
    S_RunAbilityUpdates(caster);
    T_EQ(caster->ancient_root->mode, ANCIENT_ROOTED);
    overlay = movement_find_entangle_overlay(caster, parent);
    T_NOT_NULL(overlay);
    T_ASSERT(overlay && overlay->construction);
    T_NULL(movement_find_entangle_overlay(caster, far_parent));

    S_RunAbilityUpdates(caster);
    T_EQ(movement_find_entangle_overlay(caster, parent), overlay);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old_gold);
    free_slk_rows(gold_rows);
}

TEST(wc3_movement, wisp_waits_for_incomplete_entangled_mine_then_boards) {
    edict_t *mine, *wisp;
    slkTestData_t *rows, *old_abilities;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    mine = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    wisp = alloc_test_unit(MAKEFOURCC('e','w','s','p'), 0.0f, 0.0f);
    mine->data.UnitAbilities = &test_entangled_mine;
    mine->s.player = wisp->s.player = 0;
    if (!mine->construction) mine->construction = G_AllocConstruction();
    assert(mine->construction);
    mine->health.value = mine->health.max_value = 1000.0f;
    wisp->stand = unit_stand;
    unit_stand(wisp);

    T_ASSERT(S_CargoOrderBoard(wisp, mine));
    T_ASSERT(!mine->cargo || mine->cargo->count == 0);
    T_EQ(wisp->secondarygoal, mine);
    T_NOT_NULL(wisp->currentmove);
    T_STREQ(wisp->currentmove->animation, "stand");
    T_ASSERT(wisp->currentmove && wisp->currentmove->proc == CAbilityBattlestations);

    G_FreeConstruction(mine);
    wisp->currentmove->think(wisp);
    T_EQ(mine->cargo->count, 1);
    T_EQ(mine->cargo->units[0], wisp);
    T_ASSERT(wisp->paused);
    T_ASSERT(wisp->s.renderfx & RF_HIDDEN);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, harvest_order_boards_wisp_into_entangled_mine) {
    slkTestData_t *rows, *old_abilities;
    edict_t *parent, *mine, *wisp;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    mine = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    wisp = alloc_test_unit(MAKEFOURCC('e','w','s','p'), 0.0f, 0.0f);
    parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    mine->s.player = wisp->s.player = 0;
    setup_test_goldmine(parent, &test_goldmine_stock, 5000);
    mine->data.UnitAbilities = &test_entangled_mine;
    mine->construction = G_AllocConstruction();
    T_NOT_NULL(mine->construction);
    mine->health.value = mine->health.max_value = 1000.0f;
    wisp->data.UnitAbilities = &wisp_harvest_abilities;

    T_ASSERT(unit_issuetargetorder(wisp, "harvest", mine));
    T_EQ(wisp->secondarygoal, mine);
    T_ASSERT(wisp->currentmove && wisp->currentmove->proc == CAbilityBattlestations);

    G_FreeConstruction(mine);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, autoharvestgold_immediate_order_boards_wisp_into_entangled_mine) {
    slkTestData_t *rows, *old_abilities;
    edict_t *parent, *mine, *wisp;
    uint32_t steps;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    mine = alloc_test_unit(MAKEFOURCC('e','g','o','l'), 0.0f, 0.0f);
    wisp = alloc_test_unit(MAKEFOURCC('e','w','s','p'), 180.0f, 0.0f);
    parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    mine->s.player = wisp->s.player = 0;
    setup_test_goldmine(parent, &test_goldmine_stock, 24000);
    mine->data.UnitAbilities = &test_entangled_mine;
    mine->health.value = mine->health.max_value = 1000.0f;
    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    wisp->movetype = MOVETYPE_STEP;
    wisp->collision = 16.0f;
    wisp->unitinfo.MoveSpeed = 220.0f;
    wisp->stand = unit_stand;
    unit_stand(wisp);
    T_ASSERT(S_MineOverlayBind(mine, parent));

    /* NightElf07's script assigns this immediate order to Wisps. It must
     * select the player's Entangled Mine and enter the mine's cargo. */
    T_ASSERT(unit_issueimmediateorder(wisp, "autoharvestgold"));
    T_ASSERT(!mine->cargo || mine->cargo->count == 0);
    T_EQ(wisp->secondarygoal, mine);
    T_ASSERT(wisp->currentmove && wisp->currentmove->proc == CAbilityBattlestations);
    for (steps = 0; steps < 64 && (!mine->cargo || mine->cargo->count == 0); steps++) {
        if (!wisp->currentmove || !wisp->currentmove->think) break;
        wisp->currentmove->think(wisp);
    }
    T_EQ(mine->cargo->count, 1);
    T_EQ(mine->cargo->units[0], wisp);
    T_ASSERT(wisp->paused);
    T_ASSERT(wisp->s.renderfx & RF_HIDDEN);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, rallied_wisp_waits_for_entangled_mine_then_automatically_boards) {
    UnitBalance_t balance = { .buildTime = 1, .foodUsed = 0, .foodMade = 0 };
    edict_t *producer, *mine, *wisp;
    slkTestData_t *rows, *old_abilities;
    uint32_t steps;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), -128.0f, 0.0f);
    mine = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    wisp = alloc_test_unit(MAKEFOURCC('e','w','s','p'), 0.0f, 0.0f);
    producer->data.UnitProfile = &wisp_rally_producer_profile;
    producer->movetype = MOVETYPE_NONE;
    producer->collision = 64.0f;
    producer->stand = unit_stand;
    producer->s.player = mine->s.player = wisp->s.player = 0;
    mine->data.UnitAbilities = &test_entangled_mine;
    if (!mine->construction) mine->construction = G_AllocConstruction();
    assert(mine->construction);
    mine->health.value = mine->health.max_value = 1000.0f;
    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    wisp->data.UnitBalance = &balance;
    wisp->collision = 16.0f;
    wisp->movetype = MOVETYPE_STEP;
    wisp->unitinfo.MoveSpeed = 220.0f;
    wisp->health.value = wisp->health.max_value = 100.0f;
    wisp->stand = unit_stand;
    wisp->training = true;
    wisp->s.renderfx |= RF_HIDDEN;
    producer->build = wisp;
    unit_stand(wisp);

    T_ASSERT(G_SetRallyEntity(producer, mine));
    ai_train_build(producer);
    T_ASSERT(!wisp->training);
    T_ASSERT(!(wisp->s.renderfx & RF_HIDDEN));
    /* Training places the Wisp beside its producer; let its rally order reach
     * the mine before construction finishes. */
    for (steps = 0; steps < 64 && wisp->currentmove &&
         wisp->currentmove->proc == CAbilityBattlestations &&
         strcmp(wisp->currentmove->animation, "stand"); steps++)
        wisp->currentmove->think(wisp);
    T_ASSERT(!mine->cargo || mine->cargo->count == 0);
    T_EQ(wisp->secondarygoal, mine);
    T_ASSERT(wisp->currentmove && wisp->currentmove->proc == CAbilityBattlestations);
    T_STREQ(wisp->currentmove->animation, "stand");

    G_FreeConstruction(mine);
    wisp->currentmove->think(wisp);
    T_EQ(mine->cargo->count, 1);
    T_EQ(mine->cargo->units[0], wisp);
    T_ASSERT(wisp->paused);
    T_ASSERT(wisp->s.renderfx & RF_HIDDEN);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, entangle_overlay_restores_original_permanent_state) {
    cstring_t const filename = Test_TempPath("wc3-entangle-lifecycle.bin");
    uint32_t const ability = MAKEFOURCC('A','e','n','t');
    edict_t *clent, *caster, *parent, *overlay;
    gameClient_t *client;
    slkTestData_t *rows, *old_abilities;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    /* The test archive omits normal unit metadata; make the resulting hbar
     * building explicit so runtime spawn follows the authored Aent contract. */
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    clent = &g_edicts[0];
    client = &game.clients[0];
    clent->client = client;
    caster = alloc_test_unit(MAKEFOURCC('e','T','S','T'), 0.0f, 0.0f);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 32.0f, 0.0f);
    caster->no_pathing = true; /* rooted Ancient state */
    caster->s.player = parent->s.player = client->ps.number;
    setup_test_goldmine(parent, &test_goldmine_stock, 5000);
    caster->data.UnitAbilities = &test_entangle_caster;
    T_ASSERT(G_ActorSetSkillPermanent(caster, ability, true));
    G_ActorAddSkill(caster, ability);
    if (!caster->ancient_root) caster->ancient_root = G_AllocAncientRoot();
    assert(caster->ancient_root);
    caster->ancient_root->ability = MAKEFOURCC('A','r','o','1');
    caster->ancient_root->mode = ANCIENT_UPROOTED;
    G_SelectEntity(client, caster);
    caster->no_pathing = false;
    T_ASSERT(!movement_issue_entangle_command(clent, client, caster, parent));
    caster->no_pathing = true;
    caster->ancient_root->mode = ANCIENT_ROOTED;
    caster->s.flags |= EF_BUILDING;
    caster->aiflags |= AI_IMMOBILE;
    caster->runtime.flags |= UNIT_BALANCE_BUILDING;
    T_ASSERT(movement_issue_entangle_command(clent, client, caster, parent));
    overlay = movement_find_entangle_overlay(caster, parent);
    T_NOT_NULL(overlay);
    T_ASSERT(overlay && overlay->mineoverlay->entangle_permanent_before);
    T_ASSERT(overlay && overlay->construction);

    T_ASSERT(WriteGame(filename));
    T_ASSERT(G_ActorSetSkillPermanent(caster, ability, false));
    if (!overlay->mineoverlay) overlay->mineoverlay = G_AllocMineOverlay();
    assert(overlay->mineoverlay);
    overlay->mineoverlay->entangle_permanent_before = false;
    T_ASSERT(ReadGame(filename));
    T_ASSERT(G_ActorSkillPermanent(caster, ability));
    overlay = movement_find_entangle_overlay(caster, parent);
    T_NOT_NULL(overlay);
    T_ASSERT(overlay && overlay->mineoverlay->entangle_permanent_before);
    S_MineOverlayRelease(overlay);
    T_ASSERT(G_ActorSkillPermanent(caster, ability));
    T_ASSERT(!(parent->s.renderfx & RF_HIDDEN));
    T_ASSERT(!parent->paused);
    remove(filename);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, entangle_missing_overlay_unit_id_reports_unavailable) {
    static char const missing_unit_id[] =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"Dur1\"\nC;Y1;X4;K\"DataA1\"\n"
        "C;Y2;X1;K\"Aent\"\nC;Y2;X2;K\"Aent\"\n"
        "C;Y2;X3;K10\nE\n";
    slkTestData_t *rows, *old_abilities;
    edict_t *clent, *caster, *parent;
    gameClient_t *client;
    uint32_t count_before;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities(); setup_test_world();
    rows = parse_slk_string(missing_unit_id);
    old_abilities = G_SetSLKRows("AbilityData", rows);
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    clent = &g_edicts[0]; client = &game.clients[0];
    clent->client = client;
    caster = alloc_test_unit(MAKEFOURCC('e','T','S','T'), 0.0f, 0.0f);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 32.0f, 0.0f);
    caster->data.UnitAbilities = &test_entangle_caster;
    caster->s.player = parent->s.player = client->ps.number;
    if (!caster->ancient_root) caster->ancient_root = G_AllocAncientRoot();
    assert(caster->ancient_root);
    caster->ancient_root->ability = MAKEFOURCC('A','r','o','1');
    caster->ancient_root->mode = ANCIENT_ROOTED;
    G_ActorAddSkill(caster, MAKEFOURCC('A','e','n','t'));
    G_SelectEntity(client, caster);
    count_before = globals.num_edicts;

    T_ASSERT(!movement_issue_entangle_command(clent, client, caster, parent));
    T_EQ(globals.num_edicts, count_before);
    T_NOT_NULL(client->menu.on_entity_selected);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, one_tree_cannot_entangle_multiple_gold_mines) {
    uint32_t const ability = MAKEFOURCC('A','e','n','t');
    edict_t *clent, *caster, *first, *second, *parent1, *parent2;
    gameClient_t *client;
    slkTestData_t *rows, *old_abilities;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    clent = &g_edicts[0];
    client = &game.clients[0];
    clent->client = client;
    caster = alloc_test_unit(MAKEFOURCC('e','T','S','T'), 0.0f, 0.0f);
    parent1 = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 32.0f, 0.0f);
    parent2 = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 48.0f, 0.0f);
    caster->s.player = parent1->s.player = parent2->s.player = client->ps.number;
    caster->no_pathing = true; /* rooted Ancient state */
    setup_test_goldmine(parent1, &test_goldmine_stock, 5000);
    setup_test_goldmine(parent2, &test_goldmine_stock, 5000);
    caster->abilities.added[0] = ability;
    caster->abilities.added[1] = MAKEFOURCC('A','r','o','1');
    caster->abilities.added_count = 2;
    if (!caster->ancient_root) caster->ancient_root = G_AllocAncientRoot();
    assert(caster->ancient_root);
    caster->ancient_root->ability = MAKEFOURCC('A','r','o','1');
    caster->ancient_root->mode = ANCIENT_ROOTED;
    caster->s.flags |= EF_BUILDING;
    caster->aiflags |= AI_IMMOBILE;
    caster->runtime.flags |= UNIT_BALANCE_BUILDING;
    G_SelectEntity(client, caster);
    T_ASSERT(movement_issue_entangle_command(clent, client, caster, parent1));
    first = movement_find_entangle_overlay(caster, parent1);
    T_NOT_NULL(first);
    T_ASSERT(first && (!first->mineoverlay || !first->mineoverlay->entangle_permanent_before));
    T_ASSERT(G_ActorSkillPermanent(caster, ability));

    T_ASSERT(!movement_issue_entangle_command(clent, client, caster, parent2));
    second = movement_find_entangle_overlay(caster, parent2);
    T_NULL(second);

    unit_die(caster, NULL);
    T_ASSERT(M_IsDead(first));
    T_NULL(first->mineoverlay->parent);
    T_ASSERT(!(parent1->s.renderfx & RF_HIDDEN));
    T_ASSERT(!parent1->paused);
    T_ASSERT(!G_ActorSkillPermanent(caster, ability));

    gi.Write = old_write;
    gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Depletion must retire an Entangled Mine even when every Wisp has already left it. */
TEST(wc3_movement, empty_entangled_mine_dies_when_parent_is_depleted) {
    slkTestData_t *rows, *old_abilities;
    edict_t *parent, *mine;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    mine = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    setup_test_goldmine(parent, &test_goldmine_stock, 0);
    mine->data.UnitAbilities = &test_entangled_mine;
    mine->think = monster_think;
    mine->health.value = mine->health.max_value = 1000.0f;
    T_ASSERT(S_MineOverlayBind(mine, parent));

    level.time = 0;
    G_RunEntity(mine);
    T_ASSERT(M_IsDead(mine));
    T_ASSERT(!(parent->s.renderfx & RF_HIDDEN));
    T_ASSERT(!parent->paused);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

static char const cargo_unload_test_data[] =
    "ID;PWXL;N;E\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"DataA1\"\n"
    "C;Y1;X4;K\"Dur1\"\n"
    "C;Y2;X1;K\"Acar\"\n"
    "C;Y2;X2;K\"Acar\"\n"
    "C;Y2;X3;K8\n"
    "C;Y2;X4;K0.3\n"
    "E\n";

/* Give cargo scenarios the same lifecycle callbacks as spawned units. */
static edict_t *cargo_unload_transport(void) {
    static UnitAbilities_t const abilities = { .abilList = "Acar,Adro,Adri,Atdp" };
    edict_t *transport = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 256, 256);
    transport->data.UnitAbilities = &abilities;
    transport->think = monster_think; transport->stand = unit_stand; transport->die = unit_die;
    unit_stand(transport);
    FOR_LOOP(i, 3) {
        edict_t *unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 256, 256);
        T_ASSERT(S_CargoTryLoad(transport, unit));
    }
    return transport;
}

TEST(wc3_movement, unload_all_stop_and_move_cancel_remaining_passengers) {
    slkTestData_t *rows = parse_slk_string(cargo_unload_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    setup_test_world();
    FOR_LOOP(i, 2) {
        edict_t *transport = cargo_unload_transport();
        level.time = 1000;
        T_ASSERT(S_CargoBeginUnloadAll(transport));
        T_EQ(transport->cargo->count, 2);
        if (i) order_move(transport, Waypoint_add(&MAKE(vec2_t, 512, 512)));
        else order_stop(transport);
        level.time += 1000; G_RunEntities();
        T_EQ(transport->cargo->count, 2);
        T_ASSERT(transport->cargo->units[0]->paused);
        T_ASSERT(transport->cargo->units[0]->s.renderfx & RF_HIDDEN);
    }
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_movement, unload_all_pause_and_stun_suspend_passengers) {
    slkTestData_t *rows = parse_slk_string(cargo_unload_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    setup_test_world();
    edict_t *transport = cargo_unload_transport();
    level.time = 1000;
    T_ASSERT(S_CargoBeginUnloadAll(transport));
    transport->paused = true;
    level.time += 300; G_RunEntities();
    T_EQ(transport->cargo->count, 2);
    transport->paused = false; transport->stunned = true;
    level.time += 300; G_RunEntities();
    T_EQ(transport->cargo->count, 2);
    transport->stunned = false;
    level.time += 300; G_RunEntities();
    T_EQ(transport->cargo->count, 1);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_movement, unload_all_command_and_instant_dispatch) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    gi.Write = movement_noop_write; gi.unicast = movement_noop_unicast;
    slkTestData_t *rows = parse_slk_string(cargo_unload_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    cstring_t drop[] = { "button", "Adro" }, instant[] = { "button", "Adri" };
    edict_t *clent = &g_edicts[0];
    setup_test_world();
    edict_t *transport = cargo_unload_transport();
    transport->svflags |= SVF_MONSTER;
    G_SelectEntity(clent->client, transport);
    level.time = 1000;
    G_ClientCommand(clent, 2, drop);
    T_NOT_NULL(clent->client->menu.on_location_selected);
    if (clent->client->menu.on_location_selected)
        T_ASSERT(clent->client->menu.on_location_selected(clent, &transport->s.origin2));
    T_EQ(transport->cargo->count, 3); /* Point-target unload waits for move arrival. */
    T_ASSERT(transport->movement.cargo_unload_pending);
    T_ASSERT(S_CargoBeginUnloadAll(transport)); /* Repeated clicks do not bypass Dur. */
    T_EQ(transport->cargo->count, 2);
    G_ClientCommand(clent, 2, instant);
    T_ASSERT(!transport->cargo || transport->cargo->count == 0);
    edict_t *passenger = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 256, 256);
    T_ASSERT(S_CargoTryLoad(transport, passenger));
    level.time += 1000; G_RunEntities();
    T_EQ(transport->cargo->count, 1); /* Instant cancels the old timed unload. */
    gi.Write = old_write; gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_movement, zeppelin_unload_moves_to_selected_point_before_ejecting) {
    cstring_t filename = Test_TempPath("zeppelin-unload-point-save.bin");
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    gi.Write = movement_noop_write; gi.unicast = movement_noop_unicast;
    slkTestData_t *rows = parse_slk_string(cargo_unload_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    cstring_t drop[] = { "button", "Atdp" };
    edict_t *clent = &g_edicts[0];
    vec2_t destination = { 512, 512 };
    setup_test_world();
    edict_t *transport = cargo_unload_transport();
    transport->svflags |= SVF_MONSTER;
    transport->movetype = MOVETYPE_STEP;
    transport->collision = 0;
    transport->abilities.added[0] = MAKEFOURCC('A','c','a','r');
    transport->abilities.added[1] = MAKEFOURCC('A','t','d','p');
    ARRAY_COUNT(transport->abilities.added) = 2;
    G_SelectEntity(clent->client, transport);
    level.time = 1000;
    G_ClientCommand(clent, 2, drop);
    T_NOT_NULL(clent->client->menu.on_location_selected);
    T_ASSERT(clent->client->menu.on_location_selected(clent, &destination));
    T_EQ(transport->cargo->count, 3);
    T_ASSERT(transport->movement.cargo_unload_pending);
    T_EQ(transport->goalentity->s.origin2.x, destination.x);
    T_EQ(transport->goalentity->s.origin2.y, destination.y);
    T_EQ(transport->movement.cargo_unload_ability, MAKEFOURCC('A','t','d','p'));
    level.time += 100;
    T_ASSERT(WriteGame(filename));
    T_ASSERT(ReadGame(filename));
    transport = &g_edicts[transport - g_edicts];
    T_ASSERT(transport->goalentity && transport->goalentity->inuse);
    T_EQ(transport->currentmove->proc, CAbilityMove);
    T_ASSERT(transport->movement.cargo_unload_pending);
    T_EQ(transport->movement.cargo_unload_ability, MAKEFOURCC('A','t','d','p'));
    for (int i = 0; i < 300 && transport->movement.cargo_unload_pending; i++) {
        level.time += FRAMETIME;
        G_RunEntities();
    }
    T_ASSERT(!transport->movement.cargo_unload_pending);
    T_ASSERT(transport->s.origin2.x > 400 && transport->s.origin2.y > 400);
    T_EQ(transport->cargo->count, 2);
    remove(filename);
    gi.Write = old_write; gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_movement, replacement_point_order_cancels_pending_cargo_unload) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    gi.Write = movement_noop_write; gi.unicast = movement_noop_unicast;
    slkTestData_t *rows = parse_slk_string(cargo_unload_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    cstring_t drop[] = { "button", "Adro" };
    edict_t *clent = &g_edicts[0];
    vec2_t unload_point = { 512, 512 }, replacement = { -512, -512 };
    setup_test_world();
    edict_t *transport = cargo_unload_transport();
    transport->svflags |= SVF_MONSTER;
    transport->movetype = MOVETYPE_STEP;
    transport->collision = 0;
    G_SelectEntity(clent->client, transport);
    level.time = 1000;
    G_ClientCommand(clent, 2, drop);
    T_ASSERT(clent->client->menu.on_location_selected(clent, &unload_point));
    T_ASSERT(transport->movement.cargo_unload_pending);
    T_ASSERT(move_selectlocation(clent, &replacement));
    T_EQ(transport->goalentity->s.origin2.x, replacement.x);
    T_EQ(transport->goalentity->s.origin2.y, replacement.y);
    for (int i = 0; i < 600 && transport->currentmove && transport->currentmove->proc == CAbilityMove; i++) {
        level.time += FRAMETIME;
        G_RunEntities();
    }
    T_EQ(transport->cargo->count, 3);
    T_ASSERT(!transport->movement.cargo_unload_pending);
    gi.Write = old_write; gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_movement, cargo_unload_rejects_point_when_no_pathable_cell_exists) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    uint8_t const blocked_pathmap[] = { 2 };
    gi.Write = movement_noop_write; gi.unicast = movement_noop_unicast;
    slkTestData_t *rows = parse_slk_string(cargo_unload_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    cstring_t drop[] = { "button", "Adro" };
    edict_t *clent = &g_edicts[0];
    vec2_t destination = { 300, 300 };
    setup_test_world();
    edict_t *transport = cargo_unload_transport();
    transport->svflags |= SVF_MONSTER;
    transport->collision = 0;
    umove_t *old_move = transport->currentmove;
    edict_t *old_goal = transport->goalentity;
    CM_SetupTestPathmap(1, 1, blocked_pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {0, 0}, .max = {512, 512}));
    G_SelectEntity(clent->client, transport);
    level.time = 1000;
    G_ClientCommand(clent, 2, drop);
    T_ASSERT(clent->client->menu.on_location_selected != NULL);
    if (clent->client->menu.on_location_selected)
        T_ASSERT(!clent->client->menu.on_location_selected(clent, &destination));
    T_EQ(transport->cargo->count, 3);
    T_EQ(transport->currentmove, old_move);
    T_EQ(transport->goalentity, old_goal);
    T_ASSERT(!transport->movement.cargo_unload_pending);
    gi.Write = old_write; gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_movement, cargo_unload_ability_removal_clears_pending_arrival) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    gi.Write = movement_noop_write; gi.unicast = movement_noop_unicast;
    slkTestData_t *rows = parse_slk_string(cargo_unload_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    cstring_t drop[] = { "button", "Atdp" };
    edict_t *clent = &g_edicts[0];
    vec2_t destination = { 512, 512 };
    setup_test_world();
    edict_t *transport = cargo_unload_transport();
    transport->svflags |= SVF_MONSTER;
    transport->collision = 0;
    G_SelectEntity(clent->client, transport);
    level.time = 1000;
    G_ClientCommand(clent, 2, drop);
    if (clent->client->menu.on_location_selected)
        T_ASSERT(clent->client->menu.on_location_selected(clent, &destination));
    T_ASSERT(transport->movement.cargo_unload_pending);
    abilityitem_t item = S_AbilityItem(MAKEFOURCC('A','t','d','p'));
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    T_ASSERT(S_AbilityMessage(transport, A_DISABLE, &call));
    T_ASSERT(!transport->movement.cargo_unload_pending);
    T_EQ(transport->cargo->count, 3);
    gi.Write = old_write; gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_movement, unload_all_round_trip_resumes_remaining_cargo) {
    cstring_t filename = Test_TempPath("cargo-unload-save.bin");
    slkTestData_t *rows = parse_slk_string(cargo_unload_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    setup_test_world();
    edict_t *transport = cargo_unload_transport();
    /* Runtime abilities survive data-pointer rebinding during ReadGame. */
    transport->abilities.added[0] = MAKEFOURCC('A','c','a','r');
    ARRAY_COUNT(transport->abilities.added) = 1;
    level.time = 1000;
    T_ASSERT(S_CargoBeginUnloadAll(transport));
    edict_t *second = transport->cargo->units[0], *third = transport->cargo->units[1];
    level.time += 100;
    T_ASSERT(WriteGame(filename));
    order_stop(transport); cargo_drop_all(transport);
    T_ASSERT(ReadGame(filename));
    T_EQ(transport->cargo->count, 2);
    T_EQ(transport->cargo->units[0], second);
    T_EQ(transport->cargo->units[1], third);
    level.time = 1299; G_RunEntities(); T_EQ(transport->cargo->count, 2);
    level.time = 1300; G_RunEntities(); T_EQ(transport->cargo->count, 1);
    T_ASSERT(!second->paused && !(second->s.renderfx & RF_HIDDEN));
    level.time = 1600; G_RunEntities(); T_ASSERT(!transport->cargo || transport->cargo->count == 0);
    T_ASSERT(!third->paused && !(third->s.renderfx & RF_HIDDEN));
    remove(filename);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_movement, unload_all_zero_duration_roc_hold_and_single_slot) {
    slkTestData_t *rows = parse_slk_string(
        "ID;PWXL;N;E\nC;Y1;X1;K\"alias\"\nC;X2;K\"Data11\"\nC;X3;K\"Dur1\"\n"
        "C;Y2;X1;K\"Acar\"\nC;X2;K8\nC;X3;K0\nE\n");
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    setup_test_world();
    edict_t *transport = cargo_unload_transport();
    T_ASSERT(S_CargoUnloadAt(transport, 1));
    level.time = 1000; G_RunEntities();
    T_EQ(transport->cargo->count, 2); /* A cargo-slot click only ejects that passenger. */
    T_ASSERT(S_CargoBeginUnloadAll(transport));
    T_EQ(transport->cargo->count, 1);
    G_RunEntities(); T_EQ(transport->cargo->count, 1);
    level.time += FRAMETIME; G_RunEntities(); T_ASSERT(!transport->cargo || transport->cargo->count == 0);
    T_ASSERT(!S_CargoBeginUnloadAll(transport));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_movement, unload_all_transport_death_ejects_remaining_cargo) {
    slkTestData_t *rows = parse_slk_string(cargo_unload_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    setup_test_world();
    edict_t *transport = cargo_unload_transport();
    level.time = 1000;
    T_ASSERT(S_CargoBeginUnloadAll(transport));
    edict_t *passenger = transport->cargo->units[0];
    transport->health.value = 0; unit_die(transport, NULL);
    T_ASSERT(!transport->cargo || transport->cargo->count == 0);
    T_ASSERT(!passenger->paused && !(passenger->s.renderfx & RF_HIDDEN));
    level.time += 1000; G_RunEntities();
    T_ASSERT(!S_CargoBeginUnloadAll(transport));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_movement, unload_all_repeats_one_passenger_per_cargo_duration) {
    static UnitAbilities_t const transport_abilities = { .abilList = "Acar" };
    slkTestData_t *rows = parse_slk_string(cargo_unload_test_data);
    slkTestData_t *old_abilities;
    edict_t *transport, *first, *second, *third;

    reset_entities();
    setup_test_world();
    old_abilities = G_SetSLKRows("AbilityData", rows);
    transport = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 256.0f, 256.0f);
    first = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 256.0f, 256.0f);
    second = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 256.0f, 256.0f);
    third = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 256.0f, 256.0f);
    transport->data.UnitAbilities = &transport_abilities;
    transport->think = monster_think; transport->stand = unit_stand;
    if (!transport->cargo) transport->cargo = G_AllocCargo();
    assert(transport->cargo);
    transport->cargo->units[0] = first;
    transport->cargo->units[1] = second;
    transport->cargo->units[2] = third;
    transport->cargo->count = 3;
    first->s.renderfx |= RF_HIDDEN; first->paused = true;
    second->s.renderfx |= RF_HIDDEN; second->paused = true;
    third->s.renderfx |= RF_HIDDEN; third->paused = true;
    level.time = 1000;

    T_ASSERT(S_CargoBeginUnloadAll(transport));
    T_EQ(transport->cargo->count, 2);
    T_ASSERT(!(first->s.renderfx & RF_HIDDEN));
    T_ASSERT(!first->paused);
    T_ASSERT(second->s.renderfx & RF_HIDDEN);
    T_ASSERT(third->s.renderfx & RF_HIDDEN);

    level.time += 299;
    G_RunEntities();
    T_EQ(transport->cargo->count, 2);

    level.time += 1;
    G_RunEntities();
    T_EQ(transport->cargo->count, 1);
    T_ASSERT(!(second->s.renderfx & RF_HIDDEN));
    T_ASSERT(!second->paused);
    T_ASSERT(third->s.renderfx & RF_HIDDEN);

    level.time += 300;
    G_RunEntities();
    T_ASSERT(!transport->cargo || transport->cargo->count == 0);
    T_ASSERT(!(third->s.renderfx & RF_HIDDEN));
    T_ASSERT(!third->paused);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, occupied_burrow_exposes_attack_stop_and_stand_down_only_with_cargo) {
    static UnitAbilities_t const burrow_abilities = {
        .id = MAKEFOURCC('o','b','u','r'),
        .abilList = "Abun",
    };
    static UnitWeapons_t const burrow_weapons = {
        .id = MAKEFOURCC('o','b','u','r'),
        .attacksEnabled = 3,
        .attack1 = { .damageDice = 1 },
    };
    static UnitBalance_t const burrow_balance = {
        .id = MAKEFOURCC('o','b','u','r'),
        .speed = 0,
    };
    edict_t *burrow = alloc_test_unit(MAKEFOURCC('o','b','u','r'), 256.0f, 256.0f);
    edict_t *peon = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 256.0f, 256.0f);
    gameCommandButton_t buttons[16];
    uint8_t count;
    bool attack, stop, stand_down;

    burrow->data.UnitAbilities = &burrow_abilities;
    burrow->data.UnitWeapons = &burrow_weapons;
    burrow->data.UnitBalance = &burrow_balance;

    count = G_GetCommandButtons(burrow, buttons, (uint8_t)(sizeof(buttons) / sizeof(buttons[0])));
    attack = stop = stand_down = false;
    FOR_LOOP(i, count) {
        if (!strcmp(buttons[i].command, STR_CmdAttack)) attack = true;
        if (!strcmp(buttons[i].command, STR_CmdStop)) stop = true;
        if (!strcmp(buttons[i].command, "Astd")) stand_down = true;
    }
    T_ASSERT(!attack);
    T_ASSERT(!stop);
    T_ASSERT(!stand_down);

    if (!burrow->cargo) burrow->cargo = G_AllocCargo();
    assert(burrow->cargo);
    burrow->cargo->units[0] = peon;
    burrow->cargo->count = 1;
    count = G_GetCommandButtons(burrow, buttons, (uint8_t)(sizeof(buttons) / sizeof(buttons[0])));
    attack = stop = stand_down = false;
    FOR_LOOP(i, count) {
        if (!strcmp(buttons[i].command, STR_CmdAttack)) attack = true;
        if (!strcmp(buttons[i].command, STR_CmdStop)) stop = true;
        if (!strcmp(buttons[i].command, "Astd")) stand_down = true;
    }
    T_ASSERT(attack);
    T_ASSERT(stop);
    T_ASSERT(stand_down);
}

TEST(wc3_movement, stand_down_stops_attack_before_unloading_burrow) {
    static UnitAbilities_t const burrow_abilities = {
        .id = MAKEFOURCC('o','b','u','r'),
        .abilList = "Abun",
    };
    edict_t *burrow = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 256.0f, 256.0f);
    edict_t *peon = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 256.0f, 256.0f);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 320.0f, 256.0f);

    burrow->data.UnitAbilities = &burrow_abilities;
    burrow->stand = unit_stand;
    if (!burrow->cargo) burrow->cargo = G_AllocCargo();
    assert(burrow->cargo);
    burrow->cargo->units[0] = peon;
    burrow->cargo->count = 1;
    peon->s.renderfx |= RF_HIDDEN;
    peon->paused = true;
    burrow->attack1.type = ATK_PIERCE;
    burrow->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    target->targtype = TARG_GROUND;

    order_attack(burrow, target);
    T_NOT_NULL(burrow->currentmove);
    T_ASSERT(burrow->currentmove->proc == CAbilityAttack);
    T_ASSERT(burrow->combatentity == target);

    S_CargoStandDown(burrow);

    T_ASSERT(!burrow->cargo || burrow->cargo->count == 0);
    T_ASSERT(!(peon->s.renderfx & RF_HIDDEN));
    T_ASSERT(!peon->paused);
    T_NOT_NULL(burrow->currentmove);
    T_ASSERT(burrow->currentmove->proc != CAbilityAttack);
    T_NULL(burrow->combatentity);
    T_NULL(burrow->goalentity);
}

TEST(wc3_movement, removing_loaded_unit_releases_transport_slot) {
    edict_t *transport, *passenger;

    reset_entities();
    setup_test_world();
    transport = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    passenger = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    if (!transport->cargo) transport->cargo = G_AllocCargo();
    assert(transport->cargo);
    transport->cargo->units[0] = passenger;
    transport->cargo->count = 1;
    passenger->s.renderfx |= RF_HIDDEN;
    passenger->paused = true;

    G_FreeEdict(passenger);
    T_ASSERT(!transport->cargo || transport->cargo->count == 0);
    T_NULL(transport->cargo->units[0]);
    T_ASSERT(!passenger->inuse);
}

TEST(wc3_movement, cargo_unload_at_releases_requested_occupant_and_keeps_remaining_order) {
    edict_t *burrow = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 256.0f, 256.0f);
    edict_t *first = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 256.0f, 256.0f);
    edict_t *second = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 320.0f, 256.0f);

    first->s.renderfx |= RF_HIDDEN; first->paused = true;
    second->s.renderfx |= RF_HIDDEN; second->paused = true;
    if (!burrow->cargo) burrow->cargo = G_AllocCargo();
    assert(burrow->cargo);
    burrow->cargo->units[0] = first; burrow->cargo->units[1] = second; burrow->cargo->count = 2;

    T_ASSERT(S_CargoTransportForUnit(first) == burrow);
    T_ASSERT(S_CargoUnloadAt(burrow, 0));
    T_EQ(burrow->cargo->count, 1);
    T_ASSERT(S_CargoUnitAt(burrow, 0) == second);
    T_NULL(S_CargoTransportForUnit(first));
    T_ASSERT(!(first->s.renderfx & RF_HIDDEN));
    T_ASSERT(!first->paused);
    T_ASSERT(second->s.renderfx & RF_HIDDEN);
    T_ASSERT(second->paused);
}

/* -----------------------------------------------------------------------
 * Suite runner
 * --------------------------------------------------------------------- */

#endif /* BZ_TESTS */
