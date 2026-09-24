# Warcraft III Way Gates

## Runtime ownership

Way Gate gameplay is owned by `skills/s_waygate.c`. `Awrp` is registered as an
innate passive whose `CAbilityWarp` procedure owns Smart validation, persistent
approach state, movement-leave/interruption cleanup, traversal, and completion.
`m_unit.c` only exposes generic ability hooks: target-owned Smart interactions
and a post-accept notification for immediate/spell orders that do not replace
the current move themselves.

`Awrp`-derived aliases retain their authored level data. `DataA` / `DataB`
(`Wrp1` / `Wrp2`) define the rectangular entry width and height. Way Gate use
does not invent a `targs`-based ground/air/structure restriction: the reference
behavior accepts a live movable unit and performs the exact rectangle test.

Each gate stores independent runtime state on its edict:

- destination X/Y;
- whether a destination has been assigned;
- active/inactive state.

The destination is an arbitrary point, not a pointer to another gate. This
supports one-way gates and destinations that contain no gate entity.
`WaygateSetDestination`, `WaygateGetDestinationX/Y`, `WaygateActivate`, and
`WaygateIsActive` operate on this state. Activation also adds/removes the
persistent `alternate` animation property used by the active gate model.

Generated map script normally configures preplaced gates through those JASS
natives. The raw `war3mapUnits.doo` Way Gate destination ordinal is retained by
the common world reader, but the optional raw-unit placement path does not yet
resolve that ordinal through `war3map.w3r`; script-configured gates are the
supported path.

## Explicit Smart traversal

Smart/right-click offers the target's authored abilities a generic target-order
hook before relation-based attack/follow fallback. An active configured Way
Gate consumes `smart`; an inactive/unconfigured gate does not.

A movable unit whose centre is already inside the authored rectangle teleports
immediately. Otherwise `CAbilityWarp` installs its own walk behavior and stores:

- the authoritative gate pointer;
- that gate's `spawn_time` incarnation;
- the approach waypoint/entity owned by the behavior.

Those pointers are separate from shared movement fields such as
`secondarygoal`, so leaving Way Gate behavior cannot erase another ability's
state. Replacing the move publishes the existing generic `A_MOVE_LEAVE` event;
accepted immediate/spell orders use `A_ORDER_ACCEPTED` when no new move was
installed. Rejected and Shift-queued replacement orders do not publish either
transition, so the in-flight gate order remains authoritative.

The approach point comes from
`CM_ClosestStaticPathablePointInRectForRadiusFlags()`. The generic router checks
every pathmap cell intersecting the authored rectangle and respects the mover's
collision radius and movement-class static pathing. It deliberately ignores
live-unit occupancy. Temporary crowds are resolved by normal move-time
collision/local avoidance rather than making a durable gate entrance appear
unusable at order submission.

While approaching, the behavior rechecks gate incarnation, activation,
destination assignment, liveness, movability, and the exact rectangular entry.
The destination itself is read at traversal time, so a script may retarget a
gate while a unit is walking toward it. Disabling or recycling the gate cancels
the traversal without teleporting the unit.

Relocation uses `G_FindUnitUnstuckPosition`, updates the authoritative XY
origin, dirties fog blockers when necessary, relinks the same edict, and emits
the Move ability's authored special effect at source and destination. Unit
identity, selection, ownership, stats, inventory, buffs, and queued orders are
not recreated. Completion enters `unit_stand()`, which starts the next
Shift-queued order through the normal queue lifecycle.

Because traversal is initiated by one explicit order, arriving inside another
gate does not recursively teleport during the same simulation update.

## Save/load

The current save format version remains 44. Way Gate adds runtime state and
in-flight approach state to the expanded `edict_t`. The movement schema relocates both `waygate_target` and
`waygate_goal` through `F_EDICT`; `waygate_target_spawn_time` remains the
incarnation guard, and `currentmove` continues through the existing `F_MMOVE`
relocation. A save taken during an explicit gate approach therefore resumes the
same guarded target and waypoint after load. Saves with the previous `edict_t`
size are rejected by the existing header-size guard.

## Remaining gap: automatic portal routing

Retail Warcraft III can choose a Way Gate while processing an ordinary distant
movement order when the portal route is preferable to walking. OpenRealm does
**not** implement that discovery yet.

The shared router remains game-agnostic. The rectangle helper above is generic
static pathing math; it does not know about `Awrp`, destinations, activation, or
portal edges. Future automatic traversal needs a game-side navigation extension
(or a genuinely generic portal-edge contract) that compares:

```text
walk to entrance + portal transition + walk from destination
```

against the ordinary route, invalidates it when a gate is disabled/retargeted,
and preserves the original Move/Attack-Move goal after traversal. Multi-gate
routes and disconnected terrain belong to that same portal-routing design.

## Regression coverage

`game/tests/t_waygate.c` covers runtime/JASS state, true/false activation,
`alternate` presentation, custom `Awrp` aliases, rectangular explicit Smart
traversal, inactive fallback, gate-generation rejection, live activation and
destination revalidation, rejected versus accepted replacement orders, Stop,
Hold Position, Shift queueing, the accepted-instant-order cleanup hook, and a
save/load taken during an active approach.

`game/tests/t_pathfinding.c` covers sub-cell interaction rectangles, skipping a
statically blocked intersecting cell, and the static-only contract under live
unit occupancy.

The earlier full-suite report exposed a fixture bug in the JASS native test:
`UnitAddAbility(g, 'Awrp')` was called without an `Awrp` row in the synthetic
`AbilityData`, so the add failed and the first destination getter returned zero.
The regression fixture now installs both stock `Awrp` and a derived `Zwrp` row
and asserts that `UnitAddAbility` succeeds before exercising the five natives.

Patches prepared without local execution should be verified with at least:

```sh
make test-wc3-engine WC3_PATTERN='wc3_waygate.*'
make test-wc3-engine WC3_PATTERN='wc3_pathfinding.static_rect_query*'
make test-wc3-engine WC3_PATTERN='wc3_save.waygate*'
```

Then run the normal full test target before merge.
