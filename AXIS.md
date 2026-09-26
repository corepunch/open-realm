# Coordinates, model transforms, and camera angles

## Engine pose contract

All three game backends expose **Z-up world coordinates**. World positions, camera targets, collision,
picking, lighting and culling agree before projection. OpenGL eye space looks down -Z with +Y screen-up;
that is a view convention, not a reason to swap world Y/Z.

`R_GetEntityMatrix` in `renderer/r_ents.c` is the shared model-to-world boundary:

```text
interpolated entity -> mandatory per-game R_EntityPose -> modelPose_t + model basis
modelToWorld = translation * orientation * scale * modelBasis
native skinned vertex -> modelToWorld -> view/projection
```

Every game implements `R_EntityPose`, including WC3. There is no optional override or implicit fallback.
The hook decodes game-owned placement conventions and returns the basis declared in that game's coordinate header.
The engine applies the basis once in the composition. Skinning stays in native model space. Drawing, bounds,
model-space picking, model cameras and attachments use the resulting transform. A global view rotation cannot
repair sideways walking because it rotates the mesh and its movement vector together.

`modelPose_t.angles` uses `orientation_t`: **yaw, pitch, roll in radians**. Its canonical frame is +X forward,
+Y left, +Z up. Positive yaw turns left, positive pitch raises the nose, and positive roll rotates about forward.
Composition is `Rz(yaw) * Ry(-pitch) * Rx(roll)`, implemented once by `Quaternion_fromOrientation`.
The names describe the pose rather than the asset's native axes; the per-game basis aligns native model axes.

The existing `entityState_t.angle`/`renderEntity_t.angle` is canonical heading in radians. WoW's `rotation`
field retains authored placement Euler components in degrees: raw binary and wire fields are deliberately not
renamed yaw/pitch/roll, because their components have source-format meanings. The game adapter decodes them into
the named pose. Network layouts, field widths, entity flags and save formats are unchanged; protocol version 9
corrects heading quantization to unsigned 16-bit turns in radians. The renderer's
`modelPose_t` and attachment pose are derived presentation state, not snapshot additions.

| Game | Native unit-model forward / up | Mandatory model basis | Coordinate owner |
|---|---|---|---|
| WC3 MDX | +X / +Z | Identity | `games/warcraft-3/common/wc3_coords.h` |
| WoW M2 | +X / +Z | Identity | `games/world-of-warcraft/common/wow_coords.h` |
| SC2 M3 | -Y / +Z | `Rz(+90 degrees)` | `games/starcraft-2/common/sc2_coords.h` |

The identity entries apply to model-to-actor orientation, not every on-disk placement format in that game.
Matrices remain derived values. Static WMO and grass instances retain their existing caches. Dynamic calls may
re-evaluate the same pure builder; that is not double conversion. No mutable per-frame matrix cache was added,
so picking between frames and nested UI render views do not depend on cache preparation order.

## SC2 authored placement and facing

`SC2_RunUnit` writes `atan2f(dir.y, dir.x)` as gameplay heading. `R_GetEntityMatrix` then aligns the M3's -Y
front with that heading. The model loader contains no extra orientation or 100x scale correction.

Layout-camera console models and unit portraits set `RF_PORTRAIT_LIGHTING`. `R_EntityPose` returns
`sc2_native_basis` (identity) for that flag. Those cameras are authored in native M3 axes — the console
looks along +Y with +Z up — and the Birth pose was matched to an identity matrix. Applying `sc2_model_basis`
there turns the HUD edge-on. See [console chrome](docs/games/starcraft-2/hud-layout-pipeline.md).

Placed-object `Angle` already describes native mesh placement. `SC2_SpawnEntities` calls
`SC2_PlacementHeading(raw) = raw - pi/2`, so the final transform preserves the authored placement:
`Rz(raw - pi/2) * Rz(pi/2) = Rz(raw)`. This applies to placed units and scenery alike. It requires no model-name,
selection or movement heuristics. Galaxy `UnitCreate` and `UnitSetFacing` both decode degrees through
`SC2_FacingRadians` at the native API boundary; host callbacks receive radians. The same coordinate header owns `SC2_EulerFromCamera` and
`SC2_CameraFromEuler`; degree interpolation remains with the camera/map state.

