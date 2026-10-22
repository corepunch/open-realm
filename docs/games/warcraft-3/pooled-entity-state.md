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

Ability pools have 2,048 slots (2,047 usable, slot zero reserved), except the destructable pool at 8,192: every map-placed destructable owns a persistent record, and retail campaign maps peak near 5,000 (NightElf04, UndeadX01). Unit combat/progression/ability state, sound, inventory, harvest, visibility, basic-attack missiles, movement, retained orders, destination routes and wander use `MAX_ENTITIES` slots because they can serve an entire active unit population; they are not restricted to the small ability-pool cap. Other ability pools previously measured at 256 or fewer. Each game shutdown logs `WC3 pool peak <name>=N/cap` for pools that were used; the map audit logs retain it, so rerun `make audit-wc3-maps` after adding a pool or raising content density. Allocation zeroes the selected record. Exhaustion logs the pool name and fails through `gi.error`; it never provides a shared writable fallback. `G_FreeAncientRoot(ent)` releases that record and clears its member. `G_PoolsReleaseEdict()` releases every attached record during entity teardown. Game initialization and map replacement reset all pools and detach all edict pool pointers.

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

Save format 106 includes unit combat/progression/ability state, sounds, inventory, harvest, vision and basic-attack missile roles, alongside Soul Trap and the movement/order/route/wander roles. Each pool validates its own capacity on read. Only pointer fixups and process-owned exclusions need descriptors; unlisted scalar state still persists. Records retain their owning entity identity, not pool-slot indexes or process addresses. See [save/load](save-load.md).

`wc3_pools.release_reuses_zeroed_owned_state` checks initial absence, release, reuse and reset. `wc3_save.all_sparse_pools_restore_records_and_entity_references` checks all pool attachments, absent state on another entity, scalar state and entity references. Construction tests verify release on stop, cancellation and completion, a fresh lifecycle after stop, worker restoration, and completion events/food happening once. Existing ability, combat, construction, mining, item, cargo and save tests exercise the lifecycle entry points in both ROC and TFT modes.

```sh
make test-wc3-engine
make test
```

## Travel state

`movement` is optional active-travel scratch; `orders` retains unit intent at rest,
`route` belongs to routing destinations, and `wander` owns Awan's idle schedule and
private waypoint. `G_UnitIsMoving()` checks travel-record presence, including paused
or route-waiting operations. Stationary facing never needs a movement record.
See [entity and player state](state-review.md) for allocation/release, save fixups,
and the fields moved out of the old embedded edict state.

## Soul Trap relationships

`edict_t.soultrap` points to `soulTrap_t` only while a unit carries souls or is
trapped. `CAbilitySoulTrap` explicitly attaches carrier/target records at capture;
merely owning `Asou` does not allocate. The record owns list links, spawn-incarnation
checks, the filled-item binding, capture-time visibility owner and temporary skill
ownership. A unit may occupy both roles, so releasing one must preserve the other.

Release unlinks targets before removing filled items or injected skills, then
frees empty records. Carrier death/disable/removal releases its captured targets;
target removal cleans its bound item. Generic edict teardown remains the final
pool release. Queries accept null without allocating. Save format 103 persists all
four entity links with explicit fixups and retains role/visibility metadata.

## Unit and feature records

`unit_state` holds unit-owned combat profiles, progression, statuses, skills and
cooldowns. Unit spawn attaches it. Map placements attach it before `SP_CallSpawn`
only when the placement has a `UnitBalance` row, so `war3map.doo` Hero progression
is available to unit initialization without allocating unit records for scenery.
Pure effect/waypoint queries leave it absent.
Status slots and incarnation counters survive empty intervals until entity teardown.
`inventory` attaches on the first successful item ownership; `G_InventoryItem`
reads physical slots without allocating or applying current inventory capacity.
`vision` attaches on explicit shared/forced sight or reveal changes, `missile` at
basic-attack launch, and `sound` at response registration/queueing. Those records
remain until entity teardown. `harvest` is released when the carry transaction
becomes empty. Mine occupancy lives on the mine's `goldmine` record.

Accepted worker build intent lives in `orders`; client placement choice lives in
`client->menu`. See the complete retained-field rationale in
[state review](state-review.md#whole-edict-ownership-pass).

## Rejected restores and owned child entities

A truncated pool restore resets all typed pools, detaching both decoded and
partially read records without running gameplay callbacks. Whole-game restore
also clears attachments on later payload/VM errors. It resets old pools before
replacing raw edicts; this does not provide full level/VM rollback.

Wander's private waypoint is an owned child with a saved spawn incarnation in
addition to its local route serial. Cleanup validates that identity before
freeing it. Build indicators have a reciprocal worker `owner` reference; direct
indicator removal detaches accepted-build intent before entity-slot reuse.
