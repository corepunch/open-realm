# SC2 UI Layout Format

Documents the `.SC2Layout` XML format, directory structure, frame class hierarchy, and Galaxy Script scripting system used by StarCraft II's UI. Sourced from community RE of `core.sc2mod` and the `sc2layout-schema` / `sc2-xsd` repos.

See [References](references.md) for links to all source material.

## Overview

SC2's UI system is XML-driven with no runtime scripting language accessible to maps in the way WoW exposes Lua. The scripting layer is **Galaxy Script** — a compiled C-like language processed by the SC2 Editor.

| Concept | SC2 | WoW equivalent |
|---|---|---|
| Layout manifest | `DescIndex.SC2Layout` | `FrameXML.toc` |
| Layout files | `.SC2Layout` XML | `.xml` FrameXML files |
| Scripting | Galaxy Script (`.galaxy`, compiled) | Lua |
| Font styles | `.SC2Style` / `FontStyles.SC2Style` | `Fonts.xml` |
| UI bundle archive | `.SC2Interface` (MPQ) | AddOn `.zip` |
| Skin/texture catalog | `GameData/Assets.txt` | `war3skins.txt` |

## DescIndex.SC2Layout — the manifest

The engine reads a single manifest at `UI/Layout/DescIndex.SC2Layout`. Every layout file the UI needs must be reachable from here via `<Include>`:

```xml
<Desc>
    <Include path="UI/Layout/Common/StandardTemplates.SC2Layout"/>
    <Include path="UI/Layout/UI/GameUI.SC2Layout"/>
    <Include path="UI/Layout/Glue/QueryUI.SC2Layout" requiredtoload="IS_QUERY_UI_ENABLED"/>
</Desc>
```

Load order matters: constants and templates must appear before frames that reference them. This is enforced by listing `Common/` files before `UI/` and `Glue/` files.

## Layout Directory Structure (core.sc2mod)

```
UI/Layout/
  DescIndex.SC2Layout           ← manifest
  Common/                       ← templates, constants, shared elements
    StandardConstants.SC2Layout
    StandardTemplates.SC2Layout
    StandardTooltip.SC2Layout
    StandardDialog.SC2Layout
    StandardTileListTemplates.SC2Layout
    StandardNavigationTemplates.SC2Layout
  Glue/                         ← main menu / front-end screens
    GlueUI.SC2Layout
    BattleUI.SC2Layout
    ScreenHome.SC2Layout
    ScreenMulti.SC2Layout
    ScreenLobby.SC2Layout
    ...
  UI/                           ← in-game HUD
    GameUI.SC2Layout             ← root frame for in-game UI
    CommandPanel.SC2Layout
    ConsolePanel.SC2Layout
    CommandButton.SC2Layout
    ChatBar.SC2Layout
    CashPanel.SC2Layout
    ResourcePanel.SC2Layout
    PortraitPanel.SC2Layout
    MinimapPanel.SC2Layout
    InfoPanel.SC2Layout
    UnitStatusBar.SC2Layout
    ...
  ConsoleSkins/                 ← console chrome skins
    ConsoleSkin_Template.SC2Layout
    ConsoleSkin_Forged.SC2Layout      ← Terran
    ConsoleSkin_Nerazim.SC2Layout     ← Protoss
    ConsoleSkin_Cerberus.SC2Layout    ← Zerg
    ConsoleSkin_Alliance.SC2Layout    ← WC3 Alliance
    ConsoleSkin_Horde.SC2Layout       ← WC3 Horde
    ...
  Editor/                       ← SC2 Editor's own UI
  LoadingScreens/
```

## .SC2Layout XML Syntax

Root element is always `<Desc>`. Frames declared with `<Frame type="FrameClass" name="FrameName">`.

```xml
<Desc>
    <Frame type="ConsolePanel" name="ConsolePanel" template="ConsolePanel/ConsolePanelTemplate">
        <Anchor relative="$parent"/>   <!-- fill-parent shorthand: all 4 sides -->
    </Frame>

    <Frame type="Image" name="BackgroundImage">
        <Anchor side="Top"    pos="Min" relative="$parent" offset="0"/>
        <Anchor side="Bottom" pos="Max" relative="$parent" offset="0"/>
        <Anchor side="Left"   pos="Min" relative="$parent" offset="0"/>
        <Anchor side="Right"  pos="Max" relative="$parent" offset="0"/>
        <Texture val="@UI/MenuBarButtonNormal"/>
        <Color val="255,255,255"/>
    </Frame>
</Desc>
```

### Anchor shorthand

`<Anchor relative="$parent"/>` with no `side`/`pos` attributes means "fill all four sides of the parent" — expands to four anchors: Top/Min, Bottom/Max, Left/Min, Right/Max.

### Attribute types

| Type | Example |
|---|---|
| `Boolean` | `true` / `false` |
| `Float` | `1.0` |
| `Color3` | `255,128,0` or `#FF8000` |
| `Side` | `Top` / `Left` / `Right` / `Bottom` |
| `Blend` | `Add` / `Alpha` / `Normal` / `Multiply` |
| `TexCoord` | `0.0`–`1.0` |

### Constants

Referenced as `##ConstantName##` or `#ConstantName` — defined in `StandardConstants.SC2Layout`.

### Templates

