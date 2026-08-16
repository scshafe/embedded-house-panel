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

The version-1 Unix-socket adapter retains and offers snapshots exactly as
before, but it has no renderer-ready acknowledgement and therefore returns
immediate `not_committed` with reason `acknowledgement_unavailable`. It never
fabricates commitment from a successful socket write or connected client, so
the production version-1 path remains dark until the version-2 acknowledgement
protocol is implemented and enabled.

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
  protocol-v1 JSON Lines snapshots. Publication returns a pending sequence;
  awaiting it returns immediate non-commit because version 1 is one-way.
- `SysfsSuspend`, which writes only the explicitly selected `freeze` or `mem`
  token to exactly `/sys/power/state`.

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
| `BHP_PANEL_SUPERVISOR_DAY_BRIGHTNESS` | Logical percentage, 0 through 100 |
| `BHP_PANEL_SUPERVISOR_NIGHT_BRIGHTNESS` | Logical percentage, 0 through 100 |
| `BHP_PANEL_SUPERVISOR_GRACE_DAY_BRIGHTNESS` | Logical percentage, 0 through 100 |
| `BHP_PANEL_SUPERVISOR_GRACE_NIGHT_BRIGHTNESS` | Logical percentage, 0 through 100 |
| `BHP_PANEL_SUPERVISOR_OFF_BRIGHTNESS` | Required to be exactly `0`; nonzero off/suspend output is rejected |
| `BHP_PANEL_SUPERVISOR_SUSPEND_MODE` | Exactly `disabled`, `freeze`, or `mem` |
| `BHP_PANEL_SUPERVISOR_POWER_STATE_PATH` | Required for `freeze` or `mem`; exactly `/sys/power/state` |
| `BHP_PANEL_SUPERVISOR_POLL_INTERVAL_MILLISECONDS` | 50 through 5,000 milliseconds |
| `BHP_PANEL_SUPERVISOR_SUSPEND_RETRY_BACKOFF_MILLISECONDS` | Required for enabled suspend; 1,000 through 300,000 milliseconds |

`SUSPEND_MODE=disabled` is the explicit non-suspending configuration and does
not require `POWER_STATE_PATH`. Latitude and longitude are treated as sensitive
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

Every publication is one complete version-1 snapshot with monotonically
increasing `event_sequence`, display mode, presence state, solar state, system
state, effective logical brightness, UTC observation timestamp, and local
request ID. A newly accepted client receives the retained current snapshot
before the next incremental snapshot. At most one UI client is retained;
extra, slow, or disconnected clients are isolated and closed without faulting
panel policy. The server never reads client data; the UI cannot issue power
commands through this channel. A retained one-way publication still succeeds
with zero clients, but awaiting presentation reports
`acknowledgement_unavailable`; it never counts as committed. The canonical shape
and vocabularies are documented in
[`docs/interfaces/panel-mode-protocol.md`](../docs/interfaces/panel-mode-protocol.md).

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

The backoff controls a new suspend request after a successful suspend write has
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
