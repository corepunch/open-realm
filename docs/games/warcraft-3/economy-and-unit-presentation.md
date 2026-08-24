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

The gold regression followed commit `55724517`, which correctly changed buildings to footprint-authored collision while mine entry
still used a fixed 180-unit radius. Entry now uses worker radius + mine radius + one movement step, so a worker transitions before
the collision solver prevents the next step.

A second stop-at-the-mine failure came from initialization order: `Agld` loaded capacity 1, then the registered `Agl2` placeholder
reused `SP_ability_goldmine` even though no `Agl2` row exists, resetting the shared capacity to zero. At contact, `0 < 0` failed and
the worker waited forever. `a_goldmine_overlayed` deliberately has no initializer until its authoritative data/behavior is implemented.

### Mine Entry — Collision Formula

ROC `PathTextures\16x16Goldmine.tga` is 16x16, but its no-walk (`COLOR32.b`) region is the central 8x8 cells. The existing diagonal
scan therefore produces the correct 128-unit radius (`8 * 32 / 2`). A prior test and proposed column-scan change claimed 192 units;
direct inspection of all 256 decoded pixels disproved that claim. The mine-entry regression belongs in the interaction threshold,
not in a fabricated larger footprint.

### AbilityData.slk Column Names

The archives use different physical schemas for the same semantic data slots:

- ROC `War3.mpq`: `Data<level><slot>` (`Data11`, `Data12`, `Data13`, then `Data21`...).
- TFT `War3x.mpq`: `Data<slot-letter><level>` (`DataA1`, `DataB1`, `DataC1`, then `DataA2`...).

Skill code must call `AB_Data(classname, level, slot)`. Do not pass either archive spelling to `AB_Number`; doing so breaks the other
release. Confirm headers with:

```sh
build/bin/mpqtool -mpq "data/Warcraft III/War3.mpq" cat "Units/AbilityData.slk" | rg ';K"Data' | head
build/bin/mpqtool -mpq "data/Warcraft III/War3x.mpq" cat "Units/AbilityData.slk" | rg ';K"Data' | head
```

Bounded ROC and TFT startups both resolve `Ahar` slots to damage=1, lumber capacity=10, gold capacity=10.

### Lumber gather cycle

Each swing does `HARVEST_TREE_DAMAGE` (slot 1 = 1) HP of damage to the tree and adds the same amount
as lumber. Capacity (`HARVEST_LUMBER_CAPACITY`, slot 2 = 10) fills after 10 swings, triggering
`harvest_walkback`. When a tree's HP reaches zero on the lethal chop, `tree->die()` is called
(m_tree.c owns the fall animation and pathing removal). `tree_die` sets the death sequence and its
first frame in the same transition, so the lethal snapshot cannot retain the upright hit frame.

The deposit transition validates `secondarygoal` before publishing resume. A dead tree is replaced
with the nearest live tree; if none exists, the worker stands. Therefore `RESUME_LUMBER` always names
a live target and a worker never spends a movement tick targeting the tree it just felled.

### Gameplay message stream

`GAMEMSG` is a synchronous game-owned observation stream for tests and diagnostics. It is separate from `GAMEEVENT`, whose values
and dispatch are the Warcraft/JASS trigger contract. `G_SubscribeMessage` installs a callback; `G_UnsubscribeMessage` removes it;
`G_PublishMessage` delivers `{ type, actor, target }`, where actor and target are stable entity numbers rather than edict pointers.

Harvest publishes transitions only: move-to-resource, enter-mine/start-chop, chop/tree-felled, return-to-base, deposit, and resume.
Movement ticks do not publish messages. Tests subscribe immediately before the order and assert both sequence and entity numbers.
The lethal-trip test specifically requires `CHOP -> TREE_FELLED -> RETURN_LUMBER -> DEPOSIT_LUMBER -> RESUME_LUMBER -> START_CHOP`,
with the resume/start target equal to the next live tree rather than the felled entity.

## Immobile Units

`AI_IMMOBILE` is the single no-translation/no-facing-change flag. `SP_SpawnUnit` derives it from authoritative `UnitUI.slk:isBldg`
through `UNIT_IS_BUILDING`; there is no class-ID list. Movement selectors (move, attack-move, patrol) and ground move orders reject
immobile units, and the low-level movement and turn functions enforce the contract for combat and future order paths too. An immobile
tower may still execute actions, but it does not rotate under the current contract. `UNIT_IS_BUILDING` remains the correct lookup for
non-movement classification (shadow type, footprint collision, repair targets, building-kill XP, fog rendering, selection grouping).

## Presentation Geometry

FDF is authoritative for screen-space HUD geometry. Frames that exist in War3.mpq FDF files are loaded via `UI_EnsureFDF` and
bound with generated headers (see `game/generated/`). `InfoPanelBuildingDetail.fdf` owns the building-detail sub-panel including
`BuildTimeIndicator`, `BuildingActionLabel`, and `BuildQueueBackdrop`; C only binds entity state, text, and queue contents into
those frames. Native WC3 frame types that have no FDF in the MPQ (portrait, command button, minimap, tooltip) are constructed as
static `FRAMEDEF` objects in C with inline float literals; do not add `#define` position constants for them.
`ConsoleUI.fdf` and `ResourceBar.fdf` are loaded directly from War3.mpq; the minimap viewport is a C-constructed `FT_MINIMAP`
frame anchored inline.

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
make test-wc3-engine WC3_PATTERN='wc3_combat.ability_data_resolves_roc_and_tft_columns'
make test-wc3-engine WC3_PATTERN='wc3_unit.*'
make test-wc3-engine WC3_PATTERN='wc3_game.hud_*'
make test-wc3-engine WC3_PATTERN='wc3_game.overhead_*'
```

The movement suite covers large-footprint mine entry, the complete gold deposit/resume cycle, exact lethal tree trips with next-tree
selection, the no-live-tree stop path, non-lethal chops, and both sides of the immobility contract. The in-engine fixture
`tests/wc3-engine-data/Units/UnitUI.slk` supplies `isbldg` for the same metadata lookup used by the game.
