# Warcraft III pooled entity state and components

## Ownership and access

Sparse lifecycle records live in `game/g_pool.c`. Most attach directly to `edict_t` pointers; Way Gate and Avatar use the component list described below. Types have no edict/game prefix: `ancientRoot_t`, `goldMine_t`, `cargo_t`, `channel_t`, etc. Allocate explicitly at the owning lifecycle entry point with `G_AllocAncientRoot()` or the corresponding PascalCase allocator. Reads never allocate and never substitute a shared zero object.

```c
if (!ent->ancient_root)
    ent->ancient_root = G_AllocAncientRoot();
/* The owned transition requires this record. */
assert(ent->ancient_root);
ent->ancient_root->mode = ANCIENT_ROOTING;
```

A dispatch or classification query may legitimately examine an entity without that behavior. Check its pointer there, as with `ent->client` in Quake II. Inside the owning behavior, assert required state and access it directly. Do not silently return from a required transition because initialization was missed. Standard C assertions print the failed expression, source file and line in assertion-enabled builds; disabling assertions does not add recovery behavior.

The reference is id Software's [Quake II edict definition](https://github.com/id-Software/Quake-2/blob/master/game/g_local.h) and [client code](https://github.com/id-Software/Quake-2/blob/master/game/p_client.c): `edict_t` stores `struct gclient_s *client`, and gameplay uses `ent->client->...` or `client = ent->client` directly. Allocation and attachment are separate lifecycle operations. Warcraft's sparse pools use that access pattern; Quake II itself preallocates its client array.

## Optional components

`edict_t.components` owns an intrusive singly linked list. `waygate_t` and `avatar_t`
start with `component_t { uint32_t id; component_t *next; }`; their former edict
members are removed. `COMPONENT_WAYGATE` and `COMPONENT_AVATAR` are stable exact
layout IDs, independent of authored ability rawcodes. There is one instance of
each type per entity. No runtime ability inheritance or component update loop is
introduced; existing ability procedures and scheduling retain behavior ownership.

- `G_GetComponent` / `G_GetWaygate` / `G_GetAvatar` only query; null entities and
  absent records return null. Cache the typed result within an operation.
- `G_AddComponent` explicitly allocates from the existing typed pool, prepends
  the common header, and returns the existing record on repeated initialization.
- `G_FreeComponent` unlinks before returning storage to its typed pool.
  `G_FreeWaygate` and `G_FreeAvatar` retain the owning behavior's release API.
- `G_PoolsReleaseEdict` drains the component list. It performs raw storage cleanup;
  ability-owned inverses still run in their existing lifecycle paths.
- `G_PoolsReset` clears heads and pools without following potentially partial
  restored links or invoking gameplay callbacks.

The static `component_types` registry in `g_pool.c` selects allocation and raw
release. Save descriptors in `g_save.c` select the concrete payload schema and
capacity. Adding a component requires both registrations, a header-first payload,
and typed accessors; it does not require another edict pointer. Direct attachments
remain for the other records while migration proceeds.

Lookup is O(attached components), rather than a direct load. Header and slot
alignment costs apply to every reserved typed-pool slot, including unused ones;
a smaller edict alone does not establish lower total memory consumption. Keep
hot unit/combat and movement access direct until measurements justify migration.

A class/superclass mask is not a concrete-layout identity. If family queries
become necessary, represent them separately; family matches must never authorize
casts to an arbitrary derived payload. Retail binary evidence establishes ability
objects, owner/reference plumbing and inheritance, but does not establish this
per-edict linked-list layout: see [binary evidence](ability-inheritance-binary.md).

Format 107 persists exact component IDs and payloads in the existing typed pool
sections. The edict head and record `next` links are runtime exclusions. Restore
validates the saved ID against the section type, clears links, and attaches only
a completely decoded record. List order is not a gameplay contract and may change
on restore. Counts, capacity checks and entity-reference fixups retain the pool
contract. Earlier versions are rejected; network messages are unchanged.

`wc3_components.mixed_list_release_reuse_and_entity_lifetime` covers mixed-list
lookup, head/non-head removal, zeroed reuse, edict teardown and reset.
`wc3_save.component_links_are_transient_and_wrong_type_is_rejected` checks link
exclusion and exact-type validation. Existing all-pool and truncated-load tests
cover mixed direct/component restoration and cleanup; Avatar and Way Gate suites
exercise the real ability and JASS lifecycle paths.

Verification commands (run gameplay suites for both RoC/TFT in each configuration):

```sh
make build test-wc3-engine WC3_PATTERN='wc3_*'
make BUILD=release BIN_DIR=/tmp/wc3-components-release/bin LIB_DIR=/tmp/wc3-components-release/lib SHARE_INSTALL=/tmp/wc3-components-release/share build openwarcraft3-tests test-wc3-engine WC3_PATTERN='wc3_*'
make test
```

The full suite also includes client-input tests; see the
[local SDL crash diagnostic](../../test-performance.md#local-macos-sdl-event-test-crash)
when evaluating local failures separately from gameplay coverage.

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

Save format 107 includes unit combat/progression/ability state, sounds, inventory, harvest, vision and basic-attack missile roles, alongside Soul Trap and the movement/order/route/wander roles. Each pool validates its own capacity on read. Only pointer fixups and process-owned exclusions need descriptors; unlisted scalar state still persists. Records retain their owning entity identity, not pool-slot indexes or process addresses. See [save/load](save-load.md).

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
