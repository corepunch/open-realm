# Warcraft III pooled entity state and components

## Ownership and access

Sparse lifecycle records live in `game/g_pool.c`. Ability-owned and producer-owned records attach through the component list described below; item and the `MAX_ENTITIES` unit/movement records still attach directly to `edict_t` pointers. Types have no edict/game prefix: `ancientRoot_t`, `goldMine_t`, `cargo_t`, `channel_t`, etc. Attach explicitly at the owning lifecycle entry point with `G_AddComponent` (components) or the PascalCase `G_Alloc*` allocator (direct pointers). Reads never allocate and never substitute a shared zero object.

```c
ancientRoot_t *root = G_GetAncientRoot(ent);
if (!root || root->mode == ANCIENT_ROOT_UNINITIALIZED)
    root = G_AddComponent(ent, COMPONENT_ANCIENT_ROOT);
/* The owned transition requires this record; cache it for the operation. */
root->mode = ANCIENT_ROOTING;
```

A dispatch or classification query may legitimately examine an entity without that behavior. Check its pointer there, as with `ent->client` in Quake II. Inside the owning behavior, assert required state and access it directly. Do not silently return from a required transition because initialization was missed. Standard C assertions print the failed expression, source file and line in assertion-enabled builds; disabling assertions does not add recovery behavior.

The reference is id Software's [Quake II edict definition](https://github.com/id-Software/Quake-2/blob/master/game/g_local.h) and [client code](https://github.com/id-Software/Quake-2/blob/master/game/p_client.c): `edict_t` stores `struct gclient_s *client`, and gameplay uses `ent->client->...` or `client = ent->client` directly. Allocation and attachment are separate lifecycle operations. Warcraft's sparse pools use that access pattern; Quake II itself preallocates its client array.

## Optional components

`edict_t.components` owns an intrusive singly linked list. `waygate_t`, `avatar_t`,
`polymorph_t`, `raven_t`, `ensnare_t`, `artillery_t`, `sleep_t`, `rally_t`,
`sacrifice_t`, `unsummon_t`, `blightGrowth_t`, `shadowMeld_t`, `militia_t`,
`revival_t`, `buildwork_t`, `ancientRoot_t`, `goldMine_t`, `mineOverlay_t`,
`acolyteMine_t`, `cargo_t`, `soulTrap_t`, `research_t`, `food_t`, `stock_t`, `channel_t`,
`destructable_t` and `construction_t` start with
`component_t { uint32_t id; component_t *next; }`; their former edict members are
removed. The `COMPONENT_*` enumerators in `g_local.h` (`COMPONENT_WAYGATE` through
`COMPONENT_CONSTRUCTION`) are stable exact layout IDs, independent of authored
ability rawcodes. There is one instance of
each type per entity. No runtime ability inheritance or component update loop is
introduced; existing ability procedures and scheduling retain behavior ownership.

