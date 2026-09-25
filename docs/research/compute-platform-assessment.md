# Compute Platform Assessment

Research snapshot: 2026-08-05.

**Status:** current recommendation and selection framework. The development
platform is the FRDM i.MX 8M Plus board
([ADR 0008](../decisions/0008-adopt-frdm-imx8mplus-and-waveshare-7-inch-dsi-development-platform.md));
no final product platform has been selected.

This document owns the dated board research, primary-source evidence, candidate
matrix, and procurement gates for a future cordless product. The proposed
cross-platform power design lives in
[power-domain architecture](../architecture/power-domain-architecture.md).
~~The current BeagleBone Black remains the already-owned characterization target
for the kernel module, GPIO wake, cape, and power experiments.~~ The Black was
lost; the FRDM i.MX 8M Plus development board is now the characterization
target (see the 2026-09-24 amendment below).

## Recommendation

1. Do not buy a BeagleBone AI-64 for the cordless direction. It is a compute
   upgrade, but it does not solve onboard wireless or documented low-power wake
   and requires a 5 V source rated for more than 3 A.
2. Use an NXP i.MX 8M Plus system-on-module as the practical product baseline
   for comparison. It has credible suspend evidence and enough CPU, NPU, ISP,
   audio, camera, and display capability for local short-utterance STT plus
   lightweight gesture inference.
3. Use BeagleY-AI as the open-hardware AI challenger. It preserves more of the
   BeagleBoard design ethos, provides 4 TOPS and onboard Wi-Fi, but needs
   board-level suspend/current and GPIO-wake proof.
4. Prefer BeaglePlay when battery life and open hardware matter more than local
   AI. It has the best public BeagleBoard PIR-to-suspend example but no
   dedicated NPU and only 2 GB RAM.
5. Keep Qualcomm QCS6490/RUBIK Pi 3 as a gated high-performance challenger. Its
   vendor claims excellent sleep current and 12 TOPS, but the public claim does
   not define the complete sleep configuration or PIR/GPIO wake matrix.
6. Characterize the owned BeagleBone Black before purchasing a replacement.
   Its results establish the real display, sensor, sleep, wake, network, and
   energy requirements against which every candidate will be tested.

This is a recommendation to investigate, not an authorization to purchase.
The accepted direction was recorded in
[ADR 0003](../decisions/0003-characterize-beaglebone-before-selecting-product-platform.md);
[ADR 0008](../decisions/0008-adopt-frdm-imx8mplus-and-waveshare-7-inch-dsi-development-platform.md)
supersedes its characterize-before-purchase decision (item 6).

**Amended 2026-09-24:** the BeagleBone Black was lost, so item 6 can no longer
be executed. The FRDM i.MX 8M Plus evaluation board and a Waveshare 7inch DSI
LCD (C) were purchased as the development platform. See
[ADR 0008](../decisions/0008-adopt-frdm-imx8mplus-and-waveshare-7-inch-dsi-development-platform.md)
for the part list, procurement snapshot, and new hardware gates. The candidate
matrix, openness axes, and observation contract below are unchanged.

## Requirements that drive the choice

The eventual platform must support:

- a protected rechargeable battery and safe charge-while-operating path;
- Wi-Fi plus reliable Tailscale/Lexinet recovery after wake;
- a wake-capable motion input;
- a touchscreen vocabulary UI;
- a microphone, with an optional speaker path;
- local short-utterance speech-to-text as a desired capability;
- lightweight gesture recognition as a desired capability;
- a small, appliance-oriented Linux image;
- hardware and software openness where it does not foreclose power, media, or
  inference requirements;
- a product enclosure with predictable connectors, thermals, radio clearance,
  serviceability, and battery access.

The required interval between charges, occupied screen-on time, acceptable
wake latency, and listening policy are not yet specified. Those are platform
selection inputs, not enclosure-stage details.

## Candidate matrix

