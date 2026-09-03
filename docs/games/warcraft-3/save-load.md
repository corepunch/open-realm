# Warcraft III Save/Load

## Contract

The WC3 game module owns save/load. `GetGameAPI()` exposes `SaveGame` and `LoadGame` callbacks through `server/game.h`; the JASS `SaveGame` and `LoadGame` natives use the same callbacks and resolve names through `gi.SavePath`.

`WriteGame()` writes the current game state to a versioned binary file. The file contains:

- `W3SV` magic, format version 2, canonical map path, `sizeof(edict_t)`, entity count, client count, script identity, and native-handle registry counts;
- level frame/time, authoritative Warcraft time-of-day state, and started/script-started flags;
- each client `GAMECLIENT` state, including its `PLAYER` state, JASS settings, runtime removed/result-presentation state, researched tech, text storage, camera values, messages, and HUD caches;
- each camera target as an entity index;
- the quest and quest-item graph's strings and status flags;
- the fixed point-order waypoint edict ring and its circular allocation cursor;
- one used flag per entity slot and a raw `edict_t` block for used slots;
- group membership, trigger enabled state, timer state, unread gameplay events, and a semantic JASS VM snapshot;
- a `W3OK` commit footer and FNV-1a checksum over the complete preceding payload.

`WriteGame()` removes the destination when any record or footer write fails. `ReadGame()` validates the commit footer, checksum, format, script identity, and quest/group/trigger/timer/event registry counts before mutating clients or entities. A truncated or rejected partial write therefore cannot become a loadable artifact or clear the live world. A header mismatch names the failing field and prints saved versus live counts; do not treat a generic `header mismatch` line as complete.

The version 2 layout retains the authoritative `level.timeofday` record and game-state event condition fields (`state`, `limitop`, `limitval`) from version 1, and adds the client removal/pending-result fields used by victory/defeat presentation.

Groups, timers, triggers, and event handlers may grow after `main()`. The header accepts a save that has *at least* as many of those objects as the freshly initialized map, then `RestoreRegistrySlots()` allocates the extras. A live count higher than the save still rejects. Quests remain an exact match because they are restored in place.

The current format is process-independent for entity relationships: `F_EDICT` fields and camera targets are written as entity indexes and resolved back to `g_edicts[index]` by `ReadGame()`. Before raw edict records replace the freshly loaded map baseline, `ReadGame()` clears the baseline spatial tree and then links each restored entity exactly once. Client pointers are restored from player slots, player names from inline JASS name storage, and map-player rows from the loaded map plus `PLAYER.number`. Malformed headers, truncated records, and entity indexes reject the load; client pointers are never read from the file as addresses.

Quest objects and items are restored in place so the running JASS VM's light handles keep their object identity. Loading rejects a quest or item count mismatch instead of leaving those handles dangling. Loading completely reloads the saved map first, then applies state.

Groups, triggers, timers, and events use deterministic creation ordinals. Group membership is stored as entity indexes. Each trigger stores its disabled flag plus action/condition function names so a trigger created after `main()` still has its callbacks after load. Timers preserve their handler name, duration, remaining time, periodic/paused/running flags, and resume relative to the load time. Timer callbacks and timer-expire trigger actions enter the normal coroutine queue and retain `GetExpiredTimer()` context.

Event handler registrations store type, subject entity index, trigger index, timer index, region, range, game-state ID, `limitop`, and limit value. The extra condition fields preserve `TriggerRegisterGameStateEvent` time-of-day registrations across save/load. The unread portion of the bounded gameplay event ring preserves event type, subject/source entity indexes, and the target registration ordinal. Consumed queue entries are not saved. Loads reject queue overflow and unresolved entity or registration IDs.

## JASS Snapshot

The embedded snapshot starts with `JSVM`, snapshot format version 2, a program-identity hash, mutable-global count, and sleeping-coroutine count. It stores:

- mutable scalar globals and sparse array entries;
- integer, real, boolean, string, code, null, and supported typed-handle values;
- shared-handle identity by stable native-domain ID;
- VM-owned handle identity and bounded payloads for sounds, camera setups, rects, locations, forces, game caches, regions, fog modifiers, and converted value objects;
- `boolexpr`, `conditionfunc`, and `filterfunc` handles by semantic JASS function name;
- sleeping coroutine frames as function/block token ordinals, locals, operand stack values, wake delay, and event context.

The snapshot never writes parser pointers, dictionary links, refcount addresses, stack pointers, or `jmp_buf`. Code values and coroutine PCs resolve against the already-parsed program after the identity hash matches. Handles relocate through game-owned codecs for entities (`unit`, `widget`, `destructable`, `item`, `effect`), players, quests, quest items, events, triggers, groups, and timers. Safe VM-owned handles serialize their payload and snapshot-local identity so aliases remain aliases after load. Unsupported non-null handle types reject the save with a diagnostic instead of writing an address or silently dropping the value.

