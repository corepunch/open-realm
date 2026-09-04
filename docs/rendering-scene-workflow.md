# Rendering scene workflow

For campaign loading and asset diagnostics, see [WC3 loading and assets](games/warcraft-3/loading-and-assets.md).

Use these commands to launch a selected game's UI or model scene without entering a full gameplay session. Build the target first, then pass the scene command as a late `+` command.

## Warcraft III

The main menu is the model-backed UI smoke scene:

```bash
make openwarcraft3
build/bin/openwarcraft3 -data 'data/Warcraft III' +ui_start_command menu_main
```

The 3D portrait is created by the `MainMenu3d` UI scene in `games/warcraft-3/menu/screens/main_menu.c`.

### White MDX geometry diagnosis

If menu scenes or in-game MDX models retain geometry but render large white regions, log a failed
`MDLX_GetTexture` result in `games/warcraft-3/renderer/mdx/r_mdx_geoset.c` and inspect the
`g_textures` chain in `renderer/r_texture.c`. A valid `mdxTexture_t.texid` with no
`R_FindTextureByID` result means the texid index was truncated; it is not an MPQ lookup or BLP decode failure.

`ADD_TO_LIST(VAR, LIST)` expands to two unbraced statements. Never use it as the sole body of an
unbraced `if`: only `VAR->next = LIST` becomes conditional while `LIST = VAR` always executes. This is
especially destructive when a runtime cache returns an object already present in an intrusive list,
because assigning that old object as the head discards every newer entry. Reproduce the WC3 menu path with:

```bash
build/bin/openwarcraft3 -data 'data/Warcraft III' +menu_game +com_frame_limit 1
```

## World of Warcraft

The character-create scene is the fastest character renderer test:

```bash
make openwow
build/bin/openwow -data data/world-of-warcraft +menu_character_create
```

The scene can also be selected through the character-select flow with `+menu_character_select`. Race, gender, class, and appearance are initialized by the WoW UI and passed through `wow_playerinfo`; for a gameplay-model test without the UI use:

```bash
build/bin/openwow -data data/world-of-warcraft \
  +map World/Maps/Azeroth/Azeroth.wdt \
  +set wow_playerinfo '\race\Human\sex\Male\class\1\appearance\0'
```

The character-create model is assembled in `games/world-of-warcraft/menu/menu_xml.c` and rendered through the M2 path in `games/world-of-warcraft/renderer/m2/r_m2.c`.
For default appearance, starter-outfit, saved-character, and DBC diagnosis, see [WoW Character Display](wow-character.md).

### Make shortcuts

Build and launch in one step:

```bash
make build-run-wow-map        # map ID 1 (Kalimdor)
```

If the binary is already built, skip the rebuild:

```bash
make run-wow                  # same map, no rebuild
make run-wow ARGS="+map 0"    # pass extra args (e.g. Eastern Kingdoms)
```

Per-race playercreate spawns (build + run):

```bash
make build-run-wow-human
make build-run-wow-orc
make build-run-wow-dwarf
make build-run-wow-undead
make build-run-wow-tauren
make build-run-wow-nightelf
make build-run-wow-gnome
make build-run-wow-troll
```

## StarCraft II

The default map is `Maps/TerranTest.SC2Components` — a flat 96×96 Mar Sara terrain with a Terran
Command Center, 2 SCVs, 2 Marines, and a Vespene Geyser for player 1.

```bash
make opensc2
build/bin/opensc2 -data data/StarCraft2 +map Maps/TerranTest.SC2Components
```

SC2 UI and map rendering are separate from the WoW character scene; do not use a WoW-style character command unless a future SC2 screen registers one.

### Make shortcuts

```bash
make build-run-sc2            # build + launch TerranTest
make run-sc2                  # launch TerranTest, no rebuild
make run-sc2 ARGS="+map Foo"  # override map
```

## Screenshots

Use the engine `screenshot [frame-delay]` command rather than desktop capture. It writes `screenshots/shotNNNN.png`; with a delay,
the Nth fully rendered frame after command execution is captured. This makes bounded command-line QA deterministic:

```bash
build/bin/openwow -data data/world-of-warcraft +map playercreate +screenshot 10 +com_frame_limit 20
build/bin/openwarcraft3 -data 'data/Warcraft III' +menu_main +screenshot 10 +com_frame_limit 20
```

Place `+screenshot N` after the map/menu selector and keep `com_frame_limit` larger than `N`. `+screenshot 1` captures the first
rendered frame; bare `+screenshot` captures the next rendered frame. On macOS Retina displays, capture uses the GL
viewport/drawable size rather than SDL's logical window size, so the PNG includes the complete framebuffer.

### Headless / no-focus-steal mode

Pass `+vid_hidden 1` to create the GL window without ever showing it. The window never appears on screen and the
application does not steal keyboard focus from the IDE. All rendering and screenshot capture work normally because
`glReadPixels` reads from the GL framebuffer regardless of window visibility.

```bash
# Screenshot without window appearing — VS Code keeps focus
make run-sc2 ARGS="+vid_hidden 1 +screenshot 5 +com_frame_limit 20"
make run-wow ARGS="+vid_hidden 1 +screenshot 5 +com_frame_limit 20"
```

`+vid_hidden 1` sets the `vid_hidden` cvar before the renderer initialises. It is implemented in
`renderer/r_main.c` by substituting `SDL_WINDOW_HIDDEN` for `SDL_WINDOW_SHOWN` at window creation.

For bounded startup or stdout diagnostics, add `+com_frame_limit N`; do not use it when manually inspecting a scene unless `N` is large enough to reach the scene.
