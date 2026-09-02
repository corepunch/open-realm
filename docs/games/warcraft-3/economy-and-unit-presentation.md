# WC3 Economy And Unit Presentation

## Gathering Contract

`unit_issuetargetorder(..., "smart", target)` routes workers with `Ahar` to gold or lumber in `m_unit.c`.
The same Smart resolver is also the post-production action layer for producer Rally; Rally stores a target but does not duplicate harvest/repair/item logic. See [rally-points.md](rally-points.md).
The Gather command reaches the same state machines through `harvest_menu_selecttarget`.

- Gold: `harvest_gold_start` -> walk to mine -> capacity-gated hidden mining wait -> carry finite mine gold -> nearest live same-owner drop-off accepting gold -> deposit -> resume the mine while it remains harvestable.
- Lumber: `harvest_start` -> walk into `HARVEST_RANGE` -> swing/damage -> carry lumber -> nearest live same-owner drop-off accepting lumber -> deposit -> resume or find another tree. Successful chops clamp carried lumber to `HARVEST_LUMBER_CAPACITY`; a tree that cannot take damage (for example an invulnerable destructible) does not award lumber.
- Carried resources are mutually exclusive presentation/gameplay state: collecting gold clears stale carried lumber and its `RF_HAS_LUMBER` tag; collecting lumber clears stale carried gold and `RF_HAS_GOLD`. Depositing clears both carry counters/tags, so a worker carrying nothing uses its ordinary animation set rather than retaining a lumber/gold carry model.
- Smart/right-click resource switching preserves the current carry until the replacement resource is actually collected. With no carry, a tree starts lumber harvesting and a Gold Mine starts mining. Partial lumber plus a tree resumes that lumber trip; partial lumber plus a Gold Mine keeps the lumber while approaching/mining and replaces it only when gold is obtained. Gold plus a tree keeps the gold while approaching and replaces it only on the first successful chop. Gold plus a Gold Mine first approaches the clicked mine; on reaching its interaction boundary the worker does not mine another load, returns the existing gold to the nearest valid gold drop-off, then resumes the clicked mine after deposit.
- If an explicitly clicked live tree is buried behind other trees, routing remains responsible only for reaching the best legal approach.  Once that route is exhausted outside `HARVEST_RANGE`, Harvest selects a reachable replacement tree and continues lumber work, matching retail behavior.  See [WC3 Pathfinding And Harvest Reachability](pathfinding.md).
- `s_goldmine.c` uses the mine's authored no-walk pathing footprint plus the worker radius and one movement step as the primary entry boundary. A collision-circle contact check remains the fallback for mines without a path texture. Mine footprints are authoritative; do not restore the old fixed 180-unit radius.
- A chop is lethal when tree life is less than or equal to `HARVEST_TREE_DAMAGE`. The lethal path must call `tree->die` because
  `m_tree.c` owns the fall animation and removal of the tree's pathing obstruction.

Food/supply ownership and upkeep are documented separately in [Warcraft III Food, Supply, And Upkeep](food-and-upkeep.md). Gold workers carry the gross harvested amount; the deposit transition applies the current player's gold upkeep rate before crediting stored gold. Standard lumber remains at a 100% rate.

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

A worker carrying lumber or gold may explicitly Smart/right-click a compatible return building; that exact clicked building becomes the initial return target rather than being replaced immediately by the nearest candidate. Activating the stock worker `Ahar` command while the main selected worker is carrying resources performs the same no-target Return Resources action instead of entering Harvest target-selection mode. While any targeted command callback is armed, a Smart/right-click command cancels that target mode and restores the normal command card before considering movement or an entity Smart order. This prevents a later left-click from being consumed by a stale Harvest callback and retasking the worker group that originally entered target mode.

### Gather / Return Resources Command Button

Warsmash models `CAbilityHarvest` as one toggled ability rather than two unrelated command-card abilities. `isToggleOn()` is true exactly when the carried-resource amount is positive; while false the command uses the ordinary `Art`, `Buttonpos`, `Tip`, `Ubertip`, and `Hotkey` fields and the Harvest base order, while true the command card uses `Unart`, `UnButtonpos`, `Untip`, `Unubertip`, and `Unhotkey` and the no-target Return Resources base order (`852020`). The resource type does not choose the UI state: carried gold and carried lumber both select the same Return Resources presentation.

