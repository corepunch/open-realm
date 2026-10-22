# Warcraft III entity and player state

## Optional travel and retained intent

`edict_t.movement` is a `movement_t *`. Its presence means a travel operation
owns routing scratch. `G_UnitIsMoving()` exposes that same check. A paused unit,
a unit turning before its next step, or a unit waiting for a route can still have
an active travel operation; physical displacement is measured from position samples.
Queries never allocate a record or return a shared writable zero object.

| Record | Owner and lifetime |
|---|---|
| `movement_t *movement` | Active travel: progress watermark, blocked decisions, heading, formation speed, displacement, resumable route and worker avoidance. `G_BeginMovement` attaches it; stationary `unit_setmove`, Stop, arrival and death release it. Steering entry points can begin travel for interaction approaches. |
| `unitOrders_t *orders` | A live unit's retained command intent: Follow/Patrol/Attack-Move waypoints, guard anchors, cargo/drop interaction identity, resource-return target, Wisp egress, Shift-order FIFO and pending item drop/give context. Unit spawn initializes it; it survives stationary phases and combat detours. Entity teardown releases it. Effects/destinations do not need it merely to participate in routing. |
| `routeCache_t *route` | Destination-owned flow cache, shared group destination and waypoint incarnation. Allocated when a destination participates in routing. It does not indicate that the destination is moving. Reusing a waypoint increments its serial and clears its field/shared destination. |
| `wander_t *wander` | Awan-owned idle deadline, random state and private waypoint identity. Initialized through ability lifecycle/idle dispatch; disable/death/removal releases the waypoint and record. It persists during the idle interval between trips. |

`umove_t.travel` explicitly declares a travel-capable behavior; animation names do
not determine ownership. All walk/approach behaviors declare it, including item
pickup/drop/give. A Follow order inside its stopping distance releases movement
scratch while retaining its target, then begins travel again when the target moves.
`unit_facegoal` turns stationary attacks and repair work without allocating a route.
Immobile steering attempts do not create travel state.

The audit also covered fields outside the old embedded movement struct:

- `wander_*` moved into `wander_t`.
- `heatmap2*` and `waypoint_generation` moved into `routeCache_t`.
- The per-edict `order_queue` moved into `orders->queue`; pending `item_drop`
  moved into `orders`, and spell-approach inventory origin moved into its already
  owned `channel_t` record with explicit entity fixups.
- The overloaded `secondarygoal` split into `orders->resource_goal` for worker
  resource-return identity and `route->shared_goal` for group destination sharing.
- `old_origin` remains an event sample: region/proximity events must detect
  teleports as well as ordinary steps.
- `goalentity` remains the current behavior target shared by attacks, spells,
  missiles and travel. `movetype` and `unitinfo.MoveSpeed` describe unit physics
  and configuration even while standing. `velocity` is used by missiles and spell
  thinkers, not as the unit travel-state flag.

On the local arm64 build, the combined refactor reduces `sizeof(edict_t)` from 5,800 to 856 bytes (4,944 bytes smaller). The broader ownership pass alone reduces the previous 4,224-byte layout by 3,368 bytes.
This is an edict-layout measurement, not a claim about total resident RAM: the
existing typed-pool implementation also reserves backing storage for optional roles.

## Whole-edict ownership pass

The pass checks all remaining private fields for ownership, lifetime, producers,
consumers, redundant mirrors and persistence. The base entity no longer reserves
fixed unit status, ability, cooldown, weapon, progression, inventory or sound arrays.

| State | Owner and lifetime |
|---|---|
| `unitState_t *unit_state` | Unit combat profiles, armor/stat modifiers, attack clocks and target incarnation; Hero progression/ranks/XP/shortcut pulse; status slots and incarnation counter; cooldowns, ability overrides and autocast. Unit spawn initializes it, and explicit skill/status mutations can attach it. Stable slots and counters remain until entity teardown. Ordinary effects and waypoints do not acquire it through queries. |
| `visibility_t *vision` | Forced-visibility reference counts, explicit shared-sight recipients and permanent-invisibility reveal deadline. First explicit vision/reveal mutation attaches it; FOW reads absence as no override. |
| `harvest_t *harvest` | Carried gold/lumber. `S_SetCarriedResource` attaches on positive cargo and releases on empty cargo, updating carry art and command UI in the same transaction. Mine occupancy belongs to `goldMine_t`, alongside its existing worker/mine relationship. |
| `inventory_t *inventory` | Physical item slots, attached on first successful item ownership. Retained until entity teardown. `G_InventoryItem` is a non-allocating physical-slot query; current ability capacity remains a separate eligibility rule. |
| `attackMissile_t *missile` | Captured basic-attack damage type and Defend reflection state, attached at missile launch. Spell projectiles use their own behavior contracts. |
| `unitSounds_t *sound` | Resolved selection/order/ready/chop/death variants and pending channels. Sound registration/queueing attaches it; silent entities need no record. The unused attack-sound member was removed. |
| Worker build intent | Accepted project and preview live in retained `orders`; the player's placement cursor choice lives in `client->menu.build_project`. These had different owners despite sharing the old edict field. |
| Independent switches | Training, supply-wait notification, rally-marker identity, alternate icon, animation override and explicit vertex-color flags join `unit_flags`. Stun remains derived from status records. |
| Entity callbacks | Removed unused `idle`, `move`, `run` and `attack` callback slots and save descriptors. Kept the six callbacks with live producers/consumers. |

