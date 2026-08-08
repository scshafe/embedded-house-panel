# Buildroot package: panel supervisor

This directory is the canonical Buildroot glue for the panel-supervisor work
stream. It builds from the repository root so the supervisor uses the shared
structured logger, disables all other application domains, and installs the
foreground Linux runtime.

Public Buildroot interface:

- Kconfig symbol: `BR2_PACKAGE_BHP_PANEL_SUPERVISOR`
- package target: `bhp-panel-supervisor`
- installed daemon: `/usr/sbin/panel_supervisord`
- IPC client group: `vocabulary-panel`

`S70panel_supervisord` remains opt-in because its GPIO/input, backlight, local
IPC, solar-time, and suspend adapters have not been exercised on the target
assembly. The daemon validates its complete environment before opening any
device. Its init path creates the IPC directory as root:vocabulary-panel mode
2750, allowing the UI to traverse/connect to the group-owned socket without
permission to replace it.

When enabled, the init service runs one foreground shell supervisor around the
foreground daemon. An unexpected exit is followed by at most three restart
attempts, each after an explicit five-second backoff; exhausting that allowance
leaves the service stopped until an operator starts it again. Every attempt and
outcome is a structured init log. `panel_supervisord.pid` remains the daemon
PID, while `panel_supervisord-supervisor.pid` identifies the process targeted
by `stop`, `status`, and `restart`. TERM is forwarded to the daemon and awaited,
so intentional service control uses the daemon's blank-before-exit path and
never consumes a restart. Socket ownership and mode are re-established before
each replacement daemon is marked ready.

Development builds use Buildroot's local-source method. Release verification
always allocates a fresh output named by the exact project content ID, so no
prior local-package stamp can satisfy the gate.
