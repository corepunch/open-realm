# Warcraft III

This is the main game target for OpenWarcraft3. It is the most complete path in the tree, and the place where the engine is being shaped into a playable RTS rather than only a viewer.

The code here owns Warcraft III-specific game logic, JASS, SLK/profile parsing, MDX and W3M rendering hooks, the client-side FDF UI, and the Warcraft III test suites.

## Status

Playable prototype. Not parity yet.

The current build can start from the menu, launch campaign or map content, render Warcraft III terrain/models/UI, run a server-authoritative simulation, and execute a growing slice of unit, command, combat, JASS, UI, and networking behavior. It is still an indie engine project in motion: solid foundations, visible gameplay, lots of systems present, and plenty of rough edges.

## Working

- Warcraft III MPQ-backed data loading through the shared archive layer.
- W3M/W3X map archive loading for maps present in the configured data folder or test archive.
- Warcraft III terrain rendering through the game renderer hooks.
- MDX model loading and rendering, including animation evaluation paths used by units and UI models.
- Client-side FDF UI runtime for menus, dialogs, map selection, options, single player flow, and Warcraft-style frame layout.
- Main menu and Single Player campaign selection flow.
- Quake-style console, cvars, command completion, and map command routing.
- Listen-server loopback play path and UDP client/server plumbing.
- Server-authoritative unit simulation with selection commands, movement, collision, basic pathing, animation states, health, damage, death, and inventory slots.
- Basic RTS command and ability coverage, including move, stop, attack, train, summon, item flow, Thunderbolt-style target spell behavior, timed life, stun status, and selected aura/status hooks.
- JASS VM and native API coverage for many gameplay-facing functions.
- SLK/profile parsing for Warcraft III data tables.
- Deterministic test assets and game-specific tests under `games/warcraft-3/tests/`.

## Partial

- Combat and abilities are functional but intentionally selective. Many Warcraft III ability codes are registered as stubs or partial local behavior.
- Fog of war has authoritative server-side state and network sync coverage, but the full gameplay/rendering polish is still evolving.
- UI is client-side and FDF-driven, but not every in-game panel or Battle.net-era screen is implemented.
- Asset compatibility is strongest around original Warcraft III data, with ongoing work for later 1.29-era data and edge cases.
- Multiplayer transport and lobby flows exist, but this is not yet a finished multiplayer product.

## Not There Yet

- Full campaign scripting and trigger parity.
- Complete Warcraft III object data, tech tree, upgrades, build rules, and race-specific gameplay.
- Full ability, buff, autocast, item, and hero-system parity.
- Complete save/load, replay, AI, and editor-level map behavior.
- Production-grade user experience around errors, missing assets, and unsupported maps.

## Build And Run

Build the default target:

```bash
make build
```

Run the Warcraft III client:

```bash
make run
```

Run a map directly:

```bash
make run-map
```

Useful validation:

```bash
make test
make test-ui
make test-jass
```

## Notes

This target expects a legitimate Warcraft III data folder. Original assets, names, and game data belong to Blizzard Entertainment. The repository contains code, fixtures, and generated test assets, not the retail game data.

## Documentation

File formats, renderer notes, UI/FDF behavior, and gameplay coverage work used by this target. Engine-wide client, server, networking, and runtime-module docs still live under the top-level `doc/architecture/` tree.

### File Formats

- [Warcraft III Map Format](docs/file-formats/map.md)
- [Warcraft III MDX Model Format](docs/file-formats/mdx.md)
- [SLK Spreadsheet Format](docs/file-formats/slk.md)
- [UI Frame Definition File](docs/file-formats/fdf.md)

### Detailed File Notes

- [Warcraft III File Format Specifications](docs/file-docs/readme.md)
- [MPQ Archive Format](docs/file-docs/mpq.md)
- [W3M / W3X Map Container](docs/file-docs/w3x-w3m.md)
- [war3map.w3e Terrain](docs/file-docs/w3e.md)
- [war3map.doo Doodads](docs/file-docs/doo.md)
- [war3map.shd / war3map.wpm](docs/file-docs/shd-wpm.md)
- [Warcraft III FDF Reference](docs/file-docs/fdf.md)

### Implementation Notes

- [Warcraft III UI System](docs/architecture/ui.md)
- [UI Flow](docs/architecture/ui-flow.md)
- [UI Quick Reference](docs/architecture/ui-quick-reference.md)
- [Map Renderer](docs/architecture/map-renderer.md)
- [Ability Coverage](docs/architecture/ability-coverage.md)
- [ROC Ability Checklist](docs/architecture/roc-ability-checklist.md)
- [Cinematics](docs/cinematics.md)
- [Sounds](docs/sounds.md)
