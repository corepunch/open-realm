#ifndef WOW_CHARACTER_UTILS_H
#define WOW_CHARACTER_UTILS_H

#include "common/shared.h"
#include <strings.h>

/* Renderer and m2tool slot enums share this order (none, head, shoulders,
 * chest, shirt, belt, legs, boots, gloves, tabard, cape). */
static BYTE Wow_CharacterSlotForInventoryType(DWORD inventory_type) {
    switch (inventory_type) {
        case 1: return 1; case 3: return 2; case 4: return 4; case 5: case 20: return 3;
        case 6: return 5; case 7: return 6; case 8: return 7; case 10: return 8;
        case 16: return 10; case 19: return 9; default: return 0;
    }
}

/* Classic CreatureDisplayInfoExtra NPCItemDisplay slot order. */
static BYTE Wow_CharacterCreatureItemSlot(DWORD index) {
    static BYTE const slots[] = { 1, 2, 4, 3, 5, 6, 7, 0, 8, 9, 10 };
    return index < sizeof(slots) ? slots[index] : 0;
}

/* whoa s_itemPriority, indexed by the shared slot order and eight body-atlas regions. */
static signed char Wow_CharacterTexturePriority(DWORD slot, DWORD region) {
    static signed char const priorities[11][8] = {
        { -1, -1, -1, -1, -1, -1, -1, -1 }, { -1, -1, -1, -1, -1, -1, -1, -1 },
        { -1, -1, -1, -1, -1, -1, -1, -1 }, {  1,  1, -1,  1,  1,  1,  1, -1 },
        {  0,  0, -1,  0,  0, -1, -1, -1 }, { -1, -1, -1, -1,  5,  2, -1, -1 },
        { -1, -1, -1, -1, -1,  0,  0, -1 }, { -1, -1, -1, -1, -1, -1,  2,  0 },
        { -1,  3,  0, -1, -1, -1, -1, -1 }, { -1, -1, -1,  4,  4, -1, -1, -1 },
        { -1, -1, -1, -1, -1, -1, -1, -1 },
    };
    return slot < 11 && region < 8 ? priorities[slot][region] : -1;
}

/* Race name → ChrRaces.dbc numeric ID. One config table, not a strcmp ladder;
 * the classic client-file prefix for undead is "Scourge", so both spellings
 * resolve to 5. Shared by renderer (model-path parse) and game (spawn select). */
static DWORD Wow_RaceNumber(LPCSTR name) {
    static struct { LPCSTR name; DWORD id; } const races[] = {
        { "Human", 1 }, { "Orc", 2 }, { "Dwarf", 3 }, { "NightElf", 4 },
        { "Scourge", 5 }, { "Undead", 5 }, { "Tauren", 6 }, { "Gnome", 7 },
        { "Troll", 8 }, { "BloodElf", 10 }, { "Draenei", 11 },
    };
    if (!name) return 0;
    FOR_LOOP(i, sizeof(races) / sizeof(races[0]))
        if (!strcasecmp(races[i].name, name)) return races[i].id;
    return 0;
}

/* Character models omit some race-specific variants; choose only IDs present in the loaded model. */
static WORD Wow_CharacterGeosetPick(WORD const *available, DWORD count, WORD group,
                                    WORD preferred, WORD fallback) {
    WORD lowest = 0;
    FOR_LOOP(i, count)
        if (available[i] == preferred) return preferred;
    FOR_LOOP(i, count) {
        if (available[i] == fallback) return fallback;
        if (available[i] / 100 == group && (!lowest || available[i] < lowest)) lowest = available[i];
    }
    return lowest;
}

#endif
