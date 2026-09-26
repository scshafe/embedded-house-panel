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
