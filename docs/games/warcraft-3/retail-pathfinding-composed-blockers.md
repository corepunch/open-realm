# Composed dynamic blockers and yielding

Payoff163 integrates ROUTE-03.1, ROUTE-03.2 and ROUTE-05.2 through ordinary
public JASS orders and server frames. It extends the [yield identity and group
policy](retail-pathfinding-yield-lifecycle.md) work with complete trajectories,
including intermediate route handoffs and cold-load continuations.

## Endpoint ownership

The dynamic-gap fixture exposed three singleton Move differences that short
resolver or retry windows did not reach:

1. After a terminal partial leg, the next shared waypoint inherited retry1.
   Retail `PathGroup_ResetMemberRoutes` (`16d6a0`) calls
   `Path_ResetBuffers(-1,1,0,1)` for each resolved member. Move now clears the
   member retry/delay at that handoff. The shared route and request timestamps
   keep their own lifetimes. `1687e0` does not clear the blocker identity;
   `168360` owns that operation.
2. At an intermediate partial fine endpoint, the singleton performed a final
   retry instead of consuming the next adaptive waypoint. Original `167070`
   consumes it through `165d10`, invalidates the fine index and returns2. It
   retains the fine table and turns toward the current fine endpoint during
   this held visit. In the after-insertion scene, owner1777 retains17 fine
   points while adaptive index1 becomes0; the before-insertion counterpart
   reaches that transition at1771.
3. A group waypoint can change before the member's search timestamps permit
   destination replacement. Original `167e40` compares floored coordinates
   shifted by1 and requires both request deltas to reach10. At1583 the new
   caller destination is(79.5,79.5), but the retained path still requests
   (95.5,65.5): coarse work3 and three points. The engine requested the new
   destination immediately, charging5 and producing four points even though
   later motion happened to agree. Explicit charged-work assertions exposed
   that difference. Move now retains the old routing destination until the
   gate allows replacement; arrival and stopped heading keep the caller's
   current destination.

`move_advance_endpoint` now implements this transition for both singleton and
physical-group owners. The previous duplicated group branch is removed.
`G_AdvanceUnitMoveAdaptiveDestination` invalidates only the fine index, matching
`165f10`; the later admitted refill owns table replacement and charged work.
These are constant-time state transitions, with no additional world scan,
per-unit allocation, order/rawcode exception or scheduling-budget change.

The preserved nonempty fine table with `index == UINT32_MAX` is a valid stopped
state awaiting refill. Both save writer and reader accept that sentinel while
continuing to reject any other out-of-range index. The new direct save test
failed before this change, as did saves in three complete dynamic scenes.
The saved layout and Save133 version are unchanged.

## Public scene coverage

The immutable headers are exported from complete original Frida step streams;
they never use engine output. Normal five-ms server frames run the probe's
public ten-ms timers from owner1024. The tests use authored fixture profiles
with captured collision31, speed270 and the observed map geometry, including
the locked-map random initialization. They do not inject route buffers, retry
state, intermediate poses or individual search outcomes.

| Family | Public scene | Engine owner steps |
| --- | --- | ---: |
| Asymmetric yielding | Same/different-player crossings; same/different-player one-lane tunnels; three equal-speed converging movers | 1,692 |
| Dynamic blockage | Neutral unit inserted before/after fine refill; slower moving peer; blocker removal while waiting, on a partial leg or later; terrain gap closure and reopening | 8,189 |

Every step checks retained/predicted fine pose, committed velocity, observed
facing, fine and adaptive count/index, delay/retry, blocker role, request
timestamps and charged fine/member-coarse/group-coarse work. A final facing
without a following observation is explicitly unasserted; the exporter records
that absence instead of inventing a numerical expectation. Final public order
retirement is checked independently.

Cold saves occur during waits, partial-route travel and the retained-table
transition. A fresh world then loads each save and must reproduce the unchanged
retail suffix. Terrain edits and the later scripted removal remain active after
load. These are engine save continuations against uninterrupted retail captures;
there is no claim of a retail UI-save witness for these exact thirteen scenes.

The initial cold-load fixture incorrectly restored default map geometry and
left the previous scene's timers alive. Correcting the fixture, rather than
changing Move, resolved those failures. Each case now retires the prior timer
queue and reinstalls its actual map bounds/pathmap before load.

## Yield graph and outcomes

Retail `168360` clears the requester's blocker identity before collection and
skips a candidate that already has a resolvable blocker. Requester4 therefore
points to a candidate with no outgoing edge. Peer20 points to the requester,
whose edge was just cleared. A persistent cyclic wait is excluded by the
resolver's edge-creation contract; the three-mover witness exercises the
resulting ordered chain, not a fabricated cycle.

The complete scenes retain the original committed-speed comparison, player
relation and newest-created-first owner order. Same-player tunnels assign the
slower peer20 while the faster requester retries; different-player tunnels
assign requester4 instead. Both tunnel orders eventually retire through
can't-path. The crossings and three-mover convergence arrive normally. Timed
removal and terrain reopening release blocked travel through the original
countdown/refill transitions rather than bypassing admission.

## Evidence and reproduction

The strict verifier checks19 complete archived observations, seven
observer-free public controls and five full normalized repeats, retaining13,724
observed member steps. It also preserves all539 owner-window records in the
three original research fixtures. Raw capture sizes/hashes, binary and metadata,
public Preload timelines, complete normalized streams, repository sources and
regenerated engine headers are pinned. A fresh verification is a new evaluation
of archived captures, not a new retail run.

```sh
python3 tools/ghidra/research/verify_composed_blocker163_live.py \
  --expected tools/ghidra/fixtures/retail-composed-blocker163-1.27.json \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27/research \
  --output /tmp/composed-blocker163-new.json
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test 'wc3_movement.public_composed_yield_matches_retail_owner_streams'
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test 'wc3_movement.public_dynamic_blockers_match_retail_owner_streams'
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test 'wc3_save.retained_fine_table_with_invalid_index_round_trips'
```

Use `-tft` for the second data schema. Some variants have only one observation
or no individual observer-free control; exact exclusions are retained in the
manifest. No whole-scene Unicorn replay, larger-crowd generalization or UI-group
trajectory parity is inferred from these cases.

Raw maps, Frida probes/captures and controls remain in the report root under
`research/ROUTE-03.1/`, `ROUTE-03.2/` and `ROUTE-05.2/`. Red/green engine logs,
fresh strict reports and decompiler readbacks are under
`/GitHub/wc3-analysis/runtime/payoff163/`. Ghidra comments at16d6a0,167070,
165f10,1687e0 and167e40 are saved and mirrored in `MapPathfinding.java`.

See the [TODO ledger](retail-pathfinding-todo.md#route--route-progression-and-yielding),
[engine evidence](retail-pathfinding-engine.md#composed-dynamic-blockers-and-yields-payoff163)
and [frozen scenes](../../../tools/ghidra/fixtures/retail-composed-blocker163-1.27.json).
