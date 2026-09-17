# Transmute

## Contract

`ANtm` is TFT-only `CAbilityTransmute` (parent `AAsm`). ROC `AbilityData.slk`
has no row. Instant unit-target hero ultimate (`AB_SPELL`, `SPELL_TARGET_UNIT`).
It kills the target and credits gold equal to `goldCost * DataA` (stock 0.8).

Ubertip: cannot be used on Heroes, or creeps above level `DataC`. WorldEdit /
Hive OE labels (`goldCostFactor; lumberCostFactor; maxCreepLv; allowBounty`)
map DataA–D.

| Field | Stock TFT | AbilityMetaData / OE | Runtime use |
| --- | ---: | --- | --- |
| `DataA` | 0.8 | goldCostFactor | `floor(UnitBalance.goldCost * DataA)` gold |
| `DataB` | 0 | lumberCostFactor | lumber credit when non-zero |
| `DataC` | 5 | maxCreepLv | reject when `UnitBalance.level > DataC` |
| `DataD` | 1 | allowBounty | documented; normal death bounty path unresolved |
| `BuffID` | `BNtm` | — | presentation; kill is instant |
| `targs` | air,ground,enemy,neutral,nonhero | — | living enemy/neutral non-heroes |
| `Cost` / `Cool` / `Rng` | 150 / 45 / 650 | — | mana, cooldown, range |

Gold income goes through `G_CreditResourceIncome` so Low/High Upkeep applies
(same as Liquipedia). Critters with `goldCost=0` yield no gold.

`nonhero` is not enforced by `S_SpellAllowsTarget`; `A_VALIDATE` rejects heroes
explicitly (same pattern as Possession / Charm). Allies fail `targs` enemy/
neutral. Over-level creeps fail `DataC`.

Summoned / Resistant Skin counters from wiki are not in AbilityData targs and
are left unresolved unless a later brief proves them.

## Data Flow

```text
AbilityData.slk (ANtm, TFT only)
  -> DataA/B/C, targs, Cost, Cool, Rng
CAbilityTransmute A_VALIDATE
  -> alive, non-hero, enemy|neutral, level <= DataC
CAbilityTransmute A_EXECUTE
  -> G_CreditResourceIncome(goldCost * DataA [, lumber])
  -> G_SetHealth(0) + die
```

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw ANtm
```

ROC prints `AbilityData: not found`; TFT prints the row above.

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.transmute*'
```

Focused tests use non-stock `DataA=0.5` / `DataC=3` / `goldCost=200` so they
cannot pass on hardcoded retail constants. They cover gold credit, kill, hero /
over-level / ally rejects without mana spend, and lumber when DataB is set.
