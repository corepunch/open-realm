# Harvest and Return Resources current ownership (ORDER-01.12)

This document records the retail 1.27 `GetUnitCurrentOrder` contract for Ahar
gold and lumber workers, and the engine owners that reproduce it. It covers
Human Peasant and Orc Peon gold and Human Peasant lumber. Other race resource
paths are listed under [Remaining scope](#remaining-scope).

## Retail evidence

All captures come from game.dll 1.27.1.7085 (sha256 `d51e5680...8236`) in owned
research environments B/C. Two public JASS probes run on the flat
`formation_policy` arena with a 64x64 shadow member:

| Map | Probe | Scenes | Public markers | Stream sha256 |
| --- | --- | --- | --- | --- |
| `RS-H012a-261020.w3m` | `tools/frida/research/harvest012_probe.j` | 21 gold scenes, 240 ticks each | 5027 | `55eef175...a90b` |
| `RS-H012b-261020.w3m` | `tools/frida/research/harvest012b_probe.j` | 7 inside-mine, 3 lumber, 1 Peon | 3831 | `3aa63c7f...ed6d` |

Each map has two read-only observed repeats (`harvest012_observer.js` records
the probe markers plus every worker `680320` admission and `693490` append)
and one observer-free control. Within each map, all three public streams are
byte-identical, which gives 26,574 public markers in total. The observer makes no game calls
and writes nothing. The frozen bundle
`tools/ghidra/fixtures/retail-harvest012-captures-1.27.json.gz` stores every
raw stream. `tools/ghidra/fixtures/retail-harvest012-1.27.json` holds the
derived ownership model (232 + 118 transitions) and the 129 + 70 observed
admissions. It also pins 1,547 original instruction bytes of `438410`,
`438880`, `437fb0`, `687f60` and the `680320` `Unit198` branch.

## Ownership contract

| Phase | Head (`GetUnitCurrentOrder`) | Publication |
| --- | --- | --- |
| Smart on a mine/tree, approach, hidden interval, chopping | `851971` | issued target Smart |
| Explicit Harvest on a mine/tree | `852018` | issued target Harvest |
| Mine exit with gold, carried arrival at a mine, full lumber load | `852017` | internal append (`438410` caller `4384e4`; lumber `437fb0` caller `43817c`) and issued target Resume Harvesting to the nearest drop-off |
| Internal deposit (gold or lumber) | `852018` | shared deposit `438880` (caller `438a27`) and issued target Harvest to the remembered mine or tree |
| Smart on a drop-off while carrying | `851971` until deposit, then `852018` | as above |
| Explicit Return Resources (`852020`) | `852020` until deposit, then `0` | the `438880` third argument returns before resumption |
| Resume Harvesting (`852017`) | carrying: return then `852018`; empty: walk to the remembered mine | issued immediate |
| `autoharvestgold` | `852018` | issued immediate `852021`, then issued target Harvest to the nearest mine |
| Stop / Move / death | `0` / `851986` / `0` | ordinary |

Rejections are synchronous and publish nothing: empty Return Resources,
Resume Harvesting without a remembered resource, and Harvest on a drop-off.
Resumption also yields to any user successor (`687f60`), so a queued command
activates instead of the internal head.

### Hidden miners retain commands below `Unit198`

A worker inside a gold mine owns one `Unit198` separation-disable count.
Original `680320` therefore takes its positive-depth branch: it cancels queued
successors (`673e80`) and appends the new user command without dispatching it
(return address `680351`). The public head stays `851971`/`852018`/`852017`
until the hidden interval ends. Then the mine pays out and the retained
command activates and publishes:

- Move: `851986`, carrying the load.
- Stop: `0`.
- Hold Position: `0` after publishing `851993`.
- Smart on the hall: `851971`, then deposit and Harvest resume.
- Harvest on the mine: `852018`, then `852017` one tick later.
- Resume Harvesting: `852017` with an immediate publication.
- `autoharvestgold`: `852021` followed by Harvest `852018`.

A second replacement while inside keeps only the newest command. Empty Return
Resources is rejected while inside. This corrects the earlier engine claim that
hidden miners are non-orderable; that test now asserts the retained contract
(`wc3_movement.gold_miner_inside_retains_orders_and_unregisters_once`).

KillUnit inside a mine retires the head and reveals the corpse in the same
tick. RemoveUnit of the mine keeps the hidden head for the current tick; the
worker leaves on its next tick with no payout and head `0`. RemoveUnit of the
drop-off during a return retires the head on the next tick and keeps the load.

## Early-return publisher inventory

Retail publishers. Each return below skips the internal append, so the
current head completes instead.

| Publisher | Early return | Integrated as |
| --- | --- | --- |
| `438410` mine exit / carried arrival | user successor pending (`687f60(1,1)`) | `goldmine_begin_return`: `G_UnitQueuedOrderCount` -> stand -> FIFO activation |
| `438410` | no drop-off (`414600`) | stand, head `0`; load kept |
| `438410` | ability virtual `2fc(d0031,target)` accepts (sets flag `0x400`) | not integrated; no public producer witnessed |
| `438880` deposit | third argument set (explicit Return) | `S_HarvestDepositResumes`: head `852020` completes |
| `438880` | cannot move (`Unit_CanMoveWithQueryFlags`) or successor pending (`687f60(0,0)`) | successor check integrated; movement-disabled deposit not witnessed |
| `438880` | no remembered resource (`415ca0`, then `416740`) | depleted/removed mine or no live tree -> stand |
| `438880` | virtual `2fc(d0032,target)` accepts | not integrated |
| `437fb0` lumber completion | cannot move / successor pending | successor check integrated |
| `437fb0` | AI admission flag `4` immediate `d0006` branch | not integrated (AI scope) |
| `437fb0` | partial load and a replacement tree -> Harvest append | not integrated (remaining) |
| `680320` | `Unit198 > 0` -> append without dispatch | hidden-miner retention |

Engine entry points that return before a harvest publication:

| Entry | Early return |
| --- | --- |
| `G_IssueUnitTargetOrder` Harvest | invalid target family -> reject before FIFO |
| `unit_issueimmediateorder` Return/Resume | `S_HarvestCanIssueImmediate` false -> reject before FIFO |
| `unit_issueimmediateorder` hidden miner | unwitnessed immediate commands (other than Stop, Hold, Return, Resume, `autoharvestgold`) reject |
| `S_HarvestAutoGoldOrder` | no mine -> reject; immediate callback replaces the head -> no Harvest; mine lost during callback -> head `0` |
| `S_HarvestPublishPhase` | callback replaces the head or move, or the worker is lost -> caller skips its task |
| `unit_issuetargetorder_now` / `unit_issueorder_now` | hidden miner -> reject (unreachable: retention activates after exit) |
| `ai_walkmine` | mine not harvestable -> stand (differs from retail mine death; remaining) |
| `ai_minegold` | mine removed or reused -> release without payout, stand |
| `harvestgold_walkback` | zero payout -> stand |
| `ai_goldmine_walkback` / `ai_harvest_walkback` | no compatible drop-off -> stand with load |

## Engine owners

- `s_goldmine.c`:
  - `goldmine_register_miner`/`goldmine_unregister_miner` acquire and release
    the `Unit198` count.
  - `goldmine_begin_return` publishes Resume Harvesting at mine exit and at a
    carried arrival.
  - `goldmine_finish_deposit` publishes the resumed Harvest through
    `S_HarvestDepositResumes`.
  - `ai_minegold` releases a worker whose mine was removed.
  - `S_GoldMineCanHarvest` rejects removed mines.
  - `CAbilityGoldMine` subscribes to generic death events through `AB_ENGINE_EVENTS`; it releases hidden membership before public death callbacks. The worker need not own Agld.
- `s_harvest_lumber.c`:
  - `S_HarvestPublishPhase` publishes an internal head before its task, so a
    callback replacement wins.
  - `S_HarvestAcceptsTarget` / `S_HarvestStartTarget` own the concrete target-family rules; general order code only dispatches.
  - Immediate Return/Resume validation and dispatch.
  - `autoharvestgold` admission and activation.
  - `harvest_lumber_begin_return` for full loads, and the lumber deposit
    resume.
  - `S_CanReturnResourceAt` rejects removed drop-offs.
  - The command-card Return issues the public `returnresources` order.
- `m_unit.c`:
  - Smart heads.
  - A validated Harvest target family that now follows the ordinary
    FIFO/retention path, including Shift and hidden-miner retention.
  - Hidden-miner target, point and immediate admission through
    `G_RetainSuspendedUnitOrder`.
  - Queued activation of `returnresources`, `resumeharvesting` and
    `autoharvestgold`.
  - Mine membership cleanup is delegated to the Gold Mine lifecycle owner.

Save format 218 retains the hidden miner's `Unit198` count and the retained
successor (see [save-load.md](save-load.md)).

## Verification

- `games/warcraft-3/game/tests/t_harvest012.c` contains five regressions:
  - JASS-observed Human gold phases, including save/load inside the mine and
    during a return, death, mine and drop-off removal, and slot reuse.
  - Human lumber phases.
  - Six hidden-miner command families.
  - The Gold Mine death owner receives the worker event without the worker owning Agld; repeated delivery releases no additional count.
  - An authored Orc Peon follows the gold heads and restores a saved hidden Move successor without loader errors.
- The preserved three-test baseline failed92 assertions per edition and then passed202.
  Integration adds a genuine five-failure death-owner regression and a Peon
  fixture regression (the archive lacked Ahar). The five journeys now pass241
  assertions per edition; including the217-version rejection gives six tests /251 assertions.
- `tools/ghidra/verify_wc3_pathing_harvest012.py --binary <game.dll>` re-derives
  the model from the frozen streams, checks the instruction guards and runs
  the engine regressions in Classic and TFT.
- `tests/test_wc3_pathfinding_harvest012.py` rejects rehashed semantic
  mutations: a changed head, a divergent repeat, a changed caller, a missing
  admission, an instrumented control, lost ownership, an owner lease and an
  incomplete footer.

## Remaining scope

- **Mine death during approach:** retail keeps Smart and walks past the
  normal entry point, clearing at about 63.2 units from the mine centre
  (H012 scene14, ticks 3366..3399). The engine retires the head at once.
- **Drop-off removal during a return:** retail publishes two `852017` events
  that still name the removed hall before the head becomes `0` (scene13). The
  engine publishes none.
- **Partial-load tree retarget:** the static `437fb0` path appends Harvest to a
  replacement tree; it is not live-witnessed or integrated.
- **Admission flag exclusions:** the `687f60` flag-2 and `0xd0005` exclusions,
  and the `437fb0` AI admission-flag immediate branch.
- **Other races:** Ghoul `Ahrl` lumber, Acolyte `Aaha`, and Wisp `Awha` lumber
  and Entangled cargo gold keep their existing owners and are not
  ownership-verified here.
- **Construction:** owned by ORDER-01.16.

## Integration verification limits

The Peon regression uses the existing stock-shaped `opeo` balance row and an
explicit `foot` movement profile. Its minimal UnitAbilities fixture retains Ahar;
the retail `war3.mpq` row is `Ahar,Arep,Asal` (UnitData `movetp=foot`, turnRate0.6,
propWin60). Repair and Pillage are outside this fixture's gold inputs. It uses the
fixture's authored Argl drop-off role rather than certifying Great Hall geometry.

A gold-only saved world omits the unrelated synthetic LTlt tree used by lumber
fixtures; that tree has no DestructableData row and cannot establish valid
restoration. A failing-first error-spy check catches the previous apparent
success despite the loader diagnostic. The spy forwards errors and requires
zero during the Peon restore.

The strict verifier fixes both mandatory patterns (`wc3_harvest012.*` and the
Save217 rejection) and their six-test/251-assertion expectation. A failing-first
contract test rejects empty patterns, a missing save check or empty expected
counts, while replaying the real captures. This does not extend the retail
ownership evidence to trajectories, cadence or the remaining rows above.

The composed recovery checkpoint (Stone390, Harvest012 and repaired Reach031)
passes9,974 native tests/34 reports and2,912 Python evidence tests, actual
`make test TEST_JOBS=16` exit0 under native SDL2. The composed unit/save/spell gate
passes941 tests/135,662 assertions per edition with the real `wc3_spell.*`
pattern. These gates preserve the remaining behavior limits above.
