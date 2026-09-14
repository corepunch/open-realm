# Warcraft III Building Construction

## Contract

Construction is server-authoritative. `UnitProfile.Builds` remains the source of which structures a worker can offer; a client cannot select an arbitrary unit rawcode and bypass that list. Training follows the same authority rule through `UnitProfile.Trains`. In-place structure tier upgrades follow it through `UnitProfile.Upgrade` (`uupt`): the command card and the eventual `upgrade <rawcode>` request both re-evaluate the authored target list, per-player technology maximum, normal requirements, and owner resources before the upgrade can begin. `games/warcraft-3/game/g_building.c` owns the shared technology/requirement checks as well as building resource availability, structure-upgrade lifecycle, and placement validation.

Command-card activation is independent of JASS `EnableUserUI`. `button CmdBuild`, `button <unit rawcode>`, and other gameplay button commands must continue through `CMD_Button()` when `EnableUserUI(false)` is recorded; that native suppresses presentation affordances, not gameplay command authorization. A rebase briefly special-cased `client->no_ui` inside `CMD_Button()`, which made the button animate client-side while silently discarding both the Peasant Build submenu and Town Hall training requests.

Build-menu state and command-bar transport also have separate lifecycles. `AbilityBuild_Command()` installs its `cmdbutton`/`refresh` callbacks even for a reserved player slot that has not completed `ClientBegin`, but only serializes `LAYER_COMMANDBAR` when `client->connected` is true. This keeps in-engine command tests and pre-connect state changes from touching the server message buffer while preserving the menu state that a connected client will render.

The runtime developer override is:

```sh
+set wc3_build_all 1
```

It makes every structure, trained unit, or in-place structure upgrade already present in the selected producer's final `Builds` / `Trains` / `Upgrade` list available regardless of technology maximums or `Requires`. Structure construction also keeps the existing debug behavior of bypassing gold, lumber, and food charges. Training and structure upgrading bypass technology/requirement gates but still perform their normal resource/food availability checks and payment/reservation. It does **not** invent commands outside those authored lists, bypass building classification, or relax map bounds, terrain/static pathing, live-unit occupancy, or build-on-target rules. This keeps the cheat useful for tech-tree testing without allowing invalid production commands or world placement.

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

## In-place structure upgrades (`uupt`)

Unit-type structure upgrades such as Human Town Hall -> Keep are distinct from `UpgradeData.slk` research. The source building's `UnitProfile.Upgrade` / `uupt` list supplies the allowed target unit rawcodes. `G_GetBuildingUpgradeCommandState()` re-checks that list on both command-card construction and command execution, so a client cannot submit an arbitrary building morph.

The accepted command follows the Warsmash `CAbilityUpgrade` / `CUnit.beginUpgrade` ownership model:

```text
UnitProfile.Upgrade / uupt
    -> G_GetBuildingUpgradeCommandState
       -> target is a building
       -> target technology maximum
       -> target Requires / Requiresamount
       -> relative target gold/lumber cost
       -> target Food Used - source Food Used
    -> G_StartBuildingUpgrade
       -> deduct exact accepted cost
       -> reserve target Food Used on the existing edict
       -> mark target technology in progress
       -> clear ordinary orders
       -> hold the source building on its Birth progress sequence
       -> publish UPGRADE_START events
    -> G_RunBuildingUpgradeFrame
       -> advance target UnitBalance.buildTime
       -> completion: G_TransformUnitType(existing edict, target)
    -> target unit data/model/stats/abilities/pathing
       -> preserve edict/JASS identity, owner, position, and health percentage
       -> refresh target Food Made/Food Used
       -> target rawcode now satisfies normal techtree counts
       -> publish UPGRADE_FINISH events
```

`Misc.RelativeUpgradeCost` follows Warsmash semantics: zero (and the compatibility default when the field is absent) charges the target unit's gold/lumber cost minus the source unit's cost. Non-zero charges the target's full unit cost. The displayed command tooltip uses the same calculated values as command acceptance/payment. Food is always the positive target-minus-source `foodUsed` delta for the availability check; once accepted, the existing building owns the target Food Used value immediately, matching Warsmash.

