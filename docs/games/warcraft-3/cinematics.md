# WC3 Cinematic / Cutscene System

## Architecture

Cutscenes in Warcraft III are driven entirely by the map's JASS script (`war3map.j`). The engine provides JASS native bindings; the script orchestrates timing, camera, dialogue, and unit movement.

### Flow

1. **Enter cinematic mode:** JASS calls `CinematicModeBJ(true, player)` → `ShowInterface(false)` → sets `client_ui_state = CLIENT_UI_CINEMATIC`.
2. **Dialogue:** `TransmissionFromUnitWithNameBJ(...)` → `SetCinematicSceneBJ(...)` → `SetCinematicScene(...)` → sets speaker/dialogue text on `currentplayer`.
3. **Unit movement:** `IssuePointOrderLocBJ(unit, "move", location)` → `unit_issueorder` → `order_move` → `unit_setmove(self, &move_move_walk)`.
4. **Per-frame update:** `monster_think` → `M_MoveFrame` advances `self->s.frame` each game tick. `ai_move_walk` moves the unit.

### ESC / Skip Mechanism

- ESC is bound to `cmd cancel` (`games/warcraft-3/share/config.cfg` line 9).
- `CMD_Cancel` (`g_commands.c:199`) publishes `EVENT_PLAYER_END_CINEMATIC` for the canceling player and all other human players.
- The map registers a trigger via `TriggerRegisterPlayerEventEndCinematic(trigger, player)`. When the event fires, the trigger sets a skip flag (e.g. `udg_IntroSkipped = true`) and calls cleanup: `CinematicModeBJ(false, ...)`, `SetUserControlForceOn(...)`, `ResetToGameCameraForPlayer(...)`.
- The main cinematic coroutine checks the skip flag after each `TriggerSleepAction` and returns early.

### Skip Cutscene Cvar

The `skip_cutscene` cvar provides an engine-level fast-forward. When set to `1`:
- `SetCinematicScene` returns early (no dialogue)
- `TriggerSleepAction` sleeps only 1ms
- Camera durations forced to 0
- Cinefilter forced off

`ShowInterface` and `EnableUserControl` still honor the JASS booleans while the script fast-forwards. Keeping cinematic
UI/input state prevents edge-scroll camera messages from racing the script: the final camera setup must land before cleanup
restores input.

This is separate from the JASS-level ESC skip mechanism.

## Key Files

| File | Role |
|------|------|
| `games/warcraft-3/game/api/api_misc.h` | `SetCinematicScene`, `EndCinematicScene`, `ShowInterface`, `EnableUserControl` |
| `games/warcraft-3/game/api/api_camera.h` | Camera control natives (all check `G_SkipCutscene`) |
| `games/warcraft-3/game/api/api_cinefilter.h` | Cinefilter overlay (fades, masks) |
| `games/warcraft-3/game/api/api_trigger.h` | `TriggerSleepAction`, `TriggerWaitForSound` |
| `games/warcraft-3/game/api/api_unit.h` | `IssuePointOrderLoc`, `SetUnitAnimation`, `SetUnitPosition` |
| `games/warcraft-3/game/g_commands.c` | `CMD_Cancel` — publishes `EVENT_PLAYER_END_CINEMATIC` |
| `games/warcraft-3/game/g_main.c` | `G_SkipCutscene()` (cvar check), `G_Cinefade()`, `G_RunClients()` (camera lerp) |
| `games/warcraft-3/game/g_ai.c` | `unit_setmove()`, `unit_changeangle()`, `unit_moveindirection()` |
| `games/warcraft-3/game/g_monster.c` | `M_MoveFrame()` (animation clock), `monster_think()` |
| `games/warcraft-3/game/skills/s_move.c` | `order_move()`, `ai_move_walk()` |
| `games/warcraft-3/game/g_events.c` | `G_ExecuteEvent()` — dispatches JASS triggers |
| `games/warcraft-3/jass/jdo.c` | `jass_calltrigger()`, `jass_evaluatetrigger()` — coroutine execution |
| `games/warcraft-3/game/hud/hud_cinematic.c` | Loads FDF cinematic/message frames, binds runtime data, serializes HUD layers |
| `games/warcraft-3/game/hud/hud_write.c` | C-constructed `FT_FRAME`+`FT_TEXTAREA` message overlay (size, font, inset, anchor) |

