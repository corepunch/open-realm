# Galaxy Scripting

## Contract

The SC2 game module owns Galaxy lifecycle through `games/starcraft-2/game/galaxy/galaxy_host.c`:

1. `SC2_LoadMap` passes its authoritative map directory to `galaxy_set_script_dir`; `galaxy_open` loads its `MapScript.galaxy` through VFS.
2. Galaxy `include` directives load NativeLib, LibertyLib, and CampaignLib once per VM.
3. `galaxy_start` calls `InitGlobals` and then `InitTriggers`. `InitLibs` remains unsupported and emits a startup warning.
4. `SC2_ClientBegin` calls `galaxy_fire_mapinit` after the local client enters the map.
5. `SC2_RunFrame` calls `galaxy_tick` to resume yielded trigger coroutines.
6. `SC2_Shutdown` calls `galaxy_close`.

Trigger functions use the Galaxy signature `bool function(bool testConds, bool runActions)`. The host compiles a no-argument wrapper before starting a coroutine. Dynamically compiled wrappers belong to the root VM. `TriggerExecute(..., waitDone=true)` pushes that wrapper onto the active coroutine; a child `Wait` therefore preserves the child frame and resumes the parent only after the child returns. Calling the wrapper through the synchronous expression evaluator loses the yielded child frame and advances the parent to the next trigger prematurely.

## Event Callbacks

Galaxy event callbacks follow the Warcraft III split. `gevent_s` is the registration; `gameEvent_t` fields copied into `jass_context` are what `GetTriggerUnit`, `GetEventDamage`, and `GetTriggeringRegion` read. Galaxy keeps the same two records in `galaxy_trigger.h`:

| WC3 | Galaxy | Read by |
|---|---|---|
| `gevent_s` subject, trigger, region, range, limit | `sc2evreg_t` | `TriggerAddEvent*` |
| `gameEvent_t.edict` / context unit | `sc2evresp_t.unit` | `EventUnit` |
| context source | `source`, `target`, `cargo` | `EventUnitDamageSourceUnit`, `EventUnitTarget`, `EventUnitCargo` |
| `eventValue` | `amount` when `dmg` is set, else `ival` | `EventUnitDamageAmount`, `EventPlayerProperty`, stage and id readers |
| context point | `x,y,z` with `has_point` | `EventUnitTargetPoint`, `EventUnitDamageSourcePoint` |
| context region / timer | `region`, `timer` | `EventUnitRegion`, `EventTimer` |
| context player | `player` | `EventPlayer` |

A null `unitref` matches every unit. Player `-1` is `c_playerAny`. Nested publishes push the response and restore the outer `Event*` values and `TriggerGetCurrent` when the inner callback returns. Disabled triggers do not run. `TriggerGetExecCount` counts fires.

`galaxy_tick` is the scheduler. Each call advances the game and real clocks by one server frame (`FRAMETIME`, 100 ms). `AITimePause` freezes only the AI clock. Elapsed, periodic, and timer callbacks fire from that tick. Game speed does not scale the clocks yet; both game and real time step with the server frame.

These producers fill a response and run matching callbacks:

- `UnitCreate` / `UnitCargoCreate` create; cargo load and dropship unload are cargo events
- `UnitKill` / `UnitRevive` change life, then publish death or revival
- `UnitRemove`, `UnitSetProperty*`, `UnitIssueOrder`, idle when the order queue drains
- `UnitSetPosition` and the per-tick position sample publish region and range crossings
- `PlayerModifyProperty*` and `PlayerSetAlliance` publish on an actual change

Attack, damage, experience, inventory, selection, ability, progress, chat, dialog, purchase, and key/mouse callbacks store their filters. Nothing in the server publishes them yet, so those `Event*` readers stay empty until a combat or UI path calls `sc2_ev_emit`. `InitLibs` stays off for that reason. A `Wait` inside an event callback does not keep `Event*` data after the yield: the response stack is not copied into the shared coroutine.

`RegionFromId` still returns null. `TriggerAddEventUnitRegion` and `TriggerAddEventUnitRangePoint` log that gap and return without a script error. A script error there aborts `InitTriggers` before `gt_IntroQ_Init`, so the TRaynor01 dropship orders are never registered. A null `abilcmd` is the any-order filter (`gt_UnitMovementCheck` passes `null` as its third registration). The VM stores that literal as a null handle, so order construction and event registration read it as command 0 instead of requiring an integer. `RegionGetCenter` on that unresolved region returns a null point for the same reason. `ConversationDataStateGetValue` returns an integer; a null handle there makes `PlayerModifyPropertyInt` abort `gt_Initialization_Func` before `TriggerExecute(gt_IntroQ)`. Unstored conversation state reads as 0. `UnitSetState` stores every writable `c_unitState*` index from `natives.galaxy` (including targetable 18 and tooltipable 20). Read-only indexes still raise a script error. Rejecting targetable aborted `gt_Init03Units_Func` before the intro. `TimerPause` on a null timer returns without a script error. `InitLibs` never creates `libNtve_gv__GameTimer`, and cinematic mode pauses that slot. Event dispatch calls the trigger with `testConds=true`, so `gt_IntroCargoUnload` can reject cargo that is not in `gv_introUnitCargoStart`.

