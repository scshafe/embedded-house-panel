# Virtual panel

`PanelSimulation` composes the real shell controller, canonical study reducer and
shared LVGL surfaces. `ScriptedPanelSupervisor` owns raw SDL pointer observation
and emits protocol-v2 lifecycle snapshots in the native simulator. It never
creates a production touch-provenance token. Every result is
`simulation_not_hardware_evidence`.

The memory renderer, native window and guest DRM mode use the same
`panel_ui::LvglPanelDisplay` and subordinate vocabulary widgets. The built-in
Montserrat fonts and fixed fixture clock keep screenshots deterministic; no
animation controls the captured state. Unsupported glyphs fail explicitly after
the shell's UTF-8 policy validates text.

## Commands

- `bhp-panel-render --scenario FILE --output PNG` renders one 1024x600 PNG.
- `bhp-panel-sim --scenario FILE [--screenshot PNG] [--exit-after-ms N]` opens
  LVGL's SDL2 window. `--state-directory DIR` selects the durable study store;
  otherwise the memory store is explicitly fixture-only.
- The guest `bhp-panel-ui --simulation-drm` branch additionally requires explicit
  lifecycle socket/UID, DRM card/connector, touch socket, touch axis bounds, a
  scenario and a fixture control socket. The ordinary executable remains
  fail-dark. No production service is enabled.

The closed scenario schema is [scenario.schema.json](scenario.schema.json).
Every scenario in `scenarios/` uses synthetic fixture data. Agenda strings are
fixture presentations, including recording and delivery outcomes: no microphone,
contacts, calendar, Messages, companion, draft submission or civil-time provider
exists in this surface. `AGENDA-011` remains unresolved. Night RGB `0x180000` is
an explicitly labelled simulation fixture, never a calibrated FRDM value.

## Input and evidence

Native keyboard: F or Space flips, N next, B back, 1-4 ratings, U touch-only Undo,
D day, G grace, R night, X blank, C clock untrusted, Escape quits. Keys representing
touch are simulation controls; no real touch provenance is claimed.

JSON lines on stdin use the same event route:

```json
{"type":"tap","x":500,"y":300}
{"type":"utterance","phrase":"Lexi flip"}
{"type":"lifecycle","state":"grace"}
{"type":"scenario","path":"simulator/scenarios/agenda-today.json"}
{"type":"screenshot","path":".build/screenshots/manual.png"}
{"type":"quit"}
```

Semantic intents pass through `ShellController` before the source-qualified study
reducer. Voice-only reveal is provisional; Undo remains touch-only. The fake
voice binding comes from the separate deterministic media-broker process; its
content-addressed active-set artifacts are revalidated by the shell. Direct
`source: voice` intent injection is rejected. The recognizer is acoustically
unvalidated and never a production recognizer.
Every render has a new shell generation. The guest rejects a normalized touch
bound to an older generation until the supervisor publishes and receives the
next exact lifecycle commitment; redraws cannot silently reuse generation IDs.

The guest's mode-0600 Unix datagram fixture-control socket permits only scenario
selection, screenshots and quit. It ignores a scenario's lifecycle field: real
supervisor publications retain exclusive lifecycle authority. Mouse/touch input
reaches the guest only through its supervisor evdev observer.
Scenario selection pauses HTTP and creates an isolated memory fixture session;
the durable live session remains intact. The clock-untrusted scenario uses an
explicitly labelled fixture diagnostic while retaining the real lifecycle.
Losing the authenticated lifecycle connection cancels dependent work and latches
the display black until a fresh authenticated snapshot arrives.

Memory/SDL results say `simulated_commit`. DRM reports `committed` only after
`drmModePageFlip`'s matching kernel completion event. A modeset call, widget
construction or flush invocation alone is insufficient; a 500ms completion
timeout returns failed and permanently faults that DRM instance. A real page
flip on virtio-gpu proves that software path only. It does not close any FRDM
gate or prove physical pixels/backlight, GT911 wiring, wake, power or calibration.

With `BHP_BUILD_HTTP_LEXINET_CLIENT`, `--lexinet-config FILE` plus
`--state-directory DIR` connects the async study worker. The ignored JSON config
contains `base_url`, `profile_ref`, `topic_refs`, optional `credential_file` and
`fixture_ca_file`. Guest fixture trust additionally requires explicit
`allow_qemu_host_test_ca: true` and the exact QEMU host address. Certificate and
hostname verification stay enabled. Credentials are read from the protected file,
never arguments or logs. Startup restores the durable cache before HTTP recovery.
An idle restored session sends no empty polling syncs. Pending review events,
page refill and successor creation use the canonical reducer's persisted request
identities. Terminal boundary errors wait for an explicit reload; retryable
failures retain the durable request for recovery.
