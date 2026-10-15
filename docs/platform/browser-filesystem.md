# Browser filesystem: Patch 9 (experimental)

The game-data importer writes immutable installation generations to the
origin-private file system at `openrealm/game-data/wc3/install-*`, with the
active manifest in `openrealm/game-data/wc3/installation.json`.

The launcher checks manifest versions 1, 2 and 3, file names/sizes, byte totals,
required `War3.mpq`, and MPQ header/table extents before native startup.
Manifest version 1 mounts `/opfs/openrealm/game-data/wc3`; versions 2 and 3 mount
`/opfs/openrealm/game-data/wc3/<generation>` via the engine's `-data` argument.
Version 3 permits strictly validated nested `Maps/...` paths (not arbitrary
relative paths); versions 1 and 2 continue to accept their existing flat
archive files. These checks are only format checks, not integrity hashes or
complete game-data validation. Importing a new generation does not alter already-open files.
The MPQ structural check validates declared lengths, supported header versions,
block/hash table offsets and counts, but cannot prove the payloads are playable.

The importer worker can read multiple ISO 9660/Joliet images and mix them with
direct MPQ/W3M/W3X uploads in one import. It selects recognized WC3 archives
from each disc and stores their source byte ranges in a version-3
generation. The combined selection must contain `War3.mpq`; duplicate archive
names are errors, not implicit overrides. Patch 5 additionally extracts a
known original RoC `Setup.mpq` map profile into nested `Maps/` directories.
Patch 6 can recover directly named `War3x.mpq`/`War3xLocal.mpq` members from
a TFT `Setup.mpq`. The follow-on demo import patch accepts the documented
September 2002 demo disc separately and identifies it as demo in the manifest.
Patch 7 additionally
scans uncompressed anonymous installer blocks for nested MPQs, assigning game
archive filenames only where internal Warcraft III file-path hash evidence
identifies their roles. It also recognizes named base/local/patch members. It refuses to commit
a selected TFT installer that cannot supply `War3x.mpq`, rather than silently
claiming an expansion installation.
`Setup.mpq` itself is not a playable archive and is never installed.
Both browser pages expose ISO selection from patch 4. See
[browser ISO reader](browser-iso9660.md).

Before staging, the worker asks `navigator.storage.estimate()` for remaining
quota when available. An explicit shortage prevents an import; absence or
failure of this optional estimate does not prevent trying to write. OPFS can
still fail during a write, in which case the uncommitted generation is removed
and the active manifest is retained. Importing again **replaces** the manifest
rather than merging with the current installation; select both RoC and TFT in
the same operation. The importer and launcher coordinate through the origin-scoped Web Locks API:
- The importer holds `openrealm-wc3-installation` exclusively for imports or removals.
- The launcher holds the same lock in shared mode while resolving the manifest
  and acquiring a shared lease named `openrealm-wc3-generation:<generation>`
  (or `:legacy` for version-1 root installations).
- That lease lasts for the running page's lifetime; generation garbage collection
  obtains a non-blocking exclusive lease before deleting an old generation.
- After successful commit the current and immediately previous generation remain;
  earlier unleased generations are reclaimed. Stale generations are also
  pruned ahead of the quota estimate so they do not block future imports.
  Busy generations are skipped and can be collected after closing their game
  tabs and importing again. Removal
  fails without deleting data if any game generation is leased. This keeps an
  extra preceding generation for older tabs opened before Web Locks were added.

Web Locks are required for safe installation and engine startup; the UI reports
unsupported browsers instead of silently allowing unsafe concurrent commits.
Cross-tab serialization is origin-specific; a different browser profile/origin
has independent storage. Interrupted uncommitted generations are still cleaned
up by the worker; incomplete legacy leftovers can be reclaimed on a later
successful import. Browser storage estimates are advisory, and persistent-storage
permission is not requested by the worker.

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

## ISO importer hardening

`port/web/site/mpq-header.js` is shared between `install-worker.js` and
`filesystem-bridge.js`: it checks the MPQ signature within the first 1024
bytes, version 0/1, header size, declared archive bounds, sector shift,
positive power-of-two hash count, nonzero block count and both table extents.
Standalone `.mpq` inputs may have up to 4096 bytes of trailing
signature/padding data beyond the declared archive; map files may
have an application-specific prefix. It does **not** decrypt every file,
validate CRCs, or guarantee playable game data.

A complete base import requires `War3.mpq`. An import that includes
`War3x.mpq` but lacks `War3xLocal.mpq` is marked
`completeness: "partial-expansion"` and lists `missing: ["War3xLocal.mpq"]`
in the version-3 manifest. It is retained for users with incomplete direct
uploads but is visibly labelled partial, not full Frozen Throne support.
`War3xLocal.mpq` without `War3x.mpq` is rejected. A selected TFT installer
that yields no `War3x.mpq` still fails without replacing the prior manifest.
Neither field proves the game is complete; additional patch/archive versions,
localization and proprietary game-data coverage need real-disc tests.

