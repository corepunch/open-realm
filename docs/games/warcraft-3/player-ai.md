# Warcraft III Player AI

## Goal

Implement server-side computer players through Warcraft III's authoritative AI scripts and gameplay paths. The first acceptance target is ROC `Maps/Campaign/Human02.w3m`: the map calls `StartCampaignAI(Player(0), "h02_red.ai")`, and the Blackrock Orc player must execute that script against `Player(1)`.

This is player AI, not unit AI. Blizzard's AI script requests economy, production, defenders, captains, and assaults. Existing construction, training, movement, harvesting, and combat state machines perform the resulting actions.

See [Player AI Postmortem](player-ai-postmortem.md) for the final architecture assessment, evidence from the archived scripts,
implementation lessons, and rules for future work.

## Authoritative Sources

Use these sources in priority order:

1. `Scripts/common.ai` and the requested campaign or melee `.ai` file from the active MPQ set.
2. The map's `war3map.j`, W3I player setup, resource triggers, restrictions, and alliances.
3. Typed SLK/profile rows for costs, prerequisites, food, builders, and producers.
4. Existing server-side construction, training, order, pathing, and fog-of-war APIs.

Do not translate `h02_red.ai` into a C wave table or add map-name checks. `Scripts/orc.ai` is a later melee-AI dependency; Human02 does not load it.

## Verified Human02 Contract

`Scripts/h02_red.ai` directly calls six `common.ai` helpers:

- `CampaignAI`
- `SetReplacements`
- `CampaignDefenderEx`
- `InitAssaultGroup`
- `CampaignAttackerEx`
- `SuicideOnPlayer`

These helpers are not six simple engine directives. Their transitive path starts Blizzard's perpetual `CampaignBasics` and `BuildLoop` threads and reaches production, harvesting, unit counts and costs, captain management, readiness, attack, command, and sleep natives. Human02 therefore requires the campaign economy/build loop and captain behavior; implementing only wave timers is not script compatibility.

`CampaignAI` chooses difficulty once. Each `Campaign*Ex` uses the easy, normal, or hard quantity; the script's final branch gives an unsupported higher difficulty the hard quantity. The Single Player campaign difficulty selector stores `wc3_campaign_difficulty`; for stock ROC/TFT campaign map paths `G_SpawnEntities` seeds `GetGameDifficulty()` from it before map startup, so campaign AI sees the selected Easy/Normal/Hard value unless the map later changes game difficulty itself. Non-campaign maps retain the existing Normal default.

### Defender Requests

| Unit | Easy | Normal | Hard |
|---|---:|---:|---:|
| Headhunter | 1 | 1 | 1 |
| Grunt | 1 | 1 | 2 |
| Raider | 0 | 0 | 1 |

Replacement counts are 0, 1, and 3 for easy, normal, and hard.

### Assault Schedule

`M2`, `M3`, and `M4` are 120, 180, and 240 seconds. `SuicideOnPlayer` adds each delay to a cumulative `sleep_seconds` deadline, starts preparing before that deadline based on missing-unit production estimates, forms a captain, and waits for readiness or timeout. It then calls `SuicidePlayer` and waits for the assault captain to become empty. The values below are nominal deadlines, not unconditional launch instants.

| Wave | Nominal time | Composition by difficulty |
|---|---:|---|
| 1 | 2 min | 2 Grunts; 1/1/2 Headhunters |
| 2 | 4 min | 2 Grunts; 2/2/3 Headhunters |
| 3 | 7 min | 4 Grunts; 0/0/2 Raiders; 1 Headhunter |
| 4 | 9 min | 3 Grunts; 1/1/3 Headhunters |
| 5 | 12 min | 1/1/3 Grunts; 0/0/2 Raiders; 4 Headhunters |
| 6 | 15 min | 4/4/5 Grunts; 1/1/2 Headhunters |
| 7 | 19 min | 2/2/3 Grunts; 0/0/2 Raiders; 3/3/4 Headhunters |

Waves 5-7 repeat with cumulative delays of 3, 3, and 4 minutes. Acceptance tests must check preparation, readiness, timeout, captain-empty waits, and difficulty quantities rather than exact launch timestamps alone.

The map's authored resource setup and recurring resource triggers remain authoritative. Production must pay ordinary costs, but tests must not require the AI to operate without grants that the map intentionally provides.

## JASS-Derived Controller Model

`common.ai` describes the division of responsibility more precisely than a generic planner design would:

- **The script owns policy and scheduling.** `StartThread`, `Sleep`, `BuildLoop`, `CampaignBasics`, and attack functions form cooperative concurrent loops. `BuildLoop` runs every two seconds; `CampaignBasics` runs every five seconds and staggers its first repeated pass by player number. Do not add a second C think scheduler around those loops.
- **Production is an ordered desired-count program.** `SetBuildUnit`, `SetBuildUpgr`, and `SetBuildExpa` append typed requests. `OneBuildLoop` scans them in script order. An unaffordable unit or expansion stops that pass, preserving priority over later requests; an upgrade attempt does not stop the scan.
- **Unit requests are declarative.** `SetProduce(qty, id, town)` asks the engine to move the current count toward a target at any town (`-1`) or a specific town. It is not an instruction to spawn `qty` units immediately.
- **The script performs virtual resource reservation.** `StartUnit` computes the unmet desired count, limits the immediate request to what current virtual gold/lumber can afford, then debits the full unmet amount from the pass-local totals before scanning later requests. Engine actions still perform real payment and legality checks.
- **Counts use equivalence classes.** `TownCountEx` treats upgraded halls/towers and transformed units as satisfying requests for their base form. Native counts must expose the raw facts expected by this JASS helper; C must not add a competing hardcoded substitution policy.
- **Economy assignment is periodically rewritten.** `InitAI` calls `StopGathering`; `CampaignBasicsA` then clears and reapplies desired gold/lumber worker counts per town. When the defense captain is fighting, campaign wood demand becomes zero temporarily.
- **Defense and assault are distinct captain roles.** `ATTACK_CAPTAIN`, `DEFENSE_CAPTAIN`, and `BOTH_CAPTAINS` are explicit. Defender requests persist in `defense_*`; each wave resets and repopulates transient `harass_*` min/max requests. `CaptainInCombat(false)` refers to defense work while attack formation uses `CaptainInCombat(true)`.
- **Attacks are blocking script procedures.** `SuicideOnPlayer` does not enqueue a fire-and-forget order. It prepares and fills the attack captain, starts the player assault, then yields until combat and captain-empty conditions finish or time out before the script advances.
- **`CommandAI` is a per-player ordered interrupt channel.** `CommandsWaiting`, `GetLastCommand`, `GetLastData`, and `PopLastCommand` support polling and interruptible sleeps. Preserve command/data pairing and determine FIFO versus LIFO behavior from authoritative runtime evidence before implementing it; do not model this as one overwriteable flag.
- **AI mode is native state.** `StandardAI` and `CampaignAI` select different flee, repair, hero, targeting, timed-life, and artillery policies through setter natives. These settings belong to the player AI runtime even when Human02 does not exercise every one. `SetPeonsRepair` now consumes the existing Repair-family autocast path rather than inventing a second repair scheduler: each active bot reconciles owned repair-capable workers to the requested state, including workers created later through JASS `CreateUnit`. The established nearest-valid target scan, normal Repair order, pathing, target masks, costs, and repair rates own the actual work.
- **Alliance capabilities are independent and directional.** Combat/targeting friendship is `ALLIANCE_PASSIVE`; `SHARED_VISION`, `SHARED_XP`, and control flags do not independently make a target friendly. `SetPlayerAlliance` changes only the source player's relation, so Blizzard/map helpers must author the reverse direction separately when they require mutual alliance.
- **Unit-target Move is persistent follow.** Smart on a passive allied unit (after higher-priority interactions such as Repair) and explicit target `move` retain the live unit as `movement.follow_target`. The follower uses its acquisition range as the stand-off distance, auto-acquires nearby hostiles, and resumes the retained follow after combat. Point Move, Attack-Move, Patrol, Stop, and Hold Position replace that default movement goal.
- **Local acquisition is controller-agnostic.** `G_FindNearestEnemy` uses the acquiring unit owner's directional `PASSIVE` relation; it is not restricted to fights where one side is the human slot. This allows an allied computer Hero to defend against another computer player while keeping Neutral Passive out of ordinary acquisition. Neutral-owned units still do not initiate through `ai_stand`.
- **Kill XP honors shared XP.** Nearby living, unsuspended Heroes owned by the killer or covered by the killer player's directional `ALLIANCE_SHARED_XP` participate in one recipient set. The victim's available XP is divided by that set before the existing per-Hero level factor is applied. A forced kill against a `PASSIVE` ally awards no Hero XP. If that nearby set is empty and `Misc/GlobalExperience` is enabled, the same eligibility rules are retried without the `HeroExpRange` distance gate.

