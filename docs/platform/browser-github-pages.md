# Host the experimental Warcraft III browser build on GitHub Pages

**Status: deployment design and example, not an enabled production feature.**
OpenRealm's `python3 port/web/serve.py` is a convenient local test server that
adds `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`. GitHub Pages is a static host and
does not offer repository-level control of those response headers. A scoped
Service Worker can add the headers to same-origin responses *after it controls
the page*. Initial navigation cannot rely on a not-yet-installed worker.

## Build and deployment outline

1. Build the **engine**, not the foundation smoke test, on Linux with Emscripten
   activated: `make web-warcraft3-engine`. Output: `build/web/engine/`.
2. Publish **the entire** `build/web/engine/` directory as the Pages artifact
   (or a dedicated deployment branch). Keep `index.html`, `index.js`,
   `index.wasm`, `install-worker.js`, `filesystem-bridge.js`, installer
   support scripts, `audio-worklet.js`, and any `.data`/other emitted files
   together. Do not publish Blizzard MPQs or imported disc contents.
3. Copy `docs/platform/examples/github-pages-sw.js` to
   `build/web/engine/sw.js`. Customize the service worker cache version when
   shipping a changed engine. See the source comments for security constraints.
4. Add the *registration gate* below **before** `filesystem-bridge.js` in the
   generated page, preferably by maintaining a deployment-specific
   `port/web/site/engine-shell.html` variant and rebuilding rather than
   hand-editing emitted HTML after each build. This example is intentionally
   **not installed** by the repository build.
5. Set GitHub repository **Settings → Pages → Build and deployment** to use
   your chosen GitHub Actions Pages deployment workflow. Deploy the output
   folder as the Pages artifact. The exact workflow depends on your CI setup.
6. Visit `https://<account>.github.io/<repository>/` (or your configured
   custom HTTPS domain). On the first visit, install the worker, then reload
   under worker control. Verify `crossOriginIsolated === true` before
   starting the threaded engine. Ordinary `file://` URLs do not work.

A repository project site has a path prefix; avoid root-absolute URLs such
as `/index.wasm` or `/sw.js`. Relative URLs resolve within that project's
Pages origin and path. Emscripten-generated asset paths must follow the same
rule. This example does not provide an automatic GitHub Pages workflow.

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

**Important integration detail:** The existing
`port/web/site/filesystem-bridge.js` can start the game immediately when it
finds OPFS data. Registering the worker in the page without *gating that
startup* is insufficient. A production implementation must defer loading
`filesystem-bridge.js` and the Emscripten engine until the registration
function reports `true`, and display a reload/error status otherwise.
This document does not claim the snippet alone completes that integration.

For a first-visit page that runs non-isolated, do not initialize any
pthreads/WasmFS/OffscreenCanvas engine components before the reload. A
`controllerchange` listener plus a single reload is a useful production
refinement. If isolation remains false after control, show a failure rather
than loop forever.

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

Run a normal static HTTP server from the emitted folder:

```sh
cd build/web/engine
python3 -m http.server 8000
```

Open `http://localhost:8000/`. Service Workers are supported on localhost
as a secure-context exception. On the first visit reload after the worker
takes control. Check in developer tools:

```js
navigator.serviceWorker.controller !== null
crossOriginIsolated
typeof SharedArrayBuffer
```

The expected values *after control* are `true`, `true`, and `"function"`.
Also confirm a threaded engine launches and reads an OPFS installation.
Neither loading the page nor seeing a registered worker proves full game
compatibility. `serve.py` remains useful for native header-driven local
development and debugging without Service Workers.
