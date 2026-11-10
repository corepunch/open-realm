# SC2 Unit Types Audit

Audit of StarCraft II unit type support in the OpenWarcraft3 engine, covering catalog resolution,
spawning, simulation, and gameplay mechanics. Date: 2026-11-10.

## How Unit Types Work

SC2 units are not hardcoded — they are resolved dynamically from XML catalog data at map load time:

1. **Catalog layers** are loaded in order: `Core.SC2Mod` → `Liberty.SC2Mod` → `LibertyMulti.SC2Mod` → campaign mods → map-local overrides.
2. Each `CUnit` entry in `GameData/UnitData.xml` defines parent inheritance, vitals (Life/Energy/Shields), speed, mover type, footprint, abilities, weapons, behaviors, and target flags.
3. `SC2_MapResolveUnit()` walks the catalog parent chain to resolve all inherited properties into a concrete `sc2MapObject_t`.
4. At spawn time, `SC2_UnitInit()` initializes an edict with resolved vitals, speed, height, radius, supplies, ability/weapon/behavior link arrays, and target attribute flags.

The engine **does not hardcode unit type names** — any CUnit entry present in the loaded catalog data can be spawned via `spawn <CatalogID>` or Galaxy `UnitCreate()`. The completeness of unit support therefore depends on:
- Whether the user's SC2 data installation contains the catalog XML with the unit definition
- Whether the engine's simulation supports the gameplay mechanics that unit requires

## Currently Supported Unit Types

The unit testing cookbook ([unit-testing-cookbook.md](../../unit-testing-cookbook.md)) documents these verified catalog IDs:

### Terran

| Unit               | Catalog ID         | Spawnable | Movement | Combat | Notes |
|--------------------|--------------------|-----------|---------:|-------:|-------|
| SCV                | `SCV`              | ✅        | ✅       | ❌     | No gathering/repair |
| Marine             | `Marine`           | ✅        | ✅       | ✅     | Basic infantry, weapon fires |
| Marauder           | `Marauder`         | ✅        | ✅       | ✅     | |
| Reaper             | `Reaper`           | ✅        | ✅       | ✅     | No cliff jumping |
| Ghost              | `Ghost`            | ✅        | ✅       | ✅     | No Snipe/EMP/Cloak |
| Hellion            | `Hellion`          | ✅        | ✅       | ✅     | No Hellbat transform |
| Siege Tank         | `SiegeTank`        | ✅        | ✅       | ✅     | Mobile mode only |
| Siege Tank (sieged)| `SiegeTankSieged`  | ✅        | ❌       | ✅     | Spawns stationary, no un-siege |
| Thor               | `Thor`             | ✅        | ✅       | ✅     | |
| Viking (air)       | `VikingFighter`    | ✅        | ✅       | ✅     | Flying; no transform to ground |
| Viking (ground)    | `VikingAssault`    | ✅        | ✅       | ✅     | No transform to air |
| Medivac            | `Medivac`          | ✅        | ✅       | ❌     | No heal/load/unload |
| Banshee            | `Banshee`          | ✅        | ✅       | ✅     | No cloak |
| Battlecruiser      | `Battlecruiser`    | ✅        | ✅       | ✅     | No Yamato Cannon/Tactical Jump |
| Raven              | `Raven`            | ✅        | ✅       | ❌     | No abilities (turret/seeker/scan) |

### Protoss

| Unit               | Catalog ID         | Spawnable | Movement | Combat | Notes |
|--------------------|--------------------|-----------|---------:|-------:|-------|
| Probe              | `Probe`            | ✅        | ✅       | ❌     | No gathering/warp-in |
| Zealot             | `Zealot`           | ✅        | ✅       | ✅     | No Charge |
| Stalker            | `Stalker`          | ✅        | ✅       | ✅     | No Blink |
| Sentry             | `Sentry`           | ✅        | ✅       | ✅     | No Guardian Shield/Force Field/Hallucination |
| High Templar       | `HighTemplar`      | ✅        | ✅       | ❌     | No Feedback/Psionic Storm/Archon merge |
| Dark Templar       | `DarkTemplar`      | ✅        | ✅       | ✅     | No permanent cloak/Archon merge |
| Archon             | `Archon`           | ✅        | ✅       | ✅     | Spawns directly; no merge process |
| Immortal           | `Immortal`         | ✅        | ✅       | ✅     | No Barrier |
| Colossus           | `Colossus`         | ✅        | ✅       | ✅     | No cliff walking |
| Observer           | `Observer`         | ✅        | ✅       | ❌     | No detection aura |
| Warp Prism         | `WarpPrism`        | ✅        | ✅       | ❌     | No phase/transport modes |
| Phoenix            | `Phoenix`          | ✅        | ✅       | ✅     | No Graviton Beam |
| Void Ray           | `VoidRay`          | ✅        | ✅       | ✅     | No Prismatic Alignment |
| Carrier            | `Carrier`          | ✅        | ✅       | ❌     | No Interceptor spawning |
| Mothership         | `Mothership`       | ✅        | ✅       | ❌     | No cloaking field/recall/time warp |