Console chrome is not a Galaxy animation. `SC2_HUD_BuildFrameForWrite` names the `Birth` sequence and `R_SetEntityAnimFrame` samples its last frame. Resource icons and the minimap are separate layout frames. `UISetFrameVisible` is still a no-op, so the map script does not hide those models.

## VM Lookup

Root globals and functions retain their canonical linked lists for ownership and declaration order, plus root-owned 4096-bucket indexes for lookup. Bucket entries use dedicated `hash_next` links. Local variables remain short linked lists.

Declarations are prepended. Hash insertion must also prepend so duplicate-name behavior remains "latest parsed declaration wins." Coroutine VM states copy the root indexes when created and share the declaration objects.

This index removed the confirmed TRaynor01 startup bottleneck: an instrumented 200-frame run exceeded 2.455 billion function comparisons and 2.210 billion variable comparisons before being killed with linear lookup.

## Runtime Semantics

- A zero-result expression used as a coroutine call argument becomes null, matching synchronous `VM_EvalCall`. Reading `jass_topvalue` without this normalization underflowed the stack in `jass_coroutine_buildlocals`.
- Null equality is value-first: any two null values compare equal even when one has a declared type such as `string` and the other is the untyped `null` literal.
- `StringWord(value, index)` is one-based, splits on C whitespace, and returns null when the requested word does not exist. CampaignLib uses that null sentinel to terminate story-room initialization loops.
- Wrapper compilation clears a previously logged runtime error before parsing so unsupported earlier expressions are not misreported as wrapper parse failures.
- `SoundLink` handles retain the catalog ID and asset index. `SoundLengthSync` and `TransmissionSend` resolve the layered `CSound`
	parent chain, expand the mounted asset path, and derive duration from the OGG sample rate and final PCM granule. Transmission
	duration modes apply default/add/subtract/set semantics to that asset duration; a blocking transmission yields its coroutine and
	submits the resolved OGG to the client.
- Galaxy-created units resolve the full layered `CUnit` record, not only its model. Radius, footprint, flags, and mover class use the
	same path as map-placed units. `Fly` movers steer directly and skip ground collision/pathing; ground movers retain flow-field routing.
- `UnitIssueOrder` preserves `c_orderQueueReplace` and `c_orderQueueAddToEnd`. Appended transport and departure orders remain queued
	until the active move reports idle. `SpecOpsDropshipTransport` then empties cargo into a compact two-row formation before departure.
- Flying-unit Z comes from the mover's broad `Air` height surface plus the resolved `CUnit.Height`; ground units use exact terrain.
	`SpecialOpsDropship` authors `Mover="Fly"`, `PlaneArray[Air]=1`, and `Height=3.75`. A bounded TRaynor01 trace found the old path at
	zero terrain clearance (`Z=0.0-0.4`) inside the ravine. Exact terrain plus `Height` still descended below its roughly `8.0` rim.
	The broad Air surface plus `Height` kept the same XY route at `Z=9.2-10.1` across the gap. `t3SyncHeightMap` is not the Air surface:
	sampled values followed the depression down to `-0.08`, so do not substitute it for the broad-height query.

## Diagnostic Workflow

Run the isolated VM/native suite:

```sh
make test-galaxy
```

Run the TRaynor01 lifecycle with a bounded frame count:

```sh
make run-sc2 ARGS="-com_fast_forward +vid_hidden 1 +com_frame_limit 1500"
```

With the inspected map and current partial library startup, the bounded simulation registers 127 triggers and executes:

- `gt_Initialization_Func`
- `gt_Init01Technology_Func` through `gt_Init07Help_Func`
- `gt_IntroQ_Func`
- intro setup, cinematic, cinematic end, and cleanup
- `gt_StartGame_Func`

It must exit from `com_frame_limit` without an infinite-loop assertion or memory fault. A plain 200-frame run is only a startup
probe: frame counts without fast-forward do not guarantee enough simulation time for dialogue waits.

