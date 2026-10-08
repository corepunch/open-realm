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

For HumanX06 elevator placement groups, trigger-driven height transitions,
and DTep wall-blocker placement policy, see [Warcraft III elevators](elevators.md).

Runtime state records whether the object is initialized, dead, solid at its
placement, and currently contributing a static pathing footprint. Alive and
death pathing resources are retained separately so the footprint can change at
the death transition.

The resource lookup is data-driven: `dest_schema` maps the `pathTex` and
`pathTexDeath` columns from `Units\\DestructableData.slk` into
`DestructableData_t`; `SP_SpawnDestructable` loads both through
`M_LoadPathTex` and keeps them as `alive_pathtex` and `death_pathtex`.
`LoadTGA` accepts the uncompressed 8-bit grayscale and 24/32-bit BGR(A)
formats used by WC3 pathing resources. It validates the complete header, ID
field, dimensions, allocation size, and pixel payload before decoding.

Every destructable path texture follows its entity's authored facing in
`entity_pathtex_transform()` in `games/warcraft-3/game/g_world.c`; the policy
does not depend on a gate rawcode or `targType`. Unit/building path textures
retain their existing axis-aligned behavior for now. `UnitData.slk` also
assigns path textures to buildings, including the orientation-named
`CityBuildingLarge_0`/`_45`/`_90`/`_135` resources; building path textures may
also need to follow facing, but verify how map placement angles and those
authored variants interact before changing their policy.
`DestructableData.slk` assigns path textures to many shapes besides gates,
including the 10x2 stone wall (`LTw0`), the 16x4 elevator blocker (`DTep`), and
Tree Bridge destructables (`LTt0`-`LTt5`, `ATt0`-`ATt1`) whose target type is
`debris`. Square or symmetric textures may look unchanged under rotation, but
restricting orientation to selected rawcodes omits other authored shapes. When
pathing does not match a destructable, confirm its `pathTex`/`pathTexDeath`
resolved and check the actual stamped pathmap against the facing. The regression
suite covers the transform and a rotated non-gate pathing blocker through
`CM_BakeStaticObstacles()`.

## Walkable Model Height

Walkable destructable routing remains a 2D pathing concern, but their visible
support height is model-authored. Live `walkable` destructables publish
`EF_GROUND_SURFACE`; non-floating units publish `EF_GROUND_CONFORM`. The client
maps those to renderer flags, and the WC3 renderer casts a vertical ray through
each candidate surface with `MDLX_TraceModel`. It replaces the server's coarse
`surface->s.origin.z` support with the highest MDX intersection while preserving
the server-authored vertical offset (including mutable `FlyHeight`).

This deliberately does not modify `routing.c`, `g_ai.c`, or the static pathing
bake. `pathTex`/`pathTexDeath` remain authoritative for where units may route;
the MDX trace is presentation-only and determines how the actor follows the
bridge deck in Z. Dead or placement-disabled destructables clear
`EF_GROUND_SURFACE`, so their retained Death geometry cannot lift units.

## Destructable Animation Speed (JASS)

`SetDestructableAnimationSpeed(d, speedFactor)` is registered in
`api/api_destructable.h` and `api/api_module.c`. `common.txt` already declares its
retail JASS signature. It checks that the handle represents a live destructable
and writes the per-edict `animation_speed` multiplier. Spawned entities start at
`1.0`; `0.0` freezes and positive factors speed up/slow down playback. In line
with `SetUnitTimeScale`, OpenWarcraft currently clamps negative factors to zero;
negative retail behaviour has not been conclusively established.

`G_RunDestructableAnimation` advances the server-owned `s.frame` at
`FRAMETIME * animation_speed` for scripted animation overrides, including
`SetDestructableAnimation` followed by `QueueDestructableAnimation`. It preserves
the rate when switching to a queued clip. The normal entity state/animation
clock and the existing save field (`g_save.c`) already carry the value; no new
network or save-format fields are necessary. This native does not change
health, pathing, collision, occluder height or animation selection.

The `wc3_destructable.set_animation_selects_only_resolved_model_sequences`
regression covers JASS dispatch, independent instances, zero freeze, half-speed
scheduler advancement, queued sequence rate retention, negative clamping and
resetting to 1.0. Verify visual frame timing in-game separately; runtime-model
sampling is not proven by a headless frame test. See [Elevators](elevators.md)
for the script-owned transition and height contract.

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

Alive and dead visuals use sequences from the same authored MDX model; there is
no separate dead-model field in `DestructableData.slk`. The persistent destroyed
bridge geometry is selected by the model's final `Death` frame. Once that
sequence completes, `tree_decay1` pins the entity to that exact final frame so
death-only geosets remain visible. Restoration releases the held frame and
selects `Birth` or `Stand` as requested.

Single-variation destructables register the unsuffixed model named by object
data. For LT05 this is
`Doodads\\Terrain\\WoodBridgeLarge45\\WoodBridgeLarge45.mdx`; registering an
invented `WoodBridgeLarge450.mdx` lets the renderer's filename fallback find the
model but leaves the game-side animation cache unable to resolve its sequences.
The authoritative model has looping `Stand` at `133..1333`, non-looping `Death`
at `2000..3000`, and non-looping `Birth` at `3333..10000`. Restore with
`birth=true` plays the full Birth sequence before Stand; `birth=false` selects
Stand immediately.

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

## Map Random-Item Tables

A placement's `droppedItemSetPtr` is retained alongside its inline item sets.
At death it is resolved against the `war3map.w3i` random-item tables by the
table's explicit `tableNumber`; it is not treated as an array index. Each set
in the resolved table receives its own 0–99 roll and contributes at most one
item using the same cumulative-percentage and no-item-remainder rules as an
inline set.

