# Water, cliff, bridge and widget target goals (FOOT-04.2 recovery)

This is an unfinished recovered candidate, not a FOOT-04.2 closure. The original
Claude worktree is `agent-ad242504f7459a0ea`; the integration candidate is
`/tmp/open-realm-takeover-supportgoal`. Its base predates the accepted Stone390/Harvest012/Reach031 commit
`d14ed1e48`. Apply only the support delta over that commit; its older corpus
metadata and unchecked BASE-03.1 must not replace the accepted main tree.

## Preserved reference evidence

Two complete read-only observations and one observer-free public control survive
for each family in `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/FOOT-04.2/`:

| Family | Accepted source files | Scenes | Public markers per run | Native records per observation | Physical commits |
|---|---|---:|---:|---:|---:|
| supportgoal | observe7, observe8, control1 | 200 | 11319 | 136858 | 31669 |
| widgetgoal | observe1, observe2, control1 | 40 | 1907 | 23634 | 5015 |

The five movement types are foot, fly, amph, hover and float, each with radii
8/24/40/56. Ten support goals cover deep water, deck, banks, cliff, outside,
retained/removed floating-unit targets, the bridge widget and queryzero. Widget
scenes kill or remove the approached bridge through public JASS natives; 36
scenes bind a successor physical group. Original source hashes match the
recovered map builders, probes, observers and input helpers. Full native streams
repeat after explicit pointer normalization; public controls match literally.

The frozen JSON bundle preserves every parsed record and the preload text's
original CRLF bytes. `Path.read_text()` silently converted those newlines and
failed the original footer hashes; loading bytes and decoding preserves them.
Do not change the original hashes. Twelve contract tests include that rejection and supplemental argument/core tampering,
missing repeat/event/guard/engine-pattern cases, hooked controls and mismatched
environment/display pairs. The latter first reproduced false acceptance and now
requires B/:98 or C/:99. The original guard union has3085 instructions from Bridge311, Goal310 and nine
Move/path/target-loss functions; supplemental support-level functions bring the
current union to3286 instructions plus two CUnit virtual-table guards.

## Engine regression and partial fixes

The recovered support regression failed120 of760849 assertions in each edition
before the recovered engine patch. Move rejected destructable targets. The patch
admits a live destructable through the existing one-shot Move target owner,
validates its world state without player diplomacy, and excludes its selection
radius from physical mover-radius addition. Both editions then pass the complete
200-scene fresh/saved support regression: one test /781201 assertions each.

The handoff verifier named a widget fixture and test that were absent. Both are
now derived from the original widget captures. The public KillDestructable and
RemoveDestructable journey initially failed368 of18849 assertions per edition.
The candidate publishes intrinsic target loss after the changed death footprint
and before public death callbacks; removal publishes after its footprint and
queued identity. This reuses existing registered subscribers and Move recovery.
A birth/inuse guard handles synchronous lifetime changes after death delivery.
No save fields were added; the candidate remains on batch Save218.

Intrinsic Lost publication first improved the Classic widget result to126
failures. Further investigation found both production and fixture differences:

- Two new complete read-only observations reproduce every original core native
  record and all1907 public markers exactly; the original observer-free control
  is reusable. All345 recovery argument rows match between observations. The
  original capture/preload bytes are separately retained, including CRLF.
- Native Unit vD4 resolves to684650. Float (`Unit1fc == 0x10`) queries water
  support and computes `((R2I(z)+0x20c0)&~127)-0x1f81`, then SAR7. Float death
  scenes supply callback level2, rather than retained bridge level4. Generic
  widgets use6523a0/652270 and the retained placement registry.
- Original170080 first tests the endpoint footprint WITHOUT654060. A clear
  endpoint returns without applying the support predicate. Only a blocked
  endpoint enters14a1e0 with the captured level. Passing the level predicate to
  the first geometry check moved clear Float starts incorrectly; the candidate
  now keeps those stages separate.
- The original map reuses its initial bridge through scene20. Later scenes
  follow same-callback Remove/CreateDestructableZ(0) replacements. Their retained
  support level becomes5. The widget fixture now replays that constructor and
  release history, rather than using an unrelated fresh bridge in every scene.
