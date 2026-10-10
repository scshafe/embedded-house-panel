# Continue development on Arch Linux

**Amended 2026-09-30:** the user reports receiving the FRDM board and is moving
software development to the Arch PC. This guide prepares the workstation; it
neither flashes a device nor closes any hardware gate. The current development
branch is `main`, which now includes all `pre-board` and cloud-session work.

**Amended 2026-10-04: migrated to cole-arch-pc as a scshafe-dev project.**

- The checkout is `~/src/embedded-house-panel`, the scshafe-dev convention for
  every project on this PC. The `~/Projects/beaglebone-house-panel` path below
  is superseded.
- The repository is adopted as a scshafe-dev `native` project. `dev.toml`
  `[verify]` is `make verify`, and `dev check .` must also pass. `AGENTS.md`
  ends with the managed "Verify and landing" section.
- `make verify` passed on Arch on 2026-10-04: 48/48 CTests, the exact screen
  comparisons, and the Python suites. It skipped the same three cases as the
  Mac: no AArch64 cross compiler, `KERNEL_DIR` unset, and `BHP_BUILDROOT_DIR`
  unset.
- `make verify` needs `go` on `PATH`: the plan verification map names it, and
  `check_plan_verification_map.py` fails with `RECORDED_EXECUTABLE_UNRESOLVED`
  without it. The bootstrap package list now includes `go`.
- The Mac's migration folder, including `lexinet.bundle` (9/9 `SHA256SUMS` OK),
  and the outer folder's notes are in
  `~/Documents/embedded-house-panel-from-air/`. The pinned Lexinet checkout is
  `~/.local/share/embedded-house-panel/lexinet` at `ce42272`.
  `config/device.env` was recreated with that path and the Mac's base revision.
  GitHub's `scshafe/lexinet` does not contain `ce42272`; use the bundle.
- No FRDM board was visible on USB on 2026-10-04: no new USB device, and no
  `/dev/ttyUSB*` or `/dev/ttyACM*`. Inventory starts once the board's debug USB
  enumerates.
