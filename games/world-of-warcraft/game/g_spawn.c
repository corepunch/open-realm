#include "g_wow_local.h"

/*
 *  Player spawn teleport.  The (race, class) → (map, x, y, z, facing) table is
 *  AzerothCore's playercreateinfo data (packet-sniffed by AC from retail), and
 *  lives in serverdata/playercreateinfo.csv, generated into
 *  build/generated/g_playercreateinfo.c.  That file owns the data and its
 *  lookups (Wow_SelectSpawnPoint / Wow_PlayerCreateMap); only the entity
 *  placement stays here because it touches game-runtime state.
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
    ent->client->ps.origin = (VECTOR2){ sp->x, sp->y };
    fprintf(stderr, "WoW: respawned at map=%u (%.1f %.1f %.1f)\n", sp->map, sp->x, sp->y, sp->z);
}
