# Scripted Doodad Animation

## Contract

Warcraft III doodads are map scenery rather than JASS handles.  Scripts control them by type and area through:

```text
SetDoodadAnimation(x, y, radius, doodadID, nearestOnly, animName, animRandom)
SetDoodadAnimationRect(rect, doodadID, animName, animRandom)
```

The map's `war3map.doo` placement owns each doodad's type, position, variation, scale and facing. OpenRealm represents those placements as `SVF_STATIC_SCENERY` edicts. The `data.Doodads` row comes from `Doodads\Doodads.slk` **plus map-local `war3map.w3d` edits and custom IDs** when present. Original-object edits are applied before custom IDs inherit their parent rows; the original global SLK data is not mutated.

Changing a doodad animation is presentation state only.  It does not turn the doodad into a destructable and does not change its pathing footprint.

## Runtime Flow

```text
JASS SetDoodadAnimation*
    -> match map doodads by rawcode and radius/rect
    -> resolve requested sequence from the registered MDX model
    -> assign the sequence and begin at its authored first frame
    -> advance it through the normal server animation clock
    -> looping sequence: wrap to the authored start
    -> non-looping sequence: hold the authored final frame and stop spawning new PRE2 particles (existing particles drain normally)
```

`animRandom=false` uses the ordinary first matching sequence.  `animRandom=true` chooses among numbered MDX variants that share the same logical animation sync point.

The special animation names `show` and `hide` toggle doodad presentation without changing pathing.  The retail `soundon`/`soundoff` special names are still outside the current doodad-audio implementation.

## Prologue01 Banner Behavior

The original `Prologue01` map does **not** kill or animate the `ncp3` Circle of Power unit when a tutorial waypoint is reached.  Its completion trigger runs the abort action, which contains:

```jass
call SetDoodadAnimationRectBJ( "death", 'LOo2', gg_rct_Spot01 )
call SetUnitColor( udg_Circle01, GetPlayerColor(Player(PLAYER_NEUTRAL_PASSIVE)) )
```

`LOo2` is the separate banner/flag doodad placed in the middle of the Circle of Power.  Its authored `Death` sequence removes the banner and emits the smoke puff seen in retail.  The Circle unit remains and is recoloured independently.

The Circle's completed appearance is a separate `SetUnitColor` operation. Prologue01 initializes each checkpoint Circle as `PLAYER_COLOR_LIGHT_GRAY`, then changes it to the neutral-passive player's color after the banner Death animation starts. The Circle remains owned by its original map player; only the MDX replaceable team-color presentation changes.

The same pattern is used for the later tutorial waypoint banners and the final objective (`gg_rct_SpotVictory`).

This distinction matters when debugging the scene: `CircleOfPower.mdx` animation traces cannot observe the banner transition because the transition belongs to the `LOo2` doodad model.

Doodad model lookup uses both `dir` and `file` for RoC rows whose `file` is a short name: `dir\file\file.mdx` (for example `Doodads\LordaeronSummer\Plants\Wheat\Wheat.mdx`). Rows whose `file` already contains a path use that path directly. A numeric variation suffix is considered only when `numVar > 1`; when that suffixed asset is absent, lookup falls back to the unsuffixed base model. The earlier file-only resolver produced bare names such as `Wheat.mdx` and empty models on RoC maps.

## Verification

Automated coverage should verify:

- rectangle filtering changes only matching doodad rawcodes inside the rect;
- `nearestOnly` changes one nearest matching doodad inside the radius;
- `show`/`hide` are presentation-only;
- non-looping sequences hold their final authored frame;
- looping sequences wrap to their authored start.

Runtime verification for `Maps\Campaign\Prologue01.w3m` is to reach `gg_rct_Spot01`: the centre banner should play its death transition and smoke, while the Circle of Power ground marker remains.

## Custom Doodads and Rexxar Zone Boundaries

`war3map.w3d` is a separate doodad **object-data** file, not the `war3map.doo`
placement list and not `war3map.w3b` (destructables). Its modification records
use the simple field/type/value layout followed by a four-byte end marker, as
in `war3map.w3b`; they do not include the level/data-pointer integers used by
`war3map.w3a`. The game reads both original and custom rows, resolves the
`dfil` (model file) and
`dvar` (model variation count) changes through map-local typed rows, and then
spawns each placed doodad using the already-existing MDX model pipeline.

