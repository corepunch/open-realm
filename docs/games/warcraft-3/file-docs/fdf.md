# Warcraft III FDF (Frame Definition File) Reference

> Source: https://www.hiveworkshop.com/pastebin/e23909d8468ff4942ccea268fbbcafd1.20598  
> "The Big UI-Frame Tutorial" by Tasyen

---

## Coordinate System

The FDF coordinate system is **fixed** regardless of window resolution:

- **Origin (0.0 / 0.0)**: Bottom-left corner of the 4:3 safe-area (roughly at the bottom-left of the minimap)
- **Top-right (0.8 / 0.6)**: Top-right corner of the 4:3 safe-area (roughly the Upkeep Level indicator)
- **Y-axis**: goes **upward** (0 at bottom, 0.6 at top)
- **X-axis**: goes **rightward** (0 at left, 0.8 at right)
- Pixel-based coords require conversion: e.g. `BlzGetLocalClientWidth() / BlzGetLocalClientHeight() * 0.6` for fullscreen width

### 4:3 Safe-Area vs. Widescreen

Most Frame-group frames **cannot leave the 4:3 area** (0,0)–(0.8,0.6). If they extend past it, they become malformed (text cuts off, backdrops shrink, etc.).

**SimpleFrames** are free of this limitation.

To allow a normal Frame to leave 4:3, reparent it to one of:
- `"ConsoleUIBackdrop"` (V1.32+, below SimpleFrames layer)
- `BlzGetFrameByName("Leaderboard", 0)` (above SimpleFrames, must be created first)
- `BlzGetFrameByName("Multiboard", 0)`

### Fullscreen Frame (Lua)

To create a frame that always fills the full screen:

```lua
BlzFrameSetSize(frame, BlzGetLocalClientWidth()/BlzGetLocalClientHeight()*0.6, 0.6)
BlzFrameSetAbsPoint(frame, FRAMEPOINT_BOTTOM, 0.4, 0.0)
```

---

## Frame Types

Two groups: **SimpleFrames** (type name starts with `SIMPLE`) and **Frames**.

```
BACKDROP        BUTTON          CHATDISPLAY     CHECKBOX
CONTROL         DIALOG          EDITBOX         FRAME
GLUEBUTTON      GLUECHECKBOX    GLUEEDITBOX     GLUEPOPUPMENU
GLUETEXTBUTTON  HIGHLIGHT       LISTBOX         MENU
MODEL           POPUPMENU       SCROLLBAR       SIMPLEBUTTON
SIMPLECHECKBOX  SIMPLEFRAME     SIMPLESTATUSBAR SLIDER
SLASHCHATBOX    SPRITE          STATUSBAR       TEXT
TEXTAREA        TEXTBUTTON      TIMERTEXT
```

GLUE variants produce an audio click when activated.

---

## FDF Syntax

- Actions end with `,`
- Only `Frame`, `String`, `Texture`, `Layer`, `StringList` open `{ }` blocks
- `IncludeFile` and `StringList` live outside blocks
- In-file paths use single `\`
- Comments: `// line comment` or `/* block comment */`

```fdf
IncludeFile "UI\FrameDef\UI\EscMenuTemplates.fdf",

Frame "BUTTON" "ButtonTemplate" {
    ....
}

Frame "BUTTON" "FrameNameA" INHERITS WITHCHILDREN "ButtonTemplate" {
    Frame "BACKDROP" "FrameNameB" {
        Frame "TEXT" "FrameNameC" INHERITS "EscMenuLabelTextSmallTemplate" {
        }
    }
}

StringList {
    MYLABEL "My Label Text",
    MYVALUE "My Value Text",
}
```

Only **MainFrames** (those outside other `Frame` blocks) can be created or inherited via the API. Child frames are created automatically when their ancestor MainFrame is created.

---

## Positioning

### Native API

```jass
native BlzFrameSetPoint    takes framehandle frame, framepointtype point, framehandle relative, framepointtype relativePoint, real x, real y returns nothing
native BlzFrameSetAbsPoint takes framehandle frame, framepointtype point, real x, real y returns nothing
native BlzFrameClearAllPoints takes framehandle frame returns nothing
native BlzFrameSetAllPoints takes framehandle frame, framehandle relative returns nothing
```

