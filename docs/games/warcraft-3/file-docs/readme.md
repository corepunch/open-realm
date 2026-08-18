# Warcraft III File Format Specifications

Community-compiled documentation of all known Warcraft III file formats, organized for use as Copilot / LLM context when writing parsers, test data generators, and map tools.

**Primary sources:**
- https://867380699.github.io/blog/2019/05/09/W3X_Files_Format (the Zepir/WC3C spec, mirrored)
- https://github.com/ChiefOfGxBxL/WC3MapSpecification (living document, versioned tables)
- https://github.com/Luashine/wc3-file-formats (Kaitai Struct definitions, .ods tables)

> **Version note:** Structures marked `[TFT]` are Frozen Throne / Reforged era. Classic RoC variants differ in some fields.  
> File format version numbers referenced here are those in the binary header of each file.

---

## Directory Layout

```
wc3-formats/
├── README.md                  ← this file
├── map-container/
│   ├── mpq.md                 ← MPQ archive format
│   └── w3x-w3m.md             ← W3X/W3M map container (header + MPQ)
├── terrain/
│   ├── w3e.md                 ← war3map.w3e  – terrain / tilepoints
│   ├── shd.md                 ← war3map.shd  – shadow map
│   └── wpm.md                 ← war3map.wpm / war3mapPath.tga – pathing
├── units-doodads/
│   ├── doo.md                 ← war3map.doo  – doodads (trees, destructables)
│   └── unitsdoo.md            ← war3mapUnits.doo – units & items placement
├── triggers-scripts/
│   ├── j.md                   ← war3map.j    – JASS2 script
│   ├── wts.md                 ← war3map.wts  – trigger strings
│   ├── wtg.md                 ← war3map.wtg  – GUI triggers
│   └── wct.md                 ← war3map.wct  – custom text triggers
├── object-data/
│   ├── w3i.md                 ← war3map.w3i  – map info
│   ├── w3u-w3t-w3b-etc.md     ← war3map.w3u/t/a/b/d/h/q – object data
│   ├── w3r.md                 ← war3map.w3r  – regions
│   ├── w3c.md                 ← war3map.w3c  – cameras
│   └── imp.md                 ← war3map.imp  – imported files list
├── sounds-regions/
│   └── w3s.md                 ← war3map.w3s  – sound definitions
├── visual-formats/
│   ├── blp.md                 ← BLP texture format
│   ├── mdx-mdl.md             ← MDX (binary) and MDL (text) model formats
│   └── mmp-minimap.md         ← war3map.mmp / minimap files
├── slk/
│   └── slk.md                 ← SYLK / SLK table format + key game SLK files
├── campaign-replay/
│   ├── w3n.md                 ← W3N campaign archive
│   ├── w3g.md                 ← W3G replay format
│   └── w3z-w3v.md             ← W3Z save files, W3V variable cache
└── tools/
    └── tools.md               ← Recommended tools (MPQ editors, hex editors, etc.)
```

---

## Data Primitives (all files)

All WC3 binary files share these primitive types. Little-Endian unless stated.

| Type | Size | Notes |
|------|------|-------|
| `int` | 4 bytes | Signed 32-bit LE. Equivalent to C `int`. |
| `short` | 2 bytes | Signed 16-bit LE. Range used is −16384..16383 (top 2 bits free for flags). |
| `float` | 4 bytes | IEEE 754 single-precision LE. |
| `char` | 1 byte | Single ASCII byte. |
| `char[4]` | 4 bytes | Four-char ID code (e.g. `"W3E!"`, `'Ahbz'`). Stored as-is, no null terminator. |
| `string` | variable | Null-terminated UTF-8 byte sequence. Length = content + 1. |
| `flags` (int) | 4 bytes | Each bit is an independent boolean. |
| `byte` | 1 byte | Raw unsigned byte. |

### TRIGSTR_ strings

If a string value starts with `TRIGSTR_` (case-sensitive), it is a trigger string reference. WC3 resolves it at display time from the `.wts` string table.

- `TRIGSTR_007`, `TRIGSTR_7abc`, `TRIGSTR_07` → all reference trigger string **#7**
- `TRIGSTR_ab7`, `TRIGSTR_` → reference trigger string **#0**
- `TRIGSTR_-7` (negative) → resolves to empty string `""`
- Convention: always written as `TRIGSTR_NNN` (3 digits) + null terminator → 12 bytes total.

### Color codes in strings

`|c00BBGGRR` embeds a color change inline in a displayed string.  
`BB` `GG` `RR` are 2-digit hex values for blue, green, red respectively.

### UTF-8 conversion

WC3 strings are UTF-8. For pure ASCII content treat as normal C strings. For multi-byte characters the standard UTF-8 encoding applies (see primary source for full conversion tables).

---

## File Version Table (across WC3 patches)

| File | Classic / RoC | TFT (1.07+) | Pre-Reforged (1.29–1.31) | Reforged (1.32+) | 2.0+ |
|------|--------------|-------------|--------------------------|------------------|------|
| `war3map.w3e` | 11 | 11 | 11 | 11 | 12 |
| `war3mapUnits.doo` | 7 | 8_11 | 8_11 | 8_11 | — |
| `war3map.doo` | 7 | 8_11 | 8_11 | 8_11 | — |
| `war3map.w3r` | 5 | 5 | 5 | 5 | — |
| `war3map.w3c` | 0 | 0 | 0 | 0 | — |
| `war3map.w3s` | 1 | 1 | 1 | 3 | — |
| `war3map.w3u/.w3t/.w3a/etc.` | 1 | 2 | 2 | 2 | — |
| `war3map.wts` | 1 | 1 | 1 | 1 | — |
| `war3map.w3i` | 18 | 25 | 25–28 | 31 | 33 |
| `war3map.imp` | 1 | 1 | 1 | 1 | — |

> Sources: ChiefOfGxBxL/WC3MapSpecification version tables; cross-verified with Luashine/wc3-file-formats.
