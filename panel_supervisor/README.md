# Panel Supervisor

The panel supervisor is the sole owner of presence interpretation, offline solar
state, backlight policy, UI mode publication, and suspend/resume orchestration.
`PanelPolicy` remains a pure C++17 state machine. `PanelSupervisor` applies its
decisions through explicit ports, while `panel_supervisord` wires those ports to
Linux facilities and runs the foreground polling loop.

No adapter invents a path, coordinate, brightness, delay, or suspend mode. The
daemon validates the complete configuration before opening a device, binding a
socket, writing a backlight node, or requesting suspend. Any missing or invalid
field is a typed startup error and the daemon exits without hardware action.

After validation, the daemon constructs the exact configured backlight adapter
and writes logical brightness zero before it creates the presence or solar
adapters or starts policy acquisition. A failed blank is logged and terminates
startup. This fail-closed write also keeps the screen dark while the Linux clock
is still synchronizing.

## Policy and lifecycle

`PanelSupervisor` exposes four operations:

- `start(trace_id)` acquires all inputs and applies initial state.
- `poll(trace_id)` acquires all inputs and reconciles a normal cycle.
- `resume(trace_id)` first records `recomputing_after_resume`, then reacquires
  clock, presence, and solar state before applying any output.
- `state()` returns the latest observations and successfully applied outputs.

Presence states are `unknown`, `present`, `grace_period`, and `absent`. Solar
states are `unknown`, `day`, and `night`. System states are `starting`,
`active`, `suspended`, `recomputing_after_resume`, and `faulted`.

| Raw presence and elapsed time | Presence state | System state | Requested brightness | UI mode |
| --- | --- | --- | --- | --- |
| detected | `present` | `active` | configured present day/night | day/night |
| not detected, elapsed `< grace_period` | `grace_period` | `active` | configured grace day/night | day/night |
| not detected, elapsed `>= grace_period` | `absent` | `suspended` | configured off output | suspended |
| not detected, no prior detection | `absent` | `suspended` | configured off output | suspended |

The exact grace deadline is absent. A wall clock moving backward relative to a
remembered presence instant is an explicit error. An error from the presence
device is never interpreted as absence.

The production supervisor also owns PIR debounce and wake-rate policy. It will
apply an explicitly configured monotonic debounce rule to raw ordered presence
evidence and an explicitly configured attributed-wake ceiling/backoff before
allowing repeated product actions or suspend requests. The exact intervals,
window, ceiling, and degraded response have no production defaults; on-target
evidence must select them. `PresenceSource` continues to report state/events and
wake capability only. It does not select or apply product-policy parameters.

These controls are not implemented by the current host scaffold. The custom
Black experiment calls `pm_wakeup_event` on every GPIO edge. A supervisor rule
can reject chatter and rate-limit the next action after Linux resumes, but
userspace cannot prevent or recover the energy cost of the kernel edge that
already woke Linux. Any pre-wake debounce or gate requires a separately reviewed
kernel/hardware mechanism and target evidence; no experimental overlay value is
a production default.

Output order is direction-safe. Every cycle writes exact logical brightness
zero, publishes the complete UI mode snapshot, and awaits an explicit
presentation outcome. Only `committed` permits the requested nonzero brightness
write. `not_committed` and `failed` outcomes keep applied brightness at zero,
emit a structured `presentation_degraded` warning, complete the cycle, and do
not skip a requested suspend. Timeout, disconnect, stale, duplicate, mismatch,
and explicit negative acknowledgement are degraded presentation outcomes rather
than cycle faults.

The host implementation now speaks only protocol v2. The Unix-socket adapter
validates the full acknowledgement tuple, and `PanelSupervisor` independently
rejects a committed variant unless its release, pending publication sequence,
nonzero render generation, session, challenge, snapshot digest, and display mode
are structurally valid and exactly bound where the power owner has the expected
value. A successful write or connected client never grants readiness. Target
services remain disabled because `panel_ui` has no display-commit backend; this
host evidence makes no brightness or physical-frame claim.

