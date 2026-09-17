# Dispel Magic / Disenchant / Devour Magic

## Contract

`Adis`, `Adch`, and `Advm` share one registered procedure `CAbilityDispelMagic`
in `s_human_abilities.c`. Each rawcode keeps its own `AbilityData` row through
`abilityitem_t.code`. Do not add a second procedure.

| Rawcode | Archive | Class (TFT) | Parent | `code=` |
| --- | --- | --- | --- | --- |
| `Adis` | ROC + TFT | `CAbilityDispelMagic` | `AAsm` | `Adis` |
| `Adch` | TFT only | `CAbilityDisenchant` | `Adis` | `Adch` |
| `Advm` | TFT only | `CAbilityDevourMagic` | `AAsm` | `Advm` |

`Adch` is Disenchant(old). TFT also authors `Adcn` (Disenchant(new)) with
`code=Adis`, and creep `Adsm` with `code=Adis`. Creep `ACde` has `code=Advm`.
Those aliases are unregistered here; parent owns `s_skills.c`.

### Authored fields

| Field | `Adis` | `Adch` | `Advm` | Meaning |
| --- | ---: | ---: | ---: | --- |
| `Area` | 200 | 200 | 200 | point blast radius |
| `DataA` | 0 | 0 | 50 | Advm: HP heal per buff removed |
| `DataB` | 200 | 300 | 75 | Adis/Adch: summoned damage; Advm: mana heal per buff removed |
| `DataE` | 0 | 0 | 180 | Advm: summoned damage (not DataB) |
| `DataF` | 0 | 0 | 1 | unused here |
| `targs` | air,ground,ward,invu,vuln(,tree TFT) | … + `enemy` | air,ground,ward,invu,vuln,tree | point-area filter not yet applied |

Ubertips:

- Adis/Adch: remove all buffs in area; DataB damage to summoned units.
- Advm: consume magical buffs; each consumed buff heals the caster DataA HP and
  DataB mana; DataE damage to summoned units.

Retail Advm string wording is “each unit that is devoured of magic”. This
implementation heals **per timed buff actually removed**, as required by the
focused tests (non-stock DataA/DataB × two buffs).

### Summoned predicate

`S_SummonAt` / `summon_unit` set `edict->owner` to the summoner. Mirror Image
also sets `AI_ILLUSION`; Spirit Wolf also sets `summon_ability`. Purge uses
`owner`. Dispel uses the same `owner` mark (plus `summon_ability` /
`AI_ILLUSION` when present). Ordinary units have `owner == NULL` and take no
summoned damage.

## Data Flow

```text
AbilityData.slk (Adis / Adch / Advm)
  -> Area, DataA/B/E, Cost, Rng, targs
CAbilityDispelMagic
  -> clear timed abilstatus slots (skip S_StatusIsUndispellable; S_HumanStatusExpired first)
  -> if DataE > 0: heal caster DataA/DataB per removed buff; damage summons DataE
  -> else: damage summons DataB
S_SpellDamage / S_SpellHeal
```

Cyclone `DataA==0` (ROC) is undispellable via `S_StatusIsUndispellable` owned by
`s_cyclone.c`. Skipped slots do not increment Advm's per-buff heal count. See
[Cyclone](cyclone.md).

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Adis
build/bin/ability_audit -data 'data/Warcraft III' -raw Adch
build/bin/ability_audit -data 'data/Warcraft III' -raw Advm
```

ROC prints `AbilityData: not found` for Adch/Advm; TFT prints the rows above.

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.dispel*'
make test-wc3-engine WC3_PATTERN='wc3_spell.devour*'
```
