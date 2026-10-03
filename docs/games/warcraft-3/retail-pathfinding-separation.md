# Retail pathfinding evidence: Separation and spatial maintenance

[Contract, current ledger and reproduction](retail-pathfinding.md).
Retail **1.27.1.7085** only; addresses and report paths use the conventions there.

## Separation update anchor

The owner update `6f15aa80` rotates a parity counter at `+0x53c` and visits every
other entry of the list at `+0x51c`, calling `6f1702f0` with shared scratch
container `owner+0x540`. Thus this pass is staggered across pathing updates.
`6f1702f0` first checks/decrements a ushort cooldown through `6f171320`.
It selects a five-float configuration row at `6fd54398 + 20 * class`, prepares
a proximity query through `6f170960`, and accumulates XY values at `+0x18/+0x1c`
from nearby records. The end of the routine applies configured thresholds and
calls `6f15fc70`; an empty/small result clears those XY values and sets a seven-
update cooldown through `6f16ebd0`.

The accumulated vector is consumed by `6f16ffa0` on a later update: it adds
the XY values to the current position, checks the candidate through `6f16ee80`,
and applies it through `6f05c820` → `6f15f7b0` when accepted. The check uses the
same four footprint classes, temporarily excludes the owner occupancy record,
and temporarily sets fine-system `+0xd4` to 1. Query enumeration
`6f170c00` → `6f170b30` walks the coarse proximity map, deduplicating candidates
with stamps and filtering linked-record tags. Candidate predicate `6f16e830`
rejects the owner, nonmatching object type `0x60706375`, selected object flags,
nonpositive footprint, nonzero mover `+0xc0`, missing separation state at
`mover+0xac`, mismatched category bits `[27:20]`, or insufficient rank bits
`[31:28]`. Gameplay meanings of those categories/ranks are pending.
This ties separation to the
existing occupancy data rather than proving an independent avoidance mesh.

The arithmetic and eligibility contracts follow; complete live replay remains open.

### Separation vector update

Assembly of `6f1702f0` and the original arithmetic slices recover the
non-overlap update. Denote the selected configuration row as
`[radius, minimum, cap, pairWeight, damping]`, the retained vector at
separation `+18/+1c` as `v`, and the difference between the current mover
position and a candidate position as `delta`:

```
d = engine_length(delta)
if d < radius:
    weight = pairWeight * (1 - d / radius)^2
    v += engine_set_length(delta, weight)

length = engine_length(v) * damping
if length < minimum:
    v = (0, 0)
    cooldown = 7
else:
    v = engine_set_length(v, min(length, cap))
```

The pair operation is applied in candidate enumeration order. The tail runs
after the neighbor loop, not after each contribution. `engine_set_length`
is `6f15fc70`, which uses the engine inverse-square-root helper; it does not
merely multiply the input vector by the requested length. Rows 0–4 therefore
use pair weight **0.4**, damping **0.7**, minimum **0.01**, and output caps
**0.5** for row 0 or **0.2** for rows 1–4. Radii are **5/7/8/9/10** fine
cells. The caps correspond to 16 or 6.4 world units per eligible separation
visit, not per rendered frame.

This update retains memory: before examining neighbors, `6f1702f0` calls
`6f16ffa0` to try applying the **previously stored vector** to the mover's
position. That routine tests the proposed position with `6f16ee80` and calls
`6f05c820` only on acceptance. It does not clear the retained vector. New
pair contributions are then added to it before damping/clamping. Thus an
isolated nonzero retained vector can decay over subsequent visits; this is
not a fresh zeroed overlap sum on every visit. An active cooldown returns
before either application or accumulation. A positive mover `+c0` instead
clears the vector and installs the idle cooldown.

When computed pair distance is below
`DAT_6fcd53a0 = 0.0010000000474974513`, the code replaces `delta` with a
vector from `6f1d19e0`, using the pathing owner's PRNG, and recomputes its
length before applying the same radius/weight logic. Original execution of
this branch is verified below; it must not be replaced with a fixed direction
in a parity implementation.

`verify_wc3_pathing_separation.py` now also executes **480** original pair
slices: `6f170359..6f170367` computes the reciprocal radius, and
`6f1703e0..6f170518` runs subtraction, length, weighting, normalization and
accumulation. Inputs cover five radii, twelve distances, four directions,
and both zero/nonzero prior vectors. Maximum absolute component difference
from the mathematical formula is **4.01e-6** in this corpus. **42** original
output-tail cases (`6f170525` through return) cover three directions, seven
magnitudes and both caps; maximum difference is **6.65e-6**, with exact
cooldown-bit preservation checks. The independent assertions use a 0.0002
component tolerance; these are numerical formula checks, not bit-identical
reimplementations of the engine helpers. No helper functions are stubbed.
Position resolution and
complete crowd dynamics remain outside these slice tests.

### Random direction for coincident positions

`6f1d19e0` consumes exactly one `6f1b7130` draw from the supplied owner
state and constructs the direction as follows:

```
u = (draw & 0x7fffff) / 8388608
angle = engine_multiply(u, 6.2831854820251465)
delta = engine_cos_sin(angle)
```

