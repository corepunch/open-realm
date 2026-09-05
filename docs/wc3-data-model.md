# WC3 Data Model

Read this when working on unit stats, combat, abilities, hero systems, pathfinding, or any code that reads from SLK/metadata tables.

## SLK And INI Loading

WC3 table loading follows the same boundary as `stb_dbc.h`: callers declare a `slkField_t` schema and own an
`stbSlkCache_t` containing only decoded typed rows. `Stb_SlkCacheLoad` and `Stb_SlkCacheLoadBuffer` perform file parsing
and decoding; `Stb_SlkCacheFind` resolves an explicitly declared FOURCC row key; `Stb_SlkCacheFree` releases owned
strings and rows. The row key is a normal schema entry with an empty column name and `STB_SLK_FOURCC`; the loader never
writes an implicit key into byte zero.

INI files with fixed row layouts use `Stb_IniCacheDecode` into an `stbSlkCache_t`. Dynamic dictionaries such as skin,
miscellaneous, command, and ability text files remain `stbIniCache_t` values queried through `Stb_IniCacheFind`.
`sheetRow_t`, fields, linked lists, and append operations are private implementation details of
`games/warcraft-3/sheet/sheet.c` and must not cross the parser boundary.

## The Base-vs-Computed Column Trap

Several UnitBalance.slk columns exist in both a **base** form and a **computed** (real) form. The base column is the editor-entered value; the computed column includes bonuses (hero attributes, etc.). Always read the computed column at runtime — base values are 0 or wrong for heroes.

| Stat | Wrong (base) | Correct (computed) | Source |
|---|---|---|---|
| Max HP | `uhp` | `uhpm` → realHP | UnitBalance.slk |
| Max mana | `manaN` / old `umpc` | `umpm` → realM | UnitBalance.slk |
| Armor | `udef` (0 for heroes) | `udfc` → realdef (incl. AGI bonus) | UnitBalance.slk |

The old codes `umpc`, `uagc`, `uinc`, `ustc` are **not registered** in the metadata table — `UnitStringField` returns NULL and accessors silently read as 0. Any unregistered field code logs a one-time warning.

## Unit Field Code Reference

Gameplay reads typed rows through `G_UnitBalance`, `G_UnitData`, `G_UnitUI`, `G_UnitWeapons`, `G_UnitAbil`, and
`G_UnitProfile`. Profile/INI files are merged into `UnitProfile_t`; there is no separate macro access layer.

`G_BindEntityData` resolves these rows once and stores direct, table-named pointers on `edict_t` (`UnitBalance`,
`UnitWeapons`, `ItemData`, and so on). Keep those pointers flat: their exact names are the table-to-edict contract.
They do not replace mutable runtime values such as attacks, armor, movement speed, hero attributes, health, or mana;
items, upgrades, and JASS natives modify those values after spawn. Cohesive transient state belongs in the existing
named edict sections (`item`, `destructable`, `cargo`, `movement`, `channel`, and `sound`). The server-visible prefix
through `areabounds` must remain aligned with `server.h`.

### Map-local `war3map.w3u` presentation overrides

`CM_LoadMap` parses original-unit edits and user-created units from `war3map.w3u` into `MAPINFO.originalUnits` and
`MAPINFO.userCreatedUnits`. Before map entities spawn, `G_SetMapUnitOverrides` builds stable per-map `UnitProfile_t`
and `UnitUI_t` rows. Original-unit edits are applied first; a custom unit then inherits the already-overridden base
rows and applies its own registered Profile/UI modifications. `G_UnitProfile(id)` and `G_UnitUI(id)` check these
exact-ID rows before falling back to the base-SLK/custom-ID remap.

This is required because spawned edicts retain immutable typed-row pointers; never implement map overrides with one
mutable scratch row. The override values point into map-owned `war3map.w3u` modification storage, so the caches are
cleared/rebuilt at map load and cleared again during unit-data shutdown.

The current merge covers fields already mapped to `UnitProfile_t` or `UnitUI_t` in `UnitsMetaData`. This includes
`uani` (`animProps`, Required Animation Names), `umdl` (model), `usca` (model scale), profile/name fields, tint/team-
colour fields, selection/shadow fields, and `usnd`. Balance/Data/Weapons/Abilities object-data merge remains separate
work and still uses base typed rows through `ResolveUnitID`.

`uani` is especially important because a different visible form does not necessarily mean a different model file.
`UnitProfile.animProps` supplies persistent secondary MDX animation tags such as `alternate`; `G_SetUnitAnimation()`
combines them with the requested animation family and selects a matching tagged sequence. Per-unit JASS changes from
`AddUnitAnimationProperties` mutate the edict's active tags and reselect its logical animation. See
[Required Animation Names](games/warcraft-3/unit-animation-properties.md).

