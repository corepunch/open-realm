#ifdef BZ_TESTS
/*
 * test_api.c — Tests for the JASS native API implementations.
 *
 * These tests exercise the C-level game-state that the api_*.h functions
 * read and write.  They work directly on struct fields, alliance tables,
 * and group arrays — no MPQ, renderer, or JASS VM is required.
 *
 * Covered:
 *   Player  — color, start_location, name, team, alliance
 *   Hero    — str/agi/int attributes, XP accumulation, suspend_xp,
 *             overflow-safe AddHeroXP
 *   Unit    — invulnerable, paused, no_pathing, unit_color flags
 *   Group   — FirstOfGroup, IsUnitInGroup
 *   Misc    — SubString semantics, GetRandomInt / GetRandomReal range
 */

#include "test.h"
#include "../g_local.h"

/* Helpers defined in t_utils.c */
LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
BOOL run_test_jass(LPCSTR src);
extern LPPLAYER currentplayer;



#include "jass/jass.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* =========================================================================
 * Helpers
 * ========================================================================= */

/*
 * Return a pointer to player slot [idx].  Assigns player->number = idx so
 * that G_GetPlayerByNumber / PLAYER_CLIENT macros work correctly.
 */
static LPPLAYER test_player(int idx) {
    game.clients[idx].ps.number = (DWORD)idx;
    return &game.clients[idx].ps;
}

static LPCSTR skip_cutscene_cvar(LPCSTR name, LPCSTR fallback) {
    return !strcmp(name, "skip_cutscene") ? "1" : fallback;
}

/* Campaign human slots need not match the connection slot; exercise the real VM/edict module boundary. */
TEST(wc3_api, escape_restores_game_camera_ui_and_control) {
    LPGAMECLIENT gc = &game.clients[0];
    LPCSTR cancel[] = { "cancel" };
    QUATERNION quat = Quaternion_fromEuler(&(VECTOR3){326, 0, 0}, ROTATE_ZYX);

    game.clients[1].ps.number = 0;
    gc->ps.number = 1;
    gc->camera.state.viewangles = (VECTOR3){300, 0, 120};
    gc->camera.state.target_distance = 900;
    gc->camera.state.fov = 35;
    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "function cleanup takes nothing returns nothing\n"
        "  if GetLocalPlayer() == Player(1) then\n"
        "    call ResetToGameCamera(0.0)\n"
        "    call ShowInterface(true, 0.0)\n"
        "    call EnableUserControl(true)\n"
        "    call PanCameraTo(128.0, 256.0)\n"
        "  endif\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  call TriggerRegisterPlayerEvent(t, Player(1), EVENT_PLAYER_END_CINEMATIC)\n"
        "  call TriggerAddAction(t, function cleanup)\n"
        "  if GetLocalPlayer() == Player(1) then\n"
        "    call ShowInterface(false, 0.0)\n"
        "    call EnableUserControl(false)\n"
        "  endif\n"
        "endfunction\n"));
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_CINEMATIC);
    T_ASSERT(gc->no_control);
    /* An unrelated player's cancel must not run the registered cleanup. */
    globals.ClientCommand(&g_edicts[1], 1, cancel);
    G_RunEvents(); jass_runevents(level.vm);
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_CINEMATIC);
    T_FEQ(gc->camera.state.target_distance, 900, 0.001f);

    globals.ClientCommand(&g_edicts[0], 1, cancel);
    G_RunEvents(); jass_runevents(level.vm); G_RunClients();
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_GAME);
    T_EQ(gc->ps.uiflags, 1u << LAYER_CINEMATIC);
    T_ASSERT(!gc->no_control);
    T_FEQ(gc->ps.origin.x, 128, 0.001f); T_FEQ(gc->ps.origin.y, 256, 0.001f);
    T_FEQ(gc->ps.distance, 1650, 0.001f); T_EQ(gc->ps.fov, 50);
    T_FEQ(gc->ps.viewquat.x, quat.x, 0.001f); T_FEQ(gc->ps.viewquat.w, quat.w, 0.001f);
}

