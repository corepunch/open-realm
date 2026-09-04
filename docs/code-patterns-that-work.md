# Code Patterns That Work Well

Patterns observed across the codebase that are consistently good. Reference these when writing new code.

## File-shaped structs with trailing arrays

`SC2Map` loads binary terrain layers (heightmap, cell flags, texture masks) as one flat struct with trailing array fields. Read the blob once, validate dimensions, keep the pointer. No per-element decoding, no parallel arrays to keep in sync.

```c
typedef struct {
    DWORD width, height;
    BYTE  cells[];
} terrainData_t;
terrainData_t *td = (terrainData_t *)ri.FS_ReadFile(path, &size);
// validate: sizeof(header) + width*height == size
```

Zero intermediate allocations. Zero pointer chasing during draw.

## Read-whole-file → archive-from-memory

The SC2 map loading pattern: read the outer file once into a buffer, then open that buffer as an MPQ archive via `SFileOpenArchiveFromMemory`. All subsequent reads decompress from that pointer. When done, close the archive handle, free the outer buffer once. No repeated file opens, no seeking.

**When to use:** any format that wraps a container (SC2Map, MPQ, ZIP, any archive-inside-a-file).

**When not:** single flat files like BLP textures — just read and parse directly.

## Pointer-walk parsers with const BYTE *

ADT, M2, and MDX parsers all take `BYTE const *data` and walk the chunk tree with pointer arithmetic. No re-reading, no intermediate buffers, no fseek. This works identically with heap pointers and mmap'd pointers.

```c
// Wow_LoadAdt pattern: walk chunk tree in-place
BYTE const *p = data + header->mcnk_offset;
for (i = 0; i < MCNK_COUNT; i++) {
    MCNK const *chunk = (MCNK const *)p;
    // read from chunk->*, advance p
}
```

## Quake-style resident model registration

`renderer/r_model.c` owns the filename-to-model registry. A cache miss asks the active game renderer to load one file-shaped model;
a case-insensitive hit returns that same resident pointer. Missing-model placeholders are cached too. Individual release calls drop
caller ownership, while registration sequence marks and shutdown provide the actual release points. Model formats do not add a second
appearance, outfit, or decoded-array cache; textures remain separately registered by filename.

For WoW M2s, the placeholder/fallback shell returned for a missing model has `model->m2->file == NULL`. Any renderer-side
classification code that walks M2 arrays must short-circuit on that condition before reading file-backed arrays. The regression
smoke test lives in `games/world-of-warcraft/game/tests/t_smoke.c` and is runnable with:

```sh
make test-wow-engine PATTERN='wow_smoke.missing_m2*'
```

## Keep file-format owners separate

WoW M2 geometry and animation remain in `renderer/m2/r_m2.c`; WDBC file images, indexes, schema offsets, and character-data
resolution remain in `renderer/m2/r_dbc.c`. The boundary passes resolved value structs and texture paths, never raw DBC records.

## Hidden-header allocation for free-function dispatch

`FS_MmapFile` stores metadata (size, flags) in 16 bytes before the returned pointer. The free function reads this header to decide which deallocation strategy to use (munmap vs MemFree). This keeps the API surface clean — one allocate function, one free function, no side-channel.

Used also in the UI pool allocator and renderer shader caches where mixed allocation sources need a single free path.

## Table-driven parsing for keyed/text formats

All UI layout engines (`stb_fdf.h`, `stb_sc2layout.h`, `stb_wowxml.h`), SLK tables (`stb_slk.h`), and catalog XML (`sc2_map.c`) use DDX-style schema tables + generic loop dispatch:

```c
static const struct field_s {
    const char *name;
    ptrdiff_t   offset;
    int         type;
} fields[] = {
    { "Width",  offsetof(FRAMEDEF, Width),  FLOAT },
    { "Height", offsetof(FRAMEDEF, Height), FLOAT },
    { "Text",   offsetof(FRAMEDEF, TextStorage), TEXT },
};
// then one generic loop: for each token, walk table, dispatch by type
```

No manual `if/else`, `strcmp` ladders, or ad hoc token handlers. Adding a field is one table entry.

- **WC3 FDF (`games/warcraft-3/common/stb_fdf.h`)**: `fdf_parseItem_t items[]` and `fdf_parse_class_t classes[]` use `FDF_F(field, type)` descriptors with typed conversion callbacks (`FDF_ParseFloat`, `FDF_ParseVector2`, `FDF_ParseColor`, etc.).
- **SC2 Layout (`games/starcraft-2/common/stb_sc2layout.h`)**: `sc2_frame_attrs[]` (XML attributes), `sc2_frame_fields[]` with `sc2FrameFieldType_t` typed dispatch (`FLOAT`, `RESOLVED_FLOAT`, `BOOL`, `COLOR`, `DESC_FLAGS`, `PROJECTION`, `LAYER_*`, `TEXTURE_TYPE`), `sc2_child_tags[]` (child node dispatch), and enum tables (`sc2_sides[]`, `sc2_positions[]`, `sc2_frame_types[]`).
- **WoW FrameXML (`games/world-of-warcraft/menu/stb_wowxml.h`)**: `uiwow_node_types[]` (tag-to-type and flags), `uiwow_script_tags[]` (script handlers), `uiwow_button_part_tags[]` / `uiwow_button_text_tags[]` (button layers), `uiwow_shared_attrs[]` (typed XML attributes), and `uiwow_point_factors[]` (anchor positioning factors).

