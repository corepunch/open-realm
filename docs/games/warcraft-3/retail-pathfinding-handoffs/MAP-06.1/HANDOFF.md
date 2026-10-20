<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-06.1/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **MAP-06.1**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-06.1/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-MAP-06.1.json` -> [`MAP-06.1-expected.json`](../../../../../tools/ghidra/fixtures/research/MAP-06.1-expected.json) (uncompressed sha256 `974810fd91377a9839bb923e397e0e0255c92d67782cbc971a6bb9c41a2dc98f`, 55704 bytes)

# MAP-06.1 handoff — map destruction/reload with a live mover: retained vs rebuilt state, first subsequent movement

**Status.** Live-verified with read-only observers on two retail flows started from JASS while a Footman `M` is mid-route:
`ChangeLevel` to a second copy of the map (1 transition) and `RestartGame(false)` (8 complete restart cycles in one capture).
Instruction-verified teardown order and functions (PathMaps_Release, SpatialMap_Release, PathOwner_Replace/Destroy). Original-code
verified pool/map release and reuse (MAP-05.1). Established: nothing pathing-related survives a reload — owner, pools, maps,
link storage, spatial objects, maintenance requests and movers are all destroyed and rebuilt; the first movement after every
reload is bit-identical to the original first movement (exact position/velocity words) up to the teardown point. Observer-free
controls reproduce the JASS markers of both flows. Not established: in-game UI "Restart Mission"/"Load map" menu paths (JASS
natives used instead).

## Functions (assembly)

| VA | Role | ABI | Evidence |
|---|---|---|---|
| 6f0509d0 | PathOwner_Replace | thiscall ECX seed | SMemAlloc(0x978) (0509fb..050a0a) → PathOwner_Construct(seed); old [6fd53a48] → PathOwner_Destroy + SMemFree; publish new (050a55) |
| 6f1591e0 | PathOwner_Destroy | thiscall ECX owner | first 6f15b620; callers 6f0509d0/6f053110/6f04c020 |
| 6f15b670 | PathMaps_Release | thiscall ECX owner; RET0 | vtable+10(0) on +24C, +250, +234, +238, +23C..248; each slot cleared |
| 6f14cac0 | SpatialMap_Release | thiscall ECX map, [4]; RET4 | full compaction; cancel maintenance request; free links/dirty/cells; pool return |
| 6f15ab60 | PathMaps_Create | thiscall ECX owner, [4],[8]; | calls PathMaps_Release first (15ab81), then map factories |
| 6f15ee60 | Mover_RetireSpatialObjects | thiscall ECX mover, [4]; RET4 | Retire +94 then +98 |

## Retained vs rebuilt (live)

| State | ChangeLevel (A→B) | RestartGame (8 cycles) |
|---|---|---|
| teardown order | M retired first (caller 6f16eb83, unit removal); then one interleaved loop: groups of 3–4 region-style objects (flag `0x10000000`, caller 6f063b8b) before each of the other 50 mover retirements (caller 6f1466b8), 158 region objects in total, ending with 3 movers; then PathMaps_Release → PathOwner_Destroy | identical sequence (97 run-length groups) in every cycle |
| outstanding spatial records at release | proximity 100 → 0, fine 7,076 → 0 (release full compaction recycles 258 objects; 2 already recycled by maintenance) | identical |
| path owner | destroyed and replaced; new object at the same heap address `0x46200c0` (Storm reuse, freshly constructed) | same |
| maps / search systems | returned to the old owner's map pool (recycled head `0xa170164`), destroyed with it; new maps from the new owner (`0xa1700a0`→`0x92e00a0`) | new each cycle (`0xf4b00a0`, `0xa4400a0`, `0xa6200a0`, `0xa5800a0`…; heap reuse repeats `0xa5800a0` in cycles 4/7/8) |
| link storage | freed at release (`link-reserve` 0 bytes); new 1 MiB block per map at the first mover commit | same, every cycle |
| spatial object pool | old pool (raw 260) gone; 5 new 64-object blocks during load | same |
| maintenance requests | cancelled at release; new requests, first deadline `3dccccce` on a new clock | same |
| movers, routes, orders | destroyed; M recreated by the script at a new address (`0x16930098`→`0x16970098`) | new address each cycle |
| JASS state | new script instance (`tick=0 label=init scenario=5`) | restarts at `tick=0` each cycle |

## First subsequent movement (exact words, frozen `expected-MAP-06.1.json` sha256 `974810fd91377a9839bb923e397e0e0255c92d67782cbc971a6bb9c41a2dc98f`)
* ChangeLevel: map B's `M` repeats map A's first 46 commits exactly (start `c7fa0040,c7fa0040` → `43238000,42830000`, then the
  0.1 s-cadence route with velocity `3186ff7c,4106ff7b`); A's sequence ends with its teardown commit at tick 21, B continues
  (111 commits) to arrival.
* RestartGame: in all 8 cycles the commit sequence equals cycle 0 up to the teardown (prefix 43–49 of 46–49 commits); the only
  differing commits are the teardown commits (partial integration then zero velocity: `4295a57e`, `4294cd81`, `42953980`), whose
  position depends on where the restart falls inside the frame (wall-clock), not on retained state.
* JASS markers of map B ticks 1–20 equal map A's except the scenario id and the label of tick 20.

## Reproducer
```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
for s in changelevel_a changelevel_b restart; do python3 tools/frida/research/sep03_map06_make_map.py \
  --base /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/Human02Interlude-original.w3m \
  --tool /run/media/lofcz/ssd_external/GitHub/open-realm/build/bin/mpqtool --scenario $s --output /tmp/RS-$s.w3m
  cp -n /tmp/RS-$s.w3m /run/media/lofcz/ssd_external/Games/w3-research/Maps/RS-MAP-06.1-$s.w3m; done
