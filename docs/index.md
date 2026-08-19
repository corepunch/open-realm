# OpenWarcraft3 Documentation

OpenWarcraft3 is a Quake-style engine and game implementation for Warcraft III,
World of Warcraft, and StarCraft II data. The repository [README](https://github.com/corepunch/openwarcraft3/blob/main/README.md)
contains build, installation, and project-overview information.

## Start Here

- [Architecture](architecture/runtime.md): runtime modules, client/server boundaries,
  networking, rendering, sound, UI, and fog of war.
- [Coordinate-System Contract](../AXIS.md): native game axes, current WoW conversions,
  and the migration plan for axis-aware rendering.
- [Warcraft III](games/warcraft-3/readme.md): target status, formats, renderer, UI,
  cinematics, sounds, and gameplay coverage.
- [World of Warcraft](games/world-of-warcraft/readme.md): target status, ADT terrain,
  M2 display, DBC data, gameplay systems, and grass rendering.
- [StarCraft II](games/starcraft-2/readme.md): target status, SC2Map loading, M3 models,
  catalog data, and HUD layout.
- [Diagnostic Tools](diagnostic-tools.md): bounded renderer and asset-inspection workflows.
- [Documentation Guide](documentation-guide.md): documentation placement and completion rules.

All durable documentation lives in this `docs/` tree. Shared workflows are stored at
the root, engine architecture under `docs/architecture/`, and game-specific material
under `docs/games/<game>/`.
