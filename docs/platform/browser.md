# Browser platform — phase 1 foundation

This stage adds an opt-in Emscripten / SDL2 / WebGL 2 smoke-test target. It
**does not** compile or start OpenWarcraft III yet. The native build graph in
`Makefile` still produces ELF/dylib/DLL modules and must not be run under `emcc`
as if these were browser-loadable modules.

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

1. Inventory module entry points and shared-library dependencies in the
   `shared`, `client`, `server`, `renderer`, `games/warcraft-3` and `jass`
   modules. Introduce explicit static registration instead of native dynamic
   loading where required; retain the existing game/client ABI boundaries.
2. Port the native OpenRealm entry point and frame pump to browser scheduling.
3. Audit the existing GLES3 renderer and shader paths against WebGL 2.
4. Provide a WASM filesystem bridge with random-access archive reads and
   persistent local game data.
5. Add a launcher, installer, audio, save persistence and PWA support.

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

The platform probe page now exposes a launcher-only file importer.
`site/installer.js` sends File objects to `site/install-worker.js`, which
reads them in 1 MiB chunks and stores them under OPFS
`openrealm/game-data/wc3/`. A versioned `installation.json` manifest is
written last, after copying completes. The worker supports cancellation and
removal. OPFS is origin-scoped and no data is sent to a server.

This is a *storage foundation*, not the game-data engine mount. The importer
does not claim that every MPQ is sufficient or valid for a playable Warcraft
installation. It currently supports flat selected MPQ/W3M/W3X files, and
does not extract CASC, ZIP archives or directories. Existing MPQ code has not
yet been connected to OPFS. Interrupted commits can leave orphan files,
and replacing an existing installation is not a fully atomic transaction;
the engine must not trust file presence without validating a manifest and
actual archive contents. Avoid importing over an active game session.

A later patch must mount this storage into Emscripten FS using efficient
random-access reads, validate complete installations, and integrate the
actual OpenRealm WASM engine. The probe remains only a probe.

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
