#ifdef BZ_TESTS
/*
 * t_rawcode.c: JASS rawcodes are big-endian integers, as in the original game.
 *
 * 'hfoo' is 0x68666F6F == 1751543663 for scripts, while the engine keeps its
 * little-endian four-character ids. The VM converts literals and the natives
 * listed in api_rawcode.h, so these tests write the decimal values on purpose.
 */

#include "test.h"
#include "../g_local.h"

bool run_test_jass(cstring_t src);
void setup_test_world(void);

TEST(wc3_rawcode, literals_are_big_endian) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(I2S('hfoo') == \"1751543663\", \"I2S of a rawcode literal\")\n"
        "  call BJassAssert('hfoo' == 1751543663, \"decimal literal equals the rawcode\")\n"
        "  call BJassAssert('hfoo' == $68666F6F, \"hex literal equals the rawcode\")\n"
        "  call BJassAssert('A000' == 1093677104, \"A000\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_rawcode, ordering_and_arithmetic_follow_the_text) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert('A000' < 'A001', \"A000 < A001\")\n"
        "  call BJassAssert('A00Z' < 'A010', \"A00Z < A010\")\n"
        "  call BJassAssert('A001' - 'A000' == 1, \"consecutive rawcodes differ by one\")\n"
        "  call BJassAssert('A000' + 42 == 'A00Z', \"A000 + 42 is A00Z\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_rawcode, short_literals_are_unchanged) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert('d' == 100, \"one character\")\n"
        "  call BJassAssert('ab' == 24930, \"two characters\")\n"
        "  call BJassAssert('abc' == 6382179, \"three characters\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_rawcode, unit_natives_take_and_return_big_endian_ids) {
    setup_test_world();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit literal = CreateUnit(Player(0), 'hbar', 0.0, 0.0, 0.0)\n"
        "  local unit decimal = CreateUnit(Player(0), 1751277938, 128.0, 0.0, 0.0)\n"
        "  call BJassAssert(literal != null, \"literal id creates a unit\")\n"
        "  call BJassAssert(decimal != null, \"decimal id creates a unit\")\n"
        "  call BJassAssert(GetUnitTypeId(literal) == 1751277938, \"GetUnitTypeId returns the big-endian id\")\n"
        "  call BJassAssert(GetUnitTypeId(decimal) == 'hbar', \"decimal creation yields the same type\")\n"
        "  call BJassAssert(I2S(GetUnitTypeId(literal)) == \"1751277938\", \"printed id\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_rawcode, reforged_creation_natives_accept_big_endian_ids) {
    setup_test_world();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = BlzCreateUnitWithSkin(Player(0), 'hbar', 0.0, 0.0, 0.0, 'hbar')\n"
        "  local item i = BlzCreateItemWithSkin('spro', 64.0, 64.0, 'spro')\n"
        "  local destructable d = BlzCreateDestructableWithSkinPitchRollColor('DTsb', 128.0, 128.0, 0.0, 0.0, 0.0, 1.0, 0, 'DTsb', PLAYER_COLOR_RED)\n"
        "  call BJassAssert(GetUnitTypeId(u) == 'hbar', \"unit rawcode\")\n"
        "  call BJassAssert(GetItemTypeId(i) == 'spro', \"item rawcode\")\n"
        "  call BJassAssert(GetDestructableTypeId(d) == 'DTsb', \"destructable rawcode\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_rawcode, string_conversions_use_big_endian_ids) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(UnitId(\"h\") == 0, \"incomplete unit rawcode\")\n"
        "  call BJassAssert(AbilityId(\"\") == 0, \"empty ability rawcode\")\n"
        "  call BJassAssert(UnitId2String(1751543663) == \"hfoo\", \"UnitId2String\")\n"
        "  call BJassAssert(UnitId(\"hfoo\") == 1751543663, \"UnitId\")\n"
        "  call BJassAssert(AbilityId(\"A000\") == 1093677104, \"AbilityId\")\n"
        "  call BJassAssert(AbilityId2String(1093677104) == \"A000\", \"AbilityId2String\")\n"
        "endfunction\n"
    ));
}

/* Order ids and rawcodes share one integer (build and train orders carry the
 * unit's rawcode). Only values that no numeric order id can reach are
 * converted: the largest id of the order table is 852597 (0xD0275), the
 * smallest four-character rawcode with a printable first letter is 0x30303030. */
TEST(wc3_rawcode, order_ids_convert_only_rawcode_values) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(OrderId(\"manaflareon\") == 852512, \"numeric order id\")\n"
        "  call BJassAssert(OrderId2String(852512) == \"manaflareon\", \"numeric order id back\")\n"
        "  call BJassAssert(OrderId(\"hfoo\") == 1751543663, \"rawcode order\")\n"
        "  call BJassAssert(OrderId2String(1751543663) == \"hfoo\", \"rawcode order back\")\n"
        "  call BJassAssert(OrderId2String(808464432) == \"0000\", \"smallest printable rawcode\")\n"
        "  call BJassAssert(StringLength(OrderId2String(16777215)) == 3, \"largest unconverted value\")\n"
        "  call BJassAssert(StringLength(OrderId2String(16777216)) == 1, \"smallest converted value\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_rawcode, table_is_sorted_and_terminated) {
    extern jassRawcode_t jass_rawcodes[];
    uint32_t count = 0;

    for (; jass_rawcodes[count].name; count++) {
        T_ASSERT(jass_rawcodes[count].mask || jass_rawcodes[count].order_mask);
        if (count) T_ASSERT(strcmp(jass_rawcodes[count - 1].name, jass_rawcodes[count].name) < 0);
    }
    T_ASSERT(count > 100);
}

#endif /* BZ_TESTS */
