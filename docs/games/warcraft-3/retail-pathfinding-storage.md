# Retail spatial records and engine storage

Payoff135 closes MAP-05.1 with the shared raw record core and proximity owner
integration. Payoff136 moves the actual engine fine map onto the same core and
closes SEP-03.1/03.2/03.3 and MAP-05.3. Static region producers and the full
consumer matrix remain FOOT-03.1/03.2. These closures do not certify complete
retail pathfinding or the IceCrown frame/creation performance targets.

## Representation and data flow

`wc3_pathing_records.h` owns eight-byte `{kind_and_next,payload}` records. The
low24 bits select the next slot, with `ffffff` as the end marker. Kinds0/1/2
are removal, insertion and fine-search metadata. Ordinary payloads refer to
stable pooled identities; metadata payloads pack the request stamp in the low16 bits and node index in the high16 bits.

Objects occupy address-stable blocks of64. The engine stores logical identity
indices instead of native pointers and uses36-byte mutable objects instead of
retail's72-byte object. Entity-slot reuse cannot repurpose a retired spatial
identity while old records still reference it. The last dead reference returns
an object to the LIFO pool. Record storage starts lazily with131072 slots
(1MiB), grows by131072 slots and retains capacity within the map lifetime.

Movement emits removal strips before insertion strips. Within each operation,
`1d4ae0` strips are consumed in reverse: highX, lowX, highY, lowY; each strip is
row-major. Intersection cells retain their old insertion. Each new record
prepends, including removals. This preserves slot identity as well as candidate
order; sorting all changed cells would lose the native allocation sequence.

Publication costs O(changed cells); link allocation and object reuse are O(1)
amortized. Queries visit only the requested cells and their raw chains. Cleanup
visits the dirty bitmap and dirty chains, without scanning every owner or
sorting candidates. Eight-byte records replace the previous20-byte active links.
Retained history increases the number of records visited between cleanup ticks;
this change does not assert an overall measured performance win.

## Query, cleanup and lifetime

`170c00` increments a32-bit query stamp only for a nonempty clipped rectangle.
Each nonempty raw cell increments another stamp. An ordinary insertion writes
the query stamp even when eligibility rejects the candidate. A removal writes
the cell stamp, masking older coverage in that cell. Metadata is skipped.
Source exclusion stamps the source before traversal.

Dirty compaction keeps the first effective live insertion, removes other
records in chain order, decrements references and pushes reclaimed slots LIFO.
There is no record-count threshold. One recurring map request uses software
`1/10`, word`3dccccce`; repeating deadlines add the period to the previous due
word. The engine merges this request with script timers and the path owner by
deadline and registration sequence, including primary-clock epoch rebasing.

A full sweep compacts every nonempty cell but **leaves the dirty bitmap set**.
The following dirty sweep still visits marked surviving cells and advances their
stamps. Region-style retirement emits no removal records, so untouched dead
region records require a full sweep. Actual fine region producers remain open.

Dirty and full maintenance reset only the map counter when it exceeds
`7fffffff`. Object stamps are retained. Labelled forced-jump tests reproduce both
the one-query alias and the live-insertion unlink hazard. Natural multi-hour
wrap was not captured. No safer epoch interpretation is substituted silently.

Allocation failure is fatal, matching Storm's nonreturning policy. No API
returns successful partial membership. The native diagnostic that releases a
map while owners remain live is preserved in MAP-05.1's handoff; it is not a
supported engine ownership operation. Normal reset clears all identities,
records, stamps and the request; no old owner survives map replacement.

## Save contract

Save120 performs full proximity compaction, then saves the map stamp and each
logical owner rectangle/object stamp. Load inserts rectangles in save order and
registers a fresh maintenance request. It reconstructs allocation slots and
free lists; no process pointers are serialized. Previous formats, including119,
are rejected. Payoff136 extends stamp persistence to the fine map in Save121;
region-cell serialization remains FOOT-03 work.

## Evidence and regressions

The target is game.dll1.27.1.7085, SHA256
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`.

- [MAP-05.1 handoff](retail-pathfinding-handoffs/MAP-05.1/HANDOFF.md): original
 131072-link crossing, exact36-cell insertion/removal/reuse indices, pooled
 object reuse, map release and the explicitly invalid stale-owner control.
- [SEP-03.1 handoff](retail-pathfinding-handoffs/SEP-03.1/HANDOFF.md) and
 [SEP-03.2 handoff](retail-pathfinding-handoffs/SEP-03.2/HANDOFF.md): lazy records,
 compaction,12 accumulated deadlines, block allocation and labelled aliases.
- `verify_wc3_pathing_records.py` compiles the actual production header and
 executes unmodified retail constructors/updates/queries/compactors. It compares
 every raw chain/index, the entire free list, dirty cells, map/object stamps and
 reference counts after composed mutations and the allocation boundary.
- `retail-spatial-storage-inputs-1.27.json.gz` retains complete Frida AB/BA
 captures plus their raw observer-free JASS controls. Each181-marker stream
 matches its control. There are697/698 cleanup callbacks per map, with1MiB
 initial link blocks and20/20 LIFO recycled allocations. The observer v1's later
 reused-object labels remain a documented limitation; they are not used to prove
 candidate identity after reuse.
- `retail-spatial-storage-ghidra-1.27.json` is saved-program readback of14
 functions and four layouts. `WC3SpatialMapPrefix` owns derived fields after the
108-byte shared map header; extending the shared header would misdescribe
adaptive subclasses. Function comments preserve earlier research annotations.
- `t_proximity.c` covers exact frozen allocation indices, pooled owner reuse,
 dense4096-owner cleanup, forced aliases, flagged retirement, full-sweep dirty
 history, real scalar dispatch, source/owner order and Save120 reconstruction.

Reproduce from the repository root:

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_records.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report /tmp/spatial-storage.json
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_proximity.*'
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 build/bin/openwarcraft3-tests -data build/tests -tft +dedicated 1 +test 'wc3_proximity.*'
```

