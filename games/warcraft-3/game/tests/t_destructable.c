/*
 * t_destructable.c â€” Breakable destructable lifecycle tests.
 *
 * Covers placement life/flags, normal combat damage, callback-independent
 * one-time death, targetability, death animation state, and reversible static
 * pathing footprints.
 */
#ifdef BZ_TESTS

#include "test.h"
#include "../g_local.h"

void setup_test_pathmap(uint32_t width, uint32_t height, uint8_t const *cells);
void setup_test_world(void);
void reset_entities(void);
void CM_SetupTestWorldBounds(box2_t const *bounds);
bool CM_LineIsWalkableForRadius(vec2_t const *a, vec2_t const *b, float radius);
bool CM_PointIsPathableForRadius(vec2_t const *location, float radius);
uint32_t CM_BuildHeatmapForRadius(edict_t *goalentity, float radius);
bool CM_FlowCanReach(uint32_t generation, float x, float y);
edict_t *Waypoint_add(vec2_t const *spot);
bool unit_issuetargetorder(edict_t *self, cstring_t order, edict_t *target);
void T_Damage(edict_t *target, edict_t *attacker, int damage);
bool run_test_jass(cstring_t src);
slkTestData_t *parse_slk_string(char const *slk_text);
void free_slk_rows(slkTestData_t *rows);

TEST(wc3_destructable, unknown_entity_without_data_is_not_destructable) {
    edict_t ent = { .inuse = true, .class_id = MAKEFOURCC('d', 'u', 'm', 'y') };
    T_ASSERT(!G_IsDestructable(&ent));
}

typedef struct {
    uint16_t width;
    uint16_t height;
    color32_t map[1];
} one_cell_pathtex_t;

static one_cell_pathtex_t destructable_blocked_death_pathtex = {
    .width = 1,
    .height = 1,
    .map = { { 0, 0, 1, 255 } },
};

typedef struct {
    uint16_t width, height;
    color32_t map[15];
} bridge_band_pathtex_t;

/* Clear padding | blocked rail | clear deck | blocked rail | clear padding. */
static bridge_band_pathtex_t destructable_bridge_band_pathtex = {
    .width = 5,
    .height = 3,
    .map = {
        {0,0,0,255}, {0,0,1,255}, {0,0,0,255}, {0,0,1,255}, {0,0,0,255},
        {0,0,0,255}, {0,0,1,255}, {0,0,0,255}, {0,0,1,255}, {0,0,0,255},
        {0,0,0,255}, {0,0,1,255}, {0,0,0,255}, {0,0,1,255}, {0,0,0,255},
    },
};

typedef enum {
    BRIDGE_X,
    BRIDGE_Y,
    BRIDGE_DIAGONAL,
} bridge_axis_t;

typedef struct {
    uint32_t id;
    uint16_t width, height;
    bridge_axis_t axis;
    cstring_t mask;
} human06_bridge_fixture_t;

/* These masks are the red channel of the retail pathing TGAs; LoadTGA stores
 * that channel in COLOR32.b, which is the routing blocker channel. */
static cstring_t const human06_bridge_large135_mask =
    "..................####.........." "..................####.........."
    "................####............" "................####............"
    "..............####.............." "..............####.............."
    "............####................" "............####................"
    "..........####................##" "..........####................##"
    "........####................####" "........####................####"
    "......####................######" "......####................######"
    "....####................######.." "....####................######.."
    "..####................######...." "..####................######...."
    "####................######......" "####................######......"
    "##................######........" "##................######........"
    "................######.........." "................######.........."
    "..............######............" "..............######............"
    "............######.............." "............######.............."
    "..........######................" "..........######................"
    "........######.................." "........######..................";

static cstring_t const human06_bridge_extra0_mask =
    "################################" "################################"
    "####....####........####....####" "####....####........####....####"
    "................................" "................................"
    "................................" "................................"
    "................................" "................................"
    "................................" "................................"
    "................................" "................................"
    "................................" "................................"
    "................................" "................................"
    "################################" "################################"
    "################################" "################################";

static cstring_t const human06_bridge_extra90_mask =
    "####..............####" "####..............####" "####..............####" "####..............####"
    "##................####" "##................####" "##................####" "##................####"
    "####..............####" "####..............####" "####..............####" "####..............####"
    "##................####" "##................####" "##................####" "##................####"
    "##................####" "##................####" "##................####" "##................####"
    "####..............####" "####..............####" "####..............####" "####..............####"
    "##................####" "##................####" "##................####" "##................####"
    "####..............####" "####..............####" "####..............####" "####..............####";

static human06_bridge_fixture_t const human06_bridge_fixtures[] = {
    { MAKEFOURCC('Y', 'T', '1', '9'), 32, 32, BRIDGE_DIAGONAL, human06_bridge_large135_mask },
    { MAKEFOURCC('Y', 'T', '2', '0'), 32, 22, BRIDGE_X, human06_bridge_extra0_mask },
    { MAKEFOURCC('Y', 'T', '2', '2'), 22, 32, BRIDGE_Y, human06_bridge_extra90_mask },
};

typedef struct {
    uint16_t width, height;
    color32_t map[32 * 32];
} human06_bridge_pathtex_t;

static human06_bridge_pathtex_t make_human06_bridge_pathtex(human06_bridge_fixture_t const *fixture) {
    human06_bridge_pathtex_t pathtex = { .width = fixture->width, .height = fixture->height };

    FOR_LOOP(y, fixture->height) FOR_LOOP(x, fixture->width) {
        pathtex.map[x + y * fixture->width] = MAKE(color32_t, .r = 255, .g = 255,
            .b = fixture->mask[x + y * fixture->width] == '.' ? 0 : 255, .a = 255);
    }
    return pathtex;
}

static edict_t *make_test_destructable(float life, float x, float y) {
    edict_t *ent = G_Spawn();

    ent->class_id = MAKEFOURCC('B', '0', '0', 'X');
    ent->s.class_id = ent->class_id;
    G_BindEntityData(ent);
    ent->s.model = 1;
    ent->s.scale = 1.0f;
    ent->s.origin = (vec3_t){ x, y, 0.0f };
    ent->targtype = TARG_DEBRIS;
    ent->health.value = life;
    ent->health.max_value = life;
    ent->destructable.initialized = true;
    ent->destructable.placement_solid = true;
    ent->destructable.alive_collision = 1.0f;
    ent->destructable.pathing_active = true;
    ent->collision = 1.0f;
    return ent;
}

