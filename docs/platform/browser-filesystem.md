# Browser filesystem: Patch 9 (experimental)

The game-data importer writes immutable installation generations to the
origin-private file system at `openrealm/game-data/wc3/install-*`, with the
active manifest in `openrealm/game-data/wc3/installation.json`.

The launcher checks manifest versions 1 and 2, file names/sizes, byte totals,
required `War3.mpq`, and the bounded MPQ header probe before native startup.
Manifest version 1 mounts `/opfs/openrealm/game-data/wc3`; version 2 mounts
`/opfs/openrealm/game-data/wc3/<generation>` via the engine's `-data` argument.
These checks are only format checks, not integrity hashes or complete game-data
validation. Importing a new generation does not alter already-open files.

The importer worker can also read one ISO 9660/Joliet image, select the
recognized WC3 MPQs and store them in the same version-2 generation format.
`Setup.mpq` is deliberately not installed as a playable archive; its internal
content is not yet extracted. See [browser ISO reader](browser-iso9660.md).

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
