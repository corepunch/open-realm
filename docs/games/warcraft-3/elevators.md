# Warcraft III elevators

Blizzard.j stores an elevator's logical level in destructable occluder height.
`GetElevatorHeight` computes `1 + R2I(occH / 128)` and clamps invalid levels
to 1; `ChangeElevatorHeight` writes `128 * (level - 1)`. This is mutable
runtime state, not just an occlusion hint. ElevatorPuzzle.mdx translates its
walkable deck to approximately 0, 128.03, and 255.892 units in stand, stand
second, and stand third. The engine should use the requested 0/128/256 level
height during a transition, matching Blizzard.j's synchronous state update.

HumanX06 bridge placements DTrx_1439/1440/1783 at (1792,5120/4864/4608) are
the left A/B/C group; DTrx_0717/0718/1379 at (2176,5120/4864/4608) are the
right group. All six get ChangeElevatorHeight(..., 2) during
Init_04a_Environment. Initial walls are WEST on left, EAST on right, NORTH on
both A pieces, and SOUTH on both C pieces, closed. DTfx_1784 at (1216,4864)
is the left plate; DTfx_0721 at (2752,4864) is the right. Each trigger disables
itself, kills its plate, switches off its rune rect, waits 0.50 seconds, moves
its three pieces to level 3, plays the elevator sound, waits 1.30 seconds, opens
the outer wall, and opens the shared divider only if the other plate has
already activated. DTep wall blockers use 192-unit offsets and suppress shared
edges when an elevator is found 256 units away. Keep that wall policy distinct
from deck height.

`ChangeElevatorHeight` transition pairs from Blizzard.j are 2->1 birth,
3->1 birth third, 1->2 death, 3->2 birth second, 1->3 death third, and 2->3
death second. It queues stand, stand second, or stand third at the target.
MDX transition clips have the non-looping sequence flag. Destructables need
their own animation clock because they do not own the unit movement callback.

Asset findings from local retail archives: DTrx and DTrf both resolve to
`Doodads/Cinematic/ElevatorPuzzle/ElevatorPuzzle.mdx`; its TEXS entries are
replaceable ID 11 with empty path and `Textures/gutz.blp`. DTfx, DTfp, and
XTmp resolve to `Doodads/Cinematic/FootSwitch/FootSwitch.mdx`; its TEXS entries
are replaceable ID 11 plus `Textures/LightningBall.blp` and
`Textures/BlueSqGlow.blp`. Object data assigns Cliff1.tga (DTrx/DTfx) and
Cliff0.tga (DTrf/DTfp) as the ID-11 replacement. Both MDX models are in War3x.mpq only. `gutz.blp` and
`LightningBall.blp` exist in War3.mpq and War3x.mpq; `BlueSqGlow.blp` is in
War3x.mpq only. Cliff0/Cliff1 replacement BLPs are in War3.mpq. The model and
one switch layer therefore require expansion data. OpenWarcraft already has
expansion archive visibility controls; verify HumanX06 launches with expansion
enabled and both War3x.mpq/War3xLocal.mpq mounted. The existing renderer
retains TEXS replaceable IDs, binds the entity image for replaceable slots, and
converts object-data `.tga` names to `.blp`. The HumanX06 runtime trace showed
that the blight path appended `Blight` to the authored `Cliff1.tga` replacement
and requested the nonexistent `Cliff1Blight.tga`; the renderer then bound its
placeholder for MDX replaceable texture ID 11. Keep the destructable's authored
image for this replacement and use the server-authored green vertex tint for
blight presentation. Do not infer a suffixed asset path from `textureFile`
unless the corresponding asset is present in the loaded archives.
