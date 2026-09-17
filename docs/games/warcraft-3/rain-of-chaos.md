# Rain of Chaos

## Contract

`ANrc` is TFT/ROC `CAbilityRainOfChaos` (parent `AAsm`). TFT `ANr3` is an
`AbilityData` alias whose `code` is `ANrc` (button layout variant), so both
share `CAbilityRainOfChaos` and read their own rows through
`abilityitem_t.code`.

Rain of Chaos is **not** Rain of Fire: it is a point-target simple spell with
**no** `AB_CHANNEL`. After cast, `caster->channel.code` stays `0` and the caster
may walk away; a dedicated thinker owns the remaining landings.

| Field | Meta | Meaning |
| --- | --- | --- |
| `DataA` | `Nrc1` | Inferno ability rawcode (`abilCode`), stock `ANin` — use `S_SpellDataId` |
| `DataB` | `Nrc2` | Number of units created |
| `Dur` | — | Delay between landings (seconds) |
| `Area` | — | Scatter radius around the cast point |
| `Rng` / `Cool` / `Cost` | — | Cast range, cooldown, mana (stock cost `0`) |

Stock TFT/ROC `ANrc` L1: `DataA=ANin`, `DataB=2`, `Dur=1`, `Area=900`,
`Rng=1000`, `Cool=120`. L2/L3 raise `DataB` and shorten `Dur`. `ANr3` authors
`DataB=2` as the button variant.

Each landing summons through the Inferno row linked by DataA:
`S_SummonAt(owner, S_SpellUnitId(inferno, level), scatter, S_SpellDuration(inferno, …))`
with `BTLF` timed life. Inferno landing damage (`ANin` DataA), impact delay
(`DataC`), and stun remain on the unfinished Inferno ability — not implemented
here.

## Data Flow

```text
AbilityData.slk (ANrc / ANr3)
  -> DataA (Inferno abilCode), DataB (count), Dur (interval), Area
CAbilityRainOfChaos
  -> spawn thinker at cast point; first landing immediate
rain_of_chaos_think
  -> S_SpellDataId/UnitId/Duration on Inferno -> S_SummonAt + BTLF
  -> schedule next landing after Dur; free when count exhausted
```

Zero `Dur` cannot schedule further landings: the thinker logs and frees
(Flame Strike-style), rather than spinning every frame.

## Known Pitfalls

- `ability_audit -raw` prints DataA as `ANin` (abilCode). Always use
  `S_SpellDataId` for DataA; `S_SpellData` returns 0 for that cell.
- Do not add `AB_CHANNEL` or clear the thinker when the caster moves. That is
  Rain of Fire behavior, not Rain of Chaos.
- Scatter uses `rand()` inside `Area`. Tests assert count, ownership, `class_id`,
  and `BTLF`, not exact coordinates.

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw ANrc
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.rain_of_chaos*'
```

Focused tests cover procedure/flags for `ANrc`/`ANr3`, authored non-stock
count/interval/UnitID, immediate first landing, Dur-spaced later landings via
`G_RunEntities`, scatter within Area, and continuation after the caster moves.

## Remaining Gaps

- Full Inferno (`ANin`): landing damage, stun buff, impact delay, and presentation.
- Effect edict `XErc` / `CEffectRainOfChaos` presentation.
