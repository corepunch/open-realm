# Runtime Unit Spawn Lifecycle

## Contract

`SP_SpawnAtLocation()` is the presentation-aware runtime spawn path. It binds
unit data, initializes the edict, installs the normal unit callbacks, and calls
`birth`, which selects the birth animation and stores the authored build time in
`edict.wait`.

`SP_SpawnAtLocationNoBirth()` performs the same initialization without that
presentation lifecycle. It is appropriate when the caller owns the resulting
state, such as restoring a gold-mine overlay.

The runtime spawn path links the edict only after `SP_CallSpawn()` completes.
`SP_SpawnUnit()` derives a building's collision radius from its authored data;
linking first leaves the server broad-phase bounds at zero and lets movers walk
through the building even though its entity state reports collision.

JASS `CreateUnit` is an immediate creation operation. `unit_create()` therefore
uses the no-birth path, enters `stand` once, applies the requested facing, and
activates food. It must not call `SP_SpawnAtLocation()` followed by `stand()`:
`stand()` changes the animation but does not clear the birth wait, leaving a
ready unit with a stale build-time delay.

Public mobile creation uses the verified32-ring fine footprint admission through
Move's `S_InitUnitPosition`, including stationary neighbours and static pathing.
The initial scalar write starts from retail's `-128000.5` native-fine sentinel;
preserving cancellation is necessary for exact fractional GetUnitX/Y results.
The accepted pose survives save/load and begins ordinary scheduled Move without
losing its native bits. If no point is admitted, creation retains its handle and
logs the unresolved unit/coordinates. See [original public spawn evidence](retail-pathfinding-engine.md#public-spawn-admission-and-initial-mover-pose).

Movement-disabled units keep the requested point. Warsmash resolves `movetp`
through `PathingGrid.getMovementType()`; a value that names no movement type
becomes `MovementType.DISABLED`, which `isPathable()` accepts everywhere and
`intersectsAnythingOtherThan()` never collides. Retail `Units\UnitData.slk`
authors `_` on all 136 building and scenery rows (farms, towers, the `nfrm`
Frostmourne pedestal) and one of `foot`/`horse`/`fly`/`hover`/`float`/`amph` on
everything else. `M_UnitMoveDisabled()` in `skills/s_move.c` is that class, and
Public admission and `G_CanRepositionUnitAt()` consult it, so `CreateUnit`, `SetUnitPosition` and
`SetUnitPositionLoc` agree. A row with no `movetp` cell at all stays mobile.

Do not classify scenery from speed or weapons: an earlier heuristic (no movement
type + zero speed + no enabled attack, `CreateUnit` only) left towers nudged,
farms not, and `SetUnitPosition` inconsistent with `CreateUnit`.

The public creation search snapshots quantized live-unit occupancy once per
request and uses the verified class-sized fine rectangles. Candidate tests
reuse that snapshot; they do not rescan the full actor set for each cell.

Construction, training, and summons use `SP_SpawnAtLocation()` directly because
their owning systems may consume the birth presentation or replace it with a
construction/hidden lifecycle afterward.

## Verification

`wc3_api.createunit_static_scenery_keeps_requested_spawn` verifies that a
stock-shaped Frostmourne row keeps its requested point when pathing is blocked.
`wc3_api.createunit_custom_static_scenery_keeps_requested_spawn` verifies a
custom rawcode inherits the scenery rows and applies a non-stock max-health
override without moving.
`wc3_api.movement_disabled_armed_unit_keeps_requested_position` verifies an
armed `_` row with a non-zero speed cell through both `CreateUnit` and
`SetUnitPosition`. All live in `games/warcraft-3/game/tests/t_api.c`.

`wc3_api.createunit_starts_ready_without_birth_delay` separately verifies that
the created unit enters `stand` with `wait == 0`, and
`wc3_api.createunit_links_building_collision_bounds` covers the corresponding
server-link contract with a synthetic building row and verifies that its
collision-sized bounds are visible to `BoxEdicts()`.

Run both Warcraft III data modes with:

```sh
make test-wc3-engine WC3_PATTERN='wc3_api.createunit_*'
```

See [Human07 Mission Troubleshooting](human07-troubleshooting.md) for the complete
mission investigation, deferred `RemoveUnit` semantics, `SuicidePlayer`, and the
confirmed building-link regression.


## Ordered construction and shared defaults

`g_construction.h` declares the construction boundaries. `SpawnUnit` executes
flat `unit_construct_*` stages using a stack-local `unitConstruction_t`.
It captures the original row pointers and path-texture string once. Capturing
pointers does not copy or freeze the underlying metadata table: supported object
data edits must advance its generation. A callback rebind does not replace the
captured balance/data/UI/weapons rows with the new unit's rows.

| Stage | Inputs and writes | Observable ordering / invalidation |
|---|---|---|
| Allocate/request | Zero edict, identity, requested XY/Z, owner | Lowest eligible edict and reuse cooldown; no batch reservation of identities |
| Bind/owner | Nine data-row references, owned-pool sequence | Nested creation receives its own sequence at this same boundary |
| Visuals | Live classification/model and captured UI scale/radius | Geometry precedes initialization callbacks |
| Abilities | Existing ordered `A_UNIT_INIT` dispatch | Callbacks may rebind data, change owner/geometry, add abilities or create units; never restart earlier callbacks |
| Stats | Captured balance/data/weapons; live stock and ability traits | Health, mana, speed, sight and acquisition publish after callbacks |
| Lifecycle | Captured movement category, live owner and abilities | Monster flags, neutral sleep and thinker selection |
| Combat | Captured balance/weapons; live Hero and player upgrades | Shared attack defaults become visible here; earlier callbacks still see zero or their own writes |
| Assets | Live class/projectiles, captured path-texture string | Cargo, footprint/collision, team and vertex color preserve their former order |
| Support/sounds/autocast | Live world, rows and ability list | Ground height before sounds; default autocast uses the current ability row |
| Monster | Live movement/aura query and stand/gold-mine behavior | Initial animation consumes RNG at the existing point |
| Server/fine publication | Requested pose and initialized collision | Both publications remain before public placement; ranks are not collapsed |
| Hero/birth/UI | Current unit plus original caller's owner argument | No-birth creation still executes bookkeeping |
| Public placement/stand/facing | Move admission and exact retail scalar functions | Region callbacks may run; second stand remains before facing |
| Food/AI/checkpoint | Completed unit | Native returns synchronously; presentation checkpoint follows authoritative changes |

`G_UnitRuntimeType` in `g_unit_type.c` replaces the independent direct-mapped
row/type/resource caches with address-stable records. Its hash index retains the
latest record for each rawcode; replaced versions remain allocated until the
level cache is cleared. The constructor acquires its record once and uses its
prepared bindings. Later resource stages reacquire the live type if a callback
changes the rawcode or metadata generation. Combat keeps the captured rows.
Resource bindings are explicitly derived mutable state, separate from the
immutable attack values received by instances.

The definition also retains its ordered initialization plan. A registration
reset advances an epoch before freeing cached plans; the next constructor
reacquires its plan before dereferencing it. `UNIT_INIT_RUN` remains a mutation
barrier. Move declares `UNIT_INIT_RUN_LOCAL` for its initialization, which changes
its own movement/repulsor state without callbacks or metadata/registry/ability
mutations. Consecutive folded no-ops and local initialization avoid repeating
metadata checks; ordinary callbacks force revalidation. Every procedure still
has its live fingerprint checked, so a replacement takes the full path without
repeating earlier callbacks.

Attack owns shared configuration in `skills/s_attack_profile.c`.
`S_AttackProfileRead` does not allocate. `S_AttackProfileWrite` copies the current
logical profile into the unit's optional typed override pool on the first write.
Both slots retain distinct identities, including all-zero profiles, because
attack target selection uses slot identity. Creation compiles the authored
profile once per definition and publishes its pointer at the combat boundary.
Projectile defaults are similarly shared after their original registration
point. Hero bonuses, upgrades, morphs and other gameplay writes use the explicit
write accessor. Timers, swing state and targets remain in the mutable unit.

Immutable profiles are interned only during preparation/restoration, never by
ordinary reads or gameplay mutations. An object-data edit cannot change the
values already received by an existing unit. Initialization preserves previous
origin, projectile and runtime-bonus values where the old constructor did not
assign them. Owned overrides are released with the edict. Save format 104 stores
logical defaults plus an override mask and values, rather than process pointers;
see [save/load](save-load.md).

Appending media keeps existing resource handles valid. `SV_SetConfigString`
invalidates binding revisions when changing an occupied slot; no-op writes and
appends still update the lookup/synchronization machinery without invalidating
resolved handles. Explicit namespace/world resets advance the revision.

### Instance layout after attack separation

The field-layout audit groups the remaining storage by ownership:

| Group | Current storage | Classification |
|---|---:|---|
| Engine/network prefix, before `class_id` | 188 bytes | Retain shared Quake-style edict contract |
| Two attack default/override pairs | 32 bytes | Shared immutable defaults plus optional owned state |
| Movement state | 664 bytes | Mutable simulation state; retained inline |
| Bound object-data row references | 72 bytes | Instance binding identity; retained for live rebind semantics |
| Animation request/properties | Two 8-byte immutable references | Shared logical text; edits publish new records |
| Sound state | 16 bytes plus one 8-byte profile pointer | Shared immutable variants; pending events stay per instance |
| Ability-specific state | Typed pool pointers | Existing sparse ownership and persistence |
| Runtime-type/resource bindings and fog checkpoints | External derived records | Rebuildable indexes, excluded from saved pointers |

The x86-64 layout after attack separation was 2608 bytes per edict, down from 2784. The former two
104-byte attack values become 32 bytes of default/override pointers, removing
176 bytes per instance and 704 KiB of clearing for 4096 units. The server-shared
prefix ends at `class_id` offset 188 and is unchanged. Other measured groups are
movement state (now 664 bytes), bound row references (72 bytes), and sound defaults/events
(originally 76 bytes). These remained instance storage in the attack change; the layout reduction
alone does not establish the requested constructor/frame-time targets.
The retained-destination transform memo subsequently adds eight bytes (2616-byte
edict, save format105). Its world-request key and transform revision are derived
state and cleared on load; it avoids repeating public coordinate conversion on
every movement owner tick.
Sound-profile separation then reduces the edict to 2560 bytes (format106).
Each unit retains 16 bytes of pending events and one immutable profile reference,
removing the warm constructor’s 76-byte sound copy. Resource registration and
explicit profile edits freeze logical values through `G_SetUnitSoundProfile`;
readers never allocate. Existing definitions survive resource cache resets until
game shutdown, and saved profiles are logical values rather than process pointers.

Attack defaults are prepared once and shared. The first-movement regression confirms that an ordinary movement order
requires no attack override; actual attack mutations allocate their own
104-byte slot. Overrides use the existing typed-pool lifecycle, with separate
pools for each weapon slot. Immutable values remain alive until game shutdown,
including across definition-cache reset and save restoration.

### Constructor trace regression

`wc3_spawn.construction_stages_preserve_captured_rows_nested_creation_and_rng`
records 26 boundaries per unit. Its fixtures cover ordinary creation, callback
row replacement and nested creation, including fractional/negative requested
positions. `tests/construction_trace.h` contains the logical fingerprints captured
from the instrumented original constructor before stage extraction (source301).
The source302 extraction matched all 104 records in both Classic and TFT.
The fingerprints exclude pointers, padding and resource index numbering; they
include exact float words, both attack profiles, callback counts, allocation and
owner ordering, fine pose and pathing RNG. The test also checks callback-visible
values directly and compares the next animation RNG output between repeats.
This is an engine preservation oracle, not evidence that all retail gaps are closed.

Run the fixture with:

```sh
make test-wc3-engine WC3_PATTERN='wc3_spawn.*'
```

For cross-build inspection, set `WC3_CONSTRUCTION_TRACE` to a fresh report path
when running the test binary. It appends `scenario entity stage fingerprint`
records. Review changes at the affected boundary before updating the reference;
do not regenerate it just to make a failure disappear. Trace calls compile out
of normal game builds.

### Runtime308 validation gate

The complete `make test` passed with 2,624 in-engine tests and 6,878,935 assertions
in each Classic/TFT mode. Release Warcraft III, StarCraft II and World of Warcraft
builds passed. The constructor fixtures additionally preserve distinct old/new
metadata defaults through save/load, verify first movement does not allocate an
attack override, and reacquire prepared ability plans after registry reset.

The real-map report `perf308-icecrown-raw-spawn-and-move-4096.jsonl` under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/` contains the original
6,630 entities including 4,471 trees. Its final positions, membership records and
RNG exactly match `perf221-icecrown-raw-spawn-4096.jsonl`. Synchronous creation is
8.476353 ms CPU, with first-type creation reported separately as 0.780387 ms and
placement preparation as 71.840773 ms. This is still within the old 8.47–8.88 ms
range: the structural change is validated, but has not established a constructor
speedup or the sub-2-ms target. No rendered deadline acceptance is claimed here.

Animation-state separation reduces the edict from 2560 to 2368 bytes (format107).
Prepared type bindings own the normalized authored property record. Each unit
publishes that record at the original visual stage; runtime property edits bind
a separate immutable value. Warm requests reuse their parsed primary family and
resolved selection, preserving walk-variant encounter order and RNG calls. The
192-byte per-instance reduction is 768 KiB for 4096 units. Performance validation
for this step is pending; it does not establish the target by itself.
