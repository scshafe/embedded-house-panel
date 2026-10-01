# Continue development on Arch Linux

**Amended 2026-09-30:** the user reports receiving the FRDM board and is moving
software development to the Arch PC. This guide prepares the workstation; it
neither flashes a device nor closes any hardware gate. The current development
branch is `pre-board`, including the cloud-session work after `feba096`.

## Get the complete source

If the migration archive was delivered through Taildrop, retrieve and unpack it
as follows. `file get` keeps conflicting files by default; do not use overwrite.
The archive name includes this migration date. Extract it as your normal user:

```sh
mkdir -p "$HOME/Downloads"
sudo tailscale file get "$HOME/Downloads"
cd "$HOME/Downloads"
sha256sum -c house-panel-migration-2026-09-30.tar.gz.sha256
tar -xzf house-panel-migration-2026-09-30.tar.gz
```

This uses the existing [Taildrop inbox workflow](https://tailscale.com/kb/1106/taildrop);
no SSH account or Tailnet admin change is needed for an already eligible device.


At migration preparation time GitHub contains only the older `main` branch.
The earlier no-push instruction remains in effect. The supplied
`house-panel.bundle` is self-contained Git history, including all local branches
and archival refs. Copy the migration folder to this machine first. Adjust the
bundle directory below to its actual location; clone into a new directory.

```sh
# Run on Arch. Requires Git; if absent, install it with your normal pacman workflow.
cd "$HOME/Downloads/house-panel-migration"
sha256sum -c SHA256SUMS
# Verify bundle integrity without requiring an existing project checkout.
git init --bare bundle-check.git
git -C bundle-check.git bundle verify "$PWD/house-panel.bundle"
mkdir -p "$HOME/Projects"
git clone --branch pre-board "$PWD/house-panel.bundle" "$HOME/Projects/beaglebone-house-panel"
cd "$HOME/Projects/beaglebone-house-panel"
git remote rename origin migration-bundle
git remote add origin git@github.com:scshafe/embedded-house-panel.git
git branch --unset-upstream
git status --short --branch
git log -5 --oneline
```

There is just one repository directory in this new layout. Do not reproduce the
Mac's outer/inner directory nesting. `AGENTS.md`, plans, ADRs, tests, fonts and
screen baselines are committed and arrive with the clone. The migration
manifest records the expected `pre-board` commit; compare it with `git rev-parse
HEAD`. No upstream is configured until this branch is published by an authorized
push. A normal clone of GitHub `main` is not an equivalent starting point yet.

`archive/2026-09-30-font-verification-draft` and
`archive/2026-09-30-study-verification-draft` preserve unique older verification
drafts. `archive/2026-09-30-pre-retarget-stash` preserves the superseded ADR stash.
These are historical recovery refs; continue on `pre-board` and do not merge
them. Other verification copies were matched against committed file contents.
The bundle includes those contents through history, without copying worktrees.

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

The handoff also supplies `lexinet.bundle`, read from the existing source
checkout without changing it. It includes the pinned history, avoiding a
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

The migration folder contains Git bundles, checksums, a revision manifest and
selected public review artifacts if available. `config/device.env`, tokens,
private TLS/SSH keys, Tailnet state, VM disks, databases, build caches, machine
logs and per-session `.claude/handoff.local.md` are excluded. Recreate local
configuration deliberately. Durable source/evidence summaries are committed;
the old Mac's private/runtime files are preserved there and not deleted.

After the baseline, a suitable agent continuation prompt is:

> Read AGENTS.md and docs/operations/arch-development.md. We are continuing
> pre-board on Arch; the FRDM board has arrived but has no new hardware evidence.
> Check the current Git state and recorded decisions, run the Arch baseline,
> then proceed through the FRDM arrival inventory. Preserve measured-evidence
> gates, existing source pins, and the no-push rule unless I change it. Do not
> install system packages or flash storage implicitly.
