# Command feedback

## Point confirmation

Warcraft point-order acknowledgement uses `UI\Feedback\Confirmation\Confirmation.mdx` as a transient client-local effect. The server emits only the accepted world point and whether the accepted command is an attack-point command; the client reuses the loaded model, grounds it to terrain, advances the authored animation by elapsed time, and drops it after the existing one-second confirmation lifetime.

- ordinary Move/SmartPoint, Rally point, Patrol, and successful point-target spells use green `(0,255,0)` tint;
- explicit Attack-to-point / attack-move uses red `(255,0,0)` tint;
- target-unit clicks do not synthesize `TargetUnitConfirm`, matching the current Warsmash implementation;
- the confirmation is independent of persistent Rally/waypoint presentation.

The generic temporary-event IDs append `TE_ATTACK_CONFIRMATION` after the pre-existing event values so existing missile/impact protocol numbers do not change.

## Persistent command-state markers

Rally uses the generic world-indicator protocol documented in [server-selected-effects.md](../../architecture/server-selected-effects.md) and the gameplay/presentation contract in [rally-points.md](rally-points.md). It is rebuilt from authoritative Rally state instead of being kept alive by the original mouse click.

Warsmash-style queued `WaypointIndicator` flags remain unimplemented because the current OpenRealm order model does not yet expose an equivalent queued player-command target list. Internal movement route waypoints must not be rendered as a substitute.

## Validation

When validating manually:

1. Move/SmartPoint should still show the green confirmation.
2. Attack-to-point should show the same model tinted red.
3. Rally-to-point should show a green transient confirmation plus the persistent Rally marker.
4. Patrol and a successful point-target spell should show green confirmation.
5. Rejected target checks should show no confirmation.
6. Target-unit commands should not display a fabricated point/unit confirmation model.

This implementation change was prepared without compiling or running tests, per the caller constraint.
