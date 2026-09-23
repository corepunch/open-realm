# Warcraft III Attack Damage

See [Unit Stat Modifiers](unit-stat-modifiers.md) for the shared ledger and
damage-entry contracts used by attack, armor, spell, and direct damage effects.

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

Melee resolves the target-facing stages at the damage point. Missile attacks roll at launch but defer `G_AttackDamage()` until projectile impact, so armor/defense changes while the missile is in flight affect the hit. Before that normal impact path, a basic missile may be consumed or retargeted by the Defend projectile reaction. Spell missiles provide their own `currentmove/endfunc` and do not enter this physical-attack/Defend-missile branch.

`WPN_ARTILLERY` shares the physical projectile launch path instead of falling through the melee resolver. The unit-level authored `minRange` is a real dead zone: a mobile attacker backs away until the target is outside it, while a holding/immobile attacker cannot fire. At the attack damage point an artillery unit target is converted to a fixed point, matching Warsmash's `CBehaviorAttack`: the missile flies to that snapshot even if the original unit moves afterward. The projectile snapshots its attack type, splash bands, factors, and matching `splashTargs` mask at launch, so later orders or target selection cannot change impact behavior. Impact then applies those values around the fixed point. If the original unit target is still inside the blast, it retains the ordinary primary-hit pipeline; secondary victims (and all victims of Attack Ground, which has no primary unit) receive physical splash damage without duplicating primary-only orb/lifesteal/cleave behavior. `CmdAttackGround` uses the same point-target projectile/splash path and is exposed only for the implemented `WPN_ARTILLERY` class. Attack Ground approach/retreat steps consult Move-owned `S_UnitCanTranslate` (immobile, Cyclone, Roots, Ensnare, Purge pause) every tick: a locked unit holds its firing point without translating or cancelling, and still fires in-range points.

`T_Damage()` ignores targets whose life is already zero, and `unit_die()` is a one-shot transition once `SVF_DEADMONSTER` is set. This matters for simultaneous or near-simultaneous missile impacts: only the first lethal hit may publish WC3 death events, so map-authored death/loot triggers cannot run twice for the same corpse.

### Defend projectile reaction

An active `Adef` target may react to a basic ranged attack before the ordinary
hit resolves. Data F is a 0..100 chance and is gated by
`Misc.DefendDeflection`. Piercing uses Data G as the successful-deflection
damage-taken factor; Magic/Spells uses Data H. A unit-source success retargets
the same missile edict to its original owner and sets
`projectile_reflected`, preventing a second return. A building-source success
consumes the missile rather than reflecting it into the structure. The flag and
projectile target/owner references cross save/load, so a return flight remains
semantically the same after loading.

## Runtime Attack Fields

`edict_t.attack1` and `attack2` are mutable copies of `UnitWeapons.slk` data. Target validation checks both authored target masks without changing either profile. Attack timing, projectile setup, and damage select the compatible profile from the current target, leaving both authored runtime copies in their original slots. Candidate scans and repeated target validation are read-only, so their traversal order cannot change the active weapon.

For each `unitAttack_t`:

- `damageBase`: permanent base used by the HUD range and roll.
- `numberOfDice`, `sidesPerDie`: `NdS` random component.
- `permanentDamageBonus`: permanent modifier ledger used to survive Hero stat recomputation (for example `ratx`).
- `temporaryDamageBonus`: item/temporary modifier added to each roll but not folded into the base range.
- `cooldown`, `damagePoint`, `range`: attack timing/range.
- artillery splash metadata (`Farea`/`Harea`/`Qarea`, `Hfact`/`Qfact`, `splashTargs`) is consumed by the generic `WPN_ARTILLERY` impact path; bounce/line/missile-splash weapon classes remain separate gaps.

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

### Hit resolution order