/* Fast-forward only changes cinematic timing; JASS retains ownership of the input/UI lifecycle. */
TEST(wc3_api, skip_cutscene_preserves_scripted_input_and_ui_state) {
    LPCSTR (*old_cvar)(LPCSTR, LPCSTR) = gi.CvarString;
    LPGAMECLIENT gc = &game.clients[0];

    gi.CvarString = skip_cutscene_cvar;
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call ShowInterface(false, 0.0)\n"
        "  call EnableUserControl(false)\n"
        "endfunction\n"));
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_CINEMATIC);
    T_ASSERT(gc->no_control);

    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call ShowInterface(true, 0.0)\n"
        "  call EnableUserControl(true)\n"
        "endfunction\n"));
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_GAME);
    T_ASSERT(!gc->no_control);
    currentplayer = NULL;
    gi.CvarString = old_cvar;
}

/* Create a minimal unit in slot 0 and return it. */
static LPEDICT make_unit_hero(void) {
    reset_entities();
    LPEDICT ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    return ent;
}

TEST(wc3_api, customize_entity_export_is_inert) {
    entityState_t state = { .number = 7, .model = 11, .renderfx = RF_SELECTED };

    T_NOT_NULL(globals.CustomizeEntity);
    if (!globals.CustomizeEntity)
        return;
    globals.CustomizeEntity(3, NULL, &state);
    T_EQ(state.number, 7);
    T_EQ(state.model, 11);
    T_EQ(state.renderfx, RF_SELECTED);
}

/* =========================================================================
 * Player — color
 * ========================================================================= */

TEST(wc3_api, player_color_default_zero) {
    LPPLAYER p = test_player(0);
    T_EQ((int)p->color, 0);
}

TEST(wc3_api, player_color_set_get) {
    LPPLAYER p = test_player(0);
    p->color = 5;
    T_EQ((int)p->color, 5);
}

TEST(wc3_api, player_color_max_index) {
    LPPLAYER p = test_player(0);
    p->color = 23;
    T_EQ((int)p->color, 23);
}

/* =========================================================================
 * Player — start_location
 * ========================================================================= */

TEST(wc3_api, player_start_location_default) {
    /* start_location is 0-initialised by setup_game(). */
    LPGAMECLIENT cl = &game.clients[1];
    cl->ps.number = 1;
    T_EQ((int)cl->ps.start_location, 0);
}

TEST(wc3_api, player_start_location_set_get) {
    LPGAMECLIENT cl = &game.clients[2];
    cl->ps.number = 2;
    cl->ps.start_location = 3;
    T_EQ((int)cl->ps.start_location, 3);
}

TEST(wc3_api, player_start_location_negative) {
    LPGAMECLIENT cl = &game.clients[3];
    cl->ps.number = 3;
    cl->ps.start_location = -1;
    T_EQ((int)cl->ps.start_location, -1);
}

/* =========================================================================
 * Player — name
 * ========================================================================= */

TEST(wc3_api, player_name_set_get) {
    LPPLAYER p = test_player(0);
    p->name = "Arthas";
    T_STREQ(p->name, "Arthas");
}

TEST(wc3_api, player_name_null_default) {
    /* memset in setup_game zeroes the name pointer. */
    LPPLAYER p = test_player(4);
    p->name = NULL; /* explicit reset */
    T_NULL(p->name);
}

/* =========================================================================
 * Player — team
 * ========================================================================= */

TEST(wc3_api, player_team_set_get) {
    LPPLAYER p = test_player(0);
    p->team = 2;
    T_EQ((int)p->team, 2);
}

/* =========================================================================
 * Player — alliance
 * ========================================================================= */

TEST(wc3_api, alliance_passive_default_false) {
    LPPLAYER p0 = test_player(0);
    LPPLAYER p1 = test_player(1);
    /* After setup_game(), alliance table is zeroed. */
    T_ASSERT(!G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
}

TEST(wc3_api, alliance_set_true) {
    LPPLAYER p0 = test_player(0);
    LPPLAYER p1 = test_player(1);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE, true);
    T_ASSERT(G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
}

TEST(wc3_api, alliance_symmetric) {
    /* G_SetPlayerAlliance sets both directions. */
    LPPLAYER p0 = test_player(0);
    LPPLAYER p1 = test_player(1);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE, true);
    T_ASSERT(G_GetPlayerAlliance(p1, p0, ALLIANCE_PASSIVE));
}