The upgrade state is owned by the **existing building edict**, not a hidden training child. OpenRealm reuses the edict's otherwise producer-local `research` scalar block for target rawcode, exact charged gold/lumber, duration, and progress; ordinary `UpgradeData.slk` research continues to live on hidden training queue edicts. This keeps save/load on the existing serialized edict layout while making the two lifecycles unambiguous through `G_BuildingUpgradeActive()`.

While upgrading, the command card exposes only `CmdCancelBuild`, ordinary target/point/immediate orders and idle auto-acquisition are rejected, and the normal build-queue panel shows the target icon with the `UPGRADING` progress label. The source structure remains attackable and retains its current health rather than receiving construction health gain. The source footprint remains authoritative until completion.

Player cancellation publishes `EVENT_PLAYER_UNIT_UPGRADE_CANCEL` / `EVENT_UNIT_UPGRADE_CANCEL`, restores the source Food Used value, removes the target in-progress reservation, and refunds the **exact full amount charged**. Ownership transfer first cancels/refunds the upgrade while the original owner is still authoritative, preventing resource and techtree reservations from crossing players. Destruction or forced `RemoveUnit` tears down the same transient state without a refund. This is intentionally separate from spawned-building cancellation, whose construction refund/death policy remains unchanged.

Completion generalizes `G_TransformUnitType()` from mobile->mobile morphs to same-family building->building morphs. Cross-family unit<->building transforms remain rejected. For buildings the helper releases the old pathing texture, binds and spawns the target unit data on the same edict, preserves health/mana ratios and temporary combat modifiers, reapplies food, links the entity, and rebuilds static obstacles so the target footprint becomes authoritative. Selection/JASS references keep the same edict identity; no destroy-and-respawn substitution is used.

The current implementation intentionally does not attempt a separate target-model construction overlay or retail-specific upgrade dust/effect layer; it drives the source building's authored `Birth` sequence by authoritative progress. Those presentation details should be added only after their exact data/retail contract is established.

Technology/count changes mark the owner's command card dirty instead of pushing UI from inside arbitrary JASS/entity callbacks. `G_UpdateClientCommandCards()` consumes that flag after entity simulation, while the initial `G_ClientBegin()` command-card write clears any dirty state accumulated by W3I or map-init JASS before the game HUD is shown. Build/skill submenus retain a refresh callback so a tech update rebuilds the current submenu rather than forcing the main card; active location/entity targeting defers the refresh until that input mode is resolved so cursor state is not stranded. Runtime spawns, ownership changes, removals, deaths, construction start/completion, and training completion invalidate affected command cards. The same invalidation reaches connected shared-control viewers whose selected unit belongs to the changed owner; those viewers also evaluate build state against the owner's resources and technology.

## Melee visibility versus campaign restrictions

The local `data/WarsmashModEngine` reference keeps a worker's full `Builds` list visible in a melee game,
including structures with unmet prerequisites. `AbstractCAbilityBuild.innerCheckCanUse()` compares each
requirement against the player's unlocked technology count; the command-card visitor's
`missingRequirement()` disables the icon and adds requirement text. Its `techtreeMaximumReached()` instead
sets `omitIconEntirely`. These are separate rules: missing a Keep does not hide an Arcane Sanctum.

The authoritative unit fields are `Builds` (`ubui`), `Requires` (`ureq`), and `Requiresamount` (`urqa`).

Requirement counting follows `UnitProfile.Upgrade` (`uupt`) transitively for completed structures. An upgraded structure therefore continues to satisfy prerequisites on its predecessor: a Keep satisfies a Town Hall requirement, while a Castle satisfies both Keep and Town Hall requirements. This is requirement equivalence only; exact unit-type counts and techtree maximums remain keyed to the actual current rawcode.
They come from the race `*UnitFunc.txt` profiles and unit-object overrides; prerequisite rawcodes can denote
unit/building counts or researched upgrade levels. A missing amount means one. `Requirescount` / `Requires1`
and subsequent tiers are separate tier requirements, not a general hide-locked-buildings option.

