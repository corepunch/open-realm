# Old Cutscene Flow (openwarcraft3-old)

This document describes how the legacy codebase at `data/openwarcraft3-old/src` determined cutscene mode and drew cutscene UI, and how that differs from the regular gameplay HUD.

## Architecture Overview

The cutscene system uses a **layer-based layout architecture**:

1. The server writes UI frame trees to numbered **layout layers** via `svc_layout` messages
2. The client stores each layer as a raw blob and deserializes it every frame
3. A `uiflags` bitmask in `playerState_t` controls which layers are visible (bit set = hidden)
4. The cinematic layer (`LAYER_CINEMATIC`) is a separate layer from gameplay layers (console, command bar, portrait, etc.)

## Layer Enumeration

`game/g_local.h:56-65`:

```c
enum {
    LAYER_PORTRAIT,     // 0 - unit portrait
    LAYER_CINEMATIC,    // 1 - cinematic letterbox/speaker/dialogue
    LAYER_CONSOLE,      // 2 - bottom UI console (commands, buttons)
    LAYER_COMMANDBAR,   // 3 - unit command buttons
    LAYER_INFOPANEL,    // 4 - selected unit info panel
    LAYER_INVENTORY,    // 5 - unit inventory
    LAYER_MESSAGE,      // 6 - floating text messages
    LAYER_QUESTDIALOG,  // 7 - quest progress dialog
};
```

Each layer is independently addressable. The server writes different UI frame trees to different layers; the client composites them in layer order (lower = drawn first).

## Player State Fields

`common/shared.h:257-269`:

```c
struct playerState_s {
    ...
    DWORD uiflags;       // bitmask: bit N set = layer N is HIDDEN
    FLOAT cinefade;      // 0.0-1.0, full-screen black overlay alpha
    USHORT stats[MAX_STATS];
    LPCSTR texts[MAX_STATS];
};
```

- `uiflags`: controls layer visibility. Bit N corresponds to `LAYER_*` value N. Set = hidden.
- `cinefade`: black overlay for cinematic fade transitions, computed server-side by `G_Cinefade()`.
- `texts[]`: used for cinematic speaker (`PLAYERTEXT_SPEAKER = 0`) and dialogue (`PLAYERTEXT_DIALOGUE = 1`). The frames `CinematicSpeakerText` and `CinematicDialogueText` reference these via `Stat = MAX_STATS + PLAYERTEXT_*`.

## How Cutscene Mode Is Triggered

### JASS Entry Point

The Warcraft III map script calls `ShowInterface(false, fadeDuration)`. This maps to:

`game/api/api_misc.h:890-898`:
```c
DWORD ShowInterface(LPJASS j) {
    BOOL flag = jass_checkboolean(j, 1);      // false = cinematic mode
    FLOAT fadeDuration = jass_checknumber(j, 2);
    LPPLAYER player = currentplayer;
    if (player) {
        UI_ShowInterface(PLAYER_ENT(player), flag, fadeDuration);
    }
    return 0;
}
```

### Server-Side State Change

`game/hud/ui_console.c:454-459`:
```c
void UI_ShowInterface(LPEDICT ent, BOOL flag, FLOAT fadeDuration) {
    if (flag) {
        // RETURN TO GAME: hide only cinematic layer
        ent->client->ps.uiflags = 1 << LAYER_CINEMATIC;
    } else {
        // ENTER CINEMATIC: hide everything EXCEPT cinematic layer
        ent->client->ps.uiflags = ~(1 << LAYER_CINEMATIC);
    }
}
```

| Mode | `uiflags` value | Layers visible |
|------|-----------------|----------------|
| Gameplay (`flag=true`) | `1 << LAYER_CINEMATIC` (= 2) | portrait, console, commandbar, infopanel, inventory, message, questdialog — cinematic hidden |
| Cinematic (`flag=false`) | `~(1 << LAYER_CINEMATIC)` (all bits except bit 1) | only cinematic — everything else hidden |

### Cinematic Scene Content

Additional JASS natives control the scene:

