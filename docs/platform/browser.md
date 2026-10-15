# Browser platform — experimental Warcraft III port

For end-user build, serving, import and run instructions, see
[Build and run OpenRealm Warcraft III in a browser](browser-run.md). This page
records implementation stages, ownership and technical limitations. The
foundation target below remains a separate smoke test, not the playable engine.

The initial stage added an opt-in Emscripten / SDL2 / WebGL 2 smoke-test
target. The repository now also has an experimental statically linked browser
engine; follow [the run guide](browser-run.md) to build and launch it. The
native build graph in `Makefile` still produces ELF/dylib/DLL modules and must
not be run under `emcc` as if these were browser-loadable modules.

## Build

Activate an Emscripten SDK (`emsdk_env.sh`) and run:

```sh
make web-warcraft3-foundation
python3 port/web/serve.py
```

Open `http://127.0.0.1:8000/`. The server adds the COOP/COEP headers required
for Emscripten pthreads and shared memory. Do not open generated HTML from
`file://` or serve it with a plain static server that omits those headers.
A dark canvas and a status of "WebGL 2 probe running" indicate browser-side
SDL2, WASM and WebGL 2 initialization. This
is **not** proof that OpenRealm's actual GLES3 renderer is WebGL 2 compatible.
No Warcraft game files are used or distributed.

## Next integration stages

### On-screen browser controls

The browser launcher overlays **Esc** (send an Escape key press to the game,
including during cinematics) and **fullscreen** (toggle fullscreen for the
canvas and controls together) at the upper-right corner of the game view.
These are browser DOM controls, not game UI frames, and remain available after
the canvas transfers to the rendering pthread. Fullscreen requires a user
gesture and may be unavailable under restrictive embedding policies.

1. Inventory module entry points and shared-library dependencies in the
   `shared`, `client`, `server`, `renderer`, `games/warcraft-3` and `jass`
   modules. Introduce explicit static registration instead of native dynamic
   loading where required; retain the existing game/client ABI boundaries.
2. Port the native OpenRealm entry point and frame pump to browser scheduling.
3. Audit the existing GLES3 renderer and shader paths against WebGL 2.
4. Provide a WASM filesystem bridge with random-access archive reads and
   persistent local game data.
5. The experimental launcher, installer and AudioWorklet playback have since
   been added. Save persistence and PWA support remain future work.

The source-only platform probe deliberately avoids browser-pthread requirements,
proprietary retail archives, new gameplay code and modifications to existing
native module ownership.

## Phase 2 — engine frame scheduling and module audit

`common/main.c` now provides `OpenRealm_Frame`, shared by the native busy loop
and the Emscripten callback (`emscripten_set_main_loop_arg`). It preserves the
online → server → client frame order, clock deltas, fast-forward behavior and
frame-limit shutdown. Native frame throttling remains native-only. Dedicated
stdin polling is excluded from Emscripten.

Run `make web-warcraft3-engine-check` for a **source-only** audit of this
integration and the existing module entry points. This is not an Emscripten
compiler test and does not produce a playable game.

The inspected modules already call `GetGameAPI`, `R_GetAPI` and `M_GetAPI`
through direct C symbols. They do not currently require `dlopen` shims for
these entry points. A subsequent patch must link the actual common, server,
client, game, renderer, menu, shared, JASS and sheet modules as a single wasm
program, resolving portability issues explicitly rather than weakening linker
checks. The `web-warcraft3-foundation` target remains a standalone graphics
probe in this phase. Browser file storage and installation are not yet present.

## Phase 3 — browser-local game data importer

