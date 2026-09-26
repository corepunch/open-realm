#include "shared/test.h"
#include "renderer/r_game.h"
#include "common/wow_coords.h"
#include "renderer/wow/r_wowmap.h"

/* Flying actors and projectiles must not need a height flag to use their heading. */
TEST(wow_coordinates, actor_heading_is_independent_of_ground_anchor) {
    model_t model = { .modeltype = ID_MD20 };
    FOR_LOOP(i, 4) FOR_LOOP(grounded, 2) {
        renderEntity_t ent = { .model = &model, .origin = {3, 5, 7}, .scale = 2,
            .angle = i * M_PI / 2, .flags = grounded ? RF_GROUND_ANCHOR : 0 };
        MATRIX4 matrix;
        R_GetEntityMatrix(&ent, &matrix);
        VECTOR3 front = Matrix4_multiply_vector3(&matrix, &MAKE(VECTOR3, 1, 0, 0));
        T_FEQ(front.x, 3 + 2 * cosf(ent.angle), 0.0001f);
        T_FEQ(front.y, 5 + 2 * sinf(ent.angle), 0.0001f);
        T_FEQ(front.z, 7, 0.0001f);
    }
}

/* Grass producers sample radians over 2*pi; rotation must keep native Z upright. */
TEST(wow_coordinates, grass_uses_radian_heading) {
    model_t model = { .modeltype = ID_MD20 };
    FOR_LOOP(i, 8) {
        renderEntity_t ent = { .model = &model, .origin = {2, 4, 6}, .scale = 1,
            .angle = i * M_PI / 4, .flags = RF_GROUND_EFFECT };
        MATRIX4 matrix;
        R_GetEntityMatrix(&ent, &matrix);
        VECTOR3 front = Matrix4_multiply_vector3(&matrix, &MAKE(VECTOR3, 1, 0, 0));
        VECTOR3 up = Matrix4_multiply_vector3(&matrix, &MAKE(VECTOR3, 0, 0, 1));
        T_FEQ(front.x, 2 + cosf(ent.angle), 0.0001f);
        T_FEQ(front.y, 4 + sinf(ent.angle), 0.0001f);
        T_FEQ(up.x, 2, 0.0001f); T_FEQ(up.y, 4, 0.0001f); T_FEQ(up.z, 7, 0.0001f);
    }
}
/* Preserve every MDDF axis, not merely yaw-only scenery. */
TEST(wow_coordinates, tilted_doodads_preserve_authored_transform) {
    model_t model = { .modeltype = ID_MD20 };
    FOR_LOOP(i, 12) {
        renderEntity_t ent = { .model = &model, .origin = {3, 5, 7}, .scale = 0.5f + i * 0.25f,
            .rotation = { i * 31, -(float)i * 17, i * 47 } };
        MATRIX4 old, basis = { .v = {0,1,0,0, 0,0,1,0, 1,0,0,0, 0,0,0,1} }, tmp, matrix;
        Matrix4_identity(&old); Matrix4_translate(&old, &ent.origin);
        Matrix4_multiply(&old, &basis, &tmp); old = tmp;
        Matrix4_rotate(&old, &MAKE(VECTOR3, 0, ent.rotation.y - 90, 0), ROTATE_XYZ);
        Matrix4_rotate(&old, &MAKE(VECTOR3, 0, 0, -ent.rotation.x), ROTATE_XYZ);
        Matrix4_rotate(&old, &MAKE(VECTOR3, ent.rotation.z - 90, 0, 0), ROTATE_XYZ);
        Matrix4_scale(&old, &MAKE(VECTOR3, ent.scale, ent.scale, ent.scale));
        R_GetEntityMatrix(&ent, &matrix);
        FOR_LOOP(k, 16) T_FEQ(matrix.v[k], old.v[k], 0.0001f);
        modelPose_t pose = { .origin = ent.origin, .scale = ent.scale };
        T_FEQ(R_EntityPose(&ent, &pose)->v[0], 1, 0.0001f);
        T_FEQ(pose.angles.pitch, -DEG2RAD(ent.rotation.x), 0.0001f);
        T_FEQ(pose.angles.roll, DEG2RAD(ent.rotation.z), 0.0001f);
    }
}

TEST(wow_coordinates, preview_attachment_facing_leaves_parent_camera_pose_fixed) {
    model_t model = { .modeltype = ID_MD20 };
    renderEntity_t ent = { .model = &model, .scale = 1, .attachment = { .model = &model, .angles.yaw = M_PI / 2 } };
    MATRIX4 parent, child;
    R_GetEntityMatrix(&ent, &parent);
    child = parent;
    Matrix4_translate(&child, &MAKE(VECTOR3, 2, 3, 4));
    R_GetAttachmentMatrix(&ent, &child, &child);
    VECTOR3 front = Matrix4_multiply_vector3(&child, &MAKE(VECTOR3, 1, 0, 0));
    T_FEQ(parent.v[0], 1, 0.0001f); T_FEQ(parent.v[1], 0, 0.0001f);
    T_FEQ(front.x, 2, 0.0001f); T_FEQ(front.y, 4, 0.0001f); T_FEQ(front.z, 4, 0.0001f);
}

TEST(wow_coordinates, wmo_placement_and_native_child_share_one_basis) {
    wowMapObjDef_t def = { .position = {17000, 42, 16900}, .rotation = {17, 31, 47}, .scale = 1536 };
    WOWPLACEMENT source = { .pos = {17000, 42, 16900}, .rot = {17, 31, 47}, .scale = 1536 };
    wowWmoDoodadDef_t child = { .position = {2, 3, 4}, .quat = {0, 0, 0.70710678f, 0.70710678f}, .scale = 0.5f };
    MATRIX4 render, collision, local, world;
    Wow_InstanceMatrix(&def, &render);
    Wow_PlacementMatrix(&source, &collision);
    FOR_LOOP(k, 16) T_FEQ(render.v[k], collision.v[k], 0.0001f);
    Wow_WmoDoodadLocalMatrix(&child, &local);
    Matrix4_multiply(&render, &local, &world);
    VECTOR3 native = Matrix4_multiply_vector3(&local, &MAKE(VECTOR3, 1, 0, 0));
    T_FEQ(native.x, 2, 0.0001f); T_FEQ(native.y, 3.5f, 0.0001f); T_FEQ(native.z, 4, 0.0001f);
    VECTOR3 expected = Matrix4_multiply_vector3(&collision, &native);
    VECTOR3 actual = Matrix4_multiply_vector3(&world, &MAKE(VECTOR3, 1, 0, 0));
    T_FEQ(actual.x, expected.x, 0.0001f); T_FEQ(actual.y, expected.y, 0.0001f); T_FEQ(actual.z, expected.z, 0.0001f);
}