## Debugging

### Console Commands
- `skip_cutscene 1` — fast-forward all cinematic timing
- `skip_cutscene 0` — restore normal timing

### Log Output
- `SetCinematicScene: player=N speaker=... time=T` — dialogue shown for player N
- `EndCinematicScene: player=N time=T` — dialogue cleared for player N
- `Game event matched: type=17 ... disabled=0/1` — `EVENT_PLAYER_END_CINEMATIC` dispatch
- `Client cancel command: player=N ...` — ESC pressed by player N

### Common Issues

**Mismatched player numbers in SetCinematicScene/EndCinematicScene:**
`currentplayer` is the `GetLocalPlayer()` presentation selector, not the owner
of the unit that fired an event. Event ownership belongs in
`JASSCONTEXT.playerState` for `GetTriggerPlayer()`; local-player execution lives
in `JASSCONTEXT.localPlayerState`. `TriggerExecute()` must inherit those values
independently.

Human02 provides a concrete regression case: the victory chain begins when the
Blademaster (map player 4) dies, then executes nested victory/cinematic triggers
whose local UI guards target the connected Human player (map player 1). If the
dying unit owner is copied into `currentplayer`, the trigger and waits still run
but the player-1 camera/UI/dialogue branches are skipped, producing an invisible
victory cinematic before `RemovePlayer(Player(1), VICTORY)`. The
`wc3_api.enemy_event_keeps_trigger_player_separate_from_local_player_context`
test covers this event-owner/local-player split through a nested
`TriggerExecute()` chain.

