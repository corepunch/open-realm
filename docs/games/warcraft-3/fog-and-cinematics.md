# Warcraft III Fog And Cinematic Visibility

> This document covers gameplay fog of war (exploration/current sight). Atmospheric distance mist from `SetTerrainFogEx` is a separate renderer/environment system; see [Environmental Terrain Fog](environmental-fog.md).

## Contract

Camera position, fog state, and entity visibility are independent systems. Camera natives move the gameplay camera without changing
fog, while fog natives can reveal or mask remote terrain without moving the camera.

OpenRealm represents Warcraft's three fog states with the existing per-player `explored` and `visible` planes:

| JASS state | `explored` | `visible` | Meaning |
| --- | ---: | ---: | --- |
| `FOG_OF_WAR_MASKED` | 0 | 0 | unexplored |
| `FOG_OF_WAR_FOGGED` | 1 | 0 | explored without current sight |
| `FOG_OF_WAR_VISIBLE` | 1 | 1 | explored with current sight |

`G_FowClearVisible()` clears current visibility without clearing exploration. A temporary visible reveal therefore naturally becomes
fogged after the modifier stops when no normal sight source still covers the area.

## Direct Trigger Reveals

`SetFogStateRect`, `SetFogStateRadius`, and `SetFogStateRadiusLoc` write directly into the authoritative player fog grid through
`G_FowSetStateRect()` and `G_FowSetStateRadius()`.

- `MASKED` clears `visible` and `explored`.
- `FOGGED` clears `visible` and sets `explored`.
- `VISIBLE` sets both planes.

The operations reuse the existing server fog-grid resolution and dirty-row network path. They do not maintain a second cinematic
exploration map. With `useSharedVision`, the same state is also applied to viewers receiving the source player's shared vision.

## Fog Modifiers

`CreateFogModifierRect`, `CreateFogModifierRadius`, and `CreateFogModifierRadiusLoc` create disabled modifier handles.
`FogModifierStart` applies the modifier once immediately and then makes it participate in `G_FowUpdate()`; `FogModifierStop` removes it. The synchronous first application is required for map scripts that start and stop/destroy a `VISIBLE` modifier in the same trigger turn to permanently explore an area without holding current sight open.

Started modifiers apply the same three-state cell writer after normal unit sight:

- `VISIBLE` keeps terrain explored and currently visible;
- `FOGGED` keeps terrain explored while suppressing current sight in the modifier area;
- `MASKED` keeps the modifier area unexplored while active.

Stopping a `VISIBLE` modifier stops forcing current vision but does not erase the exploration it already created, so the normal
`VISIBLE -> FOGGED` transition is preserved.

## Camera And Consumers

Camera movement does not call the fog API, and fog state changes do not call camera APIs. Maps that want a cinematic pan and reveal must
request both actions explicitly.

The server's existing `G_FowPlayerCanSeeEntity()` remains the foreign-entity visibility gate. Known gameplay invisibility is evaluated there per viewer: owners/shared-vision allies keep access to their invisible units, while hostile viewers need a detector covering the target. Permanent Invisibility (`Apiv`) has dedicated saved state; Sorceress Invisibility/Wind Walk and hidden wards retain `RF_HIDDEN`, but only those recognized invisibility states participate in this visibility policy. Snapshot customization clears `RF_HIDDEN` only for a viewer who is allowed to see that invisible entity, leaving the authoritative entity flag untouched. This deliberately avoids treating cargo, mine workers, revival/training placeholders, and other non-invisibility uses of `RF_HIDDEN` as revealable. The same viewer-specific predicate gates selection, automatic hostile acquisition, attack continuation, and unit-target spell validation. Far Sight's saved timed thinker and passive detector abilities contribute true sight through the shared query.

The client receives current and explored fog as separate planes, so world fog and minimap fog consume the same authoritative state while the minimap camera box continues to derive from camera state independently.

Day/night sight-radius selection consumes the server-owned clock documented in [time-of-day.md](time-of-day.md); fog must not derive a
second clock from `level.time`.

## Known Gaps

This change intentionally does not alter unrelated compatibility areas that need broader evidence:

- `FogEnable` / `FogMaskEnable` retain their existing global behavior.
- Ghost/Ghost Visible and Shadow Meld are not yet normalized into the player-aware invisibility path; current coverage is Permanent Invisibility, Sorceress Invisibility, Wind Walk, and the explicitly hidden ward mechanics.
- `FOW_CELL_SIZE` is unchanged.
- Camera Z-offset native parity is separate from fog-state handling.

## Verification

The WC3 API tests cover:

- all three direct fog states through rect/radius/radius-location natives;
- `useSharedVision` propagation;
- a same-turn `VISIBLE` start/stop still recording exploration;
- a temporary `VISIBLE` modifier falling back to `FOGGED`;
- active `FOGGED` and `MASKED` modifiers;
- Far Sight true sight and player-local Permanent Invisibility visibility are covered by the spell/ward regression suite.

Runtime campaign validation should additionally check that a camera-only pan into unexplored terrain remains masked and that a
cinematic reveal can show a remote area without coupling camera movement to fog mutation.
