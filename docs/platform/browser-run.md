# Build and run OpenRealm Warcraft III in a browser

> **Experimental:** use the native build for the best performance and stability.
> The browser target is a real, statically linked engine build, but full runtime
> compatibility is still under development. A long loading pause can look like
> a freeze; allow a minute or more before concluding that startup failed.
> Warcraft III save/load is unsupported in this browser build. Audio uses an
> AudioWorklet after a user gesture and can underrun during loading stalls.

This guide builds the playable browser engine. `make web-warcraft3-foundation`
builds a separate WebGL smoke test and does not run Warcraft III.

## Requirements

- Linux, Git, Python 3, Make, and a C build toolchain.
- An activated Emscripten SDK (`emcc` on `PATH`).
- Chrome or Edge recommended, with WebGL 2, OffscreenCanvas, OPFS and the Web
  Locks API available.
- Warcraft III game data that you are legally entitled to use. The project
  does not distribute Blizzard archives.

The browser must support cross-origin isolation for WebAssembly threads. The
included server configures the required COOP/COEP headers. Use `localhost` or
HTTPS; opening the HTML with `file://` will not work.

## Build

Install and activate Emscripten using the
[Emscripten SDK instructions](https://emscripten.org/docs/getting_started/downloads.html),
then build from the repository root:

```sh
source ~/emsdk/emsdk_env.sh
make web-warcraft3-engine
```

The output is `build/web/engine/`. For an unoptimized debug build with symbols:

```sh
python3 port/web/build-engine.py --debug
```

That output is `build/web/engine-debug/`. If the build reports that
`renderer/conchars_sysfont.h` is missing, generate the native font header first
with `make font`, then rerun the browser build.

## Serve and open the game

Run the repository server from the project root:

```sh
python3 port/web/serve.py
```

It serves `build/web/engine/` at <http://127.0.0.1:8000/> and adds the
cross-origin isolation headers needed for pthreads. To serve a debug build:

```sh
python3 port/web/serve.py --directory build/web/engine-debug
```

Open the URL in Chrome or Edge. The page checks browser-local game data and
starts the engine when it finds a valid installation. On first use, import
game data using the controls above the game canvas.

## Import game data

Select one or more `.iso`, `.mpq`, `.w3m` or `.w3x` files, then choose **Import
selected files**. The page shows files it recognizes, their source and copy
progress. The active installation is replaced only after a successful import;
each import must include the base `War3.mpq` archive.

- To install retail RoC and TFT together, select both discs in the same import.
  Imports do not merge with the previous installation.
- The importer recognizes named game archives and can identify some embedded
  archives in installer `Setup.mpq` files. It extracts maps from one known RoC
  installer layout. Unknown installer layouts are skipped or reported rather
  than guessed.
- The documented September 2002 demo ISO is supported separately. The page
  labels it as a limited demo installation; do not combine it with retail ISOs
  or standalone MPQs. Other demo disc layouts are not recognized.
- Direct MPQ uploads and loose maps are supported. `Setup.mpq` is installer
  data and is not installed as a game archive.

Files remain in the browser's origin-private file system (OPFS); they are not
uploaded. Keep using the same browser profile and URL, and avoid clearing site
data if you want to keep the installation. The importer and launcher use Web
Locks to protect active generations across tabs. Removal is available from the
page, but cannot delete a generation that an open game tab is using.

## Controls and troubleshooting

The canvas uses the Warcraft III defaults bundled with the browser build:
left-click selection, right-click smart orders and middle mouse button pan.
The **Esc** overlay sends Escape to the game, and the fullscreen control
toggles canvas fullscreen. Interact with the page once to unlock browser audio.

If the page says cross-origin isolation is unavailable, use the included
`port/web/serve.py` server and verify in DevTools:

```js
crossOriginIsolated // true
typeof SharedArrayBuffer // "function"
```

If it reports missing or invalid game data, check that the import completed,
that `War3.mpq` was selected or found on disc, and that the page is being opened
from the same origin where the data was imported. To try again, select the
required base and expansion sources together and re-import.

If startup remains on a loading screen, wait before treating it as a failure.
Browser rendering and asset loading can be slow; audio may underrun during long
stalls. Save/load is not available in the browser build. Some maps and campaign
flows may still fail because the engine and browser filesystem path have not
been verified across all retail game data.

## Current capabilities and limits

The browser target statically links the engine, renders through WebGL 2,
embeds Warcraft III default configuration, imports supported media into OPFS,
and mounts the selected generation through WasmFS. The page includes local
installer status, detected archives, import cancellation/removal, Escape and
fullscreen controls, and AudioWorklet playback.

Run the dependency-free browser tests from the repository root with:

```sh
node --test port/web/tests/*.test.cjs
```

These tests cover ISO parsing, synthetic archive imports, UI and launcher
behavior. They do not establish compatibility with every retail disc, map,
browser/device combination or a complete real-data playthrough. Use the native
application when reliability or performance matters.

For implementation details, see [Browser platform](browser.md),
[Browser filesystem](browser-filesystem.md), and
[Browser ISO import](browser-iso9660.md).
