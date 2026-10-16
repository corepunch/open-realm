<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/ORDER-03.2/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **ORDER-03.2**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ORDER-03.2/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-ORDER-03.2.json` -> [`ORDER-03.2-expected.json.gz`](../../../../../tools/ghidra/fixtures/research/ORDER-03.2-expected.json.gz) (uncompressed sha256 `2390bc63184c1a191e197700f345de6e778d7f312305eea3feb2911b6aeb94e7`, 637381 bytes)

# ORDER-03.2 handoff — destroy an order/unit/subscriber from a subscriber, then nested dispatch

**Status.** Instruction-verified (A: agent pin/unwind, per-level stack sentinel, depth-1-only reclamation, outermost-exit
growth, CScriptEvent packet and event-data lifetime, producer subscriber pre-check), original-code verified (O: 411 cases
of `verify_ORDER-03.2_nested_lifetime.py` — 11 named + 400 seeded random over two agents, nesting to depth 4, clears,
callback/agent reference drops — plus the 10 named `03.2` cases of the ORDER-03.1 oracle; 0 model mismatches) and
live-verified (L: public JASS on three probe maps — first probe phases 4–9, `order03b` nested/pre-check probe,
`order03c` player-family payload probe — each with observer-free controls and repeats).
Established: depth and unwind at every level, nested delivery sets, stop/RemoveUnit/KillUnit/DestroyTrigger inside a
subscriber followed by nested dispatch, payload words seen before and after nested events, absence of stale callbacks.
Not established: engine behaviour (owner); trigger waits (`TriggerSleepAction`) inside nested actions; physical CUnit
destruction time (6790f0, excluded).
Player-family payload lifetime was predicted from assembly (both families resolve the event-held order through identity) and is confirmed live by the `order03c` probe (below).

## Functions (ABIs from assembly; names written via gw.py under ORDER-03.1/03.2)

Dispatcher/table functions are in the ORDER-03.1 handoff (6f071dc0, 6f071e00, 6f0725b0/0725d0, 6f0728c0/0728e0,
6f071d00, 6f071bd0, 6f071a90, …). Added here:

| VA | Role | ABI | Evidence |
|---|---|---|---|
| 6f072190 | Agent_HasEventSubscriber | thiscall ECX agent, [4] event; RET4; EAX 0/1 | 072193 table null → 0; else 0721d0(event, 0) |
| 6f0721d0 | EventTable_HasSubscriber | thiscall ECX table, [4] event, [8] callback (0 = any); RET8 | live match → 1 (072287); unlinks tombstones it passes at depth 0 (07220f..072253) |
| 6f67c230 | Unit_FireIssuedPointOrderEvent | thiscall ECX unit, [4] order; RET4 | 67c266..67c29b samples unit `0x8024c` and player `0x80227` subscribers **before** any delivery; neither → return; player fire 67c3d3 (→ 2614e0) precedes unit fire 67c3f2 (→ 67ae10) |
| 6f67bd10 | Unit_FireIssuedOrderEvent | thiscall ECX unit, [4] order; RET4 | same with unit `0x8024b` / player `0x80226` (player 2610c0 before unit 67ae10) |
| 6f67ae10 | Unit_FireScriptEvent | thiscall ECX unit, [4] event, [8] event data; RET8 | 0x14-byte packet (Storm 401, line 0x54d), vt 6fa9f7f4 CScriptEvent, refs+1 (67ae90), data counted at +10 (67aecf..67aeee), handle 282e30, unit vt+10 (67aef7), handle release 283f40, packet ref release (67af10..67af19) only after the dispatch returns |
| 6f262cb0 | Player_FireScriptEvent | thiscall ECX player, [4] event, [8] data; RET8 | same structure (262d0a..262dd3) |
| 6f2614e0 / 6f2610c0 | Player_FireIssuedPointOrderEvent / …IssuedOrderEvent | RET8 | `HasEventSubscriber(player, 0x80227/0x80226)` then 262cb0 |

Engine-relevant cross references (already named): 6f298d20 CTriggerWar3_OnEvent (wrapper+20 guard),
6f0557b0 Agent_RequestDeferredRelease, 6f15e500 AgentWrapper_OnClockRequest (drain), 6f04c2c0 AgentPayload_ReleaseFromWrapper
(event 0x40190064 then 6f071d00 at depth 0), 6f061320 AgentIdentity_ResolvePayload (wrapper+20 retirement check used by
`Order_GetCommand` 685cd0 / `Order_GetPointX` 685cf0 through identity +38/+3c).

## Fields

| Struct+off | Meaning | Encoding | Write sites | Read sites |
|---|---|---|---|---|
| packet (CScriptEvent) +4 | refs; producer holds one for the whole delivery | u32 | 67ae90 / 67af10 | release → virtual 0 |
| packet +8 | event word, overwritten per node by the remap | u32 | 67ae6e, 071f0d | handlers |
| packet +C | registration that forwarded it | ptr | 27d2ea (`packet+C = reg`) | 298d82 |
| packet +10 | counted CScriptEventData | ptr | 67aeee / 262d9c | 27d1f0 (identity → reg+24/28) |
| reg +24/+28 | identity of the event data delivered | WC3PathIdentity, reset to -1 after delivery (27d311/27d31d) | 27d2a0/27d2a6 | 298d8c slot 5 |
| agent +4 | +1 per nesting level during delivery | u32 | 071dcd | 071ddc |
| table +0 | nesting depth (1..n) | u8 | 072180/071d90 | reclamation only at 1 |

Types: `types-ORDER-03.2.json` (WC3ScriptEventPacket, 6 prototypes).

## Behaviour

Frozen: `expected-ORDER-03.2.json`, sha256 `2390bc63184c1a191e197700f345de6e778d7f312305eea3feb2911b6aeb94e7` — oracle named results of both scripts, first-probe controls
for ticks 25–45, normalized observer rows, the complete nested/payload control marker lists and rows.

### Live — first probe (`order03_probe.j`, forward; event `0x8024c`, order 851986)

| Phase (tick) | Subscriber action | Retail decisions (depth = unit table byte +0 at the marker) |
|---|---|---|
| 4 (25) N | N1 registers N4, then nested `IssuePointOrder(N)` | Outer depth 1, nested depth 2; nested delivers P1, N1 (inner: `DestroyTrigger(N3)` → release request only), N2, **N4** (registered by the outer action, linked after the outer sentinel, before the nested sentinel); N3.reg visited but suppressed. Outer resumes with N2 only (N3 suppressed, N4 behind its sentinel). Unit refs 23→24 (outer) →25 (nested). Count rose 64→66 at depth 2 (internal re-subscription); growth 16→32 buckets only at the outermost exit (071f78). After the JASS callback: N3, N3.reg wrapper destroy and depth-0 unregister. |
| 5 (28) | — | N1, N2, N4. |
| 6 (32) R | R1 `IssueImmediateOrder(R,"stop")` | Immediate-order event `0x8024b` delivered nested at depth 2 (RI: ord 851972, current 851972); back in R1: current order 0; R2: **ord 851986, px −1700, py −700** (outer payload intact), current 0. Outer packet 0x14-byte CScriptEvent, nested one separate. Observed lifetimes: the unit's replaced COrderTarget (vt b78994) gets its release request inside Stop (caller 673671); a COrderPoint (vt b7886c, refs 2) gets its release request from the JASS handle release (28a34b) only after R2 returned — inference: this is the order held by the event data; both wrappers are destroyed at the next primary-clock drain. |
| 7 (36) K | K1 `RemoveUnit(K)` | Synchronously only stop tasks (`0xd01a4`, `0xd0144`, `0xd0162`, `0xd0178`, `0xd0156`, depth 1); K2 and K3 **still delivered** (unit handle valid, life 420, current 0); death trigger KD not fired. After the callback the same tick: `0xd0164` (mass ability unsubscription at depth 1 leaving tombstones, count 65→25), `0xd015a`, release request (6905ef), wrapper destroy, `0x40190064`, `Agent_ClearEventSubscribers` at depth 0 (table freed). Tick 37: `GetUnitTypeId` 0, life 0. |
| 8 (40) L | L1 `KillUnit(L)` | Internal death `0xd0153` at depth 2 performs ability cleanup (tombstones, count 65→25), player death `0x80261`, then unit death `0x80235` at **depth 3** delivering LD (bucket chain `[<tombstone 0xd0175>, LD.reg]`, tombstone skipped, not reclaimed at depth 3); back in L1 life 0; L2, L3 still delivered (life 0). |
| 9 (45) | `RemoveUnit(L)` outside dispatch | `0xd01a4`/`0xd01a0` at depth 1, release request, wrapper destroy, `0x40190064`, clear at depth 0. |

### Live — `order03b_probe.j` (map `RS-ORDER-03.2-nested`)

| Phase (tick) | Action | Retail decisions |
|---|---|---|
| 1 (5) V | player trigger P2 registers V's **first** unit trigger V1 during the player delivery | **No unit dispatch at all** for this order (pre-check sampled no `0x8024c` subscriber; count 63→64 only during P2); V1 not delivered. |
| 2 (7) | — | P2 then V1 (eval 1). |
| 3 (10) W | P2 `DestroyTrigger(W1)` (W's only trigger) during the player delivery | Unit dispatch still happens (pre-check saw W1); W1.reg visited, `CTriggerWar3_OnEvent` returns 0; after the callback W1/W1.reg destroyed, unregister at depth 0, count 66→65. |
| 4 (12) | — | P2 only. |
| 5 (15) M | M1 `RemoveUnit(M)` then nested `IssuePointOrder(M)` | Nested order rejected: only internal `0xd02a5` (ret 0), **no order events**; M1 exit and M2 see the unit valid (life 420, current 851986); removal completes after the callback (`0xd0164`, `0xd015a`, release, `0x40190064`, clear); tick 16 handle dead. |
| 6 (20) X | X1 `DestroyTrigger(X2)`, nested order, then registers X3 | Nested: P2, X1 (X2 suppressed); X3 appended after the outer sentinel at depth 1 → not delivered in the outer pass. |
| 7 (22) | X1 `DestroyTrigger(X2)` again (stale handle, no request), nested, registers another X3 | Nested: P2, X1, X3(first); outer: X3(first) but not the new X3. |
| 8 (25) Y | Y1 nests twice (points −650, −700) | Depth 1→2→3, unit refs 23→24→25, two stacked sentinels; each level's later subscribers (Y2) run after the inner level unwinds; markers after each nested call show that level's own point (outer still −700/−650 respectively). |

### Live — `order03c_probe.j` (map `RS-ORDER-03.2-payload`)

Player triggers Pa, Pb (`0x80227`, registered in that order) and Pi (`0x80226`, player immediate order); unit
trigger Z1/Q1 (`0x8024c`).

| Phase (tick) | Action | Retail decisions |
|---|---|---|
| 1 (5) Z | Pa issues `Stop` to Z during the **player** delivery | Pi delivered nested inside Pa (`ord=851972`, current 851972); Pa exit: current 0; Pb (same player dispatch, later subscriber) reads **ord 851986, px −1936, py −700**; then the unit dispatch delivers Z1 with the same original payload, current 0. |
| 2 (10) Q | Pa `RemoveUnit(Q)` during the player delivery | Pb and Q1 still delivered with the original payload, unit valid (life 420, current 0). After the JASS callback (same tick) the deferred removal runs inside Q's own `0xd0164` dispatch (depth 1): ability cleanup unsubscribes with tombstones and the Defend ability issues **two `undefend` immediate orders (852056)**, delivered to Pi with `GetTriggerUnit()` = Q and life **0.000**; then release, `0x40190064`, clear. Tick 11: Q's handle is dead (`…:0:0.000:0`). |

### Oracle (forced-state, labelled)

`verify_ORDER-03.2_nested_lifetime.py` named cases (four subscribers C0..C3 on E unless stated):

| Case | Decisions |
|---|---|
| clear-then-nested-same-agent | C0 clears (tombstones all, count 0), nested dispatch delivers nothing (ret 0), C0 registers C5, second nested delivers C5 at depth 2; outer skips/reclaims tombstones, does not deliver C5; next pass: C5 only |
| destroy-agent-then-nested | C1 clears and drops the last external agent ref; nested ret 0; agent destroyed only at outer unwind (after the last delivery, before the outer return) |
| nested-unregisters-outer-current / -next / -earlier | removal inside a nested pass is a tombstone; the outer pass continues from the current node's next pointer and skips tombstones |
| nested-remove-and-reregister | re-registration is a new tail node after both sentinels: not delivered by either pass |
| depth-four-growth-deferred | 20 registrations at depth 4: count 1→21 while buckets stay 4; growth to 8 at the outermost exit |
| nested-other-bucket-tombstone-survives | tombstone in another bucket survives (not visited) |
| self-last-ref-then-nested | callback destroyed inside its own unregister, then nested dispatch: no call into it |
| cross-agent-clear-during-other-dispatch | agent 1's subscriber clears agent 0 while agent 0 is dispatching (depth 1): agent 0's remaining subscribers skipped |
| agent-refs-unwind-order | agent refs 2 (pinned) →3 (nested) →2 →1 (subscriber drops a ref) → destroyed at outer unwind |

Random: 400 scripts (seed 3032; two agents, cross-agent nesting, clears, callback ref drops): 988 deliveries, 285 nested
dispatches, depth ≤ 4, 119 callback destructions, 2 agent destructions; 0 mismatches. The ORDER-03.1 oracle's 03.2
group (nested-same-event-sees-inserted, nested-removal-tombstone-reclaimed-by-outer, nested-other-event-same-bucket,
three-level-nesting, clear-all-during-dispatch, clear-all-in-nested-dispatch, agent-last-ref-released-during-dispatch,
register-after-clear-during-dispatch, cross-agent-nested, dispatch-without-table) is frozen here as well.

### Rules

1. Each nesting level pins the agent and links its own sentinel; a nested pass delivers every live node linked before
   it started (including nodes appended by outer actions); outer passes never deliver nodes appended after their own
   sentinel. Unwind restores depth level by level; the agent is destroyed only after the outermost level returns.
2. Only a depth-1 pass reclaims tombstones; growth happens at the outermost exit.
3. Order destruction (Stop/replacement) inside a subscriber does not alter the payload of the running event: each
   producer call owns a CScriptEvent packet and counted event data until it returns; nested events get their own.
4. `RemoveUnit` from a subscriber defers removal to the release drain (subscriptions survive the current dispatch; the
   unit handle stays valid; nested orders to it are rejected without events). `KillUnit` delivers death nested
   synchronously.
5. `DestroyTrigger` suppresses delivery at once (all levels) and unsubscribes at the drain, so no stale callback runs.
6. Producer pre-check: unit/player subscriber presence is sampled once before the player delivery.

## Reproducer (main checkout root)

```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research; G=/run/media/lofcz/ssd_external/Games/w3/game.dll
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_ORDER-03.2_nested_lifetime.py --binary $G \
  --report /tmp/o32.json --random 400 --expected $R/ORDER-03.2/expected-ORDER-03.2.json   # exit 0, report sha 028f9d8f…7210
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_ORDER-03.1_subscriber_dispatch.py --binary $G \
  --report /tmp/o31.json --random 600 --expected $R/ORDER-03.1/expected-ORDER-03.1.json   # 03.2 group included
