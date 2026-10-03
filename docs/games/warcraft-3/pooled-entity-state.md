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

Pools have 2,048 slots (2,047 usable, slot zero reserved), except the destructable pool at 8,192: every map-placed destructable owns a persistent record, and retail campaign maps peak near 5,000 (NightElf04, UndeadX01). All other pools peak at 256 or fewer. Each game shutdown logs `WC3 pool peak <name>=N/cap` for pools that were used; the map audit logs retain it, so rerun `make audit-wc3-maps` after adding a pool or raising content density. Allocation zeroes the selected record. Exhaustion logs the pool name and fails through `gi.error`; it never provides a shared writable fallback. `G_FreeAncientRoot(ent)` releases that record and clears its member. `G_PoolsReleaseEdict()` releases every attached record during entity teardown. Game initialization and map replacement reset all pools and detach all edict pool pointers.

Construction state exists only while construction is in progress, including paused Human construction. Completion and stop release it after worker cleanup; `ent->construction != NULL` is the active-state check. There is no separate `active` flag.

Item and destructable state attaches during their spawn routines. Optional ability state attaches when its owner activates or initializes its lifecycle. General movement, combat, HUD and AI queries must account for absent optional state without turning every entity into an owner.

## Presence and behavior state

The construction, Sacrifice and Polymorph records use pointer presence as their active-state contract. Sacrifice exists on a queued result until completion/cancellation. Polymorph exists until removal or death; death releases the inverse record without restoring the original presentation. Reapplying Polymorph preserves the first application's original model, scale and movement speed. A destructable record attaches at spawn, so no separate `initialized` flag is needed.

Some state must remain independent of record presence:

| Record | Why presence does not mean the behavior is active |
|--------|-------------------------------------------------|
| Shadow Meld | Tracks fading and the Hide order before the unit becomes invisible. |
| Militia | Tracks an approach to a Hall before transformation, and the return pairing order. |
| Way Gate | Keeps its destination while disabled; destination assignment and activation are independent JASS operations. |
| Channel | Retains its monotonically increasing cast serial after cancellation, so stale thinkers cannot match a replacement cast. |
| Revival | An awaiting dead Hero and a Hero actively queued for revival are distinct states. |
| Repair | The legacy construction path has a work record with no concrete Repair ability rawcode. |
| Sleep, item, destructable | Eligibility, sleeping, inventory/world placement, death and pathing are states of persistent owners. |
| Raven, Ensnare, Ancient Root | Their phases distinguish transitions and settled forms; attachment alone cannot identify a phase. |

## Save/load and verification

Combined branch save format 90 persists the complete records of all 27 pools. Only pointer fixups and process-owned exclusions need descriptors; unlisted scalar state still persists. Records retain their owning entity identity, not pool-slot indexes or process addresses. See [save/load](save-load.md).

`wc3_pools.release_reuses_zeroed_owned_state` checks initial absence, release, reuse and reset. `wc3_save.all_sparse_pools_restore_records_and_entity_references` checks all pool attachments, absent state on another entity, scalar state and entity references. Construction tests verify release on stop, cancellation and completion, a fresh lifecycle after stop, worker restoration, and completion events/food happening once. Existing ability, combat, construction, mining, item, cargo and save tests exercise the lifecycle entry points in both ROC and TFT modes.

```sh
make test-wc3-engine
make test
```