TEST(wc3_destructable, blight_presentation_is_initial_and_one_way) {
    vec2_t point = { 32.0f, 32.0f };
    edict_t *tree;

    setup_test_world();
    tree = make_test_destructable(100.0f, 32.0f, 32.0f);
    G_SetBlightPoint(&point, true);
    T_ASSERT(G_IsPointBlighted(&point));
    T_ASSERT(G_IsDestructable(tree));
    G_BlightInitializeDestructable(tree);
    T_ASSERT(tree->destructable.blighted);
    T_ASSERT(tree->vertex_color_set);
    T_EQ(tree->vertex_color.r, 120);
    T_EQ(tree->vertex_color.g, 185);
    T_EQ(tree->vertex_color.b, 72);
    T_EQ(tree->vertex_color.a, 255);
    G_SetBlightPoint(&point, false);
    T_ASSERT(tree->destructable.blighted);
}

static edict_t *make_destructable_test_attacker(float x, float y) {
    static UnitWeapons_t const weapons = { .attacksEnabled = 3 };
    edict_t *ent = G_Spawn();

    ent->class_id = MAKEFOURCC('h', 'f', 'o', 'o');
    ent->s.class_id = ent->class_id;
    G_BindEntityData(ent);
    ent->data.UnitWeapons = &weapons;
    ent->s.model = 1;
    ent->s.origin = (vec3_t){ x, y, 0.0f };
    ent->health.value = 100.0f;
    ent->health.max_value = 100.0f;
    ent->svflags |= SVF_MONSTER;
    ent->attack1.type = ATK_NORMAL;
    ent->attack1.targetsAllowed = 256u; /* TARGET_FLAG_DEBRIS */
    return ent;
}

TEST(wc3_destructable, metadata_loads_alive_and_dead_bridge_resources) {
    static cstring_t const slk =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"ID\"\n"
        "C;Y1;X2;K\"file\"\n"
        "C;Y1;X3;K\"pathTex\"\n"
        "C;Y1;X4;K\"pathTexDeath\"\n"
        "C;Y1;X5;K\"walkable\"\n"
        "C;Y2;X1;K\"LT05\"\n"
        "C;Y2;X2;K\"Doodads\\Terrain\\WoodBridgeLarge45\\WoodBridgeLarge45.mdx\"\n"
        "C;Y2;X3;K\"PathTextures\\CityBridgeLarge45.tga\"\n"
        "C;Y2;X4;K\"PathTextures\\CityBridgeLarge45Death.tga\"\n"
        "C;Y2;X5;K1\n"
        "E\n";
    slkTestData_t *rows = parse_slk_string(slk);
    slkTestData_t *saved = G_SetSLKRows("DestructableData", rows);
    DestructableData_t const *bridge = G_DestructableData(MAKEFOURCC('L', 'T', '0', '5'));

    T_STREQ(bridge->file, "Doodads\\Terrain\\WoodBridgeLarge45\\WoodBridgeLarge45.mdx");
    T_STREQ(bridge->pathingTexture, "PathTextures\\CityBridgeLarge45.tga");
    T_STREQ(bridge->deathPathingTexture, "PathTextures\\CityBridgeLarge45Death.tga");
    T_ASSERT(bridge->walkable);

    G_SetSLKRows("DestructableData", saved);
    free_slk_rows(rows);
}

TEST(wc3_destructable, wood_bridge_uses_authoritative_animation_intervals) {
    animation_t animations[] = {
        { .name = "Stand", .interval = { 133, 1333 }, .flags = 0 },
        { .name = "Death", .interval = { 2000, 3000 }, .flags = 1 },
        { .name = "Birth", .interval = { 3333, 10000 }, .flags = 1 },
    };
    animation_t const *stand = G_SelectAnimationForProperties(animations, 3, "stand", NULL);
    animation_t const *death = G_SelectAnimationForProperties(animations, 3, "death", NULL);
    animation_t const *birth = G_SelectAnimationForProperties(animations, 3, "birth", NULL);

    T_NOT_NULL(stand);
    T_EQ(stand->interval[0], 133);
    T_EQ(stand->interval[1], 1333);
    T_EQ(stand->flags, 0);
    T_NOT_NULL(death);
    T_EQ(death->interval[0], 2000);
    T_EQ(death->interval[1], 3000);
    T_EQ(death->flags, 1);
    T_NOT_NULL(birth);
    T_EQ(birth->interval[0], 3333);
    T_EQ(birth->interval[1], 10000);
    T_EQ(birth->flags, 1);
}

TEST(wc3_destructable, placement_applies_life_flags_and_editor_id) {
    edict_t *dest = make_test_destructable(200.0f, 0.0f, 0.0f);
    doodad_t placement = {
        .flags = 2,
        .treeLife = 40,
        .unitID = 12345,
    };

    G_InitializeDestructablePlacement(dest, &placement);

    T_FEQ(dest->health.value, 80.0f, 0.01f);
    T_EQ(dest->destructable.editor_id, 12345);
    T_ASSERT(dest->destructable.placement_solid);
    T_ASSERT(dest->destructable.pathing_active);
    T_ASSERT(!(dest->s.renderfx & RF_HIDDEN));
    T_ASSERT(G_DestructableIsAttackable(dest));
}

TEST(wc3_destructable, hidden_nonsolid_placement_is_not_targetable) {
    edict_t *dest = make_test_destructable(100.0f, 0.0f, 0.0f);
    doodad_t placement = { .flags = 0, .treeLife = 100 };

    G_InitializeDestructablePlacement(dest, &placement);

    T_ASSERT(dest->s.renderfx & RF_HIDDEN);
    T_ASSERT(dest->s.flags & EF_NOT_SELECTABLE);
    T_ASSERT(!dest->destructable.placement_solid);
    T_ASSERT(!dest->destructable.pathing_active);
    T_ASSERT(!G_DestructableIsAttackable(dest));
}