`Blizzard.j` adds map-facing lifecycle rules:

- `MeleeStartingAI` starts a race script only for slots that are both playing and computer-controlled.
- After melee startup, `ShareEverythingWithTeamAI` grants shared vision, shared control, and shared advanced control between co-allied computer players. Legal action and observation checks must honor these authored alliances when melee support is added.
- `PauseAllUnitsBJ` calls `PauseCompAI` for every computer-controlled slot while separately pausing units. `PauseCompAI` must therefore be harmless when that player has no active AI VM.
- Melee hero items and several other AI-adjacent behaviors remain ordinary map triggers. AI implementation must not absorb responsibilities already owned by `Blizzard.j`.

These functions point to a script-compatible executor with authoritative native services, not a replacement C strategy engine. Utility scoring and search may later complement melee tactics, but they must not reinterpret the campaign script's queue, timing, or captain state.

## VM And Lifecycle Contract

The map VM is `level.vm`. It loads `Scripts/common.j`, `Scripts/Blizzard.j`, and `war3map.j`, and `G_RunFrame` pumps its coroutine queue.

A root from `jass_newstate()` owns independent globals, functions, and coroutines. One serialized AI VM per started computer player is therefore the intended model:

```text
war3map.j StartCampaignAI(player, script)
                    |
                    v
       player-owned AI VM root
  common.j -> common.ai -> requested .ai
                    |
                    v
       player-bound AI native context
                    |
                    v
       authoritative game operations
```

    The root also owns every parsed program and the declaration names borrowed from its AST. `jass_close` must release coroutine stacks and locals first, then globals, functions, types, and finally parsed programs; freeing the AST earlier invalidates borrowed names, while omitting it leaks every AI restart.

Before adding roots, fix and test ownership:

- close the map VM and all AI VMs before level reset and shutdown;
- bind the AI player explicitly while an AI coroutine runs and restore prior context afterward;
- keep VM execution serialized because `jass_host` and several current-context variables are process-global;
- audit declaration/program ownership in `jass_close`, not only coroutine cleanup;
- pause sleeping and runnable AI coroutines without affecting the map VM;
- retain one ordered command/data collection per AI player for `CommandAI` and the `CommandsWaiting` family, with removal order verified before implementation;
- stop AI on restart, player removal, victory/defeat, and map unload.

Do not allocate active AI merely because W3I says `MAP_CONTROL_COMPUTER`. Human02 has several computer slots, but only the player passed to `StartCampaignAI` owns this script. The native call starts or replaces the player's AI; `PauseCompAI` controls it.

## Gameplay Boundary

AI code issues legal game actions; it does not select units, operate menus, synthesize client commands, grant resources, or write movement/combat thinker fields.

Start with narrow shared operations rather than a speculative universal dispatcher:

- extract construction initiation from `build_menu_send_builder` into a builder/building/point operation;
- route menu placement and `IssueBuildOrder*` through it;
- expose training through the existing capability, tech, prerequisite, cost, food, queue, charge/refund, and completion paths;
- wrap existing immediate, point, and target orders with owner and visibility validation;
- return a typed failure reason so script-native state can wait, retry, or report an impossible request.

The menu remains a client adapter. Authoritative state remains in `GAMECLIENT`, entities, and typed metadata.

## Fog Of War

Fog grids are currently updated only for players marked `client_connected`. Add an independent consumer mask or reference count for started AI players; do not fake network connections.

AI may inspect its own and allied state. Live enemy selection and target orders must pass `G_FowPlayerCanSeeEntity`. Remember hidden contacts only as entity number plus last-seen type, position, coarse health, and observation time. Resolve and revalidate an entity immediately before use; never retain a live hidden pointer.

The target player passed explicitly by `SuicideOnPlayer` is authored script data, not an observation leak. Selecting that player's current hidden units would be a leak.

## Implementation Plan

### Phase 0: Executable Contract And Diagnostics

- Add an archive-backed test that creates an AI VM and loads `Scripts/common.j`, `Scripts/common.ai`, and `Scripts/h02_red.ai` from the active archive set.
- Bind `Player(0)`, call the AI script's `main`, and report the exact first unresolved native or runtime error.
- Add `ai_debug` diagnostics for VM lifecycle, native calls, requests, captain state, action results, and timing. Keep detailed traces behind `WC3_DEBUG_AI` or `ai_debug`.
- Verify the Human02 map call, player controllers, target player, difficulty, and resource triggers in ROC and TFT data.

Exit: the unchanged script parses and reaches a deterministic, player-attributed unsupported-native diagnostic. Gameplay is unchanged.

### Phase 1: AI VM Ownership

- Add a fixed `MAX_PLAYERS` array of private AI owners to level state; do not widen network structs.
- Give each started player an independent VM root, requested script name, mode, pause state, and diagnostic counters.
- Load `common.j`, `common.ai`, then the requested script and start `main` as a coroutine.
- Make player binding/restoration explicit around every resume.
- Implement start, replace, pause, resume, stop, level reset, and shutdown.
- Close the existing map VM correctly before `memset(&level, 0, ...)`.

Tests: two AI roots have independent globals and sleepers; map callbacks restore context; pause freezes only the selected AI; replacement and unload leave no runnable coroutine or owned VM.

Exit: two no-op AI scripts can run, sleep, pause, resume, and shut down independently.

### Phase 2: Authoritative Construction, Training, And Orders

- Extract `G_IssueBuildOrder(builder, building, point)` from menu state.
- Adapt `build_menu_send_builder`, `IssueBuildOrder`, and `IssueBuildOrderById` to the shared operation.
- Add an authoritative training entry point around existing queue and payment logic.
- Add player-bound wrappers for immediate, point, and target orders.
- Define reason codes for wrong owner, dead/busy actor, unknown capability, missing prerequisite, insufficient gold/lumber/food, full queue, hidden/invalid target, blocked placement, and unreachable destination.