OpenRealm mirrors that ownership without adding a second visible `Artn` button. `ability_t::is_toggle_on` exposes dynamic ability presentation to `G_BuildCommandButton()`; `Ahar` reports true when either `edict_t::harvested_gold` or `edict_t::harvested_lumber` is positive. The button keeps `Ahar` as its client command string so `CMD_Button` continues through the existing authoritative Harvest handler, which dispatches to no-target return while carrying. `Artn` remains the base handler/capability used by resource-return structures and aliases rather than becoming a second Peasant command-card entry.

`S_SetCarriedResource()` is the transition owner for the two carry counters and `RF_HAS_GOLD`/`RF_HAS_LUMBER`. When its empty/non-empty state flips for a selected worker, it invalidates that viewer's command card so the next server-frame refresh changes Gather to Return Resources after the first resource is acquired and changes it back after deposit. Increasing a positive carried amount or switching directly between positive lumber/gold amounts does not force a redundant command-card rebuild because the toggle state is unchanged.

The in-engine regression coverage is `wc3_movement.harvest_command_button_toggles_to_return_resources_ui` plus `wc3_movement.carried_resource_toggle_invalidates_selected_command_card`. Their synthetic WC3 ability UI data lives at the real archive paths `Units/HumanAbilityFunc.txt` and `Units/HumanAbilityStrings.txt` under `games/warcraft-3/tests/resources-src`, per the fixture rules in `CONTRIBUTING.md`.

### Target Mode And Worker Selection

Targeted command state is server-owned in `gameClient_t::menu.on_entity_selected` / `on_location_selected`. A right-click (`smart` or `smartpoint`) while either callback is active cancels that target mode and restores the normal command card; it must not also issue the Smart/Move order to the still-selected server-side group. Otherwise the callback can survive a ground right-click, consume the next left-click that the player intended as a fresh unit selection, and leave the previous worker group selected. A following tree Smart order then legitimately iterates that stale group and appears as if unrelated gold miners spontaneously switched to lumber.

### Mine Entry — Collision Formula

ROC `PathTextures\16x16Goldmine.tga` is 16x16, but its no-walk (`COLOR32.b`) region is the central 8x8 cells. The existing diagonal
scan therefore produces the correct 128-unit radius (`8 * 32 / 2`). A prior test and proposed column-scan change claimed 192 units;
direct inspection of all 256 decoded pixels disproved that claim. The mine-entry regression belongs in the interaction threshold,
not in a fabricated larger footprint.

The August 31 Human02 trace exposed a second boundary case after point-flow routing was made monotonic. Three miners eventually
stopped at fixed positions with `peons=0 capacity=1`; the two front workers were 36.9 and 40.6 units from the authored footprint while
the strict `worker collision + one step` threshold was 35.0. This is a crowded final-approach settle, not a larger Gold Mine. Gold now
uses the ordinary footprint/circle threshold first, then hands a worker to the mine queue when either the shared point flow reports its
adjusted route goal or Move's existing near-goal settle detector confirms that the worker has stopped making progress at that boundary.
The same rule is used for gold return/deposit. A blocked route farther from the building is not treated as arrival.

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
flow generation. Entry/deposit logs identify `via=footprint`, `circle`, `route_goal`, or `settled`. Gold return uses the same cvar and emits `WC3_GOLD_RETURN start`, periodic `approach`, `deposit_range`, and `deposit` transitions so a Town Hall return failure can be distinguished from a mine-entry failure. If the worker remains outside all completion modes, log rejection in `g_ai.c:move_is_valid` separately for static pathmap and entity-circle collision. Do not enlarge the authored mine footprint to hide a routing/crowding problem.
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

A later August 31 Human02 trace exposed a distinct crowding failure at the Town Hall. A gold carrier remained at `(-4075.4,-3838.9)` for 24 logged approach updates while still 107.4 world units from the authored Town Hall footprint. It was well outside the normal one-step deposit boundary and moved again only after the local lane cleared. This is not a deposit-range or mine-capacity condition: centre-directed interaction routing had funnelled multiple returning workers through the same blocked-building approach. Gold and lumber return now prefer a collision-sized directly reachable cell beside the authored footprint, then hand completion back to their unchanged footprint/contact checks.

