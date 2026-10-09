# Proposed text (not applied) — ACC-02.2

## TODO (retail-pathfinding-todo.md, ACC-02.2 line) — evidence sentence for the owner to use when closing

Evidence: special-marker/object reachability (research ACC-02.2): 108 producer-built child states
(54 ordinary tuples x marker 0/nonzero, both producer orders), 96 original 15d1c0 reducer cases with
markers at every child position, level-1 tuple closure equal to the 54 ordinary tuples, class pair 11 and
upper-level byte6 rejected by a static/dynamic writer inventory including save/load (162f20/163120), and
512 supplied-record 148e90 controls reducing object flags to the same effective lane set. A class2 parent
over clear children is reachable only through a marker. Object eligibility/producers stay FOOT-03.

## retail-pathfinding-search.md, "Ordinary classification reachability" — append

Special markers and object flags add no four-lane class tuple. A marker changes a lane only when all four
children are clear in that lane (0 → 2); blocked and mixed children ignore it. Level-1 tuples with markers
are again the 54 ordinary tuples. Byte6 is written only on the base map (15c030) and copied by save/load
(163120 saves ushort cell+6, 162f20 restores it and zeroes stamp/node index). Class pair 11 is never
written. With constructor dimensions every parent has four in-range children, so the "missing child is
blocked" reducer path does not occur; mixed parents over clear children arise only from markers. Marked
clear cells are only level-0 nodes; node byte20 copies the marker only with warp enabled, byte23 equals the
level-0 parent's byte20. 148e90 blocks a lane iff the fine high byte or an eligible type-1 link within the
first 49 links has a lane bit (02/04 ground, 80, 40, 04), the same per-cell set as terrain flags.

## Proposed new ID (text only)

- ACC-05.4: after PathMaps_LoadAdaptiveCells every adaptive cell stamp and node index is zero; verify the
  accelerator stamp counter after load so that a first post-load search cannot alias stamp 0 / node 0.
