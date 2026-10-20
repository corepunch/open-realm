<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/ACC-01.1/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **ACC-01.1**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ACC-01.1/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-ACC-01.1.json` -> [`ACC-01.1-expected.json`](../../../../../tools/ghidra/fixtures/research/ACC-01.1-expected.json) (uncompressed sha256 `45bf241ec3ae6ee727a86fed4baf88ea272e87d3099cb0496a8a5978dc97f64b`, 298656 bytes)

# ACC-01.1 handoff — adaptive side/corner/level-transition branch inventory

**Status.** Established at *instruction-verified* level (all 45 functions reached from 162cb0, every one
of 289 conditional jumps / 578 outcomes annotated) with *original-code oracle* support: 127,792 exhaustive
predicate calls confirm the 16 size-2/base predicate formulas; passive coverage of 14 accepted
original-code corpora (complete requests only) attributes 458 outcomes as already exercised. Reachability:
**463 reachable** (R 439, R-gate 18, R-low 4, R-marker 2), **115 unreachable with proofs** (U-null 37,
U-pad 55, U-ctx 19, U-entry 2) and **2 caller-domain** (C-out, ROUTE-01.2). Ten reachable outcomes were
absent from the accepted corpora; ACC-01.2 supplies producer witnesses for all of them. Not established:
anything beyond 1.27.1.7085 static/emulated behaviour; no live observation was needed. Nothing closed.

**Integration audit (Payoff126).** The frozen inventory has463 reachable,
113 `U-*` outcomes and two `C-out` caller-domain outcomes. The earlier115
unreachable wording above includes those two separate caller-domain cases.
The engine integration keeps ROUTE-01.2 and the ushort-alias regime separate.

## Branch inventory with exact preconditions

Notation: node at level L with representative (x,y) (coordinate of the lookup that created it), square
origin X0=x>>L<<L, Y0, side s=2^L; stored size z (1 or 2); `c(u,v)` = base cell in map with selected class
pair exactly 00; lookup(l,u,v) = 1625f0. Jcc anchors are in `expected-ACC-01.1.json`.

| ID | Branch | Exact precondition / effect | Anchors | Existing corpora |
| --- | --- | --- | --- | --- |
| S1 | same base cell | floor(src)==floor(goal): result 1, one exact goal point, stamp not incremented | 164d2c, 164d3a | taken yes; **x equal / y differs (164d3a fall) missing** |
| S2 | blocked start | lookup(0,src) = -1 (class01, base class10, outside): goal idx -1, result 1 exact goal | 164d58 | yes |
| S3 | same node | src, goal in one promoted node: result 1 | 164d91 | yes |
| S4 | source outside base map | setup 0, 164a20 starts from stale c4 | 164cfe/164d0d | C-out (ROUTE-01.2) |
| L1 | budget | `w=work++; w>=budget` → -1 (stale pops charged; exhaustion reports budget+1) | 163f85 | yes |
| L2-4 | stale / goal / dispatch | generation mismatch discarded; goal index popped returns (goal node may be coarse); byte22==0 → 1643d0 with byte20 else 1644d0 | 163fa7, 163fc1, 163fd3 | yes |
| P1 | partial result | nearest==source → exact source, result 0; else nearest representative +0.5 into ac/b0 | 162d3c | yes |
| F1 | lookup outside | (u>>l, v>>l) out of map (negative wraps) → -1 | 162616, 16261f | yes (low edge) |
| F2/F3 | blocked / mixed | 01 → -1; 10 → -2 above base, -1 at base | 162650, 16265b | yes |
| F4 | promotion | from 00 cell: while level<3 and parent pair 00, move up; stops on any nonzero parent; parent out of range impossible (else NULL deref) | 162676, 1626c2, 1626cf, 162698/9d (U-pad) | yes |
| F5 | node identity | stamp equal → existing index; else create with **this lookup's (u,v)** as representative, byte20 = level cell byte6 iff warp | 1626de, 163f22 | yes |
| BN | base north (x,y-1) | lookup!= -1; z2: c(x+1,y-1); x odd and found level>0: 163d40 (always accepts, U-ctx) | 163afb..b34 | yes (163b34 taken U-ctx) |
| BE | base east (x+1,y) | z2: c(x+2,y) ∧ c(x+2,y+1) (1631e0) | 163199..1b3 | yes |
| BS | base south (x,y+1) | z2: c(x,y+2) ∧ c(x+1,y+2) (164870) | 164829..843 | yes |
| BW | base west (x-1,y) | z2: c(x-1,y+1); y odd and promoted: 1653e0 (always accepts) | 164fac..018 | yes |
| BC | base corners | after N,E,S,W: NE iff N∧E [z2 c(x+2,y-1)], SE iff E∧S [z2 c(x+2,y+2)], SW iff S∧W [z2 c(x-1,y+2)], NW iff W∧N [none]; order NE,SE,SW,NW | 164429..485, 1638d9.., 1642c9.., 16468c.., 163a56 | yes |
| BX | special edge | byte20 m≠0 ∧ record[m] bit0 ∧ record xy ≠ (x,y) → lookup(0,rx,ry); -1 skips; else 165220 (g+1, byte23=m) | 16449a..4b8, 1653b6 | **1644b8 fall, 1653b6 taken missing** |
| CC | size-2 clamp | z2: x=min(x,X0+s-2), y=min(y,Y0+s-2) (CMOVG, no Jcc) | 16454b, 164565/6d | yes |
| CW | side walkers | order N(l=L,t=x,row Y0-1), E(col X0+s,t=y), S(row Y0+s), W(col X0-1); node → [z2 predicate] → relax, flags set iff walker level≠0; -1 → nothing; -2 → l'=l-1, mid=(t>>l<<l)+2^l'; t<mid: halves (t, mid) else (mid-(l'==0?1:z), t); first half recursive (keeps start flag), second iterative (keeps end flag); split nodes are exactly level l' | per walker 9 Jccs | yes |
| CP | size-2 side predicates | interior iff t < (t>>l<<l)+2^l-1 (only l≥1): N 163cd0 c(x+1,y), E/S 163370, W 1651a0 c(x,y+1) — **always accept** in search context; boundary (always at l=0): N 163dd0 c(1,0)∧c(1,1)∧(c(1,2)∨c(-1,0)); E 1635b0 occ∧((A∧B)∨(C∧D)∨(D∧A)); S 164e50 occ∧((c(-1,0)∧c(-1,1))∨(c(1,-2)∧c(1,-1))∨(c(1,-1)∧c(-1,0))); W 165470 c(0,1)∧c(1,1)∧(c(2,1)∨c(0,-1)) | walker Jccs + predicates | yes; **E y=0 (1635dd) and S x=0 (164e78) need a marker (R-marker)** |
| CK | coarse corners | NE(X0+s,Y0-1) iff N.east∧E.north; SE(X0+s,Y0+s) iff E.south∧S.east; SW(X0-1,Y0+s) iff S.west∧W.south; NW(X0-1,Y0-1) iff W.north∧N.west; lookup at L, -2 → same coordinate one level lower, -1 stops; z2: NE c(x+1,y), SE 163370, SW c(x,y+1), NW none | 1645ec..643, 16398c.., 16436c.., 16474d.., 163a9b.. | yes |
| R1 | fresh relax | state -1: no g test; nearest ← if d²(rep,goal cell) < nearest (strict) | 1640b6, 164107 | yes |
| R2-4 | g test, open, reopen | cand ≥ g → none; open → generation++; closed → 1641d0 unlink (head/interior; tail = start, unreachable) | 1640be, 1640c6, 164200, 164217 | **164200 taken (head reopen) missing** |
| RS | special relax | same, cost g+1, byte23 := parent byte20 | 165287..2d8 | **16528f fall, 165297 both, 1652d8 taken missing** |

Edge cost between representatives is 1d58e0((24dx)²+(24dy)²) (ACC-04.1: equals floor sqrt); h uses the
same to the goal cell; heap key g+h.

## Unreachability proofs (summary; per-outcome text in JSON `meaning`)

- **U-null (37):** `TEST reg,reg` after `LEA reg,[cells+idx*8]` — map storage + 8*index is never 0.
- **U-pad (55):** high-side bounds of +1/+2 offsets: constructor base side is D/2+9 (padding ≥9 blocked
  cells beyond the last fine-covered base cell) and every candidate is a clear, fine-covered cell; parent
  bounds in 1625f0 follow from side_l = side_0>>l. Five of these outcomes were nevertheless executed by
  the accepted *unpadded* synthetic 32x32 corpora (`adaptive-size2`, `-size2-passage`, `unit-default-route`):
  163384/1633c7/163de6/16488a/165492 taken — a header artifact, preserved below.
- **U-ctx (19):** invariants H (clear level-l cell ⇒ all base descendants class 00, unmarked), N (node level
  byte of a returned index belongs to that node; fails only under the ushort alias beyond 65,535 nodes,
  ACC-05.1) and G (side geometry: E x≥2, W x≥1, N y≥1, S y≥2). Includes: interior predicates always accept
  (163d0c, 1651d8), promoted-odd predicates always accept (163b34, 163db9, 165018, 165459 + their bounds),
  E/S boundary bounds that cannot fail (163643, 163686, 164ee8, 164f2b, 1635d8, 16360d, 164ea8), and
  TestBaseCell x=-1/y=-1 (162b8d, 162b95: preceded by an alternative lying inside the expanding node).
- **U-entry (2):** heap empty at 163f50 entry (164a20 always enqueues), closed-list tail unlink (tail is
  the start node, g=0).

## Reproducer (repository root)

```sh
P=/GitHub/wc3-analysis/verify-venv/bin/python; B=/run/media/lofcz/ssd_external/Games/w3/game.dll
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research; O=$R/ACC-01.1/existing-coverage
# 1. passive coverage of the accepted corpora (14 runs; same arguments as the corpus manifest)
$P tools/ghidra/research/acc01_coverage_wrapper.py --coverage-out $O/adaptive-size1.json -- tools/ghidra/verify_wc3_pathing_adaptive.py --binary $B --report $O/adaptive-size1.report.json
#   ... adaptive-size2 (--size-input 1), adaptive-budget8 (--budget 8), adaptive-size2-passage, terrain-producer,
#   stamp-wrap, adaptive-storage, gate-markers, gate-edges, multiple-gates, gate-threshold, refill-handoff,
#   refill-progress, unit-default-route: exact argv recorded in each $O/*.json "argv"
# 2. inventory + predicate oracle + join
$P tools/ghidra/research/verify_acc01_1_branches.py --binary $B --existing-coverage $O --out /tmp/acc011.json --fixture $R/ACC-01.1/expected-ACC-01.1.json
```

## Provenance

game.dll `d51e5680...d8236`. `expected-ACC-01.1.json` sha256
`45bf241ec3ae6ee727a86fed4baf88ea272e87d3099cb0496a8a5978dc97f64b`; repeat `inventory-repeat.json` exit 0.
Scripts: `verify_acc01_1_branches.py` `57b60777...e19679`, `acc01_coverage_wrapper.py` `12524db0...23038`,
`acc_research_harness.py` `c2915cca...df7929c`. Disassembly: read-only Ghidra cache, per-function sha256 in
JSON `disassembly_sha256`; call graph `callgraph-162cb0.json`. Wrapped corpus exits match their manifest
expectations (size2/passage/terrain-producer/budget8 exit 1 by design).

## Captures

| Capture | Status |
| --- | --- |
| `existing-coverage.v1/` | **superseded, kept**: first wrapper counted direct predicate/lookup controls as covered (e.g. 1635b0 low-edge outcomes) |
| `existing-coverage/` | accepted: only outcomes inside 162cb0/1627e0/162910 (entry→own RET at same ESP); others in `outside_request_outcomes` |
| `inventory-ACC-01.1.json` = expected; `inventory-repeat.json` | complete, equal |

## Observer controls

Block/code hooks only read registers/memory; wrapped scripts keep their own assertions (exit codes
unchanged). Predicate oracle cells are supplied **controls**; reachability is decided by ACC-01.2 producers.

## Exclusions

Out-of-map source (C-out) → ROUTE-01.2. Ushort alias regime (could reach U-ctx/N outcomes) → ACC-05.1.
Witness construction → ACC-01.2. Costs/ties → ACC-04.1. Special-edge semantics beyond reachability → GATE.

## Mismatches preserved

- First coverage attribution (v1) overstated coverage; superseded by request-only attribution.
- U-pad outcomes executed only by unpadded synthetic headers in accepted corpora (5 outcomes above): those
  corpora are valid controls but their high-edge behaviour is not constructor-reachable.

## Proposed integration

`mapping-rows-ACC-01.1.txt` (renames/comments already applied via `gw.py`, log `ghidra-writes.jsonl`;
6f164a20 was already `PathAcc_StartSearch` from ROUTE-01.2 — unchanged), `proposed-docs-ACC-01.1.md`.

## Suggested failing engine regressions

1. Interior predicates and promoted-odd predicates: assert the engine never rejects these (or omit them),
   e.g. no engine route differs when 163cd0/1651a0/163d40/1653e0 are forced true over the ACC-01.2 corpus.
2. Walker split coordinates: for t ≥ mid the first half uses mid-1 at level 0 and mid-z above; first
   lookup sets the representative (frozen node tables in ACC-01.2 `details`).
3. End-flag rule: corners only after both adjacent end segments relaxed at walker level ≥1 (a level-0 end
   segment never opens the corner).

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
45bf241ec3ae6ee727a86fed4baf88ea272e87d3099cb0496a8a5978dc97f64b  expected-ACC-01.1.json
45bf241ec3ae6ee727a86fed4baf88ea272e87d3099cb0496a8a5978dc97f64b  inventory-repeat.json
d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236  game.dll
```
