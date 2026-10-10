# Warcraft III In-Game Menu

## Contract

The in-game Menu is a Warcraft FDF overlay, not a front-end screen transition. The upper HUD
`UpperButtonBarMenuButton` and default F10 binding both send the server command `menu`. The game module serializes Blizzard's
`EscMenuMainPanel` plus its separately authored `EscMenuBackdrop` as one singleton `svc_window` with
`UI_WINDOW_MODAL | UI_WINDOW_UNIQUE`.

The current reachable flow is:

```text
Menu/F10 -> MainPanel
  Save Game    -> EscMenuSaveGamePanel / editable name -> menu_save_named <name>
  Load Game    -> EscMenuSaveGamePanel / selectable save list -> menu_load_named <name>
  Pause/Return -> close menu / resume
  End Game     -> EndGamePanel
    Restart    -> reload current mission
    Previous   -> MainPanel
    Quit       -> campaign mission: campaign selector; otherwise front-end main menu
    Exit       -> ConfirmQuitPanel
      Cancel   -> EndGamePanel
      Confirm  -> exit application
```

Save and Load are enabled in single-player when Blizzard's authored `EscMenuSaveGamePanel` is available. The Save panel accepts a
name in `SaveGameFileEditBox`; the Load panel enumerates `.sav` files from the writable save directory and enables Load when at least
one file has a readable OpenRealm map header. The legacy `quick.sav` row is displayed as `Quick Save`. Options opens the
authored `EscMenuOptionsPanel` Sound page, whose checkboxes and sliders edit client audio cvars through
[server-authored cvar controls](../../architecture/client-windows.md#server-authored-cvar-controls); OK accepts, while Cancel
and Escape restore the values from before the page was opened. Help and Tips remain visibly disabled. OpenRealm enables the authored `RestartButton` for single-player missions, labels it `Restart Mission`, and
routes it through the existing deferred current-map reload used by `RestartGame`; multiplayer keeps the button visible but disabled.
Current Warsmash disables `PauseButton`. OpenRealm hides that duplicate control and keeps `ReturnButton` as the menu's single
resume action.

## OpenRealm Data Flow

```text
games/warcraft-3/share/config.cfg
  F10 -> cmd menu

hud/hud_console.c
  UpperButtonBarMenuButton -> menu

G_ClientCommand
  menu              -> UI_ShowMainMenu
  menu_endgame      -> UI_ShowGameMenuEndGame
  menu_restart      -> G_RequestRestartGame(false), single-player only
  menu_quit_game    -> G_RequestQuitGame -> campaign selector for campaign maps, otherwise main menu
  menu_confirm_exit -> UI_ShowGameMenuConfirmExit
  menu_save_game    -> UI_ShowGameMenuSave
  menu_load_game    -> UI_ShowGameMenuLoad
  menu_save_named <name> -> WriteGame(<save-dir>/<name>.sav)
  menu_load_named <name> -> G_RequestLoadGameNamed -> deferred MenuAction("load", name)
  menu_save_quick        -> compatibility wrapper for `Quick Save` / `quick`
  menu_load_quick        -> existing quick-load/front-end compatibility path

hud/hud_menu.c
  EscMenuMainPanelGame_Load()
  EscMenuSaveGamePanel_Load()
  -> bind EscMenuMainPanel + EscMenuBackdrop + child panels
  -> select visible panel
  -> size controller and backdrop to active panel
  -> svc_window(BZ_WC3_WINDOW_MENU, MODAL|UNIQUE)

client/cl_window.c
  first modal window                 -> pause 1
  transient EDITBOX                  -> client-owned text/cursor state
  transient LISTBOX                  -> client-owned selected row/scroll state
  `{ControlName}` onclick placeholder -> escaped current control value
  ordinary onclick                   -> server command
  close_window[_notify]              -> close local window
  disconnect_game                    -> generic deferred main-menu transition (not used by WC3 Quit Mission)
  quit_application                   -> queue normal `quit` command
  last modal closes/disconnects      -> modal ownership released
```

Submenu changes replace the existing unique menu window. `CL_WindowOpen()` therefore preserves local window identity and the client
still sees a modal in the list, so moving MainPanel -> EndGamePanel -> ConfirmQuitPanel does not pulse pause ownership off and on.

## Layout

`EscMenuMainPanel.fdf` declares `EscMenuBackdrop` separately from `EscMenuMainPanel`. OpenRealm reparents the backdrop under the
controller and the authored panel subtrees beneath the backdrop so decoration is serialized/drawn before controls. Each panel change
copies the active panel's assigned width and height to both the controller and backdrop, matching Warsmash's
`updateEscMenuCurrentPanel()` sizing rule. The latest OpenRealm implementation centers the bounded controller/backdrop; this rebase
preserves that existing policy rather than restoring the older direct TOP/-0.05 anchor experiment.

The client resolves the symbolic `DecorateFileNames` and Esc-menu skin keys through its local `war3skins.txt` after the server sends
the generic image configstrings. The FDF template cache is global, so the server must preserve those symbolic keys rather than
materializing a recipient's race-specific image index. Keep menu serialization between `UI_SetCurrentClient(client)` and
`UI_SetCurrentClient(NULL)` for per-recipient state, but do not resolve menu artwork on the server.

## Pause And Modal Input

The menu uses the generic client-window modal lifecycle documented in [Pause And Modal UI](pause-and-modal-ui.md). Opening the first
client modal sends `pause 1`; closing the final modal sends `pause 0`. Warcraft game code maps that client modal owner to an
authoritative simulation pause only for the supported single-client policy, while server/network transport continues.

Do not stop `SV_Frame` wholesale. While the menu is open, modal input blocks world selection/orders, control groups, minimap input,
and manual camera movement; transport remains live so submenu and close commands can still arrive and the client cannot time out.

## Client-Owned Menu Actions

`UI_WINDOW_CLOSE_ACTION`, `UI_WINDOW_CLOSE_NOTIFY_ACTION`, `UI_WINDOW_CLOSE_COMMAND_PREFIX`, `UI_WINDOW_DISCONNECT_ACTION`, and
`UI_WINDOW_QUIT_ACTION` are interpreted locally by `client/cl_window.c`. Restart uses `close_window_command menu_restart`, so the
client forwards the server-owned restart request and immediately releases the menu's modal/pause ownership while the deferred map
reload is pending. Quit uses the same close-and-forward pattern with `menu_quit_game`: Warcraft game code still owns whether the
outgoing world is a campaign mission, and `G_RequestQuitGame()` selects `menu_single_player_campaign` for classic or Frozen Throne
campaign map paths while preserving `menu_main` for non-campaign games. The generic `disconnect_game` client action remains a
main-menu-only session boundary and must not be used for WC3 Quit Mission because it has no campaign context. Neither path may call
`CL_Disconnect()` and immediately show the menu while the campaign server/game module is still alive, because game teardown owns
FDF/template state that the front-end must rebuild afterward.

Use ordinary `onclick` strings for server-owned state transitions such as `menu_endgame` and `menu_confirm_exit`. Save/Load commit
buttons use `close_window_command ...` plus control placeholders such as `{SaveGameFileEditBox}` and the resolved inner save-list
control. The client expands those from its local edit/list state, escapes quotes/backslashes, forwards the command, and then closes
the menu. `modal_flags` and
`quest_dialog_open` are runtime-only save fields, so a save requested while the modal still owns pause cannot restore that ownership
without a live client window.

## Save/Load Scope

The in-game panel now supports persistent named saves. `FS_ListSaves()` enumerates `.sav` basenames from the same directory used by
`FS_SavePath()`, sorts them newest-first with a case-insensitive tie-break, and the game module filters out entries whose save header no longer resolves to a map.
The Save/Load presentation uses Blizzard's authored `EscMenuSaveGamePanel` and its `FileListFrame` placeholder; gameplay code does not
reparent, resize, or otherwise patch that FDF layout. Retail code creates the chooser control in this placeholder, so OpenRealm loads
the authored `MapListBox` backdrop and scrollbar and adds a native `FT_LISTBOX` state child. The transient `uiListBox_t` transport keeps rows selectable and
scrollable. Rows use `display\thidden-value`; the renderer shows only the display text while the selected hidden basename is
submitted. The list renderer uses the same whole-row count as scrolling/hit-testing (`floor(innerHeight / itemHeight)`) and clips each
row to its own rectangle, so a partial extra row or glyph overhang cannot escape the chooser. Selecting a row copies that basename
into the Save edit box so it can be overwritten or amended. `quick` is shown as `Quick Save`.

`SaveGameFileEditBox` is serialized from its authored panel as `uiEditBox_t` with its FDF name and max length. Each
new Save dialog opens with a filesystem-safe local timestamp such as `2026-09-07 01-42-30`; the player may freely edit that default
before committing. The transient-window client owns editing while
the modal lives; text is not round-tripped to the server on every keypress. The Save command submits the final value once. Menu save
names are trimmed, may optionally end in `.sav`, are limited to `CMDARG_LEN - 1`, and reject control characters plus
`\ / : * ? " < > |`. `Quick Save` normalizes to the existing `quick` basename. Saving the same name currently replaces that file
directly; the authored overwrite-confirm panel remains disabled. Delete removes the selected row and refreshes the Save panel without
closing the modal.

Named loads use `G_RequestLoadGameNamed()` -> `MenuAction("load", name)`. This keeps the same Quake-II-style session boundary as the
console `load` command: the selected save is captured while the window callback is active, then `SV_GetSaveMap` / `SV_LoadGame` run
from the following client frame after the callback has returned.

## Known Gaps

- The overwrite-confirm panel remains disabled. Saving an existing name overwrites it immediately.
- Help and Tips remain disabled. Only the Sound page of Options is functional; its other pages and category toggles are disabled.
- Restart Mission is enabled only in single-player and reloads the current `map` cvar through `G_RequestRestartGame(false)`.
- The in-game pause button is an OpenRealm extension over the cited current Warsmash behavior: both Pause and Return resume/close.
- F10 opens the menu through the established OpenRealm binding. Current Warsmash Java itself wires the upper menu button but not its
  keyboard F10 route.

## Validation

Do not validate this from the front-end menu: `svc_window` requires an active game connection. On a campaign map, verify:

1. Mouse Menu and F10 both open MainPanel.
2. The first modal acquires pause only after the client receives the window; transport and UI remain responsive.
3. Save Game opens the authored Save dialog with the filename prefilled from local date/time, allows that value to be edited, and Save writes `<name>.sav` before closing the modal. `Quick Save` still targets `quick.sav`.
4. Save and Load both show readable saves in the authored scrolling list; row selection/scrolling stays local, and Load rebuilds from the selected save through the
   deferred named-load session action. A corrupt/unreadable save is not offered.
5. Cancel from Save/Load replaces the same unique window with MainPanel without releasing/reacquiring modal pause.
6. Pause and Return close the window and release the modal pause owner.
7. End Game opens EndGamePanel; in single-player, Restart Mission closes the menu and reloads the current mission; in multiplayer it remains disabled.
8. Quit Game leaves the world through the deferred session boundary and returns to a freshly rebuilt main menu with valid FDF bindings.
9. Previous returns to MainPanel without closing/reopening the modal lifecycle.
10. Quit leaves the map and returns to the Warcraft front-end.
11. Exit opens ConfirmQuitPanel; Cancel returns to EndGamePanel.
12. Confirm exits the application.
13. Leaving the menu open beyond the client timeout interval does not disconnect or advance paused simulation.

### Load availability

The ESC-panel **Load Game** entry is enabled when the writable save directory contains at least one safe `.sav` basename. This top-level availability check deliberately does not depend on rendering the authored save list or parsing a save header. The Load panel performs the stricter readable-save filtering before enabling its inner **Load** action, so a UI-layout or save-version mismatch cannot incorrectly disable the top-level menu entry.


### Save/Load chooser layout

Named Save/Load uses the shared Esc-menu backdrop and the geometry, controls, and hierarchy from Blizzard's FDF files. An authored
`MapListBox` fills `FileListFrame`; its native state child retains client-local row and scroll state. Only complete authored-height
rows are drawn inside the chooser viewport. Save starts with an editable `YYYY-MM-DD_HH-MM-SS` name.