Stock Human archive values checked in both editions:

| Structure | Rawcode | ROC `Requires` | TFT `Requires` |
|---|---|---|---|
| Blacksmith | `hbla` | `htow` (Town Hall) | `htow` |
| Workshop | `harm` | `hkee,hbla` (Keep, Blacksmith) | `hkee,hbla` |
| Arcane Sanctum | `hars` | `hkee` (Keep) | `hkee` |
| Gryphon Aviary | `hgra` | `hcas,hlum` (Castle, Lumber Mill) | `hkee,hlum` (Keep, Lumber Mill) |

ROC Peasants list ten structures; TFT adds `hvlt` (Arcane Vault), which has no prerequisite in the
inspected profile. Campaign omissions are authored restrictions. For example, ROC Human02's
`war3map.j:InitTechTree_Player1` sets `hbla`, `harm`, `hars`, and `hgra` maxima to zero, hiding them
independently of owned structures or research. Do not copy a campaign command-card subset into melee rules.

Bounded verification on Bandit Ridge used the following command, also without `-tft` for ROC:

```sh
build/bin/openwarcraft3 -data 'data/Warcraft III' -tft +map 'Maps/FrozenThrone/(2)BanditRidge.w3x' +idleworker +button CmdBuild +screenshot 10 +com_frame_limit 25
```

Temporary traces immediately after `G_GetBuildCommandState()` confirmed `wc3_build_all=0`, maxima `-1`,
and `BUILD_COMMAND_DISABLED` for Workshop, Sanctum, and Aviary. The Aviary reason was Keep in TFT and Castle
in ROC. The traces were removed after verification. `BUILD_COMMAND_UNAFFORDABLE` remains visible/clickable
so the attempted command can report a resource shortage, matching Warsmash's resource-preview behavior.

Disabled command buttons follow Warsmash's `AbilityDataUI.disable()`: `UI_CommandButtonImage()` resolves
the recipient's `CommandButtonDisabledArtPath` skin field and appends `DIS` plus the normal art basename
(for example `DISBTNWorkshop.blp`). The directory comes from `UI\war3skins.txt`, not a hardcoded path.
`UI_WriteCommandButtonFrame()` sends that texture with white modulation, replacing the previous normal
texture/gray tint, and retains the disabled click/hotkey behavior. Enabled buttons keep their original art.
Missing skin data or an overlong derived path produces an explicit diagnostic instead of substituting
enabled artwork; missing texture assets follow the ordinary image-registration diagnostics.
The `wc3_building.disabled_command_button*` tests cover texture selection, basename-only art, unchanged
input gating, and invalid skin/path handling in both editions.

## Placement

`G_SnapBuildingPoint()` implements the WC3 pathing-grid alignment used by both authoritative placement and the client ghost:

```text
base lattice = 64 world units
odd half-pathing-map dimension -> +32 on that axis
no authored pathing texture -> 32-unit fallback grid
```

The placement cursor carries its authored pathing-texture width/height in dedicated `entityState_t.pathing_width` / `pathing_height` fields. `client/cl_view.c:CL_AddBuilding()` uses those dimensions for the same visual snap. Cursor metadata also carries the authoritative prevented/required pathing-bit masks and the builder entity number to exclude from live-occupancy preview checks. These fields are delta-serialized and have an explicit network round-trip test; world-position fields remain world position only.

`G_EvaluateBuildPlacement()` checks:

- building classification;
- snapped pathing footprint inside the map path grid;
- `UNBUILDABLE` and `UNWALKABLE` static pathing;
- normalized `preventPlace` (`unwalkable`, `unbuildable`, `blighted`);
- normalized `requirePlace` for the same supported flags;
- static building/destructable footprints already baked into `pathmap.original`;
- live unit-circle occupancy, excluding the builder;
- exact build-on-target presence when `isBuildOn` requires a `canBuildOn` structure.

