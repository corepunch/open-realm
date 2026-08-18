# war3map.doo — Doodads (Trees & Destructables)

Stores placed doodads: trees, rocks, barrels, crates, and other destructable/decorative objects.  
**Not** units or items — those are in `war3mapUnits.doo`.

Doodad type IDs come from `doodads.slk` inside `war3.mpq`.

## Header [TFT format, version 8]

```
char[4]  file ID          "W3do"
int      version          8
int      subversion        0x0000000B  (always 11; purpose unclear)
int      doodad_count      number of doodad records that follow
```

## Doodad record (variable length due to item sets)

```
char[4]  type ID          doodad type (from doodads.slk)
int      variation        variation index (LE)
float    x                map X coordinate
float    y                map Y coordinate
float    z                map Z coordinate
float    angle            rotation in radians  (degrees = radians * 180 / π)
float    scale_x
float    scale_y
float    scale_z
byte     flags            visibility/solidity (see below)
byte     life             HP percentage (0x64 = 100%, 0xAA = 170%)
int      item_table_ptr   random item table index, or -1 for none
                          if >= 0: drop items from that item table (defined in w3i)
                          if -1:  use inline item sets below
int      item_set_count   number of item sets (only > 0 if item_table_ptr == -1)
[item_set_count * item_set]
int      editor_id        unique editor ID for this doodad instance
```

### Doodad flags

| Value | Meaning |
|-------|---------|
| `0` | Invisible and non-solid |
| `1` | Visible but non-solid |
| `2` | Normal (visible and solid) |

### Item set structure

```
int      item_count        number of items in this set
[item_count times:]
  char[4]  item_id         item type ID (from ItemData.slk), or random item ID
  int      chance          drop chance in percent (integer)
```

**Random item IDs** follow this encoding (4 chars):
- Char 1: `Y`
- Char 2: `Y` = any type; `i`..`o` = specific item type (`i` = charged, then in dropdown order)
- Char 3: `I`
- Char 4: `/` (ASCII 47) = any level; `0`+ = level (ASCII 48 + level)

## Special doodads section (after all regular doodads)

```
int      special_version   always 0
int      special_count     number of special doodads (cliffs, etc.)
[special_count * 16 bytes each:]
  char[4]  doodad_id
  int      z               (usually 0)
  int      x               W3E tile coordinate
  int      y               W3E tile coordinate
```

Special doodads cannot be edited after placement and include cliff geometry.

## Worked example (compact layout)

```
tt tt tt tt   ← type ID (4 chars)
vv vv vv vv   ← variation
xx xx xx xx   ← X
yy yy yy yy   ← Y
zz zz zz zz   ← Z
aa aa aa aa   ← angle
xs xs xs xs   ← scale X
ys ys ys ys   ← scale Y
zs zs zs zs   ← scale Z
ff            ← flags
ll            ← life
bb bb bb bb   ← item table pointer
cc cc cc cc   ← item set count
dd dd dd dd   ← editor ID
```
