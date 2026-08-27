# Breakable Destructables

The first breakable-destructable slice gives placed crates, barrels, trees,
gates, and similar map objects an explicit server-authoritative lifecycle. A
destructable is neither a unit nor an item, although it uses the normal attack
pipeline and can publish the existing widget death event.

## Placement State

Destructables spawned from `war3map.doo` retain their editor creation ID and
apply the placement's initial-life percentage and visibility/pathing flags.
Their maximum life, target type, model, selection radius, and alive/death
pathing textures come from destructable object data.

Runtime state records whether the object is initialized, dead, solid at its
placement, and currently contributing a static pathing footprint. Alive and
death pathing resources are retained separately so the footprint can change at
the death transition.

## Combat And Death

Both explicit attack orders and contextual right-click orders accept an alive,
visible, targetable destructable. Neutral ownership does not turn a crate click
into a move order. Units approach and execute their existing melee or ranged
attack; the resulting damage is then routed through the destructable lifecycle.

Lethal damage performs one built-in transition:

1. Mark the destructable dead and clamp life to zero.
2. Disable further damage and targeting.
3. Leave combat and switch to its death pathing state.
4. Start the model's `death` animation when present.
5. Rebuild static obstacles from the unchanged terrain baseline.
6. Publish `EVENT_UNIT_DEATH` and invoke an optional compatibility callback.

The `dead` guard is set before events or callbacks, so overlapping hits cannot
repeat death processing. A missing death callback or animation does not prevent
the state transition.

## Inline Item Drops

Placed destructables retain borrowed references to their parsed
`war3map.doo` inline item sets for the lifetime of the loaded map. On a normal
death, each set receives one 0–99 roll and selects at most one entry by
cumulative percentage. Probability left below 100 intentionally produces no
item.

Loot is marked processed before selection and spawning, so duplicate lethal
hits, callbacks, or repeated kill requests cannot create extra items. Static
pathing is rebuilt first; selected valid rawcodes then spawn through
`SP_SpawnAtLocation` as ordinary neutral-passive world items. Multiple results
are distributed around the destroyed object rather than stacked at one point.
Invalid item rawcodes are reported and skipped.

Dead remains keep their model and continue rendering. A transmitted entity
flag maps to a renderer-only `RF_NOT_SELECTABLE` flag, excluding the remains
from point and rectangle selection without hiding them.

## Pathing Model

The path map now keeps an immutable terrain layer separate from the baked
static-obstacle layer. When a destructable changes state, static obstacles are
rebuilt from terrain plus the current footprints of live entities. This removes
an alive footprint without erasing terrain restrictions and permits a type's
optional death-pathing texture to replace it.

## Phase Boundary

This slice implements the first eight steps of the clean-room specification:
placement life, runtime health, normal attacks, lethal detection, one-time
death, death animation, post-death target disabling, and pathing replacement.

Inline configured item sets and world-item spawning are implemented. Random
item-table pointers, encoded random-item placeholders, richer killer/event
context, and complete scripted kill/remove/restore semantics remain deferred.
Death works normally when no loot or callback exists.

## Validation

The `wc3_destructable.*` in-engine tests cover placement state, hidden and
non-solid objects, callback-independent lethal damage, one-time events and
callbacks, neutral contextual targeting, dead-order rejection, alive-footprint
removal, death-footprint replacement, weighted selection, intentional no-item
results, multiple world-item drops, and one-time loot processing.
