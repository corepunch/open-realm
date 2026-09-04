# WC3 Human02 memory investigation

## Baseline and method

Measured on Apple M1 / macOS 26.5.2 at `1ec256a6`, after the two server/entity allocation reductions.
`make run-map` selects ROC `Maps/Campaign/Human02.w3m`. Its recipe does **not** forward `ARGS`;
use the binary directly for a bounded run:

```sh
make -j4 openwarcraft3 install-share
build/bin/openwarcraft3 -data 'data/Warcraft III' +map Maps/Campaign/Human02.w3m +com_frame_limit 6000
pgrep -x openwarcraft3
vmmap -summary <pid>
vmmap -w <pid>
heap -s --showSizes <pid>
```

The default logical 1024×768 window has a **2048×1536 drawable**, with **MSAA disabled**.
The uninstrumented process measured **464–471 MiB physical footprint** and approximately **99 MiB live malloc**.
After removing instrumentation and rebuilding, a matching capture measured **468.1 MiB** (468.3 MiB peak).
RSS, VM reservation, and physical footprint are different metrics; macOS compressed/private and graphics memory
make RSS alone misleading. Do not add aliased graphics mappings or read-only shared libraries to the footprint.

For allocation attribution, use a separate bounded run:

```sh
env MallocStackLogging=1 build/bin/openwarcraft3 -data 'data/Warcraft III' +map Maps/Campaign/Human02.w3m +com_frame_limit 6000
malloc_history <pid> -allBySize > /tmp/wc3-allocations.txt
malloc_history <pid> -callTree -invert -ignoreThreads -chargeSystemLibraries > /tmp/wc3-allocation-tree.txt
```

Stack logging increased the observed footprint to about 486 MiB: use it for **attribution**, not the baseline.
Its allocation totals include VM mappings, allocator rounding, and potentially aliased backing stores; they are
not an additive physical-memory budget. For example, large AppKit catalog `mmap` entries are not equivalent RAM savings.

Temporary `fprintf(stderr, ...)` counters measured geoset dimensions, frame occupancy, ground vertices,
loopback high-water marks, sheet cursor positions, and resident model references. They were removed after ROC/TFT runs.
No optimization or capacity change was implemented by this investigation.

## Largest opportunities

| Owner | Evidence on ROC Human02 | Proposed work and estimated savings |
|---|---|---|
| MDX GL buffers | 744 geosets, six buffers each; allocation stacks attribute ~69.9 MiB to their backing allocations | Consolidate into one vertex buffer plus one index buffer per geoset: roughly **46 MiB** less backing allocation. Pack across geosets/models for a potential **60–67 MiB** reduction; measure actual footprint afterward. |
| FDF frame pools | Two 4096-slot arrays, **26.78 MiB each**; only 84 server frames and 1167 UI frames used | Allocate stable frame storage as needed while preserving IDs/pointers and capacity semantics; approximately **40–45 MiB** opportunity. Do not merely choose caps from this one map. |
| Menu/loading residency | Main-menu glue allocations account for ~31.8 MiB, campaign loading model ~15.2 MiB in allocation stacks; still resident during gameplay | Fix ownership and registration lifetime for models **and textures**; up to **~47 MiB** attributed storage, including **18.2 MiB of MDX buffers already counted above**. Shared assets and driver retention reduce the actual saving. |
| Loopback queues | Two 8 MiB arrays; largest observed queued payload 86,185 bytes | Restore a justified packet/queue budget or allocate queue storage on demand: **~15 MiB** opportunity without changing entity limits or wire structs. Stress-test bursts and large startup packets. |
| Texture storage | Driver CPU texture-level allocations ~37.5 MiB; GPU texture allocations ~57.8 MiB | Asset lifetime first, then investigate compressed storage for suitable world art and single-channel masks/fonts. Potential further tens of MiB; requires measured implementation and image-quality checks. These figures overlap menu/loading assets. |
| Terrain scratch | `whole_map_buffer` retains 99,846 × 44 = 4,393,224 bytes after upload | Free construction scratch after all ground layers: **4.19 MiB**. Also fix loss of the previous allocation on growth/map reload. |
| Drawable surfaces | IOSurface region total ~63 MiB at 2048×1536 | Existing `+vid_mode 0` produces 1280×960 and measured ~420 MiB footprint; IOSurface mappings fell to ~26.5 MiB. This is a quality/resolution tradeoff, not an asset-memory fix. |

