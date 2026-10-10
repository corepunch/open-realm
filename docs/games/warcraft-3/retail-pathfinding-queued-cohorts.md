# Queued movement acquires the first compatible spatial cohort

Target: Warcraft III 1.27.1.7085. Payoff232 advances GROUP-04.6 and removes a
whole-group scan from queued activation. It does not close the broader task.
See [movement integration](retail-pathfinding-engine.md), the
[research backlog](retail-pathfinding-todo.md) and the
[executable corpus](retail-pathfinding-corpus.md).

## Retail contract

`Move_FindPreviousRequestCohort` (`5fa950`) calls the variadic selector parser
`67e790` with callback `5faaf0` and these selectors:

| Stack input | Meaning |
|---|---|
| `8`, radius word, source mover bridge | Center-only circle around the source's predicted world position |
| `24`, player | Restrict candidate ownership |
| `31` | End selectors and execute query |

Initializer `013490` writes **1000 world units** to `6fd6fb38` through the
software integer conversion. The older mapping's “maximum8 candidates within24”
was incorrect: both numbers are selector codes. The terminating parser folds
owner/dead exclusions into the spatial mask; its initial Unit predicate defaults
must not be mistaken for the final circle filters.

`059c10` predicts the source position. Its zero touching mode dispatches through
`05f290` to `05d680`. That query converts world center and radius to fine
coordinates, materializes the complete bounding rectangle through
`05eb20`/`05f010`, then evaluates candidates. The unit map visits X before Y;
within a cell the newest effective record comes first, and each identity is
retained once. It compares the candidate's retained mover center against the
radius squared with software arithmetic. Candidate collision radius is not
added. Equality is accepted. A callback returning zero stops evaluation after
materialization, then the private query depth and active flag are restored.

`5faaf0` rejects self, differing previous-request identities, differing
**authored movement-type bits at Unit+1fc**, unresolved owners, unequal fine
coordinates of the destination and a physical owner already containing twelve
rows. The type bits are distinct from fine collision category and pathing query
mask: Foot and Horse both use query2/categoryCA, but have type bits1 and4.
Forced-ground collision publication must not change this authored comparison.

On success the callback constructs a fresh request, attaches the activating
source first, then every nonnull prior-owner row except that source in retained
row order. It marks peers ready and returns zero. It does not combine additional
independent physical groups. Latest submitted request history stays separate
from the FIFO entry currently activating.

## Engine data flow and cost

`move_start_queued_group` now queries the shared Move proximity publications.
`S_VisitMoveCircle` retains entity index/incarnation pairs before invoking any
consumer, uses retained fine centers, and supports nested queries with private
reusable storage. Callback edits cannot insert new candidates into the outer
snapshot; removed/reused identities are rejected before use. Teardown releases
the derived query storage. Authoritative group/save layout is unchanged.

The compatible-peer callback binds the source and inherited rows directly to
the new physical owner. Ordinary captain admission also publishes its derived
binding immediately. This prevents a local spatial query from falling back to
an O(all groups) owner search during normal activation.

Query work is O(covered cells + covered record links + collected candidates),
followed by at most twelve inherited rows. Unrelated physical groups do not
contribute to the scan. Query contexts allocate only when their retained
capacity/depth grows. Dense local populations still require genuine candidate
work; this change makes no overall CPU or frame-rate acceptance claim.

## Evidence and verification

`tools/ghidra/research/Work232Evidence.java` saves names, corrected calling
conventions, selector/radius comments and xrefs in Ghidra. It exports 1,796
instructions from the relevant original functions. `MapPathfinding.java`
retains the correction for future mapping runs.

`work232_oracle.py` executes the unchanged original circle query, numerical
helpers, rectangle materializer and raw cell traversal. The fixture supplies
constructed spatial records, canonical owners and private query-context
storage. Only Storm memory imports and the caller-provided visitor are host
adapters. Eight cases cover 999.875/1000/1000.125/1100 and continuing/stopping
visitors. In particular 1000.125 is excluded despite the candidate's collision
radius, and an early stop still leaves the complete materialized snapshot.

Two fresh read-only Frida captures reuse the frozen selected-queued corridor
map SHA256 `2005c82b5f77c49376d4815f779e6f7c6720bfae2d2cb1b4fb6726e55ea987a2`.
Each completes 305 public markers, two searches and four candidate callbacks.
Both show the selector sequence above, the shared latest request history, the
first failed search and the later first-success stop. Their input clocks and
trajectory words remain separate; there is no observer-free control for these
two captures. Existing frozen retail trajectories supply the movement checks.

The failing-first engine regressions cover the first spatial peer with 1,024
unrelated owners, exact radius boundaries, Foot/Horse type separation and
nested/mutating query callbacks. No established expected trajectory was
modified. The pre-existing fourteen selected-order tests pass unchanged in
Classic and TFT, including staggered/multiple Shift activation and saved
continuations.

Run the complete focused contract with a new output path:

```sh
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/verify_wc3_pathing_work232.py \
  --binary /path/to/retail/game.dll --report /tmp/work232-fresh.json
```