The common collision-model contract exposes `CM_GetPathingFlagsAt()` so the generic client can sample immutable terrain + baked-static pathing without reaching into WC3 routing internals or mutating movement's dynamic path map. WC3 implements the byte-mask query in `server/sv_routing.c`; game backends without this style of pathing grid return `false`, which suppresses cell classification rather than leaving a cross-game unresolved symbol.

A declared pathing texture that fails to load is a placement failure. Do not silently degrade such a structure to a one-cell footprint; `M_LoadPathTex()` already reports the missing asset.

Placement is checked once when the player confirms the ghost and again when the worker reaches the site. Resources are charged only after the arrival-time validation passes. Construction spawns at the stored snapped waypoint, not at the worker's current position. Placement rejection uses the plain UI message `Unable to build there.`; requirement/resource failures remain separate command-state messages. `G_LevelString()` resolves only exact `TRIGSTR_<id>` tokens. Previously it parsed arbitrary UI text as trigger-string ID 0, so Human02's WTS entry 0 (the map name) replaced ordinary errors with `Human02`.

Once the initial placement is accepted, `G_IssueBuildOrder()` also publishes `EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER` and `EVENT_UNIT_ISSUED_POINT_ORDER`. The builder is `GetOrderedUnit()`, `GetIssuedOrderId()` is the building rawcode, and `GetOrderPointX/Y/Loc()` expose the accepted snapped build point. This happens at order acceptance, before the worker travels; arrival-time revalidation and construct-start/finish are later, separate events. Prologue02 relies on this exact point-order handoff to advance from Burrow placement into the lumber tutorial. See [Issued Target and Point Order Events](issued-target-order-events.md).

### JASS construction orders

`IssueBuildOrderById(whichPeon, unitId, x, y)` is a thin JASS adapter over
`G_IssueBuildOrder()`. The integer is the building rawcode, so scripted
construction uses the same server-authoritative worker build list, technology,
resource, placement, pathing, waypoint, and issued-point-order behavior as
command-card and AI construction. It does not spawn a completed structure
directly; `CreateUnit` remains the immediate-spawn path.

Building rawcodes passed to `IssuePointOrderById` or
`IssuePointOrderByIdLoc` are routed to the same build path before the generic
order-id translation. Canonical movement/spell numeric IDs continue through the
normal `G_OrderId2String()` router added by the JASS spell-order work; building
rawcodes are a distinct Warcraft point-order case and are not entries in that
order table. The string-form `IssueBuildOrder` remains a stub.

Construction completion publishes both
`EVENT_PLAYER_UNIT_CONSTRUCT_FINISH` and `EVENT_UNIT_CONSTRUCT_FINISH` for the
same structure, so `GetConstructedStructure()` is valid from either callback
family.

### Placement cancellation

Build placement is a server-owned UI mode. `G_CancelBuildPlacement()` is the single teardown path: it clears the player's pending `build_project`, sends an empty `svc_cursor` so the client removes the ghost model, and restores the normal command card. The command-card `CmdCancel`, the gameplay `cancel` command, and both `smart`/`smartpoint` right-click paths use this teardown. A right-click while the build ghost is active is therefore consumed as cancellation and must not issue an order to the selected worker.

A successful left-click copies the pending project to the worker order before clearing the player's placement cursor state. Leaving the worker's pre-spawn Build move (Stop, Move, or another replacement order) also clears the worker-owned `build_project`; no structure exists yet and no resources have been charged, so this is a full pre-payment cancellation rather than a construction refund.

### Placement grid preview

While a normal footprint-based structure is selected for placement, the client draws a terrain-conforming grid under the ghost model. Every texel position in the authored pathing-map dimensions is represented by one solid splat rectangle, matching Warsmash's preview presentation: green when the local pathing sample satisfies the same prevented/required masks used by `G_EvaluateBuildPlacement()`, and red when the cell is out of bounds, violates those masks, or overlaps a live entity collision circle. The builder is excluded by the ignored-entity field packed into `entityState_t.pathing_preview`, matching the authoritative `G_LiveUnitBlocksBuild()` exclusion. Dead/non-selectable snapshot entities are not preview blockers.