**ESC moves units but leaves both the cinematic camera and cinematic HUD active:**
After the entity/player contract trim (#162), an incremental build could leave `libjass` compiled against the old
`edict_t`/`GAMECLIENT` layout. `jass.h` includes `game/g_local.h`; `jass_eventplayer()` dereferences `unit->client->ps`,
but the original JASS make rule tracked only JASS sources and `libshared`, not game/common headers.
The trigger's global actions still ran, while `GetLocalPlayer()` guards failed for camera/UI cleanup.

Confirmed on ROC `Maps\Campaign\Human02.w3m`: connection edict 0 represents map player 1. Before rebuilding the VM,
ESC at 4200 ms left server and decoded client `client_ui_state=2`, cinematic quaternion and FOV 45; camera prediction
was inactive. Rebuilding JASS without changing its behavior restored the same skip path: `ResetToGameCamera` and
`ShowInterface(true)` ran for player 1, both sides returned to state 0, FOV 50, distance 1650, gameplay quaternion
approximately `(-0.292,0,0,0.956)`. This was a stale module ABI, not a missing snapshot field.

`JASS_HEADERS` in `games/warcraft-3/game.mk` now tracks the shared/game header dependency closure. Do not widen the
network structs or force the client UI to game mode to mask this failure.

Regression checks:

```sh
make test-jass-build
make test-wc3-engine WC3_PATTERN='wc3_api.escape*'
make test
```

The VM test sends the real cancel command through the event queue, uses a nonmatching connection/map slot, checks an
unrelated player's cancel does nothing, and asserts gameplay UI flags, control, target, FOV, distance and quaternion.
`net.cinematic_cleanup_restores_camera_and_ui_samples` separately checks serialization through `CL_ParseServerMessage`.
The build test uses `make -n -W <header>`; removing `JASS_HEADERS` makes it fail on `common/shared.h`.

For bounded campaign verification, create a config containing 150 `wait` lines, `cmd cancel`, 80 more `wait` lines,
then `screenshot`. Launch once without `-tft` and once with it:

```sh
build/bin/openwarcraft3 -data 'data/Warcraft III' +map 'Maps\Campaign\Human02.w3m' +exec /tmp/escape.cfg +com_frame_limit 280
```

Use the ESC binding's command (`cmd cancel`), not `skip_cutscene`, which exercises a different lifecycle.
See [build/platform contracts](../../build-and-renderer-platforms.md) for incremental-build and pixel-format constraints.

**TransmissionFromUnitWithNameBJ not showing dialogue:**
`ForceEnumPlayers` must populate the force for `IsPlayerInForce(GetLocalPlayer(), ...)` guards in `Blizzard.j`. If empty, all transmissions are skipped silently.

**Transmissions flash too fast:**
`TriggerWaitForSound` must sleep the full millisecond duration, not a fraction.

**Cinematic HUD layers hidden:**
`CLIENT_UI_CINEMATIC` hides portrait, console, command bar, info panel, inventory via `UI_LayoutShouldSkipLayoutLayer` in `client/cl_unit_layout.c`.

**Cinematic chrome/text visible but talking-head portraits missing:**
Warcraft unit portraits use a companion model (`Foo_Portrait.mdx`) stored in `cl.portraits[]` beside the world model in `cl.models[]`. Initial `CS_MODELS` configstrings must remain registration data until `CL_PrepRefresh()`, because that pass loads both files. Do not eagerly populate `cl.models[]` from `CL_ParseConfigString()` before `cl.refresh_prepped`: `CL_PrepRefresh()` skips non-null model slots, which otherwise prevents every companion portrait from loading. Once refresh is prepared, a dynamic model configstring must release and reload both the world-model and portrait-model slots together. `SetCinematicScene` transports the normal model configstring index; `FT_PORTRAIT` prefers `cl.portraits[index]` on the client.

**Fast-forward ends at the last cinematic camera position:**
Log server `playerState.origin`, client camera prediction, `client_ui_state`, and `no_control` together. If gameplay input
becomes active before the final camera native, edge scrolling can send `clc_camera_position` during the accelerated script
and overwrite the authoritative snap. `skip_cutscene` must shorten timing only; the JASS cleanup owns the UI and input
transition.

### DisplayText Message Overlay

`DisplayTextToPlayer` and its timed variants pass message content and `(x,y)` screen position to `UI_ShowText`. The server constructs the message overlay as a static C `FRAMEDEF` pair (`FT_FRAME` root with `FT_TEXTAREA` child) in `hud_write.c`. The C code owns the full-screen parent, text-area size, font, inset, and default anchor as inline float values. A valid JASS position overrides only the copied text frame's anchor for that serialized message. Missing or out-of-range coordinates retain the default anchor.

## Branch Maintenance

WC3 gameplay features live on `main` as `wc3:` commits. Feature branches that diverge from `main` will miss these fixes. Before testing WC3 campaign maps, always check that the branch is up to date with `main`:
```
git log --oneline main..HEAD | wc -l   # commits behind main
git log --oneline HEAD..main | wc -l   # commits ahead of main
```

Key `wc3:` commits to watch:
- `6cd01ebd` — cinematic dialogs/portraits (ForceEnumPlayers fix)
- `55724517` — collision & pathfinding parity
- `76b701a4` — JASS natives (camera, events)
- `4a56a651` — gradual unit turning
- `dcac4868` — authentic collisionSize

## Implementation Notes

### Unit Movement During Cutscenes

Units move using the same system as normal gameplay: JASS scripts issue move orders via `IssuePointOrder`/`IssuePointOrderLoc`. The pathfinding and collision system handles the rest. For cutscenes with many simultaneous movers (e.g. 8 footmen), the destination-keyed heatmap cache (16 slots) and unreachable-cell skipping prevent performance issues.

### Camera Control

Camera follows units via `SetCameraTargetController`. The camera interpolation runs in `G_RunClients()` each frame, lerping position/quaternion/FOV between `camera.start_time` and `camera.end_time`. The `G_UpdateCameraTarget` function follows `target_controller` unit's position plus offset.

`SetCameraQuickPosition` is not a camera movement native. It records Warcraft's spacebar/quick-position recall point for the local player. Assigning that point must leave `camera.state.position` unchanged; treating it like `SetCameraPosition` makes quest discovery and cinematic scripts unexpectedly jump the view when they only intend to set the later spacebar target.

Space first traverses the WC3 UI's eight-entry automatic recent-alert history. When that history is empty, the UI sends the game-owned `quickcamera` command; the server reads the authoritative scripted quick position and applies it through normal camera bounds/state handling. See [alerts-and-minimap-pings.md](alerts-and-minimap-pings.md).

Camera target bounds are per-player runtime state. `G_InitMapPlayer()` initializes `PLAYER.camera_bounds` from the four W3I camera-bound points, then `SetCameraBounds` may replace that rectangle at runtime. Because OpenRealm executes one server-side JASS VM rather than one VM per rendered client, an unscoped `SetCameraBounds` (`currentplayer == NULL`) applies to every game client; inside a local-player context it updates only that player's bounds. `GetCameraBoundMinX/Y` and `GetCameraBoundMaxX/Y` read the same current rectangle. Do not mutate `level.mapinfo->cameraBounds`: W3I remains immutable initial map metadata.

W3I stores its four integer camera complements in **left, right, bottom, top** order, while the JASS `CAMERA_MARGIN_*` selector constants are left, right, top, bottom. `mapCameraBounds_t.complement` deliberately follows the W3I on-disk order because `CM_ReadInfoInto()` reads the structure directly. The complements crop the complete W3E terrain to the playable rectangle; they are not camera margins themselves. `GetCameraMargin()` first reconstructs that playable rectangle from `CM_GetWorldBounds()` plus the complements, derives the axis-aligned default camera rectangle from the eight W3I camera-bound floats, and returns the gap between those two rectangles on the requested side. This matches Warsmash's `GetCameraMargin`: playable-map area and default camera bounds are separate inputs.

Do not implement `GetCameraMargin()` as `complement * TILE_SIZE`. World Editor generated `main` functions already start from playable-area edge constants and add/subtract `GetCameraMargin()` before calling `SetCameraBounds`; using the complement width there applies the unplayable terrain border a second time. Once `SetCameraBounds` became authoritative in OpenRealm, that old approximation manifested as the camera stopping roughly a viewport too early at the map edge. Warsmash's `GameCameraManager.applyVelocity()` also clamps its camera target directly to the rectangle supplied by `SetCameraBounds`, so the correct fix is to reproduce the native margin calculation rather than expanding the runtime clamp by a guessed viewport offset.

All camera target writers use `G_ClampCameraPosition()`: user `clc_camera_position`, `SetCameraPosition`/`PanCameraTo`, camera setups, and target-controller following. The WC3 player snapshot carries the current `BOX2 camera_bounds`, allowing `CL_SetCameraPosition()` to clamp edge-scroll, keyboard, middle-drag, and minimap click/drag before applying local prediction. When a new snapshot shrinks the bounds, `CL_ParsePlayerInfo()` reclamps any pending prediction before comparing it with the authoritative server origin; this prevents the old loop where the server rejected an out-of-bounds prediction and the client immediately restored it.

The camera bounds constrain the **target**, not the visible frustum. Do not widen `SetCameraBounds` to compensate for a view that appears to stop too early. WC3's world projection must instead use the actual gameplay viewport above the command console. `V_RenderView()` assigns `{0, 0.22, 1, 0.76}` to both `viewDef.viewport` and `viewDef.scissor`; `Matrix4_getCameraMatrix()` derives aspect ratio from that viewport rather than the full window. Renderer screen rays (`R_LineForScreenPoint`) and drag-selection normalization use the same viewport, so ground picking, middle-dragging, minimap camera-footprint projection, and the visible world all agree. A full-window projection followed only by scissoring narrows the horizontal ground footprint and leaves the camera target vertically off-center inside the visible WC3 world area; near map edges this can look like an over-tight camera clamp even when the JASS bounds themselves are correct.

The August 30, 2026 viewport change (`9c22952`, `wc3: match retail camera viewport and bounds semantics`) also established a presentation contract for post-world overlays. `R_RenderView()` calls `R_RevertSettings()` before drawing 2D overhead bars, and `V_RenderView()` draws the drag-selection marquee after `re.RenderFrame()`, so both paths are back in full-window GL state even though their content belongs to the world. Those overlays must temporarily re-apply `viewDef.scissor` and then restore the full-window scissor before `SCR_DrawLayout()` draws the HUD. Do not move the WC3 HUD boundary into renderer constants or rely on opaque console art to hide world overlays; the client-authored `viewDef.scissor` remains the authoritative rectangular world boundary.

The classic console is not itself rectangular: the retained info/status panel, portrait/minimap region, build queue, and command-card buttons can protrude above the flat world-scissor bottom while still being authored HUD. Drag selection therefore has one additional client-side rule. `SCR_LayoutClampSelectionRect()` examines the visible retained layout frames that extend into the bottom console and constrains the marquee's moving corner before the rectangle is used for both drawing and `EntitiesInRect()`. This keeps the selection box out of transparent status/command-card art without changing the camera viewport or teaching the shared renderer Warcraft-specific button coordinates. Full-screen world games naturally skip this rule because their world scissor already reaches the bottom of the UI canvas. See [economy-and-unit-presentation.md](economy-and-unit-presentation.md) for overhead-bar presentation details.

Regression coverage:

```sh
make test-wc3-engine WC3_PATTERN='wc3_api.camera_bounds*'
make test
```

The full test target covers the standalone `net.playerstate_camera_bounds_roundtrip` and `net.camera_prediction_reconciles_to_server_clamped_bound` cases. The implementation change must also retain the existing cinematic cleanup tests because camera bounds are transmitted in `PLAYER`. Camera JASS regression tests run against the deliberately minimal `games/warcraft-3/tests/resources-src/Scripts/common.j`; when a test uses a `common.j` constant or native, add its real declaration/value to that fixture. In particular, `GetCameraMargin` requires the integer `CAMERA_MARGIN_LEFT/RIGHT/TOP/BOTTOM` selectors (`0/1/2/3`). Leaving those globals undefined passes a non-integer value to the native and trips `jass_checkinteger()` before the assertion can run.

Visual viewport-overlay verification must cover both archive modes because the HUD art can differ while the world boundary contract must not:

```sh
build/bin/openwarcraft3 -data 'data/Warcraft III' -roc +map 'Maps\Campaign\Human02.w3m' +com_frame_limit 1200
build/bin/openwarcraft3 -data 'data/Warcraft III' -tft +map 'Maps\Campaign\Human02.w3m' +com_frame_limit 1200
```

During each bounded run, drag a world selection marquee downward through the command console and select a living unit near the lower world edge. Overhead health/mana bars must stop at `viewDef.scissor`. The marquee must stop earlier wherever retained bottom-console frames protrude above that rectangular boundary, including the status/info panel and command-card/build-button area; no console pixel may reveal the marquee.

### Cinefilter

Full-screen overlay effects (fades, blurs) use `SetCineFilterTexture`/`SetCineFilterStartColor`/`SetCineFilterEndColor`/`SetCineFilterDuration`/`DisplayCineFilter`. The runtime interpolation is in `G_Cinefade()` which lerps between start/end alpha.