The [ISO 9660/Joliet reader](browser-iso9660.md) can now be invoked by the
installer worker with up to eight ISO files plus direct game archive files in
one selection, to selectively copy recognized Warcraft III game MPQs through
the existing OPFS generation writer. It requires `War3.mpq` in the combined
selection, rejects duplicate archive names and checks estimated storage quota
before staging when the browser provides an estimate. Imports still replace,
rather than merge with, the existing installation. Both browser file pickers
now accept ISO images alongside standalone archives and show detected files
and copy progress (patch 4). Patch 5 extracts the 44-map profile observed
on one original RoC `Setup.mpq` disc, without executing installer code;
unknown layouts are reported and skipped.

The platform probe page now exposes a launcher-only file importer.
`site/installer.js` sends File objects to `site/install-worker.js`, which
reads them in 1 MiB chunks and stores them under OPFS
`openrealm/game-data/wc3/`. A versioned `installation.json` manifest is
written last, after copying completes. The worker supports cancellation and
removal. OPFS is origin-scoped and no data is sent to a server.

This is a *storage foundation*, not the game-data engine mount. The importer
does not claim that every MPQ is sufficient or valid for a playable Warcraft
installation. It supports selected ISO 9660 images, loose MPQ/W3M/W3X
files and validated nested `Maps/` outputs from recognized original RoC
`Setup.mpq` archives; it does not offer general-purpose installer extraction,
CASC or ZIP support. The experimental engine uses a WasmFS OPFS mount to expose the
selected archive directory to the native MPQ reader; this needs browser
runtime validation. Interrupted commits can leave orphan files,
and replacing an existing installation is not a fully atomic transaction;
the engine must not trust file presence without validating a manifest and
actual archive contents. Avoid importing over an active game session.

The existing experimental WasmFS mount uses seekable reads; end-to-end
browser verification and complete installation validation remain necessary.
The probe remains only a probe.

## Phase 4 — experimental static engine link

Run `make web-warcraft3-engine` with an activated Emscripten SDK. This new
target invokes `port/web/build-engine.py`, which compiles the real native
module source groups into separate WASM objects and attempts a single link.
The existing C entry points (`GetGameAPI`, `R_GetAPI`, `M_GetAPI`) are retained.
The normal native Makefile outputs are not replaced.

This is an **experimental compile/link target**, not a claim of a successful
build or a working game. The first compiler diagnostics should be resolved in
follow-up browser-port patches. In particular, browser networking, audio,
OpenGL ES/WebGL API compatibility, filesystem persistence, generated font
header production and startup asset mounts require additional integration.

The output `build/web/engine/` includes the launcher helper scripts. The page
validates its active OPFS installation before loading Emscripten's generated
script, then passes the active archive directory to native startup. Existing
MPQ/game file loading and full runtime compatibility still require browser
verification. Do not upload or ship retail data with this target.


## Phase 10 — engine build source preflight

The engine build script generates per-module unity compilation units beneath
`build/web/engine`. Quoted include paths are relative to **that generated file**,
not to the repository working directory. The generator therefore writes
relative paths back to each original source file.

Run `make web-warcraft3-preflight` to validate the source groups and generate
unity inputs without invoking Emscripten. Then run `make web-warcraft3-engine`
with an activated SDK and capture the **first** compiler or linker failure.
The preflight checks file existence and include layout only; it cannot prove
that the browser code links, renders or boots. Do not redistribute game data.

### Browser build compatibility (wasm32)

The experimental engine build excludes `g_save.c` and links browser-only
`web_save_unsupported.c` functions returning failure. WC3 save/load is therefore
**unsupported** in this build: `g_save.c` encodes 64 bits of relocation data into
a pointer-sized slot, and wasm32 pointer slots are only 32 bits wide. The native
save format and its assertions remain unchanged. A browser save implementation
must use explicitly sized on-disk relocation records and regression coverage
before enabling this feature.

Browser builds also exclude `BZ_CLIENT_WORLD` from the combined game/engine
compilation flags; server/game routing owns the WC3 pathing implementation,
while client-world helpers must not be compiled into the game unity module.
`-sWASMFS=1` is a final-link setting, not a compile-only setting.

### Static WASM module symbol ownership