The archive is
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-04.6/payoff232/`.
It retains the late-input capture that reached no cohort search, the initial
synthetic-map reset mistake and the test fixture whose collision rectangle
crossed an earlier cell. None defines retail expectations. The corrected
fixtures publish through the original region update and use separate context
lifetimes.

Persistent canonical candidate publication, completion/recovery and wider
producer lifetimes remain GROUP-04.6 work. Preferred source selection follows.

## Route sources prefer adaptive-enabled members (Payoff233)

`16c6d0 PathGroup_SelectRouteSourceMember` does not simply select the nearest
member. It predicts each member's fine pose using retained position plus old
velocity multiplied by original elapsed time. It maintains separate strict
minimum squared distances for paths with `ownedPath+88 & 200000` set and clear.
If the enabled pool has a candidate, that pool wins regardless of the ordinary
pool's distance. Strict ties preserve member order. Ordinary selection defaults
to row zero; the preferred pool starts empty. Group flag `80 & 200` bypasses the
scan and returns row zero.

`16de50 PathGroup_PrepareRouteFromMembers` publishes the predicted mean heading
and fresh `10000` state before selecting the formation origin. Ordinary groups
use the preferred member's prediction; bypass groups use the destination itself.
The scheduler class still comes from the first individual path, independently
of the selected route source.

OpenRealm already saves the exact inverse policy as
`movement.adaptive_disabled`. `0594a0` owns the retail flag; resets through
`168740` and class changes through `168c00` preserve it. Fresh nonstructure
creation enables it even for authored flight; a flying type rebind disables it.
Physical flying class, formation-held state and adaptive enablement are separate
properties. `move_group_source` now consumes this existing state with one linear
member pass, no allocation or new saved fields. `move_group_seed_route` preserves
the bypass destination rule. Network and save layouts are unchanged.

The unchanged-original oracle executes all of `16c6d0`, including its native
elapsed and software arithmetic helpers, for 48 combinations: distinct/static
tied/moving positions, every three-member preferred mask and both bypass values.
The fixture supplies canonical group/member/path fields and clock state;
external Storm allocation is a host storage adapter. The exported 426 original
instructions also cover route preparation and adaptive-policy producers, but the
oracle does not claim to execute the complete route-construction graph.

Two fresh read-only captures on `Work233c.w3m` each record six source decisions:
both fresh Gryphon Riders choose the nearer row zero; transforming the nearer
unit to a Hippogryph through an authored Chaos alias makes retail choose the
farther, still-enabled row one; transforming both restores nearest row zero.
Both creation and first route admission show the same selections. An
observer-free control matches all eight public markers. Its private decisions
and complete motion trajectory are not observed. `PreloadEnd` records wall time
and differs between runs; it is excluded from simulation equality.

The failing public engine regression creates two flyers, rebinds the nearer
flyer through the real type-transform producer, issues a shared point order and
checks the farther origin before and after save/load. The original 48-case
matrix checks source identity, predicted coordinates, bypass origins and fresh
flags. Existing selected-order retail trajectories are unchanged.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/verify_wc3_pathing_work233.py \
  --binary /path/to/retail/game.dll --report /tmp/work233-fresh.json
```

Evidence is archived under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-04.6/payoff233/`.
The first oracle mistakenly wrote the request-clock pointer field at owner14
instead of the authoritative path clock at owner54. The corrected oracle
asserts native elapsed time is two before executing the selector. Early Chaos
probes targeted the same type and did not perform the required rebind; a delay
alone did not fix them. Those attempts remain rejected evidence. No established
retail fixture was rewritten. GROUP-04.6 still requires persistent canonical
candidate publication, completion/recovery and broader producer lifetimes.

## Disabled group routing retains a real cache (Payoff234)

Adaptive disablement bypasses search work, not route publication. Complete
`167120 Path_RequestGroupRoute` first establishes footprint/query context,
clears a stale target-region pointer, obtains the adjusted destination from
`167d70(mode1)` and multiplies it by the hierarchy inverse scale. With path
flag `88 & 200000` clear, it erases any previous adaptive points, appends this
single point, sets index zero and returns success. It performs no interval
check, scheduler admission, coarse search or timestamp write.

The caller `16ce10` still refreshes formation and resets owner counters after
this successful replacement. Subsequent unchanged destinations retain the valid
one-point route and bypass replacement. The member's independent fine route
still owns its own work budget and terrain checks.

OpenRealm previously returned the requested destination with `group_count=0`
and left `group_goal` unset. Every owner visit therefore looked uncached, while
Move never saw a real replacement to reset its age/counters. This also left
saved group state inconsistent with retail. `G_UnitMoveGroupDestinationStatus`
now publishes the one-point cache and ordinary destination/radius/mask/revision
metadata, reports an actual replacement once, and shares the existing retained
route exit. Storage uses the existing geometrically reserved route buffer; warm
cached visits neither allocate nor consume search admission. Save and network
layouts are unchanged.

The original oracle runs all of `167120`, its native route-table erase/append
operations, path constructor and software arithmetic. Thirty-six cases cover
four classes, zero/one/three previous points and three integer/fractional
adjusted destinations. Even exhausted supplied scheduler buckets retain their
entire words; path timestamps `7c/80` stay unchanged. These are controlled
kernel inputs, not a claim that every disabled class has a public producer.
Only external Storm memory calls receive host storage.

Two fresh read-only captures reuse the frozen `Work233c` flight-rebind probe.
Each observes three `167120` calls. Fresh and partly rebound groups retain
adaptive enablement and call `166c30` once each. When both flyers are rebound,
the group path has flags `00400000`, returns one destination point at index zero
and never calls `166c30`. Capacity128 survives native path reuse. An unhooked
control matches all eight public markers; its private route state and full
motion are not observed. `PreloadEnd` wall time is not simulation equality.

Failing-first engine regressions cover all36 original table results, unchanged
cache reuse, budget/timestamp preservation, a real flying type rebind followed
by public group admission, replacement versus cached age, and cold save/load.
The synthetic exhausted work word is restored before serialization because it
exceeds legal saved scheduler limits; that fixture correction does not change
retail expected route words. Existing flying Follow and selected-order retail
motion fixtures remain unchanged.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/verify_wc3_pathing_work234.py \
  --binary /path/to/retail/game.dll --report /tmp/work234-fresh.json
```

