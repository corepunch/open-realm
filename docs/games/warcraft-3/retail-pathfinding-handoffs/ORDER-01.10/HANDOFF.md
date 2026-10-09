<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/ORDER-01.10/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **ORDER-01.10**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ORDER-01.10/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-ORDER-01.10.json` -> [`ORDER-01.10-expected.json`](../../../../../tools/ghidra/fixtures/research/ORDER-01.10-expected.json) (uncompressed sha256 `e6fecc75b2cb04f2df2630eeff9e0b21e21207a6d246804c2c5ab31a000d8b2d`, 307363 bytes)

# ORDER-01.10 handoff — public Attack / Attack Move / Attack Ground ownership

**Status.** Live-verified (**L**: two owned observed repeats + one observer-free JASS control per scene, all public
records identical) and assembly-mapped (**A**) for: public head identity of Attack (851983) on a target, Attack Move
(851983 point), Attack Ground (851984), attackonce (851985); approach → attack/cooldown → completion; automatic combat
sub-behaviors (acquisition during Attack Move, idle acquisition, combat after Stop/attackonce completion) that never
change the public head; target loss by KillUnit/RemoveUnit (deferred retirement); replacement by Move and by a new
Attack; the public target→point conversion for unattackable targets (air for Footman, invulnerable, self); subject
death and recreated-handle reuse; queued handoff through genuine Shift input (Move→Attack Move, Attack Move→Move).
The complete ordered internal decisions (Attack/Move dispatch codes, every internal task prepend, user-order
admission/append/head dispatch) are frozen per case and repeat exactly. **Not established:** a publicly reachable
*rejection* of a public Attack while one is active (every attack-target variant tried is accepted, see below; generic
unsupported-order rejection is already ORDER-01.9/01.17 evidence); live save/load of an active/queued Attack (no UI
save captured); AI-owned completion (Unit5c bit4 → d0006 append, static only); numerical combat timing parity
(cooldown/backswing/approach distances are recorded, not certified as engine numerics); acquisition-range geometry.
One queue-scene acquisition anomaly is preserved unexplained (see Mismatches).

## Functions (Ghidra names written via gw.py, `ghidra-writes.jsonl`)

| VA | Name | ABI (assembly) | Role / evidence |
|---|---|---|---|
| 6f49a5f0 | CAbilityAttack_Dispatch | thiscall ECX ability, stack4 event, RET4, EAX 1/0 | CAbilityAttack vtable 6fadb5a4 slot 0c (RTTI `.?AVCAbilityAttack@@`). Switch on event+8: d0002/d0003/d000f→49a980, d0010→49af00, d0011→49b0f0, d0144→49ed50, d0166→49e990, d0168→49e510, d016a→49e6b0, d0163→49ea80, d01c1→493ff0, plus swing events d01b0/1b1/1b2/1bd/1a4/1ad (A, L) |
| 6f49a980 | Attack_CreateTasksFromOrder | thiscall ECX ability, stack4 event, RET4 | Unit-target and point (Attack Move) chains; forwards unattackable targets to Move only after 39a5a0(d0003) admits (A); live chains below (L) |
| 6f49af00 | Attack_CreateAttackGroundTasks | thiscall ECX ability, stack4 event, RET4 | d0010; sets ability20 bit1000; chain incl. d0169 ground task (A, L) |
| 6f49b0f0 | Attack_CreateAttackOnceTasks | ECX ability, stack4 event; `OR [ecx+20],10000` then `jmp 49a980` (RET4 there) | d0011 (A, L) |
| 6f49e990 | Attack_HandleOrderCompleteTask | thiscall, stack4 event, RET4 | d0166 consumed last; task34==d000f and ability3c<=0 and Unit5c bit4 → `690930(ECX=d0006, EDX=owner, stack target 0)` + 693490 append (A; never reached for user-owned subjects, L-negative) |
| 6f49ed50 | Attack_HandleCleanupTask | thiscall, stack4 event, RET4 | d0144 cleanup unless task34==d000f (A) |
| 6f49e510 | Attack_HandleTargetTask | thiscall, stack4 event, RET4 | d0168: target identity → ability6c/70, slot 496b80 → ability2b8, ability20\|=4 (A) |
| 6f49e6b0 | Attack_HandleTimerWaitTask | thiscall ECX ability, no stack, RET | d016a: if ability200 timer remaining >0 set bit800 and clear Unit5c bit0 → the task queue (and the public head) waits for the swing event d01b2 (A, L) |
| 6f49ea80 | Attack_HandleReactionDelayTask | thiscall ECX ability, no stack, RET | d0163: "ReactionDelay" timer → d01af (A, L: d01af +2 ticks) |
| 6f493ff0 | Attack_OnAcquireScanTimer | thiscall ECX ability, no stack, RET | d01c1 periodic (every 5 probe ticks), acquisition prepends a sub-chain (A, L) |
| 6f207160 | Jass_DispatchTargetOrder | cdecl: 4 unit, 8 player byte, c 0, 10 order id, 14 target; EAX 1/0 | Shared target-order native dispatch (caller 206e94). Invalid target + order flag2 → 69dd60 target position → point order (Unit_CreatePointOrder) admitted with 680320 mode1 (A, L) |
| 6f690930 | OrderImmediate_Create | fastcall ECX command, EDX owner, stack4 target, RET4 | 684a70 immediate order (A; live Stop 851972 from native) |
| 6f5fe1a0 | Move_CreateAttackMoveTasks (**name wrong**, comment appended) | thiscall ECX Move, stack4 event | It is the issued Patrol d0016 expansion; see Mismatches and ORDER-01.18 |

Previously named and used: 680320 Unit_AdmitOrder (mode1 = replace, dispatch1), 693490 Unit_AppendUserOrder,
67abe0 Unit_DispatchUserOrderHead, 679cc0 Unit_DispatchAbilityOwnedOrder, 691e60 Unit_PrependInternalTask,
67df00 Unit_DispatchInternalTaskQueue, 5fda10 CAbilityMove_Dispatch, 2039d0 Jass_GetUnitCurrentOrder.

## Fields (`types-ORDER-01.10.json`, fills undefined bytes of the existing 720-byte WC3AttackRangePrefix)

| Struct+offset | Meaning | Encoding | Write sites | Read sites |
|---|---|---|---|---|
| WC3AttackRangePrefix+30 | owning unit | ptr:WC3UnitOrdersPrefix | constructor (not traced) | 49ea0c, 49ed6x, 493ff0; live: equals the unit bound by 2039dc for every subject |
| WC3AttackRangePrefix+6c | task_target | WC3PathIdentity (-1/-1 none) | 49e510 (d0168) | 493ff0 rearm period choice |
| WC3UnitOrdersPrefix+19c/+1b4 | user head / count (existing) | identity / u32 | 680320, 693490, head pop | 2039d0 (public), observer |

Methods with explicit ABIs: 12 entries in `types-ORDER-01.10.json` (all from assembly; RET sizes listed above).

## Public identity vs internal tasks

* The public `GetUnitCurrentOrder` value is always `order+24` of the **user** head (Unit19c). Internal tasks
  (Unit174 chain: action d0162, event, point d016b/d016c, target d016f/d0168, order-parameter d0166/d0144 carrying
  d000f/d0012) never become the public value. Attack Ground and attackonce create order-parameter tasks with
  **d000f** (not d0010/d0011); only the user head carries 851984/851985.
* Automatic acquisition (Attack Move, idle units, after Stop, after attackonce completion) prepends
  `action1, d0168(target), d0163, d016f(target), d016a, d0148, action7` (creation order; idle units add
  `action0, d014b, d016c(own position)` first) **without** touching the user head.

## Behaviour (frozen: `expected-ORDER-01.10.json`, sha256 `e6fecc75b2cb04f2df2630eeff9e0b21e21207a6d246804c2c5ab31a000d8b2d`)

Probe ticks are 0.1 s public timer callbacks; "tick N" in public rows = value read inside callback N. Subjects are
Player(0) `hfoo` (case 2: `hmtm`), acquire range 128, at (272, y); enemies Player(1) paused `hfoo` (case 3 `hgry`).
Rows y=300/960/1620 (660 apart; the 160-unit layout of the exploratory capture cross-acquired, see Captures).

| Scene/case | Public input (tick) | Public head transitions (tick: order) | Key retail facts |
|---|---|---|---|
| v3a/0 Attack target | attack enemy@(1008,300) (1); KillUnit(enemy) (60) | 0: 0 → 1: 851983 → 62: 0 | approach stops at (863.273,303.809); hits at 29/42/56 (period 13–14); **kill_after = 851983**, retire at 62 after d016a wait |
| v3a/1 Attack Move through enemy | attack point (1728,960) (1); KillUnit(enemy) (60) | 1: 851983 → 94: 0 | acquisition at 24 keeps 851983; hits 29/43/56; resume after kill, arrival (1716.141,960.810) at 94 |
| v3a/3 Attack on air (Footman→`hgry`) | attack target (1) | 1: 851983 → 28: 0 | admitted as **point** order 851983 at target position words [1148977152,1154121728]=(1008,1620); attack-move arrival at 28 |
| v3b/2 Attack Ground (Mortar) | attackground (1700,300) (1); Stop (150) | 1: 851984 → 150: 0 | in range at 12 (555.228,312.295); ground swings with damage to the splash enemy at 35/70/105/140 (source head 851984) |
| v3b/4 replacement | attack (1); attack invulnerable unit (12); move (1500,960) (25); attack (45); KillUnit (90) | 1: 851983 → 25: 851986 → 45: 851983 → 94: 0 | tick 12 is **accepted** (true) and becomes point 851983 to (1500,1290) [1153138688,1151418368]; every replacement is 680320 mode1, count 1→1, with d0144 cleanup dispatched to Move+Attack before the new chain; kill_after 851983 |
| v3b/9 RemoveUnit target | attack (1); RemoveUnit(enemy) (45) | 1: 851983 → 48: 0 | **remove_after = 851983**; d01a4 at 45, retire at 48 after d01b2 |
| v3c/5 subject death/reuse | attack (1); KillUnit(subject) (40); CreateUnit same spot (42) | 1: 851983 → 40: 0; new unit 0 | death retires synchronously (Unit_QueueRemovalTasks d015a/159/158/154/155/153/178/144/action4) |
| v3c/6 idle acquisition | none; enemy created adjacent (10); KillUnit (60) | 0 throughout | acquisition at 14; hits 19/32/46/59 with source head 0; after kill returns via d016c |
| v3c/7 plain Attack Move | attack point (1728,1620) (1) | 1: 851983 → 55: 0 | arrival (1712.696,1620.617) |
| v3d/8 attackonce | attackonce target (1) | 1: 851985 → 35: 0 | first hit 29 with head 851985; d0166 at 35 then idle acquisition; later hits head 0 |
| v3d/10 Footman Attack Ground | attackground (1500,960) (1); Stop (150) | 1: 851984 → 150: 0 | **accepted**; approaches to (1414.047,959.992), d0169 at 44, no damage, head 851984 persists |
| v3d/11 Stop during combat | attack (1); Stop (40) | 1: 851983 → 40: 0 | Stop 851972 head dispatch completes in the call (count 1→0); acquisition at 41; combat continues with head 0 |
| v3d/12 Attack self | attack self (1); move (20); attack self while moving (22) | 1: 851983 → 2: 0 → 20: 851986 → 22: 851983 → 23: 0 | target self → point order at own position [1132986368,1151418368] → arrival next tick |
| q1/0 queued Attack Move | move (1008,320) (1); Shift+A click (helper) | 1: 851986 → 50: 851983 → 77: 0 | append via 6b954a count 1→2; head dispatch at Move arrival (caller 67def0); arrival near clicked (606.654,237.903) |
| q1/1 queued Move behind Attack Move | attack point (1008,1280) (151); Shift+M click; KillUnit (200) | 151: 851983 → 202: 851986 → 225: 0 | queued Move activates at Attack Move arrival; **no acquisition of the paused enemy** (see Mismatches) |

Complete ordered decisions (verbatim in the expected JSON, `decisions[case]`). Attack-target issue tick:
`HEAD(851983)` → Attack d000f → prepend `d0162(0) d0166(d000f) d0148 d014a d016a d016f d0163 d0168 d014c d014e d0162(1)
d0178 d0144(d000f)` → consume `d0144(Move,Attack) d0178(Move) d014e d014c d0168 d0163` → wait; +2 ticks `d01af` →
Move `d016f` approach; in range `d0196`; swing cycle `d01b1 → d01b2(+5) → d01b0(+3) → d01b1(+5)…`; target loss
`d01a4 → +d016c, Move d016c, Attack d016a (wait)`; next `d01b2 → d014a d0148 d0166(Move,Attack)` → head pops.
Attack Move: prepend `d0162(0) d0166(d000f) d014d d014a d016b(point) d0149 d0148 d014e d0162(7) d0178 d0144(d000f)`;
arrival `Move d0196 → d014a d014d d0166`.

## Reproducer

```sh
F=/run/media/lofcz/ssd_external/GitHub/open-realm/tools/frida/research; R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
for v in a b c d; do python3 $F/order0110_make_map.py --probe order0110_probe.j --variant $v \
  --base $R/../runtime/Human02Interlude-original.w3m --output $R/ORDER-01.10/maps/RS-ORDER-01.10-v3$v.w3m; done