- Driving only captured Move commits did not advance the real primary release
  heap. Removal's death footprint therefore stayed present forever in the first
  fixture. The corrected fixture drains due releases before each owner commit,
  and saves/restores both before mutation and while removal is pending. The
  public Remove callback also clears its JASS bridge variable, as the original
  probe does, rather than serializing a freed handle.

With those corrections, the complete40-scene widget regression passes124273
assertions per edition before the subsequent result-code observer was added.
The earlier200-scene support gate passes781201 per edition. The strengthened
candidate now asserts literal167290-equivalent perimeter/retry result codes AND
callback counter order; those tables existed in the handoff but were previously
unused by the C tests. Both complete families pass907610 assertions per edition
in two tests (support782669, widget124941). The result observer must cover the
shared retry kernel as well as early target-perimeter3; observing only the
endpoint wrapper missed the separate blocker-retry caller and produced246
observation failures. Twelve Python contract tests pass. Shared-source gates, corpus registration and full acceptance are recorded in
the final checkpoint below.
This document does not close FOOT-04.2.

The supplemental verifier retains all raw callback context words, but compares
only the first as654060's input. Its three adjacent stack words are not read by
that callback and are not a semantic equality claim. All pre-existing core
record fields remain part of exact repeat comparison. The static union now
contains3286 instructions plus the CUnit vD4/vE4 table guards. Supplemental
capture tooling uses `/home/lofcz/.local/share/uv/tools/frida-tools/bin/python`;
system Python has no Frida module. Reserve B/C through
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/_env/live.sh`; the obsolete
README's owner environment A is not used.

## Reproduction and next gate

```sh
make -j16 openwarcraft3-tests build/lib/libgame.so
make test-assets
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 build/bin/openwarcraft3-tests -data "$PWD/build/tests" +dedicated 1 +test 'wc3_movement.widgetgoal*'
python3 -m unittest discover -s tests -p test_wc3_pathfinding_supportgoal.py
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_supportgoal.py --binary /path/to/game.dll --report /tmp/supportgoal-fresh.json
```

The strict command recomputes the original game/Storm kernels and runs the
engine gates. It requires Unicorn; use the verification environment shown above. Reports
are under `.../research/codex-takeover-20261020/`: `supportgoal-valid-red-*`,
`supportgoal-expanded-*`, `widgetgoal-target-loss-green-classic-0.log` (despite its
proposed filename, it fails126), `supportgoal-contract-tests-red.log` and
`supportgoal-contract-tests-green.log`. `widgetgoal-primary-release-engine.json`
records the first complete widget success; `supportgoal-complete-result-engine`
and `supportgoal-recovery-contract-tests.log` own the strengthened acceptance. A launch before the rebuild completed
ran zero tests and is excluded. The first red reporting driver rejected the
failure-footer shape after the real Classic test failed; the native failure
footer survives and the corrected TFT driver records exit1.

Inspect `G_EnterDestructableDeathState`/`G_RemoveDestructable`,
`CAbilityMove(A_TARGET_LOST)`, `S_StopUnitMovementWithRecovery`,
`G_FindUnitMoveRecoveryPosition` and `move_place_unit`. Raw event chronology
places Lost/recover/rebind between complete physical commits. The retained level, captured Float water level, endpoint-only first test and
primary release must retain their separate owners; do not substitute expected
positions or special-case a scene. The larger
FOOT-04.2 matrix, shared-code gates and full checkpoint remain required.

## Remaining prepared-water input correction

The previous Float override called `CM_GetWaterHeightAtPoint`; it was NOT
the complete native684650 producer. The prepared-input correction below replaces
that approximation and repairs the newly reproduced ramp producer. Accepted
results and the retained scope boundaries are recorded below.

Original78dee0 forwards to7455a0, which reads the immutable Terrain+780/+784
prepared grid. Signed fine coordinates are divided by4 with truncation toward
zero and clamped to `Terrain.b4-1`/`b8-1`; this is not bilinear interpolation.
Builder748ab0 stores one software-scalar value per W3E cell. It traverses corner
order00,10,01,11, selects max(terrain height, decoded water height + direct water
metadata offset) per corner when the water-type list is present, adds with
Warcraft scalar operations, then multiplies by.25. Ground presentation grids
and the maximized flyer field are different consumers.

Two45-second read-only diagnostics captured the entire16x16 grid at probe start;
these are intentionally NOT complete journey acceptance. Both arrays agree:
211 values are raw word42b19998 (88.79998779296875),32 are128,12 are
108.39999389648438, and one is98.59999084472656. Raw W3E water level8550 decodes
to89.5. This grid therefore also distinguishes its direct metadata adjustment
from the simulated/rendered water surface's tile-scaled offset. The first
callback-only decoder patch is insufficient for nonuniform inputs.

Source/proof and diagnostic artifacts are preserved under
`.../research/codex-takeover-20261020/`: `widgetgoal-watergrid-diagnostic-a/b.jsonl`,
`widgetgoal-water-support-grid-diagnostic.json`, and the checked observer
`tools/frida/research/widgetgoal_watergrid_observer.js`. Native sources are
748ab0 (producer),7455a0 (cell lookup),78dee0 (wrapper),684650 (Float integer
level),6523a0/652270 (ordinary widget level),654060 (candidate predicate) and
69a840/171340/170080 (captured context and clear-footprint fast path).

The correction below retains all240 literal trajectories/result codes and adds
the prepared-grid and public Stop regressions. Its derived field rebuilds after
load without changing the Save218 stream. FOOT-04.2 and the existing
numeric/input-domain owners retain the documented wider scope.

## Prepared input recovery checkpoint (2026-10-20)

The separate game-owned `water_support` field now follows748ab0 software corner
accumulation and7455a0 signed fine-coordinate cell lookup.7488a0 creates a water
type only when any packed vertex carries water;7615f0 selects `<tileset>Sha`,
with the native748918 LSha fallback. CWaterDB constructor6ddf50 registers height
first; its vtable20 resolves6ef3b0 and6ef370. This metadata adjustment is direct,
not multiplied by128. Ground startup017c30/017c40 supplies-256 and128. These are
immutable map inputs, unlike the flyer field's removed-widget history. Clearing
support at map release or load invalidates the new field; its next query rebuilds
it from the loaded map and map data set. No serialized stream or edict field was
changed, so this checkpoint does not advance Save218. The public saved Stop
regression exercises that cold rebuild.

The complete original256-word grid regression failed256/256 before correction.
It now passes, including negative/outside cell clamps, a map with raw water but
no water vertices, and non-stock Water.slk height12.5 (expected102, not1689.5).
The public Stop regression uses the stock-.7 surface adjustment: restoring the
old surface producer makes the blocked unit remain embedded in both fresh and
saved worlds (44/46 assertions;2 failures). Restoring the prepared producer makes
all46 pass, including the clear-footprint fast path. Before adding the new ramp
regression, both editions pass13 tests/952434 assertions across support fields
and the complete240 original journeys; the later focused gate passes12 tests/
44877 assertions per edition. Fifteen Python contract checks pass, including
rejection of altered raw grid repeats and bounded diagnostics claiming journey
acceptance. One intermediate build failed because the new fixture used a
nonexistent loader name; it was corrected to existing `parse_slk_string` and the
next production/test builds completed without warnings.

The frozen bounded water-input bundle retains both original raw diagnostic files,
not engine output. Its verifier checks provenance/source hashes, complete native
observer sequences/counters and both256-word arrays; the C header is compared to
those words. The currently registered static guard union is3726 instructions
plus the two CUnit table entries. Nine additional prepared-water/metadata
functions account for440 instructions. That checkpoint preceded the ramp port and final acceptance below; its
then-failing ramp test was retained and repaired.

A further read of7451f0 found a genuine omitted input:7446e0's ramp ground-height
offset. Two new bounded read-only observations on a separate authored ramp map
have exactly matching2050 producer records (1024 ground corners,1024 ramp
offsets, entry and completed grid). Twenty corner calls add raw42800000 (=64).
The complete256-word prepared-grid regression initially failed14 cells (242/256
pass). That failure drove the port below; the temporary production TODO/error
has been removed after implementing and checking its native producer. The original journey maps/captures were unchanged.

Next port7446e0 through7445c0,744750,74a910,73a380 and7452b0. Their724 instruction
bytes, full disassembly and scalar/rotation data are exported to
`preparedwater-ramp-functions.jsonl` and `preparedwater-ramp-disassembly.txt` under
the takeover report directory. Important: Ghidra loses some integer returns at
security-cookie epilogues; use assembly.744750 returns-1/0/1/2.73a380 returns
-1/0/1/2. Assembly74470c/74470f/744712 maps0/1/2 to.25/.75/.5 times128;
the earlier enum-order interpretation was wrong. Its corner rotation table is
{0,0},{0,2},{2,0},{2,2}.74a910's initial minimum is Terrain988, not an invented
constant. Constructor729540 sets that field at729f6c to signed truncation of
`(Terrain.MaxHeight - runtime723d4)/128` using SSE. Original patch/TFT
UI\MiscData.txt has Terrain.MaxHeight1536 and MinHeight-128; the runtime723d4
startup writer017c10/017c20 multiplies raw43000000 (=128) by rawc0000000 (=-2),
so the stock cap is14. Exact bytes/disassembly and both constant words are in
`preparedwater-ramp-cap-source.txt`. The cap must still read authored MaxHeight,
not substitute14.

The new map SHA256 is3d9776adc4b838fe9490cc383161abe575328fed9379a7ae1bd95c18b7084f4e;
`preparedwater-ramp-input-a/b.jsonl` and `preparedwater-ramp-input-summary.json`
retain both bounded runs and all words. Their footer reports missing preload,
as expected when stopped45 seconds into the longer probe; these are producer
input diagnostics only. No movement parity or leaf closure follows from them.
The exact map builder is preserved in the takeover runtime directory as
`build-ramp-input-map.py`; both maps use new filenames in B/C and do not replace
the accepted widget arena. Copy this builder/freeze raw ramp evidence into the
repository as part of completing that regression, then refresh pins/guards,
run shared/strict/full gates, and port only the support delta onto d14ed1e48.

## Ramp producer integration

The recovered producer now ports7446e0 and its topology helpers. The original
ramp map's256 values all pass. The native oracle executes28,416 packed terrain
inputs with unchanged classification, cached high-ramp flags, software multiply
and security-cookie functions; no callee is replaced. Inputs include every3x3
ramp mask across shifted straight/corner layer bands, plus seeded boundary,
invalid-topology and authored-cap cases. Native offsets include32 (two cases),
64 (1249 cases),96 (one case) and zero (27164 cases). The C regression compares
native kinds/types and every offset, plus corner classification where the
production caller admits typeS. Literal input/output rows are in
`retail_preparedwater_ramp_kernels.h`, not generated from the engine algorithm.

Nine supplementary runs execute748ab0 unchanged on dry packed maps with
preallocated native grid storage. Native capacity/count make allocation
unnecessary; there is no allocator or scalar stub. The authored MaxHeight
values1536/-256/0 produce Terrain988 values14/0/2, and the resulting grids
actually differ. The production regression reads those values from
`game.config.misc`, initializes its real prepared field and compares all36 cells
for every case. It therefore rejects a hardcoded cap14. The original emulator
also reproduces all2048 ground/ramp corner results from each live diagnostic,
using the captured packed map input (4096 literal results total).

The bounded ramp map reproduces byte-for-byte with
`preparedwater_ramp_make_map.py --base <original-widget-arena> --output <fresh>`.
The raw map and both diagnostic streams are frozen separately from the full
support/widget journey captures. The strict verifier reruns the original
28,416 cases and all nine producers, rejects altered corner results, checks
literal headers and guard/pin completeness, then runs the actual engine gates.
The guard union is4477 instructions plus10 scalar/table/data spans. No saved
layout changed. The final isolated integration is based on d14ed1e48; its
corpus/owner registration preserves the accepted BASE-03.1 inventory. The final accepted full checkpoint is recorded below; this delivery does not
close FOOT-04.2.

## Metadata float and full-checkpoint repair

The first full checkpoint ran9988 native tests and2896 Python tests but exited2.
Its two native failures were the existing follow-range test in both editions:
the recovered target-radius expression inferred absence of a physical mover
from `SVF_MONSTER`. A unit-shaped target in that test has no publication flag,
yet retains its collision radius. The engine now excludes the actual destructable
class, preserving the existing unit contract and its unchanged128-unit assertion.
The full Python run also rejected stale Captain473, Group012 and Waygate377
source pins. Their captured bytes/expectations remain unchanged; candidate source
pins were refreshed and all three complete original/engine replays rerun, rather
than weakening any validator (10/156882,20/1146726,39/229803 tests/assertions per
edition respectively).

A subsequent numeric review found that MaxHeight uses71f510 ->1a8d50 ->Storm
Ordinal574. It is a separate producer from JASS S2R070de0. Storm reads a leading
minus, decimal chunks/fractional digits and a wrapping signed exponent using
binary64 SSE arithmetic, then rounds once to float. Leading plus or whitespace
at that boundary yields zero. Its actual table initializer1503a390 and all
parser/power/exponent instructions execute unchanged in the supplementary oracle.
Storm SHA256 is36339f69727f0f3dcb4431a9567b84f56007c331cfa3b183862af43e8314eb72,
matching the installed retail and both isolated observation environments.

The cap then uses native SSE subtract/divide and CVTTSS2SI. Invalid or out-of-range
results are integer-indefinite80000000, rather than Warcraft's wrapping integer
conversion. Example: `1.536e3` supplies1536, while `1e400` produces infinity and
cap80000000. The engine implements this producer with explicit binary64 rounding
stores and a guarded portable signed conversion. The Water.slk height retains
its separate typed scalar field path and direct prepared-grid adjustment.

The677 literal original metadata words/caps first failed758/1354 assertions;
all1354 pass after correction. Eight actual INI-to-prepared-field cases add296
assertions. Temporarily restoring the old consumer made12 of those296 fail;
all296 pass with the corrected producer. The unchanged follow-range test also
passes, giving3 tests/1654 assertions per edition at this focused gate. These
are actual field reads through `S_InitFlightSupport` and
`S_GetPreparedWaterSupport`, including exponent/overflow inputs, not a helper-only
claim. The strict verifier recomputes every metadata and complete field result
from the original game/Storm binaries. Its union is now4512 game instructions,
10 data spans and the exact Storm binary identity. Sixteen function names/notes
are applied and saved in Ghidra; no prototype was changed.

The final integration replay and full checkpoint completed on this corrected
source as recorded below. No TODO is closed by this delivery; the finite arena,
clock-producer and fresh-process/rendering exclusions retain their existing
FOOT-04.2, NUM-02.3 and E2E-05.2 owners.

The corrected strict integration replay passes472 tests/2000479 assertions in
each edition, including the full `wc3_unit.*` gate and both metadata tests.
The22 evidence-contract tests,38 corpus tests and12 exclusion-owner tests pass.
The successful full checkpoint freezes this corrected source; the first
rejected checkpoint remains failure evidence.

## Accepted recovery checkpoint

The actual `make -j16 test` process exits0 on source tree
`e72eccec586de70f2d602b6356398694f1962731`:34 JUnit reports contain9992 native
tests, zero failures/errors/skips, and all2934 Python tests pass. The complete
strict native/engine replay passes472 tests/2000479 assertions per edition.
All three preserved Captain473/Group012/Waygate377 shared-code replays pass.
The root workspace received exactly that tested tree before documentary
acceptance records were updated. Production/test modules build without warnings.
Reports, make PIDs/terminal exit records and accepted source hashes are under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/codex-takeover-20261020/`:
`support-metadata-full-checkpoint-status.json`, `support-metadata-accepted-checkpoint.json`
and `support-metadata-integration-strict.json`.

No leaf closes:311/336 complete,25 open. Continue the existing finite-input,
clock-producer and restoration/rendering owners rather than inferring complete
pathfinding parity from these accepted journeys and producer kernels.
