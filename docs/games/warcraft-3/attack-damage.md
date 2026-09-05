# Warcraft III Attack Damage

## Contract

OpenRealm keeps normal WC3 attack damage in three layers:

1. `unitAttack_t.damageBase` + dice describe the permanent displayed range.
2. `unitAttack_t.temporaryDamageBonus` is added after the dice roll and is shown as a green/red `+N/-N` suffix.
3. `G_AttackDamage()` applies the target-facing attack-type/defense-type multiplier and numeric armor.

The ordinary Attack 1 path is:

```text
unit object data
  -> permanent runtime attack mutations (`ratx`, `ratd`, Hero primary attribute)
  -> roll `damageBase + NdS + temporaryDamageBonus`
  -> attack-type x defense-type multiplier
  -> numeric armor
  -> `T_Damage`
```

Melee resolves the target-facing stages at the damage point. Missile attacks roll at launch but defer `G_AttackDamage()` until projectile impact, so armor/defense changes while the missile is in flight affect the hit. Spell missiles provide their own `currentmove/endfunc` and do not enter this physical-attack mitigation branch.

## Runtime Attack Fields

`edict_t.attack1` and `attack2` are mutable copies of `UnitWeapons.slk` data. Both are initialized at spawn even though combat order selection still uses Attack 1.

For each `unitAttack_t`:

- `damageBase`: permanent base used by the HUD range and roll.
- `numberOfDice`, `sidesPerDie`: `NdS` random component.
- `permanentDamageBonus`: permanent modifier ledger used to survive Hero stat recomputation (for example `ratx`).
- `temporaryDamageBonus`: item/temporary modifier added to each roll but not folded into the base range.
- `cooldown`, `damagePoint`, `range`: attack timing/range.
- splash/bounce metadata is parsed but the special weapon behaviors are not yet implemented.

The displayed permanent range is:

```text
min = max(0, damageBase + numberOfDice)
max = max(0, damageBase + numberOfDice * sidesPerDie)
```

A temporary bonus is rendered separately, for example `12 - 18 +3`.

## Damage Roll

`skills/s_attack.c:ai_rolldamage1()` performs one RNG draw per die:

```text
raw = damageBase
    + sum(random(1..sidesPerDie))
    + temporaryDamageBonus
```

Malformed zero-sided dice contribute `+1` rather than taking modulo zero, matching the current Warsmash edge behavior.

OpenRealm still uses the process C `rand()` stream. It does not yet have Warsmash's dedicated seeded simulation RNG contract for attack rolls.

## Gameplay Constants

`InitConstants()` loads combat values from the active Misc data cache. `war3mapMisc.txt` is loaded after stock Misc files and can override them.

Relevant fields:

- `DamageBonusNormal`
- `DamageBonusPierce`
- `DamageBonusSiege`
- `DamageBonusChaos`
- `DamageBonusMagic`
- `DamageBonusHero`
- `DamageBonusSpells` (if absent, copy the active Magic row as Warsmash does)
- `DefenseArmor`
- `StrAttackBonus`
- `AgiDefenseBonus`
- `AgiAttackSpeedBonus`

The defense-column order is:

```text
small, medium, large, fort, normal, hero, divine, none
```

Stock fallback values are retained only for missing data/bootstrap tests. In particular, ordinary attack classes deal `0.05x` to Divine while Chaos deals `1.00x`.

## Numeric Armor

Let `A` be `G_UnitArmorValue(target)` and `K` be `Misc.DefenseArmor` (stock fallback `0.06`).

For `A >= 0`:

```text
multiplier = 1 / (1 + K*A)
```

For `A < 0`:

```text
multiplier = 2 - (1 - K)^(-A)
```

`G_AttackDamage()` applies:

```text
final = raw * DamageBonus[attackType][defenseType] * armorMultiplier
```

and preserves OpenRealm's existing minimum-one physical-hit rule.

`armor_value` is the derived/base armor plus tracked persistent modifiers. `permanent_armor_bonus` (research) and `temporary_armor_bonus` (items) are kept separately so `G_RecomputeHeroStats()` cannot erase them when Agility changes. `G_UnitArmorValue()` then layers timed status armor such as `Bdef` on top.

## Hero Attack Math

For heroes, `G_RecomputeHeroStats()` applies the active `Misc.StrAttackBonus` to the current primary attribute (`STR`, `AGI`, or `INT`) and adds `permanentDamageBonus` when rebuilding each runtime attack.

Current limitation: OpenRealm's `doodadHero_t` stores only total STR/AGI/INT, not Warsmash's separate base and bonus attributes. Therefore primary-attribute item/stat bonuses are still folded into the permanent damage range rather than displayed as a separate green primary-attribute damage bonus. `AIat` attack bonuses are separated correctly.

Agility attack timing uses:

```text
totalBonus = agility * Misc.AgiAttackSpeedBonus
clampedBonus = clamp(totalBonus, -0.90, +4.00)
divisor = 1 + clampedBonus
```

Both damage point and cooldown recovery are divided by that divisor. OpenRealm does not yet have the other Warsmash attack-speed modifier sources, so only the Agility contribution is currently present.

## Upgrade Effects

The generic research dispatcher implements:

- `ratx`: permanent flat attack damage using `base + mod*(level-1)` and applying only the old/new delta.
- `ratd`: attack dice using the same level-value/delta rule.
- `rarm`: armor using the unit type's `armorPerUpgrade`, tracked in `permanent_armor_bonus`.

These affect existing owned units and newly spawned units that inherit already-researched player tech.

## Item Attack/Armor Modifiers

`AIat` changes `temporaryDamageBonus` rather than `damageBase`. This prevents later Hero stat recomputation from erasing the item bonus and makes the HUD show the modifier separately.

`AIde` changes both `temporary_armor_bonus` and the current `armor_value`, so later Hero Agility recomputation preserves the item armor modifier.

## Known Gaps

The current implementation intentionally does not invent the larger Warsmash combat-listener architecture. Remaining work includes:

- attacker pre-damage listeners (critical strike, bash, Wind Walk, attack replacement/orbs);
- low-ground miss and target evasion;
- target damage-taken and final-damage listeners;
- generic distinction between attack type and damage type / numeric-armor bypass;
- `MSPLASH`, `ARTILLERY`, `MBOUNCE`, `MLINE`/`ALINE` damage behavior;
- combat selection between Attack 1 and Attack 2;
- separate Hero base-vs-bonus attributes for Warsmash-exact green primary-stat damage;
- seeded combat RNG independent from unrelated `rand()` consumers;
- non-Agility attack-speed buffs/debuffs.

## Verification

Relevant source/tests:

- `games/warcraft-3/game/skills/s_attack.c`
- `games/warcraft-3/game/g_phys.c`
- `games/warcraft-3/game/m_unit.c`
- `games/warcraft-3/game/g_building.c`
- `games/warcraft-3/game/skills/s_item_stats.c`
- `games/warcraft-3/game/tests/t_combat.c`
- `games/warcraft-3/game/tests/t_building.c`

The combat tests cover representative type multipliers, Divine, data-driven constants, positive/negative armor, Hero modifier preservation, and the attack-speed cap. Research tests cover `ratx` level-delta semantics.

## See Also

- [Unit Altitude And Support Surfaces](unit-altitude.md) — projectile target Z adds the target model-origin altitude and authored `impactZ`.
