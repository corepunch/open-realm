# WC3 Economy And Unit Presentation

## Gathering Contract

`unit_issuetargetorder(..., "smart", target)` routes workers with `Ahar` to gold or lumber in `m_unit.c`.
The Gather command reaches the same state machines through `harvest_menu_selecttarget`.

- Gold: `harvest_gold_start` -> walk to mine -> capacity-gated hidden mining wait -> carry finite mine gold -> nearest live same-owner drop-off accepting gold -> deposit -> resume the mine while it remains harvestable.
- Lumber: `harvest_start` -> walk into `HARVEST_RANGE` -> swing/damage -> carry lumber -> nearest live same-owner drop-off accepting lumber -> deposit -> resume or find another tree.
- If an explicitly clicked live tree is buried behind other trees, routing remains responsible only for reaching the best legal approach.  Once that route is exhausted outside `HARVEST_RANGE`, Harvest selects a reachable replacement tree and continues lumber work, matching retail behavior.  See [WC3 Pathfinding And Harvest Reachability](pathfinding.md).
- `s_goldmine.c` uses the mine's authored no-walk pathing footprint plus the worker radius and one movement step as the primary entry boundary. A collision-circle contact check remains the fallback for mines without a path texture. Mine footprints are authoritative; do not restore the old fixed 180-unit radius.
- A chop is lethal when tree life is less than or equal to `HARVEST_TREE_DAMAGE`. The lethal path must call `tree->die` because
  `m_tree.c` owns the fall animation and removal of the tree's pathing obstruction.

The gold regression followed commit `55724517`, which correctly changed buildings to footprint-authored collision while mine entry
still used a fixed 180-unit radius. A scalar building collision radius is sufficient on a face but not at a square footprint corner,
where static pathing may stop the worker at a larger centre-to-centre distance. Entry therefore measures the worker against the mine's
authored no-walk footprint and transitions when the worker radius plus one movement step would touch it. The historical
worker+mine collision+step formula remains a fallback when no path texture is available.

Gold-mine tuning is no longer copied into process-wide globals. `s_goldmine.c` resolves the mine entity's own `UnitAbilities.slk`
list, follows `AbilityData.slk:code` to the `Agld` base ability, and reads Data1/Data2/Data3 as maximum gold, mining duration, and
internal mining capacity. This is required for custom maps where two `Agld`-derived abilities can configure different capacities or
durations in the same simulation. `Agl2` remains a separate marker until its overlay behavior is implemented; it cannot overwrite
`Agld` mining data.

### Gold Mine Capacity And Occupancy

The familiar "five workers on gold" is economic saturation, not mine occupancy. Stock `Agld` data resolves to mining capacity 1, so
only one worker may be hidden inside a normal Human/Orc mine at a time. Additional workers retain their Harvest state outside the
mine and are reconsidered when a miner exits. The authoritative occupancy counter remains `mine->peonsinside`; each admitted worker
also stores the exact mine pointer and spawn generation in `worker->goldmine` so duplicate admission cannot increment the counter
twice and an edict recycled while a worker is hidden cannot be decremented accidentally.

Entry sets `RF_HIDDEN` and temporary invulnerability. The generic `paused` flag is intentionally not used because
`monster_think()` returns immediately for paused units and would stop the internal mining timer as well. Instead, the three normal
order entry points reject orders while `S_GoldMineWorkerIsInside()` is true. Exit always unregisters once, restores the worker's prior
invulnerability state, reveals it, and wakes waiting workers. `G_FreeEdict()` also calls `S_GoldMineReleaseWorker()` before clearing an
entity, so scripted removal of an inside miner cannot strand the mine's occupancy counter.

