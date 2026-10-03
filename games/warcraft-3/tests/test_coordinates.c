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