- `SetCinematicScene(speaker, text)` — sets `playerState.texts[PLAYERTEXT_SPEAKER]` and `texts[PLAYERTEXT_DIALOGUE]`, then calls `UI_WriteCinematicLayer()` to re-send the layout
- `EndCinematicScene()` — clears texts and portrait
- `SetCineFilterTexture`, `SetCineFilterStartColor`, `SetCineFilterDuration`, etc. (10 functions in `game/api/api_cinefilter.h`) — configure `level.cinefilter` for fade transitions

`G_Cinefade()` in `game/g_main.c:75-83` computes the current fade alpha by lerping between start/end color alpha over the filter duration, and `G_RunClients()` at line 106 writes it to each client's `playerState.cinefade` every frame.

## How the Cinematic Panel FDF Works

### FDF Loading

At game init, `UI_Init()` in `game/ui/ui_init.c:76-103` loads FDF files from the MPQ:

```c
void UI_Init(void) {
    UI_ParseFDF("UI\\FrameDef\\GlobalStrings.fdf");
    UI_ParseFDF("UI\\FrameDef\\UI\\CinematicPanel.fdf");   // ← cinematic panel
    UI_ParseFDF("UI\\FrameDef\\UI\\ConsoleUI.fdf");
    UI_ParseFDF("UI\\FrameDef\\UI\\ResourceBar.fdf");
    UI_ParseFDF("UI\\FrameDef\\UI\\SimpleInfoPanel.fdf");
    UI_ParseFDF("UI\\FrameDef\\UI\\UpperButtonBar.fdf");
    UI_ParseFDF("UI\\FrameDef\\UI\\QuestDialog.fdf");
    ...
    Init_CinematicPanel();
}
```

The FDF file `UI\FrameDef\UI\CinematicPanel.fdf` is loaded from the MPQ (no local copy in the source tree). It defines the `CinematicPanel` frame template with its children (letterbox bars, speaker name text, dialogue text area, portrait frame).

### Frame Name Binding

`Init_CinematicPanel()` at `game/ui/ui_init.c:69-74`:
```c
void Init_CinematicPanel(void) {
    UI_FRAME(CinematicSpeakerText);
    UI_FRAME(CinematicDialogueText);
    CinematicSpeakerText->Stat = MAX_STATS + PLAYERTEXT_SPEAKER;
    CinematicDialogueText->Stat = MAX_STATS + PLAYERTEXT_DIALOGUE;
}
```

`UI_FRAME(x)` resolves to `LPFRAMEDEF x = UI_FindFrame("x")` — it looks up the frame by name from the parsed FDF templates.

`Stat` values >= `MAX_STATS` (>= 32) cause the frame to read from `playerState.texts[Stat - MAX_STATS]` instead of `playerState.stats[]`. This is how speaker name and dialogue text flow from server-set values to client-rendered text.

### Initial Layout Send

When a client begins, `G_ClientBegin()` at `game/g_main.c:183-188`:
```c
static void G_ClientBegin(LPEDICT edict) {
    UI_FRAME(ConsoleUI);
    UI_FRAME(CinematicPanel);

    UI_WriteLayout(edict, ConsoleUI, LAYER_CONSOLE);
    UI_WriteLayout(edict, CinematicPanel, LAYER_CINEMATIC);
    ...
}
```

Both layouts are serialized and sent to the client at connection time. After that, the server re-writes specific layers when state changes (e.g., `UI_WriteCinematicLayer()` on `SetCinematicScene`).

## Client-Side Rendering

### Layout Receiving

`client/cl_parse.c:71-87` — `CL_ParseLayout()` receives `svc_layout` messages:
- Reads layer ID byte
- Frees the old layout blob for that layer
- Deserializes frame entitites until a 0/0 terminator
- Stores the raw blob in `cl.layout[layer]`

### Frame Rendering With uiflags Gating

`client/cl_scrn.c:625-654` — `SCR_DrawOverlays()`:

