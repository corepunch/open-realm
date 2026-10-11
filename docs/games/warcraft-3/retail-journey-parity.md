# WC3 long-journey retail parity

Long journeys replay the same probe map in the original 1.27.1.7085 game (Wine + Frida) and in
OpenRealm, then compare every sampled public word and every raw velocity commit. A journey is a
real campaign or melee map with its terrain, pathing, doodads and destructables, and dozens of
mixed subjects (groups of up to twelve, heroes, flyers, single Move/Patrol/Stop orders,
mid-route redirects) moving across the map for minutes. Short fixtures cannot reach these
interactions. See also [retail-pathfinding.md](retail-pathfinding.md) and the
[engine-side pathfinding notes](retail-pathfinding-engine.md).

## Contract

- `tools/frida/make_wc3_journey_map.py` writes one map that both runtimes load. Only
  `war3map.j` changes: `CreateAllUnits`, `InitCustomTriggers` and `RunInitializationTriggers`
  are replaced by a probe. `config` still runs, so player slots, controllers, forces and
  `InitCustomTeams` are the map's own. Coordinates must be exact eighths.
- The probe owns its own 0.1 s timer and exports rows only through `SaveInteger`/`SaveReal`
  plus `Preload("PATHMETA ...")` markers. The original side reads them with Frida hooks and the
  engine side reads them from its hashtable. No probe-specific engine code exists.
- Parity runs use `+set wc3_path_scheduler retail` and TFT data (`-tft`), because the original
  runs TFT. The default `responsive` scheduler deliberately admits fine searches differently.
- Probe kinds (spec `"probe"`):

| Probe | File | Rows |
|---|---|---|
| journey (default) | `tools/frida/wc3_journey_probe.j` | `tick, case, order, GetUnitFacing, GetUnitX, GetUnitY` every `sample_every` ticks |
| `unitdata` | `tools/frida/wc3_unitdata_probe.j` | default/current turn, prop window, move speed, acquire range, fly height per type |
| `alliances` | `tools/frida/wc3_alliance_probe.j` | `GetPlayerAlliance` 16x16x10, controller, slot state, allied-victory state, team; before and after `SetPlayerController(Player(0), USER)` |
| `grouporder` | `tools/frida/wc3_grouporder_probe.j` | ForGroup enumeration order after `GroupAddUnit` in creation order |

## Diagnostic Workflow

```sh
W=/run/media/lofcz/ssd_external/Games/w3            # original install, also the engine -data
R=/GitHub/wc3-analysis/reports/journey
python3 tools/frida/make_wc3_journey_spec.py --data $W --base 'war3.mpq:Maps\Campaign\Human05.w3m' \
    --name human05_crossing --seed 5 --output tools/frida/journeys/human05_crossing.json
python3 tools/frida/make_wc3_journey_map.py --data $W --spec tools/frida/journeys/human05_crossing.json \
    --output $W/Maps/Journey/human05_crossing.w3m
# Original (isolated Xvfb display, Frida server on 127.0.0.1:27046):
DISPLAY=:94 WINEPREFIX=... python tools/frida/trace_wc3_journey.py --data $W \
    --map 'Maps\Journey\human05_crossing.w3m' --x11-display :94 --commits --output $R/human05-retail.jsonl
# Engine:
WC3_JOURNEY_MAP=Maps/Journey/human05_crossing.w3m WC3_JOURNEY_OUT=$R/human05-engine.jsonl \
WC3_JOURNEY_COMMITS=$R/human05-engine-commits.txt build/bin/openwarcraft3-tests -data $W -tft \
    +set wc3_path_scheduler retail +dedicated 1 +test wc3_journey.replay
python3 tools/frida/compare_wc3_journey.py $R/human05-retail.jsonl $R/human05-engine.jsonl \
    --engine-commits $R/human05-engine-commits.txt
```

`trace_wc3_journey.py --call RVA[:ARGS[:out|group|eOFF]...]` also records a thiscall function's
ECX, stack words, return value, an `out` dword, a physical-group snapshot (`group`: flags80,
then member flags28, mover14 and moverD8 per member) or ECX-relative dwords at entry (`e8`).
Calls are interleaved with commits in execution order, so a group decision precedes its
members' commits. Examples used so far:

| Call | Meaning |
|---|---|
| `169b50::group` | `PathGroup_CanShareSpeedCap` result plus the member flags that decided it |
| `16b060:1:out` | selected shared speed cap |
| `168310:1:e4:e8:e10` | scheduler admission: bucket limit, work before admission, queue count |

The general tracer (`trace_wc3_pathfinding.py`) sends one message per hooked call and stalls
after roughly 95k messages. The journey tracer batches rows and stops at `PATHMETA complete`.

## Verified Original Rules

Each rule below was found by a journey divergence, then checked against the original with a probe.

