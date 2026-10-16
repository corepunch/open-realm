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
- active/inactive state;
- the native edge ID and retained allocation-attempt state.

The destination is an arbitrary point, not a pointer to another gate. This
supports one-way gates and destinations that contain no gate entity.
`WaygateSetDestination`, `WaygateGetDestinationX/Y`, `WaygateActivate`, and
`WaygateIsActive` operate on this state. Activation also adds/removes the
persistent `alternate` animation property used by the active gate model, even
when exhaustion leaves the native edge inactive.

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

Save format version 45 adds Way Gate runtime state and
in-flight approach state to the expanded `edict_t`. The movement schema relocates both `waygate_target` and
`waygate_goal` through `F_EDICT`; `waygate_target_spawn_time` remains the
incarnation guard, and `currentmove` continues through the existing `F_MMOVE`
relocation. A save taken during an explicit gate approach therefore resumes the
same guarded target and waypoint after load. Version 44 saves are rejected by
the exact-version guard, independently of the `edict_t` header-size check.

## Remaining gap: automatic portal routing

The retail 1.27 binary producer, source-cell stamping, and search-consumer
chains are mapped in [retail-pathfinding.md](retail-pathfinding.md#special-edges-are-way-gate-records).
That evidence is separate from the OpenRealm implementation below.

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

## Native allocation lifetime

[Payoff93](retail-pathfinding-engine.md#way-gate-exhaustion-retains-ability-owned-allocation)
ports the original 1..255 identity pool. Awrp initialization reserves the lowest
free ID once, including inactive gates. The 256th gate retains ID zero: public
activation remains false and destination getters return zero. Setters do not
retry after another gate is removed; removing/recreating Awrp permits allocation
again. Animation follows the requested activation independently of ID zero.

The ability state owns its ID; availability is reconstructed from live owned
records, including after load. A generic `A_UNIT_REMOVING` notification releases
the ID when RemoveUnit takes effect, before deferred edict memory reclamation,
so a creation in the same script callback can reuse it. Later removal cleanup
is idempotent. Save98 retains allocation attempts, allocated IDs and exhaustion,
rejects duplicate ownership and invalid active/configured zero IDs, and rejects
older save versions. Two full 256-gate save/load cycles and immediate ID reuse
are covered by the normal game tests. Automatic portal routing is now integrated as described below.

## Source overlap publication

Source creation publishes even while inactive; later creation overwrites overlap
IDs. Removal clears its full source rectangle without restoring another live
gate's overwritten bytes. The adaptive parents subdivide clear marked cells,
so this affects ordinary routes even with traversal disabled. Save99 retains
marker and class history directly. See [the verified engine port](retail-pathfinding-engine.md#way-gate-overlap-publishes-ordinary-routing-history).

## Automatic Move traversal

[Payoff95](retail-pathfinding-engine.md#way-gate-special-edges-reach-retained-move-routes)
connects ability-owned gate IDs,source markers and quantized exit records to
member/group adaptive searches. Move consumes portal sentinels using the cached
route exit and the current active bit. Retarget affects fresh searches; disable
skips a retained crossing and permits ordinary walking where terrain allows it.
Traversal retains the Move order,integrates existing velocity and preserves
region notifications. This is separate from explicit Smart's approach behavior.

Two retail repeats and all410 production motion commits match cached retarget,
fresh retarget and disabled walking through final arrival. Ten save checkpoints
also match3094 continuation commits. The JASS destination getters still expose
stored script coordinates; native coarse-record getter quantization remains
unverified in the engine. Blocked exits,ID reuse,chained gates and paired group
regrouping retain their existing backlog requirements.
