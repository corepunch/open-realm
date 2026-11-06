#ifdef BZ_TESTS
/* Persistent Hero/idle-worker shortcut state tests. */
#include "test.h"
#include "../g_local.h"

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);

static uint32_t shortcut_root_number;
static bool shortcut_root_extended;
static bool shortcut_hero_parented;
static bool shortcut_worker_parented;
static bool shortcut_count_parented;
static bool shortcut_skill_count_seen;
static bool shortcut_hero_alert_seen;
static float shortcut_hero_alert_deadline;
static uint32_t shortcut_hero_entities[8];
static uint32_t shortcut_hero_entity_count;
static uint32_t shortcut_vital_entities[2][8];
static uint32_t shortcut_vital_counts[2];
static uint32_t shortcut_vital_backdrop_count;
static int16_t shortcut_hero_y[8];
static int16_t shortcut_bar_y[2][8];
static edict_t *shortcut_targeted_hero;

/* Record the actual game-written HUD vital wire contract without a server
 * or renderer mock: snapshots and fog should never be consulted. */
static int32_t shortcut_wire[128];
static uint32_t shortcut_wire_count, shortcut_wire_packets;
static handle_t (*shortcut_original_memalloc)(long size);
static bool shortcut_fail_vital_alloc;
static handle_t shortcut_test_memalloc(long size) {
    if (shortcut_fail_vital_alloc && size == (long)sizeof(unitShortcutVital_t)) {
        shortcut_fail_vital_alloc = false;
        return NULL;
    }
    return shortcut_original_memalloc(size);
}

static void shortcut_vital_write(pfWriteType_t type, void const *value) {
    T_ASSERT(type == PF_BYTE || type == PF_SHORT);
    T_ASSERT(shortcut_wire_count < 128);
    if (shortcut_wire_count < 128) shortcut_wire[shortcut_wire_count++] = *(int32_t const *)value;
}
static void shortcut_vital_unicast(edict_t *ent) {
    T_NOT_NULL(ent);
    shortcut_wire_packets++;
}

static int shortcut_test_image(cstring_t name) {
    T_ASSERT(name && *name);
    return 1;
}

static int shortcut_test_font(cstring_t name, uint32_t size) {
    T_ASSERT(name && *name);
    T_EQ(size, HUD_FONT_SIZE);
    return 1;
}

static void shortcut_test_unicast(edict_t *ent) { (void)ent; }

static bool shortcut_test_entity_target(edict_t *clent, edict_t *target) {
    T_NOT_NULL(clent);
    T_NOT_NULL(target);
    shortcut_targeted_hero = target;
    if (clent && clent->client) clent->client->menu.on_entity_selected = NULL;
    return true;
}

static void shortcut_test_write(pfWriteType_t type, void const *value) {
    uiFrame_t const *frame;

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
    if (frame->flags.type == FT_TEXTURE && frame->tex.index) {
        shortcut_vital_backdrop_count++;
        T_EQ(frame->color.r, 0);
        T_EQ(frame->color.g, 0);
        T_EQ(frame->color.b, 0);
        T_EQ(frame->color.a, 220);
        T_FEQ(frame->size.width, 0.0360f, 0.0001f);
        T_FEQ(frame->size.height, 0.0060f, 0.0001f);
    } else if (frame->flags.type == FT_COMMANDBUTTON && frame->onclick) {
        if (!strncmp(frame->onclick, "herobutton ", 11)) {
            unsigned number;

            shortcut_hero_parented = true;
            shortcut_hero_alert_seen = !!(frame->flagsvalue & UIFLAG_ALERT_RED_PULSE);
            shortcut_hero_alert_deadline = frame->value;
            if (shortcut_hero_entity_count < sizeof(shortcut_hero_entities) / sizeof(shortcut_hero_entities[0]) &&
                sscanf(frame->onclick, "herobutton %u", &number) == 1) {
                shortcut_hero_y[shortcut_hero_entity_count] = frame->points.y[FPP_MIN].offset;
                shortcut_hero_entities[shortcut_hero_entity_count++] = (uint32_t)number;
            }
        }
        if (!strncmp(frame->onclick, "idleworker ", 11)) shortcut_worker_parented = true;
    } else if (frame->flags.type == FT_SIMPLESTATUSBAR) {
        uint32_t kind;

        T_ASSERT(frame->flagsvalue & UIFLAG_ENTITY_STATUSBAR);
        T_ASSERT(frame->stat == ENT_HEALTH || frame->stat == ENT_MANA);
        T_EQ(!!(frame->flagsvalue & UIFLAG_HEALTH_GRADIENT), frame->stat == ENT_HEALTH);
        T_ASSERT(frame->tex.index);
        T_FEQ(frame->size.width, 0.0340f, 0.0001f);
        T_FEQ(frame->size.height, 0.0040f, 0.0001f);
        T_EQ(frame->color.r, 0);
        T_EQ(frame->color.g, 255);
        T_EQ(frame->color.b, frame->stat == ENT_HEALTH ? 0 : 255);
        kind = frame->stat == ENT_HEALTH ? 0 : 1;
        if (shortcut_vital_counts[kind] < 8) {
            shortcut_bar_y[kind][shortcut_vital_counts[kind]] = frame->points.y[FPP_MIN].offset;
            shortcut_vital_entities[kind][shortcut_vital_counts[kind]++] = (uint32_t)frame->value;
        }
    } else if (frame->flags.type == FT_STRING) {
        shortcut_count_parented = true;
        if (frame->text && !strcmp(frame->text, "2")) shortcut_skill_count_seen = true;
    }
}

