#ifdef BZ_TESTS
/* Persistent Hero/idle-worker shortcut state tests. */
#include "test.h"
#include "../g_local.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);

static DWORD shortcut_root_number;
static BOOL shortcut_root_extended;
static BOOL shortcut_hero_parented;
static BOOL shortcut_worker_parented;
static BOOL shortcut_count_parented;

static int shortcut_test_image(LPCSTR name) {
    T_ASSERT(name && *name);
    return 1;
}

static int shortcut_test_font(LPCSTR name, DWORD size) {
    T_ASSERT(name && *name);
    T_EQ(size, HUD_FONT_SIZE);
    return 1;
}

static void shortcut_test_unicast(LPEDICT ent) { (void)ent; }

static void shortcut_test_write(pfWriteType_t type, void const *value) {
    LPCUIFRAME frame;

    if (type != PF_UIFRAME || !value) return;
    frame = value;
    if (frame->flags.type == FT_SIMPLEFRAME &&
        (frame->flagsvalue & UIFLAG_EXTEND_WIDESCREEN_X)) {
        shortcut_root_number = frame->number;
        shortcut_root_extended = true;
        T_FEQ(frame->size.width, UI_BASE_WIDTH, 0.0001f);
        T_FEQ(frame->size.height, UI_BASE_HEIGHT, 0.0001f);
        return;
    }
    if (!shortcut_root_number || frame->parent != shortcut_root_number) return;
    T_EQ(frame->points.x[FPP_MIN].relativeTo, UI_PARENT);
    T_EQ(frame->points.y[FPP_MIN].relativeTo, UI_PARENT);
    if (frame->flags.type == FT_COMMANDBUTTON && frame->onclick) {
        if (!strncmp(frame->onclick, "herobutton ", 11)) shortcut_hero_parented = true;
        if (!strncmp(frame->onclick, "idleworker ", 11)) shortcut_worker_parented = true;
    } else if (frame->flags.type == FT_STRING) {
        shortcut_count_parented = true;
    }
}

TEST(wc3_shortcuts, peasant_plain_stand_is_idle_but_busy_move_is_not) {
    umove_t busy = { "walk", NULL, NULL, NULL };
    LPEDICT worker;

    reset_entities();
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    worker->svflags |= SVF_MONSTER;
    worker->s.player = 0;
    worker->stand = unit_stand;
    unit_stand(worker);

    T_ASSERT(G_ActorHasSkill(worker, "Ahar"));
    T_ASSERT(G_UnitIsIdleWorker(worker));
    unit_setmove(worker, &busy);
    T_ASSERT(!G_UnitIsIdleWorker(worker));
    unit_stand(worker);
    T_ASSERT(G_UnitIsIdleWorker(worker));
}

TEST(wc3_shortcuts, hold_position_worker_is_not_idle) {
    LPEDICT worker;

    reset_entities();
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    worker->svflags |= SVF_MONSTER;
    worker->s.player = 0;
    worker->stand = unit_stand;
    worker->movement.holding_position = true;
    unit_stand(worker);

    T_ASSERT(!G_UnitIsIdleWorker(worker));
}

TEST(wc3_shortcuts, controlled_unit_invalidation_marks_player_dirty) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT worker;

    reset_entities();
    setup_test_world();
    client->ps.number = 0;
    client->shortcuts.dirty = false;
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    worker->svflags |= SVF_MONSTER;
    worker->s.player = 0;

    G_InvalidateUnitShortcutsForUnit(worker);
    T_ASSERT(client->shortcuts.dirty);
}

TEST(wc3_shortcuts, hero_button_double_click_selects_then_centers_camera) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT clent;
    LPEDICT hero;

    reset_entities();
    setup_test_world();
    client->ps.number = 0;
    clent = &g_edicts[0];
    clent->client = client;
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 320.0f, 448.0f);
    hero->svflags |= SVF_MONSTER;
    hero->s.player = 0;
    client->camera.state.position = (VECTOR2){ 64.0f, 96.0f };
    client->camera.old_state.position = client->camera.state.position;
    level.time = 1000;

    G_ActivateHeroButton(clent, hero->s.number);
    T_ASSERT(G_IsEntitySelected(client, hero));
    T_FEQ(client->camera.state.position.x, 64.0f, 0.001f);
    T_FEQ(client->camera.state.position.y, 96.0f, 0.001f);

    G_ActivateHeroButton(clent, hero->s.number);
    T_FEQ(client->camera.state.position.x, hero->s.origin2.x, 0.001f);
    T_FEQ(client->camera.state.position.y, hero->s.origin2.y, 0.001f);

    level.time += 501; /* beyond Warsmash 500 ms double-click window */
    client->camera.state.position = (VECTOR2){ 80.0f, 112.0f };
    client->camera.old_state.position = client->camera.state.position;
    G_ActivateHeroButton(clent, hero->s.number);
    T_FEQ(client->camera.state.position.x, 80.0f, 0.001f);
    T_FEQ(client->camera.state.position.y, 112.0f, 0.001f);
}

