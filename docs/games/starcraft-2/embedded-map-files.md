# Embedded Map Files

Full binary specifications for all known embedded files in `.SC2Map` archives.
Sources: SC2Mapster wiki (https://sc2mapster.wiki.gg/wiki/File_Formats/Maps), RFEphemeration/sc2-map-analyzer source, and cross-verification against the engine parser in `games/starcraft-2/common/sc2_map.c`.

See [file-formats/mapinfo.md](file-formats/mapinfo.md) for the full `MapInfo` struct.
See [file-formats/objects.md](file-formats/objects.md) for the full `Objects` XML schema.

## File Index

| File | Magic | Format | Status |
| --- | --- | --- | --- |
| `MapInfo` | `MapI` | binary struct | parsed |
| `Objects` | — | XML | parsed |
| `t3Terrain.xml` | — | XML | parsed |
| `t3HeightMap` | `HMAP` | binary | parsed |
| `t3SyncCliffLevel` | `CLIF` | binary | parsed |
| `t3CellFlags` | `LFCT` | binary | parsed |
| `t3TextureMasks` | `MASK` | binary | parsed |
| `t3SyncHeightMap` | `SMAP` | binary | parsed |
| `t3SyncPathingInfo` | `PATH` | binary | **not yet parsed** |
| `t3Water` | `WATR` | binary | **not yet parsed** |
| `t3FluffDoodad` | `DLFT` | binary | **not yet parsed** |
| `t3HardTile` | `HRDT` | binary | **not yet parsed** |
| `t3VertCol` | — | binary | unknown layout |
| `PaintedPathingLayer` | — | binary | partially understood |
| `Terrain` | — | binary | sparse |

---

## `MapInfo`

See [file-formats/mapinfo.md](file-formats/mapinfo.md) for the complete struct with all fields.

---

## `Objects`

See [file-formats/objects.md](file-formats/objects.md) for the full `<PlacedObjects>` XML schema.

---

## `t3Terrain.xml`

XML file. Root element is `<terrain version="N">`. The primary child is `<heightMap tileSet="..." dim="W H">`.

```xml
<terrain version="113">
    <heightMap tileSet="TileSetName" dim="W H">
        <masks name="t3TextureMasks"/>
        <vertData
            quantizeBias="1.0"
            quantizeScale="0.5"
            standardHeight="2.0"
            name="t3HeightMap"/>
        <cliffSetList num="N">
            <cliffSet i="0" name="CliffTypeID"/>
        </cliffSetList>
        <textureList num="N">
            <texture i="0" name="XilDirt"/>
            <texture i="1" name="XilSand"/>
        </textureList>
        <cliffCellList num="N" numOccupied="N">
            <cc i="flatIndex" f="flags" cid="cliffSetIndex" cvar="variant"/>
        </cliffCellList>
    </heightMap>
</terrain>
```

Key attributes:

| Attribute | Location | Meaning |
| --- | --- | --- |
| `quantizeBias` | `<vertData>` | Subtracted in height decode formula |
| `quantizeScale` | `<vertData>` | Multiplier converting raw uint16 sum to world units |
| `standardHeight` | `<vertData>` | Additional subtraction (reference height level) |
| `cliffSet i` | `<cliffSet>` | Cliff set index |
| `cliffSet name` | `<cliffSet>` | Cliff type catalog ID (resolved via `CliffData.xml`) |
| `texture i` | `<texture>` | Texture layer index |
| `texture name` | `<texture>` | Texture name or path; `.dds` extension added if absent |
| `cc i` | `<cc>` | Flat row-major index into half-resolution cliff grid |
| `cc f` | `<cc>` | Flags: bit 0 = active, bit 1 = ramp indicator |
| `cc cid` | `<cc>` | Index into `<cliffSetList>` |
| `cc cvar` | `<cc>` | Rotation variant (0–3), selects pre-rotated model file `_00`–`_03` |

Cliff cell grid: one entry per 2×2 terrain-cell block. `cliff_width = (map_width + 1) / 2`. Grid position from index: `cell_x = i % cliff_width`, `cell_y = i / cliff_width`.

---

## `t3HeightMap` (magic `HMAP`)

Source: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3HeightMap

**Header — 32 bytes:**

```c
struct {
    char   magic[4];    // "HMAP"
    uint32 version;     // 101
    uint32 sizeX;       // vertex count = map cell width + 1
    uint32 sizeY;       // vertex count = map cell height + 1
    uint8  padding[16];
} HEADER;
```

**Data** starts at byte 32: `sizeX * sizeY` records of 6 bytes each, row-major, Y=0 at world bottom (south edge):

```c
typedef struct {
    uint16 heightAdjustments;  // small dents/jitters
    uint16 heightBase;         // plateau heights
    uint16 mask;               // 0x00–0x03 only
} CHUNK;
```

Note: the wiki lists `heightAdjustments` first, `heightBase` second. The engine reads `heightBase` at offset +0 and `heightAdjustments` at +2. The formula adds them so the order does not affect the decoded height.

**Height decode formula** (parameters from `t3Terrain.xml`):

```
height = (heightAdjustments + heightBase) * quantizeScale
         - quantizeBias - standardHeight - 1
```

**Standard cliff elevations:**

| Raw combined value | World height level |
| --- | --- |
| 0 | cliff floor |
| 8 | bottom level |
| 10 | middle level |
| 12 | highest level |

**Encoding (reverse formula):**
```
combined = (height + quantizeBias + standardHeight + 1) / quantizeScale
```
Result must be ≤ `2 × 0xFFFF`. Split freely between `heightBase` and `heightAdjustments`.

**Dimension confirmed by test:** 4×3-cell map → `height_map_width=5, height_map_height=4`.

---

## `t3SyncCliffLevel` (magic `CLIF`)

Source: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3SyncCliffLevel

**Header — 32 bytes:**

```c
typedef struct {
    DWORD magic;    // "CLIF"
    UINT  version;  // 100
    UINT  sizeX;    // equals map cell width (NOT width+1)
    UINT  sizeY;    // equals map cell height
    DWORD zero[4];
} HEADER;
```

**Data** starts at byte 32: `sizeX * sizeY` `USHORT` values, row-major, Y=0 at world bottom.

Each value is the integer cliff level for that cell. Decode:
- `value < 0x40` → level = value directly
- `value >= 0x40` → level = `value >> 6` (level packed into upper 10 bits)

Typical values: 1 (low ground), 8–12 (cliff tiers). SC2 supports 4 distinct height tiers (levels differ by ~2 each).

**Dimension confirmed by test:** 4×3-cell map → `cliff_level_width=4, cliff_level_height=3`.

**Corner sampling for cliff config:** A 2×2-cell cliff block starting at cell `(cx, cy)` samples the four CLIF entries at `(cx, cy)`, `(cx+1, cy)`, `(cx, cy+1)`, `(cx+1, cy+1)`.

---

## `t3CellFlags` (magic `LFCT`)

Source: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3CellFlags

**Header — 32 bytes:**

```c
struct {
    DWORD magic;    // "LFCT"
    UINT  version;  // 101
    DWORD zero[4];  // four reserved DWORDs  ← come BEFORE sizeX/sizeY
    UINT  sizeX;    // at byte offset 24
    UINT  sizeY;    // at byte offset 28
} HEADER;
```

**Data** starts at byte 32: `sizeX * sizeY` `BYTE` values, row-major.

The lower nibble carries cliff/ramp type; the upper nibble carries additional flags not fully documented. Real maps use values beyond 0x03 (e.g. `0x10`, `0x1b`). Check `(value & 0x0f)` when testing for cliff holes.

| Lower nibble | Color (SC2 Map Analyzer) | Meaning |
| --- | --- | --- |
| `0x0` | Black | Normal ground |
| `0x1` | Red | Ramp-adjacent; exact effect unclear |
| `0x2` | Yellow | Unconfirmed |
| `0x3` | Green | **Cliff hole** — terrain geometry suppressed; cliff model sits here without Z-fighting. Units fall through visually but path normally. |

The wiki states this file does not influence pathing. Use `t3SyncPathingInfo` for movement/building pathing.

`MapInfo` dimensions are the authoritative map size. `t3CellFlags` keeps its own file-shaped layer dimensions.

---

## `t3TextureMasks` (magic `MASK`)

Source: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3TextureMasks

**Header — 64 bytes:**

```c
typedef struct {
    DWORD magic;     // "MASK"
    UINT  version;   // 102
    DWORD unk;       // unknown
    UINT  sizeX;
    UINT  sizeY;
    DWORD zero[11];  // reserved
} HEADER;
```

**Data** follows immediately: blocks of `64 × 32 = 2048` bytes each.

```c
typedef struct {
    BYTE data[64 * 32];
} BLOCK;

BLOCK blocks[(FileSize - 64) / (64 * 32)];
```

**Layer count:**
```
numLayers = (FileSize - 64) / (sizeX * sizeY / 2)
```

**Nibble encoding:** Each byte stores two 4-bit texture layer indices (0–15):
- High nibble (`byte >> 4`) is the first pixel
- Low nibble (`byte & 0x0F`) is the second pixel

**Block layout:** Blocks advance in the +X direction; a new row starts after `sizeX / 64` blocks. Each block covers a 64×64-pixel region of the map at one texture layer.

**Texture assignment:** Each layer index maps to the texture at that index in `t3Terrain.xml`'s `<textureList>`.

---

## `t3SyncHeightMap` (magic `SMAP`)

Source: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3SyncHeightMap

Parsed by the engine and added to the decoded `t3HeightMap` values.

**Header:**

```c
struct {
    char   magic[4];    // "SMAP"
    uint32 version;     // 102
    uint32 sizeX;
    uint32 sizeY;
    byte   padding[12];
    uint32 unknown[3];
    byte   padding2[24];
} HEADER;
```

**Data:** `sizeX * sizeY` records of 4 bytes each:

```c
typedef struct {
    int16  height;  // signed! relative offset
    uint16 mask;
} CHUNK;
```

**Formula:** `level_offset = height / 256.0`

This is a fine-detail layer on top of `t3HeightMap` — small bumps, hills, and potentially in-game terrain deformations (e.g. from Ultralisks). Layer on top of the main height after decoding `t3HeightMap`.

---

## `t3SyncPathingInfo` (magic `PATH`)

Source: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3SyncPathingInfo

**Not yet parsed by the engine.**

**Header:**

```c
struct HEADER {
    char    magic[4];   // "PATH"
    uint32  version;    // 100
    uint32  sizeX;
    uint32  sizeY;
    uint8   padding[16];
};
```

**Data:** `sizeX * sizeY` `uint16` values:

| Value | Meaning |
| --- | --- |
| `0x0000` | Passable terrain |
| `0x0002` | Ramp |
| `0x0023` | Impassable terrain |

Preferred over `t3CellFlags` for movement/building pathing. The wiki notes that setting all values to `0x00` produced no observable effect in single-player testing, so runtime use may differ.

---

## `t3Water` (magic `WATR`)

Source: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3Water

**Not yet parsed by the engine.**

**Header:**

```c
struct {
    DWORD magic;    // "WATR"
    UINT  version;  // 110
    UINT  amount;   // number of water rectangles
    DWORD zero[5];
} HEADER;
```

**Data:** `amount` records of 56 bytes each:

```c
typedef struct {
    CHAR  template[40]; // name of <CWater id="..."> in WaterData.xml (39 chars + null)
    FLOAT startX;
    FLOAT startY;
    FLOAT endX;
    FLOAT endY;
} CHUNK;
```

The `template` string references a `<CWater>` entry in `Base.SC2Data/GameData/WaterData.xml`, which controls color, height, refraction, and type. Lava uses `<IsLava value="1"/>` plus associated shoreline definitions.

Water fills the specified rectangle unnaturally (no flow simulation); it clips abruptly at the boundary coordinates.

---

## `t3FluffDoodad` (magic `DLFT`)

Source: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3FluffDoodad

**Not yet parsed by the engine.**

**Header:**

```c
struct {
    DWORD magic;    // "DLFT"
    UINT  version;  // 103
    DWORD zero[4];
    UINT  amount;
} HEADER;
```

**Data:** `amount` records of 9 bytes each:

```c
typedef struct {
    FLOAT x;     // world X
    FLOAT y;     // world Y
    BYTE  type;  // 0x00–0x07; visual density/type variant
} CHUNK;
```

Type values 0x01 and 0x07 appear invisible on Desert Oasis; 0x06 appears at double density. Exact semantics undocumented.

---

## `t3HardTile` (magic `HRDT`)

Source: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3HardTile

**Not yet parsed by the engine. Observed only in specific maps (e.g. Metalopolis). Purpose unknown.**

**Header:**

```c
struct HEADER {
    DWORD magic;    // "HRDT"
    UINT  version;  // 102
    DWORD unk[4];
    UINT  amount;   // number of BLOCKs
};
```

**Block and subblock layout:**

```c
typedef struct {
    FLOAT  x, y, z;         // world position
    FLOAT  rotation[9];     // 3×3 rotation matrix (row-major)
    FLOAT  scaleX, scaleY;
    USHORT unk;
} SUBBLOCK;

typedef struct {
    UINT     num;           // number of subblocks
    SUBBLOCK sub[num];
    USHORT   sep;           // separator
    CHAR     n1[12];        // texture name: "\0TextureName\0"
    DWORD    unk;
} BLOCK;
```

---

## `PaintedPathingLayer`

Source: RFEphemeration/sc2-map-analyzer `read.cpp`

Not documented by the SC2Mapster wiki. Understood from the sc2-map-analyzer source.

**Layout:** 16-byte header (skipped), then `8 × 256 × 256` bytes — eight 256×256 grids of per-cell type indicators.

**Known byte values:**

| Value | Meaning |
| --- | --- |
| `0x80` | No painted pathing (editor default) |
| `0x00` | No ground / cliff-walk / build pathing |
| `0x01` | Ground pathable (overrides `t3SyncPathingInfo`) |
| `0x81` | Non-buildable |

---

## Archive And Asset Formats

| Extension | Format | Notes |
| --- | --- | --- |
| `.SC2Map` | MPQ archive | Map archive |
| `.SC2Mod` | MPQ archive | Mod/dependency archive |
| `.SC2Archive` | MPQ archive | Asset package |
| `.s2ma` | MPQ archive | Battle.net/cache map name |
| `.SC2Components` | directory tree | Unarchived project form from Galaxy Editor |
| `.m3` | binary | Model geometry + animations |
| `.m3a` | binary | Supplemental bone animations for a specific model |
| `.dds` | DDS texture | Primary texture format |
| `.tga` | TGA image | Only image format usable in-game (not PNG/JPG) |
| `.ogv` | Ogg video | Video file |

## Cliff Models

### Path Pattern

```
Assets\Cliffs\{MeshName}\{MeshName}_{Config}_{Variant:02d}.m3
```

Fallback: `Assets\Cliffs\{MeshName}\{MeshName}_{Config}_00.m3`

### Config String

Four characters encoding the relative cliff level at each corner of the 2×2-cell tile:
- `'A'` = base level (minimum, diff 0)
- `'B'` = 1 step above base
- `'C'` = 2 steps above base
- `'D'` = 3 steps above base

The four letters use the StarCraft II Art Tools object order: bottom-left, bottom-right, top-right, top-left, counted counterclockwise from above.

The suffix `_00`, `_01`, etc. is an art variation count, not a rotation. `cvar` from `t3Terrain.xml` selects this model variation. The engine may rotate compatible cliff pieces as needed when a rotated config is not present as its own file.

SC2 supports 4 height tiers total; WC3 has only 2.

### Geometry And Materials

Cliff objects are full M3 models, not terrain quads extruded from the heightmap. The official Art Tools requirements say each cliff object tiles in a 200×200 art-unit footprint, has its pivot centered in X/Y at Z=0, and must have matching outer vertices so neighboring cliff pieces and terrain weld visually.

The floor/ceiling and walkable top/bottom portions are part of the cliff-model/material path. The Art Tools docs explicitly require vertex color for tri-planar cliff texturing; black vertex-alpha allows the terrain material to show through on the tops and bottoms of cliff objects. Cliff materials also use mask blend / alpha mask behavior, and SC2 Terrain materials can sample the terrain colors below a model.

Renderer implications:

- Do not fill every `t3CellFlags` `0x03` cliff hole by drawing continuous terrain; non-flat cliff blocks need terrain suppressed to avoid z-fighting with the cliff model. Flat 2×2 cliff blocks are different: they have no side model, so their floor/ceiling cells still need ground terrain.
- Preserve M3 batch material references instead of baking every cliff triangle into one opaque texture.
- Decode M3 vertices from `vertexFlags`. Real Liberty cliff models such as `CliffNatural0_AABB_00.m3` use `vertexFlags=0x182007d`, a 32-byte one-UV format. Treating every vertex as the local 40-byte runtime struct imports only 56 of the 71 vertices and can drop cap/floor/ceiling geometry.
- Honor material vertex-color / vertex-alpha flags, alpha-mask layers, and terrain material references before expecting cliff tops/bottoms to render correctly.

### Height Tiers (from `CliffMeshData.xml`)

```xml
<CCliffMesh default="1">
  <CliffHeights index="0" value="-4.000000"/>
  <CliffHeights index="1" value="0.000000"/>
  <CliffHeights index="2" value="2.000000"/>
  <CliffHeights index="3" value="4.000000"/>
</CCliffMesh>
```

The Liberty mod overrides `CliffHeights[0]` to `-8.0` for most tilesets.

## Localization

User-facing strings live in locale folders, not in data XML:

```
enUS.SC2Data/LocalizedData/GameStrings.txt
```

Map title, author, description, website, and all UI text are resolved here. Do not assume `enUS` is the only locale.

## XML Catalog Loading Order

1. Base game XML from `base.SC2Assets` / `patch.SC2Assets`
2. Dependency mod archives
3. Map-local XML (`Base.SC2Data/GameData/`)
4. Matching ordinary attributes are updated; array-like attributes are **appended** (not replaced)

## Terrain And Pathing Work Items

Files not yet parsed by the engine:

- `t3SyncPathingInfo` — authoritative movement/building pathing
- `t3Water` — water/lava rectangles
- `t3FluffDoodad` — ambient doodad scatter
- `t3HardTile` — unknown purpose, rare
- `t3VertCol` — vertex colors, layout unknown
- `PaintedPathingLayer` — editor-painted pathing overrides
