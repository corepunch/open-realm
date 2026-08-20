# WoW Area Triggers and Map Transitions

## Overview

WoW has two kinds of locations that look like "entering a place":

- **Towns and castles** (Stormwind, Ironforge, Stratholme) — these are **WMO buildings embedded in the outdoor ADT map**. The player walks into them; no map change occurs. They load automatically when the outdoor map loads.

- **Dungeons and instances** (Deadmines, Shadowfang Keep, BRD) — these are **separate maps** stored as global-WMO WDT files (MPHD flag `0x01`). A map change occurs when crossing a trigger portal in the outdoor world.

## Map Types

All maps are in `DBFilesClient\Map.dbc`. The directory name in field 1 becomes the WDT path:

```
World/Maps/<dir>/<dir>.wdt
```

Dungeon maps use MPHD flag `0x01` in the WDT — the renderer detects this and suppresses terrain drawing, placing the single WMO directly from the WDT-level MODF chunk.

## Loading a Map from the Command Line

```sh
# Load outdoor world by numeric ID (Map.dbc field 0)
make run-wow ARGS="+map 0"               # Eastern Kingdoms
make run-wow ARGS="+map 1"               # Kalimdor

# Load a dungeon directly by numeric ID (spawns at entrance position)
make run-wow ARGS="+map 36"              # Deadmines
make run-wow ARGS="+map 33"             # Shadowfang Keep

# Load outdoor world and warp to a named location (WorldSafeLocs or areatrigger)
make run-wow ARGS="+map 0 +warp stormwind"
make run-wow ARGS="+map 1 +warp crossroads"
```

`+warp` is a deferred client command — it fires after the player has connected and the map is loaded.

## The `warp` Command

**In-game usage:** type `warp <name>` in the console.

**Search order:**

1. **WorldSafeLocs** on the current map — case-insensitive substring match. Returns the first entry whose name contains the query. Teleports player within the current map.
2. **areatrigger_teleport** data — case-insensitive substring match across all entries. If found, triggers a cross-map teleport (pending mechanism, same as area triggers).

**Examples:**

```
warp stormwind         → WorldSafeLoc "Stormwind City" (EK, same map)
warp ironforge         → WorldSafeLoc "Ironforge" (EK, same map)
warp deadmines         → areatrigger "DeadMines Entrance" → loads map 36
warp "scarlet monastery graveyard" → loads map 189
```

## In-World Area Triggers (Dungeon Portals)

When a player walks through a dungeon entrance portal in the outdoor world, an area trigger fires automatically.

**Data sources:**

| Source | Content | Local file |
|---|---|---|
| `AreaTrigger.dbc` | Trigger volumes (position, radius/box) | Read from MPQ at map load |
| `areatrigger_teleport.csv` | Destination (map, x, y, z, orientation) | `serverdata/areatrigger_teleport.csv` → `build/generated/g_areatrigger_teleport.c` |

`areatrigger_teleport.csv` was extracted from AzerothCore's `areatrigger_teleport` SQL table (276 entries). It covers all classic WoW dungeon entrances and exits.

## Lifecycle

```
Map A loads
  → Wow_SpawnEntities → Wow_LoadAreaTriggers
      reads AreaTrigger.dbc, caches entries for current map_id

Wow_RunFrame (every tick)
  → Wow_CheckAreaTriggers(player)
      for each trigger: sphere or OBB overlap test
      on hit: set wow_pending_teleport{x,y,z,orientation}
               gi.MenuAction("map", "World\\Maps\\Deadmines\\Deadmines.wdt")

Map B loads (Deadmines)
  → Wow_SpawnEntities
      sees wow_pending_teleport.pending == true
      uses pending x/y as spawn_origin
  → Wow_InitPlayer places player at (x, y, terrain_z)
  → post-init override: player->s.origin.z = pending.z  (SQL z is authoritative inside dungeons)
                         player->s.angle = pending.orientation
                         pending.pending = false
  → Wow_LoadAreaTriggers loads Deadmines exit triggers
```

## Dungeon Spawn Position (Direct Load)

When a dungeon map is loaded directly (e.g. `+map 36`) without going through a trigger, `Wow_SelectSpawnPoint` returns `~0u` because no `playercreateinfo` entry exists for dungeon map IDs. The code then:

1. Calls `Wow_HasSpawnForMap(map_id)` — returns false for dungeons (no race spawns there)
2. Falls back to `Wow_AreaTrigSpawnForMap(map_id)` — first `areatrigger_teleport` entry targeting this map
3. Sets the pending teleport mechanism with those coordinates

If no areatrigger entry targets this map, loading fails with an error log.

## Key Implementation Points

| Symbol | File | Purpose |
|---|---|---|
| `WOWAREATRIG` | `g_wow_local.h` | AreaTrigger.dbc record struct (10 fields, 40 bytes) |
| `WOWAREATRIGTELEPORT` | `g_wow_local.h` | areatrigger_teleport destination struct |
| `wow_pending_teleport` | `g_wow.c` (static) | Cross-map teleport state — cleared by Wow_SpawnEntities |
| `Wow_LoadAreaTriggers` | `g_wow.c` | Reads AreaTrigger.dbc for current map, filters by map_id |
| `Wow_CheckAreaTriggers` | `g_wow.c` | Per-frame player overlap test, fires map change on hit |
| `Wow_WdtPathForMapId` | `g_wow.c` | Builds WDT path from numeric Map.dbc ID |
| `Wow_HasSpawnForMap` | `g_playercreateinfo.c` | True if any race/class has playercreateinfo for this map |
| `Wow_AreaTrigSpawnForMap` | `g_areatrigger_teleport.c` | First entry targeting a given map_id |
| `Wow_AreaTrigTeleportByName` | `g_areatrigger_teleport.c` | Case-insensitive substring search by trigger name |
| `Wow_TeleportPlayerToPos` | `g_spawn.c` | Teleport to explicit x/y/z/orientation |

## AreaTrigger.dbc Field Layout (WoW 1.12)

10 fields, no string block, 40 bytes per record. Struct cast directly from the record:

| Field | Type | Meaning |
|---|---|---|
| 0 | uint32 | trigger id |
| 1 | uint32 | map_id — map where the trigger exists |
| 2–4 | float | center x, y, z |
| 5 | float | radius (> 0 → sphere; == 0 → use box) |
| 6–8 | float | box half-extents x, y, z |
| 9 | float | box orientation (radians, rotates XY into local frame) |

## Caveats

- Full portal-graph visibility culling is not yet implemented — all exterior WMO batches are suppressed when the camera is inside the WMO AABB, rather than per-portal. This is correct for correctness but may over-draw in large multi-room dungeons.
- `WMOAreaTable.dbc` is not loaded — per-room ambient/fog/sound switching within a dungeon is not yet implemented.
- MODD doodads that need a per-instance MOLT directional light (`0x04` flag) are still skipped (TODO in r_wowmap_wmo.c).
