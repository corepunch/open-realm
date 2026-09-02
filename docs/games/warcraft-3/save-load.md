# Warcraft III Save/Load

## Contract

The WC3 game module owns save/load. `GetGameAPI()` exposes `SaveGame` and `LoadGame` callbacks through `server/game.h`; the JASS `SaveGame` and `LoadGame` natives use the same callbacks and resolve names through `gi.SavePath`.

`WriteGame()` writes the current game state to a versioned binary file. The file contains:

- `W3SV` magic, format version 6, `sizeof(edict_t)`, entity count, client count, script identity, and native-handle registry counts;
- level frame/time and started/script-started flags;
- each client `GAMECLIENT` state, including its `PLAYER` state, JASS settings, researched tech, text storage, camera values, messages, and HUD caches;
- each camera target as an entity index;
- the quest and quest-item graph's strings and status flags;
- one used flag per entity slot and a raw `edict_t` block for used slots;
- group membership, trigger enabled state, timer state, unread gameplay events, and a semantic JASS VM snapshot;
- a `W3OK` commit footer and FNV-1a checksum over the complete preceding payload.

`WriteGame()` removes the destination when any record or footer write fails. `ReadGame()` validates the commit footer, checksum, format, script identity, and quest/group/trigger/timer/event registry counts before mutating clients or entities. A truncated or rejected partial write therefore cannot become a loadable artifact or clear the live world.

The current format is process-independent for entity relationships: `F_EDICT` fields and camera targets are written as entity indexes and resolved back to `g_edicts[index]` by `ReadGame()`. Client pointers are restored from player slots, player names from inline JASS name storage, and map-player rows from the loaded map plus `PLAYER.number`. Malformed headers, truncated records, and entity indexes reject the load; client pointers are never read from the file as addresses.

Quest objects and items are restored in place so the running JASS VM's light handles keep their object identity. Loading rejects a quest or item count mismatch instead of leaving those handles dangling. This supports the normal `+map ... +load ...` path, where map initialization recreates the same quest graph before applying saved state.

Groups, triggers, timers, and events use deterministic creation ordinals. Group membership is stored as entity indexes. Trigger enabled state is restored in place. Timers preserve their handler name, duration, remaining time, periodic/paused/running flags, and resume relative to the load time. Timer callbacks and timer-expire trigger actions enter the normal coroutine queue and retain `GetExpiredTimer()` context.

The unread portion of the bounded gameplay event ring preserves event type, subject/source entity indexes, and the target registration ordinal. Consumed queue entries are not saved. Loads reject queue overflow and unresolved entity or registration IDs.

## JASS Snapshot

The embedded snapshot starts with `JSVM`, snapshot format version 1, a program-identity hash, mutable-global count, and sleeping-coroutine count. It stores:

- mutable scalar globals and sparse array entries;
- integer, real, boolean, string, code, null, and supported typed-handle values;
- shared-handle identity by stable native-domain ID;
- sleeping coroutine frames as function/block token ordinals, locals, operand stack values, wake delay, and event context.

The snapshot never writes parser pointers, dictionary links, refcount addresses, stack pointers, or `jmp_buf`. Code values and coroutine PCs resolve against the already-parsed program after the identity hash matches. Handles relocate through game-owned codecs for entities (`unit`, `widget`, `destructable`, `item`, `effect`), players, quests, quest items, events, triggers, groups, and timers. Unsupported non-null handle types reject the save with a diagnostic instead of writing an address or silently dropping the value.

Saving is allowed only at a VM safe point. `jass_writesnapshot()` rejects a request while a synchronous JASS call or coroutine is actively executing. Yielded `TriggerSleepAction` coroutines are safe and resume from their saved semantic PC after load.

## Field Table

`games/warcraft-3/game/g_save.c` keeps the `field_t fields[]` table synchronized with `struct edict_s` in `g_local.h`.

`EDICTFIELD(x, type)` describes one scalar field with `array_size == 0`. `EDICTFIELD(x, type, count)` describes a contiguous array from the base offset; the serializer walks `count` elements using the field type's element size. For example, the six inventory pointers use `EDICTFIELD(inventory, F_EDICT, MAX_INVENTORY)` rather than six duplicate descriptors.

