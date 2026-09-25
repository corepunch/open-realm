/* Compile the WC3 game renderer into the headless renderer test. Function
 * sections let the linker retain R_RegisterMap and its dependencies without
 * requiring a GL context for unrelated draw paths. */
#define R_RegisterMap R_TestProductionRegisterMap
#define R_BlightTexture R_TestProductionBlightTexture
#define R_LoadModel R_TestProductionLoadModel
#define R_TerrainArt R_TestProductionTerrainArt
#define R_CliffType R_TestProductionCliffType
#define R_ReleaseModel R_TestProductionReleaseModel
#include "../games/warcraft-3/renderer/r_game.c"

#include "test.h"

static uint32_t test_spn_render_count;
static renderEntity_t test_spn_render_entity;
static matrix4_t test_spn_render_transform;

void R_GetEntityMatrix(renderEntity_t const *entity, matrix4_t * matrix) {
    Matrix4_identity(matrix);
    Matrix4_translate(matrix, &entity->origin);
}

void MDX_RenderModel(renderEntity_t const *entity, mdxModel_t const *model, matrix4_t const * transform) {
    (void)model;
    test_spn_render_count++;
    test_spn_render_entity = *entity;
    test_spn_render_transform = *transform;
}

mdxSequence_t const *MDLX_FindSequenceByName(mdxModel_t const *model, cstring_t name) {
    (void)model; (void)name; return NULL;
}

TEST(renderer_model, production_spn_dispatch_retains_spawn_after_parent_update) {
    static uint32_t key = 100;
    static vector3_t pivot = { 1.0f, 2.0f, 3.0f };
    static mdxSequence_t parent_sequence = { .interval = { 0, 1000 } };
    static mdxSequence_t child_sequence = { .interval = { 0, 1000 } };
    static mdxModel_t child_mdx = { .sequences = &child_sequence, .num_sequences = 1 };
    static model_t child_model = { .modeltype = ID_MDLX, .mdx = &child_mdx };
    static wc3SpawnData_t spawn_row = { .name = "TestSpawn", .model_path = "TestUI\\Models\\quad_sprite.mdx", .model = &child_model };
    mdxEvent_t event = { .num_keys = 1, .globalSeqId = (uint32_t)-1, .keys = &key };
    mdxModel_t parent_mdx = { .events = &event, .sequences = &parent_sequence, .num_sequences = 1,
                              .pivots = &pivot, .num_pivots = 1 };
    model_t parent_model = { .modeltype = ID_MDLX, .mdx = &parent_mdx };
    renderEntity_t parent = { .origin = { 10.0f, 20.0f, 30.0f }, .model = &parent_model, .number = 7, .team = 2, .scale = 1.0f };
    wc3EventSoundState_t saved_state = event_sound_state[7];
    uint32_t saved_time = tr.viewDef.time;
    render_phase_t saved_phase = tr.render_phase;

    snprintf(event.node.name, sizeof(event.node.name), "SPNxTestSpawn");
    event.node.node_id = 0; event.node.parent_id = (uint32_t)-1;
    parent_mdx.nodes[0] = &event.node; parent_mdx.node_list[0] = &event.node; parent_mdx.num_nodes = 1;
    spawn_data_rows = &spawn_row; spawn_data_count = 1;
    event_sound_state[7] = (wc3EventSoundState_t){ 0 }; R_W3ClearEventSpawns();
    test_spn_render_count = 0; tr.render_phase = RENDER_PHASE_SOLID; tr.viewDef.time = 0;

    R_UpdateEntityPresentation(&parent); /* First observation seeds the crossing state. */
    T_EQ(test_spn_render_count, 0);
    parent.frame = 150; tr.viewDef.time = 150;
    R_UpdateEntityPresentation(&parent);
    T_EQ(test_spn_render_count, 1);
    T_EQ(test_spn_render_entity.model, &child_model);
    T_EQ(test_spn_render_entity.team, 2);
    T_ASSERT(test_spn_render_entity.number > MAX_GAME_ENTITIES);
    T_FEQ(test_spn_render_transform.v[12], 11.0f, 0.001f);
    T_FEQ(test_spn_render_transform.v[13], 22.0f, 0.001f);
    T_FEQ(test_spn_render_transform.v[14], 33.0f, 0.001f);

    /* Drawing the pool does not depend on another parent entity update. */
    tr.viewDef.time = 250; R_W3DrawEventSpawns();
    T_EQ(test_spn_render_count, 2);
    T_EQ(test_spn_render_entity.frame, 100);
    tr.render_phase = RENDER_PHASE_LIGHTS; R_W3DrawEventSpawns();
    T_EQ(test_spn_render_count, 2);
    tr.render_phase = RENDER_PHASE_SOLID; tr.viewDef.time = 1150; R_W3DrawEventSpawns();
    T_EQ(test_spn_render_count, 2);
    T_ASSERT(!event_spawns[0].active);

    spawn_data_rows = NULL; spawn_data_count = 0; R_W3ClearEventSpawns();
    event_sound_state[7] = saved_state; tr.viewDef.time = saved_time; tr.render_phase = saved_phase;
}
