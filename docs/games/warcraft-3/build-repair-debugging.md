# Warcraft III Build/Repair approach debugging

Build placement and Repair target two different things: the gameplay target can
be a building or a build-site waypoint, while movement must stop at a legal
interaction point outside blocked geometry.  A structure centre is commonly
inside its authored pathing footprint and must not be used as a reachable flow
field destination.

Repair therefore keeps the building in `ent->build` and routes toward a
collision-safe point beside `building->pathtex`.  Build-site travel uses the
worker collision radius when requesting direct/flow steering.  This mirrors the
resource-gathering rule that a blocked resource/structure remains the behavior
target while movement owns a separate legal approach boundary.

The Repair walk-to-work handoff uses the **current** footprint distance only.
The worker must actually be within `worker collision + Repair Rng` before
entering `stand work`; adding one future movement step to that comparison makes
the walk state hand off early while the work state still sees the worker out of
range.  That creates a walk/work loop in which construction progress and repair
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

## Runtime tracing

Tracing is disabled by default.  Set `wc3_build_repair_debug` at runtime:

- `1` logs Build/Repair order creation, approach selection, transitions into
  work, rejection/stop reasons, construction start/completion, and animation
  resolution failures;
- `2` additionally logs sampled flow/approach state and construction progress
  to birth-frame mapping.

Prefixes are `WC3_BUILD_PATH`, `WC3_REPAIR`, and `WC3_BUILD_ANIM`.  A failed
case should be captured from the order line through the first stop/work line so
path generation, movement, ability validation, and animation ownership can be
distinguished without enabling unrelated diagnostics.
