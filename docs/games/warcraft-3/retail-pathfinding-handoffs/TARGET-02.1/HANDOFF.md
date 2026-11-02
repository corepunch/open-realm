<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/TARGET-02.1/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **TARGET-02.1**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/TARGET-02.1/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-TARGET-02.1-crowd.json` -> [`TARGET-02.1-expected-crowd.json.gz`](../../../../../tools/ghidra/fixtures/research/TARGET-02.1-expected-crowd.json.gz) (uncompressed sha256 `c1829ef0c77c98d78b577f7e2e0eaafc68e69d9ef905633a7058ba53d7e2d741`, 1514787 bytes)
> - `expected-TARGET-02.1.json` -> [`TARGET-02.1-expected.json.gz`](../../../../../tools/ghidra/fixtures/research/TARGET-02.1-expected.json.gz) (uncompressed sha256 `228b3f6878f0f64d11a07dd75741937f74de09275a6ba7bbdda7e0fa1c74d85d`, 989275 bytes)

> Research handoff for **TARGET-02.1**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/TARGET-02.1/`.
> Frozen expected results: `expected-TARGET-02.1.json` (map a, sha256 `228b3f6878f0f64d11a07dd75741937f74de09275a6ba7bbdda7e0fa1c74d85d`, 989,275 bytes),
> `expected-TARGET-02.1-crowd.json` (maps b/c, sha256 `c1829ef0c77c98d78b577f7e2e0eaafc68e69d9ef905633a7058ba53d7e2d741`, 1,514,787 bytes).
> Field layout of the compact rows is in each file (`fields`, `context.rows_fields_note`).

# TARGET-02.1 handoff — delayed changed-cell producers, refresh cadence and admission/replan timing across target-order families

**Status.** Established:
(a) **A** — the complete writer inventory of path request timestamps `+7c` (fine) / `+80` (adaptive) in the path module, so the
only *delayed changed-cell producers* are the path's own admitted searches that keep their timestamp (coarse ≥32 pops, fine
bucket work ≥64 after charging); the fixed visibility callback and its installer; the group/member gate call order per owner visit.
(b) **L** — 11 scenes on a flat walled arena (two observed repeats each, identical decision rows, identical absolute owner counters,
identical public markers): ground Smart (walking and stepped target), target **Move**, **Attack**, **Holy Light** (unit-target spell),
**flying** and **amphibious** followers, a ground follower of a flying target, a crowd, 36 same-row Smart followers (scheduler
contention) and fog loss. Every gate/interval/reload/timestamp-retention decision obeys the recovered rules (0 violations).
(c) **Which changes request a new route** (exact per-tick rule below), including two delayed producers proven live:
*member* changed cells within 10 visits of the member's own kept search (accepted at exactly timestamp+10, or later if old-route
searches renew a timestamp), and *group* samples within 10 visits of a deferred coarse admission (sample **discarded** until the
next refresh, 17 or 34 visits later).
Not established: structure targets (StructureFollowRange / target `+248&400` point branch), multi-member follow groups (shared
formation offsets), refresh reloads above 17 in this arena (formula covered by the existing 4,002-case oracle), full visibility
policy branches (TARGET-03.1/03.2). Observer-free controls: see *Controls*.

## Functions (ABIs from assembly; excerpts `static/asm-excerpts-TARGET-02.1.txt`)

