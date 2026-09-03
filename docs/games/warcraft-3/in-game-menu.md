# Warcraft III In-Game Menu

## Contract

The in-game Menu is a Warcraft FDF overlay, not a front-end screen transition. The upper HUD
`UpperButtonBarMenuButton` and default F10 binding both send the server command `menu`. The game module serializes Blizzard's
`EscMenuMainPanel` plus its separately authored `EscMenuBackdrop` as one singleton `svc_window` with
`UI_WINDOW_MODAL | UI_WINDOW_UNIQUE`.

The current reachable flow is:

```text
Menu/F10 -> MainPanel
  Pause/Return -> close menu / resume
  End Game     -> EndGamePanel
    Previous   -> MainPanel
    Quit       -> leave current game/front-end
    Exit       -> ConfirmQuitPanel
      Cancel   -> EndGamePanel
      Confirm  -> exit application
```

Save, Load, Options, Help, Tips, and Restart remain visibly disabled until their underlying behavior exists. Current Warsmash also
disables `PauseButton`; OpenRealm deliberately retains its newer pause-menu behavior and labels that button `Resume Game`, with the
same close action as Return.

## OpenRealm Data Flow

```text
games/warcraft-3/share/config.cfg
  F10 -> cmd menu

hud/hud_console.c
  UpperButtonBarMenuButton -> menu

G_ClientCommand
  menu              -> UI_ShowMainMenu
  menu_endgame      -> UI_ShowGameMenuEndGame
  menu_confirm_exit -> UI_ShowGameMenuConfirmExit

hud/hud_menu.c
  EscMenuMainPanelGame_Load()
  -> bind EscMenuMainPanel + EscMenuBackdrop + child panels
  -> select visible panel
  -> size controller and backdrop to active panel
  -> svc_window(BZ_WC3_WINDOW_MENU, MODAL|UNIQUE)

client/cl_window.c
  first modal window            -> pause 1
  ordinary onclick              -> server command
  close_window[_notify]         -> close local window
  disconnect_game               -> CL_Disconnect(..., false)
  quit_application              -> queue normal `quit` command
  last modal closes/disconnects -> modal ownership released
```

Submenu changes replace the existing unique menu window. `CL_WindowOpen()` therefore preserves local window identity and the client
still sees a modal in the list, so moving MainPanel -> EndGamePanel -> ConfirmQuitPanel does not pulse pause ownership off and on.

## Layout

`EscMenuMainPanel.fdf` declares `EscMenuBackdrop` separately from `EscMenuMainPanel`. OpenRealm reparents the backdrop under the
controller and the authored panel subtrees beneath the backdrop so decoration is serialized/drawn before controls. Each panel change
copies the active panel's assigned width and height to both the controller and backdrop, matching Warsmash's
`updateEscMenuCurrentPanel()` sizing rule. The latest OpenRealm implementation centers the bounded controller/backdrop; this rebase
preserves that existing policy rather than restoring the older direct TOP/-0.05 anchor experiment.

The recipient's Warcraft race skin must be active before the FDF is first resolved because the Esc-menu art uses `DecorateFileNames`
skin keys. `EscMenuBackdrop` is additionally refreshed from the stock `EscMenuBackground` and `EscMenuBorder` keys for each recipient
before serialization. The FDF template cache is global, so this late resolution prevents stale decorated image indexes from turning
the menu backdrop into unrelated Esc-menu highlight/slider art. Keep menu load/write between `UI_SetCurrentClient(client)` and
`UI_SetCurrentClient(NULL)`.

## Pause And Modal Input

The menu uses the generic client-window modal lifecycle documented in [Pause And Modal UI](pause-and-modal-ui.md). Opening the first
client modal sends `pause 1`; closing the final modal sends `pause 0`. Warcraft game code maps that client modal owner to an
authoritative simulation pause only for the supported single-client policy, while server/network transport continues.

Do not stop `SV_Frame` wholesale. While the menu is open, modal input blocks world selection/orders, control groups, minimap input,
and manual camera movement; transport remains live so submenu and close commands can still arrive and the client cannot time out.

## Client-Owned Menu Actions

`UI_WINDOW_CLOSE_ACTION`, `UI_WINDOW_CLOSE_NOTIFY_ACTION`, `UI_WINDOW_DISCONNECT_ACTION`, and `UI_WINDOW_QUIT_ACTION` are interpreted
locally by `client/cl_window.c` rather than forwarded as game commands. The server authors which button exposes those tokens, but leave
and application-exit only happen after explicit local activation.

Use ordinary `onclick` strings for server-owned state transitions such as `menu_endgame` and `menu_confirm_exit`.

## Known Gaps

- Save, Load, Options, Help, Tips, and Restart remain disabled.
- The in-game pause button is an OpenRealm extension over the cited current Warsmash behavior: both Pause and Return resume/close.
- F10 opens the menu through the established OpenRealm binding. Current Warsmash Java itself wires the upper menu button but not its
  keyboard F10 route.

## Validation

Do not validate this from the front-end menu: `svc_window` requires an active game connection. On a campaign map, verify:

1. Mouse Menu and F10 both open MainPanel.
2. The first modal acquires pause only after the client receives the window; transport and UI remain responsive.
3. Save/Load/Options/Help/Tips render disabled and do nothing.
4. Pause and Return close the window and release the modal pause owner.
5. End Game opens EndGamePanel; Restart is disabled and pause remains owned continuously.
6. Previous returns to MainPanel without closing/reopening the modal lifecycle.
7. Quit leaves the map and returns to the Warcraft front-end.
8. Exit opens ConfirmQuitPanel; Cancel returns to EndGamePanel.
9. Confirm exits the application.
10. Leaving the menu open beyond the client timeout interval does not disconnect or advance paused simulation.
