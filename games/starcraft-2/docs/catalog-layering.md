# SC2 Catalog Dependency Layering

This document describes how StarCraft II resolves catalog data across dependency mods, loose/global files, and map-local overrides. It is the authoritative reference for the catalog loading order implemented in `sc2_map.c`.

## SC2 Standard Dependency Chain

Every SC2 map depends on one or more mods. The dependency chain forms a tree where later mods override earlier ones for the same catalog `id`. The full Wings of Liberty chain (the minimum supported by OpenWarcraft3):

```text
Core.SC2Mod                          ← base game data (432 units, 2042 models)
  └─ Liberty.SC2Mod                  ← WoL multiplayer balance
       ├─ LibertyMulti.SC2Mod        ← WoL post-launch patches
       └─ LibertyCampaign            ← WoL campaign assets (950 units, 2150 models)
```

Extended chains for Heart of the Swarm and Legacy of the Void:

```text
Liberty.SC2Mod
  └─ Swarm.SC2Mod                    ← HotS multiplayer
       ├─ SwarmMulti.SC2Mod          ← HotS post-launch patches
       ├─ SwarmCampaign              ← HotS campaign
       └─ Void.SC2Mod                ← LotV multiplayer
            ├─ VoidMulti.SC2Mod      ← LotV post-launch patches
            └─ VoidCampaign          ← LotV campaign
```

Each dependency only adds or overrides entries. The base `Core.SC2Mod` provides all foundational data; later mods refine it.

