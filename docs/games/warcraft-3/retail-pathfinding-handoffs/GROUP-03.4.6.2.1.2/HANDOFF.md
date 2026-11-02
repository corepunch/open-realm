<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-03.4.6.2.1.2/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **GROUP-03.4.6.2.1.2**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-03.4.6.2.1.2/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-GROUP-03.4.6.2.1.2.json` -> [`GROUP-03.4.6.2.1.2-expected.json`](../../../../../tools/ghidra/fixtures/research/GROUP-03.4.6.2.1.2-expected.json) (uncompressed sha256 `ceaf13b5dee8d701bd362e669362151677129e89ca6ff58561a11aca219c40b5`, 64034 bytes)
> - `expected-f.json` -> [`GROUP-03.4.6.2.1.2-expected-f.json`](../../../../../tools/ghidra/fixtures/research/GROUP-03.4.6.2.1.2-expected-f.json) (uncompressed sha256 `3fba4a928c7c8ea200b49dd35faec06349b74ade78280c8650bdabd81dea1c4c`, 24682 bytes)
> - `expected-g.json` -> [`GROUP-03.4.6.2.1.2-expected-g.json`](../../../../../tools/ghidra/fixtures/research/GROUP-03.4.6.2.1.2-expected-g.json) (uncompressed sha256 `a2beb3afc5ceba0e866486a0fed4d29fc4ff2bc48e002834bf7d0da27b6aa2b7`, 31344 bytes)

# GROUP-03.4.6.2.1.2 handoff — remaining Captain approach-range producers

**Status.** Every remaining branch of the private Captain approach range `9d86f0` now has a publicly
reachable, live-verified producer except the target-adjusted maximum, which is **unreachable** in 1.27
(assembly: its only setter has no code or data reference; live: the target identity is `-1` in every call).
Established at **A + L** (two identical observed repeats per scene plus a JASS-identical observer-free
control): Hero minimum (strict `>`, 883 → 600, 884 → 600.4), illusion (`unit+5c.40000000`) and BTLF early
return (**50.0, not 0** — prior Ghidra/engine claim corrected), no-Attack-object fallback 300
(`UnitRemoveAbility(u,'Aatk')`), melee/ranged/special attack-slot suppression from the Attacks Prevented
buffs, weapon-type and special-target gating, Long Rifles upgrade input, illusion/BTLF roster admission
(`GroupTimedLife`, default on), private re-creation on an outer-circle departure, and retention of the stored
physical range when the producer changes during an active private approach. Siege 6c.20 and roster bonus were
not repeated (Payoff121). **Not established:** retail save/load of these states (only the A-level
serializer list below) and Cloud/Silence (only Drunken Haze clones were cast; all three classes share the same
apply/remove methods, A).

## Functions

| VA | Name (gw.py) | ABI (assembly) | Role / evidence |
|---|---|---|---|
| 6f9d86f0 | CaptainAI_GetAuthoredApproachRange (existing) | thiscall ECX captain, +4 out, +8 unit, RET8, EAX=out | order: `5c&40000000` → 50; `48cb80(unit,'BTLF',1,0,1,1)`≠0 → 50; `unit+1e8`==0 → 300; else `70+0.6*max` (soft-float 6f06f9c0/6f06fbb0, 0.6=`3f19999a`); then if rawcode byte+33 in A..Z and not illusion: `COMISS 600 > v → 600`; then `captain+6c&20` → +200. A+L |
| 6f4985c0 | Attack_GetMaximumEnabledRange (existing) | thiscall ECX attack, +4 out, RET4 | max over slots 0/1 of enabled `258+8*slot` starting at 0; target-adjusted `2cc` only if `2bc/2c0` valid **and** resolves **and** weapon type 2..8 **and** targets not special — unreachable (below). A+L |
| 6f499790 | Attack_IsWeaponSlotEnabled (existing; ABI was single-arg in Ghidra) | thiscall ECX attack, +4 slot, RET4, EAX 0/1 | `20 & (80000<<slot)`; `224>0` && type==1 && targets∉{1,40,80,100} → 0; `228>0` && 4996f0 → 0; `22c>0` && 3ba140 → 0. A+L |
| 6f4996f0 | Attack_IsRangedSuppressibleSlot | thiscall ECX attack, +4 slot, RET4 | type `dc[slot]` in 2..8 and targets not special. A |
| 6f3ba140 | Attack_IsSpecialTargetSlot | thiscall ECX attack, +4 slot, RET4 | targets `218[slot]` ∈ {1,40,80,100}. A+L (tree-only slot = `0x40`) |
| 6f497da0 | Attack_AdjustSuppressionCounters | thiscall ECX attack, +4 release, +8 melee, +c ranged, +10 special, RET10 | ±1 on 224/228/22c; on increment re-tests current slot 2b8 and cancels via 49d280. A+L |
| 6f501ae0 / 6f521550 | BuffAttackPrevention_Apply / _Remove | thiscall ECX buff, void | vtable +364/+368 shared by CBuffSilence 6faf4458, CBuffCloudOfFog 6faf99b4, CBuffDrunkenHaze 6fb4ff8c; mask `buff+f0` (Nsi1) bit1→melee, 2→ranged, 4→special, 8→48f380. A+L (caller 501b38 live) |
| 6f206310 | JassNative_IsUnitIllusion | cdecl handle | returns `5c>>30 & 1`. A |
| 6f205540 | Unit_IsHeroNotIllusion | thiscall ECX unit, EAX 0/1 | same predicate as inlined in 9d86f0. A |
| 6f62af90 | Unit_CreateIllusionCopy | (fastcall; not typed) | CAbilityItemIllusion → 677870 flags 10000 → `or [unit+5c],40000000` at 677ac7. A+L (`5c=40001005`) |
| 6f9bbb90 | JassNative_GroupTimedLife | cdecl bool | town `2d0.100`; town init 9c7b20 writes `2d0=80181` (on by default). A |
| 6f9c9d60 | TownAI_AssignOwnedUnitCaptain | thiscall ECX town, +4 unit, RET4 | illusion/BTLF → 9ccdb0; others → defense captain (or attack with 2d0.2000). A |
| 6f9ccdb0 | TownAI_AssignTemporaryUnitCaptain | thiscall ECX town, +4 unit, RET4 | with 2d0.100 attaches to attack captain 9c64c0. A+L (attach caller 9cce50 for both illusions) |
| 6f9c32d0 | CaptainAI_RecruitOwnedPool (existing) | — | rejects `5c & 42000000` (illusions) but **not** BTLF: a BTLF Footman created before AI start is recruited by AddAssault (caller 9c348e). A+L |
| 6f49c4d0 | Attack_SetRetainedTargetUnreferenced | thiscall ECX attack, +4 agent, +8 range, RET8 | only writer of `2bc/2c0/2c8/2cc` besides ctor 491f30 (−1/0) and load 499860; **no code or data references** in the image. A |
| 6f9d8eb0 | CaptainAI_HandleMemberRangeDeparture (existing) | — | mode0: `bc−c4 > floor(bc/10)` → 9d16c0 whole-roster (no private reissue), else `ReissueMemberOrder(d0012, captain)` → fresh 9d86f0 (caller 9d8ff4). A+L (Ghidra decompile hides the `jmp 9d8ff4` after 9d16c0) |
| global 6fd3c7ac | Math_RuntimeFifty | WC3PathScalar | static init 6f001cd0 `Math_FromInteger(50)`; live `42480000`. A+L |

## Ghidra writes (gw.py, logged in `ghidra-writes.jsonl`; not saved)
Renamed 12 default functions (497da0, 4996f0, 3ba140, 501ae0, 521550, 206310, 205540, 62af90, 9bbb90, 9c9d60,
9ccdb0, 49c4d0), labelled `6fd3c7ac Math_RuntimeFifty`, appended plate comments to those plus 9d86f0/4985c0/
499790 (texts in `ghidra-comments.json`, mirrored in `mapping-rows-GROUP-03.4.6.2.1.2.txt`). No types/prototypes
applied; see `types-GROUP-03.4.6.2.1.2.json`.

## Fields

| Struct+off | Meaning | Encoding | Writers | Readers |
|---|---|---|---|---|
| Unit+5c bit 40000000 | illusion | flag | 677ac7 (illusion copy) | 9d86f0, 205540, 206310, 9c32d0 (mask 42000000), 9c9d60, AI 9d0650… |
| Attack+224/228/22c | melee/ranged/special Attacks-Prevented counts | i32 | 497da0 | 499790; saved by 496e80, loaded by 499860 |
| Attack+2b8 | current attack slot | i32 | ctor/attack | 497da0 |
| Attack+218/21c | targets allowed per slot (special = exactly 1/40/80/100) | u32 | data | 499790, 4996f0, 3ba140, 4985c0 |
| Attack+dc/e0 | weapon type (1 normal, 2 missile, 4 instant, …) | u32 | data | idem |
| Attack+2bc..2cc | retained target identity + FloatMini adjusted range | ident/float | ctor (−1/0), 49c4d0 (unreferenced), load | 4985c0, 4984a0 |
| Town+2d0 bit 100 | GroupTimedLife | flag, default 1 | 9bbb90, 9c7b20 | 9ccdb0 |

Types fragment: `types-GROUP-03.4.6.2.1.2.json` (sha256 in Provenance) adds Attack 224/228/22c/2b8, appends
Unit+5c evidence, 11 method ABIs and the global.

## Behaviour (public producers; frozen `expected-GROUP-03.4.6.2.1.2.json`, sha256 ceaf13b5dee8d701bd362e669362151677129e89ca6ff58561a11aca219c40b5)

Scene: 15 Player(0) recruits (Footman-derived `hRA*`, Paladin-derived `HRA*`) at (-1936+80c,-976-80r), attack
captain home (-1936,-144), `StartCampaignAI` at tick 9 (0.1 s ticks). Pre-admission changes at tick 2.
All initial calls: caller 5fd922 (Move task 0xd0170), captain 6c=1, reissue caller 9d8ad2. Words are raw.

| Unit | Input | 5c | Attack 20 / counters | slots → max | 9d86f0 word (world) | stored Move b0 word (fine) |
|---|---|---|---|---|---|---|
| hRA0 Footman 90 | — | 205 | 22084012 / 0,0,0 | 1,0 → 42b40000 | 42f80000 (124) | 409b0000 (4.84375) |
| hRA1 melee 90 | Haze Nsi1=1 | 205 | 1,0,0 | 0,0 → 0 | 428c0000 (70) | 404a0000 (3.15625) |
| hRA2 missile 400 | Nsi1=2 | 205 | 0,1,0 | 0,0 → 0 | 428c0000 (70) | 404a0000 |
| hRA3 missile 400 | Nsi1=1 | 205 | 1,0,0 | 1,0 → 43c80000 | 439b0000 (310) | 412a8000 (10.65625) |
| hRA4 missile 400 | none (post: Nsi1=2) | 205 | 0,0,0 | 1,0 → 43c80000 | 439b0000 (310) | 412a8000 |
| hRA5 Footman | `UnitApplyTimedLife(BTLF)` pre | 205 | — (no Attack reads) | — | 42480000 (50) | 40220000 (2.53125) |
| hRA7 Footman | `UnitRemoveAbility('Aatk')` pre | 205 | Attack ptr 0 | — | 43960000 (300) | 41258000 (10.34375) |
| hRA9 Rifleman | (post: Rhri) | 205 | type 4 | 1,0 → 43c80000 | 439b0000 (310) | 412b0000 (10.6875, radius 32) |
| hRAs tree-only 200 | Nsi1=4 | 205 | 0,0,1, targets 40 | 0,0 → 0 | 428c0000 (70) | 404a0000 |
| hRAt tree-only 200 | Nsi1=1 | 205 | 1,0,0, targets 40 | 1,0 → 43480000 | 433e0000 (190) | 40dd0000 (6.90625) |
| HRA0 Hero 100 | — | 1205 | slot1 disabled | 1,0 → 42c80000 | 44160000 (600) | 419e0000 (19.75) |
| HRA1 Hero 883 | — | 1205 | | → 445cc000 | 44160000 (600) | 419e0000 |
| HRA2 Hero 884 | — | 1205 | | → 445d0000 | 44161999 (600.4) | 419e1999 (19.7625) |
| Footman illusion (tick 40) | Wand of Illusion on hRA0 | 40001005 | captain 6c=1001 | none | 42480000 (50) | 40220000 |
| Hero illusion | Wand on HRA0 | 40001005 | | none | 42480000 (50) | 40240000 (2.5625) |

Illusions attach at tick 40 through 9cce50; at tick ~630 they expire and detach through 9d7906 (member removal).
Shared re-admission one tick after each private admission writes `3efae148` (0.49) into b0, as already known.

**Replacement and retention (scene g).** Members leave the outer circle one at a time with `SetUnitPosition(u,-2000,-1744)`
every 110 ticks; each departure (9d8eb0 mode0 with `bc−c4 = 1`) reissues d0012 from 9d8ff4 and calls 9d86f0 again.
A change 10 ticks after the departure (during the private walk back) leaves the stored b0 word untouched until the
shared re-admission 20–30 ticks later; the next departure reads the new state:

| Unit | dep-1 tick: range / b0 | change (tick) | b0 after change | dep-2 tick: range / b0 |
|---|---|---|---|---|
| hRA4 | 90: 310 / 412a8000 | Haze Nsi1=2 (100; counters 0,1,0) | 412a8000 until 110 | 530: 70 / 404a0000 |
| hRA6 | 200: 124 / 409b0000 | BTLF (210) | 409b0000 until 220 | 640: 50 / 40220000 |
| hRA8 | 310: 124 / 409b0000 | remove Aatk (320) | 409b0000 until 330 | 750: 300 / 41258000 |
| hRA9 | 420: 310 / 412b0000 | Long Rifles (430; slot ranges 600/200) | 412b0000 until 450 | 860: 430 / 41670000 (14.4375) |
| HRA0 | — | — | — | 970: 600 / 419e0000 |

Teleporting two members at once (scene e) or with a still-missing inner member (scene f, ticks 90–580) instead
runs the whole-roster 9d16c0 branch (`bc−c4 = 2 > 1`): no private reissue, no 9d86f0 call.

**Model replay (A+L).** `tools/ghidra/research/verify_GROUP-03.4.6.2.1.2_model.py` recomputes all 45 frozen
9d86f0 calls (both scenes) from the captured input words alone and reproduces every range word, every enabled-slot
trail and all 45 stored `(range+radius)/32` b0 words, provided the multiply/add/divide **truncate toward zero** like
the original soft-float helpers: Hero 884 gives `44161999`, IEEE round-to-nearest would give `4416199a`
(`model-check.json`, passed=true).

**Target-adjusted range:** all calls read `2bc/2c0 = ffffffff/ffffffff`, `2cc = 0`; the branch cannot be reached.

## Reproducer

```sh
cd /run/media/lofcz/ssd_external/GitHub/open-realm
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research; P=/home/lofcz/.local/share/uv/tools/frida-tools/bin/python
python3 tools/frida/research/GROUP-03.4.6.2.1.2_make_map.py --base /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/Human02Interlude-original.w3m --tool build/bin/mpqtool --output $R/GROUP-03.4.6.2.1.2/maps/RS-G0346212-g.w3m
$R/_env/install-map.sh $R/GROUP-03.4.6.2.1.2/maps/RS-G0346212-g.w3m
$R/_env/live.sh $P tools/frida/research/GROUP-03.4.6.2.1.2_capture.py --data {DATA} --map 'Maps\RS-G0346212-g.w3m' --mode observe --remote {REMOTE} --x11-display {DISPLAY} --seconds 260 --continue-at 30 --output $R/GROUP-03.4.6.2.1.2/captures/g-observe-N-env{ENV}.jsonl
$R/_env/live.sh $P tools/frida/research/GROUP-03.4.6.2.1.2_capture.py --data {DATA} --map 'Maps\RS-G0346212-g.w3m' --mode control --remote {REMOTE} --x11-display {DISPLAY} --seconds 260 --continue-at 30 --output $R/GROUP-03.4.6.2.1.2/captures/g-control-1-env{ENV}.jsonl
python3 tools/frida/research/GROUP-03.4.6.2.1.2_analyze.py --capture <capture.jsonl> --report <capture>-report.json
python3 tools/frida/research/GROUP-03.4.6.2.1.2_expected.py --report <f-1> --report <f-2>      # identical=true
python3 tools/frida/research/GROUP-03.4.6.2.1.2_control_compare.py --prefix RSG --reference <control-preload> --other <observe-preload>...
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_GROUP-03.4.6.2.1.2_model.py --expected $R/GROUP-03.4.6.2.1.2/expected-GROUP-03.4.6.2.1.2.json
```
Scene f (creation/illusions) used the previous probe revision; `maps/RS-G0346212-f.j` is its exact instrumented
script (the repository probe now holds the scene-g schedule; ticks 0-55 are identical).

## Provenance
| Item | sha256 |
|---|---|
| game.dll 1.27.1.7085 | d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236 |
| base runtime/Human02Interlude-original.w3m | 199683be6cd1a888ade00f3dce1e3e93f16e92e05724717048b033108b5a2104 |
| maps/RS-G0346212-f.w3m | a2419941a8d598f5057759f48b2b77c04535dd64e9abf56e195e3d9c9d024807 |
| maps/RS-G0346212-f.j | 21df3b3dffb6ab9f7f1f0b9dae8db8d2e2634ebc39ba5123c6caf4dcde37d833 |
| maps/RS-G0346212-f.w3u | a8a331ab9bcc5143985dbe0a36dec1784cb46f50854d2e28b658376d61f61118 |
| maps/RS-G0346212-f.w3a | e82b533514000f86ee8086fcf2ffdb647e5d1b76c15497989f338da22fa81f87 |
| maps/RS-G0346212-f.ai | b6ab97b5fd85e296751d49fb8161e6f7bd09fae0cfdcf1461b677099bfd4a3a1 |
| maps/RS-G0346212-g.w3m | a0ec9c0acdbc5dac48b5e044f215e32c2a979f3b319b2a14e83814722ff6a397 |
| maps/RS-G0346212-g.j | 0b982422654026f17846fda71c2426a886d8be4352946e0eca50a1a4bc453d91 |
| maps/RS-G0346212-g.w3u | a8a331ab9bcc5143985dbe0a36dec1784cb46f50854d2e28b658376d61f61118 |
| maps/RS-G0346212-g.w3a | e82b533514000f86ee8086fcf2ffdb647e5d1b76c15497989f338da22fa81f87 |
| maps/RS-G0346212-g.ai | b6ab97b5fd85e296751d49fb8161e6f7bd09fae0cfdcf1461b677099bfd4a3a1 |
| maps/RS-G0346212-d.w3m | 7d829d85a285dd80c360dfa7405d86b04721fc31fd4a1c9be9f71c6594a8329e |
| maps/RS-G0346212-d.j | 1bc92996316cb610f421fcaa76bfe28de746dd5d28024b6ddd909227336ecc6e |
| maps/RS-G0346212-d.w3u | a8a331ab9bcc5143985dbe0a36dec1784cb46f50854d2e28b658376d61f61118 |
| maps/RS-G0346212-d.w3a | e82b533514000f86ee8086fcf2ffdb647e5d1b76c15497989f338da22fa81f87 |
| maps/RS-G0346212-d.ai | b6ab97b5fd85e296751d49fb8161e6f7bd09fae0cfdcf1461b677099bfd4a3a1 |
| maps/RS-G0346212-e.w3m | e0539e6180afce1b42e7f6329936df5c9a3872883c716c4fca02884b003ce9ec |
| maps/RS-G0346212-e.j | 57defff5ca88b733ccf0af44418406e61cbcd2b7495b630860f01f4ddcf94d0e |
| maps/RS-G0346212-e.w3u | a8a331ab9bcc5143985dbe0a36dec1784cb46f50854d2e28b658376d61f61118 |
| maps/RS-G0346212-e.w3a | e82b533514000f86ee8086fcf2ffdb647e5d1b76c15497989f338da22fa81f87 |
| maps/RS-G0346212-e.ai | b6ab97b5fd85e296751d49fb8161e6f7bd09fae0cfdcf1461b677099bfd4a3a1 |
| tools/frida/research/GROUP-03.4.6.2.1.2_analyze.py | 0a75ed6c436618b49072a567fee6c8f391d2556a1293d994cc5260e0aa2a42cc |
| tools/frida/research/GROUP-03.4.6.2.1.2_capture.py | 2944b0d042621ae83461cda94517ca2d111ce968668a4e4bd7a9567880de7d88 |
| tools/frida/research/GROUP-03.4.6.2.1.2_control_compare.py | 2cae252498affd9657d2622ae11efa37c837e4943371903765fa6a669b85754f |
| tools/frida/research/GROUP-03.4.6.2.1.2_expected.py | e1e59447f62fe8b379e678d714b731de4a6390dfddf0960301725ab31de13397 |
| tools/frida/research/GROUP-03.4.6.2.1.2_make_map.py | 39a83492766ce9d3dd39828465e5665bc561bf120069f568e042217a503dedbe |
| tools/frida/research/GROUP-03.4.6.2.1.2_observer.js | 418b436f4d625e64e3d315242418566766a28675d0356247fb97ae40f9b56724 |
| tools/frida/research/GROUP-03.4.6.2.1.2_probe.ai | 4048f15e6e5ba1b2cce2668c4f33f771c3ddaf5b0708d40bfbed378d4c4afca3 |
| tools/frida/research/GROUP-03.4.6.2.1.2_probe.j | 73900f61278b3c5ad48d788be5c221b73a8692b38ac48994b746e6eb03b532d4 |
| types-GROUP-03.4.6.2.1.2.json | 207da03151d1b71adb3690d0ca6f13226825714fd579a449ca45c318fdfffec2 |
| mapping-rows-GROUP-03.4.6.2.1.2.txt | 06adcb0d1c4a346087b8749464c72b25f47c09c7d5a3140aaff5f41bed89c32f |
| ghidra-comments.json | 09ed80b0b3b24db12ce7edeb6f5cd47afffefd02b09bbd2f911a27a9492cb443 |
| ghidra-writes.jsonl | 068d1fda3dfb34695b2d5783e8b4a5353aecb4e24cf643d0e674279bc9dbade6 |
| proposed-docs-GROUP-03.4.6.2.1.2.md | 088751e9e606f8a65a20a7de9c502990eabe27b507754b1079e5f5884c2804ec |
| expected-GROUP-03.4.6.2.1.2.json | ceaf13b5dee8d701bd362e669362151677129e89ca6ff58561a11aca219c40b5 |
| shared make_wc3_pathfinding_map.py (imported read-only) | 08656832143f7be6662ad58cbc3dbb9b2d048ae6f1283c48651a9049e44b3dbf |
Seeds: none supplied; game RNG state is whatever the engine derives for the map (identical JASS streams across observed and control runs show the scene is deterministic).

## Captures (including failures)
| Capture | sha256 | rows | note |
|---|---|---|---|
| captures/a-explore-1-envC.jsonl | 62a06dd41d1289ead526705e3eb9a97a6d07970129bba6a82384e30a5794a42a | 12 | FAILED: map rejected before loading (JASS parameter named `ability`); no markers |
| captures/a-explore-2-envB.jsonl | 933fa1b0fc50f26b265dff31c7a865094bd8916a1f5d65bd87052fcf579e94d4 | 16 | FAILED: same map, screenshots show main menu |
| captures/b-explore-1-env.jsonl | 07a2eaf94a6f5c7b192dca556906781583db9a0a81ec6df8f061c573a319ea74 | 15 | FAILED diagnostic: same script without w3a, still rejected (isolates script) |
| captures/c-explore-1-envB.jsonl | 4229b45ee9fdafc5d53703ffe5b92dc6cfdcd6710ad27b817821a1edb2956957 | 429 | PERTURBED: instruction probes at 9d871e/9d8743/4985dc/49865c; every armed range 70; excluded |
| captures/d-observe-1-envC.jsonl | 93edc15ae682cdde3296173b5caa6ff4b0d799bed516e046af9b0b6fb5335339 | 505 | first clean function-hook capture (wand fixed); creation values equal f |
| captures/diag-knowngood-env.jsonl | e49927c1c67ad641717d21d4d63ccb6f48bdbc8de954c7bec463fae6daac6f8a | 4 | control: PathCaptainRanges121e loads (environment OK) |
| captures/e-control-1-envC.jsonl | 2d42d876d28abf2d9c7a09cc0f256959d6041168965a0c7253f32655416debad | 4 | observer-free control for e |
| captures/e-observe-1-envB.jsonl | 7abd637eba9f4dbed3f4b051f374d8ebfc0522c73051bb5d2b67cf497d1b749e | 581 | two simultaneous teleports -> whole-roster branch, no private reissue |
| captures/f-control-1-envC.jsonl | 20a7e330917f5e29cf842e80f6ea550212c3f7f069e8894a74f790e941379341 | 9 | observer-free control; 1258 JASS markers equal both repeats |
| captures/f-observe-1-envC.jsonl | 258a3f1f50b48a75c2ff3f654b1682f2d08b113c1a1f5128e459788e51d52eee | 1559 | creation + illusions + 2 private reissues (Rhri 430, Hero 600); repeat A |
| captures/f-observe-2-envB.jsonl | 87e8b923364cb06a32aaf9ecf020ba8a2867bea288f4dfda7b1458c6eae0afa5 | 1559 | repeat B, normalized identical to A |
| captures/g-control-1-envB.jsonl | 61588f3c91b00c078049bb4199173a2cfcfc60ec867edd01628b55085eec2371 | 10 | observer-free control; JASS markers equal |
| captures/g-observe-1-envB.jsonl | 0877e2b6f623b15e3cfb74c48cfe99a66b5cdfcc01750e52786d005be9d372d5 | 2252 | replacement/retention repeat A |
| captures/g-observe-2-envC.jsonl | 868becbeb491ba1cd7759de1f313c87a093d457aa6f95b02f7fee4f845b2532d | 2252 | replacement/retention repeat B |
| captures/c-explore-1-envB-preload.txt | dc57af1d873eebb3705a0456f58b827441b4f1fb1a068d1366e55144962b99c1 | | JASS Preload output |
| captures/d-observe-1-envC-preload.txt | fab0ff72bedb2e6bda9be5a9797d5333067c519b676fdbd3f9593013fa7aae02 | | JASS Preload output |
| captures/e-control-1-envC-preload.txt | 29d3ef9ba273e7e51fba65741a279efa60c2fd03e4ff6d3514ef71d899285c0b | | JASS Preload output |
| captures/e-control-1-envC-previous-preload.txt | fab0ff72bedb2e6bda9be5a9797d5333067c519b676fdbd3f9593013fa7aae02 | | JASS Preload output |
| captures/e-observe-1-envB-preload.txt | b89451219234ab2afd05922fdf0bccecd5ad8935bc72893596cc3c1a1bc25d81 | | JASS Preload output |
| captures/e-observe-1-envB-previous-preload.txt | dc57af1d873eebb3705a0456f58b827441b4f1fb1a068d1366e55144962b99c1 | | JASS Preload output |
| captures/f-control-1-envC-preload.txt | db9313ff0adac9185da7de5ca7f891e58a5ad949d5382289b677cd44807a633a | | JASS Preload output |
| captures/f-control-1-envC-previous-preload.txt | 29d3ef9ba273e7e51fba65741a279efa60c2fd03e4ff6d3514ef71d899285c0b | | JASS Preload output |
| captures/f-observe-1-envC-preload.txt | 51144f9c581a72d8946dadc6cb9e095da37677d9bf5b174fc67940517427afbd | | JASS Preload output |
| captures/f-observe-1-envC-previous-preload.txt | db9313ff0adac9185da7de5ca7f891e58a5ad949d5382289b677cd44807a633a | | JASS Preload output |
| captures/f-observe-2-envB-preload.txt | d6ace42c63fa5adefb4a6a44f36318ee06dfb568b696a9a0e6582898fc250161 | | JASS Preload output |
| captures/f-observe-2-envB-previous-preload.txt | b89451219234ab2afd05922fdf0bccecd5ad8935bc72893596cc3c1a1bc25d81 | | JASS Preload output |
| captures/g-control-1-envB-preload.txt | 4d6a3613838959a0b59b7f97971f43c3e258ed97f2aa09b41bdd9a1e534d0f78 | | JASS Preload output |
| captures/g-control-1-envB-previous-preload.txt | d6ace42c63fa5adefb4a6a44f36318ee06dfb568b696a9a0e6582898fc250161 | | JASS Preload output |
| captures/g-observe-1-envB-preload.txt | 91c0569ebc6f21c84399f176c25f5782f0e1d801af90930748eb1cef2f2d125b | | JASS Preload output |
| captures/g-observe-1-envB-previous-preload.txt | 4d6a3613838959a0b59b7f97971f43c3e258ed97f2aa09b41bdd9a1e534d0f78 | | JASS Preload output |
| captures/g-observe-2-envC-preload.txt | 9daa21347d4699292f9a5ca1b5a140180ae70f77c3e692c672a691d064fa7eb0 | | JASS Preload output |
| captures/g-observe-2-envC-previous-preload.txt | 51144f9c581a72d8946dadc6cb9e095da37677d9bf5b174fc67940517427afbd | | JASS Preload output |

## Public vs forced state
All producers are public: object data (w3u/w3a), JASS natives (`CreateUnit`, `UnitApplyTimedLife`,
`UnitRemoveAbility`, `IssueTargetOrder` drunkenhaze, `UnitAddItemById`/`UnitUseItemTarget` Wand of Illusion,
`SetPlayerTechResearched`, `SetUnitPosition`), AI natives (`SetCampaignAI`, `GroupTimedLife`, `CreateCaptains`,
`SetCaptainHome`, `InitAssault`, `AddAssault`). The observers only read memory. No forced-state experiment.

## Preserved mismatches / corrections
1. Ghidra 9d86f0 plate, `MapPathfinding.java` row and engine comment: "native unit5c.40000000 or BTLF gives0"
   — **wrong**; the early return writes `Math_RuntimeFifty` = 50.0. Engine `s_move.c` sets `world=0` for BTLF.
2. Engine applies BTLF after the siege bonus (`world=0` overrides) — retail returns before Hero minimum and
   siege bonus; with the corrected 50 the order matters (no +200, no 600 clamp).
3. Engine `G_UnitIsHero` uses hero stats; retail uses rawcode high byte A..Z and not illusion.
4. Engine has no illusion branch and no Attacks-Prevented suppression; `S_UnitAttackApproachRange` treats a unit
   whose Attack ability was removed as armed if its weapon data is present (retail: Attack pointer null → 300).
5. Ghidra signature of 499790 lists one parameter; assembly is ECX attack + stack slot (RET4).
6. Exploratory capture `c-explore-1` used instruction probes inside 9d86f0/4985c0 and returned 70 for every armed
   recruit (probe perturbation of the soft-float sequence); preserved, excluded. Captures a/b failed to load
   (JASS parameter named `ability` shadows a type); preserved.

## Exclusions
Siege threshold/roster bonus (Payoff121). Retail save/load of buffs, illusion flag and counters is not captured
(A-level: CAbilityAttack save 496e80 writes 224/228/22c/2b8/2bc/2c8; load 499860 restores them) — proposed
**GROUP-03.4.6.2.1.3**. Combat-time producers (attack target acquisition) do not affect this range because
2bc is never set. Cloud/Silence casts share the A-level apply/remove path but were not cast live.

## Engine entry points
- `games/warcraft-3/game/skills/s_move.c` `move_follow_approach_range()` (captain branch): order exactly as 9d86f0;
  illusion (`aiflags & AI_ILLUSION`) or `unit_findstatus(BTLF)` → world 50 and return before Hero/siege;
  Attack-ability absent (runtime removal of Aatk, not only data) → 300; Hero = rawcode first char 'A'..'Z' and
  not illusion; `if (600 > world) world = 600`; then siege +200. Keep `wc3_mul`/`wc3_add`/`wc3_div` truncation
  semantics (Hero 884 must yield `44161999`, physical `419e1999`).
- `games/warcraft-3/game/skills/s_attack.c` `S_UnitAttackSlotEnabled()` / `S_UnitAttackApproachRange()`: three
  suppression counts fed by Attacks Prevented (Nsi1) buffs of Silence/Drunken Haze/Cloud; melee mask disables
  weapon type normal unless special targets; ranged mask disables types 2..8 unless special; special mask
  disables slots whose targets are exactly none/tree/wall/debris. Max starts at 0 and uses enabled slots only.
- AI (`g_bot.c` / roster admission): `GroupTimedLife` default **true**; newly owned illusions/timed-life units
  attach to the attack captain only through that policy; AddAssault recruitment excludes illusions but not BTLF.
- `g_save.c`: persist suppression counts (or the buffs that recompute them), illusion flag and timed-life buffs so
  fresh post-load admissions see the same producer state; stored physical ranges already persist (Save115).

## Failing-regression expectations
Use the public scene (or a reduced engine scene with the same inputs): attack captain home (-1936,-144), recruits
as in the table, `StartCampaignAI` at 0.9 s. Expect the 9d86f0 world words above, i.e. `(range+radius)/32` b0 words
`404a0000` (70+31), `412a8000` (310+31), `40220000` (50+31), `41258000` (300+31), `40dd0000` (190+31),
`419e0000` (600+32), `419e1999` (600.4+32), `40240000` (illusion Hero 50+32). Departure scene: a single member
leaving the outer circle receives a fresh private range from its *current* state (310→70 after a ranged-mask haze,
124→50 after BTLF, 124→300 after Aatk removal, 310→430 after Long Rifles), while a change during the active private
approach keeps the old b0 word; two simultaneous departures produce no private reissue. Save/load in the middle of
each walk-back must keep the stored word and reproduce the same fresh value afterwards.

## Proposed new IDs
- GROUP-03.4.6.2.1.3 — retail save/load of attack suppression counters, illusion flag and BTLF across a captain
  private approach (needs a public SaveGame/LoadGame retail scene).
