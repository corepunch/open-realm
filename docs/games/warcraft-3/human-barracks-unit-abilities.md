# Human Barracks Unit Abilities

## Contract

Human Barracks research is data-driven through `UpgradeData.slk` and each unit
type's `Upgrades Used` list. The runtime applies researched effects to existing
owned units and to newly spawned units through
`G_ApplyPlayerUpgradesToUnit()`. The Human Footman `Adef` command is authored on
the unit before `Rhde` completes; `rlev` gates the command and sets its runtime
ability level to researched level plus one.

## Implemented abilities

- `Adef` / Defend is a toggle owned by `CAbilityDefend`. It installs the
  authored timed status and the `defend` required-animation property.
- Defend applies its authored movement, outgoing attack-speed, outgoing damage,
  and incoming damage modifiers. Basic ranged missiles can be consumed or
  returned to their unit source according to `DataF`, `DataG`, `DataH`, and the
  `Misc.DefendDeflection` switch. Reflected missiles carry
  `edict_t.projectile_reflected` so they cannot reflect again.
- `Rhri` / Long Rifles uses `ratr` to update both runtime attack ranges.
- `Rhan` / Animal War Training uses `rhpx` to update maximum life while keeping
  the current-life ratio. The persistent contribution is stored in
  `edict_t.permanent_health_bonus` so Hero stat recomputation retains it.

## Data flow

```text
UpgradeData.slk + UnitBalance.upgrades
  -> player tech state
  -> G_ApplyUpgradeLevelDelta / G_ApplyPlayerUpgradesToUnit
  -> mutable unit attacks, health, and ability levels
  -> HUD command card and combat/movement/projectile consumers
```

`games/warcraft-3/tests/resources-src/Units/AbilityData.slk` contains the
minimal `DataC` inventory and ability rows needed by the headless tests. Tests
must provide an authored inventory ability when they exercise player inventory
orders; an untyped synthetic map does not receive the RoC Hero inventory
synthesis.

## Known gaps

Attack 2 is initialized and receives research range/stat updates, but combat
orders still select Attack 1. Full Attack 1/Attack 2 selection, custom map
upgrade-object overrides, W3I upgrade-availability records, and ownership
transfer semantics remain unresolved.

## Verification

Run the focused in-engine tests with:

```text
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_building.*'
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_spell.*'
```

The umbrella `make test` target also runs these ability, inventory, combat,
save/load, and fixture checks without requiring a game launch.