`Work234Evidence.java` saves the native publication/cached-return contracts,
xrefs and438 original instruction encodings. The archive is
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-04.6/payoff234/`.
This advances GROUP-04.6; persistent canonical request candidates, other group
policy producers and wider completion/recovery still require integration.

## Ready members partition by ordered connectivity (Payoff235)

`16bcf0` visits the twelve ready slots in candidate order. Each surviving slot
creates a physical owner through `16bdb0`; `16b7b0` consumes that seed, appends
its native member row, binds the mover and recursively consumes eligible
neighbors in the same slot order. Membership is transitive connectivity, not
distance from the initial seed. A consumed slot becomes `ffffffff` before
recursion; request bit200 suppresses neighbor joining entirely.

The distance predicate depends on the two owned path policies:

| Policies | Distance and threshold |
|---|---|
| Either path lacks200000 | Software subtract, square, sum, square root, then wrapped integer conversion; accept <=40 fine units |
| Both paths carry200000 | `1627e0` on predicted poses scaled by0.5, source lane, maximum coarse footprint, work60, warp1; accept unsigned result <=40 |
| Canonical request100 | Replace threshold40/work60 with90/work150 |

The accelerator uses its retained hierarchy. Fine member exclusions do not
authorize clearing all coarse rectangles. A fractional fine separation41 can
join through quantized coarse cells while the geometric branch rejects it.
A wall can conversely reject an adaptive pair whose geometric distance is
small. Neither a common Euclidean threshold nor a star-shaped cluster is an
equivalent replacement.

OpenRealm `move_group_publish_ready` now publishes independent physical owners
through a stack-local ready set and depth-first traversal. At most twelve rows
and66 bounded pair queries participate; it does not search the existing group
registry. Geometry/gates are prepared lazily once for the complete ready set,
and each adaptive pair reuses the retained hierarchy. Every split preserves the
original request identity, copied target/policy and shared-parameter reference;
each physical owner seeds its own route. Admission callbacks can replace or
remove an earlier candidate, so publication validates generation and current
ownership before binding. Existing save fields already represent these owners;
there is no layout/version change.

`work235_oracle.py` executes complete unchanged `16b7b0` scopes, native row
construction/append/bind, software prediction and adaptive queries. The192
cases cover eight arrangements, all eight preferred-policy combinations and
default/captain/bypass flags. They include ordered transitive joins, exact and
fractional thresholds, old velocity and a wall. Supplied ready pointers and
cached world headers are explicit; the only external substitutions provide
Storm allocation storage. All ready slots and the native mover flag byte are
also checked. This does not establish a public producer for forced flags100/200.

Two complete read-only `Work235b` captures and an observer-free control agree
on eight public markers. Birth flyers and genuine Chaos flight rebinds produce
two physical groups for fine positions8/90/48; the connected8/48/88 scene
produces one. Nine recursive binds and five physical owners occur per capture.
The third scene's actual canonical candidate order is8/48/88; the probe's
`GroupAddUnit` sequence is not substituted for observed candidate order.
`work235_observer.js` never calls game functions or writes game memory.
Preload file wall-clock headers differ and are excluded from public marker
comparison; raw bytes, including CRLF, are preserved for source hashes.

Failing-first engine tests preserve the frozen native results. Production
point admission, physical partition, shared captain references, nested issued
order replacement, Stop and cold save/load have focused regressions. The first
valid red run had430 failures/1935 assertions; its Stop call initially used a
point-order API and was corrected to the immediate-order API. One premature
run loaded the preceding game module and executed zero tests; another loaded
the old red module during compilation. Neither is acceptance evidence. The
nested-callback regression exposed a retained NULL row after ordinary detach;
publication now skips that row before testing ownership. The original failing
backtrace is archived. The initial192-case build also rejected a misspelled test pathing enum; the final
fixture uses the existing `CM_PATHING_UNWALKABLE`. No prior retail fixture was
rewritten. The first Frida attempt used its read-only `this.depth` property and
failed; it remains archived separately. Accepted repeats use `bindDepth`.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/verify_wc3_pathing_work235.py \
  --binary /path/to/retail/game.dll --report /tmp/work235-fresh.json
```

