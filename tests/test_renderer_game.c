/* Compile the WC3 game renderer into the headless renderer test. Function
 * sections let the linker retain R_RegisterMap and its dependencies without
 * requiring a GL context for unrelated draw paths. */
#define R_RegisterMap R_TestProductionRegisterMap
#define R_BlightTexture R_TestProductionBlightTexture
#define R_LoadModel R_TestProductionLoadModel
#define R_TerrainArt R_TestProductionTerrainArt
#define R_CliffType R_TestProductionCliffType
#define R_ReleaseModel R_TestProductionReleaseModel
#define MDLX_DrawSpriteInstance R_TestCursorSprite
#define MDLX_FindSequenceByName R_TestCursorSequence
#include "../games/warcraft-3/renderer/r_game.c"

#include "test.h"

extern void R_TestUseProductionModelLoader(bool enabled);
static uint32_t test_spn_render_count;
static renderEntity_t test_spn_render_entity;
static mat4_t test_spn_render_transform;
static handle_t test_renderer_archive;
static char test_sound_path[512];
static vec3_t test_sound_origin;
static uint32_t test_sound_count;
static uint32_t test_splat_count;
static vec2_t test_splat_origin;
static float test_splat_radius;
static color32_t test_splat_color;
static vec2_t test_splat_uv_mins, test_splat_uv_maxs;
static texture_t test_splat_texture;

static handle_t test_renderer_alloc(long size) { return calloc(1, (size_t)size); }
static void test_renderer_free(handle_t ptr) { free(ptr); }