The mine's `resources` field is its remaining gold. `S_GoldMineInitUnit()` initializes a newly spawned `Agld`-derived mine from the
ability's maximum-gold field; map/JASS `SetResourceAmount` can then override that value normally. A completed mining interval transfers
`min(mine->resources, HARVEST_GOLD_CAPACITY)` to the worker and subtracts exactly that amount from the mine. A partial final trip is
therefore possible. When the remaining amount reaches zero, the mine enters its death/depleted state before waiting workers are
woken, so none can enter an empty mine. A worker that deposits the final trip does not resume walking back to the depleted mine.

### Resource Return Drop-Offs

Resource return is capability-driven rather than keyed to a building class ID. `S_CanReturnResourceAt` reads the candidate's
`UnitAbilities.slk:abilList` and accepts the stock Return Resources configurations as follows:

- `Argd`: gold.
- `Arlm`: lumber.
- `Argl`: gold and lumber.

For custom abilities whose `AbilityData.slk:code` resolves to the `Artn` Return Resources base ability, data slots A/B provide the
gold/lumber acceptance flags. This preserves the same capability model for custom drop-off units instead of adding Town Hall or
Lumber Mill rawcode checks to worker logic.

`S_FindNearestResourceDropoff` scans live, in-use entities owned by the worker's player and chooses the compatible candidate with
the smallest `Vector2_distance` from the worker. This is geometric distance; it does not pathfind to every candidate and compare
route lengths. A Human Lumber Mill therefore competes with a Town Hall for lumber return and wins when it is geometrically closer,
while a Lumber Mill remains ineligible for gold.

Return completion is footprint-aware for buildings that expose an authored pathing texture. The worker deposits when its collision radius plus one simulation step reaches the building's no-walk footprint; the older worker+building collision+step test remains the fallback when no path texture is available. This prevents a Peasant carrying gold from stopping at a Town Hall corner while still outside the scalar centre-circle threshold. The return move revalidates its target before each movement/deposit tick. If the selected drop-off dies, is removed, changes owner, or no longer exposes a compatible Return Resources ability, the worker retargets the nearest remaining compatible drop-off. If none exists, the worker stands while preserving the carried resource and carry visual.

### Mine Entry — Collision Formula

ROC `PathTextures\16x16Goldmine.tga` is 16x16, but its no-walk (`COLOR32.b`) region is the central 8x8 cells. The existing diagonal
scan therefore produces the correct 128-unit radius (`8 * 32 / 2`). A prior test and proposed column-scan change claimed 192 units;
direct inspection of all 256 decoded pixels disproved that claim. The mine-entry regression belongs in the interaction threshold,
not in a fabricated larger footprint.

### Remote Mine-Entry Reports

First collect the executable/game-module revision, device OS, archive release (ROC/TFT and patch), exact map, and whether one worker
also gets stuck. History matters: `5f6d4e5d4` added collision-relative entry; `ae9467863` fixed the `Agl2` capacity reset and the
ROC/TFT ability-column lookup. An older package is a hypothesis until its revision is confirmed. The August 26 handheld report
states the latest build from August 25 was used, so do not attribute that report to the August 24 fixes without checking its SHA.

Distinguish the states: `ai_walkmine` keeps the walk animation while outside the entry threshold; a full mine switches to
`harvestgold_move_wait` (stand), while an empty/dead/zero-capacity mine is not harvestable. First verify `unit_issuetargetorder` actually
calls `harvest_gold_start`: smart orders require worker `Ahar` and an `Agld`-derived target; otherwise a non-enemy target becomes a
plain move to its center, which also stops at collision. For a failing bounded run, `+set wc3_harvest_path_debug 2` logs `WC3_GOLD_PATH` approach/entry/wait events including centre distance,
worker+mine collision contact, movement step, distance to the mine's authored pathing footprint, occupancy, capacity, resources, and
flow generation. Gold return uses the same cvar and emits `WC3_GOLD_RETURN start`, periodic `approach`, `deposit_range`, and `deposit` transitions so a Town Hall return failure can be distinguished from a mine-entry failure. If the worker remains outside entry range, log rejection in `g_ai.c:move_is_valid` separately for static pathmap and entity-circle collision. Do not enlarge an interaction radius without this evidence.
Movement uses `FRAMETIME` through `unit_movedistance`; low rendering FPS alone does not shrink the per-tick entry allowance.