`Work235Evidence.java` saves the functions, xrefs and542 instruction encodings;
`MapPathfinding.java` carries the portable contract. Archive:
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-04.6/payoff235/`.
Focused validation is implementation9/12 since the last full-suite checkpoint.
This is a ready-member partition payoff for GROUP-04.6. Persistent canonical
candidate/readiness lifetime, busy recovery, all request exclusion scopes and
broader callback/trajectory parity remain open. No performance target or
whole-journey equality is claimed here.

## Queued activation repartitions inherited members (Payoff236)

`5faaf0` attaches the activating source to a fresh canonical request first,
then copies resolved old physical rows in their retained order. Attachment
through `169620` stores the identity independently of readiness: a pending
ready word at `ac+4*i` is `ffffffff`. Each peer's `89cd10` marks it ready and
invokes wrapper virtual `c` (`249b60`). While the source remains pending,
`169c50` reports a conflict and `16bcf0` releases fine exclusions and returns
`-1`. The peers still belong to their original physical owner at this boundary.
Source readiness subsequently runs the ordinary ordered distance partition.
An inherited physical group is not an exemption from the current predicate.

OpenRealm queued activation now calls the same `move_group_publish_ready`
routine as direct admission. A displaced old peer can receive a separate
physical owner, with the common request identity retained. Default queued
reconstruction does not inherit captain bit100 or the original UI formation
policy. The bounded twelve-row traversal and lazy per-request geometry setup
remain unchanged; this adds no scan of the existing physical group registry.
Save layout and network contracts are unchanged.

`work236_oracle.py` executes complete original callback, native request/mover/
path/group factories, old-row preparation, readiness and publication for48
cases. Native `16d1c0` resolves the old rows; native `16dd70` alone deliberately
clears the resolved pointer. The cases cover all eight path-policy masks,
transitive joins, geometric/adaptive boundaries and predicted old velocity.
Both pending and successful publication restore every fine exclusion counter.
Supplied inputs are the unit history/category, callback neighbor, owner clock,
empty cached hierarchy and VM publication gate. Only Storm storage imports use
host adapters. Sixteen supplied-neighbor cases lie outside the public1000-world
circle; the production-query engine matrix intentionally covers the other32.

Two complete read-only captures of the existing frozen selected queued corridor
show one peer-ready pending publication, then source readiness and one successful
physical publication each. Each has305 public markers. The observer tracks the
canonical request beyond the neighbor-search return; restricting every hook to
that search misses the final source publication. The initial two narrow-scope
captures are archived separately and do not certify that final boundary. These
repeats have no new observer-free control and do not establish a crowded public
retail relocation journey or wall-clock input equivalence.

The failing-first engine matrix initially has60 partition failures; the extra
spawn/save test initially uses the local Footman fixture, whose omitted collision
cell produces radius0 and no fine membership. It therefore tests no neighbor
join. The accepted spawn/save test uses the existing authored Peasant radius16
fixture through CreateUnit and SetUnitX. No authored data or retail expectation
is changed. It covers inherited ownership before activation, physical split,
cold save/load and independent Stop. Original function/xref/instruction evidence
is saved by `Work236Evidence.java` and mapped in `MapPathfinding.java`.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/verify_wc3_pathing_work236.py \
  --binary /path/to/retail/game.dll --report /tmp/work236-fresh.json
```

Archive: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-04.6/payoff236/`.
This is implementation10/12 in the focused-validation cadence. Persistent
canonical readiness, busy recovery and wider callback lifetimes still keep
GROUP-04.6 open. No overall performance or complete retail fidelity is claimed.

## Ordinary ground and flight selections share the primary request (Payoff237)

The second context request in `6b8c10` is selected by authored movement bits
`Unit+1fc == 10h`: FLOAT, not flight. `685340` maps foot/fly/horse/hover/float/
amph/unbuild to1/2/4/8/10h/20h/40h. Ordinary ground and flying selections attach
to the same primary canonical request. With Alt, a non-FLOAT unit instead uses
the third special request when current-flight flag `Unit+5c & 20000000h` is set
and forced-ground count `Unit+200 <= 0`. Missing optional requests fall back to
the primary. FLOAT takes precedence over that special-flight branch.
`6ba800` agrees with this classification. Older main/air descriptions in the
mapping are superseded by the explicit Payoff237 comments; historical context
field names do not establish movement-category meaning.

OpenRealm now sends ordinary non-FLOAT selected movers through the existing
shared point-order producer, including mixed ground/flyer and all-flyer groups.
This preserves one requested point, shared history, queued context and physical
owner rather than assigning independent formation slots. Alt flight splitting
and mixed FLOAT admission remain on the earlier producer and keep GROUP-04.6
open. The bounded selection scan remains linear in at most twelve candidates;
there is no scan of the physical-group registry or new per-unit allocation.
Save layout and network contracts are unchanged.

`work237_oracle.py` executes288 complete original `6b8c10` scopes across eight
movement categories, current-flight flags, three forced-ground counts, ordinary/
Alt policies and three optional-request configurations. Native wrapper, canonical
and mover constructors retain pending readiness. Supplied inputs are the unit
bridge/category/counters and request context; only Storm storage is adapted.
The canonical factory entry is `89c890`. Two rejected scratch runs incorrectly
started at its interior `89c8a0`; they are archived and supply no expectations.

Two ordinary and two Alt read-only public captures select two Footmen and two
Gryphons. Each has82 markers, four attachments, four canonical retains and four
recursive binds. Ordinary packets have flags8 and produce one four-member
physical owner. Alt packets have flags18h and produce two two-member owners,
with primary/special request identities and bind flags0eh. The observer neither
writes game memory nor calls game functions. There is no fresh observer-free
control or whole-journey timing claim. `Work237a.w3m` is built by replacing only
the probe script in frozen Work235b; its SHA256 is
`4b5e9b8d713090cf3bbf3ff95f2a74b38d856d7f6e7caee94b18974b765fc1cb`.

The failing-first mixed/all-flight regression initially reports94 failures in154
assertions. Its final fixture uses authored Footman/Gryphon metadata so cold
save/load retains the movement category. Immediate and Shift orders, common
coordinates/history, saved physical ownership and independent Stop pass in both
Classic/TFT. No earlier retail expectation or authored data is rewritten.
Alt captures certify the next integration's input contract, not implemented
Alt parity. `Work237Evidence.java` saves six functions, xrefs and341 instruction
encodings, with portable corrections in `MapPathfinding.java`.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/verify_wc3_pathing_work237.py \
  --binary /path/to/retail/game.dll --report /tmp/work237-fresh.json
```

