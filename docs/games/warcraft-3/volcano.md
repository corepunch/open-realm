# Volcano

## Contract

`ANvc` is TFT-only `CAbilityVolcano` (parent `AAsm`). ROC `AbilityData.slk`
has no row. It is a point-target channeled hero spell (`AB_SPELL | AB_CHANNEL`,
`SPELL_TARGET_POINT`). The Firelord must keep channeling; movement or stun
cancels remaining waves through the shared `spell_run_frame` /
`S_SpellChannelActive` path.

WorldEdit `AbilityMetaData` labels (`WESTRING_AEVAL_NVC*` / `NVCU`):

| Field | Stock TFT | AbilityMetaData | Runtime use |
| --- | ---: | --- | --- |
| `DataA` | 3 | Rock Ring Count | presentation only (molten-rock missiles per wave); no separate blocking-rock / impact-point contract without projectile helpers |
| `DataB` | 8 | Wave Count | number of damage pulses |
| `DataC` | 5 | Wave Interval (seconds) | delay between pulses |
| `DataD` | 2 | Building Damage Factor | buildings take wave damage × `DataD` |
| `DataE` | 100 | Full Damage Amount | per-wave unit damage inside `Area/2` |
| `DataF` | 0.5 | Half Damage Factor | past `Area/2` (to `Area`) take `DataE * DataF` |
| `Dur` / `HeroDur` | 2 / 1 | — | **stun** lengths only |
| `Area` | 500 | — | outer blast radius; full-damage radius is `Area/2` |
| `UnitID` | `Volc` | Destructible ID | volcano doodad at the channel point (`G_CreateDestructable`) |
| `BuffID` | `BNvc,BNva` | — | presentation; stun uses `Bstu` |
| `targs` | ground,structure,notself,tree,debris | — | living ground/structure + tree/debris destructibles |

Ubertip duration “Lasts 35 seconds” is channel length from the wave schedule:
first wave at `t=0`, then every `DataC` seconds for `DataB` waves
(`8 * 5s` → last wave at 35s). Do **not** treat `Dur`/`HeroDur` as channel
lifetime.

Ubertip only names `DataE` for damage; it does not mention half damage. The
`Full Damage Amount` / `Half Damage Factor` AbilityMetaData pair still requires
a radial split. No separate full-damage radius field exists on `ANvc`, so the
inner half of `Area` takes full `DataE` and the outer ring takes `DataE * DataF`
(same inner-full / outer-partial shape as Self Destruct when only one Area is
authored). Buildings still multiply by `DataD` after that split.

`DataA` Rock Ring Count is the authored molten-rock missile count used by the
retail presentation. This engine applies each wave as a uniform Area pulse; do
not invent blocking rock rings or per-missile targeting until a shared rock
projectile helper exists.

## Data Flow

```text
AbilityData.slk (ANvc, TFT only)
  -> DataB/C/D/E/F, Area, Dur/HeroDur, UnitID, targs, Cost, Rng
CAbilityVolcano + S_SpellChannelThinker
  -> G_CreateDestructable(UnitID) at point (Volc doodad)
  -> volcano_think pulses while S_SpellChannelActive
T_Damage / G_DestructableApplyDamage
  -> units: DAMAGE_TYPE_NORMAL path (hits spell-immune; not S_SpellDamage)
  -> trees/debris in Area take the same wave damage
unit_addtimedstatus(Bstu)
  -> Dur for non-heroes, HeroDur for heroes (same as Storm Bolt)
```

## Known Pitfalls

A prior “finish Volcano coverage” test was reverted because it invented field
meanings (notably treating `DataF=40` as damage). Always map fields from
`AbilityMetaData` / the ubertip, not from guessed numeric coincidences.

Do not treat `DataF` as a flat damage number. Do not treat `Dur` as channel
lifetime.

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw ANvc
build/bin/ability_audit -data 'data/Warcraft III' -tft -raw ANvc
```

ROC prints `AbilityData: not found`; TFT prints the row above. AbilityMetaData
strings: `WESTRING_AEVAL_NVC1`…`NVC6`, `NVCU`.

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.volcano*'
```

Focused tests use non-stock `DataB=2`, `DataC=1`, `DataE=40`, `DataD=3`,
`DataF=0.25`, `UnitID=Vtst` so they cannot pass on hardcoded retail constants.
They cover first-wave damage and building factor, outer-ring half damage,
`Bstu` Dur vs HeroDur, tree/debris destruction, Volc doodad spawn, the second
pulse via `G_RunEntities`, caster-move cancel, and out-of-area immunity.
