<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/ORDER-03.1/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **ORDER-03.1**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ORDER-03.1/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-ORDER-03.1.json` -> [`ORDER-03.1-expected.json`](../../../../../tools/ghidra/fixtures/research/ORDER-03.1-expected.json) (uncompressed sha256 `595cd7ed40c60ac8a0eb9a203bb471152a36acf3ec9d587ac0587c1af60c7ed9`, 262598 bytes)

# ORDER-03.1 handoff — subscription insert/remove while dispatching to several subscribers

**Status.** Instruction-verified (A: every register/unregister/dispatch/clear/growth/pool routine below), original-code
verified (O: Unicorn executes the unmodified routines in 627 cases — 27 named + 600 seeded random — and an independent
model predicts every delivery, delivered remapped word, depth/count, agent and callback reference, destruction point,
final chain incl. tombstones and pool counts: **0 mismatches**) and live-verified (L: public JASS unit/player
order-event scenes on `RS-ORDER-03.1-forward/reverse`, observer-free controls with identical 208/202 marker strings,
forward repeated on environments C and B with identical normalized 1287-row subscriber timelines).
Established: delivery order, iterator (dispatch-start snapshot), insertion/removal during delivery, reference counts,
deferred growth. Not established here: nested dispatch / unit & order destruction / payload lifetime (ORDER-03.2);
engine behaviour (owner). A second probe (`order03b_probe.j`, player-dispatch insertion pre-check) is captured under
ORDER-03.2; its live result for the pre-check (item 9 below, currently **A only**) will be reported there.

## Functions (ABIs from assembly; Ghidra names written via gw.py)

| VA | Role | ABI | Evidence |
|---|---|---|---|
| 6f0725b0 | Agent_RegisterEventSubscriber (agent vtable+8) | thiscall ECX agent, [4] event, [8] remapped event, [C] callback; RET0C | 0725b3 `push 1; call 0720a0; jmp 0725d0` |
| 6f0725d0 | EventTable_Register (label inside 0725b0) | thiscall ECX table, same args; RET0C (07271d) | scan 072603..07268e: tombstone unlink only if depth==0 (07261f); same event sets found (072679); same callback → update remap only (0726cf→0726e5); else 0x10 node from 6fd3cce4 (0726a4), **append as tail** (0726c5..0726e0); callback refs+1 (0726ef); count+1 via 072170 only if no node of that event existed (07270a..072715) |
| 6f0728c0 | Agent_UnregisterEventSubscriber | thiscall ECX agent, [4] event, [8] callback (0 = all callbacks of event); RET8 | agent+8 null → RET8 (0728d1); else jmp 0728e0 |
| 6f0728e0 | EventTable_Unregister (label) | thiscall ECX table; RET8 (0729e9) | match: depth 0 → 072820 unlink + release + free (0729fb..072a21); depth>0 → release callback ref, node+8=0 **tombstone** (072a31..072a45); other live callback of same event → `other` (0729ad) and early return; count-1 (071d80) only if found && !other (0729cf..0729de) |
| 6f071dc0 | Agent_DispatchEvent (agent vtable+14) | thiscall ECX agent, [4] event, [8] packet; RET8; EAX 0/1 | table null → 0 (071df1); agent refs+1 (071dcd) around 071e00, refs-1 then virtual0 at zero (071ddc..071de7) |
| 6f071e00 | EventTable_Dispatch | thiscall ECX table, [4] event, [8] packet; RET8 (071fa3); EAX = any handler returned nonzero (071f17..071f24) | depth+1 (072180); saved count (071e39); **stack sentinel** (ebp-3c, callback 0) linked as bucket tail (071e5f..071e86); loop stops at sentinel (071f48); callback 0: depth==1 → unlink+free (071eaa..071ef7), else skip; event match → packet+8 = node+C (071f0d), `call [vt+0C]` (071f14), next read after the call (071f3b); sentinel unlinked 072820 (071f54); depth-1 (071f5e); depth 0 && count > saved → 071a90 (071f63..071f73). **No reference is taken on the callback object.** |
| 6f071da0 | Agent_DispatchPacket (vtable+10) | thiscall ECX agent, [4] packet; RET4 | vt+14(packet+8, packet) |
| 6f072190 / 6f0721d0 | Agent_HasEventSubscriber / EventTable_HasSubscriber (named under ORDER-03.2) | ECX agent [4] event RET4 / ECX table [4] event [8] callback(0=any) RET8; EAX 0/1 | live non-tombstone match; also unlinks tombstones it passes at depth 0 (07220f..072253) |
| 6f071d00 | Agent_ClearEventSubscribers | thiscall ECX agent; RET0 | 071bd0; returns 1 → free table to 6fd3ccd0, agent+8=0 (071d1e..071d2d) |
| 6f071bd0 | EventTable_ClearOrTombstone | thiscall ECX table; RET0; EAX depth==0 | buckets n-1..0 head-first; depth 0 unlink/release/free + free array (071ca1..071ce1); depth>0 release + tombstone (071c61..071c71); count=0 always (071ce9) |
| 6f071a90 | EventTable_GrowIfDense | thiscall ECX table; RET0 | only if count > 4*buckets (071ac7) and buckets < 0x40 (071acf); 0727d0 collect, free old array, 2n zeroed array, 0722a0 reinsert (tombstones freed) |
| 6f0727d0 / 6f0722a0 | EventTable_DetachAllNodes / ReinsertNodes | thiscall ECX table, [4] list; RET4 | splice order = bucket 0..n-1 chain order; pop head-first, append to new tails |
| 6f072170 / 6f071d80 | EventTable_IncrementCount / DecrementCount | thiscall ECX table; RET | word+2 ±1; increment tail-jumps 071a90 when depth==0 |
| 6f072180 / 6f071d90 | EventTable_EnterDispatch / LeaveDispatch | thiscall ECX table; RET | byte+0 ±1 |
| 6f0720a0 / 6f0717f0 | Agent_GetEventTable / EventTable_Init | ECX agent [4] create RET4 / ECX table RET | header dword 0x400 (depth 0, 4 buckets, count 0) |
| 6f072820 | EventTable_UnlinkIteratorNode | thiscall ECX bucket slot, [4] iterator {+4 prev,+8 cur}; RET4 | prev.next=cur.next; cur==tail → slot=prev (0 if single) |
| 6f06a3c0 | ObjectPool_FreeElement | thiscall ECX pool, [4] element, [8],[C] unused; RET0C | LIFO push on +10, live +8 −1 |
| 6f071680 | EventBucketPools_Init | thiscall ECX 6fd3ccf8, [4] per block; RET4 | five 0x14-byte pools: 0x10/0x20/0x40/0x80/0x100-byte arrays |
| 6f27aa90 | CUnitEventReg_Init | thiscall ECX reg, [4] trigger, [8] unit, [C] event, [10] filter; RET10 | unit vt+8(event,event,reg) then reg vt+8(event,event,trigger) |
| 6f27a530 | CPlayerEventReg_InitUnitEvent | same, [8] player; RET10 | |
| 6f27d1f0 | CUnitEventReg_OnEvent (vt+0C) | thiscall ECX reg, [4] packet; RET4 | packet+8 != reg+34 → 0; boolexpr filter for 0x8023c..58/0x8031e..29; packet+C=reg; own vt+10 dispatch → 1 |
| 6f298d20 | CTriggerWar3_OnEvent (vt+0C) | thiscall ECX trigger, [4] packet; RET4 | wrapper unresolved / not 2b61676c / **wrapper+20 != 0** (298d4e) → 0 without evaluating; event 0x80274 → +60−1 clamped; else 6f299c90 |
| 6f299c90 | CTriggerWar3_Fire | thiscall ECX trigger; RET | +58 eval+1; enabled & conditions → +5C exec+1, 6f2979f0(0) actions (synchronous) |
| 6f217cf0 / 6f2178f0 | Jass_TriggerRegisterUnitEvent / …PlayerUnitEvent | JASS natives | event + 0x80200 |
| 6f1fd210 | Jass_DestroyTrigger | JASS native | resolve, tail-jump vt+5C = 6f0557b0 Agent_RequestDeferredRelease (no unsubscription) |

Producer (named under ORDER-03.2): 6f67c230 Unit_FireIssuedPointOrderEvent pre-checks `HasEventSubscriber(unit,0x8024c)`
(67c26b) and `(player,0x80227)` (67c282) **before** either dispatch, then fires the player event (67c3d3 → 2614e0) and
then the unit event (67c3f2 → 67ae10).

## Fields

| Struct+off | Meaning | Encoding | Write sites | Read sites |
|---|---|---|---|---|
| agent+4 | reference count | u32 | 071dcd/071ddc pin; many | 071ddf zero → virtual0 |
| agent+8 | event table | ptr WC3AgentEventTable, lazily created | 0720a0, 071d2d | 071dc6, 0728c3, 072193 |
| table+0 | dispatch depth | u8 | 072180, 071d90 | 0725d0/0728e0/071bd0 (==0), 071eaa (==1), 072174 |
| table+1 | bucket count | u8 power of two 4..64 | 0717f0 (4), 071b36 (×2), 071ce1 (0 at free) | event & (n−1) |
| table+2 | **distinct-event** count | u16 | 072170, 071d80, 071ce9 | 071a90 (>4n), 071e39/071f6b |
| table+4 | bucket array of circular-list **tail** pointers | ptr | 0717f0, 071b2e | all |
| node+0/+4/+8/+C | next / event / counted callback (0 = tombstone or stack sentinel) / remapped event | 16 bytes, pool 6fd3cce4 | 0725d0, 0728e0, 071e00 | 071e00 copies +C into packet+8 |
| packet+8 | event word seen by the handler | u32, overwritten per node | 071f0d | 27d1f0, 298d5c |
| trigger+58/+5C/+60 | eval count / exec count / running executions | u32 | 299c93, 299cad, 298d65 | GetTriggerEvalCount/ExecCount |
| wrapper+20 | pending release request | ptr | 15e0e0 (via 0557b0) | 298d4e handler guard |

Layouts/methods: `types-ORDER-03.1.json` (WC3AgentEventTable, WC3AgentEventNode, WC3UnitOrdersPrefix+8 event_table,
16 instruction-verified prototypes). Pool objects are labelled (AgentEventNodePool 6fd3cce4, AgentEventTablePool
6fd3ccd0, AgentEventBucketPools 6fd3ccf8).

## Behaviour

Frozen: `expected-ORDER-03.1.json`, sha256 `595cd7ed40c60ac8a0eb9a203bb471152a36acf3ec9d587ac0587c1af60c7ed9`
(oracle named results for the 03.1 group, live control markers for ticks 0–20/50–53, normalized observer rows).

### Live public scene (`order03_probe.j`, forward variant; unit U = handle 1048780 'hfoo', order 851986 = 0xd0012 move)

Events: unit point order 0x8024c (JASS 76+0x80200), player point order 0x80227 (39+0x80200). U's table before the scene:
depth 0, 32 buckets, count 64; the 0x8024c bucket holds internal nodes `[U-self, CAbilityMove(b62794), CAbilityAttack(adb5a4)]`
followed by the registrations.

| Phase (tick) | Producer | Exact retail decisions |
|---|---|---|
| 1 (10) | `IssuePointOrder(U)` | Inside the native: player table dispatch first (P1), then U's table at depth 1: T1,T2,T3,T4 (registration order; P1 was registered between T2 and T3). Every action runs before `issue end`. Unit refs 23→24 during delivery. |
| 2 (15) | same | T1: registers T5 → `[..,T4.reg,<sentinel>,T5.reg]` (appended after the dispatcher sentinel, count stays 64, depth 1) and `DestroyTrigger(T3)` → release request only (T3 refs 3), **no unregister**. T2: `DestroyTrigger(T2)` on itself; its action continues; `GetTriggerEvalCount/ExecCount(GetTriggeringTrigger())` now read **0/0**. T3.reg is still visited, `CTriggerWar3_OnEvent` returns 0 (wrapper+20), T3's eval stays 1 (no marker). T4: `DestroyTrigger(T1)` (already delivered). T5 not delivered. Dispatch return 1. |
| after 2 | next primary-clock advance (Game_AdvancePrimaryClocks→SimClock_AdvanceRequests 054190→AgentWrapper_OnClockRequest 15e500) | wrapper destroy T3, T2, T1 (request order), each releases its reg (release requests at 296cca); reg wrappers destroyed T3.reg, T2.reg, T1.reg; each **unregisters at depth 0** (27773b→0728e0): refs 2→1, node unlinked, chain `[.., T4.reg, T5.reg]`, count unchanged 66 (other 0x8024c callbacks remain); destructors trigger then reg (refs 0). |
| 3 (20) | `IssuePointOrder(U)` | P1, T4 (eval 3), T5 (eval 1). |
| 10 (50) | `IssuePointOrder(G)`; G1 registers 21 distinct unit events (54..74) on trigger GX | count 64→85 at depth 1, buckets stay 16; G2 still delivered; at the outermost exit (071f78) growth 16→32 buckets (85 > 64); then count 87 after internal re-subscriptions. |
| 11 (53) | `IssuePointOrder(G)` | G1, G2 in original order after rehash. |

Reverse variant (registration T4,T3,P1,T2,T1): phase 1 delivers P1,T4,T3,T2,T1; phase 2 T4 destroys T1 (later
subscriber) → T1 suppressed in the same dispatch, so T5 is never registered and T3 survives; phase 3 delivers P1,T4,T3.

Internal (publicly reached) tombstones: a replacing move order makes U run its own `0xd0144` (stop task) dispatch at
depth 1, inside which CAbilityMove 600340 unregisters its completion subscriptions (566d0/566f0 → 0728e0, depth 1):
tick 15, 0x40190065: CAbilityMove refs 26→25, count 66→65, chain `[U, CAbilityAttack, CAbilityAttack, CAbilityMove]` →
last node becomes a tombstone; 0x40190066: refs 25→24, count 65→64. The tombstones stay linked (a repeated unregister
in the next d0144 pass is a no-op, refs 24→24) until a depth-0 scan or depth-1 pass of that bucket reclaims them.

### Original-code oracle (forced-state, labelled: supplied agents/callbacks/handler bodies; Storm alloc + CRT memset supplied)

Named 03.1 cases (four subscribers C0..C3 on event E=0x8024c, remaps 0xd0000+i; second dispatch shows the next pass):

| Case | First dispatch deliveries | Second dispatch | Final chain / refs |
|---|---|---|---|
| baseline-four | C0 C1 C2 C3 | same | 4 nodes, count 1, refs 2 each |
| insert-same-event-from-first / from-last | C0..C3 (C4 not delivered) | C0..C3 C4 | appended at tail |
| insert-other-event-same-bucket (F=0x80250) | C0..C3; count 1→2 | F: C4 only | |
| remove-later (C0 removes C2) | C0 C1 C3 | C0 C1 C3 | tombstone unlinked by the depth-1 pass; C2 refs 1 |
| remove-earlier (C2 removes C0) | C0..C3 | C1 C2 C3 | tombstone of C0 left behind the iterator, unlinked next pass |
| remove-self | C0..C3 | C0 C2 C3 | |
| remove-all-for-event (callback 0) | C0 C1 | none (ret 0) | count 0, chain empty after the next depth-1 pass |
| remove-then-reinsert-later | C0 C1 C3 | C0 C1 C3 C2(remap 0xd00f2) | re-registration is a new tail node |
| reregister-existing-updates-remap | C3 receives 0xd00ee already in this pass | same | no new node, refs unchanged |
| last-ref-callback-removed-during-dispatch (refs start 0) | C0, **destroy C2 inside unregister**, C1, C3 | C0 C1 C3 | no stale call into C2 |
| self-removal-last-ref-destroys-running-callback | C1 destroyed **while its own handler runs** | | dispatcher holds no callback ref |
| return-values-or | all return 0 → EAX 0; empty event G → 0 | | |
| growth-deferred-to-depth-zero | count 1→21 at depth 1, buckets 4 | | growth to 8 at outermost exit |
| growth-at-depth-zero-immediate | growth to 8 at the 17th distinct event | | |
| tombstones-survive-growth-are-freed | C3 tombstoned, freed by the rehash | | 23 nodes |

Random: 600 seeded scripts (seed 3031: up to 3 ops per handler call, nested dispatch to depth 3, clear-all, growth
threshold crossings) — 2,185 deliveries, 361 nested dispatches, 978 surviving tombstones, 230 callback destructions,
150 growths; 0 model mismatches, 0 faults.

### Rules (all A+O; 1–6 also L)

1. Delivery order = per-event registration (append) order within the agent's table; player-unit triggers are on the
   player table, which the order producer dispatches **before** the unit table.
2. Iterator: a dispatch delivers only nodes linked before its start (stack sentinel = tail). Registration during
   delivery is never delivered by the running pass; it is delivered by the next pass, including nested passes (03.2).
3. Re-registering the same (event, callback) changes the delivered remap immediately and adds no node.
4. Removal during delivery: callback ref released immediately, node tombstoned; a tombstone is skipped by every pass and
   unlinked only by a depth-1 pass over that bucket, a depth-0 register/unregister/has-subscriber scan, a depth-0 clear
   or a rehash.
5. Count = distinct events with a live callback; growth only at depth 0 (immediately on a depth-0 registration, or at the
   outermost dispatch exit if the count grew).
6. JASS `DestroyTrigger` is not an unsubscription: delivery is suppressed by `wrapper+20` from that moment; the
   registration leaves the unit table at the next primary-clock release drain at depth 0 (no tombstone).
7. The dispatcher pins the agent (refs+1) but not callbacks; destroying the agent from a subscriber is deferred to unwind.
8. Return value is the OR of handler results (CUnitEventReg returns 1 when its event matches; the trigger handler's
   result is not propagated).
9. (A only, live pending under ORDER-03.2) Unit_FireIssuedPointOrderEvent samples "has subscribers" for unit and player
   before both dispatches.

## Reproducer (main checkout root)

```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_ORDER-03.1_subscriber_dispatch.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report /tmp/order031.json --random 600 \
  --expected $R/ORDER-03.1/expected-ORDER-03.1.json        # exit 0; report sha 778c5fc3…2d63 (deterministic)