The retained base fields have shared entity contracts:

- The server prefix, spatial links, collision, bounds and snapshot state serve all
  entity classes. Class/spawn identity, metadata views and owner/target links are
  also common to units, items, destructables and effects.
- Health/mana pairs and damage/resource/velocity/lifetime scalars are used by
  destructables, resource entities, missiles and spell thinkers as well as units.
  Moving them into unit state would introduce unrelated allocations.
- `summon_ability`, `status_effect_code` and `aura_effect_role` identify summons and
  owned visual effects. Their consumers include generic effect sweeps and removal;
  they must not require the unit combat/status buffers.
- Animation requests, properties, sequence clocks, callbacks and current behavior
  serve destructables and visual effects too. Physics type, mutable movement/sight
  configuration, race/target classification, script flags and tint are persistent
  entity configuration, independent of active travel or a particular ability.
- `old_origin` is event sampling, and `goalentity`/`combatentity`/`wait` are shared
  behavior dispatch state. Their use by movement does not make them travel scratch.

Every new role participates in release/reset, peak reporting and entity-owned
save/load. Save admission uses each pool's actual capacity instead of applying
one destructable-specific limit to all pools. The move relocation anchor is
initialized data beside authored moves, avoiding displacement into the enlarged
pool BSS while preserving the existing validation range.

## Review checklist

| Original review area | Completed result |
|---|---|
| Struct/enum purpose and consumers | Documented declarations in `g_local.h`; named player/HUD/entity-data records replace anonymous bundles. |
| Player JASS state | Named `playerScript_t`; retained because spawn, natives, HUD, XP and saves use it. |
| Camera redundancy | Named camera record and grouped pan; independent scalar, pan, controller and noise contracts remain explicit. |
| Optional movement, including fields outside Movement | Active scratch, retained orders/FIFO/waypoints, destination cache and Awan schedule have separate owned records. |
| Animation names | Immutable interned content replaces fixed per-edict arrays; saves store text and restore shared identity. |
| Cooldown duplication | One authoritative deadline; recovery derives remaining time. |
| Ability payload duplication | One related-entity slot plus typed message payload; registry class derives from the bound ability. |
| Event/message enums | Audited production phases; JASS numeric ABI slots remain stable. Unsupported events are not implemented by deleting constants. |
| SC2API placement | Remains a WC3-owned protocol adapter; no SC2 simulation dependency introduced. |

The remaining inline fields that movement also touches are general entity contracts,
not hidden travel scratch: the fixed server prefix (`s`, collision, bounds, area
links) describes every entity; `movetype`/`unitinfo` retain physics and configuration;
`goalentity`, `combatentity`, `currentmove`, callbacks and `wait` dispatch attacks,
spells, work and missiles as well as travel. `unit_flags` and `aiflags` are persistent simulation rules; stun is queried from live status records. `velocity` belongs to missiles/thinkers;
`old_origin` samples region/proximity events even for stationary teleports. None
should be used instead of `G_UnitIsMoving()` to infer active unit travel.

A second full-diff audit found and fixed rejected point/selection Move paths that
assumed `order_move` always attached scratch. Translation-disabled units now reject
before destination/formation work. It also restored authored Rooted Turning through
stationary facing and steering entry points without allocating travel. Regressions
failed first (null dereferences and a missing turn), then passed in classic/TFT.

## Attack timing

Weapon cooldown retains only `unit_state->attack_cooldown_end_time` in simulation milliseconds.
Active state and remaining seconds are derived when queried, including after a
replacement order. The former active flag and remaining-time mirror are gone.
`attack_backswing_end_time` is a different post-hit gate, and `wait` is the
move-local animation/damage/recovery clock; neither duplicates the weapon deadline.
No-op per-frame mirror-refresh calls were removed.

## Player records and camera

`client_s` contains named records for script-owned player state, camera, info-panel
cache, resource-bar cache, shortcuts, active messages and retained message history.
`playerScript_t` is live runtime state: spawn initializes it, JASS natives mutate it,
XP reads handicap, and HUD/save paths consume names and overrides. It is not test state.

