#include "renderer/r_game.h"
#include "shared/test.h"
#include "common/cmodel.h"

TEST(wc3_coordinates, transport_fixture_has_real_two_player_map_metadata) {
    mapInfo_t info;
    T_ASSERT(CM_ReadMapInfo("Maps/Transport.w3m", &info));
    T_STREQ(info.mapName, "Two-player transport fixture");
    T_EQ(info.fileFormat, 18);
    T_ASSERT(info.players[0].used); T_ASSERT(info.players[1].used);
    T_EQ(info.players[0].playerType, kPlayerTypeHuman); T_EQ(info.players[1].playerType, kPlayerTypeHuman);
    T_FEQ(info.players[0].startingPosition.x, -256, 0.01f);
    T_FEQ(info.players[1].startingPosition.x, 256, 0.01f);
    T_EQ(info.num_teams, 1); T_EQ(info.teams[0].playerMasks, 3);
    CM_FreeMapInfo(&info);
}

TEST(wc3_coordinates, mandatory_identity_preserves_mdx_placement) {
    FOR_LOOP(i, 12) {
        model_t model = { .modeltype = ID_MDLX };
        renderEntity_t ent = { .model = &model, .origin = {3, 5, 7}, .scale = 0.5f + i * 0.25f, .angle = i * 0.47f };
        modelPose_t pose = { .origin = ent.origin, .angles.yaw = ent.angle, .scale = ent.scale };
        mat4_t const *basis = R_EntityPose(&ent, &pose);
        mat4_t identity, expected, actual;
        Matrix4_identity(&identity);
        FOR_LOOP(k, 16) T_FEQ(basis->v[k], identity.v[k], 0.0001f);
        Matrix4_identity(&expected); Matrix4_translate(&expected, &ent.origin);
        Matrix4_rotate(&expected, &MAKE(vec3_t, 0, 0, RAD2DEG(ent.angle)), ROTATE_XYZ);
        Matrix4_scale(&expected, &MAKE(vec3_t, ent.scale, ent.scale, ent.scale));
        R_GetEntityMatrix(&ent, &actual);
        FOR_LOOP(k, 16) T_FEQ(actual.v[k], expected.v[k], 0.0001f);
    }
}

TEST(wc3_coordinates, game_authored_pose_offset_reaches_model_matrix) {
    model_t model = {.modeltype = ID_MDLX};
    renderEntity_t ent = {.model = &model, .origin = {3, 5, 7}, .scale = 1,
        .angle = 0.7f, .pose_offset = {.yaw = -0.02f, .pitch = 0.05f, .roll = -0.08f}};
    mat4_t actual, expected;
    orientation_t angles = {.yaw = 0.68f, .pitch = 0.05f, .roll = -0.08f};
    quaternion_t q = Quaternion_fromOrientation(&angles);
    Matrix4_from_rotation_translation_scale_origin(&expected, &q, &ent.origin,
        &(vec3_t){1, 1, 1}, &(vec3_t){0, 0, 0});
    R_GetEntityMatrix(&ent, &actual);
    FOR_LOOP(i, 16) T_FEQ(actual.v[i], expected.v[i], 0.000001f);
    T_FEQ(ent.angle, 0.7f, 0); /* Presentation never writes gameplay facing. */
}

#include "games/warcraft-3/common/terrain.h"
#include "games/warcraft-3/game/tests/fixtures/retail_slope272.h"
TEST(wc3_coordinates, ground_interpolation_matches_unchanged_retail_kernel) {
    FOR_LOOP(i,sizeof(slope272_ground)/sizeof(*slope272_ground)) {
        float point[2],heights[4],actual;uint32_t bits;
        memcpy(point,slope272_ground[i].point,sizeof(point));
        memcpy(heights,slope272_ground[i].heights,sizeof(heights));
        actual=W3_InterpolateGroundHeight(point[0]/128,point[1]/128,heights);
        memcpy(&bits,&actual,sizeof(bits));T_EQ(bits,slope272_ground[i].result);
    }
}
