# Night Elf Wisp Detonate (`Adtn`)

## Gameplay contract

`Adtn` is a point-targeted magical self-sacrifice, **not** Goblin Sapper
Kaboom (`Asds`). The `detonate` order uses Warcraft order ID `852145`.
An accepted out-of-range point order uses the normal spell Move-approach
thinker; replacing that Move cancels the pending cast. At execution the
Wisp's **current** position, not the clicked point, is the blast centre.
There is no ordinary death-triggered Detonate when an enemy kills a Wisp.

The ability reads each concrete AbilityData alias independently:

| Authored field | Application |
|---|---|
| `Rng` | Approach radius for the point order |
| `Area` | Radius around the caster at impact |
| `DataA` (`Dtn1`) | Mana removed per eligible unit, down to zero |
| `DataB` (`Dtn2`) | Damage to hostile eligible summons |
| `targs` | Affected unit/ward classification |

Dispelling magical statuses uses `S_DispelUnitMagic()` and the regular
`STATUS_REMOVE_DISPEL` lifecycle. Timed-life, physical-only and protected
buff records remain. All eligible units, including friendly/allied units,
lose the authored mana amount except invulnerable and spell-immune ones;
invulnerable units can still lose eligible buffs. Damage is restricted to
hostile eligible summons, illusions and wards. `S_SummonIsDispelImmune()`
protects Animate Dead and Resistant Skin; standard spell immunity also
applies. The effect is instantaneous, and no ordinary area HP damage is
dealt. A valid cast consumes the Wisp even if no unit is hit.

Sacrifice uses `AI_SCRIPT_EXPLODED` and `unit_die()` to release food,
harvest assignments, selected-unit state and other standard unit lifecycle
resources, suppressing the raisable corpse. The unit's `specialArt` is
rendered by the existing death path, so the ability handler must not spawn
another explosion. Spell-effect events are emitted before the death events
by the existing spell executor. Banish does not prohibit a magical cast.

## Blight dispelling

`G_DispelBlightRadius()` removes raster Blight from unmaintained terrain
corners, while preserving corners within living `Abli` Blight Growth
sources' current radius (including a conservative tile margin). Detonate,
Dispel Magic and Disenchant invoke this operation; Destroyer's Devour Magic
does not. It reuses the existing per-player dirty-row replication.

**Current limitation:** the Blight raster stores a single painted bit per
cell and does not track Sacrificial Skull versus authored/pre-placed source
identity. Therefore this implementation conservatively protects live
building-growth zones but can erase other unowned/pre-placed Blight. Exact
skull-only source provenance and removal of obsolete destructable Blight
tints need separate data-model work. Do not mistake these for proven retail
source rules.

## Validation

In-engine `wc3_spell.detonate_*` tests use non-stock `DataA=17`,
`DataB=123`, `Area=247` plus an `A0dt` derived alias. They cover mana,
summon damage, ally/self protection, buff dispel, invulnerability, spell
immunity, Resistant Skin, Banish, empty blasts and no death-triggered
explosion. The existing `wc3_spell.self_destruct_*` tests must continue to
pass unchanged; Goblin Sapper Kaboom remains a separate ability.

Remaining retail observational checks: dynamic occupied-point blocking,
exact area-edge footprint inclusion, stock `specialArt` and sound timing in
ROC and TFT, unusual resistant summon classification, and spell finish/end
ordering during self-sacrifice. No new global debug logging is necessary.

Sources: Blizzard classic [Wisp](https://classic.battle.net/war3/nightelf/units/wisp.shtml),
[Spell Basics](https://classic.battle.net/war3/basics/spellbasics.shtml),
and Warcraft III patch 1.15 notes (Blight dispel and Banish exceptions).