TEST(wc3_destructable, generated_script_reuses_and_activates_hidden_placement) {
    doodad_t placement = { .flags = 0, .treeLife = 100, .unitID = 77 };
    edict_t *dest, *created;
    uint32_t count;

    setup_test_world();
    dest = make_test_destructable(100.0f, 64.0f, 96.0f);
    G_InitializeDestructablePlacement(dest, &placement);
    count = globals.num_edicts;
    G_SetDestructableScriptBinding(true);
    created = G_CreateDestructable(dest->class_id, 64.0f, 96.0f, 0.0f, 0.5f, 1.25f, 2);
    G_SetDestructableScriptBinding(false);

    T_ASSERT(created == dest);
    T_EQ(globals.num_edicts, count);
    T_ASSERT(dest->destructable.script_bound);
    T_ASSERT(dest->destructable.placement_solid);
    T_ASSERT(!(dest->s.renderfx & (RF_HIDDEN | RF_NO_SHADOW)));
    T_ASSERT(!(dest->s.flags & EF_NOT_SELECTABLE));
    T_FEQ(dest->health.value, dest->health.max_value, 0.01f);
    T_FEQ(dest->s.origin2.x, 64.0f, 0.01f);
    T_FEQ(dest->s.origin2.y, 96.0f, 0.01f);
}

TEST(wc3_destructable, instant_kill_cheat_makes_gate_damage_lethal) {
    edict_t *dest, *attacker;

    setup_test_world();
    dest = make_test_destructable(500.0f, 0.0f, 0.0f);
    dest->class_id = MAKEFOURCC('L', 'T', 'g', '1');
    dest->s.class_id = dest->class_id;
    dest->targtype = TARG_WALL;
    attacker = make_destructable_test_attacker(10.0f, 0.0f);
    attacker->s.player = 0;
    attacker->attack1.targetsAllowed = 128u; /* TARGET_FLAG_WALL */
    game.clients[0].cheat_instant_kill = true;

    dest->s.renderfx |= RF_HIDDEN;
    T_Damage(dest, attacker, 1);
    T_ASSERT(!dest->destructable.dead);
    T_FEQ(dest->health.value, 499.0f, 0.01f);

    dest->s.renderfx &= ~RF_HIDDEN;
    T_Damage(dest, attacker, 1);

    T_ASSERT(dest->destructable.dead);
    T_FEQ(dest->health.value, 0.0f, 0.01f);
}

TEST(wc3_destructable, lethal_damage_does_not_require_die_callback) {
    edict_t *dest = make_test_destructable(25.0f, 0.0f, 0.0f);
    edict_t *attacker = make_destructable_test_attacker(10.0f, 0.0f);

    dest->die = NULL;
    T_Damage(dest, attacker, 25);

    T_ASSERT(dest->destructable.dead);
    T_FEQ(dest->health.value, 0.0f, 0.01f);
    T_ASSERT(dest->svflags & SVF_DEADMONSTER);
    T_ASSERT(dest->s.flags & EF_NOT_SELECTABLE);
    T_ASSERT(dest->s.renderfx & RF_NO_SHADOW);
    T_ASSERT(!(dest->s.renderfx & RF_HIDDEN));
    T_ASSERT(!G_DestructableIsAttackable(dest));
    T_NOT_NULL(dest->currentmove);
    T_STREQ(dest->currentmove->animation, "death");
}

static int death_callback_count;

static void count_death_callback(edict_t *self, edict_t *attacker) {
    (void)self;
    (void)attacker;
    death_callback_count++;
}

TEST(wc3_destructable, death_transition_event_and_callback_fire_once) {
    edict_t *dest = make_test_destructable(10.0f, 0.0f, 0.0f);
    edict_t *attacker = make_destructable_test_attacker(10.0f, 0.0f);

    death_callback_count = 0;
    dest->die = count_death_callback;
    T_Damage(dest, attacker, 10);
    T_Damage(dest, attacker, 10);
    G_KillDestructable(dest, attacker);

    T_EQ(death_callback_count, 1);
    T_EQ(level.events.write, 1);
    T_EQ(level.events.queue[0].type, EVENT_UNIT_DEATH);
    T_ASSERT(level.events.queue[0].edict == dest);
    T_ASSERT(level.events.queue[0].source == attacker);
}

TEST(wc3_destructable, smart_order_attacks_neutral_destructable) {
    edict_t *attacker = make_destructable_test_attacker(0.0f, 0.0f);
    edict_t *dest = make_test_destructable(50.0f, 32.0f, 0.0f);

    dest->s.player = PLAYER_NEUTRAL_PASSIVE;

    T_ASSERT(unit_issuetargetorder(attacker, "smart", dest));
    T_ASSERT(attacker->goalentity == dest);
    T_ASSERT(attacker->combatentity == dest);
}

TEST(wc3_destructable, smart_order_requires_destructable_target_mask) {
    edict_t *attacker = make_destructable_test_attacker(0.0f, 0.0f);
    edict_t *dest = make_test_destructable(50.0f, 32.0f, 0.0f);

    attacker->attack1.targetsAllowed = 64u; /* TARGET_FLAG_TREE only */

    T_ASSERT(!unit_issuetargetorder(attacker, "smart", dest));
    T_ASSERT(attacker->goalentity == NULL);
}

TEST(wc3_destructable, tree_requires_explicit_attack) {
    edict_t *attacker = make_destructable_test_attacker(0.0f, 0.0f);
    edict_t *dest = make_test_destructable(50.0f, 32.0f, 0.0f);

    dest->targtype = TARG_TREE;
    /* Standard melee targs1 commonly contains debris but not tree. Retail
     * still lets explicit Attack cut a tree down. */
    attacker->attack1.targetsAllowed = 256u; /* TARGET_FLAG_DEBRIS */

    T_ASSERT(!unit_issuetargetorder(attacker, "smart", dest));
    T_ASSERT(unit_issuetargetorder(attacker, "attack", dest));
    T_ASSERT(attacker->goalentity == dest);
}

