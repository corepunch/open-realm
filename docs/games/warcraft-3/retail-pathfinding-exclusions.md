# Published rectangles own coarse request exclusion

Payoff130 integrates the first production correction from the
[MAP-04.1](retail-pathfinding-handoffs/MAP-04.1/HANDOFF.md) and
[MAP-04.2](retail-pathfinding-handoffs/MAP-04.2/HANDOFF.md) research handoffs.
Payoff146 below closes MAP-04.1 with the complete supplied spatial matrix.
MAP-04.2 retains the remaining exit/recovery integration; group owners keep
their separate existing scope.

## Contract and engine change

Original `Path_RequestAcceleratedRoute` (`166c30`) clears the rectangles of any
nonnull self and target spatial objects, searches, then rebuilds self and target
in that same order. Object collision eligibility is not a prerequisite. A
category-zero flyer still owns a published fine rectangle. The function reads
the current objects and their rectangles again when rebuilding.

`15d360` clips a half-open fine rectangle once, then visits each hierarchy level
from `floor(min/scale)` through `floor(max/scale)`, including the upper edge.
Rebuilding reads current fine cells; it does not restore saved class bytes.
Consequently, a pending terrain edit inside this rounded coverage becomes
published during restoration, while edits elsewhere remain pending. The actual
retail target-edit repeats publish fine `(31,35)` to base `(15,17)` and its three
parents; six other edited cells stay unpublished.

The previous engine adapter filtered out flying targets and reconstructed the
rectangle from live display position and collision radius. The new
`move_acc_object_rectangle` consumes the active published fine rectangle.
`move_build_acc_route` owns the complete clear/search/rebuild sequence shared by
member and group requests. Admission occurs before this scope; no search result
can return to its caller before restoration. Dirty object owners synchronize
once at entry, retaining their established publication policy. An uncommitted
display sample does not independently move the rectangle.

This removes repeated world-to-fine conversion and footprint classification at
up to four rectangle observation points per admitted coarse search. It adds no map
scan: unchanged dirty membership checks remain constant work. No throughput
speedup or broader performance target acceptance is inferred from that change.

## Evidence and regression coverage

The new production regression fails **12 of 64 assertions** against the previous
engine. Four scenarios combine ground/flight with unchanged/shifted display
position. Each checks fine-only terrain edits, exact covered base and parent
publication, an uncovered edit and unchanged object membership.

The stage observer checks all **1,360 native class bytes at each of five
boundaries**, across all four lanes, against the unchanged original MAP-04.1
snapshots. It covers self-only and aliased self/target requests with both ground
and flying objects. Separate production checks cover exact, partial,
interval-denied and FIFO-denied coarse exits; denied requests never enter the
scope and every result leaves the whole hierarchy payload restored.

`verify_wc3_pathing_exclusions.py` independently reproduces:

- 45 complete original coarse and 45 fine requests, with 45 observer-free controls;
- 18 pending terrain edit requests and 8 exact/partial/pre-acquire denial exits;
- a fresh instruction-level inventory of 12 exclusion sites, compared in full;
- three complete retail captures: 9 fine and 27 coarse request scopes, all balanced;
- 322 marker records in each target-edit repeat, compared in full;
- the retained failed first observer attempt, explicitly rejected as evidence.

The compressed input bundle retains all four raw capture files, the full heavier
target-overlap reference, the original Ghidra function-start inventory and the
complete static report. The complete combined frozen report reconstructs
byte-for-byte (`94e1fd49…fa3c18`); no failed capture, late movement record or
unresolved callback was dropped. The reference observer differs from the new
observer: this is not an observer-free live target-overlap run. The three original
request/map functions were annotated, saved and read back in Ghidra; their actual
signatures are recorded without claiming a new prototype change.

The large original oracle includes hierarchy-participating region objects and
pre-existing exclusion counters. Reproducing those cases does **not** establish
their integration into the current engine adapter. Similarly, fresh static
inventory retains unresolved virtual calls in recovery and portal scopes.
Those limitations kept MAP-04.1/02 open at Payoff130. The supplied hierarchy
and outer-counter matrix is integrated at Payoff146 below; recovery callers
remain a separate requirement.