The same trace also showed multiple Peasants legitimately converging on one tree. Pre-assigning deterministic angular lanes fixed direct overlap but made workers deviate even when the worker ahead would naturally clear the route, and it required full-edict peer scans. Lumber now keeps the closest direct legal chop point. Resource-worker local avoidance treats a same-direction Peasant as a short queue, then uses deterministic bounded right-first passing only for crossing or persistently pinned traffic. Static routing remains independent of transient occupancy; see `worker-crowd-routing.md` for the 30-Peasant Human02 simulation that selected this policy.

An August 31 Human02 trace with all three starting Peasants ordered together exposed a separate regression after generic
flow fields became resumable. While the shared mine field was still pending, all three reported `flow=0 direct=0`, yet movement
still committed a step using the previous facing. A later lumber trace reproduced the same state on newly clicked trees: several
workers changed position for multiple ticks while their collision-sized field was still generation 0, then corrected as soon as
the field became ready. The fix is shared in `unit_moveindirection`: `flow_generation == 0 && !flow_direct` means no heading has
been resolved and the unit holds position without losing its order. Gold's narrower guards remain valid but are no longer the only
protection against stale-heading movement.

Lumber return is also footprint-aware, matching gold return. A Lumber Mill or Town Hall may have authored no-walk pathing that
extends beyond its scalar collision circle; carried lumber deposits when the worker's radius plus one simulation step reaches that
authored footprint (with collision contact as the fallback). When a collision-safe direct point exists inside the drop-off contact
boundary, lumber return steers to that point immediately instead of requesting a whole-map field toward the blocked building
centre. The same trace verified that lumber-to-gold switching itself is correct: workers retained carried lumber through mine
approach/entry and replaced it only when gold was actually collected.

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

Each successful swing does `HARVEST_TREE_DAMAGE` (slot 1 = 1) HP of damage to the tree and adds the same amount as lumber, clamped to `HARVEST_LUMBER_CAPACITY`. A rejected destructible hit (including invulnerability) leaves carried lumber unchanged. Smart/right-clicking another tree with partial lumber resumes harvesting without clearing the existing amount. A worker carrying gold may also switch to a tree; the gold remains carried while approaching and is replaced by lumber only when a chop actually succeeds. Capacity (`HARVEST_LUMBER_CAPACITY`, slot 2 = 10) fills after 10 stock swings, triggering `harvest_walkback`; reissuing Harvest while already full remembers the requested tree and returns before another chop. When a tree's HP reaches zero on the lethal chop, `tree->die()` is called
(m_tree.c owns the fall animation and pathing removal). `tree_die` sets the death sequence and its
first frame in the same transition, so the lethal snapshot cannot retain the upright hit frame.

The automatic return transition first selects the nearest compatible return building and revalidates it while travelling; an explicit Smart/right-click return instead starts with the compatible building the player selected. The deposit transition then validates `secondarygoal` before publishing resume. A still-live remembered tree is resumed directly. If that tree died while the worker was away, replacement selection is centered on the dead tree's position so the worker stays in the same forest; only a return with no remembered tree searches around the worker. If no live tree exists, the worker stands. Therefore `RESUME_LUMBER` always names a live target and a worker never spends a movement tick targeting the tree it just felled.

### Gameplay message stream

`GAMEMSG` is a synchronous game-owned observation stream for tests and diagnostics. It is separate from `GAMEEVENT`, whose values
and dispatch are the Warcraft/JASS trigger contract. `G_SubscribeMessage` installs a callback; `G_UnsubscribeMessage` removes it;
`G_PublishMessage` delivers `{ type, actor, target }`, where actor and target are stable entity numbers rather than edict pointers.

Harvest publishes transitions only: move-to-resource, enter-mine/start-chop, chop/tree-felled, return-to-base, deposit, and resume.
Movement ticks do not publish messages. Tests subscribe immediately before the order and assert both sequence and entity numbers.
The lethal-trip test specifically requires `CHOP -> TREE_FELLED -> RETURN_LUMBER -> DEPOSIT_LUMBER -> RESUME_LUMBER -> START_CHOP`,
with the resume/start target equal to the next live tree rather than the felled entity.

