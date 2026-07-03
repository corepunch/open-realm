# GameData/Assets.txt — UI Texture Skin Catalog

`GameData/Assets.txt` is the StarCraft II equivalent of Warcraft III's `war3skins.txt`. It is present in every mod and campaign archive (`.SC2Data` containers) and maps logical UI texture keys to physical archive paths.

## Format

Plain text, one entry per line:

```
UI/LogicalName=Assets\Textures\filename.dds
```

- Keys use forward-slash (`/`) as a namespace separator.
- Values use backslash (`\`) as the archive path separator (Windows convention).
- Blank lines and lines without `=` are ignored.
- Comments are not present in practice.
- Many keys have race-variant suffixes (`_Terr`, `_Prot`, `_Zerg`) for entries that differ per race.

## Where it lives

| Archive | Notes |
|---------|-------|
| `Mods/Core.SC2Mod/Base.SC2Data` | Base entries; most HUD and button textures |
| `Mods/Liberty.SC2Mod/Base.SC2Data` | Liberty-specific entries; minimap buttons, autocast, borders |
| `Campaigns/Liberty.SC2Campaign/Base.SC2Data` | Campaign-specific overrides |

The VFS (see `common/common.c :: FS_OpenFile`) searches archives in reverse load order, so the highest-priority archive's copy wins. Because `Liberty.SC2Mod` is loaded after `Core.SC2Mod`, a call to `gi.ReadFile("GameData/Assets.txt")` returns the Liberty version — which contains fewer HUD entries than Core's.

## Key categories

### Resource panel

```
UI/ResourceIcon0=Assets\Textures\icon-mineral.dds
UI/ResourceIcon1=Assets\Textures\icon-gas.dds
UI/ResourceIcon2=Assets\Textures\icon-highyieldmineral.dds
UI/ResourceIconSupply=Assets\Textures\icon-supply.dds
UI/ResourceIconPlayer=Assets\Textures\ui_ingame_resourcesharing_playericon.dds
```

### HUD buttons (Core.SC2Mod)

```
UI/MenuBarButtonNormal=Assets\Textures\ui_gamemenu_topbuttons_normalpressed.dds
UI/MenuBarButtonHover=Assets\Textures\ui_gamemenu_topbuttons_normaloverpressedover.dds
UI/IdleButtonNormal=Assets\Textures\ui_idlepeon_normalpressed_terran.dds
UI/IdleButtonHover=Assets\Textures\ui_idlepeon_normaloverpressedover_terran.dds
UI/WarpButtonNormal=Assets\Textures\ui_warpin_normalpressed.dds
UI/WarpButtonHover=Assets\Textures\ui_warpin_normaloverpressedover.dds
UI/AllianceToggleButtonNormal=Assets\Textures\ui_alliance_button_normalpressed.dds
UI/AllianceToggleButtonHover=Assets\Textures\ui_alliance_button_normaloverpressedover.dds
UI/CharacterSheetToggleButtonNormal=Assets\Textures\ui_techlist_button_normalpressed.dds
UI/CharacterSheetToggleButtonHover=Assets\Textures\ui_techlist_button_normaloverpressedover.dds
UI/TeamResourceToggleButtonNormal=Assets\Textures\ui_resourcesharing_button_normalpressed.dds
UI/TeamResourceToggleButtonHover=Assets\Textures\ui_resourcesharing_button_normaloverpressedover.dds
```

### Minimap / command card (Liberty.SC2Mod)

```
UI/MinimapTerrain=Assets\Textures\BTN-Minimap-ToggleTerrain.dds
UI/MinimapColor=Assets\Textures\BTN-Minimap-ToggleAllies.dds
UI/MinimapPing=Assets\Textures\BTN-Minimap-Ping.dds
UI/ButtonAutocast=Assets\Textures\ProtossAutoCast8x4.dds
UI/ClearSelection=Assets\Textures\BTN_Observer_Deselect.dds
UI/ClearSelectionBackground=Assets\Textures\UI_Ingame_Observer_Deselect.dds
UI/BorderRoundedWhite=Assets\Textures\border-rounded-white.dds
```

### Portraits / panels (Core.SC2Mod)

```
UI/BlankPortraitBackground=Assets\Textures\terranblankportrait_static.dds
UI/StandardGameTooltip=Assets\Textures\ui_battlenet_tooltip_outline.dds
UI/ObjectivePanelCategoryBackground=Assets\Textures\ui_objectives_frame_title.dds
UI/CreditsPanelBackground_Left=Assets\Textures\ui_credit_frame_l.dds
UI/CreditsPanelBackground_Middle=Assets\Textures\ui_credit_frame_m.dds
UI/CreditsPanelBackground_Right=Assets\Textures\ui_credit_frame_r.dds
UI/SubtitleBorder=Assets\Textures\ui_storymode_subtitle_frame.dds
UI/BattleBuddyFriendsFrame=Assets\Textures\ui_battlebuddy_frame_terran.dds
UI/BattleBuddyMicrophoneFrame=Assets\Textures\ui_battlemic_terran.dds
```

## Default CTexture rule

`Core.SC2Mod/Base.SC2Data/GameData/TextureData.xml` also defines a default CTexture rule:

```xml
<CTexture default="1">
    <File value="Assets\Textures\##id##.dds"/>
</CTexture>
```

This means any CTexture catalog ID without an explicit entry falls back to `Assets/Textures/<id>.dds`. However, SC2 UI textures use human-readable IDs (`MenuBarButtonNormal`) while the actual files use a different naming scheme (`ui_gamemenu_topbuttons_normalpressed.dds`), so the default rule does not apply to the `UI/` namespace in practice.

## How our engine loads it

`games/starcraft-2/game/hud/hud.c` uses a two-tier lookup in `sc2_hud_image_index()`:

1. **Static `paths[]` table** — hardcoded entries for Core.SC2Mod keys that the VFS cannot reach (Liberty's Assets.txt takes priority). This covers ~30 HUD entries: menu bar buttons, idle/warp buttons, portraits, panels, tech glossary, etc.

2. **Runtime `assets_catalog[]`** — loaded from `gi.ReadFile("GameData/Assets.txt")` in `SC2_HUD_InitLayoutHost()`. Gets Liberty.SC2Mod's version, which covers minimap buttons, autocast overlay, rounded border, clear-selection icons, and other Liberty-specific entries.

Any `UI/` key not found in either table logs `SC2_HUD: unresolved UI resource` and returns index 0 (no texture).

## Discovery workflow

Use `mpqtool grep` to find where a given key is defined:

```bash
# Which archive and line defines UI/MenuBarButtonNormal?
build/bin/mpqtool -mpq "data/StarCraft2/Mods/Core.SC2Mod/Base.SC2Data" \
    grep "UI/MenuBarButtonNormal" GameData

# Scan Liberty.SC2Mod for all minimap-related keys:
build/bin/mpqtool -mpq "data/StarCraft2/Mods/Liberty.SC2Mod/Base.SC2Data" \
    grep "UI/Minimap" GameData

# List all UI/ entries from an archive:
build/bin/mpqtool -mpq "data/StarCraft2/Mods/Core.SC2Mod/Base.SC2Data" \
    cat GameData/Assets.txt | grep "^UI/"
```