python3 $F/order0110_make_map.py --probe order0110_queue_probe.j --terrain patrol_queue --variant q \
  --base $R/../runtime/Human02Interlude-original.w3m --output $R/ORDER-01.10/maps/RS-ORDER-01.10-q1.w3m
$R/_env/install-map.sh $R/ORDER-01.10/maps/RS-ORDER-01.10-v3?.w3m $R/ORDER-01.10/maps/RS-ORDER-01.10-q1.w3m
(cd $R/ORDER-01.10/helper && winegcc -o order0110_ui_input.exe $F/order0110_ui_input.c)
$R/ORDER-01.10/run-batch.sh x; $R/ORDER-01.10/run-batch.sh y; $R/ORDER-01.10/run-batch.sh z   # observe 1, observe 2, controls (live.sh B/C)
C=$R/ORDER-01.10/captures; for v in a b c d; do python3 $F/order0110_summarize.py \
  --capture $C/RS-ORDER-01.10-v3$v-observe-1-env?.jsonl --capture $C/RS-ORDER-01.10-v3$v-observe-2-env?.jsonl \
  --control $C/RS-ORDER-01.10-v3$v-control-1-env?-preload.txt --output $R/ORDER-01.10/summary-v3$v.json; done
python3 $F/order0110_summarize.py --capture $C/RS-ORDER-01.10-q1-observe-1-envB.jsonl --capture $C/RS-ORDER-01.10-q1-observe-2-envB.jsonl --output $R/ORDER-01.10/summary-q1.json
python3 $F/order0110_expected.py --task ORDER-01.10 --scene v3a=$R/ORDER-01.10/summary-v3a.json ... --scene q1=$R/ORDER-01.10/summary-q1.json --check $R/ORDER-01.10/expected-ORDER-01.10.json
python3 $F/order0110_verify.py --expected $R/ORDER-01.10/expected-ORDER-01.10.json --report $R/ORDER-01.10/verify-ORDER-01.10.json
```
(`run-batch.sh` wraps `live.sh … order0110_capture.py --mode observe|control --seconds 190`; q1 adds
`--input 5 700 330 attack shift --input 155 700 330 move shift --input-helper helper/order0110_ui_input.exe`.)
`verify-ORDER-01.10.json`: 15 claims pass, 6/6 negative controls rejected (mutated order word, retire tick,
dropped transition, identity flag, damage source head, head point).

## Provenance (sha256 prefixes; full values via `sha256sum`)

Binary `d51e5680…8236` (live installs B/C hash-checked by the capture script). Base map
`runtime/Human02Interlude-original.w3m`; builder `make_wc3_pathfinding_map.py` `08656832…` (unmodified, wrapped by
`order0110_make_map.py` `85aa8cb0…`, terrain scenario `patrol_lifetime` / `patrol_queue`). Probe `order0110_probe.j`
`87471eef…`, queue probe `order0110_queue_probe.j` `1283e9bf…`, observer `order0110_observer.js` `691859d9…`,
summarizer `64c8b61e…`, expected `d88e0961…`, verifier `298d9fe6…`, helper source `order0110_ui_input.c` `4928162d…`
(copy of runtime/payoff124/wc3_ui_input.c + `attack`/A key), helper .so `64497701…`. Maps: v3a `c8639546…`, v3b
`0387071a…`, v3c `5070f56d…`, v3d `e1cf55c2…`, q1 `56da3099…`. Capture metadata records the capture-script hash at
run time (`order0110_capture.py` later gained the PROBES list/WINEPREFIX selection only; current `457faf80…`).
No seed dependence: no random natives; repeats compare exact words.

## Captures (all in `captures/`)

| Capture | Status | jsonl / preload sha256 prefix |
|---|---|---|
| o110-a-observe-1-envB | **failed**: harness sent no post-start loading key, scenario never ticked | 6713d470 / – |
| o110-a-observe-2-envB | **failed**: observer TypeError (read-only `this.depth`) | 8a374764 / – |
| o110-a-observe-3-envB | complete, **exploratory/contaminated**: 10 lanes 160 apart; Attack Move and the forwarded attack-air unit acquired neighbouring-lane enemies; footman `attackground` accepted | 9efffca6 / 80bcaace |
| o110-v2b/-v2d-observe-1-envB | complete, superseded probe (attack-self in v2b, post-completion damage rows); used only to design v3 | e5718051 / 28dd90d0 |
| o110-v2a/-v2c | **no capture** (live.sh wait exceeded the 420 s timeout) | – |
| RS-…-v3a/b/c/d-observe-1-envB | complete | d717d941 / 774d3765 / 6accc1c7 / 9066a4c0 |
| RS-…-v3a/b/c-observe-2-envC, v3d-observe-2-envB | complete; public, words and decisions identical to -1 | fc9a3a3d / 381125c7 / ba9f1053 / c4fbf890 |
| RS-…-v3a/b/c/d-control-1-envB (no attach) | complete; 619/624/618/839 markers identical to both observed runs | preload 333dcf99 / 55eeb194 / a165358e / c18ff5e9 |
| RS-…-q1-observe-1/2-envB | complete; public identical; decisions identical except Shift-input landing ticks (22/180 vs 16/168) and the queue count that follows | 0727ea6d / 661703be |

## Observer controls and public vs forced
All inputs are public JASS natives (CreateUnit, IssueTarget/Point/ImmediateOrder, KillUnit, RemoveUnit, PauseUnit,
SetUnitInvulnerable, SetUnitAcquireRange, SetPlayerAlliance/Controller) and genuine owned Win32 SendInput (Shift+A,
Shift+M) to the owned window. No game memory is written; the observer only reads. Controls: the observer-free
Preload record stream equals both observed streams for every v3 scene. The queue scene has no observer-free control
(input is triggered from observed ticks). Note: `SetUnitState(UNIT_STATE_MAX_LIFE,…)` has no effect (life stays 420).

## Exclusions (owning IDs)
Live save/load of an active or queued Attack/Attack Move/Attack Ground — proposed **ORDER-01.19**; AI-owned
completion d0006 append and Unit5c bit4 producers — proposed **ORDER-01.20**; acquisition target selection/range
and idle guard-return geometry (d016c) — existing GROUP-03/TARGET scope; combat numerics (approach stop distance,
cooldown 13–14 ticks, mortar 35 ticks) — not certified here; Smart right-click on enemies (d0003 into 49a980),
destructable/item targets (6849c0 branch, d0161/d016e), spells, Hold/Patrol composition (ORDER-01.7/01.18).

## Mismatches preserved
1. **Ghidra/ledger claim corrected.** Ghidra `6f5fe1a0 Move_CreateAttackMoveTasks` with plate "d0016 dispatch … may
   create internal attack/alternate order690fa0" and the ledger owner-inventory row "ORDER-01.10 … Shared dispatch
   and Move d0016 at5fe1a0" and TODO text "start with shared order dispatch and Move5fe1a0": d0016 is issued Patrol
   (851990); 5fe1a0 is the Patrol→851991 expansion. Attack Move is CAbilityAttack d000f point form (49a980). The
   name could not be changed (gw.py refuses non-default names); comment appended; coordinator should rename (e.g.
   `Move_ExpandIssuedPatrol`).
2. **No public Attack rejection found.** Attack on air, invulnerable and self targets are accepted (native returns
   true) and converted by 207160 into a point Attack Move to the target position; Footman Attack Ground is accepted.
3. **q1 case 1 anomaly (unresolved).** In the patrol_queue-terrain queue scene, an Attack Move passing a paused
   hostile Footman at (640,1280) never acquired it (68 d01c1 scans, no acquisition chain), unlike v3a case 1. The
   queue probe differs by `GetLocalPlayer()` owner, `SetCameraBounds(-1024…3072)` and speed 100; a b-variant without
   the camera bounds/local-player calls (`RS-ORDER-01.10-q2`, `order0110_queue_probe_b.j`) is queued and will be
   reported in `HANDOFF-addendum.md`. The handoff ordering it witnesses (queued Move after Attack Move arrival) is
   unaffected.
4. Engine mismatches (current OpenRealm, from source reading): see Engine entry points E1–E7.

## Engine entry points (must change) and failing-regression expectations
* **E1 `skills/s_attack.c:S_OrderAttack`, `order_attackmove`, `S_OrderAttackGround`; `m_unit.c:unit_issueorder_now`,
  `unit_issuetargetorder_now`** never set `edict_t.current_order_id`. Expected: public `attack` target →
  `GetUnitCurrentOrder`=851983 during approach, attack and cooldown; `attack` point → 851983 through acquisition,
  combat and resume until arrival → 0; `attackground` → 851984 until Stop/replacement. Automatic `order_attack`
  (acquisition, retaliation) must not write it (idle unit stays 0 through combat; Attack Move keeps 851983).
* **E2 unattackable target** (`m_unit.c` "attack" target branch → `S_OrderAttack` returns false when
  `!S_AttackCanTarget`): retail accepts and issues a point Attack Move at the target's current position (snapshot,
  no tracking). Regression: Footman `IssueTargetOrder(u,"attack",flyer)` returns true, head 851983, unit moves to the
  flyer's position at issue, head 0 at arrival; `attack` on self → true, 851983 then 0 on the next frame;
  invulnerable target same as flyer.
* **E3 Attack Ground without artillery** (`S_OrderAttackGround` `attack_ground_valid`): retail Footman accepts
  851984, approaches the point and stands with 851984 (no damage) until replaced/stopped.
* **E4 `attackonce` (851985)** missing from the `m_unit.c` order table: retail accepts; head 851985 until the first
  hit's swing completes (hit at 29, head 0 at 35), then idle acquisition continues with head 0.
* **E5 target loss** (`attack_stop_if_target_invalid` / `A_TARGET_REMOVED` → `attack_finish_after_combat`): retail
  keeps 851983 synchronously after `KillUnit`/`RemoveUnit` of the attacked target (records kill_after /
  remove_after = 851983) and retires only when the current swing's timer completes (60→62, 45→48, 90→94); Attack
  Move resumes the point under 851983. Contrast ORDER-01.15 Follow (synchronous 0).
* **E6 replacement/Stop**: Move/Attack replacement swaps the head in the call (851983↔851986); Stop gives 0 in the
  call and automatic combat may continue with 0 (do not resurrect 851983).
* **E7 queue** (`G_UnitStartNextQueuedOrder` → owners): queued Attack Move behind Move publishes 851983 at Move
  arrival; queued Move behind Attack Move publishes 851986 at Attack Move arrival.
* Death (`m_unit.c` death path) already matches: 0 synchronously; recreated handle 0.

## Proposed new IDs
**ORDER-01.19** live save/load (UI save) of active Attack/Attack Move/Attack Ground and queued attack orders;
**ORDER-01.20** AI-owned Attack completion (`49e990` d0006 append under Unit5c bit4) and its producers.

Files: `expected-ORDER-01.10.json`, `verify-ORDER-01.10.json`, `types-ORDER-01.10.json`, `mapping-rows-ORDER-01.10.txt`,
`proposed-docs-ORDER-01.10.md`, `ghidra-writes.jsonl`, `ghidra-apply.sh`, `summary-*.json`, `captures/`, `maps/`.
Repository (new files only): `tools/frida/research/order0110_{probe.j,queue_probe.j,queue_probe_b.j,make_map.py,observer.js,capture.py,summarize.py,expected.py,verify.py,ui_input.c}`.
