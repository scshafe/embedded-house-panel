# Vocabulary Panel

Owns the unprivileged Lexinet-backed flash-card experience and display rendering.

## Public promises

- `FlashCardSession` is the canonical deterministic interaction state machine.
- `LexinetClient` is the extension point for authoritative card/review storage.
- A renderer consumes `FlashCardSnapshot`; it does not own learning state or power policy.
- Loading, empty, offline/degraded, and error states are programmatically distinct.

The current terminal executable uses `FixtureLexinetClient` so the interaction can be exercised without the network. `HttpLexinetClient` is a guarded, optional adapter for the **provisional** contract in `docs/interfaces/lexinet-contract.md`; the Buildroot package compiles it with certificate-verified TLS, but no target service may send real requests until the Lexinet repository/API has been inspected.

All network operations are synchronous at the adapter boundary. A production GUI controller must call them on a cancellable worker and feed results back into `FlashCardSession`; render/event threads must never perform HTTP.

## Host verification

```sh
../scripts/verification/verify_host_cpp.sh vocabulary_panel
```

Or with CMake:

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug --output-on-failure
```

## Extension points

- `@extension-point lexinet_client`: real HTTP, fixture, or explicit bounded offline cache.
- `@extension-point flash_card_renderer`: the first concrete target implementation is the [LVGL 9.5 renderer](gui/lvgl/README.md), with explicit direct-DRM and fbdev transports; Qt remains a bounded fallback experiment.

See [flash-card UX](../docs/behavior/flash-card-ux.md) and [GUI research](../docs/research/gui-strategies.md).
