# Proposed text (not applied) — SEP-03.2

## TODO evidence sentence (SEP-03.2)

Evidence: research SEP-03.2 (instruction + original-code; live allocation/reuse; labelled stamp interventions). Query stamps
increment once per query/collector/compaction; the only repair is the compaction-start reset `>0x7fffffff` → 0, not reachable
in observed play (~7 h at the 40-mover fine rate of ~86k/clock unit). Labelled post-repair hazards are frozen (stale object
stamps can suppress or duplicate candidates until rewritten). The link table allocates lazily (first 1 MiB = 131,072 links at the
first record of a map, live) and grows by `0x20000` links via SMemReAlloc, never shrinking while the map lives; candidate order
is unchanged across growth and across LIFO reclamation (free-list pop order frozen). Object pool blocks hold 64 objects (4,612
bytes); reclaimed objects are reused LIFO (20/20 live).

## Ledger text

* Spatial stamps: 32-bit counter per map (+B4), object stamp +38 (`-1` = dead); reset only at compaction start when the counter
  has its sign bit set.
* Storage: links never shrink until SpatialMap_Release (ShrinkToFit to 0).

## Proposed new ID (text only)

**SEP-03.5** Decide the engine's spatial stamp representation (native 32-bit counter with retail reset semantics, or epoch-safe)
and add a regression across the reset boundary using the labelled fixture of `verify_SEP-03.2_spatial_stamps_blocks.py`.