| VA | Role | ABI | Evidence |
|---|---|---|---|
| 6f16c150 PathGroup_TickMovement | owner visit | ECX group, plain RET | asm 16c150..16c213; order below |
| 6f16cd30 PathGroup_SampleDestination | cached destination sampler | thiscall ECX group, stack4 out `WC3PathVector2*`, RET 4 | asm; live 14,614 sampler rows (map a) |
| 6f15bb40 **PathOwner_IsTargetHidden** (renamed) | calls owner+258 | ECX owner, stack4 target mover, stack8 group, RET 8, EAX | asm 15bb40..5c |
| 6f15cc80 **PathOwner_SetTargetHiddenCallback** (renamed) | sole writer of owner+258 | ECX owner, stack4 callback, RET 4, EAX previous | asm |
| 6f04dc90 **PathOwner_InstallTargetHiddenCallback** (renamed) | wrapper | ECX callback, plain RET; only caller MapStart_FinalizeInitialPathing 1eab6f with 23a760 | asm |
| 6f23a760 PathGroup_IsTargetNotVisible | the only target-visibility policy | fastcall ECX target mover, EDX group, EAX 0/1 (NEG/SBB/INC), plain RET; `!66fdd0(member0 unit, target unit, 0, 4)`; 0 when either payload is missing | asm 23a760..83e |
| 6f16ce10 PathGroup_RequestRoute | group gate + route | ECX group, stack dest*, region, ready*, final*, RET 10; final not written when it returns 0 | asm + live (`final` garbage on failure) |
| 6f167e40 Path_CheckDestinationChange | gate | ECX path, stack dest*, shift, ready*, RET c, EAX changed | asm; 28,880 oracle cases; live |
| 6f167120 **Path_RequestGroupRoute** (renamed) | group path request | ECX path, stack source*, straight flag, RET 8; path88 bit 200000 clear → single-point route, no admission/timestamp | asm |
| 6f166c30 Path_RequestAcceleratedRoute | coarse | ECX path, 3 stack, RET c; 168910 mode1 → 168310 → search; `+80=0` on denial (166cbf) or pops<32 (166dbb) | asm + live |
| 6f166e90 Path_RequestFineRoute | fine | ECX path, 2 stack, RET 8; 168910 mode0 → 168310 → search; `+7c=0` on denial (166f40) or bucket work<64 after charge (166fd6) | asm + live |
| 6f168910 Path_CheckRequestInterval | interval | ECX path, (mode,current,elapsed*), RET c; writes `+7c[mode]=current` on pass (16894c), `current-10` repair (16892d) | asm; oracle |
| 6f168960 **Path_ClearRequestTimestamp** (renamed) | unreferenced clear | ECX path, stack mode, RET 4 | asm, no xrefs |
| 6f169680 PathGroup_UpdateTargetRefresh | countdown | ECX group, plain RET | asm; live |
| 6f16fbd0 Mover_StepRoute | member gate | existing 10-arg ABI; gate return 16fca2, accept SetDestination return 16fce4 | live |
| 6f5fc640 Move_BeginTargetTaskRequest | target task | ECX CAbilityMove, stack target, range*, persistent, p5, RET 10 | asm + live callers 5ffa99 (Move_HandleTargetTask) / 49a2cb (Attack) |
| 6f05a5c0 Move_BeginTargetRequest | group/range setup | ECX self bridge, 8 stack, RET 20; stack6 persistent | asm + live |
| 6f49a240 (Attack chase) | attack approach | ECX CAbilityAttack, stack target, flag; RET 8; flag≠0 → range FLT_MAX | asm + live |

## Fields (proposed in `types-TARGET-02.1.json`)

| Field | Meaning | Encoding | Writers | Readers |
|---|---|---|---|---|
| Group+4c/+50 `target_offset` | fixed XY (no target) / offset added to the predicted target | 2×f32 fine | 16db00 (16db13/19) | 16cd30 |
| Group+64 `target_refresh` | refresh countdown | i32; −1 = sampled this visit | 16cd45/48 (0), 16cd9e (−1), 169680 (reload / −1 per visit, 0 sticky), 1698a8 (0) | 16cd30, 169680 |
| Group+6c `unseen_counter` (existing) | consecutive hidden visits | u32 | 16cd45/72/92 | 16a790, 16c390 |
| Path+7c/+80 (existing) | fine / adaptive request timestamps | owner counter | 16894c (current), 16892d (current−10), 166cbf/166f40 (denied→0), 166dbb (coarse pops<32→0), 166fd6 (fine bucket work<64→0), 1658f3/165912, 166094/1660b3, 168960 (0) | 168910, 167e40 |
| Owner+258 `target_hidden_callback` | fixed `PathGroup_IsTargetNotVisible` | code ptr | 15cc80 only | 15bb40 |

