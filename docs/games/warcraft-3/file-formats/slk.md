# SLK Spreadsheet Format

SLK (Symbolic Link) is a plain-text spreadsheet interchange format that Warcraft III uses to store game data tables — unit statistics, item data, ability data, upgrade data, and more. The game ships with a large set of `.slk` files inside the MPQ archive.

## File Format Overview

An SLK file is line-oriented. Each line begins with a record type identifier followed by semicolon-separated fields:

```
ID;PWXL;N;E
B;Y<rows>;X<cols>
C;Y<row>;X<col>;K<value>
E
```

| Record | Meaning |
|--------|---------|
| `ID` | File identification line — always `ID;PWXL;N;E` for WC3 SLK files |
| `B` | Dimension bounds — total rows (`Y`) and columns (`X`) |
| `C` | Cell data — row (`Y`), column (`X`), and value (`K`) |
| `E` | End-of-file marker |

The first row (`Y=1`) contains column headers. Subsequent rows hold data records. The value field `K` can be:
- A bare number: `K123` or `K3.14`
- A quoted string: `K"Footman"`
- An empty field means the cell inherits the value from the same column on the previous row (run-length encoding)

## Key SLK Files

| File (inside MPQ) | Contents |
|-------------------|----------|
| `Units\UnitData.slk` | Base unit stats (HP, speed, armour, attack) |
| `Units\UnitUI.slk` | Unit display names, icons, sound sets |
| `Units\UnitWeapons.slk` | Attack type, projectile, range |
| `Units\UnitBalance.slk` | Build time, cost, food |
| `Units\UnitAbilities.slk` | Per-unit ability assignments |
| `Abilities\AbilityData.slk` | Spell data (cooldown, cast range, damage) |
| `Items\ItemData.slk` | Item stats |
| `Upgrades\UpgradeData.slk` | Research costs and effects |
| `Doodads\Doodads.slk` | Doodad model paths and properties |
| `DestructableData\DestructableData.slk` | Destructible HP, death animation |

## INI / Profile Files

Alongside SLK files, Warcraft III uses `profile.txt` style INI files (`war3mapSkin.txt`, `TriggerData.txt`, etc.). These follow a standard `[Section]\nKey=Value` layout. OpenWarcraft3 parses both via `games/warcraft-3/sheet/sheet.c`.

## Parsing in OpenWarcraft3

The Warcraft III SLK/profile parser lives in `games/warcraft-3/sheet/`. It works in two phases:

### Phase 1 — Tokenisation (`SheetParseTokens`)

Each line is read into a buffer. The `SheetParseTokens` function replaces every `;` separator with a NUL byte and counts the resulting token count. `GetToken(buffer, index)` then walks the NUL-separated list to retrieve any token by index.

### Phase 2 — Cell storage (`FS_FillSheetCell`)

For each `C` record the parser extracts `Y`, `X`, and `K` fields and calls `FS_FillSheetCell(x, y, text)`, which appends the text to a flat string buffer and links a `sheetCell_t` node into a singly-linked list.

```c
typedef struct SheetCell {
    LPSTR  text;   // pointer into the flat string buffer
    USHORT column;
    USHORT row;
    LPSHEET next;
} sheetCell_t;
```

### Phase 3 — Row assembly (`FS_MakeRowsFromSheet`)

After all cells are parsed, `FS_MakeRowsFromSheet` converts the flat cell list into an array of `sheetRow_t` records, one per data row. The first row provides the column header names; subsequent rows are turned into `sheetField_t` key-value pairs keyed by header name.

```c
// Look up a value in a row by column name:
LPCSTR value = FS_GetField(row, "HP");
```

The resulting row array drives Warcraft III unit spawning (`games/warcraft-3/game/g_spawn.c`) and metadata lookups (`games/warcraft-3/game/g_metadata.c`) at runtime.

## Example SLK Snippet

```
ID;PWXL;N;E
B;Y5;X4
C;Y1;X1;K"unitID"
C;Y1;X2;K"HP"
C;Y1;X3;K"moveSpeed"
C;Y1;X4;K"name"
C;Y2;X1;K"hfoo"
C;Y2;X2;K200
C;Y2;X3;K270
C;Y2;X4;K"Footman"
C;Y3;X1;K"hpea"
C;Y3;X2;K250
C;Y3;X3;K270
C;Y3;X4;K"Peasant"
E
```

## Related Source Files

| Source | Purpose |
|--------|---------|
| `games/warcraft-3/sheet/parser.c` | Token parser helpers |
| `games/warcraft-3/sheet/sheet.c` | SLK parser and row accessor |
| `common/shared.h` | `sheetRow_t`, `sheetField_t` type definitions |
| `games/warcraft-3/game/g_metadata.c` | Unit metadata lookups from SLK tables |
| `games/warcraft-3/game/g_spawn.c` | Unit spawning using SLK data |
