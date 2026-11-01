# Warcraft III entity and player state

This review describes the reconciled branch layout. Upstream's component-list and
travel-only allocation migration is not adopted: native clocks and retained
ownership must survive stationary phases, Stop, replacement and save/load.

## Entity ownership

`edict_t` remains a fixed, flat array entry. Movement and retained order lifecycle
fields are embedded. Their presence does not establish physical movement; use the
owning order/task and sampled positions to distinguish travel, turning, suspension
and waiting. A Follow inside its stopping distance retains its target and owner
state so it can resume when the target moves.

Optional ability, construction, mine, item, destructable and other feature state
uses direct typed pointers and fixed-capacity typed pools. Owning transitions
allocate explicitly; queries never allocate or return a shared writable zero
record. Re-read a sparse pointer after callbacks that may release it, and check
entity incarnation before continuing a reentrant transaction.

Route caches and Wander records have separate lifetimes. A destination's route
cache does not mean it is moving. Wander retains its idle schedule and private
waypoint identity between trips. Route jobs and derived steering caches rebuild
on load; retained native clock and ownership fields persist.

`old_origin` is a region/proximity event sample, including teleports. Physics
configuration, `goalentity`, `combatentity`, animation state and `wait` serve more
than locomotion. Health, mana and lifetime fields also serve destructables,
missiles and effects. They are not travel-only storage.

Physical inventory slots remain embedded. `G_InventoryItem` reads a bounded slot
without allocating; current inventory ability capacity is a separate eligibility
rule. `G_ItemState` validates an item before returning its direct sparse record.
Build previews retain their reciprocal worker `owner` reference so direct preview
removal clears the worker's `build_preview` before slot reuse.

Attack profiles share immutable defaults and allocate per-slot overrides only
when needed. Attack deadlines, backswing recovery, primary timer requests and
animation `wait` have separate meanings; this merge does not replace the existing
native timing model with upstream's single-clock representation. Entity switches
remain direct fields, alongside independent status and AI state.

## Player and presentation records

Named player records retain the enclosing `game.clients` lifetime. Script state
owns the disabled-ability list; camera state borrows target entities and retains
its scalar controllers; HUD caches and shortcut state rebuild presentation.
Message records retain their text. Metadata views are rebound by class on load.

Animation requests and property text use the branch's immutable
`unitAnimationText_t` records. The queued animation remains a bounded inline
buffer. Save/load writes request/property content and resolves animation metadata
after rebinding models; it does not serialize process addresses or adopt the
upstream claim that every animation string is an unbounded interned pointer.

## Ability and event contracts

The existing `abilityProc_t` dispatcher and typed message payloads remain.
`abilityCall_t.client` retains its branch meaning; message-specific target,
status, queue and function payloads must not be collapsed into a generic entity
slot. A call is borrowed synchronous context: owners copy anything required after
return into their retained lifecycle state.

JASS event numbers remain stable. Similar payloads do not make admission,
notification, inverse and refresh phases interchangeable. Unsupported numeric
event slots remain part of the script ABI.

## Persistence and validation

Save225 combines incoming gameplay fields with direct sparse pointers and embedded
movement/order state. Pool records use explicit entity-reference fixups and
registered callbacks. Rejection cleanup must detach partially restored records
without invoking gameplay callbacks against undecoded references. Early header
rejection preserves the existing world; later load failure is not a rollback of
the entire level or VM.

The merge does not establish fresh test success. Validation must cover native
movement/order continuations, sparse allocation/release, save rejection and cold
restoration, plus the incoming feature regressions against this layout. Upstream
optional-movement and component-list tests require adaptation and cannot serve as
proof of the retained representation unchanged.

See [pooled state](pooled-entity-state.md), [save/load](save-load.md),
[ability implementation](ability-implementation.md),
[animation properties](unit-animation-properties.md), and
[SC2API adapter](sc2api-agent-interface.md).