## Verification commands

```sh
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 build/bin/openwarcraft3-tests \
  -data build/tests +dedicated 1 +test 'pathfinding.coarse_*'
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_exclusions.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report /tmp/exclusions-fresh.json
python3 -m unittest discover -s tests -p test_wc3_pathing_exclusions.py
```

Use `-tft` for the second engine data mode. Frozen stage cells reside in
`games/warcraft-3/game/tests/retail_coarse_scopes.h`; Python checks compare every
value with the original report and reject altered inputs, missing hashes and
escaping filenames. Full-suite validation follows the authorized approximately
twelve-commit cadence. Logs are under `/GitHub/wc3-analysis/runtime/payoff130/`.

Focused Classic and TFT each pass **564 tests / 7,376,090 assertions**: all
23 pathfinding tests, 349 movement tests, 191 save/load tests and the actual map
reload regression. The three new coarse regressions pass 108,940 assertions.
Twenty-five Python evidence/corpus checks pass, with 362 corpus entries and
471 pinned inputs validated from the isolated staged tree. A fresh strict
`oracle-exclusions` entry passes. The first denial fixture used the ordinary
FIFO; it was corrected to exhaust the target policy selected by the real
scheduler. No production scheduler policy was changed to accommodate the test.

See also the [engine integration ledger](retail-pathfinding-engine.md),
[search contracts](retail-pathfinding-search.md) and
[remaining research queue](retail-pathfinding-todo.md).

## Complete fine and coarse scope matrix (Payoff146)

`166e90` captures pooled self and target identities, increments both occupancy
words, builds the route, then decrements target and self. Both operations run
when the pointers alias. The engine previously overlaid a low flag in its
per-cell adapter: it could hide ordinary records but represented neither the
counter transition nor hierarchy-participating self/target records. Queries
with a null self additionally discarded all ordinary target/bystander occupancy.

The fine builder now holds the actual record counters across its synchronous
search. The per-cell adapter consumes them without adding another exclusion.
Records are address-stable pool allocations; metadata/link growth does not
invalidate the captured pointers. Restoration runs before every post-search
return. There are no gameplay callbacks in the closed search graph; allocation
failure terminates rather than exposing a half-restored scope. No edict layout
or save format changes: these counters are temporary derived search state.
Other endpoint/segment scopes retain their existing query policy and remain
separately covered by the broader lifecycle tasks.

The original oracle now optionally exports complete fine windows rather than
only differences. Its existing45-request and18-edit frozen payloads are
unchanged. The compressed full-stage export has uncompressed SHA256
`3dd2f2d63bc77280d6c79bed0d2ce842a28ca3904ffd22eb98df0bd2aa4d75d6`.
The exporter copies original values into `retail_exclusion_stages.h` and checks
every fine difference against the older frozen report; it does not calculate
expected occupancy policy.

Two production-entry regressions run all45 combinations: five self/target
roles, three counter regimes and three raw cell encounter orders. Fine checks
compare all324 cells and three counter words at six boundaries, then every
route word and the obstruction-dependent initial index. Coarse checks compare
all1360 classification bytes at five boundaries across all four lanes, unchanged
counters and every resulting coarse point. Fine fails2,511 assertions before
the fix; coarse already passes and receives the broader regression coverage.
The complete original scope oracle, retained Frida captures/controls and
instruction-level exit inventory also reconstruct unchanged.

These fixtures deliberately supply the original asymmetric rectangles,
category06 and held outer counters. They establish request-scope behavior,
not additional public widget-construction reachability. The earlier actual
public flight/pending-terrain regressions remain in the suite. This closes
MAP-04.1's overlapping exclusion matrix; MAP-04.2's wider recovery/exit work
remains open. Saved Ghidra readback covers166e90,1489a0 and166c30 with no unsaved
changes and matching `MapPathfinding.java` notes.

Logs and failing-first builds: `/GitHub/wc3-analysis/runtime/payoff146/`.
Full-repository validation follows the authorized twelve-commit cadence;
this is implementation commit11 after the Payoff135 checkpoint.