B=/GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/Human02Interlude-original.w3m
python3 tools/frida/research/order03b_make_map.py --base $B --tool build/bin/mpqtool --scenario nested --output /tmp/RS-ORDER-03.2-nested.w3m
python3 tools/frida/research/order03c_make_map.py --base $B --tool build/bin/mpqtool --scenario payload --output /tmp/RS-ORDER-03.2-payload.w3m
$R/_env/install-map.sh /tmp/RS-ORDER-03.2-nested.w3m /tmp/RS-ORDER-03.2-payload.w3m
tools/frida/research/order03b_runs.sh /tmp/c nested-observe-1:observe nested-control-1:control nested-observe-2:observe
tools/frida/research/order03c_runs.sh /tmp/c payload-observe-1:observe payload-control-1:control payload-observe-2:observe
python3 tools/frida/research/order03_analyze.py --units nested repeat /tmp/c/nested-observe-1.jsonl /tmp/c/nested-observe-2.jsonl
python3 tools/frida/research/order03_analyze.py markers /tmp/c/nested-observe-1.jsonl /tmp/c/nested-control-1-rs-o3-nested.txt
python3 tools/frida/research/order03_expected.py $R ORDER-03.2 --check
```
First-probe captures and their reproduction are in the ORDER-03.1 handoff (same files).

## Provenance

game.dll `d51e5680…8236`. Base map `199683be…2104`. First-probe maps/scripts: see ORDER-03.1. Nested map
`a46d335a…5cf` (probe `order03b_probe.j` `b7cd3621…ab6b`, builder `order03b_make_map.py` `6d6ffbc3…a11c`); payload map
`604a9e28…7732` (probe `order03c_probe.j` `3b274ce3…5215`, builder `order03c_make_map.py`). Observer `order03_observer.js`
`1e048c04…`, launcher `order_trace.py` `beb82075…`. Oracle `verify_ORDER-03.2_nested_lifetime.py` (+ imported harness
`verify_ORDER-03.1_subscriber_dispatch.py`), hashes in the report/expected. No seed in live scenes; oracle seed 3032.

## Captures

| Capture | Env | Status |
|---|---|---|
| ORDER-03.1/captures forward-observe-1/2, forward-control-1, reverse-observe-1, reverse-control-1 | C/B | as in ORDER-03.1 (phases 4–9 used here) |
| nested-observe-1.jsonl (+ rs-o3-nested) | C | complete, 124 markers, equal to control |
| nested-control-1 (+ rs-o3-nested) | C | observer-free control |
| nested-observe-2.jsonl | C | repeat: 826 normalized rows identical to run 1 |
| payload-observe-1.jsonl (+ rs-o3-payload) | C | complete, 43 markers, equal to control |
| payload-control-1 (+ rs-o3-payload) | C | observer-free control |
| payload-observe-2.jsonl | C | repeat: 322 normalized rows identical to run 1 |

Hashes of every file are in `expected-ORDER-03.2.json` (`live.nested.captures`, `live.payload.captures`,
`live.captures`).

## Controls and repeats

All observer runs reproduce their observer-free control marker lists exactly (first probe 208/202, nested 124, payload 43).
Normalized observer timelines repeat exactly (first probe forward across environments C/B; nested and payload twice).

## Public vs forced-state

Public: every live phase (JASS natives only). Forced-state (labelled): all oracle cases (supplied agents, callbacks,
handler bodies calling original register/unregister/dispatch/clear, supplied Storm allocation/memset); agent destruction
there is modelled by its two table effects (clear + last reference), which the live RemoveUnit path performs later at
depth 0 instead.

## Exclusions (owning IDs)

Deferred release drain order/ties and wrapper reuse → ORDER-05.1; release requests made during a drain → ORDER-05.1/05.2;
physical CUnit destruction (6790f0) → ORDER-04.2/BASE-03.1; trigger sleep/wait inside nested actions, `TriggerExecute`,
filters/conditions with side effects → not covered (BASE-03.1 inventory); the meaning of internal events
`0xd0164/0xd015a/0xd02a5` beyond their observed order → ORDER-06 (cancellation/interruption).

## Mismatches preserved

* `retail-pathfinding-movement.md` "Movement subscriptions…": "These are deliberately suspended prefixes … general
  reentrancy remains open" → resolved for nesting (rules 1–2 above).
* OpenRealm: `G_ExecuteEvent` drops queued non-death events whose subject is deferred-free (`G_IsDeferredFree`), and
  actions run later than the producer; retail delivers the remaining subscribers of a running dispatch after
  `RemoveUnit`, synchronously, with a valid unit. `DestroyTrigger` is a no-op (see ORDER-03.1).

## Engine entry points

`games/warcraft-3/game/g_events.c` (`G_ExecuteEvent` deferred-free/spawn checks, `G_RunEvents`),
`games/warcraft-3/jass/jdo.c` (`jass_calltriggerevent`, nested `jass_calltriggercontext`), `games/warcraft-3/game/m_unit.c`
(`G_PublishIssuedPointOrder`/`G_PublishIssuedImmediateOrder`: pre-check and player-before-unit order), unit removal/kill
(`RemoveUnit`/`KillUnit` natives in `games/warcraft-3/game/api/`, `G_IsDeferredFree` users), `api/api_trigger.h`
(`DestroyTrigger`).

## Failing engine regressions (suggested)

1. Nested order scene (first probe tick 25 / nested probe ticks 20–25): the exact marker lists in
   `expected.live.controls.forward` (ticks 25–28) and `expected.live.nested.control`; key assertions: inner delivery
   includes N4/X3 registered by the outer action; the outer pass never delivers them; destroyed N3/X2 never run.
2. Stop inside a subscriber (tick 32): RI enter/exit inside R1 (`ord=851972`), R2 `ord=851986 px=-1700.000
   py=-700.000` with current order 0.
3. RemoveUnit inside a subscriber (tick 36 / nested tick 15): K2/K3 (M2) delivered with `…:420.000:0` (`…:851986`);
   nested order to the removed unit produces no events; next tick the handle is dead (`…:0:0.000:0`).
4. KillUnit inside a subscriber (tick 40): LD enter/exit (life 0) inside L1, then L2/L3 with life 0.
5. Pre-check (nested tick 5/7): V1 registered by the player trigger is not delivered for the same order but is on the
   next.
6. Player-family payload (payload probe): Pb and Z1 after Pa's Stop read `ord=851986 px=-1936.000 py=-700.000`;
   RemoveUnit from a player subscriber still delivers Pb and Q1, then two `undefend` (852056) player immediate-order
   events with life 0.000 during the deferred removal in the same tick (`expected.live.payload.control`).

## Artifacts

`HANDOFF.md`, `expected-ORDER-03.2.json`, `oracle-report-ORDER-03.2.json` (+stdout/stderr), `types-ORDER-03.2.json`,
`mapping-rows-ORDER-03.2.txt` (8 rows = gw.py writes, `ghidra-writes.jsonl`), `proposed-docs-ORDER-03.2.md`, `maps/`,
`captures/`. Repository (new files): `tools/ghidra/research/verify_ORDER-03.2_nested_lifetime.py`,
`tools/frida/research/order03b_{probe.j,make_map.py,runs.sh}`, `order03c_{probe.j,make_map.py,runs.sh}`; shared
ORDER-03 tooling listed in ORDER-03.1.

Proposed new IDs: **ORDER-03.5** producer subscriber pre-check; **ORDER-03.6** deferred RemoveUnit inside unit-event
actions (text in `proposed-docs-ORDER-03.2.md`).


## Engine integration and correction (Payoff178)

ORDER-03.2 is now implemented and closed. Preserve the earlier observations
above as archived research; this section corrects their admission interpretation.
RemoveUnit suspends execution rather than rejecting a valid nested point Move:
the native returns true and a new logical head remains queryable, without
physical execution or a public issued-order event. Two fresh complete admission
runs freeze this return explicitly; common markers match the prepared control.
Incomplete new controls and failed launches are retained and rejected, not
counted as completion.

Read-only caller stacks also resolve the two forced Defend-off events: ability
availability retirement, then independent detach, each calls modal605aa0 and
transient688790. Inactivity/life0 is committed beforehand. The engine models
separate generic retirement and removal messages; Defend owns the notifications.
No fixed-count notification loop or raw selector special case is introduced.

A further instruction-verified producer difference is explicit in the engine:
Unit_FireDeathEvent67b8a0 calls Player_FireUnitDeathEvent260ab0 before checking
unit subscribers. Issued-order producers check both family presences before
player delivery. The late unit-death registration regression distinguishes them.

See [engine contract and reproductions](../../retail-pathfinding-engine.md#nested-orders-retain-their-packet-and-removal-suspends-execution-payoff178).
Frozen fresh correction: [integration178](../../../../../tools/ghidra/fixtures/research/ORDER-03.2-integration178.json).
No proposed extra TODO leaves were adopted. Physical destruction timing,
arbitrary removed-unit spell/target/immediate admission and nested sleeps
remain excluded from this evidence.