Tests cover every reason and its successful inverse. Include Human and Orc construction lifecycle tests because Human02 builds Orc structures.

Exit: a computer-owned worker can build and harvest, a producer can train, and a unit can move and attack without selection or menu state.

### Phase 3: AI Native Closure

Implement the transitive `common.ai` surface in executable vertical slices, rerunning the archive-backed script test after each slice:

1. Context and scheduling: `GetAiPlayer`, `Sleep`, `StartThread`, difficulty, command queue.
2. Queries: own unit/town counts, completion state, costs, mines, buildings, upgrades, ignored units, and unit liveness.
3. Economy/production: `SetProduce`, `SetUpgrade`, expansion hooks used by the active path, harvesting, and stop-gathering.
4. Campaign settings: campaign mode, replacement count, flee/repair/chopping flags, timed-life policy, and required setup calls.
5. Captains: create/reset captains, add assault units/defenders, readiness/full/empty/combat/retreat state, guard posts, and home/target operations.
6. Assault: `SuicidePlayer` and the legal orders needed to move the selected captain toward the authored target player.

Use explicit diagnostics for every unresolved native or unsupported semantic. A no-op is acceptable only when authoritative Human02 behavior proves that the call has no observable effect; document the reason at the implementation.

Exit: `CampaignAI` starts both periodic threads, defender requests persist, attack requests fill through ordinary production queues, and the first assault completes through script control flow.

Implemented query subset: `GetUnitCount` and `GetPlayerUnitTypeCount` include live queued/constructing units because `common.ai` compares them against desired totals; `GetUnitCountDone` excludes training and active construction. All three exclude dead entities. `GetUnitGoldCost`, `GetUnitWoodCost`, and `GetUnitBuildTime` read typed `UnitBalance.slk`; `GetUpgradeLevel` and `UnitAlive` read authoritative tech and entity lifecycle state. `GetHeroId` and `GetHeroLevelAI` expose the temporary Hero rawcode/level context while a `SetHeroLevels` callback executes. The callback is evaluated synchronously in an isolated JASS scratch state and its returned ability rawcode is passed through ordinary `G_HeroLearnSkill`, so candidate-skill, required-level, max-rank, and skill-point rules remain shared with human Heroes. Newly trained and immediately JASS-created AI Heroes receive the callback when ready, and each subsequent level transition invokes it after the new skill point is awarded. `SetPeonsRepair` is reconciled for existing owned units when its policy changes, for units that finish training, and for immediately usable units created through JASS `CreateUnit`. `UpgradeData.slk` provides authoritative base/increment research costs and times. `GetUpgradeGoldCost`/`GetUpgradeWoodCost` expose the next researched level, and `SetUpgrade` now selects a legal owned producer and enters the normal `G_QueueResearch` path, so requirements, payment, queueing, in-progress locking, events, cancellation, and completion stay shared with human research. Expansion planning remains unresolved.

`StopGathering` stops only the invoking bot's units whose active movement belongs to lumber, gold, or wisp harvesting. It releases workers hidden inside gold mines through the ordinary mine-membership lifecycle, preserves carried resources, and leaves unrelated orders and other players untouched. A bounded ROC Human02 run now passes this call and reports `CreateCaptains` as the next unresolved native.

`CreateCaptains` initializes bot-owned assault and defense captain state and discards prior membership when scripts recreate captains. A bounded ROC Human02 run now passes captain creation and reports `IgnoredUnits` as the next unresolved native while translating campaign defender requests into build totals.

`CaptainInCombat(attack)` selects the assault or defense roster and returns true when any live member has a valid `combatentity`; stale, removed, or dead targets are cleared through `unit_affectingcombat`. A bounded ROC Human02 run now passes the defense economy check and reports `AddDefenders` as the next unresolved native.

`AddDefenders(qty, id)` idempotently fills the defense captain from live, completed, owned units of the requested type without stealing members already assigned to either captain. `common.ai` separately owns the production request; the boolean result reports whether the requested roster quantity is currently satisfied. A bounded ROC Human02 run now passes defender assignment and reports `FillGuardPosts` as the next unresolved native.

`InitAssault` starts a fresh forming attack captain without disturbing the defense roster. Each `AddAssault(qty, id)` adds that request to the captain's desired total and idempotently reserves live, completed, owned units of the requested type without stealing defense members; its boolean result reports whether that typed request is currently filled. This matches `FormGroup`, which rebuilds the roster on every retry before testing captain fullness and readiness.

`CaptainGroupSize`, `CaptainIsEmpty`, and `CaptainIsFull` inspect the attack captain and count only live roster members. Fullness compares that count with the accumulated desired total; a fresh zero-desire captain is therefore both empty and full, allowing Blizzard's zero-unit formation path to complete. Dead and removed members remain harmless stale references until the next `InitAssault` rebuild.

`SuicidePlayer(target, check_full)` launches the live attack captain against the target player's valid units, falling back to the bot's staged point when no target is visible. With `check_full`, an undersized captain remains forming and returns false so `common.ai` can retry. Once the deadline permits a partial wave, the same native launches any live roster members, records the staged goal when available, marks the captain active, and returns true.

### Captain Readiness Provenance

The authoritative reference is the ROC demo `data/Warcraft3demo/Game.dll`, SHA-256
`286823c37a1083e91f07d040e46a9df7af4c4952e01fcbba460589bd4e297654`. It is a PE32 x86 image linked by
Microsoft linker 6.0, timestamped September 14, 2002, with image base `0x6f000000`. The native wrappers are at
`0x6f30f6b0` (`CaptainReadiness`), `0x6f30f700` (`CaptainReadinessHP`), and `0x6f30f750`
(`CaptainReadinessMa`); all call the calculator at `0x6f3122e0`. The wrappers pass `(1, 1)`, `(1, 0)`, and `(0, 1)`
respectively. In this build the first two flags select the same HP result, while the mana wrapper selects mana.

For each selected stat, the calculator sums current and maximum values separately for live heroes and live non-heroes,
computes each aggregate percentage, and returns the lower category. It does not average per-unit percentages. Its
fixed-real divider returns `1.0` when numerator and denominator are equal, including `0/0`, so an absent category and a
category containing only mana-less units score 100%. Conversion to the JASS integer truncates toward zero. In compact
form, with category $c \in \{\text{hero}, \text{unit}\}$:

$$
R_c = \begin{cases}
100 & \text{if } \sum current_c = \sum maximum_c \\
100 \cdot \dfrac{\sum current_c}{\sum maximum_c} & \text{otherwise}
\end{cases}, \qquad readiness = \operatorname{trunc}(\min(R_{hero}, R_{unit})).
$$

Archive `Scripts/common.ai` independently establishes intent rather than the formula: `FormGroup` waits for
`CaptainReadiness() >= 50`, and `CommonSleepUntilTargetDead` retreats at `CaptainReadinessHP() <= 40`. Stock
`common.ai` declares but does not call `CaptainReadinessMa`. Jassbot documents all three declarations as integer natives
present since patch 1.00, but supplies no behavioral comments; do not use that declaration-only page to infer the math.

Reproduce the evidence with:

