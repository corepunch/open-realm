# Browser filesystem: Patch 9 (experimental)

The game-data importer writes immutable installation generations to the
origin-private file system at `openrealm/game-data/wc3/install-*`, with the
active manifest in `openrealm/game-data/wc3/installation.json`.

The launcher checks manifest versions 1, 2 and 3, file names/sizes, byte totals,
required `War3.mpq`, and the bounded MPQ header probe before native startup.
Manifest version 1 mounts `/opfs/openrealm/game-data/wc3`; versions 2 and 3 mount
`/opfs/openrealm/game-data/wc3/<generation>` via the engine's `-data` argument.
Version 3 permits strictly validated nested `Maps/...` paths (not arbitrary
relative paths); versions 1 and 2 continue to accept their existing flat
archive files. These checks are only format checks, not integrity hashes or
complete game-data validation. Importing a new generation does not alter already-open files.

The importer worker can read multiple ISO 9660/Joliet images and mix them with
direct MPQ/W3M/W3X uploads in one import. It selects recognized WC3 archives
from each disc and stores their source byte ranges in a version-3
generation. The combined selection must contain `War3.mpq`; duplicate archive
names are errors, not implicit overrides. Patch 5 additionally extracts a
known original RoC `Setup.mpq` map profile into nested `Maps/` directories.
`Setup.mpq` itself is not a playable archive and is never installed.
Both browser pages expose ISO selection from patch 4. See
[browser ISO reader](browser-iso9660.md).

Before staging, the worker asks `navigator.storage.estimate()` for remaining
quota when available. An explicit shortage prevents an import; absence or
failure of this optional estimate does not prevent trying to write. OPFS can
still fail during a write, in which case the uncommitted generation is removed
and the active manifest is retained. Importing again **replaces** the manifest
rather than merging with the current installation; select both RoC and TFT in
the same operation. Old generations are deliberately retained because active
engine readers may have them open. This affects the storage space needed for
future installs; persistent-storage permission is not requested by the worker.

The browser engine uses Emscripten WasmFS (`-sWASMFS=1`) and mounts the upstream
OPFS backend at `/opfs` before `Sys_ResolveShareDirectory`, `Com_Init`, or
archive discovery. The existing `common/mpq.c` uses normal seek/read operations;
archives are **not** copied into MEMFS. There is no 256 MiB per-file or 512 MiB
total staging limit. Browser storage quotas and address-space limits still
apply, and compatibility with large archives is not yet established.

The WasmFS OPFS backend must not initialize on the browser main thread.
The experimental engine build enables pthreads and `PROXY_TO_PTHREAD`. Hosting
must provide a secure context and COOP/COEP cross-origin isolation to allow
SharedArrayBuffer. Confirm worker startup before testing actual game assets.

**Save compatibility:** The legacy Patch 5 `IDBFS` directory is not deleted or
migrated by Patch 9. WasmFS does not automatically mount that legacy directory.
Existing saves remain in browser storage but may be inaccessible until a
future explicit migration. Do not delete site data or assume a completed save
migration. Browser user-directory defaults also require validation before
claiming save persistence is complete.

The native engine build remains experimental and has not been compiled or run
as part of this patch. A runtime probe must verify mounting and seekable
reads, including seek/EOF, >256 MiB archives, and multi-tab behavior. On OPFS
failure the browser build should fail startup rather than stage full archives.

To test locally, serve build output over HTTPS (or localhost) with headers:
`Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`. The requirements apply to the
worker-backed game, not just the launcher. See `docs/platform/browser.md`.

Patch 4 adds worker event-driven browser UI: phase updates and a validated
per-source archive list precede byte-copy progress, while a final `done`
confirms that the manifest has been committed. The foundation build now
copies the importer scripts next to the emitted HTML; the engine build already
did so. Browser UI behavior is exercised by
`node --test port/web/tests/installer-ui.test.cjs`; see
[browser ISO reader](browser-iso9660.md) for the message contract.