Savings are estimates from measured owners, **not benchmarked patches**, and must not be summed without removing overlaps.
The first four changes plus scratch reclamation suggest roughly **120–170 MiB** of opportunity, depending on packing and
asset sharing. That points initially toward approximately **300–350 MiB**, not a proven 2–3× reduction.
Getting near **230 MiB / 2×** likely requires additional texture/terrain work and/or a smaller drawable.
**~155 MiB / 3× at unchanged quality is not established by these measurements.**

The bounded pathfinding accelerator adds one generation-stamped node array plus one index heap per pathing cell. With
the current 24-byte node and 4-byte heap index this is 1.75 MiB on a 256x256 pathmap and 5.25 MiB on a 384x512 pathmap.
The storage is global scratch, not per unit; mover persistence adds two points, one radius, and one validity flag.

## MDX: tiny streams incur large backing allocations

`games/warcraft-3/renderer/mdx/r_mdx_load.c:R_SetupGeosetVertexBuffer` creates separate buffers for
positions, UVs, normals, skin/weights, all-white colors, and indices. The white stream contains no varying information.
The five-stream arrangement dates to `62e559a76`; `2d28b3c13` added the white-color buffer for the unified shader.

For the measured geosets, rounding each allocation to the observed 16 KiB driver granularity gives:

| Layout | ROC: 744 geosets | TFT: 740 geosets |
|---|---:|---:|
| Current six buffers | 69.88 MiB | 69.75 MiB |
| 40-byte vertex + index buffers, per geoset | 23.84 MiB | 24.06 MiB |
| Unrounded vertex/index payload | 2.87 MiB | 3.14 MiB |

The 40-byte calculation retains position/normal/UV/skin/weights and removes the redundant color stream.
Use a constant white vertex attribute when the array is disabled, restoring it at the relevant draw boundary;
generic attribute values are context state, not a substitute for explicit draw-state ownership.
Alternatively retain white in a shared interleaved vertex format; allocation-count savings remain the main benefit.
Use shared model buffers with geoset offsets to eliminate the remaining per-geoset rounding. Preserve CPU positions/indices
needed by picking, collision, and cliff construction; do not blindly free every CPU array after GL upload.

## FDF: capacity and per-frame payload

`games/warcraft-3/common/stb_fdf.h` defines `FRAMEDEF frames[MAX_UI_CLASSES]` separately in game and UI libraries.
`sizeof(FRAMEDEF) = 6856`; the embedded `Menu` alone is 2788 bytes, including 32 menu items, even for non-menu frames.
`UI_ClearTemplates` clears the whole pool. Baseline VM regions showed approximately 27 MiB private game data and
23 MiB private UI data, so these are not merely an 80 MiB-style untouched reservation.

| Occupancy at bounded shutdown | ROC | TFT |
|---|---:|---:|
| Game frames | 84 | 88 |
| UI frames | 1167 | 955 |
| Pool capacity, each | 4096 | 4096 |

Both modules still use frames; **do not simply delete the server pool** based on the general client-HUD architecture summary.
Pointers/relative frame IDs rely on the frame table, so growing it by moving `realloc` is unsafe without auditing those contracts.
Per-type payload allocation or carefully designed stable storage can reduce memory without reducing supported frame counts.
Menu payload savings overlap pool-sizing savings.

When instrumenting this header, explicitly rebuild both consumers, e.g.
`make -W games/warcraft-3/menu/menu_main.c -W games/warcraft-3/game/g_main.c openwarcraft3`:
the UI target does not list this common header directly as a prerequisite.

## Asset lifetime

`UI_PreloadGlueSceneModels` loads main-menu and panel models even on the direct-map startup path.
`UI_ResetGlueSceneModels` only zeroes its cache; it does not release the acquired references.
At renderer shutdown, registry logging still showed references for `MainMenu3d`, the top-left/right panels,
the campaign `LordaeronBackground`, loading bar, logo, and other glue assets.

