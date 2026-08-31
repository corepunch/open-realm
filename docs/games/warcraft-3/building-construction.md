# Warcraft III Building Construction

## Contract

Human construction is server-authoritative. `UnitProfile.Builds` remains the source of which structures a worker can offer; a client cannot select an arbitrary unit rawcode and bypass that list. Training follows the same authority rule through `UnitProfile.Trains`: command-card visibility and the eventual `button <rawcode>` request both re-evaluate the producer list, per-player technology maximum, and target requirements before a unit can enter the queue. `games/warcraft-3/game/g_building.c` owns the shared technology/requirement checks as well as building resource availability and placement validation.

The runtime developer override is:

```sh
+set wc3_build_all 1
```

It makes every structure or trained unit already present in the selected producer's final `Builds` / `Trains` list available regardless of technology maximums or `Requires`. Structure construction also keeps the existing debug behavior of bypassing gold, lumber, and food charges; training continues to use its normal `player_pay()` resource check. It does **not** invent commands outside `Builds` / `Trains`, bypass building classification, or relax map bounds, terrain/static pathing, live-unit occupancy, or build-on-target rules. This keeps the cheat useful for tech-tree testing without allowing invalid production commands or world placement.

## Build-menu data flow

```text
UnitProfile.Builds
    -> G_GetBuildCommandState
       -> SetPlayerTechMaxAllowed state
       -> Requires / Requiresamount
       -> current gold/lumber/food
    -> ui_builds
       -> hidden at tech maximum
       -> disabled with reason when requirements/resources fail
       -> available otherwise
```

`SetPlayerTechMaxAllowed`, `GetPlayerTechMaxAllowed`, `AddPlayerTechResearched`, `SetPlayerTechResearched`, `GetPlayerTechResearched`, and `GetPlayerTechCount` now have game-state backing rather than no-op JASS stubs. W3I technology-availability entries initialize a player's matching technology maximum to zero before map script execution; map JASS can subsequently change that state.

`G_GetPlayerTechCountValue()` counts researched levels plus live owned entities of the requested rawcode. In-progress structures therefore count against a maximum as soon as they exist.

`-1` is the unlimited/default sentinel for `SetPlayerTechMaxAllowed`; non-negative values are exact maxima. Starting a queued unit spawns its hidden entity immediately, so it also counts against the maximum before training completes. The queued entity carries `edict.training` until `ShowTrainedUnit()` succeeds; requirement counts exclude both `construction.active` structures and `training` units so in-progress production cannot satisfy a prerequisite early.

Training command flow is:

```text
UnitProfile.Trains
    -> G_GetTrainCommandState
       -> SetPlayerTechMaxAllowed state
       -> Requires / Requiresamount
    -> G_GetCommandButtons
       -> hidden at tech maximum
       -> disabled with requirement text when prerequisites fail
       -> available otherwise
    -> SP_TrainUnit
       -> re-check the same state before payment/spawn
```

Technology/count changes mark the owner's command card dirty instead of pushing UI from inside arbitrary JASS/entity callbacks. `G_UpdateClientCommandCards()` consumes that flag after entity simulation, while the initial `G_ClientBegin()` command-card write clears any dirty state accumulated by W3I or map-init JASS before the game HUD is shown. Build/skill submenus retain a refresh callback so a tech update rebuilds the current submenu rather than forcing the main card; active location/entity targeting defers the refresh until that input mode is resolved so cursor state is not stranded. Runtime spawns, ownership changes, removals, deaths, construction start/completion, and training completion invalidate affected command cards.

## Placement

`G_SnapBuildingPoint()` implements the WC3 pathing-grid alignment used by both authoritative placement and the client ghost:

```text
base lattice = 64 world units
odd half-pathing-map dimension -> +32 on that axis
no authored pathing texture -> 32-unit fallback grid
```

The placement cursor carries its authored pathing-texture width/height in dedicated `entityState_t.pathing_width` / `pathing_height` fields. `client/cl_view.c:CL_AddBuilding()` uses those dimensions for the same visual snap. The fields are delta-serialized and have an explicit network round-trip test; world-position fields remain world position only.

`G_EvaluateBuildPlacement()` checks:

- building classification;
- snapped pathing footprint inside the map path grid;
- `UNBUILDABLE` and `UNWALKABLE` static pathing;
- normalized `preventPlace` (`unwalkable`, `unbuildable`, `blighted`);
- normalized `requirePlace` for the same supported flags;
- static building/destructable footprints already baked into `pathmap.original`;
- live unit-circle occupancy, excluding the builder;
- exact build-on-target presence when `isBuildOn` requires a `canBuildOn` structure.

The common routing layer exposes `CM_GetPathingFlagsAt()` specifically so placement can read the immutable terrain + baked-static pathing result without mutating movement's dynamic path map.

A declared pathing texture that fails to load is a placement failure. Do not silently degrade such a structure to a one-cell footprint; `M_LoadPathTex()` already reports the missing asset.

Placement is checked once when the player confirms the ghost and again when the worker reaches the site. Resources are charged only after the arrival-time validation passes. Construction spawns at the stored snapped waypoint, not at the worker's current position. Placement rejection uses the plain UI message `Unable to build there.`; requirement/resource failures remain separate command-state messages. `G_LevelString()` resolves only exact `TRIGSTR_<id>` tokens. Previously it parsed arbitrary UI text as trigger-string ID 0, so Human02's WTS entry 0 (the map name) replaced ordinary errors with `Human02`.

### Placement cancellation

