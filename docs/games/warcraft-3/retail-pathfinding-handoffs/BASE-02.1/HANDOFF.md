<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/BASE-02.1/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **BASE-02.1**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/BASE-02.1/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-BASE-02.1.json` -> [`BASE-02.1-expected.json`](../../../../../tools/ghidra/fixtures/research/BASE-02.1-expected.json) (uncompressed sha256 `32ee710114a0f3d3e51bd1ad200c0d13e9006c380c4181fd31cfc43633c4b3d5`, 78829 bytes)

# BASE-02.1 research handoff — movement types: authored producer, lanes, masks, support surface

**Status.** Established: (1) the complete authored producer `movetp` → bits → query/category/class, by
instruction reading, an original-code Unicorn oracle (game.dll + Storm.dll + msvcr120.dll, 60 strings,
810 table calls, 60 builder-slice runs) and live custom-w3u capture (15 `umvt` clones, 852 live parses
byte-exact, 24 birth publications); (2) the lane each type uses (fine query, published class, adaptive
lane mask); (3) live runtime producers: crow-form morph (Amrf on hfoo, Arav on edot), Ensnare
forced-ground push/pop, Burrow category toggle, SetUnitFlyHeight + Amrf flag; (4) the support-Z
function and its per-type branches, instruction-level plus live ground/hover/flyer/Amrf/landing/
Ensnare witnesses on land. Two complete traced runs are identical after normalization and the
observer-free control reproduces all 574 JASS markers. **Not established:** live water/bridge/cliff
support rows (owned by MAP-02.2), live submerge/harvest-ghost/wind-walk/locust/land-mine/tornado
producers (instruction-level only), save/load of `Unit+1fc`/`+200` (instruction-level only). Not a closure.

## 1. Functions (VA | role | ABI | evidence)

| VA | Role (Ghidra name, applied via gw.py) | ABI (asm-verified) | Evidence |
| --- | --- | --- | --- |
| 6f66bf40 | UnitProfile_BuildFromUnitData | thiscall builder; slice 66c909..66c944 writes row+1a8/1ac/1b0 | asm 66c90f..66c93e; Unicorn slice (only 6b3640 stubbed); live 852 parses, caller 66c91b |
| 6f6b3640 | UnitDataSlk_GetMovementTypeString | thiscall ECX SLK row, stack4 out, RET4; column +e0 `movetp` | asm; live strings byte-exact |
| 6f6b3620 / 6f6b3600 | UnitDataSlk_GetMoveHeight / GetMoveFloor | thiscall, stack4 out, stack8, RET8; columns +ec/+f8 → row+1f8/+1fc (66d210/66d227) | asm |
| 6f685340 | UnitMovementType_ParseName (coordinator) | fastcall ECX char*; plain RET; EAX bits; ECX==0 → 0 | asm; oracle 60 strings; live 852 |
| 6f685e30 | UnitMovementType_ToPathingMask (coordinator) | fastcall ECX bits, EDX selector (≠0 category, 0 query); plain RET; EAX zero-extended | asm 685e30..685e97; oracle 135 inputs × EDX {0,1,2,80000000,ffffffff} |
| 6f685db0 | UnitMovementType_ToPathClassFlags (existing) | fastcall ECX bits; plain RET; 2→6, 10→4, 20→2, else 0 | oracle 135 inputs; live 30 publications |
| 6f678af0 | UnitProfile_GetMovementType | fastcall ECX rawcode; EAX row+1a8 (miss 0) | asm |
| 6f678a80 / 6f678a10 | UnitProfile_GetMoveHeight / GetMoveFloor | fastcall ECX out float*, EDX rawcode; EAX=out (miss → runtime zero) | asm 678a87/678a89 |
| 6f678b70 | UnitProfile_GetTargetedAs | fastcall ECX rawcode; row+1b4 (targType&1e) | asm |
| 6f6785d0 | (unnamed) type-data copier | copies row+1a8→data+6c, moveHeight→+70, moveFloor→+74 | decompile+asm; callers 670a00, 677902 |
| 6f68a060 | (unnamed) unit birth init | 68a4dc `Unit+1fc = data+6c`; 68a4e2/68a4ee class publish; 68a0d5 `Unit+5c|=20000000` iff bits==2; 68a164 adaptive on | asm; live caller words 68a4f3/68a169 for all 24 units |
| 6f670950 | Unit_RebindTypeRetainingPublicIdentity (existing) | thiscall ECX unit, stack4 new rawcode, RET 0x28 | asm 670b06 (1fc), 670b18 (class), 670def/dff (5c), 670e2f (15c authored), 670ea2 (adaptive type≠2); live 6 rebinds |
| 6f66d780 | CUnit_GetSupportZ (vtable 6fb77eb0+e4) | thiscall ECX unit; stack +4 point*, +8 layer (→ECX of 78d1e0), +C out-on-deck*, +10 force; RET 0x10; **x87 ST0** | asm 66d780..66d98d; live 684480 outputs |
| 6f68f390 | CUnit_GetFlyHeightOffset (vtable +e8) | thiscall ECX unit; x87 ST0 | asm; live (Amrf vs control) |
| 6f684480 | Unit_RefreshSupportPosition (existing) | thiscall ECX unit, stack4 out xyz*, stack8 force; sets Unit+280 bit2 (deck) 684599, bit20 (deep water via 64eca0) 6845c2 | asm; live 25k calls |
| 6f64eca0 | PathWorld_IsDeepWaterPoint | stack x,y by value; RET 8; terrain point blocks 02 and not 40 | asm |
| 6f6877b0 / 6f69c840 | CUnit_PushForcedGround / PopForcedGround | thiscall ECX unit, stack4 out*, stack8 rate*; RET 8 | asm; live Ensnare caller 5e81fa / 5d92d3 |
| 6f699840 | CUnit_SetTargetedAs | thiscall ECX unit, stack4 flags; RET 4; Unit+24c | asm; live 4→2→4 |
| 6f544c60 | AbilityMorph_DispatchFormTransition | fastcall ECX ability; fly→ground 569b80, ground→fly 56a000, else vt+3f8 | asm; live |
| 6f569b80 / 6f56a000 | AbilityMorph_BeginLanding / BeginTakeoff | fastcall ECX morph ability | asm 569bd3..569cf0; live (2 each) |
| 6f56bc30 | AbilityMorph_SetForms | thiscall ECX ability, stack4 normal, stack8 alt; RET 8; Unit+20 bit 800 | asm; live |
| 6f4ba8d0 / 6f4ba940 | AbilityBurrow_/AbilitySubmerge_SetUnitCategory | fastcall ECX unit, EDX hide, stack4 keepBuild; RET 4 | asm; burrow live, submerge asm only |
| 6f699220 / 6f699290 | CUnit_PublishGhostProfile / RepublishAuthoredProfile | fastcall ECX unit | asm only |
| 6f9d6e60 / 6f9d2610 | CaptainAI_PublishMovementType / GetQueryMask | thiscall ECX captain, stack4 bits, RET4 / fastcall | asm only (captain virtual actor, not a unit) |
| 6f2152c0 / 6f203bf0 | Jass_SetUnitFlyHeight / Jass_GetUnitFlyHeight | natives; Get returns Unit+208 raw | asm; live |
| 6f698680 / 6f698630 | CUnit_SetFlyHeight / SetFlyFloor | thiscall; RET 0x10 / RET 4 | asm; live |
| 05c7e0 / 05c6f0 / 05c7b0 / 05c770 / 0594a0 | existing bridge publishers | unchanged | live hooks (caller words recorded) |

Storm ordinal 509 at game.dll IAT 6fa7c830 = Storm.dll `1503a5a0` (SStrCmpI) → Storm IAT `15041254` =
msvcr120 `_strnicmp`, max length 7fffffff.

## 2. Fields

| Struct+off | Meaning | Width | Writes | Reads | Evidence |
| --- | --- | --- | --- | --- | --- |
| profile row+1a8 | movement bits (685340) | u32 | 66c924 | 678b40 (678af0) | asm, oracle, live |
| row+1ac / +1b0 | category / query byte | u32 | 66c931 / 66c93e | 690cd0 / 690c70 | asm, oracle, live |
| row+1b4 | targType & 1e | u32 | 66c97a | 678b70 | asm |
| row+1f8 / +1fc | moveHeight / moveFloor (float) | f32 | 66d219 / 66d230 | 678adb / 678a6b | asm |
| CUnit+1fc | current movement bits | u32 | 68a4dc birth, 670b06 rebind, 569cbe landing (=1), 68d134 save-load, 568f86 (unreferenced setter) | 66d949/66d960 support, 68f39b fly offset, 670ddb | asm; live |
| CUnit+200 | forced-ground counter | s32 | 6877bf ++, 69c846 --, 68d14b load | 69cf4a, 66d8d2, 546420 | asm; live 0→1→0 |
| CUnit+204..+210 | fly-height clamp object: +208 value, +20c min (moveFloor), +210 max (moveHeight) | f32 | 670ab0..670afc, 698630, 698680 | 69cf5b/63, 66d901, 203c06 | asm; live |
| CUnit+214 | fly-height interpolator | obj | 698664, 69cf6c | 68f3c7 | asm |
| CUnit+20 bit 800 | "fly-height applies" | bit | 56bc30 (set iff either morph form flies, else clear) | 68f394 | asm; live: Amrf add sets, removal keeps |
| CUnit+5c bit 20000000 | flyer support branch | bit | 68a0d5, 670def/670dff (bits==2) | 66d8c6 | asm; live |
| CUnit+24c | current targeted-as | u32 | 699840 | — | live 4→2→4 under Ensnare |
| CUnit+280 bit 2 / 8 / 20 / 10 | on deck / elevPts>1 / deep water / 697700 predicate | bits | 684599 / 670dc9, 68ac92 / 6845c2 / 68a13b | 66d790, 66d7aa, 66d969 | asm; live (bit 8 for flyers' elevPts) |
| CUnit+284/288/28c | cached support x/y/z | f32 | 684622/68462b | 66d7f2..66d862 | asm |

## 3. Behaviour (frozen in `expected-BASE-02.1.json`, sha256 `32ee710114a0f3d3e51bd1ad200c0d13e9006c380c4181fd31cfc43633c4b3d5`)

Parser (case-insensitive exact; NUL terminates): `fly`2 `hover`8 `foot`1 `horse`4 `unbuild`40 `float`10
`amph`20; `_` `-` `none` `""` and every other tested text → 0 (incl. `foot,fly`, `hover,float`, ` foot`,
`foot `, `foo`, `flyy`, `amphibious`, `boat`, `unbuildable`, `unamph`, `f\xf6\xf6t`). `FOOT\0junk` → 1.

| movetp (live clone) | bits | query | category | class flags→class | adaptive lane mask (6fce4570[class]) | birth adaptive | fly offset e8 | support Z (66d780) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| foot (hM00), horse (hM01) | 1, 4 | 02 | ca | 0→0 | 06000006 | 1 | 0; Unit+214 if Unit+20&800 | terrain/deck (78d1e0) + e8 |
| hover (hM03, umvh 50) | 8 | 02 | ca | 0→0 | 06000006 | 1 | row moveHeight | max(terrain/deck, water) + moveHeight; live 640+50=690 |
| fly (hM02 umvh150/umvf40, FLY hM10) | 2 | 04 | 00 | 6→3 | 04000004 | 1 (rebind→0) | Unit+214 | e8 + base + (z(-3)−base)·e8/max (max>0.01) |
| float (hM04, Float hM11) | 10 | 40 | ca | 4→2 | 40000040 | 1 | 0 | max(base, water) |
| amph (hM05) | 20 | 80 | ca | 2→1 | 80000080 | 1 | 0 | water only if Unit+280&20 (deep) |
| unbuild (hM06) | 40 | 00 | 08 | 0→0 | 06000006 | 1 | 0 | terrain/deck |
| none/""/_/-/foot,fly/boat (hM07..09,12..14) | 0 | 00 | 00 | 0→0 | 06000006 | 1 | 0 | terrain/deck |

Water/deck/cliff rows for the same branches are live-verified in `../MAP-02.2/HANDOFF.md` (8 points × foot/hover/float/amph/fly,
8068 Unicorn cases): ground/deck strictly-higher rule, hover/float water max, flyer on-deck flag 0, and the amphibious
one-refresh lag — 684480 calls e4 (6f68458e) **before** updating Unit+280 bit20 from 64eca0 (6f6845b9/6845c2), so the first
refresh after entering deep water still uses the seabed. Suggested regression 6 below.

Live runtime witnesses (types2, identical in repeat):
- Amrf add (`56bc30` from OnAdd 55d409): Unit+20 1646→1e46; UnitRemoveAbility keeps 1e46. SetUnitFlyHeight(200,0)
  writes +208/+210=200 for Amrf hfoo, control hfoo and hsor; GetUnitFlyHeight = 200 for all three; support Z:
  Amrf hfoo 768→968, control hfoo 768, hsor 552 (unchanged).
- Crow form (Amrf hfoo idx18, Arav edot idx19 with Redt): takeoff → class 6 (670b1d), authored nmdm/edtm 0/04 (670e35,
  694602), adaptive 0 (670ea7), Unit+1fc 1→2, Unit+5c +20001000; landing `569b80` → ca/02 (569ce8), adaptive 1 (569cf5),
  Unit+1fc 2→1, **class not republished, Unit+5c 20000000 kept** until delayed rebind (class 0, authored ca/02,
  adaptive 1). Landing-interim support uses the flyer blend (idx18 Z 731.755 with Unit+1fc=1).
- Ensnare on hgry (Player 1): push (caller 5e81fa, CBuffEnsnare*) → 15c ca/02 (687816), Unit+200 0→1, Unit+24c 4→2,
  **Unit+1fc stays 2, no class publish**; Z descends from 863.306 to 595.688 (consistent with ground support + moveFloor 90; terrain Z at that point not separately captured); pop (5d92d3) → 0/04 (69c899).
- Burrow ucry→ucrm: rebind authored ca/02, then 4ba8d0(EDX=1, stack0) → c8/02 (twice); unburrow EDX=0 → ca/02 before
  the reverse rebind.

## 4. Reproducer (repository root)

```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research/BASE-02.1; D=/run/media/lofcz/ssd_external/Games/w3-research
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_base021_movement_types.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report $R/oracle-movement-types.json
python3 tools/frida/research/base021_make_map.py --base /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/Human02Interlude-original.w3m \
  --tool $R/tools/bin/mpqtool --output $R/maps/RS-BASE-02.1-types2.w3m && cp -n $R/maps/RS-BASE-02.1-types2.w3m $D/Maps/