### Zerg

| Unit               | Catalog ID         | Spawnable | Movement | Combat | Notes |
|--------------------|--------------------|-----------|---------:|-------:|-------|
| Drone              | `Drone`            | ✅        | ✅       | ❌     | No gathering/morph to building |
| Larva              | `Larva`            | ✅        | ❌       | ❌     | No morph/production |
| Overlord           | `Overlord`         | ✅        | ✅       | ❌     | No supply aura/transport/morph |
| Zergling           | `Zergling`         | ✅        | ✅       | ✅     | No Baneling morph |
| Baneling           | `Baneling`         | ✅        | ✅       | ✅     | No self-destruct/burrow |
| Roach              | `Roach`            | ✅        | ✅       | ✅     | No burrow/tunneling claws |
| Hydralisk          | `Hydralisk`        | ✅        | ✅       | ✅     | |
| Mutalisk           | `Mutalisk`         | ✅        | ✅       | ✅     | No bounce attack |
| Corruptor          | `Corruptor`        | ✅        | ✅       | ✅     | No Corruption/morph to Brood Lord |
| Infestor           | `Infestor`         | ✅        | ✅       | ❌     | No Neural Parasite/Fungal/Infested Terran |
| Ultralisk          | `Ultralisk`        | ✅        | ✅       | ✅     | No cleave mechanics |
| Brood Lord         | `BroodLord`        | ✅        | ✅       | ✅     | No broodling spawning |
| Overseer           | `Overseer`         | ✅        | ✅       | ❌     | No detection/changeling |
| Queen              | `Queen`            | ✅        | ✅       | ✅     | No inject/creep tumor/transfuse |

## Units NOT in the Cookbook (But Likely in Full Catalog Data)

These units exist in retail SC2 `Liberty.SC2Mod` but are not listed in the testing cookbook. They should resolve via `spawn` if the user has full SC2 data installed:

### Terran (Missing from Cookbook)

| Unit               | Expected Catalog ID   | Special Mechanic Required |
|--------------------|-----------------------|---------------------------|
| MULE               | `MULE`                | Timed life, mineral gathering |
| Hellbat            | `HellionTank`         | Transform from/to Hellion |
| Widow Mine         | `WidowMine`           | Burrow, sentinel targeting |
| Cyclone            | `Cyclone`             | Lock-on targeting |
| Liberator          | `Liberator`           | Mode transform (air/ground) |
| Liberator (AG)     | `LiberatorAG`         | Defender mode zone |

### Protoss (Missing from Cookbook)

| Unit               | Expected Catalog ID   | Special Mechanic Required |
|--------------------|-----------------------|---------------------------|
| Adept              | `Adept`               | Shade teleport |
| Disruptor          | `Disruptor`           | Purification Nova |
| Tempest            | `Tempest`             | Extended range |
| Oracle             | `Oracle`              | Revelation/Stasis Ward |

### Zerg (Missing from Cookbook)

| Unit               | Expected Catalog ID   | Special Mechanic Required |
|--------------------|-----------------------|---------------------------|
| Ravager            | `Ravager`             | Corrosive Bile (skillshot) |
| Lurker             | `Lurker`              | Burrow requirement |
| Swarm Host         | `SwarmHostMP`         | Timed spawns |
| Viper              | `Viper`               | Abduct/Blinding Cloud/Parasitic Bomb |
| Nydus Worm         | `NydusWorm`           | Tunnel transport |

### Campaign-Only (Liberty Story)

Campaign maps from `LibertyStory.SC2Campaign` define additional unit variants (Firebat, Goliath, Diamondback, Spectre, etc.) that are only available when running those specific campaign maps.

## Simulation Gaps

The engine's SC2 simulation provides a baseline of:

### What Works
- **Spawning**: Any catalog-defined unit resolves through `SC2_MapResolveUnit` with full parent inheritance
- **Movement**: Ground (`Mover=Ground`) and air (`Mover=Fly`) movement with shared WC3 pathfinding router
- **Vitals**: Life, energy, and shields with regeneration rates from catalog data
- **Basic combat**: Weapon range/period/damage from `CWeapon` catalog, direct life damage, armor subtraction
- **Attack orders**: Move, stop, hold position, patrol, attack-move, target attack
- **Selection/control**: Click selection, control groups, Smart orders
- **HUD**: Command card buttons resolved from `CUnit.CardLayouts` + `CButton` catalog
- **Galaxy scripting**: Unit creation/removal/property changes through Galaxy VM natives
- **Target flags**: Air/Ground/Light/Armored/Biological/Robotic/Mechanical/Psionic/Massive/Structure/etc.
- **Behaviors**: Script-managed behavior stacks with timed durations
- **Cooldowns/charges**: Per-unit cooldown and charge tracking

