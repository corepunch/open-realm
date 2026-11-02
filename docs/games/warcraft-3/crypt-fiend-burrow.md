# Crypt Fiend Burrow (TFT)

The `Abur` ability is a researched, no-target transformation, not the neutral-creep
night sleep ability `ACsp`. The original unit `ucry` transforms into the burrowed
unit `ucrm`, and back again via `burrow`/`unburrow` orders. Research `Rubu`
permits the command only in TFT mode. Both forms use the same
`Units\\Undead\\CryptFiend\\CryptFiend.mdx` model. Burrowed presentation comes
from the model's `alternate` animation property, which selects `Stand Alternate`;
the transition uses the non-looping `Morph` and `Morph Alternate` sequences.
The alternate unit row provides movement, attack and regeneration properties;
the server uses the existing transform path rather than maintaining a separate
copy of those statistics.

## Verification and remaining fidelity

- Verify no Burrow command in RoC and a disabled command before Rubu in TFT.
- Research Rubu at the Crypt and verify Burrow/Unburrow on one and multiple Fiends.
- While burrowed check invisibility vs detection, 5 HP/sec regardless of blight,
  disabled movement/attacks, damage from area spells, and save/load.
- Retail transitions take approximately 1.45 seconds. This implementation uses
  immediate `G_TransformUnitType` and does **not** yet sequence transition
  animations/timing, so that is an outstanding presentation/behaviour gap.
- The transform path relies on `ucrm` authored row for regeneration and visibility;
  compare those effects in actual loaded Warcraft data during manual verification.

Research references: Blizzard's classic Crypt Fiend page and TFT manual.
