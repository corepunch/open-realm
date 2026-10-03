# Warcraft III pooled entity state

## Ownership and access

Sparse lifecycle records live in `game/g_pool.c` and attach directly to `edict_t` pointers. Types have no edict/game prefix: `ancientRoot_t`, `goldMine_t`, `cargo_t`, `channel_t`, etc. Allocate explicitly at the owning lifecycle entry point with `G_AllocAncientRoot()` or the corresponding PascalCase allocator. Reads never allocate and never substitute a shared zero object.

```c
if (!ent->ancient_root)
    ent->ancient_root = G_AllocAncientRoot();
/* The owned transition requires this record. */
assert(ent->ancient_root);
ent->ancient_root->mode = ANCIENT_ROOTING;
```

A dispatch or classification query may legitimately examine an entity without that behavior. Check its pointer there, as with `ent->client` in Quake II. Inside the owning behavior, assert required state and access it directly. Do not silently return from a required transition because initialization was missed. Standard C assertions print the failed expression, source file and line in assertion-enabled builds; disabling assertions does not add recovery behavior.

The reference is id Software's [Quake II edict definition](https://github.com/id-Software/Quake-2/blob/master/game/g_local.h) and [client code](https://github.com/id-Software/Quake-2/blob/master/game/p_client.c): `edict_t` stores `struct gclient_s *client`, and gameplay uses `ent->client->...` or `client = ent->client` directly. Allocation and attachment are separate lifecycle operations. Warcraft's sparse pools use that access pattern; Quake II itself preallocates its client array.

## Lifetime

Each pool currently has 2,048 slots, with slot zero reserved: 2,047 simultaneous records per lifecycle. Allocation zeroes the selected record. Exhaustion logs the pool name and fails through `gi.error`; it never provides a shared writable fallback. `G_FreeAncientRoot(ent)` releases that record and clears its member. `G_PoolsReleaseEdict()` releases every attached record during entity teardown. Game initialization and map replacement reset all pools and detach all edict pool pointers.

Item and destructable state attaches during their spawn routines. Optional ability state attaches when its owner activates or initializes its lifecycle. General movement, combat, HUD and AI queries must account for absent optional state without turning every entity into an owner.

## Save/load and verification

Save format 62 persists the complete records of all 27 pools. Only pointer fixups and process-owned exclusions need descriptors; unlisted scalar state still persists. Records retain their owning entity identity, not pool-slot indexes or process addresses. See [save/load](save-load.md).

`wc3_pools.release_reuses_zeroed_owned_state` checks initial absence, release, reuse and reset. `wc3_save.all_sparse_pools_restore_records_and_entity_references` checks all pool attachments, absent state on another entity, scalar state and entity references. Existing ability, combat, construction, mining, item, cargo and save tests exercise the lifecycle entry points in both ROC and TFT modes.

```sh
make test-wc3-engine
make test
```
