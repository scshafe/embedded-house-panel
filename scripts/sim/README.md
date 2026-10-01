# Virtual panel operations

These tools implement ADR 0009 and the [virtual-panel plan](../../.claude/plans/2026-09-25-virtual-panel.md).
Every result is `simulation_not_hardware_evidence`. They never enable production
services or change the active FRDM profile. All runtime files live in `.build/`.

## Headless and native display

```sh
make verify-panel-goldens
make sim-panel
make verify-panel-simulator
```

The first command builds the shared LVGL renderer and checks exact pixels against
43 committed baselines. Differences produce a PNG and JSON under
`.build/screenshots/`. `make update-panel-goldens` is an explicit update: inspect
`.build/screenshots/review/before-after.png` and include the before/after images
in the baseline-changing commit. An initial baseline has no predecessor.

`make sim-panel` builds the opt-in SDL2 target, starts the loopback-only synthetic
Lexinet HTTPS server if needed, and opens the 1024×600 panel. Its CA, server key,
cache and outbox are ignored checkout-local files. The CA is never installed in
macOS trust. TLS certificate and hostname checks remain enabled. The default
profile is `fixture-default`; all words and topics are synthetic.

[Simulator input commands](../../simulator/README.md) cover mouse, keyboard and
JSON lines. `make verify-panel-simulator` opens each scenario in its own actual
SDL window and writes `.build/sim/native/contact-sheet.png`, a labelled `contact-sheet.html`, and `report.json`.
Scenario capture uses deterministic data without network timing.

On Linux, the same native launcher runs the SDL executable directly; launch it
from an X11/Wayland desktop session. See [Arch setup](../../docs/operations/arch-development.md).
The guest launcher below remains Apple Silicon/macOS-specific.

## Debian arm64 guest