Values below are vendor claims or measurements for named configurations, not
interchangeable whole-product measurements. “No published evidence found” is
not proof that a mode is impossible; it means the project would own the proof.

| Candidate | Connectivity and media | Local inference | Published low-power evidence | Openness | Current judgment |
|---|---|---|---|---|---|
| BeagleBone Black | External Wi-Fi, eMMC, existing 4D cape | Not suitable for modern local STT/vision | TI documents AM335x DeepSleep0 and GPIO0 wake; this assembly remains unmeasured | Open board design and mature upstream support | Owned characterization and kernel-proof platform, not assumed product choice |
| BeagleBone AI-64 | Wi-Fi requires M.2 E-key; eMMC, DSI, dual CSI, audio serial ports | 8 TOPS plus vision accelerators | No board-specific PIR/suspend recipe or measured whole-board suspend result found | Open board design; accelerator stack remains specialized | Compute upgrade, not cordless upgrade; reject as default |
| BeaglePlay | Onboard dual-band Wi-Fi, eMMC, camera, OLDI display | Quad A53; no dedicated NPU advertised | Official PIR/button GPIO suspend-to-RAM camera demo | Strong open-hardware orientation | Open, battery-oriented option if AI is modest or remote |
| BeagleY-AI | Onboard Wi-Fi 6/BLE, two exposed CSI paths, DSI/OLDI/HDMI; microSD by default | 4 TOPS | Configurable PMIC and wake domain, but no board-level dormant-current or PIR recipe found | Strongest open-hardware AI candidate | Measure as open challenger |
| i.MX 8M Plus SOM | Variant-dependent Wi-Fi/eMMC; rich PDM/I2S, CSI, DSI/LVDS/HDMI | 2.3 TOPS NPU, dual ISP, HiFi4 DSP, four A53 cores | Verdin modules publish roughly 0.10–0.18 W suspend-to-RAM in named configurations | Proprietary silicon/SOM; open Linux/Yocto work and published carrier resources | Best-balanced baseline |
| QCS6490 / RUBIK Pi 3 | Wi-Fi, UFS, dual CSI, display, I2S/headset-class media | 12 TOPS and strong CPU/GPU/DSP | Vendor claims about 5 mA at 4.2 V in sleep; test definition and wake matrix unclear | Proprietary hardware; open upstream Linux base plus optional value-add overlays | High-upside gated prototype |
| Raspberry Pi CM5 | Optional Wi-Fi/eMMC, mature CSI/DSI/I2S ecosystem | No integrated NPU; add-on accelerator required | Strong cold-off controls; retained Linux suspend is not its leading path | Proprietary board/SoC design with broad Linux community | Ecosystem fallback if cold boot is acceptable |

## Candidate evidence

### BeagleBone Black