`UiModeNotifier` is a privileged power-path TCB port, not a plugin boundary for
untrusted code. The production build wires only `UnixSocketUiModeNotifier`;
host fakes exist for deterministic policy tests. Its publish result freezes the
exact commitment expectation before `await_presentation` begins. A UI that
connects later is retained for the next publication and cannot amend or satisfy
the current one.

## Linux runtime adapters

The production slice includes:

- `EvdevPresenceSource`, which either opens an explicitly configured node or
  rescans `/dev/input/event*` for exactly one verified `EVIOCGNAME`. Both modes
  require `EV_SW`/`SW_FRONT_PROXIMITY` and query current state with `EVIOCGSW`;
  name discovery never chooses the first or an ambiguous match.
- `SystemClock`, which uses Linux `adjtimex` readiness evidence and refuses
  `TIME_ERROR`, `STA_UNSYNC`, or `STA_CLOCKERR` before returning the current
  time. Non-Linux production-clock use is an explicit unsupported-platform
  error.
- `OfflineSolarCalculator`, which uses UTC, explicit latitude/longitude, and
  NOAA's fractional-year solar equations. Day is solar zenith at or below the
  standard sunrise/sunset threshold of 90.833 degrees; otherwise it is night.
  The documented calculation range is 1800 through 2100. See the
  [NOAA solar equations](https://gml.noaa.gov/grad/solcalc/solareqns.PDF).
- `SysfsBacklight`, which rereads the configured `max_brightness`, maps a logical
  0..100 percentage using overflow-safe nearest-integer rounding, and writes the
  mapped integer to the configured `brightness` node. It accepts only the same
  single-component `/sys/class/backlight/<device>/{brightness,max_brightness}`
  pair. Values are not clamped.
- `UnixSocketUiModeNotifier`, the supervisor-owned local Unix stream server for
  strict protocol-v2 JSON Lines snapshots and acknowledgements. Publication
  returns a pending sequence; only the exact latest authenticated peer tuple can
  return committed, and the final supervisor boundary validates it again.
- `SysfsSuspend`, which writes only the explicitly selected `freeze` or `mem`
  token to exactly `/sys/power/state`. For `mem` it first reads exactly
  `/sys/power/mem_sleep`, selects the configured `s2idle`, `shallow`, or `deep`
  variant if another one is bracketed, and rereads to confirm the kernel took
  it. An unlisted variant (`PANEL_SUPERVISOR.MEM_SLEEP_STATE_UNAVAILABLE`) or an
  unconfirmed selection (`PANEL_SUPERVISOR.MEM_SLEEP_SELECTION_FAILED`) stops
  before `/sys/power/state` is touched; the kernel's default variant is never
  inherited silently.
- `CaptureClosureGatedSuspend`, which wraps `SysfsSuspend` whenever suspend is
  enabled. After the display is blank and immediately before the suspend-state
  write, `ProcfsCaptureClosureProbe` reads two kernel-visible signals: every
  `/proc/asound/card*/pcm*c/sub*/status` (anything but exactly `closed` is
  open) and every process descriptor linked to `/dev/snd/pcmC*D*c`. Broker
  telemetry is never consulted. Open capture takes the bounded termination
  path: SIGTERM, a 500 ms grace, SIGKILL, and a 2,000 ms deadline, polling every
  50 ms. `UidScopedMediaTerminator` signals only processes whose real and
  effective UIDs both equal `BHP_PANEL_SUPERVISOR_MEDIA_BROKER_UID`, pinning each
  with a pidfd so a reused PID is never signalled; it never targets root.
  Without that UID, open capture refuses the suspend
  (`PANEL_SUPERVISOR.CAPTURE_TERMINATION_UNAVAILABLE`). Capture still open at
  the deadline refuses it (`PANEL_SUPERVISOR.CAPTURE_CLOSURE_UNVERIFIED`), and
  unreadable evidence refuses it (`PANEL_SUPERVISOR.CAPTURE_CLOSURE_PROBE_FAILED`).
  The gate never guesses closure and never waits past the deadline. Microphone
  rail-off evidence remains an FRDM gate.

The clock adapter proves that Linux currently reports the clock synchronized;
it cannot prove that an upstream server or saved clock was honest. The target
service is still ordered after Chrony, and target evidence must exercise cold
boot, network loss, and resume. The solar calculator rejects an unrepresentable
UTC time or a year outside its documented range; a plausible but externally
wrong clock remains a deployment evidence failure, never a reason to add a
guessed timezone offset.

Privileged writable-file opens use close-on-exec and, where the platform
provides it, `O_NOFOLLOW`. The daemon writes exact values and returns typed
open, read, write, ioctl, close, bind, permission, accept, send, and calculation
errors.
It does not retry a different device, brightness maximum, or suspend mode.

## Complete daemon configuration

`panel_supervisord` accepts no positional arguments and has no runtime defaults.
Set every required variable:

| Variable | Contract |
| --- | --- |
| `BHP_PANEL_SUPERVISOR_LATITUDE` | Decimal degrees, -90 through 90, positive north |
| `BHP_PANEL_SUPERVISOR_LONGITUDE` | Decimal degrees, -180 through 180, positive east |
| `BHP_PANEL_SUPERVISOR_PRESENCE_GRACE_SECONDS` | Non-negative decimal seconds |
| `BHP_PANEL_SUPERVISOR_PRESENCE_INPUT_PATH` | One option: explicit absolute evdev node |
| `BHP_PANEL_SUPERVISOR_PRESENCE_INPUT_NAME` | Preferred alternative: exact verified `EVIOCGNAME`; set exactly one selector |
| `BHP_PANEL_SUPERVISOR_BACKLIGHT_PATH` | Exact `/sys/class/backlight/<device>/brightness` shape |
| `BHP_PANEL_SUPERVISOR_BACKLIGHT_MAX_PATH` | Matching exact `<device>/max_brightness` path |
| `BHP_PANEL_SUPERVISOR_MODE_SOCKET` | Absolute Unix socket path |
| `BHP_PANEL_SUPERVISOR_PANEL_UI_UID` | Exact nonzero numeric UID of the dedicated `panel_ui` service; group membership is insufficient |
| `BHP_PANEL_SUPERVISOR_PRESENTATION_TIMEOUT_MILLISECONDS` | Explicit acknowledgement deadline, 1 through 60,000 milliseconds |
| `BHP_PANEL_SUPERVISOR_DAY_BRIGHTNESS` | Logical percentage, 0 through 100 |
| `BHP_PANEL_SUPERVISOR_NIGHT_BRIGHTNESS` | Logical percentage, 0 through 100 |
| `BHP_PANEL_SUPERVISOR_GRACE_DAY_BRIGHTNESS` | Logical percentage, 0 through 100 |
| `BHP_PANEL_SUPERVISOR_GRACE_NIGHT_BRIGHTNESS` | Logical percentage, 0 through 100 |
| `BHP_PANEL_SUPERVISOR_OFF_BRIGHTNESS` | Required to be exactly `0`; nonzero off/suspend output is rejected |
| `BHP_PANEL_SUPERVISOR_SUSPEND_MODE` | Exactly `disabled`, `freeze`, or `mem` |
| `BHP_PANEL_SUPERVISOR_POWER_STATE_PATH` | Required for `freeze` or `mem`; exactly `/sys/power/state` |
| `BHP_PANEL_SUPERVISOR_MEM_SLEEP_PATH` | Required for `mem`, rejected otherwise; exactly `/sys/power/mem_sleep` |
| `BHP_PANEL_SUPERVISOR_MEM_SLEEP_STATE` | Required for `mem`, rejected otherwise; exactly `s2idle`, `shallow`, or `deep`, from the board profile's `BHP_BOARD_MEM_SLEEP_STATE` once `FRDM-GATE-SUSPEND` evidence exists |
| `BHP_PANEL_SUPERVISOR_POLL_INTERVAL_MILLISECONDS` | 50 through 5,000 milliseconds |
| `BHP_PANEL_SUPERVISOR_SUSPEND_RETRY_BACKOFF_MILLISECONDS` | Required for enabled suspend; 1,000 through 300,000 milliseconds |
| `BHP_PANEL_SUPERVISOR_MEDIA_BROKER_UID` | Optional; the media broker's exact nonzero service UID, distinct from the UI UID. Enables the capture-closure termination path; unset, open capture refuses every suspend |

`SUSPEND_MODE=disabled` is the explicit non-suspending configuration and does
not require `POWER_STATE_PATH`. `scripts/boards/render_service_environment.sh`
renders the board-derived keys (backlight node, PIR input name, mem_sleep
variant) from `config/boards/<board>.env`; an unmeasured value becomes a
`REPLACE_ME_UNMEASURED_<gate>` token that this parser rejects. Latitude and longitude are treated as sensitive
deployment data and are never included in structured daemon logs.

At initial startup only, `SystemClockUntrusted` and transient Linux clock
synchronization-query failures are explicit retryable boundary conditions. The
daemon leaves the already-blanked display off and retries `start()` at the
configured, bounded poll interval (50 through 5,000 milliseconds). The wait is
signal-interruptible. A permanent clock-query failure, any non-clock failure,
and any failure after the initial cycle remain fatal; no guessed time reaches
solar policy. Runtime and post-resume clock-readiness failures deliberately do
not enter the startup retry loop: preserving the exact `poll` versus `resume`
event is required for correct resuspend semantics. Until that stateful retry is
implemented and hardware-tested, these failures take the fatal blank path.

## UI socket contract and trust boundary

The supervisor creates and listens on the configured Unix stream path, applies
mode `0660`, and owns its lifecycle. It recovers only a same-effective-owner
socket whose connection probe proves it stale and whose type/device/inode/owner
still match immediately before removal. Active, foreign-owner, and non-socket
paths are preserved. Shutdown likewise removes only the socket identity this
process created. The deployment pre-creates the parent directory with the UI
group and setgid mode so `0660` grants only the intended access.

Every publication is one complete protocol-v2 lifecycle snapshot encoded by the
shared canonical codec. The server accepts only an exact-UID peer, creates a
fresh 128-bit challenge per connection, and requires the one exact bounded
presentation acknowledgement for its latest snapshot. The first exchange also
binds the common build-stamped appliance release identity. Timeout, disconnect,
stale, duplicate, mismatch, malformed input, and explicit presentation failure
leave brightness at zero and complete as visible degraded outcomes. Only a
newer exact `committed` render generation permits nonzero brightness. At most
one UI client is retained; extra, slow, or unauthorized clients are closed
without granting readiness or changing panel policy. Each accept pass has a
fixed four-syscall budget, so a replenished connection backlog cannot prevent
the acknowledgement loop from rechecking its monotonic deadline. Reverse traffic is
acknowledgement-only; the UI cannot issue power
commands through this channel. A retained one-way publication still succeeds
with zero clients, but awaiting presentation reports
`acknowledgement_unavailable`; it never counts as committed. The canonical shape
and vocabularies are documented in
[`docs/interfaces/panel-mode-protocol.md`](../docs/interfaces/panel-mode-protocol.md).

## Touch observation and provenance host boundary

`TouchObservationAuthority` is the supervisor-owned single-use evidence
authority seam accepted by ADR 0005. It records evidence only against one exact
committed protocol-v2 presentation, issues opaque CSPRNG identities through an
injected port, and records contact start and completion as separate transitions.
The authority reads its injected monotonic clock at both transitions and derives
the duration internally; its completion API accepts no duration or qualification
value, so an observer caller cannot forge or reset the measured hold. It exposes
separate media-broker and personal-context redemption entry points, so audience
is never supplied inside a claim. Snapshot, render, connection, presence, night,
suspend, restart, expiry, hold, and clock failures invalidate in-progress contact
measurement or unconsumed evidence; structured failures do not echo identifiers
or raw input.

The process-neutral record and bounded handoff live only in
`shared/touch_observation_protocol/`. Its producer uses try-only publication;
queue saturation or producer contention invalidates the stream instead of
blocking panel policy or delivering a plausible partial contact. Coordinates
remain signed raw-axis evidence with no scale, orientation, range, calibration,
resolution, or gesture inference. This is host software evidence: no raw-touch
hardware evidence, cross-process claim transport, exact-holder-UID admission
adapter, target permission cutover, or physical touch behavior is claimed yet.
The role-bound entry points must not be exposed directly to an untrusted request
parser; the future adapter must derive which one is callable from exact kernel
peer credentials.

**Amended 2026-09-25:** the optional `TouchObservationRuntime` wraps the real
lifecycle notifier and owns one `EvdevTouchSource`. No touch configuration means
disabled. Supplying any touch key requires all of these explicit values before
startup hardware actions:

| `BHP_PANEL_SUPERVISOR_` suffix | Contract |
| --- | --- |
| `TOUCH_INPUT_PATH` | Exact absolute evdev node; no discovery or symlink following |
| `TOUCH_INPUT_NAME` | Exact `EVIOCGNAME` identity |
| `TOUCH_PROTOCOL` | `single` (tablet) or `type_b` (multitouch slots) |
| `TOUCH_X_MIN`, `TOUCH_X_MAX`, `TOUCH_Y_MIN`, `TOUCH_Y_MAX` | Signed 32-bit raw bounds, minimum below maximum; must match kernel `EVIOCGABS` |
| `TOUCH_CONTACT_KEY` | `272` (`BTN_LEFT`) or `330` (`BTN_TOUCH`) for single; `0` for Type B |
| `TOUCH_SLOTS` | `1` for single; explicit 1 through 32 Type B slots, kernel slot bounds must match |
| `TOUCH_SOCKET` | Dedicated absolute socket, distinct from the lifecycle path |
| `TOUCH_TTL_MILLISECONDS` | Explicit 1 through 5000 ms observation TTL |
| `TOUCH_MAX_CONTACT_MILLISECONDS` | Explicit 1 through 60000 ms contact bound |
| `TOUCH_MAX_OBSERVATIONS` | Explicit 1 through 1024 retained observations |

The adapter exclusively grabs that device, verifies raw bounds and required
capabilities, and samples the initial kernel contact state. A contact already
held at startup or cancelled during a lifecycle transition must release before
it can start again. Type B tracking identities and all configured slots are
observed; simultaneous contacts cancel the gesture rather than selecting a
finger arbitrarily. SYN_DROPPED, malformed input, read failures, and an exhausted
256-event work budget disable the input observer visibly. The UI alone applies
coordinate calibration and hit testing.

The main daemon services observations in at most 5 ms stop-aware slices between
policy cycles. Every lifecycle publication first invalidates the authority and
active contact. Events queued while awaiting a new committed frame are drained
without forwarding; only subsequent contacts on that exact day/present
commitment can yield observations. Disconnect, absence/grace, night, suspend
preparation, backpressure, and contact failure invalidate evidence. Authority
duration comes from its own monotonic reads, never evdev or UI timestamps.
Start/update forwarding IDs cannot be redeemed; only completion registers the
new unpredictable authority ID. Forwarding sequence numbers count all records.

The dedicated shared Unix observation server enforces exact UI credentials and
reverse silence. Its root-owned parent path must not be writable by the UI;
use a setgid directory with the UI group and mode `2750` for an inherited-group
`0660` socket. Touch failure logs a structured degraded result and never changes
the lifecycle acknowledgement, faults policy, grants brightness, or delays
suspend. Raw axes and observation identities never appear in those logs.

The current policy publishes a new lifecycle snapshot each poll. A contact may
not span that boundary; a local feature render also requires the UI to reject
the prior binding until its next lifecycle acknowledgement. Virtual tap scripts
must wait for the current commitment and use a configured poll interval suited
to their interaction. No generation, duration, calibration, or raw-device
permissions are guessed. The separate capability-holder claim IPC and
production permission cutover remain gated; this implementation does not grant
the UI access to the authority's redemption endpoints.

## Suspend and shutdown behavior

Writing `/sys/power/state` normally returns after physical resume. Whenever an
enabled suspend write returns successfully, the daemon calls
`PanelSupervisor::resume()` immediately, forcing fresh clock, presence, and
solar acquisition instead of restoring pre-suspend outputs. If absence causes
another suspend request, a separate configured retry backoff rate-limits that
next write; poll cadence cannot shorten it. A self-pipe makes both poll and
backoff waits immediately interruptible. SIGINT/SIGTERM disables any new
suspend write during the final resume and lets the foreground process exit
instead of immediately sleeping again.

The capture-closure gate runs after the rate-limit wait and before every
suspend-state write, so a refused closure is a failed suspend request. The
backoff controls a new suspend request after a successful suspend write has
returned and resume recomputation still finds the room absent. A failed suspend
write is not retried: retryable and nonretryable suspend adapter errors both
remain fatal so an unproven power transition cannot become an implicit loop.
This implemented resuspend backoff is one state guard, not the complete planned
PIR debounce and attributed wake-rate policy described above.

Except for the explicitly classified initial clock-readiness wait and the
explicit degraded presentation outcomes above, the daemon exits on every
operational failure. Backlight writes, structured-log writes, and rejected
suspend writes remain fatal. The daemon does not silently turn a hard failure
into absence, day, night, a guessed brightness, or a different suspend mode.
All operations and failures use the repository's canonical
`bhp::StructuredLogger`, typed `Result<T>`, and causal `SupervisorError` chain.

Once the validated backlight adapter exists, every explicit fatal path and
every caught post-construction exception first makes a best-effort exact-zero
write. The primary failure remains the reason for the nonzero exit. A separate
structured record reports whether emergency blanking succeeded; if blanking
also fails, that record retains the backlight adapter cause plus the primary
error code, message, and causal-code chain. Normal signal shutdown also
requires a successful zero write before returning success.

## Build, install, and test

Configure from the repository root so the canonical logging target is present:

```sh
cmake -S . -B build -DBHP_BUILD_TESTS=ON
cmake --build build --target panel_supervisord panel_supervisor_simulation
ctest --test-dir build --output-on-failure
cmake --install build --prefix /usr
```

The install command places the foreground daemon at
`/usr/sbin/panel_supervisord` with the default GNU install directories. Run
`panel_supervisord --help` for its concise usage record; all operational
configuration comes from the environment above.

The host suite covers the pure policy and orchestrator, output ordering, full
notification snapshots, configuration fail-closed behavior, solar threshold
and UTC cases, safe brightness mapping, exact suspend writes, Unix socket
single-client/stale-path behavior, exact-name evdev selection rules, clock
synchronization observations, startup clock retry classification, retry after
an untrusted-clock cycle without intermediate outputs, signal-interruptible
waits, fatal safety-blank behavior after a lit runtime state, preservation of
primary and secondary blank-failure evidence, logging-stream failure, and
failed, timed-out, or disconnected presentations continuing through exact-zero
brightness to suspend.
Test-only injected file access stands in for sysfs where production path
validation is intentionally exact; no host test claims target hardware evidence.

The deterministic `panel_supervisor_simulation` still exercises present/day,
grace, absent/suspend, and present/night resume without Linux hardware. Its
values are test examples, not discovered panel settings.

**Amended 2026-08-08:** assigned future evidence-selected PIR debounce and
attributed wake-rate policy to the supervisor while recording that the current
host scaffold does not implement it and that no unmeasured value is a
production default.

**Amended 2026-09-25:** `mem` now requires an explicit, confirmed
`/sys/power/mem_sleep` variant (ADR 0008 `FRDM-GATE-SUSPEND`); the i.MX 8M Plus
offers `s2idle` and, with PSCI system suspend, `deep`, and which one wakes
reliably from the PIR is unmeasured.

**Amended 2026-09-30:** capture-closure fixture/policy tests also run on macOS.
Three live procfs/pidfd cases remain Linux-only and report explicit skips on
other hosts; non-Linux coverage instead asserts that unavailable pidfds refuse
termination with `ENOSYS`. No Linux evidence requirement or runtime gate changes.
