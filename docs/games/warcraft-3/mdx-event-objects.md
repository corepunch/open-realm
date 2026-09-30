# Warcraft III MDX Event Objects

Warcraft III MDX `EVTS` records are renderer-owned presentation events.  They
fire when an animation crosses an authored event key; gameplay simulation does
not need a network entity for the child presentation they create.

## Contract

An event object's first three name characters select its event family.  Classic
Warcraft data leaves the fourth character as a separator and uses the remainder
as the row key.  For example:

```text
SPNxSomeSpawn
    ^^^^^^^^^ row key in Splats\SpawnData.slk
```

`MDLX_EventObjectId()` owns that parsing and trims padding from the row key.
`MDLX_EventKeyCrossed()` owns sequence/global-sequence timing, including wraps.
`R_UpdateEntityPresentation()` consumes those events before world-entity
frustum culling, but only for client-visible entities and only in the color
pass.  The shadow-map pass must not play sounds or create duplicate children.
Entity-camera scenes keep their event clocks separately from game edict numbers
and draw retained `SPN` children in the source model's camera view. Each live
camera entity supplies a stable, unique `renderEntity_t.instance_id`; that key
is independent of both the model and the synthetic entity number, so two
instances of one model do not share an event clock or retained child. Replacing
an instance ID's model or evicting its bounded event-clock entry clears that
instance's retained children. UI entities use synthetic numbers, so sharing the
edict-indexed clock can reseed an event on every frame; omitting the camera
child pass makes one-frame effects such as the main-menu Infernal meteor
disappear immediately after spawning.

Current runtime consumers are:

| Prefix | Data source | Behavior |
|---|---|---|
| `SND` | `UI\SoundInfo\AnimLookups.slk` -> `AnimSounds.slk` | Play the authored animation sound at the animated event node. |
| `SPN` | `Splats\SpawnData.slk` | Spawn the row's `Model` at the animated event-node transform and play sequence 0 once. |
| `SPL` | `Splats\SplatData.slk` | Create a renderer-owned terrain splat at the animated event-node position, using the authored texture-atlas life/decay ranges and colour phases. |
| `FPT` | `Splats\SplatData.slk` | Use the same SplatData path for footprint event objects; placement comes from the animated event-node transform. |
| `UBR` | `Splats\UberSplatData.slk` | Create a renderer-owned terrain splat at the animated event-node position and apply the authored birth/pause/decay colour phases. |

Warsmash uses the same `SPN` lookup chain: `EventObjectEmitterObject` loads
`Splats\SpawnData.slk`, reads the row's `Model`, and `EventObjectSpn` creates an
independent model instance at the event node's world location/rotation/scale,
plays sequence 0, and hides it after that sequence reaches its end:

- <https://github.com/Retera/WarsmashModEngine/blob/main/core/src/com/etheller/warsmash/viewer5/handlers/mdx/EventObjectEmitterObject.java>
- <https://github.com/Retera/WarsmashModEngine/blob/main/core/src/com/etheller/warsmash/viewer5/handlers/mdx/EventObjectSpn.java>

## `SPN` data flow

```text
parent MDX sequence crosses EVTS key
        ↓
MDLX_EventObjectId("SPN")
        ↓
Splats\SpawnData.slk row
        ↓ Model
R_LoadRegisteredModel()
        ↓
MDLX_EventWorldTransform()
        ↓
renderer-owned transient instance
        ↓
sequence 0 from its authored first frame
        ↓
remove when sequence 0 ends
```

The transient snapshots the event-node matrix rather than retaining the parent
entity.  This is required for death events: the source unit may disappear while
the spawned explosion/debris model is still animating.  `SPN` effects therefore
live in a small renderer-only pool and never add `entityState_t`, save-game, or
game-module fields.

`MDLX_EventWorldTransform()` keeps the animated node's rotation/scale basis and
sets its translation to the transformed authored pivot.  This mirrors the MDX
node transform consumed by attachments while ensuring a non-zero event pivot
places the spawned model at the event point rather than at the parent origin.

`SpawnData.slk` rows cache registered model handles lazily.  Map registration
releases those borrowed handles, reloads the table while the map archive is the
priority source, and clears active transients.  That keeps model-cache ownership
with the shared renderer registry and permits map/archive overrides to take
normal filesystem precedence.

