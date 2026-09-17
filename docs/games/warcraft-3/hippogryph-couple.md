# Hippogryph Couple (Mount / Pick up Archer / Dismount)

## Contract

`Acoa`, `Acoh`, and `Adec` are one mount/dismount system. They are **not**
Defend (`Adef`), Raven Form, or cargo load. TFT class names:

| Rawcode | Archive | Class | Parent | Role |
| --- | --- | --- | --- | --- |
| `Acoa` | ROC+TFT | `CAbilityCoupleArcher` | `Acou` | Archer mounts a partner unit |
| `Acoh` | ROC+TFT | `CAbilityCoupleHippogryph` | `Acou` | Partner picks up an Archer |
| `Adec` | TFT only | `CAbilityDecouple` | `AAsm` | Rider splits back into two units |

`Acou` (`CAbilityCouple`) is an abstract parent with no AbilityData row. Do not
register it.

Orders (Hive / `OrderIds`): `mounthippogryph` (`Acoa`), `loadarcher` (`Acoh`),
`decouple` (`Adec`).

### Authored fields (TFT)

| Rawcode | DataA | DataB | UnitID | Rng | Cool | Cost |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| `Acoa` | `ehip` (partner type) | — | `ehpr` (rider) | 100 | 0 | 0 |
| `Acoh` | `earc` (partner type) | — | `ehpr` (rider) | 100 | 0 | 0 |
| `Adec` | `earc` (companion 1) | `ehip` (companion 2) | — | 0 | 30 | 0 |

ROC `AbilityData` keeps `Acoa`/`Acoh` string rows but leaves `DataA`/`UnitID`
empty; `Adec` is absent. Empty UnitID must fail the mount (do not invent
`ehpr`). Stock ubertips still say “cannot dismount”; that text predates TFT
`Adec` on the rider.

`targs` is empty (`_`). Companion gating is ability-owned: same-owner living
unit whose `class_id` equals authored `DataA` (via `S_SpellDataId`).

### Couple Instant (out of scope)

`Aco2`/`Aco3` share `code=Acoi` (`CAbilityCoupleInstant`, parent `AAcs`):
area auto-find, `DataB` move-to-partner flag, large range. Keep the existing
`Acoi` stub; do not treat them as `Acoa`/`Acoh` aliases.

## Data Flow

```text
Acoa/Acoh AbilityData
  -> DataA partner fourcc, UnitID rider fourcc, Rng
CAbilityCoupleArcher / CAbilityCoupleHippogryph
  -> A_VALIDATE: friend + alive + class_id == DataA + UnitID present
  -> A_EXECUTE: SP_SpawnAtLocationNoBirth(UnitID), G_FreeEdict both inputs
Adec AbilityData
  -> DataA/DataB companion fourccs
CAbilityDecouple
  -> A_EXECUTE: spawn DataA + DataB at rider origin, G_FreeEdict rider
```

Merge consumes **two edicts** and creates a **third** (Warsmash CoupleInstant /
retail UnitID). Dismount is the inverse. Facing copies from the caster/rider.
HP blending and selection-replacement polish are not required for the first
contract.

## Registry rows

```c
{ "Acoa", CAbilityCoupleArcher, AB_SPELL, SPELL_TARGET_UNIT },
{ "Acoh", CAbilityCoupleHippogryph, AB_SPELL, SPELL_TARGET_UNIT },
{ "Adec", CAbilityDecouple, AB_SPELL, SPELL_TARGET_NONE },
```

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Acoa
build/bin/ability_audit -data 'data/Warcraft III' -raw Acoh
build/bin/ability_audit -data 'data/Warcraft III' -raw Adec
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.hippogryph_couple*'
```

Focused tests cover procedure registration, authored DataA/UnitID/DataB
mapping with non-stock fourccs, successful mount/dismount, and rejects for
enemy, dead, and wrong-type partners without mana spend.
