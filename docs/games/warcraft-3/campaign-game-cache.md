# Warcraft III Campaign Game Cache

## Contract

Warcraft III campaign maps use the JASS `gamecache` natives to carry state from
one map to another. The cache is keyed by:

```text
campaign file
  -> mission key
      -> entry key + value type
```

Retail Warcraft III campaign caches persist state only when map script crosses
the explicit `SaveGameCache()` boundary. OpenRealm follows the same lifecycle:
`Store*()` mutates only the current JASS handle, while `SaveGameCache()` commits
that handle according to `wc3_gamecache_mode`.

OpenRealm supports three modes:

```text
wc3_gamecache_mode disk       (default)
wc3_gamecache_mode memory
wc3_gamecache_mode disabled
```

`disk` is the retail-like default: an explicit `SaveGameCache()` commits the
cache to the process snapshot and the OpenRealm sidecar so later maps and later
processes can restore it. `memory` keeps the same explicit save boundary but
commits only to process memory. `disabled` gives every `InitGameCache()` call a
fresh empty handle; handle-local `Store*()`/`GetStored*()` still work and
`SaveGameCache()` returns success, but commits nowhere.

This distinction matters for the Reign of Chaos Human campaign. Human02 first
tries to restore the carried-over Arthas. If that data is absent, its map script
uses a fallback level-2 Arthas and spends the fallback Hero skill points. Before
game-cache support, OpenRealm's `RestoreUnit()` always returned null, so the map
could only take that fallback path. With the default disk mode, a normal Human01
completion that executes `SaveGameCache()` preserves the Hero state for Human02.

Directly launching Human02 without first producing the campaign cache is still a
cache miss by design; the map's own fallback remains authoritative in that case.

## Runtime Ownership

The implementation lives in:

- `games/warcraft-3/game/g_gamecache.c` — cache storage, persistence, unit snapshots;
- `games/warcraft-3/game/api/api_misc.h` — JASS native adapters;
- `games/warcraft-3/game/g_local.h` — bounded runtime representation.

`ggamecache_t` is the JASS handle payload and aliases `gameCache_t`. Each cache is
bounded to `MAX_GAMECACHE_ENTRIES`; exhaustion is reported to `stderr` rather
than overwriting another key. Saved in-memory campaign snapshots are also
bounded (`MAX_GAMECACHE_MEMORY_CACHES`) so map transitions do not create an
unbounded process-lifetime allocation.

The typed key is significant. Integer, real, boolean, string, and unit values
with the same mission/key pair are independent so the matching `HaveStored*`,
`GetStored*`, and `FlushStored*` native operates on its own type.

## Stored Unit State

`StoreUnit()` snapshots the gameplay state OpenRealm currently owns and can
restore deterministically:

- unit rawcode;
- Hero level, XP, attributes, XP-suspension flag, and unspent skill points;
- learned Hero ability rawcodes and ranks;
- current/max health and mana;
- explicit unit colour override;
- inventory item rawcodes and charges by slot.

`RestoreUnit()` creates a fresh unit for the `forWhichPlayer` argument at the
requested position/facing, restores Hero progression and learned ranks, rebuilds
Hero-derived stats, then restores health/mana and inventory.

The snapshot deliberately does not claim to serialize transient simulation
objects such as buffs, cooldown timers, current orders, production/revival state,
projectile state, or trigger-local references. Those require separate saved-game/state contracts.

## Storage Modes and Persistence Format

The default `disk` mode loads the last committed sidecar when `InitGameCache()`
opens a campaign cache and writes a new committed sidecar only when map script
calls `SaveGameCache()`. Ordinary Hero XP changes, skill learning, `StoreUnit()`,
and other `Store*()` calls do **not** write to disk by themselves. Unsaved handle
mutations remain private until the explicit save call, matching the retail
campaign-script lifecycle.

Set:

```text
+set wc3_gamecache_mode memory
```

to retain the same explicit `SaveGameCache()` boundary while keeping committed
state only in process memory. This survives map transitions in one OpenRealm
process but not a restart.

Set:

```text
+set wc3_gamecache_mode disabled
```

to force every `InitGameCache()` to start empty. Stores remain usable inside the
current handle and `SaveGameCache()` remains script-compatible, but no committed
memory snapshot or disk sidecar is produced. This is useful when testing a
campaign map's authored cache-miss fallback directly.

Unknown mode values are reported and treated as `disk`, so a typo does not
silently disable campaign persistence.

OpenRealm does **not** write a fake retail `.w3v` file. Retail game-cache binary
compatibility has not been established.

For a JASS cache name such as:

```text
Campaigns.w3v
```

OpenRealm writes a versioned private sidecar through the engine-owned
`FS_UserPath()` policy:

```text
gamecache-Campaigns.w3v.orcgc
```

The game module does not link directly against `common` to call that resolver.
`server/game.h` exposes it as `game_import.UserPath`, and `SV_InitGameProgs()`
provides `FS_UserPath`. This keeps writable-path ownership on the engine side and
avoids unresolved engine symbols in `libgame`.

This places writable campaign state under the normal per-game user directory
(`$XDG_DATA_HOME/warcraft-3/` on Unix when set to an absolute path, otherwise `~/.local/share/warcraft-3/`, with the existing portable `share/warcraft-3/` fallback when no writable per-user directory is available).

The sidecar uses an explicit little-endian, length-prefixed format with the
`ORGCACHE` magic and version `1`. Writes go to a temporary file and are renamed
into place after the complete cache has been written, so a failed write does not
silently leave a partially updated cache.

Campaign names are reduced to their basename and unsafe filename characters are
replaced when constructing the private sidecar path. The JASS-visible campaign
name itself is preserved unchanged in memory.

## Native Coverage

Implemented campaign-cache operations:

- `InitGameCache`
- `SaveGameCache`
- `StoreInteger`, `StoreReal`, `StoreBoolean`, `StoreString`, `StoreUnit`
- `HaveStoredInteger`, `HaveStoredReal`, `HaveStoredBoolean`, `HaveStoredString`, `HaveStoredUnit`
- `GetStoredInteger`, `GetStoredReal`, `GetStoredBoolean`, `GetStoredString`
- `RestoreUnit`
- `FlushGameCache`, `FlushStoredMission`
- all typed `FlushStored*` operations

`SyncStored*` currently consumes/validates its arguments but is a no-op. That is
sufficient for the local single-player campaign path; multiplayer synchronization
semantics remain separate work.

`ReloadGameCachesFromDisk` is still outside the registered native surface.

## Verification

`games/warcraft-3/game/tests/t_api.c` contains coverage for:

- typed scalar store/get/have/flush behavior;
- disabled mode keeping stores local to one handle while `SaveGameCache()`
  remains script-compatible and commits nowhere;
- `SaveGameCache()` committing a process-memory snapshot while unsaved handle
  mutations remain private;
- restoring a level-2 Paladin with Holy Light rank 1 and exactly one remaining
  Hero skill point.

For a retail-like campaign run, use the default `disk` mode: complete Human01 so
its script reaches `SaveGameCache()`, then load Human02. The carried state may be
restored in the same process or after restarting OpenRealm. Use
`+set wc3_gamecache_mode memory` when persistence should stop at process exit, or
`+set wc3_gamecache_mode disabled` when intentionally testing Human02's
cache-miss fallback. Starting Human02 without a previously committed Human01
cache is a legitimate miss in every mode.
