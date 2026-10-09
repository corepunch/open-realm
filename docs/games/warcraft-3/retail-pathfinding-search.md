# Retail pathfinding evidence: Search, footprints and map construction

[Contract, current ledger and reproduction](retail-pathfinding.md).
Retail **1.27.1.7085** only; addresses and report paths use the conventions there.

## Anchors and entry points

| Preferred VA | Recovered role / evidence |
| --- | --- |
| `6f147600` | `CLrPathingSys` constructor; vtable `6fa908ec`; open queue at `+0x44` |
| `6f14f570` | `CLrPathingAcc` constructor; vtable `6fa90c40`; open queue at `+0x70` |
| `6f1657c0` | `CLrPath` constructor; vtable `6fa91c40`; two 0x20-byte containers at `+0x34/+0x54` |
| `6f166c30` | Path-owned accelerated request; calls `6f162cb0`, uses buffer `path+0x54` |
| `6f166e90` | Path-owned fine request; calls `6f148100`, uses buffer `path+0x34` |
| `6f167ce0` | Fine waypoint consumer; refills when index `+0x74` reaches count `+0x50` |
| `6f165b60` | Accelerated-buffer progress; can request another route and return a pending result |
| `6f168910` | Per-path request admission from elapsed counter and separate mode timestamps |
| `6f14ad50` / `6f164c30` | Fine / accelerated search setup |
| `6f14a4c0` / `6f163f50` | Fine / accelerated heap-pop search loops |
| `6f14a560` / `6f164020` | Fine / accelerated cheaper-path relaxation |
| `6f1483f0` / `6f148240` | Shared binary min-heap insertion / removal |
| `6f1625f0` | Accelerator cell classification and promotion through map levels |
| `6f1644d0` | Expansion across a coarse square's sides and corners |
| `6f163260` | One side's recursive subdivision when a neighbour is mixed |
| `6f1489a0` | Fine cell mask and linked nearby-object blocking query |

## Why this is A*-family search

Both searches use 0x24-byte node records and the same 0x0c-byte heap records.
Fine insertion `6f147c30` and accelerated insertion `6f162780` form the heap key
from node `+0x14 + +0x18`. Relaxation derives the first term from the parent's
accumulated cost plus edge cost, derives the second from distance to the goal,
rejects a non-improving cost, and records the parent at `+0x1c`.

The heap is ordered by ascending unsigned key. Queue records also carry node
index and generation; search loops discard stale generations. Both loops stop
on their destination node or a configured work limit. Counters include popped
stale records, so call them **pop/iteration counts**, not expanded-node counts.
The fine loop also has the special-object early exit described below; a
nonnegative result alone does not establish that the destination was reached.

## Queue ties, reopening, and fine-loop termination

