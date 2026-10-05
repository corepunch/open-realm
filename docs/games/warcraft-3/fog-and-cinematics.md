# Warcraft III Fog And Cinematic Visibility

> This document covers gameplay fog of war (exploration/current sight). Atmospheric distance mist from `SetTerrainFogEx` is a separate renderer/environment system; see [Environmental Terrain Fog](environmental-fog.md).

## Contract

Camera position, fog state, and entity visibility are independent systems. Camera natives move the gameplay camera without changing
fog, while fog natives can reveal or mask remote terrain without moving the camera.

OpenRealm represents Warcraft's three fog states with the existing per-player `explored` and `visible` planes:

| JASS state | `explored` | `visible` | Meaning |
| --- | ---: | ---: | --- |
| `FOG_OF_WAR_MASKED` | 0 | 0 | unexplored |
| `FOG_OF_WAR_FOGGED` | 1 | 0 | explored without current sight |
| `FOG_OF_WAR_VISIBLE` | 1 | 1 | explored with current sight |

`G_FowClearVisible()` clears current visibility without clearing exploration. A temporary visible reveal therefore naturally becomes
fogged after the modifier stops when no normal sight source still covers the area.

## Direct Trigger Reveals

`SetFogStateRect`, `SetFogStateRadius`, and `SetFogStateRadiusLoc` write directly into the authoritative player fog grid through
`G_FowSetStateRect()` and `G_FowSetStateRadius()`.

- `MASKED` clears `visible` and `explored`.
- `FOGGED` clears `visible` and sets `explored`.
- `VISIBLE` sets both planes.

The operations reuse the existing server fog-grid resolution and dirty-row network path. They do not maintain a second cinematic
exploration map. With `useSharedVision`, the same state is also applied to viewers receiving the source player's shared vision.

## Per-Unit Shared Vision

`UnitShareVision(unit, player, true)` adds that player to the unit's persistent `shared_vision` recipient mask. During
`G_FowUpdate()`, the unit's ordinary day/night sight is rasterized for the union of its normal owner/alliance viewers and
its explicit per-unit recipients. Passing `false` removes only that explicit relationship. This is deliberately separate
from `ALLIANCE_SHARED_VISION`: sharing one unit does not expose the owner's other units or change alliance state.

The relationship is saved with the unit, follows the unit as it moves, and is idempotent because it is represented as a
player bit rather than a reference count. Removing the unit naturally removes the sight source. `UnitShareVision` does not
create a fog modifier, force camera movement, or grant true sight/detection; entity invisibility continues through the
existing per-viewer visibility policy. The NightElf06 `UnitShareVisionBJ(true, gg_unit_Utic_0055, udg_Player)` campaign
reveal therefore works through the generic sight pipeline rather than a Tichondrius-specific exception.

## Fog Modifiers

`CreateFogModifierRect`, `CreateFogModifierRadius`, and `CreateFogModifierRadiusLoc` create disabled modifier handles.
`FogModifierStart` applies the modifier once immediately and then makes it participate in `G_FowUpdate()`; `FogModifierStop` removes it. The synchronous first application is required for map scripts that start and stop/destroy a `VISIBLE` modifier in the same trigger turn to permanently explore an area without holding current sight open.

Started modifiers apply the same three-state cell writer after normal unit sight:

- `VISIBLE` keeps terrain explored and currently visible;
- `FOGGED` keeps terrain explored while suppressing current sight in the modifier area;
- `MASKED` keeps the modifier area unexplored while active.

Stopping a `VISIBLE` modifier stops forcing current vision but does not erase the exploration it already created, so the normal
`VISIBLE -> FOGGED` transition is preserved.

## Camera And Consumers

Camera movement does not call the fog API, and fog state changes do not call camera APIs. Maps that want a cinematic pan and reveal must
request both actions explicitly.

The server's existing `G_FowPlayerCanSeeEntity()` remains the foreign-entity visibility gate. Known gameplay invisibility is evaluated there per viewer: owners/shared-vision allies keep access to their invisible units, while hostile viewers need a detector covering the target. Permanent Invisibility (`Apiv`) and Shadow Meld (`Ashm`/`Ahid`) have dedicated saved state; Sorceress Invisibility/Wind Walk and hidden wards retain `RF_HIDDEN`, but only those recognized invisibility states participate in this visibility policy. Snapshot customization clears `RF_HIDDEN` only for a viewer who is allowed to see that invisible entity, leaving the authoritative entity flag untouched. This deliberately avoids treating cargo, mine workers, revival/training placeholders, and other non-invisibility uses of `RF_HIDDEN` as revealable. The same viewer-specific predicate gates selection, automatic hostile acquisition, attack continuation, and unit-target spell validation. Far Sight's saved timed thinker and passive detector abilities contribute true sight through the shared query.

