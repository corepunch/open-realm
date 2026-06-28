# Tools

This directory contains the standalone command-line tools used to inspect
Warcraft III assets and game data.

## `mpqtool`

Archive utility for MPQ files.

Use it to list contents, dump files, inspect image headers, or create and
pack test archives.

Syntax:

```bash
build/bin/mpqtool -mpq "<archive.mpq>" <command> [args...]
```

Commands:

- `ls [path]` list archive entries under a path
- `cat <file>` print a file to stdout
- `imginfo <file>` inspect texture metadata
- `create [max-files]` create a new archive
- `pack <src> <archive-file> [<src> <archive-file> ...]` add files to an archive
- `wow-install [-strip-data-prefix] <output-dir> <disc1.mpq> <disc2.mpq> <disc3.mpq> <disc4.mpq>` rebuild vanilla WoW runtime MPQ archives from installer tomes; `-strip-data-prefix` writes manifest containers such as `Data\terrain.MPQ` directly as `<output-dir>/terrain.MPQ`

Examples:

```bash
build/bin/mpqtool -mpq "data/Warcraft III/War3.mpq" ls UI/FrameDef
build/bin/mpqtool -mpq "data/Warcraft III/War3.mpq" cat UI/FrameDef/Glue/MainMenu.fdf
build/bin/mpqtool -mpq "data/Warcraft III/War3.mpq" imginfo UI/Widgets/Glues/GlueScreen-Button1-Border.blp
build/bin/mpqtool wow-install -strip-data-prefix "data/world-of-warcraft" "build/wow-install/WoWDisc1/Installer Tome.mpq" "build/wow-install/WoWDisc2/Installer Tome 2.mpq" "build/wow-install/WoWDisc3/Installer Tome 3.mpq" "build/wow-install/WoWDisc4/Installer Tome 4.mpq"
```

The Makefile shortcut extracts all four WoW disc ISOs into build scratch space
and repacks `data/world-of-warcraft/*.MPQ`:

```bash
make install-wow WOW_ISO_DIR="/path/to/wow-isos"
```

## `fdftool`

FDF scene parser and viewer.

Use it to inspect parsed UI frame trees, check layout behavior, and preview
the decoded scene graph from one or more FDF files.

Syntax:

```bash
build/bin/fdftool -mpq "<archive.mpq>" -fdf "<file.fdf>" [-fdf "<file.fdf>" ...] -root "<FrameName>" [options]
```

Useful options:

- `--info` print parsed frame/tree information and exit
- `-width <px>` and `-height <px>` set the preview window size

Examples:

```bash
build/bin/fdftool -mpq "data/Warcraft III/War3.mpq" -fdf "UI\\FrameDef\\Glue\\MainMenu.fdf" -root MainMenuFrame
build/bin/fdftool -mpq "data/Warcraft III/War3.mpq" -fdf "UI\\FrameDef\\UI\\ConsoleUI.fdf" -fdf "UI\\FrameDef\\UI\\ResourceBar.fdf" -root ConsoleUI
build/bin/fdftool -mpq "data/Warcraft III/War3.mpq" -fdf "UI\\FrameDef\\Glue\\MainMenu.fdf" --info
```

## `fdfbindgen`

FDF binding header generator. It writes generated C to stdout, so redirect it
to whichever checked-in or temporary header path you want. Name generated
headers after the consuming `.c` file: `games/warcraft-3/ui/screens/main_menu.c` should include
`games/warcraft-3/ui/generated/main_menu.h`.

Use it to generate per-screen C structs and binding functions from FDF frame
names, so screen controllers do not hand-write lookup structs or assign
`UI_FindFrame` / `UI_FindChildFrame` strings themselves. It emits `<Prefix>_t`
and `<Prefix>_Load(...)`; the struct uses flat direct fields, e.g.
`main_menu.ExitButton`. The load function parses configured FDF paths once,
then binds the selected root from the global FDF frame table.
For single-root headers it also emits `<Prefix>_Bind(...)`, which binds the
same generated fields against an existing root frame. Use this for cloned FDF
trees such as reusable row or pane templates.

