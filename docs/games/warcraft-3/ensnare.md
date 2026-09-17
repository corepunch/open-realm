# Ensnare

## Contract

`Aens` is TFT `CAbilityEnsnare` (parent `AAsm`). `ANen` (Naga, TFT-only) and
`ACen` (creep) are `AbilityData` aliases whose `code` is `Aens`, so they share
`CAbilityEnsnare` and read their own rows through `abilityitem_t.code`.
`ACen` remains unregistered here.

| Rawcode | Archive | Notes |
| --- | --- | --- |
| `Aens` | ROC and TFT | Raider Ensnare |
| `ANen` | TFT | Naga Ensnare (`code=Aens`) |
| `ACen` | ROC and TFT | creep Ensnare — unregistered |

Casting binds one living enemy/neutral unit so it cannot move for `Dur` /
`HeroDur`. Air units are forced onto the support surface for the buff duration
and can be attacked as land units while grounded.

## ROC vs TFT

| Field | ROC `Aens` | TFT `Aens` |
| --- | --- | --- |
| `targs` | `ground,air,enemy,neutral` | same |
| `cool` / `rng` | 20 / 400 | 16 / 500 |
| `Dur` / `HeroDur` | 20 / 5 | 12 / 3 |
| `DataA` / `DataB` / `DataC` | 0.6 / 200 / 128 | same |
| `BuffID` | empty | `Bena,Beng` |

OE labels for DataA–C are `flyingUnitAdjust`, `flyingUnitHeight`, and
`meleeRange`. Immediate land-and-lock clears `AI_FLYING` and sets
`unitinfo.FlyHeight` to 0; gradual adjust/height from DataA/DataB is not
consumed yet. DataC melee-range presentation is unused.

ROC omits `BuffID`; apply `Bens` (`CBuffEnsnare`) as the documented fallback
(same pattern as empty `Aams` → `Bams` / empty `Acyc` → `Bcyc`). TFT
`BuffID=Bena,Beng` selects `Bena` (`CBuffEnsnareAir`) for flyers and `Beng`
(`CBuffEnsnareGround`) for ground units. Both are subclasses of `Bens` in the
TFT class tree; move lock and land restore treat all three as ensnare.

## Data Flow

```text
AbilityData.slk (Aens / ANen)
  -> targs, Cost, Rng, Dur/HeroDur, BuffID, DataA-C
CAbilityEnsnare
  -> A_EXECUTE: unit_addtimedstatus(Bena | Beng | Bens fallback)
unit_refreshstatusflags
  -> while ensnared: clear AI_FLYING, FlyHeight=0, M_CheckGround
  -> after expiry/dispel: restore AI_FLYING + authored moveHeight when movetp=fly
order_move
  -> S_UnitIsEnsnared / BEer reject movement
```

Restore uses authored `UnitData.moveTypeName == "fly"` and `moveHeight`, not a
saved status payload, so expiry, death cleanup, and dispel all re-enter the same
refresh path after the buff slot is cleared.

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Aens
build/bin/ability_audit -data 'data/Warcraft III' -raw ANen
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.ensnare*'
```

Focused tests cover alias procedure lookup, ground `Bens` move lock, flyer
land-and-lock, expiry restore of `AI_FLYING`/altitude, ground targets staying
non-flying, and recast refresh.