Archive: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-04.6/payoff237/`.
This is implementation11/12 in the focused-validation cadence. Canonical
lifetime, busy recovery and the remaining selected request producers remain
open. No overall performance or complete retail fidelity is claimed.

## Alt flight requests publish independently in candidate order (Payoff238)

For non-FLOAT selected Move, Alt separates current flyers into the third request
while grounded units retain the primary. Authored flight alone is insufficient:
retail's positive forced-ground count suppresses the special branch; OpenRealm's
ability-owned effective flight flag already represents this distinction.
Each request retains the common clicked coordinates and its own request history.

Preparation must precede callbacks, and candidate admission must retain the
global order. Running the ground batch followed by the flight batch would change
that order. Each canonical request publishes when its own last attached candidate
becomes ready. Ground/fly/fly/ground therefore creates the flight physical owner
first, then the ground owner, despite allocating the primary request first.
All-flyer and all-ground selections have one populated request class.

Move now prepares the populated classes, admits candidates in the original
selection order and publishes each class at its readiness boundary. The existing
stable physical slot doubles as pending staging storage; its newest-first visit
sequence is published through an O(1) unlink/prepend at readiness. This avoids
allocating a second physical object just to establish visit order. Candidate
classification/admission is O(N), bounded by twelve rows. Pending Shift orders
retain separate class contexts through the existing queue owner. Shared Captain
requests retain their prior producer. Save fields and wire format are unchanged.

`work238_oracle.py` executes ten complete original attachment/readiness/physical
publication scopes, using native request/mover/path factories, `6b8c10`,
`89caf0`, `89cd10`, `16bcf0`, `16bdb0` and `16b7b0`. Flight masks0/6/10/9/15
and forced-ground controls cover both interleavings, both homogeneous classes
and grounded authored flyers. Frozen results retain candidate slots, exact
physical birth boundary, ordered members and native flags1000eh. The engine
matrix compares membership, relative birth order and formation bits0eh; it does
not claim equality of every intermediate physical flag. Inputs supply static
fine poses, disabled adaptive paths, clock, empty hierarchy and VM gate; only
Storm storage uses host adapters. The first two scratch runs used an incorrect
candidate count/stride; they are rejected and archived. Candidate identities are
twelve-byte rows, independently of the four-byte readiness array.

The public witnesses are the four frozen Payoff237 captures, including two
genuine Alt mixed selections. There is no new live capture or observer-free
control in this chunk. The original failing engine regression reports96 failures
in188 assertions. Additional regressions compare all ten native cases, queued
contexts, cold save/load, independent Stop and synchronous issued-order
conditions. The callback harness initially registered a player event before
creating its client edict, leaving a NULL event subject. The accepted version
establishes the player before registration and uses a synchronous trigger
condition. Issued-order event actions also dispatch synchronously in the current
engine. One verifier run started during compilation loaded the previous module
and is rejected. The first full checkpoint exposed an observer trigger retained across the test's
VM replacement. Its condition pointer referenced the freed VM; the core records
this in the second loop iteration. The test now destroys its listener before
replacing the VM. Earlier retail fixtures and authored data remain unchanged.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/verify_wc3_pathing_work238.py \
  --binary /path/to/retail/game.dll --report /tmp/work238-fresh.json
```

