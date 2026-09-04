# UI Flow

This document traces the current client-side UI path: input, menu commands, frame rendering, and unit-data queries.

## Overview

```text
Client input
  -> UI mouse/key event
  -> active uiScreen_t
  -> frame tree update
  -> UI_DrawFrame
  -> renderer API
  -> OpenGL renderer
```

All FDF parsing, layout solving, screen transitions, and frame rendering happen client-side. The server sends data for game-dependent UI, but it does not author UI frame trees.

## Startup Flow

```text
common/main.c
  -> Com_Init
      -> share/warcraft-3/config.cfg   (shipped game defaults)
      -> ~/.local/share/warcraft-3/config.cfg      (writable user config)
      -> ~/.local/share/warcraft-3/autoexec.cfg    (optional local overrides)
      -> command-line cvars
  -> CL_Init
      -> R_GetAPI
      -> re.Init
      -> M_GetAPI
      -> menu.Init
      -> UI_MenuCommandLocal(ui_start_command)
```

Important cvars:

| cvar | Purpose |
|------|---------|
| `ui_start_command` | Initial command, usually `menu_main` |
| `com_frame_limit` | Exit after N frames |

## Loading Flow

Loading follows the Quake-style client state split:

1. `CL_BeginLoadingMap()` sets `cls.state = ca_loading` and seeds the loading text/progress.
2. `SCR_DrawScreenField()` draws the loading plaque only while `cls.state == ca_loading`.
3. `CL_PrepRefresh()` loads configstring-backed assets and registers the map.
4. Only after all required assets are ready does `CL_PrepRefresh()` send `begin` and promote the client to `ca_active`.
5. Snapshot parsing may update `playerstate`, but it must not flip `cls.state` by itself.

That separation matters because `ca_active` means "gameplay can render now", not "we already received a player snapshot." If the loading plaque disappears before the world is ready, the client should keep `ca_loading` until the precache gate completes.

## Main Menu Glue Edition Selection

The Warcraft III main-menu scene is skin-driven rather than selected by hard-coded menu paths:

```text
fs_expansion
  -> UI\war3skins.txt
  -> Theme_String("GlueSpriteLayerBackground", "Default")
  -> GlueSpriteLayerBackground_V0 (RoC) or _V1 (TFT) when no unversioned field exists
  -> UI_PreloadGlueSceneModels
  -> renderer model camera + glue sprite layers
```

`games/warcraft-3/menu/menu_theme.c` treats `fs_expansion=0` as skin version 0 and any non-zero value as version 1. An explicit
unversioned skin field remains authoritative; otherwise only the matching `_V0`/`_V1` field is considered. The same decorated
lookup is used by `GlueSpriteLayerTopLeft`, `GlueSpriteLayerTopRight`, `MainMenuLogo`, and other FDF/model/texture keys, so one
edition selector keeps the glue presentation consistent.

`games/warcraft-3/menu/menu_glue_scene.c` renders the selected background as a model with `RDF_USE_ENTITY_CAMERA`; the main menu is
therefore not a static BLP backdrop. Current lifecycle gaps remain deliberate: OpenRealm starts glue layers directly in their
`Stand` sequences, does not parse `MenuZFog`, and has no safe in-menu RoC/TFT Edition-button restart flow. Because RoC mode omits
TFT MPQs at the filesystem layer, an Edition button must not be implemented as a UI-only cvar toggle without a corresponding data
source/UI restart lifecycle.

## Menu Navigation Flow

1. SDL input is translated by the client input layer.
2. `UI_MouseEventLocal` or `UI_KeyEventLocal` updates UI state.
3. The current `uiScreen_t` receives the event.
4. Button frames inspect mouse containment and event state in `games/warcraft-3/menu/menu_render.c`.
5. If a clicked frame has `OnClick`, `UI_MenuCommandLocal` executes the command.
6. Menu commands call direct screen/action handlers.

Example menu command:

```text
menu_game
```

The screen switch is local to the client. No network traffic is required for menu transitions.

## Single Player Flow

The WC3 single-player frontend uses the native Blizzard glue FDFs and keeps campaign/map metadata separate from the
actual `map` command:

```text
MainMenu.fdf
  -> SinglePlayerMenu.fdf
      -> CampaignMenu.fdf
          -> campaign selection
          -> MissionSelectFrame
          -> selected mission
          -> map "..."

      -> SkirmishButton
          -> existing local map browser
          -> GameSetup
          -> lobby_config / lobby_slot
          -> map "..."
```

`games/warcraft-3/menu/screens/single_player.c` parses the skin-selected `CampaignFile` (with the classic campaign
string files as fallback). Campaign selection changes the campaign backdrop and builds a mission list dynamically
from every parsed `MissionN` / `FileN` entry; selecting a campaign alone does not start its first map. This mirrors
Warsmash's `CampaignMenuUI` approach and does not depend on non-portable `Mission0Frame`...`Mission13Frame` children
being present in the retail `MissionSelectFrame`. The campaign Back button returns Mission Select to Campaign Select
first, then returns to the Single Player menu.

Mission visibility is controlled by `wc3_campaign_mission_visibility`. The default `all` mode shows every parsed
map-backed chapter so campaign testing is not blocked by frontend progression state. `played` mode shows only
missions whose `wc3_campaign_played_<campaign>_<mission>` cvar is non-zero; launching a mission marks that cvar for
the current process. This is intentionally a frontend/testing bridge until profile-backed retail campaign mission
availability (`SetMissionAvailable`/profile persistence) is implemented.

The generated selector reuses Warcraft's `MapListBox` template. That template is a `CONTROL` root in the retail FDF,
so bound map-list controls must participate in the same hit testing and mouse-event dispatch as programmatic `FRAME`
map lists. A `CONTROL` map list that is renderable but excluded from interaction produces visible campaign rows that
cannot be clicked.

`CampaignMenu.fdf` can also expose legacy/static frames such as `HumanButton`, `OrcButton`,
`UndeadButton`, `NightElfButton`, and `TutorialButton`. Their presence is not a signal that the authored FDF already
contains the active campaign selector: Warsmash builds the visible campaign entries dynamically from
`CampaignStrings`. OpenRealm likewise always creates its data-driven campaign list. Suppressing that list merely
because an optional static button frame bound successfully can leave the campaign screen showing only the
difficulty control, with no campaign entry to click.

The menu's Easy/Normal/Hard selection writes `wc3_campaign_difficulty` (`0`, `1`, or `2`). For stock ROC/TFT campaign
map paths, `G_SpawnEntities` uses that value as the initial `GetGameDifficulty()` state before `war3map.j`
initialization; non-campaign maps retain the existing Normal default. Map script calls to `SetGameDifficulty` may
still change it later. The Single Player profile caption uses the engine `name` cvar, which is also the local human
name used by Game Setup. A full Warcraft profile database is not implemented.

Single Player Custom Game deliberately reuses the existing local map-selection and Game Setup controllers instead
of duplicating their W3I parsing, map info, slot, race, team, color, and game-speed logic. The source mode only
changes glue presentation and Cancel destinations; the final local-server launch path remains shared.

### Victory / defeat handoff

`RemovePlayer` owns authoritative per-player result state, not campaign navigation. It records
`PLAYER_STATE_GAME_RESULT`, moves the runtime slot to `PLAYER_SLOT_STATE_LEFT`, and publishes the appropriate player
result event. Because map result handlers may start an ending cinematic, the temporary native `GameResultDialog`
fallback is queued rather than written inline. `UI_FlushPendingGameResults()` normally writes it only after queued
JASS result work and outside `CLIENT_UI_CINEMATIC`.

There is one terminal exception. Stock Blizzard.j `CustomVictoryBJ` / `CustomDefeatBJ` calls `RemovePlayer` and then
its single-player result dialog path calls `PauseGame(true)`. When `RemovePlayer` ran from JASS work scheduled by the
frame's first event pass, the new VICTORY/DEFEAT event was otherwise stranded: the server pause prevented the next
simulation frame that would consume it. `G_DrainPausedResultEvents()` therefore drains only result events actively
blocking pending result handoffs before the paused scheduler takes over. Once that event has run, a script-paused
result fallback may overlay `CLIENT_UI_CINEMATIC`, because waiting for a later cinematic-to-game transition would
again require a simulation frame that the script pause has intentionally frozen. `UI_ShowGameResult()` sends a
dedicated modal `svc_window` instead of un-hiding a persistent layout layer. Transient windows render above the
persistent HUD, so the rest of the ending-cinematic suppression stays untouched. The result window reuses the
Esc-menu backdrop art at the authored dialog bounds; on victory, Quit is re-anchored directly below Continue with a
small gap so both actions stay inside that window. The result is not Escape-dismissable because its explicit buttons
own the Blizzard.j/session continuation.