The cursor packs its preview-only metadata into one aligned `DWORD` to stay comfortably inside the 32-bit entity-delta field mask: bits 0..15 are the ignored entity number, bits 16..23 are prevented pathing flags, and bits 24..31 are required pathing flags. `EntityPathingPreviewPack()` and the matching accessors are the only code that should depend on that wire layout.

The snapshot `entityState_t.collision` field is the gameplay collision radius used for this preview. Do not substitute `entityState_t.radius`: WC3 uses `radius` for the selection/UI circle and `edict.collision` for unit occupancy. `SP_SpawnUnit()` publishes the authored/runtime collision value into `s.collision` after building pathing has finalized its derived collision radius.

Rendering deliberately reuses the terrain-splat path rather than uploading a regenerated RGBA texture each mouse frame. `client/tr_public.h:renderSplatRect_t` is a generic client→renderer scene primitive; `renderer/r_ents.c:R_DrawSplatRects()` batches all submitted rectangles with the built-in white texture through `R_BeginSplatBatch()` / `R_AddRectSplat()` / `R_EndSplatBatch()`. The placement colours use alpha 166/255 (approximately 65%). Warsmash uses about 20% for its inspected `MeleeUI.handleBuildCursor()` splat; OpenRealm deliberately uses the stronger opacity so blocked and buildable cells remain obvious against terrain and the placement model. This keeps the plan under the ghost model and avoids one draw/upload per pathing cell.

The Warsmash reference also samples one 32-world-unit pathing cell per preview texel and colours prevented/required pathing failures red and valid cells green. OpenRealm preserves that presentation model but keeps placement authority on the server.

The placement building itself is rendered as a white-tinted MDX instance at full 255/255 alpha, so the ghost model remains fully opaque while the terrain plan provides the placement feedback underneath it. `renderEntity_t.tint` owns this presentation state. The inspected Warsmash source contains a commented-out `0.5f` vertex-alpha experiment in its build-cursor code; OpenRealm deliberately keeps the placement model opaque.

Build-on-target structures (`UnitData.isBuildOn`) are intentionally excluded from the coloured grid for now. Their authoritative validity depends on finding an eligible `canBuildOn` parent, and that parent-eligibility state is not yet part of the client cursor contract. The ghost still uses authored dimensions for snapping; suppressing the grid is preferable to showing a misleading all-green plan.

### Spawned construction cancellation

Once a race-specific structure has spawned, cancellation is owned by the unfinished structure rather than by the worker's placement mode. `G_GetCommandButtons()` exposes Warcraft's `CmdCancelBuild` while `construction.active` is true, and the active construction icon in the info panel submits the same command. `CmdCancelBuild` resolves through the shared Cancel handler to `G_CancelStructureConstruction()`.

The authoritative lifecycle is:

```text
validate live controlled construction
    -> publish EVENT_PLAYER_UNIT_CONSTRUCT_CANCEL
    -> publish EVENT_UNIT_CONSTRUCT_CANCEL
    -> refund 75% of the recorded base gold/lumber payment
    -> unit_die()
       -> release Human Repair/power-build workers or the race-owned construction worker
       -> clear construction/self-linked queue state
       -> clear food and selection through normal death cleanup
       -> publish ordinary death events
       -> mark the structure dead
       -> rebake static pathing without the dead building footprint
```

The construction record stores the payer and exact base gold/lumber charge at spawn time. This avoids refunding later Human power-build Repair spending and keeps ownership changes from redirecting the refund to the structure's current owner. `wc3_build_all` constructions record no payment and therefore receive no cancellation refund. The refund calculation is isolated in `G_ConstructionCancelRefund()` so integer-rounding parity can be refined independently if retail observation shows a different edge case.

