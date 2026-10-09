<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/BASE-02.2/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **BASE-02.2**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/BASE-02.2/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-BASE-02.2.json` -> [`BASE-02.2-expected.json`](../../../../../tools/ghidra/fixtures/research/BASE-02.2-expected.json) (uncompressed sha256 `486b75d962cb10ad8fff948c177e698290ad95907472f19f08bcca5a47c709ae`, 4561 bytes)

# BASE-02.2 handoff — object categories, tags, ownership and consumer eligibility (research, not a closure)

**Status.** Instruction-verified producer inventory for every object kind that writes the fine map
(`owner+238`): mover objects (units by movement type, items, CaptainAI virtual actors, missiles,
destructable/building own movers) and static region objects (widget pathing textures, CTriggerRegion).
Category words, the static flag and the four consumers' eligibility are **original-code oracle verified**
(FOOT-03.1: 50,112 predicate cases, 0 mismatches; FOOT-03.2: 16 real-producer scenarios) and **live
verified** for Footman `010000ca`, item `01000018` and an LTcr crate's three regions `c2/10/08`
(FOOT-03.2 capture, 164 live consumer calls = model). Not established: live building / under-construction
/ gold mine / Way Gate / ward rows (same code path, instruction only), destructable own-mover category,
missile fine objects, item pathing textures. Consumer predicates themselves are handed off in FOOT-03.1.

## Functions (new names written to Ghidra under rule 0; rows in `mapping-rows-BASE-02.2.txt`)

| VA | Role | ABI (asm) | Evidence |
| --- | --- | --- | --- |
| `6f14cf20` SpatialMap_CreateObject | CPmRegion (vt `6fa90948`) from pool `owner+5d8` (14cb60/14c880), registry activation vt+c→`1c53f0`; +2c map,+30 payload,+1c..+28=-1,+34..+40=0 | thiscall ECX map; [esp+4] payload, [esp+8] descriptor/NULL; RET 8; EAX object | asm; executed (FOOT-03.2) |
| `6f14cf70` SpatialMap_CreateStaticObject | 14cf20 then `or [eax+40],10000000` (`6f14cf7e`) | thiscall ECX map; payload, desc; RET 8 | asm; executed |
| `6f15ed40` Mover_ActivateSpatialPose (existing) | proximity object on `owner+234` (`15ba40`), fine object on `owner+238` (`15bb30`), then `or [eax+34],01000000` at `6f15ee0a` (fine only) | thiscall, RET 4 | asm; `15ee0a` executed |
| `6f064460` RegionCollection_ResizeWithCategories | retire surplus (14dae0), create missing via 14cf70, `+34 = old&ff000000 | 01000000 | cat&ffffff` (`6f064533..49`) | thiscall ECX {cap,count,objs}; [4] owner agent, [8] count, [c] category array; RET c | asm; executed; live |
| `6f6501a0` Widget_ApplyPathing (vt+148, all widget classes) | `05c660(keep==0)`, `05c4a0(0)`, `05c630(publish==0)`; texture `6524c0(primary)`→res+cc `pPathingFootprint` / +d0 `pPathingFootprintAlt`; masks `c2,10,08` (+`04` iff res+64 bit0); raster `22e9c0`+`652b40`; gated `04e0b0`; publish→vt+16c | thiscall; [4] publish, [8] keepOwnOccupancy, [c] primaryTexture; RET c (`6503a9`) | asm; call sites below |
| `6f063b40` RegionCollection_Retire | 14dae0 each object (static ⇒ no removal records, +38=-1), Storm403 array if cap>1 | thiscall, RET plain | asm; executed |
| `6f05c660` UnitMoverBridge_SetFineOccupancyDisabled | arg≠0 clears mover+98 `+34` bit 01000000 (`05c67c`), else sets (`05c696`); `15fb40` republishes bounds | thiscall ECX bridge, [4], RET 4 | asm; live (RemoveUnit ⇒ `000000ca`) |
| `6f05c7b0`/`05c770`/`05c7e0` (existing) | category low24 into fine `+34`; query to owned path +9c | thiscall RET 4/4/8 | existing + live |
| `6f653790` Widget_SetPathingCategory (vt+15c non-unit) | SetCategory(arg) only | thiscall, RET 8 | asm |
| `6f65a8a0` CItem_RefreshMoverPathingProfile (CItem vt+16c) | radius max(vt168,1.0)→05c2e0; SetCategory(vt160=`18`) | fastcall-like thiscall, RET plain | asm; live |
| `6f657ae0`/`6f657ad0` CItem_GetPathingCategory/QueryMask | `18` / `10` | `mov eax,imm; ret` | asm |
| `6f650d10`/`6f650d00` Widget_GetPathingCategory/QueryMask | `0a` / `2` (CWidget, CSelectable, CDestructable, CCaptainAI) | `mov eax,imm; ret` | asm |
| `6f05f970` CTriggerRegion_CreateSpatialObject (vt+60) | 14cf70 on owner+238 → region+20; `+34` never written ⇒ inactive | thiscall, RET plain | asm |
| `6f699260` CUnit_PublishPathingProfileWithBit10 | vt160/164 `|10` both → vt15c; caller `43b7e0`←`424220` | thiscall | asm |
| `6f623bd0` AbilityGhost_SetUnitCategory; `4ba8d0`/`4ba940` (BASE-02.1 names) | category ^ `02`/`c2` and `08` per flags; query vt158 | fastcall ECX unit, EDX flag, [4] flag; RET 4 | asm |
| `6f59cdf0`, `6f5adad0`, `6f63c2c0`, `6f63c430` | TornadoWander (0,4), WispHarvest (0,0), LandMine (8,8), Locust (0,0) via unit vt15c | vtable methods (RTTI) | asm |
| `6f14dab0` SpatialObject_ReleaseIfUnreferenced | dead & `+3c&ffffff==0` ⇒ vt+10 (`1c5490`) | stdcall [4], RET 4 | asm; executed |
| `6f22e9c0`, `6f652b40`, `6f650a70` | texture raster; insert (`01000000`) / remove (`0`) record callbacks | existing ABI (widget oracle) | executed |

Widget_ApplyPathing call sites (args publish,keep,primary): units/items/rebind `(1,1,1)` (`658cb6`,`65b04b`,`670e74`,`68eb64`,`6981f0`,`699c45`);
destructable create `6c2155` `(0,1,alive)`; death `6c152d` `(0,1,0)`; `6c3661` `(0,1,0)`; restore `6c3846` `(0,1,1)`.
Note `05c4a0/05c530/05c630` toggle proximity-object (`mover+94`, map `owner+234`) bits 08000000/02000000/04000000 — not read by any fine consumer.

## Object/field table (CPmRegion, 0x44+ bytes)

| Field | Meaning | Writes | Reads |
| --- | --- | --- | --- |
| +14/+18 | registry slot / generation | 1c53f0; 14dae0 (-1) | 054530 resolvers |
| +1c..+28 | saved Y/X rectangle (exclusive max) | 14cf20 (-1×4), 14e770 | 14dae0 removal emit |
| +2c / +30 | owning map / payload (unit mover tag `60706375`, item mover `60706f73`, widget agent `+agl` at +c) | 14cf20 | 148ad0→1480d0 |
| +34 | bit 01000000 active; low24 category; other high bits unused by consumers | 15ee0a, 05c660, 05c7b0, 064460, 14d000 (load) | all four predicates |
| +38 | per-query visit stamp; -1 = retired | predicates, 14dae0 | predicates, 14dab0 |
| +3c | low24 outstanding cell records | 14d9e0 (+1), 14e050 (−1) | 14dab0 |
| +40 | low28 suppression count; 10000000 static; 20000000 moving; 40000000 group transient; 80000000 excluded (no writer found) | 14cf7e; 1606e0/15fe80/15eee0; 169aa8/16a410/16b038/16c2ea/16c337/16dbf0; inc/dec sites (FOOT-03.1) | predicates |

## Behaviour table (frozen in `expected-BASE-02.2.json`, sha256 `486b75d962cb10ad8fff948c177e698290ad95907472f19f08bcca5a47c709ae`)

| Object kind | Producer | +34 (live words) | Static | Fine search / segment (mode0) | Endpoint (mode1) | Hierarchy | Collector token |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Unit foot/horse/hover/float/amph | 15ed40 + 05c7b0(685e30) | `010000ca` (live) | no | blocks 02/08/40/80 when idle | blocks 02/08/40/80 (also moving) | never | unit mover |
| Unit fly | same | `01000000` | no | none | none | never | — |
| Unit unbuild / LandMine | same / 63c2c0 | `01000008` | no | build(08) only | build only | never | unit mover |
| Unit movetp none (buildings' own mover, halt) | same | `01000000` | no | none | none | never | — |
| CaptainAI virtual actor | 9d2f90/9d6e60 | `01000002`→`(table&~8)` | no | 02 (c2: +40/80) | same | never | (payload tag not checked) |
| Item | CItem 65a8a0 | `01000018` (live) | no | item(10)/build(08) | same | never | NULL (tag 60706f73) |
| Widget region c2 | 064460 | `010000c2` (live) | yes | 02/40/80 | same | ground(06)/float/amph lanes | NULL |
| Widget region 10 | 064460 | `01000010` (live) | yes | item query only | same | none | NULL |
| Widget region 08 | 064460 | `01000008` (live) | yes | build only | same | none | NULL |
| Widget region 04 (res+64 bit0) | 064460 | `01000004` | yes | fly(04) | same | ground(06)+flight lanes | NULL |
| CTriggerRegion | 05f970 | `00000000` | yes | never | never | never (links count to 49 cap) | — |
| Destructable own mover | Widget_ApplyPathing keep=1 | category unpublished (inference: 0) | no | — | — | never | — |

Consumer columns are the FOOT-03.1 oracle `composed` rows (exact bits in its expected JSON). No consumer reads player
ownership: the four predicates read only map/cell words, link kind, object +30/+34/+38/+40 and fine-system +a4/+a8/+d4
(asm `1489a0`,`148e90`,`148ad0`,`149170`). Owner/ally policy appears only after collection (`168360`, ROUTE/SEP).

## Reproducer (repository root)

```sh
# static evidence: research/_ghidra/{dis.sh,rng.sh,ctx.py,vt.py} over full-text.dis (objdump of the hash-guarded DLL)
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_FOOT-03.1_eligibility.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report /tmp/foot031.json   # exit 0, 0 mismatches
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_FOOT-03.2_two_categories.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report /tmp/foot032.json --expected /tmp/foot032-exp.json
```
Live reproduction: see FOOT-03.2 HANDOFF (same capture provides the live words above).

## Provenance
Assembly excerpts of every cited function: `static-asm.txt` (sha256 `5375c0cc0e7b3acc3734e1f5bfd959866b1f10af8ba9f34255e5402f5b620c88`), cut from `research/_ghidra/full-text.dis` (`objdump -d -M intel`, sha256 `c8026b7d…4d9a`).
game.dll `d51e5680…d8236`; objdump listing `research/_ghidra/full-text.dis`; oracle/live artifact hashes in FOOT-03.1/03.2
HANDOFFs. Ghidra writes: `ghidra-writes.jsonl` (25 renames/comments + `15ee0a` pre-comment); renames of `063d10`/`05bd30`
refused (already MAP-04.2 `WidgetList_ToggleSpatialExclusion` / `UnitMoverBridge_ToggleSpatialExclusion`, compatible; comment appended).

## Exclusions (owners)
Authored movetp parser/support surfaces: BASE-02.1. Public construction/building and under-construction live rows,
invalidation timing: E2E-02.2/E2E-06.2. Way Gate special byte6 markers: GATE-01..04/ACC-02.2. Proximity-map bits
(05c4a0/05c530/05c630): SEP. Missile pathing: no owner — proposed **BASE-02.8** (missile/destructable own-mover
fine-object activity and category). Item pathing textures (CItem vt+b0 constant 1 path): proposed under BASE-02.8.

## Mismatches preserved
- Ledger (`retail-pathfinding-search.md` "category … unresolved", engine "ground-unit eligibility until BASE-02 supplies the
  complete table"): superseded by this table; no contradiction with BASE-02.4 words.
- Seed note "685e30 unbuild → query 00/category 08" confirmed; `6877b0` horse→fly override not re-verified here (BASE-02.1).

## Proposed integration
`mapping-rows-BASE-02.2.txt`; `proposed-docs-BASE-02.2.md`. Suggested failing engine regressions:
1. Item at a cell: ground query passes, item(10)/build(08) queries blocked; item never blocks a Footman route.
2. Crate/region categories `c2/10/08`: ground/float/amph blocked, flyer clear, item admission displaced, build blocked.
3. Unit with movetp `unbuild` blocks only building placement; `fly` blocks nothing.
4. Unit occupancy never changes adaptive classes; widget `04` region marks both ground and flight lanes.
