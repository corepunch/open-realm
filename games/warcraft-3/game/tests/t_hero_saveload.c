#ifdef BZ_TESTS
/*
 * A walking Hero must restore origin, learned skills, runtime abilities, and
 * inventory through WriteGame/ReadGame. The live campaign sweep is a local
 * diagnostic; this is the CI contract that does not need retail maps.
 */
#include <string.h>
#include "shared/test.h"
#include "../g_local.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void setup_test_world(void);
void reset_entities(void);
void setup_test_pathmap(DWORD width, DWORD height, BYTE const *cells);
void CM_SetupTestWorldBounds(LPCBOX2 bounds);
void CM_ProcessPathJobs(DWORD work_budget);

static LPEDICT make_walk_hero(FLOAT x, FLOAT y) {
    LPEDICT hero = alloc_test_unit(MAKEFOURCC('H', 'p', 'a', 'l'), x, y);
    hero->s.player = 0;
    hero->svflags |= SVF_MONSTER;
    hero->movetype = MOVETYPE_STEP;
    hero->collision = 0.0f;
    hero->unitinfo.MoveSpeed = 270.0f;
    hero->stand = unit_stand;
    hero->birth = unit_birth;
    hero->die = unit_die;
    hero->think = monster_think;
    unit_stand(hero);
    gi.LinkEntity(hero);
    return hero;
}

static LPEDICT give_item(LPEDICT hero, DWORD class_id, DWORD slot, DWORD charges) {
    LPEDICT item = alloc_test_unit(class_id, hero->s.origin2.x + 32.0f, hero->s.origin2.y);
    item->targtype = TARG_ITEM;
    item->item.in_world = true;
    item->item.inventory_slot = -1;
    T_ASSERT(G_AddItemToSlot(hero, item, slot));
    G_SetItemCharges(item, charges);
    return item;
}

static void step_walk(LPEDICT hero, DWORD frames) {
    DWORD i;
    for (i = 0; i < frames; i++) {
        if (!hero->currentmove || !hero->currentmove->think) break;
        hero->currentmove->think(hero);
        level.time += FRAMETIME;
    }
}

static LPCSTR hero_audit_cvar(LPCSTR name, LPCSTR fallback) {
    return !strcmp(name, "wc3_hero_saveload_audit") ? "1" : fallback;
}

static void step_hero_audit(DWORD frames) {
    DWORD i;
    for (i = 0; i < frames; i++) {
        G_RunEntities();
        CM_ProcessPathJobs(65536);
        G_HeroSaveLoadAuditFrame();
        level.time += FRAMETIME;
    }
}

static void arm_hero_audit(LPCSTR map) {
    strlcpy(level.map_path, map, sizeof(level.map_path));
    level.started = true;
    ((LPMAPINFO)level.mapinfo)->fileFormat = 24;
    ((LPMAPINFO)level.mapinfo)->players[0].used = 1;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
}

