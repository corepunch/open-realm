# war3map.w3e — Terrain / Environment

The terrain file stores the complete heightmap and tileset data for the map. The map is a grid of **tiles**; each tile has 4 corners called **tilepoints**. All geometry derives from tilepoints, not tile centres.

For a map of size `W×H` tiles there are `(W+1)×(H+1)` tilepoints, stored row by row from the **bottom-left** corner going right then up (when viewed from above).

## File version

| Version | WC3 era |
|---------|---------|
| 11 | Classic RoC, TFT, pre-Reforged, Reforged 1.32 |
| 12 | Reforged 2.0+ |

## Header

```
char[4]  file ID              "W3E!"
int      version              0x0B = 11 (classic/TFT/Reforged), 0x0C = 12 (2.0+)
char     main tileset         single character (see table below)
int      custom tilesets      1 = using custom tilesets, 0 = standard
int      a                    number of ground tilesets used  (max 16)
char[4][a]  ground tileset IDs   e.g. "Ldrt" = Lordaeron Summer Dirt
int      b                    number of cliff tilesets used   (max 16)
char[4][b]  cliff tileset IDs    e.g. "CLdi" = Lordaeron Cliff Dirt
int      Mx                   map width  + 1  (number of tilepoint columns)
int      My                   map height + 1  (number of tilepoint rows)
float    center offset X      typically -(Mx-1)*128/2
float    center offset Y      typically -(My-1)*128/2
```

### Main tileset codes

| Code | Tileset |
|------|---------|
| `A` | Ashenvale |
| `B` | Barrens |
| `C` | Felwood |
| `D` | Dungeon |
| `F` | Lordaeron Fall |
| `G` | Underground |
| `I` | Icecrown |
| `J` | Dalaran Ruins |
| `K` | Black Citadel |
| `L` | Lordaeron Summer |
| `N` | Northrend |
| `O` | Outland |
| `Q` | Village Fall |
| `V` | Village |
| `W` | Lordaeron Winter |
| `X` | Dalaran |
| `Y` | Cityscape |
| `Z` | Sunken Ruins |

Ground tileset IDs are found in `TerrainArt\Terrain.slk` inside `war3.mpq`.  
Cliff tileset IDs are found in `TerrainArt\CliffTypes.slk`.  
The cliff tile list in the header is largely informational — the engine derives it from the ground tile list automatically.

### Center offsets

The in-game coordinate origin `(0,0)` is placed at the map center. The offsets translate the tilepoint grid (whose raw origin is bottom-left) to this centered space:

```
center_offset_x = -(Mx - 1) * 128 / 2
center_offset_y = -(My - 1) * 128 / 2
```

One tile = 128 world-space units.

## Tilepoint data (7 bytes each)

Total tilepoints = `Mx * My`. Each tilepoint:

```
short   ground height       raw value; see formula below
short   water level + flag  high bit (0x8000) = boundary flag 1; low 15 bits = water level
nibble  flags               upper 4 bits of next byte (see flag table)
nibble  ground texture      lower 4 bits — index into ground tileset table
byte    texture details     sub-tile detail texture (rocks, bones, holes, etc.)
nibble  cliff texture       upper 4 bits of last byte — index into cliff tileset table
nibble  layer height        lower 4 bits — cliff layer (0..N)
```

> **Byte layout:** bytes 3–4 are a `short` for water level. Byte 5 packs flags (high nibble) and ground texture (low nibble). Byte 6 is texture detail. Byte 7 packs cliff texture (high nibble) and layer height (low nibble).

### Ground height formula

```
display_height = (ground_height - 0x2000 + (layer - 2) * 0x0200) / 4
```

- `0x2000` (8192) = ground zero level
- Layer 2 = layer zero; each layer step = `0x0200` (512) raw units
- Range: `0xC000` (min, −16384) to `0x3FFF` (max, +16383); normal = `0x2000`

### Water level formula

```
display_water_level = (water_level - 0x2000) / 4 - 89.6
```

- `−89.6` = water zero (from `Water.slk`, height `−0.7`, scaled by 128)

### Tilepoint flags (4-bit field, shown as 16-bit positions in the short)

| Bit (in the full 2-byte group) | Value | Meaning |
|-------------------------------|-------|---------|
| Bit 15 of water short | `0x8000` / `0x4000` | Boundary flag 1 (WE shadow on map edge) |
| Bit 4 of flag nibble | `0x0010` | Ramp flag |
| Bit 5 of flag nibble | `0x0020` | Blight flag (Undead ground appearance) |
| Bit 6 of flag nibble | `0x0040` | Water flag (enables water rendering) |
| Bit 7 of flag nibble | `0x0080` | Boundary flag 2 (camera bounds area) |

### Example tilepoint decode

Raw bytes: `51 21 00 62 56 84 13`

```
ground height: 0x2151 = 8529
  → display = (8529 - 8192 + 1*512) / 4 = 212.25

water short:   0x6200
  boundary flag 1: 0x6200 & 0xC000 = 0x4000 → SET
  water level:     0x6200 & 0x3FFF = 0x2200 = 8704
  → display water = 8704/4 - 89.6 = 2086 - 89.6 = 38.4   (below ground, invisible)

byte 0x56:
  flags nibble = 5 → water flag (0x40) + ramp flag (0x10) both set
  ground texture = 6 → ground tileset index 6

byte 0x84:
  texture detail = 0x84 = 132

byte 0x13:
  cliff texture = 1 → cliff tileset index 1
  layer height  = 3
```

## References

- Ground texture IDs: `TerrainArt\Terrain.slk` in `war3.mpq`
- Cliff texture IDs: `TerrainArt\CliffTypes.slk` in `war3.mpq`
- Water zero level: `TerrainArt\Water.slk`
- Version 12 changes (Reforged 2.0): see ChiefOfGxBxL/WC3MapSpecification `Terrain/12.md`
