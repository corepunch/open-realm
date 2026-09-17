# Volcano

## Contract

`ANvc` is TFT-only `CAbilityVolcano` (parent `AAsm`). ROC `AbilityData.slk`
has no row. It is a point-target channeled hero spell (`AB_SPELL | AB_CHANNEL`,
`SPELL_TARGET_POINT`). The Firelord must keep channeling; movement or stun
cancels remaining waves through the shared `spell_run_frame` /
`S_SpellChannelActive` path.

| Field | Stock TFT | AbilityMetaData | Runtime use |
| --- | ---: | --- | --- |
| `DataA` | 3 | Rock Ring Count | unresolved presentation |
| `DataB` | 8 | Wave Count | number of damage pulses |
| `DataC` | 5 | Wave Interval (seconds) | delay between pulses |
| `DataD` | 2 | Building Damage Factor | buildings take `DataE * DataD` |
| `DataE` | 100 | Full Damage Amount | per-wave unit damage |
| `DataF` | 0.5 | Half Damage Factor | unresolved inner-ring detail |
| `Dur` / `HeroDur` | 2 / 1 | — | **stun** lengths only |
| `Area` | 500 | — | blast radius |
| `UnitID` | `Volc` | Destructible ID | unresolved volcano doodad |
| `BuffID` | `BNvc,BNva` | — | presentation; stun uses `Bstu` |
| `targs` | ground,structure,notself,tree,debris | — | living ground/structure hits |

Ubertip duration “Lasts 35 seconds” is channel length from the wave schedule:
first wave at `t=0`, then every `DataC` seconds for `DataB` waves
(`8 * 5s` → last wave at 35s). Do **not** treat `Dur`/`HeroDur` as channel
lifetime.

## Data Flow

```text
AbilityData.slk (ANvc, TFT only)
  -> DataB/C/D/E, Area, Dur/HeroDur, targs, Cost, Rng
CAbilityVolcano + S_SpellChannelThinker
  -> volcano_think pulses while S_SpellChannelActive
T_Damage
  -> DAMAGE_TYPE_NORMAL path (hits spell-immune; not S_SpellDamage)
unit_addtimedstatus(Bstu)
  -> Dur for non-heroes, HeroDur for heroes (same as Storm Bolt)
```

## Known Pitfalls

A prior “finish Volcano coverage” test was reverted because it invented field
meanings (notably treating `DataF=40` as damage). Always map fields from
`AbilityMetaData` / the ubertip, not from guessed numeric coincidences.

`DataA` (rock rings), `DataF` (half-damage factor), `UnitID=Volc` destructible
spawn, and tree/debris destruction are presentation or missing-helper gaps.
They are intentionally unimplemented until a correct retail contract is wired.

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw ANvc
```

ROC prints `AbilityData: not found`; TFT prints the row above.

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.volcano*'
```

Focused tests use non-stock `DataB=2`, `DataC=1`, `DataE=40`, `DataD=3` so
they cannot pass on hardcoded retail constants. They cover first-wave damage
and building factor, `Bstu` Dur vs HeroDur, the second pulse via
`G_RunEntities`, caster-move cancel, and out-of-area immunity.