### Frame Points

```
FRAMEPOINT_TOPLEFT    FRAMEPOINT_TOP       FRAMEPOINT_TOPRIGHT
FRAMEPOINT_LEFT       FRAMEPOINT_CENTER    FRAMEPOINT_RIGHT
FRAMEPOINT_BOTTOMLEFT FRAMEPOINT_BOTTOM    FRAMEPOINT_BOTTOMRIGHT
```

### FDF Position Actions

```fdf
SetPoint TOPLEFT, "FrameName", TOPLEFT, xOffset, yOffset,
SetAllPoints,
Anchor TOPLEFT, x, y,   -- SimpleFrames only; shorthand for SetPoint to parent
```

`SetPoint` is equivalent to `BlzFrameSetPoint(self, ownPoint, BlzGetFrameByName(name, 0), attachedPoint, x, y)`.

Offsets: `+x` → right, `+y` → up.

`UseActiveContext,` inside a Frame causes `SetPoint` to use the Frame's creation context instead of context 0.

### Expansion direction from a single point

| Point type | Frame expands |
|---|---|
| `LEFT` | rightward → |
| `RIGHT` | leftward ← |
| `CENTER` | all directions ↔ |
| `TOP` | downward ↓ |
| `BOTTOM` | upward ↑ |

---

## Common FDF Actions

### Size

```fdf
Width 0.2,
Height 0.1,
```

### Control Backdrops (functional children)

```fdf
ControlBackdrop "FrameName",           -- enabled state
ControlDisabledBackdrop "FrameName",   -- disabled state (optional; falls back to ControlBackdrop)
ControlPushedBackdrop "FrameName",
```

### Decoration / Inheritance

```fdf
Frame "BUTTON" "MyButton" INHERITS WITHCHILDREN "EscMenuButtonTemplate" {
    ...
}
DecorateFileNames,   -- use StringList variable names for file paths in this Frame
```

### Tooltip

```fdf
ToolTip "TooltipFrameName",
```

### Layer (SimpleFrames only)

```fdf
Layer "BACKGROUND" { ... }
Layer "ARTWORK"    { ... }
Layer "ARTWORK_OVERLAY" { ... }
```

Higher layers render above lower layers. A child SIMPLEFRAME's `BACKGROUND` layer renders above the parent's `ARTWORK` layer.

### LayerStyle

```fdf
LayerStyle "IGNORETRACKEVENTS",       -- no mouse input
LayerStyle "NOSHADING",
LayerStyle "NOSHADING|IGNORETRACKEVENTS",
LayerStyle "SETSVIEWPORT",            -- clips visuals to this frame's rect
```

### Font Actions

```fdf
FrameFont "MasterFont", 0.011, "",     -- Frame-group text font
Font      "InfoPanelTextFont", 0.009,  -- SimpleFrame String font

FontColor    1.0 0.8 0.0 1.0,
FontDisabledColor 0.5 0.5 0.5 1.0,
FontHighlightColor 1.0 1.0 1.0 1.0,
FontShadowColor  0.0 0.0 0.0 0.9,
FontShadowOffset 0.001 -0.001,

FontJustificationH JUSTIFYLEFT,    -- or JUSTIFYCENTER, JUSTIFYRIGHT
FontJustificationV JUSTIFYTOP,     -- or JUSTIFYMIDDLE, JUSTIFYBOTTOM
FontJustificationOffset 0.01 0.0,

FontFlags "IGNORECOLORCODES",
FontFlags "IGNORENEWLINES",
FontFlags "NOWRAP",
FontFlags "NONPROPORTIONAL",
FontFlags "FIXEDSIZE",
FontFlags "HIGHLIGHTONMOUSEOVER",
```

### BACKDROP Actions

