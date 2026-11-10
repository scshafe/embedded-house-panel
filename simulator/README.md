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
animation frame; elapsed wall time never changes that capture. Live text the
fonts cannot draw, or that is too long for its box, is drawn marked (`?`, `…`)
and logged, never an exit (panel_ui README, "Live text never ends the UI").

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
authority. A malformed datagram, an unknown type or a request that fails is
logged as `control_rejected` with its code and the UI keeps running
(`@canonical simulation_fixture_control`, review Q7). A socket file left by a
killed UI is removed at start by `bhp::unix_socket_path::clear_stale_socket`
(only this user's socket, with nothing bound to it), so a UI restarted by
systemd starts; a live UI's socket, a link or any other file still refuses. Mouse/touch input
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

`--study-config ENV_FILE` is the production alternative to `--lexinet-config`
(the two are exclusive): the `BHP_VOCABULARY_PANEL_*` keys of
`config/vocabulary-panel.env.example`, read strictly by
`read_study_client_configuration`, with live data (`fixture_data` false). The
renderer alone also takes a test-only `--study-test-ca-file PEM` for the fixture
HTTPS server; `bhp-panel-ui` has no such option.
`panel_simulation_https_test.py` covers a create, a rating queued while the
server is down, and its single delivery after both processes restart.

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
Live text with missing glyphs shows `?` for each; text that exceeds its reserved
layout bounds is cut with a visible `…`. Nothing is shrunk; fixed shell text
that stops fitting shows in the pixel-exact goldens. `"fixture_card": "unfit"` seeds the
fixture session with such a card (`study-unfit-card`).

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

## Voice lab recorder

Added 2026-10-19 for issue #23. The `voice_lab` surface records takes through
the real media broker, in process, on the paced fake source
(`VoiceLabRuntime`, `@canonical simulator_voice_lab_runtime`).
- **Takes:** they go to a temporary store that is deleted when the
  simulation ends.
- **Window and touch:** a stand-in supervisor opens the broker's window while
  the scripted lifecycle is day-active with presence asserted, and grants each
  synthetic touch once.
- **Flow:**
  - Record arms and starts a lease, and live levels follow.
  - Stop commits the take for review.
  - Keep hands the take to the uploader's hand-off directory through the
    broker's `hand_off`, then moves on to the next prompt.
  - Reject and Retake delete the take through the broker's `discard`.
- **Leaving day-present** fails the take and leaves no files.

**With the voice-lab uploader attached** (`--voice-lab-uploader-socket` and
`--voice-lab-uploader-uid`, or `attach_voice_lab_uploader_peer` in tests), its
state drives the recorder, except during a take:
- **Signing in:** Sign in asks it for a device code, which is shown with the
  plain verification URI.
- **The PC:** the D-021 preview is shown with its digest, and Approve sends
  that digest.
- **Prompts:** "Continue Missing" walks its prompts, skipping room tone and
  procedures.
- **Keeping a take:** once the broker has handed the take off, the uploader
  is asked to keep it for the prompt, with when the prompt was shown.
- **The status line:** the queue count, and why uploads wait.

Without an uploader the prompts, the sign-in code and the queue count stay
fixtures. Scenario files set only the starting screen (`voice_lab_state`:
`signed_out`, `signing_in`, `approve`, `ready`, `recording`, `review`,
`complete`, `unavailable`).

## Music

Added 2026-11-07 (#69,
[ADR 0013](../docs/decisions/0013-keep-a-spotify-refresh-token-on-the-panel.md)).
The `music` surface is a remote for Spotify Connect on the owner's speaker.
The speaker plays the stream itself, so nothing plays on the panel, and the
UI holds no token. Its intents are touch only. They reach
`bhp-music-control` through the music-control protocol
(`music_control/music_protocol.h`):
- **Play here** (`music.use_speaker`) moves Spotify playback to the
  configured speaker.
- **Play, Pause and Skip** (`music.play`, `music.pause`, `music.next`).
- **Volume - and +** step by 10 (`music.volume_down`, `music.volume_up`),
  shown only when Spotify reports the speaker's volume.

**With the service attached** (`--music-control-socket` and
`--music-control-uid`, or `attach_music_control_peer` in tests), its state
drives the surface. The service being down is never fatal (review Q7): at
start or after a loss the surface shows unavailable, the UI logs
`music_control_lost`, and it connects again on a `ReconnectSchedule`
(`@canonical ui_service_reconnect`: 1 s, doubling to 60 s, reset once the
service answers), with no UI restart.
- **The screen:** signed out, checking, missing (asleep, off or not
  listed), restricted, ready, playing or paused.
- **Refusals:** shown in words.
- **Track text:** cut to 40 characters (artists to 60). Text with glyphs
  outside the panel's Latin fonts is not drawn ("Title not shown").
- **A lost service** shows "Music unavailable".

Without the service the track, the speaker's name and the volume stay
fixtures. Scenario files set only the starting screen (`music_state`:
`signed_out`, `unavailable`, `checking`, `missing`, `restricted`, `ready`,
`playing`, `paused`).

## Reports

Added 2026-11-08, step 2 of the accepted
[bug-report design](../docs/architecture/bug-reports-and-self-repair.md).

**The day bar** of every daytime surface has a **Report** button
(`shell.open_report`).
- **A tap snapshots the context.** The surface it was pressed on, that
  surface's state and the lifecycle tokens go to `bhp-reporter`, which copies
  the log window at that moment. Then the report screen opens.
- **The dot.** An amber dot on the button means a report's fix is ready to
  install.
- **On the report screen** the button is drawn, so the bar looks the same,
  but does nothing.

**The report screen:**
- **The form.** A lowercase keyboard with digits and `, . ? '`, one closed
  intent per key (`report.key.*`), because the shell routes intents rather
  than raw input. Plus Space, Delete, Cancel, Send and Reports.
- **Send** submits the text, 500 characters at most, with the snapshot. It
  waits if the snapshot has not come back yet.
- **Cancel and Done** return to the surface Report was tapped on.
- **The Reports list** shows the newest reports as kind, status and date:
  "Waiting to send", "Received", "Being looked at", "Fix in PR #n", "Fix
  merged", "Update ready", "Installed" or "Won't fix".
- **Refusals** (the rate limits, a full queue, an expired snapshot) are shown
  in words.
- **A spoken description (step 3).**
  - **Record** starts a `bug_report` lease through the same broker runtime
    as the voice-lab recorder: a touch, and only while the mic window is
    open. **Stop** ends it.
  - The take is handed to the reporter's directory, and Send names it.
  - If the window closes mid-recording, nothing is kept, and the report can
    still go in words.
- **Voice:** "Lexi report a bug" (`shell.open_report`, `reversible_local`) opens
  the report screen with a snapshot. It never records.

**With the reporter attached** (`--bug-reporter-socket` and
`--bug-reporter-uid`, or `attach_bug_reporter_peer` in tests), the list and the
dot are live, and report bodies never come back to the UI. Without it the
screen says reporting is unavailable and that nothing was saved. A reporter
that is down at start, or lost later, is reconnected on the same schedule as
music (`bug_reporter_lost`, then `bug_reporter_connected`). Scenario
files set only the starting screen (`report_state`: `compose`, `recording`,
`sending`, `sent`, `list`, `unavailable`).

## Weather

Added 2026-11-10, Step 5 of the weather plan (ADR 0015).

**The day bar** shows the forecast:
- the high and low in °F, °C or K, colored by the Celsius value in every
  unit;
- or the sunrise and sunset, `↑ 6:40` / `↓ 16:48` in the place's local time.
  The arrows are drawn as lines, because the font subset has none.

A tap on the forecast opens the `weather` surface (`shell.open_weather`) from
every day surface. On the weather surface itself, the forecast does nothing.

**The weather surface:**
- **The menu.**
  - Rows for Units, Show and Location (Automatic, or Choose a place…).
  - Done returns to the surface the tap came from.
  - The attribution: "Weather data by Open-Meteo.com".
- **The search.**
  - A drawn QWERTY keyboard with 84 × 64 px keys, with hyphen, apostrophe,
    Space and Delete (`weather.key.*`).
  - Search, then up to five places, one tap each (`weather.result.1`–`5`).
- **Fixtures.** Without a weather worker the menu changes only the scene, and
  the search answers as `FixtureWeatherClient` does: names starting "boul"
  find the recorded five places; anything else finds none.
- **Live.** `attach_weather()` makes the bar and menu follow a
  `weather_panel::WeatherWorker`. A forecast shows only if it is fresh for
  the place's own date; "Weather stale" appears after 3 h.
- **Scenario states** (`weather_state`): `menu`, `automatic_unavailable`,
  `search`, `searching`, `results`, `no_results`, `offline`.
- **Scenario weather fields:**
  - `unit`: `F`, `C` or `K`;
  - `bar`: `temperatures` or `sun_times`;
  - `sunrise` and `sunset`: `HH:MM`.

