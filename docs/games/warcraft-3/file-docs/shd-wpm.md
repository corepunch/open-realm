# war3map.shd — Shadow Map

No header. Raw byte array.

```
File size = 16 * map_width * map_height bytes
```

Each byte controls one **1/16-tile** cell (tiles are subdivided 4×4):

| Value | Meaning |
|-------|---------|
| `0x00` | No shadow |
| `0xFF` | Shadow |

---

# war3mapPath.tga — Image Path File (old, ≤ patch 1.21)

Standard 32-bit uncompressed RGB TGA with a black alpha channel.

- Image size: `(map_width * 4) × (map_height * 4)` pixels (each tile = 4×4 pixels)
- Top-left of image = upper-left corner of map

## TGA header (18 bytes)

```
byte    ID Length           0
byte    Color Map Type      0
byte    Image Type          2  (uncompressed RGB)
--- Color Map Specification (5 bytes) ---
byte[2] First Entry Index   0
byte[2] Color Map Length    0
byte    Color Map Entry Size 0
--- Image Specification (10 bytes) ---
byte[2] X Origin            0
byte[2] Y Origin            0
byte[2] Image Width         map_width * 4  (LE)
byte[2] Image Height        map_height * 4 (LE)
byte    Pixel Depth         32 (0x20)
byte    Image Descriptor    0x28  (0x20=top-left origin, 0x08=8-bit alpha)
```

Example header: `00 00 02 00 00 00 00 00 00 00 00 00 WW WW HH HH 20 28`

## Pixel format

4 bytes per pixel: `BB GG RR AA`  
Alpha channel = 0 (black) normally; white = blight.

## Color → pathing semantics

| Color | Build | Walk | Fly |
|-------|-------|------|-----|
| White (R+G+B) | no build | no walk | no fly |
| Red (R only) | build ok | no walk | fly ok |
| Yellow (R+G) | build ok | no walk | no fly |
| Green (G only) | build ok | walk ok | no fly |
| Cyan (G+B) | no build | walk ok | no fly |
| Blue (B only) | no build | walk ok | fly ok |
| Magenta (R+B) | no build | no walk | fly ok |
| Black (none) | build ok | walk ok | fly ok |

Rule: **Red channel set = no walk**, **Green channel set = no fly**, **Blue channel set = no build**.  
Alpha: `0x00` = normal ground, `0xFF` = blight.

---

# war3map.wpm — Path Map File (new, ≥ patch 1.30)

Replaces `war3mapPath.tga` in all modern WC3 versions.

## Header

```
char[4]  file ID            "MP3W"
int      version            0
int      width              map_width  * 4
int      height             map_height * 4
```

## Data

`width * height` bytes, one per 1/16-tile cell.

## Flag byte

| Bit | Mask | Meaning (1 = restriction active) |
|-----|------|----------------------------------|
| 0 | `0x01` | Unused |
| 1 | `0x02` | No walk |
| 2 | `0x04` | No fly |
| 3 | `0x08` | No build |
| 4 | `0x10` | Unused |
| 5 | `0x20` | Blight |
| 6 | `0x40` | No water |
| 7 | `0x80` | Unknown |

## Common flag values

| Value | Terrain type |
|-------|-------------|
| `0x00` | Bridge doodad (all passable) |
| `0x08` | Shallow water |
| `0x0A` | Deep water (no walk + no fly) |
| `0x40` | Normal ground |
| `0x48` | Water ramp / unbuildable ground / unbuildable doodad parts |
| `0xCA` | Cliff edge / solid doodad (no build + no walk) |
| `0xCE` | Map boundary edges |
