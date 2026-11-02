> Engine integration: Payoff180 implements the repeating request, callback mutation and release timing in `g_range.c`, with Move-owned prediction/spatial queries, failing-first regressions and cold logical saves. [Engine evidence](../../retail-pathfinding-engine.md#range-listeners-poll-ordered-occupants-at-request-deadlines-payoff180). The prepared research text below remains historical: `DestroyTrigger` was already implemented in Payoff177, and no additional TODO leaves are adopted.

<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/ORDER-05.2/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **ORDER-05.2**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ORDER-05.2/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-ORDER-05.2.json` -> [`ORDER-05.2-expected.json`](../../../../../tools/ghidra/fixtures/research/ORDER-05.2-expected.json) (uncompressed sha256 `3899704e3385f83dfbbaa32134c289e0f874431d7a478b79dfae4a7064e841fa`, 96209 bytes)

# ORDER-05.2 handoff — repeating agent requests and callbacks that schedule/cancel other requests

**Status.** Instruction-verified (A: 6f054370 re-reads repeat/cancel after the receiver callback, 6f053710 rearm,
6f15e500 one-shot stop and release teardown), original-code verified (O: group 05.2 of `verify_ORDER-05.1_request_heap.py`
— 10 named cases, plus the 400 random scripts with repeating wrappers and callbacks that start/stop/release/schedule;
final heap, live count, free-list head and wrapper `+1C/+20` compared in every case, 0 mismatches) and live-verified
(L: `order05_probe.j` scenario `requests`, phases 3–4: range-listener request callbacks that destroy a tied peer,
register a new listener, and destroy their own trigger; observer == control, identical repeat, 0 pop-order violations).
Established: invocation order, catch-up, tie stability, the moment a cancel takes effect for each mutation kind, release
chains inside one drain, and the final heap/live/free-list state.
Not established: engine behaviour (owner); presentation-clock repeaters from a public producer.

Shared material (clock/request layout, producer ABIs, oracle harness, probe, observer, captures, provenance): see
`../ORDER-05.1/HANDOFF.md`. Captures are under `../ORDER-05.1/captures/`.

## Functions (names written via gw.py under ORDER-05.2)

| VA | Name | ABI | Evidence |
|---|---|---|---|
| 6f053710 | SimClock_RearmAgentRequest | thiscall ECX clock, [4] request; RET4 | `Math_Add(clock+40, request+8)` → `+4`; flags `(f & ~10000) | 20000`; heap push 04f990; `+14` serial untouched |
| 6f054370 | SimClock_ExecuteRequest (existing; comment added) | fastcall ECX request | after vt+48 returns: `flags & 1` and `!(flags & 10000)` → 053710, else free to `+38`, `+3C`-- |

Types: `types-ORDER-05.2.json` (1 prototype; layouts in ORDER-05.1). Mapping rows: `mapping-rows-ORDER-05.2.txt`.
Ghidra: `ghidra-writes.jsonl` (1 rename, 2 plate comments). Disassembly: `asm/rearm.txt`.

## Behaviour

Frozen: `expected-ORDER-05.2.json`, sha256 `3899704e3385f83dfbbaa32134c289e0f874431d7a478b79dfae4a7064e841fa` — the 10
named 05.2 oracle results, control markers for ticks 21–45, 275 normalized live rows, observer==control, repeat, ordercheck.

### Live — scenario `requests`, phases 3–4 (listeners RA, RB at phase 1.125+k/8; RC at 1.225+k/8)

| Tick | Callback (inside a range-listener request at clock = its deadline) | Retail decisions |
|---|---|---|
| 28 | RC enter at `40366662` (2.85) | RC's action ends; its request rearms to `403e6662` |
| 28 | RA enter at `4037fffc` (2.875): `DestroyTrigger(RB)`, then `TriggerRegisterUnitInRange(RD)` | RB trigger release ser 74 at `4038019f` (2.875+1e-4). RD: StartTimer from clock time 2.875 → `4077fffc` (1.0, ser 75), then `403ffffc` ser 76 (cancelled), ser 77. RA rearms to `403ffffc` **after** its action |
| 28 | RB's request, same deadline 2.875, ser 45 | still executes in the same drain: enter emitted, **no RB action** (trigger suppressed), rearm to `403ffffc` |
| 28 | releases | ser 74 (RB trigger) → its teardown queues ser 79 at `40380342` → that queues the RB listener release ser 80 at `403804e5`; StopTimer cancels RB's rearmed request (ser 45) |
| 30 | deadline `403ffffc` (3.0) | RA (42) → RB (45, cancelled, silent) → RD (76, cancelled, silent) → RD (77): **RD enter** el 3.005 (0.125 s after the action that registered it) |
| 36 | RC enter at 3.6: `DestroyTrigger(RC)` (its own trigger) | RC rearms to `406e6662` (3.725) first; releases chain at `40666805` (ser 96) → `406669a8` (ser 98) → listener release `40666b4b` (ser 99) → StopTimer cancels the rearmed request |
| 36 | deadline `4067fffc` (3.625) | RA (42) then RD (77): RD always runs directly after RA, its creator |
| 36 (3.725) | `406e6662` | RC's cancelled request pops silently; RC never fires again |

### Oracle (forced-state, labelled; harness in ORDER-05.1)

| Case | Decisions |
|---|---|
| repeating-catch-up | 0.125 repeater behind by 0.5 s runs at 10.125, .25, .375, .5 in one drain; each call sees its own deadline; rearmed to 10.625 |
| repeating-zero-period-clamped | period 0 → 1e-4; 20 calls in a 2 ms drain; deadlines accumulate by truncating adds (`10.000099`, `…198`, …) |
| repeating-callback-cancels-other | 2nd call of A stops B (serial 2): B pops at 10.3 silently and is freed; A continues |
| repeating-callback-schedules-other | A starts B (0.0625) at 10.125 → B due 10.1875 runs in the same drain; B ties A at 10.25 and runs after it (larger serial); A's 3rd call releases C → C at +1e-4 |
| repeating-stops-itself | A's 2nd call stops A: no rearm, freed; B continues |
| repeating-restarts-itself | restart inside its own callback cancels the running request (no rearm) and the new one (0.2) runs at 10.325 |
| repeating-equal-period-tie-rotation | two 0.125 repeaters: A, B, A, B, … (no rotation) |
| live-mirror-callback-releases-tied-peer-and-starts-new | mirrors tick 28: tied peer still runs and rearms; release (+1e-4) tears it down; the rearmed request pops silently; the new repeater ties the creator and runs after it |
| live-mirror-callback-releases-itself | mirrors tick 36: rearm first, release cancels the rearmed request |
| advance-five-ms-steps | 5 ms advances: release at +1e-4 in the first step; 0.125 repeater in steps 25, 50 |

Live-vs-oracle difference: the live release is a chain of three wrappers (trigger → second `a8099c` wrapper →
listener), each 1e-4 later; the oracle wrapper tears itself down in one step. Both cancel the rearmed request before its
next deadline.

### Rules

1. Rearm happens after the callback and only if the request is still repeating and not cancelled; the new deadline is
   the popped deadline + period (software add), so a late repeater catches up one period at a time within one drain.
2. Serial is kept on rearm: repeaters due together keep their creation order forever.
3. Stop (cancel) inside a request's own callback prevents its rearm. A release scheduled inside the callback does not:
   the request rearms and is cancelled when the release runs (1e-4 later, same drain if the drain target allows).
4. A peer due at the same deadline is not affected by a release scheduled before it pops; only StopTimer/teardown
   (cancel bit) prevents a callback.
5. Requests created in a callback use the popped deadline as their time base and drain in the same call when due.

## Engine entry points and failing-regression expectations

- `games/warcraft-3/game/api/api_trigger.h` `DestroyTrigger` (stub), `TriggerRegisterUnitInRange`; `g_events.c`
  `EVENT_UNIT_IN_RANGE`. Expected (live tick 28–36): with RA/RB registered together and RC 0.1 s later, an approach at
  2.80 s delivers RC (2.85) then RA (2.875); RA's action destroys RB and registers RD → RB never runs again, RD's first
  enter is 0.125 s after RA's, and later RA, RD fire together in that order; RC destroying itself in its own action
  fires no further RC events.
- `games/warcraft-3/game/g_timer.c` `TimerFireScalar`/`TimerDrain`: reference for "rearm after callback, cancel
  prevents rearm"; the agent clock needs the same catch-up loop with serial-stable ties.
- `games/warcraft-3/game/g_utils.c` `G_RunDeferredFrees` / `g_main.c` frame step 7: releases scheduled by a callback
  must be able to complete (and chain) inside the same primary advance.

## Reproducer

As ORDER-05.1 (same captures and oracle); `python3 tools/frida/research/order05_expected.py $R ORDER-05.2 --check`
and the oracle with `--expected $R/ORDER-05.2/expected-ORDER-05.2.json` (exit 0).

## Open gaps / proposed IDs

- The identity of the middle `a8099c` wrapper in the release chain (trigger → ? → listener) is not resolved.
- No new IDs beyond ORDER-05.4/05.5 (see ORDER-05.1).
