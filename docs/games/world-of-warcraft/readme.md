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

- [Data Loading](data-loading.md): MPQ data layout, WDT/ADT map entry, DBC helpers, and tool commands.
- [DBC Reference](dbc-reference.md): WDBC binary format, packed appearance/equipment values, and per-table field layouts for character/creature/UI DBCs.
- [File Formats](file-formats.md): collected reverse-engineered format notes for MPQ/CASC, WDT/ADT/WDL, WMO, M2/SKIN/ANIM, BLP, DBC/DB2, WDB, and related files.
- [Terrain And World Rendering](terrain-and-world-rendering.md): WDT tiles, ADT chunks, splats, alpha maps, doodads, WMOs, and height queries.
- [M2 And Character Display](m2-and-character-display.md): M2 loading, creation/select data flow, packed defaults, DBC-backed outfit data, geosets, and component texture rules.
- [WoW Character Display Quick Reference](../../wow-character.md): authoritative DBCs, saved appearance values, common pitfalls, and bounded diagnostic commands.
- [Grass Rendering System](grass-rendering-system.md): terrain-advertent grass rendering.
- [GPU Terrain Height Atlas And Static Grass Batches](static-grass-and-height-atlas.md): exact MCVT atlas, static M2 instance batches, patch culling, alpha handling, and staged verification plan.
- [References](references.md): public schema references and local source/tool entry points.
- [Sounds](sounds.md)
- [Magic And Effects](magic-and-effects.md): magic schools, damage types, multi-school system, buffs/debuffs, DoT/HoT, crowd control, status effects.
- [Enemies And Creatures](enemies-and-creatures.md): creature types/taxonomy, classifications (normal/elite/rare/boss), difficulty tiers, aggro/threat, NPC AI roles.
- [Weapons And Classes](weapons-and-classes.md): weapon types, class weapon access, combat roles (tank/healer/DPS), all class specializations, primary/secondary stats.

### Short Version

`openwow` mounts locally supplied WoW client data, opens a WDT path such as `World/Maps/Azeroth/Azeroth.wdt`, loads nearby ADT tiles, renders terrain chunks and splat layers, and uses M2 models for creatures/player-style actors. Character appearance is data-driven by packed appearance/equipment values, DBC records, M2 skin section IDs, and composed body textures.

The important rule for future work: keep WoW-specific policy under `games/world-of-warcraft/`. Engine modules should stay format-agnostic and receive generic renderer/game data through the selected-game boundary.

### Related Projects

- [whoahq/whoa](https://github.com/whoahq/whoa) — open-source WoW 3.3.5a client in C++11 (Windows/macOS/Linux), launches against extracted client data
- [Kelsidavis/WoWee](https://github.com/Kelsidavis/WoWee) — custom WoW client with Warden emulation, SDL2+Vulkan+OpenGL, Linux-native, most active reimplementation
- [Reinisch/Warcraft-Arena-Unity](https://github.com/Reinisch/Warcraft-Arena-Unity) — arena combat engine in Unity, data-driven spells/auras/effects, AI behavior graphs
- [TrinityCore](https://github.com/TrinityCore/TrinityCore) — reference WoW server emulator, spell system and combat mechanics
- [wowemulation-dev/warcraft-rs](https://github.com/wowemulation-dev/warcraft-rs) — Rust crates for BLP/M2/ADT/WDT/WMO parsing