Host dependencies: Apple Silicon macOS, QEMU with Cocoa, HVF, PNG and user
networking, `hdiutil`, SSH, CMake, Ninja, pkg-config, OpenSSL and SDL2. Tools do not
install host software. On this Mac QEMU is built locally with
`scripts/sim/build_local_qemu.sh --execute`; its signed app bundle and libraries
stay inside `.build/qemu/install/`. The launcher prefers that local bundle,
then a local binary, then an installed QEMU on PATH. The launcher prepares a pinned [Debian generic arm64 cloud
image](https://cloud.debian.org/images/cloud/trixie/20260914-2601/), verifies its
SHA-512, and creates a 24 GiB sparse overlay. The guest requires network access
for Debian packages and pinned source dependencies. No NXP firmware is used.

```sh
scripts/sim/run_virtual_panel.sh             # JSON dry run
scripts/sim/run_virtual_panel.sh --execute   # foreground Cocoa VM
python3 scripts/sim/provision_virtual_panel.py           # dry run
python3 scripts/sim/provision_virtual_panel.py --execute # separate terminal
```

Provisioning waits for cloud-init, uploads only Git-visible sources, builds
natively with CMake, runs Linux tests, builds the simulated backlight kernel
module, and starts isolated `bhp-sim-*` services. If cloud-init installed a newer
kernel than the running image, the wrapper reboots the guest and waits for SSH;
matching headers are required. Reprovisioning completes compilation and tests
before replacing all running simulation services and reloading the module. SSH uses the ignored key in `.build/sim/ssh_key`,
port 2228, and an isolated known-hosts file. Source archives contain no ignored
credentials. Only the public fixture CA is copied to the guest.

```sh
python3 scripts/sim/panel_ctl.py status             # JSON dry run
python3 scripts/sim/panel_ctl.py --execute status
python3 scripts/sim/panel_ctl.py --execute tap 500 300
python3 scripts/sim/panel_ctl.py --execute presence off
python3 scripts/sim/panel_ctl.py --execute presence on
python3 scripts/sim/panel_ctl.py --execute scenario night
python3 scripts/sim/panel_ctl.py --execute screenshot night
python3 scripts/sim/panel_ctl.py --execute select-touch type_b
python3 scripts/sim/panel_ctl.py --execute tap 500 300 --multitouch
make verify-panel-vm
```

The supervisor exclusively observes the selected raw input device. The UI has
only DRM and authenticated normalized touch access. Each committed generation
must be acknowledged by a lifecycle publication before a subsequent touch is
accepted. Simulation policy publishes every second; allow at least a second
between interactions. Contacts crossing a lifecycle change are invalidated.
The host prints real simulated-backlight brightness; QEMU pixels cannot dim.
The launcher explicitly selects the observed tablet through QMP at startup so
Cocoa button and absolute-axis events reach the same device; device ordering
alone does not guarantee this. The controller repeats the same selection before
a single-touch gesture. A real Cocoa
window click has been verified, alongside QMP single-touch and Type-B contacts.
Use Control-Option-G to release QEMU's captured Mac pointer.

Day/night changes select a fixture longitude around solar noon/midnight at the
current UTC time; the guest clock remains chrony synchronized. Presence grace is
an explicitly accelerated three-second fixture. Suspend stays **disabled**:
there is no measured host wake guarantee. Existing fake-adapter tests cover
suspend policy. The fixture-control endpoint cannot publish lifecycle snapshots
or inject application touches.

VM verification must actually observe a running guest, active services, accepted
touch commits, PIR blanking and every PNG. Missing QEMU/guest is a failed gate.
The VM contact sheet is `.build/sim/vm/contact-sheet.png`, with a labelled HTML
companion and `report.json`. Screenshots, commands and service logs remain under
`.build/sim/`; no VM result
closes `FRDM-GATE-*`. The generic [QEMU virt machine](https://www.qemu.org/docs/master/system/arm/virt.html)
exercises aarch64 software, Linux DRM completion, evdev and socket authentication.
It does not emulate the FRDM display wiring, GT911, power rails, physical red
calibration, suspend current or wake chain. Production uses the fail-dark backend
until real `FRDM-GATE-DISPLAY` evidence exists.

**Amended 2026-09-25:** added deterministic visual tiers and isolated guest tools.
Actual execution status belongs in the plan and generated reports, never in the
existence of these launch instructions.

The `clock-untrusted` guest scenario labels unavailable **fixture** clock data;
it preserves the real NTP-backed supervisor lifecycle. The native scenario uses
the scripted diagnostic lifecycle. Guest grace uses real 70%→60% backlight
values; native deterministic fixtures use 80%→40%. These two declared
differences are checked semantically; the other 27 guest captures match the
headless goldens exactly. Neither fixture is a household timezone decision.

The ignored fixture TLS certificate expires after 30 days. Launch and provisioning
then stop with `FIXTURE_TLS.EXPIRED` before starting another server. Preview recovery
with `python3 scripts/sim/fixture_https.py --rotate-expired`; add `--execute` to
verify and stop only the recorded fixture process, preserve its old identity under
`.build/sim/tls-expired-*/`, and start a replacement. An unrelated listener or a
reused PID is rejected. Afterwards run
`python3 scripts/sim/provision_virtual_panel.py --execute` to copy the new public
CA to the guest, and restart any native panel with `make sim-panel`. Trust is
never rotated automatically or installed in macOS trust.

## Microphone capture closure

`scripts/sim/guest/capture_closure_matrix.py` proves the supervisor's
capture-closure gate against a real `snd-aloop` capture substream inside the
guest. It runs as root and needs the guest's generic arm64 kernel. Each case
opens the loopback capture node, hands the descriptor to a holder dropped to an
exact numeric UID with `setpriv --clear-groups`, then runs the check-only
`capture_closure_check` from the guest's CMake test build. That tool evaluates
the daemon's `CaptureClosureGatedSuspend` with an inner suspend that never
writes `/sys/power/state`. The six cases are:

- closed baseline;
- a broker-UID holder that exits on SIGTERM;
- a broker-UID holder that ignores SIGTERM and is killed;
- a foreign-UID holder, which refuses the suspend at the 2,000 ms deadline;
- a holder with no termination path configured, which refuses at once;
- closed again at the end.

```sh
python3 scripts/sim/guest/capture_closure_matrix.py --check-binary <build>/panel_supervisor/capture_closure_check            # JSON plan
sudo python3 scripts/sim/guest/capture_closure_matrix.py --execute --check-binary <build>/panel_supervisor/capture_closure_check
```

The report goes to `/var/lib/bhp-sim/capture-closure-matrix.json`. It loads
`snd-aloop` only if needed and unloads it afterwards. `--stand-in` swaps the
device for a regular file at `/dev/snd/pcmC99D0c`, for hosts without ALSA. That
proves holder discovery and the termination path but not kernel substream
status, and the report says `kernel_substream_status: not_exercised`. Every
result is `simulation_not_hardware_evidence`; microphone rail-off is an FRDM
gate.