Handle encoding dispatches value handles, VM-owned payloads, and function handles before consulting the game host. A failed host lookup means null only for host-owned native domains, such as a removed unit; applying that rule to VM-owned handles would silently replace valid sounds, camera setups, rects, locations, forces, and game caches with null.

Point-order waypoints follow Quake II's body-queue/TRAIL pattern: 256 classless, collisionless, `SVF_NOCLIENT` edicts are reserved before map entities and recycled as a ring. Its base, count, and cursor live in serialized level state. Consequently `goalentity` and other waypoint references use the ordinary `F_EDICT` index relocation path; there is only one entity pointer address domain.

Saving is allowed only at a VM safe point. `jass_writesnapshot()` rejects a request while a synchronous JASS call or coroutine is actively executing. Yielded `TriggerSleepAction` coroutines are safe and resume from their saved semantic PC after load.

## Field Table

`games/warcraft-3/game/g_save.c` keeps the `field_t fields[]` table synchronized with `struct edict_s` in `g_local.h`. Fixed-size
`edict_t` and `GAMECLIENT` records are still copied as one block; the adjacent `runtime_fields[]` and
`client_runtime_fields[]` tables describe the process-owned bytes that must be zeroed before that copy. This keeps the
common path memcpy-shaped while making pointer exceptions declarative rather than a hand-maintained assignment list.

`EDICTFIELD(x, type)` describes one scalar field with `array_size == 0`. `EDICTFIELD(x, type, count)` describes a contiguous array from the base offset; the serializer walks `count` elements using the field type's element size. For example, the six inventory pointers use `EDICTFIELD(inventory, F_EDICT, MAX_INVENTORY)` rather than six duplicate descriptors.

- Add every persistent `edict_t` entity pointer to `fields[]` as `F_EDICT`, including array elements and nested fields.
- Persistent movement defaults are part of that rule: Attack-Move/Patrol waypoints and `movement.follow_target` must be encoded as entity indexes rather than raw pointers.
- Do not add process-owned pointers such as path textures, metadata rows, animations, movement callbacks, or function pointers. `WriteEdict()` clears those pointers and `ReadEdict()` rebinds class metadata plus class-owned unit/destructable lifecycle callbacks; spatial links are rebuilt with `gi.LinkEntity`.
- Add process-owned edict or client pointers/callbacks to the corresponding runtime-field table so the fixed record copy cannot write an address into the save file.
- When adding a new pointer or changing an existing edict field, update the table and the round-trip test together. A raw pointer omitted from the table can write an address into the save file.
- Keep the table sentinel `{ NULL, 0, 0, 0 }`; all serializer loops stop at `field->name == NULL`.

This follows the Quake 2 `g_save.c` pattern while avoiding Quake 2's old global pointer addresses and unbounded save stream.

## Native Usage

JASS save names are relative user-state names. Names containing `/` or `\\` are rejected. The engine's `FS_SavePath()` policy determines the writable directory.

```jass
call SaveGame("chapter-01")
call LoadGame("chapter-01", false)
```

`LoadGame` completely reloads the saved map before restoring state. The initialized map must have the same JASS program. `main()` recreates the baseline native registries; objects created later in gameplay are allocated from the save and filled from trigger/event/timer records. The native names resolve through `FS_SavePath()`.

## Quake 2 Lifecycle

`data/Quake-2-master` is the reference. WC3 cannot copy the files 1:1 (one map, embedded JASS instead of cross-level `game.ssv`), but the steps must stay in Q2 order.

| Q2 | This engine |
| --- | --- |
| `SV_Savegame_f` → `WriteServerFile` / `WriteGame` / `WriteLevel` | `save` → `ge->SaveGame` / `WriteGame` |
| `SV_Loadgame_f` → `ReadServerFile` / `ReadGame` | save header supplies the map path |
| `CL_Changing_f`: plaque + `ca_connected` | `CL_BeginLoadingMap` + `CL_RestartRefresh` |
| `SV_Map(..., loadgame)` → `SpawnEntities`, two `RunFrame`s, `SV_CreateBaseline` | `SV_Map` → `G_LoadMap` / `G_StartScripts` (`main()`) |
| `SV_CheckForSavegame`: `SV_ClearWorld` then `ReadLevel` | `SV_LoadGame` calls `ReadGame` immediately (`gi.ClearWorld` then edicts) |
| `SV_Map` ends with `reconnect`; already-connected client sends `new` | `SV_Map` sends loopback `client_connect`; do **not** `CL_Connect` |
| `CL_ParseServerData` → `CL_ClearState` zeros `refresh_prepped` and `image_precache` | `CL_RestartRefresh` zeros prepped flags plus `cl.pics` / `cl.fonts` / `cl.models` because same-map load never changes `CS_WORLD` |

Q2 `ReadLevel` states the contract we hit: SpawnEntities has already run the same way as at save time, the server has cleared world links, then edicts are overwritten and `linkentity` rebuilds the tree. Skipping `ClearWorld` left the baseline area lists pointing at the same edict addresses and hung in `SV_AreaEdicts_r`. Calling `CL_Connect` after that `client_connect` wiped the client netchan and raced the handshake — Q2 never does that on load.

