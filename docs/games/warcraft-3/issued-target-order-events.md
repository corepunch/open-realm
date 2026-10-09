# Warcraft III Issued Target and Point Order Events

## Contract

Warcraft trigger events describe an order when the order is **accepted**, not when a delayed behavior later reaches the front of a unit's queue. OpenRealm publishes the matching player-unit and unit event families from the authoritative order submission path:

| Order shape | Player-unit event | Unit event | Event context |
|---|---|---|---|
| immediate/no target | `EVENT_PLAYER_UNIT_ISSUED_ORDER` (38) | `EVENT_UNIT_ISSUED_ORDER` | `GetOrderedUnit()`, `GetIssuedOrderId()` |
| point | `EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER` (39) | `EVENT_UNIT_ISSUED_POINT_ORDER` | plus `GetOrderPointX/Y/Loc()` |
| widget/unit target | `EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER` (40) | `EVENT_UNIT_ISSUED_TARGET_ORDER` | plus `GetOrderTarget*()` |

Immediate, target, and point orders publish at acceptance. Named Warcraft orders now use the canonical numeric order IDs covered by OpenRealm's stock order table rather than the historical first-four-bytes approximation; build placement remains a special case whose issued order ID is the building rawcode.

## Point orders

`G_IssueUnitPointOrder()` publishes event 39/its unit-scoped counterpart only after the order has been accepted. This applies whether the order starts immediately or is appended by Shift to the per-unit FIFO. Dequeuing the stored order later must **not** publish a second issued-order event.

The issued point retained for the callback is the authoritative point accepted by the order. `GetOrderPointX()` and `GetOrderPointY()` expose its coordinates and `GetOrderPointLoc()` returns an equivalent JASS location handle.

Rally-point changes are also point orders when a point rally target is accepted. They remain immediate producer metadata rather than movement FIFO work, but their issued-point event is still authored at acceptance time.

## Building placement

A successful construction placement is a point order even though it does not pass through ordinary Move/Attack point-order dispatch. Immediate shared callers use `G_IssueBuildOrder()`. Player Shift-placement uses `G_IssueUnitBuildOrder()`, which either starts the build immediately or appends it to the worker FIFO. Both publish the issued-point event once when the snapped construction destination is accepted; delayed execution uses `G_ExecuteBuildOrder()` and does not publish again.

For a build order:

```text
GetOrderedUnit()   -> worker/builder
GetIssuedOrderId() -> building unit rawcode
GetOrderPointX/Y() -> accepted snapped build point
```

Using the structure rawcode for `GetIssuedOrderId()` is important because Warcraft map triggers commonly distinguish the selected construction project by comparing the issued order ID directly with a unit rawcode.

Publication happens before the worker travels to the site, including when Shift leaves that site waiting in the FIFO behind earlier work. Arrival-time/queued-start placement revalidation, resource payment, structure spawning, and construct-start/finish events are separate lifecycle stages and do not re-emit the original point-order event.

## Prologue02 compatibility case

The Orc tutorial map registers `Trig_B4_PlaceBox_Done` as trigger ordinal 122 for player-unit event 39. Runtime tracing showed that the Burrow placement completed and later construction-complete narration ran, but trigger 122 never dispatched. The following lumber tutorial triggers (`Trig_L_HarvestingLumber` and `Trig_L1_HarvestLumber_Q`) therefore never started.

The missing contract was not a trigger-queue or coroutine failure: OpenRealm's construction placement path simply did not publish the issued point-order event. Publishing event 39 from accepted building placement restores the authored handoff without adding map-specific behavior.

## Target orders

`G_IssueUnitTargetOrder()` publishes accepted movement/combat/repair target orders at submission time. The event source preserves the selected target for `GetOrderTarget()`, `GetOrderTargetUnit()`, and `GetOrderTargetDestructable()`. Shift-queued target orders likewise publish once at insertion rather than again when they execute.

## Known limitations

- The numeric order table intentionally covers the established movement orders and stock spell orders that OpenRealm can route through its implemented spell pipeline; it is not yet Warcraft's complete order catalog. Unknown/custom four-character order strings retain the historical FourCC fallback. Build orders use the structure rawcode directly.
- Point rally changes now publish point-order events, but entity-target rally changes still return through the older rally metadata path without target-order publication.
- Shift-queued spell casts are not accepted yet; a spell-aware queued-order representation is required before those can preserve Warcraft cast semantics.
- Order event callback data is only meaningful while handling an issued-order event; callers should not treat the getters as durable unit state.

## Regression coverage

`wc3_api.build_placement_publishes_point_order_event_context` submits a real accepted build order and verifies that a player-unit point-order callback sees the builder, building rawcode, and accepted X/Y coordinates exactly once.

## Immutable callback ownership

