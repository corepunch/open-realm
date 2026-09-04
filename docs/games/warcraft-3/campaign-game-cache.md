# Warcraft III Campaign Game Cache

## Contract

Warcraft III campaign maps use the JASS `gamecache` natives to carry state from
one map to another. The cache is keyed by:

```text
campaign file
  -> mission key
      -> entry key + value type
```

The campaign cache is persistent across map loads, not level-local state.
`InitGameCache()` therefore creates a JASS handle and loads the last cache state
committed by `SaveGameCache()`. `Store*()` mutates only that handle until the
explicit save boundary is crossed.

OpenRealm controls where the committed cache lives with:

```text
wc3_gamecache_mode memory   (default)
wc3_gamecache_mode disk
```

`memory` keeps committed game caches in process memory only. It survives map
transitions in the same OpenRealm process but performs no game-cache disk read
or write, so restarting OpenRealm starts with an empty cache. `disk` uses the
same process-memory snapshot and additionally reads/writes the OpenRealm sidecar
so campaign state survives a process restart.

This distinction matters for the Reign of Chaos Human campaign. Human02 first
tries to restore the carried-over Arthas. If that data is absent, its map script
uses a fallback level-2 Arthas and spends the fallback Hero skill points. Before
game-cache support, OpenRealm's `RestoreUnit()` always returned null, so the map
could only take that fallback path. A normal campaign run must instead preserve
the Human01 Hero state and let Human02 restore it.

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

The default `memory` mode never opens the sidecar path. `SaveGameCache()` copies
the current handle into the process-level campaign snapshot, and a later
`InitGameCache()` with the same campaign file copies that committed snapshot
into the new JASS handle. Unsaved `Store*()` changes are not visible to a new
handle.

Set:

```text
+set wc3_gamecache_mode disk
```

to make `SaveGameCache()` persist committed state to disk and allow
`InitGameCache()` to seed the process snapshot from a previous run. An invalid
mode is reported and treated as `memory`; it is not silently interpreted as a
disk mode.

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
- `SaveGameCache()` committing a process-memory snapshot while unsaved handle
  mutations remain private;
- restoring a level-2 Paladin with Holy Light rank 1 and exactly one remaining
  Hero skill point.

For Human campaign verification in the default mode, play Human01 and transition
to Human02 without restarting OpenRealm. For persistence across separate
processes, launch with `+set wc3_gamecache_mode disk`, save Human01 using the
same writable OpenRealm profile, then load Human02 with the same setting.
Starting Human02 directly without a committed Human01 cache intentionally
exercises the map's fallback path, not campaign carry-over.