`S_ResolveAttackHit` runs Evasion first, then Curse miss, damage bonuses, and
Bash. Evasion (`S_EvasionRoll`) and Bash resolve the owned alias plus rank
through `S_ResolveAbilityAlias` (native `abilList`, runtime-added honoring
removals, ranked hero slots, authored `code=` mapping) and read chance/bonus
from the alias row — so creep `ACes`/`ACev` (100% evade) and `ACbh` work
instead of only base `AEev`/`AHbh`. Critical Strike keeps its explicit
`ACct` fallback. Tests use 0/100 chances; low-percentage rolls are not
asserted probabilistically.

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
- `DefendDeflection`

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

Both damage point and cooldown recovery are divided by that divisor. The same
shared accumulator also includes implemented status modifiers such as Bloodlust,
Frenzy, Unholy Frenzy, Cripple, Slow Poison, and Footman Defend Data D before
the Warcraft `[-90%, +400%]` clamp is applied.

## Upgrade Effects

The generic research dispatcher implements:

- `ratx`: permanent flat attack damage using `base + mod*(level-1)` and applying only the old/new delta.
- `ratd`: attack dice using the same level-value/delta rule.
- `rarm`: armor using the unit type's `armorPerUpgrade`, tracked in `permanent_armor_bonus`.

These affect existing owned units and newly spawned units that inherit already-researched player tech.

## Item Attack/Armor Modifiers

`AIat` changes `temporaryDamageBonus` rather than `damageBase`. This prevents later Hero stat recomputation from erasing the item bonus and makes the HUD show the modifier separately.

`AIde` changes both `temporary_armor_bonus` and the current `armor_value`, so later Hero Agility recomputation preserves the item armor modifier.

## Missile Presentation

Ranged Attack 1 missiles remain ordinary server entities with `MOVETYPE_FLYMISSILE`; the renderer does not own a parallel WC3-only projectile object. `G_StartProjectilePresentation()` therefore initializes presentation state when the missile is spawned: the entity is non-selectable, does not cast a unit shadow, and selects the projectile model's authored `Stand` sequence, falling back to `Birth` only when no `Stand` sequence exists. `SV_Physics_Toss()` advances that sequence while the missile is in flight. Ordinary missiles continue to update yaw from their current homing direction every simulation frame; artillery missiles instead carry `AI_PROJECTILE_FIXED_TARGET` plus the launch-time point in `channel.origin`, so they no longer depend on a moving/freed target entity. The existing snapshot `model`/`frame`/`angle` fields remain sufficient for presentation, and the fixed point rides in already-persisted edict state rather than adding a client-only projectile object. Attack and spell missiles also inherit the firing unit's existing `s.player`, allowing the client MDX renderer to resolve replaceable team-colour textures from the correct source player just as it does for ordinary entities.

This is important for Warcraft missile MDX files whose visible geosets/emitters begin in a sequence interval above frame zero. Merely assigning the registered missile model with `frame == 0` can leave a valid projectile simulation entity visually empty. Spell missiles using the same `MOVETYPE_FLYMISSILE` path receive the same initialization rather than maintaining a second animation policy.

The current implementation still moves the simulation entity on a straight three-dimensional line toward the target. Authored projectile arc/pitch and a post-impact `Death` presentation are separate fidelity gaps; they are not approximated here because the current `entityState_t` Warcraft path has only yaw orientation and frees basic attack missiles immediately when damage resolves.

## Weapon Target Legality And Acquisition

Attack 1 target legality is owned by `skills/s_attack.c`, not by the generic AI scanner. `UnitWeapons.slk` `targs1` (or map-object `ua1g`) is decoded to the runtime `attack1.targetsAllowed` Warcraft `targetflag` mask. `G_TargetFlagForType()` is the shared `TARGTYPE` -> targetflag conversion used by ordinary units and destructables.

`S_AttackCanTarget()` applies that authored mask to explicit Attack orders and every later attack recheck. For ordinary units, the target's `UnitData.targetType` supplies the ground/air/structure/etc. category; destructables continue through `G_DestructableCanBeAttackedBy()`. This keeps target legality in the Attack ability instead of teaching generic AI about Spirit Towers, Burrows, or other particular unit rawcodes.