`F_EDICT` / `F_CLIENT` still convert pointers to indexes only at the save boundary. Q2 `F_FUNCTION` stored a process-relative code offset; we store JASS function names instead.

## JASS Handles Stay Pointers at Runtime

Do not replace JASS VM `HANDLE` / `LPCJASSFUNC` / `LPEDICT` fields with integers. Q2 keeps `edict_t *` and `think` pointers in memory and remaps them in `WriteField1` / `ReadField`. The VM should do the same.

- Host-owned natives (`unit`, `widget`, `item`, `player`, `quest`, `trigger`, `group`, `timer`, `event`) already snapshot as stable ordinals through `G_SaveJassHandle` / `G_LoadJassHandle`.
- VM-owned payloads (sounds, rects, locations, forces, game caches) snapshot identity plus bytes.
- `code` / trigger actions snapshot as function names, the analog of Q2 `F_FUNCTION` without a relocated code segment.

Integer handle tables inside the interpreter would duplicate that field table, break light-handle aliasing, and still need a remap step on load. When a new pointer appears on `edict_t` or a native object, add a field/codec entry; do not change the VM's in-memory representation.

HUD FDF trees cache `CS_IMAGES` / `CS_FONTS` slots. Those tables die with `memset(&sv)` in `SV_Map`. `G_LoadMap` memsets the single `hud` accumulator and clears the FDF pool; serialize then re-`ImageIndex`es from names. See [HUD Media Lifetime](hud-media.md).

The format does not yet snapshot fog grids, bot runtime, alliances, stock state, or cinematic filter. Client message storage is part of `GAMECLIENT`, but transient presentation lifetimes are not reconstructed. Unit/destructable lifecycle callbacks are restored from class data, preventing resumed orders from calling stale or null process addresses; arbitrary active `umove_t` actions are not yet restored and require stable semantic move IDs. Menu callbacks are code pointers and are reset on load; restoring an active targeting/build submenu likewise requires a semantic menu-state enum rather than raw function addresses. There is no backwards-compatible reader for v9, v8, or earlier saves.

The checksum and header preflight protect normal partial/corrupt-file and wrong-map failures before mutation. Record-level semantic validation later in the stream is not fully transactional; do not treat save files as untrusted input until native records are decoded into temporary state before commit.

## Console Usage

The server registers Quake 2-style `save` and `load` commands. Save names are relative to the writable save directory and cannot contain path separators:

```sh
build/bin/openwarcraft3 -data "data/Warcraft III" +map "Maps/(2)Rivercross.w3m" +wait +save chapter-01
```

Open the in-game console with the backtick/tilde key and enter `save chapter-01` or `load chapter-01`. For command-line save diagnostics, place `+wait` between the map and `+save` commands so map initialization and `main()` have created the authoritative native registries first. A load invocation can omit `+map`:

```sh
build/bin/openwarcraft3 -data "data/Warcraft III" +load chapter-01
```

`FS_SavePath()` resolves saves to `~/.local/share/warcraft-3/saves/<name>.sav` on Unix, including macOS, and `%APPDATA%/warcraft-3/saves/<name>.sav` on Windows. This is a fixed `~/.local/share` path: `$XDG_DATA_HOME` is not read, and macOS does not use `~/Library/Application Support`. Config files use the same per-user game-data directory. If no writable per-user data directory is available, config and saves fall back to `share/warcraft-3/config/` and `share/warcraft-3/saves/`. The save filename may not contain `/` or `\`.

The Save Game and Load Game buttons exposed by the WC3 menu layout issue `save quick` and `load quick`. The shipped config binds `F6` to `save quick`; `F9` remains the quest log shortcut.

## Verification

Run the serializer round trip against both ROC and TFT test environments:

```sh
make test-wc3-engine WC3_PATTERN='wc3_save.*'
```

The tests cover entity/client fields and pointer fixups, waypoint targets, quests, mutable globals and sparse arrays, code values, native and VM-owned handle aliases/payloads, function handles, group membership, trigger state, paused/periodic timers, direct and trigger timer callbacks, `GetExpiredTimer`, sleeping-coroutine resume, checksum rejection, script mismatch, native-registry mismatch, and triggers/events created after `main()`. Corruption and preflight tests assert that rejection leaves representative live entity state unchanged. ROC and TFT are both executed by the target.

A listen-server Human02 save/load is not covered by dedicated `+test`. Confirm it with a command-buffer replay that reaches `ca_active` after `load quick`:

```sh
# wait-lines omitted; keep enough frames for main() plus a few seconds of play
build/bin/openwarcraft3 -data "data/Warcraft III" -roc -com_fast_forward \
  +set vid_hidden 1 +set r_norefresh 1 \
  +map "Maps/Campaign/Human02.w3m" +exec /tmp/save-load.cfg +com_frame_limit 250
```

The load succeeded when the log contains `WC3 LoadGame: restored` and a later `CL_SendBegin` for the same map, and does **not** contain `header mismatch` or `restoring map baseline`. Repeat with `-tft`.