```fdf
BackdropBackground   "path\to\texture",
BackdropBackgroundInsets right top bottom left,
BackdropBackgroundSize x,
BackdropEdgeFile  "path\to\border",
BackdropCornerFile "path",
BackdropCornerFlags "UL|UR|BL|BR|T|L|B|R",
BackdropCornerSize  0.012,
BackdropTileBackground,
BackdropBlendAll,
BackdropMirrored,
BackdropHalfSides,
BackdropBottomFile  "...",
BackdropTopFile     "...",
BackdropLeftFile    "...",
BackdropRightFile   "...",
```

### Texture Actions (SimpleFrames)

```fdf
File "path\to\texture",
AlphaMode "BLEND",   -- or ADD, ALPHAKEY, DISABLE, MOD, MOD2X
TexCoord left, right, top, bottom,    -- fraction of image to display
```

### SPRITE / MODEL

```fdf
Frame "SPRITE" "MySprite" {
    BackgroundArt "UI\Path\Model.mdl",
    SpriteCamera 0,
    SpriteScale x y z,
    SetPoint ...,
}
```

Code: `BlzFrameSetModel(frame, "path.mdl", cameraIndex)`, `BlzFrameSetSpriteAnimate(frame, animIndex, flags)`.

Models ignore frame size; scale with `BlzFrameSetScale`.

### STATUSBAR / SIMPLESTATUSBAR

```fdf
StatusBarSprite "path\to\model",
BarTexture "path\to\texture",
```

Code: `BlzFrameSetValue(frame, value)`, `BlzFrameSetMinMaxValue(frame, min, max)`.

### SLIDER / SCROLLBAR

```fdf
SliderLayoutHorizontal,       -- left=min, right=max (default)
SliderLayoutVertical,         -- bottom=min, top=max
SliderMinValue 0,
SliderMaxValue 100,
SliderInitialValue 50,
SliderStepSize 1,
SliderThumbButtonFrame "...",
ScrollBarDecButtonFrame "...",
ScrollBarIncButtonFrame "...",
```

### TEXTAREA

```fdf
TextAreaLineHeight 0.018,
TextAreaLineGap    0.00,
TextAreaInset      0.035,
TextAreaMaxLines   20,
TextAreaScrollBar  "ScrollBarFrameName",
```

Code: `BlzFrameAddText(frame, text)`, `BlzFrameSetText(frame, text)`.

### Button Hotkeys

```fdf
TabFocusPush,          -- on the parent Frame
ControlShortcutKey "StringListName",   -- on each child Button
TabFocusDefault,
TabFocusNext "OtherButtonName",
```

Hotkeys fire `FRAMEEVENT_CONTROL_CLICK`. After creation, hide+show the parent to activate them.

### POPUPMENU

```fdf
MenuItem "Option Text", -2,
MenuItemHeight 0.014,
MenuBorder 0.01,
MenuTextHighlightColor r g b a,
PopupButtonInset x,
PopupMenuFrame "MenuFrameName",
PopupTitleFrame "TitleFrameName",
PopupArrowFrame "ArrowFrameName",
```

Code: `BlzFrameGetValue(frame)` → selected index (0-based, or -1 if none). Broken outside 4:3 in V1.31.x.

---

## Origin Frames

```jass
BlzGetOriginFrame(ORIGIN_FRAME_GAME_UI, 0)
BlzGetOriginFrame(ORIGIN_FRAME_WORLD_FRAME, 0)
BlzGetOriginFrame(ORIGIN_FRAME_PORTRAIT, 0)
BlzGetOriginFrame(ORIGIN_FRAME_MINIMAP, 0)
BlzGetOriginFrame(ORIGIN_FRAME_COMMAND_BUTTON, index)
BlzGetOriginFrame(ORIGIN_FRAME_ITEM_BUTTON, index)
BlzGetOriginFrame(ORIGIN_FRAME_HERO_BUTTON, index)
BlzGetOriginFrame(ORIGIN_FRAME_MINIMAP_BUTTON, index)
BlzGetOriginFrame(ORIGIN_FRAME_SYSTEM_BUTTON, index)    -- UpperButtonBar*
BlzGetOriginFrame(ORIGIN_FRAME_UBERTOOLTIP, 0)
BlzGetOriginFrame(ORIGIN_FRAME_CHAT_MSG, 0)
BlzGetOriginFrame(ORIGIN_FRAME_UNIT_MSG, 0)
BlzGetOriginFrame(ORIGIN_FRAME_TOP_MSG, 0)
BlzGetOriginFrame(ORIGIN_FRAME_PORTRAIT_HP_TEXT, 0)     -- V1.32+
BlzGetOriginFrame(ORIGIN_FRAME_PORTRAIT_MANA_TEXT, 0)   -- V1.32+
```