The client receives current and explored fog as separate planes, so world fog and minimap fog consume the same authoritative state while the minimap camera box continues to derive from camera state independently.

Day/night sight-radius selection consumes the server-owned clock documented in [time-of-day.md](time-of-day.md); fog must not derive a
second clock from `level.time`.

## Known Gaps

This change intentionally does not alter unrelated compatibility areas that need broader evidence:

- `FogEnable` / `FogMaskEnable` retain their existing global behavior.
- Ghost/Ghost Visible are not yet normalized into the player-aware invisibility path. Shadow Meld now uses dedicated unit state with the same owner/shared-vision/detector visibility query as Permanent Invisibility, Sorceress Invisibility, Wind Walk, and explicitly hidden ward mechanics.
- `FOW_CELL_SIZE` is unchanged.
- Camera Z-offset native parity is separate from fog-state handling.

## Verification

The WC3 API tests cover:

- all three direct fog states through rect/radius/radius-location natives;
- `useSharedVision` propagation;
- per-unit `UnitShareVision` recipient isolation, revocation, idempotence, and save persistence;
- a same-turn `VISIBLE` start/stop still recording exploration;
- a temporary `VISIBLE` modifier falling back to `FOGGED`;
- active `FOGGED` and `MASKED` modifiers;
- Far Sight true sight and player-local Permanent Invisibility visibility are covered by the spell/ward regression suite.

Runtime campaign validation should additionally check that a camera-only pan into unexplored terrain remains masked and that a
cinematic reveal can show a remote area without coupling camera movement to fog mutation.

## Ordered source checkpoints

`G_FowUpdate` retains per-source geometry and adds ordered prefix checkpoints for
ordinary unit sight. A checkpoint is the complete byte visibility plane and row
mask after every source in its edict block has committed its blocker rim. It
contains neither scripted modifiers nor timed spell reveals; those retain their
original position after unit sight.

Exact source-input comparison records independently dirty blocks for each viewer.
Evaluation restores the preceding clean prefix, replays dirty sources in their
original order, and compares the completed output with the old checkpoint. If
those outputs match, unchanged later blocks can be skipped until the next dirty
block. It never supplies an earlier source with a later source's visibility.
Applying a saved prefix uses ordinary visibility/exploration writes, preserving
scripted exploration masking and both dirty-row planes.

Block size starts at 256 edict slots and doubles until all possible checkpoints
fit within 2 MiB per viewer (32 MiB for all 16 viewers). Storage is allocated only
for reached checkpoints. A map whose single plane exceeds that budget uses the
exact full replay. Actual blocked-cell changes, source insertion/removal and viewer membership
changes conservatively invalidate affected prefixes. A changed blocker witness
with an identical blocked-cell grid preserves sight (damage to a surviving tree
or an overlapping blocker, for example). Appending a non-revealing entity does
not invalidate sight; partial checkpoint endpoints are validated on replay.
All-moving or nonconverging inputs can still require a full ordered replay; this
is not an approximation or a promise of constant fog cost. Checkpoints are
derived state, released with fog state and reconstructed after loading.

`wc3_game.fow_prefix_checkpoints_match_full_ordered_replay_across_dirty_blocks`
compares all five visibility/exploration/row planes against the original
rasterizer from the same initial state. Its 800 sources span multiple blocks;
mutations include separated moving sources, sight-radius changes, blockers,
scripted exploration masking, death, shared vision, insertion, removal and owner
changes. Test counters separately verify that clean source blocks were skipped.

Runtime310 validation: the focused fog suite passes 10,699 assertions in 15 tests
in both Classic and TFT. The full engine suite passes 2,625 tests and 6,879,123
assertions in each mode. `perf310-icecrown-raw-spawn-and-move-4096.jsonl` retains
exact source221 final positions, membership and RNG on the populated IceCrown
map. That dedicated capture does not establish rendered fog performance; the
rendered capture still fails presentation deadlines.

The runtime310 same-binary rendered A/B probe (`perf310-fog-ab.py` beside the
reports) times `G_FowUpdate` using native Frida entry/exit hooks. The disabled arm
sets the derived `fow_prefix_block` to zero before fog updates; it preserves the
same rasterization and other caches. Median fog CPU was 9.901318 ms enabled and
9.670793 ms disabled; this pair does **not** establish a speedup. Both captures
still miss presentation deadlines. The corresponding `perf310-icecrown-fog-`
`prefix-4096.jsonl` and `full-replay-4096.jsonl` are diagnostic captures, not raw
constructor or deadline acceptance. Follow-up runtime311 removes invalidation
when a blocker-record edit leaves the actual blocked-cell grid unchanged.

