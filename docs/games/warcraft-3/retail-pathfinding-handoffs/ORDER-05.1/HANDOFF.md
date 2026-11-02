<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/ORDER-05.1/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **ORDER-05.1**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ORDER-05.1/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-ORDER-05.1.json` -> [`ORDER-05.1-expected.json`](../../../../../tools/ghidra/fixtures/research/ORDER-05.1-expected.json) (uncompressed sha256 `083e251b9b64a1b7fdce88c6626d4b32ae6f6dc5ff3078c96ad8db827e76a470`, 60536 bytes)

# ORDER-05.1 handoff — agent request heap: equal/different deadlines, pop and tie order, cancellation, wrapper/block reuse

**Status.** Instruction-verified (A: queue 6f15e310, producers 6f15dc30/6f15d6f0/6f15e0e0, cancel 6f15d7a0, heap
6f04f990/6f04f3c0, drain 6f052380, execute 6f054370, OnClockRequest 6f15e500, clock select 6f15c650), original-code
verified (O: `verify_ORDER-05.1_request_heap.py`, 432 cases = 32 named + 400 seeded random over both clocks, 0 model
mismatches; 12 named cases are group 05.1) and live-verified (L: public JASS probe `order05_probe.j` scenario
`requests`, phases 1–2: a `DestroyTrigger` burst and range listeners; observer run == observer-free control (exact
Preload text), an identical repeat, and a pop-order check over all 717/715 logged pops).
Established: the request clocks, request layout, key order (float deadline, unsigned serial), min-delay clamp,
truncating deadline words, cancel-by-flag with silent pop, LIFO block reuse, release idempotence, drain-time semantics.
Not established: presentation-clock requests from a public producer (none occurred live; oracle only); engine
behaviour (owner).

## Functions (ABIs from assembly; names written via gw.py under ORDER-05.1)

| VA | Name | ABI | Evidence |
|---|---|---|---|
| 6f15e310 | SimClock_QueueAgentRequest (existing name) | thiscall ECX clock, [4] receiver, [8] &value, [C] delay word, [10] &deadline, [14] serial; RET14; EAX request | block from free list `+38` (else pool `+24`), `+3C`++, request = block+4, flags `20000`, heap push 04f990 |
| 6f15dc30 | SimClock_ScheduleAgentRequest | thiscall ECX clock, [4] receiver, [8] &value, [C] &delay; RET0C | `COMISS min,delay; CMOVA` → delay ≥ `38d1b717` (NaN unchanged); `++clock+50`; deadline = Math_Add 06fbb0(time, delay) |
| 6f15d6f0 | AgentWrapper_StartTimer | thiscall ECX wrapper, [4] &period, [8] value; RET8 | old `+1C` gets `|=10000` (no unlink), clock 15c650, same clamp/serial/deadline, `+1C` = new, repeat bit1 if `+4E` bit0 |
| 6f15d7a0 | AgentWrapper_StopTimer | thiscall ECX wrapper; RET | `+1C` `|=10000`, `+1C`=0, hooks 15e890 / 15e830 by `+4C` 40000/20000 |
| 6f15e0e0 | AgentWrapper_ScheduleRelease (existing) | thiscall ECX wrapper; RET | returns if `+20`≠0; delay 0 → clamped 1e-4; value 0; stores `+20` |
| 6f04f990 | RequestHeap_Push | thiscall ECX heap (= clock+4), [4] request; RET4 | append (04ff60) + sift-up; `UCOMISS` equal → `CMP serial; SBB` (unsigned), else `COMISS` |
| 6f04f3c0 | RequestHeap_PopMin | thiscall ECX heap; RET; EAX slot 1 | last slot removed (052d80), sift-down with the same order |
| 6f15c650 | AgentOwner_SelectRequestClock | thiscall ECX owner, [4] wrapper; RET4 | wrapper `+14` bit31 → owner+68 else owner+14 |
| 6f052380 | SimClock_DrainDueRequests (existing) | thiscall ECX clock; RET | while count>1 and `time ≥ top.deadline` (`COMISS; JC`): pop, time = deadline, clear `20000`, execute; restore time |
| 6f054370 | SimClock_ExecuteRequest (existing) | fastcall ECX request; RET | `10000` → free silently; else receiver vt+48(request); repeat∧¬cancel → 053710; else free (LIFO `+38`, `+3C`--) |
| 6f15e500 | AgentWrapper_OnClockRequest (existing) | thiscall ECX wrapper, [4] request; RET4 | request = `+20` → vt+10(0) (teardown); else signals by `+4C` 400/100/200, then a fired non-repeating `+1C` → StopTimer |

Types: `types-ORDER-05.1.json` (WC3AgentRequestClock, WC3AgentRequest, WC3AgentWrapperRequestFields; 11 prototypes).
Mapping rows: `mapping-rows-ORDER-05.1.txt` (6 rows). Ghidra: `ghidra-writes.jsonl` (6 renames, 7 plate comments).
Disassembly: `asm/requests.txt`.

## Fields

| Struct+off | Meaning | Encoding | Write | Read |
|---|---|---|---|---|
| owner(6fd53a48)+14 / +68 | primary / presentation request clock | WC3AgentRequestClock | ctor, 15c490 (load) | 15c650 |
| clock+10/+1C/+20 | heap array / capacity / count incl. slot 0 | ptr/u32/u32 | 04ff60, 052d80 | 052380, 0521f0 |
| clock+38 / +3C | LIFO free list of 0x28-byte blocks / live requests | ptr/u32 | 054370 / 15e310 | 15e310 |
| clock+40/+44/+48/+4C/+50 | time / epoch / span (300) / flags (bit0 pause) / serial | f32/u32/f32/u32/u32 | 052380, 054190, 0521f0, producers | — |
| request+4/+8 | deadline / period | f32 words from software add | 15e310, 053710 | heap, drain |
| request+10 | 20000 queued, 10000 cancelled, 1 repeating | u32 | 15e310, 052380, 15d6f0, 15d7a0 | 054370 |
| request+14/+18/+1C | serial / receiver wrapper / value | u32/ptr/u32 | 15e310 | heap, 054370 |
| wrapper+1C / +20 | timer request / release request | ptr | 15d6f0 / 15e0e0 | 15d7a0, 15e500 |

## Behaviour

Frozen: `expected-ORDER-05.1.json`, sha256 `083e251b9b64a1b7fdce88c6626d4b32ae6f6dc5ff3078c96ad8db827e76a470` — oracle
summary and the 12 named 05.1 results; live captures' sha256, observer==control, repeat equality, ordercheck, the
control markers for ticks ≤ 20 and 205 normalized rows (blocks/receivers renamed by first appearance, deadline words and
serials kept).

### Live — `order05_probe.j` scenario `requests` (map `RS-ORDER-05-requests`), ticks 0.1 s (JASS timer)

| Phase (tick) | Public action | Retail decisions (primary clock) |
|---|---|---|
| 1 (5) | create D1..D4, then `DestroyTrigger` D3, D1, D4, D2 in one callback | four releases, one per trigger wrapper (vt `a8099c`), all deadline `3f000686` (0.5+1e-4 by software add, clock word `3efffff1`), serials 36, 37, 38, 39 in call order |
| 1 (5) | `DestroyTrigger(D3)` again | no request (the native does not reach 15e0e0) |
| 1 (5→) | next primary advance (5 ms) | the four pop in serial order (D3, D1, D4, D2), clock time = `3f000686` in each; each tears its wrapper down (145c70); blocks B0..B3 freed in that order |
| 1 (6) | create N1..N4 | handles 1048787..790 are new (D handles 1048783..786 are not reused by the next tick: handle allocation is not this free list) |
| 2 (10) | `TriggerRegisterUnitInRange` RA, RB (same callback) | each listener: StartTimer period 1.0 (`3ffffff8`), then 0.125 twice via 15fe75/15e63a; each start cancels the previous. Blocks reused LIFO: B3, B2, B1 (RA), B0, B4, B5 (RB) — the last freed release block first |
| 2 (10→) | due at `3f8ffff8` | cancelled 0.125 requests (serials 41, 44) pop silently; RA (42) then RB (45) execute; rearm `+0.125` |
| 2 (11) | RC registered 0.1 s later | own phase: `3f9cccc4` = 1.225, +k/8 |
| 2 (15) | `SetUnitPosition(U)` into range | 7 unit-internal releases (serials 49–55); RA's request at 1.5 detects U → enter RA (el 1.505), then RB at the same deadline (el 1.505), RC at 1.6 (el 1.605) |
| 2 (≥20) | cancelled period-1.0 requests | pop silently at `3ffffff8`/`40066662` |

Pop-order check (`order05_analyze.py ordercheck`): every logged pop was the (float deadline, unsigned serial) minimum
of the logged pending requests of its clock and saw clock time equal to its deadline word — 717 pops (observe-2),
715 (observe-3), 0 violations.

### Oracle (forced-state, labelled)

Supplied: path owner at 6fd53a48 with both clocks (free list of 192 blocks each, capacity 512, span 300), wrappers
(vt+48 = log, then ORIGINAL 15e500, then scripted ops; vt+10 = ORIGINAL StopTimer + cancel/clear `+20`, standing in for
the 15d840 teardown), Storm alloc/memset stubs. Clock time is forced only by `settime` steps (labelled). The independent
model uses the original Math_Add/Math_Subtract for deadline words.

| Case | Decisions |
|---|---|
| equal-deadlines-serial-order | four equal deadlines pop in creation (serial) order, not receiver order |
| different-deadlines | 0.25 (two, serial order), 0.5, then 0.75 in a later drain |
| delay-clamped-to-minimum | delay 0, −1.0 and `38d1b716` all become `38d1b717` (deadline `41200068`) |
| due-boundary-equality | deadline == clock time pops; 0.125 later stays queued |
| cancel-pending-timer / restart-cancels-previous | cancelled request pops at its deadline without a callback and is freed |
| release-idempotent-and-minimum-delay | second release of a wrapper creates nothing; releases due 1e-4 later, in serial order |
| negative-identity-uses-presentation-clock | identity bit31 → owner+68; its serial counter is separate |
| free-list-lifo-reuse | freed blocks 0,1 are reused 1,0 |
| serial-wrap-tie | serials `ffffffff`, 0, 1 at one deadline pop 0, 1, `ffffffff` (unsigned compare) |
| callback-sees-own-deadline-clock-restored | callback sees its deadline, clock time restored to the drain target |
| callback-schedules-due-and-later | a request created in a callback with deadline ≤ target pops in the same drain |

Random: 400 cases (seed 5051; 6 wrappers, both clocks, scripted callbacks with sched/start/stop/release, serial start
near wrap): 4529 callbacks, 182 silent pops, 3179 rearms, 3395 requests over the whole oracle; 0 mismatches.

### Rules

1. Order key: float deadline, then unsigned serial; serial is per clock, incremented per new request, kept on rearm.
2. Delay = max(delay, 1e-4) by `COMISS`; deadline = truncating software add of clock time and delay.
3. Cancel only flags; the heap is never searched. Superseded timers and stopped requests stay queued to their deadline.
4. Inside a drain, clock time = the popped deadline; new requests are relative to it and may drain in the same call.
5. Freed blocks are reused last-freed first. A wrapper has at most one release (`+20`).

## Engine entry points and failing-regression expectations

- `games/warcraft-3/game/g_utils.c` `G_DeferFreeEdict` / `G_RunDeferredFrees`: pops `deferred_frees[count-1]`
  (LIFO). Retail: equal-deadline releases complete in call order → removals of D3, D1, D4, D2 queued in one callback
  must complete D3, D1, D4, D2.
- `games/warcraft-3/game/api/api_trigger.h` `DestroyTrigger` is a stub (no suppression, no release). Expected: a
  destroyed trigger is not delivered again and its registrations go at the next primary advance (5 ms), before the
  next JASS timer tick.
- `games/warcraft-3/game/api/api_trigger.h` `TriggerRegisterUnitInRange` + `g_events.c` `EVENT_UNIT_IN_RANGE`: the
  engine edge-detects per frame. Expected (live): listeners poll at 1/8 s from registration time; RA and RB
  registered together fire in registration order at the same deadline; RC registered 0.1 s later fires 0.1 s later
  for the same approach (el 1.505 / 1.505 / 1.605).
- `games/warcraft-3/game/g_timer.c` (`TimerLess`, heap, `TimerDrain`) and `wc3Clock_t` in `g_local.h`: the existing
  JASS timer heap already uses (deadline, serial); an agent request clock can reuse this structure with a separate
  serial and the 1e-4 clamp.

## Reproducer (worktree root)

```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_ORDER-05.1_request_heap.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report /tmp/o05.json --random 400 \
  --expected $R/ORDER-05.1/expected-ORDER-05.1.json            # exit 0