TEST(wc3_shortcuts, peasant_plain_stand_is_idle_but_busy_move_is_not) {
    umove_t busy = { "walk", NULL, NULL, NULL, .travel = true };
    edict_t *worker;

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
    edict_t *worker;

    reset_entities();
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    worker->svflags |= SVF_MONSTER;
    worker->s.player = 0;
    worker->stand = unit_stand;
    worker->orders->holding_position = true;
    unit_stand(worker);

    T_ASSERT(!G_UnitIsIdleWorker(worker));
}

TEST(wc3_shortcuts, controlled_unit_invalidation_marks_player_dirty) {
    gameClient_t *client = &game.clients[0];
    edict_t *worker;

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


TEST(wc3_shortcuts, hidden_hero_is_not_in_shortcut_roster) {
    gameClient_t *client = &game.clients[0];
    edict_t *hero;

    reset_entities();
    setup_test_world();
    client->ps.number = 0;
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 64.0f, 64.0f);
    hero->svflags |= SVF_MONSTER;
    hero->s.player = 0;

    T_ASSERT(G_UnitShowsHeroShortcut(client, hero));
    hero->s.renderfx |= RF_HIDDEN;
    T_ASSERT(!G_UnitShowsHeroShortcut(client, hero));
    hero->s.renderfx &= ~RF_HIDDEN;
    T_ASSERT(G_UnitShowsHeroShortcut(client, hero));
}

TEST(wc3_shortcuts, hero_skill_point_change_invalidates_shortcut_badge) {
    gameClient_t *client = &game.clients[0];
    edict_t *hero;

    reset_entities();
    setup_test_world();
    client->ps.number = 0;
    client->shortcuts.dirty = false;
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 64.0f, 64.0f);
    hero->svflags |= SVF_MONSTER;
    hero->s.player = 0;
    hero->unit_state->hero.skillpoints = 0;

    T_ASSERT(G_HeroModifySkillPoints(hero, 1));
    T_EQ(hero->unit_state->hero.skillpoints, 1);
    T_ASSERT(client->shortcuts.dirty);
}

TEST(wc3_shortcuts, hero_damage_alert_sets_deadline_and_invalidates_owner) {
    gameClient_t *client = &game.clients[0];
    edict_t *hero;

    reset_entities();
    setup_test_world();
    client->ps.number = 0;
    client->shortcuts.dirty = false;
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 64.0f, 64.0f);
    hero->svflags |= SVF_MONSTER;
    hero->s.player = 0;
    level.time = 5000;

    G_AlertHeroShortcutDamage(hero);

    T_ASSERT(hero->unit_state->shortcut_alert_until > level.time);
    T_ASSERT(client->shortcuts.dirty);
}

