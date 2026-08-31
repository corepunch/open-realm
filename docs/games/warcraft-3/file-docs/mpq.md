# MPQ Archive Format

MPQ ("Mo'PaQ") archives are the container format used throughout Warcraft III for game data and maps. They function similarly to ZIP files — a directory with compressed files inside.

## Key characteristics

- Supports multiple compression algorithms (zlib, bzip2, PKWARE Implode, LZMA, sparse, etc.)
- Has a hash table for fast filename lookup
- Supports digital signatures
- WC3 maps (`.w3m`/`.w3x`) are MPQ archives with an additional 512-byte prefix header

## File lookup order

When WC3 resolves a file path it searches in this order:
1. Real filesystem (if registry key `HKCU\Software\Blizzard Entertainment\Warcraft III\Allow Local Files = 1`)
2. The map MPQ (`*.w3m` / `*.w3x`)
3. `war3patch.mpq`
4. `war3x.mpq` / `war3xlocal.mpq` (TFT expansion)
5. `war3.mpq` (base game)

OpenWarcraft's mounted archive order follows the active game mode, with one path-aware ROC compatibility rule. Patched
`War3Local.mpq` archives can contain `Scripts/*.ai` copies matching TFT while ROC `Scripts/common.ai` resolves from
`War3.mpq`. In ROC mode `FS_ArchiveFileVisible` therefore hides only `.ai` files under `Scripts/` from
`War3Local.mpq`; all other localized files remain visible. TFT mode leaves those AI files visible under normal expansion
precedence. Apply this policy in both `FS_OpenFile` and `FS_ReadFileAll`; fixing only one API creates inconsistent VFS
results between streaming and whole-file consumers.

Verify the archive policy with:

```sh
make test-commands
build/bin/mpqtool -mpq 'data/Warcraft III/War3.mpq' cat Scripts/common.ai
build/bin/mpqtool -mpq 'data/Warcraft III/War3.mpq' cat Scripts/human.ai
```

## Standard WC3 MPQ archives

| Archive | Contents |
|---------|----------|
| `war3.mpq` | Base game data (RoC) |
| `war3x.mpq` | Frozen Throne expansion data |
| `war3xlocal.mpq` | Localized TFT strings/audio |
| `war3patch.mpq` | Latest patch overrides |

## Which paths work inside a map MPQ

These SLK/txt overrides work reliably when placed inside a `.w3x`:
```
Units\unitUI.slk
Units\AbilityData.slk
UI\MIDISounds.slk
Units\HumanUnitFunc.txt
Units\HumanUnitStrings.txt
Units\HumanAbilityFunc.txt
Units\HumanAbilityStrings.txt
Units\HumanUpgradeFunc.txt
Units\HumanUpgradeStrings.txt
```

These do **not** work reliably inside a map MPQ:
```
TerrainArt\CliffTypes.slk    ← must be in a separate MPQ patch via MPQDraft
Units\UnitMetaData.slk
Scripts\Blizzard.j
Units\MiscData.txt
```

## Tools

- **Ladik's MPQ Editor** — https://www.zezula.net/en/mpq/download.html (GUI editor)
- **OW3 MPQ layer** (`common/mpq.c`) — in-tree Warcraft III MPQ reader. Handles archive open/read/find operations without StormLib at build or run time, normalizes forward-slash paths to the backslash-separated form used in MPQs, and decodes zlib, adaptive Huffman plus Blizzard ADPCM, and standalone mono/stereo Blizzard ADPCM sectors. TFT voice assets such as `Units\\Creeps\\SpiritWolf\\SpiritWolfYesAttack1.wav` use standalone mono ADPCM (`0x40`).
- **mpqtool** (`tools/mpqtool.c`) — in-tree CLI built by `make build`. Supports `ls [subdir]` for incremental directory listing and `cat <file>` for dumping file contents to stdout. Usage: `build/bin/mpqtool -mpq <path> ls [subdir]` / `build/bin/mpqtool -mpq <path> cat <archive-file>`.
- **SFmpqapi** — older API, source available at https://sfsrealm.hopto.org/downloads/SFmpqapi.html
- **MPQDraft** — creates executable patches embedding a custom MPQ (for overriding files that can't go in the map MPQ)

## References

- Justin Olbrantz (Quantam) "Inside MoPaQ": http://www.zezula.net/en/mpq/main.html
- Ladislav Zezula's MPQ documentation at same site
