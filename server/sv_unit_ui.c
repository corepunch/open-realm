/*
 * sv_unit_ui.c — Retired server-authored unit UI opcode.
 *
 * The in-game HUD is built by menu module from replicated entity/player state.
 * Keep this handler only to consume old requests without desynchronizing the
 * client message stream.
 */

#include "server.h"

void SV_HandleUnitUIRequest(LPCLIENT client, LPSIZEBUF msg) {
    BYTE num_selected = MSG_ReadByte(msg);

    (void)client;
    if (num_selected > 12) {
        return;
    }
    for (BYTE i = 0; i < num_selected; i++) {
        (void)MSG_ReadShort(msg);
    }
}