- Later on 2026-10-04 the board enumerated (after a reboot of the PC, whose
  running kernel had lost its modules to an upgrade) and ARRIVAL-01, -03 to -06
  ran. Status, access and the 2026-10-05 health check:
  [frdm-bringup.md, Arrival status](frdm-bringup.md#arrival-status-2026-10-05).
- **Amended 2026-10-05:** `dev.toml [hardware]` (scshafe-dev 0.9.0) declares the
  board: the console, the LAN address, `access = "owner"`, the owner-gated
  actions and the hardware gate `make verify-board`. Export `config/device.env`
  (`BHP_BOARD_SSH_TARGET`, `BHP_BOARD_CONSOLE`, `BHP_FRDM_FACTORY_BACKUP_DIR`)
  before running it.
- **Amended 2026-10-10:** `access = "agent"` (scshafe-dev 0.11.0): the project
  agent opens the console and reaches the board over ssh with its own key;
  `dev identity verify` checks the grant. The owner-gated actions are unchanged.

## Get the complete source

**Amended 2026-10-02:** the user authorized publishing. `pre-board` was
fast-forwarded into `main` and pushed to GitHub, together with the archival refs
below. Clone GitHub directly; the Taildrop archive and Git bundle are no longer
needed:

```sh
# Run on Arch. Requires Git and a GitHub SSH key for this machine.
mkdir -p "$HOME/Projects"
git clone git@github.com:scshafe/embedded-house-panel.git "$HOME/Projects/beaglebone-house-panel"
cd "$HOME/Projects/beaglebone-house-panel"
git status --short --branch
git log -5 --oneline
```

There is just one repository directory in this new layout. Do not reproduce the
Mac's outer/inner directory nesting. `AGENTS.md`, plans, ADRs, tests, fonts and
screen baselines are committed and arrive with the clone. Continue on `main`;
the merged `pre-board`, `virtual-panel`, `frdm-imx8mp-retarget` and
`architecture/apply-review-amendments` branches were retired after merging.

`archive/2026-09-30-font-verification-draft` and
`archive/2026-09-30-study-verification-draft` preserve unique older verification
drafts. `archive/2026-09-30-pre-retarget-stash` preserves the superseded ADR stash.
These are historical recovery refs on GitHub; fetch one only if needed and do not
merge them. The Mac's verification worktrees were matched against committed file
contents and removed.

## Install host dependencies explicitly

On an Arch Linux **x86_64** desktop, run:

```sh
scripts/environment/bootstrap_arch_build_host.sh --dry-run
scripts/environment/bootstrap_arch_build_host.sh --execute
scripts/environment/check_host_dependencies.sh --profile host --strict
scripts/environment/check_host_dependencies.sh --profile buildroot --strict
pkg-config --modversion openssl libcurl libpng sdl2 libdrm
```

The execute command uses interactive `sudo pacman -Syu --needed`, so review the
full upgrade transaction. Arch does not support partial upgrades. Packages cover
GCC/G++, CMake, Ninja, Python, ShellCheck, ripgrep, OpenSSL, curl, PNG, JSON, SDL,
DRM, Buildroot prerequisites, and `picocom`/USB inventory tools. No AUR packages, service enablement or Tailnet
admin changes are performed. The official [sdl2-compat package](https://archlinux.org/packages/extra/x86_64/sdl2-compat/)
provides SDL2, and [pkgconf](https://archlinux.org/packages/core/x86_64/pkgconf/)
provides pkg-config. Package versions follow the workstation's Arch repositories;
project source pins remain in CMake and the Buildroot configuration.

## Verify and open the panel

```sh
export CMAKE_BUILD_PARALLEL_LEVEL=2
make verify
make verify-docs
make verify-panel-goldens
make sim-panel
```

Initial builds download hash-pinned LVGL and, if absent, JSON headers. A fresh
machine therefore needs HTTPS access to the declared dependency sources.
Generated build trees stay under `.build/`; do not copy the Mac's `.build/`
into the Arch checkout. Increase parallelism later based on RAM and disk.

Run `make sim-panel` from a terminal in the Arch graphical desktop (X11 or
Wayland). It runs the native Linux SDL binary, starts the local synthetic HTTPS
fixture, and opens the same 1024×600 shared renderer used on the Mac. SSH alone
usually has no desktop display; use the headless checks there instead. The
Mac app-bundle/codesigning path remains exclusive to macOS.

Keys: H Home, V vocabulary, A Agenda preview, R night, D day, T advance the
preview clock one minute, Escape quit. Home configuration is
[`panel_ui/config/applications.json`](../../panel_ui/config/applications.json).
The fixture server listens only on loopback:9443 and generates fresh ignored
TLS identity and synthetic study state. Nothing is added to system trust.

To capture all native states, from the graphical desktop:

```sh
make verify-panel-simulator
```

The existing `run_virtual_panel.sh` launcher requires Apple Silicon macOS,
Cocoa and HVF. It is **not** the Arch launch command. Headless and native SDL
work do not require that VM. An Arch QEMU runner and its input/display proof
need a separate Linux adaptation; do not treat installing QEMU as validation.

## Optional Lexinet contract work

Lexinet is not on GitHub. The Mac's ignored
`.build/arch-migration/house-panel-migration/` folder holds `lexinet.bundle`,
read from the existing source checkout without changing it. Copy that folder to
`$HOME/Downloads/` on Arch yourself. It includes the pinned history, avoiding a
Mac-specific SSH remote:

```sh
git clone --branch main "$HOME/Downloads/house-panel-migration/lexinet.bundle" "$HOME/Projects/lexinet"
git -C "$HOME/Projects/lexinet" checkout --detach ce42272df315f357e10998dd48e9ffd1a935c2ff
export BHP_LEXINET_SOURCE_DIR="$HOME/Projects/lexinet"
export BHP_LEXINET_BASE_REVISION=3e15ef60e1970757e756d74622846edba166757c
```

Normal panel `make verify` does not require a live Lexinet server. The separate
`make verify-lexinet-live` harness needs Go matching Lexinet's `go.mod` and
**PostgreSQL 16 server binaries**, including `initdb` and `pg_ctl`. The rolling
Arch `postgresql` package must not be assumed to supply that major version.
Provision an isolated PostgreSQL 16 toolchain/container workflow before rerunning
this gate; its current host-binary runner supports `--postgres-bin-dir` through
`scripts/lexinet/live_contract.py --help`. Do not point it at a personal database.
See [the existing real-server proof](../testing/lexinet-live-contract-validation.md).
Real Tailnet authorization/revocation/TLS qualification remains separate from
being able to SSH to this development PC. User-controlled admin changes remain
human steps; never copy panel credentials or Tailnet state into Git.

## Resume point and board arrival

Read these first in the new session:

1. `AGENTS.md` and [the ADR registry](../decisions/README.md), particularly ADRs
   0005–0010 for touch, lifecycle, simulation and Tailnet choices.
2. [Latest pre-board validation](../testing/pre-board-completion-validation.md)
   and [the pre-board plan](../../.claude/plans/2026-09-25-pre-board-completion.md).
   The cloud session implemented real Lexinet loopback tests, pinned Tailscale
   packaging, capture-closure enforcement and fault-matrix mapping. These are
   newer than the UI-only handoff.
3. [The end-to-end roadmap](../../.claude/plans/2026-08-05-end-to-end-development-roadmap.md)
   and [FRDM arrival checklist](frdm-bringup.md).

Next: establish the Arch baseline above, then inventory the received board and
accessories using ARRIVAL-01 onward. User-reported receipt is not an observed
board revision or electrical measurement. Preserve factory storage according to
the bring-up runbook before modifying it; do not enable the project production
services or fill `UNMEASURED` values from assumptions. The firmware EULA and
other outstanding human decisions retain their gates.

The old plan labels some Linux/VM tasks “Mac-local”. They describe where those
checks were intended to run, not a requirement to compile on a Mac. Adapt the
runner and amend the mapped verification steps before claiming those remaining
proofs on Arch. Keep the accepted pins: the Buildroot 2026.08 decision still
requires the archive digest/source identity update; do not silently adopt a
newer release or reuse the old 2026.05.1 archive as evidence for it.

## What travels and what stays local

Preparation was verified on Apple Silicon macOS on 2026-09-30: `make verify`
passed, including 48 CTests and 43 exact screen comparisons. A fresh bundle
clone passed `git fsck --full`, `make verify-docs`, 22 simulator tests, 3 Arch
bootstrap guard tests and 29 panel verification tests. The bundle verified as
self-contained. Linux launcher selection was unit-tested; no command has yet
been run on the Arch workstation and no hardware was exercised.

The Mac run explicitly skipped three Linux-only procfs/pidfd cases, AArch64
cross compilation (compiler unavailable), the presence kernel-module build
(`KERNEL_DIR` unset), and Buildroot defconfig evaluation (`BHP_BUILDROOT_DIR`
unset). These remain separate Linux/target verification work; the passing host
gate does not establish them. Package installation remains an explicit user
action on Arch.

The project now travels through GitHub. The Mac's migration folder retains Git
bundles, checksums, a revision manifest and selected public review artifacts. `config/device.env`, tokens,
private TLS/SSH keys, Tailnet state, VM disks, databases, build caches, machine
logs and per-session `.claude/handoff.local.md` are excluded. Recreate local
configuration deliberately. Durable source/evidence summaries are committed;
the old Mac's private/runtime files are preserved there and not deleted.

After the baseline, a suitable agent continuation prompt is:

> Read AGENTS.md and docs/operations/arch-development.md. We are continuing
> on main on Arch; the FRDM board has arrived but has no new hardware evidence.
> Check the current Git state and recorded decisions, run the Arch baseline,
> then proceed through the FRDM arrival inventory. Preserve measured-evidence
> gates and existing source pins; ask before pushing. Do not
> install system packages or flash storage implicitly.
