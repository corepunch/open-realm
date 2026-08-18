# World of Warcraft File Formats

These notes gather the scattered public reverse-engineering references that are most useful for the current `openwow` target. Treat them as a map of where to look, not as a frozen specification: Blizzard changed many file layouts across classic, TBC, Wrath, Cataclysm, Warlords, Legion, and modern builds.

For implementation work in this repository, validate each field against the local client data with `mpqtool`, `m2tool`, or a small purpose-built inspector before changing renderer/game behavior.

## Source Tiers

| Source | Best Use | Caution |
| --- | --- | --- |
| [wowdev wiki](https://wowdev.wiki/) | Deep binary layouts, chunk names, version notes, DBC/DB2 schema pages, WDB/packet background. | Pages vary in age and completeness; check page history and version annotations. |
| [WoTLK Modding Wiki](https://wotlkdev.github.io/wiki/) | Clear higher-level explanations for 3.3.5-era ADT/WDT/WDL, M2, DBC, and modding workflows. | Easier to read than wowdev, but not a substitute for byte-level layout checks. |
| [warcraft-rs](https://github.com/wowemulation-dev/warcraft-rs) | Executable parser reference for MPQ, DBC, BLP, M2, WMO, ADT, WDT, and WDL across WoW 1.x through 5.x. | Rust implementation details may target parser/converter goals that differ from our renderer. |
| `data/whoa-master` | Local client-reconstruction reference for component textures, geoset prep, DBC records, M2 scene/model code, and map object scaffolding. | It is C++ and client-shaped; copy concepts, not architecture. Keep policy under `games/world-of-warcraft/`. |
| [pywowlib](https://github.com/wowdev/pywowlib) | Compact Python/Cython readers for M2, WMO, ADT, BLP, MPQ/CASC, and WDBX-adjacent data. | Its version support table marks some formats partial or untested. |
| TrinityCore/getMaNGOS docs | DBC tables and server-side extraction lore. | Server projects often document the fields they need, not every client rendering nuance. |

## Containers And File Discovery

| Format | Role | References |
| --- | --- | --- |
| `MPQ` | Pre-Warlords archive container used by classic-era clients. Current `openwow` data paths are MPQ-oriented. | [warcraft-rs MPQ support](https://github.com/wowemulation-dev/warcraft-rs), [pywowlib archive support](https://github.com/wowdev/pywowlib), local `tools/mpqtool.c`. |
| `CASC` / TACT | Content-addressable storage that replaced MPQ in Warlords-era WoW. Not the current `openwow` target, but relevant if modern data is ever supported. | [Warcraft Wiki CASC](https://warcraft.wiki.gg/wiki/CASC), [wowdev GitHub TACTKeys](https://github.com/wowdev/TACTKeys), [wowdev GitHub TACTSharp](https://github.com/wowdev/TACTSharp). |
| Listfiles / file data | Mapping opaque archive/file IDs back to paths, especially for modern clients. | [wowdev wow-listfile](https://github.com/wowdev/wow-listfile), [wow.export](https://github.com/Kruithne/wow.export), [WoWDBDefs](https://github.com/wowdev/WoWDBDefs). |

Implementation notes:

- Classic-era paths are case-insensitive archive paths such as `World\Maps\Azeroth\Azeroth.wdt`.
- Later clients increasingly route file lookup through `FileData`/DB2 IDs and listfiles instead of path-only lookup.
- Tests should not require local retail data. Keep local-client experiments manual unless minimal fixtures are added to generated test archives.

## Terrain And World Layout

| Format | Role | Key Data | References |
| --- | --- | --- | --- |
| `WDT` | Per-map world definition and 64x64 tile presence table. Can also reference a global WMO map. | `MVER`, `MPHD`, `MAIN`, sometimes `MWMO`/`MODF`, and modern `MAID`. | [wowdev WDT](https://wowdev.wiki/WDT), [wow-wdt docs.rs](https://docs.rs/wow-wdt), [WoTLK ADT/WDT/WDL overview](https://wotlkdev.github.io/wiki/theory/adt). |
| `ADT` | Terrain tile. One tile is about `533.333` yards/world units and contains a 16x16 grid of `MCNK` chunks. | `MCVT` heights, `MCNR` normals, `MCLY` texture layers, `MCAL` alpha maps, `MMDX`/`MMID`/`MDDF` doodads, `MWMO`/`MWID`/`MODF` WMO instances. | [wowdev ADT](https://wowdev.wiki/ADT), [wow-adt docs.rs](https://docs.rs/wow-adt), [wow-alchemy-adt docs.rs](https://docs.rs/wow-alchemy-adt), [WoTLK ADT/WDT/WDL overview](https://wotlkdev.github.io/wiki/theory/adt). |
| `WDL` | Low-resolution terrain used for far terrain/world map style data. | Coarse map height data; useful for distant terrain planning. | [wowdev WDL](https://wowdev.wiki/WDL), [warcraft-rs WDL support](https://github.com/wowemulation-dev/warcraft-rs). |
| `WLW` / liquids | Water and liquid data. Classic/TBC liquids are often in `MCLQ`; Wrath-era data moves toward `MH2O`. | Liquid type, flags, heights, UV/depth data depending on era. | [wowdev ADT liquid notes](https://wowdev.wiki/ADT), [wow_adt MCLQ docs](https://docs.rs/wow-adt/latest/wow_adt/chunks/mcnk/mclq/). |

`openwow` currently implements the WDT-to-ADT path, `MCNK` terrain chunks, `MCVT` heights, `MCLY` layers, `MCAL` alpha maps, doodad placement, and WMO placement enough for outdoor terrain experiments. Water/liquid rendering is still a future pass.

## World Map Objects

| Format | Role | Key Data | References |
| --- | --- | --- | --- |
| WMO root | Large buildings, caves, cities, dungeons, and other static world structures. | Root header, materials, texture names, group names/info, doodad sets, doodad names/definitions, lights, fog, portals. | [wowdev WMO](https://wowdev.wiki/WMO), [getMaNGOS WMO File](https://www.getmangos.eu/wiki/referenceinfo/clientfiles/wmo-file-r20030/), [wow-wmo docs.rs](https://docs.rs/wow-wmo). |
| WMO group | Per-group geometry for one part of a WMO. | `MOGP`, `MOPY`, `MOVI`, `MOVT`, `MONR`, `MOTV`, `MOBA`, optional `MOCV`, `MOBN`/`MOBR`, `MLIQ`, `MODR`. | [getMaNGOS WMO group chunk notes](https://www.getmangos.eu/wiki/referenceinfo/clientfiles/wmo-file-r20030/), [pywowlib WMO reader](https://github.com/wowdev/pywowlib/blob/master/wmo_file.py). |

Current renderer code loads enough WMO root/group data to create visible batches and counts missing groups. Collision, portals, fog, indoor lighting, liquids, doodad-set filtering, and exact material behavior are not complete.

## Models, Skins, And Animation

| Format | Role | Key Data | References |
| --- | --- | --- | --- |
| `M2` / `MD20` / `MD21` | Animated models for characters, creatures, items, spells, and small doodads. | Header arrays for sequences, bones, vertices, textures, materials, attachments, cameras, lights, particles, ribbons, and animation tracks. | [wowdev M2](https://wowdev.wiki/M2), [WoTLK M2 overview](https://wotlkdev.github.io/wiki/theory/m2), [wow-m2 docs.rs](https://docs.rs/wow-m2), [mangos classic M2 notes](https://github-wiki-see.page/m/Marzec737/mangos-classic/wiki/M2-files). |
| `.skin` | M2 view/LOD geometry companion. Does not mean character texture skin. | Indices, triangles, submeshes/skin sections, batches, bone lookup/palettes. | [wowdev M2 Skin](https://wowdev.wiki/SKIN), [wow-m2 docs.rs](https://docs.rs/wow-m2), `data/whoa-master/src/model/M2Data.hpp`. |
| `.anim` | External animation data used by later client eras and some models. | Animation tracks split out from the base model. | [wowdev M2](https://wowdev.wiki/M2), [wow-m2 README](https://docs.rs/crate/wow-m2/latest/source/README.md). |
| `.skel`, `.bone`, `.phys` | Later-era skeleton, bone, and physics companions. | Modern model support. | [pywowlib support table](https://github.com/wowdev/pywowlib), [wow-m2 docs.rs](https://docs.rs/wow-m2). |

### M2 effect-emitter version split

Classic/TBC (`MD20` version `<264`) particle records are `0x1f8` bytes: ten 28-byte vanilla tracks followed by static
BGRA lifecycle colors and scalar scales. They are not the later WotLK record with FBlocks. The emitter position is local to
its referenced bone, so particle and ribbon spines must be transformed by `model_matrix * bone_matrix` before emission.
`data/WoWee/src/pipeline/m2_loader.cpp` is the local reference for these offsets; `data/WoWee/src/rendering/m2_renderer_particles.cpp`
keeps the corresponding per-instance particle accumulators and ribbon edge state.

Vanilla v256 headers insert `playable_animation_lookup`, a full `views` array, and another array before `render_flags`.
Read material records from `m2HeaderLegacy_t.render_flags`, not the modern `materials` offset. WoW blend IDs are
`0=opaque`, `1=alpha-key`, `2=blend`, `3=add`, `4=add-alpha`, `5=mod`, and `6=mod2x`.

Particle `verticalRange` and `horizontalRange` spread a model-space `+Z` launch vector. They are ranges, not spherical
latitude/longitude angles; a zero range must therefore emit straight upward.

### M2 renderer schema convention

- Keep on-disk records in `renderer/m2/r_m2_format.h`; do not redeclare them in `r_m2.c`.
- Add a `_Static_assert` for every known record size. Modern ribbons are `0xac`; Classic ribbons are `0xe0`.
- Select all versioned record sizes through `m2_format_def(version)`, not subsystem-specific Classic booleans.
- Access offset arrays through `m2_array_ptr`/`m2_string_ptr` and versioned tracks through the helpers in
  `renderer/m2/r_m2_utils.h`. Do not add byte-offset walkers when the file record can name the field directly.
- Keep `r_m2.c` for owned runtime state, animation evaluation, rendering, and character composition.

Character-display specifics are scattered across model files and DBC tables. For our current classic-era work, the highest-value cross-checks are:

- `data/whoa-master/src/component/CCharacterComponent.cpp` for item component texture creation and geoset visibility prep.
- `data/whoa-master/src/component/Types.hpp` for component sections and geoset groups.
- `data/whoa-master/src/db/rec/*Rec.hpp` for DBC record shape assumptions.
- [wowdev M2 AnimationList](https://wowdev.wiki/M2/AnimationList) for animation IDs/names.

## Textures And Images

| Format | Role | Key Data | References |
| --- | --- | --- | --- |
| `BLP1` | Earlier Blizzard texture format, mostly relevant to Warcraft III and older assets. | Header, palette/JPEG/raw variants. | [warcraft-rs BLP docs](https://warcraft-rs.readthedocs.io/en/latest/formats/graphics/blp.html), local `tools/blp2jpg.c`. |
| `BLP2` | World of Warcraft texture format. | Header, up to 16 mipmaps, palette block, raw/paletted/BGRA and DXT1/DXT3/DXT5 style compression paths depending on header fields. | [Warcraft Wiki BLP files](https://warcraft.wiki.gg/wiki/BLP_files), [warcraft-rs BLP docs](https://warcraft-rs.readthedocs.io/en/latest/formats/graphics/blp.html), [AddOn Studio BLP file](https://addonstudio.org/wiki/WoW%3ABLP_file). |
| Component textures | Item texture fragments pasted into character body atlases. | Slot folders under `Item\TextureComponents\...`, gender/universal suffixes, 512x512 reference rectangles that may need scaling to the actual body texture. | `data/whoa-master/src/component/CCharacterComponent.cpp`, `docs/games/world-of-warcraft/m2-and-character-display.md`. |

OpenWarcraft3 already has small BLP helpers in `tools/blp2jpg.c`, `tools/blpgen.c`, and `tools/mpqtool.c`. Prefer extending those with explicit header validation over open-coded texture parsing in renderer code.

## Client Databases

| Format | Role | Key Data | References |
| --- | --- | --- | --- |
| `DBC` / `WDBC` | Classic through Wrath client database tables. | Record count, field count, record size, string block size, fixed-size records, string offsets. | [Warcraft Wiki DBC](https://warcraft.wiki.gg/wiki/DBC), [wowdev DBC](https://wowdev.wiki/DBC), [WoTLK DBC index](https://wotlkdev.github.io/wiki/dbc/), [wow_dbc crate](https://github.com/gtker/wow_dbc). |
| `DB2` / `WDB2+` | Cataclysm and later client database evolution. | Versioned layouts, sparse/indexed sections, locale/string changes depending on era. | [wowdev DB2](https://wowdev.wiki/DB2), [wowdev DBChanges](https://wowdev.wiki/DBChanges), [WoWDBDefs](https://github.com/wowdev/WoWDBDefs), [DBCD](https://github.com/wowdev/DBCD). |
| `WDB` | Client cache data received from servers. | Versioned cache headers and per-cache signatures. Mostly not needed for offline rendering. | [wowdev WDB](https://wowdev.wiki/WDB), [Warcraft Wiki WDB files](https://warcraft.wiki.gg/wiki/WDB_files). |

Current high-value DBCs for this target:

| DBC | Why It Matters |
| --- | --- |
| `Map.dbc` | Map IDs, names, and map directory names. |
| `WorldSafeLocs.dbc` | Spawn/safe-location lookup. |
| `CharStartOutfit.dbc` | Starter outfit item display IDs by race/class/gender. |
| `ItemDisplayInfo.dbc` | Item model names, texture stems, geoset groups, flags, helmet visibility, and character texture component slots. |
| `CharSections.dbc` | Base skin, face, hair, facial hair, underwear texture variants and flags. |
| `CharHairGeosets.dbc` | Hair style to geoset selection. |
| `CharHairTextures.dbc` | Hair texture variants. |
| `HelmetGeosetVisData.dbc` | Helmet visibility masks. |
| `CreatureModelData.dbc` | Creature model file references and model data. |
| `CreatureDisplayInfo.dbc` / `CreatureDisplayInfoExtra.dbc` | Creature display records, model display variants, extra character-like appearance data. |

Do not assume `field_count * 4 == record_size`. Some classic-era files have a logical field count larger than the physical number of 32-bit fields in each record. Validate the header envelope, then bounds-check each accessed field against `record_size`.

Per-table field layouts and packed appearance/equipment values as consumed by `openwow` live in
[`docs/dbc-reference.md`](dbc-reference.md).

## UI, Scripts, Audio, And Miscellaneous Files

| Format | Role | References |
| --- | --- | --- |
| FrameXML / Lua / TOC / XML | Client UI definitions and scripts. Mostly outside the current `openwow` renderer path, except for future UI exploration. | [Warcraft Wiki UI tech](https://warcraft.wiki.gg/wiki/World_of_Warcraft_API), local `data/whoa-master/src/ui`. |
| `WTF` / config files | User settings and addon saved data. Not useful for asset rendering. | [Warcraft Wiki WTF folder](https://warcraft.wiki.gg/wiki/WTF). |
| `WAV` / `MP3` / `OGG` | Audio payloads referenced by DBCs, models, zones, and UI. | `SoundEntries.dbc`, `SoundEntriesAdvanced.dbc`, `data/whoa-master/src/sound` if/when audio matters. |
| `LST` | List metadata occasionally found in patches. | [wowdev Lst](https://wowdev.wiki/Lst). |

## Implementation Checklist

When adding support for another WoW file feature:

1. Identify the client era and archive source first.
2. Find the corresponding wowdev page, a parser implementation, and one higher-level modding explanation when possible.
3. Inspect the local MPQ data with `mpqtool` and keep path/listfile assumptions explicit.
4. Keep on-disk structs separate from runtime structs; do not use runtime `sizeof` as serialized record size.
5. Put WoW-specific policy under `games/world-of-warcraft/`, not in engine modules.
6. Prefer narrow, version-aware helpers over broad speculative format support.