TEST(wc3_destructable, explicit_attack_rejects_disallowed_destructable_class) {
    edict_t *attacker = make_destructable_test_attacker(0.0f, 0.0f);
    edict_t *dest = make_test_destructable(50.0f, 32.0f, 0.0f);

    dest->targtype = TARG_BRIDGE;
    attacker->attack1.targetsAllowed = 256u; /* TARGET_FLAG_DEBRIS only */

    T_ASSERT(!unit_issuetargetorder(attacker, "smart", dest));
    T_ASSERT(!unit_issuetargetorder(attacker, "attack", dest));
    T_ASSERT(attacker->goalentity == NULL);
}

TEST(wc3_destructable, explicit_attack_accepts_allowed_bridge) {
    edict_t *attacker = make_destructable_test_attacker(0.0f, 0.0f);
    edict_t *dest = make_test_destructable(50.0f, 32.0f, 0.0f);

    dest->targtype = TARG_BRIDGE;
    attacker->attack1.targetsAllowed = 1024u; /* TARGET_FLAG_BRIDGE */

    T_ASSERT(!unit_issuetargetorder(attacker, "smart", dest));
    T_ASSERT(unit_issuetargetorder(attacker, "attack", dest));
    T_ASSERT(attacker->goalentity == dest);
}

TEST(wc3_destructable, dead_remains_reject_attack_orders) {
    edict_t *attacker = make_destructable_test_attacker(0.0f, 0.0f);
    edict_t *dest = make_test_destructable(1.0f, 32.0f, 0.0f);

    G_KillDestructable(dest, attacker);

    T_ASSERT(!unit_issuetargetorder(attacker, "smart", dest));
    T_ASSERT(!unit_issuetargetorder(attacker, "attack", dest));
}

TEST(wc3_destructable, death_removes_alive_static_footprint) {
    uint8_t cells[8 * 8] = { 0 };
    vec2_t center = { 4.0f, 4.0f };
    edict_t *dest;

    setup_test_pathmap(8, 8, cells);
    dest = make_test_destructable(10.0f, center.x, center.y);
    CM_BakeStaticObstacles();
    T_ASSERT(!CM_PointIsPathableForRadius(&center, 0.0f));

    G_KillDestructable(dest, NULL);
    T_ASSERT(CM_PointIsPathableForRadius(&center, 0.0f));
}

TEST(wc3_destructable, death_replacement_pathing_remains_blocking) {
    uint8_t cells[8 * 8] = { 0 };
    vec2_t center = { 4.0f, 4.0f };
    edict_t *dest;

    setup_test_pathmap(8, 8, cells);
    dest = make_test_destructable(10.0f, center.x, center.y);
    dest->destructable.death_pathtex = (pathTex_t *)&destructable_blocked_death_pathtex;

    G_KillDestructable(dest, NULL);

    T_ASSERT(dest->destructable.pathing_active);
    T_ASSERT(dest->pathtex == (pathTex_t *)&destructable_blocked_death_pathtex);
    T_ASSERT(!CM_PointIsPathableForRadius(&center, 0.0f));
}

TEST(wc3_destructable, alive_walkable_bridge_opens_terrain_until_death) {
    static DestructableData_t const bridge_data = { .walkable = true };
    uint8_t cells[8 * 8] = { 0 };
    vec2_t center = { 4.0f, 4.0f };
    edict_t *bridge, *unit;

    cells[4 + 4 * 8] = 2; /* terrain no-walk under the bridge deck */
    setup_test_pathmap(8, 8, cells);
    bridge = make_test_destructable(10.0f, center.x, center.y);
    bridge->data.DestructableData = &bridge_data;
    bridge->destructable.alive_pathtex = (pathTex_t *)&destructable_bridge_band_pathtex;
    bridge->destructable.death_pathtex = (pathTex_t *)&destructable_blocked_death_pathtex;
    bridge->pathtex = bridge->destructable.alive_pathtex;
    bridge->collision = bridge->destructable.alive_collision = 32.0f;
    /* Start on the clear deck lane; approaching from x=2 would cross the
     * authored rail at x=3 and should correctly be rejected. */
    unit = make_destructable_test_attacker(center.x, center.y - 1.0f);
    /* This assertion isolates bridge surface pathing; a one-cell deck cannot
     * fit a radius-one footprint without touching its authored rails. */
    unit->collision = 0.0f;
    G_RegisterGroundSurface(bridge);
    T_ASSERT(bridge->s.flags & EF_GROUND_SURFACE);

    CM_BakeStaticObstacles();
    T_ASSERT(CM_PointIsPathableForRadius(&center, 0.0f));
    T_ASSERT(M_MoveIsValid(unit, &center));

    G_KillDestructable(bridge, NULL);
    T_ASSERT(!(bridge->s.flags & EF_GROUND_SURFACE));
    T_ASSERT(!CM_PointIsPathableForRadius(&center, 0.0f));
}

TEST(wc3_destructable, alive_walkable_bridge_preserves_clear_padding_outside_rails) {
    static DestructableData_t const bridge_data = { .walkable = true };
    uint8_t cells[9 * 7];
    vec2_t center = { 4.0f, 3.0f };
    vec2_t deck = { 4.0f, 3.0f };
    vec2_t left_outside = { 2.0f, 3.0f };
    vec2_t right_outside = { 6.0f, 3.0f };
    vec2_t left_rail = { 3.0f, 3.0f };
    edict_t *bridge;

    memset(cells, 2, sizeof(cells)); /* river no-walk across the whole footprint */
    setup_test_pathmap(9, 7, cells);
    bridge = make_test_destructable(10.0f, center.x, center.y);
    bridge->data.DestructableData = &bridge_data;
    bridge->destructable.alive_pathtex = (pathTex_t *)&destructable_bridge_band_pathtex;
    bridge->pathtex = bridge->destructable.alive_pathtex;

    CM_BakeStaticObstacles();

    T_ASSERT(CM_PointIsPathableForRadius(&deck, 0.0f));
    T_ASSERT(!CM_PointIsPathableForRadius(&left_rail, 0.0f));
    T_ASSERT(!CM_PointIsPathableForRadius(&left_outside, 0.0f));
    T_ASSERT(!CM_PointIsPathableForRadius(&right_outside, 0.0f));
}

