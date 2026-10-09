# MAP-04.1 proposed documentation (not applied)

## TODO (retail-pathfinding-todo.md, MAP-04)

Proposed evidence text for MAP-04.1. The owner closes it only after the engine regressions pass:

> Evidence: research handoff MAP-04.1. 45 complete original 166c30 and 45 complete 166e90 requests on two overlapping objects, an overlapping bystander and five terrain cells. All 1,360 class bytes are frozen at every exclusion boundary; 324 window cells are re-queried per counter state. Fine counters restore in reverse; coarse restores in the same order by rebuild from fine. Three live retail captures restore every observed request, and a three-deep same-object nesting is observed. Group-member counter values are split to MAP-04.3.

Proposed new ID (text only):
- **MAP-04.3** Capture member +40 words through a 12-member `16bcf0` publication (`169c50`/`169d60`/`16da60`), including a member that dies between acquire and release.

## Ledger (retail-pathfinding-routes.md, after "Coarse self/target exclusion clears a region, then rebuilds")

### Nested self/target exclusions over overlapping objects

The fine request (`166e90`) increments self then target `+40` and decrements target then self. It restores saved pointers, so a self==target request counts twice. The coarse request (`166c30`) clears self then target rectangles, searches, and then rebuilds self then target. It re-reads `path+a0/a4` and each `+1c` rectangle, and rebuilds from current fine cells instead of restoring bytes. With overlapping coverage, the self rebuild restores target-covered cells before the target rebuild. In the fixture, (9,9)=AA and (10,10)=80 return while (11,10) stays 0 and level-1 (5,5) is 80 instead of AA. Final bytes always equal the baseline.

The base classification counts an object only if `+40` bit `10000000` is set and its counter is zero. A rebuild while any exclusion is held therefore publishes that object as absent. Excluding self can reveal the target's identity where self's link precedes the target's in a cell (`1489a0` tests identity at 148a52 before exclusion at 148a67).

Live retail observation:
- `69a840` brackets Stop recovery with `Unit_ToggleSpatialExclusion`.
- `05ca50` and `170080` each add one more increment on the same mover spatial object (depth 3).
- All 24 nested scopes restore LIFO.

Oracle: `tools/ghidra/research/verify_map04_1_nested_exclusions.py`.

## Corpus (retail-pathfinding-corpus.md)

Proposed strict entry `map04-nested-exclusions-261006`:
- runs the oracle with `--reference expected-MAP-04.1.json --edit-reference expected-oracle-MAP-04.2.json`;
- requires `passed=true`, `observer_free_control_equal=45` and payload sha256 `4a95b59f…75b0`.