The Black is already owned and directly exercises the GPIO/input/wakeup,
display, and power-policy boundaries. TI's current AM335x SDK documentation
maps `mem` to DeepSleep0 and documents UART, GPIO0, and touchscreen wake for
the named configuration. It also requires WKUP-M3 firmware and kernel support.
RTC-only modes in the same guide are AM437x-specific and must not be projected
onto AM335x.
[TI AM335x suspend/resume](https://software-dl.ti.com/processor-sdk-linux/esd/AM335X/11_02_05_02/exports/docs/linux/Foundational_Components/Power_Management/pm_suspend_resume.html).

The Black lacks onboard Wi-Fi and modern inference acceleration, so a
successful wake proof does not automatically select it for the cordless
product. It supplies valuable measurements at zero new board cost.

### BeagleBone AI-64

BeagleBoard documents 4 GB LPDDR4, 16 GB eMMC, an 8-TOPS accelerator, dual
CSI, DSI, and compatibility-oriented BeagleBone headers. It documents an M.2
E-key connector for adding Wi-Fi rather than onboard Wi-Fi.
[BeagleBone AI-64 product page](https://www.beagleboard.org/boards/beaglebone-ai-64).

The board documentation requires a 5 V source capable of more than 3 A and
warns that attached capes or USB devices can require more. That is a source
capacity requirement rather than proof of continuous 15 W consumption, but it
is an unfavorable starting point for a small cordless enclosure. The same
documentation does not provide the product-level suspend/wake evidence this
project requires. [AI-64 design and power documentation](https://docs.beagleboard.org/boards/beaglebone/ai-64/03-design-and-specifications.html).

Its cape headers do not prove that the existing 4D overlay, framebuffer, touch
controller, backlight path, and suspend behavior work unchanged.

### BeaglePlay

BeaglePlay combines an AM625 quad Cortex-A53, 2 GB RAM, 16 GB eMMC, dual-band
Wi-Fi, camera, and OLDI display connectivity in an open board design.
[BeaglePlay product page](https://www.beagleboard.org/boards/beagleplay).

Its differentiator is evidence rather than nominal specifications: BeagleBoard
publishes a camera application that configures an external PIR or button as a
GPIO wake source, enters suspend-to-RAM, and resumes streaming. The guide also
says the working path depends on the appropriate U-Boot, firmware, and TI Linux
kernel because required patches were not upstream at publication time.
[BeaglePlay low-power video-doorbell demonstration](https://docs.beagleboard.org/boards/beagleplay/demos-and-tutorials/low-power-video-doorbell.html).

BeaglePlay is compelling if speech and vision can be offloaded over the
tailnet. It is less convincing for simultaneous local STT and gesture vision
because the advertised feature set has no NPU or ISP and the board has 2 GB
RAM. That is an inference to test rather than a claim of impossibility.

### BeagleY-AI

BeagleY-AI supplies four Cortex-A53 cores, 4 GB RAM, a 4-TOPS accelerator,
Wi-Fi 6/BLE, camera and display interfaces, and an open, community-oriented
hardware design. [BeagleY-AI product page](https://www.beagleboard.org/boards/beagley-ai).

The board normally boots from microSD, requires a stable 5 V / 3 A source, and
documents configurable PMIC rails and a wakeup clock domain. Its design guide
also says dynamic voltage switching is not currently supported. The missing
evidence is a named BSP/kernel, measured board suspend current, supported GPIO
wake route, and repeated PIR wake result.
[BeagleY-AI design and power documentation](https://docs.beagleboard.org/boards/beagley/ai/03-design.html).

This is the preferred board to challenge the i.MX baseline when publishing and
modifying the board design is valued more highly than BSP maturity and eMMC.

### NXP i.MX 8M Plus

The i.MX 8M Plus provides two or four Cortex-A53 cores, a Cortex-M7, HiFi4
audio DSP, a 2.3-TOPS NPU, two image-signal processors, two CSI inputs, and rich
audio/display interfaces. [NXP i.MX 8M Plus product page](https://www.nxp.com/products/i.MX8MPLUS).

NXP's FRDM i.MX 8M Plus development board is a complete evaluation target:
4 GB RAM, 32 GB eMMC, Wi-Fi 6/Bluetooth/802.15.4, MIPI-DSI, LVDS, HDMI, dual
MIPI-CSI, an audio DAC, and microphone/headphone connections. It is an
evaluation board, not the intended packaged form; a Verdin, DART, or similar
SOM and purpose-built carrier would follow a successful prototype.
[FRDM-iMX8MPLUS](https://www.nxp.com/design/design-center/development-boards-and-designs/FRDM-IMX8MPLUS).

Toradex publishes approximately 0.10–0.18 W suspend-to-RAM and approximately
1.4–1.6 W headless idle for named Verdin i.MX 8M Plus module/BSP
configurations. Carrier, radio, sensor, display, and regulator power are not
included automatically, so these values establish feasibility rather than the
product budget. [Verdin i.MX 8M Plus power measurements](https://developer.toradex.com/hardware/hardware-resources/power-consumption/verdin-imx8m-plus-power-consumption/).

NXP also publishes local streaming STT results. On four Cortex-A53 cores,
Whisper Tiny and Base report about 0.99 s and 1.74 s respectively for full
transcription following nine seconds of speech; Whisper Small reports about
5.51 s. The measurement excludes voice-activity-detection overhead. This is
credible evidence for local short-utterance STT, not proof of performance in
the target room or while vision runs concurrently.
[NXP speech-to-text models and measurements](https://www.nxp.com/design/design-center/software/embedded-software/speech-to-text:STT).

Because the published STT path uses the A53 cores, it may leave the NPU
available for a lightweight gesture model. Concurrent thermal, latency, and
power behavior remains an experiment.

### Qualcomm QCS6490 and RUBIK Pi 3

RUBIK Pi 3 supplies eight CPU cores, 8 GB RAM, 128 GB UFS, onboard Wi-Fi, two
CSI connectors, display and audio expansion, and 12 TOPS of advertised AI
performance. [RUBIK Pi 3 product page](https://www.thundercomm.com/product/rubik-pi/).

The vendor FAQ claims approximately 8 W with CPU/GPU/DSP fully loaded, about
4 W for CPU-only full load, and about 5 mA at 4.2 V in sleep. The last value is
approximately 21 mW, but the page does not define which peripherals are
attached or powered, whether RAM is retained, or which GPIOs can wake the
board. Do not select it until Thundercomm answers those questions and the
claim is reproduced at the battery boundary.
[Thundercomm RUBIK Pi 3 power FAQ](https://www.thundercomm.com/product/rubik-pi/).

Qualcomm Linux 2.0 supplies a Linux 6.18/Yocto 6.0 upstream-oriented base,
public development, and a fully open-source foundation. Qualcomm separately
offers capability overlays for optimized audio, camera, graphics, video,
sensors, and vision; a full-performance product may therefore combine open and
proprietary packages. [Qualcomm Linux 2.0 architecture](https://www.qualcomm.com/developer/blog/2026/06/qualcomm-linux-2-now-available).

The platform is technically attractive enough to remain in the bake-off, but
its hardware and media path are less open than BeagleBoard hardware and its
power claim is currently less reproducible than the i.MX SOM evidence.

### Secondary candidates

- NXP i.MX 93 is a battery-first alternative if short STT and very lightweight
  vision prove sufficient. NXP publishes local STT results for it, but two A55
  cores, 2 GB-class boards, and a 0.5-TOPS accelerator give less concurrency
  margin. [NXP i.MX 93](https://www.nxp.com/products/processors-and-microcontrollers/arm-processors/i-mx-applications-processors/i-mx-9-processors/i-mx-93-applications-processor-family-arm-cortex-a55-ml-acceleration-power-efficient-mpu%3Ai.MX93).
- Raspberry Pi Compute Module 5 offers optional Wi-Fi/eMMC and a mature media
  ecosystem, but no integrated NPU. It is most attractive if an external
  accelerator and MCU-controlled cold boot are acceptable.
  [Compute Module 5](https://www.raspberrypi.com/products/compute-module-5/).
- Jetson Orin Nano is a strong CUDA/TensorRT development platform but its
  documented 7–25 W power modes are poorly aligned with this battery target.
  [Jetson Orin Nano developer-kit documentation](https://docs.nvidia.com/jetson/orin-nano-devkit/user-guide/latest/).
- Newer i.MX 95 designs remain a watchlist option. They offer more compute and
  explicit heterogeneous low-power capability, but their product/BSP ecosystem
  is younger than i.MX 8M Plus and should not be selected merely for a larger
  accelerator number. [NXP i.MX 95](https://www.nxp.com/products/i.MX95).

## Openness is more than one axis

Evaluate openness separately for:

- PCB schematics, layout, and bill of materials;
- boot firmware and secure-boot ownership;
- upstream kernel support;
- GPU/NPU/ISP firmware and user-space runtimes;
- reproducible image construction;
- model-conversion and inference tooling;
- radio firmware and regulatory certification;
- replacement-module availability and lifecycle.

BeaglePlay and BeagleY-AI lead on board-design openness. That does not make
their accelerated inference path completely open. NXP modules are proprietary
hardware but have strong Yocto/Linux and carrier-board ecosystems. Qualcomm's
current Linux foundation is substantially more open than older embedded
Qualcomm stacks, but hardware-optimized capability overlays remain a separate
choice.

Open hardware is a preference in this project, not permission to accept an
unmeasured or materially worse battery, media, or maintenance outcome.

## Distribution and kernel implications

Use each board vendor's supported kernel, firmware, and Yocto/Debian image first
when proving suspend, camera, audio, display, and accelerators. A custom
upstream kernel or compact image is not a useful success if it omits the power
or inference path being evaluated.

- Keep the current Buildroot work as the BeagleBone appliance candidate.
- Use NXP/Toradex/Variscite-supported Yocto for initial i.MX experiments.
- Use TI-supported Debian/Yocto for BeaglePlay or BeagleY-AI power/media proof.
- Use Qualcomm Linux's reproducible Yocto base for a QCS6490 experiment.
- Trim the winning image only after power/media/inference paths work and their
  required firmware and licenses are documented.

The stable application and policy seams should survive a distribution change;
the kernel, device tree, display, media, inference, and power backends will not.

## Common bake-off contract

Use the same PIR, display class, microphone, camera, battery/power analyzer, and
application workload where electrically possible. Measure at the battery
boundary rather than relying on software rail counters.

### Observation contract

Every trial receives one correlation ID joining:

- externally observed stimulus ground truth;
- board-input and product-battery power trace hashes;
- a shared GPIO timing marker captured with the PIR, PM state, backlight or
  photodiode, and analyzer trace;
- confirmed state-entry evidence and suspend-stat delta;
- wake IRQ/reason and GPIO interrupt delta;
- Linux input event, first correct frame, backlight enable, Wi-Fi, tailnet, and
  Lexinet milestones;
- exact hardware, controller/SOM configuration, kernel/BSP/firmware, and test
  environment.

Classify suspend trials as `entered_and_attributed`, `aborted`, `wrong_source`,
`timeout`, or `unobserved`. Only `entered_and_attributed` trials count as wake
success. A cumulative Linux `wakeup_sources` counter is supporting evidence,
not the oracle.

Record two named power boundaries rather than mixing them:

- `board_input` for board diagnosis, with supply voltage and peripheral set;
- `product_battery` for candidate comparison, including charger, conversion,
  controller, sensor, and rail-switch losses.

The raw measurement record includes trace path and hash, sample rate, settling
and analysis windows, min/mean/max current and voltage, integrated joules,
instrument model/serial/calibration, ambient temperature, battery state of
charge/temperature, charging state, converter topology, and complete
configuration manifest.

### Stable states

- shipping/off and controller-only;
- controller plus PIR;
- each advertised Linux sleep state;
- awake headless idle with Wi-Fi associated;
- display rail off and backlight zero;
- display at three calibrated brightness levels;
- microphone idle, VAD, and STT;
- camera startup and gesture inference;
- simultaneous STT and gesture inference.

### Transitions and UX

- sensor edge to CPU execution;
- sensor edge to first correct local frame;
- sensor edge to backlight enable;
- sensor edge to Wi-Fi association;
- sensor edge to tailnet peer and Lexinet success;
- suspend/resume and cold-boot/shutdown energy;
- model-load latency and energy;
- safe shutdown and boot refusal at low battery;
- shared hardware-marker correlation between PIR, CPU, frame, backlight, and
  power traces.

### Reliability

- 100 characterization cycles for every explicitly enumerated candidate
  state, starting level, wake edge, and supported implementation/BSP cell;
- 1,000-cycle final suspend/wake soak;
- PIR edges immediately before, during, and after suspend;
- asserted, stuck-high, stuck-low, and chattering sensor behavior;
- unavailable/rebooting access point, DHCP renewal, and tailnet outage;
- repeated cold boots across battery state and charger insertion;
- a 24–72-hour masked-PIR/no-command soak to measure spontaneous wakes;
- low-state-of-charge voltage sweeps and load steps during simultaneous display,
  Wi-Fi, STT, and camera use, including charger removal and handover;
- 72-hour time-drift test;
- forced-power-loss and filesystem recovery on sacrificial media only;
- false wakes, missed wakes, unexplained resets, thermal throttling, and fan
  requirements.

Use independent state entries, fixed settling/measurement windows, blocked or
randomized state order, repeats across cold boots and thermal conditions, and a
declared sample population. Report median and p95 for characterization; reserve
p99 for the 1,000-cycle population and include counts/confidence bounds. Also
report transition joules; stable-state min/mean/max power; false wakes per hour;
missed-wake and attribution counts; STT real-time factor plus word-error or
command-success rate on a fixed room-noise corpus; and gesture confusion matrix,
false-activation rate, accuracy, and fps on a labeled corpus.

## Candidate-specific gates

### i.MX 8M Plus baseline

- Select a SOM/board variant that actually contains the NPU, audio DSP, Wi-Fi,
  eMMC, and required interfaces; similarly named variants can omit features.
- Prove that the chosen Wi-Fi driver permits suspend or can be cleanly
  quiesced.
- Reproduce suspend power on the exact carrier.
- Run NXP's STT sample and a lightweight gesture model concurrently.
- Verify GPIO/PIR resume and RTC wake under the selected BSP.

### BeagleY-AI challenger

- Measure whole-board suspend and powered-off current.
- Prove that a header GPIO reaches the documented wake domain.
- Establish robust storage using high-endurance microSD or NVMe before power
  interruption tests.
- Verify usable audio-input hardware and inference-runtime packaging.
- Compare TI's accelerator workflow and required binary components with the
  project's openness target.

### QCS6490 gate

Obtain written answers or reproducible documentation for:

- the exact state represented by 5 mA at 4.2 V;
- whether that is SOM or complete RUBIK Pi board current;
- which DDR, PMIC, radio, storage, and peripheral rails remain powered;
- GPIO/PIR and RTC wake support in the current Qualcomm Linux release;
- expected resume latency and exposed wake reason;
- whether Wi-Fi remains associated or reconnects;
- which camera, audio, vision, and NPU paths require value-add overlays.

## Immediate BeagleBone path

The Black remains valuable because it is already owned and directly tests the
hard parts every later platform still has:

1. Preserve the installed eMMC before mutating it.
2. Inventory the exact board, cape, PIR, wiring, GPIO ownership, and voltage.
3. Characterize display, touch, backlight, and blanking.
4. Run stock `gpio-keys` and custom-module wake experiments.
5. Prove the exact requested AM335x sleep state and wake reason.
6. Measure battery-boundary power, wake-to-frame, and network recovery.
7. Use those results to set quantitative gates for the new-board bake-off.

The executable checklist is
[the BeagleBone hardware baseline plan](../../.claude/plans/2026-08-05-beaglebone-hardware-baseline.md).

## Inputs needed before final selection

- Required charge interval: daily, weekly, or multi-week.
- Expected occupied/display-on hours per day.
- Acceptable wake-to-visible-frame latency after short and long vacancy.
- Whether voice is push-to-talk, presence-gated, wake-word-gated, or continuous.
- Whether the existing 4D cape must be reused in the final enclosure.
- Whether raw audio/video may ever leave the device.
- Budget for candidate development kits and a power analyzer.

Until these are answered and measurements exist, i.MX 8M Plus is a baseline
recommendation—not a procurement decision—and BeagleBone Black is an
experiment platform—not an accidental permanent constraint.
