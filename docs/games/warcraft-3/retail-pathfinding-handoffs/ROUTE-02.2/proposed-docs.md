# ROUTE-02.2 proposed documentation text (not applied)

## TODO (`retail-pathfinding-todo.md`, ROUTE-02.2 line) — proposed evidence suffix, owner closes

> Research evidence (not a closure): `verify_route02_2_blockers.py` runs 72 complete original 166140 collections
> (4 classes x 9 step codes, under/over capacity), 14 complete resolver compositions over capped ordered vectors,
> 288 complete 167bf0/165e60 selections over object occupancy, four 7-step obstruction-change sequences and
> 16+8 selection/collection compositions; expected sha256 `1433827c...611f6`, observer-free control equal.
> Engine regressions and the target-identity mismatch remain for the owner.

Proposed new ID (text only):

- **ROUTE-02.5** Capture a live retail crowd whose next-step strip reaches the 32-token blocker cap; record
  actual vector order against runtime lazy-chain order and the resolver decision (depends on FINE-01.6 chain chronology).

## Ledger (`retail-pathfinding-routes.md`, after "Append admission stops at 32 entries ...")

> **Blocker capacity is storage capacity.** Constructor `6f147340` builds fine-system table `+ac` with capacity 32
> and growth 0; `6f1480d0`/`6f148ad0` gate `count < 32` before `6f148550`, so the next-step path never resizes and
> the 33rd and later tokens are silently dropped. Candidate order is the class strip cell order
> (`148e30` rows increasing x, `148cc0` columns increasing y, `148c30` row-major rectangles for non-edge codes)
> times stored chain order per cell; stamps are per cell (grid `+b4`), so one object occupies one slot per strip
> cell. `6f168360` scans only those 32 tokens in order: slower different-group peers before the first decisive
> candidate receive blocker=self/delay 20, the first faster (or same-group/other-class) moving peer makes the
> requester wait (delay>=4) and ends the scan. A faster peer at token 32 is never seen: the requester does not
> wait, while earlier slower peers have already been assigned. Only path `+a0` (self) is suppressed; the path
> target `+a4` and fine `+a8` target are collected like any mover, and `+40` flags `20000000/40000000` do not
> exempt collection.
>
> **Waypoint selection sees occupancy at call time only.** The static closure of `167bf0/165e60/166140/168d30`
> (48 functions) contains no callbacks or virtual calls; besides stack it writes only fine `+a4/+cc/+d0`, grid
> `+b4`, object `+38` stamps and the self `+40` counter. Occupancy cannot change between samples of one call, so
> "an obstruction change between samples" is a change between successive selector calls (each reached
> waypoint). With fine `+d4 == 0` (reset by search setup `14aea1`), objects carrying `+40` flags
> `20000000`/`40000000` do not block selection but are still collected by `166140`; a target object blocks
> selection (setting `+cc`). Frozen sequence on a 5-point route (all classes): clear 0, static object at (17,16) 2,
> same object flagged 20000000 0, with `+d4=1` 2, flag cleared 2, unlinked 0. Evidence:
> `tools/ghidra/research/verify_route02_2_blockers.py`, `route02_2_callgraph.py`.

## Corpus (`retail-pathfinding-corpus.md`) — proposed entry

| ID | Artifact | sha256 | Checks |
| --- | --- | --- | --- |
| ROUTE-02.2 | `research/ROUTE-02.2/expected-ROUTE-02.2.json` | `1433827c28e1e59c74c5cb6863aad9b09f7d78ee72bdb0738715cc96abd611f6` | strip order, capped vectors, resolver, selection, write audit, target identity |