Runtime311's unchanged-occlusion follow-up passes all 10,709 assertions in 16 fog
tests in both data modes. The final rendered diagnostic
`perf311-icecrown-fog-prefix-4096.jsonl` measures median fog CPU 9.436413 ms and
maximum 105.944531 ms; its maximum presentation interval is 186.088622 ms.
It still fails the frame budget. The unchanged-blocker cases now avoid source
replay entirely in the regression fixture, but this capture does not establish
a substantial overall performance win. Pure geometry construction, authoritative
simulation cost and presentation scheduling remain outstanding work.

## Exact ordered word evaluation

The unit-sight pass maintains a world-aligned bitmap with 64 horizontal cells per
word for each active viewer. It describes visibility accumulated **so far in
source order**, not final visibility. Cell writes, packed eight-cell writes and
restored prefixes update it. It is cleared before each pass, excluded from
persistence, and never queried across scripted modifiers or timed reveals.

Compiled source geometry owns two aligned masks: unconditional base cells and
conditional blocker-rim candidates. A source is an identity operation if its base
is already visible and no unseen rim candidate has a visible cardinal neighbor.
Without an initial eligible rim candidate, propagation cannot start. A cold
source may also skip geometry entirely when its conservative rectangle is
already visible. The first contributor still executes normally; later sources
cannot justify an earlier skip. Exploration is reapplied during prefix restore,
so scripts may mask it between passes.

For a contributing cached source, base masks are merged a word at a time. Only
new bits are enumerated into byte visibility/exploration planes. Rim evaluation
runs in ascending Y/word order. Each word computes seeds from above, below and
its incoming horizontal neighbors, then propagates **only to the right** through
uninterrupted candidate runs using shifts of 1, 2, 4, 8, 16 and 32. The next word
sees the previous word's completed output; the next row sees the previous row's
completed output. Newly revealed right-hand cells never retroactively reveal a
missed cell to their left. This preserves the old ascending scalar scan rather
than performing an undirected flood fill.

The old temporary rim value 2 was only observed as nonzero within this pass.
Committing its word result to value 1 preserves subsequent neighbor tests and
produces the same visibility, exploration and dirty rows at the source boundary.
The original scalar rasterizer remains the independent test oracle. Exhaustive
8-bit candidate/seed combinations and deterministic full-width randomized cases
also compare the word operator directly to an ascending scalar scan; a separate
predicate test checks cardinal neighbors across word and map boundaries.

This replaces repeated per-cell mask/rim processing with word operations plus
enumeration of newly visible cells. Bitmap memory is
`ceil(width / 64) * height * 8` bytes per viewer; source base/rim masks use their
clipped row extent. Unique cold geometry still requires real rasterization.
No visibility update is dropped, delayed to another simulation tick or based on
worker completion time.

Historical experiments: runtime313's rectangle-only certificate skipped just
35 sources against 102,521 source evaluations on IceCrown. Runtime314's union of
all possible writes was also too conservative: unseen rim candidates can be
inert. The runtime313/314 same-window pair measured median fog CPU of 8.084270 /
8.614672 ms, so neither experiment established a performance win. Runtime315's
separate base/rim identity predicate passed 110,976 scalar-neighbor checks and
the 17 existing fog regressions before the runtime316 row-word evaluator replaced
the tile representation.


Runtime316 passes 196,514 assertions in the word/predicate tests and 10,933 in
17 whole-fog tests in both Classic and TFT modes. Its paired IceCrown capture
with 4096 added units measured median fog CPU 4.517524 ms against runtime315's
4.505763 ms; cold maxima were 99.377249 and 96.521338 ms respectively. Both missed
presentation deadlines. The row-word replay alone therefore does not establish
a performance improvement on this workload.

The subsequent cold-path implementation records shadowcast geometry directly
into the aligned base mask. It no longer allocates a second local bitmap or a
per-cell rim list, converts between them, or updates viewer state on every
shadowcast visit. The finished base is published once. Rim construction builds
clipped disk spans and intersects them with a shared blocker bitmap, reducing
its per-source square cell scan to row/word operations. The blocker bitmap is
rebuilt only when actual blocked cells change; temporary viewer visibility is
never an input to geometry compilation. Unobstructed disks are also constructed
as word spans. Ordered publication and rim evaluation remain synchronous.


Runtime317's direct construction passes the same 17 fog regressions in both
modes. On the IceCrown rendered diagnostic, median fog CPU is 3.352541 ms and
maximum 58.255559 ms, compared with runtime316's 4.517524 / 99.377249 ms. Peak
presentation interval improves from 165.833142 to 122.454727 ms, but still fails
the deadline gate. These captures establish useful progress, not completion or
retail parity. Runtime318 additionally uses blocker words for the cold source's
obstacle-presence query and retains the original floating-point disk boundary
separately from the integer-distance blocker-rim boundary.


