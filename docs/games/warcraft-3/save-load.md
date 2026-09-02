# Warcraft III Save/Load

## Contract

The WC3 game module owns save/load. `GetGameAPI()` exposes `SaveGame` and `LoadGame` callbacks through `server/game.h`; the JASS `SaveGame` and `LoadGame` natives use the same callbacks and resolve names through `gi.UserPath`.

`WriteGame()` writes the current game state to a versioned binary file. The file contains:

- `W3SV` magic, format version, `sizeof(edict_t)`, entity count, and client count;
- level frame/time and started/script-started flags;
- each client `GAMECLIENT` state, including its `PLAYER` state, JASS settings, researched tech, text storage, camera values, messages, and HUD caches;
- one used flag per entity slot and a raw `edict_t` block for used slots.

The current format is process-independent for entity relationships: `F_EDICT` fields are written as entity indexes and resolved back to `g_edicts[index]` by `ReadGame()`. Client pointers are restored from player slots after loading. Malformed headers, truncated records, and entity indexes reject the load; client pointers are never read from the file as addresses.

## Field Table

`games/warcraft-3/game/g_save.c` keeps the `field_t fields[]` table synchronized with `struct edict_s` in `g_local.h`.

`EDICTFIELD(x, type)` describes one scalar field with `array_size == 0`. `EDICTFIELD(x, type, count)` describes a contiguous array from the base offset; the serializer walks `count` elements using the field type's element size. For example, the six inventory pointers use `EDICTFIELD(inventory, F_EDICT, MAX_INVENTORY)` rather than six duplicate descriptors.

- Add every persistent `edict_t` entity pointer to `fields[]` as `F_EDICT`, including array elements and nested fields.
- Do not add process-owned pointers such as path textures, metadata rows, animations, movement callbacks, or function pointers. `WriteEdict()` clears those pointers and `ReadEdict()` rebinds class metadata; spatial links are rebuilt with `gi.LinkEntity`.
- When adding a new pointer or changing an existing edict field, update the table and the round-trip test together. A raw pointer omitted from the table can write an address into the save file.
- Keep the table sentinel `{ NULL, 0, 0, 0 }`; all serializer loops stop at `field->name == NULL`.

This follows the Quake 2 `g_save.c` pattern while avoiding Quake 2's old global pointer addresses and unbounded save stream.

## Native Usage

JASS save names are relative user-state names. Names containing `/` or `\\` are rejected. The engine's `FS_UserPath()` policy determines the writable directory.

```jass
call SaveGame("chapter-01.w3save")
call LoadGame("chapter-01.w3save", false)
```

`LoadGame` currently restores the loaded state in the already-loaded map; map selection and UI score-screen behavior remain separate work.

Unlike Quake 2's separate `WriteLevel()` path, this first WC3 save format currently stores level timing and lifecycle flags only. The active map, JASS VM, quests, fog grids, bot runtime, and other pointer-bearing level subsystems still need explicit save contracts before they can be included safely.

## Console Usage

The server registers Quake 2-style `savegame` and `loadgame` commands. Save names are relative to the writable user directory and cannot contain path separators:

```sh
build/bin/openwarcraft3 -data "data/Warcraft III" +map "Maps/(2)Rivercross.w3m" +loadgame chapter-01.w3save
```

The shipped WC3 config binds `F9` to `savegame quick`. The load command must run after `+map`, because loading restores state into the already-loaded map.

## Verification

Run the serializer round trip against both ROC and TFT test environments:

```sh
make test-wc3-engine WC3_PATTERN='wc3_save.*'
```

The test checks representative integer, float, vector, and entity fields, level timing flags, player resources, and mutable `GAMECLIENT` state. It also verifies restoration of the player edict's client pointer. ROC and TFT are both executed by the target.