The regression first failed4 assertions against the active-only implementation.
The original/C differential additionally rejected clearing the dirty bitmap on a
full sweep. Per-turn reports and build/test logs live under
`/GitHub/wc3-analysis/runtime/payoff135/`.

The batch checkpoint passes `make -j6 BUILD=release TEST_JOBS=6 test
openwarcraft3` with native SDL2: Classic and TFT each pass 2,800 tests and
9,223,538 assertions; all 755 Python tests pass. The focused proximity suite
passes 13 tests / 13,335 assertions per mode. The isolated staged tree passes
723 of 724 Python pathfinding checks, with its missing-built-binary check skipped
(the complete workspace run passes it). All 537 staged evidence hashes match.
The fresh strict spatial-record report and both preserved fine-storage oracle
reports pass. Earlier checkpoint failures exposed stale positive-speed/Patrol
API expectations, the added reload-map catalog entries and saved-comment
readback fixtures; these were corrected against existing verified contracts.
The host SDL compatibility-library crash is avoided with the documented native
SDL2 test environment, without skipping client input tests.

## Payoff136 fine-map integration

The actual fine map, owned by Move in `s_move_spatial.c`, now uses the same
pooled records as proximity. `g_world.c` publishes logical rectangles and adapts
world queries. The former144-byte per-owner rank array and sixteen intrusive
link slots are removed. Cell order comes directly from the retained chain;
step-blocker collection no longer sorts candidate ranks.

A4×4 footprint moving by one cell keeps twelve intersection memberships and
emits four removals/four insertions. Update work is proportional to changed
strips. Retirement retains the old identity until compaction releases its last
reference, so records cannot accidentally observe a reused edict. A raw-head
bitmap includes metadata and dead records, not only active objects.

Neighbor expansion calls `148d00`'s clockwise perimeter once:8/12/16/20 cells for
classes0–3, then applies the original neighbor masks. It visits the whole
perimeter even after rejection. The old eight entering-strip queries repeated
cells and stopped individual strips early, changing stamps and target identity
observations. Dynamic verdict caching is removed because a repeated predicate
must still advance the map/object stamps; terrain-only empty neighborhoods retain
their cache. No measured total-server or IceCrown speedup is claimed here.

Actual fine node creation now publishes kind2 metadata. A metadata head is
overwritten; an ordinary/removal head receives a new metadata record. Request
stamps advance at the original nontrivial setup boundary, includingFFFF/0/1/2.
The existing sparse request lookup retains within-request node identity. The
historical65536-request metadata alias case remains excluded, as in MAP-05.2;
this is not a new complete-alias guarantee.

Fine and proximity maintenance remain independent recurring software1/10
requests, merged with JASS timers by deadline and registration serial. Proximity
runs first at equal deadlines. Load registers fresh requests in that order.
Metadata/removal counts never trigger early compaction. The engine reproduces
MAP-05.3's2100-record case:2000 metadata plus ordinary insertions/removals become
36 live memberships and2064 free slots at the fine deadline. Labelled high-bit
repair retains object stamps, including the native unlink-alias hazard.

Save121 fully compacts both maps, retains independent map/object stamps and the
fine16-bit request generation, and reconstructs logical ordinary rectangles in
save order. It rejects earlier versions. It does not serialize process pointers,
retained raw link identities or the old movement-era ranks.

The supplied MAP-05.3 and SEP-03.3 reproducers run unchanged against the
hash-matched original DLL and match their frozen reports. All28 allocation sites
use Storm; allocation failure terminates, rather than exposing a recoverable
partial update. The engine likewise aborts on failed spatial allocation. A
labelled allocator substitution forces failure after eight cells of a16-cell
publication and asserts process termination before any partial-return path.
Original36-cell growth/pregrown controls retain complete membership; an actual
engine publication also crosses131072 links mid-update without losing a cell.
The labelled24-bit sentinel exhaustion and forcedNULL/zero-growth substitutions
remain diagnostics, not evidence of naturally reachable retail behavior.

Ghidra now has15 saved function annotations and an exact readback in
`retail-fine-records-ghidra-1.27.json`. `WC3FineSearchPrefix.map` points to the
188-byte spatial subclass; its shared base stays108 bytes. MapPathfinding and
MapPathfindingTypes reproduce that refinement without enlarging the base.

Validation: eight new engine regressions pass401 assertions in Classic and TFT;
focused routing90/334773, save191/27483, and spatial-load4/317515 pass. The
Classic movement suite passes349 tests/5186690 assertions. Raw original/C storage
checks compare all chains/indices/free slots/dirty cells/stamps/references;
complete AB/BA Frida controls remain362 equal public markers. Full validation
was completed at Payoff135 and stays on the agreed batched cadence. The release
smoke benchmark reports0.52 ms per100-ms simulation frame for12 movers plus4000
scenery, compared with0.50 ms at that checkpoint; this is not a controlled
speedup claim or a4096-unit/real-map acceptance measurement.

FOOT-03.1/03.2 still require actual raw static region producers, the49-record
hierarchy limit, layered terrain/region predicates and their full mixed-category
consumer regressions. This fine ordinary-owner replacement is their prerequisite,
not a substitute for those integrations.
