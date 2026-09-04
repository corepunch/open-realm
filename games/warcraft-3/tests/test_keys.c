#include "test.h"
#include "keys_name.h"

TEST(keys, parse_plain_and_named_keys) {
    keyCode_t key;
    DWORD mods;

    T_ASSERT(Key_ParseName("1", &key, &mods));
    T_EQ(key, (keyCode_t)'1');
    T_EQ(mods, 0u);

    T_ASSERT(Key_ParseName("q", &key, &mods));
    T_EQ(key, (keyCode_t)'q');
    T_EQ(mods, 0u);

    T_ASSERT(Key_ParseName("Q", &key, &mods));
    T_EQ(key, (keyCode_t)'q');
    T_EQ(mods, 0u);

    T_ASSERT(Key_ParseName("ESCAPE", &key, &mods));
    T_EQ(key, (keyCode_t)K_ESCAPE);
    T_EQ(mods, 0u);

    T_ASSERT(Key_ParseName("MOUSE1", &key, &mods));
    T_EQ(key, (keyCode_t)K_MOUSE1);
    T_EQ(mods, 0u);

    T_ASSERT(Key_ParseName("TAB", &key, &mods));
    T_EQ(key, (keyCode_t)K_TAB);
    T_EQ(mods, 0u);

    T_ASSERT(Key_ParseName("LEFTARROW", &key, &mods));
    T_EQ(key, (keyCode_t)K_LEFTARROW);
    T_EQ(mods, 0u);

    T_ASSERT(Key_ParseName("left", &key, &mods));
    T_EQ(key, (keyCode_t)K_LEFTARROW);
    T_EQ(mods, 0u);

    T_ASSERT(Key_ParseName("MWHEELUP", &key, &mods));
    T_EQ(key, (keyCode_t)K_MWHEELUP);
    T_EQ(mods, 0u);

    T_ASSERT(Key_ParseName("MWHEELDOWN", &key, &mods));
    T_EQ(key, (keyCode_t)K_MWHEELDOWN);
    T_EQ(mods, 0u);
}

TEST(keys, parse_modifier_combos) {
    keyCode_t key;
    DWORD mods;

    T_ASSERT(Key_ParseName("SHIFT+1", &key, &mods));
    T_EQ(key, (keyCode_t)'1');
    T_EQ(mods, KEY_MOD_SHIFT);

    T_ASSERT(Key_ParseName("shift+n", &key, &mods));
    T_EQ(key, (keyCode_t)'n');
    T_EQ(mods, KEY_MOD_SHIFT);

    T_ASSERT(Key_ParseName("CTRL+1", &key, &mods));
    T_EQ(key, (keyCode_t)'1');
    T_EQ(mods, KEY_MOD_CTRL);

    T_ASSERT(Key_ParseName("CONTROL+9", &key, &mods));
    T_EQ(key, (keyCode_t)'9');
    T_EQ(mods, KEY_MOD_CTRL);

    T_ASSERT(Key_ParseName("ALT+MOUSE1", &key, &mods));
    T_EQ(key, (keyCode_t)K_MOUSE1);
    T_EQ(mods, KEY_MOD_ALT);

    T_ASSERT(Key_ParseName("CTRL+SHIFT+1", &key, &mods));
    T_EQ(key, (keyCode_t)'1');
    T_EQ(mods, KEY_MOD_CTRL | KEY_MOD_SHIFT);

    T_ASSERT(Key_ParseName("CTRL+ALT+SHIFT+1", &key, &mods));
    T_EQ(key, (keyCode_t)'1');
    T_EQ(mods, KEY_MOD_CTRL | KEY_MOD_ALT | KEY_MOD_SHIFT);
}

TEST(keys, parse_rejects_unknown_names) {
    keyCode_t key = 0;
    DWORD mods = 0;

    T_ASSERT(!Key_ParseName("", &key, &mods));
    T_ASSERT(!Key_ParseName("SHIFT+", &key, &mods));
    T_ASSERT(!Key_ParseName("SHIFT++1", &key, &mods));
    T_ASSERT(!Key_ParseName("SHIFT+CTRL", &key, &mods));
    T_ASSERT(!Key_ParseName("SHIFT+CTRL+1", &key, &mods));
    T_ASSERT(!Key_ParseName("ALT+CTRL+1", &key, &mods));
    T_ASSERT(!Key_ParseName("NOTAKEY", &key, &mods));
}

TEST(keys, format_canonical_modifier_order) {
    char name[64];

    Key_FormatName((keyCode_t)'1', 0, name, sizeof(name));
    T_STREQ(name, "1");
    Key_FormatName((keyCode_t)'1', KEY_MOD_SHIFT, name, sizeof(name));
    T_STREQ(name, "SHIFT+1");
    Key_FormatName(K_MOUSE1, KEY_MOD_ALT, name, sizeof(name));
    T_STREQ(name, "ALT+MOUSE1");
    Key_FormatName((keyCode_t)'1', KEY_MOD_CTRL | KEY_MOD_SHIFT, name, sizeof(name));
    T_STREQ(name, "CTRL+SHIFT+1");
}

TEST(keys, select_slot_is_exact_stroke) {
    DWORD shift_and_plain = (1u << KEY_MOD_SHIFT) | 1u;
    DWORD ctrl_only = 1u << KEY_MOD_CTRL;

    T_EQ(Key_SelectSlot(0, shift_and_plain), 0u);
    T_EQ(Key_SelectSlot(KEY_MOD_SHIFT, shift_and_plain), KEY_MOD_SHIFT);
    T_EQ(Key_SelectSlot(KEY_MOD_CTRL, shift_and_plain), KEY_MOD_COUNT);
    T_EQ(Key_SelectSlot(KEY_MOD_CTRL | KEY_MOD_SHIFT, ctrl_only), KEY_MOD_COUNT);
    T_EQ(Key_SelectSlot(KEY_MOD_SHIFT, 1u), KEY_MOD_COUNT);
}