The native `libgame` and `libmenu` each compile their own
`stb_fdf.h` implementation, `frames` registry and host callbacks. These
must remain distinct in the browser's single linked executable. The
browser-only `web_fdf_namespace.h` provides module-scoped link names when
compiling game and menu; `GetGameAPI` and `M_GetAPI` are not renamed.

`games/warcraft-3/common/campaign_progress.c` is pure shared functionality:
it is now compiled exactly once as `wc3_common.o`, rather than being
repeated in both game and menu objects. `world_w3.c` remains owned by
the engine object. These decisions do not change native shared libraries.

### WASM FDF parser symbol ownership

The Warcraft III game and menu are separately linked native modules and
independently instantiate `stb_fdf.h` with `STB_FDF_IMPLEMENTATION` and
`STB_FDF_GLOBALS`. The parser uses module-specific `UI_*` callbacks and
separate `frames` and `strings` registries. Both instances must remain
independent in the static WASM binary. `port/web/src/web_fdf_namespace.h`
renames the parser functions, enum tables and state together with the UI
helpers for the game/menu unity objects. It is deliberately not applied to
the universal engine or native builds. Sharing one complete FDF parser between
game and menu would incorrectly bind its state and host callbacks to one
module.

### Text batching and the browser thread stack

A symbolized main-menu frame crash reached `process_text` through
`R_DrawText` and `R_GetTextSize` after the renderer and UI had initialized.
The previous `textBatch_t` included 1,020 vertices as a local variable even
while measuring text (when no drawing or batching occurs). Nested alignment
measurement therefore placed unnecessarily large vertex arrays on the limited
WebAssembly engine pthread stack. The renderer now allocates the batch through
`ri.MemAlloc` only for drawing, flushes it, and frees it before returning.
Measurement still uses the same positioning and glyph-advance logic without
allocating a batch. Native rendering and text output should remain unchanged.

### Renderer on the browser engine worker

The browser build uses `PROXY_TO_PTHREAD` for OPFS, but SDL creates its GL
context through EGL. Emscripten proxies EGL context creation to the main
thread, so the HTML canvas must stay there. Browser builds use the
`OFFSCREEN_FRAMEBUFFER` rendering path instead of transferring the canvas to
the worker. Emscripten owns frame timing; native swap-interval behavior is
unchanged. COOP/COEP cross-origin isolation is required for pthreads.

### SDL/EGL context ownership on browser pthreads

The engine uses `PROXY_TO_PTHREAD` for WasmFS OPFS. SDL creates the renderer
context through EGL, and Emscripten's EGL context operations are proxied to
the browser main thread. Do **not** combine this SDL/EGL path with transferring
`#canvas` to the pthread: the main-thread EGL implementation then attempts
`getContext()` on an already-transferred HTML canvas and throws
`InvalidStateError`. The browser link deliberately uses
`-sOFFSCREEN_FRAMEBUFFER=1` without `OFFSCREENCANVAS_SUPPORT` or
`OFFSCREENCANVASES_TO_PTHREAD`, leaving the canvas on the main thread while
allowing the worker to submit WebGL operations through Emscripten's proxy.
COOP/COEP isolation is still necessary for pthreads. This resolves the
canvas-ownership conflict in configuration; WebGL rendering remains to be
validated on the target Emscripten SDK and browser.

### Direct WebGL context for browser renderer

The browser renderer uses Emscripten's HTML5 WebGL API directly instead of
creating and swapping a context through SDL/EGL on the proxied game pthread.
SDL still owns the window and input. WebGL 2 uses
`EMSCRIPTEN_WEBGL_CONTEXT_PROXY_ALWAYS` and explicit swap control; the
`-sOFFSCREEN_FRAMEBUFFER=1` link setting supplies the presentation buffer.
Native rendering remains unchanged. This is an experimental interoperability
fix requiring runtime verification of GL initialization, frame presentation,
resize and input handling.