static int test_renderer_read(cstring_t path, void **buffer) {
    handle_t file = NULL;
    uint32_t size, read = 0;
    *buffer = NULL;
    if (!test_renderer_archive || !SFileOpenFileEx(test_renderer_archive, path, 0, &file)) return -1;
    size = SFileGetFileSize(file, NULL);
    *buffer = malloc((size_t)size + 1);
    if (!*buffer || !SFileReadFile(file, *buffer, size, &read, NULL) || read != size) {
        free(*buffer); *buffer = NULL; SFileCloseFile(file); return -1;
    }
    ((uint8_t *)*buffer)[size] = 0;
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

static void test_renderer_play_sound(cstring_t path, vec3_t const *origin, float volume) {
    (void)volume;
    snprintf(test_sound_path, sizeof(test_sound_path), "%s", path);
    test_sound_origin = *origin;
    test_sound_count++;
}

void R_GetEntityMatrix(renderEntity_t const *entity, mat4_t *matrix) {
    Matrix4_identity(matrix);
    Matrix4_translate(matrix, &entity->origin);
}

void MDX_RenderModel(renderEntity_t const *entity, mdxModel_t const *model, mat4_t const *transform) {
    (void)model;
    test_spn_render_count++;
    test_spn_render_entity = *entity;
    test_spn_render_transform = *transform;
}

void R_RenderRectSplatUV(rectSplatParams_t const *params) {
    test_splat_count++;
    test_splat_origin = MAKE(vec2_t, (params->mins->x + params->maxs->x) * 0.5f,
                             (params->mins->y + params->maxs->y) * 0.5f);
    test_splat_radius = (params->maxs->x - params->mins->x) * 0.5f;
    test_splat_color = params->color;
    test_splat_uv_mins = *params->uv_mins; test_splat_uv_maxs = *params->uv_maxs;
}

void R_RenderSplat(vec2_t const *position, float radius, texture_t const *texture,
                   splat_shader_t *shader, color32_t color) {
    (void)texture; (void)shader;
    test_splat_count++;
    test_splat_origin = *position;
    test_splat_radius = radius;
    test_splat_color = color;
}

TEST(renderer_model, production_spn_dispatch_retains_spawn_after_parent_update) {
    static uint32_t key = 100;
    static vec3_t pivot = { 1.0f, 2.0f, 3.0f };
    static mdxSequence_t parent_sequence = { .interval = { 0, 1000 } };
    static mdxEvent_t event;
    static mdxModel_t parent_mdx;
    static model_t parent_model;
    static renderEntity_t parent;
    wc3EventState_t saved_state = event_state[7];
    uint32_t saved_time = tr.viewDef.time;
    render_phase_t saved_phase = tr.render_phase;
    refImport_t saved_imports = ri;
    model_t *child_model = NULL;

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
    R_W3LoadSplatData();
    T_EQ(splat_data_count, 1);
    if (!splat_data_rows || splat_data_count != 1) goto cleanup_spn_test;
    T_STREQ(splat_data_rows[0].name, "TestSplat");
    T_STREQ(splat_data_rows[0].blend_mode, "0");
    T_EQ(splat_data_rows[0].rows, 2); T_EQ(splat_data_rows[0].columns, 2);
    T_EQ(splat_data_rows[0].uv_lifespan_start, 0); T_EQ(splat_data_rows[0].uv_decay_end, 3);
    R_W3LoadUberSplatData();
    T_EQ(uber_splat_count, 1);
    if (!uber_splat_rows || uber_splat_count != 1) goto cleanup_spn_test;
    T_STREQ(uber_splat_rows[0].name, "TestUber");
    T_STREQ(uber_splat_rows[0].blend_mode, "0");
    T_FEQ(uber_splat_rows[0].scale, 64.0f, 0.001f);
    T_FEQ(uber_splat_rows[0].birth_time, 1.0f, 0.001f);
    R_TestUseProductionModelLoader(true);
    child_model = R_W3SpawnModel(spawn_data_rows);
    T_NOT_NULL(child_model);
    if (!child_model || !child_model->mdx) goto cleanup_spn_test;
    T_EQ(child_model->modeltype, ID_MDLX);
    T_NOT_NULL(child_model->mdx->sequences);
    T_ASSERT(child_model->mdx->num_sequences > 0);
    T_EQ(parent_mdx.num_pivots, 1);
    T_EQ(R_W3SpawnModel(spawn_data_rows), child_model);
    event_state[7] = (wc3EventState_t){ 0 }; R_W3ClearEventSpawns();
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
    R_W3ClearEventSplats();
    R_W3FreeSpawnData(true);
    R_W3FreeSplatData();
    R_W3FreeUberSplatData();
    R_ShutdownModels();
    R_TestUseProductionModelLoader(false);
    if (test_renderer_archive) { SFileCloseArchive(test_renderer_archive); test_renderer_archive = NULL; }
    ri = saved_imports;
    event_state[7] = saved_state; tr.viewDef.time = saved_time; tr.render_phase = saved_phase;
}

TEST(renderer_model, production_spl_dispatch_uses_splat_atlas_and_event_transform) {
    static uint32_t key = 100;
    static vec3_t pivot = { 1.0f, 2.0f, 3.0f };
    static mdxSequence_t sequence = { .interval = { 0, 3000 } };
    mdxEvent_t event = { .num_keys = 1, .globalSeqId = (uint32_t)-1, .keys = &key };
    mdxModel_t mdx = { .events = &event, .sequences = &sequence, .num_sequences = 1,
                       .pivots = &pivot, .num_pivots = 1 };
    model_t model = { .modeltype = ID_MDLX, .mdx = &mdx };
    renderEntity_t entity = { .origin = { 10.0f, 20.0f, 30.0f }, .model = &model, .number = 10 };
    wc3SplatData_t row = {
        .name = "TestSplat", .rows = 2, .columns = 2, .scale = 32.0f, .lifespan = 1.0f, .decay_time = 1.0f,
        .uv_lifespan_start = 0, .uv_lifespan_end = 1, .uv_decay_start = 2, .uv_decay_end = 3,
        .start_r = 1.0f, .start_a = 1.0f, .middle_g = 1.0f, .middle_a = 1.0f,
        .end_b = 1.0f, .end_a = 0.0f, .texture = &test_splat_texture,
    };
    wc3SplatData_t *saved_rows = splat_data_rows;
    uint32_t saved_count = splat_data_count, saved_time = tr.viewDef.time;
    render_phase_t saved_phase = tr.render_phase;
    wc3EventState_t saved_state = event_state[10];

    snprintf(event.node.name, sizeof(event.node.name), "SPLxTestSplat");
    event.node.node_id = 0; event.node.parent_id = (uint32_t)-1;
    mdx.nodes[0] = &event.node; mdx.node_list[0] = &event.node; mdx.num_nodes = 1;
    splat_data_rows = &row; splat_data_count = 1;
    event_state[10] = (wc3EventState_t){ 0 }; R_W3ClearEventSplats();
    test_splat_count = 0; tr.render_phase = RENDER_PHASE_SOLID; tr.viewDef.time = 0;

    R_UpdateEntityPresentation(&entity);
    entity.frame = 150; tr.viewDef.time = 150; R_UpdateEntityPresentation(&entity);
    T_EQ(test_splat_count, 1);
    T_FEQ(test_splat_origin.x, 11.0f, 0.001f); T_FEQ(test_splat_origin.y, 22.0f, 0.001f);
    T_FEQ(test_splat_radius, 32.0f, 0.001f);
    T_FEQ(test_splat_uv_mins.x, 0.0f, 0.001f); T_FEQ(test_splat_uv_mins.y, 0.0f, 0.001f);
    T_FEQ(test_splat_uv_maxs.x, 0.5f, 0.001f); T_FEQ(test_splat_uv_maxs.y, 0.5f, 0.001f);

    tr.viewDef.time = 1150; R_W3DrawEventSplats();
    T_EQ(test_splat_count, 2);
    T_FEQ(test_splat_uv_mins.x, 0.0f, 0.001f); T_FEQ(test_splat_uv_mins.y, 0.5f, 0.001f);
    T_EQ(test_splat_color.r, 0); T_EQ(test_splat_color.g, 255); T_EQ(test_splat_color.b, 0);
    tr.viewDef.time = 2150; R_W3DrawEventSplats();
    T_EQ(test_splat_count, 2); T_ASSERT(!event_splats[0].active);

    R_W3ClearEventSplats();
    splat_data_rows = saved_rows; splat_data_count = saved_count;
    event_state[10] = saved_state; tr.viewDef.time = saved_time; tr.render_phase = saved_phase;
}

TEST(renderer_model, production_fpt_dispatch_uses_splat_data) {
    mdxEvent_t event = { 0 };
    wc3EventFamily_t const *family;
    char id[32] = { 0 };

    snprintf(event.node.name, sizeof(event.node.name), "FPTxTestSplat   ");
    family = R_W3EventFamily(&event);
    T_NOT_NULL(family);
    T_EQ(family->kind, WC3_EVENT_FOOTPRINT);
    T_ASSERT(MDLX_EventObjectId(&event, family->prefix, id, sizeof(id)));
    T_STREQ(id, "TestSplat");
}

TEST(renderer_model, production_ubr_dispatch_uses_data_lifetime_and_event_transform) {
    static uint32_t key = 100;
    static vec3_t pivot = { 1.0f, 2.0f, 3.0f };
    static mdxSequence_t sequence = { .interval = { 0, 4000 } };
    mdxEvent_t event = { .num_keys = 1, .globalSeqId = (uint32_t)-1, .keys = &key };
    mdxModel_t mdx = { .events = &event, .sequences = &sequence, .num_sequences = 1,
                       .pivots = &pivot, .num_pivots = 1 };
    model_t model = { .modeltype = ID_MDLX, .mdx = &mdx };
    renderEntity_t entity = { .origin = { 10.0f, 20.0f, 30.0f }, .model = &model, .number = 9 };
    wc3UberSplatData_t row = {
        .name = "TestUber", .scale = 64.0f, .birth_time = 1.0f, .pause_time = 1.0f, .decay_time = 1.0f,
        .start_r = 1.0f, .start_a = 1.0f,
        .middle_g = 1.0f, .middle_a = 1.0f,
        .end_b = 1.0f, .end_a = 0.0f,
        .texture = &test_splat_texture,
    };
    wc3UberSplatData_t *saved_rows = uber_splat_rows;
    uint32_t saved_count = uber_splat_count, saved_time = tr.viewDef.time;
    render_phase_t saved_phase = tr.render_phase;
    wc3EventState_t saved_state = event_state[9];

    snprintf(event.node.name, sizeof(event.node.name), "UBRxTestUber");
    event.node.node_id = 0; event.node.parent_id = (uint32_t)-1;
    mdx.nodes[0] = &event.node; mdx.node_list[0] = &event.node; mdx.num_nodes = 1;
    uber_splat_rows = &row; uber_splat_count = 1;
    event_state[9] = (wc3EventState_t){ 0 }; R_W3ClearEventSplats();
    test_splat_count = 0; tr.render_phase = RENDER_PHASE_SOLID; tr.viewDef.time = 0;

    R_UpdateEntityPresentation(&entity);
    entity.frame = 150; tr.viewDef.time = 150; R_UpdateEntityPresentation(&entity);
    T_EQ(test_splat_count, 1);
    T_FEQ(test_splat_origin.x, 11.0f, 0.001f);
    T_FEQ(test_splat_origin.y, 22.0f, 0.001f);
    T_FEQ(test_splat_radius, 64.0f, 0.001f);
    T_EQ(test_splat_color.r, 255); T_EQ(test_splat_color.g, 0); T_EQ(test_splat_color.b, 0);

    tr.viewDef.time = 1650; R_W3DrawEventSplats();
    T_EQ(test_splat_count, 2);
    T_EQ(test_splat_color.r, 0); T_EQ(test_splat_color.g, 255); T_EQ(test_splat_color.b, 0);
    tr.viewDef.time = 3150; R_W3DrawEventSplats();
    T_EQ(test_splat_count, 2);
    T_ASSERT(!event_splats[0].active);

    R_W3ClearEventSplats();
    uber_splat_rows = saved_rows; uber_splat_count = saved_count;
    event_state[9] = saved_state; tr.viewDef.time = saved_time; tr.render_phase = saved_phase;
}

TEST(renderer_model, production_spn_child_dispatches_its_own_ubr_event) {
    static uint32_t key = 100;
    static vec3_t pivot = { 1.0f, 2.0f, 3.0f };
    static mdxSequence_t sequence = { .interval = { 0, 1000 } };
    mdxEvent_t event = { .num_keys = 1, .globalSeqId = (uint32_t)-1, .keys = &key };
    mdxModel_t mdx = { .events = &event, .sequences = &sequence, .num_sequences = 1,
                       .pivots = &pivot, .num_pivots = 1 };
    model_t model = { .modeltype = ID_MDLX, .mdx = &mdx };
    wc3UberSplatData_t row = {
        .name = "TestUber", .scale = 16.0f, .birth_time = 1.0f, .pause_time = 1.0f, .decay_time = 1.0f,
        .start_r = 1.0f, .start_a = 1.0f, .middle_r = 1.0f, .middle_a = 1.0f,
        .end_r = 1.0f, .end_a = 1.0f, .texture = &test_splat_texture,
    };
    wc3EventSpawn_t spawn = { .model = &model, .scale = 1.0f, .active = true, .serial = 1 };
    wc3UberSplatData_t *saved_rows = uber_splat_rows;
    uint32_t saved_count = uber_splat_count, saved_time = tr.viewDef.time;
    render_phase_t saved_phase = tr.render_phase;

    snprintf(event.node.name, sizeof(event.node.name), "UBRxTestUber");
    event.node.node_id = 0; event.node.parent_id = (uint32_t)-1;
    mdx.nodes[0] = &event.node; mdx.node_list[0] = &event.node; mdx.num_nodes = 1;
    Matrix4_identity(&spawn.transform);
    spawn.transform.v[12] = 10.0f; spawn.transform.v[13] = 20.0f; spawn.transform.v[14] = 30.0f;
    uber_splat_rows = &row; uber_splat_count = 1;
    R_W3ClearEventSplats(); test_splat_count = 0; tr.render_phase = RENDER_PHASE_SOLID; tr.viewDef.time = 0;

    T_ASSERT(R_W3RenderEventSpawn(&spawn, 0)); /* seed child event state */
    tr.viewDef.time = 150;
    T_ASSERT(R_W3RenderEventSpawn(&spawn, 0));
    T_EQ(test_splat_count, 1);
    T_FEQ(test_splat_origin.x, 11.0f, 0.001f);
    T_FEQ(test_splat_origin.y, 22.0f, 0.001f);

    R_W3ClearEventSplats();
    uber_splat_rows = saved_rows; uber_splat_count = saved_count;
    tr.viewDef.time = saved_time; tr.render_phase = saved_phase;
}

TEST(renderer_model, production_snd_dispatch_uses_event_world_transform) {
    static uint32_t key = 100;
    static vec3_t pivot = { 1.0f, 2.0f, 3.0f };
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
    wc3EventState_t saved_state = event_state[8];
    void (*saved_play_sound)(cstring_t, vec3_t const *, float) = ri.PlaySoundAt;

    snprintf(event.node.name, sizeof(event.node.name), "SNDxTestSound");
    event.node.node_id = 0; event.node.parent_id = (uint32_t)-1;
    mdx.nodes[0] = &event.node; mdx.node_list[0] = &event.node; mdx.num_nodes = 1;
    anim_sound_rows = &sound; anim_sound_count = 1; event_state[8] = (wc3EventState_t){ 0 };
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
    ri.PlaySoundAt = saved_play_sound; event_state[8] = saved_state;
    tr.viewDef.time = saved_time; tr.render_phase = saved_phase;
}

static drawSprite_t cursor_drawn;
static color32_t cursor_tint;
rect_t R_UISceneRect(void) { return tr.uiScene; }
void R_TestCursorSprite(drawSprite_t const *sprite, color32_t tint) {
    cursor_drawn = *sprite; cursor_tint = tint;
}
mdxSequence_t const *R_TestCursorSequence(mdxModel_t const *model, cstring_t name) {
    for (int i = 0; i < model->num_sequences; i++)
        if (!strcmp(model->sequences[i].name, name)) return &model->sequences[i];
    return NULL;
}

TEST(renderer_cursor, retail_restart_and_paused_clock) {
    mdxSequence_t sequences[] = {
        { .name = "Normal", .interval = {333, 533} },
        { .name = "Select", .interval = {1000, 1500} },
        /* Deliberately non-stock duration: the model owns the interval. */
        { .name = "Scroll Right", .interval = {2000, 2273} },
    };
    mdxModel_t mdx = { .sequences = sequences, .num_sequences = 3 };
    model_t model = { .mdx = &mdx, .modeltype = ID_MDLX };
    drawCursor_t cursor = { .origin = {0.3f, 0.2f}, .tint = {255, 220, 80, 255}, .time = 100 };
    cursor_model = &model; cursor_anim = NULL;
    tr.viewDef.time = 100;
    T_ASSERT(R_DrawCursor(&cursor));
    T_STREQ(cursor_drawn.anim, "Normal");
    T_EQ(tr.viewDef.time - cursor_drawn.start_time, 0);
    /* Simulation remains frozen throughout; presentation keeps advancing. */
    cursor.time = 290; R_DrawCursor(&cursor);
    T_EQ(tr.viewDef.time - cursor_drawn.start_time, 190);
    cursor.time = 315; R_DrawCursor(&cursor);
    T_EQ(tr.viewDef.time - cursor_drawn.start_time, 15);
    cursor.time = 331; R_DrawCursor(&cursor);
    T_EQ(tr.viewDef.time - cursor_drawn.start_time, 16);
    cursor.hover = true; cursor.time = 351; R_DrawCursor(&cursor);
    T_STREQ(cursor_drawn.anim, "Select");
    T_EQ(tr.viewDef.time - cursor_drawn.start_time, 20);
    T_EQ(cursor_tint.g, 255);
    cursor.scroll.x = 1; cursor.time = 381; R_DrawCursor(&cursor);
    T_STREQ(cursor_drawn.anim, "Scroll Right");
    T_EQ(tr.viewDef.time - cursor_drawn.start_time, 30);
    T_EQ(cursor_tint.g, 255);
    /* A long frame crosses several loops; no 500ms clamp. */
    cursor.time = 1706; R_DrawCursor(&cursor);
    T_EQ(tr.viewDef.time - cursor_drawn.start_time, 263);
    cursor.time = 1723; R_DrawCursor(&cursor);
    T_EQ(tr.viewDef.time - cursor_drawn.start_time, 17);
    cursor_model = NULL; cursor_anim = NULL;
}

TEST(renderer_cursor, d3d_hotspot_pixel_centers) {
    mdxSequence_t sequence = { .name = "Normal", .interval = {333, 533} };
    mdxModel_t mdx = { .sequences = &sequence, .num_sequences = 1 };
    model_t model = { .mdx = &mdx, .modeltype = ID_MDLX };
    drawCursor_t cursor = { .origin = {0.403125f, 0.30078125f}, .tint = COLOR32_WHITE };
    cursor_model = &model; cursor_anim = NULL;
    tr.uiScene = (rect_t){0, 0, 0.8f, 0.6f};
    tr.drawableSize = (size2_t){1024, 768};
    R_DrawCursor(&cursor);
    T_FEQ(cursor_drawn.x, 0.403515625f, 0.0000001f);
    T_FEQ(cursor_drawn.y, 0.301171875f, 0.0000001f);
    /* Expanded canvas and high-DPI drawable: correction is half a physical pixel. */
    tr.uiScene.w = 1.2f;
    tr.drawableSize = (size2_t){2400, 1350};
    R_DrawCursor(&cursor);
    T_FEQ(cursor_drawn.x, cursor.origin.x + 0.00025f, 0.0000001f);
    T_FEQ(cursor_drawn.y, cursor.origin.y + 0.6f / 2700, 0.0000001f);
    cursor_model = NULL; cursor_anim = NULL;
    tr.uiScene = (rect_t){0}; tr.drawableSize = (size2_t){0};
}

TEST(renderer_cursor, nonlooping_custom_sequence_holds_endpoint) {
    mdxSequence_t sequence = { .name = "Normal", .interval = {600, 717}, .flags = 1 };
    mdxModel_t mdx = { .sequences = &sequence, .num_sequences = 1 };
    model_t model = { .mdx = &mdx, .modeltype = ID_MDLX };
    drawCursor_t cursor = { .tint = COLOR32_WHITE, .time = 100 };
    cursor_model = &model; cursor_anim = NULL;
    tr.viewDef.time = 0;
    R_DrawCursor(&cursor);
    cursor.time = 250; R_DrawCursor(&cursor);
    T_EQ(tr.viewDef.time - cursor_drawn.start_time, 117);
    cursor.time = 270; R_DrawCursor(&cursor);
    T_EQ(tr.viewDef.time - cursor_drawn.start_time, 117);
    cursor_model = NULL; cursor_anim = NULL;
}

TEST(renderer_cursor, authored_model_target_and_held_item) {
    mdxSequence_t seqs[] = {
        { .name = "Normal", .interval = {100, 300} },
        { .name = "Target", .interval = {500, 550} },
        { .name = "TargetSelect", .interval = {700, 900} },
        { .name = "HoldItem", .interval = {1000, 1200} },
        { .name = "Scroll Right", .interval = {1500, 1700} },
    };
    mdxModel_t mdx = { .sequences = seqs, .num_sequences = 5 };
    model_t model = { .mdx = &mdx, .modeltype = ID_MDLX }, other = model;
    texture_t icon = {0};
    drawCursor_t cursor = { .model = &model, .game = true, .tint = COLOR32_WHITE, .interaction = WC3_POINTER_TARGETING, .time = 100 };
    cursor_model = &model; cursor_anim = NULL;
    T_ASSERT(R_DrawCursor(&cursor));
    T_STREQ(cursor_drawn.anim, "Target");
    cursor.hover = true; cursor.hostile = true; cursor.tint = (color32_t){255, 0, 0, 255}; cursor.time = 120;
    R_DrawCursor(&cursor);
    T_STREQ(cursor_drawn.anim, "TargetSelect"); T_EQ(cursor_tint.r, 255); T_EQ(cursor_tint.g, 0);
    cursor.interaction = WC3_POINTER_HOLDING; cursor.skin = &icon; cursor.time = 140;
    R_DrawCursor(&cursor);
    T_STREQ(cursor_drawn.anim, "HoldItem"); T_ASSERT(cursor_drawn.skin == &icon); T_EQ(cursor_tint.g, 255);
    T_EQ(cursor_drawn.skin_slot, 21);
    cursor.scroll.x = 1; R_DrawCursor(&cursor);
    T_STREQ(cursor_drawn.anim, "Scroll Right"); T_NULL(cursor_drawn.skin);
    cursor.scroll.x = 0; R_DrawCursor(&cursor);
    T_STREQ(cursor_drawn.anim, "HoldItem"); T_ASSERT(cursor_drawn.skin == &icon);
    cursor.interaction = 0; cursor.hover = false; cursor.skin = NULL; cursor.model = &other;
    R_DrawCursor(&cursor);
    T_ASSERT(cursor_drawn.model == &other); T_NULL(cursor_drawn.skin); T_STREQ(cursor_drawn.anim, "Normal");
    cursor_model = NULL; cursor_anim = NULL;
}

TEST(renderer_cursor, signal_locks_targetselect_and_restores_target) {
    mdxSequence_t seqs[] = {
        { .name = "Normal", .interval = {0, 200} },
        { .name = "Target", .interval = {200, 400} },
        { .name = "TargetSelect", .interval = {400, 600} },
        { .name = "Scroll Right", .interval = {600, 800} },
    };
    mdxModel_t mdx = { .sequences = seqs, .num_sequences = 4 };
    model_t model = { .mdx = &mdx, .modeltype = ID_MDLX };
    drawCursor_t cursor = { .model = &model, .game = true, .interaction = WC3_POINTER_SIGNALING,
        .tint = {17, 83, 149, 255}, .time = 100 };
    texture_t color = { .first_pixel = {17,83,149,255}, .has_first_pixel = true };
    cursor.skin = &color;
    cursor_active_model = NULL;
    T_ASSERT(R_DrawCursor(&cursor));
    T_STREQ(cursor_drawn.anim, "TargetSelect");
    T_EQ(cursor_tint.r, 17); T_EQ(cursor_tint.g, 83); T_EQ(cursor_tint.b, 149);
    cursor.scroll.x = 1; cursor.hover = true;
    R_DrawCursor(&cursor);
    T_STREQ(cursor_drawn.anim, "TargetSelect");
    cursor.interaction = WC3_POINTER_TARGETING; cursor.scroll.x = 0; cursor.hover = false;
    R_DrawCursor(&cursor);
    T_STREQ(cursor_drawn.anim, "Target");
    cursor_active_model = NULL; cursor_anim = NULL;
}

TEST(renderer_cursor, resolves_all_retail_modes) {
    struct { int interaction, hover, owned, hostile, x, y, mode; } const cases[] = {
        {0,0,0,0, 0,0, 0}, {0,1,0,0, 0,0, 1}, {0,1,0,1, 0,0, 2}, {0,1,1,0, 0,0, 3},
        {1,0,0,0, 0,0, 4}, {1,1,0,0, 0,0, 5}, {1,1,0,1, 0,0, 6}, {1,1,1,0, 0,0, 7},
        {3,1,1,1, 1,1, 8}, {2,1,1,1, 0,0, 9},
        {1,1,1,1, -1,0,10}, {1,1,1,1, 1,0,11}, {1,1,1,1, 0,1,12}, {1,1,1,1, 0,-1,13},
        {2,1,1,1, -1,1,14}, {2,1,1,1, 1,1,15}, {2,1,1,1, -1,-1,16}, {2,1,1,1, 1,-1,17},
    };
    cstring_t const names[] = {"Normal", "Select", "Select", "Select", "Target",
        "TargetSelect", "TargetSelect", "TargetSelect", "TargetSelect", "HoldItem",
        "Scroll Left", "Scroll Right", "Scroll Up", "Scroll Down", "Scroll Up Left",
        "Scroll Up Right", "Scroll Down Left", "Scroll Down Right"};
    mdxSequence_t seqs[18] = {0};
    FOR_LOOP(i, 18) { snprintf(seqs[i].name, sizeof(seqs[i].name), "%s", names[i]); seqs[i].interval[1] = 100; }
    mdxModel_t mdx = { .sequences = seqs, .num_sequences = 18 };
    model_t model = { .mdx = &mdx, .modeltype = ID_MDLX };
    texture_t color = { .first_pixel = {19,87,151,255}, .has_first_pixel = true };
    cursor_active_model = NULL;
    FOR_LOOP(i, 18) {
        drawCursor_t cursor = { .model = &model, .skin = &color, .interaction = cases[i].interaction,
            .hover = cases[i].hover, .owned = cases[i].owned, .hostile = cases[i].hostile,
            .scroll = {cases[i].x, cases[i].y}, .time = 100 + i * 7 };
        T_EQ(R_ResolveCursorMode(&cursor), cases[i].mode);
        T_ASSERT(R_DrawCursor(&cursor)); T_STREQ(cursor_drawn.anim, names[i]);
        color32_t expected = i == 8 ? color.first_pixel :
            (i == 1 || i == 5) ? (color32_t){255,255,0,255} :
            (i == 2 || i == 6) ? (color32_t){255,0,0,255} :
            (i == 3 || i == 7) ? (color32_t){0,255,0,255} : COLOR32_WHITE;
        T_ASSERT(!memcmp(&cursor_tint, &expected, sizeof(expected)));
        T_ASSERT(cursor_drawn.skin == (i == 9 ? &color : NULL));
    }
    cursor_active_model = NULL; cursor_anim = NULL;
}
