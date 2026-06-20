# WC3 Data Model

Read this when working on unit stats, combat, abilities, hero systems, pathfinding, or any code that reads from SLK/metadata tables.

## The Base-vs-Computed Column Trap

Several UnitBalance.slk columns exist in both a **base** form and a **computed** (real) form. The base column is the editor-entered value; the computed column includes bonuses (hero attributes, etc.). Always read the computed column at runtime — base values are 0 or wrong for heroes.

| Stat | Wrong (base) | Correct (computed) | Source |
|---|---|---|---|
| Max HP | `uhp` | `uhpm` → realHP | UnitBalance.slk |
| Max mana | `manaN` / old `umpc` | `umpm` → realM | UnitBalance.slk |
| Armor | `udef` (0 for heroes) | `udfc` → realdef (incl. AGI bonus) | UnitBalance.slk |

The old codes `umpc`, `uagc`, `uinc`, `ustc` are **not registered** in the metadata table — `UnitStringField` returns NULL and accessors silently read as 0. Any unregistered field code logs a one-time warning.

## Unit Field Code Reference

All reads go through `UnitIntegerField` / `UnitRealField` / `UnitBooleanField` / `UnitStringField` against `UnitsMetaData`. The macros in `games/warcraft-3/game/g_unitdata.h` are the canonical access layer — add new fields there, not inline.

### Health / Mana
| Macro | Code | Notes |
|---|---|---|
| `UNIT_HP` | `uhpm` | realHP — computed max HP including STR bonus for heroes |
| `UNIT_MANA_MAXIMUM` | `umpm` | realM — computed max mana including INT bonus |
| `UNIT_MANA_INITIAL` | `umpi` | mana0 — starting amount |
| `UNIT_HIT_POINTS_REGENERATION_RATE` | `uhpr` | base regen rate |
| `UNIT_HIT_POINTS_REGENERATION_TYPE_NAME` | `uhrt` | **string** enum: `"always"/"night"/"blight"/"none"` — use `_NAME` variant, not integer |
| `UNIT_MANA_REGENERATION` | `umpr` | base mana regen rate |

### Defense / Armor
| Macro | Code | Notes |
|---|---|---|
| `UNIT_ARMOR_VALUE` | `udfc` | realdef — computed armor including hero AGI bonus; use this everywhere |
| `UNIT_ARMOR_TYPE` | `uarm` | integer armor type index |
| `UNIT_DEFENSE_TYPE_NAME` | `udty` | **string** enum: `"small"/"medium"/"large"/"fort"/"normal"/"hero"/"divine"/"none"` — `atoi` returns 0 for every unit; map via `FindEnumValue` |

### Hero Attributes
| Macro | Code | Notes |
|---|---|---|
| `UNIT_STRENGTH` | `ustr` | base STR (not `ustc` — unregistered) |
| `UNIT_AGILITY` | `uagi` | base AGI (not `uagc` — unregistered) |
| `UNIT_INTELLIGENCE` | `uint` | base INT (not `uinc` — unregistered) |
| `UNIT_STRENGTH_PER_LEVEL` | `ustp` | gain per level |
| `UNIT_AGILITY_PER_LEVEL` | `uagp` | gain per level |
| `UNIT_INTELLIGENCE_PER_LEVEL` | `uinp` | gain per level |
| `UNIT_PRIMARY_ATTRIBUTE` | `upra` | string: `"STR"/"AGI"/"INT"` |

### Attack 1
| Macro | Code | Notes |
|---|---|---|
| `UNIT_ATTACK1_DAMAGE_BASE` | `ua1b` | base damage (before hero primary-attr bonus) |
| `UNIT_ATTACK1_DAMAGE_NUMBER_OF_DICE` | `ua1d` | dice count |
| `UNIT_ATTACK1_DAMAGE_SIDES_PER_DIE` | `ua1s` | sides per die |
| `UNIT_ATTACK1_ATTACK_TYPE` | `ua1t` | string: `"normal"/"pierce"/"siege"/"spells"/"chaos"/"magic"/"hero"` |
| `UNIT_ATTACK1_BASE_COOLDOWN` | `ua1c` | full cooldown (windup + recovery) |
| `UNIT_ATTACK1_DAMAGE_POINT` | `udp1` | damage fires at this fraction of the cooldown |
| `UNIT_ATTACK1_BACKSWING_POINT` | `ubs1` | anim ends here; recovery = cooldown - damagePoint |
| `UNIT_ATTACK1_RANGE` | `ua1r` | attack range |
| `UNIT_ATTACK1_AREA_OF_EFFECT_FULL_DAMAGE` | `ua1f` | splash full-damage radius |
| `UNIT_ATTACK1_AREA_OF_EFFECT_MEDIUM_DAMAGE` | `ua1h` | splash medium radius |
| `UNIT_ATTACK1_AREA_OF_EFFECT_SMALL_DAMAGE` | `ua1q` | splash small radius |
| `UNIT_ATTACK1_DAMAGE_FACTOR_MEDIUM` | `uhd1` | medium-ring damage multiplier |
| `UNIT_ATTACK1_DAMAGE_FACTOR_SMALL` | `uqd1` | small-ring damage multiplier |
| `UNIT_ATTACK1_PROJECTILE_SPEED` | `ua1z` | 0 = melee |

