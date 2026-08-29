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
| `B` | Advisory dimension bounds — total rows (`Y`) and columns (`X`) |
| `C` | Cell data — row (`Y`), column (`X`), and value (`K`) |
| `F` | Cell/column formatting; `X` and `Y` still update the current coordinates |
| `E` | End-of-file marker |

The first row (`Y=1`) contains column headers. Subsequent rows hold data records. The value field `K` can be:
- A bare number: `K123` or `K3.14`
- A quoted string: `K"Footman"`

`X` and `Y` are stateful. A record that omits either coordinate keeps the previous value, including coordinate changes made by an `F` record. A missing `K` does not create a cell. Do not allocate solely from `B`: shipped files contain bounds larger than their populated cell range.

## Key SLK Files

| File (inside MPQ) | Contents |
|-------------------|----------|
| `Units\UnitData.slk` | Base unit stats (HP, speed, armour, attack) |
| `Units\UnitUI.slk` | Unit display names, icons, sound sets |
| `Units\UnitWeapons.slk` | Attack type, projectile, range |
| `Units\UnitBalance.slk` | Build time, cost, food |
| `Units\UnitAbilities.slk` | Per-unit ability assignments |
| `Units\AbilityData.slk` | Spell data (cooldown, cast range, damage) |
| `Units\ItemData.slk` | Item stats |
| `Units\UpgradeData.slk` | Research costs and effects |
| `Doodads\Doodads.slk` | Doodad model paths and properties |
| `Units\DestructableData.slk` | Destructible HP, death animation |

## INI / Profile Files

Alongside SLK files, Warcraft III uses `profile.txt` style INI files (`war3mapSkin.txt`, `TriggerData.txt`, etc.). These follow a standard `[Section]\nKey=Value` layout. OpenWarcraft3 parses both via `games/warcraft-3/sheet/sheet.c`.

## Parsing in OpenWarcraft3

The Warcraft III SLK/profile parser lives in `games/warcraft-3/sheet/`. It works in three phases:

### Phase 1 — Field scanning (`ScanSLKField`)

`FS_ParseSLK_Buffer` scans the input span line by line without copying lines into a fixed buffer. For each `C` or `F` record, `ScanSLKField` extracts one semicolon-delimited field at a time. For `K` fields that start with `"`, it switches to quote-aware mode: reads until the matching closing `"`, decoding `""` as a literal double-quote and treating semicolons inside quotes as ordinary characters.

Bare (unquoted) fields for `X`, `Y`, and numeric `K` values are scanned up to the next `;` or end-of-line as before. The previous `SheetParseTokens`/`GetToken` approach that blindly NUL-terminated every `;` has been replaced; quoted semicolons now parse correctly.

### Phase 2 — Cell storage (`FS_FillSheetCell`)