```sh
shasum -a 256 data/Warcraft3demo/Game.dll
rabin2 -I data/Warcraft3demo/Game.dll
r2 -q -e bin.cache=true -A -c 's 0x6f3122e0' -c pdf -c q data/Warcraft3demo/Game.dll
build/bin/mpqtool -mpq 'data/Warcraft III/War3.mpq' cat Scripts/common.ai | grep -n -C 8 CaptainReadiness
```

`AddGuardPost` stores persistent typed map positions authored by campaign AI and tracks how many replacements each post has consumed against the bot's current `SetReplacementCount` limit. `FillGuardPosts` first reserves completed owned units not already assigned to captains or another post; when no spare exists and that post still has replacements, it requests one ordinary trained unit through `SetProduce`/`SP_TrainUnit`, marks that replacement pending so repeated fill passes cannot duplicate the queue request, and decrements the post budget only after training is accepted. The retail default replacement limit is three until `SetReplacementCount` changes it. Assigned guards contribute to `IgnoredUnits`. `ReturnGuardPosts` preserves valid combat targets and returns idle guards that drift more than the documented 82.006 world-unit tolerance from their authored position. Human02 defines no posts, so its periodic fill/return cycle remains observably empty.

TFT `common.ai` uses JASS `debug call`, `debug set`, and `debug if` statements. The parser preserves the following ordinary statement as an AST node marked `TF_DEBUG`, and release execution skips that node. Parser failures now report the source line and next token, while staged bot loading identifies `common.j`, `common.ai`, or the requested script separately.

`DisplayText`, `DisplayTextI`, `DisplayTextII`, and `DisplayTextIII` are AI diagnostics, not client UI messages. They write one player-prefixed line to `stderr`, decode Blizzard's literal `\\n`, and substitute only the native family's zero to three `%d` values into a bounded buffer; unknown format sequences remain literal. With these diagnostics and `GetUnitBuildTime`, a bounded TFT Human02 run starts `h02_red.ai`, reports its authored wave estimates, and completes 6000 frames without a parser, unresolved-native, or JASS runtime error.

Human02's allied Uther patrol is map-trigger driven rather than an engine Patrol order. `Trig_Uther_Patrol_to_01_Actions` and `_02_Actions` issue alternating point `attack` orders; the corresponding region-entry triggers disable themselves, wait 12 seconds with `TriggerSleepAction`, enable the opposite reach/patrol triggers, and execute the next leg. Region conditions identify Uther through `GetEnteringUnit()`, so that native must return `JASSCONTEXT.unit` (the event subject), not `JASSCONTEXT.trigger`. Returning the trigger handle causes the generated `GetEnteringUnit() == <Uther>` condition to fail after the first leg and permanently stops the scripted patrol.

`IgnoredUnits` counts live, matching, bot-owned members across the assault and defense captain rosters. `common.ai` adds this value to desired production because captain members still contribute to `TownCount` after assignment away from town duties. A bounded ROC Human02 run now passes this query and reports `CommandsWaiting` as the next unresolved native.

`CommandAI` appends to a per-player command stack; `CommandsWaiting`, `GetLastCommand`, `GetLastData`, and `PopLastCommand` expose Blizzard's newest-command consumer contract to the bound bot VM. Empty reads return zero, commands remain isolated by player, and pending storage is released on bot replacement or shutdown.

`ClearHarvestAI` starts a fresh assignment pass; `HarvestGold` and `HarvestWood` reserve each live owned `Ahar` worker at most once across that pass. Town IDs enumerate owned completed gold-capable drop-offs in stable edict order. Workers and legal mines/trees are selected by geometric distance to that town; workers already carrying either resource return it before taking a new collection order. Missing towns or resources leave the requested quota unfilled rather than borrowing another town. A bounded ROC Human02 run now executes this economy loop and reports `CaptainInCombat` as the next unresolved native.

TFT `Blizzard.j` calls `SetAllItemTypeSlots(11)` and `SetAllUnitTypeSlots(11)` during neutral-building initialization before either campaign map starts. Stock capacities are level defaults propagated to existing units; `SetItemTypeSlots` and `SetUnitTypeSlots` provide per-unit overrides, and later spawns inherit the current defaults. Neutral shops consume `Sellitems` or non-Hero `Sellunits` plus their authored stock fields for shared purchase/restock behavior; Tavern/Hero sales remain separate work. See [Neutral Shops And Mercenary Camps](neutral-shops.md).

`EnableUserUI` is separate from `EnableUserControl` and must not be treated as a gameplay-command authorization gate. Warcraft uses it to suppress UI affordances such as unit/ability/item hover presentation and tooltips; disabling it does not make world selection, point targeting, Smart orders, inventory actions, research, or menu commands inert. OpenRealm records the per-client state for the presentation path, while `EnableUserControl` remains the control/input policy native. Gating `select`/`point`/`smart` on `EnableUserUI(false)` caused campaign fade/filter calls to leave ordinary mouse selection apparently dead after the rebase. Bounded TFT runs can still execute the native without a JASS error; client-side suppression of the corresponding hover/tooltips remains presentation work rather than server command filtering.

World selection itself may be updated before a reserved player slot has completed `ClientBegin` (notably in in-engine tests). In that state `CMD_Select` updates the authoritative selection and marks command presentation dirty, but defers portrait/info/inventory/command-bar serialization until the client is connected. Do not make selection correctness depend on an initialized server UI transport buffer.

Internal engine ownership uses `bot_t`, `level.bots`, and `G_Bot*`. Blizzard's exported JASS names retain `AI` because that spelling is part of the archive script ABI. Campaign/melee mode, replacement count, and the ROC/TFT policy toggles are persisted on each bot for the later production and captain consumers.

### Phase 4: Human02 Completion

- Run `h02_red.ai` unchanged.
- Preserve cumulative deadlines, preparation lead time, 50% readiness check, overdue launch, captain combat/empty waits, replacement count, and repeated waves 5-7.
- Recover through ordinary script polling when workers, producers, or units die, queues fill, placement fails, or resources are temporarily insufficient.
- Stop cleanly on pause, restart, player removal, victory/defeat, and map unload.

Tests assert all defender and wave quantities at easy, normal, and hard; cumulative deadlines; understrength timeout; casualty replacement; and the repeating 5-7 cycle.

Exit: bounded ROC and TFT Human02 runs show Blackrock producing and sending the authored forces toward player 1 with no client input, hidden grants, leaked VMs, or unbounded entity growth.

### Phase 5: AI Fog Consumer

- Decouple fog-grid updates from `client_connected` by registering started AI players as consumers.
- Build visible-opponent and last-seen snapshots from the server FOW query.
- Reject target actions against currently hidden entities while allowing movement toward remembered positions.
- Cover shared vision, lost vision, stale entity numbers, death, and player teardown.

Exit: hidden enemy movement cannot change current AI target data, while ordinary Human02 player-target assaults still navigate toward legal known goals.

### Phase 6: Event Acceleration And Budgets

Blizzard's campaign loops already reconcile counts on bounded two/five-second cadences. Events are an optimization and responsiveness layer, not a Human02 prerequisite.

- Observe game events and harvesting messages without consuming, reordering, or mutating map-trigger delivery.
- Mark affected cached facts dirty and wake only the relevant work.
- Keep script polling as authoritative recovery after dropped or overflowed notifications.
- Add bounded per-resume work and action-rate counters. Preserve script timing; do not impose an arbitrary 350 ms think interval on JASS coroutines.