### Movement / Collision
| Macro | Code | Notes |
|---|---|---|
| `UNIT_SPEED` | `umvs` | movement speed |
| `UNIT_TURN_RATE` | `umvr` | radians/sec turn rate |
| `UNIT_COLLISION` | `ucol` | collision radius for unit-vs-unit separation (e.g. Peasant=16). Buildings use pathing texture footprint instead (their collisionSize is ~0) |
| `UNIT_MOVE_TYPE_NAME` | `umvt` | **string** enum: `"foot"/"fly"/"hover"/"float"/"amph"/"horse"` — use `_NAME` variant |
| `UNIT_SIGHT_RADIUS` | `usid` | daytime sight range |
| `UNIT_SIGHT_RADIUS_NIGHT` | `usin` | nighttime sight range |

### Economy / Build
| Macro | Code | Notes |
|---|---|---|
| `UNIT_GOLD_COST` | `ugol` | |
| `UNIT_LUMBER_COST` | `ulum` | |
| `UNIT_FOOD_USED` | `ufoo` | food consumed |
| `UNIT_FOOD_MADE` | `ufma` | food provided |
| `UNIT_BUILD_TIME` | `ubld` | seconds; multiply by 1000 for ms |
| `UNIT_IS_BUILDING` | `ubdg` | boolean |

### Misc
| Macro | Code | Notes |
|---|---|---|
| `UNIT_LEVEL` | `ulev` | unit/creep level |
| `UNIT_ACQUISITION_RANGE` | `uacq` | auto-attack trigger range |
| `UNIT_MODEL` | `umdl` | MDX path |
| `UNIT_ABILITIES_NORMAL` | `uabi` | comma-separated ability codes |
| `UNIT_ABILITIES_HERO` | `uhab` | hero abilities |
| `UNIT_TRAINS` | `utra` | trainable unit codes |
| `UNIT_BUILDS` | `ubui` | buildable structure codes |

## Ability Field Codes

Abilities read from `AbilityData.slk` via `AB_Number(classname, "DataXY")`. The column naming uses letter+digit, **not** number+number — `Data11/12/13` don't exist and decode to 0.

| Ability | Field | Column | Value |
|---|---|---|---|
| Goldmine (`ANmi`) | `DataA1` | Max Gold | e.g. 12500 |
| Goldmine | `DataB1` | Mining Duration | |
| Goldmine | `DataC1` | Mining Capacity | |
| Harvest lumber | `DataA1` | Damage to Tree | |
| Harvest lumber | `DataB1` | Lumber Capacity | |
| Harvest lumber | `DataC1` | Gold Capacity | |

Common ability fields (all abilities):
- `Rng1` — cast/work range
- `Dur1` — duration / cooldown
- `Area1` — area radius
- `DataA1`–`DataI1` — ability-specific data fields (letter A–I, not digits 1–9)

## Combat Damage Model (WC3 1.29)

Verified from `MiscGame.txt`. Applied on the **physical attack path only** (`damage_target`, `throw_missile`). Spells and trigger damage call `T_Damage` directly and are unaffected.

### Attack × Defense Multiplier Table

```
             small  medium  large  fort  normal  hero  divine  none
none          1.00   1.00   1.00  1.00    1.00  1.00    1.00  1.00
normal        1.00   1.50   1.00  0.70    1.00  1.00    0.05  1.00
pierce        2.00   0.75   1.00  0.35    1.00  0.50    0.05  1.50
siege         1.00   0.50   1.00  1.50    1.00  0.50    0.05  1.50
spells        1.00   1.00   1.00  1.00    1.00  0.70    0.05  1.00
chaos         1.00   1.00   1.00  1.00    1.00  1.00    1.00  1.00
magic         1.25   0.75   2.00  0.35    1.00  0.50    0.05  1.00
hero          1.00   1.00   1.00  0.50    1.00  1.00    0.05  1.00
```

### Armor Reduction
`dmg /= (1 + armor * 0.06)` for positive armor.
`dmg *= (2 - 1 / (1 + (-armor) * 0.06))` for negative armor.
Minimum final damage: 1.

### Defense Type Is a String
`defType` in `UnitBalance.slk` is a string column (`"large"`, `"medium"`, etc.), not an integer. `atoi` returns 0 for every unit. Use `FindEnumValue` against the `defense_type[]` enum table. Base armor is `udef`; load `udfc` (realdef) at spawn.

