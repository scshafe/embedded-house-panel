# GUI strategy for the BeagleBone house panel

Research date: 2026-08-05

Source policy: technical claims below use upstream project documentation,
official vendor documentation, or tagged/pinned upstream source repositories.

Target under investigation: BeagleBone Black (AM335x, 512 MiB RAM) with an
unconfirmed 4D Systems 480x272 LCD/touch cape. This document is a research
decision, not a record of measurements from the target.

## Decision

Start with **LVGL 9.5.0, its software renderer, direct DRM/KMS dumb buffers, and
the LVGL evdev input driver**. Pin LVGL in the project's Buildroot external tree
and initially disable GBM, EGL, and OpenGL. Keep the display device, connector,
input event device, axis transform, and calibration explicit in validated
configuration. A missing device or unsupported mode must be a visible startup
error, not a silent choice of a different backend.

Use these fallbacks in order:

1. Use the same LVGL view with its fbdev display driver if the actual cape
   exposes a usable `/dev/fb0` but no usable KMS connector.
2. If LVGL proves materially inadequate for text/layout or development speed,
   compare **Qt 6 Widgets** using the LinuxFB platform plugin in software mode.
   Build qt6base with libdrm, prefer `QT_QPA_FB_DRM=1` for DRM
   dumb-buffer/page-flip output, and retain plain fbdev as the transport
   fallback.

Do not make SDL2's packaged `kmsdrm` backend the initial route. In current
Buildroot it is gated on GBM, EGL, and OpenGL/OpenGL ES, so it does not remove
the main unknown on this board: a working accelerated userspace graphics stack.
A custom libdrm dumb-buffer presenter combined with SDL software surfaces is
possible, but it also means owning modesetting, presentation, touch mapping,
and most widget behavior. That is a useful diagnostic or later optimization,
not the shortest path to a reliable appliance UI.

This recommendation is an engineering inference from the verified facts below.
It remains provisional until the target experiments pass.

## Verified facts

### Hardware and current Buildroot baseline