The earlier model bug was caused by resolving a custom rawcode to `originalUnitID` *before* `G_UnitUI` lookup. That
made a map-authored `umdl` invisible to spawn code. `umdl` values may carry an authored model extension; unit spawning,
build placement previews, and cinematic portraits therefore use `G_NormalizeModelFilename`, which preserves an
existing extension and adds `.mdx` only to extensionless base-SLK stems. Do not blindly append `.mdx` to a map
override.

FourCC/JASS reads use `UnitIntegerField` / `UnitRealField` / `UnitBooleanField` / `UnitStringField`. Unit metadata binds
each FourCC to its DDX field descriptor and typed-row index during `InitUnitData`, so these accessors read the same arrays
as gameplay instead of returning to `sheetRow_t`. Add fields to the owning row and DDX schema in `g_metadata.c`.

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
| `G_UnitProfile(id)->attack[0].speed` | `ua1z` | 0 = melee; Profile/INI |

### Movement / Collision
| Macro | Code | Notes |
|---|---|---|
| `UNIT_SPEED` | `umvs` | movement speed |
| `UNIT_TURN_RATE` | `umvr` | radians/sec turn rate |
| `UNIT_COLLISION` | `ucol` | collision radius for unit-vs-unit separation (e.g. Peasant=16). TFT stores it in `UnitBalance.slk`; ROC stores it in `UnitData.slk`. Buildings use pathing texture footprint instead (their collisionSize is ~0) |
| `UNIT_MOVE_TYPE_NAME` | `umvt` | **string** enum: `"foot"/"fly"/"hover"/"float"/"amph"/"horse"` — use `_NAME` variant |
| `UNIT_MOVE_HEIGHT` | `umvh` | authored default fly/model-origin height above the selected support surface; copied to mutable `unitinfo.FlyHeight` at spawn |
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
| `G_UnitProfile(id)->trains` | `utra` | trainable unit codes |
| `G_UnitProfile(id)->builds` | `ubui` | buildable structure codes |

## Ability Field Codes

Ability custom data uses `abilityDataRow_t.data[level][slot]`, with `AB_Data` retained for callers that start from a class-name string.
ROC `Data<level><slot>` columns and TFT `Data<slot-letter><level>` columns map to that canonical array in the DDX schema.

| Ability | Field | Column | Value |
|---|---|---|---|
| Goldmine (`Agld`) | slot 1 | Max Gold | 12500 |
| Goldmine | slot 2 | Mining Duration | 1 |
| Goldmine | slot 3 | Mining Capacity | 1 |
| Harvest lumber (`Ahar`) | slot 1 | Damage to Tree | 1 |
| Harvest lumber | slot 2 | Lumber Capacity | 10 |
| Harvest lumber | slot 3 | Gold Capacity | 10 |

Common ability fields (all abilities):
- `Rng1` — cast/work range
- `Dur1` — duration / cooldown
- `Area1` — area radius
- `AB_Data(..., level, slot)` — ability-specific data with ROC/TFT schema resolution

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
`DefenseArmor` is loaded from the active Misc data (`Units\MiscGame.txt` plus `war3mapMisc.txt` overrides; stock fallback `0.06`).

For non-negative armor `A` and coefficient `K`:

`dmg *= 1 / (1 + K * A)`

For negative armor:

`dmg *= 2 - (1 - K)^(-A)`

This is the Warsmash/WC3 exponential negative-armor curve. Minimum final physical-attack damage remains 1 in the current OpenRealm path.

Attack-type/defense-type rows are also loaded from the active `DamageBonus*` Misc fields rather than being fixed in `s_attack.c`; if `DamageBonusSpells` is absent, the active Magic row is used as Warsmash's fallback. Basic missile attacks roll at launch and apply type/armor mitigation at impact.

See [Attack Damage](games/warcraft-3/attack-damage.md) for the runtime modifier and timing contract.
See [Unit Altitude And Support Surfaces](games/warcraft-3/unit-altitude.md) for `moveHeight`, water/bridge support Z, fly-height natives, and projectile impact-height placement.

### Defense Type Is a String
`defType` in `UnitBalance.slk` is a string column (`"large"`, `"medium"`, etc.), not an integer. `atoi` returns 0 for every unit. Use `FindEnumValue` against the `defense_type[]` enum table. Base armor is `udef`; load `udfc` (realdef) at spawn.

## Hero Stat System

### Attribute → Derived Stats (per-point constants from UnitBalance.slk)
- **Strength**: +25 max HP per point
- **Intelligence**: +15 max mana per point
- **Agility**: armor contribution uses `Misc.AgiDefenseBonus` (stock 0.3); attack speed uses `Misc.AgiAttackSpeedBonus` (stock 0.02)
- **Primary attribute**: attack damage uses `Misc.StrAttackBonus` (stock 1.0) for whichever `upra` primary attribute is active