The fallback exists because the stock Blizzard.j result path still cannot build its normal ScriptDialogs: generic
`DialogCreate` / `DialogAddButton` / `DialogDisplay` and dialog-button event support remain incomplete. It therefore
does not try to recreate every campaign/melee policy. It uses Warcraft `GAMEOVER_*` global strings where available,
uses Restart/Load/Quit Mission for the supported single-player defeat subset, uses Continue/Continue Game for
victory, and routes those executable actions through game/session boundaries. Observer continuation remains deferred
until observer-on-death simulation policy exists.

End-of-session operations cross `gi.MenuAction` instead of making the WC3 HUD own client teardown:

```text
JASS / result UI
  -> G_RequestEndGame / G_RequestChangeLevel / G_RequestRestartGame
      -> gi.MenuAction
          -> client MenuAction (copy/defer request)
              -> next CL_Frame after SV_Frame returns
                  -> frontend menu or SV_Map
```

`gi.MenuAction` is deliberately a deferred session boundary. A JASS native such as `ChangeLevel` can run while the
old map's VM is still on the C stack; calling `SV_Map` inline from that native destroys/reinitializes map-owned state
under the active VM. `MenuAction` therefore copies the resolved map/menu argument and `CL_Frame` consumes it only
after the enclosing `SV_Frame` has returned. Keep `CustomVictoryOkBJ` itself synchronous so its `PauseGame(false)`
executes, but keep the actual world replacement deferred.

`EndGame(doScoreScreen)` currently returns to the frontend and consumes but cannot yet honor `doScoreScreen`; there
is no score-screen controller. `ChangeLevel` loads its map, `RestartGame` reloads the current `map` cvar,
`DisplayLoadDialog` enters the frontend load-game screen, and `ForceCampaignSelectScreen` returns to
`menu_single_player_campaign`. On single-player victory, the fallback Continue button delegates to Blizzard.j's
`CustomVictoryOkBJ`, preserving its `bj_changeLevelMapName` decision instead of guessing the next map in HUD code.
Because `CustomVictoryDialogBJ` has already paused single-player simulation, this fallback invocation is synchronous:
queuing `CustomVictoryOkBJ` as a coroutine would leave its `PauseGame(false)` and `ChangeLevel(...)` calls stranded
behind the very pause they are supposed to release. Multiplayer victory Continue still only dismisses the fallback.

For result-lifecycle diagnosis, `wc3_game_result_debug 1` enables game-module `WC3_RESULT` breadcrumbs for
`RemovePlayer` arguments and player/client mapping, result-event publication and matching trigger dispatch, pending-result
deferral (`event_queue`, `cinematic`, or `disconnected`), paused-result event draining/cinematic override, FDF binding,
server layout emission, and result-button session actions. Pair it with the shared `ui_layout_debug 1` transport trace when client receipt/storage must also be observed;
that generic trace logs every UI layer and the server-side result breadcrumb identifies the numeric result layer to correlate.
Both diagnostics are runtime-gated and keep Warcraft-specific knowledge out of shared client code.

Single-player result pausing is also intentionally still missing. Result UI should reuse the existing WC3 pause/modal
ownership path rather than suppressing `SV_Frame` or creating a second clock-freeze mechanism. `PauseGame` and
single-client modal pausing already flow through the generic server scheduler, which freezes simulation time while
continuing network reads and client traffic.

### Remaining frontend lifecycle gaps

The following Warsmash/retail-style lifecycle work is intentionally not approximated in the current UI code:

- menu changes are still immediate `UI_SetScreen` operations rather than Birth/Stand/Death sequence-completion
  transitions;
- campaign ambient sound and campaign-specific cursor switching are not wired;
- profile creation/deletion/persistence is not implemented;
- map `config()` is not yet run as a distinct pre-lobby configuration phase; doing so correctly requires separating
  authored map configuration from later lobby overrides;
- map/server loading remains synchronous, and the WC3 loading progress sprite does not yet represent incremental
  loader work.