## Unit Death And Corpse Interaction

`games/warcraft-3/game/m_unit.c:unit_die()` owns the live-unit to corpse transition. It
sets authoritative life to zero so `M_IsDead()` is true even when a direct/scripted caller
enters the death function without first applying lethal damage. A dead unit is no longer an
orderable actor even though its edict remains present while the death/decay presentation
runs. The transition clears the unit's server selection mask and
sets the existing snapshot flag `EF_NOT_SELECTABLE`; `client/cl_view.c` maps that to
`RF_NOT_SELECTABLE`, which `renderer/r_ents.c:R_TraceEntity()` and `R_EntitiesInRect()`
exclude. The corpse therefore remains rendered but cannot be clicked or box-selected. This
reuses the existing `entityState_t.flags` contract; death does not add a new network field.

Server authority mirrors the presentation rule. `G_SelectEntity()` refuses dead or
`EF_NOT_SELECTABLE` entities, `G_IsEntitySelected()` stops stale selection masks from
participating in `FOR_SELECTED_UNITS`, and the generic JASS/client order entry points reject
dead actors. This is required even though `unit_die()` clears the selection mask: an old
client/control-group command or direct JASS `Issue*Order` must not replace the corpse's
`death` move with `walk`, `stop`, or another live-unit move.

Death animation and corpse holding are separate states. `unit_setmove()` changes the active
sequence but intentionally does not reset `s.frame`. The death transition must therefore set
`s.frame` to `animation->interval[0]` immediately after selecting the `death` move, matching
`G_DestructableStartDeathAnimation()`. Otherwise a lethal snapshot can inherit the previous
move's timeline position and enter the death sequence late. `unit_die()` also clears a
pre-existing `AI_HOLD_FRAME` so `M_MoveFrame()` can advance death even when the actor had
been holding a construction/gameplay frame. Only when death reaches its end does
`unit_begin_decay()` select the decay state and set `AI_HOLD_FRAME` again to preserve the
final corpse pose during the decay timer.

Ordinary corpses are eventually freed by `unit_decay_think()`. Heroes preserve the same
authoritative edict for revival; `G_ReviveHero()` clears `SVF_DEADMONSTER`,
`EF_NOT_SELECTABLE`, `RF_HIDDEN`, and `AI_HOLD_FRAME` before returning the Hero to its
living stand state. See [Hero revival](hero-revival.md) for the remainder of that lifecycle.

### Verification

`wc3_unit.die_clears_selection_and_marks_corpse_unselectable` covers the authoritative
selection transition; `wc3_unit.dead_unit_rejects_orders_that_would_replace_death_animation`
covers stale client/JASS orders; and the scripted-revive test verifies that the same Hero
edict becomes selectable again. `net.entity_delta_preserves_not_selectable_flag` already
round-trips `EF_NOT_SELECTABLE`, so this change reuses a tested network bit rather than
widening `entityState_t`. Runtime verification should kill a moving selected unit, confirm
the death sequence begins from its first authored frame and runs to the final corpse pose,
then confirm the corpse has no selection circle and ignores click, box, control-group, and
right-click movement attempts.

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
frame anchored inline. Warsmash performs one runtime correction after loading `ResourceBarFrame`: Gold, Lumber, and Supply text are
all assigned the Upkeep text field's authored width. OpenRealm mirrors that normalization in `ConsoleEnsureLoaded()`. Do not size
the food string to its natural `used/cap` text width, because larger supply values otherwise overflow the authored resource-bar
region.

