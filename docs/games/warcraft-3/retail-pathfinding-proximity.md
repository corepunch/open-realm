# Ordered proximity membership and complete separation composition

Payoff132 closes SEP-02.2 by replacing repulsion's server-area enumeration with
an independent Move-owned proximity index. All 3,055 updates and 2,439 ordered
neighbor contributions in the nine frozen retail T clusters execute through
the engine's complete update, including predicted poses, retained displacement,
endpoint rejection, fine publication, accumulation, damping/cap and cooldown.
The existing implementation failed seven clusters by one or more scalar words.
Expected values remain unchanged. The regression creates all 27 units together
and follows the complete captured visit order: T2 eventually visits T5's unit 12
as a cross-cluster, zero-contribution neighbor. Isolated clusters preserved
endpoints but omitted those calls; they cannot certify the complete pair trace.

## Two independent rectangles

`Mover_UpdateProximityBounds` (`6f1604d0`) publishes position ± collision radius
at proximity inverse scale 1/8. `Separate_ConfigureQuery` (`6f170960`) constructs
the query with the selected separation radius. Both use software-scalar
subtraction/addition/multiplication, floor minima and floor maxima plus one.
`PathMaps_Create` (`6f15ab60`) pads proximity dimensions to `(fine+16)/8+1`.
Fine occupancy uses its independently quantized footprint. A fine-cell rank
cannot substitute for the proximity insertion history.

Native `6f14e770` preserves records in intersection cells; newcomers prepend.
`6f170c00/6f170b30` visit cells Y then X, newest effective links first, deduplicating
objects across the query before applying eligibility. No distance sort occurs.
An earlier displacement in the same owner pass commits both rectangles before
a later source resolves that neighbor's position.

## Engine representation

`wc3_pathing_proximity.h` stores effective active memberships with intrusive
cell/owner links. Rectangle updates keep intersection links in place, unlink
left cells and prepend entered cells. Integer link identities survive geometric
capacity growth. Reservation precedes mutation, and allocation failure reaches
the game's fatal error path rather than returning partial membership.

Queries visit only the clipped rectangle and linked occupants, with a 64-bit
visit epoch for deduplication. Updates cost the object's current links plus
entered cells; queries cost visited cells plus their memberships. No entity
scan, per-query stamp-array clear, sorting or lazy-removal backlog is required.
Free slots are reused immediately. Currently ineligible occupants retain
membership, so a later eligibility refresh does not invent a new insertion rank. The 4096-owner growth/reuse regression stores
16,384 active links and reuses them without increasing its high-water mark.
This is an algorithm/storage bound, not a claim that the overall FPS target is met.

`s_move_spatial.c` owns publication, cached geometry, candidate traversal and
map lifetime. Actual pose commits publish proximity before fine occupancy;
unchanged presentation samples retain their cell history. The Move ability
still owns eligibility, pending-vector application and pair arithmetic. Retiring
a fine record when a unit becomes a building must not retire the independent
proximity record; only complete spatial-object removal clears both grids.

This representation deliberately omits retail's physical lazy records, native
allocator identities,32-bit stamp words and maintenance requests. Full original
writer/reader/compactor execution certifies equivalent ordinary candidate order
at 1,280 updates and 160 dirty/full-cleanup checkpoints. Native maintenance
cadence, block retirement and forced stale-stamp consequences remain separate
[SEP-03 requirements](retail-pathfinding-todo.md).
SEP-03.1/02/03 and MAP-05.1/03 are not closed by this composition change.

## Save/load

Save 119 stores independent logical proximity rectangles in entity save order.
Load prepends those rectangles, reproducing the retail rebuilding rule instead
of retaining movement-era chain order. Fine rectangles keep their existing
separate stream. No process pointer, link capacity, free list or query epoch is
serialized. Prior versions are rejected.

Regressions cover retained-cell order, leave/reentry, immediate removal,
4096-owner growth/reuse and epoch wrap, live save/load reversal and malformed
rectangle/duplicate records. The full saved four-route test continues to check
pose, velocity, route cursors, wait state, repulsion and shared RNG.

Two older fixtures are corrected to their retail contracts: positive-speed
repulsor rows explicitly author speed 270; the ordinary segment predicate
`148e90` does not exclude a target merely because its identity is supplied.
The route owner's explicit suppression scope remains separately tested.

## Evidence and verification

The unchanged `SEP-02.2-expected.json.gz` has uncompressed SHA256
`e237fe7b55654d0f037630394d3732157d632f1aa2a41c085ddb01ac954aaeca`.
The portable bundle retains both full raw Frida captures, all three provenance
records/preloads, the actual map script/metadata and both full original replay
reports. Both observed runs reproduce every normalized group; all 3,543 JASS
markers equal the observer-free control.

`verify_wc3_pathing_proximity.py` freshly runs original pair slices/tails for
both complete captures: 5,601 updates,3,000 bodies and 4,321 neighbor contributions
per capture. It compares entire recorded rows, all frozen groups and every C
fixture visit/pair/input; incomplete captures or hash changes fail verification.
The engine test additionally compares every individual pair's identities,
source/candidate pose, before/after vector and shared RNG, then checks each
complete update's final pose/vector/word/occupancy. The captured owner visit
order is an input; this does not certify map-start scheduling or seed production.
T clusters do not draw RNG;
exact-overlap/random scenes retain their existing SEP-02.3/04 owners.

The original endpoint results are recorded live; endpoint execution is not
part of the isolated pair oracle. The engine's endpoint is covered by the
complete update regression. This does not certify unrelated full-map motion.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_proximity.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --report /tmp/wc3-proximity-oracle.json
```

`verify_wc3_pathing_spatial.py --engine-library` now also compares every complete
ordinary proximity query with the production active index. Existing fine-chain
comparisons remain 81,920. Ghidra's five consumer/producer comments are saved and
read back in `retail-proximity-ghidra-1.27.json`; `MapPathfinding.java` preserves
the reusable annotations. Logs: `/GitHub/wc3-analysis/runtime/payoff132/`.

Accepted validation: release production/test builds and debug tests; Classic and
TFT each pass 687 focused tests / 8,305,352 assertions in both configurations.
The final debug triad and building-retirement regressions also pass against the
latest module. The isolated staged tree passes 20 corpus and six proximity
Python checks with 364 entries / 499 pinned files, plus the fresh complete
original replay and compiled spatial/proximity comparisons above.
Network layouts are unchanged; the save contract changes to version 119.