TEST(wc3_shortcuts, hero_button_double_click_selects_then_centers_camera) {
    gameClient_t *client = &game.clients[0];
    edict_t *clent;
    edict_t *hero;

    reset_entities();
    setup_test_world();
    client->ps.number = 0;
    clent = &g_edicts[0];
    clent->client = client;
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 320.0f, 448.0f);
    hero->svflags |= SVF_MONSTER;
    hero->s.player = 0;
    client->camera.state.position = (vec2_t){ 64.0f, 96.0f };
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
    client->camera.state.position = (vec2_t){ 80.0f, 112.0f };
    client->camera.old_state.position = client->camera.state.position;
    G_ActivateHeroButton(clent, hero->s.number);
    T_FEQ(client->camera.state.position.x, 80.0f, 0.001f);
    T_FEQ(client->camera.state.position.y, 112.0f, 0.001f);
}

TEST(wc3_shortcuts, hero_button_targets_visible_hero_during_entity_targeting) {
    gameClient_t *client = &game.clients[0];
    edict_t *clent;
    edict_t *hero;
    edict_t *nonhero;
    char number[16];
    cstring_t command[] = { "herobutton", number };

    reset_entities();
    setup_test_world();
    client->ps.number = 0;
    clent = &g_edicts[0];
    clent->client = client;
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 320.0f, 448.0f);
    hero->svflags |= SVF_MONSTER;
    hero->s.player = 0;
    shortcut_targeted_hero = NULL;
    client->menu.on_entity_selected = shortcut_test_entity_target;
    snprintf(number, sizeof(number), "%u", (unsigned)hero->s.number);

    G_ClientCommand(clent, 2, command);

    T_ASSERT(shortcut_targeted_hero == hero);
    T_ASSERT(!G_IsEntitySelected(client, hero));
    T_NULL(client->menu.on_entity_selected);
    T_FEQ(client->camera.state.position.x, 0.0f, 0.001f);
    T_FEQ(client->camera.state.position.y, 0.0f, 0.001f);

    nonhero = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96.0f, 128.0f);
    nonhero->svflags |= SVF_MONSTER;
    nonhero->s.player = 0;
    client->menu.on_entity_selected = shortcut_test_entity_target;
    snprintf(number, sizeof(number), "%u", (unsigned)nonhero->s.number);
    G_ClientCommand(clent, 2, command);
    T_ASSERT(shortcut_targeted_hero == hero);
    T_NOT_NULL(client->menu.on_entity_selected);
    T_ASSERT(!G_IsEntitySelected(client, nonhero));
}

TEST(wc3_shortcuts, hero_function_key_requires_quick_second_press_to_center) {
    gameClient_t *client = &game.clients[0];
    edict_t *clent;
    edict_t *hero;

    reset_entities();
    setup_test_world();
    client->ps.number = 0;
    clent = &g_edicts[0];
    clent->client = client;
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 352.0f, 480.0f);
    hero->svflags |= SVF_MONSTER;
    hero->s.player = 0;
    client->camera.state.position = (vec2_t){ 96.0f, 128.0f };
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
    client->camera.state.position = (vec2_t){ 112.0f, 144.0f };
    client->camera.old_state.position = client->camera.state.position;
    G_ActivateHeroKey(clent, 0);
    T_FEQ(client->camera.state.position.x, 112.0f, 0.001f);
    T_FEQ(client->camera.state.position.y, 144.0f, 0.001f);
}

