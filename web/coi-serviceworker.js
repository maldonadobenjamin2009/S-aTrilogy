/* COOP/COEP bridge for static hosts such as GitHub Pages.
 * Adds the isolation headers required for WebAssembly pthreads/SharedArrayBuffer. */
self.addEventListener('install', function (event) {
  event.waitUntil(self.skipWaiting());
});

self.addEventListener('activate', function (event) {
  event.waitUntil(self.clients.claim());
});

self.addEventListener('fetch', function (event) {
  const request = event.request;
  if (request.cache === 'only-if-cached' && request.mode !== 'same-origin') return;

  event.respondWith(fetch(request).then(function (response) {
    if (response.type === 'opaque' || response.status === 0) return response;

    const headers = new Headers(response.headers);
    headers.set('Cross-Origin-Opener-Policy', 'same-origin');
    headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
    headers.set('Cross-Origin-Resource-Policy', 'same-origin');
    // Fetch gives JS a decoded body. Remove transport headers before rebuilding it.
    headers.delete('content-encoding');
    headers.delete('content-length');

    return new Response(response.body, {
      status: response.status,
      statusText: response.statusText,
      headers: headers
    });
  }));
});