### Worker-owned WebGL context (follow-up)

Direct WebGL context creation in the browser renderer still deadlocked with
`EMSCRIPTEN_WEBGL_CONTEXT_PROXY_ALWAYS` after shader loading: a synchronous GL
proxy on the engine pthread can enter a recursive pthread mutex lock. The
browser build now transfers `#canvas` to the engine pthread with
`OFFSCREENCANVAS_SUPPORT`/`OFFSCREENCANVASES_TO_PTHREAD`, and the renderer
requests `EMSCRIPTEN_WEBGL_CONTEXT_PROXY_DISALLOW`. The renderer continues to
create the context directly via Emscripten, **not** via SDL/EGL; this distinction
avoids the earlier transferred-canvas EGL error. No native build changes.

Requires a cross-origin-isolated page and OffscreenCanvas support. Test worker
context creation, shader/resource initialization, frame presentation, resize,
input and shutdown in the browser; a successful C link alone is insufficient.

### Diagnosing browser worker traps

The normal optimized build omits useful names from WASM stack traces. When
startup reaches `Loading shaders succeeded` but a worker fails with
`memory access out of bounds`, `operation does not support unaligned accesses`,
or an Emscripten pthread/proxy-queue assertion, **do not** assume the renderer
or OPFS is the original cause from the last successful log line alone. A
secondary worker failure can follow a primary engine-thread failure.

Build a separate, unoptimized DWARF-symbolized version with:

```sh
source ~/emsdk/emsdk_env.sh
python3 port/web/build-engine.py --debug
```

The result is in `build/web/engine-debug/`; it leaves the standard
`build/web/engine/` untouched. Serve the debug directory using the same
cross-origin isolation headers as the normal pthread build. In Chrome DevTools
(Sources), use WebAssembly DWARF debugging to inspect the **first** failing
thread, its C call stack and source line. Include the failing assertion and
first symbolic stack in the issue report. If needed, use the DWARF-capable
`llvm-addr2line` shipped with the Emscripten SDK against the exact matching
`index.wasm` and the `index.wasm:0x...` offsets; offsets from optimized builds
cannot be looked up in a different debug binary.

Keep pthread checks enabled. Do not mask WebAssembly traps by relaxing
alignment, disabling mutex assertions or suppressing errors in proxy queues.

### Procedural textures and the browser thread stack

The symbolized startup trace identified a stack overflow in
`R_MakeLoadingIndicatorTexture` immediately after shader initialization. Its
128×128 RGBA scratch image previously occupied 64 KiB of the engine thread
stack. The selection-circle generator had the same allocation shape, and the
blob-shadow generator used another stack-allocated RGBA buffer. These buffers
now use the renderer's existing `ri.MemAlloc`/`ri.MemFree` lifetime around
`R_LoadTextureMipLevel`, matching normal texture upload ownership. The pixels,
texture formats, and non-browser rendering behavior are unchanged.

The change avoids needing to increase the default pthread stack solely for
these temporary images. Recheck browser startup after shader loading; other
large local buffers might still need their own targeted fixes.

### Earlier browser sound initialization limitation (superseded)

The browser executable uses `PROXY_TO_PTHREAD`, placing engine startup on a
worker. SDL 2's Emscripten audio device setup currently accesses JavaScript
`audioContext` state that is unavailable in that worker and throws before
client initialization completes. For now `S_Init` declines to initialize audio
on Emscripten and preserves the normal uninitialized-sound guards. At that
stage browser playback was silent. The later AudioWorklet integration below
provides browser playback; this section records the earlier constraint rather
than current behavior. The native SDL audio path remains unchanged.

The current browser audio adapter uses a shared sample ring and AudioWorklet;
do not call the standard SDL Emscripten audio device opener from the engine
worker. Playback still needs checks across stalls, suspend/resume and device
sample rates.

### Bundled Warcraft III defaults