Other `+7c/+80` stores in 6f160000..6f172000 are mover position/velocity (`160708`, `16134f`, `161b27`, `161b4c`) or group fields
(`16aae7`, `16be3d`, `16d8a7`, `171543`); stores outside the module were not individually classified.

## Complete ordered decisions per follower owner visit (16c150, asm + every live row)

1. `16bfd0` target identity lost → group retires. 2. `16bc10` prune members, `age+1`.
3. `16cd30`: no target → `unseen=0,cd=0`, dest=`+4c`. Hidden (`15bb40`≠0) → `unseen+1`, dest=`path+1c`, **countdown untouched**.
   Visible → `unseen=0`; if `cd==0`: `cd=-1`, `flags|=1000`, dest = predicted target fine position (`05bdd0`) + `+4c`; else dest=`path+1c`.
4. `16ce10`: `167e40(dest,1)`; **changed∧ready** → `168b80(path,dest,1)` and route request; else if adaptive index < count keep the
   route (a changed-but-delayed sample is not stored); else request a route to the *old* destination. Request = `167120` →
   `166c30` (interval mode1 → admission → search; keep `+80` iff pops ≥32). Failure returns 0.
5. On failure: `169680`, `16c5d0` stop members, return. Otherwise member decisions `16c250` (each member `16a790`→`16fbd0`:
   arrival/held early return precedes the gate; gate on the group waypoint+offset; changed∧ready → reset + `168b80`;
   `Path_Advance` may request coarse/fine routes for the destination still stored), commits `16c570`, `flags&=~10000`,
   `169680` (−1 → reload `clamp(trunc(dist·0.33+0.5),16,132)` (+165 if `flags&400`); >0 → −1; 0 stays 0),
   completion/regroup, cooldown.

**Which changes request a new route:** a group route request happens only on a changed∧ready sample (or while the group has no
usable adaptive route, e.g. after an admission failure); a member route reset happens only when its gate reports changed∧ready
(or the fine target object / refresh flag changes, `16fbd0`). Same-bucket target motion never requests one (TARGET-02.4).

## Behaviour (frozen in `expected-TARGET-02.1.json` and `expected-TARGET-02.1-crowd.json`)

Order/task families (map a, both repeats identical; counter = owner `+538`):

| Scene | Public order | Target tasks (counter, caller, range, persistent) | Group flags (visits) | Group/member gates, delayed |
|---|---|---|---|---|
| 0 Smart, walking target | 851971 | 1107 5ffa99 300 0 → arrival, validate 0 at 1342 → 1342 5ffa99 300 **1** | 0x11000 (236) → 0x11801 (383) | 617/432, member 4 |
| 1 Smart, target `SetUnitY` ±64 every 0.2 s | 851971 | 1774 300 0 → 2052 validate 0 → 300 1 | 0x11000 (279) → 0x11801 (339) | 616/368, member 2 |
| 2 **Move** on unit | 851986 | 2440 validate 0 → 5ffa99 300 **1** immediately (no approach phase) | 0x11801 (618) | 617/444, member 4 |
| 3 **Attack** (enemy, walking) | 851983 | 3111 5ffa99 **90** 0; then 49a2cb 90 ×7 and **FLT_MAX** ×2 (group 0x11200), each a fresh group; 5ffa99 90 again at 3732 after the authored Stop (re-acquisition) | 11 short groups (2..284) | 1052/400, 0 (target retaliation adds its own 5ffa99 task) |
| 4 **Holy Light** on damaged ally | 852092 | 3774 5ffa99 **800** 0; reissue at l70 rejected, l140 accepted (4224) | 0x11000 (159),(130) | 287/228, member 4 |
| 5 **flying** follower (hgry) | 851971 | 4440 300 0 → 4533 validate 0 → 300 1 | 0x11000 (94) → 0x11801 (525); group coarse 2..4 pops, timestamps always 0 | 617/268, 0 |
| 6 **amphibious** follower (nmyr) | 851971 | 5107 → 5356 persistent | (250) → (369) | 617/441, member 7 |
| 7 ground follower, flying target over the wall | 851971 | 5774 → 5976 persistent | (203) → (415); member fine up to **701** pops (700 budget exhausted) | 616/542, 0 |
| 8 Smart in a crowd (8 movers same row) | 851971 | 6440 → 6691 persistent | (252) → (367) | 617/443, member 6 |

