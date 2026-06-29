# World of Warcraft

This is an alternate game target built on the same engine/runtime boundary as the Warcraft III path. It is not trying to be a complete MMO. Right now it is a focused proving ground for loading World of Warcraft client data, rendering large outdoor terrain, and exercising the selected-game module split.

The code here owns WoW-specific game stubs, M2 model handling, WDT/ADT terrain rendering hooks, and a small loading-screen UI.

## Status

Exploratory renderer and world-loading target.

The `openwow` build is useful for testing terrain/model asset loading and renderer architecture. It is not a playable World of Warcraft implementation. Think of it as a technical campsite: enough structure to explore the world data, not a finished town.

## Working

- Separate `openwow` executable and game/renderer/UI libraries.
- WDT map entry path through the selected game module.
- ADT-oriented terrain renderer code for map tiles, splats, alpha layers, terrain state, and object paths.
- M2 model loading/rendering path, including fallback handling for missing models.
- Basic creature/entity scaffolding and model registration paths.
- Loading-screen UI with WoW textures, fonts, loading title/status text, and progress display.
- DBC helpers for selected loading-screen and map metadata lookup.
- Build integration through `make openwow`.

## Partial

- Terrain rendering is the core focus; object placement, animation polish, lighting, and exact client parity are incomplete.
- Entity simulation exists only as lightweight scaffolding compared with the Warcraft III game target.
- Loading screens are functional but intentionally narrow.
- Data compatibility is tied to the locally available WoW client data layout used during development.

## Not There Yet

- MMO gameplay, combat, quests, spells, inventory, persistence, networking, or server world rules.
- Full DBC/DB2 coverage.
- Full UI implementation beyond the loading-screen style shell.
- Complete WMO, doodad, creature, particle, and animation fidelity.
- Production support for arbitrary WoW client versions.

## Build And Run

Build:

```bash
make openwow
```

Run with the Makefile's sample data path:

```bash
make run-wow
```

Or run directly with a WDT path:

```bash
build/bin/openwow -data data/world-of-warcraft +map World/Maps/Azeroth/Azeroth.wdt
```

## Notes

This target expects locally supplied World of Warcraft client data. Original assets, names, and game data belong to Blizzard Entertainment. Nothing in this directory should be read as a promise of full MMO behavior; it is a renderer/runtime exploration branch that shares the engine with the rest of OpenWarcraft3.

## Documentation

What the World of Warcraft target currently knows how to load and render.

### Documents

- [Data Loading](docs/data-loading.md): MPQ data layout, WDT/ADT map entry, DBC helpers, and tool commands.
- [File Formats](docs/file-formats.md): collected reverse-engineered format notes for MPQ/CASC, WDT/ADT/WDL, WMO, M2/SKIN/ANIM, BLP, DBC/DB2, WDB, and related files.
- [Terrain And World Rendering](docs/terrain-and-world-rendering.md): WDT tiles, ADT chunks, splats, alpha maps, doodads, WMOs, and height queries.
- [M2 And Character Display](docs/m2-and-character-display.md): M2 loading, skin files, DBC-backed outfit data, geosets, and component texture rules.
- [Grass Rendering System](docs/grass-rendering-system.md): terrain-advertent grass rendering.
- [References](docs/references.md): public schema references and local source/tool entry points.
- [Sounds](docs/sounds.md)

### Short Version

`openwow` mounts locally supplied WoW client data, opens a WDT path such as `World/Maps/Azeroth/Azeroth.wdt`, loads nearby ADT tiles, renders terrain chunks and splat layers, and uses M2 models for creatures/player-style actors. Character appearance is data-driven by packed appearance/equipment values, DBC records, M2 skin section IDs, and composed body textures.

The important rule for future work: keep WoW-specific policy under `games/world-of-warcraft/`. Engine modules should stay format-agnostic and receive generic renderer/game data through the selected-game boundary.
