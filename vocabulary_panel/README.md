# Vocabulary Panel

Owns the unprivileged Lexinet-backed study feature and its subordinate render
surface. ~~The planned neutral `panel_ui` remains the sole display/touch
owner.~~ The planned neutral `panel_ui` remains the sole display, application
hit-testing, and touch-routing owner. Under accepted
[ADR 0005](../docs/decisions/0005-validate-touch-provenance-at-capability-holders.md),
the supervisor becomes the sole raw touch-device observer and provenance issuer
at a later atomic packaging cutover; that cutover revokes the current
vocabulary/UI service's raw `bhp-touch` access. The checked-in scaffold has not
made that migration.

## Public promises

- `FlashCardSession` is the canonical deterministic interaction state machine.
- `LexinetClient` is the extension point for authoritative card/review storage.
- A renderer consumes `FlashCardSnapshot`; it does not own learning state or power policy.
- Loading, empty, offline/degraded, and error states are programmatically distinct.

The current terminal executable uses `FixtureLexinetClient` so the interaction
can be exercised without the network. `HttpLexinetClient` is a guarded,
optional adapter for the historical scaffold contract in
`docs/interfaces/lexinet-contract.md`. The Lexinet source audit confirmed that
no study API exists yet; ADR 0006 accepts the `study.v1` product semantics, but
the server-owned schema and implementation remain pending. The Buildroot
package compiles the adapter with certificate-verified TLS, but no target
service may send real requests until that server-owned contract and its
cross-repository tests replace the scaffold.

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

**Amended 2026-08-07:** separated proposed application touch routing from raw
touch observation/provenance under ADR 0005 without changing the current
service, package, permissions, or hardware claims.

**Amended 2026-08-16:** reconciled this domain boundary with ADR 0006 acceptance
of the Lexinet study semantics and neutral shared-capability ownership. The
`study.v1` API, resilient adapter, neutral-shell integration, and packaging
migration remain unimplemented.
