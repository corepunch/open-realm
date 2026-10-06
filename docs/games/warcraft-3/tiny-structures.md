# Tiny Structures (TFT)

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
only the stock Great Hall mapping is expanded to other player races.
Displacement failure now rejects the cast before item consumption.

## Follow-up parity work / manual validation

This is a **partial retail parity implementation**, not an assertion that
all of the TFT presentation and targeting behaviour is complete:

- Confirm the stock `AIbg` racial variant for all four owner races and
  custom-map alias handling; the stock code uses the owner's race.
- Manually verify green/red placement footprints, preview model, cancellation
  and re-selection in the retail campaign with actual object data.
- Verify movement-to-cast-range and mid-cast item transfer/death handling in
  the shared asynchronous item pipeline, especially item charge commitment.
- Confirm custom ability aliases, data overrides, timing, and construction
  event ordering against retail.
- Check build-on resources (e.g. unusual custom structures), blight and
  placement error messages; specialised worker-build side effects may still
  differ from item placement in custom maps.
- Verify the in-progress model and completed mechanics in `HumanX01.w3x`:
  Tiny Altar of Kings, Tiny Barracks, Tiny Castle, and the item-based Great
  Hall across races. Confirm zero and positive authored durations.
- Add dedicated headless ability/inventory/placement integration fixtures
  once the relevant object-data fixture rows are available.

## Code ownership

- `game/skills/s_skills.c`: ability registration
- `game/skills/s_item.c`: item activation and selection of target building
- `game/g_building.c`: authoritative autonomous construction lifecycle
- `game/g_save.c`: construction pool persistence

The generic client must not decide Warcraft building legality. See
[Ability Coverage](architecture/ability-coverage.md),
[Ability Implementation](ability-implementation.md), and
[Save/Load](save-load.md).
