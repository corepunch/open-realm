# Browser platform — phase 1 foundation

This stage adds an opt-in Emscripten / SDL2 / WebGL 2 smoke-test target. It
**does not** compile or start OpenWarcraft III yet. The native build graph in
`Makefile` still produces ELF/dylib/DLL modules and must not be run under `emcc`
as if these were browser-loadable modules.

## Build

Activate an Emscripten SDK (`emsdk_env.sh`) and run:

```sh
make web-warcraft3-foundation
python3 -m http.server 8000 --directory build/web
```

Open `http://localhost:8000`. A dark canvas and a status of "WebGL 2 probe
running" indicate browser-side SDL2, WASM and WebGL 2 initialization. This
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

The output `build/web/engine/` includes the launcher helper scripts, but
**the OPFS importer is still not mounted into the WASM filesystem**. Existing
MPQ/game file loading cannot yet read the imported files. Do not upload or
ship retail data with this target.


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