TEST(wc3_save, walking_hero_round_trips_abilities_inventory_origin) {
    LPCSTR path = "/tmp/openwarcraft3-wc3-hero-saveload.bin";
    LPEDICT hero, item0, item1;
    VECTOR3 saved_origin;
    DWORD saved_holy, saved_shield, saved_added, saved_item0, saved_item1, saved_charges0, saved_charges1, index;
    char saved_move[32], snap[512];
    VECTOR2 dest = { 80.0f, 0.0f };

    reset_entities();
    setup_test_world();
    ((LPMAPINFO)level.mapinfo)->fileFormat = 24;
    hero = make_walk_hero(0.0f, 0.0f);
    T_ASSERT(G_UnitIsHero(hero));
    T_ASSERT(G_InventoryCapacity(hero) >= 2);
    unit_learnability(hero, MAKEFOURCC('A', 'H', 'h', 'b'));
    unit_learnability(hero, MAKEFOURCC('A', 'H', 'd', 's'));
    hero->abilities.added[0] = MAKEFOURCC('A', 'C', 'n', 'r');
    ARRAY_COUNT(hero->abilities.added) = 1;
    item0 = give_item(hero, MAKEFOURCC('s', 'p', 'r', 'o'), 0, 3);
    item1 = give_item(hero, MAKEFOURCC('r', 'a', 't', 'f'), 1, 1);
    T_ASSERT(unit_issueorder(hero, "move", &dest));
    T_NOT_NULL(hero->currentmove);
    T_STREQ(hero->currentmove->animation, "walk");
    /* MoveSpeed 270 → ~27 units/frame; two steps stay mid-walk on an 80-unit order. */
    step_walk(hero, 2);
    T_ASSERT(hero->s.origin.x > 1.0f);
    T_STREQ(hero->currentmove->animation, "walk");
    saved_origin = hero->s.origin;
    saved_holy = hero->heroabilities[0].code;
    saved_shield = hero->heroabilities[1].code;
    saved_added = hero->abilities.added[0];
    saved_item0 = item0->class_id;
    saved_item1 = item1->class_id;
    saved_charges0 = item0->item.charges;
    saved_charges1 = item1->item.charges;
    strlcpy(saved_move, hero->currentmove->animation, sizeof(saved_move));
    index = hero->s.number;
    G_FormatHeroSaveSnap(hero, snap, sizeof(snap));
    T_ASSERT(strstr(snap, "AHhb:1") != NULL);
    T_ASSERT(strstr(snap, "spro:3") != NULL);
    T_ASSERT(WriteGame(path));
    hero->s.origin = (VECTOR3){ 0 };
    hero->heroabilities[0].code = hero->heroabilities[1].code = 0;
    hero->heroabilities[0].level = hero->heroabilities[1].level = 0;
    ARRAY_COUNT(hero->abilities.added) = 0;
    hero->inventory[0] = hero->inventory[1] = NULL;
    T_ASSERT(ReadGame(path));
    hero = g_edicts + index;
    T_FEQ(hero->s.origin.x, saved_origin.x, 0.01f);
    T_FEQ(hero->s.origin.y, saved_origin.y, 0.01f);
    T_EQ(hero->heroabilities[0].code, saved_holy);
    T_EQ(hero->heroabilities[0].level, 1);
    T_EQ(hero->heroabilities[1].code, saved_shield);
    T_EQ(hero->heroabilities[1].level, 1);
    T_EQ(ARRAY_COUNT(hero->abilities.added), 1);
    T_EQ(hero->abilities.added[0], saved_added);
    T_NOT_NULL(hero->inventory[0]);
    T_NOT_NULL(hero->inventory[1]);
    T_EQ(hero->inventory[0]->class_id, saved_item0);
    T_EQ(hero->inventory[1]->class_id, saved_item1);
    T_EQ(hero->inventory[0]->item.charges, saved_charges0);
    T_EQ(hero->inventory[1]->item.charges, saved_charges1);
    T_NOT_NULL(hero->currentmove);
    T_STREQ(hero->currentmove->animation, saved_move);
    T_EQ(hero->think, monster_think);
    {
        FLOAT x = hero->s.origin.x;
        step_walk(hero, 1);
        T_ASSERT(hero->s.origin.x > x);
    }
    remove(path);
}

TEST(wc3_save, hero_dump_formats_empty_hero) {
    char snap[64];
    G_FormatHeroSaveSnap(NULL, snap, sizeof(snap));
    T_STREQ(snap, "unit=none");
}

/* Hidden cinematic stand-ins (HumanX06Finale N000) must not lock the walker. */
TEST(wc3_save, hero_audit_skips_hidden_first_hero) {
    LPCSTR (*old_cvar)(LPCSTR, LPCSTR) = gi.CvarString;
    LPEDICT hidden, visible;
    FLOAT hidden_x, visible_x;

    reset_entities();
    setup_test_world();
    hidden = make_walk_hero(0.0f, 0.0f);
    hidden->s.renderfx |= RF_HIDDEN;
    visible = make_walk_hero(200.0f, 0.0f);
    hidden_x = hidden->s.origin.x;
    visible_x = visible->s.origin.x;
    arm_hero_audit("Maps\\Campaign\\HeroAuditHidden.w3m");
    gi.CvarString = hero_audit_cvar;
    step_hero_audit(12);
    gi.CvarString = old_cvar;
    T_FEQ(hidden->s.origin.x, hidden_x, 0.01f);
    T_ASSERT(visible->s.origin.x > visible_x + 1.0f);
}

