# Sentry Ward

## Contract

`Aeye` is ROC/TFT `CAbilityEvilEye` (parent `AAsm`). `AIsw` is an item alias whose
`code` is `Aeye`, so it shares the procedure and reads its own row through
`abilityitem_t.code`. `APwt` (Rune of the Watcher) is the same `code=` but remains
unregistered here.

| Rawcode | Archive | Notes |
| --- | --- | --- |
| `Aeye` | ROC and TFT | Witch Doctor Sentry Ward |
| `AIsw` | ROC and TFT | item Sentry Ward (`code=Aeye`) |
| `APwt` | TFT | Rune of the Watcher — unregistered |

Point cast summons an invisible timed detection ward. True sight comes from the
ward unit's `Adt1` (`CAbilityDetector`, `code=Adet`) detect range (`Rng`), not
from `Aeye` Data cells (`Aeye` has no DataA–I).

## Authoritative Fields

| Field | Meta | Stock Aeye TFT | Stock AIsw TFT | Runtime meaning |
| --- | --- | ---: | ---: | --- |
| `UnitID` | Ward Unit Type (`hwdu`, shared with Ahwd) | `oeye` | `oeye` | ward unit (`S_SpellUnitId`) |
| `Dur` / `HeroDur` | | `600` | `300` | ward timed life (`BTLF`) |
| `BuffID` | | `Beye` | `Beye` | summoned-ward presentation; not required for detect |
| `Cost` | | `50` | `0` | |

ROC omits `UnitID` / `BuffID` on both rows.

`oeye` UnitAbilities: `Adt1,Aeth`. Stock `Adt1` TFT: `Rng=1100`, `DataA=3`
(detectionType). OpenWarcraft exposes detection as `S_UnitIsDetected` for a
`RF_HIDDEN` unit inside a living friendly sentry ward's detect radius.

## Data Flow

```text
AbilityData.slk (Aeye / AIsw)
  -> UnitID, Dur
CAbilityEvilEye
  -> S_SummonAt(caster, UnitID, point, Dur)
  -> ward.summon_ability = cast rawcode; RF_HIDDEN
  -> ward.wait = Adt1 Rng (detect radius)
S_UnitIsDetected(unit)
  -> unit RF_HIDDEN and enemy-to-unit Aeye/AIsw ward within ward.wait
```

FOW reveal while the ward itself is `RF_HIDDEN` is a known gap (invisible wards
should still grant owner vision). Detection of other invisible units is the
contract covered here. `Adt1` / `Atru` remain separate passive TODOs.

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Aeye
build/bin/ability_audit -data 'data/Warcraft III' -raw AIsw
build/bin/ability_audit -data 'data/Warcraft III' -raw Adt1
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.sentry_ward*'
```