TEST(wc3_destructable, human06_bridge_fixtures_cross_from_both_sides) {
    static DestructableData_t const bridge_data = { .walkable = true };

    FOR_LOOP(fixture_index, sizeof(human06_bridge_fixtures) / sizeof(human06_bridge_fixtures[0])) {
        human06_bridge_fixture_t const *fixture = &human06_bridge_fixtures[fixture_index];

        uint8_t cells[64 * 64];
        human06_bridge_pathtex_t pathtex = make_human06_bridge_pathtex(fixture);
        vec2_t axis = fixture->axis == BRIDGE_X ? MAKE(vec2_t, 0.0f, 1.0f) :
            fixture->axis == BRIDGE_Y ? MAKE(vec2_t, 1.0f, 0.0f) : MAKE(vec2_t, 1.0f, -1.0f);
        float const extent = fixture->axis == BRIDGE_DIAGONAL ? 192.0f : 320.0f;
        vec2_t from = MAKE(vec2_t, -extent * axis.x, -extent * axis.y);
        vec2_t to = fixture->axis == BRIDGE_DIAGONAL ? MAKE(vec2_t, 320.0f, -192.0f) :
            MAKE(vec2_t, extent * axis.x, extent * axis.y);
        edict_t *bridge, *goal;
        uint32_t generation;

        memset(cells, 2, sizeof(cells));
        reset_entities();
        setup_test_world();
        setup_test_pathmap(64, 64, cells);
        CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {-1024.0f, -1024.0f}, .max = {1024.0f, 1024.0f}));
        bridge = make_test_destructable(2500.0f, 0.0f, 0.0f);
        bridge->class_id = fixture->id;
        bridge->s.class_id = fixture->id;
        bridge->s.origin2 = (vec2_t){ 0.0f, 0.0f };
        bridge->data.DestructableData = &bridge_data;
        bridge->destructable.alive_pathtex = (pathTex_t *)&pathtex;
        bridge->pathtex = (pathTex_t *)&pathtex;
        bridge->targtype = TARG_BRIDGE;
        G_RegisterGroundSurface(bridge);
        CM_BakeStaticObstacles();

        T_ASSERT(CM_PointIsPathableForRadius(&from, 0.0f));
        T_ASSERT(CM_PointIsPathableForRadius(&to, 0.0f));
        T_ASSERT(CM_LineIsWalkableForRadius(&from, &to, 0.0f));
        T_ASSERT(CM_LineIsWalkableForRadius(&to, &from, 0.0f));

        goal = Waypoint_add(&to);
        generation = CM_BuildHeatmapForRadius(goal, 0.0f);
        T_ASSERT(generation);
        T_ASSERT(CM_FlowCanReach(generation, from.x, from.y));
        goal->s.origin2 = from;
        goal->s.origin.x = from.x;
        goal->s.origin.y = from.y;
        generation = CM_BuildHeatmapForRadius(goal, 0.0f);
        T_ASSERT(generation);
        T_ASSERT(CM_FlowCanReach(generation, to.x, to.y));
    }
}

/* Runtime Human06 YT20 at (-800,320) is authored at angle zero.  Its model
 * runs north-to-south, so the path texture's long axis must be stamped on Y. */
TEST(wc3_destructable, human06_yt20_runtime_bridge_crosses_north_to_south) {
    static DestructableData_t const bridge_data = { .walkable = true };
    human06_bridge_pathtex_t pathtex = make_human06_bridge_pathtex(&human06_bridge_fixtures[1]);
    uint8_t cells[64 * 64];
    vec2_t deck = { 4.0f, 0.0f }, from = { 19.0f, 732.0f }, to = { 4.0f, -718.0f };
    edict_t *bridge;

    memset(cells, 0, sizeof(cells));
    FOR_LOOP(y, 22) FOR_LOOP(x, 64) cells[x + (21 + y) * 64] = 2;
    reset_entities(); setup_test_world(); setup_test_pathmap(64, 64, cells);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {-1024.0f, -1024.0f}, .max = {1024.0f, 1024.0f}));
    bridge = make_test_destructable(2500.0f, 0.0f, 0.0f);
    bridge->class_id = MAKEFOURCC('Y', 'T', '2', '0'); bridge->s.class_id = bridge->class_id;
    bridge->s.origin2 = (vec2_t){ 0.0f, 0.0f }; bridge->data.DestructableData = &bridge_data;
    bridge->destructable.alive_pathtex = (pathTex_t *)&pathtex; bridge->pathtex = (pathTex_t *)&pathtex;
    bridge->targtype = TARG_BRIDGE; G_RegisterGroundSurface(bridge); CM_BakeStaticObstacles();

    T_ASSERT(CM_PointIsPathableForRadius(&deck, 0.0f));
    T_ASSERT(CM_LineIsWalkableForRadius(&from, &to, 32.0f));
}

