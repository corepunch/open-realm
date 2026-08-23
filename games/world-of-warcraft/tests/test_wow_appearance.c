#include "test.h"

#include <math.h>
#include <stdlib.h>

#include "common/shared.h"
#include "common/net.h"
#include "common/stb_dbc.h"
#include "client/tr_public.h"
#include "renderer/m2/r_dbc.h"
#include "renderer/m2/r_m2_utils.h"
#include "common/ui_constants.h"
#include "common/wow_view.h"

refImport_t ri;

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

TEST(wow_m2, dbc_character_path_resolves_race_and_gender) {
    DWORD race, gender;

    T_ASSERT(M2_DbcCharacterRaceGender("Character\\Orc\\Male\\OrcMale.m2", &race, &gender));
    T_EQ(race, 2); T_EQ(gender, 0);
    T_ASSERT(M2_DbcCharacterRaceGender("Character/NightElf/Female/NightElfFemale.m2", &race, &gender));
    T_EQ(race, 4); T_EQ(gender, 1);
    T_ASSERT(!M2_DbcCharacterRaceGender("Creature\\Wolf\\Wolf.m2", &race, &gender));
}

TEST(wow_m2, classic_male_hair_path_uses_dbc_color) {
    PATHSTR path;

    T_ASSERT(m2_classic_hair_texture_path("Character\\Human\\Male\\HumanMale.m2", 0, path));
    T_STREQ(path, "Character\\Human\\Hair00_00.blp");
    T_ASSERT(m2_classic_hair_texture_path("Character/Orc/Male/OrcMale.m2", 7, path));
    T_STREQ(path, "Character\\Orc\\Hair00_07.blp");
    T_ASSERT(!m2_classic_hair_texture_path("Creature\\Wolf\\Wolf.m2", 0, path));
}

TEST(wow_renderer, world_labels_use_wowee_distance_tiers_and_fade) {
    T_ASSERT(fabsf(Wow_WorldLabelAlpha(14.0f, false, false) - 1.0f) < 0.001f);
    T_ASSERT(fabsf(Wow_WorldLabelAlpha(17.5f, false, false) - 0.5f) < 0.001f);
    T_ASSERT(fabsf(Wow_WorldLabelAlpha(20.0f, false, false)) < 0.001f);
    T_ASSERT(fabsf(Wow_WorldLabelAlpha(39.0f, false, true) - 0.2f) < 0.001f);
    T_ASSERT(fabsf(Wow_WorldLabelAlpha(59.0f, true, false) - 0.2f) < 0.001f);
}

TEST(wow_renderer, character_composite_cache_hits_then_evicts_oldest) {
    m2CompositeCacheKey_t keys[2] = { 0 };
    m2CompositeCacheParams_t params = { keys, 2, { (void *)1, 10, 20, 30, 0, false }, NULL, false };
    DWORD clock = 0, slot;

    params.clock = &clock;
    slot = m2_composite_cache_slot(&params); T_EQ(slot, 0); T_ASSERT(!params.hit);
    slot = m2_composite_cache_slot(&params); T_EQ(slot, 0); T_ASSERT(params.hit);
    params.wanted = (m2CompositeCacheKey_t){ (void *)2, 11, 21, 31, 0, false };
    slot = m2_composite_cache_slot(&params); T_EQ(slot, 1); T_ASSERT(!params.hit);
    params.wanted = (m2CompositeCacheKey_t){ (void *)3, 12, 22, 32, 0, false };
    slot = m2_composite_cache_slot(&params); T_EQ(slot, 0); T_ASSERT(!params.hit); T_ASSERT(keys[0].owner == (void *)3);
}

