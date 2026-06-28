# WC3 Cinematic / Cutscene System

## Architecture

Cutscenes in Warcraft III are driven entirely by the map's JASS script (`war3map.j`). The engine provides JASS native bindings; the script orchestrates timing, camera, dialogue, and unit movement.

### Flow

1. **Enter cinematic mode:** JASS calls `CinematicModeBJ(true, player)` → `ShowInterface(false)` → sets `client_ui_state = CLIENT_UI_CINEMATIC`.
2. **Dialogue:** `TransmissionFromUnitWithNameBJ(...)` → `SetCinematicSceneBJ(...)` → `SetCinematicScene(...)` → sets speaker/dialogue text on `currentplayer`.
3. **Unit movement:** `IssuePointOrderLocBJ(unit, "move", location)` → `unit_issueorder` → `order_move` → `unit_setmove(self, &move_move_walk)`.
4. **Per-frame update:** `monster_think` → `M_MoveFrame` advances `self->s.frame` each game tick. `ai_move_walk` moves the unit.

### ESC / Skip Mechanism

- ESC is bound to `cmd cancel` (`share/openwarcraft3.cfg` line 9).
- `CMD_Cancel` (`g_commands.c:199`) publishes `EVENT_PLAYER_END_CINEMATIC` for the canceling player and all other human players.
- The map registers a trigger via `TriggerRegisterPlayerEventEndCinematic(trigger, player)`. When the event fires, the trigger sets a skip flag (e.g. `udg_IntroSkipped = true`) and calls cleanup: `CinematicModeBJ(false, ...)`, `SetUserControlForceOn(...)`, `ResetToGameCameraForPlayer(...)`.
- The main cinematic coroutine checks the skip flag after each `TriggerSleepAction` and returns early.

### Skip Cutscene Cvar

The `skip_cutscene` cvar provides an engine-level fast-forward. When set to `1`:
- `SetCinematicScene` returns early (no dialogue)
- `ShowInterface` forces UI visible
- `EnableUserControl` forces user control on
- `TriggerSleepAction` sleeps only 1ms
- Camera durations forced to 0
- Cinefilter forced off

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
| `games/warcraft-3/ui/ui_main.c` | `UI_DrawCinematicPanel()` — renders speaker/dialogue text |

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
Indicates wrong `currentplayer` context in the JASS VM. Check `jass_eventplayer(unit)` in trigger evaluation.

**TransmissionFromUnitWithNameBJ not showing dialogue:**
`ForceEnumPlayers` must populate the force for `IsPlayerInForce(GetLocalPlayer(), ...)` guards in `Blizzard.j`. If empty, all transmissions are skipped silently.

**Transmissions flash too fast:**
`TriggerWaitForSound` must sleep the full millisecond duration, not a fraction.

**Cinematic HUD layers hidden:**
`CLIENT_UI_CINEMATIC` hides portrait, console, command bar, info panel, inventory via `UI_LayoutShouldSkipLayoutLayer` in `client/cl_unit_layout.c`.

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

### Cinefilter

Full-screen overlay effects (fades, blurs) use `SetCineFilterTexture`/`SetCineFilterStartColor`/`SetCineFilterEndColor`/`SetCineFilterDuration`/`DisplayCineFilter`. The runtime interpolation is in `G_Cinefade()` which lerps between start/end alpha.
