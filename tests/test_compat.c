#include "test.h"
#include "common/shared.h"

TEST(compat, strlcpy_reports_source_length_and_truncates) {
    char destination[4];

    T_EQ(bz_strlcpy(destination, "source", sizeof(destination)), 6);
    T_STREQ(destination, "sou");
}

TEST(compat, strlcpy_zero_size_does_not_write) {
    char destination[] = "keep";

    T_EQ(bz_strlcpy(destination, "source", 0), 6);
    T_STREQ(destination, "keep");
}

TEST(compat, strlcat_reports_combined_length_and_truncates) {
    char destination[6] = "ab";

    T_EQ(bz_strlcat(destination, "cdef", sizeof(destination)), 6);
    T_STREQ(destination, "abcde");
}

TEST(compat, strlcat_bounded_unterminated_destination_does_not_write) {
    char destination[] = {'a', 'b', 'c'};

    T_EQ(bz_strlcat(destination, "de", sizeof(destination)), 5);
    T_ASSERT(!memcmp(destination, "abc", sizeof(destination)));
}

TEST(orientation, named_angles_match_canonical_axes) {
    FOR_LOOP(i, 12) {
        orientation_t angles = { .yaw = i * 0.63f, .pitch = i * -0.31f, .roll = i * 0.47f };
        QUATERNION q = Quaternion_fromOrientation(&angles);
        MATRIX4 actual, expected;
        Matrix4_identity(&actual); Matrix4_rotateQuat(&actual, &q);
        Matrix4_identity(&expected);
        Matrix4_rotate(&expected, &MAKE(VECTOR3, RAD2DEG(angles.roll), -RAD2DEG(angles.pitch), RAD2DEG(angles.yaw)), ROTATE_XYZ);
        FOR_LOOP(k, 16) T_FEQ(actual.v[k], expected.v[k], 0.00001f);
    }
    QUATERNION up = Quaternion_fromOrientation(&MAKE(orientation_t, .pitch = M_PI / 2));
    MATRIX4 matrix;
    Matrix4_identity(&matrix); Matrix4_rotateQuat(&matrix, &up);
    VECTOR3 forward = Matrix4_multiply_vector3(&matrix, &MAKE(VECTOR3, 1, 0, 0));
    T_FEQ(forward.x, 0, 0.00001f); T_FEQ(forward.z, 1, 0.00001f);
}
