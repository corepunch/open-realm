#include "g_local.h"

/* Units created after a global slot change inherit the current capacities. */
void G_InitStockSlots(LPEDICT unit) {
    if (!unit) return;
    unit->stock.item_slots = level.stock.item_slots;
    unit->stock.unit_slots = level.stock.unit_slots;
}

/* Global slot changes affect current shops and become the default for units created later. */
void G_SetAllStockSlots(BOOL items, LONG slots) {
    DWORD value = (DWORD)MAX(0, slots);
    if (items) level.stock.item_slots = value;
    else level.stock.unit_slots = value;
    FILTER_EDICTS(unit, unit->inuse) {
        if (items) unit->stock.item_slots = value;
        else unit->stock.unit_slots = value;
    }
}

/* Unit-specific stock limits override the current global capacity without changing later spawns. */
void G_SetStockSlots(LPEDICT unit, BOOL items, LONG slots) {
    DWORD value = (DWORD)MAX(0, slots);
    if (!unit) return;
    if (items) unit->stock.item_slots = value;
    else unit->stock.unit_slots = value;
}