Camera pan fields are grouped as `camera.pan.{active,time,start,destination,rate}`.
The captured start and timed endpoint differ from continuous strafe/forward velocity.
Scalar setup interpolation, target/orient controllers and noise have independent
contracts; the game samples their results into player state. The obsolete cached
portrait mana value was removed; the old overloaded HP cache is now explicitly a
selection `count`, while live health/mana use player-state bindings.

## Animation strings

Per-edict fixed request/queue/property arrays were replaced with interned immutable
strings. `G_InternAnimationName` copies script/stack text and shares identical content;
repeated Walk requests do not allocate per unit or frame. The 80-byte request limit
was removed. Normalized property tags still use the parser's bounded temporary
serialization buffer. `G_FreeAnimationNames` releases storage at game shutdown,
independently of model-cache refresh, because logical names can outlive model metadata.

A timer cannot identify which animation family to select. Retaining logical names
is necessary for deferred model resolution, property-driven reselection and queued
JASS destructable clips; interning removes the repeated inline storage.

## Ability procedure payload

The single `abilityProc_t` dispatcher remains. `ent` is its receiver and the common
`call->entity` identifies the related actor for commands, damage, projectile impacts,
Move starts and targeted orders. Immediate and targeted orders share `call->order`;
queue messages use `entity` for the producer and `queue_item` for the queued result.
`source_item` and its spawn incarnation remain separate because an asynchronous
inventory origin can coexist with another related entity.

The redundant init-only `classname` payload was removed: `S_AbilityClass(call)`
reads the registry key from `call->item->ability`. Concrete ability rawcodes remain
in `item->code`. Order names such as `smart`/`attack` and internal command keys such
as `CmdBuild` are not FOURCCs. Typed function/status/target payloads retain their
contracts without converting function pointers or floats through untyped integers.

Every `abilityMsg_t` has production references. Similar payloads do not make
query/notification, issuer/target, add/remove or refresh/remove phases duplicates.
JASS `EVENTTYPE` values remain numerically stable, including currently unsupported
subscriptions. `G_ExecuteEvent` explicitly retains empty cases such as
`EVENT_GAME_VICTORY`; removing enum rows would break the script ABI rather than
implement those events. Struct/enum declarations now document purpose and consumers,
and `AGENTS.md` requires those comments for future declarations.

## Unit switches and derived stun

`edict_t.unit_flags` stores five independent `UNIT_FLAG_*` bits: IGNORE_ALARM,
INVULNERABLE, PAUSED, NO_PATHING and TIMED_LIFE_PAUSED. `G_UnitHasFlag` returns a
normalized boolean (important for numeric snapshots and native equality checks);
`G_SetUnitFlag` changes only the named bit. These are game-owned flags, separate
from server visibility and AI policy. Item/destructable invulnerability uses the
same entity switch. Construction, timer, bot and music pause fields retain their
own types and lifecycles.

`stunned` was a redundant cache written by status refresh and Possession cleanup.
All consumers now call `unit_hasstatusstate(unit, WC3_STATUS_STATE_STUNNED)`, which
combines legacy Bstu/Bsta/Bpos/BUsl identities with explicit state masks and ignores
expired slots before cleanup. There is no serialized stun bit or refresh-order
dependency. Status refresh still notifies owners to reconcile flight/height;
it no longer maintains the removed cache. Explicit Pause remains independent.

Regressions exercise native bit isolation and timed-life pause/resume, all-bit
save/load, overlapping built-in/custom stuns after load, and an expired stun that
previously cancelled a channel before the cleanup tick. Test fixtures now apply
real stun statuses rather than writing the removed mirror. The native flag test seeds BTLF through the status helper because `UnitApplyTimedLife` is an existing stub; it exercises the real `UnitPauseTimedLife` native for pause/resume.

## Save and network contracts