Focused validation passes **1,576 test executions /24,174,590 assertions**:
all25 pathfinding and193 save tests in debug/release Classic/TFT, both complete
351-test release movement suites, and debug public target-reinsertion checks.
Production/test builds emit no compiler warnings. Forty-seven distinct Python
checks pass; the corpus inventory count is corrected from132 to134, and both
region/cell probe builds receive the repository include root required by their
shared vector header. The failed checks are retained in the logs.

The exact staged corpus validates369 entries and642 pins; its fresh strict
`oracle-exclusions` run passes. No new live game run was needed: original code
was re-executed and the complete existing Frida captures/control markers were
revalidated. Ghidra readback is retained separately from the Payoff130 snapshot
in `retail-exclusion-scope-ghidra-1.27.json`.


## Fine consumers hold captured self through their queries (Payoff148)

Endpoint admission (`16ee80`), visible-waypoint selection (`167bf0`) and
next-step blocker collection (`166140`) capture the self spatial record and
increment its actual occupancy word. They restore that captured record on
success and rejection; an existing outer depth survives. The path target stays
published and eligible. Null self does not disable terrain or other-object
queries. Endpoint admission also temporarily selects endpoint mode and restores
it; waypoint and collector retain their inherited consumer mode.

The engine replaces three query-local boolean overlays with counted holds of
address-stable pooled records. It also removes the collector's incorrect
null-self early return. Reads of the production raw-cell predicate now observe
the same counter as nested consumers. Each hold/release is constant work, with
no allocation, map scan, new saved field or change to authoritative membership.
No throughput or frame-rate target acceptance is inferred.

The complete original-code oracle executes all three functions after their
registered scalar initializers. Its 96 supplied cases combine four footprint
classes, clear/blocked terrain, outer depths zero/one and present/absent self.
Every call is repeated without observers, comparing return values and complete
fine/map/self prefixes, including stamps and restored mode/counter. Two complete
exports match byte-for-byte. The plain frozen export has SHA256
`f3393e78cf64f85fcc8551d5cf9e3b604f9d78f42fb095ac2151b13fcfaa800e`.
`export_exclusion_consumers.py` copies native results into the engine header;
it does not calculate expected route or admission policy.

The initial 48-case engine scope regression fails 72 assertions with boolean
overlays. The expanded native matrix exposes another 20 failures from null-self
collection. The final test compares actual held counters, raw-cell visibility,
endpoint outcomes, selected indices and blocker counts for all 96 cases.
Additional fine-request checks cover exact/partial results, interval denial and
FIFO denial: every exit restores counters and the complete hierarchy payload;
pre-acquire denials never enter the scope. The first exact-exit fixture placed
its target on the start perimeter and therefore correctly triggered target
arrival; the corrected target is away from both requested routes. No production
routing policy was changed to accommodate that fixture.

Fresh validation also reconstructs the unchanged 45-request/18-edit exports,
eight original request exits, all 12 static scope inventories and the complete
retained Frida captures and repeat markers described above. There is no new
live capture claim. Saved Ghidra notes and signatures are retained in
`retail-exclusion-consumer-ghidra-1.27.json` and mirrored in
`MapPathfinding.java`; the endpoint now has its instruction-verified thiscall
ECX/stack+4/RET4 signature and unsigned footprint result.

This advances MAP-04.2 without closing it or adding child tasks. Original
`166140` holds self through moving-peer resolution as well as collection; this
chunk certifies the collection adapter and non-mover/terrain native controls.
The broader resolver lifecycle, Stop/embedded recovery, portal and group
publication scopes retain their existing integration obligations. Supplied
geometry and outer depths are not public producer reachability evidence.

Logs, red builds and repeat exports: `/GitHub/wc3-analysis/runtime/payoff148/`.
Full validation remains on the approximately twelve-implementation-commit
cadence; Payoff147 was the last full checkpoint.

