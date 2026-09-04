#include "g_wow_local.h"

/*
 *  Spawn support for the WoW game module.
 *
 *  This file is deliberately small.  Quake 2's g_spawn.c and Warcraft III's
 *  g_spawn.c are large because both parse an authored entity lump from the map
 *  (BSP entity text / war3map.doo) and dispatch each placement through a
 *  classname→spawn-function table (`spawns[]` in Q2, `SP_CallSpawn` in WC3).
 *  WoW has no such lump: ADT terrain + static doodads/WMOs are renderer-owned
 *  (never game entities), and dynamic content arrives from AzerothCore CSVs
 *  (creatures.csv / quests.csv / playercreateinfo.csv) plus DBC lookups.
 *
 *  Instead of a classname table, each WoW spawner sets the game-local `think`
 *  callback directly and there is no central type dispatcher.  That is fine at
 *  the current five think-types (creature / game-object / corpse /
 *  dynamic-object / projectile — the player reuses the creature frame), but if
 *  the entity taxonomy grows — triggers, doors, varied NPC behaviour — a
 *  `SP_CallSpawn`-style dispatcher hub belongs here, mirroring WC3's
 *  games/warcraft-3/game/g_spawn.c.  Not a bug, just the natural growth point.
 *
 *  The player-spawn data lives in serverdata/playercreateinfo.csv, generated
 *  into build/generated/g_playercreateinfo.c (which owns the data and its
 *  lookups Wow_SelectSpawnPoint / Wow_PlayerCreateMap).  Only entity placement
 *  stays here because it touches game-runtime state.
 */
void Wow_TeleportPlayer(LPEDICT ent, DWORD idx) {
    LPCWOWSPAWNPOINT sp = Wow_SpawnByIndex(idx);
    FLOAT z;
    if (!sp) return;
    z = Wow_TerrainHeight(sp->x, sp->y);
    if (z == 0.0f) z = sp->z;
    ent->s.origin = (VECTOR3){ sp->x, sp->y, z };
    ent->s.origin2 = (VECTOR2){ sp->x, sp->y };
    ent->s.angle = sp->facing;
    ent->client->ps.vieworigin = (VECTOR3){ sp->x, sp->y, 0 };
    fprintf(stderr, "WoW: respawned at map=%u (%.1f %.1f %.1f)\n", sp->map, sp->x, sp->y, sp->z);
}

/* Teleport to an explicit world position — used for area trigger destinations
 * and warp-by-name where there is no playercreateinfo entry. */
void Wow_TeleportPlayerToPos(LPEDICT ent, FLOAT x, FLOAT y, FLOAT z, FLOAT orientation) {
    FLOAT tz = Wow_TerrainHeight(x, y);
    /* SQL z is authoritative for dungeon interiors where terrain height is 0. */
    if (tz != 0.0f) z = tz;
    ent->s.origin = (VECTOR3){ x, y, z };
    ent->s.origin2 = (VECTOR2){ x, y };
    ent->s.angle = orientation;
    ent->client->ps.vieworigin = (VECTOR3){ x, y, 0 };
    fprintf(stderr, "WoW: teleported to (%.1f %.1f %.1f)\n", x, y, z);
}
