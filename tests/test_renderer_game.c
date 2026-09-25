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
static HANDLE test_renderer_archive;
static char test_sound_path[512];
static vector3_t test_sound_origin;
static uint32_t test_sound_count;

static HANDLE test_renderer_alloc(long size) { return calloc(1, (size_t)size); }
static void test_renderer_free(HANDLE ptr) { free(ptr); }

static int test_renderer_read(cstring_t path, void **buffer) {
    HANDLE file = NULL;
    uint32_t size, read = 0;
    *buffer = NULL;
    if (!test_renderer_archive || !SFileOpenFileEx(test_renderer_archive, path, 0, &file)) return -1;
    size = SFileGetFileSize(file, NULL);
    *buffer = malloc((size_t)size + 1);
    if (!*buffer || !SFileReadFile(file, *buffer, size, &read, NULL) || read != size) {
        free(*buffer); *buffer = NULL; SFileCloseFile(file); return -1;
    }
    ((BYTE *)*buffer)[size] = 0;
    SFileCloseFile(file);
    return (int)size;
}

static uint32_t test_renderer_load_slk(cstring_t path, slkField_t const *schema,
                                    void **rows, uint32_t row_stride) {
    void *buffer = NULL;
    int size = test_renderer_read(path, &buffer);
    uint32_t count = size < 0 ? 0 : Stb_SlkLoadBuffer(buffer, schema, rows, row_stride);
    free(buffer);
    return count;
}

static void test_renderer_play_sound(cstring_t path, vector3_t const * origin, float volume) {
    (void)volume;
    snprintf(test_sound_path, sizeof(test_sound_path), "%s", path);
    test_sound_origin = *origin;
    test_sound_count++;
}

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
    static mdxEvent_t event;
    static mdxModel_t parent_mdx;
    static model_t parent_model;
    static renderEntity_t parent;
    wc3EventSoundState_t saved_state = event_sound_state[7];
    uint32_t saved_time = tr.viewDef.time;
    render_phase_t saved_phase = tr.render_phase;
    refImport_t saved_imports = ri;
    model_t * child_model = NULL;

    memset(&event, 0, sizeof(event));
    event.num_keys = 1; event.globalSeqId = (uint32_t)-1; event.keys = &key;
    parent_mdx = (mdxModel_t){ .events = &event, .sequences = &parent_sequence,
                               .num_sequences = 1, .pivots = &pivot, .num_pivots = 1 };
    parent_model = (model_t){ .modeltype = ID_MDLX, .mdx = &parent_mdx };
    parent = (renderEntity_t){ .origin = { 10.0f, 20.0f, 30.0f }, .model = &parent_model,
                               .number = 7, .team = 2, .scale = 1.0f };
    snprintf(event.node.name, sizeof(event.node.name), "SPNxTestSpawn");
    event.node.node_id = 0; event.node.parent_id = (uint32_t)-1;
    parent_mdx.nodes[0] = &event.node; parent_mdx.node_list[0] = &event.node; parent_mdx.num_nodes = 1;
    R_SetMapAssetScope(NULL); R_ShutdownModels();
    T_ASSERT(SFileOpenArchive("build/tests/tests.mpq", 0, 0, &test_renderer_archive));
    if (!test_renderer_archive) goto cleanup_spn_test;
    ri.FS_ReadFile = test_renderer_read; ri.FS_FreeFile = test_renderer_free;
    ri.LoadSlk = test_renderer_load_slk; ri.MemAlloc = test_renderer_alloc; ri.MemFree = test_renderer_free;
    R_W3LoadSpawnData();
    T_EQ(spawn_data_count, 1);
    if (!spawn_data_rows || spawn_data_count != 1) goto cleanup_spn_test;
    T_STREQ(spawn_data_rows[0].name, "TestSpawn");
    T_STREQ(spawn_data_rows[0].model_path, "TestUI\\Models\\quad_sprite.mdx");
    child_model = R_TestProductionLoadModel(spawn_data_rows[0].model_path);
    spawn_data_rows[0].model = child_model;
    T_NOT_NULL(child_model);
    if (!child_model || !child_model->mdx) goto cleanup_spn_test;
    T_EQ(child_model->modeltype, ID_MDLX);
    T_NOT_NULL(child_model->mdx->sequences);
    T_ASSERT(child_model->mdx->num_sequences > 0);
    T_EQ(parent_mdx.num_pivots, 1);
    T_EQ(R_W3SpawnModel(spawn_data_rows), child_model);
    event_sound_state[7] = (wc3EventSoundState_t){ 0 }; R_W3ClearEventSpawns();
    test_spn_render_count = 0; tr.render_phase = RENDER_PHASE_SOLID; tr.viewDef.time = 0;

    R_UpdateEntityPresentation(&parent); /* First observation seeds the crossing state. */
    T_EQ(test_spn_render_count, 0);
    parent.frame = 150; tr.viewDef.time = 150;
    R_UpdateEntityPresentation(&parent);
    T_EQ(test_spn_render_count, 1);
    T_EQ(test_spn_render_entity.model, child_model);
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