Completed validation passes **1,154 engine test executions /17,282,606 assertions**.
Classic/TFT pathfinding, repulsion policy and save/load contribute452 executions
and6,899,192 assertions. Both additional movement sweeps pass351 tests and
5,191,707 assertions each; the previously pending TFT sweep finished successfully.
The initial `wc3_move.*` filter selected zero tests; those runs are explicitly
excluded and were replaced with the actual `wc3_movement.*` suite. Seven Python
evidence tests and22 staged corpus contract tests pass; the exact staged corpus
validates369 entries/652 pins and its fresh strict `oracle-exclusions` entry
passes. Production/test builds are warning-free. The first staged inventory
attempts rejected stale pins; only the owned changed files were repinned.

## Collection and yielding share one captured scope (Payoff202)

The remaining resolver composition identified by MAP-04.2 and Payoff148 is
implemented. Original `166140` captures `path+a0`, increments its spatial
record at `166265`, collects entering-strip tokens, calls the complete
`168360` resolver at `1662cc`, and only then decrements the captured record
at `1662dd`. Its sole `RET8` follows restoration. An early requester-yield
return belongs to the resolver; it must not release the enclosing hold.

The engine previously released that hold before `S_ResolveMoveBlockers`.
`G_ResolveUnitMoveStepBlockers` now owns both phases, and normal point movement
uses it. The collection-only adapter remains available for independent token
queries and its existing regressions. Ordered tokens, the32-entry cap, prior
wait preservation, blocker identity clearing and peer wait writes are unchanged.
No allocation, extra collection pass, map scan, saved field or new task is added.
This is a scope-fidelity correction, not a measured performance improvement.

The original collector oracle now has an optional, separate48-case export.
Complete original calls cover clear cells, terrain tokens, a moving peer and
self records, outer depths0/1/7, same/different/no groups and Alt speed policy.
Read-only instruction hooks observe the counter before acquisition, inside
`168360`, before release and at the final return. Original instructions and
handle resolution execute unchanged. Two corrected exports agree in full;
`retail_blocker_scope202.h` copies their actual results, with no expected-policy
model. Existing frozen exclusion, collector and trajectory expectations are
unchanged.

The production regression supplies groups through actual point-order admission,
compares all48 final token counts, wait deadlines and blocker identities, and
observes the authoritative counter inside the real resolver. With corrected
oracle inputs it fails exactly48 assertions before the fix, one scope violation
per case; all other results already match. A second regression drives ordinary
`unit_changeangle` under an existing outer hold, proving that gameplay uses the
combined owner. Both pass in Classic and TFT.

The fresh exclusion verifier also reconstructs all12 static scope inventories,
45 fine/coarse requests,18 pending edit cases,8 request exits,96 consumer calls,
three complete archived Frida captures and322 repeated edit markers. Failed
archived observers remain rejected. There is no new live capture claim.
The original full collector verifier retains3072 raw collections,756 composed
next steps,756 fine advances and72 terminal-waypoint cases. Saved Ghidra
readback for166140 has no pending changes and its note is mirrored in
`MapPathfinding.java`.