The selected-unit portrait is one of those runtime-owned frames. Current Warsmash ships a project-only
`SmashUI/UnitPortrait.fdf`; that file is useful as a parity reference but is not retail MPQ data and must not be loaded by OpenRealm.
The runtime portrait therefore mirrors its final WC3-space contract directly: the model is `(0.211, 0.4865, 0.0835, 0.085)`, while
centered `current / max` health and mana strings occupy the strip below it. The strings retain Warsmash's `TextLength 20` contract.
Health uses the Warcraft red -> yellow -> green life-ratio gradient and mana is white; units with no mana keep the mana frame but render
an empty value. These are live snapshot bindings, not static text baked into `LAYER_PORTRAIT`: the server writes the sole-selected
unit's exact current/max HP and mana to the reserved generic `playerState.stats[18..21]` UI slots each frame, and the client resolves
`UI_STAT_SELECTION_HEALTH_TEXT` / `UI_STAT_SELECTION_MANA_TEXT` when drawing the already-authored portrait frames. The health colour
is likewise derived from those current snapshot values. This deliberately avoids retransmitting the complete portrait layout for every
damage, heal, regeneration, or mana tick and remains independent of the `LAYER_INFOPANEL` presentation cache (important while a
selected building is showing its build/training queue). A normal gameplay transmission can still replace the portrait layer; once the
selected-unit portrait is authored again, the same live bindings immediately display the current values.

Holding the selected-unit portrait is also a camera control. The server-authored portrait uses a Quake-style
`+portraitcamera <entity>` layout command: press recenters on the currently sole-selected unit and installs that unit as the existing
camera target controller; release sends the captured `-portraitcamera` command and clears the controller even if the pointer left the
portrait. `G_RunClients()` then follows the target's current position each simulation frame for the duration of the hold. This reuses
the same target-controller state as `SetCameraTargetController` rather than adding a second follow-camera implementation.

The selected-unit damage, armor, and Hero-attribute blocks use retail `UI\FrameDef\UI\SimpleInfoPanel.fdf` for the
actual icon, label, value, and internal offsets. C owns only the runtime wrapper rectangles that the game repositions:
attack 1 is `(0.000, -0.04000, 0.100, 0.030125)` relative to the unit-detail top-left, attack 2 is
`(0.100, -0.03925, 0.100, 0.030125)`, armor is `(0.000, -0.07050, 0.100, 0.030125)` when an attack is
shown (or `y=-0.04000` for attackless units), and the Hero attribute block is
`(0.100, -0.03700, 0.100, 0.062500)`. The second damage block is a cloned FDF tree so its frame identity and
anchors are independent. The legacy `InfoPanelUnitDetail.fdf` speed/range/damage/attribute fields are suppressed
when `SimpleInfoPanel.fdf` is available; they remain only as an explicitly diagnosed fallback if the retail template
cannot be loaded. Hero STR/AGI/INT label positions and the primary-attribute icon are therefore data-driven by the
retail FDF/War3Skins rather than duplicated as child-coordinate literals in C. Warcraft registers both `GlobalStrings.fdf` and
`InfoPanelStrings.fdf` before parsing the info-panel frame definitions. The classic data set splits the labels across those tables:
`COLON_ARMOR` is available from `GlobalStrings.fdf`, while `COLON_DAMAGE`, resource labels, Hero attributes, and `COLON_STATUS` are
provided by `InfoPanelStrings.fdf`. Both StringLists must therefore be registered before `InfoPanelUnitDetail.fdf`,
`InfoPanelBuildingDetail.fdf`, or `SimpleInfoPanel.fdf` is parsed; otherwise the unresolved `COLON_*` ID is baked into the cached
frame text. These labels remain data-driven rather than hardcoded English. Retail StringLists use newline-delimited key/value pairs
without requiring commas, so the parser consumes one quoted value token per key while still accepting an optional comma. The
parser-owned registry is cleared with the FDF template lifecycle so stale strings cannot leak between UI loads.

The right-hand second damage block is attack slot 2, not a status/buff icon. It is emitted only when `weapsOn` enables bit 1, the
second weapon has damage dice, and `showUI2` is true. A populated dormant attack-2 row must not produce a second icon/value such as
`2 - 8`. Attack 1 remains driven by the live runtime attack record.

Buff/status presentation is a separate runtime strip at the bottom of `SimpleInfoPanelUnitDetail`. The `Status:` frame is
`0.035 x 0.010` at `BOTTOMLEFT + (0.030, 0.003)`; visible buff icons are `0.015 x 0.015`, with the first icon attached to the
label's right edge at `+0.001` and later icons chained at the same gap. Icon, tip, and ubertip metadata comes from Warcraft's
`Units\AbilityBuffData.slk` (`Buffart`, `Bufftip`, `Buffubertip`) with standard profile TXT values allowed to override it. The shared
`abilstatus[]` array also carries cooldown bookkeeping, so only entries that resolve as buff UI metadata are rendered in this strip;
`BTLF` remains reserved for timed-life presentation. Adding, replacing, removing, or expiring a status invalidates the selected-unit
info layer so the strip refreshes without polling every frame.