Source: [SC2Mapster Standard Dependencies](https://github.com/SC2Mapster/blizzard-tutorials/blob/master/docs/New_Tutorials/01_Introduction/013_Standard_Dependencies.md)

## Catalog Loading Order

SC2 uses a **last-write-wins** model. When multiple sources define the same catalog `id`, the last one loaded takes precedence. The canonical load order is:

1. **Dependency catalogs** (Core → Liberty → LibertyMulti → Campaigns) — loaded in dependency order, earliest first
2. **Loose/global catalogs** (`""` root) — fallback data not tied to a specific mod
3. **Map-local catalogs** (from the map's own `Base.SC2Data/GameData/`) — loaded last, so map data wins

This means a map can override any catalog entry from its dependencies by defining the same `id` in its own `Base.SC2Data/GameData/*.xml` files.

### Why Order Matters

Consider a Marine unit defined in three places:

| Source | `id` | `Radius` |
|--------|------|----------|
| Core.SC2Mod | Marine | 0.5 |
| Liberty.SC2Mod | Marine | 0.75 |
| Map-local | Marine | 1.0 |

With correct layering, the map sees `Radius = 1.0` (last wins). With wrong order (e.g., loose before deps), the map might see `0.5` or `0.75` depending on which loaded last.

## GameData.xml Includes Mechanism

SC2 mods and maps use a two-level file discovery system:

**Top-level** `Base.SC2Data/GameData.xml`:
```xml
<?xml version="1.0" encoding="us-ascii"?>
<Includes>
    <Catalog path="GameData/SC2Data.xml"/>
</Includes>
```

**Inner manifest** `Base.SC2Data/GameData/GameData.xml` (or `SC2Data.xml`):
```xml
<?xml version="1.0" encoding="us-ascii"?>
<Catalog>
    <CGame default="1">
        <!-- game-wide settings -->
    </CGame>
    <CUnit id="Marine" ...>
        <!-- unit definition -->
    </CUnit>
    <!-- ... 400+ lines of catalog entries ... -->
</Catalog>
```

The `<Includes>` mechanism tells the editor which XML files to parse as catalog data. Maps can add custom data spaces by creating new XML files under `Base.SC2Data/GameData/` and registering them in `GameData.xml`.

Source: [SC2Mapster Data Spaces](https://s2editor-guides.readthedocs.io/New_Tutorials/04_Data_Editor/data-spaces/), [Talv/sc2-data](https://github.com/Talv/sc2-data)

## Catalog Inheritance Model

SC2 catalogs support inheritance through the `parent` attribute:

```xml
<CModel id="MarineCatalogModel" parent="Unit" Race="Terran">
    <Occlusion value="Show"/>
</CModel>
```

Here `MarineCatalogModel` inherits from the `Unit` base model, which defines the path template `Assets\Units\##Race##\##id##\##id##.m3`. The `##Race##` and `##id##` placeholders are substituted with the child's values.

### Inheritance Rules

- The `parent` chain can be up to `SC2_MAX_CATALOG_PARENT_DEPTH` (8) levels deep
- Child entries only need to specify fields that differ from the parent
- The `id` attribute is the unique key; `parent` is the template to inherit from
- Placeholder substitution: `##id##` → entry's `id`, `##Race##` → entry's `Race` attribute

### Editor Behavior

The Galaxy Editor's "Show Field Differences" mode highlights what a mod overrides versus its parent. Best practice from SC2Mapster guides:

- "Always set `parent` to the closest matching base — inherit as much as possible"
- "Only override the fields that differ from the parent"

Source: [SC2 Data Editor Skills](https://skillsmp.com/skills/kimplaybit-starcraft-2-editor-skills-agents-skills-sc2data-units-abilities-skill-md)

## Map-Local Override Pattern

Maps store catalog overrides under `Base.SC2Data/GameData/` inside their own archive (MPQ or `.SC2Components` directory). The loading code reads these files from the open map source:

```c
// Read from map archive
xmlDocPtr doc = sc2_read_xml(source, "Base.SC2Data/GameData/ModelData.xml");
// Try backslash variant
if (!doc) doc = sc2_read_xml(source, "Base.SC2Data\\GameData\\ModelData.xml");
```

This is the same pattern used by `sc2_parse_terrain_data()` and `sc2_parse_light_data()` — they read map-local TerrainData.xml and LightData.xml from the open source after parsing the dependency catalog versions.

## Implementation in sc2_map.c

### Catalog Roots

The shared dependency root list is defined as file-static:

```c
static LPCSTR const sc2_catalog_roots[] = {
    "Mods/Core.SC2Mod/Base.SC2Data",
    "Mods/Liberty.SC2Mod/Base.SC2Data",
    "Mods/LibertyMulti.SC2Mod/Base.SC2Data",
    "Campaigns/LibertyStory.SC2Campaign/Base.SC2Data",
    "Campaigns/Liberty.SC2Campaign/Base.SC2Data",
    NULL,
};
```

### Parser Architecture

Each catalog type has three function variants:

| Variant | Purpose | I/O Source |
|---------|---------|------------|
| `_doc(catalog, doc)` | XML walking logic, decoupled from I/O | Pre-parsed `xmlDocPtr` |
| `_file(catalog, root)` | Reads from game data archives | `sc2_read_catalog_xml(root, filename)` |
| `_source(catalog, source)` | Reads from open map archive | `sc2_read_map_catalog_xml(source, filename)` |

### Load Order in sc2_parse_catalogs

```c
static void sc2_parse_catalogs(sc2Catalog_t *catalog, sc2MapSource_t *source) {
    // 1. Dependency catalogs (earliest = lowest precedence)
    for (DWORD i = 0; sc2_catalog_roots[i]; i++) {
        sc2_parse_unit_catalog_file(catalog, sc2_catalog_roots[i]);
        sc2_parse_model_catalog_file(catalog, sc2_catalog_roots[i]);
        sc2_parse_actor_catalog_file(catalog, sc2_catalog_roots[i]);
        sc2_parse_terrain_tex_catalog_file(catalog, sc2_catalog_roots[i]);
        sc2_parse_cliff_catalog_file(catalog, sc2_catalog_roots[i]);
    }
    // 2. Loose/global fallback
    sc2_parse_unit_catalog_file(catalog, "");
    sc2_parse_model_catalog_file(catalog, "");
    sc2_parse_actor_catalog_file(catalog, "");
    sc2_parse_terrain_tex_catalog_file(catalog, "");
    sc2_parse_cliff_catalog_file(catalog, "");
    // 3. Map-local overrides (highest precedence)
    sc2_parse_unit_catalog_source(catalog, source);
    sc2_parse_model_catalog_source(catalog, source);
    sc2_parse_actor_catalog_source(catalog, source);
    sc2_parse_terrain_tex_catalog_source(catalog, source);
    sc2_parse_cliff_catalog_source(catalog, source);
}
```

### Catalog Resolution in SC2_MapLoad

The map source must remain open during catalog resolution:

```c
sc2_resolve_catalogs(&source);   // parse catalogs, resolve models/textures/cliffs
sc2_source_close(&source);       // close map archive after resolution
```

## Upsert Semantics

All `sc2_catalog_add_*` functions use upsert (update-on-match):

1. Linear scan for existing entry with matching `id` (case-insensitive)
2. If found: update fields in place
3. If not found and within capacity: append new entry

This means later loads override earlier ones for the same `id`, which is the correct SC2 behavior.

## References

| Resource | URL |
|----------|-----|
| SC2 Standard Dependencies (Blizzard) | https://github.com/SC2Mapster/blizzard-tutorials/blob/master/docs/New_Tutorials/01_Introduction/013_Standard_Dependencies.md |
| SC2 Data Spaces guide | https://s2editor-guides.readthedocs.io/New_Tutorials/04_Data_Editor/data-spaces |
| SC2ModKit (JS catalog loader) | https://github.com/star-tools/modkit |
| SC2 GameData Documentation | https://github.com/chansey97/sc2-gamedata-documentation |
| SC2Mapster community | https://sc2mapster.github.io/mkdocs/data/ |
| Talv sc2-data (Core.SC2Mod structure) | https://github.com/Talv/sc2-data/tree/master/mods/core.sc2mod/base.sc2data |