The importer now recognizes the documented demo disc by the root files
`War3Demo.ico` and `SCDemo.exe`, independent of ISO filename. Unlike prior
rejection behavior, demo imports are allowed and recorded as
`edition: "demo"`, `completeness: "demo"` in the existing version-3 manifest.
`War3.mpq` still passes normal structure validation. Directly available
`RetailMaps/*.w3m` and `.w3x` files are copied to `Maps/<filename>` for the
normal map search path. Demo `Setup.mpq` is not interpreted with the retail
RoC installer-map profile. A demo disc cannot be mixed with retail ISOs or
standalone MPQs, though standalone map files may accompany it.

`installer.js` displays a persistent demo notice after importing and on
later reloads (`list`), separate from the transient progress message. The
engine launcher also identifies the demo on startup. Old manifests without
an `edition` field remain valid and are treated as retail/unclassified. The
launcher rejects unknown edition values or contradictory demo completeness
metadata, but edition is informational, not a verification of game playability.
Other demo CD layouts and loose demo MPQs are not automatically classified;
no retail ISO or working in-browser demo run was provided for verification.

The ISO fixture writer allocates enough 2048-byte sectors for each file so
multi-sector MPQs cannot overlap. Synthetic cross-tab Web Locks, rollback,
validation and pruning scenarios live in
`port/web/tests/iso-import.test.cjs`. Runtime behavior, including multi-tab
WasmFS reads and actual TFT installer fingerprints, remains unverified.

### Removing local data from a running browser game

The running engine holds a shared lease for its OPFS installation generation.
Consequently, the installer cannot remove that generation while the same page
is playing. After confirmation, **Remove local data** sets a session-scoped
one-shot removal marker and reloads the page. Navigation shuts down the old
Emscripten worker and releases its shared generation lease. On the new page,
`filesystem-bridge.js` skips engine startup and `installer.js` asks the
installation worker to remove the game data. Success or failure clears the
marker; failed removal leaves the archives intact and displays the Web Locks
error (other game tabs still need to be closed). Normal page loads have no
removal marker and continue starting the engine. This intentionally uses a
page reload instead of attempting unsafe teardown of a live WasmFS mount.

## Browser RoC/TFT selection

The engine page exposes a game-edition selector above the installer. After
validating the stored manifest, the launcher enables The Frozen Throne only if
`War3x.mpq` is present and the installation is not labelled as demo. The
selection is preserved in tab-scoped `sessionStorage` and defaults to Reign of
Chaos. Selecting a different edition reloads the document to release the
existing Web Locks generation lease and stop the current Emscripten engine.
The new engine receives `+set fs_expansion 0` (RoC) or `+set fs_expansion 1`
(TFT) as early-startup arguments. No archive re-import is needed.

A demo or base-only installation disables TFT and resets a stale expansion
preference to RoC. This only selects the existing engine's expansion mode; it
does not claim complete TFT assets or gameplay compatibility.

## Browser game control visibility

The browser Escape and fullscreen controls belong to `#game-container`, not
inside the canvas. The container creates a stacking context and keeps the
control bar above the canvas even if Emscripten adjusts canvas dimensions.
Fullscreen is requested on the container rather than the canvas, so the
controls remain visible in the browser fullscreen top layer. The page also
redirects direct `canvas.requestFullscreen()` calls to the container when
that method is writable. The Escape button continues to dispatch keyboard
events to the canvas and the fullscreen button can leave fullscreen mode.

## Browser gameplay keyboard ownership

`port/web/site/browser-keyboard.js` captures browser keyboard defaults only while
the Warcraft III canvas owns focus (or its game container is fullscreen). Actual
`keydown`/`keyup` events continue through SDL/Emscripten unchanged; the capture
listener never calls `stopPropagation()`. Ctrl/Shift, Escape and number-group
shortcuts are handled in gameplay focus; reserved browser/OS shortcuts are not
claimed. Installer inputs and buttons retain normal keyboard behavior.

When a window loses focus, the canvas blurs, or the document is hidden, the
browser adapter emits key-up events for held Ctrl/Shift keys to avoid stuck
modifiers. While the game container is fullscreen, supported browsers may
grant the optional Keyboard Lock API for Escape and modifier keys; unavailable
or denied locks fall back to ordinary keyboard events. Fullscreen exit unlocks
keyboard capture. Escape may still be intercepted by browser fullscreen controls.
Synthetic test coverage is in `port/web/tests/browser-keyboard.test.cjs`.
