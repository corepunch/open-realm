# Unstable Concoction

## Contract

`Auco` is TFT-only `CAbilityUnstableConcoction` (parent `AAsm`). Its
`AbilityData.code` is `Auco` (no aliases). ROC `AbilityData.slk` has no row.

Batrider unit-target suicide: on cast completion the caster detonates at the
primary air target, dealing authored full damage to that target and partial
splash to nearby enemy/neutral air units, then dies. This is **not** Self
Destruct (`Asds`/`Asdg`); do not register `Auco` on `CAbilitySelfDestruct`.

`BuffID` is empty. Charge-time invulnerability and `DataF` move-speed bonus are
owned by the ability class (no status buff row). Focused tests drive
`S_CastUnitTargetSpell`, which fires `A_EXECUTE` once cast range is reached, so
detonation is immediate at execute; a separate out-of-range chase thinker is not
required for the melee-spell contract.

## Authoritative Fields

WorldEdit `AbilityMetaData` labels (`Dda*` shared with Self Destruct / death
damage; `Uco*` Auco-only):

| Field | Meta | Stock Auco | Role |
| --- | --- | ---: | --- |
| `DataA` | Full Damage Radius (`DDA1`) | 0 | secondary full-damage ring; stock 0 → only the primary gets full |
| `DataB` | Full Damage Amount (`DDA2`) | 600 | damage to the primary air target |
| `DataC` | Partial Damage Radius (`DDA3`) | 200 | splash radius around the primary |
| `DataD` | Partial Damage Amount (`DDA4`) | 140 | splash damage to other air units in `DataC` |
| `DataE` | Max Damage (`UCO5`) | 0 | optional cap on total splash damage; 0 = uncapped |
| `DataF` | Move Speed Bonus (`UCO6`) | 280 | charge-phase speed bonus (max 522) |
| `Rng` | | 400 | cast/issue range |
| `targs` | | air,neutral,enemy | air only; ground invalid |
| `BuffID` | | (empty) | no authored buff token |
| `Cost` / `Cool` / `Dur` | | 0 | free, no cooldown, no duration row |

Ubertip references `DataB` (primary) and `DataD` (nearby enemy air). Damage is
physical (`T_Damage`): armor applies. Autocast order names exist
(`unstableconcoction` / on / off); register `AB_AUTOCAST`.

## Data Flow

```text
AbilityData.slk (Auco, TFT only)
  -> targs, Rng, DataA-F
CAbilityUnstableConcoction
  -> A_VALIDATE: air + enemy/neutral (S_SpellAllowsTarget)
  -> A_EXECUTE: DataB to primary; DataD splash to other air in DataC;
                optional DataA full ring / DataE splash cap;
                G_SetHealth(caster,0) + die
```

TFT also names `CMissileUnstableConcoction` for charge presentation. The cast
path does not leave a separate missile edict when execute detonates in place.

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Auco
```

## Registry

```c
{ "Auco", CAbilityUnstableConcoction, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.unstable_concoction*'
```

## Remaining gaps

- Charge-phase invulnerability and `DataF` move-speed while closing from cast
  range into contact (retail flies in after the 400-range cast commit).
- AMS quirk: retail Anti-Magic Shell can block this physical blast; not modeled.
