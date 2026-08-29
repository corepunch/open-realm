# Parser Notes

These notes translate the public format information into a practical OpenWarcraft3 loader plan. They are intentionally scoped to inspection and rendering experiments, not full StarCraft II gameplay parity.

## Suggested Loader Pipeline

1. Open `.SC2Map`/`.SC2Mod`/`.s2ma` as MPQ, or open `.SC2Components` as a directory-backed archive.
2. Build a file table with normalized slash handling.
3. Read cheap identity files first:
	- `DocumentHeader`, if present.
	- `MapInfo`.
	- locale `GameStrings.txt`.
	- minimap/loading image paths.
4. Parse map dimensions, bounds, tileset/theme, loading screen, and player slots from `MapInfo`.
5. Parse `Objects` for placed `Unit`, `Doodad`, `Point`, and `Camera` records.
6. Parse `GameData.xml` and the smallest useful subset of Unit/Actor/Model catalog XML needed to resolve placed units into M3 model paths.
7. Parse terrain texture metadata from `t3Terrain.xml`.
8. Parse binary terrain/pathing layers incrementally:
	- `t3CellFlags` for visualization and hole/cell inspection.
	- `t3HeightMap`, `t3SyncHeightMap`, and `t3SyncCliffLevel` for terrain shape.
	- `t3TextureMasks` for terrain layer blends.
	- `t3Water` for water/lava rectangles and templates.
	- `t3SyncPathingInfo` for movement/building pathing once decoded.
9. Resolve dependencies and localized strings after local parsing works on simple maps.

## Archive Abstraction

Use the same discipline as the Warcraft III MPQ path:

- One archive abstraction should support MPQ-backed and directory-backed maps.
- Query paths with normalized `/` and `\` handling.
- Keep extracted data in memory or test fixtures; do not rely on local ignored asset dumps.
- Tests should use small committed fixtures rather than a developer's StarCraft II install.

## Data Ownership

StarCraft II-specific parsers belong under `games/starcraft-2/`. Shared MPQ/container code can live in the common archive layer only if it remains game-agnostic.

Do not put StarCraft II literals into engine code. Examples of game-specific data that should stay in the StarCraft II target:

- `MapInfo`
- `t3Terrain.xml`
- `Base.SC2Data`
- terrain catalog IDs
- M3 model/material policy
- SC2 trigger/Galaxy assumptions

## Catalog Grammar Tables

`games/starcraft-2/common/sc2_map.c` decodes ordinary catalog child fields through `sc2XmlField_t` tables. Each entry names the XML production and the destination `{ offsetof, type, size }`; `sc2_parse_xml_child_field` reads the authored attribute and dispatches through the shared scalar decoder. Model, sound, actor, unit, terrain-texture, cliff, and tile records use this path.

Keep catalog layering separate from decoding. The schema fills a temporary record, then `sc2_catalog_add_*` merges only authored values into the dependency result. Nested or context-sensitive productions remain explicit: indexed unit flags, footprint area/shape geometry, actor aliases, parent inheritance, and model token expansion are not scalar fields.

Adding an ordinary catalog field should require one descriptor entry. Do not add another child-name `if`/`else` branch. If a new production is nested or repeated, add a narrowly named production callback and keep its scalar leaves table-driven.

## Galaxy Front End

Galaxy and JASS share the `libjass` AST and VM, but keep separate parser entry points. `jass_dobuffer_ex` selects them through the
`jass_syntax` table in `games/warcraft-3/jass/jdo.c`; each entry owns its token delimiters, parser callback, and preprocessing flags.
Add another source language by extending that table and emitting the existing `TOKEN` representation rather than adding mode branches
to the VM.

Native hosts include `games/warcraft-3/jass/jass_api.h`, which contains only the VM stack/coroutine ABI and does not import WC3 game
internals. The full `jass.h` remains the WC3-facing API. SC2's game library links `libjass` and publishes its native table through
`galaxy_get_natives()`.

Galaxy-specific type aliases and statement keywords are tables in `games/warcraft-3/jass/jparser.c`. Multi-character operators are
recognized by `jlex.c` only when their characters occur in the active syntax's delimiter set, which keeps `&&`, `||`, `<<`, and `>>`
out of JASS while allowing compact expressions such as `value!=3` in both languages. Parse failures set `PARSER.error` and make
`jass_dobuffer_ex` return false; they must not call `jass_rterror`, which aborts when no coroutine error boundary exists.

`continue` is intentionally rejected until the common loop AST can represent it without silently changing control flow. Galaxy
multidimensional accesses preserve the first index in `TOKEN.index` and chain later `TT_ARRAYACCESS` nodes through `TOKEN.body`.
The VM walks that chain through nested sparse `JASSARRAY` values for both reads and writes. This is required by native declarations such
as `bool[33][31] libNtve_gv__GameUIVisible`; discarding later dimensions desynchronizes statement parsing and produces bogus type-6
(`TT_IDENTIFIER`) diagnostics for `[`, the index name, and `]`. Run `make test-galaxy` for parser mode isolation, syntax coverage, VM
execution, includes, and the checked-in Galaxy smoke corpus.

## Binary Parsing Style

- Parse fixed-width integer fields with explicit little-endian reads.
- Avoid packed C structs for on-disk records with variable strings.
- Validate magic/version/dimensions before allocating arrays.
- Track byte offsets in diagnostics.
- Preserve unknown fields in dump output.
- Make dump tools useful before adding renderer behavior.

Recommended early diagnostics:

```text
sc2map: file=<path>
sc2map: archive files=<count>
MapInfo: version=<n> size=<w>x<h> bounds=<l,b,r,t> theme=<id> planet=<id>
MapInfo: players=<n>
t3CellFlags: version=<n> size=<w>x<h> counts[00]=... counts[01]=... counts[02]=... counts[03]=...
t3HeightMap: version=<n> size=<w>x<h> min=<z> max=<z> masks[0..3]=...
Objects: units=<n> doodads=<n> points=<n> cameras=<n>
Catalog: units=<n> actors=<n> models=<n> unresolvedModels=<n>
```

## Test Fixture Plan

Start with the smallest possible fixtures:

1. A tiny synthetic MPQ with a minimal `MapInfo` and `GameStrings.txt`.
2. A directory-backed `.SC2Components` fixture with the same files.
3. A fixture with `t3CellFlags` only.
4. A fixture with `Objects` and a few placed resources/start points.
5. Real-map compatibility tests only when data licensing and fixture size are acceptable.

The fixture rule from the repository instructions still applies: do not make tests depend on local retail data folders.

## Cross-Checking With Existing Tools

Use existing community tools as comparators:

- `sc2reader`: compare basic map metadata, localized strings, images, dimensions, camera bounds, tileset, player slots, and teams.
- StormLib or Ladik's MPQ Editor: compare archive listings and extracted bytes.
- SC2 Map Analyzer: compare terrain/pathing visualizations once `t3*` layers are parsed.

## Open Questions

- Exact modern `MapInfo` versions and field deltas across Wings of Liberty, Heart of the Swarm, Legacy of the Void, and current Arcade maps.
- Complete `Objects` schema and whether it is always text/XML-like for current maps.
- Exact dimensions and coordinate transforms between height, cliff, pathing, and render grids.
- Exact Unit -> Actor -> Model catalog links across base game, campaign, arcade maps, and Heroes-era assets.
- Dependency load order across maps, mods, campaigns, and Arcade content.
- Locked/protected map behavior and whether some downloaded maps omit editor-useful content.