tools/frida/research/sep03_map06_runs.sh $R/MAP-06.1/captures \
  'changelevel-observe-1:observe:RS-MAP-06.1-changelevel_a:200:--continue-every 15 --preload-names rs-changelevel_a-prechange.txt,rs-changelevel_b.txt' \
  'restart-observe-1:observe:RS-MAP-06.1-restart:200:--continue-every 15 --preload-names rs-restart-prerestart.txt'
python3 tools/frida/research/sep03_map06_analyze.py lifetime $R/MAP-06.1/captures/restart-observe-1.jsonl
python3 tools/frida/research/sep03_map06_expected.py map061 $R/MAP-06.1/captures/changelevel-observe-1.jsonl \
  $R/MAP-06.1/captures/restart-observe-1.jsonl $R/MAP-06.1/expected-MAP-06.1.json   # deterministic (re-run equal)
tools/frida/research/sep03_map06_batch7_runs.sh   # includes changelevel-control-1 / restart-control-1 (observer-free)
```

## Provenance
game.dll `d51e5680…d8236`; maps changelevel_a `4c38527b…ee83`, changelevel_b `f8887fb9…2fcf`, restart `dea2f824…4997` (probe
`c84fc4e8…` at build; ChangeLevel target `Maps\RS-MAP-06.1-changelevel_b.w3m`); controller `c8e9eb9d…`, observer v3 `42f4100a…`,
analyzer `816adb39…`, composer `e5e6f59d…dd06`, batch runner `f0143e9a…7edc`. No seed involved.

## Captures
| Capture | sha256 | Status |
|---|---|---|
| changelevel-observe-1.jsonl | `e8ffd78f…568b` | complete, 4,396 events, 0 errors, preload A `7ef71767…` (26 markers) and B `d001dbbb…` (167) |
| restart-observe-1.jsonl | `2c17d4f6…181c` | complete, 11,240 events, 0 errors, 8 full cycles + 9th started; preload (last cycle) `2beba74a…` |
| changelevel-control-1.jsonl | `87793206…0c1a` | observer-free, complete (200 s); preload A `75bc3965…b549`, B `766e12fb…4462` |
| restart-control-1.jsonl | `05e7d40e…92cd` | observer-free, complete (200 s); preload (last cycle) `dd9af50d…6361` |

## Observer controls
Observer-free runs (same maps, timing, Space presses): `changelevel-control-1` markers equal the observed run (map A 26/26, map B
167/167); `restart-control-1` last-cycle markers equal (26/26). Markers carry M's position every 0.1 s (3 decimals).

## Exclusions
Save/load (MAP-06.2); allocation details (MAP-05.1); menu-driven restart. Units other than M (50 pre-existing movers, regions) are
inventoried but not trajectory-compared.

## Mismatches preserved
* Ledger construction text: "Full proximity/accelerator release, reload, post-release timer draining … remain open" → live release
  and rebuild observed; old requests are cancelled before the owner (and its clock) is destroyed.
* First maintenance stamp differs between cycles (proximity `00000030` in 6 cycles, `00000039` and `00000036` in two): the number
  of stamp-consuming queries before the first deadline varies; no effect on M's trajectory.
* MAP-05.1 oracle hazard (stale spatial object across release, map+B0/+AC not reset) does not occur in these flows: all objects are
  retired before release and +B0 reaches 0.

## Proposed integration (not applied)
`mapping-rows-MAP-06.1.txt` (PathOwner_Destroy/Replace, Mover_RetireSpatialObjects; written to Ghidra), `proposed-docs-MAP-06.1.md`.

## Suggested failing engine regressions
1. Map reload while a unit moves: after reload the engine has no pathing state from the previous map (no maps, links, spatial
   records, maintenance timers, movers or routes); a unit created and ordered identically produces the same per-step positions as
   on first load.
2. Teardown order: units and regions are retired before map storage is released; assert zero outstanding spatial records at release.


## Engine integration (Payoff129)

The ordered `G_ReleaseLevel` boundary now precedes world loading and typed-row
replacement. Old actor routes, queues, spatial memberships and borrowed-geometry
jobs retire before the new map can reuse their storage. Actual MPQ reload tests
cover eight restarts, a changed-size level, unreused actor slots, public movement
and alternating worker/inline execution. The full frozen report remains byte
identical, including wall-clock teardown differences;10 retained raw capture and
marker files reproduce it and the219 observer-free control markers.
See [engine contract](../../retail-pathfinding-engine.md#map-replacement-retires-movement-owners-before-world-data).
The isolated original release oracle executes nine constructor/retire/release/
destructor cases; it does not substitute for the full live load flow.
