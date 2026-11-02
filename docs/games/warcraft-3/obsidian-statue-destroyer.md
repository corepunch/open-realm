# Obsidian Statue and Destroyer

Source baseline: `open-realm-main-2026-10-29-23-15.zip`. Reference:
[Blizzard's Obsidian Statue](https://classic.battle.net/war3/undead/units/obsidianstatue.shtml)
and [Destroyer](https://classic.battle.net/war3/undead/units/destroyer.shtml).
Visual evidence comes from the original MPQ/unit/ability table inspection supplied
by the project maintainer on 31 October 2026, not from guesses based on filenames.

## Owned game-side paths

- `skills/s_undead_abilities.c`: `Arpl` (Essence of Blight) and `Arpm` (Spirit Touch).
- `skills/s_requested_abilities.c`: permanent `Aave` Destroyer conversion and Morph presentation.
- `skills/s_hero_passives.c`, `skills/s_attack.c`: mana-paid `Afak` attack modifier.
- `skills/s_human_abilities.c`: area dispel / `Advm` target refund.
- `skills/s_creep_abilities.c`: `Aabs` friendly unit-target homing mana transfer.
- `skills/s_spell.c`: generic spell admission, mana commit, cooldown and attack-modifier manual order.

## Statue restoration

`Arpl` uses `DataA` HP; `Arpm` uses `DataB` mana. Both scan the
ability's authored area and target-mask using `S_SpellAllowsAreaTarget()`. Only
wounded/mana-depleted recipients qualify, up to six per casting in stable entity
order; the mask excludes mechanical recipients when `organic` is set.

The normal spell commit charges the first `Cost`; each additional recipient
through the fifth pays another `Cost`; a sixth receives restoration at no
additional cost. Existing caster mana constrains how many can be restored. The
authored `Brpl`/`Brpm` timed buff inhibits simultaneous restoration of the same
type on the same recipient. Unlike an ad-hoc global timestamp, this uses normal
status expiration and persistence. Maps omitting the buff do not receive that
suppression. If the status array is full, the candidate is skipped.

Each cast queues `CasterArt` at `origin`, `SpecialArt` at `overhead`, plus one
`TargetArt` for every recipient actually restored, and resolves the ability
sound alias through the shared sound layer.

The confirmed stock assignments are:

| Ability | Caster | Special | Target |
|---|---|---|---|
| `Arpl` | `ReplenishHealthCaster.mdx` | `ReplenishHealthCasterOverhead.mdx` | Human `HealTarget.mdx` |
| `Arpm` | `ReplenishManaCaster.mdx` | `ReplenishManaCasterOverhead.mdx` | `SpiritTouchTarget.mdx` |

The exact retail order of six recipients, target-art timing and overlap window
remain observational gaps. The current entity-order selection is deterministic,
not a claim that retail chooses the same recipients.

## Permanent Destroyer Form

`Aave` takes its source ID from `AbilityData.DataA` and destination from
`UnitID`. The conversion checks the authored research availability and the
**positive cost difference** between destination/source `UnitBalance`
(gold, lumber, extra food) before rebinding the same edict with
`G_TransformUnitType()`. Resources are charged only after successful rebind.
The destination begins with zero mana, keeps its JASS identity, is assigned its
own stats/attack/flight profile, and has no reversion thinker.

Both `uobs` and `ubsp` use
`Units\\Undead\\ObsidianStatue\\ObsidianStatue.mdx`. The destination's
`alternate` animation property selects Destroyer visuals. The game plays the
untagged `Morph` sequence while keeping the unit at ground height, then adds
`alternate` at the authored Crumble2/geoset handoff (about 48.35% through the
sequence) and raises it to the destination's authored `moveHeight` over the
remainder. This preserves the stock rubble effect on the ground. The property
change preserves the active Morph frame instead of restarting into `Morph
Alternate`; normal alternate Stand/Walk/Attack sequences take over after the
morph ends. The timing is based on the inspected stock MDX event tracks and
still warrants an in-game visual check against retail.

## Destroyer abilities

- **Orb damage ownership**: `Afak` is the mana-paid projectile modifier.
  `S_DestroyerOrbCommitAttack()` adds its `DataA` bonus to the missile only
  after payment; impacts use that snapshot and must not query `Afak` a second
  time. `ANak` is a distinct neutral/creep passive handled by
  `S_OrbAnnihilationDamage()` at hit time. These rawcodes must not be conflated.
- **Normal weapon**: `AvengerMissile.mdx`, homing, authored weapon speed 900.
- **`Afak` Orb of Annihilation**: manual one-shot and autocast use the same
  attack-modifier order, spend authored mana and add `DataA` damage on a
  successful shot; the missile uses the ability's authored `MissileArt`
  (stock: `AnnihilationMissile.mdx`). Once the weapon projectile is allocated,
  Orb spends mana once and snapshots `DataA` bonus, `DataD` full radius,
  `DataE` medium radius, `Area` outer radius and `DataB`/`DataC` falloffs.
  Even a plain `WPN_MISSILE` weapon uses the existing generic Missile Splash
  impact path for an empowered Orb. Conversely, a `WPN_MSPLASH` weapon that
  owns stock `Afak` does not perform Orb splash while the modifier is off or
  unpaid. The missile's `Death` event owns the hit
  sound and presentation; other weapons are unchanged. If the Orb cannot be
  paid for, the attack remains a normal missile attack. Manual one-shot Orb
  state is consumed when the shot is released, not again on hit.
- **`Advm` Devour Magic**: reuses the shared area-dispel inverse lifecycle. It
  now refunds `DataA` HP/`DataB` mana once per hostile buffed unit, rather than
  once per removed status; friendly buffs and Sentinel wards produce no refund.
  `DataE` damages hostile summons. It spawns authored special art and sound at
  the impact point. **Travel of the authored `DevourMagicBirthMissile.mdx` is
  still missing**.
- **`Aabs` Absorb Mana**: a unit-target spell with a homing projectile
  (`AbsorbManaBirthMissile.mdx` via authored MissileArt); on valid impact,
  transfers up to `DataB`, capped by source mana and the Destroyer's empty
  capacity, without creating mana. The missile is retained through its Death
  impact animation/SND event, rather than freed before the visual can finish. Cancellation, projectile save/load and
  mid-flight target disappearance need focused testing.

## Regression and verification

`game/tests/t_spell.c` covers the restoration recipient limit and masks, plus
the active Absorb Mana command's target admission. Remaining recommended
integration fixtures: full cast-pipeline mana accounting, overlapping-Statue
suppression, `Rusp`/resource/food rejections and permanent transformation,
projectile impact after target death or save/load, Orb mana/splash and Devour
Magic per-hostile-target refund. Custom values should be used in addition to
stock-shaped tests. The engine is validated separately by the project test
suite; this document records current implementation gaps rather than claiming
complete retail parity.

## Visual investigation and remaining verification

MPQ animation-event inspection (31 October 2026) confirms:

- `Aave`: forward morph begins at 23,333 ms; SND fires there, Crumble2 SPN
  at 24,300 ms, geoset alpha handoff at 24,333 ms, end at 25,333 ms.
- `Afak`: `AnnihilationMissile.mdx` has Birth, Stand and Death sequences,
  including launch/hit SND events; no additional splash model is authored.
- `Advm`: missile Birth has `DevourMagic.wav`, and Death has generic hit SND.
  SpecialArt is `DispelMagicTarget.mdx`.

The current projectile mover is straight-line: `Advm`'s configured 0.10
missile arc is not yet simulated. Whether retail applies the gameplay dispel
strictly at missile impact has not been established from model data alone;
the engine now follows the visibly coherent impact-time policy. Projectile
Death event playback depends on the usual renderer event-crossing behavior.
No gameplay tests were executed during patch preparation.