Save version 115 rejects prior layouts. Way Gate, Avatar, Polymorph, Raven Form,
Ensnare, artillery profiles, Sleep, Rally, Sacrifice, Unsummon, Blight Growth,
Shadow Meld, Militia, Hero revival, worker build/repair state, Ancient Root, the
mine records, Cargo, Soul Trap, Research, Food, shop Stock, the spell Channel, Destructable, Construction and Item now use the
[component list](pooled-entity-state.md#optional-components); the `MAX_ENTITIES`
unit/movement roles retain direct attachments. Movement, orders, route and wander records
use the existing entity-owned typed-pool serialization. Entity references have
explicit fixups; route jobs, resumable steering and destination flow generations
are process-local caches cleared on load. Guard intent and waypoint incarnations
survive. Animation strings follow each raw edict as text and are interned on read;
process addresses are cleared from the raw record.

No entity/player network fields or wire layouts changed. The SC2API Raw endpoint
remains the optional Warcraft-owned adapter: it translates agent protocol values
into WC3 state/orders and does not belong to the StarCraft II game module.

## Verification

- `wc3_unit.unrelated_entities_have_no_unit_state`: pure effect queries leave unit and optional feature records absent.
- `wc3_save.pool_capacity_is_checked_before_allocating_records`: per-pool admission and no partial allocation on rejection.
- `wc3_unit.movement_storage_is_optional`: optional pointer storage contract.
- `wc3_order_lifecycle.optional_movement_stop_pause_arrival_and_reuse`: real orders,
  scheduler pause/arrival, Stop, death, release and a fresh trip.
- `wc3_order_lifecycle.stationary_follow_retains_intent_without_travel_scratch`:
  waiting Follow releases scratch and resumes when the target moves.
- `wc3_unit.animation_names_are_shared_owned_and_not_truncated`: script/stack lifetime,
  shared identity and requests longer than the former 80-byte limit.
- `wc3_save.animation_strings_restore_after_intern_storage_is_released`: save/load
  after discarding all process-owned animation strings.
- `wc3_save.route_cache_rebuilds_after_load` and existing pool/save tests: destination
  cache reset, entity fixups, absent roles, invalid references and old-save rejection.
- Existing combat, movement, resource-worker, guard, cargo, waypoint and wander suites
  cover the changed production consumers in classic and TFT modes.

The local Homebrew `sdl2-compat` 2.32.72 crashes in an unchanged SDL text-input test.
Validation uses separately built upstream SDL2 2.32.10 without changing dependencies:

```sh
SDL_VIDEODRIVER=dummy SDL_DYNAMIC_API=/path/to/upstream/libSDL2-2.0.0.dylib make test
```

Network tests also require local UDP access outside the test sandbox.
See [pooled state](pooled-entity-state.md), [ability implementation](ability-implementation.md),
[animation properties](unit-animation-properties.md), [save/load](save-load.md),
[SC2API adapter](sc2api-agent-interface.md), and [camera sampling](../../architecture/client.md).

## Ownership rereview

The whole diff was traced from allocation sites through consumers, teardown and
save schemas. All 38 typed roles have save, release, reset and peak-report entries.
For the extracted roles, persistent pointers are either explicit `F_EDICT` fields
(inventory slots, status sources, retained order targets/previews, Wander goals,
Soul Trap links and channel item origin) or process-owned exclusions (routing
scratch). Queue entries contain entity numbers/incarnations, not pointers.
Scalar profiles, ranks, cooldowns, Hero progression and switches persist in their
owning raw records; they do not need pointer fixups.

The rereview added regressions and fixes for four boundary cases:

- `G_AttackDamage` accepts an entity without `unit_state`, using the original
  default damage category without allocating a record. The absent-role test also
  exercises armor, targeting, autocast, inventory, status/cooldown and sight queries.
- Failed pool reads detach every partially restored record without gameplay
  callbacks: raw records can still contain undecoded references. Whole-game reads
  reset old pools before replacing edicts and detach loaded records on later
  entity/pool/hashtable/VM/footer failure. This is rejection cleanup, not rollback
  of the entire level or VM. Early header/checksum rejection retains the old world.
- Wander records now save `waypoint_spawn_time`. Active ownership, blocked-move
  recovery and private-destination cleanup check incarnation, in-use state and
  routing presence where needed. A reused slot can have the same local route
  serial, so the serial alone is insufficient. Disable/removal cannot free the
  replacement entity. The saved field requires format 106.
- Build previews keep a reciprocal `owner` reference to their worker. Direct
  preview removal clears `orders->build_preview` before the slot can be reused;
  normal worker cleanup still clears its pointer before freeing the preview.
  Both references use the existing entity-reference save schema.

Named inline player records retain their enclosing `game.clients` lifetime:
`playerScript_t` owns the separately allocated disabled-ability list (cleared by
player/map teardown); `clientCamera_t` borrows a target entity and stores scalar
controllers; HUD caches/shortcut state are rebuilt authoring caches; active message
and message-log records retain text by value. They are not independently allocated.
`entityData_t` contains borrowed metadata views rebound by class on load.
`abilityCall_t` is a borrowed synchronous dispatch message; callees must copy any
context needed beyond the call into their owning lifecycle record. Animation names
are immutable shared allocations owned by the game-wide intern arena, released
at full game shutdown and serialized as content.

Targeted failures were reproduced before each fix. Regression names:
`wc3_unit.unrelated_entities_have_no_unit_state`,
`wc3_save.failed_pool_restore_detaches_all_partial_records`,
`wc3_save.late_restore_failure_releases_loaded_pools`,
`wc3_wander.removal_does_not_free_replacement_of_private_waypoint`, and
`wc3_building.removing_active_preview_detaches_worker_before_slot_reuse`.