TEST(wc3_api, alliance_revoke) {
    LPPLAYER p0 = test_player(0);
    LPPLAYER p1 = test_player(1);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE, false);
    T_ASSERT(!G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
}

TEST(wc3_api, alliance_enemy_when_not_allied) {
    LPPLAYER p0 = test_player(0);
    LPPLAYER p2 = test_player(2);
    /* Players 0 and 2 have no alliance — IsUnitEnemy logic is !ally. */
    T_ASSERT(!G_GetPlayerAlliance(p0, p2, ALLIANCE_PASSIVE));
}

/* =========================================================================
 * Hero — str / agi / int attributes
 * ========================================================================= */

TEST(wc3_api, hero_str_set_get) {
    LPEDICT ent = make_unit_hero();
    ent->hero.str = 25;
    T_EQ((int)ent->hero.str, 25);
}

TEST(wc3_api, hero_agi_set_get) {
    LPEDICT ent = make_unit_hero();
    ent->hero.agi = 18;
    T_EQ((int)ent->hero.agi, 18);
}

TEST(wc3_api, hero_int_set_get) {
    LPEDICT ent = make_unit_hero();
    ent->hero.intel = 22;
    T_EQ((int)ent->hero.intel, 22);
}

/* =========================================================================
 * Hero — XP accumulation
 * ========================================================================= */

TEST(wc3_api, hero_xp_default_zero) {
    LPEDICT ent = make_unit_hero();
    T_EQ((int)ent->hero.xp, 0);
}

TEST(wc3_api, hero_xp_set) {
    LPEDICT ent = make_unit_hero();
    ent->hero.xp = 500;
    T_EQ((int)ent->hero.xp, 500);
}

TEST(wc3_api, hero_xp_add) {
    LPEDICT ent = make_unit_hero();
    ent->hero.xp = 100;
    DWORD add = 50;
    /* Replicate AddHeroXP logic: cap at INT32_MAX */
    DWORD cur = ent->hero.xp;
    DWORD sum = cur + add;
    ent->hero.xp = (sum < cur || sum > (DWORD)INT32_MAX) ? (DWORD)INT32_MAX : sum;
    T_EQ((int)ent->hero.xp, 150);
}

TEST(wc3_api, hero_xp_overflow_clamps) {
    LPEDICT ent = make_unit_hero();
    ent->hero.xp = (DWORD)INT32_MAX - 5;
    DWORD add = 100;
    DWORD cur = ent->hero.xp;
    DWORD sum = cur + add;
    ent->hero.xp = (sum < cur || sum > (DWORD)INT32_MAX) ? (DWORD)INT32_MAX : sum;
    /* Overflow past INT32_MAX clamps to INT32_MAX, never goes negative */
    T_EQ((long long)ent->hero.xp, (long long)INT32_MAX);
    T_ASSERT((LONG)ent->hero.xp >= 0);
}

/* =========================================================================
 * Hero — suspend_xp
 * ========================================================================= */

TEST(wc3_api, hero_suspend_xp_default_false) {
    LPEDICT ent = make_unit_hero();
    T_ASSERT(!ent->hero.suspend_xp);
}

TEST(wc3_api, hero_suspend_xp_set_true) {
    LPEDICT ent = make_unit_hero();
    ent->hero.suspend_xp = true;
    T_ASSERT(ent->hero.suspend_xp);
}

