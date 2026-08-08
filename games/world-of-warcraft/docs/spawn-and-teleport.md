# Spawn & Teleport System

## Data Source

### playercreateinfo (AzerothCore)

WoW player spawn coordinates come from **AzerothCore** (`playercreateinfo` SQL table),
not from any MPQ/DBC file.  Blizzard hardcodes the `(race, class) → (map, x, y, z)`
mapping in wow.exe — neither `ChrRaces.dbc` nor `WorldSafeLocs.dbc` carries race/class
associations.  AzerothCore reverse-engineered these coordinates by packet-sniffing
the retail client during character creation.

We compile the same data into `g_spawn.c` as a C table:
```c
typedef struct { DWORD race, cls, map; FLOAT x, y, z, facing; } player_create_info_t;
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

### Current approach (hardcoded table)

`Wow_RaceZone()` in `g_wow.c` maps 9 race strings to 6 zone substrings:

| Race(s) | Zone Substring |
|---------|---------------|
| Human | `"Northshire"` |
| Dwarf, Gnome | `"Coldridge Valley"` |
| NightElf | `"Shadowglen"` |
| Orc, Troll | `"Valley of Trials"` |
| Undead, Scourge | `"Deathknell"` |
| Tauren | `"Camp Narache"` |

`Wow_SelectRaceSpawnPoint()` then iterates ALL WorldSafeLocs entries (via
`CM_WowGetAllSpawnCount()`), uses `strcasestr` to match the area name against
the zone substring, and picks a random match.  Returns `~0u` (sentinel) if
no match — caller falls through to `Wow_SelectRandomSpawnPoint()`.

`Wow_SelectRandomSpawnPoint()` picks a random entry with distance-based
avoidance: it excludes the two closest spawns to existing players to spread
multiplayer starts.

### Future: ChrRaces.dbc

The TODO at `g_wow.c:1669` notes that this should be driven from
`ChrRaces.dbc` rather than hardcoded strings.  The DBC contains per-race
start coordinates and map/zone IDs.  The UI module already loads this DBC
for character creation (`ui_dbc.c`), but the game module does not currently
read it.

See `docs/references.md` for the WoWee spawn profile patterns that would
provide richer per-race-per-class spawn data.

## Server Commands

### `respawn`

Client command: `respawn` (forwarded to server via `Cmd_ForwardToServer`).

Server handler in `Wow_ClientCommand`:
1. Reads race from `wow_playerinfo` cvar (set by character creation UI)
2. Calls `Wow_SelectRaceSpawnPoint` to pick a race-appropriate location
3. Falls back to `Wow_SelectRandomSpawnPoint` if race match fails
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