MAP-04.2 remains open for its broader scope/exit audit. These supplied depths
are not new public suppression producers, and this chunk does not certify
all unresolved recovery notification callbacks. Logs and both red/green builds
are in `/GitHub/wc3-analysis/runtime/payoff202/`. The first new oracle draft
inherited the preceding terminal test's goal; it correctly visited a different
entering cell. Those superseded drafts are retained there and rejected. Resetting
the explicit source/goal before the new matrix resolves the setup error; no
previously valid frozen expectation was overwritten.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_blockers.py --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report /tmp/blocker202-fresh.json --scope-reference tools/ghidra/fixtures/retail-blocker-scope202-1.27.json --scope-header games/warcraft-3/game/tests/retail_blocker_scope202.h
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'pathfinding.blocker202*'
```

Final validation passes31 pathfinding tests /3,423,013 assertions and434 movement
tests /6,250,417 assertions in each of Classic and TFT. The overlapping recovery
checks are not counted twice. Initial middle-wildcard filters selected zero tests
and are excluded; the complete movement sweeps replace them. The45 Python
contract/export tests, fresh strict blocker-scope oracle and439-entry/1156-pin
staged corpus also pass. Production/test builds are warning-free. Full repository
validation remains on the agreed cadence; this is implementation commit8/12
since the full merge checkpoint, with Payoff200 documentation-only.


## Fresh waypoint selection shares the counted scope (Payoff219)

The first visible-waypoint selection immediately after a fine search now uses
exactly the same captured self-record scope as retained-route advancement.
Previously, the search released its self/target counters and the fresh selector
fell back to the old query-local self overlay. That overlay did not implement
the authoritative spatial counter contract established by original `167bf0`.

`move_select_visible_waypoint` captures and increments the actual self record,
performs selection, then decrements that captured record and restores the
caller's prior counted-scope state. The target remains live. Null self acquires
nothing, and an existing outer hold survives. Both callers use this one helper.
It introduces no allocation, world scan, persistent field or save-format change.

A new production regression builds fresh fine routes before selection across
all32 waypoint rows of the existing96-case native consumer matrix. It observes
the raw record counter and cell predicate at entry, while held and after release,
covering all four footprint classes, clear/blocked terrain, null self and outer
depths0/1. Against the previous implementation it fails32 of120 assertions,
one missing scope observation per row. Expected clear-route goal words remain
unchanged; an absent self with no outer hold remains a genuine blocker.

The unmodified original DLL was freshly executed for all96 consumer cases plus
96 observer-free controls. Its exported consumer fixture is byte-identical to
`retail-exclusion-consumers-1.27.json`, SHA256
`f3393e78cf64f85fcc8551d5cf9e3b604f9d78f42fb095ac2151b13fcfaa800e`.
No frozen expected value was changed. Existing saved Ghidra evidence and the
original scope captures establish the contract; this chunk claims no new live
capture or public reachability result. Ghidra's167bf0 note and MapPathfinding.java
record the engine integration. MAP-04.2 remains open for its wider scope audit.

Validation reports are archived under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-04.2/payoff219/`.
This is the fifth focused implementation commit after Payoff214's full checkpoint.


Focused Classic and TFT validation each passes393 tests and3,913,526 assertions:
32 spatial tests,96 routing tests,59 target/Follow/resource/route cases and206
save cases. The new test accounts for536 passing assertions. Eight evidence
mutation tests and37 corpus tests pass, and a fresh strict `oracle-exclusions`
report verifies the original requests, consumers, exits and archived captures.
Production and test targets build without warnings. An optional broad movement
run timed out at300 seconds and has no complete report; it is explicitly not
counted as validation. Broad validation remains on the authorized batch cadence.


## Widget bounds and captain distance scopes (Payoff230)

Retail creates a widget's region identities, rasterizes its path texture, then
publishes a separate rectangle through `22f1d0 → 0642f0 → 05ee40 → 14e8d0`.
That rectangle covers the full authored texture, including empty margins.
Quarter turns exchange width and height; each half extent is dimension times
16 in the software scalar arithmetic. The collection producer subtracts map
origin, divides by32, floors both ends, and adds one to each maximum. It writes
all collection records without clipping or emitting links. Raster sample
centres and the occupied-pixel hull are different bounds.

The engine previously left every region rectangle at its allocation sentinel.
`S_PublishMoveRegions` now prepares the rectangle after rasterization.
`S_LoadMoveRegions` reconstructs it from already saved dimensions, centre and
turn, preserving the existing save format and sparse membership history.

Captain reachability (`9d8c70 → 0594f0 → 059590`) additionally clears the first
region of the target widget and source widget after clearing target, auxiliary
source and predicted source unit rectangles. It restores those three unit
rectangles, then both widget rectangles in the same order. Aliases retain both
slots. Restoration re-reads published bounds and rebuilds current fine cells;
a pending edit at the rounded upper edge publishes while unrelated edits stay
pending. The engine now performs this complete scope for success and failure.
No allocation, world conversion or entity scan is added to the query.

Two read-only retail captures and an observer-free control retain all193 public
markers from the unchanged cliff/water/LT06 map. Both captures report the same
three widget rectangles `(minY12,minX16,maxY31,maxX49)`. The original bounds
producer executes96 combinations of dimensions, rotation, fractional/negative
positions and map origins without stubs. Eight complete original distance
queries plus four aliased queries provide all1360 hierarchy class bytes before
and after restoration. Engine comparisons check every lane at every level.
The bounds regression fails2912/3264 assertions before the fix; the corrected
captain regression fails8/38 before its missing widget scope is implemented.

