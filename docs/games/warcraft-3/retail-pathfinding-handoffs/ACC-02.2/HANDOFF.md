<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/ACC-02.2/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **ACC-02.2**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ACC-02.2/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-ACC-02.2.json` -> [`ACC-02.2-expected.json`](../../../../../tools/ghidra/fixtures/research/ACC-02.2-expected.json) (uncompressed sha256 `936fca1e64b6deae13da79dcf14212cf0d603e0045f28d0d9c8d9d2562207be1`, 126043 bytes)

# ACC-02.2 handoff — special-marker and object classification domains

**Status.** Established at *instruction-verified + original-code (Unicorn) producer* level: the byte6
special-marker domain and the fine-object domain add **no four-lane class tuple beyond the 54 ordinary
ones** (base and level-1 parent), every (54 tuple x marker 0/nonzero) child state is built by original
producers, every per-lane reducer path with markers is witnessed, class pair `11` and nonzero byte6 above
the base are rejected by a writer inventory (static scan + xref closure + dynamic write census), and a
mixed parent over four clear children is reachable **only** through a marker (missing children are
unreachable with constructor dimensions). Marked clear cells are only ever level-0 nodes; node byte20 copies
the marker iff warp mode is on; node byte23 equals a level-0 parent's byte20. Object domain: reduction proof
from `148e90` assembly plus a **supplied-record control** (control, not producer evidence). Not established:
object *eligibility/producers* (FOOT-03.1), malformed saved games, live retail. No TODO item is closed.

## Functions (ABIs from assembly)

| VA | Role | ABI | Evidence |
| --- | --- | --- | --- |
| 6f15d360 | PathMaps_UpdateRectangle | thiscall ECX owner, stack rect*/NULL, mode; 15cf80 then 3x15d470 | asm, oracle A/B/C |
| 6f15cf80 | PathMaps_RebuildBase | stack rect,map,mode RET 0xc; `MOV byte[cell+7],0` (6f15d052) before 4x15d0e0 (mode0 only) | asm, census |
| 6f15d0e0 | PathMaps_ClassifyFineBlock | ECX owner, stack cell,maskptr,shift,x,y RET 0x14; 4x1493d0; writes `cell4 = cell4 & ~(C0000000>>s) | (40000000 or 80000000)>>s` only if >=1 query blocked | asm |
| 6f15d470 | PathMaps_RebuildParent | stack rect,child map,parent map RET 0xc; `MOV byte[cell+7],0` (6f15d548) then 4x15d1c0 | asm |
| 6f15d1c0 | PathMaps_ClassifyParent | stack cell,childmap,shift,x,y RET 0x14; per-lane RMW store 6f15d348 writes only 01 or 10 | asm, B (96 cases) |
| 6f04e360 | Waygate_PublishWorldSourceRectangle | fastcall ECX marker(<0x100), EDX {minY,minX,maxY,maxX} world, plain RET | asm, oracle |
| 6f15bf60 | PathMaps_ClipAndPublishGateRectangle | ECX owner, stack marker,rect RET 8; pushes `[owner+23c]` (base map only) to 15c030, then 3x15d470 | asm 6f15bfba |
| 6f15c030 | PathMaps_WriteGateMarkerBytes | stack marker,rect,map RET 0xc; only store `MOV byte[EAX+6],CL` (6f15c0ee) | asm, census |
| 6f04e210 / 6f04e550 | Waygate_SetEdgeActive / SetWorldDestination | fastcall ECX id, EDX enabled / &worldXY, plain RET | asm (6f04e210..281) |
| 6f163ef0 | PathAcc_CreateCellNode | ECX adaptive, stack cell,level,x,y RET 0x10; `MOV [cell],stamp`, `MOV word[cell+4],AX` (16-bit); byte20 = `[cell+6]` iff `[d8]!=0` (6f163f1b..35) | asm, E |
| 6f162f20 | **PathMaps_LoadAdaptiveCells** (renamed) | thiscall ECX map, stack stream RET 4; per cell `MOV [cell],0`, `MOV [cell+4], ushort<<16` (6f162fa2..faf) | asm |
| 6f163120 | **PathMaps_SaveAdaptiveCells** (renamed) | thiscall ECX map, stack stream RET 4; writes `MOVZX word[cell+6]` (6f16315e) | asm |
| 6f159cd0 | **PathOwner_GetAdaptiveMap** (renamed) | thiscall ECX owner, stack level RET 4; EAX `[owner+23c+4*level]` | asm |
| 6f1493d0 | fine accelerator-cell query | thiscall ECX fine system, stack &xy,&mask RET 8; stores mask to `[ECX+a4]`, calls 148e90 | asm |
| 6f148e90 | PathFine_TestAcceleratorCell | thiscall ECX fine system, stack x,y RET 8; EAX 1 clear/0 blocked | asm, F control |

## Fields

| Struct+off | Meaning | Encoding | Writers | Readers |
| --- | --- | --- | --- | --- |
| adaptive cell +0 | search stamp | u32 | 163ef0; 162f20 (:=0) | 1625f0 6f1626de |
| cell +4 | node index | u16 | 163ef0 (16-bit); 162f20 (:=0) | 1625f0 |
| cell +6 | special marker (Way Gate source id) | u8, base level only | 15c030 (base map from owner+23c); 162f20 (saved) | 15d1c0 (child), 163ef0, 386cf0 (debug draw) |
| cell +7 | four 2-bit classes, lane shift 0/2/4/6 = bits 7-6/5-4/3-2/1-0 | 00 clear, 01 blocked, 10 mixed; **11 never written** | 15cf80/15d470 (:=0), 15d0e0/15d1c0 (lane RMW), 162f20 (saved) | 1625f0, size-2 predicates |
| fine link +0 / +4 | link type (hi byte) / next / object | u32 / ptr | (FOOT-03) | 148e90 |
| object +34/+38/+40 | lane bits (low byte 02/04/40/80) + 01000000 / stamp (-1 excluded) / 10000000 with bits 31,27..0 clear | u32 | (FOOT-03) | 148e90 |

## Combination inventory (the ACC-02.2 deliverable)

| # | Combination | Disposition | Evidence |
| --- | --- | --- | --- |
| C1 | 54 base tuples x marker {0, nonzero} (108 child states) | **reachable**, both producer orders (gate before/after terrain); later full 15d360 preserves byte6, marker writes preserve classes | A, `A_child_states` |
| C2 | marker on blocked / mixed child | **reachable, no effect** on that lane | B (rows `blocked`,`mixed`) |
| C3 | class2 parent over four class0 children in a lane | **reachable iff >=1 child marked** (any position 0..3); unmarked: **rejected** | B (15 cases per kind); reducer asm; constructor side `W_{l+1}=W_l>>1` ⇒ children always in range, so the missing-child path (6f15d1c0 bounds) never fires |
| C4 | class1 parent with non-blocked child; class0 parent with non-clear or marked child | **rejected**: every base/marker writer (15cf80, 15c030) is followed by 15d470 over the same rectangle (15d360, 15bf60); load restores a saved snapshot | asm 6f15d3e8.., 6f15bfba..; census |
| C5 | level-1 four-lane parent tuples | **exactly the 54** ordinary tuples with or without markers (ground parent 0 ⇒ flight 0 still holds) | D (closure over 108 child states, reducer model re-checked on original 15d1c0 in B) |
| C6 | byte6 != 0 at levels 1..3 | **rejected**: 15c030 receives only `[owner+23c]`; save/load copies values that were never written | C (`nonzero_marker_bytes_levels_1_3=[0,0,0]`), census |
| C7 | class pair 11 at any level | **rejected**: only 15d0e0/15d1c0 set lane bits (clear pair, OR 01 or 10); byte7 zeroed before each recompute; 162f20 only restores saved bytes; 163ef0 16-bit store at +4 | writer scan + census (no 11 write) |
| C8 | marker propagation | marker on (13,13) makes levels 1..3 class2 in all lanes; zero publication restores clear | C |
| C9 | marked clear cell as node | **level 0 only** (parent mixed); byte20 = marker iff warp; never on promoted nodes | E (16 requests) |
| C10 | node byte23 (incoming special) | equals the level-0 parent's byte20; seen on marked (chained: level0/src7/in6) and promoted (level3/in7) destinations | E `warp_tags` (16 requests) |
| C11 | object-produced fine blocking | per lane `blocked ⇔ (hi & m) ∨ ∃ eligible link (type 01, obj34&01000000, obj40 has 10000000 and no 8fffffff bits, stamp!=-1, within 49 links) with obj34 & m`, m ∈ {06,80,40,04}: same effective {02,04,40,80} per fine cell ⇒ **no new tuple** | asm 6f148e90..fb5; F control 512 cases |
| C12 | special destination over blocked/mixed/outside cell; promoted destination | reachable (no edge / coarse child) | ACC-01.2 `6f1653b6:taken`, transitions `special parent0->child1..3` |

Implication for engine regressions: class `11` and parent markers need no engine representation; a
class2 parent over four class0 base cells must arise only from a marker.

## Frozen results

`expected-ACC-02.2.json` sha256 `936fca1e64b6deae13da79dcf14212cf0d603e0045f28d0d9c8d9d2562207be1`
(repeat `markers-repeat.json` equal, exit 0). Examples: tuple (0,1,2,0) fine flags
`[80,80,80,c0]` → unmarked word `18000000`, marked `18060000`; marker chain words base `00090000`, levels 1-3 `aa000000`.
Static scan: `cell-writer-scan-ACC-02.2.json` sha256 `12d1296c99f03d47b82b780e9a5b38ca17f8e3537ca90995a9eebba7b336ac47` (197 candidates; in the
pathing range only 15c030/15cf80/15d1c0/15d470/162f20 touch adaptive cells, the rest are fine link,
spatial and generic-vector stores). Dynamic census `dynamic-writers-ACC-02.2.json` sha256
`15e95d388f317b120f2ad11efa8362fe8e2dc1ec8436fc81a055fbc99823519c`: 3,824 requests plus their producer builds
and mode-1/mode-0 exclusion rectangles write adaptive cells only from 6f15c0ee (+6 byte, **level 0 only**,
108), 6f15d052/6f15d548 (+7 := 0), 6f15d1a8 (15d0e0 RMW, level 0), 6f15d348 (15d1c0 RMW, levels 1-3),
6f163ef9 (+0 stamp) and 6f163eff (+4 word); **zero class-pair-11 writes**.

## Reproducer (repository root)

```sh
P=/GitHub/wc3-analysis/verify-venv/bin/python; B=/run/media/lofcz/ssd_external/Games/w3/game.dll; R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
$P tools/ghidra/research/verify_acc02_2_markers.py --binary $B --out /tmp/acc022.json --fixture $R/ACC-02.2/expected-ACC-02.2.json
$P tools/ghidra/research/acc02_2_dynamic_writers.py --binary $B --witness-maps tools/ghidra/fixtures/research/ACC-01.2-witness-maps.json --out /tmp/acc022-writers.json
uv pip install --python $P --target /tmp/accw/pylib capstone==5.0.9   # private, not the shared venv
PYTHONPATH=/tmp/accw/pylib $P tools/ghidra/research/acc02_2_cell_writer_scan.py --out /tmp/acc022-scan.json
```

## Provenance

game.dll `d51e5680...d8236`. Scripts (sha256): `verify_acc02_2_markers.py` `1c35ca43...309d69`,
`acc_research_harness.py` `c2915cca...df7929c`, `acc02_2_dynamic_writers.py` `9d6d63c68419cf37dbf5111516ff9268dfe7c35ab09cc3f07d816fbd874c0c55`,
`acc02_2_cell_writer_scan.py` `233a51d8...d94465`. Supplied preconditions: empty 64x64 fine storage
(`0x00ffffff` words), padded 41/20/10/5 headers (constructor formula), node/heap/route storage, float
constants 6fd3c740/44/48, 6fd53a74 as in accepted oracles. No live run (none needed: all producers are
static-original code paths).

## Captures

| Capture | Status |
| --- | --- |
| markers-ACC-02.2.json (run1, before warp-tag section) | superseded, kept |
| markers-run2.json = expected | complete |
| markers-repeat.json | complete, equal to frozen |
| dynamic-writers (first attempt, unbounded write hook) | killed after no progress, no output — incomplete, not evidence |
| dynamic-writers-ACC-02.2.json | complete (3,824 requests) |
| cell-writer-scan-ACC-02.2.json | complete (heuristic candidate list) |

## Observer controls

All hooks are passive (block/code/mem-write reads). The marker oracle installs no hooks except in F
(none) and the census (write hook only). Repeat equals frozen.

## Exclusions

Object category/eligibility producers and counts: FOOT-03.1/03.2. Gate lifetime, ID reuse, overlapping
erasure semantics: GATE-03/04 (payoff94 already). Malformed save streams (162f20 restores arbitrary
16-bit values): out of scope. Post-load stamp state (162f20 zeroes every cell stamp/node index while the
accelerator stamp counter's post-load value was not examined): **proposed ACC-05.4** (text only).
Out-of-map adaptive source: ROUTE-01.2 (already documented on 6f164a20 by that task).

## Mismatches preserved

- Existing `retail-pathfinding-search.md` says "Missing children classify blocked, so a boundary block
  already mixed may remain mixed": true for the synthetic 16x16/17x23/31x18 headers of
  `verify_wc3_pathing_maps.py`, but with constructor dimensions no parent has a missing child.
- The ACC-02.1 table's "arbitrary mixed parents of clear unmarked children" rejection stands; with
  markers the same parent class is legitimately reachable (C3) — engine must not treat it as invalid.

## Proposed integration

`mapping-rows-ACC-02.2.txt`, `proposed-docs-ACC-02.2.md`. Ghidra writes already applied through
`gw.py` (see `ghidra-writes.jsonl`): renames 162f20/163120/159cd0, comment 15d1c0.

## Suggested failing engine regressions

1. Publish a 1x1 source (marker 9) at base (13,13) on an open map: assert base word `00090000`, parent
   class bytes `aa` at (6,6)/(3,3)/(1,1); zero publication restores `00`.
2. For each of the 96 B cases assert parent lane classes (frozen `B_reducer_paths.rows[*].parent`).
3. Assert no class pair 11 and no nonzero marker byte above level 0 after any producer sequence.
4. Chained gates 6→7 (E `warp_tags`): node set contains (level0, src7, in6) and (level3, src0, in7);
   route reaches goal with `warp_count=2`, work 3.

## Full sha256 (computed at handoff time)

```
c2915cca134d3bd04385a5412e69dee7d462d2ca8eb985a1ae9acba15df7929c  tools/ghidra/research/acc_research_harness.py
12524db067a6b2a4a86996634f9de1071ba80b4018dc239cd52cd2adc7d23038  tools/ghidra/research/acc01_coverage_wrapper.py
57b607770185779d7d5e04914556088e4a72de58ae6204edf158f15f63217b36  tools/ghidra/research/verify_acc01_1_branches.py
5ee6d92d51bc1cc2819de720517a1b58e87ac6aa4e463095889eda8ab03d1841  tools/ghidra/research/verify_acc01_2_witnesses.py
58f113555a38c604e1b35655a88fe9102171e92f1ed95c59bb3a339d0f6636f1  tools/ghidra/research/acc01_2_find_witnesses.py
1c35ca43254e0b7fd23cdf5e6fdadd32b0c111b3b2bc28517f617f9917309d69  tools/ghidra/research/verify_acc02_2_markers.py
9d6d63c68419cf37dbf5111516ff9268dfe7c35ab09cc3f07d816fbd874c0c55  tools/ghidra/research/acc02_2_dynamic_writers.py
233a51d8472e579d9a1d0f99d43220fb494fbb90ee23b95f78c0c50644d94465  tools/ghidra/research/acc02_2_cell_writer_scan.py
38b65304f9a47ff7a23241043dfbaa740268b99293ae584372c7660a09f8e8b5  tools/ghidra/research/verify_acc04_1_costs.py
34eb2e24e251b2ccd6dfda7c0ea62191ee56078c1e6f6241433f43429d1400a2  tools/ghidra/fixtures/research/ACC-01.2-witness-maps.json
936fca1e64b6deae13da79dcf14212cf0d603e0045f28d0d9c8d9d2562207be1  expected-ACC-02.2.json
12d1296c99f03d47b82b780e9a5b38ca17f8e3537ca90995a9eebba7b336ac47  cell-writer-scan-ACC-02.2.json
15e95d388f317b120f2ad11efa8362fe8e2dc1ec8436fc81a055fbc99823519c  dynamic-writers-ACC-02.2.json
936fca1e64b6deae13da79dcf14212cf0d603e0045f28d0d9c8d9d2562207be1  markers-repeat.json
d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236  game.dll
```
