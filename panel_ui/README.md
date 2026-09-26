# Panel UI

Owns the neutral, unprivileged application shell. `panel_ui` is the only future
owner of LVGL lifetime, display rendering, application hit testing, surface
routing, modal precedence, and visible intent acknowledgement. It never owns
backlight or suspend policy, raw touch observation, touch provenance, study
state, microphone capture, or external submission.

## Public promises

- `PanelIntentRegistry` is the `@canonical panel_intent_registry`. Version 1 has
  one closed, namespaced semantic ID set and no caller-supplied domain payload.
- `ShellController` is the `@canonical panel_shell_controller`. It validates one
  envelope at the local ingress boundary, never queues input across lifecycle
  transitions, and returns an accepted route or a typed content-free rejection.
- `BoundedUnicodeTextPolicy` is the `@canonical panel_unicode_text_policy`.
  Renderers reject invalid or over-budget UTF-8 instead of substituting text.
- `HeadlessRenderer` is the host evidence renderer. It has no display backend,
  device path, calibration, color, or font fallback.
- Lifecycle types come from the canonical `shared/panel_mode_protocol` library;
  this domain does not define a second protocol enum or snapshot model.
- Normalized touch records come from the canonical
  `shared/touch_observation_protocol` library; this domain does not define a
  second provenance or raw-axis model.
- `UnixSocketLifecycleClient` is the
  `@canonical panel_mode_unix_socket_client`. It connects only to an explicit
  path, authenticates the exact configured supervisor UID from kernel peer
credentials, validates typed protocol-v2 snapshots and their canonical
  digest, and sends presentation acknowledgements only.
- `PanelUiService` is the `@canonical panel_ui_service`. It owns the neutral
  controller and headless renderer for the process lifetime, processes every
  ordered snapshot on a live authenticated connection, closes a failed
  connection, and reconnects only after the explicit bounded delay. An explicit
  consecutive-failure ceiling prevents an unbounded hot loop.
- `FailDarkLifecycleDisplayCommitBackend` is the current canonical negative
  presentation backend. It can return only `failed`; a future physical backend
  may return `committed` only after its evidence-backed commit primitive
  completes.
- Surface contracts are siblings beneath the shell. They declare routable
  intent IDs but do not move feature state transitions into this domain.

## Trust boundaries

Voice envelopes carry the complete catalog/active-set/backend/recognizer/policy
identity and a `spoken_form_id`. The controller accepts voice only when the
configured binding maps that form to the envelope's exact `intent_id`. Touch
and host fixtures use the same semantic namespace without voice evidence.
Touch-only intents reject voice and host-fixture sources.

The lifecycle and normalized-touch interfaces are adapter seams only. The
canonical lifecycle client opens the configured supervisor mode socket, but it
has no request, command, lease, or feature-message send API. It accepts the
built-in application release on every typed snapshot, requires one
session/challenge and strictly increasing event and render generations for the
connection, and closes on timeout or any transport, peer, framing, digest, or
binding failure.

The renderer supplies the generation and an explicit `committed` or `failed`
commit observation for the exact typed snapshot. The client never treats
successful rendering calls or socket writes as readiness: it sends `committed`
only for the callback's explicit successful commit and sends `failed` for an
explicit attempted-generation failure. `PanelUiService` additionally requires
the backend generation to equal the exact shell frame generation before either
result can cross the connection.

`bhp-panel-ui` is the sole installed neutral lifecycle entry point. Every
invocation requires all lifecycle, reconnect, controller, Unicode, and active
surface values; it has no production defaults:

