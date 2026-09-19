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
Buffs are state-only like Frost Nova's `Bfro`: no movement/armor
consumer reads `Bfro`/`BIcb`/`Bfre` yet.

## Verification

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw AIob
make test-wc3-engine WC3_PATTERN='wc3_item_lifecycle.orb_*'
```
