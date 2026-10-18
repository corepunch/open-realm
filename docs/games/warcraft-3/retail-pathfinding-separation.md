# Retail pathfinding evidence: Separation and spatial maintenance

[Contract, current ledger and reproduction](retail-pathfinding.md).
Retail **1.27.1.7085** only; addresses and report paths use the conventions there.

[Robo-Goblin passive availability](robo-goblin-passive-availability.md) documents the
Morph375 owner lifecycle, failing-first cases and strict native projection.

[Configured common Morph availability](common-morph-passive-availability.md) documents the
Morph374 owner lifecycle, failing-first cases and strict native projection.

[Submerge passive availability](submerge-passive-availability.md) documents the
Morph372 owner lifecycle, failing-first cases and strict native projection.

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

## Mixed blocked crowd and disabled ground controls (Payoff144)

The complete engine composition now matches the SEP-04.2 mixed-owner/radius/
rank/selector wall scene and SEP-04.3 enabled/disabled ground scene:3,591 owner
visits,4,410 ordered contributions and142 retry decisions. Exact comparisons
include endpoint admission and the shared owner RNG across routing and
separation. Public positions/orders of repulse-disabled units are checked too;
they retain collision and path retry behavior. Save/load mid-order retains the
same full continuation in both scenes. Individual point orders now execute
through newest-first physical owners using the unit-owned route, retries use
the retained adjusted coarse endpoint, and arrival turning avoids a fresh
path/retry. See [engine contracts and portable reproduction](retail-pathfinding-engine.md#mixed-crowds-individual-physical-owners-and-adjusted-retries-payoff144).

Do not classify the first32–96 world units of blocked-start relocation as
repulsion: disabled controls show it too. Do not claim a native first-blocker
identity from reconstructed overlap causes, or completion for the four mixed
orders unfinished at the captured tick302 boundary.

## Primary construction work integration

Payoff226 integrates primary Human construction's separation work phase,
ordinary Repair's distinct eligibility and the shared inverse across pause,
Stop, replacement Move, target retirement and cold saves. Repeated original
captures freeze24 policy phases; broader counted suppression keeps SEP-01.2
open. See [primary construction work](retail-pathfinding-engine.md#primary-human-construction-owns-separation-suppression-payoff226).

## Inside construction contribution owners

Payoff227 integrates counted inside-construction absence/work ownership,
scripted pause independence, and distinct KillUnit/completion versus RemoveUnit
inverses. Two complete native repeats and an unhooked control freeze72 policy,
pause/hidden and depth phases; Classic/TFT engine save/owner/order regressions
pass. Remaining counted producers keep SEP-01.2 open. See
[inside construction](retail-pathfinding-engine.md#inside-construction-owns-absence-and-work-separately-payoff227).
# Remaining counted producers: closure audit323

SEP-01.2 remains open. Work in `/tmp/wc3-separation323` adds public retail
probes and initially failing engine regressions. The working Avatar implementation
and checks are described below; no TODO closure is claimed by this audit. Raw captures and source snapshots are in `/tmp/separation323`.
The accepted binary is still `d51e5680…d8236`.

The direct `688d90` acquire inventory has ten sites. Construction, pause and
removal already have integrated owners. The remaining sites reduce to the
following concrete lifecycles, all witnessed by `Separation323f.w3m`:

| Owner | Acquire return | Release return | Public producer |
| --- | --- | --- | --- |
| Avatar transition | `4eed28` | `4cde66` | Alias of AHav, entry and buff expiry |
| Morph transition | `569e78` | `551ffc` | Stock Bear and Raven, forward and reverse |
| Channel optional disable | `4eec17` | `4cc7cd` | ANcl DataE/`Ncl5` enabled; disabled variant is a control |
| Item teleport | `4361eb` | `40bf69` | Hero uses Town Portal on an allied Town Hall |
| Entangle cargo | `562066` | `56110f` | Wisp harvests an entangled mine; mine death returns it |
| Internal AI scope | `9bd6ea` | three static exits in `9bd6e0` | Campaign AI, configured Captain home and UnitApplyTimedLife |

The AI scene requires `MAP_CONTROL_COMPUTER`; the neutral-controller variants
complete but do not start the intended private AI producer. A direct public
order851974 is rejected, and ordinary player Move/Attack completion does not
establish this AI scope. The first valid configured scene records15 scope
pairs. The observer reports Frida's tail-transfer return address for the release;
the original three exit sites are independently enumerated in Ghidra.

The first teleport attempt used a Footman and was rejected. The valid producer
uses Hpal, stock stwp and UnitUseItemTarget. `unloadall` on an entangled mine did
not return the Wisp; killing the mine does. Keep these rejected/incomplete
attempts separate from the successful producer evidence.

Avatar's transition request is one second, independently of its two-second
authored buff duration. Native `0608d0` receives its delay pointer in stack
argument0 and event in argument1; ECX is the event-control object. Treating EDX
as the event silently misses every request. Bear's captured delay is1.45;
Raven forward0.6 and reverse1.6. Raw scalar clocks and delay words are retained.

The separate Avatar lifecycle probe establishes why a simple boolean hold is
insufficient: an early buff inverse acquires again and replaces the pending
transition request. Only the replacement request releases, leaving depth1.
Pause independently adds a count; the transition timer still fires while paused.
Stop does not cancel it. Removal during the hold retires the unit with the
ability contribution still counted. These observations require independent
timer ownership, exact replacement semantics, and persistence regressions.

Removing the Avatar ability during its transition terminated the exploratory
observed process. The reordered probe flushed356 markers and observed an
access violation at RVA161043 (read address74), reached through4ed8b3. It lacks
the completion marker. The corrected Avatar323c observer-free control also exits after the identical356
public markers, ending at tick357/scene7, with no completion marker. This confirms
the terminal producer outside instrumentation; only the observed process supplies
the fault address. It is not a passing full-map capture. The first control wrote
a start preload before loading completed and stalled; that run is excluded.

`wc3_avatar.separation323_public_cast_owns_suppression` failed four of
eight assertions in both Classic and TFT before the implementation below: actual public casting never acquires the count and separation
remains enabled after both casting and refresh. Setup/admission/buff assertions
passed. The working implementation below covers the counted owner, timer,
replacement, inverse and persistence. The other families and strict replay
remain required for closure.

Ghidra saves and readback name `Avatar_Begin/EndSeparationTransition`,
`Morph_Begin/EndSeparationTransition`, and `Unit_DispatchAIStopScope`.
`MapPathfinding.java` carries the same notes. Shared morph RTTI identifies the
broader family but is not dynamic proof for every subclass.


Audit323 repeat/control result: Separation323f observe8/observe9/control3 match675 public markers. The observed runs match1143 rows after dropping only event sequence and allocation identity pairs; all clock words, flags, counters, configuration arguments and timer delays remain compared. These are evidence checks, not an accepted engine corpus entry. The working engine changes below are tested separately; the full engine replay has not been certified.

Morph timing in the same capture: Bear retains its source rawcode through acquisition and changes at completion in both directions. Raven forward is already edtm at acquisition; reverse retains edtm until completion changes to edot. Current engine immediate reverse rebind must be reconciled with this evidence, not hidden by a counter-only fix.

## Working Avatar implementation in audit323

The Avatar pool now retains an independent primary-clock transition request after
BHav bonuses expire. Every begin acquires a count; replacing an existing request
cancels only that request. Its eventual callback releases once, refreshes separation,
and publishes target availability. This preserves the captured early-inverse depth1.
Death removes the bonuses without starting an inverse transition; the entry timer
still completes. Unit retirement cancels the request without balancing its old hold.
Stop and Pause do not cancel the ability request; Pause owns its separate count.

Save192 persists the request deadline, epoch, span, sequence and active bit. The
indexed Avatar queue rebuilds from the sparse pool and merges with the existing
primary scheduler. The root checkout remains Save191 until this working change is
integrated. No client or network contract changed.

Before implementation, public cast fails4/8 assertions and early-inverse/save fails
5/10. After implementation, Classic and TFT each pass12 Avatar tests/114 assertions,
including pause, Stop, death, retirement, epoch rebasing and freed-slot reuse. The
five affected suites (Avatar, repulsion policy, ability lifecycle, spell, save) pass
813 tests/84707 assertions per edition. Production and test binaries build. This is
not a full repository sweep or a completed native-row engine replay.

Separation323g authors integer urpo2/urpp17/urpg17/urpr17 on all participating unit
forms. This closes an evidence gap in the stock-unit probe, whose ground-unit
repulsion is usually disabled. Enabled observe1/observe3 and control1 match675
public markers; both observed streams match1143 normalized native rows, including
actual enabled/disabled configuration calls for all eight unit types. Observe2 was
terminated at its wall-time limit at tick703 and is excluded. Source maps, captures,
failed attempts and reports remain in the audit323 working archive.

Remaining after the Avatar and cargo work: implement Morph, Channel, ItemTeleport
and the AI scope, complete
interruption/overlap/save coverage, certify exact engine replay against the frozen
native rows, and add the strict corpus verifier and rejection tests. No new TODO
has been split off.


## Working Entangle Cargo implementation in audit323

Native `562030` acquires only for internal task `d0182`; `5610e0` releases a
positive count before popping/dispatching that task and the general cargo exit.
Separation323g observe1/observe3 witness the public Wisp Harvest acquisition at
tick551 and mine KillUnit inverse at tick579, with depth0→1→0 and enabled
configuration restored. The following immediate AI scope is a separate owner.
Ghidra and MapPathfinding name these `EntangleCargo_AcquirePassenger` and
`EntangleCargo_ReleasePassenger`.

The working engine retains a separation owner per passenger in `cargo_t`,
acquired only by Aenc and moved with that passenger during slot compaction.
Unload/removal uses the retained owner, including after a holder type change or
save/load, rather than looking up the current holder's abilities. The raw pool
record carries these booleans under the already-working Save192 contract.
The engine cargo pause flag ends before the release/configuration callback;
placement and visibility restoration follow. Ordinary cargo does not acquire
this Entangle Cargo contribution.

The public Harvest/save/mine-death regression failed10 of32 assertions before
the fix and passes32/32 in Classic and TFT. It also verifies separation remains
disabled across refresh and the survivor accepts Move after the inverse.
The removal and ordinary-cargo regression also passes: together these are two
tests and48 assertions in each edition. Broader validation is recorded below. This is
working implementation evidence, not a SEP-01.2 closure certificate.

Morph timing-field mapping is now verified in the native AbilityData loader:
6a74b0 loads rank scalar+8 from `Dur` (key6fce6170→6fb7b250) and scalar+28
from `DataC` (key6fce6188→6fb7b278), rank stride68. Getters414ff0/414c10 return
those fields. Morph569e20 computes softfloat `(Dur + ability9c) + extra`, where
extra is DataC only with flag40000. Stock Raven's0.6 forward and1.6 reverse
therefore use authored timing. Animation duration must not substitute for this
request. Bear's observed delay is1.45 in both directions.


Audit323 aggregate validation: the full `make test` run exposed two older
Captain save fixtures that created logical owners on the stack without a bot
registry entry. The fixtures now use registered Captains and owned rosters;
production save validation remains unchanged. Their focused regressions pass
64 and103 assertions in each edition, and both complete affected eight-way
shards pass472 tests/3,319,763 assertions each. Together with the other fourteen
completed shards and standalone reports, the cumulative JUnit result is8,840
tests/37,541,817 assertions, zero failures. The Python component passed2,089
tests but initially rejected stale source pins before its corpus class ran.
Refreshing only the nine changed mapper/engine/test source pins lets that
class pass all37 tests, for2,126 Python tests in total. Frozen retail numeric
fixtures and all corpus entry contracts remain unchanged. The original umbrella
exit remains nonzero in the log; passing reruns are recorded separately rather
than relabeling it as a clean first pass.

### Channel counted admission and follow-through (audit325)

Channel's `DataE` owns two contributions after the public CHANNEL callback and
before cast windup. `6f4eebb0` subtracts its own disable/order contribution,
adds a unit-wide contribution to attached abilities, then increments its own
order contribution and Unit198. Its own execution stays enabled while new
orders are disabled. `6f4cc780` reverses retained ownership, releases Unit198,
and ends channel work. The inverse uses the retained bit, not current DataE.
`6f4eec40` begins work at EFFECT and uses `DataA` for follow-through. DataD is
its repeating presentation interval, not the cast duration.

The game module uses the ordered spell scheduler for windup/follow-through.
Concrete ability availability records retain and save exclusive ownership;
scripted pause has its own counted contribution. `A_CAST_BEGIN`/`A_CAST_END`
connect the concrete Channel owner to that scheduler. Availability callbacks
cancel only the matching active cast, preserving a sibling's outer callback
continuation. A per-unit suppression code cannot represent this nesting.

DataE0 permits Stop/Move/recast interruption. DataE1 rejects new Move/recast
orders, but Stop is accepted and retained while the cast's deadline continues.
A short pause/unpause also preserves the cast. If pause lasts past completion,
Channel releases its hold, then physical pause acquires its own hold. This is
shared positive-Unit198 order retention, not a special Channel order exception.
Target Move now uses the same counted availability validator as point Move;
it previously bypassed this gate.

`6f437310` (Channel v22c) intercepts its own public order before channel work
while ability20 bit1000 is set. A same-order request from CHANNEL is accepted
without a second CHANNEL callback or acquisition. A different Channel alias
begins a nested cast; the outer continuation's disable walk cancels that nested
owner before acquiring the outer separation hold. Stop from CHANNEL cancels
the active cast, but the original begin stack still acquires afterward; its
inactive finish does not release that hold. Preserve this native retained count.

`6f4da0d0` fixes the order from DataF1 even at higher ranks, defaulting to
channel852600 for absent/unregistered names. DataB is rank-based and selects
none/unit/point/unit-or-point. SLK/W3A strings retain their text in addition to
numeric/FourCC views. `6f487cb0` registers337 literal name/ID pairs: all86
existing engine overlaps matched, and `g_order_names.inc` supplies the251
missing names without inventing command owners. Tests use literal
thunderclap852096/channel852600/berserk852100, including a W3A Ncl6 override.

Read-only repeats and observer-free controls are archived under
`research/SEP-01.2/channel325-working`. Repeats remove only the sequence field
and retain clock words, identities and states; controller/module records are
outside the comparison.

| Probe | Exact repeated native/marker rows | Exact control markers |
| --- | ---: | ---: |
| DataE0/1 lifecycle: completion, Stop/Move, pause, death/removal, recast | 1596 | 922 |
| Pause retained past completion | 1607 | 922 |
| CHANNEL/CAST/EFFECT/FINISH callbacks: Stop/same-order | 664 | 378 |
| Same callbacks with a different Channel alias | 673 | 378 |

The first no-hook attempt stalled at the initial preload marker and is excluded.
The capture controller now waits for a positive probe tick before treating a
preload file as evidence that loading continued. Failed attempts, maps, exact
source versions, hashes and native order extraction are retained.

Failing-first regressions exposed missing windup ownership (6 failures), order
admission (3), retained Stop/pause (8), alias target/order profiles (12), two
nested-callback assertion aborts, and target-Move admission (7). The final four
Channel tests pass200 assertions in each of Classic and TFT, covering cold
save/load, authored strings, callbacks, count inverses and death/removal.
The full run passed all2126 Python tests. Its24 C JUnit reports contain8224
tests and37,517,700 assertions, with one failing test in each edition: the older
Attack306 fixture directly invoked private Attack think immediately after
unpause, bypassing the production scheduler's physical-pause guard. That call
discarded the pending inverse; the new counted Stop gate exposed the retained
hold. The fixture now checks production `monster_think` stays gated, advances
the release timer, then verifies chase restart and Stop cleanup. The corrected Attack306 case passes300 assertions in each edition; Channel
passes200 and the34 pause tests pass7345, also in each edition. Current-source pins include the
new order-name include and Channel source/test; frozen retail numeric fixtures
are unchanged.

SEP-01.2 remains open for Morph, Item Teleport, AI and final combined parity
replay. These implementation checks are not a closure certificate.


### Morph interruption evidence (audit326, implementation pending)

`Morph326.w3m` covers Bear/Raven, forward and preplaced-alternate reverse, with
natural completion, Stop, pause past completion and death before completion.
Two read-only runs agree in1213 native/marker rows after removing only sequence;
the no-hook control agrees in all658 public markers. Evidence and exact source
snapshots are archived under `research/SEP-01.2/morph326-working`.

Stop is accepted while the morph order and timer remain current. Public pause
is visible immediately, but physical pause acquires its hold after the morph
inverse and type rebind. Death before the deadline retains the morph count and
source type; removal adds and releases its own separate contribution. A
preplaced alternate form's construction takes the immediate/dead-flag branch
without acquiring this timed hold.

Stock War3x AbilityData has Abrf Dur1=1.45, DataC1=0; Arav/Amrf Dur1=0.6,
DataC1=1. These explain the captured1.45/0.6/1.6 requests. Do not add unit cast
point: native ability9c is a separate duration extension. `407740`/`40bba0`
only increment/decrement ability44 and refresh on first/last transitions;
they are not Channel's all-owner exclusive availability walk. The working
Bear/Raven implementation below covers the timed owner; other Morph subclasses
are not claimed by these captures.

The working engine now has a sparse, saved Morph primary-timer owner for
Bear/Raven transitions. The regression uses non-stock Dur=.375 and landing
DataC=.625 with both RoC and TFT field names, both directions, retained
Stop/Move, pending pause, death and cold save/load. Its first lifecycle run
failed16 of912 assertions, all from Move replacing the active transition;
point admission now uses the shared counted-execution hold. All912 lifecycle
assertions then passed. These working results do not close SEP-01.2.

Morph326remove adds16 natural/ability-removal/removal+Move/unit-removal scenes.
Two complete observed streams match1171 rows, dropping only seq; the no-hook
control matches658 public markers. `540e30` cancels the requests, invokes
`551f70` for the active transition, then reverses immediately through the80000
branch to restore the base type. This balances the active hold and admits a
subsequent Move immediately. Unit retirement follows the separate retained-count
path. The public removal regression failed34/1072 assertions before the fix.
The new removal branch stores the original base type with the saved transition
and performs the inverse through the concrete owner. Ghidra and the mapper retain
this distinction. Evidence lives in `morph326-working/removal`.

After the ability-removal fix, the full wc3_unit suite passes137 tests/13906
assertions in each edition. Existing Raven fixtures now use an actual flight
endpoint and advance the primary timer before checking completion or reversing;
an active same-form recast is rejected. The two ascent tests check that Move
waits for the morph timer and that ascent survives the subsequent Move. A bare
animation-end callback cannot complete the ability timer. Death's existing
unit_die path clears AI_HOLD_FRAME before installing its death move. The saved
Morph owner includes its base type so ability removal can restore it after cold
load. Broader affected-suite verification and other counted producers remain.

The target-order extension first failed18/1229 assertions: target Move replaced
the transition, and removing an inactive base-form ability canceled Move. Target
admission now retains its validated entity/point packet under the same counted
hold as point orders. Inactive removal no longer calls stand; the active inverse
already owns completion. The target fixture must flag its target SVF_MONSTER so
queued dispatch treats it as a unit, as CreateUnit does. Final wc3_unit checks pass
138 tests/14071 assertions per edition. The earlier affected suites also passed;
the full movement sweep passes643 tests/7942439 assertions per edition for
the target-admission change.

## ItemTeleport working audit327

The exact1.27 RTTI tables are AIte6fad1418 and AItp6fad1868, not the addresses in
the separate TFT class inventory. Slots384/388 select435f10/40be30. Begin exempts
self and Inventory Unit1f0 before the all-owner disable walk and acquires198;
inverse releases198 and reverses the availability contributions. Common44 and
protection have additional paired ownership and must not be omitted.

Teleport327c exercises natural completion, early/late Stop, Move, pause, death,
item removal, target removal, caster removal and reissue. Two complete captures match1470 native/marker rows exactly after dropping
only seq, and1014 observer-free public markers. Stop is accepted but retained, Move rejected, and a pending pause
constructs its physical owner after inverse. Death/item removal invoke inverse
synchronously; target removal does so on the next update. Removing the target
after caster retirement triggers original437544 read314 in both observed
and observer-free runs. Preserve the two incomplete exploratory runs separately;
the corrected probe leaves that retired caster's destination alive. No engine
ItemTeleport implementation is claimed yet.

Morph326target confirms entity-target Move independently of point Move: two
complete repeated streams match1353 exact rows after seq removal only, and the
no-hook control matches654 markers. The active morph head remains until its
timer inverse; the retained target Move activates afterward. This backs the
shared target-admission fix above. Evidence is in `morph326-working/target`;
Ghidra569e20 and the mapper retain the result.

Effective data resolves the apparent timing discrepancy: War3Patch.mpq's
`Units\\AbilityData.slk` overrides AItp Cast1 to5 (Area1100, DataA90, DataB1).
Base war3.mpq has Cast0/Area900 and War3x.mpq Cast3/Area1100; neither is the
live effective row. The patch row and Ghidra decompilations are archived with
the complete capture. Native begin/end clocks are0.5999994278/5.599832535 on
the primary scalar clock. The entry-point/owner timing still needs a public
engine regression and implementation; no hardcoded five-second fallback.

Engine audit327 checkpoint (not a TODO closure): the actual UI path `G_UseItem`
then `G_ClientCommand select` first failed9/14 assertions with AItp absent.
The first ordered-spell implementation passes14/14: authored non-stock Cast=.375,
item-slot order852008, acquire198, retained Stop, rejected Move, inverse,
charge consumption and caster relocation. It retains the initiating item identity
in cast-owner messages and uses that identity for item-removal cancellation.
The availability record saves its exempt Inventory rawcode alongside the
exclusive-owner bit. Channel's different trailing own-order increment is unchanged.
Lifecycle/save regressions are being added. Protection/common44, destination and
passenger fidelity, target-loss scheduling and final regression validation remain
unfinished; this checkpoint must not be cited as full ItemTeleport parity.

Further static evidence: AItp slot438 resolves3f6590. Passenger selection records
Hero status, level, wrapped integer squared3D distance and owner equality before
sorting; it is not justified to copy Mass Teleport's plain edict-order truncation.
Destination validation3ff000 wraps3fef60: rejects movement categories10 and deep-water20,
requires the caster's Move owner, and uses shared-spell permission before the
248/40 and5c/02000000 checks.40d4a0 chooses the closest valid destination by squared
2D distance.40ae70 passes placement through439490/3f9c10 before relocating;
exact placement remains separate from the counted-owner checkpoint.

Target-loss timing is now narrower than “next update”: AItp event dispatcher
424370 routes d01a4 to4374f0. Invalid/dead/invisible/relocating targets unsubscribe,
then406bf0 schedules d01bd using Chaos_CommitDelay (cd53a4). The complete scene7
capture retains the removal marker at primary bits1116680862 and inverse at
1116682172, two primary quanta later. Use a scheduled failed completion, not
monster_think polling and not the original successful-cast deadline. The engine
lifecycle fixture also needs its player client pointers restored after
reset_entities; its first crash was a NULL player at UI selection, before any
lifecycle action, not evidence of a production removal crash.

The revised public lifecycle test passes122/124 assertions before target-loss
implementation; only the target hold/inverse remains. Replacing the coarse
update assertion with the frozen two-quantum deadline produces123/126, including
an explicit deadline failure. The implementation adds a failed-finish phase to
the existing saved spell continuation, with a concrete profile opt-in. It does
not apply the policy to Channel or other spell families. Verification is pending.

Verified engine327 checkpoint: three public ItemTeleport tests pass145/145
assertions in both classic and TFT. The corrected target case uses
G_DeferFreeEdict (the semantic RemoveUnit boundary), not G_FreeEdict (storage
reclamation). Its failing baseline plus the protection regression reports
140/145 before the target-loss handler/protection fix. The inverse restores both
availability and198 at the frozen two-quantum deadline. Death/item removal,
pending pause, cold save during a pending pause, duplicate-item removal and
same-item reissue pass through real dispatch. Damage/spell protection uses
separate signed counters; scripted invulnerability remains a separate boolean,
and is preserved if set during the cast. Shared damage, targeting, aura and HUD
consumers query effective invulnerability. Spell immunity includes the counted
contribution. Protection counters and the Inventory exemption are in Save192;
no network layout changed. Negative-buff removal and common44 presentation,
full destination/payload policy and broader target-loss producers remain open.
Full item suites and final matrix have not yet been reported green.

ItemTeleport payload corrections from the actual call arguments:408bb3 invokes
047710 as `Misc/TownPortalCluster`, default0, then tests authored DataB. It is a
configuration lookup, not an edition gate. Movement category10 is FLOAT,
category20 is AMPH; neither should be labeled FLY. The capped list comparator
3fb3c0 prefers owner equality, Heroes, higher level, shorter wrapped squared3D
distance, then descending native identity. The engine payload now collects
eligible own units before relocating the caster and uses the committed caster
position for the nonclustered translation. Engine tie-breaking and placement
are existing engine mechanisms; exact native placement and wider candidate
filters have not been certified by this checkpoint.

The fourth engine test adds capped passenger selection and save/load after
semantic target removal followed by actual storage reclamation. After correcting
fixture event draining (pending earlier actions cannot retain freed source
identities into a save), all four pass213/213 in both editions. Native48b890's
buff cleanup is positive AND negative magic (physical0, magic1, timed-life0,
aura0, auto-dispel0, positive1, negative1), not just negative buffs. A follow-up
regression first fails215/217. The policy-driven cleanup removes Slow; Inner Fire
still lacks its applying-owner classification and is being corrected.

Audit327 affected-suite checkpoint: the five public ItemTeleport regressions pass
229/229 assertions in both Classic and TFT. Full affected suites per edition:
items92/1215, spell512/15660, combat197/4693, Channel4/200, save219/67902
(tests/assertions). Reports are archived under
`research/SEP-01.2/teleport327-working/engine-checkpoint`. These checks cover
invalid Town Hall destinations, both magical buff polarities, independent protection
ownership, passengers, interruption and cold-save completion. They do not certify
all native placement/candidate policies or close SEP-01.2.

Audit328 AI scope correction: the enabled323 captures contain fifteen scope pairs,
including immediate creation of computer-owned units before StartCampaignAI, not
only the timed-life scene. At scene7 the same unit also receives two successive
pairs at tick634 and two at648. The internal d0006 dispatcher owns this scope;
Town enrollment alone is not its producer. `9cd390` routes a retained Captain
member to `9d8a90` and otherwise dispatches resource/Town work. Preserve the
creation and subsequent task-completion boundaries when integrating this owner.

Audit328 read-only backtrace repeats finish with720 identical marker/scope/stack
rows after dropping only sequence; all675 public markers equal the existing
observer-free `payoff323-working/evidence/enabled-control1-preload.txt`. Archive:
`research/SEP-01.2/ai328-working`. Creation runs through `9b9230 -> 693450`
before enabling ability AI mode. Tick579 Wisp return reaches append through
`5ae888`; tick634 timed-life application reaches Town reenrollment and then a
second append through `6106a2`. Tick648 physical completion first dispatches
a queued AI task through `5fa931`, then admits another via `60330e -> 6802f0`.
These distinct pairs must not collapse into one whole-function enrollment hold.
The new public computer/human creation regression fails1/8 assertions before
the engine dispatcher; final count zero alone previously hid the missing pair.

Audit328 engine checkpoint: `G_BotDispatchAIStop` owns the counted scope after
Town registration and during temporary-Captain dispatch. Temporary reassignment
retires the previous order before acquiring198; same-Captain duplicate applications
return before dispatch. The creation regression failed1/8; disabled-policy
detachment failed1/25. Both now pass, including enabled separation configuration
during creation. Full policy11/604 and bot122/1991 tests/assertions pass in each
edition. The full repository suite is pending. No new saved field or wire change
is involved. Broader Town-role re-enrollment, cargo return and physical-completion
AI producers remain required; the dispatcher does not certify them merely because
its acquire/release helper is shared.

Morph-family data audit330: the accepted patch separates normal `Dur` (transition)
from `HeroDur` (form lifetime). AEme authors1.5/60; ANcr .35/15; ANrg1.5/0.
Aave authors Cast1/Dur1.1/DataC1; Astn Cast.7/Dur.5/DataC.5; Abur Dur1.45;
Acpf/Aetf Dur.7; Aphx Cast.7/Dur.5/HeroDur10/DataC.5. All these rows author
base rawcode in DataA and alternate rawcode in UnitID. `569e20` still adds
ability9c and conditional landing DataC; shared RTTI does not establish subclass
entry timing or prove those producers integrated. Existing generic Metamorphosis
uses HeroDur for expiry but currently skips the transition; Stone Form also
rebinds immediately. These remain open in the counted-owner audit.

Cargo return audit329: complete computer repeats agree on1,229 read-only rows,
including fifteen AI scopes; human repeats agree on964 rows with no AI scopes.
Both match675 public markers from their observer-free controls. The human map
changes only Player0's controller in frozen Separation323g; the checked builder
reproduces its exact archive hash. Evidence: `research/SEP-01.2/cargo329-working`.
The observed Wisp-return virtual31c is `5803c0`; virtual320 for dead passengers
is empty. The live handler replaces orders, then computer-controlled units append
AI only when `9d04a0` does not recenter their retained attack Captain. That separate
branch requires the active world lifecycle, matching Town/Captain membership and
at least500 committed world units of separation. It remains a static finding,
not covered by these basic cargo captures. Engine cargo-return integration is pending.

Audit329 distant-Captain witness: `Cargo329assault` recruits the Wisp through
`AddAssault` before its public Harvest order. Mine destruction reaches `9d04a0`
at tick579 and returns1, suppressing the ordinary AI append after cargo's inverse.
Two complete repeats match1,222 rows with only sequence-number normalization;
all678 public markers match the observer-free control. Archive:
`cargo329-working/assault`. The earlier timed-life experiment rejected Harvest
and is excluded. Its leftover `temporary-captain` marker is merely a label in
the successful map; the Wisp has no timed-life application there. These captures
prove the return branch, not every member-update policy inside `9d16c0`.

Full-suite guard regression: model registration exposed idle Stand animation
loops calling the task-completion entry point repeatedly. A neutral creep's
five-second guard-return timer was consequently restarted by every one-second
Stand loop. The rooted-return fixture now loads `anim_pulse.mdx` explicitly;
it fails the actual arrival assertions before the fix. Stand and Stand Ready
now loop through a presentation callback, preserving combat-dependent animation
selection without emitting task completion or `A_UNIT_STAND`. Both editions
pass the order-lifecycle73/16074, unit138/14071, combat197/4693 and bot122/1991
suites; the previously failing full shards each pass947/3999492. Full repository
validation after this correction remains required.

Audit329 placement detail: two additional read-only captures match1,230 rows
and the same678 observer-free markers. The return queries Captain `(512,1024)`
and passenger `(1552,816)`; `653510` requests the latter but admits `(1584,784)`.
The engine regression must check placement admission, not exact passenger overlap.
Initial engine changes restore live-passenger Stop cleanup and the private AI
scope, or retain/relocate the logical attack Captain and update ordinary shared
member orders. Broader retreat/follow-target/centroid member policies remain
outside the basic captured branch; this is not complete `9d16c0` parity.

Audit330 Morph-family coverage: `morph330_make_map.py` derives Morph330c from
frozen Separation323g (map SHA256
`f61c2b0c627c240c38dca2b1042f47de98ae81e5897100fdcfe6298eaa56a7ee`).
Two complete captures agree on all826 native/marker rows through scene5
(tick538), and on1,115 relevant state/event rows across the full survey; all730
public markers match the observer-free control. Scenes0–6 cover Metamorphosis,
Chemical Rage, Robo-Goblin, Avenger, Stone Form, Burrow and Corporeal Form.
Raw native equality differs first at tick551 in the additional `d01c4` producer
at caller `6d81b8`. Saved Ghidra `Effect_ScheduleDeathRequest` (`6d8100`)
identifies virtual8c/`EffectDeathTime` and a separate event-clock flag: this is
a presentation-effect owner, not Morph. The verifier excludes that identified
producer, preserves raw captures, and removes only seq from compared rows. Scene7's Aetf/removed-Acpf composition changes the
starting endpoint and is not a standalone Ethereal Form contract. Evidence is
in `SEP-01.2/morph330-working`; do not describe this as an all-family pass.

Metamorphosis and Chemical Rage use normal Dur for both counted transitions;
overridden HeroDur2.5 starts only after the forward type completion. Saved
Ghidra `Morph_CompleteForwardAndScheduleLifetime` (`552120`) schedules `d01db`
after subclass completion and Hero refresh. The engine now retains distinct
primary transition and expiration requests in `morph_t`, serialized by
`morph_fields` and rebuilt into the indexed morph heap. Form lifetime contributes
no suppression. Expiration calls the concrete form owner to start the reverse;
MDX completion cannot release the transition. The initial Metamorphosis
regression failed38/134 assertions before implementation, then passed152/152
in each edition, including both SLK schemas, Stop and saved form lifetime.
Chemical Rage's missing public-order mapping failed50/286 in the expanded
regression. The mapping now belongs to `m_unit.c`; both game targets explicitly
depend on the remaining canonical `g_order_names.inc`. Final combined tests
pass304/304 in each edition, full unit suites139/14375 and save suites219/67902.
Other Morph families and broader interruption compositions still require
engine integration/verification. The earlier cargo/idle-Stand implementation
passed full movement suites645/7942478 in both editions before this Morph
change; its build-source hashes and reports are archived separately.

Probe pitfalls: a computer player autonomously reverses Stone Form and Burrow;
use a human controller for isolated orders. Avenger Form requires resources in
addition to its `avengerform` order and research. Do not flush the preload file
at the pre-loading `start` marker: the observer-free runner mistakes that file
for a running map and stops sending the loading-screen continue key. Morph330c
flushes only subsequent markers. The earlier A/B maps are exploratory.

Audit331 extends the common transition owner to Burrow and Robo-Goblin.
`Morph331.w3m` (SHA256
`e114a751a88a254e32436f57f3ec883691a1d9420526888977869cc0c970ba7b`)
exercises each direction with natural completion, Stop and scripted pause.
Two complete captures match858 native/marker rows; the category-instrumented
repeats match903 rows. Both match500 observer-free public markers. Only seq is
removed from compared events; module bases/controller metadata are excluded.
The archive is `SEP-01.2/morph331-working`.

Both forms retain their source type through normal Dur, with one counted
suppression contribution. Stop is retained until completion. Pause waits for
the morph inverse, then acquires its own physical suspension contribution;
unpause releases that contribution. The funded Robo-Goblin forward order is
accepted: the earlier underfunded exploratory map's false return is not a
successful-order quirk.

`CAbilityGroundMorph` owns endpoint/alias resolution, directional orders,
transition animations and the common timer. Robo-Goblin retains its
`CAbilityMetamorphosis` registration and delegates to the shared implementation,
so existing AI conversion discovery still recognizes it. Burrow no longer
incorrectly registers as natural creep sleep; `ACsp` retains that innate owner.

Burrow changes fine occupancy, not its query. Saved Ghidra
`AbilityBurrow_PublishCollisionFlag` (`4eddf0`, CAbilityBurrow vtable+f4) passes
Unit20 bit26 and literal keepBuild0 to `4ba8d0`. Forward completion rebinds ucrm,
then category ca becomes c8. Unburrow restores ca while still ucrm, before the
delayed ucry rebind. `morph_t.category_clear/category_set` retain the game-owned
publication and feed the existing fine-world category adapter; completed base
rebind/removal restore the authored profile. These fields are serialized with
both morph timers. Failing-first regression:192/520 assertions failed on the
old engine. Final regression passes574/574 in each edition, including saved completed
Burrow category and retained Robo-Goblin AI discovery. Affected suites pass in
both editions: unit140/14949, bot122/1991, save219/67902, object categories8/321,
fine spatial19/6476, combat197/4693 and effects3/32. Full repository/DEBUG
validation and the remaining Morph producers are still required.


### Audit332: Stone Form and Avenger Form

`morph332_probe.j` builds from the frozen Separation323g archive through
`morph330_make_map.py --probe`. Two complete read-only captures match783
state/event/marker rows exactly after dropping only sequence numbers; all495
public markers match the no-hook control. No timer/clock exclusions apply.
Map SHA256 `9ee6af091bf10b498955d1a13e867116c223a984ffa16231aa4b75ead88ae83e`;
evidence is `research/SEP-01.2/morph332-working`, including `verify332.py`.

Stone landing keeps ugar through acquisition and releases after1.0s before the
ugrm rebind. Unstone takes off/rebinds ugar before acquiring, with a.5s request.
Avenger rebinds ubsp before acquiring and schedules1.1s. Each direction has
natural, Stop and scripted-pause witnesses. Pending pause's physical contribution
begins after the morph inverse. Ghidra's shared begin/end/landing/takeoff notes
are saved; `MapPathfinding.java` records this witness.

`CAbilityStoneForm` and the delegated `CAbilityAvengerForm` use the shared
primary transition; `S_PrepareMorphFlight` shares profile publication with
Raven/Bear. `CAbilityMorphTransition` now owns sparse ascent updates independently
of the destination ability list. AI still sees Avenger's Metamorphosis identity.
The new custom-data/schema/order/pause/save regression failed134/394 before the
fix. Complete unit suite141/15379 passes in each edition, including existing
Raven ascent and Stone subgroup/direction tests. Flight-height trajectories,
blocked landing placement and other Morph-family producers are not certified
by these traces. See [Stone Form](stone-form.md) for the engine contract.


### Audit333: Corporeal/Ethereal producer evidence (integration pending)

The retained morph aliases use opposite authored endpoints: Acpf ospm→ospw,
Aetf ospw→ospm. Both directions schedule normal Dur.7 through569e20; the type
rebind is deferred until after the separation inverse. Natural, Stop and pause
scenes repeat894 relevant state/event/marker rows, with500 no-hook public markers.
The raw captures retain twelve presentation-effect timer requests from6d81b8;
their frame-clock timing differs between repeats and is excluded from the morph
comparison under the already identified6d8100 `Effect_ScheduleDeathRequest`.
No primary morph clock or Unit158/198 value is normalized.

`morph333_probe.j` makes the added ability permanent and the builder's optional
`--empty-ability-units ospm ospw` removes built-in partner abilities from both
endpoints. This is a public authored-data/UnitMakeAbilityPermanent witness, not
an unmodified stock-unit claim. Without permanence, the rebind removes the
added ability and immediately restores its base form; with the built-in partner
present, its own removal/inverse also interferes. Earlier333/333b exploratory
captures retain those outcomes and must not stand in for isolated form evidence.
Final map SHA256 `ed3594fb40bdcdf876e6091a5191fe85757d061278148a51bb0c0fa291500283`;
archive `research/SEP-01.2/morph333-working`, checked by `verify333.py`.

Unit158 is the ethereal contribution, separate from separation Unit198.
`CAbilityEthereal` vtable6fb536a8 calls48b7f0 from init80=5e7290 and48ec80
from detach a4=5c5910. Ghidra now names those `Unit_AcquireEthereal` and
`Unit_ReleaseEthereal`. Corporeal vtable6fb53e54+40c=5d3b00 releases ethereal
on forward completion; inverse414=5d3ac0 acquires it. Ethereal vtable6fb53a04
+40c=5d3b30 acquires it; inverse414=5d3ae0 releases it. The first positive/zero
edges own availability, target-lost/available and appearance updates. New mapper
rows preserve these findings. Engine333 regression/implementation is pending;
its state tests must cover the ethereal gameplay gate as well as type and timer.


### Audit334: permanent morph removal

`Morph_InitializeAuthoredFlags` (55d380) converts DataB with the original
software integer helper. Bit8 sets ability02000000, which makes540e30 cancel
requests without running the transition inverse or restoring the base type.
This flag is separate from `UnitMakeAbilityPermanent`, which retains an ability
across type rebinds. The other raw mappings are2→400000,4→800000,1→200000,
10→4000000; these mappings alone do not certify every option's downstream effect.

`morph334_probe.j` witnesses early/late removal and early death for Stone/Avenger.
Two complete repeats match408 state/event/marker rows exactly after dropping
only sequence; all248 public markers equal the no-hook control. Map SHA256
`1c7e3fb9945c7e2f2d746f1c6f33daff9033ce7c0a15f31ca4767c8574e5b823`, archive
`research/SEP-01.2/morph334-working`. The initial map used reserved JASS keyword
`code` as a local name and never reached its start marker; only corrected334b
is evidence. Stock Avenger (DataB31) early removal keeps ubsp and its counted
suppression contribution, with no later inverse. Normal completion removes Aave
itself, so a later public removal rejects. Stone early/late removal restoresugar;
early death retains the current type and suppression contribution for both.

The common morph owner now reads the authored permanent flag. Early removal
cancels its request while preserving the counted contribution and the current
type. Completed permanent forms retire their ability when no form-lifetime
request remains. Independent height movement survives that removal; removing
an inactive ability does not replace an unrelated order. The new custom-endpoint,
both-schema, flag-on/off, removal/death/cold-save regression initially failed
28 of416 assertions. The expanded regression passes468/468, including inactive
removal preserving Move and permanent ability retirement preserving independent
ascent. Unit142/15847, spell512/15662, ability lifecycle60/449 and save219/67902
pass in both editions. The updated routing/AI/build DEBUG configuration also
passes unit142/15847, spell512/15662 and ability lifecycle60/449 in both editions.

Baseline validation through332: all eight WC3 full-suite shards pass
(3,793 tests/18,753,219 assertions per edition). The repository run returned
exit2 because CorpusTests read a map-builder source during its hash update;
all other2,089 Python tests ran, with that one setup error. After pins stabilized,
the37 corpus tests pass. Routing/AI/build DEBUG configurations pass both editions:
unit141/15379, bot122/1991, movement645/7942478, order-lifecycle73/16074.
Logs and all JUnit reports are in `morph332-working/full-validation`. This
baseline predates334, whose separate build lives under `build/morph334`.

### Integration checkpoint334

The root checkout integrates the verified 323–334 engine changes with Save192:
counted ability availability/separation, Channel follow-through, item-owned Town
Portal work, AI Stop/cargo handoff, and timed ground/flight morph transitions.
The network format is unchanged; logic and saved ownership remain in the game
module. Earlier saves are rejected by the existing strict version gate.

Root Classic and TFT each pass 1,892 affected tests/8,062,536 assertions across
unit, spell, ability lifecycle, save, bot, Channel, repulsion policy, items,
Avatar, order lifecycle and the complete 645-test movement suite. The Captain
enrollment and timed-life tests belong to `wc3_bot` and `wc3_movement`; a
rejected zero-test filename-shaped filter is retained in the logs, not counted.
Routing/AI/build DEBUG passes 714 unit/spell/lifecycle tests per edition; the
unused debug-only path-texture printer was removed and the warning-free build
passes the 142-test unit suite again. SC2 and WoW production builds also pass.

The 37 corpus checks and 12 exclusion-assignment checks pass. All 559 pre-existing
corpus entry contracts remain unchanged; 2,321 indexed fixture hashes match.
The assignment checker is named `audit_wc3_pathing_exclusion_owners.py` because
the `verify_wc3_pathing_` prefix is reserved for executable native-oracle entries.
Root logs and indexed source evidence are archived in `research/SEP-01.2/root334`.
The earlier full 332 checkpoint and its original umbrella exit 2/setup-race
correction remain described above; this is focused final 334 verification.

SEP-01.2 stays open for the documented remaining producers and compositions.
In particular,333 Corporeal/Ethereal findings are native evidence only, not an
implemented engine family. BASE-03.3 is independently closed by the
[exclusion assignment audit](retail-pathfinding-exclusion-owners.md).