| Component | Owner | Attached by | Released by |
|-----------|-------|-------------|-------------|
| Way Gate | `s_waygate.c` | JASS destination/activation natives | entity teardown |
| Avatar | `s_human_abilities.c` | Avatar activation | Avatar expiry/dispel |
| Polymorph | `s_human_abilities.c` | first successful `Aply` cast; refreshes keep the first snapshot | buff expiry, `S_PolymorphRemove`, death |
| Raven Form | `s_raven.c` | `ravenform`/`unravenform` order after the type rebind | entity teardown; `rise_state` marks settled forms |
| Ensnare | `s_campaign_abilities.c` | gradual DataA land or rise; a snapped land frees it | finished rise, non-flyer, zero DataA |
| Artillery | `s_attack.c` | fixed-point projectile launch with an authored weapon | projectile teardown after `g_phys.c` impact |
| Sleep | `s_creep_sleep.c` | `A_UNIT_INIT` for `UnitData.canSleep` units; `UnitAddSleep` enable/disable on neutrals | entity teardown; `can_sleep`/`sleeping` are the live state |
| Rally | `s_rally.c` | `G_SetRallyPoint`/`G_SetRallyEntity`; the whole record is rewritten per assignment | `G_ResetRallyTarget`, invalid entity target, teardown |
| Sacrifice | `s_sacrifice.c` | `G_QueueSacrifice` on the hidden result unit | queue completion; cancel keeps it until the result is freed |
| Unsummon | `s_unsummon.c` | cast: thinker owns the demolition ledger, caster mirrors the approach | entity teardown; `unsummon_cancel_approach` zeroes the approach |
| Blight Growth | `s_blight.c` | `A_UNIT_INIT`/`A_ENABLE` with a concrete `Abli` alias | `A_DISABLE`, `A_UNIT_REMOVE`, lost ability level |
| Shadow Meld | `s_shadowmeld.c` | passive update for `Ashm`/`Ahid` owners, or the Hide order | entity teardown; `S_ShadowMeldBreak` zeroes fade/active/hide |
| Militia | `s_militia.c` | `militia_sync_form`, pairing orders and the Convert command | entity teardown; `S_CancelMilitiaPairing` zeroes an inactive pairing |
| Revival | `m_unit.c`, `s_train.c` | Hero death and `G_QueueHeroRevive`; the corpse's `awaiting` flag survives a cancelled queue | entity teardown; `G_ReviveHero` and death rewrite the whole record |
| Buildwork | `s_repair.c` | Repair start and the legacy construction work move; the whole record is rewritten | entity teardown; `repair_release` zeroes it |
| Ancient Root | `s_utility_abilities.c` | first Root command/update on an Ancient; `mode` distinguishes settled forms and transitions | entity teardown; death/removal mark it uninitialized |
| Gold Mine | `s_goldmine.c` | mine admission (`G_InitGoldMine`) and worker entry | worker exit zeroes the worker's record; teardown |
| Mine overlay | `s_goldmine.c` | Haunt/Entangle overlay binding to its parent mine | `S_MineOverlayRelease` zeroes it; teardown |
| Acolyte mine | `s_goldmine.c` | Acolyte slot claim on a Haunted mine | slot release zeroes it; teardown |
| Cargo | `s_cargo.c` | first load or corpse-cargo claim | `cargo_drop_all` empties it; teardown |
| Soul Trap | `s_item.c` | capture attaches carrier and target records | `soul_trap_free_unused` frees empty records |
| Research | `s_train.c`, `g_building.c` | research queue items and in-place building upgrades; the whole record is written at queue time | completion/cancel free the queue item or the upgrade record |
| Food | `g_food.c` | first non-zero food used/made transaction | entity teardown; ownership transfer and HUD queries check presence |
| Stock | `g_stock.c` | shop slot assignment or first stock initialization | entity teardown; stock lists are rewritten in place |
| Channel | `s_spell.c` and the spell owners | cast begin, channel thinkers, fixed-target missiles and markers | entity teardown; `code` 0 marks an idle record whose `serial` keeps increasing |
| Destructable | `g_destructable.c`, `g_spawn.c` | destructable spawn; map-placed instances keep it for their whole life | entity teardown; death keeps the record for loot and pathing |
| Construction | `g_building.c` strategies | construction start; presence is the active-construction check | completion and stop/cancel free it after worker cleanup |

- `G_GetComponent` and the typed getters (`G_GetWaygate`, `G_GetAvatar`,
  `G_GetPolymorph`, `G_GetRaven`, `G_GetEnsnare`, `G_GetArtillery`, `G_GetSleep`,
  `G_GetRally`, `G_GetSacrifice`, `G_GetUnsummon`, `G_GetBlightGrowth`,
  `G_GetShadowMeld`, `G_GetMilitia`, `G_GetRevival`, `G_GetBuildwork`,
  `G_GetAncientRoot`, `G_GetGoldMine`, `G_GetMineOverlay`, `G_GetAcolyteMine`,
  `G_GetCargo`, `G_GetSoulTrap`, `G_GetResearch`, `G_GetFood`, `G_GetStock`, `G_GetChannel`,
  `G_GetDestructable`, `G_GetConstruction`) only query;
  `G_UnitIsResearching` folds the presence-and-upgrade check used by queue walkers;
  `G_HeroIsReviving`/`G_HeroAwaitsRevival` fold the common null-and-flag check;
  null entities and absent records return null. Cache the typed result within an
  operation; the owning behaviours hold one local pointer per transition.
