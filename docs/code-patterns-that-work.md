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

## Keep file-format owners separate

WoW M2 geometry and animation remain in `renderer/m2/r_m2.c`; WDBC file images, indexes, schema offsets, and character-data
resolution remain in `renderer/m2/r_dbc.c`. The boundary passes resolved value structs and texture paths, never raw DBC records.

## Hidden-header allocation for free-function dispatch

`FS_MmapFile` stores metadata (size, flags) in 16 bytes before the returned pointer. The free function reads this header to decide which deallocation strategy to use (munmap vs MemFree). This keeps the API surface clean — one allocate function, one free function, no side-channel.

Used also in the UI pool allocator and renderer shader caches where mixed allocation sources need a single free path.

## Table-driven parsing for keyed/text formats

FDF parser (`stb_fdf.h`), SC2 layout XML, and catalog XML all use a schema table + generic parser:

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
