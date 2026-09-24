#include "renderer/r_game.h"
#include "shared/test.h"

TEST(wc3_coordinates, mandatory_identity_preserves_mdx_placement) {
    FOR_LOOP(i, 12) {
        model_t model = { .modeltype = ID_MDLX };
        renderEntity_t ent = { .model = &model, .origin = {3, 5, 7}, .scale = 0.5f + i * 0.25f, .angle = i * 0.47f };
        modelPose_t pose = { .origin = ent.origin, .angles.yaw = ent.angle, .scale = ent.scale };
        LPCMATRIX4 basis = R_EntityPose(&ent, &pose);
        MATRIX4 identity, expected, actual;
        Matrix4_identity(&identity);
        FOR_LOOP(k, 16) T_FEQ(basis->v[k], identity.v[k], 0.0001f);
        Matrix4_identity(&expected); Matrix4_translate(&expected, &ent.origin);
        Matrix4_rotate(&expected, &MAKE(VECTOR3, 0, 0, RAD2DEG(ent.angle)), ROTATE_XYZ);
        Matrix4_scale(&expected, &MAKE(VECTOR3, ent.scale, ent.scale, ent.scale));
        R_GetEntityMatrix(&ent, &actual);
        FOR_LOOP(k, 16) T_FEQ(actual.v[k], expected.v[k], 0.0001f);
    }
}