If every transient slot is occupied, the oldest active presentation is
replaced.  Event effects are visual-only, so bounded presentation memory is
preferred to growing simulation state.

## Goblin Land Mine consequence

The land-mine regression that motivated this consumer is specific: after
`Death Spell` selection was fixed, the mine's model-owned lingering fire became
visible while the immediate blast remained absent.  OpenWarcraft3 was parsing
that sequence's event objects but discarding every non-`SND` family.  Supporting
Warcraft's standard `SPN`/`SpawnData.slk` child-model contract closes that
renderer gap without adding mine-specific presentation code.  The land-mine
gameplay damage remains `Amnx`; `SPN` is presentation only and must not apply
damage or dispatch gameplay events.

See [Land Mines](land-mines.md) for the gameplay chain.

## `SPL` / `FPT` data flow

`SPL` and `FPT` event objects resolve `Splats\SplatData.slk` in renderer asset scope.  The row supplies `Dir`, `file`, `Rows`, `Columns`, `Scale`, `Lifespan`, `Decay`, the lifespan/decay UV frame ranges, Start/Middle/End RGBA values, and retained `BlendMode`, `Water`, and `Sound` metadata.  The event snapshots the animated event-node world position into the same bounded renderer-only transient pool used by UBR.

The terrain splat renderer accepts an explicit UV rectangle, so a SplatData atlas cell remains terrain-conforming even when the splat crosses tile boundaries and the generated polygons are clipped.  Atlas cells are numbered left-to-right, top-to-bottom from zero.  During `Lifespan`, the consumer advances once from `UVLifespanStart` to `UVLifespanEnd` while interpolating Start -> Middle colour; during `Decay`, it advances once from `UVDecayStart` to `UVDecayEnd` while interpolating Middle -> End colour.

`LifespanRepeat` and `UVDecayRepeat` are parsed and retained but not yet applied: their exact retail repeat-count/wrap contract remains unverified. Likewise `BlendMode`, `Water`, and `Sound` are preserved but not guessed into renderer/audio behavior. A used row with one of those unsupported non-default fields produces one bounded warning, so the partial presentation contract is not silent. This keeps the implemented subset deterministic and data-driven without approximating the remaining semantics.

When both `Lifespan` and `Decay` are zero, the current approximation draws the
splat on its event frame and retires it immediately. The retail lifetime meaning
of this data shape is not established. Such rows produce one warning per row
and map scope; the one-frame display must not be described as retail behavior.

## `UBR` data flow

`UBR` events resolve `Splats\UberSplatData.slk` in renderer asset scope, so map
archive overrides take the same precedence as `SPN`.  The row supplies `Dir`,
`file`, `Scale`, `BirthTime`, `PauseTime`, `Decay`, and the Start/Middle/End RGBA
values. `BlendMode` and `Sound` are also parsed and retained.  The event snapshots the animated event-node world position into a
bounded renderer-only transient.  Birth interpolates Start -> Middle, Pause
holds Middle, and Decay interpolates Middle -> End.  The renderer uses the same
terrain-conforming splat primitive as existing entity UberSplats; no gameplay
entity or save/network state is created.

The current generic splat primitive uses the engine's existing alpha-blended
UberSplat path. `UberSplatData.BlendMode` values beyond the default blend path
and the optional `Sound` field are not yet interpreted by this event consumer;
the renderer emits one bounded warning for a used row with those retained fields
until a verified retail blend/audio contract is implemented.

When `BirthTime`, `PauseTime`, and `Decay` are all zero, the current
approximation draws the UberSplat on its event frame and retires it immediately.
The retail lifetime meaning of this shape is unverified and is reported once
per row and map scope.

## Nested child events

`SPN` children now retain independent previous-frame/render-time event state.
Their own `SND`, `SPN`, `SPL`, `FPT`, and `UBR` keys are dispatched with the child's captured
world transform, so a spawned effect can play its own sound, create another
spawned model, or stamp an UberSplat.  Nested `SPN` creation is capped at four
presentation levels.  The cap is renderer safety only: already-created child
models continue to render, but deeper child creation is suppressed instead of
allowing cyclic custom assets to recurse indefinitely.

