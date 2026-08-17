#ifdef BZ_TESTS
#include "test.h"
#include "common/shared.h"

TEST(wow_appearance, pack_unpack_boundaries) {
    DWORD packed = Wow_PackAppearance(31, 14, 29, 14, 31, 11, 27);
    wowAppearance_t out = Wow_UnpackAppearance(packed);
    T_EQ(out.skinColorID, 31);
    T_EQ(out.faceID, 14);
    T_EQ(out.hairStyleID, 29);
    T_EQ(out.hairColorID, 14);
    T_EQ(out.facialHairStyleID, 31);
    T_EQ(out.classID, 11);
    T_EQ(out.flags, 27);
}

TEST(wow_appearance, pack_masks_inputs) {
    DWORD packed = Wow_PackAppearance(0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9);
    wowAppearance_t out = Wow_UnpackAppearance(packed);
    T_EQ(out.skinColorID, 0x1f);
    T_EQ(out.faceID, 0x0e);
    T_EQ(out.hairStyleID, 0x1d);
    T_EQ(out.hairColorID, 0x0c);
    T_EQ(out.facialHairStyleID, 0x1b);
    T_EQ(out.classID, 0x0a);
    T_EQ(out.flags, 0x19);
}

TEST(wow_appearance, facial_feature_fifth_bit_preserves_classic_values) {
    DWORD legacy = 7 | (6u << 5) | (5u << 10) | (4u << 15) | (13u << 19) | (1u << 23) | (2u << 27);
    wowAppearance_t out;

    T_EQ(Wow_PackAppearance(7, 6, 5, 4, 13, 1, 2), legacy);
    out = Wow_UnpackAppearance(Wow_PackAppearance(7, 6, 5, 4, 16, 1, 2));
    T_EQ(out.faceID, 6); T_EQ(out.facialHairStyleID, 16); T_EQ(out.flags, 2);
}

TEST(wow_appearance, equipment_pack_unpack) {
    DWORD packed = Wow_PackEquipment(1, 2, 127, 255);
    wowEquipment_t out = Wow_UnpackEquipment(packed);
    T_EQ(out.upperBodyItem, 1);
    T_EQ(out.lowerBodyItem, 2);
    T_EQ(out.handItem, 127);
    T_EQ(out.footItem, 255);
}
#endif
