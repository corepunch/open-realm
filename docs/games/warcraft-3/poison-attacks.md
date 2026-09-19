# Poison Attacks

The poison triple (`Aven` Envenomed Spears, `Apoi` Poison Sting, `Apo2`
Orb of Venom) shares one shape: single level, self `code=`, `DataA`
poison DPS, `DataD`, and a two-token `BuffID` (`Bpoi,Bpsd`, or
`BIpb,BIpd` for the `Apo2` item orb). ROC omits `BuffID`. All three rows
use `CAbilityPoisonAttack` (passive, mirroring `CAbilitySlowPoison`).

`Aven` is also the `code=` parent of the already-registered `ACvs`
(Venom Spears, creep) row, so registering it repairs `ACvs` dispatch the
way `Anhe`/`ACtc` repair `Anh1`/`ACt2`.

## Mapping

| Poison | BuffID (TFT; ROC omits) | Behavior |
| --- | --- | --- |
| `Aven` | `Bpoi,Bpsd` | native passive, `Dur`/`HeroDur` |
| `Apoi` | `Bpoi,Bpsd` | native passive, `Dur`/`HeroDur` |
| `Apo2` | `BIpb,BIpd` | held orb item, `Dur`/`HeroDur` |

`S_PoisonOnHit` in `s_status_spells.c` applies both `BuffID` tokens
with authored durations from `S_ResolveAttackHit`, checking native
ownership and held poison-orb items (the same poison from both applies
once).

`TODO(1:1)`: `DataA` poison DPS needs the status-system
periodic-damage tick first — the same gap Shadow Strike documents for
`BEsh` and Slow Poison notes for its `DataA`. Buffs are state-only: no
consumer reads `Bpoi`/`Bpsd`/`BIpb`/`BIpd` yet.

## Verification

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Aven
make test-wc3-engine WC3_PATTERN='wc3_spell.poison_*'
```