The shared heap uses slot 1 as the root; its stored count includes unused slot
zero. `6f1483f0` moves a new entry above an equal-key parent (it stops only when
the parent's unsigned key is strictly smaller). During `6f148240` removal, the
right child wins equal-child ties; the displaced last entry stops descending
when its key is less than or equal to the selected child. There is no secondary
comparison of h, node index, or generation. Eight equal-key entries inserted
with IDs 0..7 pop as **7,0,4,1,2,3,5,6**; this is neither FIFO nor general LIFO.

Fine node fields `+0x0c/+0x10` also encode list state:

| `node+0x0c` | Meaning |
| --- | --- |
| `-1` | Unlinked/new or freshly popped, before closing/re-enqueueing |
| `-2` | Open, with a generation-tagged heap entry |
| Node index or `-3` | Closed-list next index or end sentinel |

`+0x10` is the closed-list previous index (`-1` at the head); system `+0x64`
is the head (`-3` when empty). `6f147bf0` prepends a closed node and increments
its generation. `6f14a900` unlinks a closed node, repairs both adjacent links
and the head, clears its links to `-1`, and increments its generation.

For a discovered node, `6f14a560` ignores a candidate g greater than or equal
to the existing g. A cheaper candidate unlinks a closed node, or invalidates
an open node's generation, then updates parent/g/h and calls `6f147c30`.
That routine increments generation again, sets state `-2`, and inserts the new
`(g+h,index,generation)` record. Old open records remain in the heap and later
fail the generation check. Thus **closed nodes can reopen**; the inconsistent
heuristic is not combined with an unconditional closed-node exclusion.

The fine loop `6f14a4c0` proceeds as follows:

1. An empty heap returns `-1` without incrementing the work counter.
2. Otherwise it increments `+0x6c`; if the prior count was at least budget
   `+0x68`, it returns `-1` without popping. A budget stop can therefore report
   `budget+1`, including the rejected next iteration.
3. It pops and discards a generation mismatch. Stale records consume budget.
4. For a matching record, it clears links and increments generation. If its
   node index equals goal `+0x94`, it returns that index without expansion.
5. Otherwise it expands. If system `+0xcc` became nonzero, it clears that flag
   and returns the current node without closing it. The occupied-cell predicate
   sets this flag on a visited type-1 object equal to system `+0xa8`, even when
   that object's other policy flags allow passage.
6. Otherwise it closes the expanded node and continues.

`verify_wc3_pathing_queue.py` checks **10,096 heap sequences / 180,392 original
heap operations**, including all three-key sequences of lengths 1..8 plus
256 deterministic unsigned-key/tie sequences. **18 relaxation cases** cover
fresh/open nodes and sole/head/tail/middle closed-list nodes with cheaper,
equal, or worse candidate costs. These execute original container, heap,
enqueue, unlink and relaxation instructions with preallocated storage and no
code stubs. **3,410 loop cases** additionally execute the original search loop,
stubbing only expansion to emit no neighbours and optionally set `+0xcc`;
they check return value, budget, remaining heap, generations and closed state.
All pass. This queue oracle does not execute a complete terrain search; the
composed fine-grid oracle below adds static-terrain searches, without proving
final world-space route optimality.

Oracle: `verify_wc3_pathing_queue.py` → `fine-queue-loop-oracle.json`.

## Fine heuristic

Fine expansion `6f14b760` offers eight neighbours, with cardinal cost 15 and
diagonal cost 21. Relaxation `6f14a560` computes this heuristic, where shifts
are integer right shifts and deltas are relative to the goal:

```
a = 15 * max(abs(dx), abs(dy))
b = 15 * min(abs(dx), abs(dy))
if b <= (a >> 2):
    h = a
else:
    t = b + (b >> 1)
    h = a + (a >> 6) - (a >> 4) + (t >> 2) + (t >> 7)
```

This is not octile distance. `verify_wc3_pathing_search.py` executes the
original fresh-node relaxation with enqueue stubbed for all **263,169** signed
delta pairs in `[-256,256]²`; every heuristic, accumulated cost, parent index,
nearest-node squared distance and enqueue argument matches. No sampled h
exceeds obstacle-free distance `15*max(|dx|,|dy|)+6*min(|dx|,|dy|)`.

The heuristic is **inconsistent**: against goal `(0,0)`, retail produces
`h(1,4)=60` and `h(2,5)=83`, a difference of 23 across a diagonal edge costing
21. There are 12,816 such undirected-neighbour violations within the tested
domain. This is original-x86 output, not solely a property of the recovered
formula. It does not by itself prove suboptimal routes: the search reopens
cheaper closed nodes, as verified above. The composed static-grid corpus below
also matches Dijkstra costs. The no-overestimate observation
is bounded to the tested coordinate domain, not a global admissibility proof.

Oracle: `verify_wc3_pathing_search.py` → `fine-heuristic-oracle.json`.

In accelerated relaxation, assembly at `6f164044..6f1640a8` scales coordinate
differences by 24, squares/sums them, then calls integer square-root routine
`6f1d58e0` for both edge distance and destination heuristic. The special edge
routine `6f165220` instead increments accumulated cost by one; it is reached
through a warp-record path in `6f1643d0`. Its Way Gate identity is now established by the native/ability producer
[Way Gate chain](retail-pathfinding-experiments.md#special-edges-are-way-gate-records); its impact on heuristic admissibility remains unverified. A* mechanics alone do not prove
optimality, admissibility, or shortest final world-space trajectories.

## Adaptive hierarchy, not just suggestive class names

`6f1625f0` uses four map pointers at accelerator `+0x1c/+0x20/+0x24/+0x28`.
At level `l`, it indexes `(x >> l, y >> l)` in an array of 8-byte cells.
Map dimensions are at `+0x3c/+0x40`, cell storage at `+0x28`.

It selects a two-bit classification using the query's shift at `+0xd4`:

- `0x40000000`: reject (`-1`).
- `0x80000000`: subdivide (`-2`) above level zero; reject at level zero.
- Zero in the tested pair: permits promotion into a usable parent, up to level 3.

Class3 is not emitted by an ordinary full rebuild, which clears the class byte
before reducing each lane; see [producer reachability](#ordinary-classification-reachability).
Cells retain
a search stamp and a 16-bit node index; `6f163ef0` lazily creates a node for the
current search and records its level at node `+0x22`. The adaptive cell stamp
is a full DWORD at cell0/system2c, with ushort node index at cell4; the fine
request uses a different ushort stamp. Payoff78 composes actual adaptive wrap,
lane/size reuse and complete engine state controls, and fixes post-exclusion
ground classifications; [adaptive reuse payoff](retail-pathfinding-engine.md#adaptive-reuse-restores-the-original-ground-classifications).

`6f1644d0` aligns the square origin with shifts and uses side length `1 << level`.
Its side walkers split mixed neighbours by decreasing the level. At level zero,
`6f1643d0` uses individual neighbours. This is direct executable evidence of
multiresolution traversal, independent of RTTI words such as “cluster”. Map construction and rectangle propagation are recovered below; the complete
set of invalidation producers remains under investigation.

## Complete adaptive request oracle

`verify_wc3_pathing_adaptive.py` executes original parent reduction,
`6f162cb0` setup/search/reconstruction, cell promotion, node creation, side and
corner expansion, heap operations and relaxation, with no code stubs. The
harness supplies classified 32×32 base cells, four preallocated map levels,
size input 0 (stored size 1), and no warp records. Parent cells
are built by original `6f15d1c0`, not a replacement reducer. Full fine-to-base
map construction and path-owned request admission remain separate.

All **84 requests pass reachability comparison** against an independent
eight-neighbour, corner-safe base-grid search: 70 reach the destination and 14
exhaust. Fixtures include open ground, a dividing wall, gaps of widths 1..6,
vertical/horizontal barriers at six additional alignments, and 64 deterministic
random maps. This compares reachability, not route cost: the adaptive graph
and its scaled distance costs differ from the base-grid reference.

The open case produces only level-3 nodes: four pops, 14 nodes, and the reverse
route `[(27.25,27.75),(16.75,16.75),(8.75,8.75),(4.25,4.75)]`.
The one-cell-gap case takes 13 pops and creates 30 nodes across all four levels
(8 at level 0, 8 at level 1, 5 at level 2, 9 at level 3). This composes the
previously isolated hierarchy and search evidence in one executable request.

On failure, wrapper `6f162cb0` mirrors the fine wrapper's broad structure:
if the nearest tracked node `+0xd0` is still start `+0xc4`, it emits exact
start and returns 0. Otherwise helper `6f162c40` computes that node's integer
XY plus 0.5, stores it as adjusted destination `+0xac/+0xb0`, reconstructs with
that destination, and returns 0. All 14 exhausted cases verify their partial
endpoint against this selected node centre.

**Nearest adaptive node is not nearest reachable base cell.** With the solid
wall at x=16 and goal `(27,27)`, the search exhausts after 12 pops, having
created nine level-3 nodes. It selects node `(8,24)` (squared distance 370),
and returns adjusted endpoint `(8.5,24.5)`. The reachable base component
extends to `(15,27)` (squared distance 144). This difference follows from the
coarse representatives explored by this search. It does not establish the
mover's eventual stopping point, since fine routing and retries follow.

Oracle: `verify_wc3_pathing_adaptive.py` → `adaptive-class1-oracle.json`.

The expanded oracle repeats the 84-map corpus in all four query shifts
`0,2,4,6`, while marking all three unselected lanes blocked. It requires identical
route points, node-level counts, pop counts and partial-destination state
across lanes. Twenty additional endpoint/control cases bring the total to
**356 complete requests**, all passing their stated contracts. Of these,
340 compare ordinary reachability and 16 test setup bypasses separately:

| Setup condition | Result / pops / allocated nodes | Route |
| --- | --- | --- |
| Blocked starting cell, clear goal | 1 / 0 / 0 | Exact destination only |
| Both start and goal blocked | 1 / 0 / 0 | Exact destination only, despite reference graph being unreachable |
| Different base cells in the same clear level-3 cell | 1 / 0 / 1 | Exact destination only |
| Both positions floor to the same base cell | 1 / 0 / 0 | Exact destination only |
| Clear start, blocked goal | 0 / 34 / 24 | Partial route to `(27.5,26.5)` in this fixture |

These are explicit `6f164c30` setup shortcuts consumed by `6f162cb0`; result 1
does not universally prove that the accelerator searched or found a traversable
path. A blocked start sets both stored start/goal indices to `-1` and returns
setup result 1. Same-base-cell coordinates bypass lookup altogether; distinct
base cells that resolve to the same promoted node also return setup result 1.
The path-owned caller's eventual fine-route/movement handling remains separate.

`6f166c30` selects the lane as `2 * ((pathFlags88 >> 30) & 3)`. It derives
adaptive size input by taking the fine footprint class and shifting right one,
so normal finite positive footprint scalars below 1 produce input 0 (stored
size 1), and scalars at least 1 produce input 1 (stored size 2). Combined with
the unit producer's `/32`, this boundary corresponds to authored collision 32.
The extra 0.5 and 1.5 comparisons do not create additional adaptive sizes.
Original instruction slices `6f166cd3..6f166d23` and `6f166d6c..6f166d7b`
pass **20 size-boundary samples and 16 flag/lane cases**. These slices isolate
selection and do not execute admission or temporary object exclusion.

Current size-1 report: `adaptive-lanes-shortcuts-oracle.json` under the same
analysis directory. The size-2 experiment below adds a verified hierarchy
limitation. Mixed/invalid base classifications, warp edges and adaptive
shortest-path quality remain incompletely covered. The
initialized float constants and gate sentinel are supplied from the Frida
witness described under reconstruction.

## Size-2 traversal and a reduced hierarchy witness

At base level, size 2 adds positive-X/Y footprint checks around candidate
anchor `(x,y)`. East helper `6f1631e0` requires `(x+1,y)` and `(x+1,y+1)` clear;
south helper `6f164870` requires `(x,y+1)` and `(x+1,y+1)` clear. North helper
`6f163b60` checks `(x+1,y)` in addition to the candidate cell; the west wrapper
`6f164f90` checks `(x,y+1)`. These complement occupancy inherited from the
previous 2×2 footprint. A clear helper cell requires its selected class pair
to be exactly zero, not merely different from the blocked class.

Promotion adds further rules: north traversal at odd X into a nonzero-level
node calls `6f163d40`, requiring either `(x+1,y-2)` or `(x-1,y)` clear. West
traversal at odd Y into a nonzero-level node calls `6f1653e0`, requiring either
`(x-2,y+1)` or `(x,y-1)` clear. These are static recovered rules, not a complete
geometric equivalence proof for every size-2 side/corner transition.

The adaptive oracle accepts `--size-input 1` for stored size 2. Its comparison
graph uses a 2×2 base footprint anchored toward positive X/Y, with corner-safe
transitions. One-cell wall gaps now fail and two-cell gaps pass, as expected.
However, the 356-request corpus has **four differences**, all from `random_37`
in the four lanes: the reference reaches the goal while retail exhausts after
139 pops. The report is `adaptive-size2-oracle.json`; its exit status 1 is
preserved as a discovered difference, not relabelled as a passing equivalence
test.

Controlled intervention `--disable-promotion` marks every parent mixed while
leaving base cells unchanged. This forces original retail traversal through
level-zero nodes. The failing map then succeeds. This distinguishes hierarchy
effects from the common original base-level footprint rules; it does not
alter the retail executable or any live game process.

`minimize_wc3_pathing_adaptive.py` reduced that map from 157 to **18 blocked
cells in 156 probes**. No single remaining blocked cell can be deleted while
preserving the predicate; this is not a globally smallest-map claim. The
redistributable synthetic fixture is
`tools/ghidra/fixtures/pathing-adaptive-size2-passage.json` (32×32, start `(4,4)`,
goal `(27,27)`). On all four lanes:

| Hierarchy | Result | Pops | Nodes | Endpoint |
| --- | --- | --- | --- | --- |
| Original parent classifications | 0 / partial | 38 | 56 across levels 0..3 | `(16.5,26.5)` |
| Parents forced mixed | 1 / goal | 556 | 504, all level 0 | `(27.25,27.75)` |

The original partial search's selected node has squared goal distance 122,
whereas the reference component contains the goal. Full reduction history and
each candidate report are retained in `adaptive-minimal37/` under the analysis
directory. All-lane verification reports are `adaptive-minimal37-all-lanes.json`
and `adaptive-minimal37-flat-all-lanes.json`.

```
# Expected exit 1: reproduces four hierarchy/reference differences.
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_adaptive.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --size-input 1 \
  --map-json tools/ghidra/fixtures/pathing-adaptive-size2-passage.json \
  --report /tmp/adaptive-size2-passage.json
# Expected exit 0: original base traversal succeeds without promotion.
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_adaptive.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --size-input 1 \
  --map-json tools/ghidra/fixtures/pathing-adaptive-size2-passage.json --disable-promotion \
  --report /tmp/adaptive-size2-passage-flat.json
```

## Exact size-2 east-boundary veto

Read-only `--trace-graph` records original relaxation calls, expanded node IDs
and final node fields. In the reduced witness it identifies the lost passage
at candidate `(18,13)`, adjacent to the clear level-1 square covering
`[16,17]×[12,13]`. Node 12 represents that square at `(16,12)`; the east-side
walker `6f163260` descends from level 1 to level 0 and creates candidate node
36 at `(18,13)`, but never relaxes it. Its generation remains zero and parent
remains `-1`.

The local cell geometry is:

| Y / X | 16 | 17 | 18 | 19 |
| --- | --- | --- | --- | --- |
| 12 | clear, coarse square | clear, coarse square | blocked A | clear B |
| 13 | clear, coarse square | clear, coarse square | candidate | clear R1 |
| 14 | blocked C | clear D | clear R2 | clear R3 |

The ordinary size-2 east occupancy helper `6f163370` checks R1/R2/R3. It
returns 1 here. At the boundary of the subdivided east side, however,
`6f163260` calls `6f1635b0`, which requires:

```
ordinary = clear(x+1,y) && clear(x,y+1) && clear(x+1,y+1)
A = clear(x,y-1)       B = clear(x+1,y-1)
C = clear(x-2,y+1)     D = clear(x-1,y+1)
accepted = ordinary && ((A && B) || (C && D) || (D && A))
```

All three additional alternatives fail because A `(18,12)` and C `(16,14)`
are blocked. The rejection is consumed at `6f163332..6f163334`, skipping
relaxation. `5,120` original predicate checks cover every combination of the
seven cells, all four lanes, and ten interior/boundary positions. Both the
ordinary and boundary predicate results match the formula, including
out-of-bounds cells being non-clear.

With promotion disabled, original base east traversal `6f163180` accepts the
edge `(17,13) → (18,13)` (relaxation caller `6f1631c0`); this path does not use
the additional coarse-boundary predicate. Thus the passage is available to
original base-level size-2 movement within the same search subsystem.

For a causal check, `--east-boundary 18 13 --force-east-boundary` changes only
the rejected predicate result at `6f163332` in the emulator. It leaves the
original predicate execution, base classifications and all other decisions
intact. Exactly one rejection is overridden per lane, from parent node 12,
at current subdivision level 0 (caller level 1). **All four lanes then reach
the goal: 33 pops, 58 nodes**, without globally disabling promotion. Ordinary
occupancy at that coordinate still returns 1. This establishes the specific
veto as sufficient to explain the reduced witness's failure; it is not a
proposed production fix or a claim that the predicate is unnecessary elsewhere.

Reports: `adaptive-minimal37-veto-baseline.json` (unchanged failure),
`adaptive-minimal37-single-veto-all-lanes.json` (one-result intervention), and
`adaptive-minimal37-flat-graph.json` (base edge). All are under the analysis
directory. Reproduce the intervention with:

```
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_adaptive.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --size-input 1 \
  --map-json tools/ghidra/fixtures/pathing-adaptive-size2-passage.json \
  --east-boundary 18 13 --force-east-boundary --trace-graph \
  --report /tmp/adaptive-size2-single-veto.json
```

This remains a search limitation in a synthetic classified map, not yet a
live-game movement failure. Fine routing, retries and request state can change
the eventual outcome; preserve this witness when recovering those layers.

## Footprints and dynamic occupancy

Fine setup `6f14ad50` selects four footprint cases from a floating input using
thresholds 0.5, 1.0 and 1.5 in its internal coordinate space. The middle constant
is runtime-initialized: `6fd3c748` reads as zero in the static image, but was 1.0
in both inspected live processes. Do not infer a zero threshold from the file.
The producer chain is now traced through CUnit vtable `6fb77eb0`, slot `+0x16c`
(`6f6945a0`) → `6f05c2e0` → `6f15fef0`, which writes mover `+0x90`.
`6f05c2e0` scales the positive normal float by 1/32 using its exponent bits.
The input comes from virtual slot `+0x168` (`6f6742f0` → `6f674280`), reading
unit-profile `+0x19c`, with a minimum of 1 before conversion. The Footman
runtime footprint is 0.96875 (31/32). The field is `collision`: profile loader `6f66bf40` stores accessor
`6f6a8ca0` at profile `+0x19c`; accessor descriptor `+0x3f8` is bound to that
string by `6f6b0870` at `6f6b0ede`. The exact radius-versus-diameter geometric
contract is still being checked.

`6f14b760` dispatches these cases to `6f14b890`, `6f14b9b0`, `6f14bae0`, and
`6f14bc10`. All four build blocked-cell perimeter masks through `6f148d00`.
For node `(x,y)`, that helper starts at `(x-offset,y-offset)`, scans the top
row left-to-right, right column toward increasing Y, bottom row right-to-left,
then left column toward decreasing Y, without repeating corners. Bit zero is
the first cell; a bit is set when `6f1489a0` rejects that cell.

| Class / internal scalar | Expansion | Offset / perimeter width | Bits | Cardinal strip / diagonal tested cells |
| --- | --- | --- | --- | --- |
| 0: below 0.5 | `6f14b890` | 1 / 3 | 8 | 1 / 3 |
| 1: [0.5,1.0) | `6f14b9b0` | 2 / 4 | 12 | 2 / 5 |
| 2: [1.0,1.5) | `6f14bae0` | 2 / 5 | 16 | 3 / 7 |
| 3: at least 1.5 | `6f14bc10` | 3 / 6 | 20 | 4 / 9 |

Each neighbour is admitted only when `blockedBits & directionMask == 0`.
The following masks use output order `(dx,dy)`; they are hexadecimal:

| Class | (-1,-1) | (0,-1) | (1,-1) | (-1,0) | (1,0) | (-1,1) | (0,1) | (1,1) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | 83 | 2 | e | 80 | 8 | e0 | 20 | 38 |
| 1 | c07 | 6 | 3e | c00 | 30 | f80 | 180 | 1f0 |
| 2 | e00f | e | fe | e000 | e0 | fe00 | e00 | fe0 |
| 3 | f001f | 1e | 3fe | f0000 | 3c0 | ff800 | 7800 | 7fc0 |

The diagonal masks include both adjoining cardinal strips and their corner,
so a blocked side can reject a diagonal whose destination cell is clear.
The interior is not rescanned by these expansion routines. Initial footprint
validity and the world-position alignment of the even-width cases are separate
questions; do not treat this perimeter-only observation as a full collision
shape or continuous radius contract.

The underlying `6f1489a0` checks high cell-mask bits and follows low-24-bit
linked object indices. Object flags, query masks, and an excluded/special object
affect acceptance. Thus the fine search is not demonstrated to be a purely
static-terrain search with all unit interaction deferred to steering. The
[object-category inventory](retail-pathfinding-categories.md) now identifies
unit/item mover categories and independent static widget regions. Raw-link
eligibility/history remains FOOT-03.1/03.2; path-owned self/target suppression is
verified in the [full refill corpus](retail-pathfinding-routes.md#object-occupancy-and-target-exit-through-the-full-refill).

The cell predicate's executable rules are now isolated more precisely:

- Out-of-bounds coordinates or `cell & queryMask & 0xff000000` reject and set
  system `+0xd0=1`. Low-24-bit index `0xffffff` means an empty object chain.
- Links are eight bytes at map `+0x78`; link high byte 2 is skipped (search-node
  metadata), and the low 24 bits select the next link. Other link types read
  the object pointer from link `+4`.
- An object is considered when `object+0x38 != -1`, byte `+0x37` bit zero is
  set, and its visit stamp differs from the incremented map `+0xb4` stamp.
  It receives that stamp; only link type 1 then performs the blocking check.
- A visited type-1 object equal to system `+0xa8` sets system `+0xcc=1`, even
  when its policy flags permit passage. This pointer alone is not an exclusion.
- It blocks when `(objectFlags40 & 0x8fffffff)==0`, either system `+0xd4` is
  nonzero or `(objectFlags40 & 0x60000000)==0`, and
  `(objectMask34 & queryMask & 0xffffff)!=0`. Gameplay names for these policy
  bits remain unassigned.

`tools/ghidra/verify_wc3_pathing_footprints.py --exhaustive` executes the original
x86 with these independent scopes:

| Oracle layer | Cases | Executed / stubbed |
| --- | --- | --- |
| Four neighbour consumers | 1,118,464 | Every possible perimeter bit pattern; perimeter result and node allocation stubbed |
| Composed terrain expansion | 3,276 | Real perimeter scanner and cell predicate; empty/full/single/pair blockers at interior and six boundary positions; only node allocation stubbed |
| Cell high-byte predicate | 65,536 | Every cell/query high-byte pair, empty object list |
| Single-link dynamic predicate | 4,224 | Four link types, active/disabled/mask/target/query-mode combinations, zero and each single policy bit |

All **1,191,500 cases pass**. Report:
`/GitHub/wc3-analysis/reports/pathfinding-1.27/footprint-occupancy-oracle.json`.
The object matrix also checks visit stamps and `+0xcc/+0xd0` side effects.
Multi-link duplicate objects, node allocation, initial-placement validity,
and actual movement-class eligibility remain outside this oracle's scope.

Oracle: `verify_wc3_pathing_footprints.py` → `footprint-occupancy-oracle.json`.

Accelerated setup also takes a size class (`+0x90 = 1 << inputClass`), with
additional checks for class value 2 in side/corner expansion. This establishes
size-dependent traversal, not a classic scalar clearance map.

## Complete static fine-grid searches and stamp reuse

`verify_wc3_pathing_grid.py` executes the original search-entry routine
`6f14aa10`, node lookup/creation `6f147af0`, per-cell metadata insertion
`6f14d890`, occupancy queries, expansion, heap, relaxation and loop, with
**no code stubs**. It directly initializes map/search structs and preallocates
their storage. A third pass now also calls full fine-request wrapper
`6f148100`, including setup, search and route reconstruction. Constructors,
path-owned admission, dynamic movers and smoothing remain outside its scope.

The corpus contains 72 static 24×24 maps, each tested with all four footprint
classes: open terrain, a solid dividing wall, wall gaps of widths 1..6, and
64 deterministic random maps at four obstacle densities. Start `(4,4)` and
goal `(19,19)` have cleared surrounding regions. An independent Dijkstra
search uses the recovered footprint graph; it compares reachability and exact
15/21 route cost rather than reproducing retail's heuristic or heap ordering.
Every returned parent chain is checked for cycles, permitted footprint edges,
start connectivity, and summed cost. Exhausted searches also match the minimum
squared distance to the goal among reachable nodes.

All **288 searches pass: 123 reach the goal and 165 exhaust the reachable
component**. This is bounded corpus evidence of shortest paths on this static
graph, not a global proof covering scheduling, moving blockers or smoothing.

| Fixture | Class 0 cost | Class 1 cost | Class 2 cost | Class 3 cost |
| --- | --- | --- | --- | --- |
| Open | 315 | 315 | 315 | 315 |
| Solid wall | exhausted | exhausted | exhausted | exhausted |
| 1-cell gap | 342 | exhausted | exhausted | exhausted |
| 2-cell gap | 333 | 342 | exhausted | exhausted |
| 3-cell gap | 324 | 333 | 351 | exhausted |
| 4-cell gap | 315 | 324 | 342 | 360 |

Each case is then repeated using original reset `6f14a980`, incremented search
stamp, and retained per-cell metadata. All **288 repeats** produce byte-identical
node arrays, the same result and pop count, and no additional map-link entries.
This verifies reuse across two consecutive search stamps in this corpus;
Payoff77 adds full original16-bit wrap/control and engine node/route coverage;
[wrap evidence](retail-pathfinding-engine.md#fine-stamp-wrap-preserves-complete-engine-request-state).
Mixed dynamic-object lists are covered separately by the authored movement-mask
and target-chain compositions. Capacity remains open.

Node lookup checks the cell's first low-24-bit link. A type-2 metadata link has
search stamp in its low ushort at `+4` and node index in its high ushort at `+6`.
A matching stamp returns that node index; a stale stamp reuses the existing
metadata entry for the current search. A missing type-2 head is prepended by
`6f14d890`, preserving terrain high bits and the old list head, setting the
cell's bitmap bit through map `+0x98`, and incrementing metadata count `+0xb0`.
The node table is capped at `0x8000` entries in `6f147af0`; that cap and the
metadata free-list branch are visible statically but not exercised here.

Oracle: `verify_wc3_pathing_grid.py` → `fine-grid-request-oracle.json`.

## Partial layout useful for observation

| Field | Fine system | Accelerator |
| --- | --- | --- |
| Node storage / count | `+0x30 / +0x40` | `+0x5c / +0x6c` |
| Queue object / queue count | `+0x44 / +0x60` | `+0x70 / +0x8c` |
| Pop budget / counter | `+0x68 / +0x6c` | `+0x98 / +0x9c` |
| Destination cell XY | `+0x88 / +0x8c` | `+0xbc / +0xc0` |
| Closest-seen node / squared distance | `+0x9c / +0x98` | `+0xd0 / +0xcc` |

Node prefix: XY `+0/+4`, queue generation `+8`, bookkeeping `+0xc/+0x10`,
accumulated cost `+0x14`, heuristic `+0x18`, parent `+0x1c`. Accelerator node
`+0x22` holds level. These are partial layouts, not complete C++ types.

The top-level search wrappers can reconstruct a route to the closest-seen node
after failing to reach the requested node. A capped search can take this branch
too; it is not proof of an exhaustive nearest-reachable-endpoint guarantee.

## Map construction and invalidation

`6f15ab60` creates the map family on the object referenced by `6fd53a48`:

| Owner field | Role | Scale relative to fine cells |
| --- | --- | --- |
| `+0x234` | Coarse proximity map | 8 |
| `+0x238` | Fine proximity/path map | 1 |
| `+0x23c/+0x240/+0x244/+0x248` | Accelerator levels | 2, 4, 8, 16 |
| `+0x24c/+0x250` | Fine / accelerated search systems | — |

The four accelerator levels are **not** the fine map plus three coarser maps.
In the interlude capture their dimensions were 201×137, 100×68, 50×34 and
25×17, versus 384×256 fine cells. Constructor assembly adds 16 fine units to each input dimension, divides by
2, truncates and adds 1 for accelerator level zero; higher dimensions are that
base dimension shifted right by 1/2/3. The coarse proximity dimension uses the
same padded input divided by 8, then adds 1. Origin/boundary semantics still
need a complete account; blindly shifting fine dimensions gives the wrong answer.

`6f15d360(owner, rectangle, mode)` clips an update to the fine-map bounds,
updates accelerator level zero through `6f15cf80`, and propagates through three
parents using `6f15d470`. Mode 0 recomputes; mode 1 clears the base classification
byte before parent propagation. Accelerated requests temporarily use mode 1
around selected objects, then mode 0 to restore their classification.

The base classifier `6f15d0e0` queries four fine cells with one of the masks at
`6fce4570`: `06000006`, `80000080`, `40000040`, `04000004`. Each lane becomes
00 when all four queries are clear, 01 when all are blocked, or 10 when mixed.
The lane shifts are 0, 2, 4 and 6: ground, amphibious, floating and flight.
Coarse ground uses6; ordinary fine ground uses2.

Parent classifier `6f15d1c0` reduces four children. Missing children are blocked;
a clear child with a nonzero special marker at byte `+6` becomes mixed. This
prevents promotion from hiding special records. The other cell metadata is
preserved. `tools/ghidra/verify_wc3_pathing_cells.py` executes the original x86
routine under Unicorn: **20,736 combinations passed**, covering all valid child
states, marker combinations, lanes, and 1/2-cell boundary dimensions. This is
stronger than a test which only repeats our inferred formula.

The accelerator's fine-cell predicate `6f148e90` has narrower object eligibility
than ordinary collision query `6f1489a0`, including object flag tests at `+0x34`
and `+0x40`. Not every nearby moving object necessarily dirties the hierarchy.
Complete object category and lifetime mapping is still pending.

`SetTerrainPathable` is native `6f2148f0`: it negates the supplied Boolean and
calls `6f04d870` → `6f054000` to modify fine-cell high bits. No hierarchy update
call is visible in that inspected chain. The [direct cell witness](retail-pathfinding-experiments.md#direct-terrainhierarchy-divergence-witness)
confirms stale coarse classifications through 30 simulation seconds for this sequence.
The WPM load path `6f04c860` does call the full update through `6f04e0b0`.

## Terrain edit and explicit rebuild composition

See [producer/update inventory](#pathing-producer-and-update-inventory) for the
current engine owners and remaining timing/composition work.

`verify_wc3_pathing_maps.py` → `maps-oracle.json`: **11,664 complete edits,
216 edit/rebuild/reversal compositions, 30 clipped clear/restore cases**.
Original `6f04d870` receives X/Y scalar pointers in ECX/EDX, then mask byte and
blocked Boolean on the stack. Fine map: `owner+24c → system+1c`; world origins:
`[6fd3c82c]+6c/+70`. Exact dyadic inputs select
`floor((world-origin)/32)`; unsigned bounds reject outside cells. Observation at
`6f054000` checks selected cell/null, requested high-byte set/clear and unchanged
low 24 occupancy bits. Null edits preserve all storage; every edit preserves
all adaptive words until explicit rebuilding.

Full `6f15d360` matches an independent reducer/rectangle model: clip fine bounds,
visit inclusive `floor(min/scale)..floor(max/scale)` within each level, then
propagate. Empty/disjoint/inverted rectangles do nothing. Mode 1 clears base
classes; mode 0 recomputes. Unrelated cells/metadata remain exact. Missing
children classify blocked, so a boundary block already mixed may remain mixed
after a real edit/rebuild; unchanged classification alone does not prove no update.

Matrix: dimensions 16×16/17×23/31×18; origins `(0,0)`, `(-1024,-2048)`,
`(512,-256)`; four lanes; positive/negative boundaries and repeated edits.
Synthetic preallocated maps, no code stubs. Constructors/origin producers,
non-dyadic conversion, object invalidation, special markers and full JASS-native
entry remain outside this corpus; MAP-01/MAP-03 are not fully closed.

## Terrain-origin producer and map factory composition

`verify_wc3_pathing_map_construction.py` → `map-construction-oracle.json`:
25 full bounds getters, 25 loader/descriptor prefixes, 150 full base-map
initializers, 100 edits using produced origins, and 25 complete no-file loader calls
through six map and two search-system factories. No instruction patches/import stubs.

`78b0a0 → 7425a0 → 73db20` decodes first/last packed terrain records;
each coordinate is `128*index−32768`. Packed X is `word>>23`, Y is
`(word>>14)&0x1ff`; getter rectangle order is `[minY,minX,maxY,maxX]`.
`04c860` reorders to `[minX,minY,maxX,maxY]` and writes bounds to game `+6c..78`
and calls `15ab60`. Terrain extents `(dx,dy)` yield fine dimensions `(4dx,4dy)`.
For each fine dimension `D`: proximity dimension `(D+16)//8+1`; first adaptive
dimension `(D+16)//2+1`, successive levels right-shift that dimension by 1..3.
Scales are proximity 8, fine 1, adaptive 2/4/8/16. Origins therefore come from
terrain endpoints, while coarse/proximity dimensions include explicit padding.

The continuous factory cases run from `04c860` through genuine pool pops,
registry insertion, cell/dirty-table initialization, two maintenance-timer
insertions and scale assignment. Assert owner `+234..248` map pointers,
identities, dimensions, bounds, scales/inverses, initial cells and heap records.
The observation at `6f14ecb0` now resumes unchanged state through both
search-system factories and full `04c860` return. Assertions cover eight
registered identities, fine/four-adaptive map links, accelerator zeroed
256×12-byte index table and guards, restored FS:[0], and complete
`04e0b0 → 15d360` hierarchy initialization against an independent reducer.
Prerequisites: six reusable map entries and two search-system entries with
original vtables, preallocated tables, eight free registry slots, two request
blocks and heap capacity. Filename is null: this is the genuine no-file loader
branch. Terrain/WPM/archive deserialization, allocation growth, destruction/
reload and actual searches remain outside it. Maintenance/release coverage follows.

## Constructed-map maintenance and partial release

The construction oracle composes all 25 worlds with terrain edits and three
maintenance deadlines: **75 predeadline no-op drains, 75 due drains, 150 real
callbacks**. Chain: `0523d0 → 04f4d0 → 0543c0 → 14df20 → 053780 → 04fa40`.
Both proximity requests repeat, advance deadlines and retain queue membership;
callbacks observe the exact deadline. Supplied dirty empty cells clear and
high-bit stamps reset. Dirty bits are fixture state, not attributed to terrain
edits. Fine terrain edits survive; **all accelerator words remain stale through
all three rounds**. Explicit `04e0b0 → 15d360` then incorporates the edit.

**25 `15b670` release prefixes** unregister/recycle both search systems
(registry live count 8→6), clear owner links, cancel the first proximity timer
(`20001→30001`) and clear its request pointer. Stop exactly at `6f07c678`,
Storm `Ordinal_403`, with the first dirty-buffer address verified on the stack.
No replacement allocator/free is used. Full proximity/accelerator release,
reload, post-release timer draining and maintenance of actual retired occupants
remain open; the prefix does not establish those outcomes.

## Decoded WPM and image mask consumers

`verify_wc3_pathing_load_masks.py` → `load-masks-oracle.json`: **6,144 WPM
loops** (all256 byte values), **12,288 image loops** (all256 combinations of
channel values0/1/127/255), 72 zero-dimension cases. Exact original slices:
`04caba → 04cb4f` (WPM), `04cc3f → 04ccb9` (decoded image).

| WPM input condition | OR into fine word |
| --- | --- |
| `02` | `13000000` |
| `04` | `05000000` |
| `08` | `09000000` |
| `20` | `20000000` |
| `40` | `41000000` |
| `80` or both `02+40` | `81000000` |

Image nonzero byte offsets0/1/2/3 OR `09000000/05000000/03000000/20000000`;
values are Boolean, not proportional intensities. Optional vertical reversal
uses source height. Every destination word, existing flags/occupancy, padding,
guards, input bytes and final input pointer are checked. Four isolated oversized
nonzero-mask probes fault on a null OR destination: a consumer precondition,
not proof that malformed files reach it.

Decoded payloads and locals/registers are supplied. Parsing, decompression,
archive operations and full-loader execution are excluded from these slices;
no stubs, byte patches or import replacements.

## Widget pathing-texture invalidation producers (static)

Widget vtable slots`148/14c/150/154` resolve to
`6501a0/650c00/6514d0/6544f0`: create/apply, remove, rasterize and reapply
pathing-region collections. Confirmed in CUnit`6fb77eb0` and
CDestructable`6fb7d708` (not interchangeable with ability vtables).
Resource selector`6524c0 → virtual+b0 → 3422e0/342300` reads distinct
resource`+cc/+d0` pathing textures. Geometry chain:
`22f410` snap → `22e9c0` rotated byte traversal → callbacks`650a70/652b40`
→ `063e50` matching-region insertion → `22f1d0` footprint bounds →
`04e0b0 → 15d420 → 15d360` hierarchy refresh. Region membership tests
`region+34 & pixel & ff`; callbacks select record high bits0/01000000.
Creation masks are`c2,10,8`, optionally4 (resource`+64` bit0).

Concrete producers: destructable construction`6c0d90 → 6c20a0 → virtual148`;
`6c12f0/6c3630` remove then apply; registered native **DestructableRestoreLife**
`1fd3c0 → 6c3790` removes `(1,0)`, restores state, applies `(0,1,1)`.
All four widget methods gate refresh on`6fce5f10`. Setter`651160` receives0
before bulk setup in`1e5a10` and1 afterward; separate full refresh occurs in
`1eab40` after`6bdd00/04dc90/271410`. This is a concrete batching producer,
unlike fine-only terrain edits. Rasterization/overlap and one actual widget method/gate execute below; named
destructable lifecycle entry points remain static, not completed MAP-03 evidence.

## Widget rasterization and overlapping occupancy

`verify_wc3_pathing_widget_masks.py` → `widget-masks-oracle.json`: **144
A-insert/B-insert/A-remove/B-remove sequences**, 576 full stages, 2,496 original
callbacks and 3,008 exact cell records. Execute
`22e9c0 → 652b40/650a70 → 063e50 → 14d9e0`, then
`22f1d0 → 04e0b0 → 15d360`. Four orientations, textures1×1/2×3/3×2,
two origins, heterogeneous bytes and coincident/offset overlaps match independent
geometry and hierarchy models.

Pixel centers start at center−`(dimension−1)*16`, advance32 world units and
rotate by quarter turns. Bounds extend`dimension*16`, swapping axes for odd
rotations. Region masks`c2/10/8/4` select matching pixels. Insert and remove
both prepend real records (high bits01000000/0); assert complete linked history,
counts, dirty bits and callback order. Rasterization alone preserves hierarchy;
explicit footprint refresh reflects effective occupancy. Removing A preserves
B; removing B restores baseline **without first compacting record history**.

**96 full `6514d0` widget-method calls** add authentic CUnit/pose vtables,
resource-cache hits, registered mover position/heading conversion, snapping and
actual refresh-gate execution. Matrix: three dimensions × four orientations ×
two origins × two fractional offsets × gate0/1. Original setter`651160` selects
the gate. Both values produce identical fine removal records; gate0 preserves
blocked hierarchy until explicit rebuild, gate1 invokes`04e0b0` and restores it.
Snapping per axis is `trunc(v/64)*64 + sign(v)*(32*((extent>>1)&1)+16*(extent&1))`
with rotated extents; tested at interior positive/negative coordinates.

**96 paired widget lifecycles** add192 full `6544f0` reapply and192 full
`6514d0` removal calls: distinct A/B CUnit/pose identities, overlapping region
collections, both gate values. A removal preserves B occupancy; B removal
restores empty hierarchy. Original terrain lookup and proximity enumeration
visit608 empty cells; query depth/activity reset after each call.

**320 full nonempty `6544f0 → 654090` calls** add one registered stationary
occupant: two footprints × four rotations × two origins × five interior
positions × two gates × two dispatch flags. Original radius cache/RNG produce
an escape proposal across the nearest rectangle edge (Y wins distance ties),
expanded by effective radius16 plus jitter. Independent integer arithmetic
checks proposal bits using observed jitter; RNG independence is not claimed.
Unit`+5c` bit4 suppresses dispatch in160 cases;160 others call actual`69dd60`
with order`d0012` and receive rejection`dd` for an empty ability list. Pose
stays unchanged and query/ref lifetimes balance. One separate accepted Move
ability prefix executes full`69bd80` order allocation/registration: original
COrderTarget factory and`689a60` point initializer preserve the proposed bits,
registry live count5→6 and wrapper/payload identity match. It stops at`680320`
dispatch entry and does **not** establish successful displacement. Next chain
is cancellation/action-task production`673fe0`, queue`691c70` and`67abe0`
dispatch with a genuine Move subscription.
Snapshot `widget-masks-factory-stable.py` (external reports directory), SHA256
`cab7ed6b2ee40c57d6998710db68de3c629fda902452e8cab9339182f01bef63`; run with
`PYTHONPATH=tools/ghidra` for the independent numeric model.

Collections/cache/storage are supplied. Widget creation/destruction, resource
decoding/cache misses, moving poses and map-edge snapping remain open.
No stubs, instruction patches or import replacements.


### Widget destruction and external free boundary

Two original`650c00` prefixes (second argument0/1) execute pose flags/update,
`063b40` and four`14dae0` region retirements. Registry live count8→4, all four
A-region identities invalid, stamp`ffffffff`, six pending cell records each;
overlapping B stays registered. Stop is actual Storm403 trampoline`07c678`,
return address`063bb1`, freeing the collection pointer array. No fake free:
widget`+34` and collection header remain uncleared because the call has not
returned. Coarse maps remain unchanged at this boundary. A **separate** original
`15d360` rebuild removes A-only coverage and preserves offset-overlapping B;
this is not evidence that the destructor performs that rebuild.
`widget-masks-destruction-stable.py/.json` preserve the bounded cases under the
external reports directory; script SHA256
`b2ef02b6d70bdbf16118bdf015b04b067973933275dcc729093a1065402662d9`.
Full free/remaining destruction/refresh and heap ownership remain open in the
emulator. [Live Farm creation/removal](retail-pathfinding-experiments.md#live-building-footprint-creation-and-destruction)
observes the complete method return clearing the same collection; heap/region
accounting is not inferred from that pointer transition.


### Widget-produced escape through arrival

One original`6544f0` invocation now returns after order factory`69bd80`,
`680320` cancellation, `691c70` user-queue publication, `67abe0` dispatch and
Move task acceptance. Exact world proposal→grid conversion, user order`[5,105]`,
remaining task chain`d016b,d0165,d014a,d0148,d0166,d0162`, group`[20,120]`,
path`[21,121]`, member`[3,103]`, speed8 and final hierarchy refresh are asserted.
The query callback releases its temporary reference; three self-subscriptions remain.

The initial staged run was provisional. A subsequent **uninterrupted**
`6544f0` call provisions all pools, registry, subscriptions, terrain, CRT and
original mover-bounds setup before entry, preserving the proposal and all
acceptance invariants. Root rerun passes. This audit caught an invalid fixture
count: proximity storage already held occupant record0 but setup declared zero
allocated/live entries; pre-entry movement could overwrite it and cycle the
list. Correct counts1 and original`15fb40` bounds setup fix the fixture without
clearing cells or changing the proposed target. Unit refs now4→4 across the
whole call, rather than the staged transient5→4. The original admission evidence is now extended through arrival below.
Stable script/report `widget-masks-uninterrupted-stable.py/.json`, SHA256
`72c76af33698f54da61029b20fbacac00a370640b8b5d0b1f8da969bdff88525`;
staged snapshot is retained as superseded evidence.


The historical terrain-only control follows that accepted idle-unit order without replacing
its proposal or removing the widget footprint. It explicitly supplies individual path
query mask`02000000` before travel: terrain-only, with no low widget mask bits.
That intervention does not recover the public movement-class mask producer;
The stock-mask extension below closes that requirement. Empty search buffers and scheduler storage are
provisioned, then original `16c150` performs fresh search. The three-point route is
`[target, (7.5,7.5), start]`; both start and intermediate cells retain widget A's
blocking record. Original `054190` advances seven intervals of1/32 second, with
`16c150` deciding and committing movement. Every position word matches the
independent scalar integration of the preceding velocity. Seven position/velocity
pairs are frozen in `tools/ghidra/fixtures/retail-widget-escape-journey-1.27.json`.
Normalized journey SHA256:
`7d91e8aa712afb8a5025e9b0f98d2a19479f4b4c19bc7973abb6a49079bb4c0c`.

At tick7 original `5fa7a0` observes user head`[5,105]` and completes the command.
Task/user heads and tail become invalid, queue/action/Move flags become zero,
mover group identity becomes invalid, and velocity becomes zero. After two further
clock intervals the direct group tick releases group/path storage; all order/task
payload free lists and wrapper storage are restored. Widget region flags remain
unchanged on every travel tick, and start/intermediate cells are still occupied.
The oracle's existing destruction-prefix fixture then runs independently; its
Storm403 boundary remains unchanged.

Report `map-03.4-widget-journey-exact.json` certifies this composed original-code
journey. It is not a full-owner/public-widget-creation capture: the fixture supplies
cache, terrain, buffers, pools and unit/Move backing, directly ticks the group,
and uses the original RNG without an independent seed model. Multiple occupants,
active-order interrupt/resume, public producer timing and full creation/free remain
separate backlog requirements. Ghidra now names/types `Widget_ReapplyPathingAndDisplace`
and `Widget_DisplaceOccupant`, including the32-byte `WC3WidgetEscapeContext` prefix;
unused offset0 remains undefined. Saved metadata:
`map-03.4-ghidra-widget-types.json` (301 names,19 layouts,122 fields,69 prototypes).

[Engine idle admission](retail-pathfinding-engine.md#widget-escape-idle-admission)
executes a construction-margin escape through normal server frames. A discovered
inside-footprint engine failure is explicitly MAP-03.7: at worker`(0,-64)` inside
an active9×9 footprint, endpoint validation rejects intermediate occupied cells.
The supplied terrain-only query mask permits those occupied cells in this fixture;
it does not prove the public retail exclusion policy. Recover the actual movement-class
mask and exclusion/region producer before integrating inside-footprint behavior.
Do not clear the whole footprint or bypass unrelated terrain/unit collision.


### Stock Footman mask through widget escape

MAP-03.4 now also runs the accepted widget escape with the observed Footman
profile: rawcode`hfoo`, category`ca`, query`2`. Original`678b50/678b60` load the
unit rawcode, execute the original`198420` profile hash lookup through
`690c20/690c80`, and original`05c7e0` publishes category/query through the
Unit+164 mover bridge **before** uninterrupted widget admission.`05c7b0` retains
the fine-region high byte and replaces its low24 category bits;`05c770/168c40`
produce owned-path mask`02000002`. That word remains unchanged on every travel
tick. No terrain-only mask is written in this variant.

The retained footprint changes the route to
`[target, (7.5,8.5), (6.5,8.5), start]`. The original group reaches the unchanged
widget proposal after **13** intervals of1/32 second. Every raw position and
velocity matches its frozen original witness; position integration also matches
the independent software-scalar model. Fine-route index progresses2→1→0,
then becomes invalid as arrival clears the route. Route count, points, indices
and flags are frozen and checked on every tick in both variants. Arrival drains
the original task/user queues; two more clock intervals restore all group/path,
order/task payload and wrapper free lists. Footprint region words remain active.

Fixture:`tools/ghidra/fixtures/retail-widget-footman-escape-1.27.json`.
Normalized travel SHA256:
`89cc3f45a5b13db6d1b633c7ed8413cfac7680fa8c3c45dc8088c6c6a7aefc7e`.
The profile words come from archived live`movement-profile-stock-raw.jsonl`,
SHA256`cf8532cdde7740c937fe5b9a50825b6848f253f80857e7782e1be6d2c384ed0a`.
The fixture supplies that cache hit, a blank Footman presentation resource,
custom radius8, pools and direct group cadence. It executes the authentic mask
getters and bridge, not full`6945a0` class/ability notification or authored cache
parsing. Those remain BASE-03.1/MAP-02; this closes one composed widget escape,
not public construction or whole-engine trajectory parity.

Ghidra persists the rawcode+30 field,16-byte mover bridge identity prefix,
query/category setters/getters and refresh caller. Saved metadata:
`map-03.4-ghidra-final-stock-types.json`:312 names,20 layouts,124 fields and78
explicit prototypes. Public build native`206a20` and placement gate`66f050`
also have persistent descriptive names/comments; unverified parameters remain
untyped.

Eight complete owned Frida captures preserve the attempted public producers:
scripted Town Hall creation, pathing toggles and construction order variants.
They do **not** call`654090` or produce an escape. Native`206a20` accepts placement
statuses0/45, then requires`69dd60` status0 before admission. Both the initial
ramp site and a nearby flat W3E vertex patch return44 before order validation.
The final footprint observer identifies`66fba0` callback dispatch→`68f700`→
`6800f0`: query8 returns blocked at`(-2160,-720)` or`(-2736,-784)`, sets
placement context+34 to1, stops raster iteration and returns44. Thus vertex
flags alone do not certify buildability; no controller or terrain-only cause
is inferred. Raw captures`runtime/map-03.4-widget-*-raw.jsonl` embed tool/map
hashes; exact tool sources are archived beside each. The generic corpus audit
admits their complete negative status, not positive escape evidence.
Authored RoC`UnitData.slk` maps`htow` to`PathTextures\16x16Simple.tga`;
`UnitUI.slk` prevents placement on`unbuildable`. The actual texture is16×16,
with a12×12 walk-blocking interior and blue build-blocking coverage across all
256 pixels. A5×5 terrain vertex patch does not resolve ground-texture, object
or generated buildability masks. The public producer remains MAP-03.3.


## Solid-widget can't-path recovery

MAP-03.7 extends the same uninterrupted original `6544f0` admission fixture with
supplied9×9 bit2 walk-blocking pixels. Original profile getters/bridge retain
Footman mask`02000002` and category`010000ca`; custom radius8, class/profile/resource
backing and pools remain supplied. The solid texture is supplied before entry;
no query-mask or footprint mutation is made after admission.

Initial route is empty; all seven1/32 clock ticks preserve source raw position
`[1087373312,1089732608]` and velocity`[0,0]`. At tick6 the one-point route is the
source itself. At tick7 the recovery observation at `5fb190` is argument1,
clock`1046478848`, internal head`[12,112]`, retained user head`[5,105]`.
Original `603110` casts the COrderTarget through `21b890/21b7f0`, and `5fb190`
performs its fallback task chain before `5fa7a0` drains ownership. Final internal,
user and group heads are invalid, queue/action/Move flags are0, and all payload,
wrapper, group and path free lists are restored after two release ticks.

The frozen `retail-widget-solid-failure-1.27.json` derives from original execution
report`map-03.7-solid-widget-complete.json`, pinned by its SHA256. Its normalized
trajectory/lifetime digest is
`c780d38d2c2f5e48e88fdc0ff727002e452fc307524e3137ee91359dc267b060`.
The original narrow-mask escape still reaches its destination after thirteen ticks;
this variant establishes failure/cleanup for a different footprint, not a new
through-building movement rule. Public construction/texture parsing, whole owner
cadence and an independent RNG seed model remain excluded.

A missing supplied class-parent field initially hit original class-cache allocation.
RTTI already identifies COrderTarget as derived from COrderPoint (`ord.`). Original
`04ce00` checks class-row`+78` against the requested class before its cache. The
fixture now supplies that authentic parent; it does not cache a false cast or
replace the allocator. Ghidra persists `AgentClass_IsDerived/InOwner`, the two
class getters, placement callback dispatch/raster-cell predicates and their exact
operand storage. The complete saved map is320 names,22 layouts,138 fields and84
explicit x86 prototypes (`map-03.7-ghidra-types.json`).

The prior public Town Hall negative captures are explained more precisely by
`66fba0 → 68f700 → 6800f0`: callback failure44 propagates without any of the four
direct `66f050` status44 assignments. The footprint context supplies query8,
world point`+10`, orientation`+18`, texture`+54` and excluded mover bridge`+50`;
`04e060` takes that bridge pointer, not a mode integer. The first query-blocked
pixel sets context`+34` and stops the raster; `68f700` maps it to44 after restoring
its builder`+4c` and temporarily excluded objects. This is saved instruction/live
rejection evidence; a successful public construction escape producer is still open.


## Mixed fine objects reach the engine

The [idle-object engine integration](retail-pathfinding-engine.md#idle-objects-affect-nearby-move-routes)
adds192 full original mixed-chain searches, repeated metadata reuse and complete
requests under observed ground/flight masks. Production eligibility and entering
strips feed the C search directly; all route/cost/work/node results match at
O0/O2. Fresh read-only hfoo/hgry captures correlate velocity bit20000000 with
actual vector80/84 and distinguish categoryca from flight category0. The active
fine-region high bit alone does not make flyers block a flight search.

This closes incremental engine occupancy, not the full movement-category table
or target-exit matrix. Water/amphibious producers, public start/goal admission,
endpoint-mode composition and group transient policy still have separate owners.

## Ordinary classification reachability

Payoff79 closes ACC-02.1 and ACC-03.2 without adding task IDs. The existing
adaptive oracle's `--terrain-producer` composes original fine queries,
`15d360 →15cf80/15d0e0 →15d470/15d1c0`, then complete `162cb0` requests.
The frozen input/output is `retail-adaptive-terrain-producer-1.27.json`.
The engine counterpart is `pathfinding.terrain_producers_preserve_retail_size2_passage_veto_and_partial_route`.

Ordinary fine flags2/4/40/80 have16 possible per-cell combinations. Enumerating
all16^4 four-cell patterns gives **54 reachable four-lane class tuples**; each
has a complete original rectangle-producer witness. Of81 tuples using0/1/2,
**27 are rejected**: ground/flight pairs(0,1),(0,2),(2,1) cannot arise because
coarse ground6 contains flight4. Amphibious80 and floating40 are independent.
Ordinary full base/parent rebuild clears byte7 before calling the four setters,
so class3 cannot be emitted. Calling a setter alone on stale class3 storage is
outside that proof. Padding starts zero; only the clipped inclusive rectangle
is classified. Byte6 special markers and object eligibility are separate domains.

| Fixture family | Producer disposition | Remaining scope |
| --- | --- | --- |
| Ordinary adaptive open/wall/gap/random/shortcut maps and reduced passage | Selected-lane projection reachable by filling every blocked coarse cell with four finec6 cells. Synthetic unrelated-lane constants are controls; ground-clear/flight-blocked full words are rejected | Public fallback/mover journey: ACC-03.3; public shortcut/result consumption: FINE-04 |
| Reduced size2 east veto | Reachable through full original classification, padded41/20/10/5 maps and unchanged request. All four lanes retain38 pops/56 nodes, partial endpoint(16.5,26.5), ordinary occupancy1 and boundary predicate0 | Full mover retry/arrival remains ACC-03.3 |
| Four-lane adaptive wrap map | Ordinary reachable projection: ground blocked set contains flight blocked set, and80/40 are independent; existing engine fixture constructs the corresponding fine masks | Seeded warm metadata/capacity limits remain ACC-05.1 |
| Constructor/WPM/widget/terrain-rectangle maps and exclusions | Produced by original constructors, full WPM load, widget rasterization or rectangle update; clear/exclude0 and restore0/1/2 are witnessed | Dynamic producer composition and exclusion exits retain the IDs below |
| `--disable-promotion` and `--force-east-boundary` | Explicit counterfactuals. Arbitrary mixed parents of clear unmarked children and forced predicate acceptance are rejected as ordinary execution | Similar mixed flags from real special records remain ACC-02.2/ACC-01.2/GATE-03 |
| Special byte6, warp tag/node and nonordinary object flags | Unresolved beyond the documented Way Gate producer and supplied-object matrices; do not promote supplied storage to gameplay evidence | ACC-02.2, FOOT-03, GATE-01/02/03/04 |

The produced reduced map contains72 blocked fine cells in a64×64 terrain and
**2,206** allocated hierarchy cells. Engine owned requests compare all four
lanes of every cell, all56 normalized node records and the six-point partial
route twice over retained buffers. Its original destination survives the partial
search. The known reference difference remains expected exit1; this evidence
justifies preserving the retail veto, not changing it to a conventional A* edge.
See [engine payoff](retail-pathfinding-engine.md#terrain-produced-adaptive-passages-preserve-the-retail-veto).

## Pathing producer and update inventory

MAP-03.3 is an inventory task; it does not certify every producer's complete
engine lifetime. The table assigns remaining work to existing IDs rather than
creating new leaves. Fine terrain, linked occupancy, coarse proximity and
adaptive classes have distinct update contracts. A class/profile query change
must not be confused with removing a unit's occupied region.

| Producer / retail entry | Affected state and timing | OpenRealm owner / evidence and remaining ID |
| --- | --- | --- |
| Map initialization `04c860 →15ab60 →04e0b0` | Allocate fine/proximity/four adaptive maps; decode WPM fine high bits; full hierarchy before return. Bulk widget setup suppresses local refresh through`651160(0)`, then enables it and rebuilds in`1eab40` | `G_SpawnEntities`, `CM_BakeStaticObstacles`, `move_acc_prepare`; MAP-01/02 initialized grids match. Active reload/lifetime: MAP-06.1 |
| Unit spawn/profile `6945a0 →05c2e0/05c7e0` | Radius and distinct category/query publication; fine and proximity rectangles become visible through mover integration. Seven stock public profile captures and public birth placement exist; broader class callbacks are not all covered | `SP_SpawnUnit`, `S_InitUnitPosition`, `G_BindEntityData`, Move occupancy snapshot; full factory/callback graph: BASE-03.1, chronology: FINE-01.6 |
| Movement and axis/point setters `1603d0`, `05c200`, `698050` | Commit old velocity/displacement and clock, then proximity`1604d0` and fine`160590` linked rectangles synchronously; public point placement Stops first, axis setters preserve the active order | Move `unit_commit_pose`, `S_SetUnitAxisPosition`, `S_SetUnitPosition`; native fine pose is retained. Per-cell insertion/removal ordering: FINE-01.6; public cadence: NUM-02.3 |
| Radius/type changes `15fef0`, public Chaos/profile notification | Radius write commits zero displacement immediately, updating both rectangles; path/group footprint caches have separate lifetimes | `G_ChangeUnitType`, `S_UnitAbilityEvent`, Move route radius/cache checks; moving radius and local cohort parity are covered. Bound shared/target radius producers: FOOT-01.3 |
| `SetUnitPathing` `215540 →699200 →05c7e0` | False clears own query while preserving occupancy category; true republishes authored query/category. This is not wholesale static footprint removal | `api_unit.h` writes `no_pathing`; Move query0 and occupied rectangles remain distinct, public toggles covered. Wider object/hierarchy consumer policy: FOOT-03.1/02 |
| Widget spawn/reapply/death/restore `6c0d90`, `6501a0/650c00/6514d0/6544f0`, `1fd3c0` | Rasterize/remove region links; gated footprint rectangle refresh before next query; overlapping objects and retained death textures survive independently | `SP_Destructable`, `G_InitializeDestructablePlacement`, Kill/Restore/Remove and static bake; MAP-02.3/03.2/03.5/03.6 complete. General category/link chronology: FOOT-03/FINE-01.6 |
| Construction start/complete/cancel, widget notifications | Widget application and exclusion/recovery paths exist; successful public construction margin-escape producer remains unproven. Do not infer static-path removal merely from Birth animation | `G_StartConstruction`, `G_FinishConstruction`, cancellation/`unit_die` bake at their owning transitions. Repeat public building controls and settle actual producer/notification timing: E2E-02.2; freeze invalidation differences: E2E-06.2 |
| Removal/depletion `G_FreeEdict`/`G_DeferFreeEdict` counterparts | Retire region collection/storage before next route; native tree/mine/gate lifecycles confirm remaining occupancy and hierarchy | Engine central free bakes immediately after clear/hide using `G_EntityHasStaticPathing`; MAP-03.5/03.6 complete. Spatial reuse/capacity: MAP-05.1/03; full factory teardown: BASE-03.1 |
| Terrain writes `2148f0 →04d870 →054000` | Fine high bits change synchronously; hierarchy stays stale until explicit footprint/full refresh. Low occupancy bits survive | `G_SetTerrainPathingFlags` updates fine masks/legacy fields only. Payoff83 retains independent hierarchy publication and map-lifetime route ownership; widget changes refresh only their old/new rectangles. Seven repeated regional snapshots,724 full commits and eight saved continuations match. See [update contract](retail-pathfinding-engine.md#pathing-update-state-and-ownership-contract); categorical/special eligibility remains FOOT-03 |
| Request exclusions `15d360(mode1/0)` | Clear rounded source/target coarse rectangles, search, restore classifications/parents; fine occupancy is a separate exclusion scope | `move_acc_object_rectangle` restores coarse ground6; Payoff78 covered. Nested objects, early/reentrant exits and edits during request: MAP-04.1/02 |
| Way Gate activation/retarget/removal | Special records/byte6 prevent promotion from hiding portal edges; identity and link updates are not ordinary terrain flags | Explicit engine Way Gate traversal exists; adaptive special-edge routing and marker collisions/lifetime: GATE-01/02/03/04 and ACC-02.2 |
| Save/load with active owners | Engine restores terrain, separately published hierarchy classes,27 sparse pools, native fine pose/FIFO/route buffers; rebuilds fine masks/static producer snapshots without globally publishing pending terrain edits | `ReadLevel`, `ReadMoveRouteBuffers`, `ReadPools`, `G_RebindSavedMoveRoutes`; current journeys retain literal native suffixes. General cross-feature lifetime: MAP-06.2/E2E-04.2 |

Do not rebuild adaptive classes after every moving unit commit based solely on
its fine/proximity update: hierarchy object eligibility in`148e90` is narrower
than ordinary collision eligibility. Exact dynamic hierarchy publication remains
FOOT-03/E2E-06.2. Payoff81 replaces foreign-first rectangle queries with
retained active fine-cell history and closes ordinary overlapping target/blocker
chronology. Native retired-link storage and other category producers remain
MAP-05/06 and FOOT-03. See [engine insertion history](retail-pathfinding-engine.md#overlapping-targets-retain-fine-cell-insertion-history).
