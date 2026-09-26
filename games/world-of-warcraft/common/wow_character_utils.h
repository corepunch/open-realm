#ifndef WOW_CHARACTER_UTILS_H
#define WOW_CHARACTER_UTILS_H

#include "common/shared.h"
#include <strings.h>

/* Renderer and m2tool slot enums share this order (none, head, shoulders,
 * chest, shirt, belt, legs, boots, gloves, tabard, cape). */
static uint8_t Wow_CharacterSlotForInventoryType(uint32_t inventory_type) {
    switch (inventory_type) {
        case 1: return 1; case 3: return 2; case 4: return 4; case 5: case 20: return 3;
        case 6: return 5; case 7: return 6; case 8: return 7; case 10: return 8;
        case 16: return 10; case 19: return 9; default: return 0;
    }
}

/* Classic CreatureDisplayInfoExtra NPCItemDisplay slot order. */
static uint8_t Wow_CharacterCreatureItemSlot(uint32_t index) {
    static uint8_t const slots[] = { 1, 2, 4, 3, 5, 6, 7, 0, 8, 9, 10 };
    return index < sizeof(slots) ? slots[index] : 0;
}

/* whoa s_itemPriority, indexed by the shared slot order and eight body-atlas regions. */
static signed char Wow_CharacterTexturePriority(uint32_t slot, uint32_t region) {
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
static uint32_t Wow_RaceNumber(cstring_t name) {
    static struct { cstring_t name; uint32_t id; } const races[] = {
        { "Human", 1 }, { "Orc", 2 }, { "Dwarf", 3 }, { "NightElf", 4 },
        { "Scourge", 5 }, { "Undead", 5 }, { "Tauren", 6 }, { "Gnome", 7 },
        { "Troll", 8 }, { "BloodElf", 10 }, { "Draenei", 11 },
    };
    if (!name) return 0;
    FOR_LOOP(i, sizeof(races) / sizeof(races[0]))
        if (!strcasecmp(races[i].name, name)) return races[i].id;
    return 0;
}

/* ChrClasses.dbc IDs used by runtime quest-text substitutions. */
static cstring_t Wow_ClassName(uint32_t id) {
    static struct { cstring_t name; uint32_t id; } const classes[] = {
        { "warrior", 1 }, { "paladin", 2 }, { "hunter", 3 }, { "rogue", 4 }, { "priest", 5 },
        { "shaman", 7 }, { "mage", 8 }, { "warlock", 9 }, { "druid", 11 },
    };
    FOR_LOOP(i, sizeof(classes) / sizeof(classes[0]))
        if (classes[i].id == id) return classes[i].name;
    return "adventurer";
}

/* Character models omit some race-specific variants; choose only IDs present in the loaded model. */
static uint16_t Wow_CharacterGeosetPick(uint16_t const *available, uint32_t count, uint16_t group,
                                    uint16_t preferred, uint16_t fallback) {
    uint16_t lowest = 0;
    FOR_LOOP(i, count)
        if (available[i] == preferred) return preferred;
    FOR_LOOP(i, count) {
        if (available[i] == fallback) return fallback;
        if (available[i] / 100 == group && (!lowest || available[i] < lowest)) lowest = available[i];
    }
    return lowest;
}

#endif