`renderer/r_model.c:R_FreeUnusedModels` only collects unreferenced models from an older registration sequence.
`renderer/r_texture.c:R_ReleaseTexture` deliberately preserves every cached texture;
`R_ShutdownTextureCache` is the only whole-cache reclamation point. Releasing a menu model alone therefore
does **not** recover its texture memory. Use Quake-style registration marking/ownership so shared gameplay textures survive
and unused menu/loading resources can be reclaimed after loading finishes, then reacquired on return to menus.

The measured model registry had 220 ROC / 219 TFT entries and no duplicate normalized stems differing only by `.mdl`/`.mdx`.
Do not blame extension aliases for this capture.

## Other owners and misleading targets

- **Sheets reserve 80 MiB but do not consume 80 MiB physical memory.** `sheet.c` has million-entry cell/row/field pools
  plus an 8 MiB string pool, explicitly marked as a PoC in `5f0135ce6`. ROC cursor usage was 150,054 cells,
  7,568 rows, 158,724 fields, and 1,438,267 text bytes: about **8.61 MiB** of payload, consistent with the VM region.
  TFT uses 336,890 cells, 12,179 rows, 346,769 fields, and 2,414,519 text bytes: about **18.23 MiB**.
  Dynamic storage improves capacity/lifetime but does not save the untouched reserve as physical RAM.
- **Entities are no longer the main owner:** the game edict allocation is ~4 MiB and the server snapshot ring ~6 MiB.
  Keep the `UPDATE_BACKUP` history requirement; do not repeat the initial ring-size reduction without preserving history.
- **Loopback history:** `b5a725f8a` grew each queue from 256 KiB to 8 MiB for server-authored HUD traffic.
  The file's introductory 256 KiB comment is stale. `NET_Init` clears the entire 16 MiB pair. TFT high-water was 86,599 bytes.
- **Terrain geometry:** use the measured layer counts rather than estimating from map area:
  26,148 + 58,848 + 78,294 + 78,480 + 81,306 + 84,930 = **408,006 vertices**, or **17.12 MiB** at 44 bytes each.
  Indexed geometry and removal of unused vertex fields are secondary opportunities, with UV seams and layer blending preserved.
- **Fonts:** `renderer/r_font.c:R_LoadGlyphSet` expands an 8-bit coverage bitmap into RGBA before upload.
  Single-channel coverage with the matching shader sampling contract can reduce atlas storage without reducing font resolution.
- **Texture compression is additional work:** BLP1 JPEG/paletted sources are decoded to RGBA, not supplied as GPU block-compressed
  data. A compressed path requires conversion/caching and alpha/quality validation, rather than merely preserving source blocks.
  Keep fonts/UI text lossless and preserve mipmap/minification behavior.
- **Small leak:** BLP1 loader metadata remains allocated after `R_LoadTextureBLP1`; `blp1_release` exists but is not called there.
  Allocation stacks attribute only ~0.33 MiB to the info blocks in this run. Fix it, but it is not the missing hundreds of MiB.

## Verification scope

Diagnostic and restored binaries were built; ROC and TFT Human02 counters ran with `+com_frame_limit 300`.
The snapshot regression from `0094a688` still expected only `clients × MAX_PACKET_ENTITIES` after `1ec256a6` restored history.
A targeted log measured `clients=4 packet=256 history=16 entries=16384`; the test expected 1024 and prohibited the correct value.
The test now asserts the complete history-ring capacity and verifies that it remains below full-world snapshot storage.
Runtime allocation and network structs were not changed.
`make test` passed after that correction (including 113/113 server-net assertions and 928/928 in-engine WC3 assertions).
The socket suites require execution outside the filesystem/network sandbox; the initial sandboxed run could not bind UDP sockets.
Any implementation should additionally cover repeated map changes, return-to-menu/loading transitions, representative large maps,
network bursts, and visual comparison of both archive variants. Re-measure without stack logging at the same drawable and scene age.
See [performance](performance.md) for CPU hot spots and [loading and assets](loading-and-assets.md) for registration lifecycle.
