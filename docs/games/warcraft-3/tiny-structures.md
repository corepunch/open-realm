# Tiny Structures (TFT)

## HumanX01 item data and building endpoint resolution

HumanX01 drops the item rawcode `tgrh`. Its `war3map.w3t` entry has no item
modifications, so the item keeps the loaded `ItemData.slk` display name and
ability list. In the runtime data inspected for this map, the name is **Tiny
Great Hall** and the ability list is `AIbg`. That ability's stock endpoint is
the Human Town Hall (`htow`); this is why the preview and placement are a Town
Hall. The campaign's “Tiny Castle” hint text does not change the item data.

The placement cursor and cast execution both call `S_TinyStructureUnitId()`,
so the selected ability rawcode resolves to the same building for preview,
footprint/pathing validation, spawn, and construction. `S_SpellUnitId()` reads
the active ability rawcode's own level data. Only `AIbg` expands its stock
Human Town Hall endpoint to the owner's race; that rule does not apply to
`AIbl` or the other Tiny Structures. It applies only to stock `htow`/`ogre`
endpoints, and an explicit map `UnitID` override takes precedence.

The stock endpoints checked in TFT `AbilityData.slk` are:

| Ability | Stock endpoint |
|---|---|
| `AIbl` Tiny Castle | `hcas` Human Castle |
| `AIbg` Tiny Great Hall | owner-race Tier 1 hall (`htow`, `ogre`, `unpl`, `etol`) |
| `AIbt` Tiny Scout Tower | `hwtw` Human Watch Tower |
| `AIbb` Tiny Blacksmith | `hbla` Blacksmith |
| `AIbf` Tiny Farm | `hhou` Farm |
| `AIbr` Tiny Lumber Mill | `hlum` Lumber Mill |
| `AIbs` Tiny Barracks | `hbar` Barracks |
| `AIbh` Tiny Altar of Kings | `halt` Altar of Kings |

Map ability overrides carrying an AbilityMetaData `UnitID` field now update
the level's typed `unitID`; this also lets the race-dependent `AIbg` rule
distinguish the stock endpoint from a map-authored endpoint.

## First-pass gameplay implementation

The eight stock Build Tiny ability codes (`AIbl`, `AIbg`, `AIbt`, `AIbb`,
`AIbf`, `AIbr`, `AIbs`, `AIbh`) dispatch through `CAbilityTinyStructure` in
`game/skills/s_item.c`. This is a point-targeted *item spell*, not a worker
Build command. The ability reads its `UnitID` and normal duration from the
active ability level data, checks the normal building footprint against the
shared placement evaluator, and spawns an ordinary owned building without
charging a second time for its worker-construction cost.

`AIbg` is special: the stock Tiny Great Hall produces a Great Hall, Town
Hall, Necropolis, or Tree of Life from the **owner player's race**, not the
unit type carrying the item. Other stock Tiny Structures use their authored
`UnitID`. All buildings enter the existing construction lifecycle, with a
building-owned, workerless `CONSTRUCTION_TINY` clock. Completion emits the
normal JASS construction finish events and the building retains its normal
unit data and training/technology functionality.

The item-cast executor returns `false` on placement/spawn failure so the
existing asynchronous item activation path can retain the charge. There is
no added logging. Construction's per-instance duration is serialized in the
existing `construction_t` pool, with save version 67.

## Targeted building preview

Build Tiny item targeting now publishes the same `svc_cursor` model/footprint
entity used by ordinary worker construction, rather than a circular spell
decal. `S_TinyStructureUnitId` is shared by the targeting UI and the ability
executor, including the stock racial Great Hall variants. The generic client
snaps the model and colours placement cells; the game still validates a clicked
position authoritatively. Successful casts and cancelled targeting clear the
cursor. Invalid clicks keep the target mode armed without spending the item.

A Tiny Great Hall with an explicitly different authored UnitID is preserved;
only the stock Great Hall mapping is expanded to other player races. An
explicit map `UnitID` override is retained even when it equals the stock Human
endpoint.
Displacement failure now rejects the cast before item consumption.

## Follow-up parity work / manual validation

This is a **partial retail parity implementation**, not an assertion that
all of the TFT presentation and targeting behaviour is complete:

- Manually verify green/red placement footprints, preview model, cancellation
  and re-selection in the retail campaign with actual object data.
- Verify movement-to-cast-range and mid-cast item transfer/death handling in
  the shared asynchronous item pipeline, especially item charge commitment.
- Confirm custom ability aliases, data overrides, timing, and construction
  event ordering against retail.
- Check build-on resources (e.g. unusual custom structures), blight and
  placement error messages; specialised worker-build side effects may still
  differ from item placement in custom maps.
- Verify Tiny Structure preview and completed construction in the campaign
  with actual object data, along with zero and positive authored durations.
- Headless regressions cover stock endpoint resolution, racial Great Hall
  selection, and W3A `UnitID` overrides. A full inventory-to-placement
  integration fixture is still needed to assert preview/spawn agreement and
  item-charge commitment through the live command pipeline.

## Code ownership

- `game/skills/s_skills.c`: ability registration
- `game/skills/s_item.c`: item activation and selection of target building
- `game/g_building.c`: authoritative autonomous construction lifecycle
- `game/g_save.c`: construction pool persistence

The generic client must not decide Warcraft building legality. See
[Ability Coverage](architecture/ability-coverage.md),
[Ability Implementation](ability-implementation.md), and
[Save/Load](save-load.md).