`Work238Evidence.java` saves eight functions, xrefs and495 instruction encodings;
`MapPathfinding.java` records the contract. Archive:
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-04.6/payoff238/`.
This is implementation12/12 in the focused-validation cadence. Mixed FLOAT,
callback category reassignment, persistent pending/busy canonical lifetime and
complete multi-class/ShiftAlt trajectories still keep GROUP-04.6 open.

The twelve-implementation checkpoint passes in aggregate:3,348 native tests and
16,859,438 assertions per Classic/TFT, plus1,249 Python checks and the remaining
engine suites. The initial full command failed on the listener lifetime above;
after test teardown was corrected, the failed Classic shard and three unrun TFT
shards passed. Four unchanged passing shards and the Python results were retained.
Production code was identical across both runs. The fresh strict corpus accepts
193 original/evidence oracles and474 total entries; focused Alt verification
passes six tests/566 assertions per edition. Validation cadence resets to0/12.

## Player singleton publication clears shared history (Payoff239)

GROUP-01.1's representative producer inventory is now complete. These producers
share the underlying Move entry but deliberately differ in request ownership:

| Producer | Request identity and limits | Producer flags |
|---|---|---|
| Selected player Move | One primary request for ordinary non-FLOAT candidates; Alt separates current flyers. Physical owners have at most twelve members. | Packet8 ordinarily,18h with Alt; Shift adds1. Singleton packets are0 even with Alt, and create an ordinary flags0 physical owner. |
| Independent JASS Move | Each native creates an independent point order/physical owner, rather than a selected cohort. The existing scene52 pair begins with two independent owners before later selected Shift acquisition. | `206f00` directly calls `Unit_AdmitOrder` with1/1; no selected packet or shared request attachment. |
| Captain AI shared point | Logical rosters24/25 prepare12+12/12+12+1 physical requests sharing one auxiliary parameter identity. | The recorded preparation passes policy1/bindShared1. `9d1040` sets canonical100h/800h/extra400h and clears persistent completion; it does not synthesize a player packet. |

The selected/mixed-flight witnesses are Payoff237/238. Independent JASS ownership
and subsequent natural activation retain the unchanged scene52 and Payoff170
fixtures. Captain limits, sharing and producer policy retain Payoff161's two
complete repeats and observer-free controls for each roster. This inventory does
not close the wider canonical lifetime, mixed-FLOAT, dynamic callback category
or complete multi-class trajectory requirements in GROUP-04.6.

Two new actual singleton Alt captures disprove the tempting one-row prepared
cohort shortcut: packetflags0, no `6b8c10` attachment or `89c7c0` retention, and
one ordinary physical owner with flags0. Preserve the engine's multi-selection
guard. Two further actual pair-to-singleton Alt repeats expose a different bug:
`6b93a0` overwrites Unit240/244 with invalid identity when the order has no
associated canonical request. Its stores at6b94e3/6b94ec precede append/admit.
The first pair stores one shared identity into both units; the later singleton
clears only the ordered unit. All330 public markers and four observer scopes
complete. These captures are read-only; there is no new observer-free control or
whole-motion comparison in this chunk. Generic capture metadata retains its
original MAP-02.2 label; the pinned probes/maps define this producer scope.

The engine's legacy player Move path now clears latest request history before
queue/current-order replacement. This is one O(1) write per admitted candidate,
not a group scan. The current physical owner remains independent of that history,
so Shift preserves current execution. No admission-result rollback is added:
retail writes history before dispatch. Independent JASS bypasses this net
publisher and retains history; changing the common Move entry would be wrong.
No save layout, earlier retail expectations or authored object data changed.

The failing-first regression reports ten stale-history failures in64 assertions.
Immediate/Shift, ordinary/Alt, independent script-style admission, unselected
peer preservation, queued activation and cold saves now pass. Six representative
producer regressions pass422,330 assertions per Classic/TFT. Saved Ghidra evidence
contains seven functions, xrefs and1,920 instruction encodings; portable comments
in `MapPathfinding.java` retain the player/script distinction.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/verify_wc3_pathing_work239.py \
  --binary /path/to/retail/game.dll --report /tmp/work239-fresh.json
```

Archive: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-01.1/payoff239/`.
This is implementation1/12 after checkpoint238. The earlier full checkpoint is
retained; focused original/corpus and neighboring owner checks validate this fix.

## Selected FLOAT candidates retain their own request (Payoff240)

The legacy selected Move fallback offset mixed FLOAT destinations and assigned
incorrect shared ownership. All bounded two-through-twelve selections now use
one class-aware producer. Preparation chooses primary, FLOAT or optional Alt
current-flight storage, then admits the supplied rows in order. Ordinary foot and
fly share primary; FLOAT uses the second request independently of Alt. Each
populated class retains its clicked point, independent latest history and queued
context, and publishes when its final supplied row becomes ready. The fixed
three-class arrays are stack-local and O(N); publication remains O(1). Removing
the UI's duplicate mask/class scan also removes that repeated interpretation.
Captain shared-parameter construction and singleton UI handling keep their
existing producer contracts. No save fields or format changed.

The fresh original kernel executes48 complete attachment/readiness/physical
publication scopes: FLOAT masks0/2/5/15, flight masks0/6/15, ordinary/Alt and
forced-ground controls. Factories, callbacks and publication run as original
code; only Storm storage uses adapters. Static fine poses, clock, empty hierarchy
and disabled adaptive paths are supplied inputs. Engine membership, common
points, formation bits and relative physical birth order match this supplied
readiness sequence. Synthetic FLOAT-plus-flight cases verify attachment priority,
not `6ba800`'s later association choice.

Four actual selected Footman/Destroyer/Gryphon/Footman retail repeats preserve328
public markers. Ordinary request affinity is0/1/0/0; Alt is0/1/2/0. The raw
physical bind order is retained without claiming complete UI admission parity:
retail sorts nine-word candidate rows through `6bcc40` before publication. In the
Alt witness primary binds first, then FLOAT, then special, despite attachment
iteration0/1/2/0. The engine's native matrix supplies readiness order explicitly;
it does not prove the complete sorted public callback sequence. That admission
sorting and dynamic category reassignment remain GROUP-04.6, along with busy
canonical lifetime and complete multi-class/ShiftAlt trajectories. No new
observer-free control or whole-motion comparison is claimed.

The initial engine scratch used stock hdes absent from the synthetic test archive
and is rejected as a fixture error. The corrected matrix supplies captured FLOAT
movement rows and fails344/1,146 assertions before the fix, then passes all1,146.
A separate map-authored Footman FLOAT clone verifies distinct immediate/queued
ownership, history, cold-load metadata, activation and independent Stop in100
assertions. This does not rewrite authored retail data or earlier expectations.
Eight focused producer/journey checks pass22,831 assertions per Classic/TFT.
Ten Ghidra functions, xrefs and942 instruction encodings are saved; portable
comments explicitly retain the admission-sort evidence limit.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/verify_wc3_pathing_work240.py \
  --binary /path/to/retail/game.dll --report /tmp/work240-fresh.json
```

