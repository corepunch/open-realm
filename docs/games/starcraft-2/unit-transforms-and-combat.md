# SC2 unit transforms, burial and Tank combat

## Ownership and lifecycle

The game module owns these rules. `g_unit.h` owns burial and unit state, `g_sc2.c`
resolves morph destinations, and `skills/s_attack.c` executes damage. The universal
client passes the existing `hover_value` sample to the SC2 renderer; no network
struct was widened.

`morph <CatalogID>` (with `sv_cheats 1`) and the host's `UnitTransformUnit` binding
replace the catalog type in place. Vital percentages, kills, owner, selection,
map ID, custom values, state flags, AI options and cooldown records survive.
Abilities, weapons, default behaviors and normal property values come from the
new type. Orders/path state are cleared, and model, collision, flight and turret
metadata are refreshed. `UnitFromId` continues to resolve the same entity.

`burrow` is currently a cheat transition, not a racial ability executor. It sets
native read-only `c_unitStateBuried` (bit **0**). Bit **11** is
`c_unitStateHighlightable`, not burial. Burial derives rendering, selection,
movement and targeting restrictions without changing the script-owned Hidden
or MoveSuppressed bits. Unburrow therefore cannot remove a script's independent
locks. Buried units do not participate in collision pushing. Detection and
burrowed combat/movement variants are still outside this prototype.

## Catalog and rendering contracts

The retail reference is the locally supplied Liberty/Core SC2 catalog, not a
project-specific weapon name:

- `CUnit.WeaponArray` contains both `Link` and `Turret`. Indexed turret-only
  overrides preserve the weapon link; an empty `Turret` clears it. Unindexed
  turret-only entries do not create weapons. `CWeapon.TurretName` is not this
  assignment.
- SiegeTankSieged uses `CrucioShockCannon`, whose effect is
  `CrucioShockCannonSet`. That set contains `CrucioShockCannonBlastSet` and
  `CrucioShockCannonDirected`; the nested set contains `CrucioShockCannonBlast`.
- `TargetRadiusSmall` and `TargetRadiusLarge` are
  `CValidatorUnitCompareField` rows: Radius LT 1.25 and GE 1.25 respectively.
  These predicates choose splash for small targets and direct damage for large
  targets, rather than applying both leaves to every shot.
- The Liberty blast has radius/fraction pairs `(0.4687, 1)`, `(0.7812, 0.5)`,
  `(1.25, 0.25)`. SearchFilters admits ground units and excludes self, missiles,
  stasis, dead, hidden and invulnerable units; it does **not** exclude allies.
  Amount and bands are read from data, not constants in the combat loop.
- The stock Tank M3 exposes its yaw joint as `Ref_TurretZ`. The renderer rotates
  that joint's orientation while retaining its translation, so the turret does
  not orbit the model origin. Moving units reset the sampled turret heading to
  their body heading. Morphing to a unit without a turret clears the sample.

`sc2_combat_catalog.h` resolves bounded CEffectSet/CEffectDamage trees, parent
inheritance, indexed array overrides/removals, radius validators, search masks
and per-band fractions. The smallest containing band determines secondary
impact damage. The primary target receives one direct impact per applicable
leaf. Armor and death/event publication use the same game-owned path for primary
and secondary victims. Unsupported graphs/validators produce diagnostics and
fail resolution instead of silently selecting an arbitrary damage leaf.

This is not a general SC2 effect interpreter: missiles, persistent effects,
other validator families, attribute bonuses, shield absorption, cliff/fog
visibility rules and full actor/turret behavior remain unimplemented. Turret yaw
is instantaneous; pitch, multiple independent turrets and authored yaw-rate/idle
policies are not simulated. Morph and burrow command-card abilities still need
ability-specific execution.

## Verification

All regressions use generated fixture archives and synthetic model matrices;
they do not require installed retail assets or an interactive game launch.

```sh
make test-sc2
make opensc2
make test
```

- `sc2_control.transform_preserves_state_across_type_change`: identity, script
  values/locks and vitals through transformation.
- `sc2_control.burrow_command_toggles_burrowed_state`: command and inverse,
  native bit collision, effective movement lock and independent script locks.
- `sc2_control.retail_tank_effects_turret_and_cooldown`: retail-shaped catalog,
  actual attack command and `SC2_RunFrame`, bands, filters/friendly fire, cooldown,
  complementary validators, death/kill credit, a non-stock inherited variant,
  and native Galaxy transformation.
- `sc2_map.weapon_presentation_resolves_damage_and_splash`: nested sets,
  inheritance/partial bands, rejected cycles/validators and turret overrides.
- `sc2_map.turret_pivot_and_snapshot_transport`: joint lookup, pivot-preserving
  matrix rotation and packed heading network round trips.

See also [selection and control](selection-and-control.md),
[unit audit](sc2-unit-types-audit.md) and [cheat commands](../../cheat-commands.md).