- Add every persistent `edict_t` entity pointer to `fields[]` as `F_EDICT`, including array elements and nested fields.
- Do not add process-owned pointers such as path textures, metadata rows, animations, movement callbacks, or function pointers. `WriteEdict()` clears those pointers and `ReadEdict()` rebinds class metadata plus class-owned unit/destructable lifecycle callbacks; spatial links are rebuilt with `gi.LinkEntity`.
- When adding a new pointer or changing an existing edict field, update the table and the round-trip test together. A raw pointer omitted from the table can write an address into the save file.
- Keep the table sentinel `{ NULL, 0, 0, 0 }`; all serializer loops stop at `field->name == NULL`.

This follows the Quake 2 `g_save.c` pattern while avoiding Quake 2's old global pointer addresses and unbounded save stream.

## Native Usage

JASS save names are relative user-state names. Names containing `/` or `\\` are rejected. The engine's `FS_UserPath()` policy determines the writable directory.

```jass
call SaveGame("chapter-01.w3save")
call LoadGame("chapter-01.w3save", false)
```

`LoadGame` restores state in the already-loaded map; map selection and UI score-screen behavior remain separate work. The initialized map must have the same JASS program and deterministically recreated native registries. The native names resolve through `FS_SavePath()`.

The format does not yet snapshot fog grids, bot runtime, mutable event-registration fields, alliances, stock state, or cinematic filter. Client message storage is part of `GAMECLIENT`, but transient presentation lifetimes are not reconstructed. Unit/destructable lifecycle callbacks are restored from class data, preventing resumed orders from calling stale or null process addresses; arbitrary active `umove_t` actions are not yet restored and require stable semantic move IDs. Menu callbacks are code pointers and are reset on load; restoring an active targeting/build submenu likewise requires a semantic menu-state enum rather than raw function addresses.

The checksum and header preflight protect normal partial/corrupt-file and wrong-map failures before mutation. Record-level semantic validation later in the stream is not fully transactional; do not treat save files as untrusted input until native records are decoded into temporary state before commit.

## Console Usage

The server registers Quake 2-style `save` and `load` commands. Save names are relative to the writable save directory and cannot contain path separators:

```sh
build/bin/openwarcraft3 -data "data/Warcraft III" +map "Maps/(2)Rivercross.w3m" +wait +save chapter-01.w3save
```

Open the in-game console with the backtick/tilde key and enter `save chapter-01.w3save` or `load chapter-01.w3save`. The load command must run after `+map`, because loading restores state into the already-loaded map. For command-line save diagnostics, place `+wait` between the map and `+save` commands so map initialization and `main()` have created the authoritative native registries first. A load invocation is:

```sh
build/bin/openwarcraft3 -data "data/Warcraft III" +map "Maps/(2)Rivercross.w3m" +load chapter-01.w3save
```

`FS_SavePath()` resolves saves to `$XDG_DATA_HOME/warcraft-3/saves/<name>` on Linux, or `~/.local/share/warcraft-3/saves/<name>` when `XDG_DATA_HOME` is unset. macOS uses `~/Library/Application Support/warcraft-3/saves/<name>`, and Windows uses `%APPDATA%/warcraft-3/saves/<name>`. Config files use the same per-user game-data directory under `config/`. If no writable per-user data directory is available, config and saves fall back to `share/warcraft-3/config/` and `share/warcraft-3/saves/`. The save filename may not contain `/` or `\`.

The Save Game and Load Game buttons exposed by the WC3 menu layout are not wired yet; use the console commands. The shipped config currently binds `F9` to the quest log, not save/load.

## Verification

Run the serializer round trip against both ROC and TFT test environments:

```sh
make test-wc3-engine WC3_PATTERN='wc3_save.*'
```

The tests cover entity/client fields and pointer fixups, quests, mutable globals and sparse arrays, code values, handle aliases, group membership, trigger state, paused/periodic timers, direct and trigger timer callbacks, `GetExpiredTimer`, sleeping-coroutine resume, checksum rejection, script mismatch, and native-registry mismatch. Corruption and preflight tests assert that rejection leaves representative live entity state unchanged. ROC and TFT are both executed by the target.