TEST(wc3_destructable, bridge_path_texture_rotation_covers_all_quarter_turns) {
    static DestructableData_t const bridge_data = { .walkable = true };
    human06_bridge_pathtex_t pathtex = make_human06_bridge_pathtex(&human06_bridge_fixtures[1]);

    FOR_LOOP(angle, 4) {
        uint8_t cells[64 * 64];
        bool const vertical = !(angle & 1);
        vec2_t from = vertical ? MAKE(vec2_t, 0.0f, -900.0f) : MAKE(vec2_t, -900.0f, 0.0f);
        vec2_t to = vertical ? MAKE(vec2_t, 0.0f, 900.0f) : MAKE(vec2_t, 900.0f, 0.0f);
        edict_t *bridge;
        pathTexTransform_t transform;

        memset(cells, 0, sizeof(cells));
        if (vertical) FOR_LOOP(y, 32) FOR_LOOP(x, 64) cells[x + (y + 16) * 64] = 2;
        else FOR_LOOP(y, 64) FOR_LOOP(x, 32) cells[x + 16 + y * 64] = 2;
        reset_entities(); setup_test_world(); setup_test_pathmap(64, 64, cells);
        CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {-1024.0f, -1024.0f}, .max = {1024.0f, 1024.0f}));
        bridge = make_test_destructable(2500.0f, 0.0f, 0.0f);
        bridge->class_id = MAKEFOURCC('Y', 'T', '2', '0'); bridge->s.class_id = bridge->class_id;
        bridge->s.origin2 = (vec2_t){ 0.0f, 0.0f }; bridge->s.angle = angle * (float)M_PI / 2.0f;
        bridge->data.DestructableData = &bridge_data;
        bridge->destructable.alive_pathtex = (pathTex_t *)&pathtex; bridge->pathtex = (pathTex_t *)&pathtex;
        bridge->targtype = TARG_BRIDGE; G_RegisterGroundSurface(bridge); CM_BakeStaticObstacles();
        transform = CM_GetPathTexTransform(bridge);

        T_EQ(transform.turn, (angle + 1) % 4);
        T_EQ(transform.width, vertical ? 22 : 32);
        T_EQ(transform.height, vertical ? 32 : 22);
        T_ASSERT(CM_LineIsWalkableForRadius(&from, &to, 0.0f));
        G_KillDestructable(bridge, NULL);
        T_ASSERT(!CM_LineIsWalkableForRadius(&from, &to, 0.0f));
    }
}

TEST(wc3_destructable, completed_death_holds_authored_final_frame) {
    animation_t death = { .name = "Death", .interval = { 2000, 3000 }, .flags = 1 };
    edict_t *dest = make_test_destructable(10.0f, 0.0f, 0.0f);

    dest->aiflags |= AI_HOLD_FRAME;
    G_DestructableStartDeathAnimation(dest);
    T_ASSERT(!(dest->aiflags & AI_HOLD_FRAME));
    T_STREQ(dest->currentmove->animation, "death");

    dest->animation = &death;
    dest->s.frame = 2900;
    tree_decay1(dest);

    T_ASSERT(dest->aiflags & AI_HOLD_FRAME);
    T_EQ(dest->s.frame, 2999);

    G_DestructableStartAliveAnimation(dest, false);
    T_ASSERT(!(dest->aiflags & AI_HOLD_FRAME));
    T_STREQ(dest->currentmove->animation, "stand");
}

TEST(wc3_destructable, placement_retains_inline_drop_sets) {
    droppableItem_t entries[] = {
        { MAKEFOURCC('r', 'a', 't', 'f'), 100 },
    };
    droppableItemSet_t sets[] = {
        { 1, entries },
    };
    doodad_t placement = {
        .flags = 2,
        .treeLife = 100,
        .droppedItemSetPtr = (uint32_t)-1,
        .num_droppedItemSets = 1,
        .droppableItemSets = sets,
    };
    edict_t *dest = make_test_destructable(100.0f, 0.0f, 0.0f);

    G_InitializeDestructablePlacement(dest, &placement);

    T_ASSERT(dest->destructable.drop_sets == sets);
    T_EQ(ARRAY_COUNT(dest->destructable.drop_sets), 1);
    T_EQ(dest->destructable.item_table, (uint32_t)-1);
    T_ASSERT(!dest->destructable.loot_processed);
}

TEST(wc3_destructable, weighted_inline_drop_selection_honors_boundaries_and_remainder) {
    droppableItem_t entries[] = {
        { MAKEFOURCC('r', 'a', 't', 'f'), 30 },
        { MAKEFOURCC('r', 'd', 'e', '2'), 20 },
    };

    T_EQ(G_SelectDropItem(entries, 2, 0), entries[0].itemID);
    T_EQ(G_SelectDropItem(entries, 2, 29), entries[0].itemID);
    T_EQ(G_SelectDropItem(entries, 2, 30), entries[1].itemID);
    T_EQ(G_SelectDropItem(entries, 2, 49), entries[1].itemID);
    T_EQ(G_SelectDropItem(entries, 2, 50), 0);
    T_EQ(G_SelectDropItem(entries, 2, 99), 0);
    T_EQ(G_SelectDropItem(entries, 2, 100), 0);
}

TEST(wc3_destructable, death_spawns_each_inline_result_once_as_world_item) {
    droppableItem_t first_entries[] = {
        { MAKEFOURCC('r', 'a', 't', 'f'), 100 },
    };
    droppableItem_t second_entries[] = {
        { MAKEFOURCC('r', 'd', 'e', '2'), 100 },
    };
    droppableItemSet_t sets[] = {
        { 1, first_entries },
        { 1, second_entries },
    };
    doodad_t placement = {
        .flags = 2,
        .treeLife = 100,
        .droppedItemSetPtr = (uint32_t)-1,
        .num_droppedItemSets = 2,
        .droppableItemSets = sets,
    };
    edict_t *dest;
    edict_t *first;
    edict_t *second;
    uint32_t first_item;

    setup_test_world();
    dest = make_test_destructable(10.0f, 100.0f, 200.0f);
    G_InitializeDestructablePlacement(dest, &placement);
    first_item = globals.num_edicts;

    T_ASSERT(G_KillDestructable(dest, NULL));
    T_EQ(globals.num_edicts, first_item + 2);
    first = &g_edicts[first_item];
    second = &g_edicts[first_item + 1];
    T_EQ(first->class_id, first_entries[0].itemID);
    T_EQ(second->class_id, second_entries[0].itemID);
    T_ASSERT(G_IsItem(first) && G_IsItem(second));
    T_ASSERT(first->item.in_world && second->item.in_world);
    T_EQ(first->s.player, PLAYER_NEUTRAL_PASSIVE);
    T_EQ(second->s.player, PLAYER_NEUTRAL_PASSIVE);
    T_ASSERT(Vector2_distance(&first->s.origin2, &second->s.origin2) > 0.0f);
    T_ASSERT(dest->destructable.loot_processed);

    T_ASSERT(!G_KillDestructable(dest, NULL));
    G_SpawnDestructableLoot(dest);
    T_EQ(globals.num_edicts, first_item + 2);
}