Treat the table as a grammar, not merely a lookup optimization. Ordinary scalar productions use `{ name/column/tag, offset, type, count/flags }`; nested, repeated, versioned, or context-sensitive productions use explicit callbacks referenced by that grammar. Keep data-layer merging and inheritance after decoding so an absent field remains distinguishable from an authored zero.

Parser descriptors share `bzFieldType_t` from `common/shared.h`. These values describe conversion and destination-storage contracts, not source syntax: DBC's `STB_DBC_STR` aliases pointer-valued `BZ_FIELD_CSTR`, while SC2 XML's string field aliases bounded inline `BZ_FIELD_CHAR_ARRAY`. The source parser remains responsible for little-endian reads versus textual conversion and must reject shared types it cannot decode. Shader descriptors use the same table pattern but keep `uniformType_t`; samplers, GLSL matrix forms, precision, and `glUniform*` dispatch are a GPU ABI rather than parser field types.

The same idea covers **binary** files: WoW DBC rows are read with a `stbDbcField_t` schema table
(`{ column, offsetof(struct, field), type, count }`) plus `Stb_DbcParseRows` / `Stb_DbcCacheDecode`, where a
file-shaped struct mirrors the consumed subset of a row and a per-version dispatch function picks the schema when a
table's layout shifts across client versions. See
[`docs/games/world-of-warcraft/dbc-reference.md`](games/world-of-warcraft/dbc-reference.md#reading-a-dbc--the-schema-pattern).

### Audit targets

The remaining high-confidence manual grammars are bounded and should be converted when their subsystem is touched:

- `games/world-of-warcraft/common/world_wow.c`, `renderer/wow/r_wowmap_adt.c`, and `game/g_gameobject.c`: simple WoW chunk-tag bindings; use the descriptor shape already present in `renderer/wow/r_wowmap_wmo.c`.
- `games/warcraft-3/menu/screens/single_player.c`: scalar campaign keys; keep indexed mission/file productions specialized.
- `games/warcraft-3/game/g_unit_ui.c`, `tools/m2tool.c`, and `tools/mpqtool.c`: direct string-to-value maps.

Do not mechanically table-drive ordinary control flow, two-way checks, or binary productions whose meaning depends on previously decoded context. The table must clarify the format grammar rather than hide it behind generic callbacks.

## format-driven parsing with sscanf

For data with known delimiters (comma-separated vectors, SCN strings, config values):

```c
sscanf(token, "\"%79[^\"]\"", name);
sscanf(token, "%f,%f,%f", &x, &y, &z);
```

Not character walking, not separator loops. The format string IS the parser.

## Function tables for cross-module boundaries

Renderer ↔ Game ↔ UI boundaries use function tables (`ri.*`, `gi.*`, `uiimport.*`). Each module exports a `*_GetAPI(import_t)` function. This is the Quake 2/3 pattern — no direct includes across module boundaries, no global function calls.

```c
// Renderer receives:
refExport_t R_GetAPI(refImport_t imp) { ri = imp; return re; }
// Then calls ri.FS_ReadFile, ri.Cvar_Get, etc.
```

## Static utils in nearby headers

Pure, reusable local helpers go in a small header as `static` functions (e.g., `sc2_utils.h`). Subsystem-owned helpers that touch globals stay in the `.c` file that owns that state. No dedicated header for a single tiny helper.

## flags & FLAG — implicit bool

```c
if (flags & FLAG)       // not: if ((flags & FLAG) != 0)
if (ent->flags & EF_DEAD) return;
```

## Pointer + count arrays (ARRAY macro)

A pointer and its element count are one unit, not two loose variables. Declare them with `ARRAY(type, name)`, then read the count and iterate through the macros — never touch `name##_count` directly:

```c
typedef struct { ARRAY(wowVec3_t const, vertices); ARRAY(WORD const, indices); } chunks_t;
// expands to: wowVec3_t const *vertices; DWORD vertices_count; WORD const *indices; DWORD indices_count;

FOR_EACH_ARRAY(WORD const, idx, chunks.indices)   // pointer loop
    draw_vertex(chunks.vertices[*idx]);

FOR_LOOP(i, ARRAY_COUNT(chunks.indices))          // index loop when i is needed
    emit(chunks.indices[i], i / 3);

if (IS_ARRAY_EMPTY(chunks.vertices))               // NULL pointer OR zero count
    fail();

ri.MemAlloc(ARRAY_COUNT(chunks.vertices) * sizeof(*chunks.vertices));
```

`ARRAY_COUNT(name)` reads the paired count; `IS_ARRAY_EMPTY(name)` tests both pointer and count; `FOR_EACH_ARRAY(type, it, name)` walks elements by pointer; `FOR_LOOP(i, ARRAY_COUNT(name))` covers index-based iteration. The macros live next to `FOR_LOOP`/`FOR_EACH` in `common/shared.h`.

## Enum over multiple booleans

```c
typedef enum { CLIENT_UI_GAME, CLIENT_UI_LOADING, CLIENT_UI_CINEMATIC } client_ui_state;
// not: BOOL in_game; BOOL in_loading; BOOL in_cinematic;
```

## Server-authored UI state via STAT bits

`playerState_t.uiflags` bitmask controls which UI layers are visible. The server sets bits, the client checks them. No client-side game-mode `if` ladders, no hardcoded level names.

## WoW UI: log once, keep going

```c
static LPCSTR last_missing = NULL;
void warn_missing(LPCSTR path) {
    if (last_missing != path) { fprintf(stderr, "UIWow: missing %s\n", path); last_missing = path; }
}
```

One warning per unique asset, then placeholder fallback. No per-frame spam, no silent skips.
