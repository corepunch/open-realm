/*
 * t_cheats.c — Unit tests for cheat command input sanitization.
 *
 * Tests the G_ReadRawcode helper that validates and reads WC3 four-character
 * rawcodes used by `give unit`, `give item`, and `give ability`.
 */
#ifdef BZ_TESTS

#include "shared/test.h"
#include "../g_local.h"

TEST(wc3_cheats, read_rawcode_accepts_exactly_four_chars) {
    uint32_t code = 0;
    T_ASSERT(G_ReadRawcode("hfoo", &code));
    T_EQ((code >>  0) & 0xff, (uint32_t)'h');
    T_EQ((code >>  8) & 0xff, (uint32_t)'f');
    T_EQ((code >> 16) & 0xff, (uint32_t)'o');
    T_EQ((code >> 24) & 0xff, (uint32_t)'o');
}

TEST(wc3_cheats, read_rawcode_rejects_overlong_input) {
    uint32_t code = 0xdeadbeef;
    T_ASSERT(!G_ReadRawcode("hfoot", &code));
    T_EQ(code, 0xdeadbeefu); /* out unchanged */
}

TEST(wc3_cheats, read_rawcode_rejects_short_input) {
    uint32_t code = 0xdeadbeef;
    T_ASSERT(!G_ReadRawcode("hfo", &code));
    T_EQ(code, 0xdeadbeefu);
}

TEST(wc3_cheats, read_rawcode_rejects_null) {
    uint32_t code = 0xdeadbeef;
    T_ASSERT(!G_ReadRawcode(NULL, &code));
    T_EQ(code, 0xdeadbeefu);
}

TEST(wc3_cheats, read_rawcode_rejects_empty_string) {
    uint32_t code = 0xdeadbeef;
    T_ASSERT(!G_ReadRawcode("", &code));
    T_EQ(code, 0xdeadbeefu);
}

TEST(wc3_cheats, read_rawcode_matches_makefourcc) {
    uint32_t code = 0;
    T_ASSERT(G_ReadRawcode("hpea", &code));
    T_EQ(code, MAKEFOURCC('h', 'p', 'e', 'a'));
}

#endif
