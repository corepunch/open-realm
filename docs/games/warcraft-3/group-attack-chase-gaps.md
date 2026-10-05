# Warcraft III Group Attack And Chase Gaps

This document tracks the remaining gaps against the group Attack, chase, and
combat-movement specification. It describes the implementation in
`skills/s_attack.c`, `skills/s_move.c`, and the order queue as it stands after
the propulsion-window, cooldown-deadline, and active-target identity fixes. It
does not claim that
unverified behavior is a retail mismatch; items marked as coverage gaps need a
representative simulation test before changing production code.

## Implemented Baseline

Direct Attack is fanned out to controllable selected units through
`attack_menu_selecttarget()`. Each unit owns an independent Attack order and
resolves the current compatible Attack 1/Attack 2 profile at use sites. The
explicit `goalentity` remains the target while legal; automatic target search
is a separate path. Attack-Move and Follow retain their parent destination
while opportunistic combat runs, and the order queue stores entity targets as
edict number plus `spawn_time`.

UnitData `propWin` is authored in degrees and converted once to radians when
spawning. `SetUnitPropWindow`/`GetUnitPropWindow` use the native radian value.
Movement applies the window in the shared steering step; zero prevents
translation while turning. Move, Follow, and Build leave the Walk/Stand
transition to that shared steering decision so blocked turns do not restart
the Stand animation each tick. The selected
weapon's `rangeBuffer` is used only while its saved simulation-time cooldown
deadline is active; the damage point still rechecks true range. See
[Attack Damage](attack-damage.md) for weapon timing, legality, and projectile
contracts, [Pathfinding](pathfinding.md) for shared route behavior, and
[Shift Order Queue](order-queue.md) for queued-target identity.
The active direct target also retains its edict spawn generation through
save/load, and zero `PropWindow` blocks translation while the unit turns in
place.

## Resolved Fixes And Remaining Gaps

### Attack facing gate and turn animation

The shared movement step now switches to a stable Stand pose when propulsion is
blocked by the facing window. The separate attack-facing tolerance is still a
gap: melee and ranged windup callbacks advance without an attack-facing gate.
Do not reuse `propWin` for that rule; establish the authoritative attack-facing
tolerance before adding the gate and cover both melee and ranged transitions.

### Target distance uses collision geometry

`attack_target_distance()` is shared by maximum- and minimum-range checks. It
measures from the attacker's collision edge to a unit/destructable collision
edge, or to the target's authored pathing footprint when present. Regressions
cover unit collision radii for both maximum and minimum range; the existing
building-footprint test continues to cover structures. A destructable-specific
range regression is still useful.

### Active direct target incarnation is retained

Active direct Attack stores `attack_target_spawn_time` with its goal pointer,
checks the target incarnation before combat callbacks, and persists the field
through save/load. Regression coverage verifies slot reuse rejection and a live
Attack target round trip.

### Unreachable approach retains target geometry

Collision-sized Attack recovery stores its reachable endpoint on the attacker.
Only `SVF_MOVE_WAYPOINT` destinations may adopt the recovered point. The
regression `wc3_combat.unreachable_attack_keeps_target_geometry` covers unit
and destructable targets, including hits after approach and an unreachable
short-range attack. This fixes gates and other scenery shifting during an
attack chase. See [interaction-owned route endpoints](pathfinding.md#interaction-owned-route-endpoints)
for the campaign capture and the distinction from the damage/animation path.

### Long-route moving-target refresh is bounded by relative displacement

Direct steering and the final approach read the target's current coordinates.
For a longer route, `M_RefreshHeatmapForMover()` reuses a cached target field
until the target moves more than 10% of the current mover-to-target distance
and the field is at least 400 ms old. When no mover is supplied, it retains the
64-unit bound. Tests for reversals, obstacle crossings, and small displacement
at short remaining distance are still needed.

## Acceptance Coverage Still Needed

These scenarios have partial implementation support, but the current automated
tests do not establish the full specification contract:

- **Group surrounds:** movement has per-unit collision and attack stops each
  unit based on its own range, with no global surround-slot allocator. Add
  4/12/24/50 melee attackers around a moving unit and a large building. Assert
  no overlap or target-center deadlock, legal pathing, individual range stops,
  and continued approach by blocked units.
- **Moving target chase:** test live direct-target following through an obstacle,
  target reversal, target crossing behind the attacker, and target death. Check
  that the explicit order never switches to a nearer enemy.
- **Profile transform:** `attack_profile()` resolves compatibility from the
  current target when used, but add a ground-to-air transform test proving an
  incompatible swing is stopped and a compatible second profile is selected.
- **Save during actual chase:** current save coverage verifies direct target
  and cooldown fields, but should save after a real canceled windup while the
  target is moving, then verify target identity, cooldown expiry, and route
  regeneration after load.
- **Large battle cost:** run 100 attackers against 20 moving enemies and record
  bounded route requests and test duration. The current design has no central
  surround solver; that alone does not prove the full movement workload meets
  the spec's performance requirement.
- **Deterministic attack rolls:** `attack-damage.md` already records that rolls
  use process `rand()`. Replace it with an authoritative simulation RNG before
  claiming multiplayer/save-load deterministic combat.

Group-membership fan-out, direct-vs-Attack-Move target policy, Shift queue
identity, replacement orders, Stop, and cooldown deadline persistence already
have dedicated implementation paths or regressions. They should remain in the
regression suite while closing the gaps above.

## Verification Pointers

Relevant code and tests:

- `games/warcraft-3/game/skills/s_attack.c`
- `games/warcraft-3/game/skills/s_move.c`
- `games/warcraft-3/game/g_local.h` (`edict_s.movement`, active target pointers,
  and `unitOrder_t`)
- `games/warcraft-3/game/g_save.c` (`edict_fields` and order queue persistence)
- `games/warcraft-3/game/tests/t_combat.c`
- `games/warcraft-3/game/tests/t_movement.c`
- `games/warcraft-3/game/tests/t_order_lifecycle.c`

Run focused Warcraft III tests in both data variants with:

```sh
make test-wc3-engine WC3_PATTERN='wc3_combat.*'
make test-wc3-engine WC3_PATTERN='wc3_movement.*'
make test-wc3-engine WC3_PATTERN='wc3_order_lifecycle.*'
```

For completed implementation work, run `make test` as required by
`CONTRIBUTING.md`.

## See Also

- [Attack Damage](attack-damage.md)
- [Pathfinding And Harvest Reachability](pathfinding.md)
- [Shift Order Queue](order-queue.md)
