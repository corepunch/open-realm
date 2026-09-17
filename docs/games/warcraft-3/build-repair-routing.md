# Warcraft III Build/Repair approach routing

Build placement and Repair target two different things: the gameplay target can
be a structure, a mobile mechanical unit, or a build-site waypoint, while
movement must stop at a legal interaction point. A structure centre is commonly
inside its authored pathing footprint and must not be used as a reachable flow
field destination.

Repair keeps the gameplay target in `ent->build`, but routing depends on target
geometry. Buildings route toward a collision-safe point beside
`building->pathtex`; models without a footprint retain the collision-circle
fallback. Repairable mobile mechanical units keep the live unit itself as
`goalentity`, so movement follows its current position rather than baking a
static approach waypoint. Build-site travel uses the worker collision radius
when requesting direct/flow steering. This mirrors the resource-gathering rule
that a blocked resource/structure remains the behavior target while movement
owns a separate legal approach boundary.

The Repair walk-to-work handoff uses the **current** target distance only. For a
building this is the current pathing-footprint distance; for a mobile mechanical
unit it is collision-edge distance. The worker must actually be within Repair
`Rng` (plus `DataE` when the unit's authored movement type is `float`) before
entering `stand work`; adding one future movement step to that comparison makes
the walk state hand off early while the work state still sees the worker out of
range. That creates a walk/work loop in which construction progress and repair
HP never advance and the worker animation appears to restart continuously.

Human construction has two separate contribution rules.  The primary builder
always advances construction at `1.0`.  Repair `DataD` is only the time ratio
for additional power-build workers; a zero/missing `DataD` must not reject the
primary builder.

Construction owns the building `birth` sequence while
`construction.active`.  `AI_HOLD_FRAME` prevents wall-clock animation drift,
but `G_UpdateConstructionAnimation()` maps authoritative
`construction.progress` onto the authored birth frame.  Construction pausing
therefore freezes both progress and the visible model frame.
