/* Game-scoped COOP/COEP shim for static GitHub Pages hosting.
 * Installed at /open-realm/play/coi-serviceworker.js with scope
 * /open-realm/play/. No caching, request rewriting or docs interception.
 * This is only a fallback where the host cannot send isolation headers.
 */
'use strict';

self.addEventListener('install', function (event) {
  event.waitUntil(self.skipWaiting());
});

self.addEventListener('activate', function (event) {
  event.waitUntil(self.clients.claim());
});

self.addEventListener('fetch', function (event) {
  const url = new URL(event.request.url);
  // Do not touch docs, other apps, other origins, or non-GET requests.
  if (event.request.method !== 'GET' || url.origin !== self.location.origin ||
      !url.pathname.startsWith('/open-realm/play/')) return;
  event.respondWith((async function () {
    const response = await fetch(event.request);
    // Opaque responses cannot be safely reconstructed. In-scope resources
    // must remain same-origin to satisfy COEP require-corp.
    if (response.type === 'opaque' || response.status === 0 || response.status === 304)
      return response;
    const headers = new Headers(response.headers);
    headers.set('Cross-Origin-Opener-Policy', 'same-origin');
    headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
    headers.set('Cross-Origin-Resource-Policy', 'same-origin');
    return new Response(response.body, {
      status: response.status,
      statusText: response.statusText,
      headers
    });
  })());
});
