#include "test.h"

#include <stdlib.h>

#include "common/shared.h"
#include "common/net.h"


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