- **Targs owner classes (`6f6ae260` -> `6f6ac5d0`).** Relations come from the source owner's
  directional diplomacy (GetPlayerAlliance reads `diplomacy+0x38+type*0x10`): ally (`allies`,
  0x4000) is PASSIVE with SHARED_SPELLS, neutral (0x8000) is PASSIVE without SHARED_SPELLS,
  enemy (0x10000) lacks PASSIVE. The source's own player qualifies only through `player`
  (0x2000, also part of `friend` 0x6000) or `self` (0x1000). `6f6a9ef0` admits every owner when
  no relation token is authored. The W3I slot type is never consulted. Engine:
  `S_TargsAllowsOwner`, `S_SpellIsNeutral` (`skills/s_spell.c`). Before this, an
  Abomination's Disease Cloud (`ground,enemy,organic,neutral`) infected its own units on maps
  whose W3I authors the owner slot as Neutral. The resulting combat alerts disabled Human05
  group speed sharing.
- **Diplomacy defaults.** `6f1e7c20` gives each of the 16 players alliance types 0..7 toward
  itself and mutual PASSIVE with Neutral Passive, and seats players 12..15 as playing
  `MAP_CONTROL_CREEP` slots. `6f29e300` joins playing slots 0..10 with later slots 0..11 of the
  same custom force: flag 1 grants PASSIVE..SHARED_SPELLS, 8 shared vision, 2 allied
  victory, 0x10/0x20 shared/advanced control. After config and Town AI creation, `6f1e9bf0`
  makes NEUTRAL/RESCUABLE-controlled players mutually PASSIVE with USER/COMPUTER-controlled
  players and with each other, themselves included. Engine: `G_InitPlayerAlliances`,
  `G_InitControllerAlliances` (`g_utils.c`), called from `G_SpawnEntities`. The alliance
  probe matrices match for Human05, Orc05 and Lost Temple.
- **Unit-group order.** `GroupAddUnit` prepends Heroes and appends other units, so ForGroup,
  FirstOfGroup and `GroupPointOrder` (`23acd0` snapshot) visit the newest Hero first. The
  physical Move member order inherits this order, and so do the per-player fine-search budget
  charges (`168310`). Engine: `group_add_entity` (`api/api_group.h`).
- **SLK reals** use the original decimal parser `070de0` (`wc3_decimal`), which is not
  correctly rounded: `"0.4"` gives `3eccccce`, and `"1.5"` gives `3fc00001`. Tests that author
  a captured float word as SLK text must search for text that this parser maps to the word
  (see `target168_positive_fades_match_original_listener_requests`).

## Known Pitfalls

- A faithful replay needs `G_FinishMovePathingInitialization()`, the probe's own timer and
  `level.pathing_clock` span 300. Orders issued at tick 0 without them produced a false
  "head-on livelock".
- `wc3_path_scheduler responsive` (the default) admits fine searches out of retail order. All
  journey subjects of one player share one 1100-work fine budget, so the order of charges
  decides later admissions even when every route is identical.
- RoC data changes authored speeds (mortar): use `-tft` against the TFT original.
- The engine's dump must keep the velocity zero sign (`wc3_velocity_scale`). `wc3_div`
  canonicalizes it.
- The unitdata probe still differs for acquire range (default = authored UnitWeapons
  `acquire`, else sight, no sight clamp; current is 0 while weapons are off), for twelve move
  speeds (for example Obla 320 in the original, 300 in the engine; owyv 320/350; current
  speed `43afffff` against the engine's 352), and for 24 TFT neutral types that the engine's
  CreateUnit rejects (`Nal2`, `Nfir`, `ncg1`, `nlv1`, ...).
- `-loadfile` on a melee map seats a Computer in slot 1 and leaves the other W3I slots empty.
  The engine loader seats every W3I Human slot, so melee GetPlayerSlotState/controller rows
  still differ. Retail GetPlayerTeam returns -1 for non-playing slots, but the engine returns
  the W3I force index.

## Status

| Journey | Subjects | Result |
|---|---|---|
| Lost Temple crossing | 24 | bit-exact over 2400 ticks (~80k commits) |
| Human05 crossing (seed 5) | 35 | every subject exact through 18.1 s; first divergence group 11 at 18.178 s |
| Orc05 (seed 6), NightElf02 (seed 7) | - | captured, not yet analysed |

Human05 at 18.178 s: subject 14 (Grunt) is stationary with retry countdown `path98`. The
original forces its arrival (member 0x20000, mover 0x10000) on the 18.148 visit, denies sharing
on the next visit and then advances the group's coarse point. The engine's countdown has not
expired then (retry 2 at 18.148 s). The owner PRNG seeds this countdown (`167290`), so the next
step is to compare its seed and draws.

## Verification

- `wc3_journey.replay` runs only with `WC3_JOURNEY_MAP`/`WC3_JOURNEY_OUT` set and local
  original data. It is a diagnostic, not a suite regression.
- Suite regressions extracted from journeys: `wc3_api.alliance_defaults_include_neutral_passive_and_neutral_controller_slots`,
  `wc3_api.group_add_prepends_heroes_like_original`,
  `wc3_spell.aura_targets_follow_source_team_alliances_and_neutral_mask`,
  `wc3_slk.authored_scalars_use_the_original_decimal_parser`.