TEST(wc3_shortcuts, hero_buttons_match_multiselect_order) {
    gameClient_t *client = &game.clients[0];
    edict_t *clent;
    edict_t *heroes[3];
    edict_t *ordered[3] = { 0 };
    UnitData_t hero_data[3] = {
        { .priority = 1 },
        { .priority = 3 },
        { .priority = 2 },
    };
    UnitProfile_t hero_profile = { .art = "TestUI\\Textures\\solid_white.blp" };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    int (*old_image)(cstring_t) = gi.ImageIndex;
    int (*old_font)(cstring_t, uint32_t) = gi.FontIndex;

    reset_entities();
    setup_test_world();
    client->ps.number = 0;
    client->connected = true;
    clent = &g_edicts[0];
    clent->client = client;

    FOR_LOOP(i, 3) {
        heroes[i] = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 64.0f + i * 32.0f, 64.0f);
        heroes[i]->svflags |= SVF_MONSTER;
        heroes[i]->s.player = 0;
        heroes[i]->data.UnitData = &hero_data[i];
        heroes[i]->data.UnitProfile = &hero_profile;
        heroes[i]->mana.max_value = 100.0f;
        heroes[i]->mana.value = 50.0f;
        T_ASSERT(G_UnitShowsHeroShortcut(client, heroes[i]));
        G_SelectEntity(client, heroes[i]);
    }

    T_EQ(G_GetOrderedSelectedUnits(client, ordered, 3), 3);
    T_ASSERT(ordered[0] == heroes[1]);
    T_ASSERT(ordered[1] == heroes[2]);
    T_ASSERT(ordered[2] == heroes[0]);

    shortcut_root_number = 0;
    shortcut_root_extended = false;
    shortcut_hero_entity_count = 0;
    memset(shortcut_hero_entities, 0, sizeof(shortcut_hero_entities));
    memset(shortcut_vital_counts, 0, sizeof(shortcut_vital_counts));
    shortcut_vital_backdrop_count = 0;
    gi.Write = shortcut_test_write;
    gi.unicast = shortcut_test_unicast;
    gi.ImageIndex = shortcut_test_image;
    gi.FontIndex = shortcut_test_font;

    UI_WriteUnitShortcutLayer(clent);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    gi.ImageIndex = old_image;
    gi.FontIndex = old_font;
    T_EQ(shortcut_hero_entity_count, 3);
    T_EQ(shortcut_vital_counts[0], 3);
    T_EQ(shortcut_vital_counts[1], 3);
    T_EQ(shortcut_vital_backdrop_count, 6);
    FOR_LOOP(i, 3) {
        T_EQ(shortcut_hero_entities[i], ordered[i]->s.number);
        T_EQ(shortcut_vital_entities[0][i], ordered[i]->s.number);
        T_EQ(shortcut_vital_entities[1][i], ordered[i]->s.number);
        /* Y offsets are negative for positions farther down the HUD. */
        T_ASSERT(shortcut_bar_y[0][i] < shortcut_hero_y[i]);
        T_ASSERT(shortcut_bar_y[1][i] < shortcut_bar_y[0][i]);
        if (i > 0) T_ASSERT(shortcut_hero_y[i] < shortcut_bar_y[1][i - 1]);
    }
    G_ClearUnitShortcutVitals(client);

    /* F1-F7 address the same ordered Hero roster as the visible buttons. */
    client->connected = false;
    level.time = 100000;
    G_ActivateHeroKey(clent, 0);
    T_ASSERT(G_IsEntitySelected(client, ordered[0]));
    T_ASSERT(!G_IsEntitySelected(client, ordered[1]));
    T_ASSERT(!G_IsEntitySelected(client, ordered[2]));
}

TEST(wc3_shortcuts, hud_buttons_share_full_canvas_left_root) {
    gameClient_t *client = &game.clients[0];
    edict_t *clent;
    edict_t *hero;
    edict_t *worker;
    UnitProfile_t hero_profile;
    UnitProfile_t worker_profile;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    int (*old_image)(cstring_t) = gi.ImageIndex;
    int (*old_font)(cstring_t, uint32_t) = gi.FontIndex;

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
    hero->unit_state->hero.skillpoints = 2;
    hero->unit_state->shortcut_alert_until = 6000;
    hero->mana.max_value = 0.0f; /* The client hides this Hero's passive mana bar. */
    level.time = 5000;

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
    shortcut_skill_count_seen = false;
    shortcut_hero_alert_seen = false;
    shortcut_hero_alert_deadline = 0.0f;
    shortcut_hero_entity_count = 0;
    memset(shortcut_hero_entities, 0, sizeof(shortcut_hero_entities));
    memset(shortcut_vital_counts, 0, sizeof(shortcut_vital_counts));
    shortcut_vital_backdrop_count = 0;
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
    T_ASSERT(shortcut_skill_count_seen);
    T_ASSERT(shortcut_hero_alert_seen);
    T_FEQ(shortcut_hero_alert_deadline, 6000.0f, 0.01f);
    T_EQ(shortcut_vital_counts[0], 1);
    T_EQ(shortcut_vital_counts[1], 1);
    T_EQ(shortcut_vital_backdrop_count, 2);
    T_EQ(shortcut_vital_entities[0][0], hero->s.number);
    T_EQ(shortcut_vital_entities[1][0], hero->s.number);
    G_ClearUnitShortcutVitals(client);
}

/* Shared control without shared vision keeps its Hero HUD alive but must not
 * cause world snapshots or coordinates to be sent just for the vital bars. */
