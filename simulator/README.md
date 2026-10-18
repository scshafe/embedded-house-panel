# Virtual panel

`PanelSimulation` composes the real shell controller, canonical study reducer and
shared LVGL surfaces. `ScriptedPanelSupervisor` owns raw SDL pointer observation
and emits protocol-v2 lifecycle snapshots in the native simulator. It never
creates a production touch-provenance token. Every result is
`simulation_not_hardware_evidence`.

The memory renderer, native window and guest DRM mode use the same
`panel_ui::LvglPanelDisplay` and subordinate vocabulary widgets. The pinned
[project Montserrat fonts](../shared/panel_fonts/README.md) and explicit fixture
clock state keep screenshots deterministic. A scenario can select a frozen
animation frame; elapsed wall time never changes that capture. Unsupported
glyphs fail explicitly after the shell's UTF-8 policy validates text.

## Commands

- `bhp-panel-render --scenario FILE --output PNG` renders one 1024x600 PNG.
- `bhp-panel-sim --scenario FILE [--clock-preview on|off] [--screenshot PNG] [--exit-after-ms N]` opens
  LVGL's SDL2 window. `--state-directory DIR` selects the durable study store;
  otherwise the memory store is explicitly fixture-only.
- The guest `bhp-panel-ui --simulation-drm` branch additionally requires explicit
  lifecycle socket/UID, DRM card/connector, touch socket, touch axis bounds, a
  scenario, a fixture control socket and `--display-rotation 0|180`. Rotation
  turns only the raster sent to the panel and the touch points read from it
  (`panel_ui/display_rotation.hpp`); rendering, hit testing and screenshots
  stay in logical coordinates. The ordinary executable remains
  fail-dark. No production service is enabled.

The native launcher (`python3 scripts/sim/run_native_panel.py --execute`) and
provisioned guest boot select Home with `--clock-preview on`. Direct executable
invocations default this option to `off`. Loading a scenario also disables
advancement, preserving deterministic captures. The preview clock advances
from the scenario's `fixture_clock` once per elapsed monotonic minute; it does
not read household civil time or select a timezone.

The closed scenario schema is [scenario.schema.json](scenario.schema.json).
Every scenario in `scenarios/` uses synthetic fixture data. Agenda strings are
fixture presentations, including recording and delivery outcomes: no microphone,
contacts, calendar, Messages, companion, draft submission or civil-time provider
exists in this surface. `AGENDA-011` remains unresolved. Night RGB `0x180000` is
an explicitly labelled simulation fixture, never a calibrated FRDM value.

Home offers Lexinet vocabulary and the Agenda preview as large app icons. Every
normal daytime surface shares the Home button, centered time and color-coded
daily High/Low values. Navigation preserves the canonical study session, history
and pending reviews; leaving study ends provisional voice presentation and Undo.
Navigation is touch/host-fixture only, never a spoken command. The night surface
has no navigation bar or hit targets.

An optional `weather` object declares `state` (`available`, `unavailable` or
`stale`), `unit` (`F` or `C`, default `F`) and integer `high_celsius`/
`low_celsius` values. Available and stale forecasts require both values, between
-80 and 60, with high greater than or equal to low. The display converts to the
chosen unit and colors each number by its temperature: blue at or below 0°C,
cyan through 10°C, green through 20°C, amber through 30°C and coral above 30°C.
Missing values display an em dash; stale values carry a visible stale label.
All scenario weather is explicitly preview data. No live weather provider or
household location is configured, and study data provenance cannot promote
fixture clock or weather values to real observations.

## Input and evidence

Native keyboard: H Home, V vocabulary, A Agenda preview, T advances the preview
clock by one minute; F or Space flips, N next, B back, 1-4 ratings, U touch-only Undo,
D day, G grace, R night, X blank, C clock untrusted, Escape quits. Keys representing
touch are simulation controls; no real touch provenance is claimed.

JSON lines on stdin use the same event route:

```json
{"type":"tap","x":500,"y":300}
{"type":"utterance","phrase":"Lexi flip"}
{"type":"lifecycle","state":"grace"}
{"type":"scenario","path":"simulator/scenarios/agenda-today.json"}
{"type":"clock_preview","enabled":true}
{"type":"clock_step"}
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
selection, preview-clock controls, screenshots and quit. It ignores a scenario's
lifecycle field: real supervisor publications retain exclusive lifecycle
authority. Mouse/touch input
reaches the guest only through its supervisor evdev observer.
Scenario selection pauses HTTP and creates an isolated memory fixture session;
the durable live session remains intact. The clock-untrusted scenario uses an
explicitly labelled fixture diagnostic while retaining the real lifecycle.
Losing the authenticated lifecycle connection cancels dependent work and latches
the display black until a fresh authenticated snapshot arrives.

The root-only guest fixture adapter exposes the clock controls through:

```sh
python3 scripts/sim/panel_ctl.py --execute clock-preview on
python3 scripts/sim/panel_ctl.py --execute clock-preview off
python3 scripts/sim/panel_ctl.py --execute clock-step
```

These commands change explicit preview state, never raw touch provenance or
supervisor lifecycle authority. Each clock frame is an ordinary renderer
generation. The guest serializes presentation and acknowledgement so a local
animation frame cannot replace the raster awaiting its lifecycle acknowledgement.

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

**Amended 2026-09-25 (approved UI review):** shared screens use larger type,
aligned controls and agenda columns, and product-facing state copy. Routine
presentation/reveal bookkeeping does not produce a sync badge. Pending ratings
and retractions remain visibly waiting until acknowledged; an active transfer
and a rejected review have distinct messages. Provisional voice actions remain
explicit previews. No message implies that an unacknowledged review is saved.

Network configuration accepts `fixture_data` (boolean, default `true`). Set it
explicitly to `false` for real study content; legacy simulator configurations
remain conservatively labelled as previews. Scenario selection always restores
the fixture label. This marker is independent of transport authentication and
does not create a clock, calendar or personal-data provider.

The night presentation fills the screen with dim red and centers a black
`HH:MM` clock using the 192px clock-only font. Both daytime and night clocks
slide each changed digit upward while its replacement enters from below in a
400ms transition; unchanged digits and the colon stay still. Hidden or
invalidated presentations cancel motion instead of replaying missed animations
on return. Unknown time produces a black night surface; blanking and display
failure remain black. This is an explicit fixture clock, and its red value
remains uncalibrated. The supervisor alone sets brightness.

For deterministic reel captures, a scenario supplies `fixture_clock` as the
destination and `clock_transition` as `{ "previous": "07:49", "elapsed_ms": 200 }`.
Both times must be valid `HH:MM`; elapsed time must be an integer from 0 to 400.
The captured frame remains frozen until an explicit preview control enables
motion. Runtime advancement and `clock-step` use the same renderer state.

The generated fonts cover a declared Latin/punctuation subset and a separate
digits/colon clock subset, not all Unicode.
Missing glyphs and text that exceeds its reserved layout bounds fail explicitly
instead of clipping, shrinking or substituting plausible content.

## Home catalog configuration

All display tiers load `panel_ui/config/applications.json` at startup. Direct
renderer, native simulator and guest DRM invocations accept `--launcher-config FILE`
for an explicit alternate catalog. The native launch script accepts the same
option. The guest launcher passes the repository catalog explicitly; editing its
source copy requires restarting the guest UI or reprovisioning.

See [Home configuration and extension](../panel_ui/README.md#configuring-and-extending-home)
for fields and new-app registration. Catalog order determines grid order; hidden
apps have neither tiles nor Home launch routes. Pages use registered
`shell.apps_previous` / `shell.apps_next` intents and preserve lifecycle gating.
Scenario loads reset the page but retain the configured catalog. Ordinary Home
navigation retains its page and canonical study session.

`simulator/tests/fixtures/application_grid/seven_apps.json` exercises a populated
3×2 grid and overflow page. Its numbered entries are explicitly layout fixtures
that route to the existing study/Agenda previews, not seven implemented products.
For a local layout inspection:

```sh
.build/panel-headless/simulator/bhp-panel-render \
  --scenario simulator/scenarios/home.json \
  --launcher-config simulator/tests/fixtures/application_grid/seven_apps.json \
  --output .build/application-grid.png
```

The default catalog exposes only the two implemented surfaces. No future-app
placeholders, extra permissions or production providers are enabled.
