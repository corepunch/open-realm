#include "g_wow_local.h"
#include <string.h>

/*
 *  Spawn-point selection — compiled from AC's playercreateinfo SQL.
 *
 *  The MPQ files provide positions (WorldSafeLocs.dbc) and race/class
 *  metadata (ChrRaces.dbc, ChrClasses.dbc) but NO mapping between them.
 *  Neither DBC carries a race→spawn-point association.
 *
 *  AzerothCore bridges this gap with the `playercreateinfo` SQL table:
 *    SELECT race, class, map, zone, position_x, position_y, position_z,
 *           orientation FROM playercreateinfo
 *
 *  We compile that same data as a C table below.  Each (race, class)
 *  pair gets exactly one spawn position with map ID, coordinates, and
 *  facing angle.  This is the authoritative single source — no fuzzy
 *  zone-name matching, no WorldSafeLocs string search.
 *
 *  Source: AC playercreateinfo.sql, filtered for classic races (1-8)
 *  and classes (1,2,3,4,5,7,8,9,11 — no DK in 1.12).
 */

typedef struct {
    DWORD race;
    DWORD cls;
    DWORD map;
    FLOAT x, y, z;
    FLOAT facing;
} player_create_info_t;

static const player_create_info_t spawn_table[] = {
    {1,1,0,-8949.95f,-132.493f,83.5312f,0},          /* Human Warrior */
    {1,2,0,-8949.95f,-132.493f,83.5312f,0},           /* Human Paladin */
    {1,4,0,-8949.95f,-132.493f,83.5312f,0},           /* Human Rogue */
    {1,5,0,-8949.95f,-132.493f,83.5312f,0},           /* Human Priest */
    {1,8,0,-8949.95f,-132.493f,83.5312f,0},           /* Human Mage */
    {1,9,0,-8949.95f,-132.493f,83.5312f,0},           /* Human Warlock */
    {2,1,1,-618.518f,-4251.67f,38.718f,0},            /* Orc Warrior */
    {2,3,1,-618.518f,-4251.67f,38.718f,0},            /* Orc Hunter */
    {2,4,1,-618.518f,-4251.67f,38.718f,0},            /* Orc Rogue */
    {2,7,1,-618.518f,-4251.67f,38.718f,0},            /* Orc Shaman */
    {2,9,1,-618.518f,-4251.67f,38.718f,0},            /* Orc Warlock */
    {3,1,0,-6240.32f,331.033f,382.758f,6.17716f},     /* Dwarf Warrior */
    {3,2,0,-6240.32f,331.033f,382.758f,6.17716f},     /* Dwarf Paladin */
    {3,3,0,-6240.32f,331.033f,382.758f,6.17716f},     /* Dwarf Hunter */
    {3,4,0,-6240.32f,331.033f,382.758f,6.17716f},     /* Dwarf Rogue */
    {3,5,0,-6240.32f,331.033f,382.758f,6.17716f},     /* Dwarf Priest */
    {4,1,1,10311.3f,832.463f,1326.41f,5.69632f},      /* NightElf Warrior */
    {4,3,1,10311.3f,832.463f,1326.41f,5.69632f},      /* NightElf Hunter */
    {4,4,1,10311.3f,832.463f,1326.41f,5.69632f},      /* NightElf Rogue */
    {4,5,1,10311.3f,832.463f,1326.41f,5.69632f},      /* NightElf Priest */
    {4,11,1,10311.3f,832.463f,1326.41f,5.69632f},     /* NightElf Druid */
    {5,1,0,1676.71f,1678.31f,121.67f,2.70526f},       /* Undead Warrior */
    {5,4,0,1676.71f,1678.31f,121.67f,2.70526f},       /* Undead Rogue */
    {5,5,0,1676.71f,1678.31f,121.67f,2.70526f},       /* Undead Priest */
    {5,8,0,1676.71f,1678.31f,121.67f,2.70526f},       /* Undead Mage */
    {5,9,0,1676.71f,1678.31f,121.67f,2.70526f},       /* Undead Warlock */
    {6,1,1,-2917.58f,-257.98f,52.9968f,0},            /* Tauren Warrior */
    {6,3,1,-2917.58f,-257.98f,52.9968f,0},            /* Tauren Hunter */
    {6,7,1,-2917.58f,-257.98f,52.9968f,0},            /* Tauren Shaman */
    {6,11,1,-2917.58f,-257.98f,52.9968f,0},           /* Tauren Druid */
    {7,1,0,-6240.32f,331.033f,382.758f,0},            /* Gnome Warrior */
    {7,4,0,-6240.0f,331.0f,383.0f,0},                 /* Gnome Rogue */
    {7,8,0,-6240.0f,331.0f,383.0f,0},                 /* Gnome Mage */
    {7,9,0,-6240.0f,331.0f,383.0f,0},                 /* Gnome Warlock */
    {8,1,1,-618.518f,-4251.67f,38.718f,0},            /* Troll Warrior */
    {8,3,1,-618.518f,-4251.67f,38.718f,0},            /* Troll Hunter */
    {8,4,1,-618.518f,-4251.67f,38.718f,0},            /* Troll Rogue */
    {8,5,1,-618.518f,-4251.67f,38.718f,0},            /* Troll Priest */
    {8,7,1,-618.518f,-4251.67f,38.718f,0},            /* Troll Shaman */
    {8,8,1,-618.518f,-4251.67f,38.718f,0},            /* Troll Mage */
};
#define SPAWN_TABLE_COUNT (int)(sizeof(spawn_table) / sizeof(spawn_table[0]))

/* Lookup by race string (e.g. "Human" → 1) and class number. */
static DWORD Wow_RaceNumber(LPCSTR race) {
    static struct { LPCSTR name; DWORD num; } map[] = {
        {"Human",1},{"Orc",2},{"Dwarf",3},{"NightElf",4},
        {"Undead",5},{"Tauren",6},{"Gnome",7},{"Troll",8},{"Scourge",5},
    };
    FOR_LOOP(i, sizeof(map)/sizeof(map[0]))
        if (!strcasecmp(map[i].name, race)) return map[i].num;
    return 0;
}

DWORD Wow_SelectSpawnPoint(LPCSTR race, LPEDICT ent) {
    DWORD race_num = Wow_RaceNumber(race);
    if (!race_num) return ~0u;
    FOR_LOOP(i, SPAWN_TABLE_COUNT)
        if (spawn_table[i].race == race_num && spawn_table[i].cls == 1)
            return i;
    return ~0u;
}

LPCVECTOR3 Wow_GetSpawnPos(DWORD idx) {
    if (idx >= (DWORD)SPAWN_TABLE_COUNT) return NULL;
    static VECTOR3 v;
    v.x = spawn_table[idx].x;
    v.y = spawn_table[idx].y;
    v.z = spawn_table[idx].z;
    return &v;
}

void Wow_TeleportPlayer(LPEDICT ent, DWORD idx) {
    if (idx >= (DWORD)SPAWN_TABLE_COUNT) return;
    const player_create_info_t *sp = &spawn_table[idx];
    FLOAT z = Wow_TerrainHeight(sp->x, sp->y);
    if (z == 0.0f) z = sp->z;
    ent->s.origin = (VECTOR3){ sp->x, sp->y, z };
    ent->s.origin2 = (VECTOR2){ sp->x, sp->y };
    ent->client->ps.origin = (VECTOR2){ sp->x, sp->y };
    fprintf(stderr, "WoW: respawned at map=%u (%.1f %.1f %.1f)\n",
            sp->map, sp->x, sp->y, sp->z);
}