Exit: event observation changes latency only, overflow self-recovers through polling, and diagnostics show bounded work and action rates.

### Phase 7: Melee And Advanced Tactics

After Human02 is complete:

- implement `StartMeleeAI` with `Scripts/orc.ai`, then other race scripts;
- add scouting, expansion, opponent memory, and utility arbitration only where Blizzard's melee scripts require or benefit from them;
- evaluate richer squad movement or bounded tactical search only at local squad contacts;
- add replayable decision/action traces and repeated evaluation across maps, opponents, and seeds before tuning tactical quality.

This phase is not part of Human02's definition of done.

Full multiplayer melee AI is tracked by [issue #215](https://github.com/corepunch/open-realm/issues/215). A two-player
Booty Bay lobby with an Orc computer slot reaches `MeleeStartingAI`, starts the unchanged `Scripts/orc.ai`, and uses the
same per-player VM as campaign AI. The first confirmed melee-only registration is `SetHeroLevels(function SkillArrays)`:
the callback is retained as VM-owned bot policy rather than eagerly called during `StandardAI` startup. A level-up
consumer has not yet been proven/implemented, so the stored callback must not be described as active skill-selection AI.

TFT melee initialization adds `Amic` to each starting town hall and marks it permanent before race AI starts.
`UnitAddAbility` and `UnitRemoveAbility` therefore maintain per-unit runtime additions and suppressions over immutable
`UnitAbilities.slk` rows; `UnitMakeAbilityPermanent` records which present abilities survive later unit-type changes.
Duplicate adds, absent removes, and permanence requests for absent abilities return false. Runtime skill queries consume
the same overlay, and entity removal, level shutdown, and test resets release its storage.

The lobby currently exposes no per-slot difficulty selector, so `GetAIDifficulty` returns
`AI_DIFFICULTY_NORMAL` for valid players. AI-script `MeleeDifficulty()` independently returns the `common.ai` integer
`MELEE_NORMAL` (`2`); do not return the `aidifficulty` handle payload directly because those enum values are
`AI_DIFFICULTY_NEWBIE/NORMAL/INSANE = 0/1/2`, while `common.ai` uses `MELEE_NEWBIE/NORMAL/INSANE = 1/2/3`.
`aidifficulty` is a value-like JASS enum handle and must remain in the VM's payload-comparison type table. Add a lobby
field before supporting newbie or insane; both APIs should then read the same per-slot setting and map it to their own
public value scales. Until then, all four race scripts take their normal-difficulty branches: newbie-only opening and
economy changes stay disabled, and no insane-specific behavior is selected. This is a known script-compatibility gap,
not a claim that the scripts' full difficulty behavior is implemented. Do not infer difficulty from race, team, or map
settings.

Hero-policy setters remain registration/state only unless explicitly documented
otherwise. In particular, `SetHeroLevels`, `SetHeroesFlee`,
`SetHeroesTakeItems`, `SetHeroesBuyItems`, and `SetTargetHeroes` currently store
VM-owned policy but do not yet provide a proven generic C consumer for skill
selection, retreat, item pickup/purchase, or target prioritization. Do not invent
those behaviors from the flag names; implement them only when the unchanged
Blizzard AI scripts and runtime contract establish when/how the policy is
consumed.

With these startup APIs, bounded TFT Booty Bay launches start all four unchanged race scripts without a JASS runtime
error: `Scripts/human.ai`, `Scripts/orc.ai`, `Scripts/undead.ai`, and `Scripts/elf.ai`. Keep slot type fixed at `2`
(computer) and vary the race argument from `1` through `4` when reproducing the matrix. Blizzard's physical ROC scripts
in `War3.mpq` are internally consistent. In ROC mode `FS_ArchiveFileVisible` hides only `Scripts/*.ai` from
`War3Local.mpq`, whose patched copies match TFT and are incompatible with ROC `common.ai`; localized non-AI files remain
visible. TFT mode retains the ordinary expansion archive precedence. Command tests cover both policies, so do not
register the TFT-only JASS helper `SetSkillArray` as an engine native or disable the whole localization archive.

### AI Native Inventory For Retail Melee Scripts

This inventory is based on the retail TFT `Scripts/common.ai` and the four race libraries (`human.ai`, `orc.ai`,
`undead.ai`, `elf.ai`) extracted from the installed MPQs. It follows calls reachable from each race library's `main`,
including function values passed to `StartThread` and common-library worker loops. The common library declares more
natives than these four scripts use. `StartMeleeAI` is a separate map-facing JASS native which loads a race script; it
is not declared in `common.ai`.

The same 72 `common.ai` native names are reachable for each race. The race libraries vary their calls to common helper
functions, but their reachable native surface is the same. All 72 names are registered in OpenRealm's JASS module.

### Alliance Assault Target

Stock `common.ai` uses `SetAllianceTarget(unit)` and `GetAllianceTarget()` to coordinate one shared assault target among
mutually allied melee AIs. Publishing a target stores the common value for the caller and players with mutual
`ALLIANCE_PASSIVE`; publishing `null` clears that shared value. `GetAllianceTarget` returns the stored live unit. These
natives only share target state: Blizzard scripts explicitly form groups and issue `AttackMoveKillA` orders.

OpenRealm implements this coordination contract without minimap signaling or automatic group/order behavior. Other
alliance arbitration and expiration policies remain separate compatibility work.

`RemoveInjuries()` removes dead entries and live attack-captain members below 50% health from that roster, then orders
injured units toward the invoking player's primary gold-dropoff when one exists. The AI Editor documents below 50% as
the injured threshold and describes returning injured assault units home or to a healing location. `BZ_COMPAT_GUESS`:
the exact preference between a captain home, town, and nearby Fountain of Health is unresolved; OpenRealm uses the main
gold-dropoff and keeps that destination choice inside this behavior.

### Unit Conversion

`ConvertUnits(qty, id)` consumes owned live units of source type `id` through their authored no-target metamorphosis
ability. Stock `common.ai` calls `ConvertUnits(desire, OBS_STATUE)` while satisfying a requested Destroyer count, so the
implementation treats `qty` as the desired final count of the authored target form, counts already-completed target
units first, and converts only the remaining shortfall. The native uses the ordinary spell path rather than directly
rebinding the entity, preserving ability validation, spell events, cooldown/mana handling, and the existing in-place
morph identity.

`BZ_COMPAT_GUESS`: Blizzard does not document the `qty` interpretation directly; deriving it as a desired final target
count comes from the surrounding stock `Conversions(desire, unitid)` helper and avoids over-converting when target-form
units already exist. OpenRealm's generic `CAbilityMetamorphosis` path does not yet model Destroyer Form's retail
gold/lumber/food delta, so that resource-cost parity remains separate ability work rather than special-cased AI-native
accounting.

### Threatened Town Query

`TownThreatened()` reports whether any live unit or building owned by the invoking AI is currently the target of a live
hostile unit's active Warcraft attack behavior. The name is misleading: retail-facing `common.ai` documentation records
that the native covers any owned unit/building, and stock melee AI uses it as a global defense gate before launching
attacks and while refreshing `AttackMoveKill` pursuit. OpenRealm therefore does not invent a town radius, base
classifier, or recent-damage timer.

`BZ_COMPAT_GUESS`: retail documentation establishes the broad "any owned unit/building is being attacked" condition,
but not whether the engine latches that state briefly after an attack order or damage event ends. OpenRealm currently
uses the live attack-order state itself; keep that timing choice local to `G_BotTownThreatened` if direct retail capture
later establishes a persistence window.

The query requires the attacker to be executing `CAbilityAttack`, to have a live owned monster/building as its current
goal, and to be hostile under the ordinary WC3 alliance/targeting rules. Attack-move waypoints, friendly force-fire,
spell/DoT damage, stale combat links, and an owned unit merely fighting an enemy do not make the town threatened unless
an enemy is actively attacking an owned unit. This keeps `TownThreatened` a read-only AI query; it does not form the
defense captain or issue any orders.

### Towered Target Query

`IsTowered(target)` reports whether a live target is defended by an attack-capable building owned by the target's
player while the target is also in that player's base area. Stock melee AI uses this before attacking an enemy expansion:
without siege support it avoids a hall that is tower-defended. Retail-facing AI documentation also records that custom
attack-capable buildings may qualify and that the defending building's authored attack/acquisition ranges do not control
the query. OpenRealm therefore classifies a defender from ordinary unit metadata (`G_UnitIsBuilding`, enabled attack slot,
non-`ATK_NONE` attack) rather than hardcoded Guard Tower/Cannon Tower/etc. rawcodes.

`BZ_COMPAT_GUESS`: Blizzard does not expose the internal base-proximity or tower-proximity radii. OpenRealm currently uses
1024 world units for each test. Those constants live only in `g_bot.c`; direct retail/native capture can replace them without
changing generic combat range, acquisition, building metadata, or the JASS API. The defending tower is required to share the
target's owner; the invoking AI player's own towers do not make an enemy expansion "towered".

### Zeppelin Purchase

`PurchaseZeppelin()` implements the `common.ai` convenience action for obtaining a Goblin Zeppelin (`nzep`). Retail-facing
native documentation says the call requires one of the AI player's Heroes near a Goblin Laboratory. OpenRealm searches
live Neutral Passive unit shops that sell `nzep`, requires a live owned Hero within the shop's authored `Aneu`/`Aall`
activation radius plus ordinary unit/shop collision reach, then delegates to `G_ShopPurchaseUnit`.

The neutral-unit shop path remains authoritative for stock/restock, resource and food checks, legal exit placement, and
unit ownership. This native does not spawn or grant a Zeppelin directly; without a qualifying Hero/shop pair it does
nothing.

### Creep Camp Query

`GetCreepCamp(min, max, flyers_ok)` searches live `PLAYER_NEUTRAL_AGGRESSIVE` creeps and returns a representative unit
from the nearest qualifying camp. Retail-facing `common.ai` documentation defines camp power as the sum of the
`UnitBalance.level` values of all creeps in that camp; the inclusive `min`/`max` bounds filter that total. When
`flyers_ok` is false, a camp containing a unit whose authored movement type is `fly` is excluded. Stock helpers use
`GetCreepCamp(0, 9, false)` for minor camps and `GetCreepCamp(10, 100, allow_air_creeps)` for major camps.

`BZ_COMPAT_GUESS`: Blizzard does not expose the internal camp-membership structure or grouping radius. OpenRealm groups
Neutral Hostile units into connected components where neighboring creeps are at most 600 world units apart, then chooses
the qualifying component nearest the invoking AI's primary town. If no town exists, the first live owned unit supplies
the search origin. The returned representative is the camp member nearest that origin. Keep this geometry confined to
`G_BotGetCreepCamp` so direct retail/native capture can replace it without changing creep ownership, unit level data, or
assault behavior.

### Enemy Base Discovery

`StartGetEnemyBase()` / `WaitGetEnemyBase()` / `GetEnemyBase()` implement the stock asynchronous enemy-base query
contract. Retail-facing documentation requires Start before Get, and unchanged melee AI polls Wait once per second until
it returns false. OpenRealm keeps discovery state per AI player: Start clears any prior result and begins a search, Wait
reports that search as pending until completion, and Get returns only the completed live hostile town-hall result.
Calling Get before Start or while discovery remains pending returns null.

The completed search chooses the nearest live hostile AI town hall to the invoking AI's primary town. If the caller has no
primary town, its first live owned unit supplies the distance origin; without either, stable player/town enumeration wins.
All current town IDs are eligible because retail/community observation describes `GetEnemyBase()` as finding the closest
enemy base, while stock `common.ai` separately gives `GetEnemyExpansion()` higher priority before starting this discovery.
The query is read-only and does not issue captain orders, publish an alliance target, or reveal fog.

`BZ_COMPAT_GUESS`: Warcraft exposes this as an asynchronous search but does not document its exact latency or internal
search algorithm. OpenRealm currently holds the search pending for 1000 milliseconds, matching the stock script's polling
cadence, then performs the deterministic nearest-town query above. Keep the delay and target arbitration confined to the
bot discovery helpers so direct retail/native capture can replace them without changing the JASS API or town bookkeeping.

### Enemy Expansion Query

`GetEnemyExpansion()` returns a live hostile expansion hall using OpenRealm's existing AI town model. Retail
`common.ai` describes it as the enemy expansion base and calls it before asynchronous
`StartGetEnemyBase` / `WaitGetEnemyBase` / `GetEnemyBase` discovery. OpenRealm treats hostile town IDs after the primary
town (`1+`) as expansions. The query does not publish an alliance target, form a captain, issue an attack, or modify
enemy-base discovery state.

`BZ_COMPAT_GUESS`: Blizzard does not publish how this native chooses among several valid enemy expansions. OpenRealm
selects the hostile expansion nearest the invoking AI's primary town, with entity order as a stable tie-break. If the
caller has no primary town, stable player/town enumeration wins. Because town IDs currently enumerate live owned gold
drop-offs in spawn order, destroying an enemy primary hall can cause a former expansion to become town `0` and no longer
be classified as an expansion. Stable historical town identity remains separate AI-town parity work.

### Mega Target Query

`GetMegaTarget()` participates only when `SetWatchMegaTargets(true)` has enabled the bot's `BOT_WATCH_MEGA` policy. Stock
melee AI gives this query priority over ordinary enemy-expansion/base discovery, and contemporary analysis describes the
mega target as an enemy main base left vulnerable while its defending army is elsewhere. OpenRealm therefore considers
only each hostile player's primary town hall (`G_BotTown(enemy, 0)`), never an arbitrary unit or expansion.

A main hall is eligible only when it is live, hostile, not `IsTowered`, and has no nearby live non-worker combat unit owned
by that player. Workers carrying the stock/custom `Ahar` harvest command do not by themselves disqualify a vulnerable main.
The native is a read-only selector: it does not publish an alliance target, form a captain, issue orders, or ping the minimap.

`BZ_COMPAT_GUESS`: Blizzard does not publish the internal mega-target defender radius, combat-unit weighting, or arbitration
when several enemy mains are simultaneously vulnerable. OpenRealm currently uses a 1200-world-unit protection radius and,
when the invoking AI has a primary town, selects the qualifying main nearest that town; otherwise stable player-slot order
wins. Keep those choices local to `G_BotGetMegaTarget` so direct retail/native capture can replace them without changing
ordinary enemy-base discovery or captain attack behavior.

`RemoveSiege()` compacts the attack-captain roster and removes live members classified as siege units before the next
melee assault group is assembled. This matches stock `common.ai`, where `InitMeleeGroup()` calls `RemoveInjuries()` and
then `RemoveSiege()` before adding the new assault specification. Removed siege units remain ordinary owned world units;
the native does not issue movement/attack orders or destroy them, leaving artillery policy separate from roster cleanup.
`BZ_COMPAT_GUESS`: Blizzard does not document the native's exact siege classifier. OpenRealm currently treats a unit as
siege when either authored attack slot uses `ATK_SIEGE`. This is data-driven and matches the stock siege-engine combat
profile without baking race/unit rawcodes into AI code, but direct retail/native reverse engineering could reveal an
additional unit-class or weapon-type test. Keep that classifier local to `G_BotRemoveSiege`.

Expansion support follows Blizzard's `common.ai` sequence: `GetNextExpansion` selects and caches a viable unclaimed gold mine; `GetExpansionX/Y`, `GetExpansionFoe`, and `GetExpansionPeon` query that same selected site; `SetExpansion(peon, hallId)` validates the worker and requested building then submits a normal build order near the selected mine. Accepted construction is paid and placed by the regular WC3 build path, and the resulting completed hall enters the existing town enumeration automatically. No hall is spawned directly. `GetNextExpansion` preserves a selected site while valid and retains it while an accepted hall build project is active; a completed or failed request allows selection to resume. `BZ_COMPAT_GUESS`: candidate ranking uses distance from the first owned town hall; hostile blockers use a 1200-unit radius and nearest-unit priority; hall placement searches deterministic 32-unit cells around the mine, starting just beyond the normal 512-unit resource-dropoff clearance; worker choice is the nearest available construction-capable harvesting worker.

`GetTownUnitCount` is now implemented using the existing stable town IDs and nearest-town ownership rule. It counts
live owned units of the requested rawcode at that town; `dn == false` includes queued training, active construction, and
accepted builder projects, while `dn == true` counts completed units only. Invalid town IDs return zero. The nearby
town assignment uses nearest owned gold drop-off by distance, matching `SetProduce` town selection.

`UnitInvis(unit)` reports the unit's intrinsic active invisibility through `S_UnitHasInvisibilityState()`. It is
independent of which player can see or detect the unit. Stock `common.ai` checks `UnitInvis(target)` separately from
`IsUnitDetected(target, ai_player)`; the player-relative `IsUnitDetected` and `IsUnitInvisible` JASS callbacks remain
placeholders. See [Warcraft III Invisibility](invisibility.md#ai-and-jass-queries).

The remaining reachable natives are registered: `AddAssault`, `CaptainInCombat`, `CaptainIsEmpty`, `CaptainIsFull`,
`CaptainRetreating`,
`CaptainReadiness`, `CaptainReadinessHP`, `ClearHarvestAI`, `CommandsWaiting`, `CreateCaptains`, `DisplayText`,
`DisplayTextI`, `GetAiPlayer`, `GetGoldOwned`, `GetHeroId`, `GetHeroLevelAI`, `GetMinesOwned`, `GetUnitCount`,
`GetUnitCountDone`, `GetUnitGoldCost`, `GetUnitWoodCost`, `GetUpgradeGoldCost`, `GetUpgradeLevel`,
`GetUpgradeWoodCost`, `HarvestGold`, `HarvestWood`, `InitAssault`, `MeleeDifficulty`, `MergeUnits`, `SetDefendPlayer`,
`SetGroupsFlee`, `SetHeroLevels`, `SetHeroesBuyItems`, `SetHeroesFlee`, `SetHeroesTakeItems`, `SetIgnoreInjured`,
`SetMeleeAI`, `SetPeonsRepair`, `SetProduce`, `SetSmartArtillery`, `SetTargetHeroes`, `SetUnitsFlee`, `SetUpgrade`,
`SetWatchMegaTargets`, `Sleep`, `StartThread`, `StopGathering`, `TownHasHall`, `TownHasMine`, `TownWithMine`,
`UnitAlive`, `GetTownUnitCount`, `UnitInvis`, `GetNextExpansion`, `GetExpansionFoe`, `GetExpansionPeon`,
`GetExpansionX`, `GetExpansionY`, `SetExpansion`, `GetAllianceTarget`, and `SetAllianceTarget`.

Registration and behavior are separate questions. For example, several melee policy setters and `SetHeroLevels` store
per-bot policy without a proven consumer; see the policy caveats above. `StartMeleeAI`, `StartCampaignAI`, and
`GetAIDifficulty` are declared by `common.j` rather than `common.ai`; OpenRealm registers all three, and the startup
functions load the requested player-bound AI script. `CommandAI` is likewise a `common.j` native and is implemented
through the ordered per-player command queue. `Player`, `GetRandomInt`, `VersionCompatible`, JASS control flow, and
`GetGameDifficulty` are ordinary JASS/common.j facilities, not `common.ai` natives.

### Melee Economy And Production

Rivercross proved the following runtime contracts:

- Canonical neutral slots are hostile `12`, victim `13`, extra `14`, and passive `15`. Its two `ngol` mines are owned by
  neutral passive. `IsUnitType(unit, UNIT_TYPE_STRUCTURE)` must classify buildings through `G_UnitIsBuilding`; otherwise
  Blizzard's `MeleeClearExcessUnit` removes both mines during map initialization.
- Town IDs enumerate completed owned gold drop-offs in stable edict order. A mine belongs to its nearest owned town.
  `TownWithMine`, `TownHasMine`, `TownHasHall`, `GetMinesOwned`, and `GetGoldOwned` expose those authoritative facts.
- `SetProduce` uses `SP_TrainUnit` and `G_IssueBuildOrder`. Non-done counts include accepted `build_project` orders and
  queued/constructing entities so `common.ai` does not duplicate requests; done counts exclude training and construction.
- A `ClearHarvestAI` pass must reserve active matching harvesters before assigning idle workers. Restarting a worker's
  `a_goldmine` move while it is hidden inside the mine resets its mining timer and stalls income. Pending builders,
  active Human construction workers, and hidden training entities are not eligible harvest candidates.
- Queued units are suspended with no movement or animation until revealed. Their `build` field is the queue link, so an
  animation callback on a hidden trainee would sever the producer queue.
- AI structure orders are serialized while one worker has `build_project`, because pending footprints are not baked.
  Candidate sites must also have a direct collision-safe static route from the selected worker. This prevents repeatedly
  accepting a legal footprint across disconnected Rivercross terrain. During execution builders use collision-radius
  flow routing; the larger structure footprint distance remains only the arrival threshold.
- SLK placement predicate `_` means no authored predicate. Treat it like an empty value; continue reporting genuinely
  unsupported placement tokens.

With these rules, a bounded Human ROC Rivercross run repeatedly returns gold and completes five queued Peasants, a
Barracks, and a Farm without a JASS runtime error. Use `WC3_DEBUG_AI` builds for requested/accepted/completed production
and captain milestones; detailed traces remain disabled in normal builds.

### Captain Retreat State Query

`CaptainRetreating()` is now registered and reports whether the **attack captain** is in `BOT_CAPTAIN_RETREATING`.
Stock `common.ai` uses this as an engine-state query in `SleepUntilAtGoal` and `CommonSleepUntilTargetDead`; it does not
request a retreat itself. The defense captain's state does not affect this native.

This closes the reachable native lookup gap without fabricating the separate flee policy. `SetGroupsFlee(true)` is still
stored as `BOT_GROUPS_FLEE`, but OpenRealm does not yet implement retail's engine-side transition that decides a group is
losing/at a disadvantage, issues the return-home behavior, enters `BOT_CAPTAIN_RETREATING`, and later completes that
retreat. Community/AI Editor documentation ties `SetGroupsFlee` to whole-group retreat, but does not establish the exact
power/health thresholds. Do not turn `CaptainRetreating()` into a health-threshold heuristic: stock `common.ai` separately
checks `CaptainReadinessHP() <= 40`, proving that the two conditions are distinct.

With this registration, every `common.ai` native in the previously inventoried stock melee-AI reachable set has a callback.
Registration is not full behavior parity: the flee transition above and other policy consumers documented in this file remain
separate runtime work.

### Assault Captain Target Orders

`AttackMoveKill(unit)` now activates the attack captain and issues ordinary WC3 attack-move orders to every live assault
member toward the target's current position. This follows stock `common.ai`: `AttackMoveKillA` calls the native once, then
`CommonSleepUntilTargetDead` calls it again after each three-second `SuicideSleep`, so a moving target is refreshed by the
script rather than by a second C-side pursuit scheduler. Null/dead targets and empty assault rosters are no-ops. The native
does not form the captain; `InitAssault`/`AddAssault`/`FormGroup` remain responsible for roster formation.

Retail-facing documentation also describes an attack-location minimap signal and the surviving group returning after the
target dies. Those presentation/retreat details are not implemented here: OpenRealm currently has no captain retreat
transition consuming `home`, and the exact native-specific signal policy has not been recovered. Do not emulate either by
changing generic attack-move behavior; add them when the captain/presentation contracts are established.

#### Multiplayer Test Map

Use [Rivercross by Bernard](https://www.epicwar.com/maps/192650/) as the standard manually downloaded ROC multiplayer
map. It is a 64x64, two-player melee map saved by Warcraft III 1.00 editor build 4448; its 52x52 playable area keeps
startup tests small. Download `(2)Rivercross.w3m` into `data/Warcraft III/Maps/`. The expected SHA-256 is
`d0e539329a6567e7695a130607417fcd052c36388d4e76c58ec6f99302ab3cf0`.

Rivercross is free to download for play, but no redistribution license is published by its author or EpicWar. Credit
Bernard whenever referring to the map, keep the file in the ignored local `data/` tree, and do not commit it or package
it with OpenWarcraft3 without explicit permission. Tests committed to the repository must continue to use
project-authored fixtures under `games/warcraft-3/tests/resources-src/`.

```sh
build/bin/openwarcraft3 -data 'data/Warcraft III' -roc +set vid_hidden 1 \
  +lobby_start 'Maps/(2)Rivercross.w3m' +lobby_config 2 2 Rivercross \
  +lobby_slot 0 1 0 1 1 0 0 Player +lobby_slot 1 1 1 2 1 1 1 Computer \
  +map 'Maps/(2)Rivercross.w3m' +com_frame_limit 1000
```

## Verification

Run targeted tests first, then the complete suite:

```sh
make test-wc3-engine WC3_PATTERN='wc3_bot.*'
build/bin/openwarcraft3 -data 'data/Warcraft III' +set vid_hidden 1 +set skip_cutscene 1 +map 'Maps/Campaign/Human02.w3m' +com_frame_limit 6000
build/bin/openwarcraft3 -data 'data/Warcraft III' -tft +set vid_hidden 1 +set skip_cutscene 1 +map 'Maps/Campaign/Human02.w3m' +com_frame_limit 6000
make test
```

For each bounded acceptance run record:

- AI VM start, pause, replacement, and shutdown;
- difficulty and selected defender/attacker quantities;
- map-authored resource changes and AI spending;
- requested, queued, completed, assigned, and lost units by FourCC;
- nominal deadline, formation start, launch time, and captain-empty time per wave;
- failed actions grouped by reason;
- coroutine resumes, native calls, actions, and CPU time per interval.

Do not treat one visually successful run as sufficient. CI assertions use deterministic map/script state; quality evaluation uses repeated runs and reports completion rate, first-assault time, supply-block time, resource idle time, action rate, and CPU time.

## Reference-Derived Decisions

- Quake III `ai_main.c`: reuse explicit per-player setup/shutdown, one frame-owned update, residual scheduling where native C work needs it, and staggered expensive work. Do not port AAS, FPS user-command synthesis, inventory, chat, or a separate bot DLL.
- Blizzard JASS: keep strategy, desired-count queues, cooperative timing, and captain sequencing in scripts. C provides player-bound queries and legal actions; it must not duplicate the script's scheduler or build policy.
- AlphaStar: imperfect information, long horizons, hierarchical actions, and macro/micro separation justify a strict observation/action boundary and fairness metrics. Its learning architecture is out of scope.
- Gym-microRTS: parameterized legal actions and invalid-action masks support typed actions and reason codes. Its full-game results do not establish WC3 partial-observation or multi-map behavior.
- microRTS representation study: local observations improved a preliminary harvesting task. This supports keeping worker execution local, not a claim that local observations are globally superior.
- StarAlgo: duration-aware search is evidence for isolating optional search to squad movement, not for adding search to Human02.
- STARDATA: periodic full-state/action traces and dataset validation motivate replayable diagnostics if learned policies are pursued later.
- BWAPI: visible-state-by-default and a narrow game/AI interface are useful non-cheating boundary patterns.

## Guardrails

- Server authority is absolute; clients only render resulting state.
- Do not widen `playerState_t` or `entityState_t` for private AI state.
- Do not inspect hidden live enemies, fake connected clients, or route through selection/UI callbacks.
- Do not hardcode map names, Human02 behavior, race costs, prerequisites, or production capabilities in C.
- Do not silently skip unresolved scripts, natives, rawcodes, producers, targets, or placements.
- Do not directly manipulate movement/combat thinker fields from player AI.
- Keep execution serialized until JASS host and current-context state are VM-owned.

## Definition Of Done: Human02

1. `StartCampaignAI(Player(0), "h02_red.ai")` creates one player-bound AI VM and executes the unchanged archive scripts.
2. `CampaignAI` runs Blizzard's campaign basics/build loops through the required native surface.
3. Blackrock pays normal costs, uses map-authored resources, maintains defenders/replacements, and produces the difficulty-selected waves.
4. Assault captains follow `SuicideOnPlayer` timing/readiness semantics and attack player 1 through ordinary orders and unit AI.
5. Live enemy targeting obeys server fog of war; remembered contacts contain snapshots, not hidden entity pointers.
6. Pause, replacement, player removal, victory/defeat, restart, and unload release all AI coroutines and state.
7. Fixed-state tests are deterministic; bounded ROC and TFT Human02 runs pass; `make test` is green.

## Diagnostic Commands

```sh
build/bin/mpqtool -mpq 'data/Warcraft III/War3.mpq' cat Scripts/h02_red.ai
build/bin/mpqtool -mpq 'data/Warcraft III/War3.mpq' cat Scripts/common.ai | sed -n '1210,1420p;1770,1900p'
/usr/bin/grep -nE 'BotAIStartFrame|BotScheduleBotThink|BotAISetupClient|BotAIShutdownClient' \
  data/Quake-III-Arena-master/code/game/ai_main.c
```

The archive script and active game data are authoritative when ROC and TFT differ.
