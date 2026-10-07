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
