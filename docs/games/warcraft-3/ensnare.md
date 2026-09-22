# Ensnare

## Contract

`Aens` is TFT `CAbilityEnsnare` (parent `AAsm`). `ANen` (Naga, TFT-only) and
`ACen` (creep) are `AbilityData` aliases whose `code` is `Aens`, so they share
`CAbilityEnsnare` and read their own rows through `abilityitem_t.code`.
`ACen` is registered in `s_skills.c`.

Web (`Aweb` / `ACwb`, `Bwea` / `Bweb`) delegates to the same bind/height
lifecycle with air-only validation and autocast. Removing one bind leaves flight
grounded until the last Web or Ensnare slot is removed. Landing/restoration also
updates `targtype` between ground and air, not only `AI_FLYING`.
See [creep combat effects](creep-combat-effects.md).

| Rawcode | Archive | Notes |
| --- | --- | --- |
| `Aens` | ROC and TFT | Raider Ensnare |
| `ANen` | TFT | Naga Ensnare (`code=Aens`) |
| `ACen` | ROC and TFT | creep Ensnare (`code=Aens`) |

Casting binds one living enemy/neutral unit so it cannot move for `Dur` /
`HeroDur`. Air units lose `AI_FLYING` immediately so ground attacks can hit them,
then descend to the support surface over `DataA`.

## ROC vs TFT

| Field | ROC `Aens` | TFT `Aens` |
| --- | --- | --- |
| `targs` | `ground,air,enemy,neutral` | same |
| `cool` / `rng` | 20 / 400 | 16 / 500 |
| `Dur` / `HeroDur` | 20 / 5 | 12 / 3 |
| `DataA` / `DataB` / `DataC` | 0.6 / 200 / 128 | same |
| `BuffID` | empty | `Bena,Beng` |

WorldEdit `AbilityMetaData` labels (`WESTRING_AEVAL_ENS*`):

| Field | Stock | Meta name | Runtime |
| --- | ---: | --- | --- |
| `DataA` | 0.6 | Air Unit Lower Duration | seconds to land (and to rise on expiry); `<=0` snaps |
| `DataB` | 200 | Air Unit Height | land start height when `DataA>0` |
| `DataC` | 128 | Melee Attack Range | ensnared unit's attack range while bound |

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
              store spell->code in status.data; begin land (DataA/B)
  -> A_UPDATE: advance land / rise FlyHeight; M_CheckGround
m_unit status machinery (storage, iteration, timing only)
  -> unit_expirestatus: A_STATUS_REMOVE to the owner resolved from
     status.data (origin rawcode); slot wiped after the owner runs
  -> unit_refreshstatusflags: stun aggregation plus A_STATUS_REFRESH to the
     owner of each remaining status
CAbilityEnsnare (owns AI_FLYING, phases, restoration)
  -> A_STATUS_REFRESH: ground while a bind remains, else restore flight
  -> A_STATUS_REMOVE: start rise when DataA>0, restore flight only when no
     other bind remains; runs before the slot wipe so authored data is valid
order_move
  -> S_UnitIsEnsnared / BEer reject movement
s_attack range
  -> S_EnsnareMeleeRange returns DataC while ensnared
```

Land uses buff age against `DataA` with start height `DataB` (else authored
`moveHeight`). Rise after expiry stores `DataA` on the edict and lerps from 0 to
authored `moveHeight`. Zero/missing `DataA` keeps the prior instant land-and-
restore path so fixtures without those cells stay deterministic.

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
land-and-lock, gradual `DataA`/`DataB` descent, expiry restore of
`AI_FLYING`/altitude, ground targets staying non-flying, recast refresh,
dispel mid-land, overlapping binds, attack/move restoration, unrelated flight
state, and save/load during land and rise.