TEST(wc3_shortcuts, controlled_hero_vitals_are_delta_sent_without_vision_and_follow_mana_pool) {
    gameClient_t *client = &game.clients[0];
    edict_t *clent, *hero;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    int (*old_image)(cstring_t) = gi.ImageIndex;
    int (*old_font)(cstring_t, uint32_t) = gi.FontIndex;
    UnitProfile_t profile = { .art = "TestUI\\Textures\\solid_white.blp" };
    uint32_t saved_alliance;

    reset_entities(); setup_test_world();
    saved_alliance = level.alliances[0][1];
    level.alliances[0][1] = 0;
    client->ps.number = 0;
    game.clients[1].ps.number = 1;
    client->connected = true;
    clent = g_edicts;
    clent->client = client;
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 400.0f, 600.0f);
    hero->svflags |= SVF_MONSTER;
    hero->s.player = 1;
    hero->data.UnitProfile = &profile;
    hero->health.value = hero->health.max_value * 0.5f;
    hero->mana.max_value = 100.0f;
    hero->mana.value = 0.0f;
    G_SetPlayerAlliance(&game.clients[0].ps, &game.clients[1].ps, ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(&game.clients[0].ps, &game.clients[1].ps, ALLIANCE_SHARED_CONTROL, true);
    T_ASSERT(!G_GetPlayerAlliance(&game.clients[0].ps, &game.clients[1].ps, ALLIANCE_SHARED_VISION));
    T_ASSERT(G_UnitShowsHeroShortcut(client, hero));

    gi.Write = shortcut_test_write; gi.unicast = shortcut_test_unicast;
    gi.ImageIndex = shortcut_test_image; gi.FontIndex = shortcut_test_font;
    UI_WriteUnitShortcutLayer(clent);
    T_EQ(client->shortcuts.vital_count, 1);
    gi.Write = shortcut_vital_write; gi.unicast = shortcut_vital_unicast;
    shortcut_wire_count = shortcut_wire_packets = 0;
    G_SendUnitShortcutVitals(clent, true);
    T_EQ(shortcut_wire_packets, 1);
    T_EQ(shortcut_wire_count, 7);
    T_EQ(shortcut_wire[0], svc_ui_entity_stats);
    T_EQ(shortcut_wire[1], 1);
    T_EQ(shortcut_wire[2], 1);
    T_EQ(shortcut_wire[3], hero->s.number);
    T_EQ(shortcut_wire[4], compress_stat(&hero->health));
    T_EQ(shortcut_wire[5], 0);
    T_EQ(shortcut_wire[6], 1);
    G_SendUnitShortcutVitals(clent, false);
    T_EQ(shortcut_wire_packets, 1); /* no change, no packet */

    /* Zero mana with no pool still changes presentation and requires a HUD
     * update, even though the compressed current-mana byte stays zero. */
    hero->mana.max_value = 0.0f;
    G_SendUnitShortcutVitals(clent, false);
    T_EQ(shortcut_wire_packets, 2);
    T_EQ(shortcut_wire[7], svc_ui_entity_stats);
    T_EQ(shortcut_wire[8], 0);
    T_EQ(shortcut_wire[13], 0);
    hero->mana.max_value = 100.0f;
    G_SendUnitShortcutVitals(clent, false);
    T_EQ(shortcut_wire_packets, 3);
    T_EQ(shortcut_wire[20], 1);

    hero->health.value = 0.0f;
    G_SendUnitShortcutVitals(clent, false);
    T_EQ(shortcut_wire_packets, 4);
    T_EQ(shortcut_wire[25], 0);

    /* An edict reused for a different spawn must invalidate the cached roster
     * before any new vitals can be sent under the old identity. */
    hero->spawn_time++;
    client->shortcuts.dirty = false;
    G_SendUnitShortcutVitals(clent, false);
    T_ASSERT(client->shortcuts.dirty);
    T_EQ(shortcut_wire_packets, 5);
    T_EQ(shortcut_wire[28], svc_ui_entity_stats);
    T_EQ(shortcut_wire[29], 1);
    T_EQ(shortcut_wire[30], 0);

    /* Revoking shared control invalidates the authoritatively permitted
     * roster rather than continuing to stream that ally's health. */
    hero->spawn_time--;
    client->shortcuts.dirty = false;
    level.alliances[0][1] &= ~(1u << ALLIANCE_SHARED_CONTROL);
    G_SendUnitShortcutVitals(clent, false);
    T_ASSERT(client->shortcuts.dirty);
    T_EQ(shortcut_wire_packets, 6);
    T_EQ(shortcut_wire[31], svc_ui_entity_stats);
    T_EQ(shortcut_wire[32], 1);
    T_EQ(shortcut_wire[33], 0);

    level.alliances[0][1] = saved_alliance;
    G_SetClientConnected(clent, false);
    T_ASSERT(client->shortcuts.vitals == NULL);
    T_EQ(client->shortcuts.vital_count, 0);
    gi.Write = old_write; gi.unicast = old_unicast;
    gi.ImageIndex = old_image; gi.FontIndex = old_font;
}