New frozen evidence is `retail-work230-1.27.json` and its compressed capture
bundle. `verify_wc3_pathing_work230.py` executes fresh original code, checks all
935 retained instruction bytes and runs the three bounds/query/save regressions
in Classic and TFT (90,434 assertions each). Seven rejection tests detect
altered bounds, collection members, controls, ownership and query exits.
Exploratory runs missing scalar startup or the synthetic region flag remain in
the archive as rejected setup attempts, outside accepted expectations. Existing
retail fixtures were not rewritten.

Ghidra persists four named functions, their exact operand storage, the12-byte
widget collection and16-byte texture prefixes, plus the distance-scope note.
The reproducible `Work230Evidence.java` updates those records without replacing
other analysts' refined types. The global type mapper deliberately refuses the
older full schema when it would discard newer `WC3MoverPrefix` fields; the
narrow script preserves them. Archive:
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-04.2/payoff230/`.
MAP-04.2 remains open for group-publication and excluded-point scope integration.

Focused Classic and TFT each pass372 tests /3,554,684 assertions across
pathfinding, fine spatial records, captain AI, saves and the two affected
Stop/embedded-recovery cases. The broad `wc3_movement.*` attempt exceeded its
180-second limit and is retained as incomplete, not counted as validation.
The strict corpus entry also passes from the isolated staged tree. All44 Python
bounds/corpus rejection checks pass; the inventory now has466 entries,
185 executable oracles and1401 pinned inputs. Full-suite cadence advances to4/12.

## Public point queries retain raw spatial history (Payoff231)

`IsTerrainPathable → 04e090 → 04df50 → 149320 → 1489a0` uses a high-only
terrain mask. Ordinary object categories cannot change its boolean, but the
original still traverses and stamps active raw identities before rejecting their
low-mask intersection. Duplicate links observe one identity once. Terrain
rejection and an out-of-map cell return before that traversal. The previous
engine native read only the terrain byte and lost this saved query history.

`G_MovePointIsBlocked` now owns the shared raw-cell call, direct software
world/fine transform and optional counted self exclusion. Its query-local
endpoint mode includes moving objects without changing the caller's mode.
The excluded bridge is acquired before the query and released before return;
retail re-resolves its canonical identity at both boundaries. There is no
callback, identity mutation, map writer or recoverable failure in this query
closure. This does not justify collapsing scopes in other consumers.

The regression first failed its two stamp assertions against the old engine.
The native test now also saves, perturbs and reloads the resulting query/object
stamps. A separate production adapter test matches600 complete original-code
executions across full/high-only/zero masks, fractional/negative/outside cells,
terrain rejection, moving/region flags, nested counts, exclusions and duplicate
links. Only the existing Storm memory imports are storage adapters; the query,
canonical registry and exclusion functions execute unchanged retail instructions.

Two read-only Frida repeats each capture48 synchronous public queries inside
explicit script windows. All eight pathing types cover occupied, fractional,
outside, blocked, cleared and removed cases. An unhooked control preserves the
same14 public markers. Intervening background simulation changes absolute query
counters; raw captures retain those values, and comparison checks each query's
exact counter delta, result, mask, coordinates and mode restoration. No previous
retail expectation is rewritten. Failed exploratory map/capture attempts are
retained separately and excluded from acceptance.

Ghidra saves corrected `149320` boolean and `05bd30` spatial-pointer return
signatures, exact parameter storage, scope comments and xrefs through
`Work231Evidence.java`. The fixture pins468 decoded original instructions.
The strict fresh verifier executes all600 cases again and runs the native,
adapter and terrain/save regressions in both data schemas. Archive:
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-04.2/payoff231/`.

MAP-04.2 remains open for persistent group-publication ownership and the outer
Stop/recovery scopes. Persistent fine target/completion fields outside this
call are not certified here. No performance target is accepted. Focused
Classic/TFT runs cover229 tests per schema; full-suite cadence advances to5/12.

