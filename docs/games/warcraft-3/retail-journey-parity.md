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
| `pathing` | `tools/frida/wc3_pathing_probe.j` | `IsTerrainPathable` bits (1 walk, 2 fly, 4 build, 8 amphibious) per cell of spec `box` at `step`, one row per tick |

## Diagnostic Workflow

```sh
W=/run/media/lofcz/ssd_external/Games/w3            # original install, also the engine -data
R=/GitHub/wc3-analysis/reports/journey
python3 tools/frida/make_wc3_journey_spec.py --data $W --base 'war3.mpq:Maps\Campaign\Human05.w3m' \
    --name human05_crossing --seed 5 --output tools/frida/journeys/human05_crossing.json
# Head-on stress: --headon PAIRS pairs Hero-led groups at distant open spots and swaps each
# pair three times; Hero followers (target Move) and lone crossers are added. --choke puts
# each pair across a narrow passage found from war3map.wpm clearance (2-4 cells).
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
- **Hero proper names (`6f28a460`).** Creating a unit whose rawcode starts with an uppercase
  letter consumes one path-owner draw when UnitData `nameCount` is at least 2 (Udea 15, Obla
  14). The count, not the Propernames list length, sizes the slot. The draw times the count
  (multiply-high) picks a slot, and a slot used more than the per-game minimum yields to the
  next least-used slot. The table belongs to the game (`1e0d70`), not the player.
  `Unit_RebindTypeRetainingPublicIdentity` (`671470`) also picks; the engine does not yet
  re-pick on rebinding. Engine: `G_PickHeroProperName`, `G_HeroProperName`,
  `GetHeroProperName`; the per-type counters are saved (save 175). Missing these draws
  shifted every later retry seed (`167290`, `7 + bit22`) by six draws.
- **Coarse waypoint before fine progress.** `Path_Advance` (`165ae0`) calls `165b60`, which
  runs `Path_CheckAcceleratedWaypoint` (`165f10`) on every visit. That happens before
  `Path_GetFineWaypoint` (`167ce0`) and before `Path_AdvanceFineRoute` reaches
  `Path_CheckFineWaypointProgress` (`167070`). A retained intermediate coarse point already
  in accelerator range is therefore consumed and the fine leg refilled on the same visit,
  with no held stop. `167070`'s return-2 hold applies only when the fine endpoint is in range
  but the coarse point is not. Engine: `G_UnitMoveAdaptiveWaypointDue` gates the group
  member's early `move_group_advance_endpoint`.
- **Adjusted coarse goal (path+24).** `165b60` requests the coarse route to
  `Path_SelectRequestDestination(path, buf, 1)`. That is the adjusted goal at path+24, not
  the click. A new destination sets it to the click. When `166c30`'s result ends at a point
  other than its request goal (flag `0x20000000`), that endpoint becomes the adjusted goal.
  The completion retry `168740` discards the coarse and fine tables but keeps path+24. A
  later re-request for the same destination therefore searches only to the old endpoint.
  It charges little work (3 pops in Orc05, against 106 for the click), and
  `adaptive_request_time` is cleared when charged work is under 32. The interval then admits
  the re-request on the next visit. Engine: `moveFineRoute_t.adaptive_adjusted*`, used by
  `move_adaptive_waypoint` and `move_retry_goal`, and saved since format 177. Requesting
  the click again moved Orc05's forced arrival one visit early at 113.3 s.
- **Forced arrival needs a ready destination (`16fd0a`).** After `Path_Advance` returns 3 or 4,
  `Mover_StepRoute` (`16fbd0`) sets forced arrival (mover `+d8` `0x10000`, member flag
  `0x20000`) only when this visit's `167e40` reported ready. A target refresh that moves
  the destination into another comparison cell while a request timestamp is under ten
  visits old keeps the stopped status but no longer forces arrival. The member stays out of
  the forced/arrived cycle until the interval admits the replacement. In the Undead07
  head-on journey, a Peasant following a Hero restarted one visit late because the engine
  re-forced arrival on the refresh visit. Engine: `move_group_advance_endpoint(..., ready)`.
- **Short coarse results deny classification (`166c30`, `169b00`).** After every successful
  search, `166c30` compares its request goal with the first coarse point (`6f166df3`). If they
  are equal it clears path flag `0x20000000`. Otherwise it stores the scaled endpoint in
  path+24/+28 and sets the flag. `168740` clears the flag together with `0x10000000`
  (partial) when called with clearResults. The callers are the destination setter `168b80` and
  the completion retry. `169b00` denies projected classification and reseeds cooldown 66 when
  any member path has either flag. In narrow passages, coarse routes often end short. The
  engine only checked partial fine routes, so it classified the cohort when the cooldown ran
  out. It then held members (`0x200000`) and dropped speed sharing, while the original kept
  denying. Engine: `moveFineRoute_t.adaptive_short` (save 180).
- **War3Patch.mpq outranks every base archive.** The original searches War3Patch.mpq first.
  The engine mounted a data directory's archives in name order, and later mounts win, so in
  TFT mode `War3x.mpq` and `War3xlocal.mpq` shadowed the patch. Engine TFT runs therefore
  used the 1.0x `common.j` (no `PATHING_TYPE_*` globals, so `IsTerrainPathable` received a
  null type) and pre-patch SLK rows: 28 move speeds, 21 turn rates and 24 TFT neutral
  types differed from the original. `FS_ComparePaths` (`common/common.c`) now mounts the
  patch last. The `pathing` probe (`IsTerrainPathable` walk/fly/build/amphibious bits over a
  box) matches all 1,444 Orc07 cells after the fix.
- **SLK reals** use the original decimal parser `070de0` (`wc3_decimal`), which is not
  correctly rounded: `"0.4"` gives `3eccccce`, and `"1.5"` gives `3fc00001`. Tests that author
  a captured float word as SLK text must search for text that this parser maps to the word
  (see `target168_positive_fades_match_original_listener_requests`).

The journey tracer also accepts `ra` (caller RVA) and `bt` (game.dll backtrace) call options,
`aN/K` (K dwords behind stack argument N at entry), `dOFF/K` (K dwords behind `[ECX+OFF]` on
leave) and `lOFF` (the dword at `ECX+OFF` on leave). Dereferences of unmapped memory are
recorded as missing instead of faulting. `WC3_JOURNEY_RETRIES=<file>` makes the engine replay
log every retry seed draw as `time edict count word`, for comparison with `--call 1b7130::ra`.
`--call 1b7130::ra` records every `PathRandom_Next` draw with its caller, and the owner stream
is the state that `1d62bb` (retry init) uses. Draws made before the probe's
`PATHMETA case=` marker (race resolution at `1e9dd0`) are not recorded.

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
- The unitdata probe still differs for acquire range on 642 types (default = authored
  UnitWeapons `acquire`, else sight, no sight clamp; current is 0 while weapons are off) and
  for five current move speeds. Default and current turn rate, propulsion window and default
  move speed match for all 643 types, and every TFT neutral type is created, since
  War3Patch.mpq takes priority (see Verified Original Rules).
- `-loadfile` on a melee map seats a Computer in slot 1 and leaves the other W3I slots empty.
  The engine loader seats every W3I Human slot, so melee GetPlayerSlotState/controller rows
  still differ. Retail GetPlayerTeam returns -1 for non-playing slots, but the engine returns
  the W3I force index.

## Status

| Journey | Subjects | Result |
|---|---|---|
| Lost Temple crossing | 24 | bit-exact over 2400 ticks (~80k commits) |
| Human05 crossing (seed 5) | 35 | bit-exact over 2400 ticks: every sample and velocity commit (the original adds two commits after its completion marker) |
| Orc05 crossing (seed 6) | 39 | bit-exact over 2400 ticks, every sample and velocity commit (the original's trace ends two commits later) |
| Undead07 crossing (seed 8) | 35 | bit-exact over 2400 ticks, every sample and velocity commit |
| Human08 crossing (seed 9) | 35 | bit-exact over 2400 ticks, every sample and velocity commit |
| Orc05 head-on (seed 21, 2 pairs) | 47 | bit-exact over 2400 ticks: two pairs of Hero-led 12-unit groups swap places three times |
| Undead07 head-on (seed 22, 3 pairs) | 59 | bit-exact over 2400 ticks: six Hero-led groups, four Hero followers, four lone crossers |
| Human05 chokepoints (seed 31, 3 pairs) | 58 | bit-exact over 2400 ticks: each pair swaps through a 4–8-cell passage three times |
| Undead05 chokepoints (seed 34, 3 pairs) | 58 | bit-exact over 2400 ticks |
| NightElf02 crossing (seed 7) | 31 | diverges at 0.03 s: the map authors Player 0 as a Computer (see below) |

NightElf02: config makes Player 0 `MAP_CONTROL_COMPUTER`, so `6f1e9a40` creates its Town AI
before the probe switches the controller to User. On creation, each owned unit enrols
(`9b9230` -> `9c3660`) and Town AI assigns it work (`9c9d07`, near
`TownAI_AssignOwnedUnitCaptain`). The probe's Wisp is sent to harvest with `smart` at once,
because Wisps need no drop-off building. The engine's Town AI does not assign newly owned
units yet. That is the captain/Town AI work tracked as GROUP-03.4.x.

## Verification

- `wc3_journey.replay` runs only with `WC3_JOURNEY_MAP`/`WC3_JOURNEY_OUT` set and local
  original data. It is a diagnostic, not a suite regression.
- Suite regressions extracted from journeys: `wc3_api.alliance_defaults_include_neutral_passive_and_neutral_controller_slots`,
  `wc3_api.group_add_prepends_heroes_like_original`,
  `wc3_spell.aura_targets_follow_source_team_alliances_and_neutral_mask`,
  `wc3_slk.authored_scalars_use_the_original_decimal_parser`,
  `pathfinding.partial_coarse_endpoint_becomes_request_goal_until_destination_changes`,
  `wc3_movement.premature_destination_change_keeps_retry_status_without_forced_arrival`,
  `wc3_movement.selected_blocked_formation_classifies_before_destination_adjustment` (short-result variants),
  `commands.patch_archive_outranks_expansion_archives` (`test-commands`).