The intro does not end at camera `976`. Its eight-second interpolation overlaps `TRaynor01Raynor00028` (4.82 seconds), while the
dropship flies from point `379` to point `1037`. It unloads Raynor and five Marines there, then departs toward point `1038`; cleanup
starts gameplay. After the camera reaches its endpoint, `gt_OpeningLineQ_Func` plays `TRaynor01Raynor00030` (3.84 seconds) before
the new-unit, hero-game, and story-mode tip triggers run. For a real-time trace (callback names require the debug build below):

```sh
make run-sc2 ARGS="+map Maps/Campaign/TRaynor01.SC2Map +set r_vsync 1 +vid_hidden 1 +com_frame_limit 1200" 2>&1 \
	| grep -E "SC2 camera move:|TransmissionSend:|SC2_GalaxyPlaySound:|gt_(OpeningLineQ|TipUnitNewUnitMarinesQ|TipThisisnotaherogameQ|TipStoryModeQ)_Func"
```

## Isolated Regressions

`tests/test_galaxy.c` covers each confirmed failure independently:

- `vm_indexed_root_lookups`: 5000 functions/globals, exceeding the bucket count and exercising collision chains.
- `vm_null_equality`: typed-null versus untyped-null comparison and the non-null inverse.
- `vm_string_word`: indexed word extraction and the missing-word sentinel.
- `vm_string_word_loop_terminates`: the CampaignLib `while (true)`/`StringWord` sentinel pattern in coroutine mode.
- `vm_sound_link_length`: sound IDs and asset indexes reach the catalog-duration callback, including the unresolved inverse.
- `vm_coroutine_void_argument`: zero-result nested arguments cannot underflow the coroutine stack.
- `vm_coroutine_executes_dynamic_trigger`: a wrapper compiled after coroutine creation yields in a wait-done child and finishes before its parent resumes, even after an earlier logged VM error.

## Protected Calls and Missing-Native Inventory

Unknown functions and declared but unbound natives use the same protected runtime-error boundary. They terminate the current
synchronous call or coroutine (including waiting parents), leave other coroutines runnable, and report a warning through the host.
They do **not** fabricate a return value and continue the broken callback. Global-initializer evaluation has a protected boundary too.
String/integer/boolean/code native argument mismatches raise script errors instead of asserting the process. C memory errors and
unrelated engine assertions are not exceptions caught by this mechanism.

`jass_missingcount` / `jass_missingname` expose unique unresolved names for the VM lifetime. `jass_rterror_clear` clears only the
latest error; it does not erase this inventory. `galaxy_close` prints each name as `galaxy: missing function: NAME`. A later run can
reach additional gaps after the first failure in a callback is implemented. Do not enable the removed `BZ_LENIENT_NATIVES` path or
replace missing bindings with success-returning stubs: either would allow invalid dependent script actions to run.

The full [native coverage guide](galaxy-native-coverage.md) contains archive extraction commands, audit limitations, the complete
Markdown inventory, and a proposed implementation order. Objective/actor contracts and conversation schemas live in
[Galaxy presentation state](galaxy-presentation.md).

## TRaynor01 Post-Intro Failure (September 2026)

The original bounded trace showed two independent bugs:

- `ObjectiveCreate` was unknown; `VM_EvalCall` only recorded an error and returned no value, allowing its caller to continue.
- `libNtve_gf_AttachActorToUnit` then called `ActorCreate` with a live `actorscope` as argument one. The host incorrectly read
  that argument as a string. The native signature in the mounted `TriggerLibs/natives.galaxy` is
  `ActorCreate(actorscope, string actorName, string content1, string content2, string content3)`.

The follow-on blockers were `ActorScopeKill` after the opening dialogue and `ConversationDataStateImagePath` while creating tips.
[Galaxy presentation state](galaxy-presentation.md) records their native ABI, ownership, schema, lifetime, and remaining limitations.
Tests also exposed two independent issues: `IntToText` returned an integer placeholder instead of text, and comparing two distinct
opaque handles dereferenced a missing JASS type declaration in `var_eq`.

Skipping `InitGlobals` left `gv_p1_USER` at zero instead of its authored value 1 and left objective counters unset. The current startup
restores map globals before trigger registration. Attempting the complete authored `InitMap` stopped at `TriggerAddEventDialogControl` in `libNtve_InitLib`. Dialog, purchase, chat, and key callbacks now store their filters. `InitLibs` stays unwired until a UI path publishes those events and the library's other missing natives are real. A no-op binding is not that contract. See [Event callbacks](#event-callbacks).

