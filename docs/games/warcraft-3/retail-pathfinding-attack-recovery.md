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

## Active forced-capture candidate and remaining work

The exact caller table identifies a public-candidate route through `628a10`: dispatcher `642dd0` handles event `d01de` in both `CBuffInvisibility` and `CBuffWindWalk` (original RTTI/vtable linkage). With its own buff flag `0x100`, it increments Attack suppression through `495740`, then calls forced recovery with capture requested when there is no pending user successor.

The recovered notification producer is `679db0` (event write `679de3`, Unit virtual dispatch at `679dfa`). Calls include overlapping invisibility contribution `66971c` and nonzero-count refresh `68b7da`. Setup `63ce40` registers the listener at `63ce6d`, sets its `0x100` state and adds the contribution at `63ce7e`. Positive fade reaches refresh through `d01d4`; zero fade is immediate. An allied Invisibility cast on a far, still-chasing attacker with no queued successor is therefore a statically supported candidate. It is not yet an accepted public trace.

This explicit capture word is distinct from a forced call with a retained `0x40000` flag. Sequentially hiding the target and then removing `Aatk` does not establish that combination: ordinary target-loss recovery synchronously clears the bit before the public Hide call returns. An inactive loss can retain it, but ordinary new Attack construction at `49aa18` and automatic publication at `494420` clear it. No direct flag injection may be counted as public reachability.

The bounded original-byte check is complete; the public capture and engine comparison remain open:

- Completed bounded evidence: the original-byte `49d280`/`497190` service-boundary oracle covers active/inactive, forced/capture-word distinctions, source `0x200`, generic widget versus Blink Unit, alternative validity and task-pop outcomes. The portable [recovery298 fixture](../../../games/warcraft-3/game/tests/fixtures/retail_attack_recovery298.json) now contains 81 producer cases, seven actual task-pop cases and nine negative controls; [its provenance](../../../games/warcraft-3/game/tests/fixtures/retail_attack_recovery298.provenance.json) identifies the synthetic services. The original `001cd0` initializes RuntimeFifty after poisoning, and original scalar addition and two-slot maximum selection execute. Equal/adjacent range words around 333 exercise both slot orders. This is not live public or scheduler parity. The adjusted-range branch is excluded: its setter `49c4d0` has no recovered direct xrefs, constructor state is invalid, and restore capability alone does not establish public reachability. Release/stop stubs deliberately retain active `0x4`; they do not prove callback reentrancy.
- Use one enhanced 223-style public batch for ordinary target loss, owner change, and a genuine active forced producer. Direct public `Aatk` removal establishes the forced-detach route; the Invisibility candidate can establish explicitly requested forced capture only after its actual entry state is observed. Preserve existing controls instead of repeating their old claims.
- Record complete global ordered arrival/velocity polls, raw clocks, heading, actual point/range arguments, identities, task phases and public callbacks through the declared end. Require two frozen-source observations and a fresh unhooked control, then compare the entire normal and saved C suffix. Keep literal arithmetic and ordering; pointer-to-role mapping does not authorize numeric normalization.

Reusable existing tools are `tools/frida/research/target223_{probe.j,make_map.py,capture.py,observer.js}`, `tools/ghidra/research/Target223Evidence.java`, `tools/ghidra/verify_wc3_pathing_attack_recovery223.py`, and `tools/ghidra/research/attack_recovery298_oracle.py`. New observers and probes belong in a separate frozen capture directory. Existing 223/224 bytes and expectations remain unchanged.

## Prepared public probe: not yet accepted evidence

The separate `tools/frida/research/attack_recovery299_probe.j`, `attack_recovery299_observer.js`, `attack_recovery299_make_map.py`, `attack_recovery299_capture.py` and `attack_recovery299_plan.json` are prepared but unbuilt and unlaunched. Their six scenes are explicit Attack/target Hide, current-target owner change, active Aatk removal, automatic acquisition/target Hide, allied Invisibility on a far chasing attacker, and paused target loss/resume. Each scene must pass its recorded preconditions. Two observed runs, a fresh unhooked control and complete normal/saved C comparisons remain required. The prepared files are not a retail result.
