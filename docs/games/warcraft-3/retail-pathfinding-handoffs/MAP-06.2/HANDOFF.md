<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-06.2/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **MAP-06.2**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-06.2/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-MAP-06.2.json` -> [`MAP-06.2-expected.json`](../../../../../tools/ghidra/fixtures/research/MAP-06.2-expected.json) (uncompressed sha256 `0856f6f36fecb20f81f0b9b17b4e091c926d325632055de94ac952b8937d8249`, 157059 bytes)

# MAP-06.2 handoff — save/load during an active route: retained vs rebuilt path state, resumed movement vs uninterrupted control

**Status.** Live-verified (retail 1.27.1, isolated env) with read-only observers and observer-free controls:
(a) a JASS `SaveGame` at tick 20 while Footman `M` is mid-route does not change any committed movement word of the 4 tracked units
(2 repeats, each against a control); (b) an in-game **UI save (F10 → Save) while M and a 3-unit crowd are mid-route, followed by
a UI load**, resumes all 4 units with commit sequences **bit-identical** (tick, position, velocity words) to the uninterrupted
control from the save point to arrival (74/99/60/75 commits), and the observer-free repeat reproduces the same JASS position
markers. Instruction-verified save/load functions; live-verified what is retained and what is rebuilt (table below). Not
established: in-session JASS `LoadGame` and command-line `-loadfile <save>` (both failed in this env, preserved below); movers
other than the 4 tracked units are inventoried, not trajectory-compared.

## Functions (assembly; Ghidra names written via gw.py, rows in `mapping-rows-MAP-06.2.txt`)

| VA | Role | ABI | Evidence |
|---|---|---|---|
| 6f15b4f0 | PathOwner_Save | thiscall ECX owner, [4] stream; RET4 | section-framed; PathMaps_Save first, then 6f167500, 6f15c8b0 ×2, 6f15c930; caller 6f04dd30 |
| 6f15c750 | PathMaps_Save | thiscall ECX owner, [4] stream; RET4 | 6 map handles; **full compaction 6f14dfc0 of +234 then +238**; 6f14d620 both; adaptive 6f163120 |
| 6f14d620 | SpatialMap_Save | thiscall ECX map, [4]; RET4 | geometry 6f14d590, stamp +B4 (value **before** the per-cell collectors run), PathRandom +BC/+C0, per cell 6f14d4c0 |
| 6f14d4c0 | SpatialMap_SaveCell | thiscall, [4],[8] x,[C] y,[10] collector; RET10 | only cells with terrain high byte or region-style (`+40 0x10000000`) live objects; collector 6f14cdf0 increments the stamp |
| 6f14d430 | SpatialObject_Save | vtable 6fa9096c; thiscall ECX obj, [4]; RET4 | map/payload handles, +34, rect +1C..28, +38, +3C, +40 |
| 6f15b1c0 | PathOwner_LoadSaved | thiscall ECX owner, [4] | caller 6f04ced0; PathMaps_LoadSaved |
| 6f15c180 | PathMaps_LoadSaved | thiscall ECX owner, [4]; RET4 | 6f14d1d0 for +234/+238, adaptive 6f162f20, +24C/+250 handles |
| 6f14d1d0 | SpatialMap_LoadSaved | thiscall ECX map, [4]; RET4 | cells refilled `0xffffff`, dirty zeroed, saved cells via 6f14d0b0, **new maintenance request** |
| 6f14d0b0 | SpatialMap_LoadCell | thiscall ECX map, [4]; RET4 | prepends saved handles in saved order (chain reversed) |
| 6f14d000 | SpatialObject_Load | vtable 6fa9096c+4; thiscall ECX obj, [4]; RET4 | restores fields; if not region-style re-inserts the rect via 6f14d380 (prepend, row-major) |
| 6f211360 | JASS `SaveGame` | native | one save per game session (flag +3E4); rejects names with `..`/`:`; writes `save/Profile1/CustomSaves/<map>/<name>` (no extension) |
| 6f20e070 | JASS `LoadGame` | native | see failed captures |

## Retained vs rebuilt (live, UI save at tick 57 during the route, UI load at ~120 s; `ui_route-saveload-observe-2`)

| State | Result |
|---|---|
| owner, maps, pools | whole owner torn down (all movers/regions retired, release full compaction 100→0 / 7,076→0) and a new owner built; maps loaded fresh (link storage empty, then rebuilt: fine 6,976 region records from map cells, then 66 object insertions → 7,042 = records at save) |
| link storage | compacted: `linkCount == records` (55/55, 7,042/7,042), free list empty (`00ffffff`); before save it was 60 / 7,270 links with free lists |
| spatial objects | 266 saved and loaded in the same order; rect, +38 stamp, +3C refs, +40 flags identical per object |
| cell chains | every chain after load lists objects in **descending save order** (load order = save order; prepend). Static chains are unchanged; the moving crowd cell 530 was `[M,C0,C1,C2]` at save and `[C2,C1,C0,M]` after load (reversed). Idle UI load (uiload-observe-1): cell 581 `[C0,M,C2,C1]` → `[C2,C1,C0,M]` |
| map stamp +B4 | restored to the value at the start of SpatialMap_Save (proximity `0000019b`, fine `00003507`); the continuing session is ahead by the save collectors (`000001f3`, `0000474b`) |
| maintenance | new request per map on a new clock: first deadlines `3dccccce,3e4cccce,3e99999a` after load (before save the clock was at `40b6665c` = 5.7); proximity before fine |
| movers / routes / orders | reconstructed by the save system; the 4 tracked movers resume at tick 58 (first post-save commit) |
| movement | M, C0, C1, C2: post-load sequences equal the control from control index 167 to arrival, every word (74/99/60/75 commits); pre-save sequences equal the control too |
| JASS markers | observer: resumed 487 lines = control suffix; observer-free repeat (save at tick 56): resumed 489 lines = control suffix |

Scripted save (no load; `save_only` vs `save_control`, v1 and v2): SaveGame at tick 20 runs synchronously inside the trigger
(PathMaps_Save compaction fine 7,058 → 7,042), and all 4 units' commit sequences equal the control word for word (M 115, C0 133, C1 108, C2 125 commits; v2 maps repeat it).

Frozen: `expected-MAP-06.2.json` sha256 `0856f6f36fecb20f81f0b9b17b4e091c926d325632055de94ac952b8937d8249` (composer re-run reproduces it).

## Reproducer (repository root)
```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
python3 tools/frida/research/sep03_map06_make_map.py --base /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/Human02Interlude-original.w3m \
  --tool /run/media/lofcz/ssd_external/GitHub/open-realm/build/bin/mpqtool --scenario ui_route --output /tmp/RS-ui_route.w3m