## Remaining event-data gaps

The retail archives contain models using all five Classic event families.  Their
current consumers implement the common sound, child-model, terrain-decal, and
UberSplat paths, but this is not full retail parity.  The gaps below distinguish
data that is parsed and retained from behavior that is actually rendered.

### ParticleEmitter1 (`PREM`)

Retail MDX files contain top-level `PREM` chunks.  The loader parses emitter
records and their `KPEE`, `KPEG`, `KPLN`, `KPLT`, `KPEL`, `KPES`, and `KPEV`
tracks, registers emitter nodes with the model, and releases the owned data with
the model.  **No ParticleEmitter1 particles are emitted or drawn.** Loading a
model with `PREM` therefore produces a bounded warning; the retained records
are not a visual fallback.

The missing runtime work includes sampling animated emitter properties,
generating particles over time, resolving `EmitterUsesMDL` versus
`EmitterUsesTGA`, and matching authored local directions to the renderer's
coordinate axes. These semantics have not been established sufficiently to
implement them by analogy with `PRE2`; this document does not claim that the
two emitter formats are interchangeable. Until validated against stock output,
models whose appearance depends on `PREM` will be missing those particles.

### `SPL` / `FPT` SplatData fields

`SPL` and `FPT` currently use atlas frames and Start/Middle/End colors for one
life phase followed by one decay phase. `LifespanRepeat` and `UVDecayRepeat`
are parsed but not applied; retail repeat-count and frame-wrap behavior remains
unknown. `BlendMode`, `Water`, and `Sound` are also parsed, but this event path
does not implement their blend, water-surface, or audio behavior. Non-default
values on a used row produce one bounded warning rather than being presented as
supported.

### `UBR` UberSplatData fields

The event path applies `Scale`, `BirthTime`, `PauseTime`, `Decay`, and the
Start/Middle/End colors through the renderer's existing alpha-blended terrain
splat primitive. `BlendMode` and `Sound` are parsed but not interpreted by this
consumer. Non-default values produce one bounded warning. Other retail blend
modes and the row's optional sound behavior remain unverified.

### Event families and dispatch limits

The `SND`, `SPN`, `SPL`, `FPT`, and `UBR` prefixes all have consumers, but
unknown event prefixes are skipped after one warning per prefix and map scope.
Missing event rows/models and nesting-limit hits are also warned once per event
key and map scope. A fixed warning cache suppresses further distinct warnings
after capacity is reached and emits one cache-full warning. Nested child models
dispatch these same families only up to the renderer's four-level `SPN` nesting
limit. Events from deeper children are suppressed for safety. This is a renderer
limit, not a verified Warcraft limit.

The renderer event clock also includes a client-local entity incarnation value.
Observed entity remove/re-add transitions seed a fresh event clock even when the
reused edict slot resolves to the same model. This value is presentation-only;
it is not in snapshots, saves, or the wire protocol.

The `EVTS` chunk and its key records are parsed. This commit did not introduce
that base parser; it extended runtime dispatch to additional event families and
added the separate `PREM` record parser. The `PREM` parser is used to retain
stock model data, but it currently does not provide the corresponding visual
runtime behavior.

## Verification

Headless renderer tests cover:

- sequence and global-sequence event-key crossing;
- `SPN` event-name row-key extraction and padding trim;
- event-node pivot placement in the captured world transform;
- `SPL` atlas-cell selection, event-node placement, and life/decay colour phases;
- `FPT` row-key parsing through the shared SplatData path;
- `UBR` event placement and birth/pause/decay lifetime;
- nested renderer-child event dispatch uses the child transform and independent event state;

`games/warcraft-3/tests/resources-src/Splats/SpawnData.slk` supplies a minimal
`TestSpawn` row pointing at the generated `TestUI\Models\quad_sprite.mdx`
fixture so the test archive contains the same canonical table path as retail.

The remaining property is visual output of the spawned child model.  The
headless renderer suite cannot prove framebuffer appearance; the bounded manual
regression is a stock Goblin Land Mine detonation, where `Death Spell` should
show both the immediate spawned explosion and its model-authored lingering fire.
