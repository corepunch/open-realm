# Pocket Factory

## Contract

`ANsy` is TFT-only `CAbilitySummonFactory` (parent `AAsm`). OpenWarcraft3 names the
procedure `CAbilityPocketFactory`. `ANs1`, `ANs2`, and `ANs3` are `AbilityData` aliases
whose `code` is `ANsy`, so they share that procedure and read their own rows through
`abilityitem_t.code`.

ROC `AbilityData.slk` has no `ANsy` row (`AbilityData: not found`).

Point cast summons a timed factory at the target point. The factory periodically spawns
Clockwerk goblins. Production is owned by `ANsy`, not by an ability on the factory unit.

## Authoritative Fields

| Field | Meta | Stock ANsy L1 | Runtime meaning |
| --- | --- | ---: | --- |
| `DataA` | Nsy1 Spawn Interval | `5` | seconds between Clockwerk spawns |
| `DataB` | Nsy2 Spawn Unit ID | `ncgb` | Clockwerk unit rawcode (`S_SpellDataId`) |
| `DataC` | Nsy3 Spawn Unit Duration | `12` | Clockwerk timed life (seconds, `BTLF`) |
| `DataD` | Nsy4 Spawn Unit Offset | `200` | spawn distance from factory origin |
| `DataE` | Nsy5 Leash Range | `1100` | max distance Clockwerks may leave the factory; beyond it they return |
| `UnitID` | Nsyu Factory Unit ID | `nfac` | factory unit rawcode (`S_SpellUnitId`) |
| `Dur` / `HeroDur` | | `40` | factory timed life (`BTLF`) |

Rank tables:

| Rank | UnitID | DataB |
| ---: | --- | --- |
| L1 | `nfac` | `ncgb` |
| L2 | `nfa1` | `ncg1` |
| L3 | `nfa2` | `ncg2` |

`ANs1` / `ANs2` / `ANs3` keep the same UnitID/DataB tables and only change `DataA`
(`4` / `3.2` / `2.56`).

## DataB is a unitCode

`DataB` is a unitCode cell (`"ncgb"`). `ability_audit -raw` prints it as fourcc.
Always use `S_SpellDataId(code, level, 2)` for DataB — never `S_SpellData`. The
factory thinker is `pocket_factory_think` and is on the save C-function roster.

## ANfy is not the factory producer

Factory units `nfac` / `nfa1` / `nfa2` only have Rally (`ARal`) in `UnitAbilities`.
`ANfy` (`CAbilityFactory`, `UnitID=ncgb`) exists in AbilityData but is **not** on the
factory unit. Do not implement Pocket Factory production as `ANfy`.

## Runtime Ownership

```text
AbilityData.slk (ANsy / ANs1 / ANs2 / ANs3)
  -> UnitID, Dur, DataA–E
CAbilityPocketFactory
  -> S_SummonAt(caster, UnitID, point, Dur)   // factory.owner = caster
  -> classless thinker owned by factory (velocity = DataE leash)
G_RunEntities / ent->think
  -> leash: factory-owned DataB units beyond DataE order_move home
  -> S_SummonAt(factory, DataB, offset, DataC) // clockwerk.owner = factory
G_FreeEdict(factory)
  -> next think sees !factory->inuse and frees itself
```

Owner chain: factory is owned by the caster; each Clockwerk is owned by the factory
(player id still matches the caster). The thinker's `spawn_time` also equals factory
`Dur`, so factory BTLF expiry and thinker teardown share the same bound.

## DataE leash

`AbilityMetaData` labels DataE as Nsy5 Leash Range. Stock is `1100` on `ANsy` and
all `ANs*` aliases. Ubertip does not mention the leash; the meta name is the contract.

Each think tick, before the spawn-interval gate, the factory thinker pulls every
living factory-owned unit whose `class_id` matches DataB back toward the factory
when `distance(goblin, factory) > DataE`. `DataE <= 0` disables the leash. The
pull uses `order_move` to a factory waypoint and skips re-issue when the goblin
already has that home as its goal.

## Remaining

- Clockwerk death explosion is owned by Self Destruct (`Asdg` / `Asd2` / `Asd3`);
  see [self-destruct.md](self-destruct.md).

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -tft -raw ANsy
build/bin/ability_audit -data 'data/Warcraft III' -roc -raw ANsy
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.pocket_factory*'
```

Focused tests drive `S_CastPointTargetSpell` with a non-stock fixture (`DataA=2`,
`Dur=25`, `DataC=8`, `DataE=200`, `UnitID=hfoo`, `DataB=ogru`), advance with
`level.time` + `G_RunEntities` (never `globals.RunFrame`), cover alias `ANs1` DataA,
DataE return-to-factory leash, factory removal, and thinker-alloc rollback.