The low bits are converted to `[0,1)` by forming float bits
`0x3f800000 | (draw & 0x7fffff)` and adding runtime constant -1. The turn
constant is the float at `6fcd5464`. Trigonometry helper `6f071340` uses a
quadrant/index/interpolation calculation over the binary's tables, not a
host `sin`/`cos` call. Its output ordering is `(cos, sin)` as written by
`6f1d19e0`. This establishes an angular mapping; it does not prove statistical
uniformity or independence of the underlying PRNG.

The substitute vector is approximately **one fine cell long**. Its length
replaces the original near-zero distance in the repulsion falloff. Therefore,
with radius 5 and pair weight 0.4, exact overlap contributes approximately
**0.256**, rather than 0.4, before output damping and clamping. The radius-10
case similarly contributes about **0.324**. This discontinuity at the
near-zero threshold is part of the recovered implementation.

The separation oracle adds **64** complete original direction-generator
calls. For each seed it independently executes one original PRNG call and
checks that the generator leaves exactly that resulting two-word state.
Direction components match `(cos(angle),sin(angle))` within **5.65e-6** in
this corpus. A further **640** original pair slices cover those seeds, all
five nonzero radii, and both exact coincidence and distance 0.0005. They
execute the random branch and its original numerical/trigonometric helpers
without stubs, confirm the same one-draw state transition, and match the
falloff computed from the returned direction's length within **1.75e-7**.
These are numerical and state-transition checks on synthetic inputs, not
bit-identical replacement math or live overlapping-unit witnesses.

### Displacement validation and rejection

`6f16ee80` validates the proposed separation endpoint through the owner's
fine system (`owner+24c`). It copies the mover path's mask (`mover+a8` →
`path+9c`), selects one of four footprint classes from mover radius `+90`,
and calls `6f149370` → `6f1492b0`. Coordinate conversion floors both
components. With `x=floor(endpoint.x)` and `y=floor(endpoint.y)`, the checked
cells are:

| Radius in fine-cell units | Minimum cell | Occupied rectangle |
| --- | --- | --- |
| `<0.5` | `(x,y)` | 1×1 |
| `0.5 ≤ r < 1` | `(x-1,y-1)` | 2×2 |
| `1 ≤ r < 1.5` | `(x-1,y-1)` | 3×3 |
| `r ≥ 1.5` | `(x-2,y-2)` | 4×4 |

These are full occupied rectangles, distinct from the larger perimeter rings
used to derive search-neighbor masks. The validator receives only the
endpoint and does not sweep the displacement segment. Terrain-mask overlap,
map bounds and eligible object occupancy all participate through
`6f1489a0`. A mask mismatch permits a cell even when it contains other
terrain or object flags.

Before testing, the wrapper increments spatial object `+40` through
`mover+98`, when present, and forces fine-system `+d4` to **1**. It restores
both on normal return. The increment suppresses the mover's own occupied
object: `6f1489a0` ignores objects with any `+40 & 0x8fffffff` bits. The
forced mode also means object flags `0x20000000`/`0x40000000` alone do **not**
exempt an otherwise matching blocker; those bits can exempt it when the
same cell predicate runs with `+d4 == 0`. Gameplay names for these flags
remain unassigned. The wrapper leaves the copied mask in fine-system `+a4`;
it does not restore every query field.

`verify_wc3_pathing_separation.py` executes the complete original validator
and downstream footprint/cell routines without stubs in **1,184** terrain
cases: eight radii around all class boundaries, fractional/interior/edge/
negative positions and single blocked cells inside or outside each footprint.
Another **84** dynamic cases vary self exclusion, mask matching, seven
object-flag patterns and three prior `+d4` values. Assertions check the
result, restored object flags and mode; terrain cases also check mask
installation and restoration of the synthetic Windows exception chain.
This covers normal returns, not exception unwinding or concurrent queries.

On failure, assembly in `6f16ffa0` branches directly to its return. It does
not try an axis slide, shorten the vector, relocate the endpoint, or clear
the retained displacement. Its caller `6f1702f0` then continues neighbor
accumulation using that retained vector. Successful position application
through `6f05c820`, occupancy-map mutation and complete crowd trajectories
still require separate validation.

### Position application and spatial bounds

Assembly establishes the accepted-displacement chain:
`6f05c820` computes requested endpoint minus current resolved position and
passes that delta with notification flag 1 to `6f15f7b0`. That wrapper calls
`6f1603d0`, then virtual slot `+54` with a pointer to mover `+78` if the
notification flag is nonzero. The live control capture below identifies this callback as `6f16fa00`,
which dispatches spatial-region transitions; it is not by itself evidence
of the final gameplay-unit position write.

