<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-03.4.7.3/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **GROUP-03.4.7.3**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-03.4.7.3/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-GROUP-03.4.7.3.json` -> [`GROUP-03.4.7.3-expected.json`](../../../../../tools/ghidra/fixtures/research/GROUP-03.4.7.3-expected.json) (uncompressed sha256 `b75657f092e51809c6ed267341d0bce789a87f122445dba9913851cce8f7368c`, 504403 bytes)
> - `expected-interrupt.json` -> [`GROUP-03.4.7.3-expected-interrupt.json`](../../../../../tools/ghidra/fixtures/research/GROUP-03.4.7.3-expected-interrupt.json) (uncompressed sha256 `20a798345ad037ea3185dcf8eb7cd571a51d120ff32bf8874163d4e4de48c7cb`, 100779 bytes)
> - `expected-noflee.json` -> [`GROUP-03.4.7.3-expected-noflee.json`](../../../../../tools/ghidra/fixtures/research/GROUP-03.4.7.3-expected-noflee.json) (uncompressed sha256 `8f3adb15415768adb222720ec40e735ea80b11233cb783624664b88eff48ab9f`, 76921 bytes)
> - `expected-retreat.json` -> [`GROUP-03.4.7.3-expected-retreat.json`](../../../../../tools/ghidra/fixtures/research/GROUP-03.4.7.3-expected-retreat.json) (uncompressed sha256 `830bfb097b9b97ee1488672b691d24d50bded3402fedeb8693d954211d716a76`, 140003 bytes)
> - `expected-threshold7.json` -> [`GROUP-03.4.7.3-expected-threshold7.json`](../../../../../tools/ghidra/fixtures/research/GROUP-03.4.7.3-expected-threshold7.json) (uncompressed sha256 `54e3c6a74bd5cdf06683cd8e96073222cb88bc504b1e1f494772eb52db550e25`, 114358 bytes)

# GROUP-03.4.7.3 handoff — autonomous Captain home, retreat and goal policies

**Status.** The attack captain's autonomous home policy is recovered at **A + L**: four public scenes, each with
two observed repeats (environments B and C) that are identical after address normalization, plus an
observer-free control whose JASS Preload stream (including per-member positions every 0.5 s) equals both
repeats. A replay of the decoded policy (`tools/ghidra/research/verify_GROUP-03.4.7.3_policy.py`) reproduces
all 24 live re-evaluation results, retreat-bit and actor-placement side effects and the 16 goal-event GoHome
calls (`model-check.json`, passed=true; 4 AI-start SetCaptainHome calls happen before the first actor snapshot
and are not replayed). Established: occupied `SetCaptainHome` (flee off/on, 6 and 7 members), removal-driven
retreat and its threshold, empty-roster actor placement, retreat flag lifetime, `CaptainAttack` travel /
arrival / interruption, and the idle-member reissue after a far home change. **Not established:** retail
save/load of these states, the `SetMeleeAI` c0 rule (assembly only), and the combat flee trigger `9d8460`
(member attacked; assembly only) — proposed as new IDs below. Explicit `CaptainGoHome` and near-home initial
travel (GROUP-03.4.7.1) were not repeated.

## Functions (all names applied with gw.py; ABIs from instruction bodies)

| VA | Name | ABI | Role / evidence |
|---|---|---|---|
| 6f9d08e0 | CaptainAI_ReevaluateHomeRetreat | thiscall ECX captain, RET, EAX 0/1 | no town (player obj 2d4) → 0; `CaptainAI_IsNearAuthoredHome` → 0; `CaptainAI_TestTargetEngagement`==0 → home; flee bit `town2d0.20` clear **or** state==1: home iff `bc<1`; else home iff `!(6c&1000)` or (`c0<3 && bc<7`). Home: state 3→2, clear 108/114/120, `6c|=2` iff `bc>0`, `CaptainAI_GoHome`, return 1. Callers 9d5ca3 (SetHome), 9d790d (member removal). A+L |
| 6f9d73c0 | CaptainAI_TestTargetEngagement | thiscall ECX, EAX | 1 when 114/120/108 are empty, else roster scan (combat). Live always 1. A+L(empty path) |
| 6f9d2670 | CaptainAI_GoHome (existing) | thiscall ECX | near home: clear `6c.2` iff `c4 >= bc/2` (CDQ/SAR); else if `bc==0 || state==1` → `CaptainAI_PlaceActorAtPoint(home)`; then order70=d0012, `PublishPointRequest(home,1,&500,1)`. A+L |
| 6f9d6ed0 | CaptainAI_PlaceActorAtPoint | thiscall ECX, +4/+8 FloatMini objects (value at +4), RET8 | `Widget_WritePositionAfterAdmission` (teleport) then 9d7600. A+L |
| 6f9d1680 | CaptainAI_Attack | thiscall ECX, +4 x*, +8 y*, RET8 | state=2, clear list 98, clear `6c.2`, `PublishPointRequest(x,y,1,&200 6fd77f84,1)`. A+L |
| 6f9b88f0 | JassNative_CaptainAttack | cdecl (real*, real*) | attack captain 9c64c0 → CaptainAI_Attack. A+L (caller 9b8923) |
| 6f9cfeb0 | CaptainAI_OnActorGoalEvent | thiscall ECX | dispatch 9d4b5d; clear `6c.2000`; target → re-request; `CaptainAI_IsNearRetainedRequest` → GoHome (9cff7d); else republish d4/dc at 200. A+L |
| 6f9d7b00 | CaptainAI_OnPeriodicUpdate | ECX captain | dispatch 9d4b7f; if not near home reissue idle members (`unit+194==0`) with order70 (caller 9d7b9f); calls 9d09c0 (sets `6c.1000`) at 9d7c8d. Name: dispatch case; periodicity not proven. A+L |
| 6f9d7760 | CaptainAI_OnMemberRemoved | thiscall ECX, +4 unit, RET4 | detach then 9d08e0 for every removal; follow-target branches combat-only. A+L |
| 6f9bf610 / 6f9bf650 | JassNative_SetGroupsFlee / TownAI_SetGroupsFlee | cdecl bool / thiscall ECX town +4 | `town2d0.20`. A |
| 6f9cacd0 | TownAI_SetCampaignMode | thiscall ECX town, +4 campaign | campaign → clear `2d0.4000`; labels 6f9bf360 JassNative_SetCampaignAI (pushes 1), 6f9bf950 JassNative_SetMeleeAI (pushes 0). A |
| 6f9d0650 | CaptainAI_UpdateRosterCountsAndRanges (existing) | — | `c0 += delta` for every member when `2d0.4000` clear; melee: only if unit+b8 FloatMini reads 0 or non-illusion Hero. A (+L campaign c0==bc) |
| 6f9d8460 | FUN_6f9d8460 (comment only) | — | member-attacked flee handler reads `2d0.20` at 9d8507. A only, excluded |

Ghidra: 11 renames + 2 labels + plate comments (`ghidra-writes.jsonl`, texts in `ghidra-comments.json`,
mirrored in `mapping-rows-GROUP-03.4.7.3.txt`). Not saved. Types fragment `types-GROUP-03.4.7.3.json`: appended
evidence to captain 64/6c/bc/c0/c4, new `WC3TownAIPrefix+2d0 policy_flags`, 11 method ABIs, new global 6fd77f84
(200), appended 6fd77f60 (500). Note: canonical name `healthy_member_count` (c0) is misleading — under
SetCampaignAI it counts every member and unit life is not an input (threshold7 scene sets life 42/420).

## Behaviour (frozen `expected-GROUP-03.4.7.3.json`, sha256 b75657f092e51809c6ed267341d0bce789a87f122445dba9913851cce8f7368c)

Scene: Player(0) campaign AI (`SetCampaignAI`, `CreateCaptains`, `SetCaptainHome(1,-1936,-144)`,
`InitAssault`/`AddAssault`), N stock Footmen created at (-1936+80c,-976-80r), AI started at tick 9 (0.1 s ticks);
the map sends `CommandAI` phases (1 SetCaptainHome south (-1936,-1424); 2 SetGroupsFlee(true)+same; 3
CaptainAttack north (-1936,-144); 4 SetCaptainHome north; 5 SetGroupsFlee(true); 6 CaptainAttack south) and
removes members with `RemoveUnit`. Fine = (world+(7168,3072))/32: north home = (163.5,91.5), south = (163.5,51.5).
Captain counts are `[b8,bc,c0,c4,c8,cc]`; `before` words are post-detach for removals.

| Scene (members) | Public step (tick) | Live result |
|---|---|---|
| retreat (6) | phase1 occupied far home (120) | 9d08e0=0, actor stays 91.5; tick 125 update reissues all 6 idle members d0012 (9d7b9f) |
| | phase2 flee + same home (170) | 0 (bc=c0=6, 6c=1001) |
| | remove 4 (220) | 5,4,3 → 0; 2 → 1: `6c=1003`, CaptainRetreating=2, members reissued (9d180d), request home r500; actor 91.5 → 66.98 (stops 494.9 from home, tick 245); members continue to y≈52–54 |
| | goal event (tick 240) | GoHome near home, c4=2 ≥ 1 → `6c=1001`, CaptainRetreating 0 |
| | phase3 CaptainAttack north (400) | state 2, request r200; actor to 85.39 (≈195 from target); goal event (tick 420 interval, counter 2439) → GoHome, request home r500, **no** retreat bit; actor back to 66.97 (tick 450) |
| | remove last 2 (470) | actor within 500 → 0, 0; no placement |
| | phase4 SetCaptainHome north on empty roster (520) | 1: actor placed at (163.5,91.5), request r500; goal GoHome next frame |
| interrupt (6) | phase5 flee (110); phase6 CaptainAttack south (150) | actor leaves 91.5 southward |
| | remove 4 (165) | actor 364 from home → all 0 (near-home short-circuit, attack continues) |
| | arrival (190) | goal GoHome → home r500, actor returns to 76.06 |
| | phase6 again (300); remove last 2 (315) | 1 member → 1 (`6c=1003`, attack request replaced by home), 0 members → 1 with actor placement at home; goal GoHome clears bit 2 (c4 0 ≥ 0) |
| noflee (6) | phase1 (120); remove 5 (170); remove last (220) | 0 for 6..1 members; empty → 1 with placement at south home (163.5,51.5) |
| threshold7 (7) | life 42 (120); phase2 (140); remove 1 (200) | 0 (bc=c0=7), 0 (6); c0 unaffected by life |

## Reproducer

```sh
cd /run/media/lofcz/ssd_external/GitHub/open-realm
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research; P=/home/lofcz/.local/share/uv/tools/frida-tools/bin/python
for v in retreat interrupt noflee threshold7; do
  python3 tools/frida/research/GROUP-03.4.7.3_make_map.py --base /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/Human02Interlude-original.w3m --tool build/bin/mpqtool --variant $v --output $R/GROUP-03.4.7.3/maps/RS-G0473-$v-c.w3m
  $R/_env/install-map.sh $R/GROUP-03.4.7.3/maps/RS-G0473-$v-c.w3m
