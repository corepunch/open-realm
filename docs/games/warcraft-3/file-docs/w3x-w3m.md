# W3M / W3X — Map Container Format

A `.w3m` (Reign of Chaos) or `.w3x` (Frozen Throne) file is a standard MPQ archive preceded by a **512-byte header** and optionally followed by a **260-byte footer**.

## Header (fixed 512 bytes)

```
char[4]  file ID           must be "HM3W"
int      unknown           (often 0)
string   map name          null-terminated
int      map flags         (same flags as in war3map.w3i — see below)
int      max number of players
[padding with 0x00 bytes to fill the remaining 512 bytes]
```

### Map flags (header + w3i)

| Bit | Value | Meaning |
|-----|-------|---------|
| 0 | `0x0001` | Hide minimap in preview screens |
| 1 | `0x0002` | Modify ally priorities |
| 2 | `0x0004` | Melee map |
| 3 | `0x0008` | Playable area was large, never reduced to medium |
| 4 | `0x0010` | Masked areas are partially visible |
| 5 | `0x0020` | Fixed player settings for custom forces |
| 6 | `0x0040` | Use custom forces |
| 7 | `0x0080` | Use custom tech tree |
| 8 | `0x0100` | Use custom abilities |
| 9 | `0x0200` | Use custom upgrades |
| 10 | `0x0400` | Map properties menu opened at least once |
| 11 | `0x0800` | Show water waves on cliff shores |
| 12 | `0x1000` | Show water waves on rolling shores |

## Footer (optional, 260 bytes)

```
char[4]   footer sign ID   "NGIS"  (= "sign" reversed)
byte[256] authentication data
```

Present only on official Blizzard-signed maps. Purpose/algorithm not publicly documented.

## MPQ contents

Files that may appear inside the map archive:

```
(listfile)              MPQ filename list
(signature)             optional digital signature
(attributes)            optional CRC/timestamps
war3map.w3e             terrain
war3map.w3i             map info
war3map.wtg             GUI triggers
war3map.wct             custom text (JASS) triggers
war3map.wts             trigger strings
war3map.j               JASS2 main script
war3map.shd             shadow map
war3mapMap.blp          minimap image (BLP)
war3mapMap.b00          minimap image (alternate)
war3mapMap.tga          minimap image (TGA)
war3mapPreview.tga      loading screen preview
war3map.mmp             minimap icon positions
war3mapPath.tga         pathing map (old format, ≤1.21)
war3map.wpm             pathing map (new format, ≥1.30)
war3map.doo             doodads (trees, destructables)
war3mapUnits.doo        units and items
war3map.w3r             regions
war3map.w3c             cameras
war3map.w3s             sounds
war3map.w3u             unit object data (custom units)
war3map.w3t             item object data
war3map.w3a             ability object data
war3map.w3b             destructable object data
war3map.w3d             doodad object data
war3map.w3q             upgrade object data
war3mapMisc.txt         misc gameplay constants override
war3mapSkin.txt         skin/UI override
war3mapExtra.txt        extra data
war3map.imp             imported file list
war3mapImported\*.*     imported files (custom assets)
```

## Notes for implementors

- The MPQ starts at byte offset **512** (after the header).
- To open a `.w3x` programmatically: skip the first 512 bytes, then parse as a standard MPQ.
- The `war3map.j` may be relocated to `Scripts\war3map.j` by map protectors.
- `(listfile)` is needed to enumerate files by name; some protected maps omit it.