The first runtime318 rendered capture is retained as adverse evidence:
`perf318-direct-icecrown-fog-4096.jsonl` reports 10.176142-ms median fog CPU,
226.338519-ms maximum, and a 417.986757-ms peak presentation interval. Constructor
time also increased to 1978.387515 ms with 158 presentation frames during spawn.
This capture does not establish the cause of the across-pipeline slowdown and
must not be omitted when interpreting the earlier gain. The separate runtime318
raw movement capture creates all 4096 units in 8.098037 ms CPU and exactly matches
source221's final positions, member state and RNG. Raw creation remains above
2 ms, and render acceptance remains open.


Final-source validation: `make -j6 TEST_JOBS=6 test openwarcraft3` passes with
native SDL2. Both Classic and TFT complete 2,630 tests / 7,075,879 assertions,
including the exact integer-rim versus rounded-float-disk boundary fixture.
The eight launcher Python regressions also pass. The normal launcher output and
the separate release benchmark output have both been rebuilt from this source.


The final same-window runtime316/318 pair, after all builds and tests completed,
is `perf316-final-pair-icecrown-fog-4096.jsonl` versus
`perf318-final-pair-icecrown-fog-4096.jsonl`:

| Measurement | Previous geometry | Direct word geometry |
|---|---:|---:|
| Median fog CPU | 4.184831 ms | 3.617416 ms |
| First-update maximum fog CPU | 81.897565 ms | 68.348673 ms |
| Median active server CPU | 38.280777 ms | 39.441367 ms |
| Maximum presentation interval | 150.829728 ms | 129.679286 ms |
| Double-period gaps | 17 | 18 |

This supports a local fog improvement, not a total-server or deadline win.
The final rendered constructor included one presentation checkpoint and measured
16.328199 ms; its separate raw 8.098037-ms measurement remains the comparable
synchronous constructor evidence. The adverse earlier capture remains above.
Final release-specific checks also pass: 196,522 assertions in three word tests
and 10,933 in 17 fog tests, in both data modes.


## Shared ray geometry and transparent spans

The cold source evaluator now owns an explicit geometry context: read-only
blocker cells and row/column bitmaps, world dimensions, immutable ray rows and
one exclusively written source mask. It does not touch viewer visibility,
exploration, RNG or entity state. Ordered base/rim publication remains unchanged.
The original scalar rasterizer remains the differential oracle.

A ray row stores the original floating-point left/right slopes and squared
cell distance once per distance. All sources, radii and octants share it. Rows
are prepared synchronously and released at map shutdown. Binary searches use
the original strict comparisons to find the row's active interval; equality
and neighboring representable floats are tested. Changes to `start` after an
obstacle run retain the original per-cell test inside the interval.

Obstacle-free intervals can emit clipped spans without running the cell state
machine. A transposed blocker bitmap gives vertical rays the same word query as
horizontal rays. The obstacle query includes cells outside the sight circle,
because those cells can still affect shadow recursion. Only the output span is
clipped by squared distance. A clear interval cannot change the wedge or launch
recursion; blocked intervals retain the scalar transition order. Both blocker
bitmaps rebuild together only when actual occlusion cells change.

This changes excluded-column work from a linear scan to a binary search and
changes clear horizontal intervals to word writes. Clear vertical intervals
still write one output word per row. Obstructed intervals and recursive wedges
remain real work; this is not a constant-time visibility algorithm.

Focused release verification passes 213,613 assertions in five `wc3_fow.*`
tests and 10,933 assertions in 17 game fog tests in both Classic and TFT modes.
The clear-disk fixture compares all eight octants to the scalar oracle and
requires zero per-cell geometry visits; slope-boundary tests include adjacent
representable floats. The connected-viewer runtime318/320 pair reduces median
fog CPU from 14.905264 to 4.140197 ms and cold maximum from 221.504292 to
37.786242 ms, with identical final positions, membership and RNG. See
[performance evidence and capture limits](performance.md#october-5-shared-fog-rays-and-ordered-spatial-hashing).
The rendered runtime320 capture still has a 95.811346-ms maximum interval;
visibility improvement is not presentation-budget acceptance.

The final complete repository suite passes, including 2,633 tests and
7,093,714 assertions per Classic/TFT mode. No visibility/exploration,
dirty-plane, placement, membership or RNG mismatch was observed in these
regressions and captures; this remains current-engine preservation evidence,
not proof that all remaining retail fog/pathfinding differences are closed.