`G_StopConstruction()` is also called when any race-specific unfinished structure dies to combat or is directly removed. That path releases builders and the construction HUD self-link but does **not** publish construct-cancel events or refund resources. Cancellation therefore remains distinct from enemy destruction while still sharing the normal death, food, selection, and pathing lifecycle.

Static building footprints are rebuilt after building death. `common/routing.c` excludes dead `SVF_MONSTER` entities from static footprint baking even when their authored `pathtex` remains attached for presentation/save state; dead destructables retain their separate death-path-texture behavior. This prevents cancelled or destroyed buildings from leaving invisible route blockers.

## Race-specific construction lifecycles

The spawned building now records an explicit construction strategy. Human remains Repair-driven and paused; Orc, Undead, and Night Elf buildings use the shared autonomous construction clock in `G_RunConstructionFrame()`. Orc Peons and Night Elf Wisps are hidden/paused/invulnerable while inside construction. Undead Acolytes remain visible for the short summon-work window and are then released while the structure continues. Night Elf structures classified `ancient` consume their Wisp on successful completion; cancellation/destruction releases the Wisp and restores its authored Food Used.

The building owns temporary-worker state (`construction.worker` plus `spawn_time`) separately from Human `construction.primary_builder`. Completion, cancellation, combat death, and direct `G_FreeEdict()` all use the same worker-release path. Human Repair rejects active autonomous construction, so it cannot become a second construction clock.

See [Race Mechanics](race-mechanics.md) for the clean-room comparison, state ownership, save/load requirements, and deferred race-specific economy/terrain mechanics.

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

- `war3map.w3u` now merges registered `UnitUI` fields (including custom models), but other typed rows are not yet fully merged, so map-local edits to `Builds`/requirements may still resolve through the base unit row;
- `UpgradeData.slk` research has a shared queue, per-level costs/times,
  requirements, cancellation/refunds, and Blacksmith `ratd`/`rarm` effects;
  unit-type structure upgrading through `uupt` is a separate in-place lifecycle
  documented above. W3I upgrade-availability records are still parsed but are
  not yet applied to the player research state;
- `SetPlayerAbilityAvailable` remains separate from unit/building technology availability and is not yet backed by per-player disabled-ability state;
- hero training applies the authored `Requirescount`/`Requires1`... tier selected by the owner's completed hero count; custom map unit-object overrides for those fields remain incomplete until all typed `war3map.w3u` rows merge into the normalized profile;
- training still uses the legacy `player_pay()` gold/lumber payment path, while food reservation is owned by the active queue edict; queued unit icons can now cancel/refund their exact hidden queue edict, and producer death/removal cancels/refunds all queued unit entries;
- the client does not yet draw a per-cell green/red pathing splat or mirror live-unit obstruction into that splat;
- placement supports the currently decoded walk/build/blight flags, not every Warcraft compound placement type; unsupported tokens are reported to `stderr` instead of being silently discarded;
- spawned construction cancellation records the base payment for each implemented race strategy and has the base 75% gold/lumber refund, worker release/consumption teardown, cancel/death events, command/UI wiring, and footprint teardown; retail damage-adjusted cancellation refund behavior is not yet modeled because the exact damage/repair interaction still needs observation;
- Orc worker-inside, Night Elf worker/Ancient consumption, and Undead summon/release construction strategies are implemented; Haunted/Entangled Mine overlays, Blight, Wisp lumber, and full Root/Uproot remain tracked in [Race Mechanics](race-mechanics.md);
- string-form `IssueBuildOrder` remains a stub. `IssueBuildOrderById` and building rawcodes passed to the numeric point-order natives reuse `G_IssueBuildOrder()`; canonical movement/spell numeric IDs continue through the existing order table;
- Repair target masks are not yet complete enough to safely enable allied structures, repairable mechanical non-buildings, or destructibles; the current implementation keeps the pre-existing owned-building boundary;
- Repair `DataE` naval-range behavior is not implemented;
- Auto Repair currently implements the high-confidence nearest-valid owned-building path. Warcraft string immediate orders `repairon` / `repairoff` are exposed through the existing `IssueImmediateOrder` path; broader generic autocast policies and numeric `IssueImmediateOrderById` order-ID exposure remain future work. The command-card transport uses the normalized multi-selection `autocast <rawcode>` command;
- the per-player technology table is intentionally bounded at `MAX_PLAYER_TECH_STATE`; exhaustion is reported as a warning and does not overwrite an existing entry;
- `GetPlayerTechResearched` / `GetPlayerTechCount` currently have exact-rawcode semantics for both `specificonly` values because technology-equivalence groups are not represented yet.