Inline and table results are collected before spawning, so all selected items
share the same radial placement behavior. Missing tables, empty tables and
sets, zero IDs, and invalid item rawcodes are skipped safely. Encoded `YYI*`
random-item placeholders are recognized and reported but are not yet expanded
into item-level/class selections.

## Events And Scripted Lifecycle

Death events retain both the dying widget and the damage source. Widget death
registrations still receive the destructable through `GetTriggerWidget()`, while
`GetKillingUnit()` returns the attacking unit or null for a scripted kill. The
same source propagation is used for ordinary unit death events.

All destructable lifecycle natives now use the authoritative transitions:

- `KillDestructable` performs death animation, pathing replacement, loot, and
  one death event.
- `RemoveDestructable` unlinks and frees the object without death, loot, or a
  death event, then rebuilds static pathing.
- `SetDestructableLife` kills at zero and restores a dead object when assigned
  positive life.
- `DestructableRestoreLife` restores targetability, alive pathing, and either
  the birth or stand animation.
- `CreateDeadDestructable` and its Z variant create the dead presentation
  without processing a gameplay death.

Restoring a dead destructable begins a new lifecycle and resets its one-life
loot guard. A later genuine death can therefore publish another event and
resolve its configured drops again. Initially dead and explicitly dead-created
objects are marked as already processed until restored.

Dead remains keep their model and continue rendering. A transmitted entity
flag maps to a renderer-only `RF_NOT_SELECTABLE` flag, excluding the remains
from point and rectangle selection without hiding them.

## Pathing Model

The path map now keeps an immutable terrain layer separate from the baked
static-obstacle layer. When a destructable changes state, static obstacles are
rebuilt from terrain plus the current footprints of live entities. This removes
an alive footprint without erasing terrain restrictions and permits a type's
optional death-pathing texture to replace it.

Walkable destructables use a narrower contract. While alive, blocked cells in
the authored pathing texture retain the bridge rails, while clear cells only
replace terrain no-walk when they are enclosed by blocked authored pathing
across a texture axis. Clear padding outside those rails leaves the underlying
terrain untouched, so water beside the bridge does not become an alternate
crossing route. This derives the live deck from the pathing texture itself; it
does not use TGA alpha, model bounds, destructable radius, or a hard-coded
bridge width. The surface overlay is baked before normal obstacles so it cannot
erase an overlapping building, and the live bridge is not treated as a circle
blocker during movement. On death it stops supplying bridge support, terrain
pathing is restored, and its optional `pathTexDeath` enters the normal
static-obstacle bake. The authored TGA is not modified or widened, and there is
no bridge-specific collision-radius exception. For rectangular destructable
textures, WC3 first applies the authored path-texture axis transpose and then
the entity's nearest quarter-turn from `s.angle`; square textures keep the same
cell footprint for every quarter-turn. `M_CheckGround` consumes the same
transform for walkable-surface support bounds, so the routeable deck and the
support-height region remain aligned. This facing policy applies to all
destructables with path textures, regardless of `targType`; unit/building path
textures remain axis-aligned pending a separate contract review.

## Phase Boundary

This slice implements the first eight steps of the clean-room specification:
placement life, runtime health, normal attacks, lethal detection, one-time
death, death animation, post-death target disabling, and pathing replacement.

Inline configured item sets, ordinary map random-item tables, world-item
spawning, killer event context, and scripted kill/remove/restore behavior are
implemented. Encoded random-item placeholder expansion remains deferred. Death
works normally when no loot or callback exists.

## Validation

The `wc3_destructable.*` in-engine tests cover placement state, hidden and
non-solid objects, callback-independent lethal damage, one-time events and
callbacks, neutral contextual targeting, dead-order rejection, alive-footprint
removal, death-footprint replacement, weighted selection, intentional no-item
results, multiple world-item drops, and one-time loot processing.
`bridge_path_texture_rotation_covers_all_quarter_turns` also bakes a
rectangular YT20 bridge at 0, 90, 180, and 270 degrees, checks both route
orientation and the inverse after death, and verifies the transform dimensions
used by support queries.
Random-table tests additionally cover weighted boundaries, explicit table-number
lookup, multiple table sets, missing/empty data, encoded-placeholder rejection,
normal world-item state, and one-time spawning.

## Confirmed Failure Modes

- Blob shadows persisted because death kept the alive shadow state. The death
  transition now sets `RF_NO_SHADOW`; restore and scripted activation clear it.
- Death emitters persisted because the held final death frame continued to be
  evaluated. The WC3 MDX renderer now stops new emission once a non-selectable
  death remain has `oldframe == frame`; particles already emitted expire normally.
- Campaign crate loot was attached to hidden `war3map.doo` placeholders while
  generated `war3map.j` handles were being created separately. During initial
  script execution, `CreateDestructable` rebinds the matching preplaced object,
  activates it, and preserves its parsed inline and table drop metadata.

Temporary diagnostics used to establish these causes were removed after the
transitions and regression tests were added.

For a bounded path-texture diagnostic build, enable `WC3_DEBUG_ROUTING=1` and
run the relevant engine test. The router reports authored dimensions, stamped
dimensions, the selected quarter-turn, and live-deck counts for each walkable
path texture:

```sh
make WC3_DEBUG_ROUTING=1 test-wc3-engine WC3_PATTERN='wc3_destructable.*'
```

Scripted-lifecycle tests cover silent dead creation, kill versus remove,
zero-life death, positive-life restoration, birth/stand animation selection,
second death after restoration, and JASS `GetTriggerWidget()` / `GetKillingUnit()`
context.
