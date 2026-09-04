# Galaxy Scripting

## Contract

The SC2 game module owns Galaxy lifecycle through `games/starcraft-2/game/galaxy/galaxy_host.c`:

1. `galaxy_open` creates the JASS VM and loads `data/TRaynor01-galaxy/MapScript.galaxy`.
2. Galaxy `include` directives load NativeLib, LibertyLib, and CampaignLib once per VM.
3. `galaxy_start` calls `InitTriggers`; `InitGlobals` and `InitLibs` remain skipped.
4. `SC2_ClientBegin` calls `galaxy_fire_mapinit` after the local client enters the map.
5. `SC2_RunFrame` calls `galaxy_tick` to resume yielded trigger coroutines.
6. `SC2_Shutdown` calls `galaxy_close`.

Trigger functions use the Galaxy signature `bool function(bool testConds, bool runActions)`. The host compiles a no-argument wrapper before starting a coroutine. Dynamically compiled wrappers belong to the root VM. `TriggerExecute(..., waitDone=true)` pushes that wrapper onto the active coroutine; a child `Wait` therefore preserves the child frame and resumes the parent only after the child returns. Calling the wrapper through the synchronous expression evaluator loses the yielded child frame and advances the parent to the next trigger prematurely.

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
make run-sc2 ARGS="+map Maps/Campaign/TRaynor01.SC2Map +vid_hidden 1 +com_frame_limit 200"
```

A successful lifecycle run registers 127 triggers and executes:

- `gt_Initialization_Func`
- `gt_Init01Technology_Func` through `gt_Init07Help_Func`
- `gt_IntroQ_Func`
- intro setup, cinematic, cinematic end, and cleanup
- `gt_StartGame_Func`

It must exit from `com_frame_limit` without an infinite-loop assertion or memory fault.

The intro does not end at camera `976`. Its eight-second interpolation overlaps `TRaynor01Raynor00028` (4.82 seconds), while the
dropship flies from point `379` to point `1037`. It unloads Raynor and five Marines there, then departs toward point `1038`; cleanup
starts gameplay. After the camera reaches its endpoint, `gt_OpeningLineQ_Func` plays `TRaynor01Raynor00030` (3.84 seconds) before
the new-unit, hero-game, and story-mode tip triggers run. Confirm that ordering with:

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

## Remaining Gaps

The bounded intro lifecycle loads all 2,657 TRaynor01 objects with `SC2_MAX_MAP_OBJECTS` set to 4,096. Camera IDs 1660 and
976 and the intro route points resolve from the authoritative map `Objects` data; both camera applications reach the game
state callback. Camera 1660 applies instantly, then camera 976 interpolates over its authored eight-second duration. A bounded
TRaynor01 run confirmed start/mid/end eye clearances of 17.27, 22.87, and 28.19 world units above terrain respectively.

Remaining native coverage gaps:

- `CampaignMode` is a no-op stub; TRaynor01 `gt_Initialization_Func` calls it and an unimplemented native aborts the intro before `CameraApplyInfo`.
- `CinematicMode` only updates game-local state; it does not hide the gameplay layout or select `CLIENT_UI_CINEMATIC`;
- `CinematicFade` applies its final alpha immediately and ignores both interpolation and `waitUntilDone`, so the script reaches its
	one-second wait two seconds earlier than native SC2;
- multidimensional Galaxy arrays use nested sparse VM arrays; every authored index is preserved for reads and writes;
- `ObjectiveCreate` is not currently resolved on the start-game path;
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