Minimum attack range is checked alongside maximum range. It comes from `UnitWeapons.minRange` (`uamn`) rather than a Meat-Wagon rawcode special case. A mobile unit already ordered to attack a target inside that dead zone steps directly away from the target until it can fire; Hold Position and `AI_IMMOBILE` units drop the impossible attack instead.

Automatic acquisition adds one policy on top through `S_AttackCanAutoAcquire()`:

- mobile attackers may acquire a legal target anywhere inside their authored acquisition range and chase it normally;
- structures are ordinary candidates when their weapon target mask allows `structure`; there is no global AI exclusion for buildings;
- `AI_IMMOBILE` attackers only auto-acquire targets already inside their effective Attack 1 range, because they cannot chase an acquisition-range target; building-target range uses the target pathing footprint when available, matching normal attack-range checks.

This is important for defensive buildings whose acquisition range may exceed weapon range: an idle tower must not enter an attack behavior for a target it cannot approach. If an explicit or previously valid attack target is out of range while `AI_IMMOBILE`, `ai_attack_walk()` finishes that attack behavior instead of leaving the structure stuck in a non-moving walk state. This matches Warsmash's movement-disabled ranged behavior, which drops an out-of-range attack when no move behavior exists.

Relevant regression coverage is in `games/warcraft-3/game/tests/t_combat.c`: ground/air mask rejection, nearest-target filtering, structure acquisition, immobile out-of-range acquisition, explicit immobile out-of-range attack cancellation, artillery ranged-state selection, minimum-range retreat, authored three-band artillery splash, target-position snapshotting at the damage point, and explicit Attack Ground launch.

## Destructable Attack Targeting

Destructables use their `targType`/Targeted As category together with the
attacking unit's `targs1` target list. Warcraft stores `UnitWeapons.slk`
`targs1`/`targs2` as comma-separated names such as
`ground,structure,debris,item,ward`; OpenRealm decodes those names to the
`targetflag` mask (`tree=64`, `wall=128`, `debris=256`, `decoration=512`,
`bridge=1024`) before copying Attack 1 into runtime state. Map object-data
`ua1g`/`ua2g` overrides remain numeric masks.

Smart/right-click turns ordinary `debris` into an implicit attack, which keeps
breakable crates convenient. Trees keep worker harvesting precedence and do
not Smart-attack, but the explicit Attack command may attack a live tree even
when the standard weapon target list omits `tree` (as retail UnitWeapons data
commonly does). Wall/bridge/decoration classes still require explicit Attack
and the corresponding weapon target bit. This prevents right-click from
attacking every selectable destructable merely because it has life.

Relevant coverage is in `games/warcraft-3/game/tests/t_destructable.c`.

## Known Gaps

The current implementation intentionally does not invent the larger Warsmash combat-listener architecture. Remaining work includes:

- attacker pre-damage listeners (critical strike, bash, Wind Walk, attack replacement/orbs);
- low-ground miss and target evasion;
- target damage-taken and final-damage listeners;
- generic distinction between attack type and damage type / numeric-armor bypass;
- `MSPLASH`, `MBOUNCE`, `MLINE`/`ALINE` damage behavior; `ARTILLERY` now has projectile/min-range/three-band splash, damage-point target snapshotting, and Attack Ground gameplay. `ALINE` still needs its distinct line-damage implementation before sharing the Attack Ground command;
- both Attack 1 and Attack 2 target masks participate in ordinary unit target validation; the selected compatible profile drives the shared attack path. Full retail weapon-selection priorities and special weapon classes remain separate gaps;
- separate Hero base-vs-bonus attributes for Warsmash-exact green primary-stat damage;
- seeded combat RNG independent from unrelated `rand()` consumers;
- non-Agility attack-speed buffs/debuffs.

Attack-target validation takes both the attacker and target, including the animationless building recovery checks used by Orc Burrows. Those recovery checks apply the same weapon target mask as initial attack orders and attack windup validation.

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