## Bridge Stop owns cancellation and embedded recovery (Payoff247)

Original `05ca50` acquires the bridge's current fine object before `171340` and
re-resolves it for release after that function returns. `171340` integrates and
zeros velocity, detaches the group, runs optional `170080` recovery, then
invalidates the retained path. Inner recovery captures its own record and holds
that additional exclusion through placement and position publication.

`S_StopUnitMovementWithRecovery` now owns this sequence in Move. Public Stop
completes it before goal cleanup and stand; replacement point Move completes it
before admitting its new task. Previously Stop installed stand before recovering
the position, and neither caller held the bridge exclusion. Existing ordinary
move-leave ordering remains unchanged. The change adds no persistent state,
allocation, map scan or save-format change.

The new production regression first fails208 of1080 assertions against the old
engine. It drives public Stop and replacement Move through clear, admitted and
exhausted placement, four footprint classes and existing outer depths0/1. It
checks every inner boundary, final coordinates and that stand observes completed
recovery with the bridge counter restored. The previous Payoff183 regression now
calls inner recovery directly: its original inner-only counter expectations are
unchanged. No existing retail expected fixture is rewritten.

The fresh native kernel executes complete original `05ca50`, `171340`, `170080`,
the canonical registry, fine footprint/placement and position publication in32
cases, with32 matching observer-free controls and an identical repeat. Only
Storm storage and a flat terrain support-level lookup are supplied; the original
`654060` support predicate executes unchanged. The null-callback branch is a
kernel case, not a demonstrated public pathing-disable producer.

Two read-only public retail captures each contain8 bridge scopes,3 placement
searches and1 admitted commit. All79 scope events agree exactly. An uninstrumented
control preserves all14 public markers, including blocked Stop moving from
`656,656` to `624,592`. Music selection and `PreloadEnd` wall time differ and remain
in the raw captures. Retail retains an independent unit exclusion and two bridge
calls per public Stop: counters rise from1 to2 at bridge entry and3 during inner
footprint/placement, then return to1. This chunk certifies the bridge lifetime,
not complete public notification or duplicate-call chronology.

Ghidra saves the scope/caller distinction and xrefs through
`Work247Evidence.java`; the new fixture pins312 original instructions and all
capture/builder sources. MAP-04.2 remains open for canonical group-publication
ownership, the independent unit scope and remaining notification reentrancy.
The archive is `research/MAP-04.2/payoff247/` under the pathfinding reports root.

Focused Classic and TFT each pass86 tests and1,173,285 assertions, including the
new1080-assertion regression, inner recovery, public Stop/save cases, all selected
queued journeys, target-owner transfer, captain GoHome, order lifecycle and
interrupt regressions. Six evidence mutation checks and37 corpus checks pass.
The production/test targets build; the pre-existing JASS parser unused-value
warning remains. The original kernel repeat and all observer-free finals match.

## Selected canonical requests hold pending attachments (Payoff248)

`169c50` acquires every live canonical candidate's fine counter, including a
candidate with an unresolved readiness slot. If any resolved candidate is still
pending, `16bcf0` returns busy only after `169d60` restores those counters.
Successful partition consumes readiness slots while retaining the attachment
identities needed for release. Release re-resolves each current mover fine
object; the physical cohort's current member order does not own this scope.

The selected-point engine producer now has separate canonical candidate and
readiness storage, with physical groups allocated only when publication succeeds.
Both busy and successful publication hold all attached fine records. Two real
Alt+Shift repeats preserve24 scope boundaries each, including a pending moving
member with the original motion bit; four complete native kernel cases preserve
independent depths0/3 and match observer-free controls. Engine tests compare all
four scope boundaries per readiness/drop operation against literal native words.
Coarse occupancy stays authoritative throughout these fine exclusions.

This integrates the selected point owner. `16da60` target-region ownership,
captain and delayed-reconstruction publication, the independent unit exclusion
and notification reentrancy are still separate MAP-04.2 work. See
[canonical requests](retail-pathfinding-engine.md#canonical-point-requests-publish-physical-cohorts-only-at-readiness-payoff248)
for evidence and save/event limits.
