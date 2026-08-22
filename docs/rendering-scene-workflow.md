# Rendering scene workflow

Use these commands to launch a selected game's UI or model scene without entering a full gameplay session. Build the target first, then pass the scene command as a late `+` command.

## Warcraft III

The main menu is the model-backed UI smoke scene:

```bash
make openwarcraft3
build/bin/openwarcraft3 -data 'data/Warcraft III' +ui_start_command menu_main
```

For text-only layout inspection, which does not require a display:

```bash
make run-ui-text
```

The 3D portrait is created by the `MainMenu3d` UI scene in `games/warcraft-3/ui/screens/main_menu.c`.

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

The character-create model is assembled in `games/world-of-warcraft/ui/ui_xml.c` and rendered through the M2 path in `games/world-of-warcraft/renderer/m2/r_m2.c`.
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

There is currently no character-create/model portrait scene registered for SC2. Use the HUD/map scene for renderer smoke tests:

```bash
make opensc2
build/bin/opensc2 -data data/StarCraft2 +map TRaynor01
```

SC2 UI and map rendering are separate from the WoW character scene; do not use a WoW-style character command unless a future SC2 screen registers one.

### Make shortcuts

```bash
make build-run-sc2            # build + launch TRaynor01
make run-sc2                  # launch TRaynor01, no rebuild
make run-sc2 ARGS="+map Foo"  # override map
```

## Screenshots

Use the in-game `screenshot` command after the target scene is visible. The client writes `screenshots/shotNNNN.png`. On macOS Retina displays, the capture uses the GL viewport/drawable size rather than SDL's logical window size, so the PNG includes the complete framebuffer instead of only its lower-left quarter.

For bounded startup or stdout diagnostics, add `+com_frame_limit N`; do not use it when manually inspecting a scene unless `N` is large enough to reach the scene.