The browser engine link embeds the open-source `games/warcraft-3/share/`
folder under the virtual filesystem path `/share/warcraft-3/`. In particular,
`/share/warcraft-3/config.cfg` must be present when `Com_Init` reads the
shipped input bindings (`MOUSE1` selection, `MOUSE2` smart orders, etc.).
`build-engine.py --check` checks that the source config exists; the normal
`emcc` link bundles it using `--embed-file`. Rebuild the browser engine after
changing shared defaults. These files are not copied to OPFS or uploaded by
users: the OPFS importer continues to manage Warcraft III archives separately.

This built-in configuration is the read-only starting point; user config
loading and persistence are separate concerns. If HUD clicks work while
world selection/drag/orders do not, verify this bundle is included before
investigating mouse hit-testing.

### JASS callback stack usage

JASS filter, player-filter, AI integer callback, and trigger-condition evaluation
copy a full VM state to isolate callback context. These scratch VMs live on the
engine heap, not its limited WebAssembly pthread stack. In particular,
`GroupEnumUnitsOfPlayer` may evaluate many filtered units during campaign map
initialization. Allocation failures reject the corresponding filter or condition
without modifying the parent VM state.

### Browser audio (AudioWorklet)

On Emscripten the existing 44.1 kHz stereo S16 mixer feeds a dedicated
producer pthread under a sound mutex (not SDL's browser audio device).
The producer publishes samples to a shared Wasm memory ring; an AudioWorklet
consumes them on the browser's real-time audio thread and resamples to the
AudioContext output rate. The first user gesture unlocks AudioContext.
The audio worklet script is copied alongside index.js by the browser build.
The native SDL backend is unchanged. Verify playback for UI sounds, positional
unit acknowledgements, dialogue and music; also verify tab suspend/resume,
loading stalls and output-device sample rates. Audio may underrun during stalls.

The patch 4 UI adds `.iso` to the browser file input and reports ISO scanning,
validated MPQs with their source disc and size, copy progress, and final
manifest commit. Patch 5 extracts known installer-only map contents
from selected original RoC `Setup.mpq` layouts, but does not support
incremental RoC/TFT imports: select both discs in one operation.
Patch 6 originally rejected the listed September 2002 demo disc by its
`War3Demo.ico` and `SCDemo.exe` files. The later demo import patch now accepts
that disc separately, imports its base MPQ and `RetailMaps/` maps, and
persists a clearly labelled demo edition across visits. Patch 6 can also recover *named* whole
expansion MPQ members from a TFT installer. The May 2003 TFT disc's internal
`Setup.mpq` layout has not been verified against the actual retail bytes.
Patch 7 scans anonymous uncompressed installer blocks for nested MPQs,
classifies known base and expansion roles by internal file-path hash evidence,
and reports unknown archives without guessing filenames. An incomplete TFT
selection still fails without replacing the previous installation. See
[disc import format limitations](browser-iso9660.md).

The `web-warcraft3-foundation` make rule copies `installer.js`,
`install-worker.js`, `iso9660.js` and `setup-mpq.js` into the probe output next to
`index.html`; the experimental engine build also ships these assets.
See [browser ISO reader](browser-iso9660.md) for the worker event contract and
headless UI tests.

### ISO installation hardening

The browser installer now validates MPQ declared sizes and hash/block table
bounds before storing files; the launcher performs equivalent checks before
starting the engine. Web Locks prevent concurrent installation commits across
tabs, and running game tabs hold leases so cleanup cannot remove their OPFS
archive generation. See [browser filesystem](browser-filesystem.md) for lock
names, generation retention, manifest completeness and remaining limitations,
and [ISO import details](browser-iso9660.md) for real-disc verification gaps.

## Experimental static hosting on GitHub Pages

See [GitHub Pages deployment and Service Worker example](browser-github-pages.md).
This is documentation and reference code only; automatic registration, startup
isolation gating, Pages deployment and complete offline caching are not enabled.
