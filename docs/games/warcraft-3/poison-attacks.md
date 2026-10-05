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

The first authored poison buff token owns a deterministic one-second status
pulse. The pulse reads `DataA` from the applying `Aven`/`Apoi`/`Apo2` row and
retains the applying source for damage attribution. Reapplication by that same
source preserves the existing pulse deadline while refreshing the authored
status lifetime; replacement by another source starts a fresh pulse phase. The
second token remains presentation/status state so a `Bpoi,Bpsd` or
`BIpb,BIpd` pair does not double the authored DPS. If the stored source entity
is no longer valid, the status keeps its authored lifetime but deals no further
damage rather than guessing a different damage owner.

Current status storage still has one slot per buff rawcode, so exact retail
stacking between different attackers that apply the same poison buff remains a
follow-up rather than being guessed here.

## Verification

`wc3_spell.poison_dataa_ticks_through_entity_scheduler_and_preserves_same_source_phase`
uses a deliberately non-stock `DataA=7`, applies poison through
`S_ResolveAttackHit()`, drives `G_RunEntity()` rather than the status drain
directly, proves no early pulse, proves the one-second pulse, refreshes from the
same source between pulses, and verifies expiry stops later damage.

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Aven
make test-wc3-engine WC3_PATTERN='wc3_spell.poison_*'
```
