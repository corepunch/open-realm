# Self Destruct (Kaboom!)

## Contract

`Asds` is TFT `CAbilitySelfDestruct` (parent `AAat`). `Asdg`, `Asd2`, and `Asd3`
are AbilityData aliases whose `code` is `Asds`, so they share
`CAbilitySelfDestruct` and read their own rows through `abilityitem_t.code`.

ROC `AbilityData.slk` has no `Asds`/`Asdg` rows.

Clockwerk Goblins (`ncgb`/`ncg1`/`ncg2`) carry `Asdg`/`Asd2`/`Asd3` in
`UnitAbilities.abilList`. Factory units do **not** carry this ability. Death
detonation is owned by Self Destruct, not by Pocket Factory (`ANsy`) and not by
Clockwerk FOURCC special cases in `unit_die`.

`DataF` (`Explodes on Death`) gates the death path. Stock Clockwerk rows set
`DataF=1`. Stock Goblin Sapper `Asds` leaves `DataF` empty/0 and does **not**
explode when killed; its Kaboom cast path remains separate.

## Authoritative Fields

WorldEdit `AbilityMetaData` labels (`WESTRING_AEVAL_DDA*` / `SDS*`):

| Field | Meta | Stock Asdg | Stock Asd2 | Stock Asd3 | Stock Asds |
| --- | --- | ---: | ---: | ---: | ---: |
| `DataA` | Full Damage Radius | 100 | 100 | 100 | 100 |
| `DataB` | Full Damage Amount | 30 | 60 | 80 | 250 |
| `DataC` | Partial Damage Radius | 250 | 250 | 250 | 250 |
| `DataD` | Partial Damage Amount | 12 | 22 | 30 | 100 |
| `DataE` | Building Damage Factor | 1 | 1 | 1 | 3 |
| `DataF` | Explodes on Death | 1 | 1 | 1 | 0 |
| `Area` | (unused; radii are DataA/C) | 0 | 0 | 0 | 0 |
| `Dur` / `HeroDur` | cast window | 0.1 | 0.1 | 0.1 | 0.1 |
| `targs` | | ground,structure,debris,enemy,neutral | same | same | ground,structure,debris,tree,ward |

Ubertips reference `DataB` as the listed damage. Damage is physical (`T_Damage`):
armor applies; spell immunity does not block it. Buildings multiply by `DataE`.

## Data Flow

```text
UnitAbilities.abilList (Asdg / Asd2 / Asd3)
  -> unit carries Self Destruct
unit_die / BTLF expiry -> ent->die
  -> S_UnitDeathAbilities(self)
     walks abilList / abilities.added / heroabilities
     -> CAbilitySelfDestruct(A_DEATH)
        if DataF: full/partial AoE via DataA–E
```

Kaboom! is also a point-target autocast command-card spell (Hotkey B). Intentional
cast that kills the caster and deals the same blast is not implemented here;
death/`DataF` is the Clockwerk contract required by Pocket Factory.

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -tft -raw Asdg
build/bin/ability_audit -data 'data/Warcraft III' -tft -raw Asds
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.self_destruct*'
make test-wc3-engine WC3_PATTERN='wc3_spell.pocket_factory*'
```

Register `Asds`/`Asdg`/`Asd2`/`Asd3` on `CAbilitySelfDestruct` with `AB_PASSIVE`
(do not register abstract `AAat`). `Asds` is required so
`FindAbilityForCommand` can resolve `code=Asds` aliases.
