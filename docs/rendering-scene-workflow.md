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

## StarCraft II

There is currently no character-create/model portrait scene registered for SC2. Use the HUD/map scene for renderer smoke tests:

```bash
make opensc2
build/bin/opensc2 -data data/StarCraft2 +map TRaynor01
```

SC2 UI and map rendering are separate from the WoW character scene; do not use a WoW-style character command unless a future SC2 screen registers one.

## Screenshots

Use the in-game `screenshot` command after the target scene is visible. The client writes `screenshots/shotNNNN.png`. On macOS Retina displays, the capture uses the GL viewport/drawable size rather than SDL's logical window size, so the PNG includes the complete framebuffer instead of only its lower-left quarter.

For bounded startup or stdout diagnostics, add `+com_frame_limit N`; do not use it when manually inspecting a scene unless `N` is large enough to reach the scene.
