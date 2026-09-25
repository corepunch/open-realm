# Warcraft III Shift Order Queue

## Contract

Player-issued movement/combat orders use a per-unit pending FIFO that is separate from the current behavior and from a producer's training/research queue. The client captures either Shift key when the click is submitted and appends the literal `queue` modifier to the existing text command (`smart`, `smartpoint`, `select`, or `point`). The game module remains authoritative: it decides whether that modifier is meaningful for the active command, owns the queue, resolves targets, and starts the next order when the current behavior completes.

The current implementation deliberately covers the high-confidence Warsmash-compatible core:

- Shift + right-click point (`Smart`) queues movement for mobile units;
- Shift + right-click entity queues Smart target resolution, including attack, harvest, repair, item pickup, persistent follow for a passive allied unit, and fallback point movement for other accepted targets;
- Shift + Move queues point movement;
- Shift + Attack queues either an entity attack or point attack-move;
- Shift + construction keeps the same placement ghost armed, appends later building sites to the worker FIFO, and shows an owner-only Construction Site Indicator for every accepted queued site;
- an idle/Stop/Hold unit starts the first Shift order immediately instead of leaving it pending;
- a busy unit appends Shift orders in FIFO order;
- a normal Move/Attack/Smart replacement clears pending Shift orders;
- Stop and Hold Position clear pending Shift orders;
- death clears pending Shift orders;
- stale/recycled entity targets are skipped when their queued turn arrives;
- invalid queued work is skipped and polling continues to the next pending order.

