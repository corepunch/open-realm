# Team Colors

Warcraft III keeps ownership, alliance team, and presentation color as separate state. OpenRealm follows that contract:

- `playerState_t.number` identifies the player slot/owner.
- `playerState_t.team` identifies the alliance/team assignment used by lobby/gameplay rules.
- `playerState_t.color` is the Warcraft player-color index used by MDX `TeamColor`/`TeamGlow` replaceable textures.

Do not derive one of these values from another after map/lobby initialization.

## Rendering data flow

The Warcraft game module resolves a unit's visible team color and publishes it in `entityState_t.effect_flags` using `EFX_TEAM_COLOR_MASK`. The payload stores `playercolor + 1`; zero remains the generic "no published override" value and therefore does not collide with `PLAYER_COLOR_RED` (`0`).

The client/renderer remains game-agnostic: it consumes the published color and the MDX renderer selects `ReplaceableTextures\\TeamColor\\TeamColorXX.blp` and `TeamGlowXX.blp` for replaceable texture IDs 1 and 2. `G_SetEntityTeamColor()` is the game-side helper for non-unit presentation entities that use the same snapshot payload.

A newly spawned Warcraft unit resolves its initial color in this order:

1. `UnitUI.teamColor` (`utco`) when the unit type forces a color.
2. Otherwise the owning player's configured `playerState_t.color`.

For units authored in `war3mapUnits.doo`, the placement's custom team-color field takes precedence when `UnitUI.customTeamColor` (`utcc`) permits custom colors and the placement value is not `-1`. If it is unavailable/disallowed, the normal `utco` then owner-color rules apply.

## Derived presentation color

Visuals that belong to a unit must inherit the unit's **resolved presentation color**, not re-derive color from `entityState_t.player`. This matters after `SetPlayerColor`, `SetUnitColor`, authored `utco`/`utcc`, and ownership changes.

Current inheritance paths are:

- ranged attack missile edicts copy `G_GetUnitTeamColor(attacker)` at launch;
- Storm Bolt / Fire Bolt ability missiles copy the caster's resolved color at launch;
- model effects attached to a unit through `G_SpawnModelEffect(..., target, ...)` copy the target unit's resolved color; point-only effects have no owning unit and therefore keep the renderer default unless some future API supplies a color explicitly;
- the selected-unit portrait serializes `G_GetUnitTeamColor(selected)` in the portrait frame `stat`, so `SCR_LayoutDrawPortrait()` uses the same MDX replaceable texture color as the world model;
- gameplay/cinematic transmission portraits already carry the explicit `SetCinematicScene` playercolor through `UI_PLAYERSTAT_CINEMATIC_PORTRAIT_COLOR`;
- the build-placement cursor publishes the configured owner `playerState_t.color` in its cursor entity, matching Warsmash's build-cursor behavior;
- the rally destination indicator publishes the owning player's configured color;
- automatic ordinary minimap unit/building contacts render the local player's contacts white in every ally-filter state; in state `0`, non-local contacts use `renderEntity_t.team` so they inherit the resolved unit presentation color instead of re-deriving a player-slot color, while states `1` and `2` use relationship colours for non-local contacts (ally teal, hostile red, neutral/passive black);
- renderer-created authored attachment children and building-damage fire already inherit their parent `renderEntity_t.team`.

These values are sampled when the transient/derived visual is created. Existing projectile/effect instances are not recolored retroactively if the unit's color changes later.

## Runtime color changes

`SetUnitColor` changes presentation only; it does not change ownership. Explicit unit colors are stored with `WC3_UNIT_COLOR_OVERRIDE_FLAG`, allowing explicit red to remain distinguishable from the default/no-override state across campaign game-cache restore. Legacy nonzero raw `unit_color` values remain accepted when restoring older OpenRealm state.

`SetPlayerColor` updates the player's configured color and recolors existing units that are still displaying that player's previous color. Units already displaying a different explicit/custom color remain unchanged. This matches the Warsmash behavior used as the current compatibility reference.

`SetUnitOwner(unit, player, changeColor)` always transfers gameplay ownership. When `changeColor` is true, the unit is recolored to the new owner's configured player color. When false, its current visible color is preserved independently of the new owner.

## Lobby behavior

The game-setup lobby transports team and color independently. Maps with Fixed Player Settings disable both team and color controls; their slot colors cannot be cycled by the host.

## Known limits

OpenRealm currently exposes Warcraft player-color handles through the extended color range, but the active multiplayer/player and MDX texture contracts are still `MAX_PLAYERS == 16` / `MAX_TEAMS == 16`. Extending rendered/lobby colors past the classic 16 is separate work because it changes those shared limits and resource assumptions.

Map `config()` now runs before the map's normal script start/UI initialization, so its gameplay-time player colors are available before `main`. It is still not a distinct **pre-lobby** configuration phase, so exact authored-map-before-lobby slot/color precedence remains separate lifecycle work described in [UI Flow](../../architecture/ui-flow.md).

Point-only JASS/special effects do not have an owning unit/player color to inherit. OpenRealm intentionally leaves them without an explicit team-color payload rather than guessing a player slot.

## Verification

Focused in-engine coverage lives in `games/warcraft-3/game/tests/t_api.c`, `t_combat.c`, `t_ability_lifecycle.c`, `t_game.c`, `t_building.c`, and `t_rally.c`, and covers:

- recoloring existing owner-colored units through `SetPlayerColor`;
- preserving an explicit unit color when the owner player's color changes;
- `SetUnitOwner` with both values of `changeColor`;
- `utco` / `utcc` / `war3mapUnits.doo` custom-color precedence;
- explicit red `SetUnitColor` state and campaign game-cache restore;
- attached model-effect inheritance from an explicitly colored target;
- ranged attack and Storm Bolt/Fire Bolt projectile inheritance;
- selected-unit portrait color propagation;
- build-placement and rally-indicator configured-player color propagation.
