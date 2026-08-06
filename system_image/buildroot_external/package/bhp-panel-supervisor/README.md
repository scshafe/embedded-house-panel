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
permission to replace it. Development builds use Buildroot's local-source
method. Release verification always allocates a fresh output named by the exact
project content ID, so no prior local-package stamp can satisfy the gate.