done
$R/_env/live.sh $P tools/frida/research/GROUP-03.4.6.2.1.2_capture.py --data {DATA} --map 'Maps\RS-G0473-retreat-c.w3m' --mode observe --remote {REMOTE} --x11-display {DISPLAY} --seconds 200 --continue-at 30 --observer tools/frida/research/GROUP-03.4.7.3_observer.js --preload rs-group0473.txt --prefix RSH --task GROUP-03.4.7.3 --output $R/GROUP-03.4.7.3/captures/retreat-c-observe-N-env{ENV}.jsonl
# control: same with --mode control and without --observer
python3 tools/frida/research/GROUP-03.4.7.3_analyze.py --capture <capture.jsonl> --report <capture>-report.json
python3 tools/frida/research/GROUP-03.4.7.3_expected.py --report <repeat-1-report> --report <repeat-2-report> --output expected-<scene>.json   # identical=true
python3 tools/frida/research/GROUP-03.4.6.2.1.2_control_compare.py --prefix RSH --reference <control-preload> --other <observe-preload> ... --output control-compare-<scene>.json
python3 tools/ghidra/research/verify_GROUP-03.4.7.3_policy.py --expected $R/GROUP-03.4.7.3/expected-GROUP-03.4.7.3.json --output model-check.json
```
The repository `GROUP-03.4.7.3_probe.ai` now also contains phase 6; the retreat/noflee/threshold7 maps were built
before it was added — `maps/RS-G0473-<scene>-c.ai/.j/.json` are the exact packed sources (hashes below).

## Captures (failures preserved)

| Capture | sha256 | rows | note |
|---|---|---|---|
| captures/home-b-observe-1-envB.jsonl | b8032eec657c294da5c4c0f8c345b4efc925a7ad348fbb6afb2e48501af9cd16 | 753 | EXPLORATORY: map home-b; phase-4 home chosen within 500 of the actor (design flaw); superseded by retreat-c; excluded |
| captures/home-explore-1-envB.jsonl | 1cffd7996ca761a0b3f11679f280885afedb6abbbf47206a6891e4113eded4e9 | 478 | FAILED: map RS-G0473-home-a; AI script did not compile (ModuloInteger is Blizzard.j-only), captain natives never ran; excluded |
| captures/interrupt-c-control-1-envB.jsonl | ff41449a31b01837f7305ef99585757a9deac8dc7e16f2eec133d353cd95b13e | 6 | observer-free control (266 markers equal) |
| captures/interrupt-c-observe-1-envC.jsonl | e3f2844c3de5500eed3377306f7a6a2aabdfa54d83a61ab0f1a9b8f797494dd4 | 395 | interrupt repeat (env C) |
| captures/interrupt-c-observe-2-envB.jsonl | 91b3211b8e44f1a91edc927acc9cc67d4da3fffcca01bca16cc925cf95a91016 | 395 | interrupt repeat (env B), identical; frozen base |
| captures/noflee-c-control-1-envB.jsonl | 2e4b08f214a917ac805d94ed3bd6800936fa478dda3d0a09b845ff1b318a6757 | 5 | observer-free control (220 markers equal) |
| captures/noflee-c-observe-1-envC.jsonl | 35ad299e74ebce1dddba24c276ced1552a4177db3ec7c21cf157ab142ba627a3 | 331 | noflee repeat A (env C) |
| captures/noflee-c-observe-2-envC.jsonl | 6440dad1c463a250c4407c51e42b5e1433014af7bcc241de7d5f10c6b5a98526 | 12 | FAILED: Frida script destroyed during loading (no markers); replaced by observe-3 |
| captures/noflee-c-observe-3-envB.jsonl | 87faadcbad8a5f799871810bbcd5c5c6acc78fdea1ef13acbbefd672b6e3b64d | 331 | noflee repeat B (env B), identical |
| captures/retreat-c-control-1-envB.jsonl | 1881a540fb61fde3f0e52ba3ae7b0de09b26521b5cd44176759beeb939118d38 | 8 | observer-free control (373 markers equal) |
| captures/retreat-c-observe-1-envC.jsonl | 6a65d248458b38d176044fa556277557a8f8e0e5476de81aa56ba557f3906797 | 551 | retreat repeat A (env C) |
| captures/retreat-c-observe-2-envB.jsonl | f62b422769b1f4b9ab08a4c12eecdb53b1927e3c3a5d50c0e01abc0acf335171 | 551 | retreat repeat B (env B), identical |
| captures/threshold7-c-control-1-envC.jsonl | c33a9096c8bbe645b0827c5f37a14f3417783cd645401b2339845275bb8cc470 | 6 | observer-free control (556 markers equal) |
| captures/threshold7-c-observe-1-envC.jsonl | e75d0f6a81a700e3ce3530f7f256b0e482bb8640d535c9ff6d039084d138fed0 | 686 | threshold7 repeat A (env C) |
| captures/threshold7-c-observe-3-envC.jsonl | 3f3eacd8fc5ca50c1e7b84464f869421beafc50f254cfce1b556ff4623d9f27e | 686 | threshold7 repeat B (env C), identical |
| captures/threshold7-c-observe-2-FAILED-args.log | 262a0cc64f1eb2d1342b13adff6e0592c97018e814d8b7373805935e9b95a068 | | FAILED launch: zsh passed `--observer path` as one argument; no game started |
Maps RS-G0473-home-a/large-a/empty-a/-b and large-b were built during exploration; only home-a and home-b were run (above). Preload outputs `*-preload.txt` and analyzer reports `*-report.json` sit next to each capture.

Controls: `control-compare-{retreat,interrupt,noflee,threshold7}.json` — every observed repeat equals its
observer-free control (0 differences). Repeats: `expected-<scene>.json` (`repeats_identical=true`, env B vs C).

## Exclusions
- Combat: targets 108/114/120, `CaptainAI_TestTargetEngagement` roster branch, member-attacked flee 9d8460,
  follow-target removal branches of 9d7760, `AttackMoveKill`. No hostile units in any scene.
- `SetMeleeAI` c0 counting (unit+b8 FloatMini / Hero rule): assembly only.
- Defense captain (type 2) home policy: only its AI-start actor placement is recorded.
- Save/load: no retail save was taken.
- Explicit `CaptainGoHome`, near-home initial travel, member private approach details (GROUP-03.4.7.1/.2).

## Preserved mismatches with the current engine / docs
- `g_bot.c G_BotUpdateGroupFlee`/`G_BotCaptainBeginRetreat` (BZ_COMPAT_GUESS power ratio, persistence timer,
  per-member order_move) vs retail: no power/timer; member-count rule evaluated only on SetCaptainHome and member
  removal; members follow the captain request, actor stops at 500.
- Engine `CaptainRetreating` reads a state enum; retail returns `6c & 2` (raw 2) and the bit survives into an
  empty-roster GoHome until the goal event.
- `S_CaptainGoHome` empty roster prints "speed is unresolved" and returns; retail teleports the actor home.
- `S_SetCaptainHomeActor` occupied branch only rewrites member homes; retail re-evaluates and the update event
  reissues idle members toward the captain.
- `CaptainAttack` native is not registered in `api_module.c`.
- `G_BotRemoveCaptainUnit` never re-evaluates home.
- Canonical field name `healthy_member_count` (c0) does not match campaign behaviour.

## Engine entry points
- `games/warcraft-3/game/skills/s_move.c`: `S_SetCaptainHomeActor` (occupied branch, TODO GROUP-03.4.7.3),
  `S_CaptainGoHome` (near-home retreat clear rule `c4 >= bc/2`; empty-roster placement + r500 request).
- `games/warcraft-3/game/g_bot.c`: `G_BotSetCaptainHome`, `G_BotRemoveCaptainUnit`, `G_BotUpdateGroupFlee`,
  `G_BotCaptainBeginRetreat`, `G_BotCaptainUpdateRetreat`, `G_BotCaptainRetreating`.
- `games/warcraft-3/game/api/api_ai.h` / `api_module.c`: `SetGroupsFlee`, `SetCampaignAI`/`SetMeleeAI` (c0 rule),
  new `CaptainAttack`.

## Failing-regression expectations (owner to implement in t_pathfinding.c from `expected-GROUP-03.4.7.3.json`)
1. retreat: occupied far SetCaptainHome with 6 members (flee off, then on) leaves the actor at fine (163.5,91.5)
   and does not set retreat; the next update reissues all idle members.
2. retreat: removals leave 5/4/3 members → no retreat; 2 → retreat bit set, home request r500, actor stops at
   fine y 66.9775 (raw words in expected timeline) while members reach y≈52–54; retreat clears at the home-arrival
   goal event (tick 240).
3. CaptainAttack north: request r200, arrival goal event → GoHome r500, CaptainRetreating stays false.
4. Empty roster: removal of the last member far from home (noflee tick 220, interrupt tick 315) or SetCaptainHome
   on an empty captain (retreat tick 520) places the actor exactly at home before the r500 request; removal near
   home (retreat tick 470) does nothing.
5. interrupt: removals during CaptainAttack within 500 of home do not retreat; at 826 they do and replace the
   attack request.
6. threshold7: 7 and 6 members with flee never retreat; unit life does not change c0.
7. Save/load: proposed GROUP-03.4.7.3.1 (no retail witness yet).

## Proposed new TODO IDs
- **GROUP-03.4.7.3.1** Retail save/load of captain retreat/home state (`6c.2/.1000/.2000`, state 64, request
  d4/dc/e4, placed actor) through a mid-retreat and mid-attack save.
- **GROUP-03.4.7.3.2** `SetMeleeAI` c0 rule (unit+b8 FloatMini and Hero exception) with public melee AI rosters.
- **GROUP-03.4.7.3.3** Combat flee: `9d8460` member-attacked handler, `CaptainAI_TestTargetEngagement` roster
  branch and target-clearing on retreat with hostile units.

## Provenance
| Item | sha256 |
|---|---|
| game.dll 1.27.1.7085 | d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236 |
| base runtime/Human02Interlude-original.w3m | 199683be6cd1a888ade00f3dce1e3e93f16e92e05724717048b033108b5a2104 |
| maps/RS-G0473-empty-a.ai | 42041cbe89b202cb5fb99b08b7fd75668eb2661451f7d90880f50e03a0df3956 |
| maps/RS-G0473-empty-a.j | 94228b2e563db57b66476f426619912d75138d4f0415b6bac4bdf07fc01b1d9e |
| maps/RS-G0473-empty-a.json | 8edf1d6168f084b4b7600cae4facadab12097e92c32cab522bb7ac4979877517 |
| maps/RS-G0473-empty-a.w3m | a20de51d7f465464e966d42e1f717a32ba54b3760f5e765aad69b88885c086dc |
| maps/RS-G0473-empty-b.ai | 4d33150403a9c617346d9b51932fe22f396d0c638fb3e29ffd14b2c5196f33b1 |
| maps/RS-G0473-empty-b.j | 94228b2e563db57b66476f426619912d75138d4f0415b6bac4bdf07fc01b1d9e |
| maps/RS-G0473-empty-b.json | 001d2d51bce47c575792e9e4fc08409de898fea1b7c884785d0b551b376536c4 |
| maps/RS-G0473-empty-b.w3m | a0e8cc28e08629aa707fd1cd880b697c0e607d853a2c6bccde7e9e1001972a82 |
| maps/RS-G0473-home-a.ai | 0e5140bbe80690cc36666118ffb2ea7b321d6f8a18ee59b8eb38523a94e5ca94 |
| maps/RS-G0473-home-a.j | 05d31b937c573ae57521652fcdac9c6b3bff082740df4c95dbfcd9591cd01a22 |
| maps/RS-G0473-home-a.json | 2a4f0c2ffd675e28bfcc9e2faad2fba244d6015be4baf894682aba29c4c6f8a2 |
| maps/RS-G0473-home-a.w3m | 4a926a2f08f173da621b268549c9684b578e1586aecc77fdf982bae2d0219d4f |
| maps/RS-G0473-home-b.ai | 325e85c7039336d92b55fd2f056eba209d121e26bbc534da32a366e38f2daf14 |
| maps/RS-G0473-home-b.j | 05d31b937c573ae57521652fcdac9c6b3bff082740df4c95dbfcd9591cd01a22 |
| maps/RS-G0473-home-b.json | 290e6dfa304133f4249d5de87454b75b8c193adaf75b944850ed6587ed50e26f |
| maps/RS-G0473-home-b.w3m | b06081202e5188f7e6697f32e7264710a6b1d417f70d3ae4f012398ec4e55a8c |
| maps/RS-G0473-interrupt-c.ai | f599f5f5bcb18e81a1bca2c0713b80a1a5a3ea614ea4522a1a4e46ed86d20add |
| maps/RS-G0473-interrupt-c.j | 05b4602167664b42170c1cae7ad9875d148af3c803b312976822bf00291c9586 |
| maps/RS-G0473-interrupt-c.json | e55e7a795f4b16086a6cf9256a6b2be36a5246928b9ff641815294674d2a06bb |
| maps/RS-G0473-interrupt-c.w3m | f27b38ad35c70c84cd7dadca2cce72e6df4be74f3416622e1ce965093bf67ecb |
| maps/RS-G0473-large-a.ai | a7b0f19cd6ebbf1f6ad4ffab7e84b046ce511fa915a5d5031e3e85ebcb42ab26 |
| maps/RS-G0473-large-a.j | e71b37e440635b99048b35a1553ef3a2451a32d66fe22e4f7f78e27d479e6a20 |
| maps/RS-G0473-large-a.json | 8e7e8cae767ab4ea5c9a27056a9d3c2edfdddbb33af826e504a64edeaf4bc0bb |
| maps/RS-G0473-large-a.w3m | 0b7dd420fa3d115653188d6a3443149d9dca1eff933da1e6396567048f559625 |
| maps/RS-G0473-large-b.ai | 0536cf20f324bdff9119cf95a97b0dd0802d272baa6e3e724879bf2d7b98d8e1 |
| maps/RS-G0473-large-b.j | e71b37e440635b99048b35a1553ef3a2451a32d66fe22e4f7f78e27d479e6a20 |
| maps/RS-G0473-large-b.json | 4f70b5d07a5f1c887bf8b7434b060dddc694340cd9a2ab680d9cd9cfab48a040 |
| maps/RS-G0473-large-b.w3m | 7e0c4d6a3b81bb7e8e1dfe800de0c834fd8c879eca25293efc8b4472fc2cb3f5 |
| maps/RS-G0473-noflee-c.ai | 24f4042a70e299fb7cc029eae6a9ffbbb227232b9b74e20bf4091780dd39cf29 |
| maps/RS-G0473-noflee-c.j | 1a786ed9404c18e6a94db9edf8812c2496b0f0b26fe9a8b7235f31c20b1cf443 |
| maps/RS-G0473-noflee-c.json | 949b7d28fa34cd8651885f284055ca1e6007384598bd83eb45d0f0f527e76d79 |
| maps/RS-G0473-noflee-c.w3m | f3a2a20f000e8dcd710f399d3cd89999962b9460d5710c0b5dd3ab7d323ba5e2 |
| maps/RS-G0473-retreat-c.ai | 24f4042a70e299fb7cc029eae6a9ffbbb227232b9b74e20bf4091780dd39cf29 |
| maps/RS-G0473-retreat-c.j | 75edb6627c27b5da0cba59deb8df784ea11682cd2bd5a8e1ac405b48231d80ad |
| maps/RS-G0473-retreat-c.json | 68b092c910371212438cd763b3c58b3a1b4d130cf76c459c237aa466ed2e68f5 |
| maps/RS-G0473-retreat-c.w3m | f288651b4603672803151ac1f8255d0f4929794ac7e8eab44757f197c149e63f |
| maps/RS-G0473-threshold7-c.ai | 9ae70c703510829b79217dffa220e23fbb29dd2cd97f83e113a0eb067b15cafe |
| maps/RS-G0473-threshold7-c.j | 7fb5fcc92d64ab7988da38164747a3925aa8fb4e4dd027e698058e68921f9c81 |
| maps/RS-G0473-threshold7-c.json | 89812e218f6f7d3a22c03097c30f4e1072d0757c19970c0f495add7a751432f8 |
| maps/RS-G0473-threshold7-c.w3m | f186deb1a32572a2b103f7e874cf369b595a015eb373aac5506836bcb7d16ee0 |
| tools/frida/research/GROUP-03.4.7.3_analyze.py | 409588c725b9e8cc9f1688079a6e4284f954ed784375aff056a74a56f7976e75 |
| tools/frida/research/GROUP-03.4.7.3_expected.py | 904cfc0662d199729ec8a27e0c895cf0824b022dd33ffb6e5f6b8ff01803d4c0 |
| tools/frida/research/GROUP-03.4.7.3_make_map.py | c3c7cf4e09045ba804d02836e3788f991145f78f36d1e47d085a576ade8ef15f |
| tools/frida/research/GROUP-03.4.7.3_observer.js | f0f15d5169e756fe57ebc4a340f1ad6db16271af951d9e340e4ea025dcfb73bd |
| tools/frida/research/GROUP-03.4.7.3_probe.ai | b858b9a40a65f39f8f07c7edf95890610594193e151f28751f0eb4bdf0157e4c |
| tools/frida/research/GROUP-03.4.7.3_probe.j | 27073d98657da6592127ec7f5338ac3f81a2847bcabe0219e3542e6f7ac7d88b |
| tools/ghidra/research/verify_GROUP-03.4.7.3_policy.py | 1079008f254665bd16015c5cc292f5b846c0762932e87068147cbc5f5cce5875 |
| tools/frida/research/GROUP-03.4.6.2.1.2_capture.py | 2944b0d042621ae83461cda94517ca2d111ce968668a4e4bd7a9567880de7d88 |
| tools/frida/research/GROUP-03.4.6.2.1.2_control_compare.py | 2cae252498affd9657d2622ae11efa37c837e4943371903765fa6a669b85754f |
| shared tools/frida/make_wc3_pathfinding_map.py (imported read-only) | 08656832143f7be6662ad58cbc3dbb9b2d048ae6f1283c48651a9049e44b3dbf |
| control-compare-interrupt.json | cce93d94f461f55c6c2d16e5859a75a6199bb568099126ecbe9ed1e1764960e5 |
| control-compare-noflee.json | f419da1da2828452e27738f8e0b0a4e198955893ee6ca15f6960c5fe6794d75b |
| control-compare-retreat.json | 6ec47fe0f0e3cf7c40dff1bd8ddbe524599ac6233a6b5e8281ce934d2bc02fc8 |
| control-compare-threshold7.json | 1610f12622797a23eef32900f5a3d57096cbf16160156fa8c4a56786db582b96 |
| expected-GROUP-03.4.7.3.json | b75657f092e51809c6ed267341d0bce789a87f122445dba9913851cce8f7368c |
| expected-interrupt.json | 20a798345ad037ea3185dcf8eb7cd571a51d120ff32bf8874163d4e4de48c7cb |
| expected-noflee.json | 8f3adb15415768adb222720ec40e735ea80b11233cb783624664b88eff48ab9f |
| expected-retreat.json | 830bfb097b9b97ee1488672b691d24d50bded3402fedeb8693d954211d716a76 |
| expected-threshold7.json | 54e3c6a74bd5cdf06683cd8e96073222cb88bc504b1e1f494772eb52db550e25 |
| ghidra-comments.json | 27faaef8cb5308cc2b7f2612ced17c545991292693c777a34c901498a4f52af7 |
| model-check.json | 231eafee96f8f94910f4d1ce7b0f421813e2f697a7e38740d4767f45cafaef1b |
| types-GROUP-03.4.7.3.json | 469f5ae5f5884660365929b8c4e6de248fdcde31e9dbeb575993c33ba5952914 |
| mapping-rows-GROUP-03.4.7.3.txt | 810c7ac99734a6fe5bbbf2fe766fbbef7690df266cf6b2a587fe9470a5168682 |
| ghidra-writes.jsonl | 4279da97278378496d9cb1496b73f519aa18562d9fdff29fbb616bc70aab9ccf |
| proposed-docs-GROUP-03.4.7.3.md | bb6767a22a47a849c5a366ec5378b794d32f58a403b26c237cb1a514164a4f29 |
| finalize.py | b05e8911bad0d1251f34f0df69019e7ec55f9050594396276bc5e72bb3a5c08d |
Seeds: none supplied; observer-free controls with identical JASS streams show the scenes are deterministic. Decompiled retail bodies used for analysis are in asm/ (research directory only).

## Integration addendum — Payoff159

The historical "not saved" and "periodicity not proven" statements above are
superseded by [Captain policy integration](../../retail-pathfinding-captain-policy.md).
The Ghidra program was saved, nine new policy/provenance comments read back,
and all eleven peer policy function names confirmed. Two fresh readonly repeats
verify the one-second period through128 updates and12 speed publications; their
373 public markers equal the observer-free control. The GetUnitState registration
and MANA branch prove that the c0 operand unit+b8 is current mana.
Non-combat policy, CaptainAttack, retained requests and periodic deadlines now
reach the engine with failing-first regressions and Save133. The existing TODO
remains open for full new trajectories, combat and the other stated exclusions.
