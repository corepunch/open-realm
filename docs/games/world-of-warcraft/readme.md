# World of Warcraft

The `openwow` executable loads a WoW-specific game, menu and renderer. It uses the player's installed Classic archives for terrain, models, interface artwork and client databases. The game module owns character progression, NPC interactions and travel.

## Playing the current build

| Input | Use |
| --- | --- |
| **M** | Browse the current zone, explore subzones and locate landmarks or tracked objectives. [World map](world-map.md) |
| **L** | Inspect, track or abandon a quest. Return to the giver to claim eligible rewards. [Quest journal](quest-ui.md) |
| **B** | Inspect the backpack, move or split stacks, and use consumables. [Backpack](backpack.md) |
| **P** | Read spell details, cast a spell and assign it to an action button. [Spellbook](spellbook.md) |
| **T** | Discover the nearby flight point or buy a route through previously discovered stops. [Flight paths](taxi.md) |
| **First Aid** | Craft bandages from looted linen and use them from the backpack. [Crafting](crafting.md) |
| **Interact with an NPC** | Buy or sell provisions, recover recently sold stacks, and deposit or withdraw items at eligible NPCs. [Merchants](merchants.md) · [Bank](bank.md) |

Named characters retain progress through the character-selection and game save paths. [Character progress](character-progress.md)

## Engine and data coverage

The renderer loads WDT and ADT terrain, M2 models and WMO geometry. The menu runs a subset of the installed FrameXML. Gameplay uses the supported Classic DBC layouts and a bounded selection of creature, quest, item and spell data. Imported service-NPC positions currently cover map 0. Cross-map flights and a general profession or trainer system remain open.

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
- [Quest UI & Server Data](quest-ui.md): quest lifecycle, authoritative data, dialog presentation, and spawn rules.
- [Message Inbox And Client Windows](message-inbox-ui.md): server-owned notifications, window input ownership, and the non-modal welcome tutorial.
- [File Formats](file-formats.md): collected reverse-engineered format notes for MPQ/CASC, WDT/ADT/WDL, WMO, M2/SKIN/ANIM, BLP, DBC/DB2, WDB, and related files.
- [Terrain And World Rendering](terrain-and-world-rendering.md): WDT tiles, ADT chunks, splats, alpha maps, doodads, WMOs, and height queries.
- [M2 And Character Display](m2-and-character-display.md): M2 loading, creation/select data flow, packed defaults, DBC-backed outfit data, geosets, and component texture rules.
- [WoW Character Display Quick Reference](../../wow-character.md): authoritative DBCs, saved appearance values, common pitfalls, and bounded diagnostic commands.
- [Grass Tech Reference](GRASS_TECH.md): current implementation state — active vs. disabled paths, wind-sway math, phase decorrelation, density formula, and key constants.
- [Grass Rendering System](grass-rendering-system.md): architecture walkthrough, M2 instancing design, historical prototype notes.
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