Applied via `template="TemplateName/FrameName"`. A template is a named `<Frame>` in any loaded file. Template children are cloned and parented to the instantiating frame. See the two-pass resolution design in [hud-layout-pipeline.md](hud-layout-pipeline.md).

## Frame Class Hierarchy

The engine exposes a C++ class hierarchy through the layout type system. Key classes (from `SC2Mapster/sc2layout-schema`):

```
CFrame                         ← base for all UI frames
  CControl                     ← interactive base
    CButton                    ← clickable button
    CCheckBox
    CEditBox
    CSlider
    CScrollBar
    CPulldown
    CRadioButtonGroup
  CLabel                       ← text display
  CImage                       ← 2D texture display
  CProgressBar
  CListBox
  CTileList
  CScrollableFrame
  CTabControl
  CDialog
  CTooltip
  CModelFrame                  ← renders a 3D SC2 model (type="Model")
  CPortraitFrame               ← unit portrait, 2D or 3D (type="Portrait")
  CMovieFrame                  ← video playback
  CMinimapFrame                ← minimap
  CGlueUI                      ← root of main menu UI tree
  CGameUI                      ← root of in-game UI tree
  CLayer                       ← non-interactive grouping
  CBlurFrame                   ← blur effect
  CMovingFrame                 ← animated movement
  CCutsceneFrame               ← cutscene playback
  CBoard                       ← data grid
  CGraphFrame                  ← chart/graph
  CUnitStatusFrame             ← unit stat display
    CUnitStatusBar             ← health / shield / energy bar
    CUnitStatusBehavior
    CUnitStatusAbil
  CCommandPanel                ← command card container
  CLeaderPanel
  CBattleUI
  CMatchmakingSearchPanel
  ...                          ← 400+ named classes total
```

The full list is in `SC2Mapster/sc2layout-schema/sc2layout/frame_class.xml`.

### Engine mapping (openwarcraft3)

| SC2 type | `sc2FrameType` enum | Engine `uiFrameType_t` |
|---|---|---|
| `Frame` | `SC2_FRAMETYPE_FRAME` | `FT_FRAME` |
| `Image` | `SC2_FRAMETYPE_IMAGE` | `FT_TEXTURE` |
| `Button`, `CommandButton` | `SC2_FRAMETYPE_BUTTON` | `FT_FRAME` (buttons are visual containers) |
| `Portrait` | `SC2_FRAMETYPE_PORTRAIT` | `FT_PORTRAIT` |
| `Model` | `SC2_FRAMETYPE_MODEL` | `FT_PORTRAIT` (same render path) |
| `Label` | `SC2_FRAMETYPE_LABEL` | `FT_TEXT` |

## .SC2Style — Font Styles

`FontStyles.SC2Style` (and included style files) define named font styles referenced in layout XML:

```xml
<Desc>
    <Style name="SC2_StatusBarText" font="Arial" size="10" color="255,255,255"/>
</Desc>
```

Analogous to WoW's `Fonts.xml`. Referenced in `<Frame type="Label">` children via `<Style val="SC2_StatusBarText"/>`.

## .SC2Interface — UI Bundle Archive

A `.SC2Interface` file is an MPQ archive containing `.SC2Layout` files plus assets. Custom maps use this to ship UI overrides. Loaded via the same MPQ stack as `.SC2Mod` and `.SC2Map` archives, with override priority determined by load order.

## Galaxy Script

SC2's scripting language is **Galaxy Script** — a compiled C-like language, not Lua.

- File extension: `.galaxy`
- Compiled by the SC2 Editor into binary trigger data embedded in the map
- No closures, no dynamic eval, no reflection — purely procedural
- Native UI functions: `UISetFrameVisible`, `UISetFrameAnchor`, `UISetFrameSize`, `UISetFrameProperty`, `UICreateFrame`, `UIParent`, `UIGetFrameProperty`
- Trigger events respond to `UI Event` conditions

Extracted `.galaxy` files are available in `SC2Mapster/SC2GameData` under `TriggerLibs/`. The best narrative reference is the s2editor-guides.readthedocs.io tutorial series (doc 058 for Galaxy Script, docs 043/044/049 for UI events and dialogs).

Galaxy Script is distinct from **Cutlass** (the internal name for the Arcade's Lua-like scripting layer available in some newer SC2 modding contexts) — Cutlass is not Galaxy.

## Texture Key Resolution

Layout files reference textures as logical keys prefixed with `@UI/`:

```xml
<Texture val="@UI/MenuBarButtonNormal"/>
```

These keys are defined in `GameData/Assets.txt` inside each mod archive — the SC2 equivalent of WC3's `war3skins.txt`. See [Assets.txt (UI Texture Catalog)](file-formats/assets-txt.md) for the format and our resolution implementation.

## Community Resources

There is no SC2 equivalent of wowdev.wiki or HiveWorkshop's deep binary RE documentation. The closest are:

- **sc2mapster.wiki.gg** — binary map format specs (terrain, MapInfo, Objects)
- **SC2Mapster/sc2layout-schema** — authoritative XML schema for layout files
- **sc2-arcade-watcher/sc2-file-format-docs** — IDA-derived binary format specs (MPQ/CASC/terrain/replay; UI frame C++ struct layouts not yet published)

The community hub migrated from sc2mapster.com to curseforge.com/sc2 and a Discord server. Active tooling development is in the sc2-arcade-watcher GitHub org (maintained by Talv).