flock /GitHub/wc3-analysis/reports/pathfinding-1.27/research/_env/live.lock /home/lofcz/.local/share/uv/tools/frida-tools/bin/python \
  tools/frida/research/base021_trace.py --data $D --remote 127.0.0.1:27048 --map 'Maps\RS-BASE-02.1-types2.w3m' \
  --x11-display :97 --seconds 130 --continue-at 80 --output $R/captures/<new>.jsonl
python3 tools/frida/research/verify_base021_types_trace.py $R/captures/types2-first.jsonl --oracle $R/oracle-movement-types.json \
  --compare $R/captures/types2-repeat.jsonl --report $R/captures/types2-first-vs-repeat.json
flock .../live.lock /home/lofcz/.local/share/uv/tools/frida-tools/bin/python tools/frida/control_wc3_pathfinding.py --data $D \
  --map 'Maps\RS-BASE-02.1-types2.w3m' --scenario base021_types --reference $R/captures/types2-first.jsonl \
  --output $R/captures/<new-dir> --remote 127.0.0.1:27048 --x11-display :97 --seconds 130 --continue-at 80
python3 tools/frida/research/base021_freeze_expected.py --oracle $R/oracle-movement-types.json \
  --capture $R/captures/types2-first.jsonl --repeat $R/captures/types2-repeat.jsonl --output $R/expected-BASE-02.1.json