ORDER-01.5 fixes a concrete lifetime error: issued-order getters previously read
mutable arrays indexed by the unit edict. Submitting Move, then Smart, then
Stop before draining events made the older point callbacks report Stop and
lose their points. An action issuing another order also overwrote its own
callback values. The public-native regression failed before the fix in
`/tmp/wc3-order-01.5-context-valid-lifecycle-before-fix.log`.

The existing `gameEvent_t.value/point/has_point/source` now owns each accepted
submission's ID, point and target. Event dispatch copies them into the condition
and action context. The C historical publishers/getters remain separate;
JASS issued-event getters use the callback snapshot, never the historical
arrays. Dequeuing a Shift order still does not publish a second submission.

The recovered retail entries are independently registered from placeholder
native727800:

| Native | Entry | Authoritative source |
| --- | --- | --- |
| `GetUnitCurrentOrder` | `2039d0` | Resolved Unit19c/1a0 live user head; order24 command, or0 |
| `GetIssuedOrderId` | `2005b0` | Event-held order via201d20/265f90 or200f40;685cd0 reads command24 |
| `GetOrderPointX/Y` | `201010/201060` | Event-held COrderTarget;685cf0/685d30 read48/50 scalar values |
| `GetOrderPointLoc` | `200f70` | Same event-held point; original20e630 location construction |

Issued IDs accept player events38/39/40 and unit events75/76/77. Point getters
accept only39/76. Unrelated events return0 for ID and scalar queries, and a
null order location; their spell metadata remains available to spell getters.
The regression for the unrelated location failed before its separate guard in
`/tmp/wc3-order-01.5-unrelated-location-before-fix.log`.

JASS context now also preserves `eventType`, so the domain guard survives
suspension. Semantic JASS snapshot version7 saves that discriminator with the
existing value and point; version6 snapshots are rejected. The network protocol
were unchanged by the callback fix (outer W3SV56); the subsequent current-head
field in ORDER-01.4 uses W3SV57. No historical-array fallback is used on
restore. See [save/load](save-load.md).

Tests cover delayed and reentrant point conditions/actions, target snapshots in
both event families, immediate Stop snapshots, unrelated spell metadata, and
both unread events and sleeping actions across save/load. The save fixture
installs the reserved player edict's client pointer and supplies stock unit
event constants76/77 in `Scripts/common.j`; a missing player subject or constant
does not reproduce a valid gameplay registration. Actors supply their normal
health and stand callbacks because the minimal archive has no actor model.

Ghidra's hash-guarded `MapPathfinding.java` and type schema persist the native
names, exact register/stack signatures and partial Unit/order/wrapper layouts.
The saved/read-back artifact is
`/GitHub/wc3-analysis/reports/pathfinding-1.27/order-01.5-ghidra-order-query-types.json`.
Evidence is S plus production engine regressions; this leaf does not claim
the complete original callback producer/subscriber graph. Full `make -j8 test` passed (`/tmp/wc3-order-01.5-full-suite.log`):
36742/36742 engine assertions across2131 tests in each WC3 schema, alongside
the tool/shared/game suites. Production WC3 and SC2 builds passed. Rebuilding JASS with `CC='gcc -DDEBUG_JASS'`
and running both issued-context API and save tests also passed; the normal
JASS library was then rebuilt. The fresh
strict corpus is120/120 at
`/GitHub/wc3-analysis/reports/pathfinding-1.27/order-01.5-issued-context-corpus/corpus-results.json`
(manifest SHA256 `7ada8e7da79716b18097fbc72d37804a4be03ecef12b6b7367d9a8103e30c1b1`;
summary SHA256 `9c02b58de22f36882871d5c001853fbd5fc29ede107ce9cea3c2dcfe9a3bbfc4`).
All recorded sources matched at completion. These retain existing original/live
corpus expectations; the new callback fix is proved by S and engine tests.
The ordinary point-Move active
unit query is independently [ORDER-01.4](retail-pathfinding-todo.md#order-01--arrival-and-failure).

Validation commands:

```sh
LD_LIBRARY_PATH=/tmp/wc3-sdl2-build make -j8 test-wc3-engine WC3_PATTERN='wc3_api.issued*'
LD_LIBRARY_PATH=/tmp/wc3-sdl2-build make -j8 test-wc3-engine WC3_PATTERN='wc3_save.issued_order_context*'
LD_LIBRARY_PATH=/tmp/wc3-sdl2-build make -j8 test
```

## JASS spell orders

`IssueImmediateOrder`, `IssuePointOrder`, and `IssueTargetOrder` now resolve stock spell order names against abilities actually owned by the caster and dispatch through the existing spell pipeline. The corresponding `Issue*OrderById` natives convert canonical Warcraft order IDs through the same table. Point casts currently require the requested point to be in authored cast range; unit-target casts may use the existing walk-into-range behavior. Toggle/autocast semantics are intentionally not synthesized through one-shot spell casting.
