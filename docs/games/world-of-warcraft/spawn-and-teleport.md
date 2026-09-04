# Spawn & Teleport System

## Data Source

### playercreateinfo (AzerothCore)

WoW player spawn coordinates come from **AzerothCore** (`playercreateinfo` SQL table),
not from any MPQ/DBC file.  Blizzard hardcodes the `(race, class) → (map, x, y, z)`
mapping in wow.exe — neither `ChrRaces.dbc` nor `WorldSafeLocs.dbc` carries race/class
associations.  AzerothCore reverse-engineered these coordinates by packet-sniffing
the retail client during character creation.

We store the same data as `serverdata/playercreateinfo.csv`, compiled into
`build/generated/g_playercreateinfo.c` by `gen_serverdata_c.py`:
```c
typedef struct { DWORD race; DWORD cls; DWORD map; FLOAT x, y, z; FLOAT facing; } WOWSPAWNPOINT;
```

Credit: [AzerothCore](https://github.com/azerothcore/azerothcore-wotlk).

### Loading Pipeline

```text
CM_LoadMap("World/Maps/Azeroth/Azeroth.wdt")
  → CM_WowChooseSpawn(filename)
      1. CM_WowExtractMapName → "Azeroth"
      2. CM_WowFindMapId → map_id = 0 (Map.dbc lookup)
      3. CM_WowCollectWorldSafeLocs(map_id, ...)
         - reads WorldSafeLocs.dbc, filters by map_id
         - stores ALL entries (not capped at 16) in cm_wow_all_spawns[]
         - also populates world.info.players[0..15] for backwards compat
         - returns total count (e.g. 39 for Eastern Kingdoms)
```

### Public API (cmodel.h)

```c
DWORD      CM_WowGetAllSpawnCount(void);      // total entries for current map
LPCVECTOR3 CM_WowGetSpawnPos(DWORD index);    // position (x,y,z)
LPCSTR     CM_WowGetSpawnName(DWORD index);   // area name, caller must not free
```

Populated once during map load, null/malloc'd via MemAlloc.
Freed by `CM_WowFreeAllSpawns()` on next map load.
Game module uses these for spawn selection and the `respawn` command.

## Per-Race Spawn Selection

`wow_spawn_points` (generated from `playercreateinfo.csv`) is the compiled
AzerothCore `playercreateinfo` table. `Wow_PlayerCreateMap`
selects its numeric map before world loading; after the map is loaded,
`Wow_SelectSpawnPoint` requires the same map ID and returns the authored position.
There is no race-name or zone-name heuristic.

### Fast source-of-truth checklist

Do not repeat a broad search across WoWee, DBCs, and renderer code for this flow.
The ownership and implementation points are fixed:

| Question | Source of truth | Local implementation |
| --- | --- | --- |
| Which map/position does a race and class use? | AzerothCore `playercreateinfo` | `serverdata/playercreateinfo.csv` → `build/generated/g_playercreateinfo.c` |
| What directory belongs to numeric map ID N? | Client `DBFilesClient\Map.dbc`, fields 0 and 1 | `Com_WowMapPathForId` in `common/common.c` |
| Which map is currently loaded? | WDT path resolved through `Map.dbc` | `CM_WowGetMapId` in `common/world_wow.c` |
| What does Enter World request? | Selected-character cvars plus server table | `map playercreate` in `ui/menu_lua.c` |

Useful bounded checks:

```sh
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ dump 'DBFilesClient\Map.dbc' 2
make run-wow ARGS="+set wow_playerinfo '\race\Orc\sex\Male\class\1\appearance\0' +map playercreate +com_frame_limit 10"
make run-wow ARGS="+set wow_playerinfo '\race\Human\sex\Male\class\1\appearance\0' +map playercreate +com_frame_limit 10"
```

Expected evidence is `Kalimdor id=1` with Orc at `-618.5 -4251.7`, and
`Azeroth id=0` with Human at `-8950.0 -132.5`. If those disagree, inspect only
the four ownership points above before expanding the search.

## Race → Map Mismatch (root cause of "Orc spawns in Northshire")

`playercreateinfo.csv` is byte-for-byte identical to AzerothCore's
`playercreateinfo` (verified against `data/azerothcore-wotlk/data/sql/base/db_world/playercreateinfo.sql`):

| Race | map | zone | position |
|------|-----|------|----------|
| Orc / Troll | 1 (Kalimdor) | 14 (Durotar) | -618.518, -4251.67, 38.718 |
| Tauren | 1 (Kalimdor) | 215 (Mulgore) | -2917.58, -257.98, 52.9968 |
| NightElf | 1 (Kalimdor) | 141 (Teldrassil) | 10311.3, 832.463, 1326.41 |
| Human | 0 (EK) | 12 (Elwynn) | -8949.95, -132.493, 83.5312 |
| Dwarf / Gnome | 0 (EK) | 1 (Dun Morogh) | -6240.32, 331.033, 382.758 |
| Undead | 0 (EK) | 85 (Tirisfal) | 1676.71, 1678.31, 121.67 |

`Wow_SelectSpawnPoint` only matches when `wow_spawn_points[i].map == CM_WowGetMapId()`.
Previously every Make target loaded `World/Maps/Azeroth/Azeroth.wdt` (Eastern
Kingdoms, `Map.dbc` id 0), so Horde/Kalimdor entries (`map == 1`) never matched
and the player fell back to `CM_WowGetSpawnPos(0)` — the first WorldSafeLoc of
Eastern Kingdoms, i.e. **Northshire** (a human zone).

Kalimdor data (`World/Maps/Kalimdor/Kalimdor.wdt`) is present in `terrain.MPQ`.

The two halves deliberately stay data-driven:

- AzerothCore `playercreateinfo` supplies the race/class spawn's numeric `map`
  ID and coordinates.
- Client `DBFilesClient\\Map.dbc` field 0 supplies the same ID and field 1
  supplies its internal directory (`0 -> Azeroth`, `1 -> Kalimdor` in 1.12).

OpenWoW accepts numeric map arguments and resolves them through mounted `Map.dbc`:
`+map 1` becomes `World/Maps/Kalimdor/Kalimdor.wdt`. Character entry uses the
special `map playercreate` command. Its ownership chain is:

```text
UI selected character cvars
  -> game.PlayerCreateMap() / AzerothCore playercreateinfo
  -> numeric map ID
  -> client Map.dbc directory
  -> WDT path
```

The race-specific Make targets use `+map playercreate`. `build-run-wow-map`
intentionally uses `+map 1` as the explicit Kalimdor world-rendering fixture.

The first-login camera flyby is documented separately in [cinematics.md](cinematics.md).

### Fallback Spawn for Cross-Map Races

The `+map 0 +warp stormwind` workflow (see [area-triggers.md](area-triggers.md) for the
`+map N +warp X` pattern) breaks when the active character is an Orc: `Wow_SelectSpawnPoint`
returns `~0u` because the Orc's `playercreateinfo` entry is on map 1 (Kalimdor), not map 0.
Previously `Wow_SpawnEntities` returned false and `SV_Map` failed outright.

The fix is in `Wow_SpawnEntities` (`games/world-of-warcraft/game/g_wow.c`): when
`Wow_SelectSpawnPoint(race, class_id)` returns `~0u` **and**
`Wow_HasSpawnForMap(map_id)` returns true (the map has spawns for at least one
other race), the code calls `Wow_AnySpawnIndexForMap(map_id)` to obtain the first
available spawn index for any race on this map. `Wow_AnySpawnIndexForMap` is a
static helper in `g_wow.c` that iterates `Wow_SpawnCount()` entries and returns the
index of the first entry where `Wow_SpawnByIndex(i)->map == map_id`. The player
spawns at that position and a warning is logged:

```
WoW: race=%s class=%u has no spawn on map=%u; using fallback
```

The deferred `+warp` command then repositions the player to the intended destination.
`Wow_HasSpawnForMap` returning false (e.g. `+map 36` for a dungeon) still reaches
the areatrigger spawn path; that case is unchanged.

The test covering this path was renamed from
`wow_load_map_rejects_mismatched_playercreate_map` (expected failure) to
`wow_load_map_falls_back_on_mismatched_playercreate_map` (expected success).

## Entity Spawn Dispatch (why g_spawn.c is small)

`games/world-of-warcraft/game/g_spawn.c` is tiny compared to Quake 2's and
Warcraft III's `g_spawn.c`, and that is intentional. Q2 and WC3 are large
because both parse an authored entity lump from the map (BSP entity text /
`war3map.doo`) and dispatch each placement through a classname→spawn-function
table (`spawns[]` in Q2, `SP_CallSpawn` in WC3). WoW has no such lump: ADT
terrain and static doodads/WMOs are renderer-owned (never game entities), and
dynamic content arrives from AzerothCore CSVs + DBC lookups.

Instead of a classname table, each WoW spawner sets the entity's `think` pointer
directly — there is no central type dispatcher (see
[enemies-and-creatures.md](enemies-and-creatures.md)). That is fine at the
current five think-types, but if the entity taxonomy grows (triggers, doors,
varied NPC behaviour), a `SP_CallSpawn`-style dispatcher hub belongs in
`g_spawn.c`, mirroring WC3's `games/warcraft-3/game/g_spawn.c`. Not a bug — the
natural growth point.

## Server Commands

### `respawn`

Client command: `respawn` (forwarded to server via `Cmd_ForwardToServer`).

Server handler in `Wow_ClientCommand`:
1. Reads race from `wow_playerinfo` cvar (set by character creation UI)
2. Calls `Wow_SelectSpawnPoint` for the loaded map
3. Currently falls back to Orc Warrior if selection fails; this is known legacy
   debt and must not be copied into map loading or new commands
4. Teleports the player entity (`ent->s.origin`, `ent->client->ps.origin`)
5. Uses terrain height from `Wow_TerrainHeight` for Z coordinate

### `screenshot` (server → client)

The server can send `svc_gamecmd "screenshot <name>"` to the client via the
`screenshot` dispatcher in `CL_ParseGameCommand`.  The client writes a PNG
to `screenshots/wowee_<name>.png` using `glReadPixels` + `stb_write_png`.
See `client/cl_screenshot.c` for implementation.

No active server code sends this command (the tour was removed), but the
wire-format handler remains so it can be triggered from game code.

## Wire Format: svc_gamecmd

Server-to-client game commands use `svc_gamecmd` (enum value in `common.h`):

```
[byte svc_gamecmd] [string command] [short payload_size] [payload bytes]
```

Server writes via `SV_WriteGameCommand()` (`server/sv_send.c`), client
parses via `CL_ParseGameCommand()` (`client/cl_parse.c`).  Current handlers:

| Command | Handler | Purpose |
|---------|---------|---------|
| `lobby_setup` | `CL_ParseLobbySetup` | Lobby configuration |
| `lobby_chat` | `CL_ParseLobbyChat` | Chat relay |
| `screenshot` | `CL_WoweeScreenshot` | Capture framebuffer (WOW-only) |

New game code should add handlers to this dispatch chain rather than using
`Cbuf_AddText` for server→client communication.
