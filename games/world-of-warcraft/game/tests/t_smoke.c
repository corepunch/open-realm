/*
 * t_smoke.c — In-engine smoke tests (see CONTRIBUTING.md).
 *
 * These run inside the REAL, fully-linked game module under a headless
 * dedicated server — no mock harness, no re-declared globals, no per-test
 * Makefile source list.  They call the same production functions the shipping
 * binary uses.  Build/run with:
 *
 *   make test-wow-engine                 # every test
 *   make test-wow-engine PATTERN='wow_smoke.*'
 *
 * The whole file is compiled only when the game module is built with
 * -DBZ_TESTS, so production builds contain none of this.
 */
#ifdef BZ_TESTS

#include "test.h"
#include "game/g_wow_local.h"

/* Wow_Clamp is real production code (g_wow.c); testing it here proves the
 * runtime exercises the actual game module, not a copy. */
TEST(wow_smoke, clamp) {
    T_FEQ(Wow_Clamp(5.0f, 0.0f, 10.0f), 5.0f, 0.001f);
    T_FEQ(Wow_Clamp(-3.0f, 0.0f, 10.0f), 0.0f, 0.001f);
    T_FEQ(Wow_Clamp(42.0f, 0.0f, 10.0f), 10.0f, 0.001f);
}

TEST(wow_smoke, read32_little_endian) {
    BYTE bytes[4] = { 0x78, 0x56, 0x34, 0x12 };
    T_EQ(Wow_Read32(bytes), 0x12345678u);
}

TEST(wow_smoke, read_float_roundtrip) {
    FLOAT expected = 1.5f;
    BYTE bytes[4];
    memcpy(bytes, &expected, sizeof(bytes));
    T_FEQ(Wow_ReadFloat(bytes), expected, 0.0f);
}

#endif /* BZ_TESTS */