- BeagleBoard documents the BeagleBone Black as an AM3358 Cortex-A8 board with
  512 MiB DDR3L; its LCD signals are shared with the cape headers. The same
  documentation lists an SGX530 graphics block, but that hardware fact alone
  does not establish a usable kernel/EGL stack in our image.
  [BeagleBone Black system reference](https://docs.beagleboard.org/boards/beaglebone/black/ch04.html)
  and [connector/LCD details](https://docs.beagleboard.org/boards/beaglebone/black/ch05.html).
- The 4D Systems 4DCAPE-43T datasheet identifies a 4.3-inch, 480x272 resistive
  touch display with cape EEPROM. It says the cape uses drivers developed for
  existing LCD capes and that 4D Systems does not supply the driver. Therefore
  the datasheet establishes electrical/display intent, not compatibility with
  the kernel and device tree selected for this project.
  [4DCAPE-43 series datasheet](https://resources.4dsystems.com.au/datasheets/cape/4DCAPE-43-series/4DCAPE-43_Series_Datasheet_R_1_7.pdf).
- The inspected upstream Buildroot snapshot is commit
  [`3c323d714afbc772050fbf665d67700f9a869d79`](https://gitlab.com/buildroot.org/buildroot/-/commit/3c323d714afbc772050fbf665d67700f9a869d79)
  from 2026-08-04. Its generic
  [`beaglebone_defconfig`](https://gitlab.com/buildroot.org/buildroot/-/blob/3c323d714afbc772050fbf665d67700f9a869d79/configs/beaglebone_defconfig)
  selects Cortex-A8, Linux 6.18.38, and the standard BeagleBone device trees; it
  does not explicitly select this cape overlay or a GUI stack.
- Buildroot also carries a separate
  [`beaglebone_qt5_defconfig`](https://gitlab.com/buildroot.org/buildroot/-/blob/3c323d714afbc772050fbf665d67700f9a869d79/configs/beaglebone_qt5_defconfig),
  but it pins a 4.19 TI kernel and TI SGX userspace. It is useful historical
  evidence, not an appropriate current production baseline.
- The current Buildroot tree has packages for libdrm, `drm-info`, libdrm test
  programs (including `modetest`), `evtest`, and framebuffer test programs.
  These support a small discovery image before any toolkit is chosen.
  [libdrm](https://gitlab.com/buildroot.org/buildroot/-/blob/3c323d714afbc772050fbf665d67700f9a869d79/package/libdrm/Config.in),
  [drm-info](https://gitlab.com/buildroot.org/buildroot/-/blob/3c323d714afbc772050fbf665d67700f9a869d79/package/drm-info/Config.in),
  [evtest](https://gitlab.com/buildroot.org/buildroot/-/blob/3c323d714afbc772050fbf665d67700f9a869d79/package/evtest/Config.in),
  and [fb-test-app](https://gitlab.com/buildroot.org/buildroot/-/blob/3c323d714afbc772050fbf665d67700f9a869d79/package/fb-test-app/Config.in).

### DRM/KMS and scanout memory

- Linux KMS exposes connectors, modes, CRTCs, planes, and page flips. The DRM
  UAPI exposes `DRM_CAP_DUMB_BUFFER` and dumb-buffer allocation/mapping ioctls
  specifically for simple CPU-accessible scanout. Drivers commonly support
  XRGB8888 for dumb buffers; software should still query capabilities and
  formats rather than assume them.
  [Linux DRM/KMS documentation](https://docs.kernel.org/gpu/drm-kms.html) and
  [DRM userspace API](https://docs.kernel.org/gpu/drm-uapi.html).
- At 480x272, one 32-bit unpadded buffer contains 522,240 bytes (about 510 KiB),
  and two contain about 1.0 MiB. One 16-bit unpadded buffer contains 261,120
  bytes (about 255 KiB). These are arithmetic lower bounds, not measured
  allocation size or process RSS; pitch alignment, assets, caches, and toolkit
  state add memory.

### Candidate support and rendering/input paths

| Candidate | Verified Buildroot state at the inspected commit | Verified display and input path | Assessment (inference) |
| --- | --- | --- | --- |
| Direct DRM/KMS + SDL2 | SDL2 2.32.10 and libdrm are packaged. Buildroot's `SDL2_KMSDRM` option requires GBM, EGL, and GL/GLES and selects libdrm. | SDL's KMSDRM implementation uses GBM/EGL and Linux evdev; SDL touch events use normalized coordinates. A different, custom libdrm dumb-buffer presenter could use SDL software surfaces, but that presenter is not the packaged SDL KMSDRM path. | Good for a narrow custom renderer once the device path is known; too much appliance UI and input glue for the first implementation. |
| LVGL 9.5.0 | No `package/lvgl` entry exists in the inspected upstream package tree, so it needs a pinned br2-external package. | LVGL's Linux DRM driver can allocate dumb buffers directly with libdrm when GBM/EGL are disabled. It also has fbdev and evdev drivers; evdev supports an explicit event device, coordinate calibration, and axis swapping. | Best initial balance: GPU-independent, small display stack, widgets, and a clean fbdev transport fallback. Packaging work is the main up-front cost. |
| Qt 6 Widgets | Qt 6.9.1 is packaged. Qt GUI can build with LinuxFB when EGLFS/XCB are absent, and enabling libdrm enables qt6base's KMS feature. | Qt LinuxFB is software-rendered and can use either fbdev or DRM dumb buffers with `QT_QPA_FB_DRM=1`; the latter supports double buffering, page flips, and vsync. Direct-device input can use evdev, libinput, or tslib. | Strong toolkit fallback with first-class Buildroot integration. Measure image size, startup, and RSS against LVGL before accepting the broader stack. Avoid QML/Qt Quick initially. |
| GTK 3/4 | GTK 3.24.51 and GTK 4.21.0 are packaged; current Buildroot configurations require GL or EGL. | GTK's documented Linux window-system backends are Wayland, X11, and Broadway, not a direct DRM appliance backend. Wayland means adding a compositor that owns display/input. | Poor fit for a single-purpose 512 MiB direct-scanout image unless a desktop/compositor ecosystem becomes a requirement. |
| Slint | No `package/slint` entry exists in the inspected upstream package tree. | Slint's LinuxKMS backend supports OpenGL, Vulkan, software DRM dumb buffers, and legacy fbdev. It uses libinput/udev, and the project labels LinuxKMS experimental. | Attractive declarative later spike, but simultaneous Buildroot/toolchain integration and experimental-backend risk is unnecessary for the first target bring-up. |
| Browser kiosk (WPE WebKit + Cog) | Buildroot packages WPE WebKit 2.50.5 and Cog 0.18.5. | Cog's DRM platform needs GLES2, EGL, GBM, libdrm, libinput, udev, and WPEBackend-fdo/Wayland support. WPE's official hardware table lists BeagleBone/SGX530 but shows no validated WPE backend or Cog platform for it. | Reject initially: it depends on the unproven accelerated stack and introduces the largest dependency/update surface. Reconsider only if reusing a web application dominates all other constraints. |

Primary evidence for the table:

- SDL2:
  [Buildroot SDL2 options](https://gitlab.com/buildroot.org/buildroot/-/blob/3c323d714afbc772050fbf665d67700f9a869d79/package/sdl2/Config.in),
  [Buildroot SDL2 build rules](https://gitlab.com/buildroot.org/buildroot/-/blob/3c323d714afbc772050fbf665d67700f9a869d79/package/sdl2/sdl2.mk),
  [SDL KMSDRM source at 2.32.10](https://github.com/libsdl-org/SDL/blob/release-2.32.10/src/video/kmsdrm/SDL_kmsdrmvideo.c),
  [SDL Linux evdev source](https://github.com/libsdl-org/SDL/blob/release-2.32.10/src/core/linux/SDL_evdev.c),
  and [SDL touch event API](https://wiki.libsdl.org/SDL2/SDL_TouchFingerEvent).
- LVGL:
  [LVGL 9.5 introduction](https://docs.lvgl.io/9.5/introduction/index.html),
  [Linux DRM driver guide](https://docs.lvgl.io/9.4/details/integration/embedded_linux/drivers/drm.html),
  [tagged 9.5 DRM implementation](https://github.com/lvgl/lvgl/blob/v9.5.0/src/drivers/display/drm/lv_linux_drm.c),
  [evdev guide](https://docs.lvgl.io/9.4/details/integration/embedded_linux/drivers/evdev.html),
  and [fbdev guide](https://docs.lvgl.io/9.1/integration/driver/display/fbdev.html).
- Qt:
  [Buildroot Qt version and source](https://gitlab.com/buildroot.org/buildroot/-/blob/3c323d714afbc772050fbf665d67700f9a869d79/package/qt6/qt6.mk),
  [qt6base options](https://gitlab.com/buildroot.org/buildroot/-/blob/3c323d714afbc772050fbf665d67700f9a869d79/package/qt6/qt6base/Config.in),
  [qt6base Buildroot feature selection](https://gitlab.com/buildroot.org/buildroot/-/blob/3c323d714afbc772050fbf665d67700f9a869d79/package/qt6/qt6base/qt6base.mk),
  [Qt 6.9 embedded Linux platform plugins](https://doc.qt.io/qt-6.9/embedded-linux.html),
  and [Qt 6.9 direct-device input](https://doc.qt.io/qt-6.9/inputs-linux-device.html).
- GTK:
  [Buildroot GTK3 options](https://gitlab.com/buildroot.org/buildroot/-/blob/3c323d714afbc772050fbf665d67700f9a869d79/package/libgtk3/Config.in),
  [Buildroot GTK3 version](https://gitlab.com/buildroot.org/buildroot/-/blob/3c323d714afbc772050fbf665d67700f9a869d79/package/libgtk3/libgtk3.mk),
  [Buildroot GTK4 options](https://gitlab.com/buildroot.org/buildroot/-/blob/3c323d714afbc772050fbf665d67700f9a869d79/package/libgtk4/Config.in),
  [Buildroot GTK4 version](https://gitlab.com/buildroot.org/buildroot/-/blob/3c323d714afbc772050fbf665d67700f9a869d79/package/libgtk4/libgtk4.mk),
  and [GTK runtime backend documentation](https://docs.gtk.org/gtk4/running.html).
- Slint:
  [LinuxKMS backend](https://docs.slint.dev/latest/docs/slint/guide/backends-and-renderers/backend_linuxkms/)
  and [official source repository](https://github.com/slint-ui/slint).
- WPE/Cog:
  [Buildroot Cog options](https://gitlab.com/buildroot.org/buildroot/-/blob/3c323d714afbc772050fbf665d67700f9a869d79/package/cog/Config.in),
  [Buildroot Cog version](https://gitlab.com/buildroot.org/buildroot/-/blob/3c323d714afbc772050fbf665d67700f9a869d79/package/cog/cog.mk),
  [Buildroot WPE WebKit version](https://gitlab.com/buildroot.org/buildroot/-/blob/3c323d714afbc772050fbf665d67700f9a869d79/package/wpewebkit/wpewebkit.mk),
  [Cog DRM platform requirements](https://igalia.github.io/cog/platform-drm.html),
  and [WPE supported hardware](https://wpewebkit.org/about/supported-hardware.html).

The LVGL and Slint absence statements mean only “no entry in the
[in-tree package catalog at the pinned Buildroot commit](https://gitlab.com/buildroot.org/buildroot/-/tree/3c323d714afbc772050fbf665d67700f9a869d79/package).”
They do not mean either project cannot be built. Buildroot explicitly supports
project-specific packages through a br2-external tree, which is the proposed
LVGL integration route.
[Buildroot br2-external manual](https://buildroot.org/downloads/manual/manual.html#outside-br-custom).

## Engineering inferences

These statements are design judgments to test, not upstream guarantees:

- A software-rendered 480x272 appliance UI should not require the SGX GPU. A
  dumb-buffer path removes a historically fragile kernel/userspace graphics
  dependency while keeping scanout memory small relative to 512 MiB RAM.
- LVGL supplies the controls, invalidation, font/image handling, and input
  dispatch that a raw SDL/libdrm implementation would otherwise have to grow.
- Qt Widgets is more likely than LVGL to reduce work for complex text/layout,
  but is also likely to cost more image space, RSS, and startup time. Only the
  target comparison can determine whether that trade is material.
- GTK's compositor/window-system requirement and WPE/Cog's accelerated stack
  are unnecessary layers for the current single-fullscreen-view product.
- Slint may become the best authoring experience later; it should earn that role
  by beating the selected implementation in a bounded spike, not by making the
  hardware bring-up depend on an experimental backend.

## Unknown cape and kernel behavior

None of the following is verified yet, and none may become a production default
without captured target evidence:

- exact cape model, hardware revision, EEPROM contents, and overlay-loading
  mechanism;
- whether the selected kernel/device tree binds the display at all;
- whether it exposes `/dev/dri/card*`, `/dev/fb*`, both, or neither;
- DRM connector/CRTC/plane topology, preferred mode, supported formats, dumb
  buffer capability, pitch constraints, page-flip behavior, and DRM-master
  permissions;
- whether 480x272 is the preferred mode (LVGL's non-EGL DRM path uses the
  preferred mode) and whether the physical orientation matches it;
- exact `/dev/input/event*` node(s), whether touch is reported as single-touch
  or multitouch, raw axis ranges, swapped/inverted axes, pressure behavior,
  noise, release behavior, and any cape button nodes;
- backlight device/range/polarity and suspend/resume restoration behavior. The
  UI must not own those controls; they remain the supervisor's responsibility;
- steady/peak RAM, CPU use, image-size delta, boot-to-first-frame time, touch
  latency, tearing, and long-run stability for every candidate.

## Target experiment matrix

Store dated command output and measurements under the ignored
`hardware_tests/results/` area. Record the image configuration, Buildroot
commit, kernel build ID, board/cape EEPROM identity, and application revision
with every run. Do not turn a discovery result into a hidden fallback.

| Gate | Image / action | Evidence to capture | Pass or decision rule |
| --- | --- | --- | --- |
| 0. Identify hardware | Boot the current minimal BeagleBone baseline. Read board/cape markings and EEPROM; capture device tree and relevant boot/kernel logs. | Exact revisions, EEPROM strings, overlay state, `/proc/device-tree`, `dmesg`, and full `/dev/dri`, `/dev/fb*`, `/dev/input` inventory. | Stop and resolve the overlay/kernel path if no display device binds. Do not compensate in application code. |
| 1. Prove scanout APIs | Add libdrm tests, `drm-info`, `fb-test-app`, and `evtest`. Run `drm_info` and `modetest -c -p`; test a CPU-written dumb buffer and repeated page flips. Separately test fbdev if present. | Connector/mode/format/capability output, selected node and mode, photos of a geometry/color test, page-flip timestamps, errors, and DRM-master/permission requirements. | Direct DRM remains primary only if 480x272 is stable, dumb buffers work, and 20 cold boots show no corruption. Otherwise test the explicit LVGL fbdev fallback. |
| 2. Characterize touch | Run `evtest` on every candidate event node. Exercise corners, center, straight lines, press/hold/release, rapid taps, and cape buttons. | Event-node identity, EV/ABS capabilities, raw min/max, axis orientation, pressure and release traces, noise, and the calibration transform. | A calibrated nine-point grid must map inside the intended targets, and 200 deliberate taps must cause no false neighboring action. Treat a miss or phantom release as data to diagnose, not as a reason to enlarge targets silently. |
| 3. LVGL vertical slice | Pin LVGL 9.5.0 in br2-external. Build software DRM only, then the same view on fbdev if gate 1 requires it. Render the real 480x272 card layout, fonts/images, rating flow, night-red mode, and diagnostics. | Rootfs/image delta, service-start-to-first-frame, steady and peak `VmRSS`, idle and interaction CPU, frame/page-flip timing, input timestamps, and screenshots/photos. | Required UX and calibrated input must be correct. Provisional budget: first correct frame within 1 s of service start, steady RSS at or below 32 MiB, and idle CPU below 5% of one core. These are project gates, not known hardware limits; ratify or revise them from the first trace. |
| 4. Lifecycle soak | With the supervisor driving policy, run display-off/on and suspend/resume cycles; reopen DRM/input after loss and reacquire required ownership explicitly. Run a 24-hour interaction loop. | 100 display cycles, 20 suspend/resume cycles, 24-hour logs, memory trend, device reopen errors, and photos after resume. | No hang, corruption, stale touch action, unbounded RSS growth, or silent backend change. Failure must be structured and recoverable by the supervisor/service policy. |
| 5. Qt fallback comparison | Only if LVGL fails a gate or its implementation cost is demonstrably high, reproduce the same vertical slice in Qt 6 Widgets + libdrm + LinuxFB software DRM and, if needed, fbdev. | The same timing, RSS, CPU, image delta, touch, lifecycle, and developer-effort observations as gates 3-4. | Select Qt only if it passes correctness/lifecycle gates and its measured authoring benefit justifies the additional target cost. |
| 6. SDL accelerated probe | Only if a packaged SDL KMS route is still desired, first prove GBM/EGL/GLES with an isolated diagnostic image (for example `kmscube`), then run an SDL2 KMSDRM smoke test. | Exact graphics providers, EGL extensions/config, rendered output, RSS/CPU, input behavior, and failures across reboot/resume. | A missing or unstable accelerated stack ends this branch. Do not import the legacy Qt5/SGX defconfig wholesale. |
| 7. Optional Slint/browser spikes | Slint: package a pinned release and repeat the software KMS vertical slice. Browser: only after gate 6, run the local UI in sandboxed WPE/Cog and soak it. | Toolchain/package maintenance, image delta, total process RSS, startup, CPU, touch, crash recovery, security settings, and 24-hour stability. | Adopt only for a product capability that LVGL/Qt cannot economically provide and only if all correctness and lifecycle gates pass. |

The provisional numerical budgets above are deliberately testable, but they are
not claims about the unmeasured cape. Preserve the raw results so a later
decision can tighten or relax them transparently.
