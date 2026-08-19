# Coordinate-System Contract

This document records the intended coordinate contract for game data, the current
WoW implementation, and the migration required to stop compensating for WoW axes
in every renderer path.

## Desired Contract

Each game should render in its native world coordinate system. A game module owns
the meaning of `VECTOR3` components; shared renderer math operates on vectors
without silently swapping or rotating axes.

The game coordinate contract must describe more than its up axis:

```c
#define GAME_UP_AXIS       1 /* component index: 0=x, 1=y, 2=z */
#define GAME_RIGHT_AXIS    0
#define GAME_FORWARD_AXIS  2
#define GAME_WORLD_HANDEDNESS ...
```

`UP_AXIS` is useful for compile-time shader and small helper decisions, but it is
not sufficient to describe axis order, signs, or handedness. Prefer a small
per-game coordinate header and axis-aware helpers over scattered preprocessor
branches:

```c
Game_Vec3Up(void);
Game_Height(LPCVECTOR3 point);
Game_SetHeight(LPVECTOR3 point, float height);
Game_HorizontalPoint(LPCVECTOR3 point);
```

The renderer must not convert a game-native world point merely to satisfy a
shared historical convention. A graphics-API-specific conversion is acceptable
only at the final API boundary, and must be explicit.

## WoW Native Coordinates

WoW source coordinates are Y-up. In the coordinate convention used by the WoW
client data and M2/WMO assets:

| Component | Meaning |
| --- | --- |
| X | horizontal world axis |
| Y | height / up |
| Z | horizontal world axis |

The current engine instead treats Z as up. `CM_WowObjectPoint()` currently
performs this conversion for WoW object and gameplay coordinates:

```c
renderer.x = WOW_ADT_TILES * 0.5f * WOW_ADT_SIZE - wow.z;
renderer.y = WOW_ADT_TILES * 0.5f * WOW_ADT_SIZE - wow.x;
renderer.z = wow.y;
```

Source: `common/cmodel.h`, `CM_WowObjectPoint()`.

Terrain performs the same class of conversion independently. `Wow_McvtPoint()`
currently emits:

```c
renderer.x = chunk.x - local_y;
renderer.y = chunk.y - local_x;
renderer.z = chunk.z + height;
```

Source: `games/world-of-warcraft/renderer/wow/r_wowmap_terrain.c`.

This means the current WoW renderer is not using one consistent native space;
it converts each input family at its loading or submission boundary.

## Current Compensation Sites

The following are known axis/height assumptions and must be audited during the
migration:

| Area | Current behavior | Native-space direction |
| --- | --- | --- |
| Object positions | `CM_WowObjectPoint()` swaps/reverses axes | Keep source X/Y/Z unchanged |
| ADT terrain | `Wow_McvtPoint()` maps ADT samples into renderer X/Y/Z | Define terrain directly in WoW X/Y/Z |
| M2 instances | `R_GameEntityMatrix()` applies an ADT basis and fixed Euler rotations | Upload native M2/world transform |
| WMO/object transforms | `Wow_InstanceMatrix()` applies the same basis and angle offsets | Use native transform order/axes |
| Camera | `Matrix4_lookAt()` callers use `(0,0,1)` as up | Pass WoW `(0,1,0)` up |
| Grass shader | Builds horizontal XY and writes height to Z | Build horizontal XZ and write height to Y |
| Height queries | Many APIs treat `x/y` as horizontal and return/set `z` height | Treat X/Z as horizontal and Y as height |
| Splats/shadows | Project onto XY and offset Z | Project onto XZ and offset Y |
| Fog/distance | Some paths use `.xy` for ground distance | Use the game horizontal-plane helper |
| Selection/UI world markers | Several paths assume Z-up | Use native up/ground helpers |

The fixed `-90` rotations in `R_GameEntityMatrix()` are therefore not a GPU
requirement. They are compensation for feeding native WoW model data into a
Z-up renderer. Random grass yaw is separate and may remain as a native-space
rotation around Y.

## Migration Plan

1. Add the WoW coordinate contract in a WoW-owned common header. Define up,
   horizontal axes, signs, and handedness in one place.
2. Introduce axis-aware game helpers for height, ground-plane coordinates,
   camera up, and horizontal distance. Do not add generic `x/y/z` aliases that
   hide which plane a caller expects.
3. Change the WoW camera/view setup to use native Y-up and make the WoW view
   path the authoritative world-space contract.
4. Remove `CM_WowObjectPoint()` remapping from WoW runtime positions. Update
   collision, server entity state, object bounds, and renderer consumers together
   so gameplay and rendering continue to share the same points.
5. Rewrite terrain, WMO, M2, and grass submission in native coordinates. Delete
   the ADT basis matrices and fixed Euler compensation once visual parity is
   established.
6. Audit shared renderer code that directly uses `.z` as height or `.xy` as the
   ground plane. Route those cases through game-owned helpers or an explicit
   view/game coordinate contract.
7. Remove legacy `RF_GROUND_EFFECT` matrix logic after native M2 instancing and
   the GPU-generated grass path have independent transform tests.

Do not solve this by adding a single global `UP_AXIS` conditional to shared
renderer code. That leaves axis order, handedness, camera orientation, ground
projection, and asset transforms implicit and will recreate the same scattered
compensations under different names.

## Verification Contract

Coordinate migration must be verified with fixed points and orientations before
visual tuning:

- A known WoW object position must appear at the same map location before and
  after migration.
- A unit vector along native Y must point upward in the rendered scene.
- A unit-height terrain sample must change only native Y.
- A model with zero rotation must stand upright without a `-90` correction.
- A positive native-Y rotation must rotate around the up axis.
- Terrain, WMO, M2, collision height queries, splats, and camera tracing must
  agree on the same X/Z ground plane.
- Grass must use the same native terrain height and horizontal coordinates as
  terrain, without a CPU or shader axis swap.

Use bounded runtime scenes with `+com_frame_limit 100` and compare a known WoW
map in the renderer, model-scene launcher, collision trace, and grass paths.
Do not delete the existing transforms until these checks pass; the current
transforms are compensations, but they remain the active behavior contract.

## Related Code

- `common/cmodel.h` — current `CM_WowObjectPoint()` conversion.
- `games/world-of-warcraft/renderer/wow/r_wowmap_terrain.c` — ADT-to-renderer mapping.
- `games/world-of-warcraft/renderer/wow/r_wowmap_objects.c` — WMO/M2 object transforms.
- `games/world-of-warcraft/renderer/r_game.c` — shared WoW entity matrix and fixed rotations.
- `games/world-of-warcraft/renderer/wow/r_wowmap_shader.c` — GPU grass axis assumptions.
- `docs/games/world-of-warcraft/terrain-and-world-rendering.md` — current terrain/object loading flow.
- `docs/games/world-of-warcraft/static-grass-and-height-atlas.md` — planned GPU-native grass migration.