```
mpqtool is a snapshot of the owner's build (bin+lib) in `$R/tools/` (hashes `tools/SHA256SUMS`).

## 5. Provenance

game.dll `d51e5680…8236`; Storm.dll `36339f69…eb72`; msvcr120 `86e39b59…615f`; base map `199683be…2104`.
Map types2 `a014b468…80f0` (j `fd683e9f…58f2`, w3u `f5f2cce9…a545`); types (v1) `c3b88fc7…d298` (j `b16f75f8…`, probe v1 copy
`maps/base021_probe-v1.j` `e2b77b48…`). Scripts: base021_probe.j `780b5652…`, base021_make_map.py `bba7c662…`,
base021_observer.js `e2d7ef18…`, base021_trace.py `0a4a1c25…`, verify_base021_types_trace.py `4fbe02d2…`,
base021_freeze_expected.py `8a7f6f0f…`, verify_base021_movement_types.py `9a4f1533…`; imported make_wc3_pathfinding_map.py
`c336f39e…`, wc3_shipped_crt.py `631036bb…`, control_wc3_pathfinding.py `a576debb…`. Oracle report `d8eb3749…`.
No random seed is supplied; determinism established by two traced runs + one control (Frida 17.18.0).

## 6. Captures

| Capture | sha256 | Status |
| --- | --- | --- |
| captures/types-first.jsonl (v1 probe) | b7ff8703… | complete (307 markers, 267 stock); births pass; **Ensnare not cast (target invulnerable), Arav refused (missing Redt)** — probe-setup failures, preserved, not used for those producers |
| captures/types2-first.jsonl | 8430d632… | complete; accepted; 4325 rows, parse 852, map 1716, publish 70, producers 40 |
| captures/types2-repeat.jsonl | 54a6e29e… | complete; normalized births/events/parse/support-states identical (`types2-first-vs-repeat.json` 3422f95f…) |
| captures/types2-control-1/ | preload 9e5a8232… | observer-free; 574/574 markers equal (`comparison.json`) |

## 7. Observer controls
One observer-free run (spawn/resume/kill only) reproduces all 307 PATHTRACE and 267 PATHSTOCK markers (positions,
GetUnitFlyHeight, GetLocationZ, unit-type flags, orders, order acceptance). The observer only reads memory.
684480 output counts depend on presentation frames; the checker compares first/last and ordered state words only.

## 8. Exclusions
Water/bridge/cliff support rows and fixtures → MAP-02.2. Object categories for buildings/destructibles → BASE-02.2.
Captain AI producer (9d6e60/9d2610) behaviour → captain tasks (CAPTAIN/ROUTE ledger). Placement with float/amph masks on
land (hM04 stayed at its requested land point; hM01 moved) → FOOT-04.1. Fly-height interpolation timing (69cf20,
057fd0/058120) and save/load of 1fc/200 → proposed BASE-02.9 / existing SAVE tasks. Submerge, harvest/wind-walk ghost
(query |10), Locust (0,0), LandMine (8,8), TornadoWander (0,4), WispHarvest → proposed BASE-02.8 (asm only).

## 9. Mismatches preserved
- Seed note claimed 6877b0 "horse→fly override": wrong; it tests authored **targType** air bit (row+1b4) and is the
  forced-ground push.
- Ledger payoff100 says type rebind toggles adaptive by movement enum; confirmed, but forced-ground and morph-landing
  change query/category **without** a class publish, so the adaptive lane stays 3 (mask 04000004) while the fine query is 02.
- GetUnitFlyHeight reports 200 for a ground unit whose support Z did not change (Unit+208 vs e8 offset).
- Several idle units drifted from their placed points (e.g. idx 6 with order 851983 at tick 150; idx 13, 14, 16, 17); cause not investigated (enemy Player-1 gryphon nearby). Deterministic across all three runs.
- `types-first-check.json` was produced by an earlier checker revision (rawcode string orientation only).

## 10. Proposed integration
`mapping-rows-BASE-02.1.txt` (28 rows + coordinator rows; all applied to Ghidra by gw.py, log `ghidra-writes.jsonl`),
`proposed-docs-BASE-02.1.md` (ledger section, TODO note, proposed BASE-02.8/02.9).

## 11. Suggested failing engine regressions
1. Authored parser: custom `umvt` `FLY`/`Float`/`unbuild`/`foot,fly`/`""`/`boat` → bits 2/10/40/0/0/0; query/category/class
   exactly as table §3 (unbuild: query 00, category 08, class 0).
2. Ensnare an authored flyer: query 02, category ca, `Unit+200`=1, movement class remains 3, support uses the ground branch
   with the fly offset interpolating to moveFloor; on expiry restore 04/00 without a class publish.
3. Crow-form landing: between landing start and rebind the unit queries 02 / category ca with class 3 retained, then rebind
   publishes class 0 and authored profile; takeoff publishes class 3 and authored 0/04 and disables adaptive routing.
4. Burrow: category ca→c8 (query 02 kept) on burrow, back to ca on unburrow.
5. SetUnitFlyHeight(200) on a foot unit: GetUnitFlyHeight returns 200, support Z unchanged; after UnitAddAbility+Remove
   Amrf, support Z = terrain+200. Hover support = terrain + authored moveHeight regardless of SetUnitFlyHeight.
6. Amphibious unit moved from shallow into deep water: first support refresh keeps ground/seabed Z, the next uses the water
   surface (deep flag is refreshed after the height; entry lag live in MAP-02.2, exit lag instruction-level only).
