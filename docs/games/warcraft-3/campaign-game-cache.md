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

`disk` is the default: an explicit `SaveGameCache()` commits the
cache to the process snapshot and an OpenRealm sidecar in the engine-owned
script-save session sandbox so later maps in **the same session** can restore it.
The previous global sidecar location is not accessible to map scripts:
without isolation, an untrusted map could overwrite another campaign's
`Campaigns.w3v`. On a player-created named save, the engine checkpoints the
entire permitted live script namespace, including cache sidecars, into an
engine-owned immutable directory. A binding tied to that manual save's
fingerprint authorizes restoring a **fresh writable clone** of the checkpoint
after a restart or after loading an older save slot. Loading one slot can no
longer overwrite another slot's cached hero/quest data. JASS cannot select
checkpoint IDs or access binding files. Missing, stale, or invalid bindings
yield a new empty scope; the manual game snapshot may still load. See
[save/load](save-load.md#manual-save-script-checkpoints-patches-5-and-5a).

`memory` keeps the explicit save boundary but commits only to process memory,
so it cannot provide restart-safe game-cache checkpointing. `disabled` creates
empty new handles and does not commit campaign values, although handle-local
`Store*` and `GetStored*` continue to work.

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
Hero-derived stats, then restores health/mana and inventory. If a cached Hero's
current health is zero or below, OpenRealm restores that Hero with 25% of the
cached maximum health instead of recreating a zero-health living entity. Living
Heroes retain their exact cached current health, including values below 25%.

The snapshot deliberately does not claim to serialize transient simulation
objects such as buffs, cooldown timers, current orders, production/revival state,
projectile state, or trigger-local references. Those require separate saved-game/state contracts.

## Rexxar Stash: temporary carrier removal (Patch B)

The extracted OrcX01 script uses a hidden temporary `Obla` to carry the six
items from `h003` (Rexxar's Stash). It stores that unit under campaign
`Campaigns.w3v`, mission `OrcX02`, entry `Stash`, and removes the temporary
unit. `SaveGameCache()` commits the stored unit at the script's save boundary,
even if it occurs after `RemoveUnit()` has been requested. This is authored
campaign JASS: the engine must
not hardcode those keys or create an alternative storage container.

`G_GameCacheStoreUnit()` copies each item type and charges by slot into a
**value snapshot**. Neither `SaveGameCache()` nor `RestoreUnit()` requires the
source unit to remain alive after `StoreUnit()` returns. `RemoveUnit()` uses
`G_DeferFreeEdict()` to hide the unit immediately, then runs `G_FreeEdict()`
at the end-of-frame removal boundary. The latter now calls
`G_ReleaseInventoryOnUnitRemoval()` **after** the unit's `A_UNIT_REMOVE`
ability callbacks and before releasing its pooled state. Carried item entities
are removed, not left pointing at a recycled edict and not dropped into the
world; `RemoveUnit` is not equivalent to a unit dying with inventory `inv2`.
Previously transferred items owned by another unit are unaffected.

Script-detached items that still refer to the old carrier are retired too.
Consumed-item handles retained for pending JASS event responses remain alive
until those responses finish, but lose obsolete `pending_use_carrier` links;
finalization also checks the carrier's entity generation. No item or cache
serialization fields are added by this change.

**Headless verification:** `wc3_items.remove_unit_cleans_six_carried_items_without_world_drops`,
`wc3_items.remove_unit_preserves_items_already_transferred_elsewhere`,
`wc3_items.remove_unit_cleans_script_detached_item`, and
`wc3_api.rexxar_stash_cache_survives_hidden_carrier_removal`. The last test
uses a fixture `Hpal` hero with six inventory slots instead of the campaign's
`Obla`, which is not in the small test fixture. It exercises inventory handoff,
`StoreUnit`, `SaveGameCache`, deferred removal, and restoration through JASS.
It does **not** verify OrcX02 map-trigger ordering or the complete cross-map
script-save workspace, which still require campaign integration testing.

## Storage Modes and Persistence Format

The default `disk` mode loads the last committed sidecar in the active
script-save session namespace when `InitGameCache()` opens a campaign cache,
and writes a new committed sidecar only when map script calls
`SaveGameCache()`. Ordinary Hero XP changes, skill learning, `StoreUnit()`,
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
compatibility has not been established. Campaign selector unlock/completion state is a separate retail `.w3p`
concept and is persisted separately by OpenRealm in `campaign-progress.orcp`; see
[campaign-progress.md](campaign-progress.md).

For a JASS cache name such as:

```text
Campaigns.w3v
```

OpenRealm writes a versioned private sidecar through the engine-owned
`FS_ScriptCachePath()` policy:

```text
gamecache-Campaigns.w3v.orcgc
```

The game module does not link directly against `common` to call that resolver.
`server/game.h` exposes write resolution as `game_import.ScriptCachePath`,
provided by `FS_ScriptCachePath`, and read-only resolution as
`game_import.ScriptCacheReadPath`, provided by `FS_ScriptCacheReadPath`.
`InitGameCache()` uses the read-only resolver so opening an absent cache does
**not** create `campaigns/<playthrough-id>/live/cache/` or even a live
workspace. `SaveGameCache()` uses the creating resolver when attempting to
persist its sidecar. Both paths retain the same private path validation and
keep filesystem policy out of `libgame`.

The cache resides at `saves/campaigns/<playthrough-id>/live/cache/gamecache-Campaigns.w3v.orcgc`
under the normal per-user game directory (or existing portable fallback), not
beside general game configuration. The session ID is engine-generated and
never supplied by a map. Scripted save-copy and directory operations cannot
address the cache directory.

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

`ReloadGameCachesFromDisk` is registered and returns success. OpenRealm loads the
committed sidecar when `InitGameCache()` creates a handle, and the retail
campaign flow calls this native immediately before creating its cache handles.
Refreshing already-created cache handles mid-map is not implemented.

## Verification

`games/warcraft-3/game/tests/t_api.c` contains coverage for:

- typed scalar store/get/have/flush behavior;
- disabled mode keeping stores local to one handle while `SaveGameCache()`
  remains script-compatible and commits nowhere;
- `SaveGameCache()` committing a process-memory snapshot while unsaved handle
  mutations remain private;
- restoring a level-2 Paladin with Holy Light rank 1 and exactly one remaining
  Hero skill point;
- restoring a cached dead Hero at 25% of its cached maximum health so the fresh
  entity is alive and internally consistent.

For a retail-like campaign run, use the default `disk` mode: complete Human01 so
its script reaches `SaveGameCache()`, then load Human02. The carried state may be
restored when both maps run in the same play session. Restarting OpenRealm creates a fresh scope for a new game, but loading a
previously bound manual save reauthorizes its original committed cache
sidecar. In `disk` mode that sidecar is preferred over a process-memory
cache of the same name. In `memory` mode, committed cache memory survives
map changes within a single script scope, but never crosses a scope change
and does not survive application restart. Use
`+set wc3_gamecache_mode memory` when persistence should stop at process exit, or
`+set wc3_gamecache_mode disabled` when intentionally testing Human02's
cache-miss fallback. Starting Human02 without a previously committed Human01
cache is a legitimate miss in every mode.

## Named saves, stale zones and remaining verification

Manual saves bind the current private scope **after** a successful full-game
write; the `EVENT_GAME_SAVE` handler runs before that write so Rexxar can copy
zone snapshots to its named-save backup. Loading a named save selects its
binding **before** `SV_Map` and the subsequent `EVENT_GAME_LOADED` callback.
Scripted zone restores keep the current scope and dispatch
`EVENT_GAME_LOADED` with `MAP_RELOADED` temporarily set. Rexxar's transition
handler can import travelling Heroes, while its named-user-load directory-copy
handler remains gated off and cannot overwrite newer zone snapshots.

When `wc3_gamecache_mode=disk`, a new `InitGameCache` handle reads the latest
committed sidecar rather than blindly reusing an old process-memory handle.
A manually restored zone can also have preexisting JASS handles serialized
inside the older snapshot. The exact Rexxar `Import_All` sequence still needs
runtime verification to establish that those handles get reinitialized before
use; Patch 5 does not claim to rewrite serialized cache handles or guarantee
hero deduplication in every campaign script.

The binding's file fingerprint detects normal replacement or corruption of the
associated manual save; it is **not** a cryptographic authenticity signature
against an attacker with direct host-filesystem write access. The security
boundary against untrusted maps is the engine-only scope identity, private
binding directory, strict JASS path policy, and no-follow filesystem checks.

Named saves receive independent checkpoints of the permitted script-save
namespace, including committed game-cache sidecars. Loading an older save
branches from its checkpoint into a new writable scope. Runtime validation of
the campaign's serialized cache handles, `Import_All` ordering, and Hero
deduplication remains necessary before claiming full Rexxar return-visit
fidelity.

### Workspace storage policy

Campaign caches remain uncompressed in the active live workspace to preserve
`SaveGameCache`/`InitGameCache` semantics. Named-save checkpoints can share
cache blocks via reflinks on Linux, or compress them in their private storage
when CoW is unavailable. Loading a named save expands/copies those files into
a separate writable playthrough branch before map initialization. Older
uncompressed checkpoints remain readable. See [save/load](save-load.md#campaign-owned-live-workspaces-and-compact-snapshots).

### Rexxar stash handoff prerequisite

OrcX01 moves stash inventory items into a temporary hidden `Obla` carrier and
stores that carrier under the `Campaigns.w3v` / `OrcX02` / `Stash` key. The
generic `UnitAddItem` carried-item transfer now supports moving existing item
handles directly from the stash inventory to the carrier without a world drop.
The campaign-cache contents, commit boundary, temporary carrier removal and
OrcX02 restore semantics are independent of that handoff and are not modified
by the inventory-transfer patch.
