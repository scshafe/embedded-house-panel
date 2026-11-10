<!-- @canonical bench-tier-decision -->

# ADR 0014: Run a Bench Tier on the Vendor Image

**Status:** accepted. The owner decided on 2026-11-10, relayed by the
oversight session, to go ahead with idea B1 of the November review (PR #80,
`docs/research/REVIEW-AND-ENRICHMENT-2026-11.md`): a bench runtime the owner
can use every day. This record says what the tier is, that its records are
not gate evidence, and exactly what may be installed on the vendor image.

**Date:** 2026-11-10

## Context

Until now the project had three ways to run:
- **Simulation:** the host renderer, the SDL window and the virtual panel
  (ADR 0009).
- **The demo:** `scripts/deploy/frdm_panel_demo.sh` copies a bundle to `/tmp`
  on NXP's vendor image and starts it from a PC session. A power-cycle
  removes it.
- **Production:** the project's Buildroot image. It cannot be built for the
  board yet: the NXP firmware licence is an open owner decision, and the
  device tree waits for the PIR's measured values.

So the owner cannot use the panel. The board as read on 2026-11-10 (raw
record: `hardware_tests/results/frdm/bench-survey-20261110/`):
- **The OS:** NXP i.MX Distro `wrynose`, kernel `6.18.20-2.0.0`, booting
  from the eMMC with an ext4 root; 2.9 GB free.
- **What it has:** systemd 259, Python 3.14 with time-zone data, `setpriv`,
  `sha256sum` and `tar`. Its clock is synchronised by NTP, in UTC.
- **What it lacks:** Tailscale.
- **Root:** an empty password, so the serial console (`ttymxc1`) and the
  `tty1` getty give a root shell to anyone at the board. sshd takes keys only
  (`PermitRootLogin prohibit-password`, `PermitEmptyPasswords no`).
- **weston** is enabled and runs as root on `tty7`.

## Decision

1. **A fourth tier, the bench.** It is the vendor image as it is on the board.
   - The project's services run from persistent storage.
   - systemd starts them at boot, and weston is off.
   - The bench is for daily use and for feeding real bug reports to the
     inbox. It is not a product image.
2. **Its records are not gate evidence.**
   - Every record and status line it writes says `bench_not_gate_evidence`.
   - It runs the vendor's 6.18 kernel, not the project's pinned 6.12, so
     nothing seen on the bench closes an `FRDM-GATE-*` or becomes a default.
     The AGENTS.md invariant on unmeasured values holds unchanged.
   - Production configuration never takes a value from the bench.
3. **What may be installed: exactly three things.**
   - **One install directory, `/opt/house-panel/`.** It holds the project's
     own files and nothing else:
     - `releases/<id>/`: bundles built from this repository;
     - the `current` and `previous` links;
     - `state/`: the services' runtime state (study store, outboxes, tokens),
       each subdirectory owned by its service's UID.
   - **One configuration directory outside Git, `/etc/house-panel/`.**
     - `bench.json`: the location, the time zone, the rotation and the awake
       hours.
     - The optional per-service files the demo already defines, plus the
       music client ID and the inbox URL.
     - It holds no secret. It names the house's location and tailnet, so it
       never enters Git.
   - **systemd units:** `/etc/systemd/system/bhp-bench-*.service`,
     `bhp-bench.target` and their enablement links.
     - Turning weston off is done with `systemctl disable` and `mask`,
       recorded in the install directory so that uninstalling restores it.

   **Never:**
   - a vendor, `opkg` or `apt` package;
   - an entry in the user database (services run under bare UIDs through
     `setpriv`, as in the demo);
   - a udev rule, or a change to `/etc/localtime` (each unit gets `TZ`);
   - a change to sshd, `/root/.ssh/authorized_keys`, U-Boot, the boot
     partition or the boot switch;
   - flashing.

   Anything that needs one of these comes back to the owner as a question.
4. **Install, rollback and uninstall.** The board-side script
   `scripts/deploy/frdm_bench/bench_board.py` handles all three.
   - **Install** takes a bundle tarball and its SHA-256, which the PC passes
     as an argument.
     - It checks that hash, then every file against the bundle's
       `SHA256SUMS`.
     - It checks that each binary is aarch64 and that its libraries resolve.
     - It unpacks into `releases/<id>.partial`, renames that into place,
       swaps `current` in one rename, and keeps the release before it as
       `previous`. At most two releases are kept.
     - It installs the bundle's units and restarts `bhp-bench.target`.
   - **`rollback`** swaps `current` and `previous` back.
   - **`uninstall`** stops and removes the units, restores weston as it
     was, and removes `releases/`.
     - It keeps `state/` and `/etc/house-panel/` unless given `--purge`.
5. **Runtime.**
   - **One unit per service,** with `Restart=always` and bounded backoff.
   - **Under bare UIDs:**
     - the supervisor is root (it owns the backlight);
     - the UI is 1102, the media broker 1103, music 1105 and the reporter
       1106, which are the production UIDs from
       `system_image/service-principals.json`;
     - the voice-lab uploader is 1107;
     - three groups: 1108 between the broker and the uploader, 1109 between
       the broker and the reporter, and 1110 for every service's log tee to
       reach the reporter.
   - **Presence:** there is no PIR. The demo's simulated PIR device stays,
     driven by `bhp-bench-presence`. Presence is on during the configured
     awake hours, and for a set time after any touch. Suspend stays
     disabled.
   - **Day and night:** the supervisor's offline solar calculation from
     `bench.json`'s latitude and longitude. Two decimals are enough. The
     clock shows the configured time zone.
   - **Logs:** each service's output goes to the journal and, through
     `bhp-log-tee`, to `bhp-reporter`.
6. **No credential before the console is closed.**
   - No refresh token or tailnet key goes on the board while root has an
     empty password.
   - **Closing the console is the owner's step:** setting a root password
     the agent never sees.
   - **The tailnet:** enrolment is approved. The auth key is minted and
     entered by the owner, never by an agent.
     - Tailscale is not on the vendor image, and the official static
       archive (ADR 0010's pinned `1.102.2`) is not the project's own files.
       Placing it under item 3 is therefore an owner question.
   - **The music and voice-lab tokens** come from the owner's own sign-in
     steps (ADR 0012, ADR 0013).

## Consequences

- **The owner gets a panel that survives a power-cycle:**
  - the clock and real day and night;
  - the vocabulary cards, offline until the tailnet is enrolled;
  - the Report button;
  - music, after the owner's Spotify sign-in.
- **The bug-report loop** starts receiving real reports once the tailnet is
  enrolled.
- **Every vendor-image observation** has to be repeated on the project kernel
  before it counts. The bench makes that gap visible rather than closing it.
- **The install writes the eMMC's root file system.** It is reversible
  (`uninstall --purge`), and the factory backup still restores the board.

## Alternatives

- **Keep the `/tmp` demo:** nothing survives a power-cycle, and every start
  needs a PC session.
- **Wait for the project image:** blocked on the firmware licence and the
  PIR. See idea B2 in the review.
- **Install the vendor's packages or create users:** refused by the owner
  for the bench.
