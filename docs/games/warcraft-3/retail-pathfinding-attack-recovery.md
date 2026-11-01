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
7. Replace the temporary-Invisibility engine's immediate hidden-flag shortcut with a shared Unit contribution/fade owner and per-buff pending/held ownership. The existing `s_wards.c` scalar/primary timer machinery supplies clock arithmetic and save/heap patterns, but its permanent-only flag and suspension inverse cannot be reused unchanged. Add saved deadline/sequence and contribution identities with exact detach/refresh inverses. Keep this in the WC3 game module.
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
or a complete C299 comparison. Ordinary recovery's resolved-alternative validation still needs a C-specific disposition against original4968e0; forced and inactive preservation checks do not establish that branch. The two lifecycle fixtures now execute primary
requests and physical owner updates before retaining their exact final Stand and
queue assertions. See [saved recovery intent](save-load.md).