cleanup_spn_test:
    R_W3ClearEventSpawns();
    if (spawn_data_rows && spawn_data_count) spawn_data_rows[0].model = NULL;
    R_W3FreeSpawnData(false);
    if (child_model) {
        if (child_model->mdx) R_TestProductionReleaseModel(child_model);
        else test_renderer_free(child_model);
    }
    if (test_renderer_archive) { SFileCloseArchive(test_renderer_archive); test_renderer_archive = NULL; }
    ri = saved_imports;
    event_sound_state[7] = saved_state; tr.viewDef.time = saved_time; tr.render_phase = saved_phase;
}

TEST(renderer_model, production_snd_dispatch_uses_event_world_transform) {
    static uint32_t key = 100;
    static vector3_t pivot = { 1.0f, 2.0f, 3.0f };
    static mdxSequence_t sequence = { .interval = { 0, 1000 } };
    static wc3AnimSound_t sound = { .name = "TestSound", .files = "hit.wav", .directory = "Sounds", .volume = 127.0f };
    mdxEvent_t event = { .num_keys = 1, .globalSeqId = (uint32_t)-1, .keys = &key };
    mdxModel_t mdx = { .events = &event, .sequences = &sequence, .num_sequences = 1,
                       .pivots = &pivot, .num_pivots = 1 };
    model_t model = { .modeltype = ID_MDLX, .mdx = &mdx };
    renderEntity_t entity = { .origin = { 10.0f, 20.0f, 30.0f }, .model = &model, .number = 8 };
    wc3AnimSound_t *saved_sounds = anim_sound_rows;
    uint32_t saved_sound_count = anim_sound_count, saved_time = tr.viewDef.time;
    render_phase_t saved_phase = tr.render_phase;
    wc3EventSoundState_t saved_state = event_sound_state[8];
    void (*saved_play_sound)(cstring_t, vector3_t const *, float) = ri.PlaySoundAt;

    snprintf(event.node.name, sizeof(event.node.name), "SNDxTestSound");
    event.node.node_id = 0; event.node.parent_id = (uint32_t)-1;
    mdx.nodes[0] = &event.node; mdx.node_list[0] = &event.node; mdx.num_nodes = 1;
    anim_sound_rows = &sound; anim_sound_count = 1; event_sound_state[8] = (wc3EventSoundState_t){ 0 };
    test_sound_count = 0; test_sound_path[0] = '\0'; ri.PlaySoundAt = test_renderer_play_sound;
    tr.render_phase = RENDER_PHASE_SOLID; tr.viewDef.time = 0;
    R_UpdateEntityPresentation(&entity);
    entity.frame = 150; tr.viewDef.time = 150; R_UpdateEntityPresentation(&entity);
    T_EQ(test_sound_count, 1);
    T_STREQ(test_sound_path, "Sounds\\hit.wav");
    T_FEQ(test_sound_origin.x, 11.0f, 0.001f);
    T_FEQ(test_sound_origin.y, 22.0f, 0.001f);
    T_FEQ(test_sound_origin.z, 33.0f, 0.001f);
    anim_sound_rows = saved_sounds; anim_sound_count = saved_sound_count;
    ri.PlaySoundAt = saved_play_sound; event_sound_state[8] = saved_state;
    tr.viewDef.time = saved_time; tr.render_phase = saved_phase;
}
