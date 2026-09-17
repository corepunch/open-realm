# UI Flow

> Historical migration notes below may describe removed HUD callbacks. The current contract is
> [exclusive main-menu and game-authored HUD ownership](../../../architecture/ui-system.md).
> `UpdateUnitUI`, `UpdatePlayerState`, and menu image resolution have been removed; do not restore them.


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
      -> $XDG_DATA_HOME/warcraft-3/config.cfg     (writable user config; ~/.local/share fallback)
      -> $XDG_DATA_HOME/warcraft-3/autoexec.cfg   (optional local overrides; same fallback)
      -> command-line cvars
  -> CL_Init
      -> R_GetAPI
      -> re.Init
      -> M_GetAPI
      -> menu.Init
      -> CL_MenuCommand -> Cbuf_AddText(ui_start_command)
```

Important cvars:

| cvar | Purpose |
|------|---------|
| `ui_start_command` | Initial command, usually `menu_main` |
| `com_frame_limit` | Exit after N frames |

## Loading Flow

Loading follows the Quake-style client state split:

1. `CL_BeginLoadingMap()` resets `cl.loading_progress`, sets `playerState_t.client_ui_state = CLIENT_UI_LOADING`, keeps the connection in `ca_connected`, and raises the frozen loading plaque.
2. The server sends the map-authored loading `svc_layout` after configstrings; the client draws it before standalone-screen dispatch. `FT_LOADING_BAR` reads client-local progress.
3. A successful local `SV_Map()` completion advances the local plaque to its first milestone; remote clients skip that listen-server-only milestone. `CL_PrepRefresh()` then loads/registers the world, models, images, sounds, and fonts and advances progress at those real phase boundaries; `SCR_UpdateLoadingPlaque()` explicitly repaints the frozen plaque after each increase.
4. Once registration is complete, `CL_PrepRefresh()` queues `begin`, closes sound registration, sets `cl.refresh_prepped`, and advances the bar to 1.0.
5. The first usable server frame then promotes `cls.state` to `ca_active`, changes `client_ui_state` to `CLIENT_UI_GAME`, and calls `SCR_EndLoadingPlaque()`.

That separation matters because `ca_active` means "gameplay can render now", not merely "we received some server state." Progress is client-local presentation state and must not be added to `playerState_t` or the snapshot protocol.

## Main Menu Glue Edition Selection

The Warcraft III main-menu scene is skin-driven rather than selected by hard-coded menu paths:

```text
fs_expansion
  -> archive visibility (`War3x*` hidden for RoC, exposed for TFT)
  -> UI\war3skins.txt
  -> Theme_String("GlueSpriteLayerBackground", "Default")
  -> GlueSpriteLayerBackground_V0 (RoC) or _V1 (TFT) when no unversioned field exists
  -> UI_PreloadGlueSceneModels
  -> renderer model camera + glue sprite layers
```

`games/warcraft-3/menu/menu_theme.c` treats `fs_expansion=0` as skin version 0 and any non-zero value as version 1. An explicit
unversioned skin field remains authoritative; otherwise only the matching `_V0`/`_V1` field is considered. The same decorated
lookup is used by `GlueSpriteLayerTopLeft`, `GlueSpriteLayerTopRight`, `MainMenuLogo`, `CampaignFile`, and other FDF/model/texture
keys, so one edition selector keeps the glue presentation and campaign data consistent.

Installed expansion archives are mounted during `FS_AddDataDirectory` even when the process starts in RoC mode.
`games/warcraft-3/share/config.cfg` sets the generic `fs_expansion_archive_prefix` to `War3x`;
`common/common.c:FS_ArchiveFileVisible` hides that configured archive family while `fs_expansion=0`, while retaining the existing
RoC `War3Local.mpq` AI-script filter. TFT mode exposes those already-mounted archives. This avoids closing/remounting MPQs under
renderer/UI resources and makes the edition cvar safe to change at the disconnected main menu.

The native `EditionButton` is optional and is discovered under the parsed `MainMenuFrame`; OpenRealm does not recreate its layout.
Its click command is `menu_edition`. The main-menu controller suppresses repeated clicks, hides the current frame tree, plays the
authored `MainMenu Death` glue sequence to completion, then switches the edition cvar, validates expansion campaign data when
entering TFT, and queues the generic `menu_restart` console command. `Cmd_ExecuteText` appends that command after the current
frame's command execution point, so it runs on the next client frame after the current UI event/draw call stack has returned. The
client then:

```text
menu EditionButton
  -> MainMenu Death
  -> set fs_expansion + validate TFT data
  -> queue menu_restart
  -> next client frame: menu.Shutdown()
       -> release glue models
       -> release menu-owned model/texture references
       -> clear parsed FDF/theme state
  -> renderer RegisterMap(NULL) registration boundary
  -> menu.Init()
       -> reload war3skins/FDF from the selected archive view
  -> queue menu_main
       -> install/rebuild MainMenu and campaign-facing state