Map a totals: 5,683 follower visits, 308 reloads (all 16, unclamped 1..15 → sample period 17), 525 changed gates, 27 delayed member
visits in 6 episodes, 0 admission denials.

**Delayed member episode (scene 0, raw words):** counter 1255 member path accepts group final `49.0/30.514301`
(`42440000/41f41d4a`), coarse 37 pops (kept), fine 157 pops (bucket work 157, kept) → timestamps `[1255,1255]`. Counter 1261 group
samples target `49.0/32.904732` (`42440000/42039e72`), changed∧ready (`[0,1244]`), coarse 30 pops (cleared), new group waypoint
`41.5/47.5` (`42260000/423e0000`). Member gate 1261..1264: changed1 ready0, path+1c stays `42440000/41f41d4a`, no search.
1265: changed1 ready1 → SetDestination(`42260000/423e0000`,1), coarse 14 pops (→0), fine 94 pops (kept 1265).
All six map-a episodes accept at exactly `max(blocking timestamps)+10`.

**Self-renewing member delay (crowd, map c gi 11):** gate 1518 delayed by `[1514,1513]`; the stale route is exhausted, so
`Path_Advance` requests coarse at 1523 (interval pass, kept) and fine at 1524 (admission denied → 0) / 1526 (kept) for the *old*
destination; the gate is ready only at 1536 (fine 1526+10). Across 476 delayed member rows under contention, 432 accept at
blocking timestamp+10, others later through such renewals, arrival/held early returns, or stopped members.

**Delayed group sample (scheduler contention, map b/c scene 0, gi 3):** 1125 sample `49.0/13.781088` (`42440000/415c7f56`) changed∧ready
→ SetDestination; target-group bucket (row 0 policy 1: limit 300) work 331 > 300 → admission denied, path queued, `+80=0`,
RequestRoute 0 → refresh reload 16, members stopped; 1126..1132 re-request, denied; 1133 admitted, 85 pops → `+80=1133`.
1142 sample `49.0/16.171631` (`42440000/41815f80`): changed1 **ready0** (age 9) → not stored, route kept, refresh reloaded 16.
1159 sample `49.0/18.562149` (`42440000/41947f48`) changed∧ready (age 26) → stored. 36 followers: 4,166 denied group coarse and 456 denied member fine
admissions, group deferral 0..14 visits, 185 delayed group samples, every one resolved by the next refresh (180 at +17, 5 at +34).

**Hidden target (map c scene 1, fog):** after the FOGGED modifier (counter 1957) the callback blocks every visit from 1959,
`unseen` counts 1..110 (1959..2068) at a fixed destination while 169680 runs the countdown 3→0 and keeps it at 0; the approach group arrives at the cached point, `Move_HandleTargetTask` validates the still-hidden
target → **0xdd** (2068) and the Smart order ends (public order 0 before the fog is removed). Policy matrix → TARGET-03.1/03.2.

## Reproducers (repository root; `R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research`)