python3 tools/frida/research/order05_make_map.py --base /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/Human02Interlude-original.w3m \
  --tool build/bin/mpqtool --scenario requests --output /tmp/RS-ORDER-05-requests.w3m     # == maps/RS-ORDER-05-requests.w3m
O5_EXTRA=$'--continue-at\n70\n--continue-every\n5' tools/frida/research/order05_runs.sh <outdir> requests 110 requests-observe-N:observe requests-control-N:control
python3 tools/frida/research/order05_analyze.py ordercheck <outdir>/requests-observe-N.jsonl
python3 tools/frida/research/order05_expected.py $R ORDER-05.1 --check                     # "unchanged"
```

## Provenance

game.dll `d51e5680…d8236`. Base map `Human02Interlude-original.w3m`. Maps (`maps/`): requests `8b734bc1…ecce4d`,
saveload `85e5f5c6…59c0`, wrap `1e54be40…a92b` (+ `.j`/`.json`). Scripts: oracle `41c507ec…6b38` (report
`oracle-report-ORDER-05.1.json` `72d2203e…9c30df`; stdout/stderr kept), harness `verify_ORDER-03.1_subscriber_dispatch.py`
(imported unchanged), probe `032c4aa1…a5affc`, observer `c05f138c…0604ea`, launcher `order_trace.py` `beb82075…c7ba`,
runner `bb544548…6344`, map builder `8bee80f5…1658967`, analyzer `1f8c88ac…d72eab`, composer `f871cf89…f414c4`.

## Captures (all preserved, `captures/`)

| Capture | sha256 | Status |
|---|---|---|
| requests-observe-1-FAILED.{jsonl,log,txt} | empty | **failed**: the runner was wrapped in `timeout 900`, which expired while waiting 15 min for the live lock; `order_trace.py` died right after spawning war3 (env C, 09:10:12). The orphaned owned process (map RS-ORDER-05-requests) was killed by pid ~1 min later; another agent's GROUP capture had started in env C at 09:10:14 and overlapped for about a minute. Not used. |
| requests-observe-2 | `a2e7d595…0262` (Preload `4fe9ed37…14f5`) | complete; markers == control exactly |
| requests-control-1 | `47411648…52b6` (Preload `782606e6…c273`) | observer-free control |
| requests-observe-3 | `785c3556…3f7f` (Preload `17416b2a…c731`) | repeat; normalized summary == observe-2 (to the `complete` marker) |

## Open gaps / proposed IDs

- Presentation-clock (`owner+68`) requests have no observed public producer (heap empty in every live run): oracle only.
- Handle IDs are not reused by the tick after a release (outside this TODO).
- Proposed **ORDER-05.4** (port the request clocks and route releases through them) and **ORDER-05.5** (range listeners
  as 1/8 s repeating requests with registration phase); see `proposed-docs-ORDER-05.1.md`.

## Engine integration — Payoff179

The original engine-gap descriptions above are preserved as research history.
`DestroyTrigger` suppression/release is implemented by Payoff177; unit storage
release now uses a primary-clock indexed heap in Payoff179. See
[the integration contract](../../retail-pathfinding-engine.md#unit-releases-join-the-primary-deadline-heap-payoff179)
for failing-first regressions, exact callback clocks, native pool identity limits
and strict fresh-original/archive verification. Repeating range-listener phase
and pending-clock save/load remain ORDER-05.2/05.3; proposed follow-up IDs in the
original handoff were not added to the implementation TODO list.
