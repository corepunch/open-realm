# Attack target-task recovery: TARGET-01.2 F19 / D051

`Attack_RecoverTargetTask` at RVA `0x49d280` releases an Attack target and decides whether to create a point task at the target's captured position. This document separates original instruction evidence, existing public captures, and the remaining verification. It does not close TARGET-01.2 or any of the 25 open whole leaves.

The reference is Warcraft III 1.27.1.7085 `game.dll`, SHA256 `d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`. Addresses here are RVAs; the preferred image base is `0x6f000000`. See the [exact caller checklist](target-01.2-closure.md#f19-0x49d280--attack-target-task-recovery), [Patrol and earlier Attack evidence](retail-pathfinding-engine.md), and [original detach instruction evidence](../../../games/warcraft-3/game/tests/fixtures/retail_attack_detach_293.provenance.json).

## Inputs and decisions

The receiver in ECX is the Attack ability. The two incoming stack words are **forced recovery** and **request a current-target position capture**. The second word is not an incoming target pointer. At `49d338`, a cast of the resolved current target overwrites that stack location. Later decompiler references to `target_event` can therefore describe the cast result rather than the incoming word. Treating the entire variable as one pointer misstates both the caller ABI and Blink exclusion.

The instruction decisions are:

1. At `49d2aa`, return immediately unless Attack flags contain `0x4`. This inactive return does not clear a previously retained `0x40000` bit.
2. Enter position capture if the incoming capture word is nonzero. Otherwise require Attack flag `0x40000` and a clear `0x200` bit in the **source Unit's widget flags at +0x20**. These are different objects and fields. Unit `0x200` is the task-owned acquisition gate (`d0148` sets it at `690660`; `d014c` clears it at `690671`), not immobility or Attack's `0x4000` periodic-scan enable. Do not replace this predicate with the recovery's event name.
3. Resolve current target identity `+0x6c/+0x70`. A missing target prevents capture. A target successfully cast to Unit is excluded while its widget `0x800000` Blink bit is set. A non-Unit widget is not automatically excluded: a null Unit cast skips that Unit-specific Blink check while the original widget remains available.
4. Capture predicted world position through `058900`, and compute range using two original scalar additions: maximum enabled weapon range (`4985c0`) plus target collision radius (`05aa20`), then 50. This is neither the order's original click nor Move's last visible point.
5. Clear `0x40000` at `49d3ea`, release active ownership and target/windup, and apply the alternative-target rule. Forced recovery clears the alternative identity. Ordinary recovery keeps a resolved valid alternative; failed validation clears it. An unresolved weak identity is not a live reference.
6. Stop/recover the Unit, recheck active `0x4`, and run `497190` to pop the eligible task chain. Only a successful pop permits point creation and immediate queue dispatch. D051 is the CALL at `49d48b` (observed return caller `49d490`) to `692120`: Unit, internal task `0xd016c`, captured X/Y, computed range, and final NULL argument.

Forced recovery does **not** independently disable position capture. Likewise, an owner-change cause does not independently clear or set `0x40000` before entry.

## Task-phase boundary

`497190` can pop initial `d0168`, then `d0163` with completion cancellation, then `d016f`; popping `d016f` succeeds. Current Ground task `d0169` is popped but returns false, so this branch neither inserts a recovery point nor directly dispatches the successor. Current swing wait `d016a` is not popped here; `497ba0` separately handles queued wait flag `0x800`. An ordinary shared point task `d016b` is not a reason to discard an unrelated parent order.

These distinctions explain why a forced call alone is insufficient proof of a point task. The active flag, resolved target, capture predicate, and actual task-pop result must all be observed. The separate F18/D050 pre-arrival Ground branch remains outside this document's claimed verification.

## All direct incoming calls

The original binary has 17 direct CALL instructions in 15 containing functions. Numeric names below are deliberately unresolved rather than assigned a guessed public command. The conditional final row passes one exactly when `Unit_HasPendingUserSuccessor(0,0)` returns zero.

| Function RVA | CALL RVA | Recovered function | Forced | Capture word |
|---|---|---|---:|---|
| `0x497da0` | `0x497e06` | Attack_AdjustSuppressionCounters | 0 | 0 |
| `0x49cab0` | `0x49cec6` | Attack_BindWeaponTiming | 0 | 0 |
| `0x49cab0` | `0x49cf0c` | Attack_BindWeaponTiming | 0 | 0 |
| `0x49e6b0` | `0x49e6c2` | Attack_HandleTimerWaitTask | 1 | 0 |
| `0x49a390` | `0x49a3ff` | Attack_OnSwingComplete | 0 | 0 |
| `0x499310` | `0x49935a` | Attack_OnTargetArrival | 0 | 0 |
| `0x49b420` | `0x49b58f` | Attack_OnTargetLost | 0 | 0 |
| `0x49b160` | `0x49b240` | Attack_TargetOwnerChanged | 0 | 0 |
| `0x495180` | `0x4953a4` | Attack_TryBeginTargetSwing | 0 | 0 |
| `0x495180` | `0x495420` | Attack_TryBeginTargetSwing | 0 | 0 |
| `0x494290` | `0x494313` | FUN_6f494290 | 0 | 0 |
| `0x497ba0` | `0x497ba8` | FUN_6f497ba0 | 1 | 0 |
| `0x4980f0` | `0x4980f7` | FUN_6f4980f0 | 1 | 0 |
| `0x58ccd0` | `0x58cd36` | FUN_6f58ccd0 | 1 | 0 |
| `0x5e8000` | `0x5e80f8` | FUN_6f5e8000 | 0 | 0 |
| `0x5fc220` | `0x5fc245` | FUN_6f5fc220 | 1 | 0 |
| `0x628a10` | `0x628a5d` | FUN_6f628a10 | 1 | no pending user successor |

The [repository static export and instruction table](../../../games/warcraft-3/game/tests/fixtures/retail_attack_recovery298_static.json) retain all 17 direct call sites with independently verified original E8 instruction bytes, plus the Unit task-bit and invisibility event producers; this is static investigation provenance, not an accepted live-capture bundle. Existing historical function bodies are also retained under `/GitHub/wc3-analysis/runtime/claude-TARGET-01.2/target-parameter-producers53-decompilation.json`.

## What existing captures already prove

The committed [recovery223 specification](../../../tools/ghidra/fixtures/retail-attack-recovery223-1.27.json) and [literal bundle](../../../tools/ghidra/fixtures/retail-attack-recovery223-1.27.json.gz) contain two identical 27-entry recovery projections plus their unhooked public control:

| Entry state, per observation | Count | Caller return RVA | Interpretation |
|---|---:|---|---|
| forced 0, active `0x4`, `0x40000` set | 7 | `49b594` | Real ordinary target-loss recovery |
| forced 0, active `0x4` clear, `0x40000` clear | 10 | `49cecb` | Inactive entry; recovery body does not execute |
| forced 1, active `0x4` clear, `0x40000` clear | 10 | `497bad` | Inactive cleanup; not active forced recovery proof |

Four ordinary loss scenes create D051 point tasks, with identical literal point words in both repeats and range `0x432b0000` (171). They cover undetected Permanent Invisibility, hidden target, death and removal. The detector control retains the valid target. Thus it would be incorrect to say that the `0x40000` capture branch has never been observed.

[Chase222](../../../tools/ghidra/fixtures/retail-attack-chase222-1.27.json) additionally records active ordinary recovery without `0x40000` at `49935f`, with no D051 point task. [Blink224](../../../tools/ghidra/fixtures/retail-attack-blink224-1.27.json) records active ordinary flagged recovery without D051 during the Blink exclusion. Neither replaces the remaining active forced case.

223 and 224 have no `SetUnitOwner` scene. [Group341](../../../tools/ghidra/fixtures/retail-group341-1.27.json) independently proves that candidate-only owner changes preserve the alternative weak identity, while a promoted current target's owner change delivers `49b160` and clears the alternative. Its observer does not record the exact `49d280` input words or complete recovery polls, so that result must not be relabeled as a paired 223 owner-change trace.

The older observers record public rounded positions and physical-group snapshots, **not complete arrival-evaluation and velocity-commit rows with heading**. Zero such full-poll rows are present. Their missing suffix length cannot be calculated from those snapshots. Existing C save, Stop, queue and subscription tests remain useful bounded engine proofs; they do not manufacture omitted native numeric evidence.

## Active forced capture and remaining engine work

The exact caller table identifies a public-candidate route through `628a10`: dispatcher `642dd0` handles event `d01de` in both `CBuffInvisibility` and `CBuffWindWalk` (original RTTI/vtable linkage). With its own buff flag `0x100`, it increments Attack suppression through `495740`, then calls forced recovery with capture requested when there is no pending user successor.

The recovered notification producer is `679db0` (event write `679de3`, Unit virtual dispatch at `679dfa`). Calls include overlapping invisibility contribution `66971c` and nonzero-count refresh `68b7da`. Setup `63ce40` registers the listener at `63ce6d`, sets its `0x100` state and adds the contribution at `63ce7e`. Positive fade reaches refresh through `d01d4`; zero fade is immediate. The accepted299 batch now observes this route: allied Invisibility on the far chasing source enters 49d280 with active flag4, forced=1 and capture word=1 at tick1265, then creates D051. This is public native evidence, not C replay parity.

This explicit capture word is distinct from a forced call with a retained `0x40000` flag. Sequentially hiding the target and then removing `Aatk` does not establish that combination: ordinary target-loss recovery synchronously clears the bit before the public Hide call returns. An inactive loss can retain it, but ordinary new Attack construction at `49aa18` and automatic publication at `494420` clear it. No direct flag injection may be counted as public reachability.

The bounded original-byte check is complete; the public capture and engine comparison remain open:

- Completed bounded evidence: the original-byte `49d280`/`497190` service-boundary oracle covers active/inactive, forced/capture-word distinctions, source `0x200`, generic widget versus Blink Unit, alternative validity and task-pop outcomes. The portable [recovery298 fixture](../../../games/warcraft-3/game/tests/fixtures/retail_attack_recovery298.json) now contains 81 producer cases, seven actual task-pop cases and nine negative controls; [its provenance](../../../games/warcraft-3/game/tests/fixtures/retail_attack_recovery298.provenance.json) identifies the synthetic services. The original `001cd0` initializes RuntimeFifty after poisoning, and original scalar addition and two-slot maximum selection execute. Equal/adjacent range words around 333 exercise both slot orders. This is not live public or scheduler parity. The adjusted-range branch is excluded: its setter `49c4d0` has no recovered direct xrefs, constructor state is invalid, and restore capability alone does not establish public reachability. Release/stop stubs deliberately retain active `0x4`; they do not prove callback reentrancy.
- The enhanced299 native batch is accepted for its observed ordinary loss, owner change, active forced detach, automatic acquisition loss and explicitly requested forced Invisibility capture. It does not establish active retained-flag recovery in the paused scene. Implement and compare the corresponding C paths; preserve the older223/224 controls.
- Record complete global ordered arrival/velocity polls, raw clocks, heading, actual point/range arguments, identities, task phases and public callbacks through the declared end. Require two frozen-source observations and a fresh unhooked control, then compare the entire normal and saved C suffix. Keep literal arithmetic and ordering; pointer-to-role mapping does not authorize numeric normalization.

Reusable existing tools are `tools/frida/research/target223_{probe.j,make_map.py,capture.py,observer.js}`, `tools/ghidra/research/Target223Evidence.java`, `tools/ghidra/verify_wc3_pathing_attack_recovery223.py`, and `tools/ghidra/research/attack_recovery298_oracle.py`. New observers and probes belong in a separate frozen capture directory. Existing 223/224 bytes and expectations remain unchanged.

## Accepted public batch299

The [portable specification](../../../tools/ghidra/fixtures/retail-attack-recovery299-1.27.json) and [literal compressed bundle](../../../tools/ghidra/fixtures/retail-attack-recovery299-1.27.json.gz) preserve two complete observations, a fresh unhooked control, both preload files per run, and the exact frozen source bytes. All three public streams contain the same 2,066 markers. Both ordered native streams contain exactly 9,058 records, including 2,671 arrival and 2,671 velocity records. Only process pointers are mapped between observations; clocks, headings, positions, ranges, identities and order stay literal.

| Scene | Observed active recovery input | D051 result |
| --- | --- | --- |
| 0: explicit Attack, target Hide | flags `0xc0195`, source widget `0x1446`, forced0, capture0 | Creates point at tick60, range word `0x432b0000` (171) |
| 1: target becomes source player's unit | flags `0x80195`, source widget `0x1446`, forced0, capture0 | No point |
| 2: active `Aatk` removal | flags `0x8019d`, source widget `0x1446`, forced1, capture0 | No point |
| 3: automatic acquisition, target Hide | flags `0x20c4115`, source widget `0x1646`, forced0, capture0 | No point: source Unit200 is set |
| 4: allied Invisibility on chasing source | flags `0x80195`, source widget `0x1446`, forced1, capture1 | Creates point at tick1265, range171 |
| 5: pause, target Hide, resume | No active recovery entry observed | Not proof of retained40000 active recovery |

The native task-lifetime export is [retail_attack_recovery299_task_phases.json](../../../games/warcraft-3/game/tests/fixtures/retail_attack_recovery299_task_phases.json). Its 11 original functions and 1,505 instructions show that d016f survives arrival, facing, windup and cooldown. `497190` pops d016f at `49720c`; recovery clears active4 at `49d468`. Finishing d016a is distinct and cannot create another recovery point.

The frozen observer has one explicit schema defect: the642dd0 payload uses `event` for four event words, replacing that row's string discriminator. The checker accepts only that exact schema, requires d01de and the Unit-pointer relationship, and preserves all four words. Footer event counts still name the event correctly. No numerical normalization or row filtering repairs it. The first incomplete v1 attempt is rejected and excluded; its provenance is retained separately.

Reproduce the complete native evidence check without Warcraft, Frida or external Python packages:

```sh
python tools/ghidra/research/attack_recovery299_evidence.py
python -m unittest discover -s tests -p test_wc3_pathfinding_attack_recovery299.py
```

The checker and ten mutation/rejection tests pass. Complete C normal/saved299 replay and the public temporary-Invisibility recovery frontier remain unfinished. This accepted native batch does not close F19/D051.

## Remaining public Invisibility integration: exact owner and data flow

The original [fade export](../../../games/warcraft-3/game/tests/fixtures/retail_attack_recovery299_invisibility_fade.json) establishes the next C work. These findings are static evidence accompanying native299; the following engine work is not complete.

1. `CAbilityInvisibility` execute `62f050` obtains the current-level scalar through `414b50`. Loader `6a74b0` fills level-row offset20 from **DataA**. Use the configured ability/level value; native299's half-second delay is not a global constant or the animation cast point.
2. New Binv setup calls virtual354 → `63ce40`, marks buff100 pending, registers d01de and adds an invisibility contribution. Existing Binv refresh calls virtual350 → `531a80`, which extends duration without creating or restarting that contribution.
3. First positive contribution `6696e0` installs reciprocal fade slope on Unit118, modifier128 and listener12c. Scalar upward threshold d01d4 reaches `68b780` through core dispatch `6906de`. Zero fade publishes synchronously. An overlapping contribution with count other than one emits d01de immediately; one independent fade timer per buff would give the wrong result.
4. `68b780` cancels the scalar owner, publishes the nonzero-count invisible bit, delivers TargetLost, then emits d01de through `679db0`. Preserve this order.
5. Buff dispatcher `642dd0` calls `628a10` only while buff100 is pending. It increments Attack suppression through `495740`, disables the scan owner without deleting the Attack ability, then calls `49d280` with forced1 and capture requested only when no pending user successor exists. It then clears buff100.
6. Detach `6314d0` removes the invisibility contribution and unregisters d01de. It decrements Attack suppression through `495750` only after the buff has activated and cleared100. Removing a pending fade must not decrement a suppression contribution it never held.
7. The C Apiv/Binv implementation now replaces the temporary-Invisibility immediate hidden-flag shortcut with a shared contribution/fade owner and per-buff pending/held ownership, saved in format234. The existing `s_wards.c` scalar/primary timer machinery supplies the clock arithmetic and heap. This does not establish shared contribution ownership for `AIvi` Item Invisibility, `AOwk` Wind Walk, `Agho` Ghost, `Abur` Burrow or `Ahid` Shadow Meld, or their Pause/Sleep compositions; those concrete overlap dispositions remain required.
8. Drive the real configured Aivs cast in C, with a nonstock DataA value as well as the captured stock profile. Compare all native299 public callbacks and complete arrival/velocity rows through uninterrupted and saved replay. Preserve the same-target refresh, overlap, pending removal, active removal and queued-successor distinctions above; internal direct recovery-helper tests alone do not satisfy this public producer.

## Common C recovery and Save233

The game module now saves loss intent independently from acquisition permission.
Owner change consumes only existing intent; it does not synthesize target loss.
Automatic loss with acquisition permitted does not create a captured point.
Logical d016f ownership during reaction, facing, windup and cooldown no longer
requires an extant physical movement group. A forced capture request is separate
from loss history. Inactive forced entry preserves the alternative and pending
intent; committed AttackOnce d016a preserves its independent swing/FIFO and does
not insert a captured point.

The failing-first owner-change regression produced six assertion failures before
the fix. Six new recovery regressions and 250 Save233 tests pass in both editions.
The broader [validation record](retail-pathfinding-attack-recovery-validation.json)
records 1,272 tests and 316,668 assertions as **composite** validation: 24 retained
passing processes plus the two corrected complete lifecycle processes, with only
the lifecycle test source changed between checkpoints. It is not a full-suite run
or a complete C299 comparison. Ordinary recovery's resolved-alternative validation is implemented as described below; this does not establish the adjusted retained-target branch or full C299 parity. The two lifecycle fixtures now execute primary
requests and physical owner updates before retaining their exact final Stand and
queue assertions. See [saved recovery intent](save-load.md).

## Ordinary alternative validation and item life

Active ordinary recovery now validates a resolved alternative before retaining it.
Forced recovery clears it. Inactive recovery and unresolved weak identities keep
their existing words. Public AttackMove acquisition and an owner-change event
establish the ranked alternative used by the fresh and saved regression cases.
Those cases cover retaining a valid target, hidden/dead/invulnerable rejection,
forced clearing, and a freed/reused entity slot.

The predicate selects the first compatible enabled weapon, applies its complete
target mask without an alliance restriction, checks Unit visibility, and uses
actual flight for the minimum-range exemption. Expansion and
`Misc.MagicImmunesResistDamage` jointly control the special magic/ethereal damage
restrictions. Unit and Item hiding reject the candidate; the generic
Destructable branch does not borrow that rejection. The minimum-range comparison
preserves the original scalar near-equality threshold and signed authored input.
The [original predicate evidence](../../../games/warcraft-3/game/tests/fixtures/retail_attack_recovery299_alternative_validation.json)
contains 1,430 verified body, instruction and data guards. These are static
evidence, not a live original alternative-ranking capture.

The generic Item case exposed a constructor dependency: item life had never been
initialized from `ItemData.HP`. `SP_SpawnItem` now initializes current and maximum
life from the authored scalar. Negative/NaN current life clamps to zero; the
maximum retains its authored value. Fractional SLK values and integer map
overrides use the existing typed scalar conversion. Missing HP remains zero;
there is no invented default. The [item-life evidence](../../../games/warcraft-3/game/tests/fixtures/retail_attack_recovery306_item_life.json)
retains eleven original function bodies. Existing saved health fields already
carry both scalars, and load-time data rebind does not run the constructor, so
this change does not require a new save version.

The native predicate's adjusted retained-target state at Attack offsets
`2bc/2c0` remains a separate reachability limit. The current target is not used
as a substitute. The five remaining invisibility-consumer overlap dispositions
and the complete normal/saved C299 replay remain unfinished. B01 and
TARGET-01.2 remain open.

## Configured temporary Invisibility and Save234

The public `invisibility` order now resolves to Aivs; previously its name/ID was
registered without an ability owner and JASS target orders fell through.
Configured DataA (RoC Data1) starts the first contribution's primary-clock fade.
New Binv owners retain pending state until notification. Refresh extends the
existing status duration without replacing its incarnation or restarting the
fade. An additional contribution emits the native immediate notification while
retaining the first scalar deadline.

At the scalar threshold the game publishes invisibility, delivers TargetLost,
then activates pending Binv owners. Activation holds the separate Attack220
suppression count, disables scanning and requests forced recovery. The existing
pending-user-order query determines whether recovery captures a point. Pending
removal acquires no inverse suppression; active removal releases its own held
contribution. An Attack incarnation token prevents an old buff from decrementing
a detached and replaced Attack owner.

Apiv and Binv retain visibility and world presence when either one leaves first.
The temporary render flag follows the temporary contribution rather than being
left on a permanent-only unit. Shared publication has a save/load barrier, and
notification tracks in-flight status incarnations so nested delivery cannot
acquire the same buff twice. Save234 validates the stored counts, status classes
and active clock/sequence state before rebuilding queues.

The bounded producer tests use public cast/order admission, the actual primary
scheduler, two positive fade values, halfway save/load, queued Move bypass and
both published overlap orders. The saved test detaches its observation hook
during ReadGame's old-world disposal, then requires the restored pending status,
unchanged deadline/sequence and exactly one resumed publication. These checks
do not replace the complete six-scene native299 public and motion comparison.

## Initial Attack admission and damage ownership: checkpoint309

Ordinary public target Attack now constructs the reaction and initial physical
request even when reissuing the same target or while weapon cooldown is active.
Automatic replacement keeps its separate immediate path. The initial task
captures the target-compatible weapon range before changing the movement owner;
radial or widget-perimeter acceptance selects `FLT_MAX`. Save235 retains that
captured range and rejects invalid active values.

Original `497e20` releases cooldown only when the discarded damage wait still
has positive time remaining. Completed damage retains cooldown and independent
swing completion. Pending-damage cleanup also runs before a windup leaves for
an ordinary Move order; discarding the damage timer first loses this inverse. A short presentation animation cannot cancel the primary
damage wait. At the damage deadline, `49a5b0` calls `4946b0`, which arms independent
swing completion before checking buffered predicted range or widget perimeter.
A geometry miss skips delivery and keeps swing recovery. See the [original
byte-checked bodies](../../../games/warcraft-3/game/tests/fixtures/retail_attack_initial309_static.json)
and [damage rules](attack-damage.md).

The focused composite passes 1,950 tests and 736,268 assertions across both
editions. The complete uninterrupted C299 diagnostic still fails: its first 148
ordered movement records match, but the first public difference is Target life
at tick 15 (C 409.050, retail 408.443), and the first movement difference is Source
pose near 2.28 seconds. The run produced 2,059 public records versus 2,066 and 5,492
movement records versus 5,342. This does not establish full or saved replay
parity and does not close B01 or a whole leaf.

Reproduce the generated header with
`python tools/ghidra/research/attack_recovery299_c_replay.py --out /tmp/native299-header`.
Run the explicit diagnostic with
`python tools/ghidra/research/attack_recovery299_c_run.py --binary build/bin/openwarcraft3-tests --map build/tests/attack299/AttackRecovery299.w3m --data <retail-install> --log <fresh-log>`.
The launcher requires the accepted map SHA and one executed, non-skipped test.
Its temporary retail overlay stays outside the recursively scanned fixture
directory. Original evidence and expected rows remain unchanged.

The next two concrete fixes are fractional damage mitigation and independent
cooldown expiry dispatch. Integer mitigation currently turns raw 13 against
armor 2 with coefficient 0.06 into 11 instead of 11.607142857; with 0.050 regeneration,
that exactly explains the first public health difference. At the first movement
difference, retail has replaced its existing raw 90 chase at 2.249991894, resetting
velocity and refreshing the target position. The original cooldown chain is
`d01b0 ->49a4f0 ->499310(visibility=0) ->495180 ->49a240`. The callback attribution
uses original code plus chronology; native299 did not directly trace d01b0.
Neither fix is included in this checkpoint.

All five fresh parity gates pass: Captain473, Group012, Waygate377, Item Drop,
and Support Goals. Support Goals executes the same 17 test families per edition
in 34 isolated processes, with four workers; each process requires a fresh,
positive, non-skipped JUnit result agreeing with stdout. The 115 focused Python
evidence tests pass. The refreshed corpus checks 614 entries and 3,322 guarded
files. These checks do not replace the still-failing complete C299 comparison.

## Fractional physical damage: checkpoint311

The first-hit health marker now matches retail's recorded 408.443 instead of
409.050. Physical mitigation, bonuses, shields, Spirit Link, splash and bounce
retain fractions through health and damage events. Integer event IDs keep their
original 32-bit representation. Save236 and JASS snapshot 11 reject the older
integer-damage interpretation; queue/context copies retain the payload bits.
The separate integer spell-damage API is outside this precision claim.

Verification passes 4,364 test executions and 18,327,844 assertions across both
editions, all five fresh parity gates, 115 Python evidence tests and the 614-entry
corpus check. Three prior integer expectations were replaced with their actual
authored arithmetic: Defend's real factor, Moon Glaive's 50×0.5×0.5 second bounce,
and Spirit Link's 25/3 shares with total-damage conservation. The broad native
batch exposed five old reaction fixtures; their ten focused edition executions
now pass. A complete broad rerun has not passed yet.

The complete C299 comparison still fails. Its first public mismatch moved from
index 30 (health) to 37 (Source pose at tick 25). Its first movement mismatch is
still 148, near 2.28 seconds. Next implement the independent `d01b0` callback:
`49a4f0` returns during reaction; its enabled Ground branch starts a Ground swing;
otherwise a retained target reaches `499310` with visibility argument 0. That
path can replace a chase already in motion. `49d130` replaces cooldown only when
the requested duration is strictly longer than remaining time, and arms damage
only inside that replacement branch. Use the existing primary float-clock
queue, preserve independent swing, and save/validate the new timer owner.
Neither normal/saved suffix parity nor B01 closure is established.


## Independent Attack cooldown: checkpoint312

The cooldown now owns a saved primary float-clock request, independent of
animation, damage, swing completion and physical chase. Its expiry can replace
an existing chase, resetting velocity and refreshing the target position.
Reaction blocks that dispatch. An enabled artillery Ground task starts its
own swing; a retained target uses detection-only validation, followed by a
separate position-visibility query. A hidden position starts a mobile chase;
failed detection ends the task.

The producer grants the speed-cap exemption before comparing durations. Only a
strictly longer request replaces the remaining cooldown and arms damage. Damage
is capped by the actual requested duration minus .01, with the native .0001
minimum. Releasing an uncommitted hit cancels cooldown; post-hit target release
preserves it. Detach/removal cancels it. Save237 retains the timer even without a
target or Attack think callback, validates active clocks/sequences, and rebuilds
the queue. Integer millisecond fields are derived compatibility caches.

The unchanged complete C299 diagnostic now matches the first 878 ordered motion
rows, up from 148. Its first public mismatch is index114: scene0 Target facing at
tick90. Public totals remain2059 versus2066, with1771 mismatches; motion totals
are5496 versus5342, with4464 mismatches. Full replay parity and B01 remain open.
The next mismatch begins after the hidden Target finishes its Move: C starts
another Target physical request while retail continues the Source recovery.
This is a next-step observation, not a proved cause or completed correction.

[Sixteen original function bodies](../../../games/warcraft-3/game/tests/fixtures/retail_attack_cooldown312_static.json)
are guarded against the private DLL bytes, including49a4f0,49d130,499310,495180,
reaction ownership, scalar-clock scheduling and cancellation. Original native299
public/motion rows and expected values remain unchanged.

Verification: 4,414 focused test executions and 18,334,284 assertions
pass across both editions in a 40-process composite. The sole change after the
initial 38 green scopes is the corrected spell fixture; its complete family
passes again in both editions. All five gates pass on the final build.
The complete C299 diagnostic still fails as recorded above; no whole leaf closes.
The 115 Python evidence checks pass. The corpus check passes all 614 entries
and 3,323 guarded files.


## Hidden source acquisition: checkpoint313

A unit hidden through `ShowUnit(false)` can finish an already admitted Move,
but its ordinary idle scan must not acquire a new target. The scan still rearms;
`ShowUnit(true)` permits acquisition again. Gameplay invisibility remains a
separate state: the configured Ghost regression stays world-present and acquires.

This removes the hidden Target's extra chase after scene0 Move completion. The
unchanged full C299 comparison now matches the first 2,346 ordered motion rows,
up from 878. Its first motion difference is scene3's initial Source request.
The first public difference moves from 114 to 398: owner transfer in scene1 is
missing a Target `undefend` order852056. Actual totals remain 2,059 public rows
versus 2,066 expected, with 1,657 mismatches; motion totals are 5,356 versus 5,342,
with 2,992 mismatches. Full normal/saved replay parity remains open.

[Three original bodies](../../../games/warcraft-3/game/tests/fixtures/retail_attack_hidden313_static.json)
are byte-checked:493d20 source admission,493ff0 rearm-before-scan, and49c4d0's
separate adjusted-owner setter. The native hidden-source exception resolves
adjusted identity2bc/2c0, not current target6c/70 or alternative78/7c. C still has
no mapped adjusted owner. The constructor invalidates that identity; no setter
caller is recovered, while native loader restoration remains an explicit B01
gap. This patch implements the ordinary invalid-adjusted-identity case and does
not claim the exception or complete Ghost overlap behavior.

The two public scheduler regressions pass in both editions. The surrounding
12 focused processes pass 1,006 test executions and 549,160 assertions. Original
native299 public/motion rows remain unchanged. Save237 is unchanged.

The full native batch passes all eight shards: 9,414 test executions and
60,729,322 assertions, with no failures, errors or skips. The command is
`make -j8 BUILD=debug TEST_JOBS=8 TEST_JUNIT_DIR=build/tests/full313-native/junit test-wc3-engine`.
All five parity gates also pass on this build. The opt-in complete C299 replay
still fails at the differences above; the native suite does not establish its
full parity. No whole leaf closes.
The 76 refreshed corpus/item-drop Python checks pass; the corpus validates
614 entries and 3,324 guarded files.
