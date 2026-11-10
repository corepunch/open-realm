# Item Orbs

The six `AIDB`-parent attack orbs (`AIfb` fire, `AIlb` lightning, `AIob`
frost, `AIpb` poison, `AIcb` corruption, `AIzb` freeze) share one data
shape: `DataA` bonus damage, single level, self `code=`. `AIcb` also
authors `DataB` and all TFT rows carry `DataE=2`; neither has a consumer
yet. Do not register them on the guessed spell procs from the old TODOs
(`CAbilityOnFireHuman`, `CAbilityFrostNova`, `CAbilityImmolation`) —
those are wrong-class mappings of the same kind the creep-alias cleanup
unregistered.

## Mapping

| Orb | BuffID (TFT; ROC omits) | Behavior |
| --- | --- | --- |
| `AIfb` | none | damage only |
| `AIlb` | none | damage only |
| `AIob` | `Bfro` | damage + frost state, `Dur`/`HeroDur` |
| `AIpb` | none | damage only |
| `AIcb` | `BIcb` | damage + corruption state, `Dur`/`HeroDur` |
| `AIzb` | `Bfre` | damage + freeze state (`Dur` 0 = indefinite) |

All six rows use `CAbilityAttackBonus` with flags `0`, exactly like
`AIat`: pickup applies `DataA` to `temporaryDamageBonus`, drop reverses
it. `S_OrbOnHit` in `s_item_stats.c` applies the BuffID state from
`S_ResolveAttackHit` next to `S_SlowPoisonOnHit`, checking native orb
ownership and held orb items (the same orb from both applies once).
The status modifier system consumes the frost buff (`Bfro`) as the classic
50% movement and 25% attack-rate slow. `BIcb` and `Bfre` also feed their
respective authored status consumers. The Orb of Frost regression drives
`S_OrbOnHit`, checks both slow modifiers, and advances the entity scheduler
through the authored duration to verify they expire.

The `oven` item (`AIpb,Apo2`) also supplies an air-target missile attack when
the carrier has no enabled authored attack that can hit air. Its transient
profile uses the unit's Attack 1 damage, range, and timing with `Apo2`'s
authored `Abilities\\Spells\\Items\\OrbVenom\\OrbVenomMissile.mdl`; it does
not overwrite the unit's authored Attack 2.
Dropping the item removes the capability immediately because it is derived
from current inventory.

## Attack Damage Bonus Aliases

The passive item attack damage family is also handled by `CAbilityAttackBonus`.
TFT aliases `AIt6`, `AIt9`, `AItc`, `AItf`, `AItg`, `AIth`, `AIti`,
`AItj`, `AItk`, `AItl`, `AItn`, and `AItx` share `AIat`'s `DataA`
pickup/removal behavior. Their values come from each alias's own AbilityData
row, so an item carrying `AItg` uses its authored +1 while `AItx` uses +20.
ROC includes the `+6`, `+9`, `+12`, and `+15` aliases; the remaining aliases
are TFT-only. The registry maps all twelve directly to the same procedure.

## Verification

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw AIob
make test-wc3-engine WC3_PATTERN='wc3_item_lifecycle.*'
```
