# Soul Burn

## Contract

`ANso` is TFT-only `CAbilitySoulBurn` (parent `AAsm`). ROC `AbilityData.slk`
has no row. There are no `code=` aliases. Firelord hero spell, unit-target
enemy organic.

| Field | Stock L1 | Runtime use |
| --- | ---: | --- |
| `DataA` | 7.81 | damage per second while `BNso` is active |
| `DataB` | 1 | unused (not consumed) |
| `DataC` | 0.5 | outgoing attack-damage reduction fraction |
| `Dur` / `HeroDur` | 16 / 7 | buff lifetime |
| `BuffID` | `BNso` | Soul Burn status |
| `targs` | air,ground,enemy,neutral,organic | living enemy organic |
| `cost` / `cool` / `rng` | 85 / 12 / 700 | cast gate |

Ubertip and `BNso` Buffubertip both require three effects while the buff lasts:
damage over time, **cannot cast spells**, and reduced attack damage.

Silence ability `ANsi` uses a different buff `BNsi`. Both buffs block the cast
pipeline with the same "Silenced." error. Prefer `S_UnitIsSilenced` over a
second FOURCC check next to the existing `BNsi` gate.

## Data Flow

```text
AbilityData.slk (ANso, TFT only)
  -> DataA/DataC, Dur/HeroDur, BuffID, targs, Cost, Rng
CAbilitySoulBurn
  -> unit_addtimedstatus(BNso)
S_SoulBurnDamageRate
  -> g_phys.c drains DataA HP/sec
S_SoulBurnDamageReduction
  -> s_attack.c scales outgoing attack damage by DataC
S_UnitIsSilenced (BNsi or BNso)
  -> s_spell.c spell_validate / spell_validate_point
```

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw ANso
build/bin/ability_audit -data 'data/Warcraft III' -raw ANsi
```

ROC prints `AbilityData: not found` for both; TFT prints the rows above.

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.soul_burn*'
```

Focused tests use non-stock `DataA`/`DataC`/`Dur` so they cannot pass on
hardcoded retail constants. They cover procedure lookup, drain/reduction
consumers, `BNso` blocking unit-target and no-target casts without mana spend,
`BNsi` still silencing, expiry restore, and recast.
