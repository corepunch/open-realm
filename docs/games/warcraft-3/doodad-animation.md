# Scripted Doodad Animation

## Contract

Warcraft III doodads are map scenery rather than JASS handles.  Scripts control them by type and area through:

```text
SetDoodadAnimation(x, y, radius, doodadID, nearestOnly, animName, animRandom)
SetDoodadAnimationRect(rect, doodadID, animName, animRandom)
```

The map's `war3map.doo` placement owns each doodad's type, position, variation, scale and facing.  OpenRealm represents those placements as `SVF_STATIC_SCENERY` edicts whose `data.Doodads` row comes from `Doodads\Doodads.slk`.

Changing a doodad animation is presentation state only.  It does not turn the doodad into a destructable and does not change its pathing footprint.

## Runtime Flow

```text
JASS SetDoodadAnimation*
    -> match map doodads by rawcode and radius/rect
    -> resolve requested sequence from the registered MDX model
    -> assign the sequence and begin at its authored first frame
    -> advance it through the normal server animation clock
    -> looping sequence: wrap to the authored start
    -> non-looping sequence: hold the authored final frame
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
