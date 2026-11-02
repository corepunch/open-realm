# Carrion Beetles

## Contract

Crypt Lord Carrion Beetles (`AUcb`, `CAbilityCarrionScarabs`) targets one
raisable ordinary corpse and consumes it to create permanent beetle summons.
The spell supports manual corpse targeting and right-click autocast. Autocast
chooses the nearest valid corpse within authored range. Each Crypt Lord is
limited to its authored maximum of living beetles; dead beetles no longer count.

## Object data

The local ROC and TFT `AUcb` rows agree:

| Field | Stock value | Runtime meaning |
| --- | ---: | --- |
| `Cost` | 30 | Mana per successful cast |
| `Cool` | 6 | Cooldown |
| `Rng` | 900 | Corpse target range |
| `Dur` | 0 | Permanent beetle life |
| `targs` | `dead` | Corpse target category |
| `DataA` | 1 | Beetles created per consumed corpse |
| `DataC` | `ucs1` / `ucs2` / `ucs3` | Summoned unit by ability rank |
| `DataE` | 5 | Maximum living beetles per Crypt Lord |

These fields are read from the concrete ability row, so map-authored aliases
retain their own cost, range, beetle type and cap. The `AUcb` unit ability data
does not enable autocast by default; the player can toggle it with the command
button's right-click autocast control.

## Runtime flow

`s_skills.c` registers `AUcb` as a unit-target spell with `AB_AUTOCAST`.
`CAbilityCarrionScarabs` validates corpse eligibility, range and remaining
beetle capacity. Manual casts use the shared unit-target spell approach. The
autocast callback finds the nearest eligible corpse and submits the same spell
through `S_CastUnitTargetSpell`.

On execution, `DataA` beetles of the rank's `DataC` unit type are created at the
corpse location with `S_SummonAbilityAt`. A corpse is removed only after at
least one beetle was successfully created. `DataE` counts living beetles whose
`summon_ability` belongs to this Carrion Beetles ability and whose summoning
owner is the Crypt Lord. A zero-duration summon receives no timed-life status.

## Data inspection

The local rows were inspected with:

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw AUcb
build/bin/mpqtool -data 'data/Warcraft III' cat Units/UnitAbilities.slk
```

`ability_audit` reports the same `AUcb` rank values in ROC and TFT. `Ucrl` has
`AUcb` in its hero ability list, and the unit ability row leaves its autocast
default off.
