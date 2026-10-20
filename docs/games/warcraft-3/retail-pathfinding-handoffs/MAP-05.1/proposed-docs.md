# Proposed text (not applied) — MAP-05.1

## TODO evidence sentence (MAP-05.1)

Evidence: research MAP-05.1 (original-code + instruction; live first block, pool blocks, LIFO reuse and whole-owner teardown).
Crossing the 131,072-link boundary inside one update reallocates 1 MiB → 2 MiB and places the crossing object's records in
emission order; after retire + maintenance its 36 slots are reclaimed and the next object (same memory, LIFO pool) reuses them in
free-list order with no allocation; cell contents/queries list it first (`[D,B,A]`). Object pool blocks of 64 objects (4,612
bytes) cross at the 59th/123rd new object. SpatialMap_Release frees links/dirty/cells and returns the map to the owner's map pool;
the next map reuses the same identity with fresh link/cell/dirty/stamp state and a new maintenance request, but map +B0 (record
count) and +AC survive the release (harmless live: all objects are retired first, +B0 reaches 0).

## Ledger text

* Release order (live, MAP-06.1): units/regions retired → PathMaps_Release (full compaction → 0 records) → owner destroyed.
* Hazard (oracle only): an object still live across SpatialMap_Release writes removal records into the reused map and is never
  destroyed; not reachable in observed teardown flows.