## Verification

Focused automated checks after building:

```sh
make test-wc3-engine WC3_PATTERN='wc3_building.*'
make test-wc3-engine WC3_PATTERN='wc3_api.issue_build_order_by_id_*'
make test-wc3-engine WC3_PATTERN='wc3_api.construct_finish_*'
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
29. Enter normal building placement and verify the snapped footprint displays an approximately 90%-alpha green per-cell terrain plan beneath the ghost.
30. Move the ghost across unwalkable/unbuildable terrain or outside the map and verify only affected preview cells turn red.
31. Move a live unit into the footprint and verify the cells overlapped by its gameplay collision circle turn red while the selected builder itself does not.
32. Kill the blocking unit and verify its non-selectable corpse no longer paints placement cells red.
33. Verify a build-on-target structure keeps normal ghost snapping but does not display the coloured grid until parent-target preview metadata is implemented.
34. Start Orc construction and verify the Peon is hidden, paused, invulnerable, and returned outside the completed/cancelled structure.
35. Start Undead construction and verify the Acolyte remains visible for the summon-work window, is released before completion, and the building continues autonomously.
36. Start Night Elf Ancient construction and verify the Wisp leaves Food Used while inside, is consumed on completion, and is restored with Food Used on cancellation/destruction.
37. Attempt standard or Human Repair on autonomous Orc/Undead/Night Elf construction and verify it is rejected rather than adding a second construction clock.
38. Save/load while a race-owned construction worker is attached and verify the `construction.worker` edict reference and lifecycle flags round-trip.

## See Also

- [Building Damage Rendering](building-damage-rendering.md) — health-driven fire overlays are renderer presentation; construction Birth suppresses them even though construction starts at low HP.
- [Pathfinding](pathfinding.md) — static footprint baking and dynamic-unit obstacle handling used by construction placement and teardown.
- [Race Mechanics](race-mechanics.md) — Human/Orc/Undead/Night Elf construction state machines and remaining racial mechanic gaps.
- [Save / Load](save-load.md) — raw `edict_t` scalar persistence and `F_EDICT` fixups for `construction.primary_builder` and `construction.worker`.

### Campaign construction diagnostics

`+set wc3_quest_debug 1` also traces construction tutorial transitions as
`WC3_QUEST_BUILD`. The log records construct-event registrations and dispatcher
matching, the worker/building linkage when construction starts, legacy-race
completion entering `G_CompleteConstruction`, publication of
`EVENT_PLAYER_UNIT_CONSTRUCT_FINISH`, and the builder state immediately after
release. This is intended for campaign compatibility debugging and does not
change construction semantics.

## Command-Button Active Highlight

Construction and training rawcodes have no ability-table entry, so `GetAbilityIndex(NULL)`
produces the byte sentinel `255`. Idle units likewise publish `entityState_t.ability = 255`.
The client must exclude that sentinel before comparing a command frame's `stat` with the selected
unit's ability; equality alone lit every construction choice while the worker was idle.
Ability index zero remains valid, and `UIFLAG_ALTERNATE_ACTIVE` independently enables autocast glow.
The regression test `client_layout.command_glow_requires_an_ability_or_autocast` checks the actual
renderer submission for all these cases.

For a bounded visual check, load Bandit Ridge with `+idleworker +button CmdBuild +screenshot 10
+com_frame_limit 25` after the map argument, in both ROC and TFT. The native construction card
should show normal icon borders while the idle Peasant is selected.