### Major Gaps — Not Implemented

| Feature | Affected Units | Impact |
|---------|----------------|--------|
| **Unit morphing/transformation** | Siege Tank ↔ Sieged, Viking ↔ Assault, Hellion ↔ Hellbat, Liberator modes, Warp Prism modes, Lurker burrow | Units exist as separate catalog entries; no runtime transform between them |
| **Burrow** | Roach, Infestor, Baneling, Zergling, Widow Mine, Lurker | No burrow/unburrow state machine |
| **Cloak/stealth** | Ghost, Dark Templar, Banshee, Observer, Mothership | No visibility/detection system |
| **Transport/cargo** | Medivac, Warp Prism, Overlord, Nydus Worm | No load/unload/cargo mechanics |
| **Special abilities (CAbil)** | Nearly all units | Abilities listed in CUnit.AbilArray are bookkeeping only — `s_orders.c` implements Move/Stop/Hold/Patrol/Attack but no specific CAbil behaviors |
| **Effect system (CEffect)** | All damage, healing, spawning | `SC2_RunOrders` does direct life subtraction; no CEffect chain evaluation |
| **Splash/bounce damage** | Siege Tank sieged, Colossus, Mutalisk, Baneling | Single-target damage only |
| **Resource gathering** | SCV, Probe, Drone, MULE | No worker AI or resource mechanics |
| **Building construction** | All structures | No building placement/construction lifecycle |
| **Production/training** | All production buildings, Larva | No unit training queues |
| **Upgrades/research** | All tech buildings | No upgrade system |
| **Warp-in** | Gateway → Warpgate units | No warp-in mechanic |
| **Autocast** | Queen (Transfuse), Medivac (Heal), etc. | No autocast framework |
| **Timed life** | MULE, Broodlings, Interceptors, summons | No timed-life expiry |
| **Death model swap** | All units | No death model/ragdoll; death plays "Death" animation |
| **Projectile effects** | Ranged units | Damage is instant; no projectile travel |
| **Shield armor** | All Protoss units | Shield damage uses life armor, not separate shield armor value |

## Architecture Notes

### Why Most Units "Just Work" for Spawning

The catalog-driven design means the engine does not need per-unit-type code to spawn and render a unit. As long as:
1. The `CUnit` entry exists in the loaded catalog XML
2. The `CActorUnit` → `CModel` chain resolves to a valid `.m3` file
3. The `.m3` file is present in the SC2 data installation

...the unit will appear, move (if its Mover is not `None`/`Stationary`), display its command card, and participate in basic combat.

### What Would Need Per-Unit Code

Mechanics that go beyond the catalog's static data require simulation code:

- **Transform**: Need state machine to swap unit type, preserve vitals/orders, and re-link the edict to a different model/ability set
- **Burrow**: Need a hidden/underground state with distinct movement rules and detection interaction
- **Cargo**: Need a container with capacity, embarking/disembarking, and passenger entity management
- **CAbil execution**: Each ability family (CAbilTrain, CAbilBuild, CAbilMorph, CAbilResearch, CAbilEffectTarget, etc.) needs its own execution logic
- **CEffect chains**: SC2's "effect tree" system routes damage/healing/spawning through chains of CEffect entries; the current direct-life-subtraction path is a temporary shortcut

## Recommendations

### Short Term (Extends Current System)
1. **Document missing LotV/HotS unit catalog IDs** — verify which units from expansions (`Swarm.SC2Mod`, `Void.SC2Mod`) the catalog loader can handle
2. **Add Siege Tank transform** as the simplest mode-switch unit: `SiegeTank` ↔ `SiegeTankSieged` swap model + weapon + movement state
3. **Add burrow state** as a visibility flag on `sc2UnitState_t` — hides the unit, disables movement, allows un-burrow

### Medium Term (Core Simulation Features)
4. **Implement splash damage** for Siege Tank (sieged), Colossus beam, and Baneling explosion — read CEffect/CWeapon data
5. **Add cloak/detection** using the existing `SC2_UNIT_HIDDEN` state flag with a detection-radius scan
6. **Implement transport/cargo** for Medivac — the simplest SC2 transport case (pickup radius, capacity, unload)

### Long Term (Full Gameplay)
7. **CAbil executor framework** dispatching ability commands to typed handlers
8. **CEffect chain evaluator** reading the effect tree from catalog data
9. **Building placement and construction lifecycle**
10. **Resource gathering worker AI**

## Test Map Coverage

| Map | Race | Units Placed |
|-----|------|-------------|
| `Maps/TerranTest.SC2Components` | Terran | Marines, supply depots, test fixtures |
| `Maps/ProtossTest.SC2Components` | Protoss | Basic Protoss units |
| `Maps/ZergTest.SC2Components` | Zerg | Basic Zerg units |

All three are minimal fixture maps. Campaign maps (`TRaynor01`, etc.) from the user's SC2 installation contain the full roster of Liberty campaign units with their authored catalog data.