Upgrade-level text is the small `InfoPanelIconLevel` overlay attached to the bottom-right of a damage/armor icon. It is not a buff
slot. Resolve the selected unit's `UnitBalance.upgrades` rawcodes through `UpgradeData.slk` and show the player's researched level only
for the matching `melee`/`ranged`/`artillery` weapon class or `armor` class. Hero attribute presentation does not inherit an unrelated
unit upgrade marker. A matching upgrade at researched level zero is still displayed as `0`; if no matching upgrade exists, the level
overlay is cleared and hidden. The icon artwork follows the same Warsmash/War3Skins split: when that weapon or armor class has an
upgrade rawcode, use `InfoPanelIconDamage<Type>` / `InfoPanelIconArmor<Type>`; otherwise use the corresponding `...Neutral` skin
key. Warsmash tests the texture load as well as the skin field: if a Neutral texture cannot load, retry the ordinary key for the
same attack/defense type. This matters for stock Heroes: Hero status icons are not special-cased, so a Hero with no matching upgrade
class first requests `InfoPanelIconDamageHeroNeutral` / `InfoPanelIconArmorHeroNeutral`, then falls back to the ordinary Hero asset
if that Neutral asset is unavailable. A custom Hero whose `Upgrades Used` supplies the relevant class uses the ordinary family and
shows the researched-level badge like any other unit.

`Spells` is also a fallback rather than a hard alias: preserve `InfoPanelIconDamageSpells[Neutral]` when a custom skin defines it,
and only retry the corresponding Magic skin field when the Spells field is absent. Warcraft's `Heavy` defense spelling normalizes
to the `Large` icon key, and Warsmash's compatibility spelling `SEIGE` normalizes to `Siege`. Invalid/null attack types use
Warsmash's first attack-table entry (`Unknown`); invalid/null armor types use
its first defense-table entry (`Small`). For stock Human data this means Footmen use Normal attack + Large (Heavy) armor artwork and
Riflemen use Pierce attack + Medium armor artwork, while units with no matching attack/armor upgrade family use the neutral versions.
If neither the requested Neutral texture nor its ordinary fallback can load, clear the backdrop rather than retaining the previously
selected unit's icon. The resolver caches each attack/defense-family result; a missing Neutral asset or final missing icon is therefore
reported once when that cache entry is first resolved rather than on every info-panel refresh. Buildings with `foodMade > 0` use the
retail `SimpleInfoPanelIconFood` tree, `InfoPanelIconFood` skin
texture, `COLON_FOOD_PROVIDED` label, and the authored Food Provided value layout. Resource-bearing units such as Gold Mines use the
parallel `SimpleInfoPanelIconGold` tree, `InfoPanelIconGold` skin texture, `COLON_GOLD`, and the current resource amount.

`SimpleNameValue` owns the selected-unit title typography and anchor. Do not synthesize `Level 1 Peasant`-style class strings. Heroes
use the retail `SimpleHeroLevelBar` in the line below their proper name, with `SimpleXpBarConsole` / `SimpleXpBarBorder` and the
current-level XP fraction. Ordinary units keep the class line empty; Heroes populate `SimpleClassValue` through
`INFOPANEL_LEVEL_CLASS`, for example `Level 2 Paladin`. Runtime values may override a status-bar value, but its geometry and anchors
stay FDF-authored.

`SimpleInfoPanelBuildingDetail` owns the training/research shell. The queue backdrop is `0.180 x 0.090`; the FDF build-time indicator
is runtime-sized to `0.10538 x 0.0103` and remains at its authored anchor. Warcraft creates queue icons outside the FDF: active slot 0
is `0.02671875` square at top-left `(0.320546875, 0.526875)`, while waiting slots are `0.020390625` square beginning at
`(0.319140625, 0.562734375)` with horizontal stride `0.028125`. Cancellation hit targets must use those same rectangles.
Only controllable buildings use this queue/construction panel: a Peasant's `build` pointer names the structure it is working on and
does not change the Peasant's own selected-unit panel. The unused building-description String is explicitly hidden.

