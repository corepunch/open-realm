<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/ORDER-05.3/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **ORDER-05.3**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ORDER-05.3/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-ORDER-05.3.json` -> [`ORDER-05.3-expected.json`](../../../../../tools/ghidra/fixtures/research/ORDER-05.3-expected.json) (uncompressed sha256 `e1f4d270b2d02066a97463db04a90249c9954adc44e926c83f6252619ee89545`, 128298 bytes)

# ORDER-05.3 handoff — request clock wrap, pause, switch and save/load restore with pending deadlines

**Status.** Instruction-verified (A: 6f054190 advance/wrap, 6f0521f0 rebase, 6f15de50/6f15dca0 request save/load,
6f15dec0/6f15dd30 wrapper state, 6f15c490 clock restore, 6f053110 flush/teardown at load start, 6f15c650 clock
switch by identity sign), original-code verified (O: group 05.3 of `verify_ORDER-05.1_request_heap.py`, 10 named
cases, 0 mismatches; the model's wrap uses the original Math_Add/Math_Subtract words) and live-verified (L: two public
probes — `wrap`: a 340 s game across the 300 s primary/presentation wraps; `saveload`: a UI save with two pending range
listeners and a pending release, then a UI load — each with an observer-free control whose Preload output is
byte-identical to the observer run's).
Established: which requests fire in the span drain, how pending deadlines are rebased (exact words), the remainder,
that the other clock is untouched, that pause skips everything, and that save/load restores clock time/epoch/serial and
absolute deadlines verbatim (no rebase, no shift): post-load callbacks fire at the identical clock words.
Not established: pause-bit producer (never set live); save/load after a wrap (epoch ≥ 1); public presentation-clock
requests; engine behaviour (owner).

Shared material (layouts, producers, harness, probe, observer, maps): `../ORDER-05.1/HANDOFF.md`; maps in
`../ORDER-05.1/maps/`.

## Functions (ABIs from assembly; names written via gw.py under ORDER-05.3)

| VA | Name | ABI | Evidence |
|---|---|---|---|
| 6f054190 | SimClock_AdvanceRequests (existing) | fastcall ECX &increment, EDX clock; RET; EAX 1 | bit0 → return; `t=Math_Add`; `span>t` → time=t, drain; else rem=`Math_Subtract(t,span)`, `|rem-0|<3556bf95` → 0, time=span, drain, rebase 0521f0, time=rem, drain |
| 6f04f7e0 | SimClock_AdvanceRequestsBy | thiscall ECX clock, [4] &increment; RET4 | EDX=ECX, ECX=[4], call 054190 |
| 6f0521f0 | SimClock_RebaseRequestDeadlines (existing) | thiscall ECX clock; RET | every slot 1..count-1: `deadline = Math_Subtract(deadline, span)`; epoch++ once |
| 6f15de50 | AgentRequest_Save | fastcall ECX stream, EDX request (0 → writes 0) | writes 1, `+4`, `+8`, `+14`, `+1C`, repeat bit |
| 6f15dca0 | AgentRequest_Load | fastcall ECX stream, EDX clock, [4] receiver; RET4 | re-queues via 15e310 with the saved deadline pointer and serial; repeat bit restored |
| 6f15dec0 / 6f15dd30 | AgentWrapper_SaveState / AgentWrapper_LoadState | thiscall ECX wrapper, [4] stream; RET4 | `+44/+48/+4C`, lists `+24/+34`, then `+1C`, `+20`; load uses the identity clock (15c650) |
| 6f15c490 | PathOwner_LoadHeaderAndClocks | thiscall ECX owner, [4] stream, [8] companion; RET8 | header, then `+54..+64` (primary time/epoch/span/flags/serial), `+A8..+B8` (presentation), `+FC..`, `+150..` read verbatim |
| 6f053110 | Simulation_FlushAndTeardown | thiscall ECX config, [4] mode, [8] progress; RET8 | per owner clock: clear bit0, advance 0.2 (6fcd53d4) and drain; timer clocks drained/destroyed; owner destroyed, 6fd53a48=0. First call of game load 6f04ced0 (04cef7) |
| 6f15b000 / 6f15b040 | (unnamed; plate comment) | — | clock-state loaders with no code or pointer references in this build |

Types: `types-ORDER-05.3.json` (9 prototypes). Mapping rows: `mapping-rows-ORDER-05.3.txt` (7 rows). Ghidra:
`ghidra-writes.jsonl` (7 renames, 9 plate comments). Disassembly: `asm/advance.txt`, `asm/saveload.txt`,
`asm/053110-flush-teardown.txt`.

## Behaviour

Frozen: `expected-ORDER-05.3.json`, sha256 `e1f4d270b2d02066a97463db04a90249c9954adc44e926c83f6252619ee89545` — 10 named
oracle results; wrap: rebase heaps (words), near-span advances, enter markers with clock words, rows around the primary
wrap, observer==control; save/load: UI actions, request-save rows, 12 wrapper-load rows, clock at load, pre/post
comparisons, control equality.

### Live — wrap (`RS-ORDER-05-wrap`, RA/RB at 1.125+k/8, approach every 10 s)

| Clock | Observation |
|---|---|
| presentation | frame increments (`3d3c6a80`, `3d5d2f1b` …); at `4395fee1`+0.054 → wrap with an **empty** heap: epoch 0→1, time `3d394000`. Independent of primary |
| primary | fixed 5 ms steps (`3ba3d70a`); time reaches `4395fffe` (299.99994); next step: span drain fires RA (38), RB (41) due `4395ffff`, each rearms to `43960fff` |
| primary rebase | 9 queued requests: `43960fff`→`3dfff000` (RA, RB), `43961999`→`3e4cc800`, `43962666`→`3e999800`, `43980000`→`40800000`, `43e10000`→`43160000` …; epoch 0→1; time = `3ba10000` |
| after | RA/RB run at `3dfff000` (0.12497), rearm to `3e7ff800`, `3ebffc00` … (from the rebased words); enter timing relative to the approach is unchanged (el 295.001 before, 305.005, 315.003, 325.002, 335.004 after) |
| flags | primary `00001000`, presentation `00000000` for the whole run; bit0 never set |

Pop-order check: 7880 pops, 0 violations (including across the rebase).

### Live — UI save/load (`RS-ORDER-05-saveload`)

| Step | Observation |
|---|---|
| UI save (F10, s, `RSO5SAVE`, Return; tick 300) | `AgentRequest_Save` for RA/RB listeners: deadline `41f0ffff`, period `3e000000`, serials 38/41, flags `20001`; clock `41f05e1f`, serial 132 |
| UI load (~40 s later; F10, l, first item, Load) | `Simulation_FlushAndTeardown` called 7 times (modes 1/0; the first flushes the old owner at time 64.98 and resets it), then game load 6f04ced0 (its own flush first); the owner's clocks are at 0 on entry and are overwritten by the load |
| after load | primary clock `41f05e1f`, epoch 0, serial 132, 12 live requests; wrapper loads: RA/RB `41f0ffff` ser 38/41; 6 `a91064` timers `42000000` (period 4) ser 126–131; 3 `a90f48` (`43160000`, `43164ccc`, period 150) ser 20/26/31; one `a8099c` **pending release** `41f05e53` ser 132 (created by the save itself) |
| resume | markers continue at tick 310 (`41f7f765`, serial 132); the 58 markers to tick 640 equal the pre-load continuation in text, clock word and serial; the 465 in-window request rows (queue/execute/rearm/enter/release/start/stop with deadline words, serials, flags) are identical; RA/RB fire at `420bffff` (35.0) both times |

Controls: wrap-control-1 and saveload-control-1 Preload text is byte-identical to the observer runs (247 markers for
save/load, including the replayed range after load).

### Oracle (forced-state, labelled)

| Case | Decisions |
|---|---|
| wrap-rebases-pending-deadlines | 5 ms steps from 299.875: repeater due 300.0 fires in the span drain, rearms to 300.125, rebased to 0.125; others → 0.175, 0.875; epoch 1 |
| wrap-callback-schedules-during-span-drain | requests created at 300.0 (deadlines 300.05, 300.0001, 300.125) are **not** fired in the span drain; all are rebased; the 1e-4 one fires in the post-rebase drain |
| wrap-large-increment-catch-up | +0.5 across the span: fire ≤300, rebase, then catch up to 0.375 + … in the second drain |
| wrap-exact-span-remainder-zero | time lands exactly on 300 → remainder 0, epoch 1, time 0 |
| wrap-other-clock-not-rebased | primary rebase leaves the presentation clock's deadline and epoch untouched |
| paused-clock-does-not-drain | flags bit0: 40 advances change nothing (time, heap) |
| presentation-clock-independent | 0.04 presentation steps and 5 ms primary steps drain only their own heaps |
| clock-restored-backwards-pending-absolute | forced time 5.0 with deadlines at 10.x: nothing fires until the clock passes them; then catch-up in deadline order |
| clock-restored-forward-catch-up | forced time 11.0: all overdue requests fire once per period in order at their own deadlines |
| identity-sign-switch-moves-new-requests | after flipping identity bit31, a restart cancels the primary request (still popped there silently) and queues on presentation; the release also goes to presentation |

### Rules

1. Deadlines are absolute in the clock's current epoch; nothing rebases them except the wrap, which subtracts the
   span from every queued request after firing everything due by the span.
2. Requests created or rearmed during the span drain land above the span and are rebased with the rest.
3. Each owner clock wraps, pauses and drains independently; a wrapper's clock is chosen per request by its identity sign.
4. Save stores absolute deadlines and serials of wrapper `+1C`/`+20` requests; load restores the clock's
   time/epoch/span/flags/serial and re-queues the same words, so post-load callbacks fire at the same clock times
   with the same tie order. Cancelled requests are not saved.
5. Load first flushes the old simulation: due-within-0.2 s requests of the old owner run, the rest are discarded with it.

## Engine entry points and failing-regression expectations

- `games/warcraft-3/game/g_save.c` (timer/event fields) and `g_local.h` `wc3Clock_t`: a ported request clock must
  save/restore time, epoch, serial and absolute deadlines; expected: a range listener due at `41f0ffff` before a save
  fires at the same clock word after load, with the same serial order and without a missed or duplicated enter.
- `games/warcraft-3/game/g_timer.c` `TimerDrain`: a per-clock wrap step must fire everything ≤ span before rebasing
  (requests created in that drain included) and continue with the remainder.
- `games/warcraft-3/game/g_main.c` frame order (deferred frees step 7) and `g_utils.c` `G_RunDeferredFrees`: a release
  pending at save time (live: the save's own `a8099c` release) must survive load and complete at the next primary advance.

## Reproducer (worktree root)

```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
O5_EXTRA=$'--continue-at\n70\n--continue-every\n5' tools/frida/research/order05_runs.sh <out> wrap 540 wrap-observe-N:observe wrap-control-N:control
O5_EXTRA=$'--continue-at\n70\n--continue-every\n0\n--delete-save\nRSO5\n--ui-action\n74:key:space\n--ui-action\n78:key:space\n--ui-action\n82:key:space\n--ui-action\n86:key:space\n--ui-action\n100:key:F10\n--ui-action\n101:key:s\n--ui-action\n102:type:RSO5SAVE\n--ui-action\n104.5:key:Return\n--ui-action\n140:key:F10\n--ui-action\n141:key:l\n--ui-action\n142.5:click:366,204\n--ui-action\n143.5:click:353,414\n--ui-action\n180:key:space\n--ui-action\n185:key:space\n--ui-action\n190:key:space' \
  tools/frida/research/order05_runs.sh <out> saveload 330 saveload-observe-N:observe saveload-control-N:control
