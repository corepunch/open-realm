# Warcraft III Building Construction

## Contract

Human construction is server-authoritative. `UnitProfile.Builds` remains the source of which structures a worker can offer; a client cannot select an arbitrary unit rawcode and bypass that list. Training follows the same authority rule through `UnitProfile.Trains`: command-card visibility and the eventual `button <rawcode>` request both re-evaluate the producer list, per-player technology maximum, and target requirements before a unit can enter the queue. `games/warcraft-3/game/g_building.c` owns the shared technology/requirement checks as well as building resource availability and placement validation.

Command-card activation is independent of JASS `EnableUserUI`. `button CmdBuild`, `button <unit rawcode>`, and other gameplay button commands must continue through `CMD_Button()` when `EnableUserUI(false)` is recorded; that native suppresses presentation affordances, not gameplay command authorization. A rebase briefly special-cased `client->no_ui` inside `CMD_Button()`, which made the button animate client-side while silently discarding both the Peasant Build submenu and Town Hall training requests.

Build-menu state and command-bar transport also have separate lifecycles. `build_command()` installs its `cmdbutton`/`refresh` callbacks even for a reserved player slot that has not completed `ClientBegin`, but only serializes `LAYER_COMMANDBAR` when `client->connected` is true. This keeps in-engine command tests and pre-connect state changes from touching the server message buffer while preserving the menu state that a connected client will render.

The runtime developer override is:

```sh
+set wc3_build_all 1
```

It makes every structure or trained unit already present in the selected producer's final `Builds` / `Trains` list available regardless of technology maximums or `Requires`. Structure construction also keeps the existing debug behavior of bypassing gold, lumber, and food charges. Training bypasses technology/requirement gates but still performs normal gold/lumber/food availability checks and payment/reservation. It does **not** invent commands outside `Builds` / `Trains`, bypass building classification, or relax map bounds, terrain/static pathing, live-unit occupancy, or build-on-target rules. This keeps the cheat useful for tech-tree testing without allowing invalid production commands or world placement.

Food-only capacity checks have their own runtime override: `+set wc3_food_limits 0`. Unlike `wc3_build_all`, it leaves producer/tech/resource behavior intact and only removes the requirement for sufficient Food Cap/Farms; Food Used remains accounted for upkeep and HUD state.

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
Training queues are linked through each queued entity's `edict.build` pointer. Completion preserves the next queue pointer before calling `ShowTrainedUnit()`, because the reveal path calls `unit_stand()` and standing clears the completed unit's `build` field. The producer then advances to the saved next entry, so completing the head cannot discard later paid queue entries.

Training command flow is:

```text
UnitProfile.Trains
    -> G_GetTrainCommandState
       -> SetPlayerTechMaxAllowed state
       -> Requires / Requiresamount
       -> current gold/lumber/food
    -> G_GetCommandButtons
       -> hidden at tech maximum
       -> disabled with requirement/resource text when unavailable
       -> available otherwise
    -> SP_TrainUnit
       -> re-check the same state before gold/lumber payment and spawn
       -> hidden queue edict
       -> active head reserves Food Used
       -> no supply: progress waits and retries
       -> completion keeps the reservation on the visible unit
```

Only the queue head owns a food reservation; later linked queue entries remain unreserved until they advance. See [Warcraft III Food, Supply, And Upkeep](food-and-upkeep.md) for the food ownership and upkeep lifecycle.
Queue insertion reserves food immediately when the new item is the head. A later head that cannot reserve food remains paid and queued, reports the shortage once, and retries without progress. Queue icons issue `canceltrain <slot>`; cancellation refunds that item's gold/lumber and releases only a reservation actually owned by that hidden queue edict.

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

A successful left-click copies the pending project to the worker order before clearing the player's placement cursor state. Leaving the worker's pre-spawn Build move (Stop, Move, or another replacement order) also clears the worker-owned `build_project`; no structure exists yet and no resources have been charged, so this is a full pre-payment cancellation rather than a construction refund.

The current client ghost snaps correctly but does not yet render the full per-cell green/red placement texture. Live units are therefore authoritative server blockers but are not painted into the preview.

### Spawned Human construction cancellation

Once a Human structure has spawned, cancellation is owned by the unfinished structure rather than by the Peasant's placement mode. `G_GetCommandButtons()` exposes Warcraft's `CmdCancelBuild` while `construction.active` is true, and the active construction icon in the info panel submits the same command. `CmdCancelBuild` resolves through the shared Cancel handler to `G_CancelStructureConstruction()`.

The authoritative lifecycle is:

```text
validate live controlled construction
    -> publish EVENT_PLAYER_UNIT_CONSTRUCT_CANCEL
    -> publish EVENT_UNIT_CONSTRUCT_CANCEL
    -> refund 75% of the recorded base gold/lumber payment
    -> unit_die()
       -> release every Human Repair/power-build worker
       -> clear construction/self-linked queue state
       -> clear food and selection through normal death cleanup
       -> publish ordinary death events
       -> mark the structure dead
       -> rebake static pathing without the dead building footprint
```