The runtime inventory heading uses the localized `INVENTORY` StringList entry and is emitted only when the selected unit has inventory
capacity. Selecting a unit without inventory replaces that layer with the race-specific inventory cover and no heading.

Ability state and visible buff state are different namespaces. `abilstatus[]` may carry an ability rawcode such as `AHad`; status UI
resolves that ability's level-specific `AbilityData.slk` `BuffID*` to the corresponding buff rawcode (for example `BHad`) before
reading `AbilityBuffData.slk` art and tooltip fields. This prevents a learned/cooldown ability ID from being drawn as a fake status icon
and lets aura buffs use their actual Warcraft buff artwork.

The retained client solver preserves authored FDF dimensions when both opposing anchors are present; for vertical `SetAllPoints`
children the bottom edge stays attached to the runtime wrapper and authored height extends upward. Single-line stat labels also use
their declared FDF font size as the line-box height, so the Strength -> Agility -> Intelligence anchor chain does not accumulate glyph-
metric error.

Repeated quest rows already have authoritative schemas in Blizzard's `QuestDialog.fdf`. `QuestListItem` and
`QuestItemListItem` own row size and child placement. The server clones those templates, stacks each clone by the template's own
height, then binds title, selection color, and command data to the named children. It must not spawn generic text rows or impose a
parallel width/stride.

### World-unit hover health

WC3 world hover is presentation-only and remains separate from selection. `client/cl_input_w3.c` ray-picks the world on mouse motion,
then accepts only snapshot entities carrying the generic `EF_HOVER_HEALTH` capability. `G_CustomizeEntity` authors that bit per client
for living `SVF_MONSTER` units/buildings that are selectable and actively visible. This stricter visibility check intentionally differs
from `G_FowPlayerCanSeeEntity`: explored buildings may remain networked while shrouded, but must not expose current HP through hover.

The presentation path is:

```text
game.dll UI_WriteHoverLayout -> svc_layout LAYER_WORLD_HOVER
TraceEntity -> cl.hover_entity -> generic context projection/bindings -> SCR_DrawLayout
```

The renderer does not construct or draw this widget. `SCR_LayoutWorldHoverRoot` projects the model-authored overhead point and rejects
points outside the world scissor before the generic layout drawer evaluates the server-declared frames. WoW uses the same context
layer with its own frame tree; SC2 currently sends an empty layer as an explicit server-owned placeholder.

The hover nameplate/stat bar is native runtime UI, not an FDF-defined frame. ROC `FrameDef.toc` contains no world-hover frame;
retail constructs `CUnitTip` and `CStatBar` directly, so `game.dll` mirrors that lifecycle with proxy `uiFrame_t` frames rather than
shipping a project-owned FDF. `UI_WriteHoverLayout` sends the frame tree once from `G_ClientBegin`. Archive data still owns the content:
`War3Skins.txt` resolves
`ToolTipBackground`, `ToolTipBorder`, `SimpleHpBarConsoleSmall`, and `SimpleManaBarConsoleSmall`, while `UnitUI.slk:scale` and
`MiscData.txt:[SelectionCircle] ScaleFactor` determine the bar width. Keep those lookups authoritative instead of copying texture
paths or per-unit dimensions into C.

The displayed name comes from the race/campaign `Units\\*UnitStrings.txt` `Name` field (`unam`), merged into `UnitProfile_t.name`;
for example, `Units\\NeutralUnitStrings.txt` defines `[nvil] Name=Villager`. `G_CustomizeEntity` interns that text into `CS_GENERAL`
and sends its 1-based pool index through `entityState_t.name`; the generic `UI_STAT_CONTEXT_NAME` binding resolves that index while
drawing `LAYER_WORLD_HOVER`. Runtime-created units/buildings may allocate a new pooled name only after a client is already spawned.
`PF_Confignstring()` must therefore clear `sv.syncstrings[index]` whenever game code changes a configstring, allowing
`SV_SendClientMessages()` to reliably flush the new value to connected clients. Leaving the sync bit set produces the specific
regression where pre-existing hover names work but a newly constructed building has an empty hover label. Health and mana use the
corresponding context bindings over the snapshot's compressed stat bytes. A configstring is NUL-terminated on the wire, so its sixteen fixed-width name records use
ASCII Unit Separator (`0x1f`) as padding and retain a single final NUL. `CL_ParseConfigString` converts the separators back to NULs
after receipt, preserving ordinary fixed-offset C strings for renderer and UI consumers. Embedded NUL records truncate at the first
name in `MSG_WriteString` and leave later hover names empty.

