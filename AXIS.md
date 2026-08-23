# Coordinate-System Contract

This document records the intended coordinate contract for game data, the current
WoW implementation, and the migration required to stop compensating for WoW axes
in every renderer path.

To avoid this mess:
```text
engine.x = 32 * WOW_ADT_SIZE - mddf.position.z
engine.y = 32 * WOW_ADT_SIZE - mddf.position.x
engine.z = mddf.position.y
```


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

## Target State: Transform Functions After Migration

The test for whether the migration is complete is that every placement function
reduces to the minimal form: translate, rotate by file angles verbatim, scale.
No basis permutation matrix, no angle offsets, no axis-swap arithmetic.

### Wow_InstanceMatrix (WMO placement)

`wowVec3_t` and `VECTOR3` share the same `{float x,y,z}` layout, so
`def->position` and `def->rotation` can be cast directly.  `ROTATE_XYZ` applies
all three Euler components in one call.

```c
void Wow_InstanceMatrix(wowMapObjDef_t const *def, LPMATRIX4 matrix) {
    Matrix4_identity(matrix);
    Matrix4_translate(matrix, (LPCVECTOR3)&def->position);
    Matrix4_rotate(matrix, (LPCVECTOR3)&def->rotation, ROTATE_XYZ);
    if (def->scale) { float s = def->scale / 1024.f; Matrix4_scale(matrix, &(VECTOR3){s,s,s}); }
}
```

The current 30-line version only exists because the basis permutation matrix
rotates the WMO 90° relative to where its file angles expect it, so three
compensating offset terms (`rotation.y - 270`, `-rotation.x`, `rotation.z - 90`)
are then required to undo the damage.  Both the basis and the offsets vanish
together once positions and rotations are kept in native WoW Y-up space.

### R_GameEntityMatrix (M2 doodad/entity placement)

The same principle applies.  The ADT basis block (lines 190–200 of
`games/world-of-warcraft/renderer/r_game.c`) and its associated angle offsets
(`rotation.y - 90`, the forced Z-axis yaw for `RF_GROUND_ANCHOR`) reduce to
a direct rotate by `entity->rotation` once the entity origin is native WoW.
The `RF_GROUND_EFFECT` fast path similarly loses its precomputed compensation
constants and becomes a single-angle Y-rotation matrix.

### CM_WowObjectPoint / Wow_ObjectPoint (position conversion)

After migration these become identity functions — the conversion body is
deleted and callers receive `{x, y, z}` unchanged:

```c
static inline VECTOR3 CM_WowObjectPoint(float x, float y, float z) {
    return (VECTOR3){ x, y, z };
}
```

Once every caller is updated, the function itself is removed.

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