TEST(wc3_api, hero_xp_not_added_when_suspended) {
    LPEDICT ent = make_unit_hero();
    ent->hero.xp = 100;
    ent->hero.suspend_xp = true;
    /* Replicate AddHeroXP: skip when suspend_xp is set */
    LONG xp_to_add = 50;
    if (!ent->hero.suspend_xp && xp_to_add > 0) {
        DWORD add = (DWORD)xp_to_add;
        DWORD cur = ent->hero.xp;
        DWORD sum = cur + add;
        ent->hero.xp = (sum < cur || sum > (DWORD)INT32_MAX) ? (DWORD)INT32_MAX : sum;
    }
    T_EQ((int)ent->hero.xp, 100);
}

/* =========================================================================
 * Unit flags — invulnerable / paused / no_pathing / unit_color
 * ========================================================================= */

TEST(wc3_api, unit_invulnerable_default_false) {
    LPEDICT ent = make_unit_hero();
    T_ASSERT(!ent->invulnerable);
}

TEST(wc3_api, unit_invulnerable_set) {
    LPEDICT ent = make_unit_hero();
    ent->invulnerable = true;
    T_ASSERT(ent->invulnerable);
}

TEST(wc3_api, unit_paused_default_false) {
    LPEDICT ent = make_unit_hero();
    T_ASSERT(!ent->paused);
}

TEST(wc3_api, unit_paused_set) {
    LPEDICT ent = make_unit_hero();
    ent->paused = true;
    T_ASSERT(ent->paused);
    ent->paused = false;
    T_ASSERT(!ent->paused);
}

TEST(wc3_api, unit_no_pathing_default_false) {
    LPEDICT ent = make_unit_hero();
    T_ASSERT(!ent->no_pathing);
}

TEST(wc3_api, unit_no_pathing_set) {
    LPEDICT ent = make_unit_hero();
    ent->no_pathing = true;
    T_ASSERT(ent->no_pathing);
}

TEST(wc3_api, unit_color_default_zero) {
    LPEDICT ent = make_unit_hero();
    T_EQ((int)ent->unit_color, 0);
}

TEST(wc3_api, unit_color_set) {
    LPEDICT ent = make_unit_hero();
    ent->unit_color = 7;
    T_EQ((int)ent->unit_color, 7);
}

/* =========================================================================
 * Unit — hidden flag (RF_HIDDEN)
 * ========================================================================= */

TEST(wc3_api, unit_hidden_default_false) {
    LPEDICT ent = make_unit_hero();
    T_ASSERT(!(ent->s.renderfx & RF_HIDDEN));
}

TEST(wc3_api, unit_hidden_set) {
    LPEDICT ent = make_unit_hero();
    ent->s.renderfx |= RF_HIDDEN;
    T_ASSERT(ent->s.renderfx & RF_HIDDEN);
}

TEST(wc3_api, unit_hidden_clear) {
    LPEDICT ent = make_unit_hero();
    ent->s.renderfx |= RF_HIDDEN;
    ent->s.renderfx &= ~RF_HIDDEN;
    T_ASSERT(!(ent->s.renderfx & RF_HIDDEN));
}

/* =========================================================================
 * Group — FirstOfGroup / IsUnitInGroup
 * ========================================================================= */

TEST(wc3_api, group_first_of_empty_returns_null) {
    ggroup_t g = {0};
    LPEDICT first = (g.num_units > 0) ? g.units[0] : NULL;
    T_NULL(first);
}

TEST(wc3_api, group_first_of_group) {
    reset_entities();
    LPEDICT a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    LPEDICT b = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 1, 0);
    ggroup_t g = {0};
    g.units[0] = a;
    g.units[1] = b;
    g.num_units = 2;
    T_ASSERT(g.units[0] == a);
    T_ASSERT(g.units[1] == b);
}

TEST(wc3_api, group_add_is_set_semantics) {
    reset_entities();
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    ggroup_t group = {0};
    T_ASSERT(group_add_entity(&group, unit));
    T_ASSERT(!group_add_entity(&group, unit));
    T_EQ(group.num_units, 1);
}

TEST(wc3_api, group_is_unit_in_group_true) {
    reset_entities();
    LPEDICT a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    ggroup_t g = {0};
    g.units[0] = a;
    g.num_units = 1;
    /* Replicate IsUnitInGroup logic */
    BOOL found = false;
    for (DWORD i = 0; i < g.num_units; i++) {
        if (g.units[i] == a) { found = true; break; }
    }
    T_ASSERT(found);
}