Archive: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-04.6/payoff240/`.
This is implementation2/12 after checkpoint238. GROUP-04.6 remains open.

The neighboring selected/queued suites also pass24 tests/84,914 assertions per
Classic/TFT.67 focused Python/corpus checks pass. Fresh strict reports accept
Payoff238/239/240 against the final production/test modules; the corpus contains
195 original/evidence oracles and476 entries. A first neighbor command omitted
its wildcard and matched zero tests; it is rejected, not included in these totals.
Only this validation paragraph and its corpus pins were added after those runs.

## Selected admission priority preserves attachment membership (Payoff241)

The selected point producer attaches candidates first (`6b8c10`), builds all
nine-word admission rows (`6ba800`), sorts them (`6bcc40`), then publishes orders
(`6b93a0`). Attachment iteration and callback admission are separate sequences.
The comparator priorities are:

| Priority | Native input | Direction |
|---|---|---|
| 1 | Unit198 separation-suppression depth `<1` | Unsuppressed first |
| 2 | Row4 validation kind | Descending |
| 3 | Row2 current plus pending user heads | Ascending |
| 4 | Row1 heads matching this order ID | Ascending |
| 5 | Row5 primary selection subgroup membership | Descending |
| 6 | Row3 unsigned point score | Ascending |
| 7 | Unit canonical identity at+c | Ascending |

Subtraction returns signed32 results with native wrap. Unit198 is **not** an
active-order count. `687a60` follows the current/pending user-order identity chain;
order ID zero counts all heads. Engine `G_CountUnitOrders` owns this accounting,
including suspended heads already held in the FIFO.

`687b30` first takes the unsigned minimum of authored ability-vtable220 queries;
ffffffff means no override. Its fallback queries the mover's predicted world
position through `058900`, sets both Z terms to zero, evaluates
`((dx*dx+dy*dy)+0)*0.1` using software scalar word3dcccccd, then converts with
wrapped integer semantics. Engine ability owners receive the typed
`A_POINT_ORDER_PRIORITY` minimum query; Move owns the numerical fallback.
Specific retail ability overrides remain to be mapped and implemented.

`2908c0` takes the **selection manager in ECX and Unit on the stack**, returning
with RET4. It queries manager1c0's primary subgroup through290820. The earlier
inferred fastcall Unit signature was wrong. Ghidra now retains the corrected
signature, the36-byte `WC3NetPointCandidateRow` structure, ten annotated function
bodies/xrefs and1,163 checked instruction encodings; MapPathfinding.java retains
portable mappings and these evidence limits.

Four read-only captures retain328 complete public markers and six actual UI
packets: one ordinary idle, one Alt idle, and two ordinary-then-Alt mixed repeats.
For Footman/Destroyer/Gryphon/Footman, the initial publication is0,3,1,2. After
stopping the Footmen while the Destroyer retains an active head, publication is
0,3,2,1. The mixed repeats have different current-clock scores; each is retained
individually. All36 ordered public candidate pairs execute through the original
comparator and agree. A separate198-pair native oracle covers each key and
signed-wrap boundaries. There is no new observer-free control.

The engine captures keys before callbacks and sorts only UI admission. Script
and Captain producers retain their explicit row order. Before each class becomes
ready, the engine gathers its surviving members in original attachment order,
rechecking generation after nested replacement/removal callbacks. Fixed stack
storage serves at most twelve candidates: sorting needs at most66 comparisons,
and the bounded member gather allocates nothing. The physical primary cohort
therefore remains0,2,3 for ordinary selection, or0,3 for Alt, independently of
callback order. Focused rawcode grouping uses the existing selection owner;
wider Hero/subgroup classification and canonical identity reuse remain open.

The public regression fails7/32 assertions before the fix. The expanded fixture
supplies actual retail query poses and verifies exact numerical scores, idle or
active heads, sorted callbacks, physical publication and attachment membership;
it does not claim complete scene construction or mixed movement parity. Its two
tests pass316 assertions. Existing238/240 native readiness fixtures are unchanged:
their tests now invoke the explicit producer-row boundary which those kernels
supply, instead of imposing that readiness order on the sorting UI producer.
This corrects the harness scope, preserving every original kernel expectation.

Selected and queued neighboring suites pass26 tests/85,230 assertions per
Classic/TFT, including unchanged public movement journeys, Shift, interruption,
nested replacement and save/load. Fourteen evidence-mutation checks pass. Network
and save contracts are unchanged. This is implementation3/12 after checkpoint238;
full-suite validation is reserved for the agreed batch checkpoint.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/verify_wc3_pathing_work241.py \
  --binary /path/to/retail/game.dll --report /tmp/work241-fresh.json
```