Retail `PreSelect.cpp` makes the bar width `UnitUI scale * SelectionCircle ScaleFactor * 0.0005`, equivalent to the render
entity's selection radius times `0.001`. `CStatBar.cpp` uses a `0.004` frame height and `0.001` inset, so the visible fill is half
the old implementation's thickness. `CUnitTip.cpp` sizes the nameplate from rendered text with `0.008` horizontal and `0.006`
vertical padding rather than forcing it to the bar width. Placement transforms the MDX model bounds and adds 30 world units of
clearance. Selection-circle radius is not a height: using `radius * 2 + 48` placed large-footprint mines too high and tall heroes
too low. The renderer therefore exposes its model-format overhead point to the generic client layout code without leaking MDX bounds
or per-game widget construction into the client. The client rejects projected points outside the world scissor, so the widget cannot
bleed into the command console.

The same hover entity drives the ground selection-preview splat in `renderer/r_ents.c`. `G_CustomizeEntity` derives the
recipient-relative relationship by resolving the two hover-relevant WC3 neutral ownership classes before ordinary alliance state:
`PLAYER_NEUTRAL_PASSIVE` (15) is `EF_NEUTRAL`, while `PLAYER_NEUTRAL_AGGRESSIVE` (12) is `EF_HOSTILE`. Both values are inside this
engine's `MAX_PLAYERS == 16`, so a range check cannot identify them. Reserved slots 13 and 14 are not assigned special hover semantics
here; they continue through ordinary relationship handling until their presentation behavior is established. Normal foreign players use
passive-alliance and shared-control flags: passive allies without shared control are `EF_NEUTRAL`, enemies are `EF_HOSTILE`, and
own/shared-control units carry neither relationship flag. Human02 runtime evidence identified `ngol` (Gold Mine) and `nshe` (Sheep) as
owner 15 and neutral-hostile creeps as owner 12, matching the authored player-slot semantics. The renderer maps those states to faint
red, yellow, and green rings respectively, using the normal selection-circle size/texture choice and half-alpha hover presentation.
Selection remains a separate state and suppresses the hover preview while the full selected-unit circle is visible.

Authoritative click/box selection, command authority, and fog/death deselection are documented separately in
[selection-and-control.md](selection-and-control.md). Do not infer command rights from these hover relationship flags.

The authored WC3 cursor reuses that recipient-relative hover state without changing cursor assets. `client/cl_scrn.c` submits red for
`EF_HOSTILE`, the same yellow family used by the neutral hover ring for `EF_NEUTRAL`, and white for friendly/no hover. This keeps Sheep,
Gold Mines, and other Neutral Passive targets visually neutral while Neutral Hostile creeps remain hostile. `MDLX_DrawSpriteTinted`
stores the tint on the transient cursor render entity, and the MDX renderer folds it into the existing geoset-color shader value so
texture alpha and authored geoset/material colour remain independent.

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
trained-unit exit placement against static footprints, nearest compatible lumber drop-off selection, explicit Smart-click drop-off return, lumber return through an authored
blocking Town Hall footprint, rejection of lumber-only drop-offs for gold, drop-off destruction retargeting, exact lethal tree trips with next-tree selection, dead-previous-tree same-forest retargeting, the no-live-tree stop path, capacity clamping, invulnerable-tree rejection, carried-resource gold/lumber visual switching and zero-carry visual clearing,
non-lethal chops, and both sides of the immobility contract. The in-engine fixture
`games/warcraft-3/tests/resources-src/Units/UnitUI.slk` supplies `isbldg` for the same metadata lookup used by the game.
