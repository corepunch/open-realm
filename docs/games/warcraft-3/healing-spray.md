# Healing Spray

## Contract

`ANhs` is TFT-only `CAbilityHealingSpray` (parent `ANcs` Cluster Rockets).
ROC `AbilityData.slk` has no row. It is a point-target channeled hero spell
(`AB_SPELL | AB_CHANNEL`, `SPELL_TARGET_POINT`). Waves heal friendlies in
`Area` while the Alchemist keeps channeling; movement or stun cancels remaining
waves through `S_SpellChannelActive`.

Ubertip: sprays `DataF` waves; each wave heals `DataA` to all friendly units in
an area. WorldEdit / Hive OE labels (`gainedHP; frequency; missiles;
maxGainedHP; buildDmgReduction; waves`) map DataA–F.

| Field | Stock L1/L2/L3 | AbilityMetaData / OE | Runtime use |
| --- | ---: | --- | --- |
| `DataA` | 40 / 55 / 70 | gainedHP | heal per unit per wave |
| `DataB` | 1 / 1 / 1 | frequency (seconds) | delay between waves |
| `DataC` | 6 / 6 / 6 | missiles | presentation; unresolved |
| `DataD` | 280 / 385 / 490 | maxGainedHP | per-wave shared heal budget |
| `DataE` | 1 / 1 / 1 | buildDmgReduction | unused (targs exclude structure) |
| `DataF` | 3 / 4 / 5 | waves | number of heal pulses |
| `Area` | 250 | — | blast radius |
| `BuffID` | `BNhs` | — | presentation; heal does not require it |
| `targs` | friend,self,ground,air,organic | — | living friendlies in area |
| `Dur` / `HeroDur` | 0 | — | unused; channel length is the wave schedule |

First wave at cast (`t=0`), then every `DataB` seconds for `DataF` waves. Stock
L1 ends at 2s (3 waves × 1s interval). Do **not** treat `Dur` as channel life.

When `DataA * hit_count > DataD`, per-target heal scales down so the wave total
equals `DataD` (same shape as Blizzard / Cluster Rockets max damage per wave).

`DataC` missiles and `DataE` build factor are intentionally unimplemented until
a correct presentation or negative-heal path is wired.

## Data Flow

```text
AbilityData.slk (ANhs, TFT only)
  -> DataA/B/D/F, Area, targs, Cost, Rng
CAbilityHealingSpray + S_SpellChannelThinker
  -> healing_spray_think pulses while S_SpellChannelActive
S_SpellHeal
  -> friend/self organic units in Area (via S_SpellAllowsTarget)
```

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw ANhs
```

ROC prints `AbilityData: not found`; TFT prints the row above.

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.healing_spray*'
```

Focused tests use non-stock `DataA`/`DataB`/`DataD`/`DataF` so they cannot pass
on hardcoded retail constants. They cover first-wave heal, max-heal scaling,
second pulse via `G_RunEntities`, caster-move cancel, and enemy/mechanical miss.
