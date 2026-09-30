# Warcraft III Group Attack And Chase Gaps

This document tracks the remaining gaps against the group Attack, chase, and
combat-movement specification. It describes the implementation in
`skills/s_attack.c`, `skills/s_move.c`, and the order queue as it stands after
the propulsion-window and cooldown-deadline fixes. It does not claim that
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
Movement applies the window in the shared steering step. The selected
weapon's `rangeBuffer` is used only while its saved simulation-time cooldown
deadline is active; the damage point still rechecks true range. See
[Attack Damage](attack-damage.md) for weapon timing, legality, and projectile
contracts, [Pathfinding](pathfinding.md) for shared route behavior, and
[Shift Order Queue](order-queue.md) for queued-target identity.

## Confirmed Parity Gaps

### Attack facing gate and turn animation

The attack behavior calls `unit_changeangle()` while in range, but melee and
ranged windup callbacks proceed through `unit_runwait()` without waiting for a
separate attack-facing tolerance. An in-range target behind the attacker can
therefore receive damage or a projectile while the attacker is still turning.
The movement `propWin` gate does not provide this attack gate: it only controls
translation. Also, `unit_moveindirection_policy()` returns when outside the
propulsion window but leaves the active walk animation selected, so a unit can
play Walk while turning without translating.

The spec requires these to be separate rules: turn-rate movement, propulsion
window, and attack-facing tolerance. Add a facing predicate to attack windup
progression and a stable Stand/Ready pose while a walking move is turning in
place. Cover melee and ranged attacks with the target initially behind the
attacker, including the transition through the facing window.

### Target distance does not consistently use collision geometry

`attack_target_out_of_range_for_mode()` uses distance to the pathing footprint
for structure targets with a path texture. Other targets use origin-to-origin
distance. `attack_target_too_close_for()` also uses origin distance. This leaves
unit collision radii and destructable target geometry out of normal maximum-
and minimum-range checks, despite movement routing using collision-sized
pathing.

Route range checks through one target-distance helper that accounts for the
attacker collision edge and the target's supported collision/footprint
semantics. Add tests for two units with nonzero collision, a large destructable,
and the existing building-footprint case; cover both max and minimum range.

### Active direct target has no incarnation guard

Queued target orders retain `target_number` and `target_spawn_time`, but active
Attack uses `goalentity`/`combatentity` pointers without a companion spawn
generation. `S_AttackCanTarget()` verifies the edict is currently in use and
legal, but it cannot distinguish the original target from a compatible entity
that later reuses the same edict slot. Direct Attack can consequently follow a
reused slot if that slot becomes a valid target before the order is checked.

Retain the active Attack target's spawn generation and validate it before
range, damage, resume, and save/load transitions. Reuse the queue's entity
identity contract rather than adding a group-level target object. Test removal,
slot reuse by a compatible target, and save/load of a live direct Attack.

### Long-route moving-target refresh differs from the spec threshold

Direct steering and the final approach read the target's current coordinates.
For a longer route, `M_RefreshHeatmapForMover()` reuses a cached target field
until the target has moved at least 64 world units **and** the field is at least
400 ms old. The specification describes invalidating a longer route when
target displacement becomes significant relative to the current attacker to
target distance (approximately 10%). The current fixed distance/time policy is
bounded, but it is not that relative threshold and can retain an obsolete route
when a nearby target moves less than 64 units around an obstacle.

Add tests for a target reversing direction or crossing an obstacle during a
long route, and for small displacement at short remaining distance. Compare the
relative threshold with current bounded behavior before changing it; avoid
requesting a new full route every simulation tick.

### Zero propulsion-window semantics need an explicit contract

The shared movement step only enforces `PropWindow` when the runtime value is
positive. A zero value therefore permits propulsion at any facing error. The
authored UnitData field is documented in degrees and stock object metadata
expects a positive authored window, while the native accepts radians directly.
The intended behavior of native `SetUnitPropWindow(unit, 0)` has not been
established here. Keep this as an explicit open question rather than treating
the permissive branch as a proven retail fallback. Resolve it from authoritative
native behavior/data and test zero separately from an omitted/default value.

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
- `games/warcraft-3/game/tests/t_order.c`

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
