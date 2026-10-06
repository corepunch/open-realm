# WC3 Pathfinding And Harvest Reachability

Ordinary long-distance Move now uses [retail adaptive search](retail-pathfinding-engine.md#adaptive-search-reaches-long-distance-move)
to supply local fine-search turns. The engine traverses long obstructed maps,
rebuilds static lane classifications after terrain edits, and resumes saved
travel with exact position/heading/velocity words. [Way Gate special edges](retail-pathfinding-engine.md#way-gate-special-edges-reach-retained-move-routes)
now reach member/group plans and retained Move consumers. Cached retarget,fresh
retarget and disabled walking match410 retail commits plus3094 saved continuation
commits. Broader gate placement,group regrouping and complete retail trajectories
remain work in progress.

Static blocker removal now publishes its footprint change before subsequent
requests. Direct free clears live/dead-rubble footprints; public `RemoveUnit`
invalidates them immediately when hiding a building, before deferred free.
Overlapping objects and terrain remain authoritative, and obsolete fields and
adaptive classifications cannot be reused. Actual gold depletion and public
same-callback Move are covered. See [blocker lifecycle integration](retail-pathfinding-engine.md#blocker-removal-owns-static-route-invalidation).

Public JASS Move/Smart batches now have a [Move-owned shared physical group](retail-pathfinding-engine.md#public-pair-movement-uses-a-shared-move-owner):
a separate group route, retained formation slots and all-member decisions before
velocity commits. A complete public pair matches115 original commits and87 saved
continuation commits through normal frames. Ordinary selected ground Move now uses that owner too: two actual player-input
journeys match456 engine commits and304 saved suffix commits with supplied input
clocks. See [selected movement](retail-pathfinding-engine.md#selected-ground-move-uses-the-shared-physical-owner).
The first Shift Move behind an active selected ground cohort now retains the
common request through staggered arrivals and fresh physical membership; two
retail journeys match1020 commits and684 saved suffix commits. See
[queued selection](retail-pathfinding-engine.md#selected-shift-move-retains-request-ownership-through-staggered-arrival).
Idle selected ground Shift now starts that shared owner immediately and matches
456 native commits plus304 saved suffix commits. See [idle Shift](retail-pathfinding-engine.md#selected-idle-shift-starts-the-shared-physical-owner-immediately).
Two pending selected ground Shift moves now retain submission history independently
of FIFO activation and match1110 native commits plus864 saved suffix commits.
Creation-ordered physical visits survive slot reuse and save/load. See [two queued
moves](retail-pathfinding-engine.md#two-pending-shift-moves-retain-submission-history-and-physical-generations).
AI, mixed active/idle queues, mixed-lane producers, full eligibility flags and
general crowd parity remain open.

Public group Move also keeps [live maximum and cached route footprint separate](retail-pathfinding-engine.md#local-group-maximum-and-retained-route-footprint-have-separate-lifetimes)
when its largest peer grows, shrinks or disappears. Three complete journeys
match831 motion commits/730 owner states and2207 saved continuation commits.
A retained coarse route keeps its original sampled footprint; a fresh route uses
the current live maximum. Owner counter/budget publication precedes all individual
movement callbacks through the generic ability begin phase. The captain shared-radius owner now covers a bounded mixed13 phase; see
[shared captain parameters](retail-pathfinding-engine.md#shared-captain-parameters-across-unequal-physical-batches).
Other movement families remain open.

Authored repulsion now runs through Move after each owner movement pass, so
eligible idle flyers separate as well as moving units. It uses saved retail
random state, exact scalar pair/tail arithmetic and collision-sized endpoint
admission. Ownership changes, pause, removal and save/load are covered through
actual native/scheduler tests. See [the engine port and remaining crowd-order
boundary](retail-pathfinding-engine.md#authored-repulsion-reaches-idle-engine-units).

## Contract

WC3 movement keeps target selection, static routing, and interaction behavior separate:

```text
order / behavior -> target + interaction range -> routing -> collision-aware step
```

`games/warcraft-3/game/skills/s_move.c` owns per-tick steering and local block-and-slide. `server/sv_routing.c` owns static pathmap line tests, connectivity queries, and cached flow fields. Harvest target selection remains in `skills/s_harvest_lumber.c`; the router never changes a tree target by itself.

Game-owned Move queries now use [direct software world/fine conversion](retail-pathfinding-engine.md#direct-world-coordinates-preserve-boundary-cells).
This preserves boundary cells and exact corrected/route point words on maps
whose dimensions are not powers of two.

Move now [retains native fine-grid position](retail-pathfinding-engine.md#retained-fine-pose-reaches-move)
through accepted steps, final point arrival and save/load. World publication
preserves the original scalar rounding. Point Move now uses the measured
[primary clock and old-velocity phase](retail-pathfinding-engine.md#primary-clock-reaches-move-and-predicted-positions),
with predictions between30ms owner callbacks and saved clock/phase state.
[Normal JASS timer admission](retail-pathfinding-engine.md#public-timer-admission-reaches-move-from-zero)
now reproduces eight complete public spawn-to-Move journeys from clock zero.
Timers consume elapsed simulation time once, and a movement owner due at the
same timestamp runs before authored timer callbacks.

Selection Move and Shift queues now use [retail ranked formation geometry](retail-pathfinding-engine.md#ranked-formation-layout-reaches-group-orders)
for up to twelve members. Authored ranks, collision radii, tie assignments,
row dimensions and offset arithmetic reach the assigned destinations; the
original group heading/clock/refresh chain remains work in progress.

AI assault recruits now [move toward an authored captain home](retail-pathfinding-engine.md#captain-home-recruitment-and-formation-retries-reach-move).
InitAssault retains roster/home/active goal, and fullness follows retail's
formation flag so repeated typed requests do not inflate an invented total.
Stationary singleton travel now matches complete178/250-commit native journeys,
including the exact membership deadline, private point handoff and zero-radius
captain occupancy. Eight saved states per source and bot-free restores retain
2225 suffix commits. Moving captains, larger shared rosters and default
town-home production remain open. See [callback and occupancy payoff](retail-pathfinding-engine.md#stationary-captain-range-callback-and-zero-radius-occupancy).

Move's scalar turn update and scripted movement-window gate now use
[verified retail arithmetic](retail-pathfinding-engine.md). Static class footprints now also reach [routing, destinations and actual steps](retail-pathfinding-engine.md#retail-collision-classes-reach-routing-and-stepping). This is an incremental
integration; the routing/velocity pipeline does not yet have full retail parity.
[Retail segment sampling and waypoint selection](retail-pathfinding-engine.md#retail-segment-sampling-and-waypoint-selection)
now drive direct/step/retention checks and nearby route turns. Long shared fields
use the same class geometry, while their SPFA/interpolation policy remains the
engine algorithm.
Ordinary clear location orders also retain [native fine waypoints](retail-pathfinding-engine.md#clear-public-routes-retain-native-waypoints) before heading evaluation. A clear final corridor does not erase the original first fine turn.
Retained terrain legs use the bake epoch and native waypoint progress; each tick still checks current live occupancy. Repeating the full static segment sampler from each fractional source can spuriously rebuild an already admitted turn.
Individual stepping now consumes the same existing status/aura speed composition as
selection-group caps. Actual Cripple/Bloodlust Move/expiry tests cover slowing,
boosting and mixed-speed formations; see [effective-speed integration](retail-pathfinding-engine.md#effective-speed-reaches-actual-movement).
Simultaneous selection Move orders now [refresh their active cohort speed](retail-pathfinding-engine.md#active-move-cohort-speed)
after member completion, replacement, death/removal and speed changes; reserved
destinations remain stable.

Scripted decimal real coordinates now use the [recovered compiled JASS producer](retail-pathfinding-engine.md#compiled-jass-real-literals),
including signed32 prefix/fraction/denominator wrapping and software division/addition.
The same literal words survive save/load and enter public Move unchanged. Galaxy
retains its distinct source-number conversion.
Compiled decimal/octal/hex integer constants also wrap per digit before I2R and
Move consumption; [integer source evidence](retail-pathfinding-engine.md#compiled-jass-integer-words)
includes values wider than64 bits, unary minimum-integer negation and save/load.
Public S2R byte strings now have [exact original CRT evidence](retail-pathfinding-engine.md#public-decimal-byte-grammar-and-crt-locale):
only ASCII digits are accepted in the observed default C locale. All bytes80..ff
terminate parsing, including when a parsed coordinate enters Move. The engine
regression uses authored object names and public SubString/S2R, preserving the
byte producer and numerical boundary.

Ground Move, Patrol, Attack-move location orders, and Attack chases use collision-size-aware static routing through direct-line checks, waypoint acceleration, and flow generation. Attack still owns its interaction range independently: reaching a collision-safe route endpoint does not complete the attack. Harvest has an explicit collision split: Gold Mine approach and all resource-return legs use collision-sized **static-only** routing (live units ignored), while tree approach keeps live-unit collision and uses collision-sized resource-worker local avoidance.

Normal movement uses the live `unitinfo.PropWindow`, initialized from `UnitData.propWin` / object field `uprw` and mutable through `SetUnitPropWindow`. The authored value is a propulsion window in degrees: steering may continue turning toward the avoidance-resolved heading every simulation tick, but `unit_moveindirection()` does not translate until the remaining facing error is strictly inside that window. This is shared movement behavior, so Attack chase, Follow, Patrol, Harvest, Repair, Way Gate approach, and ordinary Move inherit the same turn-before-propulsion rule rather than implementing Attack-specific steering. A zero runtime value prevents translation until facing exactly matches the resolved heading, so it can be used to hold movement while retaining turning.

### Static interaction rectangles

`G_ClosestStaticPathablePointInRectForRadiusFlags()` is a WC3-owned query for interactions whose legal destination is a world-space rectangle rather than one point or an entity footprint. It searches every pathmap cell intersecting the rectangle, applies the caller's collision radius and blocked-pathing mask to `pathmap.original`, and returns the closest point inside a legal intersecting cell. It does **not** stamp live units into the query. Temporary occupancy remains a move-time collision/local-avoidance concern, preventing a crowded interaction area from becoming unavailable at order submission. Way Gates use it for authored `Wrp1`/`Wrp2` entry rectangles; the shared router retains only generic pathing primitives.

Tests cover sub-cell rectangles, blocked intersecting cells, and live occupancy being ignored by the static query.

### Movement-class static pathing

WC3 derives static routing policy from authored `movetp` rather than reducing every non-flyer to ground movement:

| WC3 movement | Shared routing policy | Static cell is blocked when |
|---|---|---|
| `foot`, `horse`, `hover` | `CM_PATHING_UNWALKABLE` | `nowalk` |
| `fly` | `CM_PATHING_UNFLYABLE` | `nofly` |
| `float` | `CM_PATHING_UNSWIMMABLE` | `nowater` |
| `amph` | `CM_PATHING_REQUIRE_ALL \| CM_PATHING_UNWALKABLE \| CM_PATHING_UNSWIMMABLE` | both `nowalk` and `nowater` |

`M_UnitStaticPathingFlags()` owns that WC3 mapping. The resulting policy is carried through destination correction, direct/swept line tests, bounded A*, closest-reachable fallback, resumable flow fields, trained-unit exit placement, formation slots, Way Gate/ability movement helpers, and pathing-aware scripted repositioning. The generic `CM_*Walkable*` entry points intentionally remain UNWALKABLE wrappers so SC2 and existing ground callers do not change semantics; games that need another movement class use the mask-aware `CM_*Pathable*Flags` forms. Flow-cache identity includes the full pathing policy as well as adjusted target cell and collision radius, so fields for ground, air, sea, and amphibious movers cannot be reused across incompatible policies.

The shared router exposes generic pathing channels and a generic `CM_PATHING_REQUIRE_ALL` query modifier; it does not define a Warcraft amphibious unit type. WC3 uses the modifier to express the Warsmash rule that an amphibious unit may traverse a cell when either walking or swimming is legal.

Static entity footprints preserve the relevant Warcraft pathing channels when they are baked. A blocked/red pathing-texture pixel contributes both UNWALKABLE and UNSWIMMABLE, while the green channel contributes UNFLYABLE. `LoadTGA` stores source BGRA bytes in `COLOR32`, so the authored red/green channels are read through the loader's existing component layout. A walkable bridge deck clears the land-pathing block over its crossing lane without clearing the underlying water-pathing channel, so ground/amphibious movers can use the deck without turning it into a naval surface. A tall model does not automatically block flight; only authored UNFLYABLE pathing does.

Dynamic command-time obstacles use requester-aware WC3 collision domains rather than the static amph predicate directly. Ground movers overlap ground blockers, `float` overlaps sea blockers, `fly` overlaps air blockers, and `amph` overlaps both ground and sea blockers. For an amphibious request, a blocker in either overlapping domain stamps both selected static channels into the temporary query map, so `CM_PATHING_REQUIRE_ALL` rejects that occupied space. Precise move-time validation uses the same ground/sea/air domain contract. See [Naval Movement And Water Pathing](naval-movement.md) for the naval-specific contract.

### Harvest Worker Routing

Harvest/resource movement now follows the Warsmash-style collision contract directly: Gold Mine approach and all resource-return movement ignore **live units** while still respecting static pathing; tree approach keeps ordinary live-unit collision but applies the deterministic resource-worker queue/pass policy when another worker blocks the local approach. Building interaction routes use the worker's real collision radius, mover-owned bounded A* detours while shared fields rebuild, and footprint-aware near-side endpoints for Town Halls/Lumber Mills.

The movement validator is split accordingly: ordinary movement calls the full static + swept-unit check, while Harvest building legs exit after the static point/line checks. `SetUnitPathing(false)` remains the stronger existing override and still bypasses all collision. The Harvest policy does not mutate the shared pathmap, does not make buildings walkable, and does not alter Harvest/Return resource accounting.

Mine/drop-off interaction legs route with the **worker's collision radius** even though live-unit collision is disabled.  This matters when static pathing changes after an order starts: a newly constructed Farm invalidates the shared field, the bounded mover-owned A* immediately seeks a collision-sized detour while the replacement field is built, and the resulting field cannot choose point-only gaps that the Peasant's physical step validator will reject.  Return-resource legs do not use the blocked drop-off centre as their normal endpoint: they choose the innermost collision-safe ring around the authored footprint and select the point on that ring nearest the worker's current side.  The bounded mover path can detour around a Farm to that point without making a worker coming from the left circle around to the right side of a Town Hall or Lumber Mill.  Because the collision-sized path grid is cell-centred, that innermost legal route endpoint can still sit slightly outside the continuous footprint+one-step deposit threshold.  Reaching that exact near-side endpoint is therefore an explicit Return Resources handoff, matching the existing `flow_goal_reached` rasterization escape rather than making the worker bounce across the same staging cell forever.

Flow-generation handles remain monotonic across cache invalidation.  Route entities can retain an old generation after `CM_BakeStaticObstacles()`; recycling generation `1` after every rebuild could otherwise let that stale handle activate an unrelated newly built field.

The implementation reuses OpenRealm's direct-line, bounded-waypoint, and shared resumable flow routing rather than replacing the entire worker pathfinder. Warsmash itself keeps a longer per-unit waypoint path in `CBehaviorMove`; a full mover-owned path remains a later option if runtime cases still expose shared-field limitations.

## Walkable Bridges And Water

The WPM remains authoritative for horizontal movement: water cells with the no-walk bit are impassable, while the passable cells authored through a bridge form the only legal crossing lane. Bridge elevation is a separate game-side contract. `M_CheckGround()` starts with W3E terrain height, then checks the level's sparse registry of live `DestructableData.walkable` entities and uses the highest authored destructable Z whose pathing-texture footprint contains the unit. Dead, hidden/non-solid, and pathing-texture-less destructables do not supply ground.

Human01's river bridge confirms the split. Map object `LT05` (`WoodBridgeLarge45`) is `walkable=1`, `onWater=1`, has a 32x32 pathing texture, and is placed at `(1216, -960, -114)`. W3E terrain at its centre is `-170.8`, so terrain-only ground snapping puts a unit 56.8 world units below the deck even though WPM routing correctly accepts the crossing. Walkable surfaces are registered at map/runtime destructable spawn and unregistered on edict free; ground checks iterate bridges rather than every map entity.

Attack range against a building is measured from the attacker's collision edge to the building's authored no-walk footprint when `pathtex` is available. `skills/s_attack.c` therefore uses `CM_DistanceToPathingFootprint()` for building targets instead of requiring the attacker to enter weapon range of the blocked building centre. This is especially important for explicit force-fire on owned/friendly large buildings: centre-distance range checks make a melee unit orbit the footprint forever even though it is already beside a valid attack surface. Non-building targets retain the existing centre-distance attack check.

Lumber's unreachable-interior-tree detection is the narrower exception: `unit_changeangle_for_radius()` uses the Peasant's collision radius so Harvest can identify when the best legal approach to a blocked tree has genuinely been exhausted outside `HARVEST_RANGE`. Do not use that route-end signal for building interactions unless the route request also carries the behavior's interaction range.

Plain right-click movement is different from an interaction order: `move_selectlocation()` already assigns a collision-safe final waypoint, so `ai_move_walk()` may safely route that order with the mover's real collision radius. A distant temporary block does not cancel a plain move order. The unit keeps the order and retries local movement while the static route remains reachable; the existing near-goal settle rule is retained for an occupied final slot. If a completed collision-sized flow field reports the mover's component unreachable, the move may stop.

## Flow-Field Lifecycle

Game routing no longer uses the old lifetime quota of two synchronous whole-map flow-field bakes. That quota avoided repeated handheld stalls, but after it was spent a later uncached move order received generation 0 forever and generic steering fell back toward the raw target. A reachable order behind trees/buildings could therefore stop even though the static router could have found a route.

`G_RequestMovePathField()` supplies WC3 class geometry to the shared-cache miss path. A cache hit returns its generation immediately; a miss takes a place in a FIFO of distinct target/footprint/pathing-mask requests and returns 0 until its job completes. `G_RunFrame()` advances the active resumable reverse shortest-path job after entity simulation through `CM_ProcessPathJobs()`. On completion, the next queued request is promoted before later entity thinks can claim the slot. Repeated requests for the same field share one queue entry, and a moving goal updates its queued destination without losing its place. This prevents early entities from repeatedly taking the slot after each completion and starving later movers. Static-pathing invalidation clears both the active job and pending requests.

The default relaxation budget is 65,536 queue pops per frame and is runtime-tunable with:

```sh
+set wc3_path_work_budget 65536
```

The value is clamped to 256-65,536. The doubled Warcraft III default is a trial aimed at shortening the route-wait frames seen when new and moving units add distinct destinations to the shared FIFO. It keeps SPFA relaxation bounded while allowing each queued destination to make more progress per frame. Only one miss is built at a time; later destinations wait in FIFO order. Route-wait diagnostics can report active flood progress and queued destination fields.

For temporary per-mover wait diagnostics, enable `wc3_route_wait_debug 1`. The log emits one `WC3_ROUTE_WAIT begin` and matching `end` line per route-field wait, with mover and goal identities, wait duration, position delta, active job target, and FIFO depth. Set it back to `0` after capturing the behavior.

Nearby detours do not wait for that whole field. `G_FindUnitMovePathWaypoint()` runs the [ported retail fine-search policy](retail-pathfinding-engine.md#retail-fine-search-drives-nearby-detours) for endpoints within48 pathing cells and charges at most2,048 queue attempts. Static endpoint correction uses the retail1/2/3/4-cell footprint; interior checks and original next-point/progressively-farther waypoint selection use [sampled segments](retail-pathfinding-engine.md#retail-segment-sampling-and-waypoint-selection). Location-order queries now include [idle ground-unit rectangles](retail-pathfinding-engine.md#idle-objects-affect-nearby-move-routes), skip moving neighbours and exclude the mover/target. Move retains the turn until reached, the target changes, or current static/live occupancy rejects its segment. Publishing a shared field does not discard it. Shared fields remain static; long routing and interaction abilities retain their existing policy. Location orders also retain [nearest partial routes](retail-pathfinding-engine.md#nearest-partial-routes-survive-blocked-goals) after fine-search exhaustion, while keeping their original destination.

When neither fine/local steering nor a resumable field resolves a heading, `unit_changeangle*()` leaves both `movement.flow_generation == 0` and `movement.flow_direct == false`. `unit_moveindirection()` treats that pair as "no heading resolved this tick" and does not commit a step. A clear static corridor with an occupied live goal retains collision-aware local steering while the field builds. This shared guard is important: a caller must never turn a pending route into movement along the unit's stale facing.

Plain Move also keeps the stand presentation while that pair is clear. The order switches to walk only after direct steering or a completed flow field supplies a heading; starting the walk pose at order submission made the old facing look like an incorrect first turn during a long route build.

The stepper rejects a collision-free candidate along a turn-lagged facing when it points more than 90 degrees away from the resolved route heading or increases distance to the active goal. The old stepper accepted the facing candidate first, so a short scripted cinematic move could advance in the wrong direction while the unit was still rotating; Human02Interlude then left Jaina on Antonidas's later ride-off path. Construction displacement uses its temporary exit point as the active progress goal until it is reached, after which the unit resumes its original order. `unit_commit_step()` and point Move arrival commits keep the network/render `origin` synchronized with authoritative `origin2` for the same reason.

The route-job fairness regression is `wc3_movement.attack_chase_waits_through_competing_route_jobs_then_resumes`; it keeps distinct route requests arriving while asserting the attacker gets its earlier queued field and resumes before those later requests drain. `wc3_movement.turn_lag_does_not_step_away_from_route_heading` covers turn-lag steering. Both live in `games/warcraft-3/game/tests/t_movement.c`. Run both game variants with:

```sh
make test-wc3-engine WC3_PATTERN='wc3_movement.*'
```

`CM_BuildHeatmapForRadius()` remains the synchronous API for tests/tools that explicitly require a completed field. Production movement goes through `M_RefreshHeatmap()` -> `CM_RequestHeatmapForRadius()`.

Production services the shared incremental build with 65,536 queue pops per 10 Hz server frame. A 256x256 open field can therefore complete within one frame. Override `wc3_path_work_budget` for slower targets. Complete destination-rooted publication remains the long-route fallback; the bounded accelerator is what removes that publication delay from nearby obstacle detours.

WC3 Move requests fields through `G_RequestMovePathField()`, using the verified1/2/3/4-cell class bounds. Field expansion, flow sampling, destination correction and closest-reachable fallback consume the same geometry as Move. Cache keys include the adjusted target cell, half-open footprint offsets and blocked mask; `G_ActivateMovePathField()` checks those bounds rather than allowing a small radius difference to cross a class boundary. Shared `CM_BuildHeatmapForRadius()` and radius request APIs retain symmetric ceil-radius geometry for their existing callers; zero-radius `CM_BuildHeatmap()` routes a point. See [long field geometry](retail-pathfinding-engine.md#long-fields-use-the-same-class-geometry-as-move).

Flow vectors only descend to a strictly lower heatmap price for both collision-sized and radius-zero point fields. The adjusted goal cell therefore has a zero vector instead of pointing back out to a higher-cost neighbour. This matters for shared interaction routes such as Gold Mines and resource drop-offs: an outward point-flow at the adjusted cell makes every worker sharing that field orbit the same wrong location. Cached prices retain `INT_MAX` for cells that the completed field cannot reach. `CM_FlowReachedGoal(generation, x, y)` identifies the adjusted goal cell, while `CM_FlowCanReach(generation, x, y)` distinguishes a disconnected cell from a zero produced by interpolation near the goal.

Harvest interactions still own their final range/contact semantics. Gold Mine approach requests a collision-sized static route toward the Mine, ignores live units at steering/move time, and continues toward the real Mine after the adjusted route goal until the Mine footprint/contact check admits the worker. Because construction rebakes static pathing and invalidates cached fields, a Farm built across the lane causes a collision-sized rebuild; while the shared field is pending, the mover-owned bounded A* waypoint provides the local detour.

Return Resources uses a stronger endpoint contract. `CM_FindInnerApproachPointToFootprintForRadius()` marks pathmap cells near the authored footprint, chooses the **innermost collision-safe ring**, then uses distance from the worker only to select the near side of that ring. Gold and lumber return route to that point with static-only collision. If grid/radius quantization leaves the innermost legal cell a few world units outside the continuous footprint+step threshold, reaching the exact endpoint is accepted as the deposit handoff instead of bouncing toward the blocked building and back. Longer detours fall back to the shared collision-sized field. `CM_PathCellWorldSize()` supplies routing-grid scale only; it is not a gameplay range constant.

The approach mask stores a small footprint-proximity rank so the inner ring can be selected without rescanning every authored footprint pixel for every candidate. Do not replace it with a simple footprint bounding box: sparse and irregular pathing textures require distance to the actual blocked pixels.

## Retail Move Destination Behavior

Two defects explained the Human01 fence report and units getting stuck behind trees or towers:

1. `CM_LineIsWalkableForRadius()` used ordinary Bresenham stepping. A 45-degree step from one cell to the next checked only those two cells, so the direct shortcut accepted an `ox/xo` arrangement even though both cardinal side cells were blocked. The shortcut now requires both side cells to be legal, matching heatmap expansion and `compute_flow_at()`.
2. Location steering requested a point-sized route while `move_is_valid()` rejected positions using the unit collision radius. The field could therefore direct a unit through a gap that its body could not occupy. Move, Patrol, and Attack-move now use `self->collision` for the direct line and field; interaction behaviors retain radius zero.

When a clicked destination is in another static connected component, the destination-rooted field reports the mover cell unreachable. `CM_ClosestReachablePointForRadius(from, target, radius)` floods the mover component with the same radius and diagonal rules, then chooses its legal cell nearest the click. The location order retargets its private waypoint once and follows a normal field to that point. This avoids both failure modes of the old behavior: freezing at the order origin and sliding forever along the blocking wall.

Ordinary destination fields remain incremental and frame-budgeted. The mover-component flood is synchronous only after a completed destination field proves the click unreachable, so this exceptional recovery does not add input-time work to reachable orders.

The current router is now deliberately hybrid. Direct collision-sized lines handle open ground, bounded per-mover A* handles nearby static detours, destination-cached integration fields amortize long routes shared by groups, and local avoidance handles live units. This is closer to retail's split between mover-owned route state and a global pathing system without claiming its unrecovered accelerator implementation.

### Route-wait diagnostics

Route-wait begin/end records are compiled only with `WC3_DEBUG_ROUTING=1`. In
that build, set `wc3_route_wait_debug 1` to write `WC3_ROUTE_WAIT` records to
`stderr`; leave it at `0` to keep the diagnostics quiet.

```sh
make WC3_DEBUG_ROUTING=1 openwarcraft3
```

### Retail Game.dll path audit

This is the historical ROC demo audit. The newer
[retail 1.27 investigation](retail-pathfinding.md) identifies two A*-family
searches, a four-level adaptive grid accelerator, footprint classes, and the
remaining hierarchy/clearance questions. Keep its addresses separate from the
demo offsets below.

The ROC demo `data/Warcraft3demo/Game.dll` (build 4486, SHA-256 `286823c37a1083e91f07d040e46a9df7af4c4952e01fcbba460589bd4e297654`) retains RTTI for `CAbilityMove`, `NIpse::CLrPathingSys`, and `NIpse::CLrPathingAcc`. `CAbilityMove` installs its vtable at `Game.dll+0x102898`. The path constructor at `+0x458040` initializes a roughly 0xb0-byte persistent object, including two 32-byte containers at `+0x2c` and `+0x4c`, coordinate/state fields, and a pathing-system pointer. Mover setup at `+0x466aa0` allocates and stores one such object. Submission at `+0x458670` resets route state and copies the requested coordinate into both current and destination fields.

The update at `+0x458930` checks flags at `+0x80`, can return a pending state from a countdown at `+0x8c`, invokes progression routines at `+0x457da0` and `+0x457f20`, and exposes multiple result states to the movement caller at `+0x4661d0`. `+0x457da0` appends 8-byte coordinate pairs to the object's route container. `+0x457f20` consults one of two global indexed arrays through a signed selector and a `-2` sentinel before advancing the route. Together with the separate `CLrPathingAcc` and `CLrPathingSys` types, this establishes persistent per-mover progress backed by global accelerated pathing data. It does not establish whether the accelerator is A*, hierarchical sectors, a portal graph, or another Blizzard-specific structure.

Group movement at `+0x46a130` and `+0x46a330` derives per-mover coordinates and path flags before updating each path object. Retail routing is therefore not a point-only line test followed by movement that independently rejects the unit footprint, nor is there evidence that every order waits for a complete destination-rooted map flood.

This supports the direction of commit `4bad783d`: using the mover's collision size consistently and resolving an
unreachable click to a legal endpoint are closer to retail's per-mover, adjusted-endpoint architecture than routing a
point toward an impossible destination. The binary does not establish that OpenRealm's bounded A*, nearest-cell flood,
SPFA integration field, four-slot cache policy, or exact `ox/xo` diagonal test matches Blizzard's algorithm. Treat the
accelerator as a behaviorally supported approximation: it reproduces immediate nearby route output and mover-owned
progress while retaining OpenRealm's group-friendly cache for long routes.

### Retail and OpenRealm algorithm outline

| Stage | Retail evidence | OpenRealm |
|---|---|---|
| Open ground | Route setup can retain or reset mover path state by destination and mode | Collision-sized Bresenham line; no search |
| Nearby detour | Persistent mover path object emits coordinate pairs; global accelerator is consulted | Verified retail fine-search ordering on current radius/corner graph; one persistent visible waypoint |
| Long/shared route | Global `CLrPathingSys` and `CLrPathingAcc`; exact sharing policy unrecovered | Four LRU destination/radius integration fields, built backward with SPFA |
| Dynamic units | Per-mover path flags and updates | Swept-circle movement plus deterministic local avoidance; not baked into static routes |
| Scheduling | Countdown/pending and multiple result states prove resumable progress | Ordinary unit paths enable adaptive search with400 attempts and refine local legs with700 fine attempts; generic moverless queries retain2,048; complete fields have a configurable per-frame queue budget |

The speed difference was primarily work selection. Before the accelerator, one nearby cache miss cleared every route
node, relaxed the complete reachable component, then copied one integer per map cell before movement could start. A
256x256 diagnostic trace printed `cells=65536` at both build start and publication. The accelerator touches only nodes
reached by the bounded fine search and clears a fixed sparse hash instead of map-sized node storage. Its contiguous
node/heap arrays bound scratch space to832KiB, but no retail evidence identifies an explicit
"L2 cache" technique.

The same corner rule is applied to direct routing and movement steps: a diagonal
segment is rejected when either cardinal side of the crossed cell corner is
blocked. `CM_LineIsWalkableForRadius()` and `M_MoveIsValid()` therefore agree
with the flow flood instead of allowing a direct order or a long simulation step
to squeeze through a diagonal wall corner. With no loaded path map, line tests
remain permissive, matching the existing no-map movement behavior.

## Retail Lumber Fallback

Retail Warcraft III continues lumber gathering when the explicitly clicked tree is alive but buried inside an unreachable group of trees.

OpenRealm keeps the clicked tree authoritative while routing can still approach it. If the worker reaches the collision-sized flow field's adjusted goal but remains outside `HARVEST_RANGE`, or the active field has no route from the worker's component, Harvest treats that approach as failed and selects a replacement tree. The replacement search excludes the failed tree and prefers the nearest live tree with a directly reachable legal harvest approach; a tree already within `HARVEST_RANGE` is immediately eligible.

This fallback is gameplay behavior in `s_harvest_lumber.c`, not a pathfinder rule. Normal movement, attack, patrol, item pickup, and spells do not acquire a different target when routing fails.

Multiple Peasants may legally harvest the same tree. Harvest now keeps the closest direct legal chop point instead of pre-assigning angular lanes. Dynamic separation is handled by the resource-worker local avoidance policy: a worker blocked by same-direction resource traffic queues briefly, while crossing or persistently pinned traffic uses deterministic right-first bounded passing. Static flow fields remain shared and occupancy-free, and the final tree approach no longer scans all edicts to allocate or reserve lanes. See [worker-crowd-routing.md](worker-crowd-routing.md) for the Human02 simulation and policy contract.

## Confirmed August 2026 Regression

A Human02 handheld trace for a Peasant ordered to an interior tree showed the worker reaching the forest edge and oscillating indefinitely at roughly 352-358 world units from the target. Every tick reported `blocked_frames=0` and, after instrumentation was extended, `flow=0 flow_goal=0`.

Two routing defects originally combined to prevent Harvest from ever receiving a usable failure/exhaustion state:

1. The lumber direct-line gate used `CM_LineIsWalkable()` as a zero-radius point test while the actual movement step used `CM_PointIsPathableForRadius(..., self->collision)`. A line could therefore be declared clear even when the Peasant could not physically fit along it. The fix remains scoped to behavior contracts that can safely use a collision-sized route; gold-mine/building interactions complete at their own contact/range boundary rather than at a flow field's adjusted goal.
2. The legacy generic heatmap build cap could permanently deny later route fields. The current implementation removes that lifetime quota entirely and uses resumable game routing instead, so lumber and ordinary movement do not depend on which route misses happened earlier in the match.

A third defect made a reached flow goal unstable: `compute_flow_at()` blended reachable neighbours including equal/higher-cost cells, so an adjusted goal beside asymmetric blocked geometry could point outward. All fields now follow only lower prices; radius-zero interaction routing then hands the final approach back to the behavior at the adjusted route end.

Do not reintroduce a distance-only timeout around Harvest to hide these routing failures. Fix and expose the routing state first, then let Harvest decide whether to retarget.

## JASS repositioning: `SetUnitPosition` vs raw X/Y

`SetUnitPosition` and `SetUnitPositionLoc` stop the current order, then admit the
requested point with the recovered policy2 fine-cell rings, bounded to32 rings.
The search uses the mover's current movement mask, quantized footprint and
same-level terrain callback, excluding its own occupancy and including live
neighbours. Public `CreateUnit` and `CreateUnitAtLoc` share that admission,
then initialize Move from the original fresh-mover sentinel before publishing
world XY. Their fractional results can differ from the original request even
when its first cell is legal. See [spawn admission and native pose](retail-pathfinding-engine.md#public-spawn-admission-and-initial-mover-pose).

`SetUnitX/Y` retain the scalar writer without the admission search or order
retirement. The legacy64-unit spiral in `G_FindUnitUnstuckPosition` remains for
item drops, cargo, summons and Way Gates until their own producers are recovered.

This distinction matters for campaign scripts. The Prologue01 Thrall investigation showed `Othr` alive and renderer-visible at the
scripted destination while a no-depth/white diagnostic exposed his geometry through a nearby structure. The old native assigned X/Y
directly, unlike Warsmash, so blocked scripted destinations could leave a unit inside authored building pathing.

## Diagnostics

Runtime Harvest logging is off by default:

```sh
+set wc3_harvest_path_debug 1
```

prints Harvest transitions and fallback reasons. Level 2 adds per-approach route state and reports when a requested tree field becomes ready:

```sh
+set wc3_harvest_path_debug 2
```

Lumber routing uses the `WC3_HARVEST_PATH` prefix. Gold-mine entry uses `WC3_GOLD_PATH`, and gold return/deposit uses `WC3_GOLD_RETURN`. For map-specific mine/model mismatches, level 2 also emits `WC3_GOLD_GEOMETRY`; level 3 adds the pathing-texture rows as `WC3_GOLD_FOOTPRINT`. Generic resumable routing does not emit per-build debug lines. A healthy interior-tree fallback should progress through a nonzero flow generation and then one of:

```text
fallback ... reason=route_goal_out_of_range
fallback ... reason=route_unreachable
fallback ... reason=movement_blocked
```

followed by `start` and `reached` for the replacement tree.

## Verification

Focused tests live in `games/warcraft-3/game/tests/t_pathfinding.c` and `t_movement.c`. They cover:

- cache separation by collision radius;
- cache separation between incompatible movement policies, including ground/UNWALKABLE and flying/UNFLYABLE fields;
- WPM UNWALKABLE, UNFLYABLE, and UNSWIMMABLE point/line queries;
- blocked/red static path-texture pixels rejecting both ground and `float` movement while green-channel pixels contribute UNFLYABLE;
- `float` static routing through swimmable cells and around unswimmable land;
- `amph` static routing accepting cells that are walkable or swimmable and rejecting cells that are neither;
- requester-aware dynamic blockers for ground, sea, air, and amphibious movers;
- `SetUnitPosition`/unstuck collision uses the same domains, so `float` ignores ground units and avoids sea units;
- flyer move validation accepting UNWALKABLE-only cells and rejecting UNFLYABLE cells;
- resumable cache misses serialize without losing a later destination;
- collision-radius-aware line walkability;
- water rejection with an explicitly passable bridge lane;
- walkable-destructable deck elevation with terrain restoration outside its footprint;
- direct-line and flow rejection of diagonal `ox/xo` corner cuts;
- rejection of a corridor too narrow for the mover;
- collision-sized Move, Patrol, and Attack-move route selection;
- collision-sized Move routing around a long wall;
- exact reachable clicks and closest reachable points across a disconnected wall;
- collision-radius expansion of the closest reachable boundary;
- end-to-end settling at the nearest reachable point for a disconnected click;
- a zero outward vector at the flow goal;
- end-to-end lumber retarget from a buried clicked tree to a reachable edge tree;
- gold-mine entry through an authored blocking mine footprint;
- gold return/deposit at an authored Town Hall footprint corner;
- lumber return to a Town Hall through an authored blocking building footprint;
- a distant temporarily blocked plain move keeps its order alive while near-goal jitter still settles;
- Public `SetUnitPosition` / `SetUnitPositionLoc` retire orders and use verified fine-cell admission; `CreateUnit` / `CreateUnitAtLoc` share admission and preserve fresh-mover scalar initialization. `SetUnitX/Y` retain the scalar writer without that search.
- flying `SetUnitPosition` unstuck checks ignore UNWALKABLE-only cells and obey UNFLYABLE cells.

Run when validating locally:

```sh
make test-wc3-engine WC3_PATTERN='wc3_api.set_unit_position*'
make test-wc3-engine WC3_PATTERN='wc3_pathfinding.*'
make test-wc3-engine WC3_PATTERN='wc3_movement.plain_move_*'
make test-wc3-engine WC3_PATTERN='wc3_movement.blocked_move_*'
make test-wc3-engine WC3_PATTERN='wc3_movement.*'
make test-wc3-engine WC3_PATTERN='wc3_movement.lumber_*'
```

### Interaction-owned route endpoints

Radius-0 point fields remain available to callers whose interaction contract specifically needs point routing. Attack chases instead use the attacker's collision radius so their field cannot route through a gap the move-time validator rejects. Both forms strictly descend to their adjusted legal route endpoint. Once that endpoint is reached, `unit_changeangle()` exposes `flow_goal_reached` and steers toward the real entity target; the attack behavior continues to use its own range test. Gold Mine entry/return may hand off immediately at the route goal or after Move's bounded near-goal settle detector proves a crowded worker has stopped making progress at the interaction edge. This keeps routing monotonic without turning a distant blocked route into a successful interaction.

An unreachable field can resolve an approach inside the mover's connected
component even when the target center lies in another component. That approach
belongs to `movement.flow_fallback_approach`. Only an entity marked
`SVF_MOVE_WAYPOINT` may replace its coordinates with the recovered endpoint.
Attack and Follow must retain the target's authoritative position, footprint,
spatial publication and identity while using the mover-owned approach.

The former recovery branch treated every positive-radius route as a location
order. Once Attack started requesting collision-sized routes, it could write
the closest reachable point directly into `goalentity->s.origin2`. In a local
NightElfX01 capture, Maiev approaching the Elven Gate changed the gate from
`(1984, -6592, 160)` to `(1968, -6704, 160)`. Its spatial bounds were not relinked,
so presentation and collision geometry also disagreed. A direct `T_Damage` or
`S_ResolveAttackHit` probe did not reproduce this: the corrupting call was
`unit_changeangle()` during attack approach. This explains why hitting scenery
appeared to move only some props; the failure depends on route connectivity,
not the destructable's model or hit animation.

`wc3_combat.unreachable_attack_keeps_target_geometry` drives issued Attack
orders against destructable and unit targets across a separating wall. It
checks unchanged XYZ, facing, bounds and target identity through recovery,
both when weapon range cannot reach the target and when the approach allows
hits across the wall. `wc3_movement.unreachable_move_settles_at_closest_boundary`
preserves the private-waypoint recovery case. The corrected local campaign
capture retains the gate at its original XYZ through approach and repeated
hits (life 100 to 35); this verifies the engine regression, not additional
retail fidelity.

```sh
make test-wc3-engine WC3_PATTERN='wc3_combat.unreachable_attack_keeps_target_geometry'
make test-wc3-engine WC3_PATTERN='wc3_movement.unreachable_move_settles_at_closest_boundary'
```

## Authored movement profiles and public speed

Original and custom map UnitData rows now retain movement type, turn rate and
movement window through normal unit creation. Custom types select the same
ground/water/amphibious/flight query masks as stock units. UnitBalance speed,
minimum and maximum editor fields accept their authored integer representation.
Move steps, group caps and `GetUnitMoveSpeed` share the effective speed consumer;
`GetUnitDefaultMoveSpeed` keeps the immutable profile value. Explicit zero speed
is clamped by the authored limits and survives save/load; disabled movement
ignores setters. Misc unit/building limits remain map data. See
[the speed producer port](retail-pathfinding-engine.md#authored-speed-limits-reach-move)
for the original/live evidence and remaining hero, modifier and clock gaps.

## See Also

- [Unit Altitude And Support Surfaces](unit-altitude.md) — vertical support surfaces share the WPM terrain classification but are independent of horizontal routing.
- [Naval Movement And Water Pathing](naval-movement.md) — `float`/`amph` reuse this router with water-aware pathing policies and movement collision domains.

### Shared SC2 movement consumers

SC2 and WC3 include the same `server/sv_routing.c` in their server worlds.
`CM_AccelerateRoute` retains the mover-owned waypoint; `CM_SlideRoute` is WC3's
bounded generic left/right deflection loop extracted from `unit_desired_heading`.
WC3 retains its speed-priority ring limit, resource-worker policy, collision
callbacks, and turn-rate handling. SC2 calls the shared search for blocked static
steps instead of repeatedly rejecting the same flow direction at a corner.
See [SC2 selection and control](../starcraft-2/selection-and-control.md) for the
separate snapshot-precision defect that made both ground and cinematic movement
appear to advance in whole cells.

Retail-backed movement masks now distinguish authored float40 and amph80 from
ground02 and fly04. Map loading derives amphibious blockage from combined
walk/float blockage; baked ground footprints and command-time unit categories
block the appropriate lanes. See [mask evidence](retail-pathfinding-engine.md#authored-movement-masks-reach-terrain-and-object-queries)
for the stock profile table, original/C checks and remaining support-surface gaps.

Ordinary public point Move now uses the [verified retail arrival range and heading](retail-pathfinding-engine.md#point-move-arrival), stops at its predicted pose and publishes zero velocity. It no longer snaps to the clicked endpoint; internal approach owners retain their separate arrival policies.


Public WC3 speed drops now clamp the current movement vector immediately
through the verified retail fine-coordinate arithmetic. Higher caps preserve
the vector; eight following movement samples remain bit-identical across
save/load. See [the original and repeated live evidence](retail-pathfinding-engine.md#speed-drops-clamp-existing-velocity-immediately). Nonzero-elapsed owner-clock
production and temporary speed-effect restoration remain open.


Move now consumes authored AIms flat bonuses through the largest usable
native/item contribution. The current getter reflects inventory changes;
physical stepping retains the bonus last published by a setter or accepted
Move order, as observed in retail. That retained state survives save/load. See
[flat bonus publication evidence](retail-pathfinding-engine.md#flat-bonuses-retain-their-publication-state).


Move now retains native fine position between accepted steps and across save/load.
Public `SetUnitX/Y` also reprojects both fine axes through the retail delta
operation, preserving velocity, facing and the active order. Even a setter
that writes the same visible coordinate can change the next movement words.
See [axis-position evidence](retail-pathfinding-engine.md#public-axis-position-writes-retain-the-next-move-step).
Point Move now consumes [primary-clock prediction](retail-pathfinding-engine.md#primary-clock-reaches-move-and-predicted-positions)
between callbacks. Other ability cadence and the full original route owner remain open.


Fine target routing now retains a suppressed ground-unit target's identity during
perimeter queries and returns the original approach-node centre. Moving target
records remain observable. Ordinary point routes still retain their exact world
destination words. The2,304-case original/C matrix verifies the search exit,
work and parent chains; overlapping runtime target/foreign-blocker link order
remains FINE-01.6. See [target identity exits](retail-pathfinding-engine.md#target-identity-exits-reach-the-engine).

Public `SetUnitPosition/Loc` now retire Move/Patrol, queued orders and group
routing before writing the retained fine pose. Save/load preserves the resulting
stationary state. [Original Stop/placement evidence](retail-pathfinding-engine.md#forced-position-stop-reaches-the-engine)
includes repeated native words and actual engine regressions; blocked placement
legality and the remaining forced writers are still tracked separately.

Blocked public ground placement now uses retail32-unit cell rings and the
first accepted cell centre, with the authored nearest-vertex terrain-level
condition. [Original evidence and limits](retail-pathfinding-engine.md#blocked-placement-reaches-the-engine)
include six exact public destinations; bridge overlays, map edges and the
separate embedded Stop recovery remain open.

The path owner now has a saved retail two-word random generator. Public JASS
seeded random queries use its exact words, replacing libc range/modulo draws;
its overlap-direction helper matches original scalar sine/cosine output.
Authored repulsion now consumes this owner state through Move; original
proximity cell order and multi-neighbor draw chronology remain open. See [engine payoff17](retail-pathfinding-engine.md#deterministic-owner-random-state-reaches-public-natives).


Ordinary location detours now retain the complete native fine curve, first raw
successor and0.49-cell progress threshold. The actual Move callback matches a
controlled original wall detour's34 position/velocity/heading steps exactly,
including turn stops and natural arrival; save/load retains the same remaining
motion. See [complete fine-route detour](retail-pathfinding-engine.md#retained-fine-routes-reproduce-a-complete-retail-detour)
for the fixture and remaining world-origin/scheduler/adaptive/yield limits.

Move's retained detours also carry the published native source into reconstruction,
progress and steering. A nonzero world origin no longer changes the frozen
retail detour's motion words. See [native route inputs](retail-pathfinding-engine.md#native-route-inputs-survive-world-projection)
for the four-origin regression and remaining producer scope.

Ordinary unit Move now uses the original enabled adaptive stage at all distances,
with400 adaptive/700 fine attempt limits. This fixes the nearby winding-maze
stall caused by the inherited48-cell admission gate. Full original route buffers
match across four footprints; the public engine Move reaches its destination and
repeats600 saved continuation frames exactly. See
[activation defaults and budget evidence](retail-pathfinding-engine.md#ordinary-path-defaults-enable-adaptive-routing).


Ordinary Move now collects next-step moving blockers and applies the original
committed-velocity yield policy: requester waits at least4 eligible advances,
or the slower same-player peer waits at least20. Candidate order and earlier
peer side effects are retained. Save71 preserves live waits and blocker
references; removal clears references without cancelling the remaining delay.
The public Move/save/removal regression and two exact retail decision repeats
are recorded in [ordered moving waits](retail-pathfinding-engine.md#ordered-moving-waits-reach-ordinary-move).
Original overlapping cell-link order, group-bit8 producers, caller retry
composition and full crowd trajectory parity remain open.

During countdown, Move turns toward the final caller goal using its retained
predicted native source. It no longer reconstructs that direction from rounded
world coordinates. Four stopped oblique ticks match retail and survive public
save/load exactly before movement resumes. See
[native waiting headings](retail-pathfinding-engine.md#waiting-headings-preserve-the-native-caller)
for complete original callers and two live repeats; acquisition-time waypoint
selection and peer retry remain separate work.


Peer20 fine-route retries now restore the final-goal heading and stop the
requester before another step, clear its fine leg and retain its coarse route
for the next thinker. Save71 stores retry_count and all ordinary wait/route
state. The public-order regression resumes through actual frames and repeats
1440 state words after load. Source-footprint admission is still required before
porting every idle/terrain/ineligible retry: circle-valid sources can overlap
quantized fine footprints. See [the verified peer retry and remaining scope](retail-pathfinding-engine.md#payoff32-blocked-fine-leg-retry-and-retained-coarse-plan).


Public JASS group point orders now share a bounded twelve-member snapshot
dispatch across string, numeric ID, Loc and numeric ID Loc forms. Numeric forms
previously returned false; string forms exceeded retail's twelve-member limit.
The original shared CMoveReq and complete twelve-member physical movement now
have engine comparisons; wider eligibility and producer lanes remain open. See [retail group
admission](retail-pathfinding-engine.md#public-group-point-orders-admit-twelve-members).

Selected ground Move now uses retained physical cohorts for ordinary input,
idle Shift, pending Shift and mixed active/idle Shift. Mixed admission preserves
an active head while starting the idle peer immediately under one common
request. Formation is assigned during physical activation. Four complete native
journeys match1510 normal-frame clock/position/velocity/facing commits and1020
saved suffix commits; early-finished peers and later cohort joining are both
covered. Save79 retains the existing histories and owner generations. See
[mixed Shift integration](retail-pathfinding-engine.md#mixed-active-and-idle-shift-share-one-submitted-request).

Two independent active singleton point owners also preserve their current heads
when queuing one common selected ground request. Later physical acquisition
creates and joins one cohort;1022 original motion commits and844 saved suffix
commits match exactly. This strengthens the same engine admission port. See
[independent active owners](retail-pathfinding-engine.md#independent-active-owners-accept-the-same-pending-ground-request).


Ground-unit Smart Follow now uses Move-owned target cohorts. It approaches,
retains its user head during persistent following, refreshes cached destinations
on the recovered countdown and retains same-bucket sub-cell changes. Target
arrival adds both collision radii to Misc.FollowRange. The original moving-target
speed-change journey matches1015 absolute commits plus2595 saved suffix commits.
Save80 retains target generation, range and refresh state. Structures, flight,
visibility/loss and delayed replan producers remain explicitly open. See
[Smart Follow integration](retail-pathfinding-engine.md#smart-follow-tracks-a-moving-target-through-a-speed-change).

Ground Follow now cancels synchronously when its target dies or is removed,
detaching its physical owner before stand. Actual original target pool/public
handle reuse never silently adopts the replacement. The public death and
removal journeys each match948 normal-frame commits and1746 Save80 suffix
commits, including a save while the replacement exists but Follow is idle.
[Payoff48 evidence](retail-pathfinding-engine.md#follow-cancels-synchronously-before-target-pool-reuse)
records the exact scope; save format80 is unchanged.

Public target axis teleport now keeps a retained point Move through turn waits;
its retail fine arrival/retry policy owns completion instead of legacy near-goal
settling. Four Follow teleport journeys match4091 normal commits and7872 saved
suffix commits. A test-only commit observer records movement before same-clock
JASS writes, and saves use completed-owner boundaries. Save80 is unchanged.
[Payoff49 scope and evidence](retail-pathfinding-engine.md#follow-tracks-public-target-teleports-without-premature-point-settling)
leaves collision resizing open.

Public Chaos now changes unit type and collision in place after its authored
requirements are met. Existing Follow groups retain their range; new nearby
approaches use retail's half predicted edge distance and persistent Follow uses
the resized radii. Five normal journeys match5075 commits and9725 saved suffix
commits, including saves before deferred/research-gated morph. The retained
ability itself owns pending work; removing it cancels the morph, and successful
resolution consumes it. Save format80 is unchanged.
[Payoff50 scope and evidence](retail-pathfinding-engine.md#follow-retains-active-range-and-admits-resized-targets-with-half-edge-approaches)
leaves moving-unit occupancy, other locomotion/body families and exact automatic
morph timing open.


Public point Move now retains its goal across a moving Chaos type rebind. Move
commits the old pose, refreshes collision-sized routing and occupancy, then
reissues the retained point task after the observed handoff delay. The new type's
authored speed replaces the scripted old speed. All nine boundary-radius journeys
match original absolute motion, including the90-second scalar timer/owner drift
and Save81 continuations. See [moving radius and owner deadlines](retail-pathfinding-engine.md#moving-radius-changes-retain-point-motion-and-scalar-owner-deadlines).
Other task/locomotion families and shared-group maximum-radius mutation remain
open; this does not establish full retail search or movement parity.

Ordinary point Move preserves the clicked destination even when terrain blocks
it. The group/member routes retain their own intermediate and adjusted goals;
fine search can finish with a partial route, retry on the next owner, and force
range acceptance while still requiring the final turn. The captured blocked
5x5 goal matches all207 production engine commits and46 Save82 suffix commits
per variant. See [blocked point goals](retail-pathfinding-engine.md#blocked-point-goals-retain-the-click-through-retry-and-forced-arrival).


Ordinary ground point Move also retains outside-map clicks. Routing clamps
to the world bounds inset by four path cells, while the public waypoint stays
unchanged. The admitted route owns steering and arrival; a generic unreachable
field cannot rewrite it. The complete outside-west journey matches191 retail
commits and47 Save82 suffix commits per variant. See [outside point goals](retail-pathfinding-engine.md#outside-point-goals-clip-routing-while-retaining-the-public-click).


The [stationary captain pair](retail-pathfinding-engine.md#stationary-captain-pair-private-followers-to-shared-arrival)
now matches369 complete native physical commits: independent target followers
transition into one shared home formation only after both range callbacks enter.
Move retains the virtual target's one-cell search region, exact callback clock,
roster order and saved entry state. Save84 continuations cover both private
owners, shared admission, final survivor, cancellation and bot-free restoration.
Larger/mixed-radius rosters, moving captains and native owned-pool reuse remain open.


The [mixed captain and blocked-home extension](retail-pathfinding-engine.md#mixed-captain-pairs-and-blocked-home-retries)
now uses retail point placement for virtual captains and temporary actor rectangle
exclusion during coarse admission. Footman/Knight369 and blocked pair524 complete
native commits match, including retained shared footprint, natural retry and
saved forced arrival. Range listeners use the admitted actor's position; the
later shared order retains the authored home. General dynamic coarse cell-link
updates, larger captain rosters and moving captain behavior remain open.


Captain recruitment now follows current owned-pool insertion order, including
true owner transfer and lower-slot reuse. Same-owner assignment preserves
priority; partial AddAssault retains the first recruit and prepends the next
captain member. Save85 preserves these producers. Four complete original
controls match 908 physical and 4344 saved continuation commits. See
[owned-pool ordering](retail-pathfinding-engine.md#captain-owned-pool-order-survives-transfer-and-reused-slots);
larger rosters and live captain membership changes remain separate gaps.

Stationary captain-home admission now supports three verified ground recruits.
The all-entered gate hands their private followers to one shared point group,
whose active membership and retry budget survive completion and save/load.
[The complete three-member journey](retail-pathfinding-engine.md#stationary-captain-three-member-shared-journey)
matches 608 retail commits and 2,352 saved continuation commits. Rosters beyond the verified thirteen-member boundary still report the unresolved
handoff; cross-batch shared parameters and live membership remain explicit extensions.


Stationary captain admission now spans the native twelve-member boundary.
[The complete homogeneous thirteen-member journey](retail-pathfinding-engine.md#stationary-captain-thirteen-member-batch-boundary)
uses private followers until all13 enter, then creates twelve-member and singleton
physical groups in roster order. A replacement path clears the previous fine-search
admission timestamp. All3647 retail commits and15768 saved suffix commits agree,
with public placement using the original W3E levels and normal startup PRNG.
The mixed-radius cross-batch shared owner remains GROUP-03.4.6.2; live/moving
captains and rosters beyond13 remain separate extensions.


Private captain home followers now use authored weapon range independently of
collision size. [Retail payoff62](retail-pathfinding-engine.md#private-captain-approach-range-is-independent-of-collision)
corrects premature stopping for large recruits and keeps physical range through
save/load. Map-local weapon edits now reach spawned/restored typed data. The
mixed13 regression matches3471 commits and17368 saved suffix commits through
activation; shared12+1 parameter publication and subsequent recovery remain open.

Mixed thirteen-recruit captain movement now binds12+1 physical batches to one
Move-owned parameter generation. The first phase matches4560 original commits
and9618 saved continuations through12 seconds, including live63→31 radius and
retained63 path footprint. Captain handoff composes bounded old-source recovery;
Save86 retains the published/pending speed and regroup cooldown. The next engine
gap is range-departure private approach at12.03 seconds, followed by the second
shared publication. Full5462-commit mixed parity remains open. See
[shared captain parameters](retail-pathfinding-engine.md#shared-captain-parameters-across-unequal-physical-batches).


The mixed thirteen-recruit captain regression now also covers range departure
from a shared point leg into a private target approach:4867 exact movement
commits before15 seconds and7917 saved continuation commits. Move owns the
retained deadline, stopped-pose recovery and new physical admission. See
[captain range departure](retail-pathfinding-engine.md#captain-range-departure-into-a-private-approach)
for original captures and the remaining logical-roster/second-generation gap.


The stationary alive mixed thirteen-recruit captain journey now matches all5462
retail movement commits,353 shared footprint observations and11132 saved suffix
commits. Move retains logical roster membership after a physical task finishes,
so the16-second all-entered callback creates the second12+1 shared generation.
New task activation clears retained partial routes and invalidates their indices.
Save87 persists logical actor/outer membership and validates completed members.
See [logical-roster reentry](retail-pathfinding-engine.md#logical-captain-roster-survives-physical-completion)
for exact references and scope. Mixed cancellation, roster mutation and moving
captains remain independent follow-up work.


Largest-recruit public Stop is now covered before and after shared captain
admission. Complete early/late journeys match5458/5593 original commits and
11764/12572 saved suffix commits. The surviving shared owner has one reference;
its live radius falls to31 while cached path footprint stays63. Later publication
uses a new shared generation. These regressions validate the current Move port;
no new steering heuristic is needed. See [cancellation evidence](retail-pathfinding-engine.md#largest-captain-recruit-stop-before-and-after-shared-admission).


Payoff67 fixes final shared-binding retirement after public Stop and prevents
repeated StartCampaignAI from replaying main. Complete all13 Stop repeats match
3549 physical commits/12 footprints; eight Save88 continuations remain idle.
Empty bound physical groups retire after shared publication, and the next
prepass collects their zero-reference owner. Ghidra retains the AI+248 creation
gate and opaque+24c environment. Private AI VM restoration, RemoveUnit/retarget
and fresh parameter reuse after cancellation remain open.
See [final-binding evidence](retail-pathfinding-engine.md#final-captain-binding-stop-and-repeated-ai-initialization).

`CaptainGoHome()` now dispatches through the AI player's attack captain into
Move. Occupied home changes retain the virtual actor's position; explicit GoHome
moves that radius-zero actor at the slowest retained roster speed and submits
shared12+1 roster requests. The all-entered callback tightens actor arrival from
500 to200 while retaining the original500-unit request policy for idle members.
The actor publishes moving fine-object flags and survives healthless virtual
owner updates. Two retail captures and actual public AI/save continuations match
6251 motion commits and398 footprints through23.8s. Private follow retries after
that point and autonomous occupied-home admission remain open.
See [CaptainGoHome evidence and limits](retail-pathfinding-engine.md#public-captaingohome-and-moving-virtual-captain).

Payoff69 extends exact moving-captain travel through27.2s (7419 commits and624
footprints). An accepted changed member destination now resets fine/adaptive
buffers, retry and delay before advancing the existing neighbour wait. Retail's
pending20-tick delay clears at the same point; saved continuations agree before
and after the reset. The next difference at27.27s is a one-point failed fine
refill, whose caller retains a coarse waypoint; formation destinations already
match. See [the evidence and remaining scope](retail-pathfinding-engine.md#changed-captain-destination-resets-pending-waits).

### Upstream AI integration and retail movement

The October 2026 upstream integration brings production-queue filling, visible
assault target selection, sleeping JASS condition resumption, melee policy
natives, patrol tests and destructable path-texture rotation into the retail
pathfinding branch. These policy implementations remain the documented engine
approximations; merging them does not establish numerical retail parity.

The route FIFO retains game-authored half-open footprint bounds in pending
requests, active jobs and cache identities. Repeated requests share a slot;
queued moving targets update that slot without losing their place. WC3 passes
mover and goal identities through `G_RequestMovePathField()` for diagnostics.
Interaction movement can resume a previously resolved heading while a field
builds; scheduled point cohorts retain the retail fine-route consumer and
pre-turn movement window. Accepted motion still commits software scalar
velocity and retained fine pose.

Two upstream hypotheses conflict with existing repeated retail evidence.
`AddAssault` continues to reconcile the retained typed roster, and repeated
`StartCampaignAI` retains the existing initialized VM. The merged common.ai
formation regression exercises its authored maximum through that backend.
See [owned-pool recruitment](retail-pathfinding-engine.md#captain-owned-pool-order-survives-transfer-and-reused-slots)
and [AI initialization](retail-pathfinding-engine.md#final-captain-binding-stop-and-repeated-ai-initialization).
Save format89 rejects earlier layouts and clears the process-local
route-resume/wait record while retaining serialized retail movement state.

The larger fixture set exposed a128-file truncation in `mpqtool pack`. The
tool now consumes every supplied pair; the140-file archive regression in
`tests/test_parity_maps.py` verifies files on both sides of the old limit.

Move also consumes [intermediate partial fine endpoints](retail-pathfinding-engine.md#partial-fine-refill-and-stopped-coarse-handoff)
through their retained coarse plan. Reconstruction keeps the exact fractional
source in a one-point exhausted refill, and coarse handoff explicitly stops
translation while turning. The public captain scene matches7933 exact commits
and7310 saved suffix commits through the30-second observation domain; longer
travel and complete pathfinder parity remain open.

The later upstream synchronization through`a4acf914` also includes optional EOS
Internet multiplayer. The SDK-free production/test build and required release
`make test` pass: RoC/TFT each2478 tests/2804139 assertions,447 Python
pathfinding checks and9 release-preparation checks. All143 payoff70 corpus
source fingerprints remain unchanged; this networking merge does not alter
the verified movement consumers.


Authored destructible creation now applies float `fixedRot` degrees and the
retail map clamp/dimension-parity snap before linking. Both creation orders of
file-backed overlapping tree/gate textures match captured fine masks and all
four static hierarchy levels; removal preserves terrain and the surviving
footprint. See [authored widget creation](retail-pathfinding-engine.md#authored-widget-creation-preserves-snapped-pose-and-rotation).


Move's static hierarchy uses the retail allocation padding and affected
initialization region, including ground coarse mask6 versus fine mask2. The
full MPQ-backed WPM load and direct-map adapter match every captured terrain
movement cell and all four allocated hierarchy levels. See [file-backed map
initialization](retail-pathfinding-engine.md#file-backed-maps-retain-native-hierarchy-allocation).


### Point travel through pathing toggles and scripted pause

Move keeps its coarse/adaptive route lanes separate from the current fine query.
`SetUnitPathing(false)` uses query0 without discarding the acquired detour or
ordinary occupancy. Scripted pause releases the physical point task, exposes
suspension head851973 and retains the user order for delayed reactivation after
unpause. Fractional axis displacement while paused becomes the next route's
source. Save95 retains these independent lanes and the resume deadline; actual
RunFrame and saved continuations match the complete original wall journey.
See [query and pause ownership](retail-pathfinding-engine.md#pathing-queries-and-scripted-pause-preserve-distinct-owners).


Ordinary authored flight now bypasses adaptive group/member planning and runs
budget700 fine searches against flight-blocking terrain, preserving partial
fine routes. Its category-zero active fine rectangles remain linked; collision
eligibility is separate from spatial lifetime. Chaos/rebind deadlines run on the
primary clock, including saved batched-frame continuation. See [mode transitions](retail-pathfinding-engine.md#movement-modes-select-routing-policy-independently-of-spatial-membership).

Temporary Slow/Bloodlust changes now publish through their applying ability
owners. Move recomputes the effective cap, integrates the old pose and clamps
retained velocity when required. Public buff getters/removal use attached status
identity and owner-supplied policy. A complete retail travel/return witness matches
590 main-mover commits and4356 saved suffix commits; captured spell application
times are explicit inputs, while the separate cast scheduler remains open.
See [temporary modifiers](retail-pathfinding-engine.md#temporary-speed-modifiers-publish-through-their-applying-owners).

Movement performance data structures and the bounded Rise of the Naga measurements
are documented in [WC3 performance](performance.md#october-4-movement-and-fine-grid-scaling).
