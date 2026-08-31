# Warcraft III Fog And Cinematic Visibility

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

The server's existing `G_FowPlayerCanSeeEntity()` remains the foreign-entity visibility gate. The client receives current and explored
fog as separate planes, so world fog and minimap fog consume the same authoritative state while the minimap camera box continues to
derive from camera state independently.

## Runtime Diagnostics

`wc3_fow_debug` is an off-by-default runtime diagnostic for campaign/scripted reveal investigations. Use level `1` to trace fog-related
JASS calls and fog-modifier create/start/stop lifecycle, including player, state, shared-vision and `afterUnits` arguments, world
bounds/center, and the authoritative server sample cell after each direct write. Level `2` additionally traces active-modifier sample
cells plus explored-plane network chunks on the server and the corresponding explored rows after client decode.

```sh
+set wc3_fow_debug 2
```

Logs use the `WC3_FOW` prefix. For a reveal that visually remains masked, the expected sequence is a `set_*` or `modifier_start` line, a
server result with `explored=1` (and `visible=1` for `VISIBLE`), a `send_explored` line covering the changed rows, and a matching
`client_explored` line whose explored count is non-zero. The first missing stage identifies whether the problem is script dispatch,
authoritative fog mutation, or server-to-client fog transport. `FogEnable` and `FogMaskEnable` are also logged because cinematic scripts
may use those globals instead of region modifiers. Keep the cvar at `0` outside bounded investigations.

## Known Gaps

This change intentionally does not alter unrelated compatibility areas that need broader evidence:

- `FogEnable` / `FogMaskEnable` retain their existing global behavior.
- Invisible-unit detection/true-sight is not added to `G_FowPlayerCanSeeEntity()`.
- `FOW_CELL_SIZE` is unchanged.
- Camera Z-offset native parity is separate from fog-state handling.

## Verification

The WC3 API tests cover:

- all three direct fog states through rect/radius/radius-location natives;
- `useSharedVision` propagation;
- a same-turn `VISIBLE` start/stop still recording exploration;
- a temporary `VISIBLE` modifier falling back to `FOGGED`;
- active `FOGGED` and `MASKED` modifiers.

Runtime campaign validation should additionally check that a camera-only pan into unexplored terrain remains masked and that a
cinematic reveal can show a remote area without coupling camera movement to fog mutation.
