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

## JASS spell orders

`IssueImmediateOrder`, `IssuePointOrder`, and `IssueTargetOrder` now resolve stock spell order names against abilities actually owned by the caster and dispatch through the existing spell pipeline. The corresponding `Issue*OrderById` natives convert canonical Warcraft order IDs through the same table. Point casts currently require the requested point to be in authored cast range; unit-target casts may use the existing walk-into-range behavior. Toggle/autocast semantics are intentionally not synthesized through one-shot spell casting.