Stats are precomputed at base attributes; deltas are applied live on attribute change. Gaining STR heals by the HP gained; losing attributes cannot drop a living hero below 1 HP. Call `G_RecomputeHeroStats` whenever `hero.str/agi/intel` change.

### XP and Leveling
- Max level: `Misc/MaxHeroLevel` from `MiscGame.txt` (default 10).
- XP to reach level L is accumulated from `Misc/NeedHeroXP`; when the table runs out, `NeedHeroXPFormulaA/B/C` extend the per-level requirements. Stock data yields L1=0, L2=200, L3=500, L4=900, L10=5400. If no Misc data is available, the fallback is the stock per-level sequence `200,300,400,...` rather than formula-extension of a synthetic single entry.
- Attributes at level L: `base + trunc((L-1) * perLevelGain)` — **truncated** toward zero (bare float→int cast, no rounding), matching the WC3 binary.
- XP is the source of truth; level only ever increases. `SetHeroLevel` works by granting enough XP to reach the target level.
- Level-up fires both `EVENT_PLAYER_HERO_LEVEL` and `EVENT_UNIT_HERO_LEVEL` once per level gained. `GetLevelingUnit()` resolves to the Hero for either event family.
- Unspent Hero skill points are independent mutable runtime state. Level-up adds one point per crossed level; `UnitModifySkillPoints` can add/remove points directly without changing XP/level, while `GetHeroSkillPoints` reports the current pool.

### XP on Kill (from MiscGame.txt)
Key constants (WC3 1.29 defaults):
- `HeroExpRange` = 1200 (XP-share radius)
- `GrantNormalXP` = 25; `GrantNormalXPFormulaB` is 0 in ROC and 5/level in TFT.
- `GrantHeroXP` list = 100,120,160,220,300 (for hero kills)
- `HeroFactorXP` list = 80,70,60,50,0 (% when hero outlevels victim by N levels)
- `BuildingKillsGiveExp` = 0
- `GlobalExperience` = 1; used only as a fallback when no eligible Hero is inside `HeroExpRange`

Read live from `game.config.misc` so map overrides stay 1:1. `MaxLevelHeroesDrainExp` and `SummonedKillFactor` remain unconsumed by the current kill-XP implementation.

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

The single-unit info panel remains a server-baked `svc_layout` snapshot, but the portrait HP/mana strings are live player-state bindings rather than baked text. `G_UpdateClientInfoPanels` runs after `G_RunEntities`, iterates connected game clients (reserved client edicts are intentionally not normal `inuse` entities), and writes the sole-selected unit's whole-number current/max HP and mana into reserved `playerState.stats[18..21]`. The client formats those values through `UI_STAT_SELECTION_HEALTH_TEXT` / `UI_STAT_SELECTION_MANA_TEXT`, so damage, healing, regeneration, and mana changes do not require a complete portrait or info-panel layout resend. `LAYER_INFOPANEL` is reserialized only for presentation state that is actually baked into that layer, such as selection identity or Hero XP.

## JASS Event Matching

- `EVENT_UNIT_DEATH` — widget-specific death triggers (`TriggerRegisterDeathEvent`/`UnitEvent`).
- `EVENT_PLAYER_UNIT_DEATH` — owner's player-unit-death triggers (`TriggerRegisterPlayerUnitEvent`); both must be published on `unit_die`.
- `EVENT_PLAYER_UNIT_*` handlers registered with a **player** as subject fire for any of that player's units (match by owner, not unit identity); the triggering unit is passed as trigger context.
- Hero progression publishes both `EVENT_PLAYER_HERO_LEVEL` and `EVENT_UNIT_HERO_LEVEL` once per level gained (loop from oldLevel+1 to newLevel); `GetLevelingUnit()` resolves to that Hero.

## Misc Data Constants (MiscGame.txt)

Read via `FS_FindSheetCell(game.config.misc, "Misc", key)`. Never hardcode defaults without a `BZ_HARDCODED_DATA_FALLBACK` comment. Common keys:

| Key | Default | Meaning |
|---|---|---|
| `MaxHeroLevel` | 10 | hero level cap |
| `NeedHeroXP` | 200 | per-level XP requirement table; extended by `NeedHeroXPFormulaA/B/C` |
| `GlobalExperience` | 1 | globally distribute kill XP only when no eligible Hero is in range |
| `HeroExpRange` | 1200 | XP-share radius |
| `GrantNormalXP` | 25 | base XP for killing a creep |
| `GrantNormalXPFormulaB` | ROC 0 / TFT 5 | XP per victim level |
| `HeroExpRange` | 1200 | |
| `BuildingKillsGiveExp` | 0 | |
