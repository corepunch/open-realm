# Map Storage And Loading

## Archive Shape

StarCraft II maps are stored as MPQ archives. The public MPQ documentation by Ladislav Zezula is the best base reference for the archive container. It notes that MPQ user data is optional and commonly appears in StarCraft II custom maps.

Relevant StarCraft II extensions:

- `.SC2Map`: map archive.
- `.SC2Mod`: mod archive, used as a dependency/data package.
- `.SC2Archive`: asset/archive package.
- `.s2ma`: Battle.net/cache-style map archive name often seen for downloaded maps.
- `.SC2Components`: unarchived component-folder form saved by the Galaxy Editor.

SC2Mapster's component-folder guide describes `.SC2Map` and `.SC2Components` as archived versus unarchived forms of the same project. In practice, `.SC2Components` is the easiest format to inspect because files can be read directly without an MPQ tool.

## Component Folders

The Galaxy Editor can save maps as component folders:

1. Open a project in `SC2Edit`.
2. Use `File -> Save As`.
3. Choose `StarCraft II Component Folders (.SC2Components)`.

The editor still uses a `.SC2Map`-looking project name in some UI paths, but the output is a directory tree. Publishing to Battle.net requires an archived map/mod, so component-folder work usually ends by saving or packing the project back into `.SC2Map`/`.SC2Mod`.

## Runtime Data Overlay

Community modding notes describe StarCraft II XML loading as a layered process:

1. Base game XML is loaded from core archives such as `base.SC2Assets` or `patch.SC2Assets`.
2. Dependency mod archives are loaded.
3. Map-local XML is loaded.
4. Matching ordinary XML attributes are updated.
5. Array-like XML attributes are appended, which can surprise tools that expect replacement behavior.

The exact dependency resolver is client/editor owned, but parser work should expect map-local XML to override or extend catalog data rather than stand alone.

## Game Data Layout

The plain-text data layer commonly appears under:

- `Base.SC2Data/GameData/`
- locale folders such as `enUS.SC2Data/`
- `GameData.xml`, which can list catalog paths.
- `GameStrings.txt`, often used for localized map names/descriptions and other user-facing text.

SC2Mapster editor guides describe moving data-space files into `Base.SC2Data/GameData` and adding catalog paths to `GameData.xml`. They also note that display text is stored separately by locale, so tooling should not assume `enUS` is the only possible language folder.

## Downloaded Maps And Replays

`sc2reader` can load maps directly and can download/load the map associated with a replay. Its public docs say map parsing can extract minimap/icon images, localized name/author/description/website, map dimensions, camera bounds, tileset, player slot data, and team alliance/enemy data.

This is useful as a sanity check for future local parser work: if an OpenWarcraft3 parser extracts those same fields from the same `.SC2Map`, it is probably reading the archive and `MapInfo`/localization data correctly.

## Practical Takeaways

- Treat `.SC2Map` as an MPQ first, not as a bespoke monolithic binary.
- Keep archive-container parsing separate from map-file parsing.
- Support directory-backed `.SC2Components` early; it makes fixtures and debugging easier.
- Expect a mix of binary terrain files, XML catalogs, text localization files, and image assets.
- Keep dependency and catalog loading explicit. A map's XML may be meaningful only after base/mod catalogs have been loaded.

