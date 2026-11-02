# MAP-04.2 proposed documentation (not applied)

## TODO

Proposed evidence text for MAP-04.2. The owner closes it after the engine regressions:

> Evidence: research handoff MAP-04.2.
> - The instruction-level inventory of 12 exclusion sites finds no exit while held. The compiled unwind restorations have no reachable throw source.
> - Every reachable exit has one restoration assertion: coarse exact/partial, fine exact/partial, and four pre-acquire denials (original code).
> - Request scopes reach no map writer and no other scope. An edit therefore happens between request phases, and the request's rebuild-from-fine restoration publishes pending edits inside its rounded coverage only: 18 original cases, and two live repeats where 1 cell is published and 6 stay pending.
> - Stop-recovery reentrancy and group member values are split out.

Proposed new IDs (text only):
- **MAP-04.3** Member +40 values through a 12-member `16bcf0`, including a member that dies between acquire and release.
- **MAP-04.4** Live witness of agent notifications or placement callbacks re-entering game logic inside Stop recovery (`05ca50`/`170080`).
- **MAP-04.5** Decide whether a public order can pass a stale unit handle to `059590` (fault path at 059720).

## Ledger

### search.md, producer inventory row "Request exclusions"

Replace "Nested objects, early/reentrant exits and edits during request: MAP-04.1/02" with:

> Request scopes have no early or reentrant exits (MAP-04.2 inventory). Edits occur only between phases. Restoration rebuilds from current fine cells and publishes pending edits inside the rounded coverage.

### routes.md, new subsection after the MAP-04.1 subsection

> **Exclusion exits and edits between request phases.**
> - The fine and coarse request scopes acquire only after interval/admission succeed. Their only exits after acquiring follow full restoration: coarse exact 166e2a and partial 166e7e; fine 167061.
> - No writer is reachable during the searches. The FuncInfo unwind states release target then self, but no throw source exists.
> - `SetTerrainPathable` changes fine flags without publication. The next request that clears and rebuilds a rectangle covering the cell publishes it: live base (15,17) changes 0 → 80 at the first target request, while edited cells outside the coverage remain 0.

## Corpus

Proposed strict entry `map04-exits-edits-261006`:
- static tool equal to `scope-exits-MAP-04.2.json` (`f773be04…`);
- oracle `--edit-reference expected-oracle-MAP-04.2.json` (`d8c0c978…`);
- live analyses recomputed from the archived captures (`98d5c514…`, `673f2e03…`, `32ef10eb…`);
- composite `expected-MAP-04.2.json` (`94e1fd49…`).
