#include "renderer/r_game.h"
#include "games/starcraft-2/renderer/m3/r_m3.h"
#include "shared/test.h"
#include "games/starcraft-2/common/sc2_coords.h"

/* The ray must follow model rotation/scale and return world-space distance for nearest-hit sorting. */
TEST(sc2_control, m3_picking) {
    m3Model_t m3 = { .boundings = { .min = {-1, -0.5f, 0}, .max = {1, 0.5f, 2} } };
    model_t model = { .modeltype = ID_43DM, .m3 = &m3 };
    renderEntity_t ent = { .model = &model, .origin = {10, 20, 8}, .scale = 2, .angle = 0 };
    LINE3 ray = {{10, 21.5f, 20}, {10, 21.5f, 0}};
    FLOAT dist;
    BOX3 box;
    T_ASSERT(R_GetEntityBounds(&ent, &box)); T_FEQ(box.max.z, 2, 0.001f);
    T_ASSERT(R_TraceModel(&ent, &ray, &dist)); T_FEQ(dist, 8, 0.001f);
    ray.a.x = ray.b.x = 11.5f;
    T_ASSERT(!R_TraceModel(&ent, &ray, &dist));
    ent.model = NULL;
    T_ASSERT(!R_GetEntityBounds(&ent, &box)); T_ASSERT(!R_TraceModel(&ent, &ray, &dist));
}

/* Exercise the SC2 executable's wire schema, including the radius consumed by its renderer. */
TEST(sc2_control, fractional_snapshot_geometry) {
    entityState_t from = {0}, to = { .number = 1, .model = 1, .origin = {35.275f, 23.625f, 8.125f},
        .radius = 0.375f, .renderfx = RF_SELECTED }, out = {0};
    FOR_LOOP(i, 2) {
        BYTE buf[256];
        sizeBuf_t msg = { .data = buf, .maxsize = sizeof(buf) };
        DWORD bits = 0;
        MSG_WriteDeltaEntity(&msg, &from, &to, true);
        int num = MSG_ReadEntityBits(&msg, &bits);
        MSG_ReadDeltaEntity(&msg, &out, num, bits);
        T_FEQ(out.origin.x, to.origin.x, 0.00001f);
        T_FEQ(out.origin.y, to.origin.y, 0.00001f);
        T_FEQ(out.origin.z, to.origin.z, 0.00001f);
        renderEntity_t ent = { .radius = out.radius };
        T_FEQ(R_SelectionRadius(&ent), 0.375f, 0.00001f);
        T_ASSERT(out.renderfx & RF_SELECTED);
        from = to; to.origin.x += 0.125f;
    }
}

/* M3's native -Y front must follow the snapshot's +X-based gameplay heading. */
TEST(sc2_control, model_front_follows_heading) {
    model_t model = { .modeltype = ID_43DM };
    FOR_LOOP(i, 4) {
        renderEntity_t ent = { .model = &model, .origin = {3, 5, 7}, .scale = 2, .angle = i * M_PI / 2 };
        MATRIX4 matrix;
        R_GetEntityMatrix(&ent, &matrix);
        VECTOR3 front = Matrix4_multiply_vector3(&matrix, &MAKE(VECTOR3, 0, -1, 0));
        VECTOR3 up = Matrix4_multiply_vector3(&matrix, &MAKE(VECTOR3, 0, 0, 1));
        T_FEQ(front.x, ent.origin.x + 2 * cosf(ent.angle), 0.0001f);
        T_FEQ(front.y, ent.origin.y + 2 * sinf(ent.angle), 0.0001f);
        T_FEQ(front.z, ent.origin.z, 0.0001f);
        T_FEQ(up.z, ent.origin.z + 2, 0.0001f);
    }
}

/* Native placed-object rotations, including asymmetric bridges, retain their old world geometry. */
TEST(sc2_control, authored_placement_and_camera_preserved) {
    m3Model_t m3 = { .boundings = { .min = {-2, -0.5f, 0}, .max = {1, 0.5f, 3}, .radius = 2 } };
    model_t model = { .modeltype = ID_43DM, .m3 = &m3 };
    FOR_LOOP(i, 12) {
        FLOAT raw = i * 0.37f;
        renderEntity_t ent = { .model = &model, .origin = {3, 5, 7}, .scale = 1.5f,
            .angle = SC2_PlacementHeading(raw) };
        MATRIX4 matrix, old;
        Matrix4_identity(&old); Matrix4_translate(&old, &ent.origin);
        Matrix4_rotate(&old, &MAKE(VECTOR3, 0, 0, RAD2DEG(raw)), ROTATE_XYZ);
        Matrix4_scale(&old, &MAKE(VECTOR3, 1.5f, 1.5f, 1.5f));
        R_GetEntityMatrix(&ent, &matrix);
        FOR_LOOP(k, 16) T_FEQ(matrix.v[k], old.v[k], 0.0001f);
        viewDef_t camera = {0};
        T_ASSERT(R_ExtractEntityCamera(&ent, 1.5f, &camera));
        VECTOR3 center = Matrix4_multiply_vector3(&matrix, &MAKE(VECTOR3, -0.5f, 0, 1.5f));
        VECTOR3 ndc = Matrix4_multiply_vector3(&camera.viewProjectionMatrix, &center);
        T_FEQ(ndc.x, 0, 0.001f); T_FEQ(ndc.y, 0, 0.001f);
    }
}

/* Use the live snapshot codec and client yaw interpolation before the common renderer boundary. */
extern FLOAT LerpRotation(FLOAT a, FLOAT b, FLOAT t);
TEST(sc2_control, snapshot_interpolation_preserves_facing) {
    model_t model = { .modeltype = ID_43DM };
    entityState_t zero = {0}, states[2] = {0};
    FOR_LOOP(i, 2) {
        entityState_t source = { .number = 1, .model = 1, .scale = 1,
            .angle = (FLOAT)DEG2RAD(i ? 10 : 350), .origin = { 3 + i, 5, 7 } };
        BYTE bytes[512]; sizeBuf_t msg = { .data = bytes, .maxsize = sizeof(bytes) };
        DWORD bits;
        MSG_WriteDeltaEntity(&msg, &zero, &source, true);
        int number = MSG_ReadEntityBits(&msg, &bits);
        MSG_ReadDeltaEntity(&msg, &states[i], number, bits);
    }
    renderEntity_t ent = { .model = &model, .scale = 1,
        .origin = Vector3_lerp(&states[0].origin, &states[1].origin, 0.5f),
        .angle = LerpRotation(states[0].angle, states[1].angle, 0.5f) };
    MATRIX4 matrix; R_GetEntityMatrix(&ent, &matrix);
    VECTOR3 front = Matrix4_multiply_vector3(&matrix, &MAKE(VECTOR3, 0, -1, 0));
    T_FEQ(front.x, 4.5f, 0.01f); T_FEQ(front.y, 5, 0.01f); T_FEQ(front.z, 7, 0.0001f);
}
