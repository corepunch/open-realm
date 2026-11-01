# Goblin Tinker: Cluster Rockets, Engineering Upgrade, Robo-Goblin and Demolish

## Source data and scope

This implementation uses the active TFT ability rows and the Goblin Tinker MPQ research report. The normal hero is `Ntin`; its mechanical form is `Nrob`. The source and destination share `HeroTinker.mdx`, with `Morph`/`Morph Alternate` and alternate idle/attack/death sequences. Neither form is the Clockwerk Goblin model. Pocket Factory already has a dedicated handler and is integrated with the Engineering variant lookup.

`ANeg` level 1/2/3 supplies movement speed +10%/+20%/+30% and attack damage +2/+4/+6. The authored `DataC`..`DataF` replacement chains cover Pocket Factory (`ANsy` to `ANs1..3`), Cluster Rockets (`ANcs` to `ANc1..3`), Robo-Goblin (`ANrg` to `ANg1..3`), and Demolish (`ANde` to `ANd1..3`). The stock rawcode pairs are kept together in the gameplay module; numeric stats are always read from the active SLK rows. The *learned* `ANeg` hero rank selects the variant; merely displaying an unlearned candidate skill does not activate it. Direct custom-map variant rawcodes remain unchanged.

## Cluster Rockets

`ANcs` and the Engineering aliases are point-targeted; `Area` increases from 200 through 230, 260 and 290. Mana cost 70, cooldown 6 seconds, and range 800 remain shared. The missile art and missile speed come from the ability function data; the stock speed is 700.

- `DataA`: damage **per damage interval**, 11.25/18.75/27.5 at spell ranks 1/2/3.
- `DataB`: 0.25-second interval.
- `DataC`: the presentation-only number of rockets, 6/12/18. Each rocket does **not** independently apply `DataA` damage.
- `DataD`: shared damage budget per interval, 105/195/300. Damage is scaled to this budget before integer distribution and capped after rounding.
- `DataE`: structure damage multiplier, 1 in stock TFT.
- `DataF`: 1.01-second damage-sequence window, yielding four 0.25-second pulses.
- `BNcs`: 1.01-second stun applied to eligible **ground** victims; air targets can take damage when allowed by `targs` but are not stunned.

Each successful cast owns a persistent interval thinker (`tinker_rockets_think`) whose timing, cursor point, rank, variant, and deterministic visual scatter seed live in serialized edict/channel fields. Caster ownership is incarnation-safe. The per-pulse budget is shared across eligible targets, and attack spell target filters exclude friendly targets. Separate non-damaging model projectiles travel to scattered impact points using the existing projectile movement system, then play their authored death presentations. Moving or interrupting the caster after the cast has released the pulses does not cancel pending pulses; this behavior remains to be verified against retail.

## Robo-Goblin

`ANrg` and `ANg1..3` are one-rank ability forms with authored cost 25, cooldown 1 second and 1.5-second morph value. The ability is **not a timed summon**: its no-target orders `robogoblin` / `unrobogoblin` switch `Ntin` and `Nrob` on the **same entity** using `G_TransformUnitType`, preserving Hero level/XP/learned skills, remaining life/mana proportions, owner, inventory, selection, buffs and save identity. Morph selects `Morph` when entering and `Morph Alternate` on return, and enables alternate animation properties for the mechanical form. The reverse command shares the same cooldown ledger as the forward one.

On entering robot form, add the current variant's `DataE` Strength (+5/+7/+9/+11); remove exactly that addition on return. `DataF` adds robot armor (+1/+2/+3/+4) in the hero-stat recompute path, not in an accumulating temporary-bonus ledger. Learning Engineering while already transformed changes the Strength difference once and recomputes armor. The `Nrob` UnitBalance row supplies mechanical classification and metal presentation; existing mechanical spell-target restrictions and worker-repair eligibility are reused. Individual retail spell immunities, repair costs and morph switch-frame timing still need retail comparison.

## Demolish and Pocket Factory

`ANde` and `ANd1..3` add building-only normal-attack multipliers of 2, 2.5, 3 and 3.5. They are effective only on the mechanical `Nrob` form when it carries Demolish. Apply the multiplier in the shared basic-attack damage path **once**, before regular attack-vs-defense multipliers; no extra splash or spell damage is introduced.

Pocket Factory selects its authored `ANs1..3` record from learned Engineering rank, including `DataA` production interval (stock 5/4/3.2/2.56 sec), `DataB` Clockwerk unit type and `UnitID` factory class. Current factories and missiles retain their source data at launch; future casts use the new Engineering rank. Stock button/learned skill rawcodes remain stable (`ANcs`, `ANsy`, `ANrg`, `ANeg`) so spell costs, cooldowns and hero-skill UI continue to use the learned base code.

## Validation and remaining retail differences

`game/tests/t_tinker.c` supplies synthetic SLK data and regression cases for rank/variant mapping, Demolish values, four Cluster Rockets damage intervals and a mid-salvo save/load. No local compilation or tests were run for this patch.

These are **still unverified** against retail: exact rocket launch frames and scatter/trajectories, damage distribution under simultaneous budget saturation, stun refresh per pulse, whether active salvos survive every interruption, Robo-Goblin unit-classification spell exclusions, 1.5-second morph switch timing and morph sounds, and whether custom-map Engineering changes applied by JASS outside hero learning should trigger an immediate recomputation. The implementation uses one consistent deterministic model for these cases rather than claiming exact retail fidelity.
