#include "test.h"

#include <math.h>
#include <stdlib.h>

#include "common/shared.h"
#include "common/net.h"
#include "renderer/m2/r_m2_utils.h"


void MemFree(HANDLE mem) {
    free(mem);
}

int Cvar_Integer(LPCSTR name, int fallback) {
    (void)name;
    return fallback;
}

static sizeBuf_t make_msg_buf(BYTE *buf, DWORD bufsz) {
    sizeBuf_t sb;
    SZ_Init(&sb, buf, bufsz);
    return sb;
}

TEST(wow_m2, legacy_materials_use_render_flags_array) {
    m2Array_t modern = { 3, 100 }, legacy = { 4, 200 };

    T_EQ(m2_material_array(modern, legacy, false).offset, 100);
    T_EQ(m2_material_array(modern, legacy, true).offset, 200);
    T_EQ(m2_blend_mode(0), BLEND_MODE_NONE);
    T_EQ(m2_blend_mode(1), BLEND_MODE_ALPHAKEY);
    T_EQ(m2_blend_mode(2), BLEND_MODE_BLEND);
    T_EQ(m2_blend_mode(3), BLEND_MODE_ADD);
    T_EQ(m2_blend_mode(4), BLEND_MODE_ADDALPHA);
    T_EQ(m2_blend_mode(99), BLEND_MODE_NONE);
    T_EQ(m2_particle_blend_mode(3), BLEND_MODE_ADDALPHA);
    T_EQ(m2_particle_blend_mode(4), BLEND_MODE_ADD);
}

TEST(wow_m2, particle_curve_preserves_fractional_scale_and_normalized_lifetime) {
    M2PARTICLECURVE curve = { { 0.1388889f, 0.1666667f, 0.0000277778f }, 0.25f, 6.0f };
    cparticle_t particle = { 0 };

    m2_particle_encode_curve(&curve, &particle);

    T_EQ(particle.size[0], 212);
    T_EQ(particle.size[1], 255);
    T_EQ(particle.size[2], 0);
    T_ASSERT(fabsf(particle.size[0] * particle.size_value_scale - curve.value[0]) < 0.001f);
    T_ASSERT(fabsf(particle.size[1] * particle.size_value_scale - curve.value[1]) < 0.001f);
    T_ASSERT(fabsf(3.0f * particle.size_time_scale - 0.5f) < 0.0001f);
    T_EQ(particle.midtime, 64);
}

TEST(wow_m2, zero_particle_curve_stays_zero) {
    M2PARTICLECURVE curve = { { 0.0f, 0.0f, 0.0f }, 0.5f, 1.0f };
    cparticle_t particle = { 0 };

    m2_particle_encode_curve(&curve, &particle);

    T_EQ(particle.size[0], 0); T_EQ(particle.size[1], 0); T_EQ(particle.size[2], 0);
    T_ASSERT(fabsf(particle.size_value_scale - 1.0f) < 0.0001f);
    T_ASSERT(fabsf(particle.size_time_scale - 1.0f) < 0.0001f);
}

TEST(wow_m2, particle_ranges_spread_an_upward_vector) {
    VECTOR3 straight = m2_particle_direction(0.0f, 2.0f * (FLOAT)M_PI, (VECTOR2){ 1.0f, -1.0f });
    VECTOR3 spread = m2_particle_direction(0.5f, 2.0f * (FLOAT)M_PI, (VECTOR2){ 1.0f, -0.5f });
    VECTOR3 torch = m2_particle_direction(0.08726646f, 2.0f * (FLOAT)M_PI, (VECTOR2){ 1.0f, 1.0f });

    T_ASSERT(fabsf(straight.x) < 0.0001f);
    T_ASSERT(fabsf(straight.y) < 0.0001f);
    T_ASSERT(fabsf(straight.z - 1.0f) < 0.0001f);
    T_ASSERT(fabsf(spread.x) < 0.0001f);
    T_ASSERT(spread.y < 0.0f);
    T_ASSERT(spread.z > 0.0f);
    T_ASSERT(torch.z > 0.996f);
}

TEST(wow_m2, format_convention_selects_file_shaped_records) {
    m2FormatDef_t const *classic = m2_format_def(263), *modern = m2_format_def(264);
    m2ParticleClassic_t cp = { 0 }; m2Particle_t mp = { 0 };
    m2RibbonClassic_t cr = { 0 }; m2Ribbon_t mr = { 0 };
    cp.speed_track.track_type = 2; mp.speed_track.track_type = 3;
    cr.visibility_track.track_type = 4; mr.visibility_track.track_type = 5;

    T_EQ(classic->particle_stride, sizeof(cp));
    T_EQ(modern->particle_stride, sizeof(mp));
    T_EQ(classic->ribbon_stride, sizeof(cr));
    T_EQ(modern->ribbon_stride, sizeof(mr));
    T_EQ(m2_particle_track(classic, &cp, M2_PARTICLE_SPEED).track_type, 2);
    T_EQ(m2_particle_track(modern, &mp, M2_PARTICLE_SPEED).track_type, 3);
    T_EQ(m2_ribbon_track(classic, &cr, M2_RIBBON_VISIBILITY).track_type, 4);
    T_EQ(m2_ribbon_track(modern, &mr, M2_RIBBON_VISIBILITY).track_type, 5);
    T_EQ(m2_particle_track(classic, &cp, M2_PARTICLE_ZSOURCE).sequence_times.size, 0);
}

TEST(wow_m2, file_arrays_are_bounds_checked) {
    BYTE data[16] = { 0 };
    m2Array_t valid = { 2, 4 }, overflow = { 4, 8 }, negative = { 1, -1 };

    T_ASSERT(m2_array_ptr(data, sizeof(data), valid, sizeof(DWORD)) == data + 4);
    T_ASSERT(m2_array_ptr(data, sizeof(data), overflow, sizeof(DWORD)) == NULL);
    T_ASSERT(m2_array_ptr(data, sizeof(data), negative, 1) == NULL);
}

/* The appearance/equipment pack/unpack unit tests live in-engine
 * (games/world-of-warcraft/game/tests/t_appearance.c).  This standalone binary
 * covers entity-state delta (de)serialization, which links common/msg.c +
 * common/net.c and therefore cannot run inside the game module. */
TEST(wow_appearance, wow_entity_delta_preserves_appearance_and_equipment) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 };
    entityState_t to = { 0 };
    entityState_t out = { 0 };
    DWORD bits = 0;
    int number;

    to.number = 7;
    to.model = 3;
    to.appearance = Wow_PackAppearance(7, 6, 5, 4, 3, 1, 2);
    to.equipment = Wow_PackEquipment(9, 8, 7, 6);

    MSG_WriteDeltaEntity(&sb, &from, &to, true);

    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 7);
    T_EQ(out.number, 7);
    T_EQ(out.model, 3);
    T_EQ(out.appearance, to.appearance);
    T_EQ(out.equipment, to.equipment);
}

TEST(wow_appearance, wow_entity_delta_preserves_fractional_radius) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 8, .model = 3, .radius = 0.5f }, out = { 0 };
    DWORD bits = 0;
    int number;

    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 8);
    T_FEQ(out.radius, 0.5f, 0.001f);
}