```sh
B=/GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/Human02Interlude-original.w3m
python3 tools/frida/research/target021_make_map.py --base $B --tool build/bin/mpqtool --probe target021  --output $R/TARGET-02.1/maps/RS-TARGET-02.1-a.w3m
python3 tools/frida/research/target021_make_map.py --base $B --tool build/bin/mpqtool --probe target021c --output $R/TARGET-02.1/maps/RS-TARGET-02.1-c.w3m
$R/_env/install-map.sh $R/TARGET-02.1/maps/RS-TARGET-02.1-a.w3m $R/TARGET-02.1/maps/RS-TARGET-02.1-c.w3m
P=/home/lofcz/.local/share/uv/tools/frida-tools/bin/python
$R/_env/live.sh $P tools/frida/research/target021_capture.py --data {DATA} --remote {REMOTE} --x11-display {DISPLAY} --map 'Maps\RS-TARGET-02.1-a.w3m' --mode observe --seconds 600 --output $R/TARGET-02.1/captures/observe-N.jsonl
$R/_env/live.sh $P tools/frida/research/target021_capture.py --data {DATA} --remote {REMOTE} --x11-display {DISPLAY} --map 'Maps\RS-TARGET-02.1-c.w3m' --mode observe --lite --preload rs-target021c.txt --seconds 600 --output $R/TARGET-02.1/captures/observe-c-N.jsonl
$R/_env/live.sh $P tools/frida/research/target021_capture.py ... --mode control ...   # observer-free
python3 tools/frida/research/target021_analyze.py $R/TARGET-02.1/captures/observe-N.jsonl --report $R/TARGET-02.1/analysis/observe-N.json
python3 tools/frida/research/target021_expected.py $R/TARGET-02.1/analysis/observe-1.json $R/TARGET-02.1/analysis/observe-2.json --names observe-1 observe-2 --extra $R/TARGET-02.1/analysis/context.json --output $R/TARGET-02.1/expected-TARGET-02.1.json
python3 tools/frida/research/target021_expected.py $R/TARGET-02.1/analysis/observe-c-{1,2}.json $R/TARGET-02.1/analysis/observe-b-{2,3}.json --names observe-c-1 observe-c-2 observe-b-2 observe-b-3 --extra $R/TARGET-02.1/analysis/context.json --output $R/TARGET-02.1/expected-TARGET-02.1-crowd.json
python3 tools/frida/research/target021_compare_preload.py <reference-preload> <other-preloads...>
```
Map a: 9 scenes × 20 s, ~9 min wall time with the v1 observer; maps b/c: ~2 min with the batched v2 observer.

## Provenance

game.dll `d51e5680…d8236`. Base `Human02Interlude-original.w3m`; mpqtool `build/bin/mpqtool`. Maps (flat 17×17 vertices,
64×64 fine cells, WPM floor 0x40, wall 0xca at fine x30..33/y0..49):
`RS-TARGET-02.1-a` `657403d3…b9d4` (probe `50010542…3ffe`, builder `e6e7f00b…b146`),
`RS-TARGET-02.1-b` `95d1dfc8…292c` (probe `f7a585f3…92fc4`, builder `7f59f3f4…cb79`),
`RS-TARGET-02.1-c` `54c3b541…a29a8c` (probe `722f82ba…acdc`, builder `4aac3c2e…6c63`).
Observer v1 `37f3c11e…7e53` / controller `9c061cf4…c58b` (a, b-1); v2 (lite + 256-row batching) `028bbf90…cebb` / `1e099c4e…f5d2`
(b-2, b-3, c-*); all versions archived under `sources/`. No seeding or state intervention was used; repeats matched with identical
absolute owner counters (the path-owner random stream was not instrumented in this task).

## Captures

| Capture | Status | sha256 (prefix) |
|---|---|---|
| observe-1 (a, v1) | complete, 2085 markers | `c5a42099` / preload `5c30952c` |
| observe-2 (a, v1) | complete; every decision row and absolute counter equal to observe-1 | `fc97b618` / `a326749c` |
| observe-b-1 (b, v1) | **incomplete**: Frida message backpressure in the 36-follower scene; SIGINT ignored, controller SIGKILLed, owned pid 880 killed by frida-kill; game reached completion first (recovered preload identical to b-2) | `749a5dfa` / `f9b7ce88` |
| observe-b-2 (b, v2 lite) | complete; scene 0 rows identical to c-1/c-2; **fog scene Smart rejected** (target not visible at night: `accepted=0 order=0 v=0`) — setup flaw kept, fixed by map c | `1ad5ce2d` / `c5f9e7ad` |
| observe-c-1, observe-c-2 (c, v2 lite) | complete, identical rows both scenes | `2436f742`, `34a7ff74` / `7c5700c6`, `481dc9c6` |
| observe-b-3 (b, v2 lite) | complete; repeat of b-2 (scene 0 rows identical to b-2/c-1/c-2, fog rejection repeated) | `b787b6e5` / `90d74e99` |
| control-1 (a) | **never ran**: queued with `timeout 1500`, expired while waiting for an environment (`control-1-NOTE.txt`) | – |
| control-2 (a, no attach) | complete, 2085/2085 markers identical to observe-1 and observe-2 | `9e916f82` / `0e4a91b5` |
| control-c-1 (c, no attach) | complete, 544/544 markers identical to observe-c-1/c-2 | `2fa5fdfc` / `c8b1f79f` |