TEST(wc3_shortcuts, vital_cache_allocation_failure_preserves_shortcut_controls) {
    gameClient_t *client = &game.clients[0];
    edict_t *clent, *hero;
    UnitProfile_t profile = { .art = "TestUI\\Textures\\solid_white.blp" };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    int (*old_image)(cstring_t) = gi.ImageIndex;
    int (*old_font)(cstring_t, uint32_t) = gi.FontIndex;

    reset_entities(); setup_test_world();
    client->ps.number = 0;
    clent = g_edicts;
    clent->client = client;
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 64.0f, 64.0f);
    hero->svflags |= SVF_MONSTER;
    hero->s.player = 0;
    hero->data.UnitProfile = &profile;

    shortcut_root_number = 0;
    shortcut_hero_parented = false;
    shortcut_hero_entity_count = 0;
    memset(shortcut_vital_counts, 0, sizeof(shortcut_vital_counts));
    gi.Write = shortcut_test_write;
    gi.ImageIndex = shortcut_test_image;
    gi.FontIndex = shortcut_test_font;
    shortcut_original_memalloc = gi.MemAlloc;
    shortcut_fail_vital_alloc = true;
    gi.MemAlloc = shortcut_test_memalloc;

    UI_WriteUnitShortcutLayer(clent);

    gi.MemAlloc = shortcut_original_memalloc;
    gi.Write = old_write;
    gi.ImageIndex = old_image;
    gi.FontIndex = old_font;
    T_ASSERT(!shortcut_fail_vital_alloc);
    T_EQ(client->shortcuts.vital_count, 0);
    T_ASSERT(client->shortcuts.vitals == NULL);
    T_ASSERT(shortcut_hero_parented);
    T_EQ(shortcut_hero_entity_count, 1);
    T_EQ(shortcut_hero_entities[0], hero->s.number);
    T_EQ(shortcut_vital_counts[0], 1);
    T_EQ(shortcut_vital_counts[1], 1);
    G_ClearUnitShortcutVitals(client);
}

TEST(wc3_shortcuts, vital_cache_is_discarded_and_rebuilt_after_save_load) {
    cstring_t const path = Test_TempPath("wc3-shortcut-vitals-save.bin");
    gameClient_t *client = &game.clients[0];
    edict_t *clent, *hero;
    UnitProfile_t profile = { .art = "TestUI\\Textures\\solid_white.blp" };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    int (*old_image)(cstring_t) = gi.ImageIndex;
    int (*old_font)(cstring_t, uint32_t) = gi.FontIndex;

    reset_entities(); setup_test_world();
    client->ps.number = 0;
    clent = g_edicts;
    clent->client = client;
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 64.0f, 64.0f);
    hero->svflags |= SVF_MONSTER;
    hero->s.player = 0;
    hero->data.UnitProfile = &profile;

    gi.Write = shortcut_test_write;
    gi.ImageIndex = shortcut_test_image;
    gi.FontIndex = shortcut_test_font;
    UI_WriteUnitShortcutLayer(clent);
    gi.Write = old_write;
    gi.ImageIndex = old_image;
    gi.FontIndex = old_font;
    T_NOT_NULL(client->shortcuts.vitals);
    T_EQ(client->shortcuts.vital_count, 1);

    T_ASSERT(WriteGame(path));
    T_ASSERT(ReadGame(path));
    client = &game.clients[0];
    T_ASSERT(client->shortcuts.vitals == NULL);
    T_EQ(client->shortcuts.vital_count, 0);
    T_ASSERT(client->shortcuts.dirty);
    remove(path);
}
#endif
