# Self Destruct (Kaboom!)

## Contract

`Asds` is `CAbilitySelfDestruct` (parent `AAat`). `Asdg`, `Asd2`, and `Asd3`
are AbilityData aliases whose `code` is `Asds`, so they share
`CAbilitySelfDestruct` and read their own rows through `abilityitem_t.code`.

ROC has `Asds` (DataB=700, no DataE/DataF). ROC has no `Asdg`/`Asd2`/`Asd3`.
TFT overlays `Asds` (DataB=250, DataE=3, DataF=0) and adds the Clockwerk aliases.

Clockwerk Goblins (`ncgb`/`ncg1`/`ncg2`) carry `Asdg`/`Asd2`/`Asd3` in
`UnitAbilities.abilList`. Factory units do **not** carry this ability. Death
detonation is owned by Self Destruct, not by Pocket Factory (`ANsy`) and not by
Clockwerk FOURCC special cases in `unit_die`.

`DataF` (`Explodes on Death`) gates the death path. Stock Clockwerk rows set
`DataF=1`. Stock Goblin Sapper `Asds` leaves `DataF` empty/0 and does **not**
explode when killed.

Kaboom! is a point-target autocast command-card spell (Hotkey B, Untip
right-click autocast). Intentional cast always deals DataA–E blast and kills
the caster, independent of `DataF`. Stock `Asds` is registered
`AB_SPELL | AB_AUTOCAST` with `SPELL_TARGET_POINT`. Clockwerk aliases stay
`AB_PASSIVE` death-only (they share Untip/Hotkey strings but this leftover
keeps click on the Sapper row).

## Authoritative Fields

WorldEdit `AbilityMetaData` labels (`WESTRING_AEVAL_DDA*` / `SDS*`):

| Field | Meta | Stock Asdg | Stock Asd2 | Stock Asd3 | Stock Asds (TFT) |
| --- | --- | ---: | ---: | ---: | ---: |
| `DataA` | Full Damage Radius | 100 | 100 | 100 | 100 |
| `DataB` | Full Damage Amount | 30 | 60 | 80 | 250 |
| `DataC` | Partial Damage Radius | 250 | 250 | 250 | 250 |
| `DataD` | Partial Damage Amount | 12 | 22 | 30 | 100 |
| `DataE` | Building Damage Factor | 1 | 1 | 1 | 3 |
| `DataF` | Explodes on Death | 1 | 1 | 1 | 0 |
| `Area` | (unused; radii are DataA/C) | 0 | 0 | 0 | 0 |
| `Rng` | cast range (point) | 0 | 0 | 0 | 0 |
| `Dur` / `HeroDur` | cast window | 0.1 | 0.1 | 0.1 | 0.1 |
| `targs` | | ground,structure,debris,enemy,neutral | same | same | ground,structure,debris,tree,ward |

Ubertips reference `DataB` as the listed damage. Damage is physical (`T_Damage`):
armor applies; spell immunity does not block it. Buildings multiply by `DataE`.

## Data Flow

```text
UnitAbilities.abilList (Asdg / Asd2 / Asd3 / Asds)
  -> unit carries Self Destruct
Click / autocast (Asds only):
  S_CastPointTargetSpell(Asds, point)
    -> CAbilitySelfDestruct(A_EXECUTE)
       self_destruct_explode(DataA–E)
       unit_die(caster)   /* DataF=0 => no second blast */
Death (DataF):
  unit_die / BTLF expiry -> ent->die
    -> S_UnitDeathAbilities(self)
       -> CAbilitySelfDestruct(A_DEATH)
          if DataF: full/partial AoE via DataA–E
```

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Asds
build/bin/ability_audit -data 'data/Warcraft III' -raw Asdg
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.self_destruct*'
make test-wc3-engine WC3_PATTERN='wc3_spell.pocket_factory*'
```

Register `Asds` on `CAbilitySelfDestruct` with `AB_SPELL | AB_AUTOCAST` and
`SPELL_TARGET_POINT`. Register `Asdg`/`Asd2`/`Asd3` with `AB_PASSIVE`. Do not
register abstract `AAat`.