## Controls and repeats

Observer-free controls (spawn, resume, loading keys, kill; no attach/script/hooks) reproduce every probe marker of the observed runs:
map a 2,085/2,085 (`analysis/control-compare-a.json`: follower/target position R2S 3 decimals, current orders and life every 0.1 s
in all nine scenes, setup/order/target-motion/stop markers) and map c 544/544 (`analysis/control-compare-c.json`, including the
36 crowd positions every second and the fog scene's order/visibility samples). Observed repeats: map a observe-1 = observe-2 in
all 5,683 decision rows with identical absolute owner counters; maps b/c scene 0 identical across b-2, b-3, c-1, c-2 (10,720
compact rows); map c scene 1 identical across c-1, c-2. The control controller presses the loading key up to six times (it cannot
see markers); presses after the game started are camera-only UI input and the marker identity shows no simulation effect.
Asset Preload lines (random music track) differ between runs and are excluded by `target021_compare_preload.py`.

## Public vs forced state

All producers are public JASS (`IssueTargetOrder smart/move/attack/holybolt`, `IssuePointOrder`, `SetUnitY`, `SetUnitMoveSpeed`,
`SetUnitState`, `SelectHeroSkill`, `CreateFogModifierRadius`, `FogEnable`, `SetPlayerAlliance`, `SetFloatGameState`). No memory was
written, no game function was called by the observer, no synthetic input was injected.

## Exclusions (owning IDs)

Visibility/invisibility/validation policy matrix and reacquisition → TARGET-03.1/03.2. Structure targets (`StructureFollowRange`,
`Move_CreateTasksFromOrder` target `+248&400` point branch) → proposed **TARGET-02.6**. Multi-member follow groups (GroupTargetOrder
Smart: member offsets, member0-only visibility, shared completion) → proposed **TARGET-02.7**. Reloads >17 (long distances) → existing
`verify_wc3_pathing_refresh.py` + scene53. Attack combat timing → ORDER-01.10. Scheduler fairness beyond the observed FIFO → SCHED.

## Preserved mismatches

* Engine `s_move.c` group sampling (~l.4572, `TODO TARGET-02.1`) stores any changed-cell sample; retail drops it when either group
  timestamp is <10 visits old (live 185×).
* Engine `move_group_adjust_destination` (~l.4392) resets the member route as soon as the bucket changes; retail keeps the old
  member destination until both member timestamps are ≥10 old, while old-route searches may renew them (live 27 + 476 rows).
* Engine `order_follow_resume` keeps flying followers on the legacy traversal (`TODO TARGET-02.1`); retail flying followers use the
  same approach→persistent physical groups (scene 5: 94 + 525 visits, group bucket 300, member fine searches).
* Ghidra plate of 16cd30 / 167e40 / 169680 said "delayed changed-cell producers remain TARGET02.1" — now answered (appended).
* observe-b-1 failure and the b fog-scene order rejection are retained, not re-labelled as evidence.

## Engine entry points

`games/warcraft-3/game/skills/s_move.c`: group owner pass (target sampling block and `move_group_route` request/admission failure
path: reload refresh, stop members, keep requesting the stored destination), `move_group_adjust_destination` / member gate (use
`movement.fine_request_time` and the member adaptive request time as `+7c/+80`), `S_AdmitUnitMoveFineRequest`,
`S_ChargeUnitMoveFineRequest`, `S_AdmitMoveCoarseRequest` (timestamp keep/clear rules, already ported), `order_follow_resume`
(flying physical groups), group `target_refresh` handling while hidden (sticky 0; TARGET-03.2 owns the unseen/0.49 policy).
Save layout: group refresh countdown and both member/group timestamps must survive save/load at every point of a delay.

## Suggested failing engine regressions

1. **Member delay** (map a scene 0): with member timestamps `[1255,1255]` and a group waypoint change at 1261, the member path
   destination stays `42440000/41f41d4a` for 1261..1264 and becomes `42260000/423e0000` at 1265, followed in the same visit by a
   14-pop coarse and 94-pop fine search; the six episodes in `expected-TARGET-02.1.json` accept at exactly timestamp+10.
2. **Self-renewing delay** (crowd gi 11): old-route coarse 1523 and fine 1526 searches postpone acceptance to 1536.
3. **Discarded group sample** (crowd gi 3): denial 1125..1132 → admitted 1133 (85 pops) → sample 1142 not stored, refresh reloaded
   → sample 1159 stored; members stop on every denied visit; 185 group delays all resolve at the next refresh.
4. **Cadence**: visible target → one sample per 17 visits (reload 16); hidden target → countdown keeps decrementing to 0 and stays
   0, `unseen` +1 per visit, cached destination unchanged; the first visible visit with countdown 0 samples.
5. **Families**: Move-on-unit starts persistent (no approach), Smart approaches with FollowRange then validates and becomes
   persistent, Attack/Holy Light approach non-persistently with range 90/800 and Attack re-creates a fresh group per chase.
6. **Flying follower** runs the physical approach/persistent groups with cheap (timestamp-clearing) group searches.

## Proposed integration

`mapping-rows-TARGET-02.1.txt` (5 renames + plate comments, written via gw.py, logged in `ghidra-writes.jsonl`, unsaved),
`types-TARGET-02.1.json` (2 group fields, 1 owner field, 8 prototypes), `proposed-docs-TARGET-02.1.md`.


## Current integration audit (Payoff356)

The preserved mismatch list above describes the original research handoff.
Payoff164 implemented the delayed member/group gates,186 implemented physical
flying Follow, and251/256 completed the visibility policy work. The current
[audit](../../retail-pathfinding-target-audit356.md) adds complete617-visit target
Move and616-visit stepped Smart production replays, both with delayed cold-save
continuations and23 literal group/member words per visit. Classic/TFT each pass
51,690 assertions; the broader target inventory passes76 tests/179,434 each.

The compact historical expected file loses23 position words through six-decimal
display rounding. It remains immutable; new fixtures use original JSONL words,
and the analyzer/freezer now retains native pose words. A complete fresh
read-only retail run matches all5,017 original actual follower decisions across
nine scenes and all2,085 public markers. Only the inherited render shadow member
is resized in its uniquely named map; script, terrain and pathing are unchanged.

Full natural36-follower numerical contention and the final captured-region/mask
composition audit remain in this existing TARGET-02.1 task. The earlier proposed
02.6/02.7 labels were research suggestions, not new accepted TODO children.

## Current integration361/362: TARGET-02.1 closed

Historical research limitations above are retained as provenance. Full natural
contention now replays22,772 actual owner decisions across36 followers, with the
1142 delayed sample and a cold saved suffix. Complete amphibious scene6 supplies
617 original visits using native radius48, turn0.5 and exact floor40/wallCA.
The producer/current-query inventory, existing visibility/loss and retained-cohort
contracts,28,880 original gate cases and1,434 guarded instructions complete the
existing leaf. Classic/TFT each pass109 tests and2,026,927 assertions;17 evidence
checks pass. Existing engine behavior matches the correct inputs, with fixture
diagnostics preserved explicitly. See [final audit](../../retail-pathfinding-target-masks362.md)
and [natural contention](../../retail-pathfinding-crowd361.md).
