# Inferno

## Contract

`ANin` is ROC/TFT `CAbilityInferno` (parent `AAsm`). It is a point-target
simple spell (`AB_SPELL`, `SPELL_TARGET_POINT`) with **no** `AB_CHANNEL`. After
cast, a delayed impact thinker owns the blast; the caster may walk away.

TFT `AUin` (`CAbilityDreadLordInferno`, parent `ANin`), `SNin`, and item `AIin`
are separate aliases and are not registered here. Do **not** treat the stale
`tools/ability_map.c` `CAbilityCreepThunderBolt` label as permission to reuse
`CAbilityThunderBolt` (unit-target missile). Inferno is a point blast + summon.

| Field | Meta | Meaning |
| --- | --- | --- |
| `DataA` | `Nin1` / `Uin1` Damage | Impact damage |
| `DataB` | `Nin2` / `Uin2` Duration | Infernal timed life (not `Dur`) |
| `DataC` | `Nin3` / `Uin3` Impact Delay | Seconds before blast + summon |
| `Dur` / `HeroDur` | — | Stun lengths (`Bstu`), not summon life |
| `Area` | — | Blast radius |
| `UnitID` | — | Summoned unit (`ninf` on TFT; ROC row empty) |
| `BuffID` | `BNin` (TFT) | Infernal presentation; stun uses `Bstu` |
| `targs` | ground,structure,debris,enemy,neutral | Living ground/structure enemies |

Stock `ANin` L1: `DataA=50`, `DataB=360`, `DataC=1`, `Dur=4`, `HeroDur=2`,
`Area=250`, `Cost=175`, `Cool=180`, `Rng=900`. TFT authors `UnitID=ninf` and
`BuffID=BNin`; ROC omits both.

AUin ubertip confirms the split: damage `DataA`, stun `Dur`, Infernal lasts
`DataB`. Liquipedia matches Impact Delay = `DataC`.

## Data Flow

```text
AbilityData.slk (ANin)
  -> DataA/B/C, Area, Dur/HeroDur, UnitID, targs
CAbilityInferno
  -> S_InfernoLand (immediate when DataC<=0, else inferno_think)
inferno_think / impact
  -> S_SpellDamage(DataA) + Bstu(Dur/HeroDur) in Area
  -> S_SummonAt(UnitID, DataB) + BTLF
CAbilityRainOfChaos
  -> each scatter landing calls S_InfernoLand on DataA Inferno row
```

Zero `DataC` impacts immediately (no thinker). Missing `UnitID` logs and skips
the summon; damage/stun still apply. Tree destruction remains unimplemented.

## Known Pitfalls

- `tools/ability_map.c` wrongly maps `ANin` → `CAbilityCreepThunderBolt`. TFT
  class registry and `ability_audit` both say `CAbilityInferno`.
- `Dur`/`HeroDur` are stun only. Summon life is `DataB`. Rain of Chaos must not
  call `S_SpellDuration` for Infernal timed life.
- `BuffID=BNin` is not the stun buff. Stun is shared `Bstu` (Storm Bolt / Volcano).
- AoE must accept `TARG_STRUCTURE` explicitly; `S_SpellAllowsTarget` rejects
  structures when `targs` also lists `ground` (same issue Volcano avoids).

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw ANin
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.inferno*'
make test-wc3-engine WC3_PATTERN='wc3_spell.rain_of_chaos*'
```

Focused tests cover procedure/flags, non-stock DataA/DataC/Dur, Area damage+stun
after delay, out-of-area immunity, UnitID summon with DataB life, HeroDur vs Dur,
and Rain of Chaos landings still summoning through the Inferno row.

## Registry (parent)

Parent must replace the `ANin` TODO in `s_skills.c` with:

```c
{ "ANin", CAbilityInferno, AB_SPELL, SPELL_TARGET_POINT },  /* Inferno */
```

## Remaining Gaps

- Tree/debris destruction on impact.
- Effect presentation / meteor art.
- `AUin` / `SNin` / `AIin` should eventually share this landing path.