For each `C` record the parser extracts `Y`, `X`, and `K` fields and calls `FS_FillSheetCell(x, y, text)`, which appends the text to a process-global string arena and links a `sheetCell_t` node into a process-global pool. Cells with `X=0` or `Y=0` are skipped before the call. `FS_FillSheetCell` checks both the cell pool and the text arena for exhaustion and logs to `stderr` before returning early if either is full. `F` records update the same current `X`/`Y` state but do not create cells.

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
LPCSTR value = FS_FindSheetCell(rows, "hfoo", "HP");
```

The parser-owned row list remains the compatibility representation for INI files. Every runtime SLK is decoded once at startup into a flat typed array defined in `games/warcraft-3/game/g_unitrow.h`:

- `UnitBalance.slk`
- `UnitData.slk`
- `UnitUI.slk`
- `UnitWeapons.slk`
- `UnitAbilities.slk`
- `AbilityData.slk`
- `ItemData.slk`
- `DestructableData.slk`
- `Doodads.slk`
- `UberSplatData.slk`
- `UnitAckSounds.slk`

Every typed row contains its source FOURCC and every ROC/TFT column. Native column names exist only in the DDX schemas; C members use semantic names such as `agilityPerLevel`. Optional `slkField_t.id` object-data IDs are readable four-character strings rather than packed integer constants. `slk_stores[]` in `games/warcraft-3/game/g_metadata.c` initializes the public `g_UnitBalance`, `g_UnitWeapons`, and corresponding per-SLK arrays and indexes once. Production retains no raw source pointer in the registry. `ShutdownUnitData` frees the typed strings, arrays, and indexes.

Spawned entities bind immutable pointers to their rows once in `SP_CallSpawn`, so gameplay uses `unit->balance->agilityPerLevel`, `unit->ui->modelFile`, or `unit->weapons->attack2.damageBase`. The mutable values derived at spawn remain in `unit->runtime`; this is separate from the immutable `UnitBalance_t`. Code that only has a FOURCC uses `G_UnitBalance(id)` and the other indexed lookup functions. Typed passthrough `UNIT_*`, `ITEM_*`, and `DESTRUCTABLE_*` macros are not part of this path; only Profile/INI fields remain macro-backed.

`UnitsMetaData` is a compile-time FOURCC dispatch table. Descriptor IDs are readable four-character string literals compared at runtime as fixed-width `DWORD` keys. Each descriptor also contains the `offsetof(edict_t, cachedRow)`, the `offsetof(TypedRow_t, member)`, and its `bzFieldType_t`. `UnitMetaString`, `UnitMetaInteger`, `UnitMetaBoolean`, and `UnitMetaReal` find that descriptor, read the immutable row pointer already cached on the edict, and address the typed member directly. Startup does not bind SLK or column-name strings, and these accessors do not consult the global typed-row indexes.

`BZ_FIELD_CSTR` values are duplicated while decoding, so typed rows do not borrow parser-arena strings. `FS_SLKFreeRows` walks the same DDX schema, frees each unique string offset, and frees the row array. This unique-offset rule is required for release aliases such as ItemData `class`/`itemClass` and DestructableData `Name`/`name`.

ROC/TFT columns that moved between tables remain represented in both owning row types. Accessors resolve the known release split, for example `UnitBalance.collision` in TFT versus `UnitData.collision` in ROC. AbilityData's ROC `Data11..Data34` and TFT `DataA1..DataI4` columns map into one `data[level][slot]` array during decoding. Test builds expose `G_SetSLKRows` to replace and restore a typed fixture; production builds do not retain that source pointer.

FOURCC-keyed tables use the sorted `slkIndex_t` sidecar. `UnitAckSounds.slk` has arbitrary-length labels such as `FootmanWhat`; its row struct owns the full source row name and lookup compares that string in the flat array. Do not truncate sound labels into FOURCC keys.

`SplatData.slk` and `UnitCombatSounds.slk` were previously loaded but had no consumers, so they are no longer loaded. Add a typed row and registry entry before introducing a runtime consumer for either table.

## Verified Archive Characteristics

The following was measured with `build/bin/mpqtool` against the startup SLKs loaded by `InitGame` and `InitUnitData`:

| Data set | Cells | Approx. stored value bytes | Data-row upper bound |
|----------|------:|---------------------------:|---------------------:|
| ROC (`War3.mpq`) | 148,596 | 984,282 | 3,956 |
| TFT (`War3x.mpq`) | 333,954 | 2,088,980 | 7,382 |

- ROC uses omitted coordinates heavily and contains thousands of `F` records. Stateful `X`/`Y` handling must be preserved.
- The inspected startup tables contain no quoted semicolons and no lines at or above the current 1024-byte line limit.
- `B` is not allocation truth. ROC `UberSplatData.slk` declares 136 rows but populates only 44; ROC `UnitAckSounds.slk` declares 20 columns but populates 19. TFT has similar mismatches in `UberSplatData.slk` and `UnitData.slk`.
- The current three one-million-entry pools occupy about 72 MiB on a 64-bit build before the separate 8 MiB text arena, despite the TFT startup corpus containing about 334,000 cells and fewer than 7,400 data rows.

## Current Risks

1. **Cached lists have shared mutable ownership.** `FS_ParseSLK` and `FS_ParseINI` cache and return the same `sheetRow_t` list. `InitUnitData` then appends cached INI lists by rewriting each tail's `next`. Since `config_files` and `profile_files` overlap, one aggregate can rewrite another and repeated rows can form cycles. A cache entry must own an immutable table; aggregate lookup order must not be represented by splicing owned row lists.
2. **Pool exhaustion corrupts memory.** Row and field cursors still have no capacity checks. `FS_FillSheetCell` now checks the cell pool and text arena and logs before returning early, but the `FS_MakeRowsFromSheet` row/field pools remain unchecked.
3. **Long values are silently truncated.** The per-field buffer in `ScanSLKField` is `MAX_SHEET_LINE` (1024) bytes; values longer than 1023 bytes are truncated without a warning. The confirmed startup corpus has no such values. Cache keys in `NormalizeSheetKey` are also silently truncated at 256 bytes.
4. **Errors are not actionable.** Invalid magic, malformed coordinates, unsupported records, allocation failure, and truncation generally return partial data or `NULL` without a filename, line, or reason.
5. **Compatibility lookups are linear.** `FS_FindSheetCell` still scans Profile/INI rows and fields. Runtime SLK lookups use sorted indexes, and spawned entities retain direct row pointers.
6. **Production parsing is now unit-tested.** `FS_ParseSLK_Buffer` is non-static and declared in `common.h`. Part 5 of `t_slk.c` exercises it directly: stateful omitted Y, stateful omitted X, F-record coordinate advance, advisory B bounds, quoted semicolons, `""` quote escapes, zero coordinate rejection, and no-magic-line tolerance.
7. **Legacy parser storage is process-scoped.** Cache entries and parser pools survive for the process lifetime. Typed SLK arrays and their owned strings are independent and are released by `ShutdownUnitData`.

## External Implementations

- [wc3libs](https://github.com/inwc3/wc3libs) uses an ANTLR grammar that keeps semicolons inside quoted values, preserves stateful coordinates, represents values as typed data, and exposes keyed object/field maps. Its loader requires `B` and allocates directly from it, which is too strict for Warcraft files with unreliable bounds.
- [HiveWE](https://github.com/stijnherfst/HiveWE/blob/main/src/file_formats/slk.ixx) validates the `ID` record, returns explicit load errors, owns dynamic strings/maps, ignores `B`, preserves coordinate state, and provides row/column indexes. Its specialized line parser assumes a narrow token order and does not provide a generally quote-aware field lexer.
- [mdx-m3-viewer](https://github.com/flowtsohg/mdx-m3-viewer/blob/master/src/parsers/slk/file.ts) deliberately ignores `B`, grows a sparse matrix from observed coordinates, and then builds case-insensitive keyed rows. Like the current parser, it splits raw semicolons and therefore is not a tokenization reference.
- Warsmash similarly demonstrates dynamic typed rows and keyed lookup, but also splits raw semicolons. Its storage model is useful; its lexer is not.

No implementation should be copied wholesale. The common useful contract is owned dynamic storage, observed-coordinate growth, stateful coordinates, explicit errors, and keyed access. wc3libs supplies the strongest lexical model; HiveWE supplies the closest table API model.

## Migration Plan

1. ✅ **Done.** `FS_ParseSLK_Buffer` is non-static and declared in `common.h`. `SheetParseTokens`/`GetToken` replaced by `ScanSLKField`, a quote-aware scanner that reads directly from the input span (no line copy). `FS_FillSheetCell` now validates X/Y (≥1) and checks pool/arena bounds. Part 5 of `t_slk.c` covers: quoted semicolons, `""` escapes, omitted `X`/`Y`, F-record coordinate advance, wrong/missing `B`, zero-coordinate rejection, and no-magic-line tolerance.
2. Remaining from original step 2: long-value truncation warning and full removal of the `MAX_SHEET_LINE` per-field cap.
3. Introduce an owned `sheetTable_t` behind the existing `sheetRow_t` result. Allocate cells/rows/fields/strings per table, fail with filename and line diagnostics, and add cache reset/destruction. Keep `sheetRow_t` stable until consumers migrate.
4. Stop mutating cached row chains. Represent profile precedence as an array of table pointers, as `abilityConfigTables` already does, or build non-owning aggregate link nodes separate from cached rows.
5. Add per-table row and case-insensitive column indexes as sidecars. Make `FS_FindSheetCell` use the index while retaining linked rows for iteration-heavy callers.
6. After all consumers use table ownership explicitly, remove the global pools, `last_parsed_sheet_tail`, and the duplicate tail caches in the sheet and game modules.

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