Named equivalents (V1.32+): `"UpperButtonBarQuestsButton"`, `"UpperButtonBarMenuButton"`, `"UpperButtonBarAlliesButton"`, `"UpperButtonBarChatButton"`, `"InventoryButton_0"`, `"CommandButton_0"`, etc.

---

## Creating Frames

```jass
-- Load a TOC file (lists FDF paths, one per line)
BlzLoadTOCFile("war3mapImported\\MyFrames.toc")

-- Create a MainFrame defined in a loaded FDF
BlzCreateFrame("FrameName", parentHandle, priority, createContext)

-- Create a SimpleFrame
BlzCreateSimpleFrame("FrameName", parentHandle, createContext)

-- Create by type (no FDF required for basic types)
BlzCreateFrameByType("BACKDROP", "myName", parentHandle, "InheritedTemplate", createContext)
```

`createContext` determines the slot used by `BlzGetFrameByName(name, context)`.

---

## Fonts (V1.32+)

Located in `war3.w3mod:fonts\`. Common variables from `war3skins.txt`:
`MasterFont`, `InfoPanelTextFont`, `EscMenuTextFont`, `EscMenuLabelTextSmallTemplate`.

Access path via `SkinManagerGetLocalPath("MasterFont")` → `"Fonts\BLQ55Web.ttf"`.

---

## Theme Keys (war3skins.txt)

Warcraft UI files often reference symbolic names (skin keys) instead of direct texture/model paths.
Those keys are resolved through `UI/war3skins.txt`.

In this project, the theme table is loaded by the client-side UI library from `UI\war3skins.txt`,
and keys are resolved through `Theme_String(...)` in `games/warcraft-3/ui/ui_theme.c` and `games/warcraft-3/ui/ui_fdf.c`.

### Main Menu Glue Layer Examples

From Warcraft III `UI/war3skins.txt`:

```ini
MainMenuLogo=UI\Glues\MainMenu\WarCraftIIILogo\WarCraftIIILogo.mdl
GlueSpriteLayerTopLeft=UI\Glues\SpriteLayers\TopLeftPanel.mdl
GlueSpriteLayerTopRight=UI\Glues\SpriteLayers\TopRightPanel.mdl
```

So the name for TopRightPanel is:
- `GlueSpriteLayerTopRight`

And the name for TopLeftPanel is:
- `GlueSpriteLayerTopLeft`

These keys are used by the client-side menu screen and frame parser when building menu glue sprite layers.

### How To Inspect Quickly

```bash
build/bin/mpqtool -mpq "data/Warcraft III/War3.mpq" cat "UI/war3skins.txt" | grep -n "GlueSpriteLayerTopRight\|GlueSpriteLayerTopLeft\|MainMenuLogo"
```

### Practical Rule Of Thumb

- If FDF says `DecorateFileNames` and uses names that are not file paths, check `UI/war3skins.txt` first.
- For sprite/model mismatches in menus, verify skin key -> model path mapping before debugging renderer code.

---

## Known Bugs / Caveats

- `POPUPMENU` outside 4:3 breaks in V1.31.x.
- `BlzGetOriginFrame` returns handle-id 0 in V1.32.6 if accessed via `BlzFrameGetChild` first.
- `BACKDROP`, `TEXTAREA`, `SIMPLEMESSAGEFRAME`, `DIALOG` — creating with wrong types can crash.
- `Multiboard`/`Leaderboard` position is affected by resolution (x=0.8 maps to right screen border in V1.31.1).
- Frame Save & Load: using loaded UIframe variables in a native after map reload crashes the game.
- Moving `SimpleInfoPanelIconDamage` directly is not recommended; the game repositions them automatically.
