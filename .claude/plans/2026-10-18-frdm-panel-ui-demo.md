# Plan: Demo the Panel UI on the FRDM Board

<!-- @canonical frdm-panel-ui-demo-plan -->

**Status (2026-10-18):** proposed; nothing below is built. Requested by the
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

## Decisions for the owner

1. **Stop weston during the demo.** The DRM backend needs DRM master. The demo
   stops weston and restarts it afterwards: a service stop, not a storage write
   or a reboot.
2. **Upside-down panel.** The weston rotation does not apply to a DRM client,
   and the hardware has no rotation. Either Step 2 adds a demo-only 180°
   option, or the owner turns the panel round for the demo and Step 2 is
   skipped. Recommended: Step 2. It is small, and it keeps the test setup as
   it is.

## Steps

- [ ] **Step 0: Restore ssh (owner).**
  - The owner moves the agent's host grant to `192.168.50.15:22`.
  - Verify: `ssh root@192.168.50.15 true` succeeds from the agent account.
- [ ] **Step 1: Cross-build for the vendor image.**
  - Add `scripts/build/frdm_vendor_sysroot.sh`. It is a dry run by default;
    with `--execute` it downloads pinned Debian trixie arm64 `.deb`s by SHA-256
    (`libc6-dev`, `libgcc-14-dev`, `libstdc++-14-dev`, `linux-libc-dev`,
    `libdrm-dev`, `libpng-dev`, `zlib1g-dev` and their runtime packages) and
    unpacks them with `dpkg-deb -x` into `.build/frdm-sysroot/`. It needs no
    root and installs nothing on the PC. Trixie's glibc 2.41 is not newer than
    the board's 2.43.
  - Add `cmake/toolchains/frdm-vendor-aarch64.cmake`: clang
    `--target=aarch64-linux-gnu`, lld, the sysroot above,
    `-static-libstdc++ -static-libgcc`. Shared links stay only to the board's
    `libc`, `libm`, `libdrm`, `libpng16` and `libz`.
  - Configure with `BHP_BUILD_PANEL_DRM`, `BHP_BUILD_LVGL_RENDERER` and
    `BHP_BUILD_PANEL_RENDERER` on, `BHP_BUILD_HTTP_LEXINET_CLIENT` off and tests
    off. Build `bhp-panel-ui`, the supervisor executable and `pir_uinput` into
    `.build/frdm-demo/`, then pack them into one tarball with the scenarios,
    `panel_ui/config/applications.json` and a SHA256SUMS file.
  - Verify:
    - `make verify` still passes;
    - `readelf -d` of each binary lists only `NEEDED` libraries from the board
      list above, and `readelf -V` requires no `GLIBC_` version above 2.41.
- [ ] **Step 2: Demo-only 180° rotation (skip if the owner turns the panel).**
  - Add `--display-rotation 0|180` to the `--simulation-drm` branch only. It is
    required, with no default, like that branch's other options.
  - Rotate the raster in software (LVGL display rotation) and map normalized
    touch through the same rotation before hit testing. The ordinary
    fail-dark entry point does not change.
  - Verify:
    - a host test renders a scenario at 180° and compares it with the 0°
      golden rotated by 180°;
    - a host test sends a touch at a widget's rotated position and checks
      that the widget receives it;
    - `make verify-panel-goldens` passes unchanged.
- [ ] **Step 3: A board runner that leaves nothing behind.**
  - Add `scripts/deploy/frdm_panel_demo.sh` with `start`, `status`, `ctl` and
    `stop`. It is a dry run by default. With `--execute`, over ssh, `start`:
    - copies the tarball to `/tmp/bhp-demo/` and checks SHA256SUMS on the
      board;
    - finds the panel connector from `/sys/class/drm` (the guest's
      `drm_device()` rule: one connected connector with 1024x600);
    - writes the supervisor env under `/tmp/bhp-demo/`. Its brightness, grace
      and presentation values are demo values, labelled as such. The touch
      bounds come from `evtest`'s reported axis ranges, and suspend is
      disabled;
    - stops weston;
    - starts `pir_uinput` and the supervisor as root;
    - starts `bhp-panel-ui --simulation-drm` with `setpriv` as UID/GID 910,
      with no passwd entry. `/dev/dri/card1` gets a devtmpfs ACL for 910, not
      a storage change.
  - `ctl` passes scenario selection, screenshots, and fake-PIR presence or
    absence through the guest's fixture control path.
  - `stop` reverses every step: stops the processes, restores
    `/dev/dri/card1`'s owner and mode, removes `/tmp/bhp-demo/`, and starts
    weston. A reboot also clears everything, since it is all in tmpfs and
    `/run`.
  - Logs and screenshots are fetched to
    `hardware_tests/results/frdm/panel-demo-<UTC>/`.
  - Verify:
    - a host test of the dry-run plan lists every board command and shows
      none writes outside `/tmp`, `/run` or `/dev`;
    - `shellcheck` passes.
- [ ] **Step 4: Run the demo with the owner.**
  - The owner powers on; the agent boots the panel DTB from the console (no
    `saveenv`), then runs `start`.
  - The owner checks, in order:
    1. Home is upright and fills the panel;
    2. a tap opens the vocabulary panel;
    3. cards respond to taps;
    4. after `ctl absence`, the supervisor dims and then blanks the panel after
       the demo grace period;
    5. after `ctl presence`, it lights again and the UI redraws.
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