python3 tools/frida/research/order05_analyze.py clocks <out>/saveload-observe-N.jsonl
python3 tools/frida/research/order05_expected.py $R ORDER-05.3 --check          # "unchanged"
```
(the frozen runs also took screenshots at 92/106/142/145/175/195/300 s; first list item = newest save.)

## Provenance

game.dll `d51e5680…d8236`. Maps and scripts as ORDER-05.1. Saves (`saves/`): RSO5SAVE observe-1 `c543a29f…1fe5`,
control-1 `fc49d25f…90b5`.

## Captures (all preserved, `captures/`)

| Capture | sha256 | Status |
|---|---|---|
| wrap-observe-1 | `5c849d57…ac33` (Preload `85c7335f…658d`) | complete (tick 3400, 340 s); env C |
| wrap-control-1 | `4b6a8a01…041f` (Preload `7a8d9195…1999`) | observer-free; Preload identical; env B |
| saveload-observe-1 | `b86bd556…057a` (Preload `e26978a7…1971`) + 7 PNG | complete UI save (tick 300) and load; env B |
| saveload-control-1 | `6506d47f…44ad8` (Preload `e377541f…69d2`) + 7 PNG | observer-free; Preload identical (247 markers); env C |

## Open gaps / proposed IDs

- Producer of request-clock flags bit0 (pause) and of the constant primary flag `0x1000`: not found.
- Save/load after a wrap (epoch 1) and with presentation-clock requests: not tested live.
- Proposed **ORDER-05.6** for both (see `proposed-docs-ORDER-05.3.md`).

## Engine integration (Payoff181)

The original research status above records its capture-time scope. The engine
now retains absolute unit-release keys and live pending JASS identities in
save144, reconstructs cold heaps, discards unsaved outgoing requests and flushes
old primary requests through software0.2s before replacement. The primary
span/rebase/remainder drain also preserves callback-created work; allocating a
first timer inside an action retains the popped callback clock.
Production regressions cover the saved UI listener words, live wrap/rearm words,
cold rebased releases, ordering, old-owner cleanup and malformed payload rejection.
See [implementation and evidence limits](../../retail-pathfinding-engine.md#pending-request-clocks-survive-wrap-and-load-payoff181).