Do not restore the old global +90 rotation inside `M3_RenderModel`. Commit `24354a8c1` removed it after authored
bridges and doodads were rotated an extra quarter turn. Source-placement decoding and model basis must change
together. The old `UnitCreate` callback also stored incoming degrees as radians; the VM regression uses a nonzero
90-degree heading to distinguish those units.

Placement preservation must include the snapshot codec. The old `NFT_ANGLE` encoded radians using
`angle / 360 * 65535`; its grid did not contain an exact quarter turn. Subtracting `pi/2` before that codec
and restoring the model basis afterward rotated a zero placement by 0.2991 degrees, and shifted both
TRaynor01 bridge placements by the same amount. Protocol 9 rounds a full radian turn into 65536 steps,
so quarter turns are exact and other headings have at most half a wire step of error (about 0.00275 degrees).
The same two-byte field wraps negative and multi-turn headings. Old clients/servers must be rebuilt together;
the versioned connection request and reply reject older peers. Earlier versions declared a protocol constant
but did not negotiate it. See [network contract](docs/architecture/network.md).

Local-data evidence: decoding the installed Liberty `Assets/Units/Terran/Marine/Marine.m3` BONE v1 names and
inverting its IREF matrices places `Ref_Head` at approximately `(0.004,-0.063,0.777)` and `Ref_Weapon` at
`(-0.124,-0.393,0.010)` in the bind pose. The
[WC3-to-M3 exporter's orientation notes](https://github.com/Darithos/W3ModelViewer) independently describe +X to -Y.

```sh
build/bin/mpqtool -mpq data/StarCraft2/Mods/Liberty.SC2Mod/base.SC2Assets \
  cat Assets/Units/Terran/Marine/Marine.m3 > /tmp/openrealm-marine-axis.m3
```

## WoW source spaces and ownership

WoW has several source spaces. `wow_coords.h` owns their conversions:

| Input | Mapping / consumer |
|---|---|
| Native M2/WMO vertices and gameplay positions | Z-up; no model up-axis conversion. |
| ADT MDDF/MODF position | `Wow_ObjectPosition(x,y,z) = (center-z, center-x, y)`, `center = 32 * WOW_ADT_SIZE`. Used by scenery, interactive entities, collision and placement bounds. |
| MDDF rotation | `Wow_DoodadOrientation`: yaw = raw Y, pitch = -raw X, roll = raw Z, converted from degrees to radians. Actor heading is added independently. |
| MODF/WMO placement | `Wow_PlacementMatrix` uses the same angle decoder plus 180-degree yaw. `Wow_InstanceMatrix` and `CM_WowWmoMatrix` delegate to it. Fixed-point scale 1024 is unity; absent/zero MODF scale is unity. |
| MCVT offsets / MCNR normals | `Wow_TerrainOffset` and `Wow_TerrainNormal` own the reversed row/column mapping and `(-ny,-nx,nz)` normal mapping. Height sampling and packed-normal decoding remain subsystem responsibilities. |
| World coordinate -> ADT tile | `Wow_TileIndex` is shared by renderer streaming, collision and interactive-object loading. |
| Camera parameterization | `Wow_EulerFromCamera` / `Wow_CameraFromEuler` adapt native downward pitch/heading to the shared orbit view. Camera interpolation remains in `wow_view.h`. |

A local Classic `Character/Orc/Male/OrcMale.m2` (MD20 version 256, 2,724 vertices) has raw bounds
X `[-0.6577,0.4515]`, Y `[-0.8431,0.8431]`, Z `[-0.0067,2.5358]`. `M2_MakeVertex` copies these positions directly.
[Issue #194](https://github.com/corepunch/open-realm/issues/194) conflated Y-height ADT placements with all native
WoW data; using one global Y-to-Z transform would rotate already Z-up geometry incorrectly.

The removed M2 chain used `B(x,y,z)=(z,x,y)` followed by fixed quarter turns. For ordinary actors it reduced to
`B * Ry(yaw-90) * Rx(-90) = Rz(yaw)`; for MDDF it reduced to
`B * Ry(y-90) * Rz(-x) * Rx(z-90) = Rz(y) * Ry(x) * Rx(z)`.
Ground anchoring now adjusts only character height. Flying actors and spell projectiles use heading without the
old `EF_GROUND_ANCHOR` workaround. Grass supplies its sampled radian yaw in `angle`, using the same shared matrix
builder. Its old handwritten branch interpreted a radian value as degrees and rotated about X.

WMO child doodads retain their native MODD quaternion/translation and compose with the parent placement.
M2 bones, sockets, lights, particles and ribbons similarly inherit model-to-world without another basis.
`renderEntity_t.attachment.angles` explicitly carries local attachment orientation. `R_GetAttachmentMatrix`
applies it after the parent socket; FrameXML character facing therefore rotates the character without rotating
the backdrop or its embedded camera. The synthesized sun already produces world-space directions.

History: `11301f70` previously unified WMO collision/render placement and camera conventions while leaving the
M2 chain in place. `9435b60c3` optimized that chain for grass; `196bfd3c3` introduced the projectile flag workaround.
The consolidation removes these independent orientation paths while preserving the source-format distinctions.

## Transform regression coverage

- `make test-sc2-engine`: real selected-unit cardinal move orders and obstacle detours, model-forward agreement,
  snapshot decode and wrapped client yaw interpolation, asymmetric picking, authored placement through the codec,
  including cardinal headings and TRaynor01 bridge angles, and model camera.
- `make test-galaxy`: nonzero `UnitCreate` and `UnitSetFacing` agree on radians at the real VM/native boundary.
- `make test-wow-engine PATTERN='wow_coordinates.*'`: actor heading with and without ground anchoring, grass
  radian yaw/uprightness, tilted/scaled MDDF equivalence, explicit preview attachment pose, MODF and nested MODD.
- `make test-wow-appearance`: reduced MODF placement against the legacy chain, camera conversion and source adapters.
- `make test-wow-abilities`: projectiles retain authored heading without ground-anchor orientation flags.
- `make openwarcraft3-tests`, then `build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_coordinates.*'`:
  mandatory WC3 identity conversion preserves translated, rotated and scaled MDX placement.
- `make test`: includes shared named-angle math, engine suites, routing, snapshot and existing gameplay coverage.

The regression fixtures exercise production movement, matrix, picking and camera entry points without a GL window.
They do not constitute a framebuffer comparison of TRaynor01 or every authored asset.

## Reference implementations

- **3ds Max:** [Object Transformation Matrix](https://help.autodesk.com/cloudhelp/2019/ENU/Max-Developer-Help/developer/3ds_max_sdk_features/modeling/transformation_and_rotation/object_transformation_matrix.html)
  separates node placement from an object offset. `GetObjectTM()` gives the combined geometry-to-world matrix.
  Max writes `ObjectOffsetTM * NodeWorldTM` using its
  [row-vector convention](https://help.autodesk.com/cloudhelp/2023/ENU/Max-Developer-Help/3ds_max_sdk_features/modeling/transformation_and_rotation/matrix_representations_of_3d_tra/matrix_fundamentals.html);
  do not copy that multiplication order into this engine's column-vector math.
- **FBX SDK:** [FbxAxisSystem](https://help.autodesk.com/cloudhelp/2020/ENU/FBX-API-Reference/cpp_ref/class_fbx_axis_system.html)
  describes up, signed front, and handedness. `ConvertScene` rotates root nodes and lets descendants inherit;
  `DeepConvertScene` also rewrites transforms/geometry/animation and supports handedness changes. This is scene
  basis conversion, distinct from aligning a unit mesh with its gameplay forward vector.
- **FBX renderer example:** [ViewScene/DrawScene.cxx](https://help.autodesk.com/cloudhelp/2020/ENU/FBX-API-Reference/cpp_ref/_view_scene_2_draw_scene_8cxx-example.html)
  composes `globalPosition * geometryOffset` for drawing. The geometry offset is not inherited by child scene
  nodes. Our native model skeleton and sockets remain inside the asset coordinate space, so their evaluated
  positions must use the asset's final model-to-world matrix.
- **Quake 2:** `data/Quake-2-master/ref_gl/gl_rmain.c` separates `R_RotateForEntity` from the fixed world-to-eye
  basis in `R_SetupGL`. This is the closest engine precedent: entity placement and the camera basis have
  different owners and purposes.

## Camera wire contract

`playerState.viewangles` and `viewCamera_t.viewangles` are three **view rotation Euler components in degrees**.
They are not world positions, nor three interchangeable gameplay yaw/pitch/roll values. `ROTATE_ZYX` builds
`Rx(x) * Ry(y) * Rz(z)`. The shared client converts these samples to quaternions, slerps them, and builds
`T(0,0,-distance) * R * T(-target)`.

Identity therefore looks straight down world -Z. X is orbit tilt from that direction, Z is the world-to-view yaw
rotation, and Y is the middle Euler rotation (not an independent roll about the final sight line). Existing comments
calling the tuple `{pitch, roll, yaw}` are historical shorthand. Rename/retype only with a full consumer and serializer
audit; a named struct alone cannot reconcile different reference directions or rotation orders.

| Producer | Native angles | Conversion to the existing view Euler tuple |
|---|---|---|
| WC3 | Existing JASS orbit values | Existing WC3 conversion, unchanged |
| SC2 | Map/Galaxy downward pitch `p`, camera azimuth `y` | `{p-90, 0, y-180}` |
| WoW | Downward pitch `p`, actor heading `y` from +X toward +Y | `{p-90, 0, 90-y}` |

`SC2_EulerFromCamera` / `SC2_CameraFromEuler` and `Wow_EulerFromCamera` / `Wow_CameraFromEuler` are the game-boundary
adapters. Both directions matter: manual view input returns through the inverse adapter before updating native game
camera state or actor movement. SC2 height offset remains a target-height value, never an angle. WoW's old wrapped
negative pitch remains internal to `wow_move`; input limits use canonical tilt (-85..-35 for downward pitch 5..55).
Do not add game conditionals or a second orientation representation to the shared client or wire protocol.

## Confirmed camera regressions

The September 2026 Euler-snapshot unification retained WC3's view contract but did not fully convert the other games:

- SC2's authored yaw was copied directly to the view rotation. The TRaynor01 bridge appeared from the opposite side
  compared with the retail reference. At native pitch 34.878 and yaw 193.947, runtime tracing recovered an eye offset
  `(5.971,24.043,17.268)`; the corrected azimuth places the eye at `(-5.971,-24.043,17.268)`.
  Apply this convention in the camera adapter, not to map coordinates or light directions.
- WoW published downward pitch 32 as orbit tilt 32 and heading 0 as view yaw 0. The measured eye offset at distance
  8.5 was `(0,4.504,7.209)`: sideways and steeply overhead. The correct view tuple is `{-58,0,90}`, giving offset
  `(-7.209,0,4.504)` behind the actor's +X heading.

History: `ea66b69ad` introduced shared Euler snapshots; `f9083e76c` introduced SC2 pitch conversion, leaving native yaw
unchanged. WMO renderer/collision placement code was duplicated; consolidating it is independent of these camera fixes.

## Verification

```sh
make -j8 opensc2 openwow openwarcraft3 install-share
make test-sc2 test-wow-appearance test-wow-game test-client-camera
build/bin/opensc2 -data data/StarCraft2 +vid_hidden 1 +com_maxfps 60 +map Maps/Campaign/TRaynor01.SC2Map +screenshot 10 +com_frame_limit 300
build/bin/openwow -data data/world-of-warcraft +vid_hidden 1 +com_maxfps 60 +set wow_playerinfo '\race\Orc\sex\Male\class\1\appearance\0' +map playercreate +screenshot 10 +com_frame_limit 300
build/bin/openwarcraft3 -data 'data/Warcraft III' +vid_hidden 1 +map Maps/Campaign/Human02.w3m +screenshot 10 +com_frame_limit 40
```

Use `com_maxfps` for bounded render captures: an uncapped hidden client can exhaust even 1000 iterations before the
server's first post-load snapshot. Do not use fast-forward for rendering. Temporary logs at `Matrix4_fromViewQuat`
can recover the eye from the inverse view matrix; remove them after diagnosis.

Tests check cardinal camera headings through the actual Euler/quaternion matrices, upright screen projection, WoW
controller input/movement, terrain-relative camera interpolation, and the reduced WMO matrix against its previous
basis chain for translated, scaled and tilted placements. Compare engine screenshots with the reference as well.
The per-game Makefiles explicitly depend on common headers: inline adapter edits must rebuild the game library,
renderer and client together. Without that dependency, unit tests could pass while the live game still used old angles.

See also: [client camera samples](docs/architecture/client.md), [shared input](docs/architecture/shared-input.md),
[SC2 rendering](docs/games/starcraft-2/terrain-and-world-rendering.md).
