/* EXAMPLE ONLY: copy to the published site's sw.js; not built automatically.
 * Update VERSION for every release to keep engine files from different builds
 * out of the same cache. Scope is the registration directory (Pages project).
 * All fetched content must originate from the same site; this does not bypass
 * CORS and does not cache user-imported MPQ/ISO files in OPFS.
 */
const VERSION = 'openrealm-wc3-pages-example-v1';
const APP_CACHE = VERSION + '-app';
const OFFLINE_ENTRY = 'index.html';
const CORE = ['index.html', 'index.js', 'index.wasm'];
const scopeURL = new URL(self.registration.scope);

function inScope(url) {
  return url.origin === scopeURL.origin &&
    url.pathname.startsWith(scopeURL.pathname);
}

function isolated(response) {
  // Response reconstruction is only valid for readable, non-opaque responses.
  if (!response || response.type === 'opaque' || response.type === 'opaqueredirect')
    return response;
  const headers = new Headers(response.headers);
  headers.set('Cross-Origin-Opener-Policy', 'same-origin');
  headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
  return new Response(response.body, {
    status: response.status,
    statusText: response.statusText,
    headers
  });
}

self.addEventListener('install', event => {
  event.waitUntil((async () => {
    // Initial cache warming is best effort. A production offline build
    // should enumerate and validate *all* required generated assets.
    const cache = await caches.open(APP_CACHE);
    await Promise.all(CORE.map(async name => {
      try {
        const response = await fetch(new URL(name, scopeURL), {cache:'reload'});
        if (response.ok && response.type === 'basic')
          await cache.put(new URL(name, scopeURL), response);
      } catch (_) { /* First-run offline cache population is optional. */ }
    }));
    await self.skipWaiting();
  })());
});

self.addEventListener('activate', event => {
  event.waitUntil((async () => {
    const names = await caches.keys();
    await Promise.all(names.filter(name =>
      name.startsWith('openrealm-wc3-pages-example-') && name !== APP_CACHE
    ).map(name => caches.delete(name)));
    await self.clients.claim();
  })());
});

self.addEventListener('fetch', event => {
  const request = event.request;
  const url = new URL(request.url);
  if (request.method !== 'GET' || !inScope(url) || request.mode === 'no-cors')
    return;
  event.respondWith((async () => {
    const cache = await caches.open(APP_CACHE);
    try {
      const response = await fetch(request);
      if (!response.ok || response.type !== 'basic') return response;
      // Preserve unmodified payloads in the cache. Each served response gets
      // new COOP/COEP headers even when served from Cache Storage.
      await cache.put(request, response.clone());
      return isolated(response);
    } catch (error) {
      const cached = await cache.match(request, {ignoreSearch:true});
      if (cached) return isolated(cached);
      if (request.mode === 'navigate') {
        const page = await cache.match(new URL(OFFLINE_ENTRY, scopeURL));
        if (page) return isolated(page);
      }
      throw error;
    }
  })());
});