Do not paper over these with fixed timers, hard-coded campaign assets, a second custom-game implementation, or fake
loading percentages. They require the corresponding lifecycle/data ownership to exist first.

## Draw Flow

Each client frame calls:

```c
menu.Refresh(msec);
CL_Input();
CL_ReadPackets();
CL_SendCommand();
CL_PrepRefresh();
SCR_UpdateScreen();
```

`SCR_UpdateScreen` calls the renderer and UI:

1. `re.BeginFrame`
2. `re.RenderFrame`
3. `ui.DrawFrame`
4. console/debug overlay
5. `re.EndFrame`

`ui.DrawFrame` dispatches to the active screen. For `menu_main`, `games/warcraft-3/menu/screens/main_menu.c` draws the `MainMenu3d` portrait background, the logo sprite, and the main menu frame tree.

## Server-Authored Modal Gameplay UI

Quest, Log, Allies, Menu, and the temporary Game Result fallback use the generic `svc_window` manager. Modal windows
own their input exclusion there: while the topmost modal is visible, lower HUD/world interaction and camera scrolling
are suppressed, while the modal's own button commands continue to reach the server. The game-result window uses
`UI_WINDOW_NO_PAUSE` because stock result scripts may already own `PauseGame(true)`, and `UI_WINDOW_NO_ESCAPE` because
closing it without choosing a result action would strand the result lifecycle.

The single-client Quest dialog additionally owns a Warcraft simulation pause. The server continues packet processing and frozen-state client traffic while `SV_RunGameFrame()` is gated, so the modal can close without deadlocking and the normal 10-second client timeout does not fire. See [Pause And Modal UI](../pause-and-modal-ui.md).

## Unit Selection and Command Card Flow

Unit UI data still comes from the server because it depends on game rules and selected entities.

```text
client selection
  -> CL_RequestUnitUI
  -> clc_request_unit_ui
  -> server/sv_unit_ui.c
  -> games/warcraft-3/game/g_unit_ui.c
  -> svc_unit_ui
  -> client/cl_unit_ui.c
  -> ui.UpdateUnitUI
  -> games/warcraft-3/menu/screens/console_ui.c
```

### Client Request

```c
void CL_RequestUnitUI(DWORD num_selected, DWORD *entity_nums) {
    MSG_WriteByte(&cls.netchan.message, clc_request_unit_ui);
    MSG_WriteByte(&cls.netchan.message, (BYTE)num_selected);
    for (DWORD i = 0; i < num_selected; i++) {
        MSG_WriteShort(&cls.netchan.message, (SHORT)entity_nums[i]);
    }
}
```

### Server Query

```c
gameCommandButton_t buttons[12];
BYTE num_buttons = ge->GetCommandButtons(ent, buttons, 12);
```

The server serializes command buttons, inventory, and build queue data into `svc_unit_ui`.

### Client Cache

```c
void ConsoleUI_UpdateUnitUI(DWORD num_units, menuUnitData_t *units) {
    cached_unit_count = num_units;
    memcpy(cached_units, units, sizeof(menuUnitData_t) * num_units);
}
```

The HUD screen renders from this cache on later frames.

In-game HUD chrome is server-authored `svc_layout` from `game/hud/`. All HUD bindings live in one `hud` object. `G_LoadMap` memsets it and `UI_LoadHud()` binds every panel because `SV_Map` wipes `CS_IMAGES` / `CS_FONTS`. See [HUD Media Lifetime](../hud-media.md).

## Key Decisions

- UI rendering is client-side for instant menu interaction.
- The server remains authoritative for game data.
- FDF assets are parsed by the UI library, not by the game DLL.
- Runtime modules communicate through Quake-style function tables.
- Campaign selection and mission selection are separate states; only a mission issues `map`.
- Campaign difficulty is startup game state, not merely a menu label.
- Single-player Custom Game reuses the local map browser/Game Setup data path instead of maintaining a second copy.

## See Also

- [UI System Architecture](./ui.md)
- [Pause And Modal UI](../pause-and-modal-ui.md)
- [UI Quick Reference](ui-quick-reference.md)
- [Runtime Modules and Cvars](../../../architecture/runtime.md)
- [FDF File Format](../file-formats/fdf.md)
