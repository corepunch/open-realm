# FS / VFS / MPQ Loading Architecture

## Stack overview

`common/common.c` owns the full filesystem layer. All game code reads files through it.

| Function | Input | Output | Memory | Free with |
|----------|-------|--------|--------|-----------|
| `FS_ReadFile(name, &size)` | loose or MPQ path | `HANDLE` (heap) | `MemAlloc` + null terminator | `FS_FreeFile` |
| `FS_ReadFileQ3(name, **buf)` | loose or MPQ path | `int` size | same as above, Q3-style | `FS_FreeFile` |
| `FS_ReadLooseFile(name, &size, extra)` | loose-file only | `HANDLE` (heap) | `fopen/fread/MemAlloc` | `FS_FreeFile` |
| `FS_MmapFile(name, &size)` | loose-file only | `void *` | `mmap` backed by file | `FS_MunmapFile` |
| `FS_ReadFileAll(name, cb, ud)` | loose or MPQ path | none (callback) | same as FS_ReadFile | caller owns buffer in callback |

`FS_ReadFile` tries MPQ archives first (`FS_OpenFile` → StormLib), falls back to `FS_ReadLooseFile`.

`FS_Init` always mounts `share/` as the built-in loose asset root. This is the tracked home for project-authored layouts such as
`share/UI/FrameDef/OpenWarcraft3/CampaignList.fdf`; mounted game archives remain authoritative for Blizzard paths. `-data` and
`extra_data` add further loose roots and archives after initialization.

## refImport_t FS surface

`client/tr_public.h` — `refImport_t`:
- `int (*FS_ReadFile)(name, **buf)` — Q3 style
- `void (*FS_FreeFile)(buf)`
- `void *(*FS_MmapFile)(name, *size)` — added 2026-08
- `void (*FS_MunmapFile)(ptr)` — added 2026-08

Wired in `client/cl_main.c` where `re = CL_GetRendererAPI(...)` is called.

### Mmap file hidden header

Every `FS_MmapFile` result has a 16-byte header before the returned pointer:
```
[mmap_size: uint32 | flags: uint32 | padding: uint64] [data...]
                                                 ^-- returned pointer
```

`FS_MunmapFile` reads the header to determine whether to `munmap` (mmap'd) or `MemFree` (fallback heap). **Do NOT mix with `FS_FreeFile`.**

## SC2 Map Loading — reference pattern

`SC2_MapLoad` in `games/starcraft-2/common/sc2_map.c`:

1. `sc2_source_open` → host `read_file` → `FS_ReadFileQ3` — one heap read of the entire `.SC2Map`.
2. `SFileOpenArchiveFromMemory` — the MPQ layer holds a `const BYTE *` into that buffer.
3. Sub-file reads decompress into `sc2_alloc` buffers, then free after use.
4. Catalog `.SC2Data` archives follow the same pattern: read → archive-from-memory → extract XML → parse → free.
5. Binary terrain layers are read once and kept alive for the session.

**Why this is clean:** only one outer-file copy. All internal access is pointer arithmetic into that buffer. Compressed data must be decompressed (unavoidable), but uncompressed sections can be walked in-place.

## WoW Map Loading — now improved with mmap

`R_RegisterMap` in `games/world-of-warcraft/renderer/wow/r_wowmap.c`:

1. `ri.FS_ReadFile(wdt_path, &data)` — reads `.wdt` once (~130 KB). Parsed, freed.
2. `Wow_LoadCameraAdts()` per frame → `Wow_LoadNearbyAdts` reloads the tile window.
3. `Wow_LoadAdtFile` loads up to `(2*WOW_ADT_RADIUS+1)²` ADT tiles (~200–800 KB each).

**After 2026-08:** `Wow_LoadAdtFile` uses `ri.FS_MmapFile` / `ri.FS_MunmapFile`. For loose files:
- Avoids full `fread` copy into heap
- OS can evict cold tile pages under memory pressure
- Falls back to `ri.FS_ReadFile` if `FS_MmapFile` is NULL or returns nothing (MPQ source, Windows)

`Wow_LoadAdt` takes `BYTE const *data` and pointer-walks the chunk tree — zero secondary heap allocations during parse. The mmap pointer works identically to a heap pointer.

`CM_WowTerrainHeightAtPoint` keeps a bounded 16-tile LRU of parsed heightfields. Height queries therefore reuse the decompressed `MCVT` data while retaining a fixed memory budget; a tile is evicted only when the active query set exceeds that budget. M2 character-component texture existence probes use a session cache (`m2_known_textures`) so failed and successful candidate paths are each read from MPQ at most once.

These are resident metadata/data caches, not an instruction to decompress every MPQ asset at startup. Textures and models remain lazy and use the renderer's loaded-resource cache; only assets that are actually referenced become resident. The texture registry is an unbounded session hash keyed case-insensitively by archive path; both successful loads and missing-path placeholders are registered. Do not cap it at the network `MAX_IMAGES` configstring count: WoW world rendering references thousands of renderer-local textures, and dropping later entries turns every draw into another MPQ search.

Modified character atlases use a separate bounded 16-entry LRU keyed by model, appearance, equipment, and display ID. A cache
hit reuses the rendered 256×256 atlas; a miss re-composites into the oldest render target. Classic `CharSections.dbc` can name
optional overlays absent from the mounted archives—for example female tauren
`Character\Tauren\Facial{Upper,Lower}Hair00_00.blp`. Register those misses once and omit the absent optional layer from the
composite; never paint the magenta missing-texture placeholder into the character skin.

## Key invariants

- Never pass an `FS_MmapFile` result to `FS_FreeFile` — will `MemFree` an mmap'd address.
- Never pass an `FS_ReadFile` result to `FS_MunmapFile` — will `munmap` a heap pointer.
- MPQ's `SFileOpenArchiveFromMemory` does NOT free the backing buffer on close — caller owns it.
- `FS_MmapFile` is macOS/Linux only (no Windows implementation); callers handle the NULL fallback.
