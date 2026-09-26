#ifdef BZ_TESTS
#include "../g_local.h"
#include "shared/test.h"
#include "common/stb_slk.h"

bool run_test_jass(cstring_t src);

static Doodads_t doodad_row = { .id = MAKEFOURCC('L', 'O', 'o', '2') };
static DestructableData_t not_destructable;

static edict_t *make_test_doodad(float x, float y) {
    edict_t *ent = G_Spawn();

    ent->class_id = doodad_row.id;
    ent->s.class_id = ent->class_id;
    ent->s.origin2 = (vec2_t){ x, y };
    ent->s.origin.x = x;
    ent->s.origin.y = y;
    ent->data.Doodads = &doodad_row;
    ent->data.DestructableData = &not_destructable;
    ent->svflags |= SVF_STATIC_SCENERY;
    return ent;
}

TEST(wc3_doodad, rect_special_hide_only_changes_matching_doodads) {
    box2_t rect = { .min = { 0, 0 }, .max = { 128, 128 } };
    edict_t *inside = make_test_doodad(64, 64);
    edict_t *outside = make_test_doodad(256, 64);

    T_EQ(G_SetDoodadAnimationRect(&rect, doodad_row.id, "hide", false), 1);
    T_ASSERT(inside->s.renderfx & RF_HIDDEN);
    T_ASSERT(!(outside->s.renderfx & RF_HIDDEN));

    T_EQ(G_SetDoodadAnimationRect(&rect, doodad_row.id, "show", false), 1);
    T_ASSERT(!(inside->s.renderfx & RF_HIDDEN));
}

TEST(wc3_doodad, radius_nearest_only_changes_one_doodad) {
    edict_t *near = make_test_doodad(32, 0);
    edict_t *far = make_test_doodad(96, 0);
    doodadAnimationRadiusParams_t const params = {
        .x = 0, .y = 0, .radius = 128, .doodad_id = doodad_row.id,
        .nearest_only = true, .anim_name = "hide", .random_animation = false
    };

    T_EQ(G_SetDoodadAnimationRadius(&params), 1);
    T_ASSERT(near->s.renderfx & RF_HIDDEN);
    T_ASSERT(!(far->s.renderfx & RF_HIDDEN));
}

TEST(wc3_doodad, jass_natives_route_radius_and_rect_to_map_doodads) {
    edict_t *radius_match = make_test_doodad(32, 32);
    edict_t *rect_match = make_test_doodad(192, 192);

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local rect r = Rect(128.0, 128.0, 256.0, 256.0)\n"
        "  call SetDoodadAnimation(0.0, 0.0, 64.0, 'LOo2', true, \"hide\", false)\n"
        "  call SetDoodadAnimationRect(r, 'LOo2', \"hide\", false)\n"
        "endfunction\n"));
    T_ASSERT(radius_match->s.renderfx & RF_HIDDEN);
    T_ASSERT(rect_match->s.renderfx & RF_HIDDEN);
}

TEST(wc3_doodad, nonlooping_animation_holds_authored_final_frame) {
    edict_t *ent = make_test_doodad(0, 0);
    animation_t death = { .name = "Death", .interval = { 1000, 1300 }, .flags = 1 };
    umove_t move = { "death", NULL, G_DoodadAnimationEnd };

    ent->animation = &death;
    ent->currentmove = &move;
    ent->s.frame = 1200;
    ent->aiflags &= ~AI_HOLD_FRAME;

    M_MoveFrame(ent);

    T_EQ(ent->s.frame, 1299);
    T_ASSERT(ent->aiflags & AI_HOLD_FRAME);
}

TEST(wc3_doodad, looping_animation_wraps_to_sequence_start) {
    edict_t *ent = make_test_doodad(0, 0);
    animation_t stand = { .name = "Stand", .interval = { 1000, 1300 }, .flags = 0 };
    umove_t move = { "stand", NULL, G_DoodadAnimationEnd };

    ent->animation = &stand;
    ent->currentmove = &move;
    ent->s.frame = 1200;
    ent->aiflags &= ~AI_HOLD_FRAME;

    M_MoveFrame(ent);

    T_EQ(ent->s.frame, 1000);
    T_ASSERT(!(ent->aiflags & AI_HOLD_FRAME));
}

TEST(wc3_doodad, spawn_enters_nonzero_stand_and_script_can_replace_it) {
    static char const *slk =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"ID\"\nC;Y1;X2;K\"file\"\nC;Y1;X3;K\"numVar\"\n"
        "C;Y2;X1;K\"ASv0\"\n"
        "C;Y2;X2;K\"Buildings\\Other\\ElvenFishVillageBuilding0\\ElvenFishVillageBuilding0\"\n"
        "C;Y2;X3;K1\n"
        "C;Y3;X1;K\"ASx2\"\n"
        "C;Y3;X2;K\"Buildings\\Other\\ElvenFishVillageBuildingRuined2\\ElvenFishVillageBuildingRuined2\"\n"
        "C;Y3;X3;K1\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *saved = G_SetSLKRows("Doodads", rows);
    box2_t area = { .min = { -1, -1 }, .max = { 1, 1 } };

    FOR_LOOP(index, 2) {
        edict_t *ent = G_Spawn();
        uint32_t first = index ? 61667 : 4167, last = index ? 66667 : 6667;
        ent->class_id = index ? MAKEFOURCC('A','S','x','2') : MAKEFOURCC('A','S','v','0');
        SP_CallSpawn(ent); /* same path as a war3map.doo placement */
        T_ASSERT(G_IsDoodad(ent));
        T_NOT_NULL(ent->animation);
        T_EQ(ent->s.frame, first);
        T_STREQ(ent->animation_request, "Stand");
        T_ASSERT(ent->think == monster_think);
        if (ent->animation && ent->think) {
            ent->think(ent);
            T_ASSERT(ent->s.frame > first && ent->s.frame < last);
            ent->s.frame = last - 1;
            ent->think(ent);
            T_EQ(ent->s.frame, first);
            T_EQ(G_SetDoodadAnimationRect(&area, ent->class_id, "portrait", false), 1);
            T_EQ(ent->s.frame, 67333);
            T_EQ(G_SetDoodadAnimationRect(&area, ent->class_id, "stand", false), 1);
            T_EQ(ent->s.frame, first);
        }
        G_FreeEdict(ent);
    }
    G_SetSLKRows("Doodads", saved); free_slk_rows(rows);
}
#endif
