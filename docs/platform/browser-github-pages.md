# Host the experimental Warcraft III browser build on GitHub Pages

**Status: deployed by the repository's GitHub Pages workflow.**
OpenRealm's `python3 port/web/serve.py` is a convenient local test server that
adds `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`. GitHub Pages is a static host and
does not offer repository-level control of those response headers. A scoped
Service Worker can add the headers to same-origin responses *after it controls
the page*. Initial navigation cannot rely on a not-yet-installed worker.

## Build and deployment outline

1. The GitHub Actions workflow prepares the embedded font, builds the
   **engine** (not the foundation smoke test) with Emscripten, and runs
   `mkdocs build`.
2. `port/web/publish-pages.py` copies allowed runtime assets to `site/play/`
   before uploading the Pages artifact. It excludes generated unity C and
   object files. Do not publish Blizzard MPQs or imported disc contents.
3. The game-scoped `coi-serviceworker.js` and startup gate in
   `pages-isolation.js` are shipped with the game. The worker scope is
   `/open-realm/play/`, matching the repository Pages URL.
4. Pushes to `main` that change documentation or browser-build inputs trigger
   the Pages deployment workflow. No game link is added to documentation
   navigation.
5. Visit `https://corepunch.github.io/open-realm/play/`. On the first visit,
   the page installs the worker and reloads once under its control. Verify
   `crossOriginIsolated === true` before the threaded engine starts. Ordinary
   `file://` URLs do not work.

A repository project site has a path prefix; avoid root-absolute URLs such
as `/index.wasm` or `/sw.js`. Relative URLs resolve within that project's
Pages origin and path. Emscripten-generated asset paths must follow the same
rule. The deploy workflow uses the game-scoped worker described below; the
sample worker remains a separate reference for a cache-enabled deployment.

## Diagnosing a failed docs build

The `Deploy Docs / build` job compiles the browser engine before running
MkDocs. A compiler or linker failure therefore skips both documentation
generation and the dependent deploy job. Inspect the first failed step with
`gh run view <run-id> --repo corepunch/open-realm --log-failed` before changing
Pages permissions or Markdown.

Run 39110161645 at commit `082f398303bd` failed at the Emscripten link:
`setting GROWABLE_ARRAYBUFFERS expects bool but got int`. Commit
`39d7d2d41` had added `-sGROWABLE_ARRAYBUFFERS=2`, but the pinned SDK 4.0.21
defines that setting as an experimental boolean (see its
[settings source](https://github.com/emscripten-core/emscripten/blob/4.0.21/src/settings.js)).
Remove the override and retain `-sALLOW_MEMORY_GROWTH=1`; changing it to `1`
would enable an experimental browser feature rather than restore the default.

After removing that flag, the same commit also failed with duplicate
`FDF_ParseLayerStyle` definitions in `game.o` and `menu.o`. The parser added
in `8d5a74dff` must follow the existing module namespaces in
`port/web/src/web_fdf_namespace.h`, just like `FDF_ParseControlStyle`.
Keep the game and menu FDF instances separate; sharing their registries or
weakening duplicate-symbol checks would break their native ownership model.

SDK 4.0.21 can emit `-Wpthreads-mem-growth` with this supported memory-growth
path. It warns about JavaScript performance, not a failed link; the engine
still needs pthreads and memory growth. Do not enable an experimental memory
feature merely to suppress that diagnostic.

To verify the full artifact locally with Emscripten 4.0.21 active:

```sh
python3 -m pip install -r requirements-docs.txt
make font
make web-warcraft3-engine
mkdocs build --strict
python3 port/web/publish-pages.py build/web/engine site/play
python3 -m unittest discover -s port/web/tests -p 'test_*.py'
```

MkDocs and its Material theme are pinned in `requirements-docs.txt`.
Strict builds reject missing navigation pages, document links and anchors. Source files and
guides outside `docs/` must use GitHub links, because MkDocs does not publish
them; paths to ignored local investigation artifacts should be plain text.

## Registration and startup gate (illustrative)

```html
<script>
async function enablePagesIsolation() {
  if (!('serviceWorker' in navigator)) {
    throw new Error('Service Workers are unavailable');
  }
  const scope = new URL('./', location.href);
  const worker = new URL('sw.js', scope);
  await navigator.serviceWorker.register(worker.href, {scope: scope.pathname});
  await navigator.serviceWorker.ready;
  if (!navigator.serviceWorker.controller) {
    // The first navigation is not isolated: perform one deliberate reload.
    // Fail visibly instead of looping if the worker still cannot control it.
    const reloadKey = 'openrealm-pages-worker-reload';
    if (sessionStorage.getItem(reloadKey))
      throw new Error('Service Worker did not take control after reload');
    sessionStorage.setItem(reloadKey, '1');
    location.reload();
    return false;
  }
  sessionStorage.removeItem('openrealm-pages-worker-reload');
  if (!crossOriginIsolated || typeof SharedArrayBuffer === 'undefined')
    throw new Error('Cross-origin isolation unavailable after worker control');
  return true;
}
</script>
```

**Important integration detail:** `port/web/site/filesystem-bridge.js` can
start the game immediately when it finds OPFS data. The deployed page therefore
loads `pages-isolation.js` before the filesystem bridge, and
`openrealmStartEngine` waits for its result before loading the Emscripten
engine. A failed isolation check reports an error and releases the archive
generation lease instead of leaving an unusable tab holding it.

The deployed bootstrap waits for `controllerchange`, reloads once, and reports
a visible failure if isolation is still unavailable after that reload. It does
not initialize pthreads, WasmFS or the game renderer before isolation succeeds.

## Worker and security rules

The example in `examples/github-pages-sw.js` handles only same-origin GET
requests inside its own registration scope. It injects COOP/COEP on HTML,
JavaScript, WASM and related same-origin responses, and uses Cache Storage for
the *web application files*. It **does not** cache ISO or MPQ user data:
Warcraft III archives remain in browser-local OPFS and are never uploaded.

Service Workers cannot bypass CORS. An opaque cross-origin response cannot be
made into a readable cross-origin-isolated resource by adding headers.
Host all required engine assets on the same origin or explicitly configure
third-party assets with compatible CORS/CORP policies. Cross-origin resources
needed by a COEP page may be blocked.

Cache Storage/OPFS are origin-scoped and may be evicted. This example uses
best-effort network-first caching and deliberately does **not** assert that
a first launch is fully offline capable. Full offline support needs a complete
asset inventory, cache-install validation, quota handling and real-browser
verification. The worker never caches POST requests or imports. Preserve the
normal browser's service-worker update behavior and use a versioned cache
name so changed engine assets cannot silently mix with earlier versions.

## Local validation without `serve.py`

For local development, start the header-enabled repository server from the
repository root:

```sh
python3 port/web/serve.py
```

Open `http://127.0.0.1:8000/`. The server provides isolation headers directly,
so the Pages Service Worker path is not involved. Check in developer tools:

```js
navigator.serviceWorker.controller !== null
crossOriginIsolated
typeof SharedArrayBuffer
```

The expected values are `true`, `true`, and `"function"`.
Also confirm a threaded engine launches and reads an OPFS installation.
Neither loading the page nor seeing a registered worker proves full game
compatibility. `serve.py` remains useful for native header-driven local
development and debugging without Service Workers.