```c
void SCR_DrawOverlays(void) {
    active_tooltip = NULL;

    // 1. Cinematic fade overlay
    if (cl.playerstate.cinefade > 0) {
        COLOR32 color = COLOR32_BLACK;
        color.a = 255 * cl.playerstate.cinefade;
        re.DrawImage(cl.pics[0], &MAKE(RECT,0,0,1,1), &MAKE(RECT,0,0,1,1), color);
    }

    // 2. First pass: clear + tooltip for visible layers
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        if ((1 << layer) & cl.playerstate.uiflags)   // ← skip hidden
            continue;
        SCR_Clear(layout); SCR_UpdateTooltip(layout);
    }

    // 3. Second pass: draw visible layers (in layer order)
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        if ((1 << layer) & cl.playerstate.uiflags)   // ← skip hidden
            continue;
        SCR_Clear(layout); SCR_UpdateTooltip(layout); SCR_DrawOverlay(layout);
    }
}
```

The draw order:
1. Full-screen black fade if `cinefade > 0`
2. All **non-hidden** layers, in layer-index order (portrait → cinematic → console → commandbar → ...)

Since `LAYER_CINEMATIC = 1` and `LAYER_CONSOLE = 2`, the cinematic panel draws **before** (underneath) the console UI in normal gameplay. In cinematic mode, only `LAYER_CINEMATIC` is visible, so only the letterbox/speaker/dialogue draws.

### Text Resolution for Cinematic Frames

`client/cl_scrn.c:114-134` — `SCR_GetStringValue()`:
```c
LPCSTR SCR_GetStringValue(LPCUIFRAME frame) {
    ...
    if (frame->stat >= MAX_STATS) {  // ≥ 32 → texts[] reference
        return cl.playerstate.texts[frame->stat - MAX_STATS];
    } else if (frame->stat > 0) {    // 1-31 → stats[] reference
        sprintf(text, "%d", cl.playerstate.stats[frame->stat]);
    } else if (frame->text) {        // direct string
        return frame->text;
    }
    ...
}
```

`CinematicSpeakerText->Stat = 32` → reads `cl.playerstate.texts[0]` (speaker name)
`CinematicDialogueText->Stat = 33` → reads `cl.playerstate.texts[1]` (dialogue text)

### Mouse Input Gating

`client/cl_input.c:124-138` checks layout layers for clickable frames. When the console/commandbar layers are hidden by `uiflags` during cinematic, their frames are not rendered, so clicks don't hit any UI buttons.

## Render Order in SCR_UpdateScreen

`client/cl_scrn.c:656-668`:
```c
void SCR_UpdateScreen(void) {
    re.BeginFrame();
    V_RenderView();       // 1. 3D scene (units, terrain, effects)
    SCR_DrawOverlays();   // 2. UI overlays (layouts, cinematic)
    CON_DrawConsole();    // 3. Developer console (on top of everything)
    re.EndFrame();
}
```

## Key Differences From Current (openwarcraft3) Cutscene Code

| Aspect | Old (`openwarcraft3-old`) | Current (`openwarcraft3`) |
|--------|--------------------------|---------------------------|
| State field | `playerState_t.uiflags` + `cinefade` | `playerState_t.client_ui_state` enum (`CLIENT_UI_GAME`/`LOADING`/`CINEMATIC`) + `uiflags` |
| FDF source | CinematicPanel loaded from MPQ `UI\FrameDef\UI\CinematicPanel.fdf`, frame tree sent via `svc_layout` | No FDF; cinematic UI is constructed programmatically by `UI_WriteCinematicLayer()` in `games/warcraft-3/game/hud/hud.c` |
| uiflags logic | `1 << LAYER_CINEMATIC` to hide cinematic; `~(1 << LAYER_CINEMATIC)` to show only cinematic | `UIFLAG_HIDE_GAMEPLAY` bitmask defined in `g_local.h:985` hides portrait/console/commandbar/infopanel/inventory |
| Camera interpolation | In `G_RunClients()` in `game/g_main.c` | Same pattern, in `games/warcraft-3/game/g_main.c:G_RunClients()` |
| Server state | `ss_cinematic` enum in `server/server.h` | Not used; client state driven by `client_ui_state` in playerState |
| Cinematic scene text | JASS `SetCinematicScene`/`EndCinematicScene` set `playerState.texts[]` | Same, in `games/warcraft-3/game/api/api_misc.h` |
| JASS entry point | `ShowInterface(flag, duration)` | Same, in `games/warcraft-3/game/api/api_misc.h:1099` |
| CineFilter | Full 10-function JASS API (`api_cinefilter.h`), `level.cinefilter` struct | Not present; fade handled by `cinefade` field |