```sh
bhp-panel-ui \
  --socket-path "$BHP_PANEL_UI_SOCKET_PATH" \
  --supervisor-uid "$BHP_PANEL_UI_SUPERVISOR_UID" \
  --receive-timeout-ms "$BHP_PANEL_UI_RECEIVE_TIMEOUT_MS" \
  --reconnect-delay-ms "$BHP_PANEL_UI_RECONNECT_DELAY_MS" \
  --maximum-consecutive-failures "$BHP_PANEL_UI_MAXIMUM_CONSECUTIVE_FAILURES" \
  --conflict-window-ms "$BHP_PANEL_UI_CONFLICT_WINDOW_MS" \
  --conflict-window-evidence-id "$BHP_PANEL_UI_CONFLICT_WINDOW_EVIDENCE_ID" \
  --maximum-source-instances "$BHP_PANEL_UI_MAXIMUM_SOURCE_INSTANCES" \
  --maximum-utf8-bytes "$BHP_PANEL_UI_MAXIMUM_UTF8_BYTES" \
  --maximum-code-points "$BHP_PANEL_UI_MAXIMUM_CODE_POINTS" \
  --allow-line-breaks "$BHP_PANEL_UI_ALLOW_LINE_BREAKS" \
  --active-surface-id "$BHP_PANEL_UI_ACTIVE_SURFACE_ID"
```

The shell variables above demonstrate explicit deployment injection; the
executable does not read them itself. No value shown is a deployment default or
hardware evidence. The process constructs the neutral controller, headless
renderer, lifecycle client factory, bounded reconnect loop, and stop control.
For every valid snapshot it builds the exact neutral frame, then the current
fail-dark backend returns an explicit `failed` presentation acknowledgement.
That negative acknowledgement is not readiness and can never permit nonzero
brightness. Only an evidence-backed DRM/fbdev backend may return `committed`.

Target service enablement remains blocked on that physical backend, so
`S75bhp-panel-ui` deliberately refuses `BHP_PANEL_UI_ENABLED=1`. The persistent
loop is host-verifiable production assembly, not a claim that a display backend
or target values exist. The host-test-only `panel_ui_core_smoke` exercises
neutral linkage and is not installed in the image.

This domain never opens a raw input device, creates provenance, or redeems an
observation. Its stream seam consumes the shared complete binding only; no
production normalized-touch transport adapter is enabled yet. The supervisor
remains the lifecycle/power authority and raw-touch observer.

## Host verification

```sh
cmake -S panel_ui -B .build/panel-ui -DPANEL_UI_BUILD_TESTS=ON
cmake --build .build/panel-ui
ctest --test-dir .build/panel-ui --output-on-failure
```

The conflict window, its evidence identifier, source-instance bound, and Unicode
budgets are required configuration. This domain provides no production values.

## Extension points

- `@extension-point panel_lifecycle_snapshot_stream`: consume the reviewed
  supervisor lifecycle protocol through the canonical Unix client without
  adding feature traffic.
- `@extension-point normalized_touch_observation_stream`: consume normalized
  supervisor observations without opening evdev or authoring provenance.
- `@extension-point panel_surface_contract`: add a sibling feature surface and
  route only its registered namespaced intents.

**Amended 2026-09-25:** `LvglPanelDisplay` now owns the LVGL lifetime, Unicode
validation, shared study/agenda fixture composition, hit geometry, feature-neutral
night color and black surface. `BHP_BUILD_PANEL_RENDERER` enables deterministic
PNG rendering; `BHP_BUILD_PANEL_SIMULATOR` adds the native SDL2 window. Both are
opt-in. The vocabulary renderer is subordinate and opens no display or raw input.
`BHP_BUILD_PANEL_DRM` adds an explicitly selected `--simulation-drm` guest branch
that uses the authenticated lifecycle client and supervisor touch stream.
`DrmPageFlipDisplay` permits a positive commit only after the kernel page-flip
completion event. The ordinary entry point retains its fail-dark backend and
production service enablement remains blocked. See the
[virtual panel contract](../simulator/README.md) for fixture scope and commands.

**Amended 2026-09-25 (approved UI review):** the shared renderer uses pinned
[project fonts](../shared/panel_fonts/README.md), a consistent layout grid,
bounded text fitting and product copy. The approved night surface is a dim red
time-only clock on black, hidden for untrusted time and without hit targets.
It remains a fixed-time simulation preview; AGENDA-011, household civil time
and physical calibration remain unresolved. All three display tiers share the
same font assets, geometry and content rules.
The [UI validation record](../docs/testing/pre-board-ui-validation.md) links
the reviewed screens and records the checks and remaining gates.
