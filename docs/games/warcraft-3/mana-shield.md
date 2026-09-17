# Mana Shield

## Contract

`ANms` is TFT `CAbilityManaShield`, whose extracted parent is `AAsm`
(`CAbilitySimpleSpell`). It is a no-target toggle, not a passive. Activation adds
the authored `BuffID` status and deactivation removes it. The stock TFT row uses
`BNms`, and the stock immediate orders are `manashieldon` (`852589`) and
`manashieldoff` (`852590`). Those orders are directional: issuing the on order
twice must leave the shield on rather than toggle it off.

The TFT `AbilityData.slk` row has three levels:

| Field | Level 1 | Level 2 | Level 3 | Meaning |
| --- | ---: | ---: | ---: | --- |
| `DataA` | 1 | 1.5 | 2 | damage absorbed per mana |
| `DataB` | 1 | 1 | 1 | fraction of incoming damage offered to the shield |
| `BuffID` | `BNms` | `BNms` | `BNms` | active shield status |

`NeutralAbilityStrings.txt` confirms the DataA direction with “1/1.5/2 damage
per point of mana” and supplies the Activate/Deactivate presentation. Do not
interpret DataA as mana spent per absorbed damage; that reverses level scaling.

## Damage Flow

`CAbilityManaShield` owns status changes. `S_ManaShieldDamage()` runs at the
central `T_Damage()` boundary and only acts when the authored buff is active.
For incoming integer damage $D$, mana $M$, DataA ratio $r$, and clamped DataB
fraction $p$:

$$
A = \min(Dp, Mr), \qquad M' = M - A/r, \qquad D' = D - \lfloor A \rfloor
$$

If mana reaches zero, the buff is removed immediately. Learned-but-inactive,
explicitly deactivated, and zero-mana states leave ordinary damage unchanged.
No extra `edict_t` state is needed because `abilstatus` is the runtime source of
truth and remains covered by the existing save/load contract.

## Verification

Inspect the TFT class and normalized row with:

```sh
rg '"ANms"' games/warcraft-3/tft-ability-classes.txt
build/bin/ability_audit -data 'data/Warcraft III' -tft -raw ANms
```

The production-path regression casts and orders the toggle, checks `BNms`, and
drives partial and complete absorption through `T_Damage()`:

```sh
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test wc3_spell.mana_shield_toggle_status_controls_authored_damage_absorption
```