```

If TFT data is unavailable, the WC3 menu restores RoC (`fs_expansion=0`) before queuing the same restart, so the rebuilt menu remains
in the available edition. `M_Init()` intentionally does not install a disconnected screen by itself, so `CL_RebuildMenu()` must always
queue the requested destination after initialization; this includes `menu_main`. Omitting that command leaves the rebuilt UI with no
active frame tree and produces a black screen after the RoC/TFT EditionButton transition. The generic engine `menu_restart` command
only runs while `cls.state == ca_disconnected`; gameplay/map state is never rebuilt inline. `+tft` and `+roc` are consumed during early
command-line processing, before `CL_Init()`/`M_Init()`, so their first rendered menu already uses the selected skin/archive view rather
than changing edition as a late console command.

Returning from a campaign world uses the same rebuild primitive at the session boundary. A game-side `MenuAction("menu", target)`
is consumed on the next client frame, disconnects the client without queuing a stale `menu_main`, shuts down the local server/game
module, clears the old map cvar/renderer asset scope, rebuilds the menu, and then enters `target`. This keeps menu FDF/model state
valid after map registration and makes Quit Campaign / EndGame / campaign-select returns work after an in-process edition switch.

`games/warcraft-3/menu/screens/single_player.c` resolves `CampaignFile` for both editions before falling back to the stock
`UI\CampaignStrings.txt` / `UI\CampaignStrings_exp.txt` names, so a post-switch Single Player entry reparses the campaign list from
the same edition selected by the main menu.

`games/warcraft-3/menu/menu_glue_scene.c` renders the selected background with `RDF_USE_ENTITY_CAMERA`.
`UI_GotoGluePanel(GLUEDEST, exited, changed)` accepts `{panel, tab, page}`. `tab` selects an authored left-panel pose;
`page` distinguishes content pages that share that pose (Options Gameplay, Video, and Sound). Each `GLUELAYER` retains
its own current/target destination, phase, and start time. The right layer uses only the panel family. RoC and TFT share
1000 ms named entry intervals and 666–667 ms exit intervals; the controller rounds exit duration to 667 ms.

`menu_main.c: UI_RequestScreen` owns every screen/subpanel command. Loading and initializing an inactive screen prepares
its resources and data before returning to synchronous map/lobby callers, but does not grant draw ownership. Load failure
retains the current screen and chrome. The boundary callback installs the destination and applies its requested content
selection only after the outgoing animation completes. Same-screen selections also defer their content changes to that
boundary. The final callback releases the pending request after both layers settle. Gameplay/shutdown cancel transitions
and clear ownership explicitly through `UI_ClearScreen`; they do not navigate to another menu.

Screens declare their left-side native FDF subtree names in `uiScreen_t.left`; remaining frames belong to the right side.
`UI_ScreenFrameOffset` walks that ancestry and samples the corresponding layer's phase clock. Outgoing contents stay
installed and drawable through exit; the central boundary callback installs/configures the incoming contents before entry.
`UI_LayoutBase` solves all native anchors without animation, and `UI_LayoutRect` translates the result exactly once for
drawing. This preserves nested and cross-side anchors. Explicit scene rectangles bypass glue motion. FDF hidden flags
still select the content. Main, Single Player, Options, LAN, and Game Setup share this presentation path; mouse, key,
and text input remain locked until transitions finish.

`menu_glue_motion.h` stores 51 samples per entrance and departure, at uniform 2% sequence intervals, extracted through
`mdxtool --info --dump-all` using the production MDX node decoder and track interpolator. The previous 21-point guessed
curve missed the landing bounce; reversing entry also missed the native departure anticipation. Samples are FDF Y-down
translations relative to the first departure key (open pose), with the final entry sample pinned to zero to avoid a
subpixel end jump. The endpoint is sampled at interval-end minus 1 ms, matching the renderer's exclusive sequence end.
The sampled RoC/TFT translation outputs are identical even though their left model geometry differs. Each partition moves
rigidly with its principal panel plane; decorative gears, chains and smaller secondary planes retain their own MDX motion.
Logo/profile contents without a moving left mesh deliberately share navigation travel. `motion[][][]` declares the mapping.

For example, TopLeftPanel node 12 (`Plane91`) travels from Y=0 to about -0.59787 in `Options Morph`, overshooting to
-0.616743 at 700 ms. Node 13 (`Plane117`) supplies LAN/Create travel; TopRightPanel node 16 (`Plane22`) supplies Options
navigation. To reproduce the samples without a graphics window:

```sh
build/bin/mdxtool -mpq 'data/Warcraft III/War3.mpq' -model 'UI\Glues\SpriteLayers\TopLeftPanel.mdx' --info --dump-all --anim 'Options Morph'
build/bin/mdxtool -mpq 'data/Warcraft III/Frozen Throne/War3x.mpq' -model 'UI\Glues\SpriteLayers\TopRightPanel.mdx' --info --dump-all --anim 'Options Birth'
```

The September 9 rendering regression was independent of FDF layout. Runtime logs showed LAN `LoadBackdrop` and
`CreateBackdrop` with `Mirrored=1`, `TileBackground=0`, but renderer UV=(0,0,1,1). `b6349c392` had placed mirroring inside
the tiling branch of `R_DrawBackdrop`. Native overlapping rectangles intentionally contain transparent padding; changing
anchors would mask the problem. Mirroring now applies to tiled and untiled backgrounds, and repeat detection uses absolute
UV width. This fixes both LAN button surrounds and the Options popup surrounds. Temporary diagnostics also confirmed the
outgoing screen was explicitly filtered during MainMenu Death by `583f1674`; removing that gate restores moving contents.

The right layer uses the panel's Birth/Stand/Death family. The left uses its selected tab's explicit enter/stand/leave
sequences during full-screen transitions as well as tab changes. Options therefore enters with `Options Morph`, rests at
`Options Stand Alternate`, and leaves with `Options Morph Alternate`; its base Birth does not animate the alternate left
panel. SinglePlayerSkirmish has a Morph family, whereas BattlenetCustomCreate uses Birth/Stand/Death. MultiplayerSubmenu
and BattlenetAdvancedOptions lack a Stand sequence and hold their authored Morph endpoint at `@1.0000`.
See [the extracted sequence inventory](../../../../games/warcraft-3/menu/panels.txt).

Changing Options pages plays left Morph Alternate followed by Morph while the right remains at Stand. Reselecting the
same page does nothing to the model. Repeated/reversed requests retain the running sequence clock and select the latest
requested page when it completes. Switching between non-default poses leaves through the base pose first. Full-screen
navigation during a running tab sequence also lets that sequence finish before leaving. An overriding family request
while the right panel is still entering can replace its pending entry, as needed by startup command selection.
Callbacks are detached before invocation; closing replaces the pending screen-boundary callback. `M_Refresh` re-reads the
active screen after advancing the scene because that step can install a different screen. Action-only transitions run
the action after exit; edition switching uses this route.

The September 2026 bounded runtime investigation confirmed that LAN installed itself before MainMenu Death started,
Options entry used base Birth followed by a jump to Stand Alternate, and `menu_video` changed FDF visibility without any
model transition. `git blame` traced the immediate command path to the earlier menu controller and the split handoff to
`b7a03afa8`; preserving immediate installation for command callers had retained the LAN flash. Separating data preparation
from presentation ownership removes that exception. `mdxtool --info` verified the animation inventory directly in War3.mpq;
RoC and TFT rendered captures checked Options entry, tab exit/re-entry, stable Video, and Main-to-LAN exit/entry.

`make test-menu` covers deferred LAN ownership, moving outgoing/incoming content, nested and cross-side anchors,
right-side preservation, Options page reselection/retargeting, Credits/Main and Skirmish/Cancel completion, LAN mode changes,
startup overrides, full navigation during a morph, authored sequence names, and close callback replacement. Sequence checks
read the original RoC TopLeftPanel MDLX header and SEQS chunk from `build/tests/tests.mpq`. FDF fixtures are packed under
the native Blizzard paths; Options includes native Gameplay/Video button subtrees to exercise stationary right-side controls.
No standalone menu test requires installed Warcraft archives. `make test-renderer-model` covers mirrored/ordinary backgrounds
with and without tiling, including negative-width repeat detection. Bounded RoC/TFT captures verify all three Options pages,
LAN, and moving entry/exit frames; WoW/SC2 builds and smoke runs exercise the shared renderer.

Build with `WC3_DEBUG_GLUE=1` to log each glue phase boundary and any missing MDX sequence that falls back to sequence zero.
The diagnostics are transition-scoped rather than frame-scoped. Rebuild when toggling the flag, then reproduce Options directly:

```sh
make -B openwarcraft3 WC3_DEBUG_GLUE=1
build/bin/openwarcraft3 -data 'data/Warcraft III' +menu_options +com_frame_limit 100
build/bin/openwarcraft3 -data 'data/Warcraft III' -tft +menu_options +com_frame_limit 100
```

Campaign background models render their stable `Stand` sequence. Their `Birth` durations vary by race and edition, so they are not
part of the fixed panel-transition clock. Entering campaign selection waits for `SinglePlayer Death`; returning declares
`SinglePlayer` as the desired panel and the closed-panel state starts its `Birth`. The retail/Warsmash
`SlidingDoors Birth -> background swap -> SlidingDoors Death` campaign wipe is still separate work; OpenRealm currently keeps
`SlidingDoors` hidden because its campaign-view ownership does not yet implement that intermediate transition state. Remaining glue
parity gaps also include `MenuZFog` and edition-sensitive `GlueScreenLoop` ambience.

The renderer texture cache also deliberately retains released textures, so same-path archive overrides are not globally invalidated by
this menu restart; edition-decorated paths reload correctly, while broader texture-cache ownership remains separate lifetime work.

## Menu Navigation Flow

1. SDL input is translated by the client input layer.
2. `M_MouseEvent` or `M_KeyEvent` updates UI state.
3. The current `uiScreen_t` receives the event.
4. Button frames inspect mouse containment and event state in `games/warcraft-3/menu/menu_render.c`.
5. If a clicked frame has `OnClick`, `UI_QueueCommand` appends its text and a newline to the console buffer through `mi.Cmd_ExecuteText`.
6. On the next client command-buffer execution, the registered console callback declares a destination screen or action; the menu transition manager performs the handoff.

Example menu command:

```text
menu_game
```

The screen switch is local to the client. No network traffic is required for menu transitions.

### Console command ownership

There is no separate menu command dispatcher. `M_Init` registers every entry in `menu_commands` with `Cmd_AddCommand`.
Buttons, checkboxes, map lists, popup selections, and campaign map launches only enqueue console text. The client drains that
buffer after the UI event stack returns, so screen initialization cannot invalidate frames still being used by a click handler.
Command-line `+menu_*`, console input, and FDF `OnClick` actions therefore share the same registered callbacks.

`menuImport_t` exposes the ordinary `Cmd_Argc`, `Cmd_Argv`, and `Cmd_ArgsFrom` readers. Numeric callbacks validate complete
unsigned DWORD tokens and argument counts; map selection accepts one quoted path; chat joins the message arguments while
preserving spaces inside quoted tokens and the optional numeric ownership prefix. Two queued UI commands each receive a newline.
The engine owns unknown-command handling and `map` loading; `CL_BeginLoadingMap` already queues `menu_ingame` at that boundary.

A bounded September 2026 run confirmed that `menu_video_mode` and `menu_single_player_difficulty` previously reached the engine
as unknown commands because only the secondary click dispatcher knew them. The same run showed that text entry changed the
focused edit box during an active screen transition. `M_TextInput` now gates SDL text events on menu activity, screen ownership,
and the transition lock before calling `UI_EditTextInput`.

Named campaign shortcuts use the actual CampaignStrings keys, including `NightElf` (the former `night-elf` argument resolved to
NULL). Campaign/mission index commands operate on the current campaign lists; lobby commands operate on the selected map and
host-owned slots. Those state prerequisites remain the screen controllers' responsibility. The legacy `menu_playerconfig`
placeholder reports that profile configuration is unimplemented; this command had no implementation in the original console table.

Glue model loading reports each failed path and caches one attempt per scene lifetime, avoiding per-frame retries/log spam.
Reset/restart creates a new scene lifetime. The write-only `show_realm_select` flag has been removed; parsed frame visibility
remains the presentation state.

`make test-menu` links the actual engine command buffer, tokenizer, registration, and cvar implementation. It exercises deferred
clicks, adjacent queued commands, malformed numeric arguments, screen commands, all named campaign shortcuts, campaign/mission
selection, LAN join/create, lobby slot changes, quoted chat/map arguments, and game-start handoff. Native GameChatroom/PlayerSlot
fixtures and their StandardTemplates dependencies are included in `tests.mpq`; the Tutorial fixture is extracted from the RoC
CampaignStrings section. `make test-commands` covers the underlying engine command and map-loading contracts.

### Custom-game slot popup commands

`UI_PopupSelectItem` appends the selected row index to a menu's `OnClick` command. GameSetup therefore binds
`menu_game_setup_slot_type <slot>` and `menu_game_setup_slot_race <slot>` without a trailing format placeholder.
Leaving a literal `%u` in either binding produces three arguments instead of two. The popup changes its visible title,
but the console handler rejects the command and never publishes the slot change. An Open slot can consequently appear
as Computer while loading and melee initialization still exclude that opponent, causing an immediate solo victory (#422).

`menu_fdf.console_lan_and_lobby_commands_deliver_arguments` selects the rendered slot popup rows, drains their commands
through the production command buffer, and checks the published slot settings and final map-start command batch. It covers
Computer/Closed/Open transitions and race changes in ROC and TFT mode using the shared PlayerSlot/GameChatroom fixtures.
Run `make test-menu`; no interactive game launch is needed to verify this command contract.

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

Campaign and mission visibility is controlled by the game-owned `wc3_campaign_visibility` CVar. The default `all`
mode shows every parsed campaign and map-backed chapter so campaign testing is not blocked by frontend progression
state. `unlocked` mode reads `campaign-progress.orcp`: explicit JASS `SetCampaignAvailable` / `SetMissionAvailable`
state wins, with `CampaignStrings` `DefaultOpen` supplying the fresh-profile campaign fallback. Mission 0 is shown for
an available campaign unless it was explicitly locked. Merely launching a mission does not create progression state;
authored campaign triggers own unlocks. See [campaign-progress.md](../campaign-progress.md).

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

`RemovePlayer` owns authoritative per-player result state, not campaign navigation. Its C implementation delegates the
transition to `G_RemovePlayerWithResult(player_num, game_result)`, which records `PLAYER_STATE_GAME_RESULT`, moves the
runtime slot to `PLAYER_SLOT_STATE_LEFT`, stops that player's AI, and publishes the appropriate player result event.
The `sv_cheats`-gated `win` and `lose` client commands deliberately call this same helper rather than switching UI or
maps directly, so map-authored victory/defeat triggers and campaign continuation remain authoritative. Because map
result handlers may start an ending cinematic, the temporary native `GameResultDialog`
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

- the campaign `SlidingDoors Birth -> backdrop swap -> SlidingDoors Death` wipe is not wired;
- campaign ambient sound and campaign-specific cursor switching are not wired;
- profile creation/deletion/persistence is not implemented;
- map `config()` is not yet run as a distinct pre-lobby configuration phase; doing so correctly requires separating
  authored map configuration from later lobby overrides;
- map/server loading remains synchronous. The WC3 loading bar now represents coarse client registration phases, but
  `SV_Map()` / game-module map loading still has no owned incremental progress source and may hold the bar near its
  initial position during a long load.

Do not paper over these with fixed timers, hard-coded campaign assets, a second custom-game implementation, or fake
sub-percentages inside synchronous server work. They require the corresponding lifecycle/data ownership to exist first.

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
3. Menu refresh outside a world, or generic layout/window drawing in-game
4. console/debug overlay
5. `re.EndFrame`

`menu.Refresh` dispatches to the active glue screen only when `CL_MenuActive()` permits it. For `menu_main`, `games/warcraft-3/menu/screens/main_menu.c` draws the `MainMenu3d` portrait background, the logo sprite, and the main menu frame tree.

## Server-Authored Modal Gameplay UI

Quest, Log, Allies, Menu, and the temporary Game Result fallback use the generic `svc_window` manager. Modal windows
own their input exclusion there: while the topmost modal is visible, lower HUD/world interaction and camera scrolling
are suppressed, while the modal's own button commands continue to reach the server. The game-result window uses
`UI_WINDOW_NO_PAUSE` because stock result scripts may already own `PauseGame(true)`, and `UI_WINDOW_NO_ESCAPE` because
closing it without choosing a result action would strand the result lifecycle.

The single-client Quest dialog additionally owns a Warcraft simulation pause. The server continues packet processing and frozen-state client traffic while `SV_RunGameFrame()` is gated, so the modal can close without deadlocking and the normal 10-second client timeout does not fire. See [Pause And Modal UI](../pause-and-modal-ui.md).

## Unit Selection and Command Card Flow

Client input sends the selection/order command and updates its local selection hint. The game's authoritative
selection path validates the entity set and authors command, inventory, queue, and information frames from
`games/warcraft-3/game/hud/`. Those frames travel through `svc_layout` to the generic client renderer and input
handlers. No menu callback, separate HUD query, or menu-owned unit cache participates.

Recipient-specific skin keys are resolved in the game before registering concrete texture paths. HUD templates
and media identities are rebuilt across map/save-load transitions; see [HUD Media Lifetime](../hud-media.md).

## Key Decisions

- UI rendering is client-side for instant menu interaction.
- The server remains authoritative for game data.
- Glue FDF assets are parsed by the menu library; HUD FDF assets are parsed by the game.
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