TEST(wc3_destructable, empty_probability_remainder_spawns_no_item) {
    droppableItem_t entries[] = {
        { MAKEFOURCC('r', 'a', 't', 'f'), 0 },
    };
    droppableItemSet_t sets[] = {
        { 1, entries },
    };
    doodad_t placement = {
        .flags = 2,
        .treeLife = 100,
        .droppedItemSetPtr = (uint32_t)-1,
        .num_droppedItemSets = 1,
        .droppableItemSets = sets,
    };
    edict_t *dest;
    uint32_t before;

    setup_test_world();
    dest = make_test_destructable(10.0f, 100.0f, 200.0f);
    G_InitializeDestructablePlacement(dest, &placement);
    before = globals.num_edicts;

    T_ASSERT(G_KillDestructable(dest, NULL));
    T_EQ(globals.num_edicts, before);
    T_ASSERT(dest->destructable.loot_processed);
}

TEST(wc3_destructable, weighted_random_table_selection_honors_boundaries_and_remainder) {
    mapRandomItem_t entries[] = {
        { 30, MAKEFOURCC('r', 'a', 't', 'f') },
        { 20, MAKEFOURCC('r', 'd', 'e', '2') },
    };
    mapRandomItem_t saturated[] = {
        { 150, MAKEFOURCC('r', 'a', 't', 'f') },
    };

    T_EQ(G_SelectRandomTableItem(entries, 2, 0), entries[0].itemID);
    T_EQ(G_SelectRandomTableItem(entries, 2, 29), entries[0].itemID);
    T_EQ(G_SelectRandomTableItem(entries, 2, 30), entries[1].itemID);
    T_EQ(G_SelectRandomTableItem(entries, 2, 49), entries[1].itemID);
    T_EQ(G_SelectRandomTableItem(entries, 2, 50), 0);
    T_EQ(G_SelectRandomTableItem(entries, 2, 99), 0);
    T_EQ(G_SelectRandomTableItem(entries, 2, 100), 0);
    T_EQ(G_SelectRandomTableItem(saturated, 1, 99), saturated[0].itemID);
}

TEST(wc3_destructable, random_item_table_lookup_uses_table_number) {
    mapRandomItemTable_t tables[] = {
        { .tableNumber = 7 },
        { .tableNumber = 42 },
    };
    mapInfo_t *mapinfo;

    setup_test_world();
    mapinfo = (mapInfo_t *)level.mapinfo;
    mapinfo->num_randomItems = 2;
    mapinfo->randomItems = tables;

    T_ASSERT(G_FindRandomItemTable(42) == &tables[1]);
    T_NULL(G_FindRandomItemTable(1));
    T_NULL(G_FindRandomItemTable((uint32_t)-1));
}

TEST(wc3_destructable, death_spawns_map_table_sets_once_as_world_items) {
    mapRandomItem_t first_items[] = {
        { 100, MAKEFOURCC('r', 'a', 't', 'f') },
    };
    mapRandomItem_t second_items[] = {
        { 100, MAKEFOURCC('r', 'd', 'e', '2') },
    };
    mapRandomItemSet_t sets[] = {
        { 1, first_items },
        { 1, second_items },
    };
    mapRandomItemTable_t tables[] = {
        { .tableNumber = 3 },
        { .tableNumber = 42, .num_sets = 2, .sets = sets },
    };
    doodad_t placement = {
        .flags = 2,
        .treeLife = 100,
        .droppedItemSetPtr = 42,
    };
    mapInfo_t *mapinfo;
    edict_t *dest;
    uint32_t first_item;

    setup_test_world();
    mapinfo = (mapInfo_t *)level.mapinfo;
    mapinfo->num_randomItems = 2;
    mapinfo->randomItems = tables;
    dest = make_test_destructable(10.0f, 100.0f, 200.0f);
    G_InitializeDestructablePlacement(dest, &placement);
    first_item = globals.num_edicts;

    T_ASSERT(G_KillDestructable(dest, NULL));
    T_EQ(globals.num_edicts, first_item + 2);
    T_EQ(g_edicts[first_item].class_id, first_items[0].itemID);
    T_EQ(g_edicts[first_item + 1].class_id, second_items[0].itemID);
    T_ASSERT(G_IsItem(&g_edicts[first_item]));
    T_ASSERT(G_IsItem(&g_edicts[first_item + 1]));

    G_SpawnDestructableLoot(dest);
    T_EQ(globals.num_edicts, first_item + 2);
}

TEST(wc3_destructable, missing_random_item_table_spawns_nothing) {
    doodad_t placement = {
        .flags = 2,
        .treeLife = 100,
        .droppedItemSetPtr = 999,
    };
    edict_t *dest;
    uint32_t before;

    setup_test_world();
    dest = make_test_destructable(10.0f, 100.0f, 200.0f);
    G_InitializeDestructablePlacement(dest, &placement);
    before = globals.num_edicts;

    T_ASSERT(G_KillDestructable(dest, NULL));
    T_EQ(globals.num_edicts, before);
    T_ASSERT(dest->destructable.loot_processed);
}

TEST(wc3_destructable, empty_encoded_and_invalid_table_entries_spawn_nothing) {
    mapRandomItem_t encoded_items[] = {
        { 100, MAKEFOURCC('Y', 'Y', 'I', '0') },
    };
    mapRandomItem_t invalid_items[] = {
        { 100, MAKEFOURCC('z', 'z', 'z', 'z') },
    };
    mapRandomItemSet_t sets[] = {
        { 0, NULL },
        { 1, encoded_items },
        { 1, invalid_items },
    };
    mapRandomItemTable_t table = {
        .tableNumber = 9,
        .num_sets = 3,
        .sets = sets,
    };
    doodad_t placement = {
        .flags = 2,
        .treeLife = 100,
        .droppedItemSetPtr = 9,
    };
    mapInfo_t *mapinfo;
    edict_t *dest;
    uint32_t before;

    setup_test_world();
    mapinfo = (mapInfo_t *)level.mapinfo;
    mapinfo->num_randomItems = 1;
    mapinfo->randomItems = &table;
    dest = make_test_destructable(10.0f, 100.0f, 200.0f);
    G_InitializeDestructablePlacement(dest, &placement);
    before = globals.num_edicts;

    T_ASSERT(G_KillDestructable(dest, NULL));
    T_EQ(globals.num_edicts, before);
    T_ASSERT(dest->destructable.loot_processed);
}

