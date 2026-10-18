# Plan: Demo the Panel UI on the FRDM Board

<!-- @canonical frdm-panel-ui-demo-plan -->

**Status (2026-10-18):** Steps 1–3 done on the host; Step 0 (ssh) and Step 4
(the demo) wait for the owner. Requested by the
owner after the panel, touch, backlight and XVF3800 came up on the vendor
image ([bring-up](../../docs/operations/frdm-bringup.md), "Panel on J27").

**Goal:** show the real panel UI (Home, launcher, vocabulary flash cards) on the
Waveshare panel with touch, driven by the real panel supervisor, on the
vendor image that is already on the board, without writing to its storage.

**Out of scope:** the project system image and its owner decisions (NXP
firmware EULA, kernel pin); voice (no recognizer is selected,
`config/voice/README.md`); the real PIR (waits for a meter); suspend; live
Lexinet data (the board is not on the tailnet); production users, services or
configuration values. Nothing here closes an FRDM gate: every record is
labelled `demo_not_gate_evidence`.

## Starting point

- The repo already has the path this demo needs. The virtual-panel guest
  ([scripts/sim/guest/runtime.py](../../scripts/sim/guest/runtime.py)) runs
  `panel_supervisor` and `bhp-panel-ui --simulation-drm`: the real shell
  controller, reducers and LVGL surfaces, presented by
  `DrmPageFlipDisplay`, with touch read by the supervisor's evdev observer and
  a uinput fake PIR (`scripts/sim/guest/pir_uinput.c`). The board demo is that
  stack on real hardware instead of virtio-gpu.
- The ordinary `bhp-panel-ui` stays fail-dark; only the `--simulation-drm`
  branch (`BHP_BUILD_PANEL_DRM`) draws.
- Board facts, read on the console on 2026-10-18 (vendor image, panel DTB):
  - glibc 2.43;
  - `/tmp` is tmpfs (1.8 GB);
  - `libdrm.so.2`, `libpng16.so.16`, `libstdc++.so.6` and `libcurl.so.4` are
    in `/usr/lib`;
  - `setpriv` and `modetest` exist, as does `/dev/uinput` (root only);
  - `/dev/dri/card1` (`imx-drm`, `root:video` 0660) drives the panel:
    connector `DPI-1`, 1024×600;
  - card1's planes have no `rotation` property;
  - weston holds DRM master;
  - touch is `event2`, axes X 0–799 and Y 0–479.
- The host has clang 23 with the AArch64 target and lld, but no aarch64
  sysroot. The `prerequisites` cross compiler in `dev.toml` is not installed.
- The agent's ssh grant still names 192.168.7.181; the board is now
  192.168.50.15. Copying megabytes over the 115200-baud console is not
  practical.

## Owner decisions (2026-10-18)

1. **Weston stops during the demo.** The DRM backend needs DRM master; `stop`
   starts weston again. The owner agreed.
2. **The panel stays upside-down and the UI rotates it** (Step 2). The weston
   rotation does not apply to a DRM client, and card1 has no hardware
   rotation. The owner chose the software option over turning the panel round.

## Steps

- [ ] **Step 0: Restore ssh (owner).**
  - The owner moves the agent's host grant to `192.168.50.15:22`.
  - Verify: `ssh root@192.168.50.15 true` succeeds from the agent account.
- [x] **Step 1: Cross-build for the vendor image.** (2026-10-18)
  - `scripts/build/frdm_vendor_sysroot.sh` is a dry run by default. With
    `--execute` it downloads 19 pinned Debian trixie arm64 `.deb`s from
    snapshot.debian.org (20261015T000000Z), checks each SHA-256 and unpacks
    them with `bsdtar` into `.build/frdm-sysroot/`. The packages are glibc,
    the gcc-14 runtime and libstdc++ headers, `linux-libc-dev`, libdrm, libpng,
    zlib and OpenSSL. The panel mode protocol needs libcrypto, which the board
    has as `libcrypto.so.3`. The script needs no root and installs nothing on
    the PC.
  - `cmake/toolchains/frdm-vendor-aarch64.cmake`: clang
    `--target=aarch64-linux-gnu` for C, C++ and assembly (LVGL has `.S`
    files), lld, the sysroot above, `-static-libstdc++ -static-libgcc`.
  - `scripts/deploy/frdm_panel_demo.sh build --execute` configures with
    `BHP_BUILD_PANEL_DRM` and `BHP_BUILD_PANEL_RENDERER` on, Lexinet HTTP and
    tests off, builds `bhp-panel-ui` and `panel_supervisord`, and compiles
    `pir_uinput.c`. It refuses any binary whose `NEEDED` libraries are not on
    the board or that needs glibc symbols above 2.41. It then packs
    `.build/frdm-demo-bundle.tar.gz` (2.2 MB): the three binaries, the
    scenarios, `applications.json`, the board script and SHA256SUMS.
  - Verify:
    - `make verify` exit 0;
    - `scripts/deploy/frdm_panel_demo.sh build --execute` exit 0: `bhp-panel-ui` needs libc, libm, libdrm, libpng16, libz, libcrypto 3 and glibc up to 2.38.