cp -n /tmp/RS-ui_route.w3m /run/media/lofcz/ssd_external/Games/w3-research/Maps/RS-MAP-06.2-ui_route.w3m
tools/frida/research/sep03_map06_ui_route_runs.sh        # control + UI save/load observe-1 (flock-wrapped)
tools/frida/research/sep03_map06_batch7_runs.sh          # observe-2 (--all-movers), observer-free save/load, controls
python3 tools/frida/research/sep03_map06_expected.py map062 $R/MAP-06.2/captures/{save_control-observe-1,save_only-observe-1,uiload-observe-1,ui_route-control-observe-1,ui_route-saveload-observe-1,ui_route-saveload-observe-2}.jsonl \
  $R/MAP-06.2/captures/{ui_route-control-observe-1,ui_route-saveload-control-1,ui_route-saveload-observe-2}-rs-ui_route.txt $R/MAP-06.2/expected-MAP-06.2.json
```
Scripted-save maps (`save_control`, `save_only`, `save_load`, `*2`) are built the same way (`--scenario save_control|save_only|save_load`);
runs via `sep03_map06_runs.sh` with `--delete-save RS`. UI action timing (window coords): 85.5 F10, 86.5 `s`, 87.5 type `RSUIROUTE`,
89.5 Return; 120 F10, 121 `l`, 122.5 click 366,204 (first list item), 123.5 click 353,414 (Load); 165/170/175 Space.

## Provenance
game.dll `d51e5680…d8236`. Maps: ui_route `cbf7183d…5d2e` (+ `.j` `0f1099c9…`), save_control-v2 `8068d17f…a549`, save_only-v2
`30f7c959…febe`, save_load-v2 `f69bcc90…03aa` (copies in `maps/`). Saves (`saves/`): RSUIROUTE observe-1 `dc9afa7e…8d07`, observe-2 and
control-1 copies, RSUITEST `ed2e44e3…cf6d`, RSMAP062main `5d00866c…3b94`; full list `captures/save-files-sha256-1923.txt` and the
`artifacts` row of every capture. Scripts (current): observer v3 `42f4100a…11a5`, controller `2999b547…6e24` (captures up to uiload used `3e3854a0…`; later edits
add UI actions/all-movers flag only), probe `66547bef…3716`, map builder `1b74b635…5fa8`, runners `925931d8…99e1` /
`f0143e9a…7edc`, composer `e5e6f59d…dd06`, compare `76bbbe61…417a`. No seed involved.

## Captures (all preserved)
| Capture | sha256 | Status |
|---|---|---|
| save_control-observe-1 / save_control2-observe-1 | `4980fb55…0bbe` / `803af597…91e1` | complete controls, 481 commits, 333 markers |
| save_only-observe-1 / save_only2-observe-1 | `a780ece6…67ca` / `535c37c2…716d` | complete, save at tick 20, sequences = control |
| save_load-observe-1 | `fbb0452f…dae2` | **failed load**: `LoadGame` at tick 22 returned to the main menu (no owner-load) |
| save_load2-observe-0-argerror.log | — | argparse error (prefix check), no game run |
| save_load2-observe-1 | `be17db47…1474` | **failed load**: second `SaveGame` skipped (+3E4), `LoadGame` at tick 35 returned to the menu |
| loadsave-observe-1 | `5df2106e…3fc9` | **failed**: `-loadfile` of a copied `.w3z` → black screen, 37 events |
| uiload-observe-1 | `cac5b586…c357` | complete: idle UI save/load at tick 160 (after arrival), 266 objects, screenshots |
| ui_route-*-observe-0-patherror.log | — | shell escaping lost `\` in the map path; no game run |
| ui_route-control-observe-1 | `010f77d6…852e` | complete uninterrupted control, 614 markers |
| ui_route-saveload-observe-1 | `4b11de16…bc03` | complete UI save tick 55 + load; tracked-mover filter hid loaded movers' commits (markers only) |
| ui_route-saveload-observe-2 | `8c4381d3…39db` | complete, `--all-movers`, save tick 57 + load; exact comparison |
| ui_route-saveload-control-1 | `ad98d160…28e3` (preload `66fc4e6d…a1ff`) | complete observer-free UI save (tick 56) + load; 1,103 markers |
| ui_route-control-control-1 | `60feabca…b6e5` (preload `dadad074…2812`) | complete observer-free control; 614 markers = observed control |

## Observer controls
Observer-free uninterrupted control: 614/614 markers equal the observed control. Observer-free UI save/load run: first pass equal to
the control (614/614) and the resumed 489 lines equal the control suffix — same result as both observed runs (save tick differs
per run: 55/57/56, wall-clock UI timing). Observed scripted runs equal their observed controls; SEP-03.1 shows
observer = observer-free for this observer.

## Exclusions
Adaptive map save/load (6f163120/6f162f20; ACC-05.x); group/formation state (GROUP-*); orders other than Move; save during path
search in flight within one frame (not targetable via UI timing); multiplayer/replay.

## Mismatches preserved
* Ledger: "Save/load … retained versus rebuilt path state unknown" → spatial state is rebuilt from saved objects with **reversed
  chain order** for cells whose order came from movement re-prepends, yet resumed movement is identical in this scenario (chain
  order did not influence these 4 units; order-sensitive repulsion is SEP-01/02/04).
* FINE-01.6 ledger text says Save91 preserves cell order; retail load re-inserts objects in save order (instruction, both maps),
  which reverses movement-ordered chains (live, proximity). Fine-map multi-entry chains were not captured (single entries under the
  tracked movers). To be reconciled by the FINE-01.6/SEP owners.
* Map stamp after load is lower than the continuing session's (save collectors); harmless here.
* Maintenance clock phase restarts at load (deadline 0.1 after load instead of continuing at multiples of the saved clock).
* JASS `LoadGame` and `-loadfile` attempts failed in this env (above); UI load was used instead.

## Proposed integration (not applied)
`mapping-rows-MAP-06.2.txt` (13 rows, written to Ghidra), `proposed-docs-MAP-06.2.md`.

## Suggested failing engine regressions
1. Save while 4 units are mid-route, load, continue: per-unit commit sequences equal the uninterrupted run from the save point to
   arrival (fixture: `expected-MAP-06.2.json` → `active_route_ui_save_load.resumed`).
2. After load, every spatial cell chain lists objects in reverse save order and link storage has `count == records`, empty free list.
3. Saving does not change subsequent movement (scripted save at tick 20 vs control).

## Engine integration — Payoff128

MAP-06.2 is integrated through Save118 logical fine rectangles, load-order
publication, order-sensitive fine-query tests and four public active-route
continuations. Full capture and insertion-stage reproduction is in
`verify_wc3_pathing_spatial_save.py`. The research limitations above remain;
proximity stamps/ordering and lazy maintenance stay in SEP-02/03. See
[engine integration](../../retail-pathfinding-engine.md#spatial-load-rebuilds-membership-in-save-order).