## Hero Stat System

### Attribute → Derived Stats (per-point constants from UnitBalance.slk)
- **Strength**: +25 max HP per point
- **Intelligence**: +15 max mana per point
- **Agility**: +0.3 armor per point; scales attack speed
- **Primary attribute**: +1 attack damage per point (`upra` column: `"STR"/"AGI"/"INT"`)

Stats are precomputed at base attributes; deltas are applied live on attribute change. Gaining STR heals by the HP gained; losing attributes cannot drop a living hero below 1 HP. Call `G_RecomputeHeroStats` whenever `hero.str/agi/intel` change.

### XP and Leveling
- Max level: `Misc/MaxHeroLevel` from `MiscGame.txt` (default 10).
- XP to reach level L: `50 * L * (L+1) - 100` (L1=0, L2=200, L3=500, L10=5400).
- Attributes at level L: `base + trunc((L-1) * perLevelGain)` — **truncated** toward zero (bare float→int cast, no rounding), matching the WC3 binary.
- XP is the source of truth; level only ever increases. `SetHeroLevel` works by granting enough XP to reach the target level.
- Level-up fires `EVENT_PLAYER_HERO_LEVEL` once per level gained.

### XP on Kill (from MiscGame.txt)
Key constants (WC3 1.29 defaults):
- `HeroExpRange` = 1200 (XP-share radius)
- `GrantNormalXP` = 25, `GrantNormalXPFormulaB` = 5/level (base XP by victim level)
- `GrantHeroXP` list = 100,120,160,220,300 (for hero kills)
- `HeroFactorXP` list = 80,70,60,50,0 (% when hero outlevels victim by N levels)
- `BuildingKillsGiveExp` = 0

Read live from `game.config.misc` so map overrides stay 1:1.

### Hero Revival
Dead heroes do **not** decay — they persist as revivable bodies (altar mechanic). `unit_decay_think` is a no-op for heroes. Revive restores HP/mana by configurable life/mana factors.

## Pathfinding and Collision

### Collision Radius
Use `UNIT_COLLISION` (`ucol`) for unit-vs-unit separation — e.g. Peasant=16. The old approach (pathing-texture cell count × 16 × 1.3) was ~2.6× too large and caused over-separation.

Buildings block via their pathing texture footprint (their `collisionSize` is ~0 and should not be used for separation). Buildings bake their footprint into the pathmap on construct.

Trees do **not** fabricate a collision circle — footprint only.

### Flow Field
- SPFA relaxation; no diagonal corner-cutting.
- Collision radius in cells uses `/32` (one cell), not `/24`.
- The old `0xffff` iteration cap truncated large maps and is removed.
- `CM_PointIsPathableForRadius` for cheap static-terrain queries.

### Movement
- Move-time validation (swept circle-vs-circle), not post-move push: units block and slide, they don't shove idle units.
- Broad-phase box spans the whole step so fast units can't tunnel through blockers between ticks.
- Avoidance resolves into a single heading per tick; slower unit yields to faster (speed-priority give-way).

## Info Panel and UI Refresh

The single-unit info panel is a server-baked `svc_layout` snapshot. It does **not** auto-update on damage/healing/regen — only on explicit `UI_SendInfoPanel` calls. `G_UpdateClientInfoPanels` runs each frame after `G_RunEntities` and re-sends only when the displayed HP/mana integer changes (diff-on-send, per-client cache).

## JASS Event Matching

- `EVENT_UNIT_DEATH` — widget-specific death triggers (`TriggerRegisterDeathEvent`/`UnitEvent`).
- `EVENT_PLAYER_UNIT_DEATH` — owner's player-unit-death triggers (`TriggerRegisterPlayerUnitEvent`); both must be published on `unit_die`.
- `EVENT_PLAYER_UNIT_*` handlers registered with a **player** as subject fire for any of that player's units (match by owner, not unit identity); the triggering unit is passed as trigger context.
- `EVENT_PLAYER_HERO_LEVEL` fires once per level gained (loop from oldLevel+1 to newLevel).

## Misc Data Constants (MiscGame.txt)

Read via `FS_FindSheetCell(game.config.misc, "Misc", key)`. Never hardcode defaults without a `BZ_HARDCODED_DATA_FALLBACK` comment. Common keys:

| Key | Default | Meaning |
|---|---|---|
| `MaxHeroLevel` | 10 | hero level cap |
| `HeroExpRange` | 1200 | XP-share radius |
| `GrantNormalXP` | 25 | base XP for killing a creep |
| `GrantNormalXPFormulaB` | 5 | XP per victim level |
| `HeroExpRange` | 1200 | |
| `BuildingKillsGiveExp` | 0 | |
