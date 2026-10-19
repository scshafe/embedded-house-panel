# Shared HTTP runtime

`bhp::http_runtime::HttpRuntime` is the canonical process-wide libcurl lifetime,
worker cancellation and common transport-budget owner. The composition root
creates it before clients; clients borrow it. `shutdown()` cancels and joins all
registered workers, refuses new handles, and only cleans up after every handle
has been destroyed. A live handle pins the global lifetime and returns the
explicit `HTTP_RUNTIME.HANDLES_LIVE` outcome. Repeated shutdown is an error.

`CancellationToken` and `TransportBudget` are shared primitives. Vocabulary owns
study contracts, credentials, retry classification and persistence. This domain
has no study semantics. It depends on libcurl and threads.

**Amended 2026-09-25:** added local lifetime, cancellation and shutdown-order tests.
No network, TLS deployment or hardware evidence is implied by these tests.

**Amended 2026-10-19 (issue #23):** `http_request.h` (`@canonical http_request`)
adds one request API on the runtime: `perform(runtime, HttpRequest, token)`.
- **Methods:** `GET`, `POST` and `PUT`. A `PUT` body comes from memory or from
  a byte range of an open file, which is how a take is sent in chunks with
  `Content-Range`.
- **Headers:** names come from a fixed set (`Accept`, `Authorization`,
  `Content-Range`, `Content-Type`, `If-None-Match`, `X-Request-ID`), and
  values may hold no control characters.
- **Budget:** each request carries its own, up to 60 s to connect and 600 s
  in all.
- **Responses:** the `ETag` comes back with the response, and any status
  (304, 4xx, 5xx) is a response, not an error.
- **Safety:** redirects are never followed and TLS verification stays on.
  The response ceiling and cancellation (mid-upload included) are enforced.
- **Retries:** one attempt per call. Retry policy stays with each domain;
  `HttpLexinetClient` keeps its three attempts on top of `perform()`.

Tests: `http_runtime_request_test` (loopback server).