Build and run the existing gold tests with either local archive set (add `-tft` for TFT):

```sh
make openwarcraft3-tests
build/bin/openwarcraft3-tests -data 'data/Warcraft III' +dedicated 1 +test 'wc3_movement.gold_*' +com_frame_limit 100
```

Historical investigation at `8204597d` confirmed `Agld` as max=12500, duration=1, capacity=1 in both local archive sets. The entry
fixture reached distance=150 with contact=144 and step=10, then entered. Current movement tests inject typed `AbilityData` rows instead
of overriding mining globals; they cover stock capacity 1 with six assigned workers, mixed custom capacities/durations, duplicate
entry/order rejection, finite-gold depletion, and partial final trips. The open test pathmap still verifies the state-machine/circle
boundary rather than a particular map's baked mine footprint.

#### Human02 starting mine

Inspect the campaign script, not just the standalone movement fixtures:

```sh
build/bin/mpqtool -mpq 'data/Warcraft III/War3.mpq' cat Maps/Campaign/Human02.w3m > /tmp/human02.w3m
build/bin/mpqtool -mpq /tmp/human02.w3m cat war3map.j > /tmp/human02.j
rg -n 'ngol|hpea|autoharvestgold|LocationOfGold' /tmp/human02.j
```

The local ROC script places `gg_unit_ngol_0009` at (-4736,-3840). Its three initial gold workers are at
(-4276.2,-3938.2), (-4147.5,-3873.2), and (-4067.4,-3981.1). Both the intro completion and cancellation paths remove/recreate
these workers and issue `autoharvestgold`. `unit_issueimmediateorder` currently implements only `stop`, so automatic campaign
gathering is unsupported; this is separate from a manually ordered worker remaining in walk at the mine.

At `8204597d`, bounded real-map runs after `cmd cancel` reproduced successful manual mining in both local archive modes.
A temporary `G_RunFrame` probe at server time 7000 ms issued normal `unit_issuetargetorder(worker, "smart", mine)` calls for the
three live starting workers; it did not change collision, positions, or mining values. All three resolved `Ahar`/`Agld`, reached
the entry threshold, and entered as the capacity-1 slot became free. ROC logged mine radius=128, worker=16, step=19
(entry threshold=163); local TFT mode logged mine radius=50, worker=16, step=19 (threshold=85). These are observations of the
local archive sets, not universal ROC/TFT constants. Do not use one archive mode's radius to diagnose the other without logging it.

