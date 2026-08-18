# MapInfo

Binary map metadata file present in every `.SC2Map` archive.

Source: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/MapInfo

## Purpose

Stores high-level map identity: dimensions, playable bounds, tileset/theme, loading screen,
and per-player slot configuration. This is the first file to parse after opening the archive.

## Binary Layout

All integers are little-endian. Variable-length strings are null-terminated (`char[]`).

```c
struct MapInfo
{
    DWORD   magic;           // "MapI"
    u32     fileVersion;

    u32     unknown0;        // present in current retail maps before dimensions
    u32     unknown1;
    u32     width;           // map cell width
    u32     height;          // map cell height
    u32     unknown2;
    u32     unknown3;

    char    theme[];         // null-terminated; tileset/theme string
    char    planet[];        // null-terminated; planet string

    u32     boundaryLeft;    // playable bounds in cell coords
    u32     boundaryBottom;
    u32     boundaryRight;
    u32     boundaryTop;

    u32     unknown4;
    u32     unknown5;

    char    loaderImagePath[];   // null-terminated; loading screen image path
    u32     loaderImageFormat;   // 0x00 = stretch_none
                                 // 0x01 = stretch_fullscreen
                                 // 0x02 = stretch_widescreen
    u32     unknown6;        // always -1
    u32     unknown7;
    u32     unknown8;
    u32     unknownWidth;
    u32     unknownHeight;
    u32     unknown9;
    u32     unknown10;

    u8      playerCount;     // assumed; number of player slot records that follow

    struct PlayerSlot
    {
        u8  isUnused;        // usage flag
        u8  pUnk1;
        u8  pUnk2;
        u8  index;           // player index (0-based)
        u8  owner;           // player type enum (see below)
        u8  pUnk3;
        u8  pUnk4;
        u8  pUnk5;
        u32 playerColor;     // -1 = use default team color
        char playerRace[];   // null-terminated; empty/null = random race
                             // race string ID defined in GameData\RaceData.xml
        u8  pUnk6;
        u8  pUnk7;
        u8  pUnk8;
        u8  pUnk9;
        u8  pUnk10;
        u8  pUnk11;
        u8  pUnk12;
        u8  pUnk13;
        u8  pUnk14;
        u8  pUnk15;
    } players[playerCount];

    // additional unknown fields follow
};
```

## `owner` Enum

| Value | Constant | Meaning |
| --- | --- | --- |
| 0 | `c_playerTypeNone` | No player / empty slot |
| 1 | `c_playerTypeUser` | Human player |
| 2 | `c_playerTypeComputer` | AI |
| 3 | `c_playerTypeNeutral` | Neutral (resources, critters) |
| 4 | `c_playerTypeHostile` | Hostile neutrals |
| 5 | `c_playerTypeReferee` | Referee |
| 6 | `c_playerTypeSpectator` | Spectator |

## Parser Notes

- Check the `MapI` magic (bytes `0x4D 0x61 0x70 0x49`) before reading anything.
- Parse variable-length `char[]` strings explicitly with `strlen`; never use packed C structs across string boundaries.
- `width`/`height` here may differ from `t3CellFlags` dimensions. Use `t3CellFlags` as the authoritative map size once terrain files are loaded, since the editor writes consistent dimensions there.
- Current retail maps include 8 bytes after `fileVersion` before `width`/`height`; `TRaynor01.SC2Map` stores `136x160` at offsets `16/20`, matching its `137x161` height-map vertex grid.
- Many fields are marked unknown. Treat them as opaque and preserve their bytes in any re-serialization.

## Cross-Reference With sc2reader

`sc2reader` parses `MapInfo` and exposes:
- `map.name` — from `GameStrings.txt`, not `MapInfo`
- `map.players` — list of `MapInfoPlayer` objects with `pid`, `name`, `race`, `color`
- `map.region`, `map.map_hash` — from the Battle.net filename

The `width`/`height` fields and `boundary*` fields are the same in both parsers.