- `G_AddComponent` explicitly allocates from the existing typed pool, prepends
  the common header, and returns the existing record on repeated initialization.
- `G_FreeComponent` unlinks before returning storage to its typed pool. The
  per-type `G_Free*` wrappers retain each owning behavior's release API, so
  `S_PolymorphRemove`, Ensnare's snap/finish paths and unit death keep their
  existing cleanup ordering.
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

Formats 107 to 112 persist exact component IDs and payloads in the existing typed
pool sections (108 added Polymorph, Raven Form, Ensnare and artillery; 109 added
Sleep, Rally, Sacrifice, Unsummon and Blight Growth; 110 added Shadow Meld,
Militia, Revival and Buildwork; 111 added Ancient Root, the three mine records,
Cargo and Soul Trap; 112 added Research, Food and shop Stock; 113 added the spell Channel;
114 added Destructable and Construction). The destructable pool keeps its 8,192-slot
capacity; a component record costs the same slot as before plus its header. Entity references inside a payload (`rally->entity`,
`sacrifice->worker`, `unsummon->target`, `revival->producer`, `revival->queue_next`,
`ancient_root->approach_goal`, `goldmine->mine`, the mine overlay links, `acolyte_mine->mine`,
`cargo->units[]` and the Soul Trap links) keep their `F_EDICT` fixups;
`SAVE_COMPONENT_PTR_FIELD_TEST` covers each one. Militia's partner pairing stays a
runtime exclusion. The edict head and record `next` links are runtime exclusions. Restore
validates the saved ID against the section type, clears links, and attaches only
a completely decoded record. List order is not a gameplay contract and may change
on restore. Counts, capacity checks and entity-reference fixups retain the pool
contract. Earlier versions are rejected; network messages are unchanged.

`wc3_components.mixed_list_release_reuse_and_entity_lifetime` covers mixed-list
lookup, head/non-head removal, zeroed reuse, edict teardown and reset;
`wc3_components.every_registered_type_attaches_releases_and_rejects_other_ids`
walks the whole registry. `wc3_save.component_links_are_transient_and_wrong_type_is_rejected`
checks link exclusion and exact-type validation. The all-pool and truncated-load
tests cover mixed direct/component restoration and cleanup; the per-field
`SAVE_COMPONENT_*_FIELD_TEST` round trips in `t_game.c` and the artillery and
Blight Growth save tests reach payloads through the typed getters. Avatar, Way
Gate, Polymorph, Raven Form, Ensnare, artillery, creep sleep, rally, Sacrifice,
Unsummon, Blight, Shadow Meld, militia, Hero revival, construction/repair,
Ancient Root, gold mining, cargo, exhume and Soul Trap suites exercise the real
ability, projectile and JASS lifecycle paths. Stack-allocated edicts in API tests
attach their records with `G_AttachComponent`.

Migration order so far follows the smallest ability-owned records with one owning
`skills/s_*.c` file each. Remaining direct attachments (item and the `MAX_ENTITIES`
unit/movement records) keep their pointers; the unit/combat/movement set stays
direct until measurements justify list lookup. Destructables carry only their own
record, so tree and gate queries from pathing and harvesting walk one node.
Food is read on every production and death transition and by the SC2API
observation. Channel is the first per-frame query on every entity:
`spell_run_frame` reads it from `G_RunEntity`, so a unit's component list should
stay short; measure before adding records that every unit carries. Cargo presence is read by the HUD and the SC2API
observation for every selected or observed unit; mine overlay and acolyte lookups
run inside `FILTER_EDICTS` sweeps on mine events only.
Revival is read by the production-queue walkers in `s_train.c` and the HUD, so
`G_HeroIsReviving` is the second component query on a per-frame path after Sleep. Sleep is queried from acquisition
filters (`G_UnitIsSleeping`), so it is the first component on a per-frame path;
its list is short because sleeping creeps rarely carry other components.

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

Construction state exists only while construction is in progress, including paused Human construction. Completion and stop release it after worker cleanup; `G_GetConstruction(ent) != NULL` is the active-state check. There is no separate `active` flag.

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

The `soulTrap_t` component (`G_GetSoulTrap`) is attached only while a unit carries souls or is
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
