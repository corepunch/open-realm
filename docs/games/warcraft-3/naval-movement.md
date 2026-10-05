# Naval Movement And Water Pathing

## Contract

Warcraft III movement comes from `UnitData.slk` `movetp`. OpenRealm recognizes the retail/Warsmash values `foot`, `horse`, `fly`, `hover`, `float`, and `amph`; unknown authored values remain movement-disabled. Routing must preserve the movement type instead of treating every non-flyer as a ground unit.

The shared WPM cell already stores `nowalk`, `nofly`, and `nowater`. WC3 routing exposes these policies through `M_UnitStaticPathingFlags()`:

| `movetp` | Static route is blocked when |
|---|---|
| `foot`, `horse`, `hover` | `nowalk` |
| `fly` | `nofly` |
| `float` | `nowater` |
| `amph` | `nowalk && nowater` |

The shared router exposes `CM_PATHING_UNSWIMMABLE` for `nowater` and the generic `CM_PATHING_REQUIRE_ALL` query modifier. WC3 represents `amph` as `REQUIRE_ALL | UNWALKABLE | UNSWIMMABLE`, so the route is rejected only when both terrain channels are blocked. The shared router does not contain an amphibious gameplay type. This follows Warsmash `MovementType.AMPHIBIOUS`, whose pathability is `!UNWALKABLE || !UNSWIMABLE`.

## Pathing Textures

WC3 pathing-texture red blocking applies to both land and naval movement. In OpenRealm's decoded `color32_t` representation the existing red-source blocking channel is `COLOR32.b`; stamping a blocked pixel sets both `nowalk` and `nowater`. The green-source channel remains `nofly`.

A live walkable bridge is special: a clear authored deck may clear terrain `nowalk` so ground units can cross, but it must not clear terrain `nowater`. `FLOAT` therefore continues to use the river/water route and never gains a naval route across the bridge deck. Blocked bridge pixels still block both land and water.

## Dynamic Collision

Warsmash separates moving-unit collision into ground, sea, and air domains. OpenRealm keeps its existing precise `BoxEdicts` broad phase but applies the same domain semantics in `skills/s_move.c`:

| Movement | Collision domain |
|---|---|
| `foot`, `horse`, `hover` | ground |
| `float` | sea |
| `amph` | ground + sea |
| `fly` | air |
| disabled | none |

Two ordinary units block one another when their domain masks overlap. Structures remain precise blockers for ground and sea movement in addition to their authored/coarse static footprint; this preserves OpenRealm's existing protection against leaking through coarse 32-unit building pathing cells. Flyers retain their prior separation from structures in this move-time circle test and rely on authored `nofly` pathing where present.

Command-time dynamic obstacle stamping is requester-aware. The shared router passes the requesting pathing policy back to WC3's `M_UnitDynamicPathingFlags()`: ground movers see ground/amphibious blockers, sea movers see sea/amphibious blockers, flyers see air blockers, and amphibious movers see either ground or sea blockers. For an amphibious request, any overlapping ground/sea blocker stamps both selected channels so the static `REQUIRE_ALL` terrain rule cannot incorrectly treat a lone unit as passable. Static terrain semantics stay generic while dynamic collision-domain policy remains game-owned.

## Rendering

Rendering height is separate from simulation pathability. Existing `M_CheckGround()` behavior remains authoritative:

- `float` uses the water surface and ignores walkable bridge height;
- `amph` uses the water surface only where terrain is swimmable and not walkable;
- an amphibious unit on a walkable bridge is not presented as swimming.

Do not use render-water detection to authorize movement.

## Existing Consumers

Movement/pathing consumers already call `M_UnitStaticPathingFlags()` rather than hard-coding ground routing. Fixing that policy therefore propagates to normal Move, Patrol, Attack/chase, attack-move waypoints, formation slots, spawn/unstuck placement, Way Gate placement, Blink-style point correction, construction approaches, and cargo transport movement.

Repair's authored naval range bonus is already implemented separately in `skills/s_repair.c` for `movetp=float` targets.

Cargo passenger placement already asks `G_FindUnitUnstuckPosition()` using the passenger, so the passenger's movement policy is authoritative. The current last-resort unload fallback still places the passenger at the transport position if no legal unstuck point exists. Retail-exact behavior for the no-legal-unload-position case remains unresolved and is deliberately not changed by the naval routing work.

## Verification

Automated coverage belongs in `games/warcraft-3/game/tests/t_pathfinding.c` and `t_movement.c`:

- land rejects `float` while water accepts it;
- `amph` accepts either land or water and rejects cells blocked to both;
- movement type selects the expected router policy;
- command-time amphibious destinations reject either a ground or sea unit while ground/sea movers ignore the opposite domain;
- a `float` route detours through connected water around unswimmable land;
- pathing-texture blocked pixels reject both ground and float routing;
- float move-time collision ignores ground units and collides with sea units;
- amphibious move-time collision collides with both ground and sea units;
- existing water-height/bridge tests continue to cover `FLOAT` presentation.

Per repository policy, build affected targets and run `make test` before committing code changes.