The extracted Rexxar campaign map report identifies custom IDs `D00E` and
`D001` derived from `LOfl`, with a `dfil` override pointing at
`Doodads\Dungeon\Props\Forcewall\Forcewall.mdl`. This is **map data**, not a
hardcoded type-to-asset mapping. `.mdl` and `.mdx` model suffixes resolve to
the renderer's `.mdx` filename. If the selected model is missing from the
active game archives/imports, adding a custom ID will not supply the missing
asset; check mounted archives before treating that as a rendering defect.

For special terrain doodads, the renderer separately resolves `pathTex` from
`Doodads.slk` and map-local `war3map.w3d` edits to find the footprint that
suppresses generated cliff faces across the full footprint and fits each model vertex to W3E height.
See [special terrain geometry](terrain-tiles-and-water.md#special-terrain-doodad-geometry-630).
A custom ID may inherit through another
map-local custom ID before reaching a stock doodad row. The renderer follows
that parent chain and bounds traversal by the number of map-local rows, so
cyclic object data fails lookup instead of hanging map registration.
Cave materials may select separate cliff and overlay UV channels. See
[cave material UV channels](terrain-tiles-and-water.md#cave-material-uv-channels-630)
for the `UVAS`/`UVBS` contract and regression coverage.
The renderer's bounded reader uses the same simple W3D modification layout as
the game parser: field ID, type, value, trailing object ID. The local OrcX01
version-1 data puts `PathTextures\4x4SimpleSolid.tga` immediately after `dptx`
and its string type. Reading two nonexistent level/data-pointer words used to
strip the first eight bytes of that path. Empty strings are valid explicit
path overrides; the minimum modification width is 13 bytes. Tests cover a
stock-shaped path record, an empty path followed by a scalar modification,
unaligned fields, and every truncated prefix of the latter record.

Run `make test-renderer-model test-renderer-shadows` for these parser and
cutout checks. The cutout test installs its own memory imports so filtered
`build/bin/test_renderer_model 'wc3_terrain.*'` runs are independent of model
test order.

OrcX01's Forcewall MDX has no geoset vertices: its appearance comes from two
`PRE2` particle emitters using `Textures\Clouds8x8ModBlight.blp` and
`Textures\ForceField.blp`. The renderer queues PRE2 particles through
`MDLX_RenderParticleEmitters`. `MDX_CalculateBounds` normally rebuilds bounds
from geoset vertices, which collapses a particle-only model to a zero-size box
and can cull its plume when the emitter anchor is outside the view. For models
without vertices, preserve the authored `MODL` box and derive a conservative
sphere radius from its half-extents when the authored radius is zero. This lets
both the entity culler and MDX culler retain particle-only models.

The original zone transition itself is **not** activated by the doodad model.
Its JASS script polls tripwire rectangles and queues a fade/transition; this
visual path is separate from the script and the SaveGame/ChangeLevel pipeline.
The normal W3I/WTS destination loading UI and JASS-controlled camera fades
already have independent owners, so no Rexxar-specific client code is required.

Only `dfil` and `dvar` W3D fields are applied by this initial support. Other
object-editor doodad overrides (including per-variation tint, pathing, and
render flags) still require an evidence-driven extension. Forcewall particle
textures and authored bounds are confirmed in the base `War3.mpq`; other
effects still require inspection of their own model data.

### Validation

- A `.w3d` custom ID with `dfil` must render its custom MDX rather than fall
  through to a missing stock-SLK row.
- A custom row inherits base doodad fields, including `walkable` and `numVar`.
- Original-object edits affect derived custom rows but not the global SLK.
- A per-map overlay is discarded on map replacement; absent `war3map.w3d`
  leaves standard doodad resolution unchanged.
- A model with `dvar > 1` probes the authored numbered MDX variant and falls
  back to the unsuffixed model if that variant is absent.
- On an actual Rexxar map, compare Forcewall geometry/animation and loading
  presentation with retail, and validate tripwire JASS separately.
