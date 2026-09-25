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

Current runtime consumers are:

| Prefix | Data source | Behavior |
|---|---|---|
| `SND` | `UI\SoundInfo\AnimLookups.slk` -> `AnimSounds.slk` | Play the authored animation sound at the animated event node. |
| `SPN` | `Splats\SpawnData.slk` | Spawn the row's `Model` at the animated event-node transform and play sequence 0 once. |

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

## Unsupported event families

OpenWarcraft3 still does not execute the other classic model-event presentation
families:

- `SPL` / `FPT` (`Splats\SplatData.slk`);
- `UBR` (`Splats\UberSplatData.slk`).

They should use the same event-key dispatcher when implemented, but their
texture/lifetime/blend contracts are separate from `SPN` model spawning and are
not inferred here.

A model spawned by an `SPN` event currently plays its own geometry, particles,
ribbons, and attachments, but nested `EVTS` from that renderer-only child are
not recursively dispatched.  Add that only when a stock/custom asset requiring
nested event objects supplies a concrete compatibility case.

## Verification

Headless renderer tests cover:

- sequence and global-sequence event-key crossing;
- `SPN` event-name row-key extraction and padding trim;
- event-node pivot placement in the captured world transform.

`games/warcraft-3/tests/resources-src/Splats/SpawnData.slk` supplies a minimal
`TestSpawn` row pointing at the generated `TestUI\Models\quad_sprite.mdx`
fixture so the test archive contains the same canonical table path as retail.

The remaining property is visual output of the spawned child model.  The
headless renderer suite cannot prove framebuffer appearance; the bounded manual
regression is a stock Goblin Land Mine detonation, where `Death Spell` should
show both the immediate spawned explosion and its model-authored lingering fire.
