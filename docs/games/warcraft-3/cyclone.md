# Cyclone

## Contract

`Acyc` is TFT `CAbilityCyclone` (parent `AAsm`). `ACcy`, `SCc1`, and `Acny` are
`AbilityData` aliases whose `code` is `Acyc`, so they share `CAbilityCyclone` and
read their own rows through `abilityitem_t.code`. Item `AIcy` is an `Acyc` alias
but remains out of scope.

| Rawcode | Archive | Notes |
| --- | --- | --- |
| `Acyc` | ROC and TFT | Druid of the Talon Cyclone |
| `ACcy` | ROC and TFT | creep Cyclone |
| `SCc1` | ROC and TFT | Cenarius Cyclone |
| `Acny` | TFT | naga Cyclone (`organic` in targs) |
| `AIcy` | ROC and TFT | item Cyclone — unregistered here |

Casting tosses one living unit into the air until `Dur` / `HeroDur` expires. While
cycloned the victim cannot move, attack, or cast, and others cannot attack or
target it with spells. Physical `S_ResolveAttackHit` / `T_Damage` also no-op.

## ROC vs TFT

| Field | ROC `Acyc` | TFT `Acyc` |
| --- | --- | --- |
| `targs` | `ground,enemy,neutral` | `ground,enemy,neutral,organic` |
| `cost` / `cool` / `rng` | 150 / 5 / 600 | 150 / 5 / 600 |
| `Dur` / `HeroDur` | 30 / 6 | 20 / 6 |
| `DataA` | 0 | 1 (`Can Be Dispelled`) |
| `BuffID` | empty | `Bcyc,Bcy2` |

TFT adds the `organic` token so mechanical units fail `S_SpellAllowsTarget`. ROC
rows omit `BuffID`; apply `Bcyc` as the documented fallback (same pattern as empty
`Aams` → `Bams`). `unit_addtimedstatus` reads only the first four characters, so
`Bcyc,Bcy2` applies `Bcyc`. Do not invent `DataC`.

`DataA` is dispel eligibility only. OpenWarcraft3 does not yet wire a Cyclone dispel
path; leave that gap documented rather than inventing one.

## Data Flow

```text
AbilityData.slk (Acyc / ACcy / SCc1 / Acny)
  -> targs, Cost, Rng, Dur/HeroDur, BuffID, DataA
CAbilityCyclone
  -> A_VALIDATE: S_SpellAllowsTarget(code, ...) (rejects cycloned / wrong targs)
  -> A_EXECUTE: unit_addtimedstatus(first BuffID token or "Bcyc")
S_UnitIsCycloned (Bcyc or Bcy2)
  -> s_move order_move / walk
  -> s_attack can_attack / attack_target_is_valid / T_Damage
  -> s_spell spell_validate / S_SpellAllowsTarget
```

Neutral is an allowed allegiance in `targs`. Do not require `S_SpellIsEnemy` alone.

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Acyc
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.cyclone*'
```

Focused tests cover alias procedure lookup, TFT organic/mechanical and ally
filters, already-cycloned rejection without mana spend, move/attack/spell/damage
locks, ROC empty-BuffID → `Bcyc`, HeroDur vs Dur, expiry restore, and recast.