The authored startup is `InitMap` → `InitLibs` → `InitGlobals` → `InitTriggers`. Current startup only performs the last two.
`InitGlobals` restoring the script's player number does not fix the separate client/lobby/native-owner mapping described in the
[HUD pipeline](hud-layout-pipeline.md#selectioninfopanel-status).

### History That Explained the Failure

| History reference | Finding |
|---|---|
| `d843c82ed`, `jdo.c` unknown-function branch | Added pending error text to the preexisting return-without-unwind path; logging was not exception handling |
| `93b476f3c`, `galaxy_game.h` | `IntToText` entered this domain split as an integer-zero placeholder |
| `15f5c9b99`, `jdo.c` handle equality | Value-handle comparison accessed `a->type->name`; Galaxy's unregistered opaque types can have a null type |

Use `git show <commit>` and `git log -p -S <symbol> -- <file>` to recover context; line numbers change as the fixes evolve.
Do not infer that the most recent log message caused the next assertion: the captured stack identified `sc2_ActorCreate` directly.

Verification:

```sh
make test-galaxy test-sc2 test-jass-build
make run-sc2 ARGS="-com_fast_forward +set fs_homepath /tmp/ow3-sc2-diagnostic +vid_hidden 1 +com_frame_limit 1500"
```

The 1,500-frame fixed-tick run exits normally after both primary objectives and the opening-line/Marine/unique-unit/story-mode tip
callbacks. `SC2_DEBUG_CUTSCENE` traces confirm two active primary objective IDs and the callbacks through `gt_TipStoryModeQ_Func`.
The suites cover protected nested/synchronous/coroutine calls, recovery and missing-name deduplication, initializer errors, type
errors, objective state/text/lifetime, actor ABI/identity/scope cleanup, and catalog layering from fixture archives.

SC2 unity builds now depend on the game headers: otherwise edits to `galaxy_*.h` did not rebuild `libgame-sc2`, leaving stale native
bindings in a diagnostic run. Temporary investigative logs must be removed; optional traces remain behind `SC2_DEBUG_CUTSCENE`.

## Campaign Local Constants and Native ABI

The September 2026 startup trace found four independent errors:

- CampaignLib's `libCamp_gf_TS_DebugProgressInitMissionPathAverage` and `...Late` declare `const int lv_type`. The local parser
  consumed `const` as a type and `int` as a name, leaving `lv_type = ...` as an assignment to a missing variable. Local declarations
  now consume `const` and retain `TF_CONSTANT` before reading the real type/name, matching global declarations.
- `libCamp_gf_FormatCredits` calls the archive-declared `FormatNumber(int number)`. A newly added native incorrectly read a second
  boolean, aborting `libCamp_gf_TS_DebugBuyTechUpdateCredits`. The binding now reads only the authored integer.
- NativeLib's `libNtve_gf_PlayAnimation` calls `StringReplaceWord(string, string, string, int maxCount, bool caseSens)`. The binding
  read the fourth argument as a boolean and aborted `gt_Init03Units_Func`. It now reads slots four/five correctly and honors positive
  replacement limits. NativeLib's animation/filter conversion calls use zero to replace all separators; `natives.galaxy` also defines
  `c_stringReplaceAll = -1`. Replacement output is sized from the input and expansion bound, avoiding the former silent 1KB truncation.
- `gt_IntroSetup_Func` passes fractional percentage components to `ColorWithAlpha`; integer-byte argument checks aborted the
  cinematic fade. Both color constructors now accept numeric percentages (0..100), convert channels together to the existing packed
  ARGB representation, and preserve explicit alpha zero. `Color` supplies opaque alpha.

The incorrect native signatures entered in `2b33814f8`; the local qualifier omission dates to `d0352ebf8`. Read the mounted
`TriggerLibs/natives.galaxy`, `NativeLib.galaxy`, and `CampaignLib.galaxy` using the extraction commands in
[the native coverage guide](galaxy-native-coverage.md), rather than inferring ABI from a script call or an existing binding.
`make test-galaxy` covers local constants in synchronous/coroutine execution, nested credit formatting, replacement count/case
modes and long output, and percentage-based color construction. The repaired TRaynor01 startup and intro complete a 1,500-tick bounded run without a Galaxy runtime error:

```sh
XDG_DATA_HOME=/tmp/ow3-sc2-state build/bin/opensc2 -data data/StarCraft2 -com_fast_forward \
  +map Maps/Campaign/TRaynor01.SC2Map +vid_hidden 1 +com_frame_limit 1500
```

For real-time camera verification, omit fast-forward and add `+set com_maxfps 60`. An uncapped hidden window can exhaust the frame
limit while waiting for the first server snapshot, without rendering the camera. Hidden windows also lack gameplay input focus;
scripted cinematic movement exercises the rendered camera without requiring keyboard focus.

## Remaining Gaps

The bounded intro lifecycle loads all 2,657 TRaynor01 objects with `SC2_MAX_MAP_OBJECTS` set to 4,096. Camera IDs 1660 and
976 and the intro route points resolve from the authoritative map `Objects` data; both camera applications reach the game
state callback. Camera 1660 applies instantly, then camera 976 interpolates over its authored eight-second duration. A bounded
TRaynor01 run confirmed start/mid/end eye clearances of 17.27, 22.87, and 28.19 world units above terrain respectively.

Remaining native coverage gaps:

- The bounded intro and post-intro start-game sequence runs without a Galaxy runtime error; this does not establish complete mission gameplay.
- `CampaignMode` is a no-op stub.
- `CinematicMode` only updates game-local state; it does not hide the gameplay layout or select `CLIENT_UI_CINEMATIC`;
- `CinematicFade` applies its final alpha immediately and ignores both interpolation and `waitUntilDone`, so the script reaches its
	one-second wait two seconds earlier than native SC2;
- multidimensional Galaxy arrays use nested sparse VM arrays; every authored index is preserved for reads and writes;
- objectives retain IDs, name/description, state, primary and visibility, but their HUD presentation is not yet implemented;
- Galaxy `continue` remains parse-safe fallthrough rather than true loop continuation.

Do not replace missing map IDs or models with guessed defaults. Resolve them from the loaded SC2 map and catalog data.

For opening-shot comparison, enable vsync so screenshot frame delays also advance real time:

```sh
build/bin/opensc2 -data data/StarCraft2 +set r_vsync 1 +vid_hidden 1 \
	+map TRaynor01 +screenshot 90 +com_frame_limit 105
```

Map/Galaxy camera pitch is degrees down from horizontal. `SC2_ViewAngles` converts it to orbit Euler (`pitch - 90`) before the snapshot; camera `1660`'s 34.9 becomes `-55.1`, not a near-nadir 34.9. The intro script applies camera `1660` (`StartGame01`, target `30.183,28.759`, pitch `34.9`, yaw `193.9`, distance `30.2`) before
spawning the dropship at point `379`, then moves toward camera `976` after the fade and a one-second wait. The route points `379`,
`1037`, and `1038` all cluster around camera `1660`, confirming that the map lookup selects the intended opening area. Tests with
the horizontal camera direction rotated by `90`, `180`, and `270` degrees all produced other incorrect map quadrants; do not mask
the incomplete cinematic lifecycle with a yaw offset.

## Reproducing Detailed Traces

Enable the existing compile-time trace without leaving a source-level define behind. `-W` forces the owning source dependency to
rebuild, because changing Make variables alone does not invalidate existing artifacts:

```sh
make -W games/starcraft-2/game/galaxy/galaxy_host.c SC2_DEBUG_CFLAGS=-DSC2_DEBUG_CUTSCENE opensc2
build/bin/opensc2 -data data/StarCraft2 -com_fast_forward +set fs_homepath /tmp/ow3-sc2-diagnostic +map Maps/Campaign/TRaynor01.SC2Map +vid_hidden 1 +com_frame_limit 1500 > /tmp/sc2-galaxy.log 2>&1
rg 'SC2 objective:|gt_(StartGame|OpeningLineQ|TipUnitNewUnitMarinesQ|TipThisisnotaherogameQ|TipStoryModeQ)_Func|runtime error:|missing function:' /tmp/sc2-galaxy.log
make -W games/starcraft-2/game/galaxy/galaxy_host.c opensc2
```

The last command restores the ordinary build. `SC2_GAME_HEADERS` in `games/starcraft-2/game.mk` now ensures header edits also rebuild
the game library. Compile-time flag changes still require an explicit rebuild.

The September 11 verification passed 107 assertions in 78 Galaxy/JASS tests, 705 assertions in 58 SC2 tests, and the JASS header
rebuild check. The bounded process exited with status 0, created two active primary objectives, and reached all five callback names
in the filter above without a Galaxy runtime error. These are recorded observations, not a requirement that future suite counts
stay fixed. Existing asset warnings, unresolved conversation ordinal patches, and other incomplete behavior were not eliminated.

On macOS, the first sandboxed GUI run could not reach a usable GL context: SDL had no display and the drawable was 0×0. That did not
reproduce the Galaxy crash. A run with display-service access reached the actual mission. Do not interpret a headless GL failure as
script evidence. Fast-forward is for script/simulation validation; use wall-clock pacing for rendering, sound timing, input, or
screenshots. Retain the engine's bounded-run flags in either case.
