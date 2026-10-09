# Proposed text (not applied) — SEP-03.1

## TODO evidence sentence (SEP-03.1)

Evidence: research SEP-03.1 (instruction + original-code + live). Every spatial membership entry is an 8-byte link record
prepended to its cell (newest first); insert P,Q lists `[Q,P]`, Q,P lists `[P,Q]`, live proximity cell 428 `[B,A]`/`[A,B]` in
both orders with observer-free controls equal (181/181 markers). Overlap-preserving moves keep retained records; leave+re-enter
re-prepends. Removal/Retire and fine-search metadata write dirtying records; one repeating maintenance request per map (period
`3dccccce`, unit-tick clock, proximity before fine at equal deadlines; 697/698 live callbacks, every gap 0.1) compacts dirty
cells only, keeping the first effective live insertion per object and recycling dead objects LIFO when their last record goes.
No record/dirty-count threshold exists; the only threshold is stamp `>0x7fffffff` → 0 at compaction start; full sweeps occur only
in PathMaps_Save 6f15c750 and SpatialMap_Release 6f14cac0. Region-style objects (+40 `0x10000000`) emit no removal records and
are reclaimed only by a full sweep. Open: writer of +3C high-byte flags (proposed SEP-03.4).

## Ledger / retail-pathfinding-separation.md (spatial records section)

* Replace "cleanup threshold/sampling cadence unknown" with: dirty-cell compaction every 0.1 game-second per map
  (soft-float accumulated deadlines `3dccccce,3e4cccce,3e99999a,…,3f7ffffd`), no sampled cleanup in 1.27.1 (6f14e180 and
  6f14e000 are unreferenced), no count threshold.
* Record ordering rule: cell chain = reverse insertion chronology of live first insertions; removal is lazy (records persist
  until the next deadline of that map).

## Proposed new ID (text only)

**SEP-03.4** Identify the writer and meaning of spatial object `+3C` high-byte flags (`0x10`/`0x80` seen live; masked out by all
count logic) and decide whether the engine needs them.

## Engine docs (when the owner ports regressions)

Regressions 1–3 of the HANDOFF; inputs are the rectangles P(2,2,4,4), Q(2,2,5,5), R(3,0,6,3) of `verify_SEP-03.1_spatial_lifecycle.py`.
