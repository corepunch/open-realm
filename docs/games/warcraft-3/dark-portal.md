# Dark Portal

## Contract

`ANdp` is ROC/TFT `CAbilityDarkPortal` (parent `AAsm`). It is Archimonde's
campaign point-target spell. TFT class extraction and `ability_audit` both name
`CAbilityDarkPortal`; do **not** reuse `CAbilityMassTeleport` (stale
`tools/ability_map.c` / `s_skills.c` TODO). `AUds` Dark Summoning is a separate
rawcode and does not share `code=ANdp`.

| Field | Meta | Meaning |
| --- | --- | --- |
| `DataA` | `Ndp1` Spawned Units (`unitList`) | Unit type(s) to spawn — read with `S_SpellDataId` |
| `DataB` | `Ndp2` Minimum Number of Units | Inclusive lower spawn count |
| `DataC` | `Ndp3` Maximum Number of Units | Inclusive upper spawn count |
| `Dur` | — | Seconds between sequential portal exits (Rain of Chaos-style) |
| `Rng` / `Cool` / `Cost` | — | Cast range, cooldown, mana (stock cost `0`) |

Stock L1–L3 (ROC and TFT numeric rows match; TFT authors DataA):

| Level | DataA | DataB | DataC | Dur | Rng | Cool |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | `nbal` (TFT) / empty (ROC) | 3 | 5 | 1 | 800 | 100 |
| 2 | `nbal` / empty | 3 | 6 | 1 | 800 | 100 |
| 3 | `nbal` / empty | 5 | 6 | 1 | 800 | 100 |

`targs` is empty/`_`: point order (`SPELL_TARGET_POINT`), not unit-target.
`UnitID` / `BuffID` / `Area` are empty. Ubertip: opens a portal so demons step
through. No aliases of `code=ANdp`.

Each cast picks a random inclusive count in `[DataB, DataC]`, then spawns that
many `DataA` units at the cast point. First unit exits immediately; further
exits are owned by a thinker spaced by `Dur`. The caster is **not** channeled
and may walk away. Spawned demons are permanent campaign troops: no `BTLF`, and
no `edict->owner` summon mark (retail Purge/Dispel do not treat them as
summoned).

`Ndp1` is typed `unitList`; stock TFT is a single `nbal` (Doom Guard). Campaign
maps may override the list (wiki also names Fel Stalkers). Runtime currently
reads the first fourcc via `S_SpellDataId`.

## Data Flow

```text
AbilityData.slk (ANdp)
  -> DataA unitList, DataB min, DataC max, Dur interval, Rng
CAbilityDarkPortal
  -> spawn thinker at cast point; first exit immediate
dark_portal_think
  -> SP_SpawnAtLocation(DataA) + food/stand + summon events (no owner/BTLF)
  -> schedule next exit after Dur; free when count exhausted
```

Missing DataA logs and skips. Zero `Dur` cannot schedule further exits: the
thinker logs and frees after the first spawn (same guard as Rain of Chaos).

## Known Pitfalls

- `tools/ability_map.c` and the old TODO map `ANdp` → `CAbilityMassTeleport`.
  That is wrong; Mass Teleport is `AHmt`.
- Do not alias `AUds` or treat Dark Portal as a teleport.
- ROC omits DataA (`0`); TFT authors `nbal`. Tests must fixture DataA.
- Do not apply `BTLF` or set `owner` — that would make Purge/Dispel deal
  summoned damage retail does not.

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw ANdp
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.dark_portal*'
```

Focused tests cover procedure/flags, non-stock DataA/DataB/DataC/Dur, immediate
first spawn, Dur-spaced later spawns via `G_RunEntities`, continuation after the
caster moves, and no `BTLF`/`owner` on spawned units.

## Registry

```c
{ "ANdp", CAbilityDarkPortal, AB_SPELL, SPELL_TARGET_POINT },  /* Dark Portal */
```

## Remaining Gaps

- Multi-entry `unitList` beyond the first DataA fourcc.
- Portal effect presentation / uber splat.
- Optional scatter when Area is authored (stock Area is 0).