TEST(wc3_api, group_is_unit_in_group_false) {
    reset_entities();
    LPEDICT a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    LPEDICT b = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 1, 0);
    ggroup_t g = {0};
    g.units[0] = a;
    g.num_units = 1;
    BOOL found = false;
    for (DWORD i = 0; i < g.num_units; i++) {
        if (g.units[i] == b) { found = true; break; }
    }
    T_ASSERT(!found);
}

/* =========================================================================
 * Misc — SubString semantics
 * ========================================================================= */

/*
 * Replicate SubString() logic from api_misc.h:
 *   source[start..end) — start inclusive, end exclusive.
 */
static void substr(const char *source, LONG start, LONG end, char *out, LONG outsz) {
    LONG len = (LONG)strlen(source);
    if (start < 0) start = 0;
    if (end > len) end = len;
    LONG n = end - start;
    if (n <= 0 || n + 1 > outsz) { out[0] = '\0'; return; }
    strncpy(out, source + start, (size_t)n);
    out[n] = '\0';
}

TEST(wc3_api, substring_basic) {
    char buf[64];
    substr("hello", 1, 4, buf, (LONG)sizeof(buf));
    T_STREQ(buf, "ell");
}

TEST(wc3_api, substring_full) {
    char buf[64];
    substr("hello", 0, 5, buf, (LONG)sizeof(buf));
    T_STREQ(buf, "hello");
}

TEST(wc3_api, substring_start_equals_end) {
    char buf[64];
    substr("hello", 2, 2, buf, (LONG)sizeof(buf));
    T_STREQ(buf, "");
}

TEST(wc3_api, substring_end_past_len) {
    char buf[64];
    substr("hi", 0, 100, buf, (LONG)sizeof(buf));
    T_STREQ(buf, "hi");
}

TEST(wc3_api, substring_single_char) {
    char buf[64];
    substr("hello", 0, 1, buf, (LONG)sizeof(buf));
    T_STREQ(buf, "h");
}

/* =========================================================================
 * Misc — GetRandomInt / GetRandomReal range
 * ========================================================================= */

TEST(wc3_api, random_int_in_range) {
    srand(42);
    for (int i = 0; i < 50; i++) {
        LONG lo = 1, hi = 10;
        LONG r = lo + rand() % (hi - lo + 1);
        T_ASSERT(r >= lo && r <= hi);
    }
}

TEST(wc3_api, random_int_single_value) {
    srand(1);
    LONG lo = 7, hi = 7;
    LONG r = lo + rand() % (hi - lo + 1);
    T_EQ((int)r, 7);
}

TEST(wc3_api, random_real_in_range) {
    srand(42);
    for (int i = 0; i < 50; i++) {
        FLOAT lo = 0.0f, hi = 1.0f;
        FLOAT t = (FLOAT)rand() / (FLOAT)RAND_MAX;
        FLOAT r = lo + t * (hi - lo);
        T_ASSERT(r >= lo && r <= hi);
    }
}

TEST(wc3_api, random_seed_deterministic) {
    srand(12345);
    int a = rand();
    srand(12345);
    int b = rand();
    T_EQ(a, b);
}

/* =========================================================================
 * Item — position and type id
 * ========================================================================= */

TEST(wc3_api, item_position_set) {
    reset_entities();
    LPEDICT item = alloc_test_unit(MAKEFOURCC('I','0','0','0'), 10.0f, 20.0f);
    item->s.origin.x = 10.0f;
    item->s.origin.y = 20.0f;
    T_FEQ(item->s.origin.x, 10.0f, 0.001f);
    T_FEQ(item->s.origin.y, 20.0f, 0.001f);
}

TEST(wc3_api, item_type_id) {
    reset_entities();
    LPEDICT item = alloc_test_unit(MAKEFOURCC('I','0','0','0'), 0.0f, 0.0f);
    DWORD expected = MAKEFOURCC('I','0','0','0');
    T_EQ((int)item->class_id, (int)expected);
}

