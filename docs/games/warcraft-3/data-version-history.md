# Warcraft III Data Version History

This note tracks selected unit and ability changes that matter when comparing
the 1.07 TFT data in `data/war3cds/combo` with the 1.29 data in
`data/Warcraft III`. A missing row in 1.07 can mean the content did not exist
yet, rather than that the older archive accidentally omitted a field or asset.

## Units added after 1.07

| Patch | Unit | Rawcode | Type |
|---|---|---|---|
| 1.15 | Goblin Tinker | `Ntin` | Neutral Tavern Hero |
| 1.15 | Robo-Goblin | `Nrob` | Tinker's transformed form |
| 1.15 | Pocket Factory | Level-dependent | Summoned building |
| 1.15 | Clockwerk Goblin | `ncgb` | Factory-produced unit |
| 1.17 | Firelord | `Nfir` | Neutral Tavern Hero |
| 1.17 | Goblin Alchemist | `Nalc` | Neutral Tavern Hero |
| 1.17 | Lava Spawn | Level-dependent | Firelord summoned unit |

## Abilities added or newly assigned after 1.07

| Patch | Ability | Rawcode | Unit | Change |
|---|---|---|---|---|
| 1.10 (3 Jul 2003) | Elune's Grace | `Aegr` | Night Elf Archer | Added a passive that reduces Piercing damage by 35% and spell/Magic damage by 20%. |
| 1.19 (19 Sep 2005) | Resistant Skin | `Arsk` | Orc Spirit Walker | Added the existing passive to Spirit Walkers, reducing negative spell duration and granting immunity to effects such as Polymorph and Possession. |

## Audit implication

When auditing ability, status, unit, or asset availability, compare the
appropriate version's rows before treating a missing rawcode as a compatibility
bug. In particular, Goblin Alchemist abilities such as Acid Bomb and the
Firelord's Lava Spawn behavior postdate 1.07. A fallback for a later ability's
missing `BuffID` does not add that ability, unit, or its presentation assets to
the 1.07 data set.

The Tinker, Firelord, Alchemist, and Spirit Walker implementation notes cover
current runtime behavior for these units and abilities; this table records the
version boundary for interpreting their source data.