- [x] **Step 2: Demo-only 180° rotation.** (2026-10-18)
  - `panel_ui/display_rotation.hpp` (`@canonical panel_display_rotation`)
    rotates the raster sent to the panel and maps panel touch points back.
    Rendering, hit testing and screenshots stay in logical coordinates.
  - `--display-rotation 0|180` is required by the `--simulation-drm` branch
    only, with no default. The VM guest passes `0`. The fail-dark entry point
    does not change.
  - Verify:
    - `ctest --test-dir .build/host-dev -R panel_ui_display_rotation_test` exit 0;
    - `ctest --test-dir .build/host-dev -R panel_display_rotation_simulation` exit 0;
    - `make verify-panel-goldens` exit 0.
- [x] **Step 3: A board runner that leaves nothing behind.** (2026-10-18)
  - `scripts/deploy/frdm_panel_demo.sh` adds `start 0|180`, `status`,
    `presence 0|1`, `scenario NAME`, `screenshot NAME` and `stop`. Each is a dry
    run without `--execute`, and the board commands use
    `BHP_BOARD_SSH_TARGET`. `start` sends the bundle over ssh into a new
    `/tmp/bhp-demo/`, checks SHA256SUMS there, and runs
    `scripts/deploy/frdm_panel_demo_board.py start`. The board script:
    - finds the touch device by name, and the panel connector with the VM
      guest's rule;
    - writes the supervisor env (demo values, suspend disabled) under
      `/tmp/bhp-demo/state`;
    - stops weston;
    - starts the fake PIR, the supervisor, and then the UI as UID/GID 910 with
      the `video` group. Each runs as a transient systemd unit (`/run`), with
      its output in `/tmp/bhp-demo/logs`. The journal is volatile on this
      image (`/run/log/journal`).
  - `stop` fetches logs and screenshots into
    `hardware_tests/results/frdm/panel-demo-<UTC>/`. It stops the units, sets
    the backlight to 255, starts weston again, and removes `/tmp/bhp-demo` and
    `/run/bhp-sim`.
  - Verify:
    - `python3 -m unittest discover -s scripts/deploy/tests -p test_*.py` exit 0;
    - `shellcheck scripts/deploy/frdm_panel_demo.sh scripts/build/frdm_vendor_sysroot.sh` exit 0.
- [ ] **Step 4: Run the demo with the owner.**
  - The owner powers on; the agent boots the panel DTB from the console (no
    `saveenv`), then runs `start`.
  - The owner checks, in order:
    1. Home is upright and fills the panel;
    2. a tap opens the vocabulary panel;
    3. cards respond to taps;
    4. after `presence 0`, the supervisor dims and then blanks the panel after
       the demo grace period;
    5. after `presence 1`, it lights again and the UI redraws.
  - Then `stop`, and the owner confirms the weston desktop is back.
  - Verify:
    - the owner's yes or no for each check;
    - the run record and screenshots under
      `hardware_tests/results/frdm/panel-demo-<UTC>/`;
    - `ls /tmp/bhp-demo` fails after `stop`.

## Risks

- **A DRM page-flip timeout on `imx-drm`.** `DrmPageFlipDisplay` treats a
  500 ms miss as permanent. If it trips, record it; do not raise the timeout
  inside the demo.
- **glibc or library mismatch.** Step 1's `readelf` checks catch it on the
  host; a static `libdrm`/`libpng` build is the fallback.
- **Touch scale.** The supervisor normalizes 0–799 × 0–479 to the panel, but
  nobody has checked the edges. Step 4's taps on edge widgets show whether it
  is right.
- **The supervisor writes the real backlight** (`2-0045`). `stop` leaves it at
  255.

## After the demo

The demo uses demo values everywhere the product needs measured ones. The
real path stays the system image (ARRIVAL-15 to -20), the speech recognizer
choice and the PIR gate.