`6f1603d0` resolves elapsed movement through `6f161040`, folds the mover's
stored velocity `+80/+84` into its position `+78/+7c`, adds the supplied
delta, captures a new time origin with `6f161090`, then updates proximity
bounds (`6f1604d0`) and fine occupancy bounds (`6f160590`) in that order.
The [motion oracle](retail-pathfinding-movement.md#stored-velocity-integration-and-movement-clocks)
executes this chain for exact binary-fraction inputs; general numerical parity remains open.

The two bound constructors both dispatch to spatial-object update
`6f14e770`, but use distinct objects:

- Proximity: mover `+94`, whose map is object `+2c`. Form position ± radius,
  multiply by map `+68` scale, floor minima and floor maxima plus one.
- Fine occupancy: mover `+98`. Use the same four quantized footprint classes
  as endpoint validation; scale is one. If object `+37` bit 0 is clear,
  dispatch the empty rectangle `(-128000,-128000,-128000,-128000)` instead.

Both rectangles are stored **Y before X**: `(minY,minX,maxY,maxX)`, with
exclusive maxima. This resolves the candidate traversal axes. The separation
oracle executes **288** original bound-construction paths across eight radii,
six fractional/negative/boundary positions and three power-of-two scales,
plus the disabled-occupancy branch. It checks the exact rectangle and receiver
at entry to `6f14e770`; it stops before that function, without a replacement
stub. Thus it does not yet establish link allocation/removal behavior.

One deliberately awkward case demonstrates why plain host arithmetic is
insufficient: X=8.999, radius=0.499, scale=2 yields proximity minX **16**,
where IEEE float32 operations rounded to nearest yield **17**. Retail uses
integer software-float helpers (`6f06fa90` subtraction, `6f06fbb0` addition),
with signed doubled significands, arithmetic alignment and truncating
normalization. The oracle's bounded finite-input reference reproduces this
behavior. Its power-of-two scales avoid assuming a general multiplication
model. This is a concrete occupied-cell difference, not merely a small
trajectory error; complete numerical parity remains open.

### Spatial mutation uses lazy cell records

`6f14e770` exits without mutations for an unchanged rectangle. For disjoint
old/new rectangles it emits removal records for the old region and insertion
records for the new. For overlap, it computes old-minus-intersection and
new-minus-intersection through `6f1d4ae0`. The helper outputs at most four
strips (low Y, high Y, low X, high X); the caller processes these in reverse
order. Cells in the intersection keep their existing chains. `6f14d960`
clips each strip to map bounds and visits Y rows then X columns.

Crucially, `6f14d9e0` does **not** physically remove an old entry. Both kinds
of operation prepend a new eight-byte record `{kind|previousHead, object}`.
Kind high byte **0** means removal; **1** means insertion. The cell's terrain
high byte is preserved while its low 24 bits become the new link index.
A removal also marks its cell in the bitmap at map `+98`.

The writer takes a link from map `+ac` free-list head if available; otherwise
it appends to the link vector (data `+78`, count `+88`). Each emitted record
increments map `+b0` and object `+3c`. These count outstanding records, not
necessarily active occupied cells. Allocation growth/failure remains unverified; reclamation is covered below.

This explains the reader's two stamp scopes. A type-0 head stamps the object
for that cell, suppressing older insertion records for the same object
further down its chain. An effective type-1 record can admit the candidate
and stamp it for the entire query. Thus the first effective record wins per
object per cell, while candidate deduplication spans the query. New entrants
appear ahead of existing occupants within a cell; movement that preserves
that cell's occupancy does not reorder its links.

`verify_wc3_pathing_spatial.py` executes **1,280** complete original rectangle
updates across 32 seeded three-object sequences, including unchanged,
overlapping, disjoint, empty and map-clipped bounds. Sixteen sequences start
with a free list and sixteen with an empty preallocated link vector. It
checks every exact cell chain, preserved terrain bits, dirty bitmap, vector
count, per-map/per-object record counts and saved bounds. The corpus emits
**2,519 insertion** and **2,298 removal** records. After each update, the
complete original separation rectangle/cell/filter/append chain performs a
query against the mutated map: all **1,280 update-time queries** return exactly the
objects whose current footprints intersect the queried cells, without
duplicates. This composes real writers with real readers; no routine is
stubbed. The reclamation extension below adds 160 queries. Heap growth, destruction
and live crowded maps remain outside this corpus.

Oracle: `verify_wc3_pathing_spatial.py` → `spatial-oracle.json`.

### Spatial record reclamation

`6f14e050` cleans one cell in place. It increments the map stamp, walks
newest-first and retains only the first effective insertion for each live
object. Removal records, older records suppressed by that cell's stamp,
dead-object records and type-2 metadata records are unlinked. Surviving
links retain their relative order. Each reclaimed slot is pushed onto map
`+ac` free list; map `+b0` and, for object-bearing records, object `+3c`
decrement. Link-vector count `+88` does not shrink.

The object helper `6f14dab0` returns true for live objects. For an object
whose stamp is `0xffffffff`, it invokes virtual slot `+10` when the low
24 bits of its remaining-record count reach zero. That destruction path
is static evidence only and is not exercised by the live-object corpus.

Two complete cleanup drivers are now verified:

- `6f14df20` scans bitmap words (`+98`, count `+a8`), cleans set-bit cells
  in ascending index order, then clears each processed bitmap word.
- `6f14dfc0` scans every cell using map cell count `+38`. It **does not
  clear the dirty bitmap**, even after reclaiming all obsolete records.

Both statically reset map stamp `+b4` to zero when its unsigned value exceeds
`0x7fffffff`, before scanning. General wrap/stale-stamp recovery is not yet
validated. A third driver `6f14e180` samples work and caps its request at
256; its sampling algorithm and actual runtime scheduling remain open.
Do not infer that either verified complete driver runs every simulation tick.

The spatial oracle now alternates **96 dirty-bitmap cleanups** and **64
full-map sweeps**, interleaved with the 1,280 rectangle updates. It checks
exact retained chains, live record counts, unchanged vector high-water
count, dirty-bitmap behavior and an acyclic free list with exactly the
reclaimed capacity. Subsequent updates exercise actual reuse of reclaimed
slots. The corpus reclaims **4,596 records**. Each cleanup is followed by
the original separation query; its complete candidate list must equal the
pre-cleanup list **in the same order**, giving **1,440 queries** overall.
The implementation therefore explains both deferred removal semantics and
why cleanup need not perturb separation summation order in these cases.
Metadata/dead-object lifetimes, heap growth and live timing remain separate
gaps.

### Live separation configuration and region callback

`runtime/separation-control.jsonl` is a completed **100-second** observer
capture of the existing `Maps\PathingRE-Control.w3m` scene, with Space sent
at 50 seconds. It ends normally with no Frida errors. This scene does not
emit the newer 300-tick `PATHTRACE` fixture markers; it is a bounded runtime
witness, not a completed synthetic crowd experiment.

The trace records **5,504 separation-update calls**, seven initial/changed
configuration observations and **996 dirty-cell cleanup calls**. The first
16 bounded cleanup rows alternate maps `0x0f6100a0` and `0x0f610168`, all
returning to RVA `0x543d5`. Full-map and sampled cleanup hooks record no
calls during this capture. This confirms dirty cleanup is used in ordinary
scene execution; the precise outer scheduling contract remains open.

Six observed separation configurations have selector/category/rank `0/0/0`
and radius 0.25. Another has `0/240/0` and radius 1. All observed mover
vtables are RVA `0xa9129c`, with slot `+54` pointing to RVA **`0x16fa00`**.
There are **no nonzero-vector separation observations**. Do not treat these
counts as evidence of successful crowd pushing or exact-overlap behavior.

Static tracing now connects packed-field construction to gameplay:
`6f693d50` → `6f05c9c0` → `6f1710e0`. The latter destroys an existing
separation object, conditionally allocates a replacement, sets its mover,
writes selector/category/rank and registers it. The gameplay wrapper obtains:

- Enabled: `6f66fc50`, a multi-condition unit-state predicate whose complete
  gameplay flag meanings remain unresolved.
- Selector: `6f695130(unit+30 rawcode)`, per-unit lookup record `+228`.
- Category: `6f695090(unit)`, `(virtualSlotECResult & 15) << 4` OR the low
  nibble of `6f6950c0(rawcode)` (lookup record `+22c`). Unit `+60` bit 0
  overrides the high nibble to 15. Do not yet label that nibble as owner.
- Rank: `6f6951a0(rawcode)`, lookup record `+230`.

Setters are `6f171040` (selector), `6f1710c0` (category) and `6f1711e0`
(rank). Their callers supply constrained values; the setters are not general
range-checked APIs. In particular the selector setter takes eight input bits
and shifts them by 16 while only clearing four destination bits, so invalid
high selector bits could also alter category bits. Authored source-field names are now recovered below; live nonzero
selector/rank cases remain to be exercised.

The observed slot-54 callback `6f16fa00` floors the new fine position and
compares it with mover `+d0/+d4`. With a path object present and a changed
cell, it obtains a scratch vector through `6f15cf30` (owner pool limited to
eight nested scratch vectors), collects old/new fine-cell region objects
through `6f14cdf0`, cancels identities present in both lists, then dispatches
leave/enter messages to region payload virtual slot `+20`. The message tags
are `0x6370266c` and `0x63702665`. It releases the scratch vector and updates
the remembered cell. Collector eligibility includes spatial object `+40`
bit `0x10000000`, distinct from ordinary collision-object filtering.
These callback internals are static evidence; event delivery and nesting-limit
behavior still require focused execution tests.

### Authored repulsion fields and paired crowd experiments

The runtime lookup offsets are now connected to literal SLK field names,
not merely inferred from observed configurations. Loader `6f6b0870` reads
`repulse`, `repulseParam`, `repulseGroup`, `repulsePrio` into source descriptors
`+3a4/+3b0/+3bc/+3c8`. Accessors `6f6a7c70/6f6b4de0/6f6b4dc0/6f6b4e00`
feed `6f66bf40`, whose assembly at `6f66ca50..6f66ca88` stores the results
into runtime lookup fields **`+224/+228/+22c/+230`**. These are respectively
enable, configuration-row selector, low category nibble and minimum rank.
The category's high nibble comes from the authentic CUnit owner getter;
Unit+60 bit1 forces15. The gameplay producer of that override remains open.

Extraction from this installation's `War3Patch.mpq` member
`Units/UnitBalance.slk` gives:

| Rawcode | repulse | repulseParam | repulseGroup | repulsePrio |
| --- | --- | --- | --- | --- |
| hfoo (Footman) | 0 | 0 | 0 | 0 |
| hgry (Gryphon Rider) | 1 | 0 | 0 | 0 |
| hbot | 1 | 1 | 1 | 0 |
| hbsh | 1 | 2 | 1 | 0 |

There are 75 explicitly enabled rows in that extracted table. These include
ship rows as well as flyers, so “air-only” would be an incorrect general
classification. The extracted SLK, selected rows and loader assembly remain
in the external analysis directory (`UnitBalance-retail.slk`,
`repulse-enabled-rows.json`, `6f6b0870-unit-loader-asm.json`,
`6f66bf40-unitdata-asm.json`).

Scenarios **17 `crowd`** and **18 `crowd_air`** use nine same-owner units on
an 80-world-unit-spaced 3×3 grid around (-1936,-976). At tick 10 all receive
individual Move orders to (-1936,-144). The primary unit retains ordinary
`PATHTRACE` records; `PATHCROWD` records all nine positions/orders every
0.1 seconds for 300 ticks. Only `war3map.j` changes in newly rebuilt copies
of the original Human02Interlude archive. These experiments use normal
`CreateUnit` placement; they do not force exact overlaps or issue a shared
formation/group command.

Both **100-second captures completed normally**, with all 300 primary ticks,
all **2,700 crowd samples**, eight accepted companion orders and the primary
accepted order. The analyzer validates fixture completeness, observer
completion and search counters; its crowd checks reject missing unit ticks
and rejected/duplicate orders.

| Witness | Footmen (`separation-crowd.jsonl`) | Gryphons (`separation-crowd-air.jsonl`) |
| --- | --- | --- |
| Separation update calls | 0 | 7,474 |
| Nonzero-vector update observations | 0 | 550, across all nine movers |
| Updates changing stored position | 0 in this subsystem | 525 |
| Dirty cleanup calls | 998 | 996 |
| Search calls | 18 adaptive, 56 fine | 18 adaptive, 9 fine |

Every observed Gryphon uses selector/category/rank `0/0/0`, footprint radius
0.25. Maximum resulting vector length is **0.5000000997 fine cells**,
consistent with row 0's 0.5 cap within software-float error. The first
eligible updates start with zero retained vectors, produce nonzero next
vectors and leave position unchanged; later visits apply retained vectors.
This is a live witness of the recovered delayed-application design. It is
not yet an exact per-neighbor replay: stored-position differences can also
include elapsed velocity integration, and candidate arrays/PRNG state are
not captured in this experiment.

All units eventually have order 0 in both fixtures. The dispersed endpoints
are observations, not proof that every ground unit reached the requested
goal. Ground-unit obstacle handling and crowd arrival require their own
movement-path reconstruction; this optional repulsion procedure cannot
stand in for them. Exact-overlap random draws, nonzero selector/group/rank,
blocked repulsion and mixed-owner filtering remain open live cases.

```sh
python3 tools/frida/analyze_pathfinding_trace.py \
  /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/separation-crowd-air.jsonl \
  --scenario crowd_air --require separation
```

The analyzer additionally rejects nonfinite/malformed separation vectors
and output lengths above the authored cap (tolerance 0.0001 for known rows
0–4). It does not claim a full arithmetic replay from those checks.

### Ground crowd stopping: retry exhaustion, not repulsion arrival

The completed Footman capture now has an end-to-end identity correlation
in `check_ground_crowd_arrivals`: the fixture's known initial 3×3 positions
identify each mover from its first arrival test; the accepted-arrival position
matches that unit's final JASS sample within 0.01 world units, with order 0.
The conversion is fixture-specific, `world=(fineX*32-7168,fineY*32-3072)`.

Only unit **7** reaches normal arrival: distance **14.056** world units from
the requested goal, within the **15.68** threshold (0.49 fine cells). The
other eight complete through the existing retry-exhaustion path. For each,
the trace contains `167290` returning 4 with retry count staying at 1,
`Path_Advance` returning 4 for that same path/counter, `16fd32` setting the
mover's force-arrival bit, and a subsequent successful `16e910` test carrying
that bit. Their final goal distances are:

| Fixture unit | Distance in world units | Completion |
| --- | ---: | --- |
| 0 | 416.029 | Retry exhaustion / forced arrival |
| 1 | 99.939 | Retry exhaustion / forced arrival |
| 2 | 84.124 | Retry exhaustion / forced arrival |
| 3 | 69.947 | Retry exhaustion / forced arrival |
| 4 (primary) | 76.059 | Retry exhaustion / forced arrival |
| 5 | 64.983 | Retry exhaustion / forced arrival |
| 6 | 99.057 | Retry exhaustion / forced arrival |
| 7 | 14.056 | Normal arrival |
| 8 | 65.370 | Retry exhaustion / forced arrival |

This explains the stopped orders in this ground crowd witness without
attributing them to a universal separation force or an enlarged ordinary
arrival radius. It does not prove disconnected terrain, optimal routing or
the specific blocker responsible for each failed attempt. Why unit 0 exhausts
far from the destination still requires linking fine occupancy failures and
route retries to the dynamic neighbors present at those instants.

The analyzer rejects missing/ambiguous mover identities, mismatched final
positions/orders and forced completions without a same-path/same-counter
retry-exhaustion chain. Synthetic tests additionally reject an otherwise
matching retry record with the wrong counter. The report is
`runtime/separation-crowd-analysis.json`, under `crowd.arrivals`.

### Fine-cell blocker observer

`trace_wc3_pathfinding.py --blockers` enables a bounded optional observer on
`6f1489a0`, the complete fine-cell predicate entry. (`6f148a10` is an internal
chain-loop address, not the predicate entry.) For failed tests inside a
fine-path request, it classifies out-of-bounds coordinates and terrain-mask
intersections directly. Otherwise it reconstructs the first effective
blocking object from the lazy chain, respecting removal records, dead stamps,
occupancy flags, query mode and mask. The recorded mover position is a stored
position snapshot, not a separately resolved instantaneous position.

Per-request route records contain failure counts and at most 32 distinct
objects with identities, first-observed position, masks and flags. Excess
object hits are counted explicitly. An unclassified failure increments
`unclassifiedHits`, which the analyzer rejects; no successful geometry verdict
is inferred from an unexplained failure. Object identity is reconstructed
from the observed failed predicate and memory state, not an extra retail call.
This instrumentation does not itself establish that a particular blocker
caused the whole search to exhaust its budget.

The first attempt, `runtime/ground-crowd-blockers.jsonl`, used internal-branch
hooks and ended with `trace-failed: script has been destroyed`, around fixture
tick 10. It has no successful observer completion and is excluded from
behavioral evidence. The replacement uses function entry/return hooks. The
cause of the first process/observer loss has not been established.

The replacement `runtime/ground-crowd-blockers-entry.jsonl` completes normally
with all 300 primary ticks and 2,700 crowd samples, and reproduces the earlier
nine final positions and eight retry-exhaustion completions. Across **113
fine requests** (including wrappers that do not run a search), the observer
classifies **16,473 terrain** and **2,423 object** rejections, zero bounds
rejections, zero unclassified failures and zero omitted object hits. All nine
fixture mover identities appear as blockers somewhere in the run.

Each exhausted mover's last actual fine search returns -1 after **701 pops
against budget 700**. The last-search rejection counts are:

| Fixture unit | Request | Object rejections | Terrain rejections |
| --- | ---: | ---: | ---: |
| 0 | 131 | 0 | 344 |
| 1 | 80 | 66 | 454 |
| 2 | 69 | 64 | 492 |
| 3 | 51 | 72 | 486 |
| 4 | 41 | 49 | 457 |
| 5 | 68 | 70 | 490 |
| 6 | 78 | 60 | 466 |
| 8 | 53 | 88 | 504 |

For example, primary unit 4's terminal request rejects cells belonging to
unit 7 (28 tests) and unit 3 (21 tests). Unit 0, which stops 416 world units
away, sees **no object rejection at all in its terminal search**: that last
attempt runs into terrain constraints and exhausts its budget. Earlier
interactions can still affect how it reached that position. This evidence
therefore separates actual local blockers from a blanket “crowd blocked it”
explanation, without claiming that terrain is globally disconnected or that
one rejected cell alone caused the overall failure.

The terminal request for unit 0 reconstructs only its current position,
`(151.1405182,87.4667969)` in fine coordinates, and its wrapper returns 1
with a one-point route despite the underlying search returning -1. Retry
exhaustion then supplies force-arrival. This is another concrete reason not
to equate successful route-wrapper return with arrival at the requested goal.
The analyzer preserves the last fine-search evidence under
`crowd.arrivals[].terminal_search`; the complete capture passes all checks.

### Candidate traversal order and stamps

`6f170960` configures the query with mover `+40`, position `+44/+48`,
radius `+4c`, category `+50` and minimum rank `+52`. It derives the query
rectangle from position ± radius using the proximity map's scale `+68`.
`6f16f570` collects into the query's eight-byte-entry vector; `6f170c00`
clips the rectangle against map bounds `+54/+58/+5c/+60` and iterates
half-open ranges. The first rectangle coordinate is the outer loop, the
second the inner; cell index is `map[+3c] * outer + inner`. The bound-construction oracle below confirms rectangle order
`(minY,minX,maxY,maxX)`: the outer loop is Y, the inner loop X.

Within each cell, `6f170b30` follows the stored eight-byte link chain.
Link high byte 2 is metadata and is skipped. High byte 1 invokes the
candidate filter; accepted objects append `{object, 0}` through `6f05ecd0`.
The preallocated append path copies directly to the end. There is no
sorting by distance in this collection chain, so pair accumulation inherits
cell traversal and link ordering. The upstream link insertion/removal order
and its dependence on unit lifecycle remain open.

Map `+b4` provides stamps: rectangle traversal increments it once for a
query stamp; each nonempty cell increments it again for a cell stamp.
Object `+38` equal to the query stamp, current cell stamp or `0xffffffff`
is skipped. Type-1 objects receive the query stamp whether their filter
accepts or rejects them. Other non-metadata link types receive the cell
stamp. If query `+20` names a source object, its stamp is set to the query
stamp before traversal. Empty clipped rectangles do not increment the
counter or stamp the source.

The separation oracle executes the complete original rectangle traversal,
cell traversal, candidate filter and append routines for **1,024** seeded
synthetic cases, without stubs. Candidate storage is preallocated, so the
allocator path is outside this evidence. It checks exact output order,
zero second words, every object stamp and final map counter, using mixed
link types, repeated objects, rejected categories, excluded sources, dead
stamps, empty/clipped rectangles and counters near unsigned wrap. Wrap
coverage establishes these routines' raw behavior only; counter reset or
stamp repair elsewhere has not been recovered. Query bounds have separate oracle coverage; live crowd witnesses follow.
The complete query + arithmetic + application chain lacks exact per-neighbor replay.

### Candidate eligibility, cooldown and authored configuration

The callback `6f16e830` accepts a candidate only when all of these conditions
hold (offsets are hexadecimal):

- Candidate spatial object `+34` has no high-byte bits set.
- Its mover pointer at object `+30` differs from query `+40` (self exclusion).
- Mover `+10` is type tag `0x60706375`, and mover `+14` bit `0x80000000`
  is clear.
- Mover footprint `+90` is positive and mover scalar `+c0` equals zero.
- Mover separation pointer `+ac` is non-null.
- Separation packed word `+20` bits 20–27 equal query ushort `+50`.
- Separation bits 28–31 are at least query byte `+52`.

This establishes an eight-bit equality category and four-bit minimum-rank
filter. It does not identify their gameplay meanings or prove that the rank
is a player priority. The spatial object must contain a valid mover pointer;
this callback does not guard a null payload before reading its type.

`6f171320` tests the low ushort of separation `+20`. Zero returns 0 without
mutation; any nonzero value decrements that ushort and returns 1, preserving
the upper half-word. `6f1702f0` skips its update body on that nonzero return.
Idle helper `6f16ebd0` replaces the low ushort with **7**, preserving upper
bits. Thus it schedules seven skipped visits, followed by an eligible visit;
these are visits to the separation object, not seven simulation frames.
The owner additionally alternates its update-list parity, as described above.

`verify_wc3_pathing_separation.py` executes these complete original routines
without stubs: **65,808** candidate cases cover every category and rank/minimum
combination, mismatched categories and individual exclusion branches;
**768** cooldown cases cover low values 0–255 under three upper-half words.
The filter/cooldown cases do not execute proximity enumeration or movement
validation; the separate arithmetic-slice coverage is described above.

Initializer `6f004790` authors sixteen five-float rows at `6fd54398`, stride
`0x14`. Its five parser calls per row are stored in reverse call order. Static
extraction of the parser's decimal-string arguments gives:

| Row | +0 | +4 | +8 | +c | +10 |
| --- | --- | --- | --- | --- | --- |
| 0 | 5.0 | 0.01 | 0.5 | 0.4 | 0.7 |
| 1 | 7.0 | 0.01 | 0.2 | 0.4 | 0.7 |
| 2 | 8.0 | 0.01 | 0.2 | 0.4 | 0.7 |
| 3 | 9.0 | 0.01 | 0.2 | 0.4 | 0.7 |
| 4 | 10.0 | 0.01 | 0.2 | 0.4 | 0.7 |
| 5–15 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |

The selector in `6f1702f0` is packed-word bits **16–19**, distinct from the
candidate category and rank. These are authored decimal values. The original
static-only experiment could not resolve CRT `isdigit`; the new
`verify_wc3_pathing_repulsion.py` executes the complete initializer with the
pinned shipped CRT/default locale and records the exact words below. The older
string/assembly extraction remains provenance for that earlier experiment.
Full crowd parity across configurations and runtime overrides remains open.

Oracle: `verify_wc3_pathing_separation.py` → `separation-oracle.json`.

The soft-float helper ABI obscures operands in the current decompilation;
recover the arithmetic from assembly and correlate object identity before
assigning radius, strength, normalization, or final displacement meanings.

## Exact scalar arithmetic and occupied-cell boundaries

`verify_wc3_pathing_numeric.py` → `numeric-oracle.json`: **200,330 complete
original-helper calls**, independent integer-bit models, no tolerance:

| Helper | VA | Calls | Recovered boundary behavior |
| --- | --- | ---: | --- |
| Add/subtract | `6f06fbb0` / `6f06fa90` | 21,772 each | Exponent-zero shortcuts; gap ≥23 discards smaller operand; signed doubled-significand alignment and truncating normalization; 32-bit exponent wrap |
| Multiply | `6f06f9c0` | 21,772 | Truncated product; pre-normalization exponent guard 1..256; separate zero-fraction shortcut. Guard can flush a product normalization would rescue |
| Floor | `6f070c80` | 20,422 | Negative nonzero magnitude <1 → −1; ±0 → +0; raw exponent ≥150 unchanged |
| Integer conversion | `6f070120` | 20,222 | Ordinary truncation toward zero; extreme inputs use modulo-32 shifts/output wrap, not saturation |
| Integer square root | `6f071530` | 21,554 | Exact integer reference, perfect-square neighbors and random words |
| Software square root | `6f071480` | 25,542 | Replicated-significand integer sqrt, `b504/b505` coefficients, exponent-parity scaling and retail multiply; negative/zero → +0 |
| Reciprocal | `6f0711e0` | 25,542 | Integer interpolation over 1,025 embedded words at `6fa810c0`; table hash recorded |
| Divide | `6f06fcd0` | 21,732 | Identical input words → 1; otherwise reciprocal then multiply |

Binary helpers: ECX output pointer, EDX left pointer, stack right pointer;
floor: ECX output/EDX input; integer conversion: ECX input/EAX result.
Tests cover targeted boundaries, cancellation, signed zeros, deterministic raw
words (seed `0x12717085`), output/input aliasing, input/guard preservation,
callee-saved registers, stack balance and return values. Constants −1/0/+1 are
seeded explicitly; no instructions or callbacks are replaced.

**864 complete bound-construction prefixes** through `6f1604d0` stop at actual
spatial mutation `6f14e770`, checking exact rectangles, including adjacent float
values. The `8.999−.499`, scale 2 witness retains retail minX16 versus host
nearest-float32 minX17. This extends the earlier bounded add model; it does not
execute spatial mutation or prove full trajectory parity. Raw NaN/infinity/
denormal/overflow cases establish helper behavior, not producer reachability.
**2,130 complete `6f168280` vector normalizations** compare exact length and
XY outputs against the integer models. Branch witness: `(3f800001,00000000)`
computes length `3f800000` (1), so the `length > 1` branch leaves X slightly
above 1 unchanged. Host correctly rounded arithmetic is not a substitute.
Reciprocal constants come from the binary; interpolation is independently
modeled, but table-generation derivation remains open. Trigonometry, producer
domains and general composed trajectories remain open.

### Engine port inputs recovered on2026-10-01

`verify_wc3_pathing_repulsion.py` now executes the complete004790 initializer
using the shipped CRT `isdigit` and the default C-locale branch, with no
import stubs. Row0 words are `[40a00000,3c23d70b,3f000000,3eccccce,3f333334]`;
row1 radius is40e00001 and rows1–4 cap is3e4cccce. Rows5–15 are zero.
These differ from host float literals. The earlier authored-string extraction
and approximate kernel oracle must not supply production coefficient words.

The category producer695090 uses CUnit vtable6fb77eb0 slotEC, which points to
685da0: a plain return of Unit+58, its player index. Its high nibble is therefore
`owner &15`; Unit+60 bit1 overrides that nibble to15, and the authored
`repulseGroup &15` supplies the low nibble. The full producer with the actual
CUnit vtable and supplied authentic rawcode cache matches1,024 owner/override/
group cases. Do not reuse the earlier unproven movement-type interpretation.
The semantic producer of Unit+60 bit1 remains open.

15fc70 unconditionally rescales the vector using reciprocal(square_root(sum))
and the requested length; the strict cap/tiny guards belong to its callers.
Its old descriptive name `Math_ClampVectorLength` did not prove it contained
those guards. Repulsion needs the unconditional operation, including near
zero inputs, rather than `wc3_velocity_cap`'s conditional policy.

The same vtableEC identity means6803f0's decompiled apparent three-argument
call is misleading: its owner getter takes no stack args. Those pushed
arguments belong to subsequent order construction/admission. The observed
public Stop ordering and engine retirement remain verified independently.

The new fixture stores600 ordered pair slices (including exact/nearly-exact
and threshold overlaps),105 complete arithmetic tails,16 settings rows and
1,024 category results. These kernels exclude proximity enumeration and
application/scheduling; engine scheduler coverage must accompany the port.


## Engine active cell history

Payoff81 ports the ordinary fine producer's effective cell order alongside the
public overlapping target/blocker journey. `wc3_pathing_spatial.h` preserves
ranks on old/new rectangle intersections, removes departed membership and
prepends entered membership. `verify_wc3_pathing_spatial.py --engine-library`
compares all81,920 active cell orders with original14e770 output across1,280
updates; original14e050 dirty/full compaction preserves this order. The engine
stores active membership rather than native obsolete records and free-list
indexes. Allocator/reference/stamp representation remains MAP-05/06.

The C probe is `wc3_spatial_engine_probe.c`, also included by the standard engine
probe. The accepted fresh corpus variant is `oracle-spatial-engine`. Public
SetUnitX leave/reentry changes ordering while overlap-preserving movement keeps
retained cells unchanged. Actual engine frames and eight saved continuations
match501 native mover commits and1,999 subsequent commits. See
[engine history](retail-pathfinding-engine.md#overlapping-targets-retain-fine-cell-insertion-history)
and [corpus](retail-pathfinding-corpus.md#overlapping-target-producer-and-engine-history).