Archive: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-04.6/payoff241/`.
GROUP-04.6 remains open for broader canonical lifetime, dynamic category changes,
ability-specific score priorities and full multi-class queued trajectories.

Final focused validation also passes150 neighboring research checks and37 corpus
checks. Fresh strict reports accept unchanged238/239/240 plus241 against the final
modules. The staged corpus has196 executable contracts and477 entries; its fixture
inventory passes. Only this validation paragraph and corresponding corpus hashes
were updated after those strict runs.

## Pending user heads retain their matching identities (Payoff242)

`Unit_CountMatchingUserOrders` (`687a60`) counts pending commands as well as the
active head. Two new read-only selected Shift/replacement captures each retain82
complete public markers. Four units initially have one Move head. The first actual
UI packet has flags9 (Shift), and each candidate's row1/row2 is1/1. Before the next
flags8 replacement packet, every candidate has2/2: its current Move and pending
Move. Unit1b4 agrees with row2. All24 ordered candidate pairs execute through the
original comparator. Input delivery clocks and numerical scores differ across the
two captures; each complete packet is frozen independently. There is no new
observer-free control or complete-trajectory claim.

The engine's queue admission had retained zero in `unitOrder_t.order_id` for
ordinary pending commands. The241 priority query consequently counted only the
current head when matching Move. `G_QueueUnitOrder` now resolves the public ID once
when an ordinary command enters the FIFO, while preserving explicit payloads
such as construction rawcodes and private order aliases. Direct ability-owned
entries may still use the existing optional zero-ID representation; the query
resolves their retained name. Ordinary pending entries therefore require only an
integer comparison during subsequent queries. The all-head query now uses the
cached ring count and current-head predicate in O(1), rather than walking the FIFO.
Matching-head queries are O(queue length), within the verified bounded selection
producer; there is no whole-entity or whole-cohort scan.

The production regression issues an immediate Move, selects two units, appends a
real Shift Move, queues Patrol, saves/loads, checks optional-ID entries and replaces
the commands through UI Move. The valid pre-fix run fails12/34 assertions; the
expanded check passes36/36. An earlier scratch run also dereferenced its synthetic
client connection after cold load; its core is retained as a rejected fixture
failure. Rebinding that connection/selection before the final UI operation fixes
the harness without changing saved authoritative state.

Focused Classic/TFT queue, interruption, nested callback and score regressions
pass49 tests/15,843 assertions per edition. Nine evidence-mutation checks pass.
Five Ghidra function bodies/xrefs and595 instruction encodings accompany saved
comments and MapPathfinding.java updates. The network and save layouts are
unchanged; zero remains a supported optional identity representation. ORDER-02.2
and02.3 retain broader internal-task/control ownership work; no task is closed by
this narrower fix. This is implementation4/12 after checkpoint238.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/verify_wc3_pathing_work242.py \
  --binary /path/to/retail/game.dll --report /tmp/work242-fresh.json
```

Archive: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ORDER-02.2/payoff242/`.

The neighboring selected suite passes24 tests/84,543 assertions per Classic/TFT.
Final research checks pass159 tests plus37 corpus checks. Fresh strict241/242
reports pass against the final modules, and the staged197-contract/478-entry
fixture inventory passes. Only this paragraph and its corpus hashes were updated
after those strict runs.

## Idle Shift recipients retain the prepared packet policy

Payoff243 distinguishes synchronous packet admission from later queued activation.
`89cd10` resolves the prepared wrapper's canonical identity, marks its mover
ready through `16d850`, then invokes the wrapper's publication callback.
`6b93a0` can reach this path immediately when appending a Shift order to an
idle unit. A busy unit remains unready while its order waits.

Four actual mixed Footman/Destroyer/Gryphon/Footman UI captures retain one busy
first Footman and three idle recipients. Sorted admission is **3,1,2,0**.
Two Alt+Shift repeats preserve policy **0xe** and publish FLOAT, special flight,
then the idle primary Footman. The primary's first readiness attempt returns
pending; after admission of the busy candidate, packet completion publishes
only the ready primary row. Two ordinary Shift repeats publish FLOAT and then
one primary cohort whose attachment order is Gryphon, second Footman. Recursive
bind records describe the same two-member owner, not two physical owners.

In all four captures the first Footman's later activation creates a **fresh
policy-zero request** at probe tick81. Its earlier idle peers have finished by
then. Carrying saved Alt bits into that reconstructed request would be incorrect.
The actual input clocks differ across repeats; retain each separately.

The engine now prepares class identities before callbacks and keeps a scoped
recipient/context/point binding while an idle FIFO head starts synchronously.
It creates a physical owner lazily on the first such start, gathers surviving
rows in attachment order, and publishes at the class's last admission. A wholly
busy class allocates no physical owner. Nested packets save/restore this binding;
later activation has none and continues through the existing spatial reconstruction.
This avoids per-idle-unit cohort queries and premature singleton construction.
There is no saved or network layout change: the temporary packet binding expires
before native return, while published physical flags and pending identities use
existing save fields.

Validation includes a failing-first public mixed-selection regression (8 failures
out of83 assertions), cold save/load of published flags and pending identities,
and later activation without inherited Alt policy. Preserve the existing all-busy
queue test's allocation expectation; an initial implementation unnecessarily
allocated an empty physical owner and was corrected rather than changing the test.
The evidence verifier also checks four complete242-marker captures,24 fresh
original comparator pairs, and391 original instruction guards from six bodies.
It claims readiness/binding/policy parity, not complete trajectory parity.

Reproduce the guarded evidence and focused engine checks:

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_work243.py \
  --binary /run/media/lofcz/ssd_external/Games/w3-research2/game.dll \
  --report /tmp/work243-fresh.json
```

Archive: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-04.6/payoff243/`.
Broader canonical lifetime, target variants and arbitrary callback compositions
remain GROUP-04.6; this result does not close that task.

Final validation:50 focused tests/16,234 assertions and26 selected-order
tests/84,649 assertions per edition;45 neighboring Python checks plus37 corpus
checks; fresh strict Work240–243 contracts pass against the final modules.
The inventory contains198 executable oracles/479 entries. This is implementation
5/12 after full checkpoint238; the full suite is reserved for the agreed batch
checkpoint. Neither the frame budget nor all remaining retail gaps are claimed
complete by this change.