for v in forward reverse; do python3 tools/frida/research/order03_make_map.py \
  --base /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/Human02Interlude-original.w3m \
  --tool build/bin/mpqtool --scenario $v --output /tmp/RS-ORDER-03.1-$v.w3m; done
$R/_env/install-map.sh /tmp/RS-ORDER-03.1-forward.w3m /tmp/RS-ORDER-03.1-reverse.w3m
tools/frida/research/order03_runs.sh /tmp/o3caps forward-observe-1:observe:forward forward-control-1:control:forward \
  reverse-observe-1:observe:reverse reverse-control-1:control:reverse      # each run goes through _env/live.sh
python3 tools/frida/research/order03_analyze.py markers /tmp/o3caps/forward-observe-1.jsonl /tmp/o3caps/forward-control-1-rs-o3-forward.txt
python3 tools/frida/research/order03_analyze.py timeline /tmp/o3caps/forward-observe-1.jsonl --ticks 15
python3 tools/frida/research/order03_expected.py $R ORDER-03.1 --check     # "unchanged"
```

## Provenance

game.dll `d51e5680…8236` (both research copies). Base map `Human02Interlude-original.w3m` `199683be…2104`. Maps:
forward `0b891b79…5408` (j `e9bd03b9…856a`), reverse `11d58dd0…eb51` (j `877ea681…ee0`); probe `order03_probe.j`
`051bb467…dbd3`, builder `order03_make_map.py` `ff1a2a7d…0040` importing `make_wc3_pathfinding_map.py` `08656832…dbf`.
Observer `order03_observer.js` `1e048c04…c9e1c8`, launcher `order_trace.py` `beb82075…ba`, analyzer
`order03_analyze.py` (current) and expected builder `order03_expected.py`; oracle
`verify_ORDER-03.1_subscriber_dispatch.py` sha recorded in the report/expected (`script_sha256`). Frida 17.18.0.
No random seed in the live scene (no random draws); the oracle seed is 3031.

## Captures (`captures/`)

| Capture | Env | sha256 | Status |
|---|---|---|---|
| forward-observe-1.jsonl (+ rs-o3-forward.txt `881d0b7b…`) | C | `32842306…` | complete, 3425 rows, 208 markers, 0 errors |
| forward-observe-2.jsonl (+ `09e310a9…`) | B | `37a77a10…` | complete repeat; normalized rows equal run 1 except physical CUnit destructor timing |
| forward-control-1.jsonl (+ `5465d28b…`) | B | `0265eaae…` | observer-free control, 208 markers equal |
| reverse-observe-1.jsonl (+ `49bec9f9…`) | B | `23173492…` | complete, 202 markers |
| reverse-control-1.jsonl (+ `b2e147b6…`) | B | `440f4d6d…` | control, 202 markers equal |
| forward-control-2.log (empty) | — | — | **cancelled before launch** (queue contention), see runs-batch2.log; no process started |

## Controls and repeats

Observer vs observer-free JASS marker lists equal in all three observer runs (exact strings including handle ids,
eval/exec counts, order ids, points, life and current order). Forward observer repeated across environments C/B.
Excluded from the frozen rows: physical CUnit destruction (CUnit vt+4 from 6790f0, ~2.3 s after RemoveUnit) whose
position relative to the final JASS marker differed between the repeats (run 1 before, run 2 after).

## Public vs forced-state

Public: all live phases (CreateTrigger/TriggerRegisterUnitEvent/TriggerRegisterPlayerUnitEvent/TriggerAddAction/
DestroyTrigger/IssuePointOrder/ExecuteFunc/ConvertUnitEvent). Forced-state (labelled): every oracle case — supplied
agents, callback objects and handler bodies calling the original register/unregister/dispatch/clear; callback objects
with initial refs 0/1/2; supplied Storm allocation and memset.

## Exclusions (owning IDs)

Nested dispatch, destruction of units/orders from subscribers, payload lifetime, deferred release drain order →
ORDER-03.2 / ORDER-05.x. Trigger conditions/filters, TriggerSleepAction inside event actions, trigger queues
(TriggerExecute/QueuedTriggers) → not covered (propose under BASE-03.1 inventory). Physical CUnit destruction timing →
ORDER-04.2/BASE-03.1.

## Mismatches preserved

* `retail-pathfinding-movement.md` ("Movement subscriptions and internal event remapping"): "bucket count byte `+1`, and
  **entry count** ushort `+2`" → +2 counts **distinct events with a live callback**, not entries: U's count stays 64 while
  four regs share 0x8024c; 0728e0 decrements only when no other live callback of that event remains.
* Same section, "general reentrancy remains open": resolved here for insertion/removal; nesting in ORDER-03.2.
* OpenRealm (engine, not ledger): `DestroyTrigger` and `GetTriggerEvalCount/ExecCount` are stubs; event delivery is
  queued (`G_RunEvents`) instead of synchronous inside the producer; `G_MakeEvent` reuses the lowest free slot and
  `FOR_EACH_EVENT` iterates slots, which is not append order once slots are freed.

## Engine entry points

`games/warcraft-3/game/api/api_trigger.h` (`DestroyTrigger`, `GetTriggerEvalCount`, `GetTriggerExecCount`,
`TriggerRegisterUnitEvent`, `TriggerRegisterPlayerUnitEvent`), `games/warcraft-3/game/g_utils.c` (`G_MakeEvent`,
`G_AllocJassTrigger`), `games/warcraft-3/game/g_events.c` (`G_ExecuteEvent`, `G_RunEvents`), `games/warcraft-3/jass/jdo.c`
(`jass_calltriggerevent`/`jass_calltriggercontext`: actions queued), `games/warcraft-3/game/m_unit.c`
(`G_PublishIssuedPointOrder` publication point), `games/warcraft-3/game/g_local.h` (`FOR_EACH_EVENT`).

## Failing engine regressions (suggested)

1. **Public scene replay** (JASS test map with `order03_probe.j`; ticks 0–20 and 50–53): the emitted marker list must
   equal `expected.live.controls.forward` exactly (97 strings), e.g. tick 15: `enter trig=P1 … eval=2 exec=2`,
   `enter trig=T1 … eval=2`, `op register begin trig=T5`, …, `op destroy end trig=T3`, `exit trig=T1`, `enter trig=T2`,
   `op destroy end trig=T2`, `exit trig=T2 … eval=0 exec=0`, `enter trig=T4 … eval=2`, …, `issue end`; tick 20: P1
   (eval 3), T4 (eval 3), T5 (eval 1). Reverse registration: `expected.live.controls.reverse` (91 strings).
2. **Synchronous delivery**: `issue begin` … all `enter/exit` markers … `issue end` inside one `IssuePointOrder`.
3. **Destroyed trigger in the same dispatch**: no action, eval count unchanged; destroyed trigger never fires later.
4. **Growth/order stability**: 21 extra registrations during delivery leave later delivery order unchanged (phase 10/11).
5. Unit-level C test for the subscription container if one is introduced: the 27 named oracle cases (inputs and full
   expected logs/chains are in `oracle.named_results`).

## Artifacts

`HANDOFF.md`, `expected-ORDER-03.1.json`, `oracle-report-ORDER-03.1.json` (+stdout/stderr), `types-ORDER-03.1.json`,
`mapping-rows-ORDER-03.1.txt` (32 rows = gw.py writes, `ghidra-writes.jsonl`), `proposed-docs-ORDER-03.1.md`, `maps/`,
`captures/`, `asm/0725b0.txt`. Repository (new files): `tools/ghidra/research/verify_ORDER-03.1_subscriber_dispatch.py`,
`tools/frida/research/order03_{probe.j,make_map.py,observer.js,analyze.py,expected.py,runs.sh}`, `order_trace.py`,
`order03b_{probe.j,make_map.py,runs.sh}`.

Proposed new IDs: **ORDER-03.3** synchronous JASS event delivery inside producers; **ORDER-03.4** DestroyTrigger
deferred release + eval/exec counters (text in `proposed-docs-ORDER-03.1.md`).

## Engine integration — Payoff177

ORDER-03.1 is integrated through synchronous issued-order publication, derived
owner/event chains with saved append ranks, insertion cutoffs, pending-destroy
guards and primary-clock cleanup. Seventeen native/registration/save/scaling
regressions reproduce the bounded public decisions; a fresh original oracle with 627 cases and a complete archived capture/control rebuild provide independent
evidence. See [engine contract](../../retail-pathfinding-engine.md#synchronous-issued-order-subscribers-and-deferred-trigger-cleanup-payoff177).
The proposed new IDs above were not adopted. Nested Stop/death/removal and full
unit/order destruction composition remain the existing ORDER-03.2 task.
