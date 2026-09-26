#include "renderer/r_game.h"
#include "games/starcraft-2/renderer/m3/r_m3.h"
#include "shared/test.h"
#include "games/starcraft-2/common/sc2_coords.h"

/* The ray must follow model rotation/scale and return world-space distance for nearest-hit sorting. */
TEST(sc2_control, m3_picking) {
    m3Model_t m3 = { .boundings = { .min = {-1, -0.5f, 0}, .max = {1, 0.5f, 2} } };
    model_t model = { .modeltype = ID_43DM, .m3 = &m3 };
    renderEntity_t ent = { .model = &model, .origin = {10, 20, 8}, .scale = 2, .angle = 0 };
    line3_t ray = {{10, 21.5f, 20}, {10, 21.5f, 0}};
    float dist;
    box3_t box;
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
        uint8_t buf[256];
        sizeBuf_t msg = { .data = buf, .maxsize = sizeof(buf) };
        uint32_t bits = 0;
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
        matrix4_t matrix;
        R_GetEntityMatrix(&ent, &matrix);
        vector3_t front = Matrix4_multiply_vector3(&matrix, &MAKE(vector3_t, 0, -1, 0));
        vector3_t up = Matrix4_multiply_vector3(&matrix, &MAKE(vector3_t, 0, 0, 1));
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
        float raw = i * 0.37f;
        renderEntity_t ent = { .model = &model, .origin = {3, 5, 7}, .scale = 1.5f,
            .angle = SC2_PlacementHeading(raw) };
        matrix4_t matrix, old;
        Matrix4_identity(&old); Matrix4_translate(&old, &ent.origin);
        Matrix4_rotate(&old, &MAKE(vector3_t, 0, 0, RAD2DEG(raw)), ROTATE_XYZ);
        Matrix4_scale(&old, &MAKE(vector3_t, 1.5f, 1.5f, 1.5f));
        R_GetEntityMatrix(&ent, &matrix);
        FOR_LOOP(k, 16) T_FEQ(matrix.v[k], old.v[k], 0.0001f);
        viewDef_t camera = {0};
        T_ASSERT(R_ExtractEntityCamera(&ent, 1.5f, &camera));
        vector3_t center = Matrix4_multiply_vector3(&matrix, &MAKE(vector3_t, -0.5f, 0, 1.5f));
        vector3_t ndc = Matrix4_multiply_vector3(&camera.viewProjectionMatrix, &center);
        T_FEQ(ndc.x, 0, 0.001f); T_FEQ(ndc.y, 0, 0.001f);
    }
}

/* Include the snapshot codec: a non-periodic angle grid introduced a 0.299-degree placement bias. */
TEST(sc2_control, snapshot_preserves_authored_placement) {
    float angles[] = { 0, M_PI / 2, M_PI, 3 * M_PI / 2, -M_PI / 2, 2 * M_PI,
        0.8427f, 0.7963f, -0.8427f }; /* TRaynor01's two bridge placements. */
    model_t model = { .modeltype = ID_43DM };
    FOR_LOOP(i, sizeof(angles) / sizeof(angles[0])) {
        entityState_t zero = {0}, states[2] = {0};
        FOR_LOOP(j, 2) {
            entityState_t source = { .number = 1, .model = 1, .scale = 1.5f, .origin = {3, 5, 7},
                .angle = j ? SC2_PlacementHeading(angles[i]) : angles[i] };
            uint8_t bytes[256]; sizeBuf_t msg = { .data = bytes, .maxsize = sizeof(bytes) };
            uint32_t bits;
            MSG_WriteDeltaEntity(&msg, &zero, &source, true);
            int number = MSG_ReadEntityBits(&msg, &bits);
            MSG_ReadDeltaEntity(&msg, &states[j], number, bits);
        }
        renderEntity_t ent = { .model = &model, .origin = states[1].origin,
            .scale = states[1].scale, .angle = states[1].angle };
        matrix4_t expected, actual;
        Matrix4_identity(&expected); Matrix4_translate(&expected, &states[0].origin);
        Matrix4_rotate(&expected, &MAKE(vector3_t, 0, 0, RAD2DEG(states[0].angle)), ROTATE_XYZ);
        Matrix4_scale(&expected, &MAKE(vector3_t, ent.scale, ent.scale, ent.scale));
        R_GetEntityMatrix(&ent, &actual);
        FOR_LOOP(k, 16) T_FEQ(actual.v[k], expected.v[k], 0.0001f);
    }
}

/* Use the live snapshot codec and client yaw interpolation before the common renderer boundary. */
extern float LerpRotation(float a, float b, float t);
TEST(sc2_control, snapshot_interpolation_preserves_facing) {
    model_t model = { .modeltype = ID_43DM };
    entityState_t zero = {0}, states[2] = {0};
    FOR_LOOP(i, 2) {
        entityState_t source = { .number = 1, .model = 1, .scale = 1,
            .angle = (float)DEG2RAD(i ? 10 : 350), .origin = { 3 + i, 5, 7 } };
        uint8_t bytes[512]; sizeBuf_t msg = { .data = bytes, .maxsize = sizeof(bytes) };
        uint32_t bits;
        MSG_WriteDeltaEntity(&msg, &zero, &source, true);
        int number = MSG_ReadEntityBits(&msg, &bits);
        MSG_ReadDeltaEntity(&msg, &states[i], number, bits);
    }
    renderEntity_t ent = { .model = &model, .scale = 1,
        .origin = Vector3_lerp(&states[0].origin, &states[1].origin, 0.5f),
        .angle = LerpRotation(states[0].angle, states[1].angle, 0.5f) };
    matrix4_t matrix; R_GetEntityMatrix(&ent, &matrix);
    vector3_t front = Matrix4_multiply_vector3(&matrix, &MAKE(vector3_t, 0, -1, 0));
    T_FEQ(front.x, 4.5f, 0.01f); T_FEQ(front.y, 5, 0.01f); T_FEQ(front.z, 7, 0.0001f);
}
