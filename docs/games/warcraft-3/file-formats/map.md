# Warcraft III Map Format (W3M / W3X)

Warcraft III maps are stored as MPQ archives with a `.w3m` (melee/campaign) or `.w3x` (custom scenario) extension. Inside each archive a set of binary and text files describes the map's terrain, units, triggers, strings, and metadata.

## Archive Layout

A typical map archive contains the following files:

| File | Description |
|------|-------------|
| `war3map.w3i` | Map info (name, author, players, camera bounds) |
| `war3map.w3e` | Terrain environment (tile types, heights, cliff levels) |
| `war3map.doo` | Doodad placement |
| `war3map.w3u` | Unit object data overrides |
| `war3map.w3t` | Item object data overrides |
| `war3map.w3b` | Destructible object data overrides |
| `war3map.w3d` | Doodad object data overrides |
| `war3map.w3a` | Ability object data overrides |
| `war3map.w3q` | Upgrade object data overrides |
| `war3map.shd` | Shadow map |
| `war3map.wpm` | Pathing map |
| `war3map.j` | JASS script (map triggers) |
| `war3map.lua` | Lua script (maps saved in v1.31+) |
| `(listfile)` | Archive file list |

## Map Info File (`war3map.w3i`)

This is the first file read at world load time (`CM_ReadInfo` in `common/world.c`). It is a flat binary record with the following layout:

```
DWORD  fileFormat          // map format version
DWORD  numberOfSaves       // incremented by the World Editor each save
DWORD  editorVersion       // World Editor build number
CSTR   mapName             // null-terminated display name
CSTR   mapAuthor
CSTR   mapDescription
CSTR   playersRecommended
...
FLOAT[8]  cameraBounds     // playable camera extents
SIZE2     playableArea     // width × height in tiles
DWORD     flags
CHAR      mainGroundType   // e.g. 'A' = Ashenvale, 'L' = Lordaeron
...
DWORD  num_players
[playerRecord × num_players]
DWORD  num_forces
[forceRecord × num_forces]
...
```

Each `playerRecord` contains:
- player slot index
- player type (human / computer / neutral / rescuable)
- player race (human / orc / undead / night elf)
- flags (fixed start position, etc.)
- player name (null-terminated string)
- starting position (X, Y floats)
- ally priority flags

## Terrain File (`war3map.w3e`)

The terrain file encodes a grid of vertices. Each vertex stores:

| Field | Type | Description |
|-------|------|-------------|
| `height` | `SHORT` | Ground height (scaled by 4) |
| `waterHeight` | `SHORT` | Water surface height |
| `flags` | `BYTE` | Ramp / boundary / water presence bits |
| `groundTextures[4]` | `BYTE[4]` | Indices into the tileset texture table |
| `cliffTexture` | `BYTE` | Cliff face texture index |
| `layerHeight` | `BYTE` | Cliff level (0 = sea floor, 1 = base, 2 = raised, …) |

The Warcraft III renderer (`games/warcraft-3/renderer/w3m/`) reads each vertex and constructs mesh layers:

- **Ground layer** — a quad grid textured with the four ground texture slots, blended by the per-vertex weights.
- **Cliff layer** — vertical faces extruded between adjacent vertices at different cliff levels.
- **Water layer** — a translucent quad grid rendered wherever the water-presence bit is set.

### Height Calculation

```c
// games/warcraft-3/renderer/w3m/r_war3map_utils.c
float GetWar3MapVertexHeight(LPCWAR3MAPVERTEX vert) {
    return (vert->height - 0x2000) / 4.0f;
}
float GetWar3MapVertexWaterLevel(LPCWAR3MAPVERTEX vert) {
    return (vert->waterHeight - 0x2000) / 4.0f;
}
```

The raw `SHORT` is biased by `0x2000` (8192) to allow negative heights, then divided by 4 to give world-space units.

## Doodad File (`war3map.doo`)

Doodads are decorative or destructible objects placed in the editor. Each record encodes:

- object type ID (4-byte tag, e.g. `'ATtr'`)
- variation index
- world-space X, Y, Z position
- rotation angle (radians)
- scale X, Y, Z
- skin override flags and life percentage

## Pathing Map (`war3map.wpm`)

A 2-bit-per-cell grid at 1/4 tile resolution. Each cell encodes walkability, flyability, buildability, and blight status. The Warcraft III pathfinder (`games/warcraft-3/game/g_pathing.c`) reads this map to determine which cells units can traverse.

## Object Data Overrides

Files such as `war3map.w3u` (units) override individual fields of the base SLK tables. The format is:

```
DWORD  version
DWORD  num_original_records
[original record × num_original_records]
DWORD  num_custom_records
[custom record × num_custom_records]
```

Each record contains a four-character unit ID followed by a list of `(field_tag, value)` pairs, terminated by a zero DWORD.

## Related Source Files

| Source | Purpose |
|--------|---------|
| `common/world.c` | Map archive loading and info parsing |
| `games/warcraft-3/renderer/w3m/r_war3map.c` | Terrain mesh construction |
| `games/warcraft-3/renderer/w3m/r_war3map_ground.c` | Ground layer geometry |
| `games/warcraft-3/renderer/w3m/r_war3map_cliffs.c` | Cliff geometry |
| `games/warcraft-3/renderer/w3m/r_war3map_water.c` | Water layer geometry |
| `games/warcraft-3/game/g_pathing.c` | Pathfinding using the pathing map |