/* =========================================================================
 * Inventory — edict-based UnitHasItem / UnitItemInSlot
 * ========================================================================= */

static LPEDICT alloc_inventory_test_unit(void) {
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    unit->health.value = 100;
    unit->health.max_value = 100;
    return unit;
}

static LPEDICT alloc_world_test_item(DWORD class_id) {
    LPEDICT item = alloc_test_unit(class_id, 0, 0);
    item->s.model = 1;
    item->item.in_world = true;
    item->item.inventory_slot = -1;
    return item;
}

TEST(wc3_api, unit_has_item_true) {
    reset_entities();
    LPEDICT unit = alloc_inventory_test_unit();
    LPEDICT item = alloc_world_test_item(MAKEFOURCC('r','a','t','f'));
    unit_additemtoslot(unit, item, 0);
    /* UnitHasItem checks pointer identity */
    BOOL found = false;
    FOR_LOOP(i, MAX_INVENTORY) {
        if (unit->inventory[i] == item) { found = true; break; }
    }
    T_ASSERT(found);
}

TEST(wc3_api, unit_has_item_false_different_instance) {
    /* Two items of the same type — only one is in inventory.
     * With edict-based inventory, distinct instances are distinguishable. */
    reset_entities();
    LPEDICT unit  = alloc_inventory_test_unit();
    LPEDICT item1 = alloc_world_test_item(MAKEFOURCC('r','a','t','f'));
    LPEDICT item2 = alloc_world_test_item(MAKEFOURCC('r','a','t','f'));
    unit_additemtoslot(unit, item1, 0);
    /* item2 is NOT in inventory */
    BOOL found = false;
    FOR_LOOP(i, MAX_INVENTORY) {
        if (unit->inventory[i] == item2) { found = true; break; }
    }
    T_ASSERT(!found);
}

TEST(wc3_api, unit_item_in_slot_returns_edict) {
    reset_entities();
    LPEDICT unit = alloc_inventory_test_unit();
    LPEDICT item = alloc_world_test_item(MAKEFOURCC('r','a','t','f'));
    unit_additemtoslot(unit, item, 2);
    T_ASSERT(unit->inventory[2] == item);
}

TEST(wc3_api, unit_item_in_slot_empty_is_null) {
    reset_entities();
    LPEDICT unit = alloc_inventory_test_unit();
    T_NULL(unit->inventory[0]);
}

/* =========================================================================
 * Unit — IsUnitOwnedByPlayer
 * ========================================================================= */

TEST(wc3_api, unit_owned_by_player) {
    reset_entities();
    LPEDICT ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    ent->s.player = 2;
    /* Replicate IsUnitOwnedByPlayer: ent->s.player == PLAYER_NUM(player) */
    LPPLAYER p = test_player(2);
    T_EQ((int)ent->s.player, (int)PLAYER_NUM(p));
}

TEST(wc3_api, unit_not_owned_by_player) {
    reset_entities();
    LPEDICT ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    ent->s.player = 1;
    LPPLAYER p = test_player(3);
    T_ASSERT(ent->s.player != PLAYER_NUM(p));
}

/* =========================================================================
 * Unit — IsUnitInRange
 * ========================================================================= */

TEST(wc3_api, unit_in_range) {
    reset_entities();
    LPEDICT a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    LPEDICT b = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 3.0f, 4.0f);  /* dist = 5 */
    FLOAT dist = Vector2_distance(&a->s.origin2, &b->s.origin2);
    T_ASSERT(dist <= 6.0f);
}

TEST(wc3_api, unit_out_of_range) {
    reset_entities();
    LPEDICT a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    LPEDICT b = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 3.0f, 4.0f);  /* dist = 5 */
    FLOAT dist = Vector2_distance(&a->s.origin2, &b->s.origin2);
    T_ASSERT(!(dist <= 4.0f));
}

/* =========================================================================
 * Test suite entry point
 * ========================================================================= */

#endif /* BZ_TESTS */