The construction record stores the payer and exact base gold/lumber charge at spawn time. This avoids refunding later Human power-build Repair spending and keeps ownership changes from redirecting the refund to the structure's current owner. `wc3_build_all` constructions record no payment and therefore receive no cancellation refund. The refund calculation is isolated in `G_ConstructionCancelRefund()` so integer-rounding parity can be refined independently if retail observation shows a different edge case.

`G_StopConstruction()` is also called when an unfinished Human structure dies to combat. That path releases builders and the construction HUD self-link but does **not** publish construct-cancel events or refund resources. Cancellation therefore remains distinct from enemy destruction while still sharing the normal death, food, selection, and pathing lifecycle.

Static building footprints are rebuilt after building death. `common/routing.c` excludes dead `SVF_MONSTER` entities from static footprint baking even when their authored `pathtex` remains attached for presentation/save state; dead destructables retain their separate death-path-texture behavior. This prevents cancelled or destroyed buildings from leaving invisible route blockers.

## Human construction and power building

A successfully started Human structure uses explicit construction state on the building:

```text
construction.active = true
construction.paused = true
construction.primary_builder = original Peasant
construction.progress = 0
life = 10% max life
```

The building's birth animation is held until construction completes. Human construction is selected from the builder's `Arep` capability, not from `UnitData.race`. Its authored pathing footprint is baked before the primary worker is relocated. Only this initial construction transition uses `SP_FindUnitExitPosition()` against the baked footprint; ordinary Repair orders approach their target through movement/pathfinding and never teleport to the structure. Selection radius is not a substitute for pathing geometry. This prevents the Lumber Mill case where the old `SP_FindEmptySpaceAround()` placed the worker inside cells that became blocked immediately after construction started. The primary worker then contributes `1.0` construction time. If that worker stops, dies, or receives another behavior, Repair teardown clears the primary-builder association and no autonomous timer advances the paused structure. A later Human Repair worker can become the new primary worker.

Additional `Arep` repairers use the ability's authored data:

- `DataC` -> incremental power-build cost ratio;
- `DataD` -> additional construction-time contribution.

Each additional worker independently contributes:

```text
progress += frame_time * DataD
```

and accumulates incremental gold/lumber cost from the building's `goldRep` / `lumberRep`, build time, and `DataC`. An additional repairer stops when that incremental payment cannot be made. `+set wc3_build_all 1` suppresses those debug-time costs.

Ordinary repair aliases remain separate and do not acquire Human power-building behavior automatically. Human power building additionally requires `construction.active && construction.paused` and a positive `DataD`; standard `Aren` / `Arst` Repair rejects an unfinished structure.

Construction HP is additive. Each builder contributes the same fraction of `(max_life - 10% start_life)` as its construction-time contribution, rather than snapping HP back to the value implied by total progress. Damage dealt while a structure is incomplete therefore remains damage until separately repaired or construction completes.

Completion clears paused/constructing state, releases the held birth animation, restores full life, assigns positive `foodMade` to the completed building's per-edict food bookkeeping, publishes `EVENT_PLAYER_UNIT_CONSTRUCT_FINISH`, and refreshes resource/HUD state. Death/removal can therefore subtract the same contribution symmetrically.

## Repair orders

`Arep`, `Aren`, and `Arst` all have a real entity-target Repair command. The exact ability rawcode is retained on the worker's Repair state so custom aliases and the three base Repair definitions read their own `AbilityData` row; Repair data is not kept in shared globals.

For completed owned buildings, the current implementation uses:

```text
HP/sec = max_life / repair_time * DataB

gold/sec = goldRep / repair_time * DataA * DataB
lumber/sec = lumberRep / repair_time * DataA * DataB
```

`repair_time` is `UnitBalance.reptm` when populated. Current ROC/test rows can leave that field at zero, so the implementation has a documented compatibility TODO that retains the previous `buildTime` duration for those rows until normalized unit-data import always exposes authoritative repair time.

Gold and lumber use per-worker floating-point accumulators; only whole accumulated resources are deducted. Affordability is checked before the corresponding HP/progress update. The `wc3_build_all` override continues to suppress incremental **power-build** cost only; it does not make ordinary completed-building Repair free.

Repair range comes from ability `Rng`. A worker outside range enters a walk behavior with the structure as `goalentity`; contact is measured against `CM_DistanceToPathingFootprint()` when authored pathing exists, with the legacy collision-circle distance only as a no-footprint fallback. When in range, the worker uses `stand work`. Leaving Repair through another order or death releases its Repair state and any primary-builder ownership.

Smart/right-click tries Repair on an otherwise non-enemy target before falling back to Move. A full-health target declines Smart Repair silently. Explicit Repair reports `Target is not damaged.` for a completed full-health building and rejects standard Repair on construction.

