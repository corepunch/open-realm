<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/ORDER-01.18/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **ORDER-01.18**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ORDER-01.18/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-ORDER-01.18.json` -> [`ORDER-01.18-expected.json.gz`](../../../../../tools/ghidra/fixtures/research/ORDER-01.18-expected.json.gz) (uncompressed sha256 `b619ea6c63fe6776d6e597a04e361ce480cd0867fdaf889b22aeb86fe5a26fbb`, 2064542 bytes)

# ORDER-01.18 handoff — Patrol combat composition, endpoint progression, threshold and queued policy

**Status.** Builds on Payoff124 (owner: issued 851990 → active 851991, 5fdff0/5fff70/690fa0/692010 ABIs, 1,707-record
combat/loss/nested/blocked public captures, Shift-queue origin witnesses) without repeating it. New here:
(1) **A + L** issued-Patrol expansion threshold: `5fe1a0` expands only when dist²(unit, point) ≥ 10000.0
(`6fd6fb28`, init `6f013530`); 60 and 99.99 units → public head 0 at issue (native returns true), exactly 100 → 851991;
(2) **L** complete ordered internal decisions of Patrol automatic combat: acquisition prepends a combat sub-chain
ahead of the retained d016b leg, 851991 never changes, KillUnit of the enemy resumes **the same leg** toward the
original endpoint (no return-to-acquisition task, unlike idle units), d0175 appends the return at arrival;
(3) **A + L** queued policy `5fcc70 Move_HasQueuedNonPatrolOrder`: a queued Move behind an active leg runs when the leg
arrives (the internally appended return is queued behind it); a queued Patrol that activates while a non-Patrol order
is queued behind it expands (origin = position at activation) and immediately re-appends itself behind the queue
(rotation), producing round-robin legs of two patrol routes; (4) **A** blocked-route policy: `5fb190` has no
Patrol case, so a cant-path leg uses the generic fallback (pop, action0, d0144×2, 5fa7a0 arrival) and its d0175 still
appends the reversed order (consistent with the owner's blocked-endpoint capture). **Not established:** live
save/load of queued continuations; unit-target Patrol (5fb110 → 5fd270 target tasks); Move-disabled gates
(5fb040) during Patrol; repeat of the two Shift-queue witnesses with identical input landing (each is a single
owned run; the landing tick is wall-clock dependent); numerical leg timing.

## Functions

| VA | Name | ABI (assembly) | Role / evidence |
|---|---|---|---|
| 6f5fe1a0 | Move_CreateAttackMoveTasks (**misnamed**, issued-Patrol d0016 expansion; comment appended) | thiscall ECX Move, stack4 event | `5fb110` unit target → `5fd270`; else dist² of current unit position to order48/50 compared with `[6fd6fb28]` (`5fe2e4 movss; comiss; ja skip`) → action7, `690fa0(ECX=d0017, EDX=owner, point, origin=current position)`, `691c70(order,1,0)` (A, L) |
| 6f5fcc70 | Move_HasQueuedNonPatrolOrder (new) | fastcall ECX unit, RET, EAX 0/1 | 67e650 iteration with callback 5fccc0: skip head; any command ∉ {d0016,d0017} → 1 (A); L rotation below |
| 6f5fdff0 | Move_CreatePatrolTasks (existing) | — | `5fb040(0,1)` gate; 5fcc70==0 → leg tasks; else action0 + `690fa0` same endpoints + `693490` append (return 5fe0b8) (A, L) |
| 6f5fff70 | Move_AppendPatrolContinuation (existing) | — | `5fb040(0,1)` gate; swapped order appended at the tail (return 5fffe8) (A, L) |
| 6f5fb190 | Move_RecoverCantPath (existing) | — | head-command switch only d0003/d004b/d004e/d0050 → Patrol legs take the generic fallback (A) |
| 6fd6fb28 | Patrol_MinimumDistanceSquared (label) | WC3PathScalar 10000.0 | `6f013530: mov edx,2710; mov ecx,6fd6fb28; jmp 070d80` (A) |

## Behaviour (frozen: `expected-ORDER-01.18.json`, sha256 `b619ea6c63fe6776d6e597a04e361ce480cd0867fdaf889b22aeb86fe5a26fbb`)

Scene p2 (`RS-ORDER-01.18-p2`, two observed repeats + observer-free control, public/words/decisions identical);
scenes p1r1/p1r2 (`RS-ORDER-01.18-p1`, single observed runs with genuine Shift input; public records of cases 0–3, 5
identical between the runs). Subjects Player(0) `hfoo` at (272,y); enemy Player(1) paused `hfoo`.

| Case | Input | Public head | Ordered decisions / facts |
|---|---|---|---|
| p2/1 | patrol (332,520) — 60 units | 0 at issue (native true) | admit 680320 result ffffffff, count 0; Move d0016 creates no task and no 851991 |
| p2/3 | patrol (371.99,680) — word 1136262840, 99.98999 units | 0 at issue | identical to case 1 |
| p2/2 | patrol (372,600) — word 1136263168, 100 units | 851991 | factory 851991 point (372,600) origin (272,600); continuations every 4–5 ticks (88 in 400) |
| p2/0 | patrol (1500,300) through enemy (900,300); KillUnit enemy (80) | 851991 throughout | t1: HEAD 851990 → M d0016 → action7 → HEAD 851991 [(1500,300),(272,300)] → M d0017 → prepend `d0162(0) d0166(d0012) d0148 d014a d0175 d016b d0148 d014e d0178 d0144 d0162(7)`; t22 acquisition prepends `d0162(7) d0148 d016a d016f d0163 d0168 d0162(1)` (head unchanged); hits 27/41/54/68 with source head 851991; t80 `A:d01a4 A:d016a A:d0148 M:d016b` (resume same leg, no wait because no swing was active); t106 `M:d0196 M:d0175 APPEND(851991,(272,300),(1500,300),5fffe8, c1→2)` → head pops → next leg; continuations 106/152/198/243/289/336/382 |
| p1r1/4 | patrol (1500,1280) speed 100; Shift+M (landed 39 and 78) | 851991 → 83: 851986 → 94: 851991 | at leg arrival 83: `APPEND(851991 return,5fffe8, c3→4)` then HEAD 851986 (first queued Move, 83–93; second identical Move 93–94) — the queued orders run before the return; then HEAD 851991 return (272,1280) |
| p1r2/4 | same; Shift+P (landed 27, 851990 to (1342.654,1213.903)) then Shift+M (landed 42) | 851991 → 83: 851986 → 93: 851991 | tick 83: `APPEND(851991 return,5fffe8, c3→4)`, HEAD 851990 → M d0016 → action7 → factory 851991 point [1151849708,1150794981] origin [1153031522,1151336496]=(1486.918,1280.006) = position at activation (5fe326) → HEAD 851991 → M d0017 → action0 → factory same endpoints (5fe0b0) → `APPEND(…,5fe0b8, c3→4)` → HEAD 851986 (Move). Afterwards the two routes alternate legs: heads at 165 (rotated route), 236 (original), 248, 249, 330 … each leg's d0175 appends its own return at the tail |

## Reproducer

```sh
F=/run/media/lofcz/ssd_external/GitHub/open-realm/tools/frida/research; R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
python3 $F/order0110_make_map.py --probe order0118_probe.j --terrain patrol_queue --variant p --base $R/../runtime/Human02Interlude-original.w3m --output $R/ORDER-01.18/maps/RS-ORDER-01.18-p1.w3m
python3 $F/order0110_make_map.py --probe order0118_probe_b.j --terrain patrol_queue --variant p --base $R/../runtime/Human02Interlude-original.w3m --output $R/ORDER-01.18/maps/RS-ORDER-01.18-p2.w3m
$R/_env/install-map.sh $R/ORDER-01.18/maps/RS-ORDER-01.18-p?.w3m
$R/ORDER-01.18/run-batch.sh 1 2          # p1 observe (with --input 3/6/20 Shift+P/M/M via helper)
$R/ORDER-01.18/run-p2.sh p 1; $R/ORDER-01.18/run-p2.sh p 2; $R/ORDER-01.18/run-p2.sh pc 1   # p2 observe x2 + control
C=$R/ORDER-01.18/captures
python3 $F/order0110_summarize.py --capture $C/RS-ORDER-01.18-p2-observe-1-envB.jsonl --capture $C/RS-ORDER-01.18-p2-observe-2-envC.jsonl --control $C/RS-ORDER-01.18-p2-control-1-envC-preload.txt --output $R/ORDER-01.18/summary-p2.json
python3 $F/order0110_summarize.py --capture $C/RS-ORDER-01.18-p1-observe-1-envC.jsonl --output $R/ORDER-01.18/summary-p1r1.json
python3 $F/order0110_summarize.py --capture $C/RS-ORDER-01.18-p1-observe-2-envC.jsonl --output $R/ORDER-01.18/summary-p1r2.json
python3 $F/order0110_expected.py --task ORDER-01.18 --scene p2=$R/ORDER-01.18/summary-p2.json --scene p1r1=$R/ORDER-01.18/summary-p1r1.json --scene p1r2=$R/ORDER-01.18/summary-p1r2.json --check $R/ORDER-01.18/expected-ORDER-01.18.json
python3 $F/order0118_verify.py --expected $R/ORDER-01.18/expected-ORDER-01.18.json --report $R/ORDER-01.18/verify-ORDER-01.18.json
```
`verify-ORDER-01.18.json`: all claims pass; 5/5 negative controls rejected (99.99 expanding, combat changing the head,
missing acquisition, missing rotation append, control mismatch).

## Provenance (sha256 prefixes)
Binary `d51e5680…8236`. Probes `order0118_probe.j` `5009fbb2…` (p1: `GetLocalPlayer()`, `SetCameraBounds(-1024…3072)`),
`order0118_probe_b.j` `a0e74e0f…` (p2: `Player(0)`, map bounds); maps p1 `1858efb7…`, p2 `7a39056f…`; observer,
capture, summarizer, expected, helper as ORDER-01.10; verifier `order0118_verify.py` `03c18619…`.

## Captures
| Capture | Status | jsonl / preload |
|---|---|---|
| p1-observe-1-envC | complete; cases 0–3,5 public identical to run 2; inputs landed 39/78 on case 4 (both Move); case 0 never acquired (artifact) | a7ce0b32 / caca36d8 |
| p1-observe-2-envC | complete; inputs landed 27 (Patrol) / 42 (Move) on case 4 → rotation witness | 02e6c069 / 898af55a |
| p2-observe-1-envB, -2-envC | complete; identical public, words, decisions; Shift inputs produced no queued orders (failed inputs) | a3fdd805 / 5d7afc9a; b5e9164e / 2a08cd2c |
| p2-control-1-envC (no attach, no input) | complete; 2,421 markers identical | – / 5fabeefb |

## Public vs forced
Public JASS natives and genuine owned SendInput only; no memory writes; observer read-only. p2's control has no
input, and its public stream equals the two observed runs (the failed inputs had no effect).

## Exclusions
Live save/load of a Patrol with queued continuation/rotated orders — proposed **ORDER-01.21**; unit-target Patrol
(5fb110/5fd270 follow tasks); Move-disabled gates (5fb040(0,1): Move7c>0, or a 48cb80 'BUsl'/515270 buff state → neither leg tasks nor
continuation); numerical leg/arrival timing and approach geometry (MOVE/ROUTE scopes); blocked live witness
(owner Payoff124 fixture retains it).

## Mismatches preserved
1. `6f5fe1a0` is named `Move_CreateAttackMoveTasks`; it is the issued-Patrol expansion (see ORDER-01.10 Mismatch 1).
2. **Probe artifact:** with `GetLocalPlayer()` + `SetCameraBounds(-1024,…,3072)` (p1, and ORDER-01.10 q1) Patrol and
   Attack Move never acquire the paused enemy; p2/q2 without them acquire normally. Not a Patrol rule; exact cause
   (bounds vs. local player) not isolated.
3. **Input attribution:** UI orders landed 15–19 probe ticks after the helper call and therefore on the unit selected
   at landing time (case 4), not on case 5; case 5's rotation scenario failed in both p1 runs. The two p1 runs thus
   differ in case 4 only, each a valid single-run witness. p2's Shift clicks produced no orders.
4. Engine (source reading): `s_patrol.c:ai_patrol_walk` reverses in place by swapping `patrol_target` and
   `S_IssuePatrolOrder` has no distance threshold; queued FIFO orders start only from `unit_stand`
   (`G_UnitStartNextQueuedOrder`), which a patrolling unit never reaches — inference: a Shift-queued order behind
   Patrol never runs in OpenRealm, while retail runs it at the end of the current leg.

## Engine entry points and failing-regression expectations
* `skills/s_patrol.c` (`S_IssuePatrolOrder`, `order_patrol`, `ai_patrol_walk`, `order_patrol_resume`), `m_unit.c`
  (`unit_issueorder_now` "patrol", `G_UnitStartNextQueuedOrder`, queue FIFO), `skills/s_attack.c`
  (`attack_finish_after_combat` → `order_patrol_resume`).
* **R1 threshold:** unit at (272,y): `IssuePointOrder(u,"patrol",332,y)` and `(371.99,y)` → true, `GetUnitCurrentOrder`=0
  immediately and next frames, no movement; `(372,y)` → 851991 and reversals at both endpoints.
* **R2 queued Move behind an active leg:** Patrol (272→1500), Shift Move queued during leg 1 → head 851991 until arrival
  at 1500, then 851986 until the Move arrives, then 851991 toward 272 (swapped endpoints) and patrol continues.
* **R3 rotation:** active leg + queued Patrol P2 + queued Move: at the leg end the return is appended; P2 activates,
  takes its origin from the activation position, immediately re-appends itself behind the Move; Move runs (851986);
  afterwards legs of the two routes alternate (head 851991 throughout).
* **R4 combat:** acquisition while patrolling keeps 851991; damage-source current order 851991; KillUnit of the enemy
  keeps 851991 and resumes the same leg toward (1500,300); the return is appended only at that arrival.
* **R5 blocked:** a cant-path leg ends like an arrival and appends the reversed 851991 (no special Patrol recovery).

## Proposed new IDs
**ORDER-01.21** live save/load of Patrol with queued continuation and rotated orders (round-robin state).

Files: `expected-ORDER-01.18.json`, `verify-ORDER-01.18.json`, `types-ORDER-01.18.json` (method 5fcc70, global
6fd6fb28), `mapping-rows-ORDER-01.18.txt`, `proposed-docs-ORDER-01.18.md`, `ghidra-writes.jsonl`, `summary-*.json`,
`captures/`, `maps/`, `run-batch.sh`, `run-p2.sh`. Repository: `tools/frida/research/order0118_{probe.j,probe_b.j,verify.py}`
plus the shared ORDER-01.10 tooling.
