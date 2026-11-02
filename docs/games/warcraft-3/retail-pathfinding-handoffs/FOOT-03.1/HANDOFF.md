<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/FOOT-03.1/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **FOOT-03.1**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/FOOT-03.1/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-FOOT-03.1.json` -> [`FOOT-03.1-expected.json`](../../../../../tools/ghidra/fixtures/research/FOOT-03.1-expected.json) (uncompressed sha256 `7bd26ec0c1ba8d1327f33fd5e18947dae8d5f843072d5afdff372deb2226ffa5`, 56896 bytes)

# FOOT-03.1 handoff — eligibility truth table for fine search, hierarchy, segments and endpoints (research, not a closure)

**Status.** The complete per-link predicate of all four consumers (plus the collector and the category-union query)
is **instruction-verified** and **original-code-oracle verified**: 50,112 predicate executions (34,944 single-link
attribute combinations, 15,120 ordered 1–3-record chains over two objects, 48 link-cap cases) match an independent
model with **0 mismatches**, repeated byte-identically; composed through the real wrappers (perimeter, four segment
classes, four endpoint classes × both modes, `04df50` with bridge self-suppression, full `15d360` rebuild). **Live**:
164 consumer calls on two shared cells (FOOT-03.2 capture) equal the model applied to the live chains. New finding:
`148e90` examines at most **49 links** per cell and requires the static flag `+40&10000000`, so unit/item occupancy
never reaches the hierarchy and a static blocker behind ≥49 newer links is invisible to it. Not covered: exception
unwinding of the mode/suppression scopes, 16-bit/32-bit stamp wrap (MAP-05/ACC-05), whole-search composition beyond
the perimeter consumer (FINE-01.x already covers chains in full searches).

## Functions

| VA | Role | ABI (asm) | Evidence |
| --- | --- | --- | --- |
| `6f1489a0` PathFine_TestOccupiedCell | fine/segment/endpoint cell predicate | thiscall ECX fine; [4] x, [8] y; RET 8; EAX 1 clear/0 blocked; writes +d0 (block), +cc (target), obj+38 | asm `1489a0..148aca`; oracle; live |
| `6f148e90` PathFine_TestAcceleratorCell | hierarchy cell predicate (only caller chain `15d0e0→1493d0`) | thiscall; x, y; RET 8; EAX; local link counter ESI, `cmp esi,32h` at `148f11` | asm; oracle; live |
| `6f1493d0` PathFine_TestAcceleratorCellWithMask | a4=*mask; 148e90 | thiscall; [4] int XY*, [8] mask*; RET 8 | asm |
| `6f15d0e0` PathMaps_ClassifyFineBlock (existing) | 4 cells → 00/01/10 at `0xc0000000>>shift` of cell dword +4 | thiscall ECX owner; [4] cell*, [8] mask*, [c] shift, [10] x, [14] y; RET 14 | asm; live words `40000000/50000000/54000000` |
| `6f148ad0` PathFine_CollectCellBlockers (existing) | collector; token via `1480d0` (mover tag `60706375`, else NULL; cap 32) | thiscall; x, y, vector*; RET c | asm; oracle |
| `6f149170` PathFine_GetCellCategoryUnion | -1 on bounds/terrain; else high byte | OR categories | thiscall; x,y; RET 8 | asm; oracle |
| `6f148060`/`6f148080` | union with a4=0 (int / scalar point); callers `04c780` / `04c650` | thiscall; [4] XY*; RET 4 | asm |
| `6f148d00` PathFine_ReadBlockedPerimeter (existing) | fine-search expansion | thiscall; x,y,offset,width; RET 10 | executed |
| `6f149440/149630/149970/149cc0` | segment class tests (crossing code, 0=full footprint) | thiscall; [4] XY*, [8] code; RET 8 | executed |
| `6f1492b0`/`6f149370` | endpoint footprint (int / scalar point), a4=*mask, class→segment fn with code 0 | thiscall; XY*, mask*, class; RET c | executed |
| `6f149320` PathFine_TestScalarPointCell | a4=*mask, floor, 1489a0 | thiscall; XY*, mask*; RET 8 | asm |
| `6f04df50` PathWorld_TestPointQuery (existing) | forces d4=1, suppresses excluded bridge (05bd30), 149320, restores d4 | fastcall ECX X*, EDX Y*; [4] mask*, [8] bridge; RET 8; EAX 1=blocked | executed |

## Truth table (exact; `expected-FOOT-03.1.json` sha256 `7bd26ec0c1ba8d1327f33fd5e18947dae8d5f843072d5afdff372deb2226ffa5`)

Common prefix: out of bounds or `cell & a4 & ff000000` ⇒ terrain verdict before links (1489a0: 0,+d0=1; 148e90: 0;
148ad0: one NULL token; 149170: -1). Per call `map+b4` is incremented; per object the **first non-type-2 record in the
cell decides** (its +38 is stamped even for removal records), so a newer removal record hides older insertions.

| Step | 1489a0 fine/segment/endpoint | 148e90 hierarchy | 148ad0 collector | 149170 union |
| --- | --- | --- | --- | --- |
| link cap | none | **49 links** (any kind) then clear | none | none |
| skip type 2 | yes | yes | yes | yes |
| object prefilter | +38≠-1, +34 bit24 | +38≠-1, +34 bit24, **+40 bit28** | +38≠-1, +34 bit24 | +38≠-1, +34 bit24 |
| stamp ≠ current | required, then stamped | same | same | same |
| decides only on type 1 | yes | yes | yes | yes |
| target | obj==+a8 ⇒ +cc=1 (before flags) | — | — | — |
| exclusion | `+40&8fffffff` | same | same | same |
| moving/group exemption | `+40&60000000` when d4==0 | none | none | none |
| category | `+34&a4&ffffff` | same | same | none (OR all) |
| effect | 0 blocked, +d0=1 | 0 blocked | token | OR |

Mode per consumer (asm): fine search `14ad50` writes a8=target, d4=0 (`14ae9b/14aea1`); segment checks (`168d30`,
`167bf0`, `148c70/148de0` strips) write a4 only and inherit d4; endpoint validators force d4=1 and restore:
`04df50` (`04e014/04e046`), `14a050` (`14a08c/14a1b5`), `14a1e0` (`14a21a/14a3c2/14a452`), `16ee80` (`16ef3a/16ef57`),
`170080` (`17014c/170171`). Hierarchy lane masks `06000006/80000080/40000040/04000004` (shifts 0/2/4/6).

Suppression counter (`+40` low 28 bits) inc/dec pairs: fine request self `166f66/167048`, target `166f7d/167041`;
waypoint segment selection self `167c2a/167cbd`; next-step collector self `166265/1662dd`; separation endpoint
`16ef24/16ef61`; embedded recovery `170133/1701d2`; portal admission `16ec34/16eca0`; `05bd30` (bridge; 04df50,
05ca50, 651590); `063d10` (region list; 651590, 0599c0); cohort publication `169c50/169d60` (≤12 members) and group
target `16da60`; RAII `058600/058740`. Reference counters: object `+3c` +1 `14da8d`, −1 `14e12f`; map `+b0` +1
`14da87`, −1 `14e11c`; dead object released by `14dab0` when `+3c&ffffff==0`. No per-cell counter exists: the chain
itself is the per-cell reference set.

Link cap (oracle, 3 filler kinds): blocker at position 48/49 ⇒ hierarchy blocked; position 50/51 ⇒ hierarchy **clear**
while fine search still blocked (fillers: inactive static CTriggerRegion links, a unit's removal records, search metadata).

## Reproducer
```sh
cd "$(git rev-parse --show-toplevel)"
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_FOOT-03.1_eligibility.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --report /GitHub/wc3-analysis/reports/pathfinding-1.27/research/FOOT-03.1/oracle-eligibility.json
# prints: 50112 predicate cases (34944 single-link, 15120 chain); 0 mismatches; report sha256 a1b7f729…ffd0
```
Live cross-check: `tools/frida/research/foot032_analyze.py` (FOOT-03.2 HANDOFF).

## Provenance
Assembly excerpts of every cited function: `static-asm.txt` (sha256 `0eebba2e181c522636393f4e6d6e1c54fcf170289576acff25c5f80cccea880c`), cut from `research/_ghidra/full-text.dis` (`objdump -d -M intel`, sha256 `c8026b7d…4d9a`).
DLL `d51e5680…d8236` (CRT not used by these slices). Report `oracle-eligibility.json` sha256
`a1b7f729b098966b8119fd796db0aebb6c40919c568f5a2d9090ea45a718ffd0` (two identical runs). Sources (sha256 in report):
`verify_FOOT-03.1_eligibility.py`, `foot03_rig.py`, `foot03_spatial_harness_copy.py` (copy of SEP-03
`sep03_map05_spatial_harness.py` sha256 `ddc8ce1c…5a97`). Only Storm imports `07c6d2/07c6d8/07c678` replaced (host
storage). Fixture-supplied: owner+238/+24c pointers, fine+1c, hierarchy map storage, game bounds; original startup
initializers `006e90/006ea0/070d80` executed. Unknown link kind 3 written directly (no producer: all 14d9e0/14d960 call
sites pass 0 or 01000000; `05f860` forwards an argument).

## Observer controls / captures
Oracle only here; the live comparison and its observer-free control are in FOOT-03.2.

## Exclusions
Stamp wrap (`14df20/14dfc0` reset at >7fffffff): MAP-05.x. Exception-unwind restoration of d4/+40: MAP-04.2.
Full search/accelerator composition with categories: FINE-01.x / ACC-02.2. Special gate markers: GATE.

## Mismatches preserved
- `retail-pathfinding-search.md` says `148e90` uses "object flag tests at +0x34 and +0x40" without the cap/static rule;
  refined here (no contradiction). Engine doc's statement that hierarchy eligibility is "narrower" is now exact.
- Footprint oracle's single-link model never set `10000000`; consistent (1489a0 ignores bit 28).

## Proposed integration
`mapping-rows-FOOT-03.1.txt` (8 rows), `proposed-docs-FOOT-03.1.md`. Suggested failing engine regressions:
1. Hierarchy classifier ignores non-static objects: a Footman-only 2×2 block stays class 00 in all lanes.
2. 49-link cap: 49 newer inactive/removal/metadata links before a static `c2` link ⇒ ground lane clear; 48 ⇒ blocked.
3. First-record-wins: `[removal(U), insert(U)]` in one cell ⇒ U ignored by all consumers until compaction.
4. Moving (`20000000`) or group (`40000000`) unit: perimeter mode0 clear, endpoint mode1 blocked.
5. Suppressed static region (`+40=10000001`) clear in all four consumers; `80000000` likewise.