When more than one FDF input is passed, matching frame paths are merged. Frames
that only exist in some inputs are emitted as optional bindings, so one checked-in
header can cover RoC/TFT shape differences and the screen controller can branch
on whether an optional pointer is present.

Syntax:

```bash
build/bin/fdfbindgen [-prefix Name] [-root FrameName] [-optional-root FrameName] [-load path] [-include path] [-no-include] [-optional-children] <file.fdf|->...
```

Useful options:

- `-prefix <Name>` choose the C type/function prefix
- `-root <FrameName>` bind only one root frame; pass it more than once for multiple roots
- `-optional-root <FrameName>` bind a selected root without failing when it is absent
- `-load <path>` emit a parse-once load for a runtime FDF path before binding
- `-include <path>` choose the generated header include, defaulting to `../ui_local.h`
- `-no-include` omit the include if the includer already provides UI declarations
- `-optional-children` keep selected roots required but bind all child frames as optional

Examples:

```bash
# UI module (glue menus):
build/bin/fdfbindgen -prefix MainMenu -root MainMenuFrame -load "UI\\FrameDef\\Glue\\MainMenu.fdf" MainMenu.fdf > games/warcraft-3/ui/generated/main_menu.h
build/bin/mpqtool -mpq "data/Warcraft III/War3.mpq" cat UI/FrameDef/Glue/MainMenu.fdf | build/bin/fdfbindgen -prefix MainMenu -root MainMenuFrame -load "UI\\FrameDef\\Glue\\MainMenu.fdf" -

# Game module (in-game HUD panels, info panels, dialogs):
build/bin/mpqtool -mpq "data/Warcraft III/War3.mpq" cat UI/FrameDef/UI/InfoPanelUnitDetail.fdf | build/bin/fdfbindgen -prefix InfoPanelUnitDetail -root InfoPanelUnitDetail -load "UI\\FrameDef\\UI\\InfoPanelTemplates.fdf" -load "UI\\FrameDef\\UI\\InfoPanelUnitDetail.fdf" -include "../g_local.h" - > games/warcraft-3/game/generated/info_panel_unit_detail.h
build/bin/mpqtool -mpq "data/Warcraft III/War3.mpq" cat UI/FrameDef/UI/ConsoleUI.fdf | build/bin/fdfbindgen -prefix ConsoleUI -root ConsoleUI -load "UI\\FrameDef\\UI\\ConsoleUI.fdf" -include "../g_local.h" - > games/warcraft-3/game/generated/console_ui.h
```

## `img2sysfont`

Console font PCX to embedded renderer header converter. It copies the raw PCX
bytes into a C array so `renderer/r_sysfont.c` can load the built-in console
font through the normal in-memory PCX decoder.

Syntax:

```bash
build/bin/img2sysfont <input.pcx> <output.h> <symbol>
```

Example:

```bash
build/bin/img2sysfont renderer/conchars.pcx renderer/conchars_sysfont.h conchars_sysfont_pcx
```

The repo shortcut is:

```bash
make font
```

## `isoextract`

Dependency-free ISO 9660/Joliet extractor for installer discs such as classic
World of Warcraft media.

Syntax:

```bash
build/bin/isoextract ls "<image.iso>"
build/bin/isoextract extract "<image.iso>" "<output-dir>"
build/bin/isoextract "<image.iso>" "<output-dir>"
```

Examples:

```bash
build/bin/isoextract ls "/Users/igor/Documents/worldofwarcraftstandardeditioneu/WoWDisc1.iso"
build/bin/isoextract "/Users/igor/Documents/worldofwarcraftstandardeditioneu/WoWDisc1.iso" "data/world-of-warcraft/WoWDisc1"
```

## `mdxtool`

MDX model viewer and inspector.

Use it to inspect model metadata, preview sequences, compare UI models against
game models, and render models with different camera modes.

Syntax:

```bash
build/bin/mdxtool -mpq "<archive.mpq>" -model "<file.mdx>" [options]
```

Useful options:

- `--anim "<sequence>"` choose a sequence to preview
- `--use-model-camera` use the model's own camera if present
- `--front-ortho` force the front-facing UI preview camera
- `--info` print model metadata and exit
- `--dump-all` print detailed model contents
- `--once` render one frame and exit

Examples:

```bash
build/bin/mdxtool -mpq "data/Warcraft III/War3.mpq" -model "UI\\Glues\\SpriteLayers\\TopRightPanel.mdx" --front-ortho
build/bin/mdxtool -mpq "data/Warcraft III/War3.mpq" -model "UI\\Glues\\MainMenu\\WarCraftIIILogo\\WarCraftIIILogo.mdx" --info
build/bin/mdxtool -mpq "data/Warcraft III/War3.mpq" -model "units\\orc\\Peon\\Peon.mdx" --use-model-camera
```

## `m2tool`

M2 model viewer and inspector for World of Warcraft assets.

Use it to preview M2 models through the WoW renderer, inspect M2 headers, array
offsets, bounds, vertex bounds, texture references, embedded/external skin
counts, animation rows, and the player character outfit configuration used by
the WoW renderer.

Syntax:

```bash
build/bin/m2tool -mpq "<archive.mpq>" -model "<file.m2>" [options]
```

Useful options:

- `--viewer` force viewer mode, which is also the default
- `--once` render one frame and exit
- `--info` print model metadata and exit
- `--dump-all` include animation rows
- `--skin "<file00.skin>"` inspect an explicit skin file instead of the derived path
- `--wow-player-config` load `CharStartOutfit.dbc` and `ItemDisplayInfo.dbc`
  for the model race/gender, print the starting display IDs, component texture
  paths, and visible/hidden character section IDs in info mode; in viewer mode,
  pass the same packed appearance/equipment to the render entity
- `--wow-player-config-only` print only the in-game character outfit/geoset
  configuration and exit
- `--appearance <bits>` override the packed WoW appearance bits used by
  `--wow-player-config`
- `--equipment <bits>` override the packed WoW equipment bits printed by
  `--wow-player-config`. Equipment bytes are local slot item indices into
  race/gender/slot item lists; byte value `0` means empty, while value `1`
  currently selects DBC-backed Horde plate preview items for Orc male upper
  body, lower body, hands, and feet.

Example:

```bash
build/bin/m2tool -mpq "data/world-of-warcraft/model.MPQ" -model "Character\\Orc\\Male\\OrcMale.m2"
build/bin/m2tool -mpq "data/world-of-warcraft/model.MPQ" -model "Character\\Orc\\Male\\OrcMale.m2" --info
make m2tool-wow-orcmale-player
```

## `maptool`

Map viewer for Warcraft III world maps.

Use it to open a map and inspect the world renderer with the current camera.

Syntax:

```bash
build/bin/maptool -mpq "<archive.mpq>" -map "<file.w3m>"
```

Example:

```bash
build/bin/maptool -mpq "data/Warcraft III/War3.mpq" -map "Maps\\Campaign\\Human02.w3m"
```

## `toolbox`

SDL tool manager for the command-line helpers in `build/bin`.

The layout follows classic commander/archive-manager tools: an MPQ browser on
the left, tool and argument fields in the center, `-?` help on the right, and
recent commands/output along the bottom. It stores recent commands next to the
executable as `toolbox_recent.txt`.

Syntax:

```bash
build/bin/toolbox [-mpq "<archive.mpq>"] [-mpqtool "<path>"]
```

Useful keys:

- `F5` run the current command
- `F6` refresh the selected tool's `-?` help
- `Enter` open the selected MPQ directory or choose a file
- `Tab` switch editable fields
- `D` copy the selected MPQ entry into the active command field

## Notes

- Paths inside MPQs usually use forward slashes, but most tools accept either
  `\` or `/` on the command line.
- The `build/bin/...` paths are the default build outputs used by this repo.