/* PauseAllUnitsBJ during intro; walk only after cleanup unpauses. */
TEST(wc3_save, hero_audit_waits_while_paused_then_walks) {
    LPCSTR (*old_cvar)(LPCSTR, LPCSTR) = gi.CvarString;
    LPEDICT hero;
    FLOAT start_x;

    reset_entities();
    setup_test_world();
    hero = make_walk_hero(0.0f, 0.0f);
    hero->paused = true;
    start_x = hero->s.origin.x;
    arm_hero_audit("Maps\\Campaign\\HeroAuditPaused.w3m");
    gi.CvarString = hero_audit_cvar;
    step_hero_audit(5);
    T_FEQ(hero->s.origin.x, start_x, 0.01f);
    hero->paused = false;
    step_hero_audit(12);
    gi.CvarString = old_cvar;
    T_ASSERT(hero->s.origin.x > start_x + 1.0f);
}

/* CinematicModeBJ(true) holds the walker until gameplay UI returns. */
TEST(wc3_save, hero_audit_waits_while_cinematic_then_walks) {
    LPCSTR (*old_cvar)(LPCSTR, LPCSTR) = gi.CvarString;
    LPEDICT hero;
    FLOAT start_x;

    reset_entities();
    setup_test_world();
    hero = make_walk_hero(0.0f, 0.0f);
    game.clients[0].ps.client_ui_state = CLIENT_UI_CINEMATIC;
    start_x = hero->s.origin.x;
    arm_hero_audit("Maps\\Campaign\\HeroAuditCinematic.w3m");
    gi.CvarString = hero_audit_cvar;
    step_hero_audit(5);
    T_FEQ(hero->s.origin.x, start_x, 0.01f);
    game.clients[0].ps.client_ui_state = CLIENT_UI_GAME;
    step_hero_audit(12);
    gi.CvarString = old_cvar;
    T_ASSERT(hero->s.origin.x > start_x + 1.0f);
}

/* HumanX06Finale stays cinematic; after the wait budget the visible Hero still walks. */
TEST(wc3_save, hero_audit_walks_after_wait_timeout_while_cinematic) {
    LPCSTR (*old_cvar)(LPCSTR, LPCSTR) = gi.CvarString;
    LPEDICT hero;
    FLOAT start_x;

    reset_entities();
    setup_test_world();
    hero = make_walk_hero(0.0f, 0.0f);
    game.clients[0].ps.client_ui_state = CLIENT_UI_CINEMATIC;
    start_x = hero->s.origin.x;
    arm_hero_audit("Maps\\Campaign\\HeroAuditFinale.w3m");
    gi.CvarString = hero_audit_cvar;
    step_hero_audit(5);
    T_FEQ(hero->s.origin.x, start_x, 0.01f);
    step_hero_audit(160);
    gi.CvarString = old_cvar;
    T_ASSERT(hero->s.origin.x > start_x + 1.0f);
}

/* +80 X snaps home on a blocked cell; the walker must retry another open axis. */
TEST(wc3_save, hero_audit_retries_when_80_unit_dest_snaps_home) {
    LPCSTR (*old_cvar)(LPCSTR, LPCSTR) = gi.CvarString;
    BYTE cells[64 * 64];
    LPEDICT hero;
    FLOAT start_x, start_y;

    reset_entities();
    setup_test_world();
    memset(cells, 0, sizeof(cells));
    FOR_LOOP(y, 64)
        FOR_LOOP(x, 64)
            if (x >= 33 && x <= 39) cells[y * 64 + x] = CM_PATHING_UNWALKABLE;
    setup_test_pathmap(64, 64, cells);
    CM_SetupTestWorldBounds(&MAKE(BOX2, .min = {-1024.0f, -1024.0f}, .max = {1024.0f, 1024.0f}));
    hero = make_walk_hero(16.0f, 16.0f);
    start_x = hero->s.origin.x;
    start_y = hero->s.origin.y;
    arm_hero_audit("Maps\\Campaign\\HeroAuditSnap.w3m");
    gi.CvarString = hero_audit_cvar;
    step_hero_audit(16);
    gi.CvarString = old_cvar;
    T_ASSERT(hero->s.origin.x < start_x - 1.0f || hero->s.origin.y > start_y + 1.0f || hero->s.origin.y < start_y - 1.0f);
}
#endif
