# WC3 Economy And Unit Presentation

## Gathering Contract

`unit_issuetargetorder(..., "smart", target)` routes workers with `Ahar` to gold or lumber in `m_unit.c`.
The Gather command reaches the same state machines through `harvest_menu_selecttarget`.

- Gold: `harvest_gold_start` -> walk to mine -> hidden mining wait -> walk to `htow` -> deposit -> resume the mine.
- Lumber: `harvest_start` -> walk into `HARVEST_RANGE` -> swing/damage -> carry lumber -> deposit -> resume or find another tree.
- `s_goldmine.c` uses the worker+mine collision contact radius plus one movement step as the entry boundary. Mine footprints are
  authoritative; do not restore the old fixed 180-unit radius.
- A chop is lethal when tree life is less than or equal to `HARVEST_TREE_DAMAGE`. The lethal path must call `tree->die` because
  `m_tree.c` owns the fall animation and removal of the tree's pathing obstruction.

The gold regression followed commit `55724517`, which correctly changed buildings to footprint-authored collision. A bounded test
showed a 16-unit worker stopping at approximately 208 units from a 192-unit mine while the stale entry threshold remained 196.
The worker could therefore never enter.

## Immobile Units

`AI_IMMOBILE` is the single no-translation/no-facing-change flag. `SP_SpawnUnit` derives it from authoritative `UnitUI.slk:isBldg`
through `UNIT_IS_BUILDING`; there is no class-ID list. Movement selectors (move, attack-move, patrol) and ground move orders reject
immobile units, and the low-level movement and turn functions enforce the contract for combat and future order paths too. An immobile
tower may still execute actions, but it does not rotate under the current contract. `UNIT_IS_BUILDING` remains the correct lookup for
non-movement classification (shadow type, footprint collision, repair targets, building-kill XP, fog rendering, selection grouping).

## Presentation Geometry

FDF is authoritative for screen-space HUD geometry. `game/hud/hud_local.h` still contains legacy construction/training constants
that must be removed as each panel is converted; do not treat those C values as the layout contract or add new geometry there.

Repeated quest rows already have authoritative schemas in Blizzard's `QuestDialog.fdf`. `QuestListItem` and
`QuestItemListItem` own row size and child placement. The server clones those templates, stacks each clone by the template's own
height, then binds title, selection color, and command data to the named children. It must not spawn generic text rows or impose a
parallel width/stride.

Overhead resource bars use two fixed slots in `renderer/r_ents.c`: mana keeps the lower/original slot, and health occupies the slot
above it. Without mana, health still sits one bar height above the projected model point.

## Verification

Focused deterministic checks:

```sh
make test-wc3-engine WC3_PATTERN='wc3_movement.*'
make test-wc3-engine WC3_PATTERN='wc3_unit.*'
make test-wc3-engine WC3_PATTERN='wc3_game.hud_*'
make test-wc3-engine WC3_PATTERN='wc3_game.overhead_*'
```

The movement suite covers large-footprint mine entry, the complete gold deposit/resume cycle, exact lethal tree chops, non-lethal
chops, and both sides of the immobility contract. The in-engine fixture `tests/wc3-engine-data/Units/UnitUI.slk` supplies `isbldg`
for the same metadata lookup used by the game.