TEST(wc3_shortcuts, hero_function_key_requires_quick_second_press_to_center) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT clent;
    LPEDICT hero;

    reset_entities();
    setup_test_world();
    client->ps.number = 0;
    clent = &g_edicts[0];
    clent->client = client;
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 352.0f, 480.0f);
    hero->svflags |= SVF_MONSTER;
    hero->s.player = 0;
    client->camera.state.position = (VECTOR2){ 96.0f, 128.0f };
    client->camera.old_state.position = client->camera.state.position;
    level.time = 3000;

    G_ActivateHeroKey(clent, 0);
    T_ASSERT(G_IsEntitySelected(client, hero));
    T_FEQ(client->camera.state.position.x, 96.0f, 0.001f);
    T_FEQ(client->camera.state.position.y, 128.0f, 0.001f);

    level.time += 200;
    G_ActivateHeroKey(clent, 0);
    T_FEQ(client->camera.state.position.x, hero->s.origin2.x, 0.001f);
    T_FEQ(client->camera.state.position.y, hero->s.origin2.y, 0.001f);

    level.time += 501;
    client->camera.state.position = (VECTOR2){ 112.0f, 144.0f };
    client->camera.old_state.position = client->camera.state.position;
    G_ActivateHeroKey(clent, 0);
    T_FEQ(client->camera.state.position.x, 112.0f, 0.001f);
    T_FEQ(client->camera.state.position.y, 144.0f, 0.001f);
}

TEST(wc3_shortcuts, hud_buttons_share_full_canvas_left_root) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT clent;
    LPEDICT hero;
    LPEDICT worker;
    UnitProfile_t hero_profile;
    UnitProfile_t worker_profile;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    int (*old_image)(LPCSTR) = gi.ImageIndex;
    int (*old_font)(LPCSTR, DWORD) = gi.FontIndex;

    reset_entities();
    setup_test_world();
    client->ps.number = 0;
    client->connected = true;
    clent = &g_edicts[0];
    clent->client = client;

    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 64.0f, 64.0f);
    hero->svflags |= SVF_MONSTER;
    hero->s.player = 0;
    T_NOT_NULL(hero->data.UnitProfile);
    hero_profile = *hero->data.UnitProfile;
    hero_profile.art = "TestUI\\Textures\\solid_white.blp";
    hero->data.UnitProfile = &hero_profile;

    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 96.0f, 96.0f);
    worker->svflags |= SVF_MONSTER;
    worker->s.player = 0;
    T_NOT_NULL(worker->data.UnitProfile);
    worker_profile = *worker->data.UnitProfile;
    worker_profile.art = "TestUI\\Textures\\solid_white.blp";
    worker->data.UnitProfile = &worker_profile;
    worker->stand = unit_stand;
    unit_stand(worker);
    T_ASSERT(G_UnitShowsHeroShortcut(client, hero));
    T_ASSERT(G_UnitShowsIdleWorkerShortcut(client, worker));

    shortcut_root_number = 0;
    shortcut_root_extended = false;
    shortcut_hero_parented = false;
    shortcut_worker_parented = false;
    shortcut_count_parented = false;
    gi.Write = shortcut_test_write;
    gi.unicast = shortcut_test_unicast;
    gi.ImageIndex = shortcut_test_image;
    gi.FontIndex = shortcut_test_font;

    UI_WriteUnitShortcutLayer(clent);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    gi.ImageIndex = old_image;
    gi.FontIndex = old_font;
    T_ASSERT(shortcut_root_extended);
    T_EQ(shortcut_root_number, 1);
    T_ASSERT(shortcut_hero_parented);
    T_ASSERT(shortcut_worker_parented);
    T_ASSERT(shortcut_count_parented);
}
#endif
