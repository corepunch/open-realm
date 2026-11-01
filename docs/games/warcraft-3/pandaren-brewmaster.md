# Pandaren Brewmaster (TFT)

The active Warcraft III dataset governs the normal (`ANbf`, `ANdh`, `ANdb`,
`ANef`) and campaign (`ANcf`, `Acdh`, `Acdb`, `Acef`) skill aliases. Gameplay
belongs in the Warcraft III game module. All rank numbers below are from the
reported active TFT data and are **not** hardcoded in the implementation.

## Breath of Fire and Drunken Haze

Breath of Fire uses `DataA` initial damage, `DataB` total initial-damage budget,
`DataC` length, `DataD` terminal cone width, and `DataE` ignition damage per
second. The corridor extends **from the caster toward the target point**. It
uses the authored area targeting rules plus enemy relationship, so allied units
outside the hostile effect are not burned. An eligible unit already carrying
`BNdh` gets the `BNbf` burn status. The initial cone hit does not consume Haze.

Ignition is owned by a dispellable `BNbf` status and a saveable one-second
thinker keyed to the status instance. New applications replace the previous
status: its old thinker retires without applying extra damage. Dispel, target
death and expiration stop damage. `BNbf` target art is owned by the status.

Drunken Haze (`ANdh`, `Acdh`) launches the authored homing bottle and samples
its `Area` at impact. The resulting `BNdh` status owns its duration, target art,
DataC movement-speed reduction and DataB attack miss chance. The slow uses the
shared strongest-negative status modifier policy and does not require bespoke
movement logic. The miss roll occurs on the **attacker**, after the defender's
ordinary evasion roll. Campaign `Acdh` keeps its separate DataB/C values.

## Drunken Brawler

The shared critical and evasion calculations also accept campaign `Acdb`.
Normal `ANdb` has critical chance `DataA=10%`, damage multiplier `DataB=2/3/4`,
and dodge chance `DataD=7/14/21%`. Campaign rank 4 uses its authored 6x
critical and 30% dodge. Existing combat timing and critical/evasion behavior
remain shared; stacking with other critical passives requires retail comparison.

## Storm, Earth and Fire

The `ANef`/`Acef` spell **does not have a UnitID**. Its Fire elemental ID is
`DataA` (`npn1`, or `npn4` for campaign rank 2). The other forms are `npn2`
Storm and `npn3` Earth. The ability now creates all three atomically, registering
ordinary summon ownership/events/abilities. If spawning one fails, previously
created elementals are retired and the hero is not hidden.

A live controller maintains the original hero's entity/JASS identity and a
`Dur` countdown (45 seconds normally, 60 seconds for campaign). While active
it hides, unlinks, pauses and suspends the original hero from world interaction
using the same game-owned hidden-unit contracts as other off-world effects.
Players selecting the hero receive the three elementals instead. Inventory,
hero skill ranks, experience and attributes remain stored on the hidden hero;
the elemental units never copy them.

The controller checks its linked elementals while the ultimate is active. If
all die, the hero is unhidden and killed. At expiry, surviving elemental(s)
vanish and the original hero returns at a legal location near the last surviving
elemental. The same controller persists in saves via the registered function
pointer and shared incarnation-safe owner identity. No new edict fields are
required. Elementals receive the authored `UnitAbilities` of their own form
(Storm: Dispel/Cyclone/Wind Walk/Resistant Skin; Earth: Spell Immunity,
Pulverize, Taunt, Resistant Skin; Fire: Permanent Immolation, Resistant Skin).

## Remaining retail parity / limitations

- Precise Breath of Fire cone sampling, target-order damage cap, projectile
  travel time and ignition tick phase have not been compared with retail.
- Drunken Haze impact after primary target death and exact bottle arc/audio
  are not yet retail-verified.
- The linked hero is suspended rather than moved to an explicit separate realm.
  Suspension of active buffs, inventory cooldown ticks, shared selections and
  JASS event ordering should be checked in campaign and multiplayer.
- Storm/Earth/Fire vanish and return animations and exact return selection
  policy remain to be compared against MDX event metadata and retail.
- Existing elemental secondary ability implementations need their normal
  per-ability tests; their behavior is not automatically proven by this patch.

The provided report did not inspect the pending combined baseline or extracted
MDX event tracks. The behavior changes above should be tested against the
active source archive and retail, rather than inferred from model durations.

## Focused verification

Check normal and campaign rank ultimate cast, individual and simultaneous
elemental death, expiry with one survivor, full-death outcome, save/load during
split, hero inventory and level preservation, repeated casting, controller
cleanup on hero removal, as well as Haze/Breath interactions with nonstock
values, immune/air/organic targets, dispel and multiple casters.