TEST(wow_renderer, view_angle_helpers_wrap_and_match_forward_axis) {
    VECTOR3 angles = { 0.0f, 90.0f, 0.0f }, forward;

    T_ASSERT(fabsf(Wow_LerpDegrees(350.0f, 10.0f, 0.5f) - 360.0f) < 0.001f);
    forward = Wow_ViewForward(&angles);
    T_ASSERT(fabsf(forward.x) < 0.001f); T_ASSERT(fabsf(forward.y - 1.0f) < 0.001f);
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

TEST(wow_m2, skin_vertex_range_is_prevalidated_before_copy) {
    WORD vertices[] = { 2, 0, 1 };
    WORD indices[] = { 0, 2 };

    T_ASSERT(m2_validate_skin_vertex_range(vertices, 3, indices, 2, 3, 0, 2));
    T_ASSERT(!m2_validate_skin_vertex_range(vertices, 3, indices, 2, 2, 0, 2));
    T_ASSERT(!m2_validate_skin_vertex_range(vertices, 3, indices, 1, 3, 0, 2));
    T_ASSERT(!m2_validate_skin_vertex_range(vertices, 3, indices, 2, 3, 2, 1));
}

TEST(wow_m2, character_geoset_selection_uses_model_fallbacks) {
    WORD available[] = { 501, 505, 902, 903, 1301, 1302 };
    WORD tauren[] = { 501, 903, 1301 }, legacy[] = { 501, 902, 1301 };
    DWORD count = sizeof(available) / sizeof(available[0]);

    T_EQ(Wow_CharacterGeosetPick(available, count, 5, 501, 501), 501);
    T_EQ(Wow_CharacterGeosetPick(available, count, 5, 503, 501), 501);
    T_EQ(Wow_CharacterGeosetPick(available, count, 5, 504, 501), 501);
    T_EQ(Wow_CharacterGeosetPick(available, count, 9, 903, 902), 903);
    T_EQ(Wow_CharacterGeosetPick(tauren, 3, 9, 903, 902), 903);
    T_EQ(Wow_CharacterGeosetPick(legacy, 3, 9, 903, 902), 902);
    T_EQ(Wow_CharacterGeosetPick(available, count, 13, 1302, 1301), 1302);
    T_EQ(Wow_CharacterGeosetPick(available, count, 13, 1303, 1301), 1301);
}

TEST(wow_m2, start_outfit_inventory_types_select_equipped_slots) {
    T_EQ(Wow_CharacterSlotForInventoryType(4), 4);  /* shirt */
    T_EQ(Wow_CharacterSlotForInventoryType(7), 6);  /* legs */
    T_EQ(Wow_CharacterSlotForInventoryType(8), 7);  /* boots */
    T_EQ(Wow_CharacterSlotForInventoryType(0), 0);  /* backpack/non-equipment */
    T_EQ(Wow_CharacterSlotForInventoryType(14), 0); /* shield attachment, not body texture */
}

TEST(wow_m2, item_display_texture_base_matches_dbc_schema) {
    T_EQ(m2_item_display_texture_base(21), 0);
    T_EQ(m2_item_display_texture_base(22), 14);
    T_EQ(m2_item_display_texture_base(23), 14);
    T_EQ(m2_item_display_texture_base(24), 14);
    T_EQ(m2_item_display_texture_base(25), 15);
}

TEST(wow_m2, char_sections_layout_matches_mounted_dbc_schema) {
    DWORD classic[20][10] = { 0 }, wrath[20][10] = { 0 };

    FOR_LOOP(i, 20) {
        classic[i][4] = i % 4; classic[i][5] = i % 10; classic[i][6] = 100 + i;
        wrath[i][4] = 100 + i; wrath[i][8] = i % 4; wrath[i][9] = i % 10;
    }
    T_EQ(m2_char_sections_layout((BYTE const *)classic, 20, sizeof(classic[0])), M2_CHAR_SECTIONS_VARIATION_FIRST);
    T_EQ(m2_char_sections_layout((BYTE const *)wrath, 20, sizeof(wrath[0])), M2_CHAR_SECTIONS_TEXTURE_FIRST);
    T_EQ(m2_char_sections_layout(NULL, 0, 0), M2_CHAR_SECTIONS_INVALID);
}

TEST(wow_m2, creature_extra_items_select_classic_npc_slots) {
    BYTE expected[] = { 1, 2, 4, 3, 5, 6, 7, 0, 8, 9, 10 };

    FOR_LOOP(i, sizeof(expected)) T_EQ(Wow_CharacterCreatureItemSlot(i), expected[i]);
}

TEST(wow_m2, race_number_resolves_from_shared_config_table) {
    T_EQ(Wow_RaceNumber("Human"), 1);
    T_EQ(Wow_RaceNumber("Orc"), 2);
    T_EQ(Wow_RaceNumber("Dwarf"), 3);
    T_EQ(Wow_RaceNumber("NightElf"), 4);
    T_EQ(Wow_RaceNumber("Scourge"), 5);
    T_EQ(Wow_RaceNumber("Undead"), 5);
    T_EQ(Wow_RaceNumber("Tauren"), 6);
    T_EQ(Wow_RaceNumber("Gnome"), 7);
    T_EQ(Wow_RaceNumber("Troll"), 8);
    T_EQ(Wow_RaceNumber("BloodElf"), 10);
    T_EQ(Wow_RaceNumber("Draenei"), 11);
    T_EQ(Wow_RaceNumber("unknown"), 0);
    T_EQ(Wow_RaceNumber(NULL), 0);
}

TEST(wow_dbc, table_parser_fills_struct_from_columns) {
    typedef struct { DWORD id, flags; LPCSTR name; } rec_t;
    static stbDbcField_t const schema[] = {
        { 0, offsetof(rec_t, id),    STB_DBC_U32 },
        { 1, offsetof(rec_t, flags), STB_DBC_U32 },
        { 2, offsetof(rec_t, name),  STB_DBC_STR },
    };
    BYTE records[2 * 12];
    BYTE strings[] = { 0, 'H', 'u', 'm', 'a', 'n', 0, 'O', 'r', 'c', 0 }; /* offset 0 is the null string */
    rec_t out[2];
    DWORD v;
    memset(records, 0, sizeof(records));
    v = 1; memcpy(records + 0, &v, 4); v = 7; memcpy(records + 4, &v, 4); v = 1; memcpy(records + 8, &v, 4);
    v = 2; memcpy(records + 12, &v, 4); v = 8; memcpy(records + 16, &v, 4); v = 7; memcpy(records + 20, &v, 4);

    Stb_DbcParseRows(records, 2, 12, strings, sizeof(strings), schema, 3, out, sizeof(out[0]));

    T_EQ(out[0].id, 1); T_EQ(out[0].flags, 7); T_STREQ(out[0].name, "Human");
    T_EQ(out[1].id, 2); T_EQ(out[1].flags, 8); T_STREQ(out[1].name, "Orc");
}

TEST(wow_dbc, table_parser_bounds_checks_columns_outside_record) {
    typedef struct { DWORD id, flags; LPCSTR name; } rec_t;
    static stbDbcField_t const schema[] = {
        { 0, offsetof(rec_t, id),    STB_DBC_U32 },
        { 1, offsetof(rec_t, flags), STB_DBC_U32 },
        { 4, offsetof(rec_t, name),  STB_DBC_STR }, /* column 4 is past a 3-column record */
    };
    BYTE records[12] = { 0 };
    rec_t out = { .id = 0xdead, .flags = 0xbeef, .name = (LPCSTR)0x1 };
    DWORD v = 9;
    memcpy(records + 0, &v, 4);

    Stb_DbcParseRows(records, 1, 12, NULL, 0, schema, 3, &out, sizeof(out));

    T_EQ(out.id, 9); T_EQ(out.flags, 0); T_EQ(out.name, (LPCSTR)0x1); /* out-of-range column untouched */
}

TEST(wow_dbc, table_parser_fills_contiguous_array_from_columns) {
    typedef struct { DWORD id; LPCSTR names[3]; } rec_t;
    static stbDbcField_t const schema[] = {
        { 0, offsetof(rec_t, id),    STB_DBC_U32 },
        { 1, offsetof(rec_t, names), STB_DBC_STR, 3 },
    };
    BYTE records[16] = { 0 };
    BYTE strings[] = { 0, 'H', 'u', 'm', 'a', 'n', 0, 'O', 'r', 'c', 0, 'D', 'w', 'f', 0 };
    rec_t out = { 0 };
    DWORD v;
    v = 7; memcpy(records + 0, &v, 4);
    v = 1; memcpy(records + 4, &v, 4);
    v = 7; memcpy(records + 8, &v, 4);
    v = 11; memcpy(records + 12, &v, 4);

    Stb_DbcParseRows(records, 1, 16, strings, sizeof(strings), schema, 2, &out, sizeof(out));

    T_EQ(out.id, 7);
    T_STREQ(out.names[0], "Human"); T_STREQ(out.names[1], "Orc"); T_STREQ(out.names[2], "Dwf");
}

TEST(wow_dbc, table_parser_array_stops_at_record_boundary) {
    typedef struct { LPCSTR names[3]; } rec_t;
    static stbDbcField_t const schema[] = {
        { 0, offsetof(rec_t, names), STB_DBC_STR, 3 },
    };
    BYTE records[8] = { 0 }; /* only columns 0-1 fit */
    rec_t out = { .names[0] = (LPCSTR)0x1, .names[1] = (LPCSTR)0x2, .names[2] = (LPCSTR)0x3 };
    DWORD v = 1;
    memcpy(records + 0, &v, 4);
    v = 7; memcpy(records + 4, &v, 4);

    Stb_DbcParseRows(records, 1, 8, NULL, 0, schema, 1, &out, sizeof(out));

    T_EQ(out.names[0], (LPCSTR)NULL); T_EQ(out.names[1], (LPCSTR)NULL); T_EQ(out.names[2], (LPCSTR)0x3);
}

TEST(wow_m2, helmet_hide_mask_bits_match_geoset_groups) {
    /* M2_CharacterGeosetVisible checks outfit->helm_hide & (1 << (section/100));
     * the HelmetGeosetVisData hide constants must equal those group bits. */
    T_EQ(M2_HELM_HIDE_HAIR, 1u << 0);      /* group 0 (head/hair) */
    T_EQ(M2_HELM_HIDE_BEARD, 1u << 1);     /* group 1 */
    T_EQ(M2_HELM_HIDE_SIDEBURNS, 1u << 2); /* group 2 */
    T_EQ(M2_HELM_HIDE_MOUSTACHE, 1u << 3); /* group 3 */
    T_EQ(M2_HELM_HIDE_EARS, 1u << 7);      /* group 7 */
}

TEST(wow_m2, pants_remain_below_transparent_boot_texture) {
    COLOR32 atlas = { 0, 0, 0, 255 };
    COLOR32 pants = { 10, 20, 30, 255 };
    COLOR32 transparent_boot = { 90, 80, 70, 0 };
    COLOR32 opaque_boot = { 90, 80, 70, 255 };

    T_EQ(Wow_CharacterTexturePriority(6, 6), 0);
    T_EQ(Wow_CharacterTexturePriority(7, 6), 2);
    T_EQ(Wow_CharacterTexturePriority(7, 5), -1);
    m2_paste_component(&atlas, 1, 1, &pants, 1, 1, 0, 0, 1, 1);
    m2_paste_component(&atlas, 1, 1, &transparent_boot, 1, 1, 0, 0, 1, 1);
    T_EQ(atlas.r, pants.r); T_EQ(atlas.g, pants.g); T_EQ(atlas.b, pants.b);
    m2_paste_component(&atlas, 1, 1, &opaque_boot, 1, 1, 0, 0, 1, 1);
    T_EQ(atlas.r, opaque_boot.r); T_EQ(atlas.g, opaque_boot.g); T_EQ(atlas.b, opaque_boot.b);
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

TEST(wow_appearance, wow_entity_delta_preserves_overhead_sprite) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 9, .model = 3, .overhead_sprite = 42 }, out = { 0 };
    DWORD bits = 0;
    int number;

    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 9);
    T_EQ(out.overhead_sprite, 42);
}