Patrol multi-waypoint semantics, explicit spell queuing, queued-order waypoint models, and Blizzard 1.29 `BlzQueue*OrderById` natives remain separate work; see [Known gaps](#known-gaps).

## Input protocol

The client does not mutate a unit queue directly. When Shift is down, it sends the same command with a trailing modifier:

```text
smart 57 queue
smart 57 1024 768 queue
smartpoint 1024 768 queue
select 57 queue
point 1024 768 queue
```

Entity Smart clicks may also carry the traced world point as `smart <entity> <x> <y>`. The game normally resolves the entity target exactly as before; the point is only a fallback for an alive walkable destructable whose entity Smart action is rejected. This lets a bridge remain non-attackable by right-click while the same click still becomes formation-aware ground movement to the clicked deck position. Without Shift the corresponding form is `smart 57 1024 768`.


The shared bind layer must still dispatch the ordinary mouse-button command while Shift is held. Explicit modified mouse binds take priority (for example `ALT+MOUSE1 +pan`); when no modified mouse bind exists, `SHIFT+MOUSE1`/`SHIFT+MOUSE2` inherit the plain `MOUSE1`/`MOUSE2` bind. This lets `+select`/`+smart` run and inspect the live Shift state without weakening exact modifier semantics for keyboard hotkeys such as control groups.

`select` and `point` are also used to finish command-card targeting. `menu_t.supports_order_queue` gates the modifier on the server, so only an explicitly queue-capable targeting mode treats Shift as order queuing. Move, Attack, Repair, and construction placement set that flag. Other target modes ignore `queue`, preserving their existing lifecycle until their reservation/cost semantics are implemented deliberately.

For a successful queue-capable target click, `Get_Commands_f()` is not called while Shift remains part of that click. This leaves the targeting callback armed so the player can add several targets/points without reopening the command button. Construction additionally marks `menu_t.order_queue_chained` after the first successful Shift placement. When the final Shift key is released, the client sends `orderqueuerelease`; the server removes the sticky construction ghost only when that chained state is set. Merely tapping Shift before a successful placement is therefore a no-op. A successful non-Shift target returns to the normal command card as before.

A small world click now sends either the entity target or the terrain point, not both. This prevents a unit-target completion from immediately being followed by a second point-target completion at the same cursor coordinate.

## Simulation ownership

`edict_s.order_queue` owns the pending player orders for that unit. The active order is represented by the existing `currentmove`/behavior state and is not duplicated at the front of the FIFO.

Each pending entry stores:

- the normalized order name (`smart`, `move`, `attack`, `repair`, or `build` in the currently supported path);
- point, entity, or construction target kind;
- the resolved point for a point order;
- entity number plus `spawn_time` for an entity order; queued Build entries reuse this otherwise-unused stable pair to identify their owner-only Construction Site Indicator for teardown;
- the issuing player number for future error-routing work;
- an optional rawcode payload (`order_id`) used by delayed construction;
- the movement group-speed cap for a queued formation Move leg.

Entity pointers are intentionally not stored in the queue. An edict slot can be freed and reused before a delayed command reaches the head of the FIFO. Number + `spawn_time` re-resolution makes that stale command fail closed rather than retargeting the new occupant of the same slot.

The FIFO is currently a bounded inline ring (`MAX_UNIT_ORDER_QUEUE`, 16 pending commands). Warsmash itself uses an unbounded `LinkedList<COrder>`; the OpenRealm cap is an implementation safety bound, not a claimed retail/Warsmash limit. Hitting the cap currently rejects the newest queued command without a dedicated command-error message.

## Submission rules

`G_IssueUnitPointOrder()`, `G_IssueUnitTargetOrder()`, and `G_IssueUnitBuildOrder()` are the queue-aware entry points. Existing `unit_issueorder()` and `unit_issuetargetorder()` remain compatibility wrappers that submit `queue=false`. `G_IssueBuildOrder()` remains the immediate shared construction API used by AI/JASS-facing callers.

For a supported order:

```text
queue=false
    -> clear pending FIFO
    -> start the replacement order now

queue=true + current active behavior
    -> append to pending FIFO
    -> leave current behavior untouched

queue=true + no active behavior
    -> start the order now
    -> leave pending FIFO unchanged
```

The implementation treats a `umove_t` whose `ability` pointer is non-null as active player-order work. Normal stand/hold states have no ability and therefore accept a first Shift command immediately. The terminal blocked-Move hold pose is explicitly recognized as completed order work; a later Shift command can start from that state instead of becoming stranded behind it.

Rally changes remain producer metadata, not unit behavior. Smart/set-rally changes are therefore applied immediately; they are not inserted into the movement/combat FIFO.

Issued-order trigger events describe command submission rather than delayed execution. An accepted point order publishes `EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER` / `EVENT_UNIT_ISSUED_POINT_ORDER` immediately, including when Shift causes the order to be appended to the FIFO; an accepted entity target similarly publishes the target-order family. Construction follows the same rule: `G_IssueUnitBuildOrder()` publishes the building rawcode and snapped point when the click is accepted, while delayed execution calls `G_ExecuteBuildOrder()` and does not publish a second event. See [Issued Target and Point Order Events](issued-target-order-events.md).

## Move and formation behavior

The existing `move_selectlocation()` formation allocator remains authoritative for a command-card or SmartPoint Move click. It resolves the per-unit slot and group speed at issue time.

For a non-queued Move it retains the shared route-waypoint optimization and starts the existing Move behavior immediately. For a queued Move it stores each unit's resolved slot and group-speed cap in that unit's FIFO. When the leg begins later, OpenRealm creates the normal private waypoint and reuses the existing `order_move()` behavior.

This preserves two important properties:

1. each selected unit owns an independent queue and may advance to the next leg at a different time;
2. queue support does not duplicate or replace the existing pathing, avoidance, formation-slot, and Move-completion code.

## Attack and Smart behavior

A queued point `attack` starts the existing `order_attackmove()` behavior when dequeued. This also corrects the generic `unit_issueorder(..., "attack", point)` path, which previously routed point Attack through ordinary Move.

A queued entity `attack` re-resolves the target and calls the existing direct Attack behavior.

A queued entity `smart` re-runs the existing Smart decision when the command begins rather than baking a guessed behavior into the queue. Depending on the target and current unit state, that can select:

- inventory item pickup;
- gold or lumber harvesting;
- resource return;
- destructable attack;
- enemy attack;
- Smart repair;
- persistent follow when the target is a live passive allied unit;
- fallback movement to the target's then-current position for other accepted targets.

This revalidation is important because a delayed Smart target can change state before its turn arrives.

## Completion and polling

`unit_stand()` is the common completion edge for many existing unit behaviors. It now clears the transient finished-behavior presentation fields and calls `G_UnitStartNextQueuedOrder()` before installing the ordinary stand/hold default behavior.

The blocked/unreachable Move terminal path (`move_hold`) also polls the FIFO before entering its legacy stationary pose. This avoids losing a queued chain when one Move leg cannot make further progress.

`G_UnitStartNextQueuedOrder()` repeatedly pops FIFO entries until one starts successfully or the FIFO is empty. For an entity target it validates:

```text
target_number < globals.num_edicts
entity is still in use
entity spawn_time still matches
```

Only then does it run the existing order resolver. A stale or no-longer-accepted order is discarded and the next queued order is tried. This mirrors the important Warsmash property that delayed orders are validated when they actually begin rather than assumed valid forever.

## Replacement, Stop, Hold, and death

A normal queue-aware Move/Attack/Smart order clears the pending FIFO before starting. This gives the expected replacement behavior:

```text
current A
pending B, C
normal X

=> X becomes current; B and C are discarded
```

`order_stop()` clears the FIFO before standing, so all Stop callers get the same cancellation policy. Hold Position explicitly clears the FIFO, then installs the existing holding-position state. `unit_die()` clears the FIFO before entering the death lifecycle.

OpenRealm does not yet have Warsmash's per-ability `onCancelFromQueue()` reservation callback. Queued construction does not reserve gold/lumber at insertion time: the placement is validated when clicked, and `G_ExecuteBuildOrder()` revalidates command state and placement when that queued build actually begins. Spell queueing remains disabled because mana/cooldown/cast reservations need an explicit policy first.

## Save/load

The queue is inline numeric data inside `edict_t`; it contains no process pointers, so it persists with the existing raw-edict save record without adding an `F_EDICT` field. Entity targets remain number + `spawn_time` and are re-resolved only at execution.

Adding the queue changes `sizeof(edict_t)`, so the save header's `edict_size` guard rejects older incompatible raw-struct saves independently of the outer `W3SV` format version. The current outer format is version 17; its evolution and compatibility policy are tracked in [Save/Load](save-load.md). The transient menu flags are still process-local: `WriteClient()` and `ReadClient()` explicitly clear `supports_order_queue`, `order_queued`, and `order_queue_chained` because targeting callbacks/menu modes are rebuilt rather than persisted.

The existing save/load limitation still applies: arbitrary active `umove_t` behavior identity is not semantically restored. Pending queue records are persisted, but exact mid-order resume requires the separate active-behavior save work described in [Save/Load](save-load.md).

## Known gaps

The following are intentionally not guessed in this patch:

- **Queued waypoint models.** Warsmash derives waypoint indicators from the current queued order plus pending orders. OpenRealm has no server-to-client order-queue presentation channel yet.
- **Patrol Shift extension.** Current OpenRealm Patrol owns two waypoints and cycles forever. Warsmash can append patrol points to an active patrol behavior; implementing that requires changing the patrol state representation rather than pretending it is a normal Move FIFO.
- **Explicit spell queuing.** Spell mana/cooldown/target validation and cast lifecycle must define when cost/reservations happen before queueing is enabled.
- **General queued construction reservations.** Shift placement is implemented, but queued sites do not reserve resources or pathing footprints before their turn; they are revalidated when execution begins.
- **Generic cancellation callbacks.** Warsmash abilities have `onCancelFromQueue()`; OpenRealm's supported queued families currently need no reservation cleanup.
- **Uninterruptible replacement policy.** Warsmash can replace pending future work while letting an uninterruptible current behavior finish. OpenRealm does not yet expose a general `interruptable()` behavior contract.
- **`BlzQueue*OrderById` natives.** The 1.29 native declarations exist in Warcraft data, but OpenRealm's current order-ID mapping is not yet a safe basis for implementing them here.
- **Queue-full feedback and exact queue length parity.** The current 16-entry safety cap has no dedicated Warcraft command error and is not claimed as a retail limit.
- **Shift-click selection toggle.** This remains a selection-system gap and is separate from Shift order queuing.

## Verification

The unit suite contains coverage for:

- first Shift Move starting immediately while idle;
- FIFO progression across later Shift Move legs;
- a non-Shift replacement clearing pending work;
- stale entity-target rejection followed by the next valid order;
- Stop clearing the pending queue;
- point `attack` selecting attack-move rather than ordinary Move;
- construction placement staying armed across successful Shift clicks;
- construction queue entries retaining the building rawcode and point;
- final Shift release removing only the placement overlay after a successful chain;
- Shift release before any successful placement leaving the overlay active;
- invalid placement, selection replacement, selected-worker death, right-click cancellation, explicit cancel, and command-card rebuild cursor lifecycles.

After building locally, useful targeted checks are:

```bash
make test-wc3-engine WC3_PATTERN='wc3_unit.*'
```

Runtime checks should additionally cover:

1. select one unit, Shift-right-click three terrain points, and confirm they are visited in order;
2. issue a normal Move halfway through and confirm the remaining chain is discarded;
3. Shift-right-click an enemy after two Move points and confirm attack begins only after both legs;
4. queue an enemy that dies before its turn, followed by another Move, and confirm the unit continues to the Move;
5. select several units and confirm each advances independently through the same issued chain;
6. click Move or Attack once, hold Shift, and click several valid targets/points without reopening the command button;
7. press Stop or Hold Position with queued work and confirm no old queued command resumes afterward;
8. hold Shift while placing several buildings, confirm the same movable ghost remains active and every accepted queued site receives a translucent placeholder, then release the final Shift key and confirm only the movable ghost is removed while accepted-site placeholders and construction orders remain queued;
9. replace or clear the worker queue and confirm all delayed construction placeholders disappear; let a queued Build begin normally and confirm its delayed placeholder is replaced by the ordinary active-build indicator rather than duplicated.

## See also

- [Building Construction](building-construction.md) — construction placement validation, cursor ownership, arrival-time revalidation, and race-specific construction lifecycles.
- [Issued Target and Point Order Events](issued-target-order-events.md) — submission-time trigger publication for immediate and queued orders.