TEST(wc3_destructable, silent_dead_state_has_no_event_or_loot) {
    edict_t *dest = make_test_destructable(100.0f, 0.0f, 0.0f);

    T_ASSERT(G_SetDestructableDeadState(dest, false));
    T_ASSERT(dest->destructable.dead);
    T_ASSERT(dest->destructable.loot_processed);
    T_EQ(level.events.write, 0);
    T_FEQ(dest->health.value, 0.0f, 0.01f);
}

TEST(wc3_destructable, restore_reenables_targeting_pathing_and_second_death) {
    edict_t *dest = make_test_destructable(100.0f, 4.0f, 4.0f);
    edict_t *attacker = make_destructable_test_attacker(8.0f, 4.0f);

    T_ASSERT(G_KillDestructable(dest, attacker));
    T_ASSERT(G_RestoreDestructable(dest, 150.0f, true));
    T_ASSERT(!dest->destructable.dead);
    T_ASSERT(!dest->destructable.loot_processed);
    T_ASSERT(!(dest->svflags & SVF_DEADMONSTER));
    T_ASSERT(!(dest->s.flags & EF_NOT_SELECTABLE));
    T_ASSERT(!(dest->s.renderfx & RF_NO_SHADOW));
    T_ASSERT(dest->destructable.pathing_active);
    T_ASSERT(G_DestructableIsAttackable(dest));
    T_FEQ(dest->health.value, 100.0f, 0.01f);
    T_NOT_NULL(dest->currentmove);
    T_STREQ(dest->currentmove->animation, "birth");

    T_ASSERT(G_KillDestructable(dest, attacker));
    T_EQ(level.events.write, 2);
    T_ASSERT(level.events.queue[1].source == attacker);
}

TEST(wc3_destructable, set_life_uses_death_and_restore_transitions) {
    edict_t *dest = make_test_destructable(100.0f, 0.0f, 0.0f);

    T_ASSERT(G_SetDestructableLife(dest, 0.0f));
    T_ASSERT(dest->destructable.dead);
    T_EQ(level.events.write, 1);
    T_NULL(level.events.queue[0].source);

    T_ASSERT(G_SetDestructableLife(dest, 40.0f));
    T_ASSERT(!dest->destructable.dead);
    T_FEQ(dest->health.value, 40.0f, 0.01f);
    T_STREQ(dest->currentmove->animation, "stand");
    T_EQ(level.events.write, 1);
}

TEST(wc3_destructable, remove_bypasses_death_event_and_loot) {
    edict_t *dest = make_test_destructable(100.0f, 0.0f, 0.0f);

    T_ASSERT(G_RemoveDestructable(dest));
    T_ASSERT(!dest->inuse);
    T_EQ(level.events.write, 0);
}

TEST(wc3_destructable, scripted_lifecycle_natives_use_authoritative_state) {
    static cstring_t const slk =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"ID\"\n"
        "C;Y1;X2;K\"file\"\n"
        "C;Y1;X3;K\"targType\"\n"
        "C;Y1;X4;K\"HP\"\n"
        "C;Y1;X5;K\"radius\"\n"
        "C;Y2;X1;K\"B004\"\n"
        "C;Y2;X2;K\"Doodads\\Test\\Test\"\n"
        "C;Y2;X3;K\"debris\"\n"
        "C;Y2;X4;K100\n"
        "C;Y2;X5;K16\n"
        "E\n";
    slkTestData_t *rows = parse_slk_string(slk);
    edict_t *dest;

    setup_test_world();
    slkTestData_t *saved = G_SetSLKRows("DestructableData", rows);
    T_ASSERT(run_test_jass(
        "globals\n"
        "  destructable scriptedDest = null\n"
        "  integer scriptedDeaths = 0\n"
        "endglobals\n"
        "function onScriptedDeath takes nothing returns nothing\n"
        "  set scriptedDeaths = scriptedDeaths + 1\n"
        "  call BJassAssert(GetTriggerWidget() == scriptedDest, \"wrong destructable widget\")\n"
        "  call BJassAssert(GetTriggerDestructable() == scriptedDest, \"wrong trigger destructable\")\n"
        "  call BJassAssert(GetKillingUnit() == null, \"scripted kill has a killer\")\n"
        "endfunction\n"
        "function verifyScriptedDeath takes nothing returns nothing\n"
        "  call BJassAssert(scriptedDeaths == 1, \"scripted death event count\")\n"
        "endfunction\n"
        "function removeScriptedDest takes nothing returns nothing\n"
        "  call RemoveDestructable(scriptedDest)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  set scriptedDest = CreateDeadDestructable('B004', 64.0, 64.0, 0.0, 1.0, 0)\n"
        "  call BJassAssert(scriptedDest != null, \"dead creation returned null\")\n"
        "  call BJassAssert(GetDestructableLife(scriptedDest) == 0.0, \"dead creation has life\")\n"
        "  call DestructableRestoreLife(scriptedDest, 60.0, true)\n"
        "  call BJassAssert(GetDestructableLife(scriptedDest) == 60.0, \"restore life failed\")\n"
        "  call TriggerRegisterDeathEvent(t, scriptedDest)\n"
        "  call TriggerAddAction(t, function onScriptedDeath)\n"
        "  call KillDestructable(scriptedDest)\n"
        "endfunction\n"));

    dest = NULL;
    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].class_id == MAKEFOURCC('B', '0', '0', '4')) {
            dest = &g_edicts[i];
            break;
        }
    }

    T_NOT_NULL(dest);
    T_ASSERT(dest->destructable.dead);
    T_ASSERT(dest->destructable.loot_processed);
    T_FEQ(dest->health.value, 0.0f, 0.01f);
    T_EQ(level.events.write, 1);

    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyScriptedDeath", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));

    jass_callbyname(level.vm, "removeScriptedDest", true);
    jass_runevents(level.vm);
    T_ASSERT(!dest->inuse);

    G_SetSLKRows("DestructableData", saved);
    free_slk_rows(rows);
}

#endif /* BZ_TESTS */