Build placement is a server-owned UI mode. `G_CancelBuildPlacement()` is the single teardown path: it clears the player's pending `build_project`, sends an empty `svc_cursor` so the client removes the ghost model, and restores the normal command card. The command-card `CmdCancel`, the gameplay `cancel` command, and both `smart`/`smartpoint` right-click paths use this teardown. A right-click while the build ghost is active is therefore consumed as cancellation and must not issue an order to the selected worker.

A successful left-click copies the pending project to the worker order before clearing the player's placement cursor state. This avoids stale `build_project` state making a later right-click look like an active placement.

The current client ghost snaps correctly but does not yet render the full per-cell green/red placement texture. Live units are therefore authoritative server blockers but are not painted into the preview.

## Human construction and power building

A successfully started Human structure uses explicit construction state on the building:

```text
construction.active = true
construction.paused = true
construction.primary_builder = original Peasant
construction.progress = 0
life = 10% max life
```

The building's birth animation is held until construction completes. Its authored pathing footprint is baked before the primary Peasant is relocated. Human Repair uses `SP_FindUnitExitPosition()` against that baked footprint and relinks the worker at the chosen point; selection radius is not a substitute for pathing geometry. This prevents the Lumber Mill case where the old `SP_FindEmptySpaceAround()` placed the Peasant inside cells that became blocked immediately after construction started. The primary Peasant then contributes `1.0` construction time. If that Peasant stops/dies/receives another behavior, no autonomous timer advances the paused structure. A later Human Repair worker can become the new primary worker.

Additional `Arep` repairers use the ability's authored data:

- `DataC` -> incremental power-build cost ratio;
- `DataD` -> additional construction-time contribution.

Each additional worker independently contributes:

```text
progress += frame_time * DataD
```

and accumulates incremental gold/lumber cost from the building's `goldRep` / `lumberRep`, build time, and `DataC`. An additional repairer stops when that incremental payment cannot be made. `+set wc3_build_all 1` suppresses those debug-time costs.

Ordinary repair aliases remain separate and do not acquire Human power-building behavior automatically.

Completion clears paused/constructing state, releases the held birth animation, restores full life, applies positive `foodMade`, publishes `EVENT_PLAYER_UNIT_CONSTRUCT_FINISH`, and refreshes resource/HUD state.

## Known gaps

The following clean-room-spec items remain incomplete:

- `war3map.w3u` modifications are not yet fully merged into the normalized typed unit rows, so map-local edits to `Builds`/requirements may still resolve through the base unit row;
- W3I upgrade-availability records are parsed but are not yet applied to a complete research/upgrade production system;
- `SetPlayerAbilityAvailable` remains separate from unit/building technology availability and is not yet backed by per-player disabled-ability state;
- hero training still lacks the additional hero-count/tier/token rules layered on top of normal `Trains`/maximum/requirement checks;
- training still uses the legacy `player_pay()` gold/lumber charging path; full food reservation/refund and queue-cancel economics are separate work;
- the client does not yet draw a per-cell green/red pathing splat or mirror live-unit obstruction into that splat;
- placement supports the currently decoded walk/build/blight flags, not every Warcraft compound placement type; unsupported tokens are reported to `stderr` instead of being silently discarded;
- build cancellation after a structure has spawned does not yet have the retail partial-refund lifecycle;
- Orc worker-inside, Night Elf worker/Ancient consumption, and Undead summon/release construction strategies remain legacy behavior;
- normal completed-building Repair still uses the older simplified HP rate; DataA/DataB repair cost/time parity is separate from Human power-building DataC/DataD;
- the per-player technology table is intentionally bounded at `MAX_PLAYER_TECH_STATE`; exhaustion is reported as a warning and does not overwrite an existing entry;
- `GetPlayerTechResearched` / `GetPlayerTechCount` currently have exact-rawcode semantics for both `specificonly` values because technology-equivalence groups are not represented yet.

## Verification

Focused automated checks after building:

```sh
make test-wc3-engine WC3_PATTERN='wc3_building.*'
make test-wc3-engine WC3_PATTERN='wc3_combat.*'
make test-wc3-engine WC3_PATTERN='wc3_movement.*'
make test-wc3-engine WC3_PATTERN='wc3_jass_map.player_technology_*'
make test
```

Runtime checks should cover at least:

1. Peasant Build submenu only exposes its `Builds` entries.
2. A `SetPlayerTechMaxAllowed(..., 0)` structure disappears; `+set wc3_build_all 1` restores it.
3. A trainable unit hidden by a tech maximum or disabled by `Requires` becomes available with `+set wc3_build_all 1`, while units absent from that producer's `Trains` list remain absent.
4. Missing structure `Requires` or resources disable the button; the cvar bypasses those structure gates.
5. Farm/Barracks ghost and spawned structure use the same 64/32 alignment.
6. Terrain, tree/building static pathing, map edges, and live units reject placement.
7. Obstruction introduced while the Peasant walks causes arrival-time rejection without charging resources.
8. One Peasant constructs at the base rate; stopping it pauses progress.
9. A replacement Peasant resumes construction.
10. Additional Peasants accelerate according to Repair `DataD` and consume incremental `DataC` costs.
11. Build a Lumber Mill and then order its primary Peasant away; the worker must begin outside the baked footprint and move normally.
12. Reject a blocked placement and verify the UI displays `Unable to build there.`, not the map name.
13. Select a building, right-click terrain and a unit, and verify placement cancels without moving/ordering the Peasant.
14. Select a building, press the command-card Cancel button, and verify the ghost model disappears when the cursor-clear message is processed.
15. Set a trainable unit maximum to zero and verify its button disappears; set the maximum back to `-1` and verify it returns.
16. Set a trainable unit prerequisite unmet/met and verify its button is disabled/enabled without changing the producer's `Trains` list.
17. With maximum one, queue the unit in one producer and verify another producer hides the command while the first unit is still training.