Use the [cinematic skip workflow](cinematics.md#common-issues), with `+set r_vsync 1 +com_frame_limit 2400` to leave time for
the post-intro approach. A 900-frame uncapped run ended before the 7000-ms probe here: `com_frame_limit` bounds main-loop
iterations, not simulation ticks. Temporary probes were removed after investigation. The console failure remains unconfirmed;
obtain its archive/patch version and corresponding order, entry-distance, and static/entity-rejection logs before changing behavior.

### AbilityData.slk Column Names

The archives use different physical schemas for the same semantic data slots:

- ROC `War3.mpq`: `Data<level><slot>` (`Data11`, `Data12`, `Data13`, then `Data21`...).
- TFT `War3x.mpq`: `Data<slot-letter><level>` (`DataA1`, `DataB1`, `DataC1`, then `DataA2`...).

The AbilityData DDX schema maps both spellings into `abilityDataRow_t.data[level][slot]`. Skill code uses that typed array directly or
calls `AB_Data(classname, level, slot)`; physical archive column names never reach gameplay code. Confirm headers with:

```sh
build/bin/mpqtool -mpq "data/Warcraft III/War3.mpq" cat "Units/AbilityData.slk" | rg ';K"Data' | head
build/bin/mpqtool -mpq "data/Warcraft III/War3x.mpq" cat "Units/AbilityData.slk" | rg ';K"Data' | head
```

Bounded ROC and TFT startups both resolve `Ahar` slots to damage=1, lumber capacity=10, gold capacity=10.

### Trained-unit exit placement

A Human02 regression observed while validating the gold/lumber return fixes was traced separately to training placement, not to the
resource deposit thresholds themselves. A bounded runtime trace showed a newly trained Peasant created by Town Hall entity 102 at
`(-3776,-4032)` being moved to `(-3961.8,-4217.8)`: the old `SP_FindEmptySpaceAround` accepted that point using dynamic-circle
collision, while `CM_PointIsPathableForRadius(..., 16)` reported it statically blocked. Every subsequent move was rejected with both
`origin_pathable=0` and `candidate_pathable=0`, leaving the worker permanently stuck.

Training therefore uses a dedicated exit search before revealing the completed unit. `SP_FindUnitExitPosition` walks deterministic
64-world-unit square rings around the producer, up to 300 candidates, and accepts the first point that fits the trained unit's actual
collision radius in the baked static pathmap and does not overlap a dynamic blocking unit. If no point is legal, the completed unit
remains hidden in the training queue and placement is retried on the next training tick; the train-finish event is not published until
placement succeeds. Scripted `CreateUnit` coordinates and the separate resource-return interaction ranges are unchanged.

### Lumber gather cycle

Each swing does `HARVEST_TREE_DAMAGE` (slot 1 = 1) HP of damage to the tree and adds the same amount
as lumber. Capacity (`HARVEST_LUMBER_CAPACITY`, slot 2 = 10) fills after 10 swings, triggering
`harvest_walkback`. When a tree's HP reaches zero on the lethal chop, `tree->die()` is called
(m_tree.c owns the fall animation and pathing removal). `tree_die` sets the death sequence and its
first frame in the same transition, so the lethal snapshot cannot retain the upright hit frame.

The return transition first selects the nearest compatible return building and revalidates it while travelling. The deposit
transition then validates `secondarygoal` before publishing resume. A dead tree is replaced
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

### World-unit hover health

WC3 world hover is presentation-only and remains separate from selection. `client/cl_input_w3.c` ray-picks the world on mouse motion,
then accepts only snapshot entities carrying the generic `EF_HOVER_HEALTH` capability. `G_CustomizeEntity` authors that bit per client
for living `SVF_MONSTER` units/buildings that are selectable and actively visible. This stricter visibility check intentionally differs
from `G_FowPlayerCanSeeEntity`: explored buildings may remain networked while shrouded, but must not expose current HP through hover.

The presentation path is:

```text
TraceEntity -> cl.hover_entity -> cl.viewDef.hover_entity -> R_DrawHealthBars
```

The same hover entity drives the ground selection-preview splat in `renderer/r_ents.c`. `G_CustomizeEntity` derives the
recipient-relative relationship from WC3 alliance state: non-passive allies are `EF_HOSTILE`, passive allies without shared control are
`EF_NEUTRAL`, and own/shared-control units carry neither relationship flag. The renderer maps those states to faint red, yellow, and
green rings respectively, using the normal selection-circle size/texture choice and half-alpha hover presentation. Selection remains a
separate state and suppresses the hover preview while the full selected-unit circle is visible.

The authored WC3 cursor reuses that recipient-relative hover state without changing cursor assets. While the current hover entity carries
both `EF_HOVER_HEALTH` and `EF_HOSTILE`, `client/cl_scrn.c` submits a red per-instance tint to the renderer; clearing/changing hover
submits white, restoring the cursor's original artwork. This step intentionally leaves non-hostile cursors untinted and does not change
the cursor animation sequence. `MDLX_DrawSpriteTinted` stores the tint on the transient cursor render entity, and the MDX renderer folds
it into the existing geoset-color shader value so texture alpha and authored geoset/material colour remain independent.

An August 2026 regression had working world picking and working selected-unit health bars, but active gameplay never copied
`cl.hover_entity` into `cl.viewDef.hover_entity`; the renderer therefore always saw hover entity 0. Targeted runtime logs confirmed the
entity number at input, view, renderer, and health-bar filtering before the fix. Keep the active-view assignment in `V_RenderView`.
The investigative logs were removed after confirmation.

### Command-card command resolution

Command-card clicks carry the original command string from `hud/hud_commands.c` to `CLIENTCOMMAND(Button)` in `g_commands.c`.
There are two command namespaces and they must not be normalized the same way:

- engine commands such as `CmdBuild`, `CmdMove`, and `CmdAttack` are full strings registered directly in `skills/s_skills.c`;
- WC3 ability commands are four-character rawcodes such as `Ahrp`, which must follow `AbilityData.slk:code` to the registered base
  handler (for example `Ahrp -> Arep`).

Use `FindAbilityForCommand()` at the command-card boundary. It preserves non-four-character engine command strings verbatim and only
runs four-character rawcodes through `G_AbilityCodeName()`. Do not pass `Cmd*` names through `FS_SLKKey()`/`G_AbilityCodeName()`: the
SLK key helper intentionally copies at most four bytes, so `CmdBuild` becomes `CmdB` and cannot match the registered `CmdBuild`
handler. The same resolver supplies the command button's `active` ability index; mana-cost lookup remains rawcode-only because engine
commands have no `AbilityData` row.

The August 30, 2026 build-menu regression was confirmed by a bounded runtime trace: the client hit `onclick="button CmdBuild"` and
the server received `CmdBuild`, but pre-dispatch SLK normalization produced `CmdB`, missed `a_build`, and fell through to the selected
unit's `trains` list. Peasants have a `Builds` list rather than a `trains` list, so the visible Build button appeared to do nothing.
The investigative trace was removed after the root cause was confirmed.

### Human building construction

The command-card `CmdBuild` resolver is only the entry point. Authoritative build availability, WC3 grid snapping, placement/pathing validation, arrival-time revalidation, and Human Repair/power-building state live in [building-construction.md](building-construction.md). Use `+set wc3_build_all 1` to bypass tech/prerequisite gates for structures and trained units already present in the selected producer's authoritative `Builds` / `Trains` list; structure resource charges are also bypassed while world-placement validation remains authoritative.

## Verification

Focused deterministic checks:

```sh
make test-wc3-engine WC3_PATTERN='wc3_movement.*'
make test-wc3-engine WC3_PATTERN='wc3_combat.command_lookup_*'
make test-wc3-engine WC3_PATTERN='wc3_combat.ability_data_resolves_roc_and_tft_columns'
make test-wc3-engine WC3_PATTERN='wc3_unit.*'
make test-wc3-engine WC3_PATTERN='wc3_game.hud_*'
make test-wc3-engine WC3_PATTERN='wc3_game.overhead_*'
```

The movement suite covers large-footprint mine entry, mine entry through an authored blocking pathing footprint, the complete gold
deposit/resume cycle, stock capacity 1 under six assigned workers, independent custom mine capacities/durations, non-orderable inside
miners, finite-gold depletion and partial final trips,
trained-unit exit placement against static footprints, nearest compatible lumber drop-off selection, lumber return through an authored
blocking Town Hall footprint, rejection of lumber-only drop-offs for gold, drop-off destruction retargeting, exact lethal tree trips with next-tree selection, the no-live-tree stop path,
non-lethal chops, and both sides of the immobility contract. The in-engine fixture
`games/warcraft-3/tests/resources-src/Units/UnitUI.slk` supplies `isbldg` for the same metadata lookup used by the game.
