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
