/*
 * cl_unit_ui.c — Legacy unit UI response parser.
 *
 * Normal selection HUD updates are local now; this parser is retained for
 * compatibility with svc_unit_ui messages.
 */

#include "client.h"

void CL_ParseUnitUI(LPSIZEBUF msg) {
    BYTE num_units = MSG_ReadByte(msg);

    if (num_units == 0 || num_units > 12) {
        if (num_units == 0 && ui.UpdateUnitUI) {
            ui.UpdateUnitUI(0, NULL);
        }
        return;
    }

    uiUnitData_t *units = (uiUnitData_t *)MemAlloc(sizeof(uiUnitData_t) * num_units);
    memset(units, 0, sizeof(uiUnitData_t) * num_units);

    for (BYTE i = 0; i < num_units; i++) {
        uiUnitData_t *unit = &units[i];
        unit->entity_num = MSG_ReadShort(msg);

        unit->num_buttons = MSG_ReadByte(msg);
        if (unit->num_buttons > MAX_COMMAND_BUTTONS) {
            unit->num_buttons = MAX_COMMAND_BUTTONS;
        }
        for (BYTE j = 0; j < unit->num_buttons; j++) {
            uiCommandButton_t *btn = &unit->buttons[j];

            strncpy(btn->art, MSG_ReadString2(msg), sizeof(btn->art) - 1);
            strncpy(btn->tooltip, MSG_ReadString2(msg), sizeof(btn->tooltip) - 1);
            strncpy(btn->ubertip, MSG_ReadString2(msg), sizeof(btn->ubertip) - 1);
            strncpy(btn->command, MSG_ReadString2(msg), sizeof(btn->command) - 1);
            btn->hotkey = MSG_ReadByte(msg);
        }

        unit->num_inventory = MSG_ReadByte(msg);
        if (unit->num_inventory > MAX_INVENTORY_SLOTS) {
            unit->num_inventory = MAX_INVENTORY_SLOTS;
        }
        for (BYTE j = 0; j < unit->num_inventory; j++) {
            uiInventoryItem_t *item = &unit->inventory[j];

            strncpy(item->art, MSG_ReadString2(msg), sizeof(item->art) - 1);
            strncpy(item->tooltip, MSG_ReadString2(msg), sizeof(item->tooltip) - 1);
            strncpy(item->ubertip, MSG_ReadString2(msg), sizeof(item->ubertip) - 1);
            item->slot = MSG_ReadByte(msg);
        }

        unit->num_queue = MSG_ReadByte(msg);
        if (unit->num_queue > MAX_BUILD_QUEUE_ITEMS) {
            unit->num_queue = MAX_BUILD_QUEUE_ITEMS;
        }
        for (BYTE j = 0; j < unit->num_queue; j++) {
            uiQueueItem_t *queue_item = &unit->queue[j];
            LPCSTR art = MSG_ReadString2(msg);

            strncpy(queue_item->art, art, sizeof(queue_item->art) - 1);
            queue_item->entity = MSG_ReadShort(msg);
        }
    }

    ui.UpdateUnitUI((DWORD)num_units, units);
    MemFree(units);
}