Repair also participates in the generic autocast hooks documented in [autocast.md](autocast.md). Right-clicking the Repair command button sends the secondary `autocast <rawcode>` command while left click remains ordinary target selection. `Arep`, `Aren`, and `Arst` share the persisted `AI_AUTOCAST_REPAIR` toggle. During an automatic acquisition opportunity, Repair scans the worker's `runtime.acquisition_range`, chooses the **nearest** target accepted by the same Repair validation used by an explicit order, then submits the normal `repair` entity order. The existing Repair behavior therefore remains authoritative for movement into `Rng`, footprint contact, costs, work animation, construction contribution, interruption, and completion. Completing one target does not disable Auto Repair; a later idle/default stand acquisition pass may choose another damaged target. Repair cancellation clears `goalentity` only when it still names the Repair-owned target: Stop/stand therefore retires the old target, while a replacement Move/Attack goal installed before the behavior switch is preserved.

Explicit Repair target selection now submits through `G_IssueUnitTargetOrder("repair", ...)`, so Shift-queued Repair uses the ordinary per-unit FIFO and target `spawn_time` revalidation rather than a Repair-specific queue.

Target eligibility remains intentionally conservative: the target must be a live owned building. Repair deliberately does **not** route this check through `S_SpellAllowsTarget()` because that helper models a smaller spell-oriented subset and treats air/ground as exclusive target types; WC3 Repair targets can combine categories such as ground + structure. Full Repair `Targets Allowed` evaluation remains a separate gap rather than silently rejecting otherwise-valid owned buildings.

## Known gaps

Construction and owned-building Repair now share the behavior described above. The following clean-room-spec items remain incomplete:

- `war3map.w3u` modifications are not yet fully merged into the normalized typed unit rows, so map-local edits to `Builds`/requirements may still resolve through the base unit row;
- research/upgrade production now has a shared queue, per-level costs/times,
  requirements, cancellation/refunds, and Blacksmith `ratd`/`rarm` effects; W3I
  upgrade-availability records are still parsed but are not yet applied to that
  player research state;
- `SetPlayerAbilityAvailable` remains separate from unit/building technology availability and is not yet backed by per-player disabled-ability state;
- hero training still lacks the additional hero-count/tier/token rules layered on top of normal `Trains`/maximum/requirement checks;
- training still uses the legacy `player_pay()` gold/lumber payment path, while food reservation is owned by the active queue edict; queued unit icons can now cancel/refund their exact hidden queue edict, and producer death/removal cancels/refunds all queued unit entries;
- the client does not yet draw a per-cell green/red pathing splat or mirror live-unit obstruction into that splat;
- placement supports the currently decoded walk/build/blight flags, not every Warcraft compound placement type; unsupported tokens are reported to `stderr` instead of being silently discarded;
- spawned Human construction cancellation now has the base 75% gold/lumber refund, worker release, cancel/death events, command/UI wiring, and footprint teardown; retail damage-adjusted cancellation refund behavior is not yet modeled because the exact damage/repair interaction still needs observation;
- Orc worker-inside, Night Elf worker/Ancient consumption, and Undead summon/release construction strategies remain legacy behavior, so their spawned-construction cancellation lifecycles are not yet enabled;
- Repair target masks are not yet complete enough to safely enable allied structures, repairable mechanical non-buildings, or destructibles; the current implementation keeps the pre-existing owned-building boundary;
- Repair `DataE` naval-range behavior is not implemented;
- Auto Repair currently implements the high-confidence nearest-valid owned-building path. Warcraft string immediate orders `repairon` / `repairoff` are exposed through the existing `IssueImmediateOrder` path; broader generic autocast policies and numeric `IssueImmediateOrderById` order-ID exposure remain future work. The command-card transport uses the normalized multi-selection `autocast <rawcode>` command;
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
18. Standard `Aren` / `Arst` Repair rejects unfinished construction; `Arep` power building only accepts paused Human construction.
19. Damage a completed owned building and verify Repair uses `reptm`, `DataA`, and `DataB`, including fractional gold/lumber deductions.
20. Order Repair from outside `Rng`; the worker must walk to the building footprint rather than teleport to it.
21. Right-click a damaged owned building with a Repair-capable worker; Smart Repair should start. Right-click a full-health building; Repair should decline and normal Smart fallback remains available.
22. Interrupt Repair or kill the worker and verify a paused building retains no stale primary-builder association.
23. Stop/Move a Peasant while it is still walking to a build site and verify `build_project` is cleared with no resource loss.
24. Select an unfinished Human building and verify `CmdCancelBuild` appears; clicking either it or the active construction icon cancels the structure.
25. Cancel an undamaged Human construction with known paid costs and verify the payer receives the 75% base gold/lumber refund, while subsequent cancellation attempts refund nothing.
26. Cancel a Human construction with primary/additional builders and verify every Repair participant is released and stops spending power-build resources.
27. Verify construct-cancel events precede the ordinary death events and `GetCancelledStructure()` resolves the unfinished building.
28. Cancel or destroy a footprint-bearing building and verify the vacated footprint becomes pathable immediately.

## See Also

- [Building Damage Rendering](building-damage-rendering.md) — health-driven fire overlays are renderer presentation; construction Birth suppresses them even though construction starts at low HP.
- [Pathfinding](pathfinding.md) — static footprint baking and dynamic-unit obstacle handling used by construction placement and teardown.
- [Save / Load](save-load.md) — raw `edict_t` scalar persistence and `F_EDICT` fixups for `construction.primary_builder`.
